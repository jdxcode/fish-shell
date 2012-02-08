/** \file builtin.c
	Functions for executing builtin functions.

	How to add a new builtin function:

	1). Create a function in builtin.c with the following signature:

	<tt>static int builtin_NAME( wchar_t ** args )</tt>

	where NAME is the name of the builtin, and args is a zero-terminated list of arguments.

	2). Add a line like { L"NAME", &builtin_NAME, N_(L"Bla bla bla") }, to the builtin_data_t variable. The description is used by the completion system. Note that this array is sorted!

	3). Create a file doc_src/NAME.txt, containing the manual for the builtin in Doxygen-format. Check the other builtin manuals for proper syntax.

	4). Use 'darcs add doc_src/NAME.txt' to start tracking changes to the documentation file.

*/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <signal.h>
#include <wctype.h>
#include <sys/time.h>
#include <time.h>

#include "fallback.h"
#include "util.h"

#include "wutil.h"
#include "builtin.h"
#include "function.h"
#include "complete.h"
#include "proc.h"
#include "parser.h"
#include "reader.h"
#include "env.h"
#include "common.h"
#include "wgetopt.h"
#include "sanity.h"
#include "tokenizer.h"
#include "wildcard.h"
#include "input_common.h"
#include "input.h"
#include "intern.h"
#include "event.h"
#include "signal.h"
#include "exec.h"
#include "highlight.h"
#include "halloc.h"
#include "halloc_util.h"
#include "parse_util.h"
#include "parser_keywords.h"
#include "expand.h"
#include "path.h"


/**
   The default prompt for the read command
*/
#define DEFAULT_READ_PROMPT L"set_color green; echo read; set_color normal; echo \"> \""

/**
   The mode name to pass to history and input
*/

#define READ_MODE_NAME L"fish_read"

/**
   The send stuff to foreground message
*/
#define FG_MSG _( L"Send job %d, '%ls' to foreground\n" )

/**
   Datastructure to describe a builtin. 
*/
struct builtin_data_t
{
	/**
	   Name of the builtin
	*/
	const wchar_t *name;
	/**
	   Function pointer tothe builtin implementation
	*/
	int (*func)(parser_t &parser, wchar_t **argv);
	/**
	   Description of what the builtin does
	*/
	const wchar_t *desc;
    
    bool operator<(const wcstring &) const;
    bool operator<(const builtin_data_t *) const;
};

bool builtin_data_t::operator<(const wcstring &other) const
{
    return wcscmp(this->name, other.c_str()) < 0;
}

bool builtin_data_t::operator<(const builtin_data_t *other) const
{
    return wcscmp(this->name, other->name) < 0;
}

int builtin_out_redirect;
int builtin_err_redirect;

/*
  Buffers for storing the output of builtin functions
*/
string_buffer_t *sb_out=0, *sb_err=0;

/**
   Stack containing builtin I/O for recursive builtin calls.
*/
static array_list_t io_stack;

/**
   The file from which builtin functions should attempt to read, use
   instead of stdin.
*/
static int builtin_stdin;

/**
   The underlying IO redirections behind the current builtin. This
   should normally not be used - sb_out and friends are already
   configured to handle everything.
*/
static io_data_t *real_io;

/**
   Counts the number of non null pointers in the specified array
*/
static int builtin_count_args( wchar_t **argv )
{
	int argc = 1;
	while( argv[argc] != 0 )
	{
		argc++;
	}
	return argc;
}

/** 
	This function works like wperror, but it prints its result into
	the sb_err string_buffer_t instead of to stderr. Used by the builtin
	commands.
*/

static void builtin_wperror( const wchar_t *s)
{
	if( s != 0 )
	{
		sb_append( sb_err, s, L": ", NULL );
	}
	char *err = strerror( errno );
	wchar_t *werr = str2wcs( err );
	if( werr )
	{
		sb_append( sb_err,  werr, L"\n", NULL );
		free( werr );
	}
}

/**
   Count the number of times the specified character occurs in the specified string
*/
static int count_char( const wchar_t *str, wchar_t c )
{
	int res = 0;
	for( ; *str; str++ )
	{
		res += (*str==c);
	}
	return res;
}

wcstring builtin_help_get( parser_t &parser, const wchar_t *name )
{
    wcstring_list_t lst;
    wcstring out;
    const wcstring name_esc = escape_string(name, 1);
    const wcstring cmd = format_string(L"__fish_print_help %ls", name_esc.c_str());
	if( exec_subshell( cmd, lst ) >= 0 )
	{	
		for( size_t i=0; i<lst.size(); i++ )
		{
            out.append(lst.at(i));
            out.push_back(L'\n');
		}
	}
	return out;
}

/**
   Print help for the specified builtin. If \c b is sb_err, also print
   the line information

   If \c b is the buffer representing standard error, and the help
   message is about to be printed to an interactive screen, it may be
   shortened to fit the screen.

   
*/

static void builtin_print_help( parser_t &parser, const wchar_t *cmd, string_buffer_t *b )
{
	
	int is_short = 0;
	
	if( b == sb_err )
	{
		sb_append( sb_err,
				   parser.current_line() );
	}

	const wcstring h = builtin_help_get( parser, cmd );

	if( !h.size())
		return;

	wchar_t *str = wcsdup( h.c_str() );
	if( str )
	{

		if( b==sb_err )
		{
			
			/*
			  Interactive mode help to screen - only print synopsis if
			  the rest won't fit  
			*/
			
			int screen_height, lines;

			screen_height = common_get_height();
			lines = count_char( str, L'\n' );
			if( !is_interactive || (lines > 2*screen_height/3) )
			{
				wchar_t *pos;
				int cut=0;
				int i;
				
				is_short = 1;
								
				/*
				  First move down 4 lines
				*/
				
				pos = str;
				for( i=0; (i<4) && pos && *pos; i++ )
				{
					pos = wcschr( pos+1, L'\n' );
				}
				
				if( pos && *pos )
				{
						
					/* 
					   Then find the next empty line 
					*/
					for( ; *pos; pos++ )
					{
						if( *pos == L'\n' )
						{
							wchar_t *pos2;
							int is_empty = 1;
							
							for( pos2 = pos+1; *pos2; pos2++ )
							{
								if( *pos2 == L'\n' )
									break;
								
								if( *pos2 != L'\t' && *pos2 !=L' ' )
								{
									is_empty = 0;
									break;
								}
							}
							if( is_empty )
							{
								/*
								  And cut it
								*/
								*(pos2+1)=L'\0';
								cut = 1;
								break;
							}
						}
					}
				}
				
				/*
				  We did not find a good place to cut message to
				  shorten it - so we make sure we don't print
				  anything.
				*/
				if( !cut )
				{
					*str = 0;
				}

			}
		}
		
		sb_append( b, str );
		if( is_short )
		{
			sb_printf( b, _(L"%ls: Type 'help %ls' for related documentation\n\n"), cmd, cmd );
		}
				
		free( str );
	}
}

/**
   Perform error reporting for encounter with unknown option
*/
static void builtin_unknown_option( parser_t &parser, const wchar_t *cmd, const wchar_t *opt )
{
	sb_printf( sb_err,
			   BUILTIN_ERR_UNKNOWN,
			   cmd,
			   opt );
	builtin_print_help( parser, cmd, sb_err );
}

/**
   Perform error reporting for encounter with missing argument
*/
static void builtin_missing_argument( parser_t &parser, const wchar_t *cmd, const wchar_t *opt )
{
	sb_printf( sb_err,
			   BUILTIN_ERR_MISSING,
			   cmd,
			   opt );
	builtin_print_help( parser, cmd, sb_err );
}

/*
  Here follows the definition of all builtin commands. The function
  names are all on the form builtin_NAME where NAME is the name of the
  builtin. so the function name for the builtin 'fg' is
  'builtin_fg'.

  A few builtins, including 'while', 'command' and 'builtin' are not
  defined here as they are handled directly by the parser. (They are
  not parsed as commands, instead they only alter the parser state)

  The builtins 'break' and 'continue' are so closely related that they
  share the same implementation, namely 'builtin_break_continue.

  Several other builtins, including jobs, ulimit and set are so big
  that they have been given their own file. These files are all named
  'builtin_NAME.c', where NAME is the name of the builtin. These files
  are included directly below.

*/


#include "builtin_set.cpp"
#include "builtin_commandline.cpp"
#include "builtin_complete.cpp"
#include "builtin_ulimit.cpp"
#include "builtin_jobs.cpp"

/**
   List all current key bindings
 */
static void builtin_bind_list()
{
	size_t i;
    wcstring_list_t lst;
	input_mapping_get_names( lst );
	
	for( i=0; i<lst.size(); i++ )
	{
		wcstring seq = lst.at(i);
		
        wcstring ecmd;
        input_mapping_get(seq, ecmd);
        ecmd = escape_string(ecmd, 1);		
        
        wcstring tname; 
		if( input_terminfo_get_name(seq, tname) )
		{
			sb_printf( sb_out, L"bind -k %ls %ls\n", tname.c_str(), ecmd.c_str() );
		}
		else
		{
			const wcstring eseq = escape_string( seq, 1 );	
			sb_printf( sb_out, L"bind %ls %ls\n", eseq.c_str(), ecmd.c_str() );
		}		
	}	
}

/**
   Print terminfo key binding names to string buffer used for standard output.

   \param all if set, all terminfo key binding names will be
   printed. If not set, only ones that are defined for this terminal
   are printed.
 */
static void builtin_bind_key_names( int all )
{
    const wcstring_list_t names = input_terminfo_get_names(!all);	
	for( size_t i=0; i<names.size(); i++ )
	{
        const wcstring &name = names.at(i);
		
		sb_printf( sb_out, L"%ls\n", name.c_str() );
	}
}

/**
   Print all the special key binding functions to string buffer used for standard output.

 */
static void builtin_bind_function_names()
{
    wcstring_list_t names = input_function_get_names();
	
	for( size_t i=0; i<names.size(); i++ )
	{
		const wchar_t *seq = names.at(i).c_str();		
		sb_printf( sb_out, L"%ls\n", seq );
	}
}

/**
   Add specified key binding.
 */
static int builtin_bind_add( wchar_t *seq, wchar_t *cmd, int terminfo )
{

	if( terminfo )
	{
		const wchar_t *seq2 = input_terminfo_get_sequence( seq );
		if( seq2 )
		{
			input_mapping_add( seq2, cmd );
		}
		else
		{
			switch( errno )
			{

				case ENOENT:
				{
					sb_printf( sb_err, _(L"%ls: No key with name '%ls' found\n"), L"bind", seq );
					break;
				}

				case EILSEQ:
				{
					sb_printf( sb_err, _(L"%ls: Key with name '%ls' does not have any mapping\n"), L"bind", seq );
					break;
				}

				default:
				{
					sb_printf( sb_err, _(L"%ls: Unknown error trying to bind to key named '%ls'\n"), L"bind", seq );
					break;
				}

			}
			
			return 1;
		}
		
	}
	else
	{
		input_mapping_add( seq, cmd );
	}
	
	return 0;
	
}

/**
   Erase specified key bindings

   \param seq an array of all key bindings to erase
   \param all if specified, _all_ key bindings will be erased
 */
static void builtin_bind_erase( wchar_t **seq, int all )
{
	if( all )
	{
		size_t i;
		wcstring_list_t lst;
		input_mapping_get_names( lst );
		
		for( i=0; i<lst.size(); i++ )
		{
			input_mapping_erase( lst.at(i).c_str() );			
		}		
		
	}
	else
	{
		while( *seq )
		{
			input_mapping_erase( *seq++ );
		}
		
	}
	
}


/**
   The bind builtin, used for setting character sequences
*/
static int builtin_bind( parser_t &parser, wchar_t **argv )
{

	enum
	{
		BIND_INSERT,
		BIND_ERASE,
		BIND_KEY_NAMES,
		BIND_FUNCTION_NAMES
	}
	;
	
	int argc=builtin_count_args( argv );
	int mode = BIND_INSERT;
	int res = STATUS_BUILTIN_OK;
	int all = 0;
	
	int use_terminfo = 0;
	
	woptind=0;

	static const struct woption
		long_options[] =
		{
			{
				L"all", no_argument, 0, 'a'
			}
			,
			{
				L"erase", no_argument, 0, 'e'
			}
			,
			{
				L"function-names", no_argument, 0, 'f'
			}
			,
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				L"key", no_argument, 0, 'k'
			}
			,
			{
				L"key-names", no_argument, 0, 'K'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;
		int opt = wgetopt_long( argc,
					argv,
					L"aehkKf",
					long_options,
					&opt_index );
		
		if( opt == -1 )
			break;
		
		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
				sb_printf( sb_err,
						   BUILTIN_ERR_UNKNOWN,
						   argv[0],
						   long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );

				return STATUS_BUILTIN_ERROR;

			case 'a':
				all = 1;
				break;
				
			case 'e':
				mode = BIND_ERASE;
				break;
				

			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;
				
			case 'k':
				use_terminfo = 1;
				break;
				
			case 'K':
				mode = BIND_KEY_NAMES;
				break;
				
			case 'f':
				mode = BIND_FUNCTION_NAMES;
				break;
				
			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}
		
	}

	switch( mode )
	{
		
		case BIND_ERASE:
		{
			builtin_bind_erase( &argv[woptind], all);
			break;
		}
		
		case BIND_INSERT:
		{
			switch( argc-woptind )
			{
				case 0:
				{
					builtin_bind_list();
					break;
				}

				case 2:
				{
					builtin_bind_add(argv[woptind], argv[woptind+1], use_terminfo );
					break;
				}

				default:
				{
					res = STATUS_BUILTIN_ERROR;
					sb_printf( sb_err, _(L"%ls: Expected zero or two parameters, got %d"), argv[0], argc-woptind );
					break;
				}
			}
			break;
		}

		case BIND_KEY_NAMES:
		{
			builtin_bind_key_names( all );
			break;
		}

		
		case BIND_FUNCTION_NAMES:
		{
			builtin_bind_function_names();
			break;
		}

		
		default:
		{
			res = STATUS_BUILTIN_ERROR;
			sb_printf( sb_err, _(L"%ls: Invalid state\n"), argv[0] );
			break;
		}
	}
	
	return res;
}

/**
   The block builtin, used for temporarily blocking events
*/
static int builtin_block( parser_t &parser, wchar_t **argv )
{
	enum
	{
		UNSET,
		GLOBAL,
		LOCAL,
	}
	;

	int scope=UNSET;
	int erase = 0;
	int argc=builtin_count_args( argv );
	int type = (1<<EVENT_ANY);

	woptind=0;

	static const struct woption
		long_options[] =
		{
			{
				L"erase", no_argument, 0, 'e'
			}
			,
			{
				L"local", no_argument, 0, 'l'
			}
			,
			{
				L"global", no_argument, 0, 'g'
			}
			,
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L"elgh",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
				sb_printf( sb_err,
						   BUILTIN_ERR_UNKNOWN,
						   argv[0],
						   long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );

				return STATUS_BUILTIN_ERROR;
			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;

			case 'g':
				scope = GLOBAL;
				break;

			case 'l':
				scope = LOCAL;
				break;

			case 'e':
				erase = 1;
				break;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}

	}

	if( erase )
	{
		if( scope != UNSET )
		{
			sb_printf( sb_err, _( L"%ls: Can not specify scope when removing block\n" ), argv[0] );
			return STATUS_BUILTIN_ERROR;
		}
        
        if (parser.global_event_blocks.empty())
		{
			sb_printf( sb_err, _( L"%ls: No blocks defined\n" ), argv[0] );
			return STATUS_BUILTIN_ERROR;
		}
		parser.global_event_blocks.pop_front();
	}
	else
	{
		block_t *block=parser.current_block;

        event_block_t eb = {};
		eb.typemask = type;

		switch( scope )
		{
			case LOCAL:
			{
				if( !block->outer )
					block=0;
				break;
			}
			case GLOBAL:
			{
				block=0;
			}
			case UNSET:
			{
				while( block && 
				       block->type != FUNCTION_CALL &&
				       block->type != FUNCTION_CALL_NO_SHADOW )
					block = block->outer;
			}
		}
		if( block )
		{
            block->event_blocks.push_front(eb);
		}
		else
		{
            parser.global_event_blocks.push_front(eb);
		}
	}

	return STATUS_BUILTIN_OK;

}

/**
   The builtin builtin, used for giving builtins precedence over
   functions. Mostly handled by the parser. All this code does is some
   additional operational modes, such as printing a list of all
   builtins, printing help, etc.
*/
static int builtin_builtin( parser_t &parser, wchar_t **argv )
{
	int argc=builtin_count_args( argv );
	int list=0;

	woptind=0;

	static const struct woption
		long_options[] =
		{
			{
				L"names", no_argument, 0, 'n'
			}
			,
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L"nh",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );


				return STATUS_BUILTIN_ERROR;
			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;

			case 'n':
				list=1;
				break;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}

	}

	if( list )
	{
        wcstring_list_t names = builtin_get_names();
        sort(names.begin(), names.end());
		
		for( size_t i=0; i<names.size(); i++ )
		{
			const wchar_t *el = names.at(i).c_str();
			
			sb_append( sb_out,
						el,
						L"\n",
						NULL );
		}
	}
	return STATUS_BUILTIN_OK;
}

/**
   Implementation of the builtin emit command, used to create events.
 */
static int builtin_emit( parser_t &parser, wchar_t **argv )
{
	int argc=builtin_count_args( argv );
	
	woptind=0;

	static const struct woption
		long_options[] =
		{
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L"h",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );
				return STATUS_BUILTIN_ERROR;

			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}

	}

	for (; woptind < argc; woptind++ )
	{
		event_fire_generic( argv[woptind] );
	}

	return STATUS_BUILTIN_OK;
	
	
	
}


/**
   A generic bultin that only supports showing a help message. This is
   only a placeholder that prints the help message. Useful for
   commands that live in the parser.
*/
static int builtin_generic( parser_t &parser, wchar_t **argv )
{
	int argc=builtin_count_args( argv );
	woptind=0;

	static const struct woption
		long_options[] =
		{
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L"h",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );
				return STATUS_BUILTIN_ERROR;

			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}

	}
	return STATUS_BUILTIN_ERROR;
}

/**
   Output a definition of the specified function to the specified
   stringbuffer. Used by the functions builtin.
*/
static void functions_def( wchar_t *name, string_buffer_t *out )
{
	const wchar_t *desc = function_get_desc( name );
	const wchar_t *def = function_get_definition(name);

	event_t search;

	search.function_name = name;
	search.type = EVENT_ANY;

	std::vector<event_t *> ev;
	event_get( &search, &ev );

	sb_append( out,
		    L"function ",
		    name,
		    NULL);

	if( desc && wcslen(desc) )
	{
		wchar_t *esc_desc = escape( desc, 1 );

		sb_append( out, L" --description ", esc_desc, NULL );
		free( esc_desc );
	}

	if( !function_get_shadows( name ) )
	{
		sb_append( out, L" --no-scope-shadowing", NULL );	
	}

	for( size_t i=0; i<ev.size(); i++ )
	{
		event_t *next = ev.at(i);
		switch( next->type )
		{
			case EVENT_SIGNAL:
			{
				sb_printf( out, L" --on-signal %ls", sig2wcs( next->param1.signal ) );
				break;
			}

			case EVENT_VARIABLE:
			{
				sb_printf( out, L" --on-variable %ls", next->param1.variable );
				break;
			}

			case EVENT_EXIT:
			{
				if( next->param1.pid > 0 )
					sb_printf( out, L" --on-process-exit %d", next->param1.pid );
				else
					sb_printf( out, L" --on-job-exit %d", -next->param1.pid );
				break;
			}

			case EVENT_JOB_ID:
			{
				job_t *j = job_get( next->param1.job_id );
				if( j )
					sb_printf( out, L" --on-job-exit %d", j->pgid );
				break;
			}

			case EVENT_GENERIC:
			{
				sb_printf( out, L" --on-event %ls", next->param1.param );
				break;
			}
						
		}

	}

	
    wcstring_list_t named = function_get_named_arguments( name );
	if( named.size() > 0 )
	{
		sb_printf( out, L" --argument" );
		for( size_t i=0; i < named.size(); i++ )
		{
			sb_printf( out, L" %ls", named.at(i).c_str() );
		}
	}
	

	sb_printf( out, L"\n\t%ls\nend\n", def );
}


/**
   The functions builtin, used for listing and erasing functions.
*/
static int builtin_functions( parser_t &parser, wchar_t **argv )
{
	int i;
	int erase=0;
	wchar_t *desc=0;

	int argc=builtin_count_args( argv );
	int list=0;
	int show_hidden=0;
	int res = STATUS_BUILTIN_OK;
	int query = 0;
	int copy = 0;

	woptind=0;

	static const struct woption
		long_options[] =
		{
			{
				L"erase", no_argument, 0, 'e'
			}
			,
			{
				L"description", required_argument, 0, 'd'
			}
			,
			{
				L"names", no_argument, 0, 'n'
			}
			,
			{
				L"all", no_argument, 0, 'a'
			}
			,
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				L"query", no_argument, 0, 'q'
			}
			,
			{
				L"copy", no_argument, 0, 'c'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L"ed:nahqc",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );


				return STATUS_BUILTIN_ERROR;

			case 'e':
				erase=1;
				break;

			case 'd':
				desc=woptarg;
				break;

			case 'n':
				list=1;
				break;

			case 'a':
				show_hidden=1;
				break;

			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;

			case 'q':
				query = 1;
				break;

			case 'c':
				copy = 1;
				break;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}

	}

	/*
	  Erase, desc, query, copy and list are mutually exclusive
	*/
	if( (erase + (!!desc) + list + query + copy) > 1 )
	{
		sb_printf( sb_err,
				   _( L"%ls: Invalid combination of options\n" ),
				   argv[0] );

		builtin_print_help( parser, argv[0], sb_err );

		return STATUS_BUILTIN_ERROR;
	}

	if( erase )
	{
		int i;
		for( i=woptind; i<argc; i++ )
			function_remove( argv[i] );
		return STATUS_BUILTIN_OK;
	}
	else if( desc )
	{
		wchar_t *func;

		if( argc-woptind != 1 )
		{
			sb_printf( sb_err,
					   _( L"%ls: Expected exactly one function name\n" ),
					   argv[0] );
			builtin_print_help( parser, argv[0], sb_err );

			return STATUS_BUILTIN_ERROR;
		}
		func = argv[woptind];
		if( !function_exists( func ) )
		{
			sb_printf( sb_err,
					   _( L"%ls: Function '%ls' does not exist\n" ),
					   argv[0],
					   func );

			builtin_print_help( parser, argv[0], sb_err );

			return STATUS_BUILTIN_ERROR;
		}

		function_set_desc( func, desc );

		return STATUS_BUILTIN_OK;
	}
	else if( list || (argc==woptind))
	{
		int is_screen = !builtin_out_redirect && isatty(1);
        size_t i;
		wcstring_list_t names = function_get_names( show_hidden );
        std::sort(names.begin(), names.end());
		if( is_screen )
		{
			string_buffer_t buff;
			sb_init( &buff );

			for( i=0; i<names.size(); i++ )
			{
				sb_append( &buff,
							names.at(i).c_str(),
							L", ",
							NULL );
			}

			write_screen( (wchar_t *)buff.buff, sb_out );
			sb_destroy( &buff );
		}
		else
		{
			for( i=0; i<names.size(); i++ )
			{
				sb_append( sb_out,
							names.at(i).c_str(),
							L"\n",
							NULL );
			}
		}

		return STATUS_BUILTIN_OK;
	}
	else if( copy )
	{
		wchar_t *current_func;
		wchar_t *new_func;
		
		if( argc-woptind != 2 )
		{
			sb_printf( sb_err,
					   _( L"%ls: Expected exactly two names (current function name, and new function name)\n" ),
					   argv[0] );
			builtin_print_help ( parser, argv[0], sb_err );
			
			return STATUS_BUILTIN_ERROR;
		}
		current_func = argv[woptind];
		new_func = argv[woptind+1];
		
		if( !function_exists( current_func ) )
		{
			sb_printf( sb_err,
					   _( L"%ls: Function '%ls' does not exist\n" ),
					   argv[0],
					   current_func );
			builtin_print_help( parser, argv[0], sb_err );

			return STATUS_BUILTIN_ERROR;
		}

		if( (wcsfuncname( new_func ) != 0) || parser_keywords_is_reserved( new_func ) )
		{
			sb_printf( sb_err,
			           _( L"%ls: Illegal function name '%ls'\n"),
			           argv[0],
			           new_func );
			builtin_print_help( parser, argv[0], sb_err );
			return STATUS_BUILTIN_ERROR;
		}

		// keep things simple: don't allow existing names to be copy targets.
		if( function_exists( new_func ) )
		{
			sb_printf( sb_err,
					   _( L"%ls: Function '%ls' already exists. Cannot create copy '%ls'\n" ),
					   argv[0],
					   new_func,
					   current_func );            
			builtin_print_help( parser, argv[0], sb_err );

			return STATUS_BUILTIN_ERROR;
		}

		if( function_copy( current_func, new_func ) )
			return STATUS_BUILTIN_OK;
		return STATUS_BUILTIN_ERROR;
	}

	for( i=woptind; i<argc; i++ )
	{
		if( !function_exists( argv[i] ) )
			res++;
		else
		{
			if( !query )
			{
				if( i != woptind)
					sb_append( sb_out, L"\n" );
				
				functions_def( argv[i], sb_out );
			}
		}				
	}

	return res;

}


/**
   The function builtin, used for providing subroutines.
   It calls various functions from function.c to perform any heavy lifting.
*/
static int builtin_function( parser_t &parser, wchar_t **argv )
{
	int argc = builtin_count_args( argv );
	int res=STATUS_BUILTIN_OK;
	wchar_t *desc=0;
	array_list_t *events;
	int i;
	array_list_t *named_arguments=0;
	wchar_t *name = 0;
	int shadows = 1;
		
	woptind=0;

	parser.push_block( FUNCTION_DEF );
	events=al_halloc( parser.current_block );

	static const struct woption
		long_options[] =
		{
			{
				L"description", required_argument, 0, 'd'
			}
			,
			{
				L"on-signal", required_argument, 0, 's'
			}
			,
			{
				L"on-job-exit", required_argument, 0, 'j'
			}
			,
			{
				L"on-process-exit", required_argument, 0, 'p'
			}
			,
			{
				L"on-variable", required_argument, 0, 'v'
			}
			,
			{
				L"on-event", required_argument, 0, 'e'
			}
			,
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				L"argument-names", no_argument, 0, 'a'
			}
			,
			{
				L"no-scope-shadowing", no_argument, 0, 'S'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 && (!res ) )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
					argv,
					L"d:s:j:p:v:e:haS",
					long_options,
					&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );

				res = 1;
				break;

			case 'd':
				desc=woptarg;
				break;

			case 's':
			{
				int sig = wcs2sig( woptarg );
				event_t *e;

				if( sig < 0 )
				{
					sb_printf( sb_err,
							   _( L"%ls: Unknown signal '%ls'\n" ),
							   argv[0],
							   woptarg );
					res=1;
					break;
				}

				e = (event_t *)halloc( parser.current_block, sizeof(event_t));
				if( !e )
					DIE_MEM();
				e->type = EVENT_SIGNAL;
				e->param1.signal = sig;
				e->function_name=0;
				al_push( events, e );
				break;
			}

			case 'v':
			{
				event_t *e;

				if( wcsvarname( woptarg ) )
				{
					sb_printf( sb_err,
							   _( L"%ls: Invalid variable name '%ls'\n" ),
							   argv[0],
							   woptarg );
					res=STATUS_BUILTIN_ERROR;
					break;
				}

				e = (event_t *)halloc( parser.current_block, sizeof(event_t));

				e->type = EVENT_VARIABLE;
				e->param1.variable = halloc_wcsdup( parser.current_block, woptarg );
				e->function_name=0;
				al_push( events, e );
				break;
			}


			case 'e':
			{
				event_t *e;
				
				e = (event_t *)halloc( parser.current_block, sizeof(event_t));
				
				e->type = EVENT_GENERIC;
				e->param1.param = halloc_wcsdup( parser.current_block, woptarg );
				e->function_name=0;
				al_push( events, e );
				break;
			}

			case 'j':
			case 'p':
			{
				pid_t pid;
				wchar_t *end;
				event_t *e;

				e = (event_t *)halloc( parser.current_block, sizeof(event_t));
				if( !e )
					DIE_MEM();
				
				if( ( opt == 'j' ) &&
					( wcscasecmp( woptarg, L"caller" ) == 0 ) )
				{
					int job_id = -1;

					if( is_subshell )
					{
						block_t *b = parser.current_block;

						while( b && (b->type != SUBST) )
							b = b->outer;

						if( b )
						{
							b=b->outer;
						}
						if( b->job )
						{
							job_id = b->job->job_id;
						}
					}

					if( job_id == -1 )
					{
						sb_printf( sb_err,
								   _( L"%ls: Cannot find calling job for event handler\n" ),
								   argv[0] );
						res=1;
					}
					else
					{
						e->type = EVENT_JOB_ID;
						e->param1.job_id = job_id;
					}

				}
				else
				{
					errno = 0;
					pid = wcstol( woptarg, &end, 10 );
					if( errno || !end || *end )
					{
						sb_printf( sb_err,
								   _( L"%ls: Invalid process id %ls\n" ),
								   argv[0],
								   woptarg );
						res=1;
						break;
					}


					e->type = EVENT_EXIT;
					e->param1.pid = (opt=='j'?-1:1)*abs(pid);
				}
				if( res )
				{
					free( e );
				}
				else
				{
					e->function_name=0;
					al_push( events, e );
				}
				break;
			}

			case 'a':
				if( !named_arguments )
					named_arguments = al_halloc( parser.current_block );
				break;
				
			case 'S':
				shadows = 0;
				break;
				
			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;
				
			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				res = 1;
				break;

		}

	}

	if( !res )
	{
		
		if( argc == woptind )
		{
			sb_printf( sb_err,
					   _( L"%ls: Expected function name\n" ),
					   argv[0] );
			res=1;
		}
		else if( wcsfuncname( argv[woptind] ) )
		{
			sb_printf( sb_err,
					   _( L"%ls: Illegal function name '%ls'\n" ),
					   argv[0],
					   argv[woptind] );

			res=1;
		}
		else if( parser_keywords_is_reserved(argv[woptind] ) )
		{

			sb_printf( sb_err,
					   _( L"%ls: The name '%ls' is reserved,\nand can not be used as a function name\n" ),
					   argv[0],
					   argv[woptind] );

			res=1;
		}
		else
		{

			name = argv[woptind++];
			
			if( named_arguments )
			{
				while( woptind < argc )
				{
					if( wcsvarname( argv[woptind] ) )
					{
						sb_printf( sb_err,
								   _( L"%ls: Invalid variable name '%ls'\n" ),
								   argv[0],
								   argv[woptind] );
						res = STATUS_BUILTIN_ERROR;
						break;
					}
					
					al_push( named_arguments, halloc_wcsdup( parser.current_block, argv[woptind++] ) );
				}
			}
			else if( woptind != argc )
			{
				sb_printf( sb_err,
						   _( L"%ls: Expected one argument, got %d\n" ),
						   argv[0],
						   argc );
				res=1;
				
			}
		}
	}

	if( res )
	{
		size_t i;
		int chars=0;

		builtin_print_help( parser, argv[0], sb_err );
		const wchar_t *cfa =  _( L"Current functions are: " );
		sb_append( sb_err, cfa );
		chars += wcslen( cfa );

        wcstring_list_t names = function_get_names(0);
        sort(names.begin(), names.end());

		for( i=0; i<names.size(); i++ )
		{
			const wchar_t *nxt = names.at(i).c_str();
			int l = wcslen( nxt + 2 );
			if( chars+l > common_get_width() )
			{
				chars = 0;
				sb_append(sb_err, L"\n" );
			}

			sb_append( sb_err,
						nxt, L"  ", NULL );
		}
		sb_append( sb_err, L"\n" );

		parser.pop_block();
		parser.push_block( FAKE );
	}
	else
	{
		function_data_t *d = new function_data_t();
		
		d->name=halloc_wcsdup( parser.current_block, name);
		d->description=desc?halloc_wcsdup( parser.current_block, desc):0;
		d->events = events;
		d->named_arguments = named_arguments;
		d->shadows = shadows;
		
		for( i=0; i<al_get_count( events ); i++ )
		{
			event_t *e = (event_t *)al_get( events, i );
			e->function_name = d->name;
		}

		parser.current_block->function_data.reset(d);
		
	}
	
	parser.current_block->tok_pos = parser.get_pos();
	parser.current_block->skip = 1;

	return STATUS_BUILTIN_OK;

}

/**
   The random builtin. For generating random numbers.
*/
static int builtin_random( parser_t &parser, wchar_t **argv )
{
	static int seeded=0;
	static struct drand48_data seed_buffer;
	
	int argc = builtin_count_args( argv );

	woptind=0;

	static const struct woption
		long_options[] =
		{
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L"h",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );

				return STATUS_BUILTIN_ERROR;

			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				break;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}

	}

	switch( argc-woptind )
	{

		case 0:
		{
			long res;
			
			if( !seeded )
			{
				seeded=1;
				srand48_r(time(0), &seed_buffer);
			}
			lrand48_r( &seed_buffer, &res );
			
			sb_printf( sb_out, L"%d\n", abs(res%32767) );
			break;
		}

		case 1:
		{
			long foo;
			wchar_t *end=0;

			errno=0;
			foo = wcstol( argv[woptind], &end, 10 );
			if( errno || *end )
			{
				sb_printf( sb_err,
						   _( L"%ls: Seed value '%ls' is not a valid number\n" ),
						   argv[0],
						   argv[woptind] );

				return STATUS_BUILTIN_ERROR;
			}
			seeded=1;
			srand48_r( foo, &seed_buffer);
			break;
		}

		default:
		{
			sb_printf( sb_err,
					   _( L"%ls: Expected zero or one argument, got %d\n" ),
					   argv[0],
					   argc-woptind );
			builtin_print_help( parser, argv[0], sb_err );
			return STATUS_BUILTIN_ERROR;
		}
	}
	return STATUS_BUILTIN_OK;
}


/**
   The read builtin. Reads from stdin and stores the values in environment variables.
*/
static int builtin_read( parser_t &parser, wchar_t **argv )
{
	wchar_t *buff=0;
	int i, argc = builtin_count_args( argv );
	int place = ENV_USER;
	wchar_t *nxt;
	const wchar_t *prompt = DEFAULT_READ_PROMPT;
	const wchar_t *commandline = L"";
	int exit_res=STATUS_BUILTIN_OK;
	const wchar_t *mode_name = READ_MODE_NAME;
	int shell = 0;
	
	woptind=0;
	
	while( 1 )
	{
		static const struct woption
			long_options[] =
			{
				{
					L"export", no_argument, 0, 'x'
				}
				,
				{
					L"global", no_argument, 0, 'g'
				}
				,
				{
					L"local", no_argument, 0, 'l'
				}
				,
				{
					L"universal", no_argument, 0, 'U'
				}
				,
				{
					L"unexport", no_argument, 0, 'u'
				}
				,
				{
					L"prompt", required_argument, 0, 'p'
				}
				,
				{
					L"command", required_argument, 0, 'c'
				}
				,
				{
					L"mode-name", required_argument, 0, 'm'
				}
				,
				{
					L"shell", no_argument, 0, 's'
				}
				,
				{
					L"help", no_argument, 0, 'h'
				}
				,
				{
					0, 0, 0, 0
				}
			}
		;

		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L"xglUup:c:hm:s",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );

				return STATUS_BUILTIN_ERROR;

			case L'x':
				place |= ENV_EXPORT;
				break;

			case L'g':
				place |= ENV_GLOBAL;
				break;

			case L'l':
				place |= ENV_LOCAL;
				break;

			case L'U':
				place |= ENV_UNIVERSAL;
				break;

			case L'u':
				place |= ENV_UNEXPORT;
				break;

			case L'p':
				prompt = woptarg;
				break;

			case L'c':
				commandline = woptarg;
				break;

			case L'm':
				mode_name = woptarg;
				break;

			case 's':
				shell = 1;
				break;
				
			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;

			case L'?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;
		}

	}

	if( ( place & ENV_UNEXPORT ) && ( place & ENV_EXPORT ) )
	{
		sb_printf( sb_err,
				   BUILTIN_ERR_EXPUNEXP,
				   argv[0] );


		builtin_print_help( parser, argv[0], sb_err );
		return STATUS_BUILTIN_ERROR;
	}

	if( (place&ENV_LOCAL?1:0) + (place & ENV_GLOBAL?1:0) + (place & ENV_UNIVERSAL?1:0) > 1)
	{
		sb_printf( sb_err,
				   BUILTIN_ERR_GLOCAL,
				   argv[0] );
		builtin_print_help( parser, argv[0], sb_err );

		return STATUS_BUILTIN_ERROR;
	}

	/*
	  Verify all variable names
	*/
	for( i=woptind; i<argc; i++ )
	{
		wchar_t *src;

		if( !wcslen( argv[i] ) )
		{
			sb_printf( sb_err, BUILTIN_ERR_VARNAME_ZERO, argv[0] );
			return STATUS_BUILTIN_ERROR;
		}

		for( src=argv[i]; *src; src++ )
		{
			if( (!iswalnum(*src)) && (*src != L'_' ) )
			{
				sb_printf( sb_err, BUILTIN_ERR_VARCHAR, argv[0], *src );
				builtin_print_help( parser, argv[0], sb_err );
				return STATUS_BUILTIN_ERROR;
			}
		}

	}

	/*
	  The call to reader_readline may change woptind, so we save it away here
	*/
	i=woptind;

	/*
	  Check if we should read interactively using \c reader_readline()
	*/
	if( isatty(0) && builtin_stdin == 0 )
	{
		const wchar_t *line;
		
		reader_push( mode_name );
		reader_set_prompt( prompt );
		if( shell )
		{
			reader_set_complete_function( &complete );
			reader_set_highlight_function( &highlight_shell );
			reader_set_test_function( &reader_shell_test );
		}
		
		reader_set_buffer( commandline, wcslen( commandline ) );
		proc_push_interactive( 1 );
		
		event_fire_generic(L"fish_prompt");
		line = reader_readline( );
		proc_pop_interactive();
		if( line )
		{
			buff = wcsdup( line );
		}
		else
		{
			exit_res = STATUS_BUILTIN_ERROR;
		}
		reader_pop();
	}
	else
	{
		string_buffer_t sb;
		int eof=0;
		
		sb_init( &sb );
		
		while( 1 )
		{
			int finished=0;

			wchar_t res=0;
			static mbstate_t state;
			memset (&state, '\0', sizeof (state));

			while( !finished )
			{
				char b;
				int read_res = read_blocked( builtin_stdin, &b, 1 );
				if( read_res <= 0 )
				{
					eof=1;
					break;
				}

				int sz = mbrtowc( &res, &b, 1, &state );

				switch( sz )
				{
					case -1:
						memset (&state, '\0', sizeof (state));
						break;

					case -2:
						break;
					case 0:
						eof=1;
						finished = 1;
						break;

					default:
						finished=1;
						break;

				}
			}

			if( eof )
				break;

			if( res == L'\n' )
				break;

			sb_append_char( &sb, res );
		}

		if( sb.used < 2 && eof )
		{
			exit_res = 1;
		}
		
		buff = wcsdup( (wchar_t *)sb.buff );
		sb_destroy( &sb );
	}

	if( i != argc && !exit_res )
	{
		
		wchar_t *state;

		env_var_t ifs = env_get_string( L"IFS" );
		if( ifs.missing() )
			ifs = L"";
		
		nxt = wcstok( buff, (i<argc-1)?ifs.c_str():L"", &state );
		
		while( i<argc )
		{
			env_set( argv[i], nxt != 0 ? nxt: L"", place );
			
			i++;
			if( nxt != 0 )
				nxt = wcstok( 0, (i<argc-1)?ifs.c_str():L"", &state);
		}
	}
	
	free( buff );

	return exit_res;
}

/**
   The status builtin. Gives various status information on fish.
*/
static int builtin_status( parser_t &parser, wchar_t **argv )
{
	
	enum
	{
		NORMAL,
		IS_SUBST,
		IS_BLOCK,
		IS_INTERACTIVE,
		IS_LOGIN,
		IS_FULL_JOB_CONTROL,
		IS_INTERACTIVE_JOB_CONTROL,
		IS_NO_JOB_CONTROL,
		STACK_TRACE,
		DONE,
		CURRENT_FILENAME,
		CURRENT_LINE_NUMBER
	}
	;

	int mode = NORMAL;

	int argc = builtin_count_args( argv );
	int res=STATUS_BUILTIN_OK;

	woptind=0;


	const struct woption
		long_options[] =
		{
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				L"is-command-substitution", no_argument, 0, 'c'
			}
			,
			{
				L"is-block", no_argument, 0, 'b'
			}
			,
			{
				L"is-interactive", no_argument, 0, 'i'
			}
			,
			{
				L"is-login", no_argument, 0, 'l'
			}
			,
			{
				L"is-full-job-control", no_argument, &mode, IS_FULL_JOB_CONTROL
			}
			,
			{
				L"is-interactive-job-control", no_argument, &mode, IS_INTERACTIVE_JOB_CONTROL
			}
			,
			{
				L"is-no-job-control", no_argument, &mode, IS_NO_JOB_CONTROL
			}
			,
			{
				L"current-filename", no_argument, 0, 'f'
			}
			,
			{
				L"current-line-number", no_argument, 0, 'n'
			}
			,
			{
				L"job-control", required_argument, 0, 'j'
			}
			,
			{
				L"print-stack-trace", no_argument, 0, 't'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
								argv,
								L":cbilfnhj:t",
								long_options,
								&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
                sb_printf( sb_err,
                           BUILTIN_ERR_UNKNOWN,
                           argv[0],
                           long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );
				return STATUS_BUILTIN_ERROR;

			case 'c':
				mode = IS_SUBST;
				break;

			case 'b':
				mode = IS_BLOCK;
				break;

			case 'i':
				mode = IS_INTERACTIVE;
				break;

			case 'l':
				mode = IS_LOGIN;
				break;

			case 'f':
				mode = CURRENT_FILENAME;
				break;

			case 'n':
				mode = CURRENT_LINE_NUMBER;
				break;

			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;

			case 'j':
				if( wcscmp( woptarg, L"full" ) == 0 )
					job_control_mode = JOB_CONTROL_ALL;
				else if( wcscmp( woptarg, L"interactive" ) == 0 )
					job_control_mode = JOB_CONTROL_INTERACTIVE;
				else if( wcscmp( woptarg, L"none" ) == 0 )
					job_control_mode = JOB_CONTROL_NONE;
				else
				{
					sb_printf( sb_err,
							   L"%ls: Invalid job control mode '%ls'\n",
							   woptarg );
					res = 1;
				}
				mode = DONE;				
				break;

			case 't':
				mode = STACK_TRACE;
				break;
				

			case ':':
				builtin_missing_argument( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}

	}

	if( !res )
	{

		switch( mode )
		{
			case CURRENT_FILENAME:
			{
				const wchar_t *fn = parser.current_filename();
				
				if( !fn )
					fn = _(L"Standard input");
				
				sb_printf( sb_out, L"%ls\n", fn );
				
				break;
			}
			
			case CURRENT_LINE_NUMBER:
			{
				sb_printf( sb_out, L"%d\n", parser.get_lineno() );
				break;				
			}
			
			case IS_INTERACTIVE:
				return !is_interactive_session;

			case IS_SUBST:
				return !is_subshell;
				
			case IS_BLOCK:
				return !is_block;

			case IS_LOGIN:
				return !is_login;
			
			case IS_FULL_JOB_CONTROL:
				return job_control_mode != JOB_CONTROL_ALL;

			case IS_INTERACTIVE_JOB_CONTROL:
				return job_control_mode != JOB_CONTROL_INTERACTIVE;

			case IS_NO_JOB_CONTROL:
				return job_control_mode != JOB_CONTROL_NONE;

			case STACK_TRACE:
			{
				parser.stack_trace( parser.current_block, sb_out );
				break;
			}

			case NORMAL:
			{
				if( is_login )
					sb_printf( sb_out, _( L"This is a login shell\n" ) );
				else
					sb_printf( sb_out, _( L"This is not a login shell\n" ) );

				sb_printf( sb_out, _( L"Job control: %ls\n" ),
						   job_control_mode==JOB_CONTROL_INTERACTIVE?_( L"Only on interactive jobs" ):
						   (job_control_mode==JOB_CONTROL_NONE ? _( L"Never" ) : _( L"Always" ) ) );

				parser.stack_trace( parser.current_block, sb_out );
				break;
			}
		}
	}

	return res;
}


/**
   The exit builtin. Calls reader_exit to exit and returns the value specified.
*/
static int builtin_exit( parser_t &parser, wchar_t **argv )
{
	int argc = builtin_count_args( argv );

	int ec=0;
	switch( argc )
	{
		case 1:
		{
			ec = proc_get_last_status();
			break;
		}
		
		case 2:
		{
			wchar_t *end;
			errno = 0;
			ec = wcstol(argv[1],&end,10);
			if( errno || *end != 0)
			{
				sb_printf( sb_err,
					   _( L"%ls: Argument '%ls' must be an integer\n" ),
					   argv[0],
					   argv[1] );
				builtin_print_help( parser, argv[0], sb_err );
				return STATUS_BUILTIN_ERROR;
			}
			break;
		}

		default:
		{
			sb_printf( sb_err,
				   BUILTIN_ERR_TOO_MANY_ARGUMENTS,
				   argv[0] );

			builtin_print_help( parser, argv[0], sb_err );
			return STATUS_BUILTIN_ERROR;
		}
		
	}
	reader_exit( 1, 0 );
	return ec;
}

/**
   The cd builtin. Changes the current directory to the one specified
   or to $HOME if none is specified. The directory can be relative to
   any directory in the CDPATH variable.
*/
static int builtin_cd( parser_t &parser, wchar_t **argv )
{
	env_var_t dir_in;
	wchar_t *dir;
	int res=STATUS_BUILTIN_OK;

	
	if( argv[1]  == 0 )
	{
		dir_in = env_get_string( L"HOME" );
		if( dir_in.missing_or_empty() )
		{
			sb_printf( sb_err,
					   _( L"%ls: Could not find home directory\n" ),
					   argv[0] );
		}
	}
	else
		dir_in = argv[1];

	dir = path_allocate_cdpath( dir_in.missing() ? NULL : dir_in.c_str() );

	if( !dir )
	{
		if( errno == ENOTDIR )
		{
			sb_printf( sb_err,
				   _( L"%ls: '%ls' is not a directory\n" ),
				   argv[0],
				   dir_in.c_str() );
		}
		else if( errno == ENOENT )
		{
			sb_printf( sb_err,
				   _( L"%ls: The directory '%ls' does not exist\n" ),
				   argv[0],
				   dir_in.c_str() );			
		}
		else if( errno == EROTTEN )
		{
			sb_printf( sb_err,
				   _( L"%ls: '%ls' is a rotten symlink\n" ),
				   argv[0],
				   dir_in.c_str() );			
			
		} 
		else 
		{
			sb_printf( sb_err,
				   _( L"%ls: Unknown error trying to locate directory '%ls'\n" ),
				   argv[0],
				   dir_in.c_str() );			
			
		}
		
		
		if( !is_interactive )
		{
			sb_append( sb_err,
				    parser.current_line(),
				    NULL );
		}
		
		res = 1;
	}
	else if( wchdir( dir ) != 0 )
	{
		struct stat buffer;
		int status;
		
		status = wstat( dir, &buffer );
		if( !status && S_ISDIR(buffer.st_mode))
		{
			sb_printf( sb_err,
				   _( L"%ls: Permission denied: '%ls'\n" ),
				   argv[0],
				   dir );
			
		}
		else
		{
			
			sb_printf( sb_err,
				   _( L"%ls: '%ls' is not a directory\n" ),
				   argv[0],
				   dir );
		}
		
		if( !is_interactive )
		{
			sb_append( sb_err,
						parser.current_line(),
						NULL );
		}
		
		res = 1;
	}
	else if( !env_set_pwd() )
	{
		res=1;
		sb_printf( sb_err, _( L"%ls: Could not set PWD variable\n" ), argv[0] );
	}

    free(dir);

	return res;
}

/**
   Implementation of the builtin count command, used to count the
   number of arguments sent to it.
 */
static int builtin_count( parser_t &parser, wchar_t ** argv )
{
	int argc;
	argc = builtin_count_args( argv );
	sb_printf( sb_out, L"%d\n", argc-1 );
	return !(argc-1);
}

/**
   Implementation of the builtin contains command, used to check if a
   specified string is part of a list.
 */
static int builtin_contains( parser_t &parser, wchar_t ** argv )
{
	int argc;
	argc = builtin_count_args( argv );
	int i;
	wchar_t *needle;
	
	woptind=0;

	const struct woption
		long_options[] =
		{
			{
				L"help", no_argument, 0, 'h'
			}
			,
			{
				0, 0, 0, 0
			}
		}
	;

	while( 1 )
	{
		int opt_index = 0;

		int opt = wgetopt_long( argc,
					argv,
					L"+h",
					long_options,
					&opt_index );
		if( opt == -1 )
			break;

		switch( opt )
		{
			case 0:
				if(long_options[opt_index].flag != 0)
					break;
				sb_printf( sb_err,
					   BUILTIN_ERR_UNKNOWN,
					   argv[0],
					   long_options[opt_index].name );
				builtin_print_help( parser, argv[0], sb_err );
				return STATUS_BUILTIN_ERROR;


			case 'h':
				builtin_print_help( parser, argv[0], sb_out );
				return STATUS_BUILTIN_OK;


			case ':':
				builtin_missing_argument( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

			case '?':
				builtin_unknown_option( parser, argv[0], argv[woptind-1] );
				return STATUS_BUILTIN_ERROR;

		}
		
	}



	needle = argv[woptind];
	if (!needle)
	{
		sb_printf( sb_err, _( L"%ls: Key not specified\n" ), argv[0] );
	}
	

	for( i=woptind+1; i<argc; i++ )
	{
		
		if( !wcscmp( needle, argv[i]) )
		{
			return 0;
		}
	}
	return 1;

}


/**
   The  . (dot) builtin, sometimes called source. Evaluates the contents of a file.
*/
static int builtin_source( parser_t &parser, wchar_t ** argv )
{
	int fd;
	int res = STATUS_BUILTIN_OK;
	struct stat buf;
	int argc;

	argc = builtin_count_args( argv );

	const wchar_t *fn;
	const wchar_t *fn_intern;
		


	if( argc < 2 || (wcscmp( argv[1], L"-" ) == 0) )
	{
		fn = L"-";
		fn_intern = fn;
		fd = dup(builtin_stdin);
	}
	else
	{
		
		if( wstat(argv[1], &buf) == -1 )
		{
			sb_printf( sb_err, _(L"%ls: Error encountered while sourcing file '%ls':\n"), argv[0], argv[1] );
			builtin_wperror( L"." );
					
			return STATUS_BUILTIN_ERROR;
		}

		if( !S_ISREG(buf.st_mode) )
		{
			sb_printf( sb_err, _( L"%ls: '%ls' is not a file\n" ), argv[0], argv[1] );
			return STATUS_BUILTIN_ERROR;
		}

		if( ( fd = wopen( argv[1], O_RDONLY ) ) == -1 )
		{
			sb_printf( sb_err, _(L"%ls: Error encountered while sourcing file '%ls':\n"), argv[0], argv[1] );
			builtin_wperror( L"." );
			return STATUS_BUILTIN_ERROR;
		}

		fn = wrealpath( argv[1], 0 );

		if( !fn )
		{
			fn_intern = intern( argv[1] );
		}
		else
		{
			fn_intern = intern(fn);
			free( (void *)fn );
		}

	}
	
	parser.push_block( SOURCE );		
	reader_push_current_filename( fn_intern );
	
    parser.current_block->state1<const wchar_t *>() = fn_intern;
	
	parse_util_set_argv( (argc>2)?(argv+2):(argv+1), wcstring_list_t());
	
	res = reader_read( fd, real_io );
		
	parser.pop_block();
		
	if( res )
	{
		sb_printf( sb_err,
			   _( L"%ls: Error while reading file '%ls'\n" ),
			   argv[0],
				   fn_intern == intern_static(L"-") ? L"<stdin>" : fn_intern );
	}
	else
	{
		res = proc_get_last_status();
	}
	
	/*
	  Do not close fd after calling reader_read. reader_read
	  automatically closes it before calling eval.
	*/
	
	reader_pop_current_filename();

	return res;
}

/**
   Make the specified job the first job of the job list. Moving jobs
   around in the list makes the list reflect the order in which the
   jobs were used.
*/
static void make_first( job_t *j )
{
    job_promote(j);
}


/**
   Builtin for putting a job in the foreground
*/
static int builtin_fg( parser_t &parser, wchar_t **argv )
{
	job_t *j=NULL;

	if( argv[1] == 0 )
	{
		/*
		  Select last constructed job (I.e. first job in the job que)
		  that is possible to put in the foreground
		*/
        
        job_iterator_t jobs;
        while ((j = jobs.next()))
		{
			if( job_get_flag( j, JOB_CONSTRUCTED ) && (!job_is_completed(j)) && 
				( (job_is_stopped(j) || (!job_get_flag(j, JOB_FOREGROUND)) ) && job_get_flag( j, JOB_CONTROL) ) )
			{
				break;
			}
		}
		if( !j )
		{
			sb_printf( sb_err,
					   _( L"%ls: There are no suitable jobs\n" ),
					   argv[0] );
			builtin_print_help( parser, argv[0], sb_err );
		}
	}
	else if( argv[2] != 0 )
	{
		/*
		  Specifying what more than one job to put to the foreground
		  is a syntax error, we still try to locate the job argv[1],
		  since we want to know if this is an ambigous job
		  specification or if this is an malformed job id
		*/
		wchar_t *endptr;
		int pid;
		int found_job = 0;
		
		errno = 0;
		pid = wcstol( argv[1], &endptr, 10 );
		if( !( *endptr || errno )  )
		{			
			j = job_get_from_pid( pid );
			if( j )
				found_job = 1;
		}
		
		if( found_job )
		{
			sb_printf( sb_err,
					   _( L"%ls: Ambiguous job\n" ),
					   argv[0] );
		}
		else
		{
			sb_printf( sb_err,
					   _( L"%ls: '%ls' is not a job\n" ),
					   argv[0],
					   argv[1] );
		}

		builtin_print_help( parser, argv[0], sb_err );

		j=0;

	}
	else
	{
		wchar_t *end;		
		int pid;
		errno = 0;
		pid = abs(wcstol( argv[1], &end, 10 ));
		
		if( *end || errno )
		{
				sb_printf( sb_err,
						   BUILTIN_ERR_NOT_NUMBER,
						   argv[0],
						   argv[1] );
				builtin_print_help( parser, argv[0], sb_err );
		}
		else
		{
			j = job_get_from_pid( pid );
			if( !j || !job_get_flag( j, JOB_CONSTRUCTED ) || job_is_completed( j ))
			{
				sb_printf( sb_err,
						   _( L"%ls: No suitable job: %d\n" ),
						   argv[0],
						   pid );
				builtin_print_help( parser, argv[0], sb_err );
				j=0;
			}
			else if( !job_get_flag( j, JOB_CONTROL) )
			{
				sb_printf( sb_err,
						   _( L"%ls: Can't put job %d, '%ls' to foreground because it is not under job control\n" ),
						   argv[0],
						   pid,
						   j->command_cstr() );
				builtin_print_help( parser, argv[0], sb_err );
				j=0;
			}
		}
	}

	if( j )
	{
		if( builtin_err_redirect )
		{
			sb_printf( sb_err,
					   FG_MSG,
					   j->job_id,
					   j->command_cstr() );
		}
		else
		{
			/*
			  If we aren't redirecting, send output to real stderr,
			  since stuff in sb_err won't get printed until the
			  command finishes.
			*/
			fwprintf( stderr,
					  FG_MSG,
					  j->job_id,
					  j->command_cstr() );
		}

		wchar_t *ft = tok_first( j->command_cstr() );
		if( ft != 0 )
			env_set( L"_", ft, ENV_EXPORT );
		free(ft);
		reader_write_title();

		make_first( j );
		job_set_flag( j, JOB_FOREGROUND, 1 );

		job_continue( j, job_is_stopped(j) );
	}
	return j != 0;
}

/**
   Helper function for builtin_bg()
*/
static int send_to_bg( parser_t &parser, job_t *j, const wchar_t *name )
{
	if( j == 0 )
	{
		sb_printf( sb_err,
				   _( L"%ls: Unknown job '%ls'\n" ),
				   L"bg",
				   name );
		builtin_print_help( parser, L"bg", sb_err );
		return STATUS_BUILTIN_ERROR;
	}
	else if( !job_get_flag( j, JOB_CONTROL ) )
	{
		sb_printf( sb_err,
				   _( L"%ls: Can't put job %d, '%ls' to background because it is not under job control\n" ),
				   L"bg",
				   j->job_id,
				   j->command_cstr() );
		builtin_print_help( parser, L"bg", sb_err );
		return STATUS_BUILTIN_ERROR;
	}
	else
	{
		sb_printf( sb_err,
				   _(L"Send job %d '%ls' to background\n"),
				   j->job_id,
				   j->command_cstr() );
	}
	make_first( j );
	job_set_flag( j, JOB_FOREGROUND, 0 );
	job_continue( j, job_is_stopped(j) );
	return STATUS_BUILTIN_OK;
}


/**
   Builtin for putting a job in the background
*/
static int builtin_bg( parser_t &parser, wchar_t **argv )
{
	int res = STATUS_BUILTIN_OK;

	if( argv[1] == 0 )
	{
  		job_t *j;
        job_iterator_t jobs;
        while ((j = jobs.next()))
        {
			if( job_is_stopped(j) && job_get_flag( j, JOB_CONTROL ) && (!job_is_completed(j)) )
			{
				break;
			}
		}
		
		if( !j )
		{
			sb_printf( sb_err,
					   _( L"%ls: There are no suitable jobs\n" ),
					   argv[0] );
			res = 1;
		}
		else
		{
			res = send_to_bg( parser, j, _(L"(default)" ) );
		}
	}
	else
	{
		wchar_t *end;
		int i;
		int pid;
		int err = 0;
		
		for( i=1; argv[i]; i++ )
		{
			errno=0;
			pid = (int)wcstol( argv[i], &end, 10 );
			if( errno || pid < 0 || *end || !job_get_from_pid( pid ) )
			{
				sb_printf( sb_err,
						   _( L"%ls: '%ls' is not a job\n" ),
						   argv[0],
						   argv[i] );
				err = 1;
				break;
			}		
		}

		if( !err )
		{
			for( i=1; !res && argv[i]; i++ )
			{
				pid = (int)wcstol( argv[i], 0, 10 );
				res |= send_to_bg( parser, job_get_from_pid( pid ), *argv);
			}
		}
	}
	
	return res;
}


/**
   Builtin for looping over a list
*/
static int builtin_for( parser_t &parser, wchar_t **argv )
{
	int argc = builtin_count_args( argv );
	int res=STATUS_BUILTIN_ERROR;


	if( argc < 3)
	{
		sb_printf( sb_err,
				   BUILTIN_FOR_ERR_COUNT,
				   argv[0] ,
				   argc );
		builtin_print_help( parser, argv[0], sb_err );
	}
	else if ( wcsvarname(argv[1]) )
	{
		sb_printf( sb_err,
				   BUILTIN_FOR_ERR_NAME,
				   argv[0],
				   argv[1] );
		builtin_print_help( parser, argv[0], sb_err );
	}
	else if (wcscmp( argv[2], L"in") != 0 )
	{
		sb_printf( sb_err,
				   BUILTIN_FOR_ERR_IN,
				   argv[0] );
		builtin_print_help( parser, argv[0], sb_err );
	}
	else
	{
		res=0;
	}


	if( res )
	{
		parser.push_block( FAKE );
	}
	else
	{
		parser.push_block( FOR );

		int i;
        const wcstring for_variable = argv[1];
		parser.current_block->tok_pos = parser.get_pos();
        parser.current_block->state1<wcstring>() = for_variable;

        wcstring_list_t &for_vars = parser.current_block->state2<wcstring_list_t>();
		for( i=argc-1; i>3; i-- )
            for_vars.push_back(argv[i]);

		if( argc > 3 )
		{
			env_set( for_variable.c_str(), argv[3], ENV_LOCAL );
		}
		else
		{
			parser.current_block->skip=1;
		}
	}
	return res;
}

/**
   The begin builtin. Creates a nex block.
*/
static int builtin_begin( parser_t &parser, wchar_t **argv )
{
	parser.push_block( BEGIN );
	parser.current_block->tok_pos = parser.get_pos();
	return proc_get_last_status();
}


/**
   Builtin for ending a block of code, such as a for-loop or an if statement.

   The end command is whare a lot of the block-level magic happens.
*/
static int builtin_end( parser_t &parser, wchar_t **argv )
{
	if( !parser.current_block->outer )
	{
		sb_printf( sb_err,
				   _( L"%ls: Not inside of block\n" ),
				   argv[0] );

		builtin_print_help( parser, argv[0], sb_err );
		return STATUS_BUILTIN_ERROR;
	}
	else
	{
		/**
		   By default, 'end' kills the current block scope. But if we
		   are rewinding a loop, this should be set to false, so that
		   variables in the current loop scope won't die between laps.
		*/
		int kill_block = 1;

		switch( parser.current_block->type )
		{
			case WHILE:
			{
				/*
				  If this is a while loop, we rewind the loop unless
				  it's the last lap, in which case we continue.
				*/
				if( !( parser.current_block->skip && (parser.current_block->loop_status != LOOP_CONTINUE )))
				{
					parser.current_block->loop_status = LOOP_NORMAL;
					parser.current_block->skip = 0;
					kill_block = 0;
					parser.set_pos( parser.current_block->tok_pos);
                    parser.current_block->state1<int>() = WHILE_TEST_AGAIN;
				}

				break;
			}

			case IF:
			case SUBST:
			case BEGIN:
				/*
				  Nothing special happens at the end of these commands. The scope just ends.
				*/

				break;

			case FOR:
			{
				/*
				  set loop variable to next element, and rewind to the beginning of the block.
				*/
                wcstring_list_t &for_vars = parser.current_block->state2<wcstring_list_t>();
				if( parser.current_block->loop_status == LOOP_BREAK )
				{
                    for_vars.clear();
				}

				if( ! for_vars.empty() )
				{
                    const wcstring val = for_vars.back();
                    for_vars.pop_back();
                    const wcstring for_variable = parser.current_block->state1<wcstring>();
					env_set( for_variable.c_str(), val.c_str(),  ENV_LOCAL);
					parser.current_block->loop_status = LOOP_NORMAL;
					parser.current_block->skip = 0;
					
					kill_block = 0;
					parser.set_pos( parser.current_block->tok_pos );
				}
				break;
			}

			case FUNCTION_DEF:
			{
				
				function_data_t *d = parser.current_block->function_data.get();
				
				if( d )
				{
					/**
					   Copy the text from the beginning of the function
					   until the end command and use as the new definition
					   for the specified function
					*/

					wchar_t *def = wcsndup( parser.get_buffer()+parser.current_block->tok_pos,
											parser.get_job_pos()-parser.current_block->tok_pos );
					d->definition = def;
		
					function_add( d, parser );	
					free( def );
				}
				else
				{
					debug(0, 
						  _(L"%ls: Missing function definition information."),
						  argv[0] );
					bugreport();
				}

			}
			break;

		}
		if( kill_block )
		{
			parser.pop_block();
		}

		/*
		  If everything goes ok, return status of last command to execute.
		*/
		return proc_get_last_status();
	}
}

/**
   Builtin for executing commands if an if statement is false
*/
static int builtin_else( parser_t &parser, wchar_t **argv )
{
	if( parser.current_block == 0 ||
		parser.current_block->type != IF ||
		parser.current_block->state1<int>() != 1)
	{
		sb_printf( sb_err,
				   _( L"%ls: Not inside of 'if' block\n" ),
				   argv[0] );
		builtin_print_help( parser, argv[0], sb_err );
		return STATUS_BUILTIN_ERROR;
	}
	else
	{
        int &if_state = parser.current_block->state1<int>();
        if_state++;
		parser.current_block->skip = !parser.current_block->skip;
		env_pop();
		env_push(0);
	}

	/*
	  If everything goes ok, return status of last command to execute.
	*/
	return proc_get_last_status();
}

/**
   This function handles both the 'continue' and the 'break' builtins
   that are used for loop control.
*/
static int builtin_break_continue( parser_t &parser, wchar_t **argv )
{
	int is_break = (wcscmp(argv[0],L"break")==0);
	int argc = builtin_count_args( argv );

	block_t *b = parser.current_block;

	if( argc != 1 )
	{
		sb_printf( sb_err,
				   BUILTIN_ERR_UNKNOWN,
				   argv[0],
				   argv[1] );

		builtin_print_help( parser, argv[0], sb_err );
		return STATUS_BUILTIN_ERROR;
	}


	while( (b != 0) &&
		   ( b->type != WHILE) &&
		   (b->type != FOR ) )
	{
		b = b->outer;
	}

	if( b == 0 )
	{
		sb_printf( sb_err,
				   _( L"%ls: Not inside of loop\n" ),
				   argv[0] );
		builtin_print_help( parser, argv[0], sb_err );
		return STATUS_BUILTIN_ERROR;
	}

	b = parser.current_block;
	while( ( b->type != WHILE) &&
		   (b->type != FOR ) )
	{
		b->skip=1;
		b = b->outer;
	}
	b->skip=1;
	b->loop_status = is_break?LOOP_BREAK:LOOP_CONTINUE;
	return STATUS_BUILTIN_OK;
}

/**
   Implementation of the builtin count command, used to launch the
   interactive debugger.
 */

static int builtin_breakpoint( parser_t &parser, wchar_t **argv )
{
	parser.push_block( BREAKPOINT );		
	
	reader_read( STDIN_FILENO, real_io );
	
	parser.pop_block();		
	
	return proc_get_last_status();
}


/**
   Function for handling the \c return builtin
*/
static int builtin_return( parser_t &parser, wchar_t **argv )
{
	int argc = builtin_count_args( argv );
	int status = proc_get_last_status();

	block_t *b = parser.current_block;

	switch( argc )
	{
		case 1:
			break;
		case 2:
		{
			wchar_t *end;
			errno = 0;
			status = wcstol(argv[1],&end,10);
			if( errno || *end != 0)
			{
				sb_printf( sb_err,
						   _( L"%ls: Argument '%ls' must be an integer\n" ),
						   argv[0],
						   argv[1] );
				builtin_print_help( parser, argv[0], sb_err );
				return STATUS_BUILTIN_ERROR;
			}
			break;
		}
		default:
			sb_printf( sb_err,
					   _( L"%ls: Too many arguments\n" ),
					   argv[0] );
			builtin_print_help( parser, argv[0], sb_err );
			return STATUS_BUILTIN_ERROR;
	}


	while( (b != 0) &&
		   ( b->type != FUNCTION_CALL && 
		     b->type != FUNCTION_CALL_NO_SHADOW) )
	{
		b = b->outer;
	}

	if( b == 0 )
	{
		sb_printf( sb_err,
				   _( L"%ls: Not inside of function\n" ),
				   argv[0] );
		builtin_print_help( parser, argv[0], sb_err );
		return STATUS_BUILTIN_ERROR;
	}

	b = parser.current_block;
	while( ( b->type != FUNCTION_CALL &&
		 b->type != FUNCTION_CALL_NO_SHADOW ) )
	{
		b->type = FAKE;
		b->skip=1;
		b = b->outer;
	}
	b->skip=1;

	return status;
}

/**
   Builtin for executing one of several blocks of commands depending
   on the value of an argument.
*/
static int builtin_switch( parser_t &parser, wchar_t **argv )
{
	int res=STATUS_BUILTIN_OK;
	int argc = builtin_count_args( argv );

	if( argc != 2 )
	{
		sb_printf( sb_err,
				   _( L"%ls: Expected exactly one argument, got %d\n" ),
				   argv[0],
				   argc-1 );

		builtin_print_help( parser, argv[0], sb_err );
		res=1;
		parser.push_block( FAKE );
	}
	else
	{
		parser.push_block( SWITCH );
		parser.current_block->state1<wcstring>() = argv[1];
		parser.current_block->skip=1;
		parser.current_block->state2<int>() = 0;
	}
	
	return res;
}

/**
   Builtin used together with the switch builtin for conditional
   execution
*/
static int builtin_case( parser_t &parser, wchar_t **argv )
{
	int argc = builtin_count_args( argv );
	int i;
	wchar_t *unescaped=0;
	
	if( parser.current_block->type != SWITCH )
	{
		sb_printf( sb_err,
				   _( L"%ls: 'case' command while not in switch block\n" ),
				   argv[0] );
		builtin_print_help( parser, argv[0], sb_err );
		return STATUS_BUILTIN_ERROR;
	}
	
	parser.current_block->skip = 1;
	
	if( parser.current_block->state2<int>() )
	{
		return STATUS_BUILTIN_OK;
	}
	
	for( i=1; i<argc; i++ )
	{
		int match;
		
		unescaped = parse_util_unescape_wildcards( argv[i] );
        const wcstring &switch_value = parser.current_block->state1<wcstring>();
		match = wildcard_match( switch_value, unescaped );
		free( unescaped );
		
		if( match )
		{
			parser.current_block->skip = 0;
			parser.current_block->state2<int>() = 1;
			break;
		}
	}
	
	return STATUS_BUILTIN_OK;
}


/*
  END OF BUILTIN COMMANDS
  Below are functions for handling the builtin commands.
  THESE MUST BE SORTED BY NAME! Completion lookup uses binary search.
*/

/**
   Data about all the builtin commands in fish.
   Functions that are bound to builtin_generic are handled directly by the parser.
*/
static const builtin_data_t builtin_datas[]=
{
	{ 		L".",  &builtin_source, N_( L"Evaluate contents of file" )   },
	{ 		L"and",  &builtin_generic, N_( L"Execute command if previous command suceeded" )  },
	{ 		L"begin",  &builtin_begin, N_( L"Create a block of code" )   },
	{ 		L"bg",  &builtin_bg, N_( L"Send job to background" )   },
	{ 		L"bind",  &builtin_bind, N_( L"Handle fish key bindings" )  },
	{ 		L"block",  &builtin_block, N_( L"Temporarily block delivery of events" ) },
	{ 		L"break",  &builtin_break_continue, N_( L"Stop the innermost loop" )   },
	{ 		L"breakpoint",  &builtin_breakpoint, N_( L"Temporarily halt execution of a script and launch an interactive debug prompt" )   },
	{ 		L"builtin",  &builtin_builtin, N_( L"Run a builtin command instead of a function" ) },
	{ 		L"case",  &builtin_case, N_( L"Conditionally execute a block of commands" )   },
	{ 		L"cd",  &builtin_cd, N_( L"Change working directory" )   },
	{ 		L"command",   &builtin_generic, N_( L"Run a program instead of a function or builtin" )   },
	{ 		L"commandline",  &builtin_commandline, N_( L"Set or get the commandline" )   },
	{ 		L"complete",  &builtin_complete, N_( L"Edit command specific completions" )   },
	{ 		L"contains",  &builtin_contains, N_( L"Search for a specified string in a list" )   },
	{ 		L"continue",  &builtin_break_continue, N_( L"Skip the rest of the current lap of the innermost loop" )   },
	{ 		L"count",  &builtin_count, N_( L"Count the number of arguments" )   },
	{ 		L"else",  &builtin_else, N_( L"Evaluate block if condition is false" )   },
	{ 		L"emit",  &builtin_emit, N_( L"Emit an event" ) },
	{ 		L"end",  &builtin_end, N_( L"End a block of commands" )   },
	{ 		L"exec",  &builtin_generic, N_( L"Run command in current process" )  },
	{ 		L"exit",  &builtin_exit, N_( L"Exit the shell" ) },
	{ 		L"fg",  &builtin_fg, N_( L"Send job to foreground" )   },
	{ 		L"for",  &builtin_for, N_( L"Perform a set of commands multiple times" )   },
	{ 		L"function",  &builtin_function, N_( L"Define a new function" )   },
	{ 		L"functions",  &builtin_functions, N_( L"List or remove functions" )   },
	{ 		L"if",  &builtin_generic, N_( L"Evaluate block if condition is true" )   },
	{ 		L"jobs",  &builtin_jobs, N_( L"Print currently running jobs" )   },
	{ 		L"not",  &builtin_generic, N_( L"Negate exit status of job" )  },
	{ 		L"or",  &builtin_generic, N_( L"Execute command if previous command failed" )  },
	{ 		L"random",  &builtin_random, N_( L"Generate random number" )  },
	{ 		L"read",  &builtin_read, N_( L"Read a line of input into variables" )   },
	{ 		L"return",  &builtin_return, N_( L"Stop the currently evaluated function" )   },
	{ 		L"set",  &builtin_set, N_( L"Handle environment variables" )   },
	{ 		L"status",  &builtin_status, N_( L"Return status information about fish" )  },
	{ 		L"switch",  &builtin_switch, N_( L"Conditionally execute a block of commands" )   },
	{ 		L"ulimit",  &builtin_ulimit, N_( L"Set or get the shells resource usage limits" )  },
	{ 		L"while",  &builtin_generic, N_( L"Perform a command multiple times" )   }
};

#define BUILTIN_COUNT (sizeof builtin_datas / sizeof *builtin_datas)

static const builtin_data_t *builtin_lookup(const wcstring &name) {
    const builtin_data_t *array_end = builtin_datas + BUILTIN_COUNT;
    const builtin_data_t *found = std::lower_bound(builtin_datas, array_end, name);
    if (found != array_end && name == found->name) {
        return found;
    } else {
        return NULL;
    }
}

void builtin_init()
{
	
	wopterr = 0;
	al_init( &io_stack );

	for( size_t i=0; i < BUILTIN_COUNT; i++ )
	{
		intern_static( builtin_datas[i].name );
	}
}

void builtin_destroy()
{
	al_destroy( &io_stack );
}

int builtin_exists( const wcstring &cmd )
{
	return !!builtin_lookup(cmd);
}

/**
   Return true if the specified builtin should handle it's own help,
   false otherwise.
*/
static int internal_help( const wchar_t *cmd )
{
	CHECK( cmd, 0 );
	return contains( cmd, L"for", L"while", L"function",
			 L"if", L"end", L"switch", L"count" );
}


int builtin_run( parser_t &parser, const wchar_t * const *argv, io_data_t *io )
{
	int (*cmd)(parser_t &parser, const wchar_t * const *argv)=0;
	real_io = io;
		
	CHECK( argv, STATUS_BUILTIN_ERROR );
	CHECK( argv[0], STATUS_BUILTIN_ERROR );
    
    const builtin_data_t *data = builtin_lookup(argv[0]);
	cmd = (int (*)(parser_t &parser, const wchar_t * const*))(data ? data->func : NULL);
	
	if( argv[1] != 0 && !internal_help(argv[0]) )
	{
		if( argv[2] == 0 && (parser.is_help( argv[1], 0 ) ) )
		{
			builtin_print_help( parser, argv[0], sb_out );
			return STATUS_BUILTIN_OK;
		}
	}

	if( data != NULL )
	{
		int status;

		status = cmd(parser, argv);
		return status;

	}
	else
	{
		debug( 0, _( L"Unknown builtin '%ls'" ), argv[0] );
	}
	return STATUS_BUILTIN_ERROR;
}


wcstring_list_t builtin_get_names(void)
{
    wcstring_list_t result;
    result.reserve(BUILTIN_COUNT);
    for (size_t i=0; i < BUILTIN_COUNT; i++) {
        result.push_back(builtin_datas[i].name);
    }
    return result;
}

void builtin_get_names(std::vector<completion_t> &list) {
	for (size_t i=0; i < BUILTIN_COUNT; i++) {
		list.push_back(completion_t(builtin_datas[i].name));
	}
}

const wchar_t *builtin_get_desc( const wchar_t *name )
{
	CHECK( name, 0 );
	const builtin_data_t *builtin = builtin_lookup(name);
    return builtin ? _(builtin->desc) : NULL;
}

void builtin_push_io( parser_t &parser, int in )
{
	if( builtin_stdin != -1 )
	{
		al_push_long( &io_stack, (long)builtin_stdin );
		al_push( &io_stack, sb_out );
		al_push( &io_stack, sb_err );
	}
	builtin_stdin = in;
	sb_out = (string_buffer_t *)malloc(sizeof(string_buffer_t));
	sb_err = (string_buffer_t *)malloc(sizeof(string_buffer_t));
	sb_init( sb_out );
	sb_init( sb_err );
}

void builtin_pop_io(parser_t &parser)
{
	builtin_stdin = 0;
	sb_destroy( sb_out );
	sb_destroy( sb_err );
	free( sb_out);
	free(sb_err);

	if( al_get_count( &io_stack ) >0 )
	{
		sb_err = (string_buffer_t *)al_pop( &io_stack );
		sb_out = (string_buffer_t *)al_pop( &io_stack );
		builtin_stdin = (int)al_pop_long( &io_stack );
	}
	else
	{
		sb_out = sb_err = 0;
		builtin_stdin = 0;
	}
}

