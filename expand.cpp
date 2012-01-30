/**\file expand.c

String expansion functions. These functions perform several kinds of
parameter expansion. 

*/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <wctype.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <termios.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include <assert.h>
#include <vector>

#ifdef SunOS
#include <procfs.h>
#endif

#include "fallback.h"
#include "util.h"

#include "common.h"
#include "wutil.h"
#include "env.h"
#include "proc.h"
#include "parser.h"
#include "expand.h"
#include "wildcard.h"
#include "exec.h"
#include "signal.h"
#include "tokenizer.h"
#include "complete.h"

#include "parse_util.h"
#include "halloc.h"
#include "halloc_util.h"

/**
   Error issued on invalid variable name
*/
#define COMPLETE_VAR_DESC _( L"The '$' character begins a variable name. The character '%lc', which directly followed a '$', is not allowed as a part of a variable name, and variable names may not be zero characters long. To learn more about variable expansion in fish, type 'help expand-variable'.")

/**
   Error issued on invalid variable name
*/
#define COMPLETE_VAR_NULL_DESC _( L"The '$' begins a variable name. It was given at the end of an argument. Variable names may not be zero characters long. To learn more about variable expansion in fish, type 'help expand-variable'.")

/**
   Error issued on invalid variable name
*/
#define COMPLETE_VAR_BRACKET_DESC _( L"Did you mean %ls{$%ls}%ls? The '$' character begins a variable name. A bracket, which directly followed a '$', is not allowed as a part of a variable name, and variable names may not be zero characters long. To learn more about variable expansion in fish, type 'help expand-variable'." )

/**
   Error issued on invalid variable name
*/
#define COMPLETE_VAR_PARAN_DESC _( L"Did you mean (COMMAND)? In fish, the '$' character is only used for accessing variables. To learn more about command substitution in fish, type 'help expand-command-substitution'.")

/**
   Description for child process
*/
#define COMPLETE_CHILD_PROCESS_DESC _( L"Child process")

/**
   Description for non-child process
*/
#define COMPLETE_PROCESS_DESC _( L"Process")

/**
   Description for long job
*/
#define COMPLETE_JOB_DESC _( L"Job")

/**
   Description for short job. The job command is concatenated
*/
#define COMPLETE_JOB_DESC_VAL _( L"Job: %ls")

/**
   Description for the shells own pid
*/
#define COMPLETE_SELF_DESC _( L"Shell process")

/**
   Description for the shells own pid
*/
#define COMPLETE_LAST_DESC _( L"Last background job")

/**
   String in process expansion denoting ourself
*/
#define SELF_STR L"self"

/**
   String in process expansion denoting last background job
*/
#define LAST_STR L"last"

/**
   Characters which make a string unclean if they are the first
   character of the string. See \c expand_is_clean().
*/
#define UNCLEAN_FIRST L"~%"
/**
   Unclean characters. See \c expand_is_clean().
*/
#define UNCLEAN L"$*?\\\"'({})"

/* An adapter class for calling functions that expect a wchar_t * and array_list_t */
class wcstring_adapter
{
private:
    std::vector<wcstring> *outputs;
public:
    wchar_t *str;
    array_list_t lst;
    
    wcstring_adapter(const wcstring &in, std::vector<wcstring> &outs)
    {
        outputs = &outs;
        str = wcsdup(in.c_str());
        al_init(&lst);
    }
    
    ~wcstring_adapter()
    {
        int i, max = al_get_count(&lst);
        for (i=0; i < max; i++) {
            wchar_t *tmp = (wchar_t *)al_get(&lst, i);
            outputs->push_back(tmp);
            free(tmp);
        }
        al_destroy(&lst);
        //str is free'd by the function we called
    }
};

int expand_is_clean( const wchar_t *in )
{

	const wchar_t * str = in;

	CHECK( in, 1 );

	/*
	  Test characters that have a special meaning in the first character position
	*/
	if( wcschr( UNCLEAN_FIRST, *str ) )
		return 0;

	/*
	  Test characters that have a special meaning in any character position
	*/
	while( *str )
	{
		if( wcschr( UNCLEAN, *str ) )
			return 0;
		str++;
	}

	return 1;
}

/**
   Return the environment variable value for the string starting at \c in.
*/
static const wchar_t* expand_var( wchar_t *in )
{
	if( !in )
		return 0;
	return env_get( in );
}

/**
   Test if the specified string does not contain character which can
   not be used inside a quoted string.
*/
static int is_quotable( wchar_t *str )
{
	switch( *str )
	{
		case 0:
			return 1;

		case L'\n':
		case L'\t':
		case L'\r':
		case L'\b':
		case L'\x1b':
			return 0;

		default:
			return is_quotable(str+1);
	}
	return 0;

}

wchar_t *expand_escape_variable( const wchar_t *in )
{

	array_list_t l;
	string_buffer_t buff;

	CHECK( in, 0 );

	al_init( &l );
	tokenize_variable_array( in, &l );
	sb_init( &buff );

	switch( al_get_count( &l) )
	{
		case 0:
			sb_append( &buff, L"''");
			break;
			
		case 1:
		{
			wchar_t *el = (wchar_t *)al_get( &l, 0 );

			if( wcschr( el, L' ' ) && is_quotable( el ) )
			{
				sb_append( &buff,
							L"'",
							el,
							L"'",
							NULL );
			}
			else
			{
				wchar_t *val = escape( el, 1 );
				sb_append( &buff, val );
				free( val );
			}
			free( el );
			break;
		}
		default:
		{
			int j;
			for( j=0; j<al_get_count( &l ); j++ )
			{
				wchar_t *el = (wchar_t *)al_get( &l, j );
				if( j )
					sb_append( &buff, L"  " );

				if( is_quotable( el ) )
				{
					sb_append( &buff,
								L"'",
								el,
								L"'",
								NULL );
				}
				else
				{
					wchar_t *val = escape( el, 1 );
					sb_append( &buff, val );
					free( val );
				}

				free( el );
			}
		}
	}
	al_destroy( &l );

	return (wchar_t *)buff.buff;
}

wcstring expand_escape_variable2( const wcstring &in ) {
    wchar_t *tmp = expand_escape_variable(in.c_str());
    wcstring result(tmp ? tmp : L"");
    free(tmp);
    return result;
}

/**
   Tests if all characters in the wide string are numeric
*/
static int iswnumeric( const wchar_t *n )
{
	for( ; *n; n++ )
	{
		if( *n < L'0' || *n > L'9' )
		{
			return 0;
		}
	}
	return 1;
}

/**
   See if the process described by \c proc matches the commandline \c
   cmd
*/
static int match_pid( const wchar_t *cmd,
					  const wchar_t *proc,
					  int flags,
					  int *offset)
{
	/* Test for direct match */
	
	if( wcsncmp( cmd, proc, wcslen( proc ) ) == 0 )
	{
		if( offset )
			*offset = 0;
		return 1;
	}
	
	/*
	  Test if the commandline is a path to the command, if so we try
	  to match against only the command part
	*/
	wchar_t *first_token = tok_first( cmd );
	if( first_token == 0 )
		return 0;

	wchar_t *start=0;
	wchar_t prev=0;
	wchar_t *p;

	/*
	  This should be done by basename(), if it wasn't for the fact
	  that is does not accept wide strings
	*/
	for( p=first_token; *p; p++ )
	{
		if( *p == L'/' && prev != L'\\' )
			start = p;
		prev = *p;
	}
	if( start )
	{

		if( wcsncmp( start+1, proc, wcslen( proc ) ) == 0 )
		{
			if( offset )
				*offset = start+1-first_token;
			
			free( first_token );

			return 1;
		}
	}
	free( first_token );

	return 0;

}

/**
   Searches for a job with the specified job id, or a job or process
   which has the string \c proc as a prefix of its commandline.

   If the ACCEPT_INCOMPLETE flag is set, the remaining string for any matches
   are inserted.

   Otherwise, any job matching the specified string is matched, and
   the job pgid is returned. If no job matches, all child processes
   are searched. If no child processes match, and <tt>fish</tt> can
   understand the contents of the /proc filesystem, all the users
   processes are searched for matches.
*/

static int find_process( const wchar_t *proc,
						 int flags,
						 std::vector<completion_t> &out )
{
	DIR *dir;
	wchar_t *pdir_name;
	wchar_t *pfile_name;
	wchar_t *cmd=0;
	int sz=0;
	int found = 0;
	wchar_t *result;

	job_t *j;

	if( iswnumeric(proc) || (wcslen(proc)==0) )
	{
		/*
		  This is a numeric job string, like '%2'
		*/

		if( flags & ACCEPT_INCOMPLETE )
		{
            job_iterator_t jobs;
            while ((j = jobs.next()))
			{
				wchar_t jid[16];
				if( j->command.size() == 0 )
					continue;

				swprintf( jid, 16, L"%d", j->job_id );

				if( wcsncmp( proc, jid, wcslen(proc ) )==0 )
				{
					string_buffer_t desc_buff;
					
					sb_init( &desc_buff );
					
					sb_printf( &desc_buff, 
							   COMPLETE_JOB_DESC_VAL,
							   j->command_cstr() );
					
					completion_allocate( out, 
										 jid+wcslen(proc),
										 (wchar_t *)desc_buff.buff,
										 0 );

					sb_destroy( &desc_buff );
				}
			}

		}
		else
		{

			int jid;
			wchar_t *end;
			
			errno = 0;
			jid = wcstol( proc, &end, 10 );
			if( jid > 0 && !errno && !*end )
			{
				j = job_get( jid );
				if( (j != 0) && (j->command_cstr() != 0 ) )
				{
					
					{
						result = (wchar_t *)malloc(sizeof(wchar_t)*16 );
						swprintf( result, 16, L"%d", j->pgid );
						completion_t data_to_push;
						data_to_push.completion = result;
						out.push_back( data_to_push);
						found = 1;
					}
				}
			}
		}
	}
	if( found )
		return 1;

    job_iterator_t jobs;
    while ((j = jobs.next()))
	{
		int offset;
		
		if( j->command_cstr() == 0 )
			continue;
		
		if( match_pid( j->command_cstr(), proc, flags, &offset ) )
		{
			if( flags & ACCEPT_INCOMPLETE )
			{
				completion_allocate( out, 
									 j->command_cstr() + offset + wcslen(proc),
									 COMPLETE_JOB_DESC,
									 0 );
			}
			else
			{
				result = (wchar_t *)malloc(sizeof(wchar_t)*16 );
				swprintf( result, 16, L"%d", j->pgid );
				completion_t data_to_push;
				data_to_push.completion = result;
				out.push_back( data_to_push);
				found = 1;
			}
		}
	}

	if( found )
	{
		return 1;
	}

    jobs.reset();
    while ((j = jobs.next()))
	{
		process_t *p;
		if( j->command.size() == 0 )
			continue;
		for( p=j->first_process; p; p=p->next )
		{
			int offset;
			
			if( p->actual_cmd == 0 )
				continue;

			if( match_pid( p->actual_cmd, proc, flags, &offset ) )
			{
				if( flags & ACCEPT_INCOMPLETE )
				{
					completion_allocate( out, 
										 p->actual_cmd + offset + wcslen(proc),
										 COMPLETE_CHILD_PROCESS_DESC,
										 0 );
				}
				else
				{
					result = (wchar_t *)malloc(sizeof(wchar_t)*16 );
					swprintf( result, 16, L"%d", p->pid );
					completion_t data_to_push;
					data_to_push.completion = result;
					out.push_back( data_to_push );
					found = 1;
				}
			}
		}
	}

	if( found )
	{
		return 1;
	}

	if( !(dir = opendir( "/proc" )))
	{
		/*
		  This system does not have a /proc filesystem.
		*/
		return 1;
	}

	pdir_name = (wchar_t *)malloc( sizeof(wchar_t)*256 );
	pfile_name = (wchar_t *)malloc( sizeof(wchar_t)*64 );
	wcscpy( pdir_name, L"/proc/" );
	
    wcstring nameStr;
    while (wreaddir(dir, nameStr))
	{
		const wchar_t *name = nameStr.c_str();
		struct stat buf;

		if( !iswnumeric( name ) )
			continue;

		wcscpy( pdir_name + 6, name );
		if( wstat( pdir_name, &buf ) )
		{
			continue;
		}
		if( buf.st_uid != getuid() )
		{
			continue;
		}
		wcscpy( pfile_name, pdir_name );
		wcscat( pfile_name, L"/cmdline" );

		if( !wstat( pfile_name, &buf ) )
		{
			/*
			  the 'cmdline' file exists, it should contain the commandline
			*/
			FILE *cmdfile;

			if((cmdfile=wfopen( pfile_name, "r" ))==0)
			{
				wperror( L"fopen" );
				continue;
			}

			signal_block();
			fgetws2( &cmd, &sz, cmdfile );
			signal_unblock();
			
			fclose( cmdfile );
		}
		else
		{
#ifdef SunOS
			wcscpy( pfile_name, pdir_name );
			wcscat( pfile_name, L"/psinfo" );
			if( !wstat( pfile_name, &buf ) )
			{
				psinfo_t info;

				FILE *psfile;

				if((psfile=wfopen( pfile_name, "r" ))==0)
				{
					wperror( L"fopen" );
					continue;
				}

				if( fread( &info, sizeof(info), 1, psfile ) )
				{
					if( cmd != 0 )
						free( cmd );
					cmd = str2wcs( info.pr_fname );
				}
				fclose( psfile );
			}
			else
#endif
			{
				if( cmd != 0 )
				{
					*cmd = 0;
				}
			}
		}

		if( cmd != 0 )
		{
			int offset;
			
			if( match_pid( cmd, proc, flags, &offset ) )
			{
				if( flags & ACCEPT_INCOMPLETE )
				{
					completion_allocate( out, 
										 cmd + offset + wcslen(proc),
										 COMPLETE_PROCESS_DESC,
										 0 );
				}
				else
				{
					wchar_t *res = wcsdup(name);
					if( res ) {
						completion_t data_to_push;
						data_to_push.completion = res;
						out.push_back( data_to_push );
					}
				}
			}
		}
	}

	if( cmd != 0 )
		free( cmd );

	free( pdir_name );
	free( pfile_name );

	closedir( dir );

	return 1;
}

/**
   Process id expansion
*/
static int expand_pid( wchar_t *in,
					   int flags,
					   std::vector<completion_t> &out )
{

	CHECK( in, 0 );
//	CHECK( out, 0 );
	
	if( *in != PROCESS_EXPAND )
	{
		completion_t data_to_push;
		data_to_push.completion = in;
		out.push_back( data_to_push );
		return 1;
	}

	if( flags & ACCEPT_INCOMPLETE )
	{
		if( wcsncmp( in+1, SELF_STR, wcslen(in+1) )==0 )
		{
			completion_allocate( out, 
								 SELF_STR+wcslen(in+1),
								 COMPLETE_SELF_DESC, 
								 0 );
		}
		else if( wcsncmp( in+1, LAST_STR, wcslen(in+1) )==0 )
		{
			completion_allocate( out, 
								 LAST_STR+wcslen(in+1), 
								 COMPLETE_LAST_DESC, 
								 0 );
		}
	}
	else
	{
		if( wcscmp( (in+1), SELF_STR )==0 )
		{
			wchar_t *str= (wchar_t *)malloc( sizeof(wchar_t)*32);
			free(in);
			swprintf( str, 32, L"%d", getpid() );
	
			completion_t data_to_push;
			data_to_push.completion = str;
			
			out.push_back( data_to_push );

			return 1;
		}
		if( wcscmp( (in+1), LAST_STR )==0 )
		{
			wchar_t *str;

			if( proc_last_bg_pid > 0 )
			{
				str = (wchar_t *)malloc( sizeof(wchar_t)*32);
				free(in);
				swprintf( str, 32, L"%d", proc_last_bg_pid );
				completion_t data_to_push;
				data_to_push.completion = str;
	
				out.push_back( data_to_push);
			}

			return 1;
		}
	}

	size_t prev = out.size();
	if( !find_process( in+1, flags, out ) )
		return 0;

	if( prev ==  out.size() )
	{
		if( flags & ACCEPT_INCOMPLETE )
			free( in );
		else
		{
			free(in);
			return 0;
		}
	}
	else
	{
		free( in );
	}

	return 1;
}
/*
static int expand_pid2( const wcstring &in, int flags, std::vector<completion_t> &outputs )
{
    wcstring_adapter adapter(in, outputs);
    return expand_pid(adapter.str, flags, &adapter.lst);
}
*/

void expand_variable_error( parser_t &parser, const wchar_t *token, int token_pos, int error_pos )
{
	int stop_pos = token_pos+1;
	
	switch( token[stop_pos] )
	{
		case BRACKET_BEGIN:
		{
			wchar_t *cpy = wcsdup( token );
			*(cpy+token_pos)=0;
			wchar_t *name = &cpy[stop_pos+1];
			wchar_t *end = wcschr( name, BRACKET_END );
			wchar_t *post;
			int is_var=0;
			if( end )
			{
				post = end+1;
				*end = 0;
				
				if( !wcsvarname( name ) )
				{
					is_var = 1;
				}
			}
			
			if( is_var )
			{
				parser.error( SYNTAX_ERROR,
					   error_pos,
					   COMPLETE_VAR_BRACKET_DESC,
					   cpy,
					   name,
					   post );				
			}
			else
			{
				parser.error( SYNTAX_ERROR,
					   error_pos,
					   COMPLETE_VAR_BRACKET_DESC,
					   L"",
					   L"VARIABLE",
					   L"" );
			}
			free( cpy );
			
			break;
		}
		
		case INTERNAL_SEPARATOR:
		{
			parser.error( SYNTAX_ERROR,
				   error_pos,
				   COMPLETE_VAR_PARAN_DESC );	
			break;
		}
		
		case 0:
		{
			parser.error( SYNTAX_ERROR,
				   error_pos,
				   COMPLETE_VAR_NULL_DESC );
			break;
		}
		
		default:
		{
			parser.error( SYNTAX_ERROR,
				   error_pos,
				   COMPLETE_VAR_DESC,
				   token[stop_pos] );
			break;
		}
	}
}

/**
   Parse an array slicing specification
 */
static int parse_slice( const wchar_t *in, wchar_t **end_ptr, array_list_t *idx )
{
	
	
	wchar_t *end;
	
	int pos = 1;

//	debug( 0, L"parse_slice on '%ls'", in );
	

	while( 1 )
	{
		long tmp;
		
		while( iswspace(in[pos]) || (in[pos]==INTERNAL_SEPARATOR))
			pos++;		
		
		if( in[pos] == L']' )
		{
			pos++;
			break;
		}
		
		errno=0;
		tmp = wcstol( &in[pos], &end, 10 );
		if( ( errno ) || ( end == &in[pos] ) )
		{
			return 1;
		}
//		debug( 0, L"Push idx %d", tmp );
		
		al_push_long( idx, tmp );
		pos = end-in;
	}
	
	if( end_ptr )
	{
//		debug( 0, L"Remainder is '%ls', slice def was %d characters long", in+pos, pos );
		
		*end_ptr = (wchar_t *)(in+pos);
	}
//	debug( 0, L"ok, done" );
	
	return 0;
}

static int parse_slice2( const wchar_t *in, wchar_t **end_ptr, std::vector<long> &idx )
{
	
	
	wchar_t *end;
	
	int pos = 1;
    
    //	debug( 0, L"parse_slice on '%ls'", in );
	
    
	while( 1 )
	{
		long tmp;
		
		while( iswspace(in[pos]) || (in[pos]==INTERNAL_SEPARATOR))
			pos++;		
		
		if( in[pos] == L']' )
		{
			pos++;
			break;
		}
		
		errno=0;
		tmp = wcstol( &in[pos], &end, 10 );
		if( ( errno ) || ( end == &in[pos] ) )
		{
			return 1;
		}
        //		debug( 0, L"Push idx %d", tmp );
		
        idx.push_back(tmp);
		pos = end-in;
	}
	
	if( end_ptr )
	{
        //		debug( 0, L"Remainder is '%ls', slice def was %d characters long", in+pos, pos );
		
		*end_ptr = (wchar_t *)(in+pos);
	}
    //	debug( 0, L"ok, done" );
	
	return 0;
}



/**
   Expand all environment variables in the string *ptr.

   This function is slow, fragile and complicated. There are lots of
   little corner cases, like $$foo should do a double expansion,
   $foo$bar should not double expand bar, etc. Also, it's easy to
   accidentally leak memory on array out of bounds errors an various
   other situations. All in all, this function should be rewritten,
   split out into multiple logical units and carefully tested. After
   that, it can probably be optimized to do fewer memory allocations,
   fewer string scans and overall just less work. But until that
   happens, don't edit it unless you know exactly what you are doing,
   and do proper testing afterwards.
*/
static int expand_variables( parser_t &parser, wchar_t *in, array_list_t *out, int last_idx )
{
	wchar_t c;
	wchar_t prev_char=0;
	int i, j;
	int is_ok= 1;
	int empty=0;

	static string_buffer_t *var_tmp = 0;
	static array_list_t *var_idx_list = 0;

	CHECK( in, 0 );
	CHECK( out, 0 );
	
	if( !var_tmp )
	{
		var_tmp = sb_halloc( global_context );
		if( !var_tmp )
			DIE_MEM();
	}
	else
	{
		sb_clear(var_tmp );
	}

	if( !var_idx_list )
	{
		var_idx_list = al_halloc( global_context );
		if( !var_idx_list )
			DIE_MEM();
	}
	else
	{
		al_truncate( var_idx_list, 0 );
	}

	for( i=last_idx; (i>=0) && is_ok && !empty; i-- )
	{
		c = in[i];
		if( ( c == VARIABLE_EXPAND ) || (c == VARIABLE_EXPAND_SINGLE ) )
		{
			int start_pos = i+1;
			int stop_pos;
			int var_len, new_len;
			const wchar_t * var_val;
			wchar_t * new_in;
			int is_single = (c==VARIABLE_EXPAND_SINGLE);
			int var_name_stop_pos;
			
			stop_pos = start_pos;

			while( 1 )
			{
				if( !(in[stop_pos ]) )
					break;
				if( !( iswalnum( in[stop_pos] ) ||
					   (wcschr(L"_", in[stop_pos])!= 0)  ) )
					break;

				stop_pos++;
			}
			var_name_stop_pos = stop_pos;
			
/*			printf( "Stop for '%c'\n", in[stop_pos]);*/

			var_len = stop_pos - start_pos;

			if( var_len == 0 )
			{
				expand_variable_error( parser, in, stop_pos-1, -1 );				
				
				is_ok = 0;
				break;
			}

			sb_append_substring( var_tmp, &in[start_pos], var_len );
			var_val = expand_var( (wchar_t *)var_tmp->buff );

			if( var_val )
			{
				int all_vars=1;
				array_list_t var_item_list;
				al_init( &var_item_list );

				if( in[stop_pos] == L'[' )
				{
					wchar_t *slice_end;
					all_vars=0;
					
					if( parse_slice( &in[stop_pos], &slice_end, var_idx_list ) )
					{
						parser.error( SYNTAX_ERROR,
							   -1,
							   L"Invalid index value" );						
						is_ok = 0;
					}					
					stop_pos = (slice_end-in);
				}				
					
				if( is_ok )
				{
					tokenize_variable_array( var_val, &var_item_list );
					if( !all_vars )
					{
						int j;
						for( j=0; j<al_get_count( var_idx_list ); j++)
						{
							long tmp = al_get_long( var_idx_list, j );
							if( tmp < 0 )
							{
								tmp = al_get_count( &var_item_list)+tmp+1;
							}

							/*
							  Check that we are within array
							  bounds. If not, truncate the list to
							  exit.
							*/
							if( tmp < 1 || tmp > al_get_count( &var_item_list ) )
							{
								parser.error( SYNTAX_ERROR,
									   -1,
									   ARRAY_BOUNDS_ERR );
								is_ok=0;
								al_truncate( var_idx_list, j );
								break;
							}
							else
							{
								/* Replace each index in var_idx_list inplace with the string value at the specified index */
								al_set( var_idx_list, j, wcsdup((const wchar_t *)al_get( &var_item_list, tmp-1 ) ) );
							}
						}
						/* Free strings in list var_item_list and truncate it */
						al_foreach( &var_item_list, &free );
						al_truncate( &var_item_list, 0 );
						/* Add items from list idx back to list l */
						al_push_all( &var_item_list, var_idx_list );
					}
				}

				if( is_ok )
				{
					
					if( is_single )
					{
						string_buffer_t res;
						in[i]=0;
						
						sb_init( &res );
						sb_append( &res, in );
						sb_append_char( &res, INTERNAL_SEPARATOR );

						for( j=0; j<al_get_count( &var_item_list); j++ )
						{
							wchar_t *next = (wchar_t *)al_get( &var_item_list, j );
							
							if( is_ok )
							{
								if( j != 0 )
									sb_append( &res, L" " );
								sb_append( &res, next );
							}
							free( next );
						}
						sb_append( &res, &in[stop_pos] );
						is_ok &= expand_variables( parser, (wchar_t *)res.buff, out, i );
					}
					else
					{
						for( j=0; j<al_get_count( &var_item_list); j++ )
						{
							wchar_t *next = (wchar_t *)al_get( &var_item_list, j );
							if( is_ok && (i == 0) && (!in[stop_pos]) )
							{
								al_push( out, next );
							}
							else
							{
								
								if( is_ok )
								{
									new_len = wcslen(in) - (stop_pos-start_pos+1);
									new_len += wcslen( next) +2;
									
									if( !(new_in = (wchar_t *)malloc( sizeof(wchar_t)*new_len )))
									{
										DIE_MEM();
									}
									else
									{
										
										wcslcpy( new_in, in, start_pos );

										if(start_pos>1 && new_in[start_pos-2]!=VARIABLE_EXPAND)
										{
											new_in[start_pos-1]=INTERNAL_SEPARATOR;
											new_in[start_pos]=L'\0';
										}
										else
											new_in[start_pos-1]=L'\0';
										
										wcscat( new_in, next );
										wcscat( new_in, &in[stop_pos] );
										
										is_ok &= expand_variables( parser, new_in, out, i );
									}
								}
								free( next );
							}
							
						}
					}
				}
				
				free(in);
				al_destroy( &var_item_list );
				return is_ok;
			}
			else
			{
				/*
				  Expand a non-existing variable
				*/
				if( c == VARIABLE_EXPAND )
				{
					/*
					  Regular expansion, i.e. expand this argument to nothing
					*/
					empty = 1;
				}
				else
				{
					/*
					  Expansion to single argument.
					*/
					string_buffer_t res;
					sb_init( &res );

					in[i]=0;

					sb_append( &res, in );
					sb_append( &res, &in[stop_pos] );

					is_ok &= expand_variables( parser, (wchar_t *)res.buff, out, i );
					free(in);
					return is_ok;
				}
			}


		}

		prev_char = c;
	}

	if( !empty )
	{
		al_push( out, in );
	}
	else
	{
		free( in );
	}

	return is_ok;
}

static int expand_variables2( parser_t &parser, wchar_t * in, std::vector<completion_t> &out, int last_idx )
{
	wchar_t c;
	wchar_t prev_char=0;
	int i, j;
	int is_ok= 1;
	int empty=0;
	
	static string_buffer_t *var_tmp = 0;
	static array_list_t *var_idx_list = 0;

	CHECK( in, 0 );
//	CHECK( out, 0 );
	
	if( !var_tmp )
	{
		var_tmp = sb_halloc( global_context );
		if( !var_tmp )
			DIE_MEM();
	}
	else
	{
		sb_clear(var_tmp );
	}

	if( !var_idx_list )
	{
		var_idx_list = al_halloc( global_context );
		if( !var_idx_list )
			DIE_MEM();
	}
	else
	{
		al_truncate( var_idx_list, 0 );
	}

	for( i=last_idx; (i>=0) && is_ok && !empty; i-- )
	{
		c = in[i];
		if( ( c == VARIABLE_EXPAND ) || (c == VARIABLE_EXPAND_SINGLE ) )
		{
			int start_pos = i+1;
			int stop_pos;
			int var_len, new_len;
			const wchar_t * var_val;
			wchar_t * new_in;
			int is_single = (c==VARIABLE_EXPAND_SINGLE);
			int var_name_stop_pos;
			
			stop_pos = start_pos;

			while( 1 )
			{
				if( !(in[stop_pos ]) )
					break;
				if( !( iswalnum( in[stop_pos] ) ||
					   (wcschr(L"_", in[stop_pos])!= 0)  ) )
					break;

				stop_pos++;
			}
			var_name_stop_pos = stop_pos;
			
/*			printf( "Stop for '%c'\n", in[stop_pos]);*/

			var_len = stop_pos - start_pos;

			if( var_len == 0 )
			{
				expand_variable_error( parser_t::principal_parser(), in, stop_pos-1, -1 );				
				
				is_ok = 0;
				break;
			}

			sb_append_substring( var_tmp, &in[start_pos], var_len );
			var_val = expand_var( (wchar_t *)var_tmp->buff );

			if( var_val )
			{
				int all_vars=1;
				array_list_t var_item_list;
				al_init( &var_item_list );

				if( in[stop_pos] == L'[' )
				{
					wchar_t *slice_end;
					all_vars=0;
					
					if( parse_slice( &in[stop_pos], &slice_end, var_idx_list ) )
					{
						parser_t::principal_parser().error( SYNTAX_ERROR,
							   -1,
							   L"Invalid index value" );						
						is_ok = 0;
					}					
					stop_pos = (slice_end-in);
				}				
					
				if( is_ok )
				{
					tokenize_variable_array( var_val, &var_item_list );
					if( !all_vars )
					{
						int j;
						for( j=0; j<al_get_count( var_idx_list ); j++)
						{
							long tmp = al_get_long( var_idx_list, j );
							if( tmp < 0 )
							{
								tmp = al_get_count( &var_item_list)+tmp+1;
							}

							/*
							  Check that we are within array
							  bounds. If not, truncate the list to
							  exit.
							*/
							if( tmp < 1 || tmp > al_get_count( &var_item_list ) )
							{
								parser_t::principal_parser().error( SYNTAX_ERROR,
									   -1,
									   ARRAY_BOUNDS_ERR );
								is_ok=0;
								al_truncate( var_idx_list, j );
								break;
							}
							else
							{
								/* Replace each index in var_idx_list inplace with the string value at the specified index */
								al_set( var_idx_list, j, wcsdup((const wchar_t *)al_get( &var_item_list, tmp-1 ) ) );
							}
						}
						/* Free strings in list var_item_list and truncate it */
						al_foreach( &var_item_list, &free );
						al_truncate( &var_item_list, 0 );
						/* Add items from list idx back to list l */
						al_push_all( &var_item_list, var_idx_list );
					}
				}

				if( is_ok )
				{
					
					if( is_single )
					{
						string_buffer_t res;
						in[i]=0;
						
						sb_init( &res );
						sb_append( &res, in );
						sb_append_char( &res, INTERNAL_SEPARATOR );

						for( j=0; j<al_get_count( &var_item_list); j++ )
						{
							wchar_t *next = (wchar_t *)al_get( &var_item_list, j );
							
							if( is_ok )
							{
								if( j != 0 )
									sb_append( &res, L" " );
								sb_append( &res, next );
							}
							free( next );
						}
						sb_append( &res, &in[stop_pos] );
						is_ok &= expand_variables2( parser_t::principal_parser(), (wchar_t *)res.buff, out, i );
					}
					else
					{
						for( j=0; j<al_get_count( &var_item_list); j++ )
						{
							wchar_t *next = (wchar_t *)al_get( &var_item_list, j );
							if( is_ok && (i == 0) && (!in[stop_pos]) )
							{
								completion_t data_to_push;
								data_to_push.completion = next; 
								out.push_back( data_to_push );
							}
							else
							{
								
								if( is_ok )
								{
									new_len = wcslen(in) - (stop_pos-start_pos+1);
									new_len += wcslen( next) +2;
									
									if( !(new_in = (wchar_t *)malloc( sizeof(wchar_t)*new_len )))
									{
										DIE_MEM();
									}
									else
									{
										
										wcslcpy( new_in, in, start_pos );

										if(start_pos>1 && new_in[start_pos-2]!=VARIABLE_EXPAND)
										{
											new_in[start_pos-1]=INTERNAL_SEPARATOR;
											new_in[start_pos]=L'\0';
										}
										else
											new_in[start_pos-1]=L'\0';
										
										wcscat( new_in, next );
										wcscat( new_in, &in[stop_pos] );
										
										is_ok &= expand_variables2( parser_t::principal_parser(), new_in, out, i );
									}
								}
								free( next );
							}
							
						}
					}
				}
				
				free(in);
				al_destroy( &var_item_list );
				return is_ok;
			}
			else
			{
				/*
				  Expand a non-existing variable
				*/
				if( c == VARIABLE_EXPAND )
				{
					/*
					  Regular expansion, i.e. expand this argument to nothing
					*/
					empty = 1;
				}
				else
				{
					/*
					  Expansion to single argument.
					*/
					string_buffer_t res;
					sb_init( &res );

					in[i]=0;

					sb_append( &res, in );
					sb_append( &res, &in[stop_pos] );

					is_ok &= expand_variables2( parser_t::principal_parser(), (wchar_t *)res.buff, out, i );
					free(in);
					return is_ok;
				}
			}


		}

		prev_char = c;
	}

	if( !empty )
	{
		completion_t data_to_push;
		data_to_push.completion = in;
		out.push_back( data_to_push );
	}
	else
	{
		free( in );
	}

	return is_ok;
}

/**
   Perform bracket expansion
*/
static int expand_brackets(parser_t &parser, wchar_t *in, int flags, std::vector<completion_t> &out )
{
	wchar_t *pos;
	int syntax_error=0;
	int bracket_count=0;

	wchar_t *bracket_begin=0, *bracket_end=0;
	wchar_t *last_sep=0;

	wchar_t *item_begin;
	int len1, len2, tot_len;

	CHECK( in, 0 );
//	CHECK( out, 0 );
	
	for( pos=in;
		 (*pos) && !syntax_error;
		 pos++ )
	{
		switch( *pos )
		{
			case BRACKET_BEGIN:
			{
				bracket_begin = pos;

				bracket_count++;
				break;

			}
			case BRACKET_END:
			{
				bracket_count--;
				if( bracket_end < bracket_begin )
				{
					bracket_end = pos;
				}
				
				if( bracket_count < 0 )
				{
					syntax_error = 1;
				}
				break;
			}
			case BRACKET_SEP:
			{
				if( bracket_count == 1 )
					last_sep = pos;
			}
		}
	}

	if( bracket_count > 0 )
	{
		if( !(flags & ACCEPT_INCOMPLETE) )
		{
			syntax_error = 1;
		}
		else
		{

			string_buffer_t mod;
			sb_init( &mod );
			if( last_sep )
			{
				sb_append_substring( &mod, in, bracket_begin-in+1 );
				sb_append( &mod, last_sep+1 );
				sb_append_char( &mod, BRACKET_END );
			}
			else
			{
				sb_append( &mod, in );
				sb_append_char( &mod, BRACKET_END );
			}

			return expand_brackets( parser, (wchar_t*)mod.buff, 1, out );
		}
	}

	if( syntax_error )
	{
		parser.error( SYNTAX_ERROR,
			   -1,
			   _(L"Mismatched brackets") );
		return 0;
	}

	if( bracket_begin == 0 )
	{
		completion_t data_to_push;
		data_to_push.completion = in;
		out.push_back( data_to_push );
		return 1;
	}

	len1 = (bracket_begin-in);
	len2 = wcslen(bracket_end)-1;
	tot_len = len1+len2;
	item_begin = bracket_begin+1;
	for( pos=(bracket_begin+1); 1; pos++ )
	{
		if( bracket_count == 0 )
		{
			if( (*pos == BRACKET_SEP) || (pos==bracket_end) )
			{
				wchar_t *whole_item;
				int item_len = pos-item_begin;

				whole_item = (wchar_t *)malloc( sizeof(wchar_t)*(tot_len + item_len + 1) );
				wcslcpy( whole_item, in, len1+1 );
				wcslcpy( whole_item+len1, item_begin, item_len+1 );
				wcscpy( whole_item+len1+item_len, bracket_end+1 );

				expand_brackets( parser, whole_item, flags, out );

				item_begin = pos+1;
				if( pos == bracket_end )
					break;
			}
		}

		if( *pos == BRACKET_BEGIN )
		{
			bracket_count++;
		}

		if( *pos == BRACKET_END )
		{
			bracket_count--;
		}
	}
	free(in);
	return 1;
}

/*
static int expand_brackets2( parser_t &parser, const wcstring &in, int flags, std::vector<wcstring> &outputs )
{
    wcstring_adapter adapter(in, outputs);
    return expand_brackets(parser, adapter.str, flags, &adapter.lst);
}
*/
/**
   Perform cmdsubst expansion
*/
static int expand_cmdsubst( parser_t &parser, wchar_t *in, array_list_t *out )
{
	wchar_t *paran_begin=0, *paran_end=0;
	int len1;
	wchar_t prev=0;
	wchar_t *subcmd;
	array_list_t *sub_res, *tail_expand;
	int i, j;
	const wchar_t *item_begin;
	wchar_t *tail_begin = 0;	
	void *context;

	CHECK( in, 0 );
	CHECK( out, 0 );
	


	switch( parse_util_locate_cmdsubst(in,
									   &paran_begin,
									   &paran_end,
									   0 ) )
	{
		case -1:
			parser.error( SYNTAX_ERROR,
				   -1,
				   L"Mismatched parans" );
			return 0;
		case 0:
			al_push( out, in );
			return 1;
		case 1:

			break;
	}

	context = halloc( 0, 0 );

	len1 = (paran_begin-in);
	prev=0;
	item_begin = paran_begin+1;

	sub_res = al_halloc( context );
	if( !(subcmd = (wchar_t *)halloc( context, sizeof(wchar_t)*(paran_end-paran_begin) )))
	{
		halloc_free( context );	
		return 0;
	}

	wcslcpy( subcmd, paran_begin+1, paran_end-paran_begin );
	subcmd[ paran_end-paran_begin-1]=0;

	if( exec_subshell( subcmd, sub_res) == -1 )
	{
		halloc_free( context );
		parser.error( CMDSUBST_ERROR, -1, L"Unknown error while evaulating command substitution" );
		return 0;
	}

	tail_begin = paran_end + 1;
	if( *tail_begin == L'[' )
	{
		array_list_t *slice_idx = al_halloc( context );
		wchar_t *slice_end;
		
		if( parse_slice( tail_begin, &slice_end, slice_idx ) )
		{
			halloc_free( context );	
			parser.error( SYNTAX_ERROR, -1, L"Invalid index value" );
			return 0;
		}
		else
		{
			array_list_t *sub_res2 = al_halloc( context );
			tail_begin = slice_end;
			for( i=0; i<al_get_count( slice_idx ); i++ )
			{
				long idx = al_get_long( slice_idx, i );
				if( idx < 0 )
				{
					idx = al_get_count( sub_res ) + idx + 1;
				}
				
				if( idx < 1 || idx > al_get_count( sub_res ) )
				{
					halloc_free( context );
					parser.error( SYNTAX_ERROR, -1, L"Invalid index value" );
					return 0;
				}
				
				idx = idx-1;
				
				al_push( sub_res2, al_get( sub_res, idx ) );
//				debug( 0, L"Pushing item '%ls' with index %d onto sliced result", al_get( sub_res, idx ), idx );
				
				al_set( sub_res, idx, 0 );
			}
			al_foreach( sub_res, &free );
			sub_res = sub_res2;			
		}
	}

	tail_expand = al_halloc( context );

	/*
	  Recursively call ourselves to expand any remaining command
	  substitutions. The result of this recursive call usiung the tail
	  of the string is inserted into the tail_expand array list
	*/
	expand_cmdsubst( parser, wcsdup(tail_begin), tail_expand );

	/*
	  Combine the result of the current command substitution with the
	  result of the recursive tail expansion
	*/
    for( i=0; i<al_get_count( sub_res ); i++ )
    {
        wchar_t *sub_item, *sub_item2;
        sub_item = (wchar_t *)al_get( sub_res, i );
        sub_item2 = escape( sub_item, 1 );
		free(sub_item);
        int item_len = wcslen( sub_item2 );

        for( j=0; j<al_get_count( tail_expand ); j++ )
        {
            string_buffer_t whole_item;
            wchar_t *tail_item = (wchar_t *)al_get( tail_expand, j );

            sb_init( &whole_item );

            sb_append_substring( &whole_item, in, len1 );
            sb_append_char( &whole_item, INTERNAL_SEPARATOR );
            sb_append_substring( &whole_item, sub_item2, item_len );
			sb_append_char( &whole_item, INTERNAL_SEPARATOR );
            sb_append( &whole_item, tail_item );
			
			al_push( out, whole_item.buff );
        }
		
        free( sub_item2 );
    }
	free(in);

	al_foreach( tail_expand, &free );
	halloc_free( context );	
	return 1;
}

/**
 Perform cmdsubst expansion
 */
static int expand_cmdsubst2( parser_t &parser, const wcstring &input, std::vector<completion_t> &outList )
{
	wchar_t *paran_begin=0, *paran_end=0;
	int len1;
	wchar_t prev=0;
    std::vector<wcstring> sub_res;
	size_t i, j;
	const wchar_t *item_begin;
	wchar_t *tail_begin = 0;	
    
    const wchar_t * const in = input.c_str();
    
	completion_t data_to_push;
	int parse_ret;
	switch( parse_ret = parse_util_locate_cmdsubst(in,
									   &paran_begin,
									   &paran_end,
									   0 ) )
	{
		case -1:
			parser.error( SYNTAX_ERROR,
                  -1,
                  L"Mismatched parans" );
			return 0;
		case 0:
			data_to_push.completion = input; 
           		outList.push_back(data_to_push);
			return 1;
		case 1:
            
			break;
	}
    
	len1 = (paran_begin-in);
	prev=0;
	item_begin = paran_begin+1;
    
    const wcstring subcmd(paran_begin + 1, paran_end-paran_begin - 1);
    
	if( exec_subshell2( subcmd, sub_res) == -1 )
	{
		parser.error( CMDSUBST_ERROR, -1, L"Unknown error while evaulating command substitution" );
		return 0;
	}
    
	tail_begin = paran_end + 1;
	if( *tail_begin == L'[' )
	{
        std::vector<long> slice_idx;
		wchar_t *slice_end;
		
		if( parse_slice2( tail_begin, &slice_end, slice_idx ) )
		{
			parser.error( SYNTAX_ERROR, -1, L"Invalid index value" );
			return 0;
		}
		else
		{
            std::vector<wcstring> sub_res2;
			tail_begin = slice_end;
			for( i=0; i < slice_idx.size(); i++ )
			{
				long idx = slice_idx.at(i);
				if( idx < 0 )
				{
					idx = sub_res.size() + idx + 1;
				}
				
				if( idx < 1 || (size_t)idx > sub_res.size() )
				{
					parser.error( SYNTAX_ERROR, -1, L"Invalid index value" );
					return 0;
				}
				
				idx = idx-1;
				
                sub_res2.push_back(sub_res.at(idx));
                //				debug( 0, L"Pushing item '%ls' with index %d onto sliced result", al_get( sub_res, idx ), idx );
				//sub_res[idx] = 0; // ??
			}
			sub_res = sub_res2;			
		}
	}
    
    
	/*
     Recursively call ourselves to expand any remaining command
     substitutions. The result of this recursive call using the tail
     of the string is inserted into the tail_expand array list
     */
    std::vector<completion_t> tail_expand;
	expand_cmdsubst2( parser, tail_begin, tail_expand );
    
	/*
     Combine the result of the current command substitution with the
     result of the recursive tail expansion
     */
    for( i=0; i<sub_res.size(); i++ )
    {
        wcstring sub_item = sub_res.at(i);
        wcstring sub_item2 = escape_string(sub_item, 1);
        
        for( j=0; j < tail_expand.size(); j++ )
        {
            
            wcstring whole_item;
            
            wcstring tail_item = tail_expand.at(j).completion;
            
            //sb_append_substring( &whole_item, in, len1 );
            whole_item.append(in, len1);
            
            //sb_append_char( &whole_item, INTERNAL_SEPARATOR );
            whole_item.push_back(INTERNAL_SEPARATOR);
            
            //sb_append_substring( &whole_item, sub_item2, item_len );
			whole_item.append(sub_item2);
            
            //sb_append_char( &whole_item, INTERNAL_SEPARATOR );
            whole_item.push_back(INTERNAL_SEPARATOR);
            
            //sb_append( &whole_item, tail_item );
            whole_item.append(tail_item);
			
			//al_push( out, whole_item.buff );
	 completion_t data_to_push;
	data_to_push.completion = whole_item;	   
         outList.push_back(data_to_push);
        }
    }
    
	return 1;
}

/**
   Wrapper around unescape funtion. Issues an error() on failiure.
*/
static wchar_t *expand_unescape( parser_t &parser, const wchar_t * in, int escape_special )
{
	wchar_t *res = unescape( in, escape_special );
	if( !res )
		parser.error( SYNTAX_ERROR, -1, L"Unexpected end of string" );
	return res;
}

static wcstring expand_unescape_string( const wcstring &in, int escape_special )
{
    wcstring tmp = in;
    unescape_string(tmp, escape_special);
    /* Need to detect error here */
	return tmp;
}

/**
   Attempts tilde expansion of the string specified, modifying it in place.
*/
static void expand_tilde_internal( wcstring &input )
{
    const wchar_t * const in = input.c_str();
	if( in[0] == HOME_DIRECTORY )
	{
		int tilde_error = 0;
        size_t tail_idx;
        wcstring home;

		if( in[1] == '/' || in[1] == '\0' )
		{
			/* Current users home directory */

			home = env_get_string( L"HOME" );
            tail_idx = 1;
		}
		else
		{
			/* Some other users home directory */
			const wchar_t *name_end = wcschr( in, L'/' );
            if (name_end)
            {
                tail_idx = name_end - in;
            }
            else
            {
                tail_idx = wcslen( in );
            }
            tail_idx = name_end - in;
            wcstring name_str = input.substr(1, name_end-in-1);
			const char *name_cstr = wcs2str( name_str.c_str() );
			struct passwd *userinfo =
				getpwnam( name_cstr );
            free((void *)name_cstr);

			if( userinfo == 0 )
			{
				tilde_error = 1;
			}
			else
			{
				home = str2wcstring(userinfo->pw_dir);
			}
		}

        if (! tilde_error)
        {
            input.replace(input.begin(), input.begin() + tail_idx, home);
        }
	}
}

static wchar_t * expand_tilde_internal_compat( wchar_t *in )
{
    wcstring tmp = in;
    expand_tilde_internal(tmp);
    free(in);
    return wcsdup(tmp.c_str());
}

void expand_tilde( wcstring &input)
{
	if( input[0] == L'~' )
	{
		input[0] = HOME_DIRECTORY;
		expand_tilde_internal( input );
	}
}

wchar_t * expand_tilde_compat( wchar_t *input )
{
    if (input[0] == L'~') {
        input[0] = HOME_DIRECTORY;
        return expand_tilde_internal_compat(input);
    }
    return input;
}

/**
   Remove any internal separators. Also optionally convert wildcard characters to
   regular equivalents. This is done to support EXPAND_SKIP_WILDCARDS.
*/
static void remove_internal_separator( const void *s, int conv )
{
	wchar_t *in = (wchar_t *)s;
	wchar_t *out=in;
	
	CHECK( s, );

	while( *in )
	{
		switch( *in )
		{
			case INTERNAL_SEPARATOR:
				in++;
				break;
				
			case ANY_CHAR:
				in++;
				*out++ = conv?L'?':ANY_CHAR;
				break;

			case ANY_STRING:
				in++;
				*out++ = conv?L'*':ANY_STRING;
				break;

			case ANY_STRING_RECURSIVE:
				in++;
				*out++ = conv?L'*':ANY_STRING_RECURSIVE;
				break;

			default:
				*out++ = *in++;
		}
	}
	*out=0;
}

static void remove_internal_separator2( wcstring &s, int conv )
{
    wchar_t *tmp = wcsdup(s.c_str());
    remove_internal_separator(tmp, conv);
    s = tmp;
    free(tmp);
}


int expand_string2( const wcstring &input, std::vector<completion_t> &output, int flags )
{
    parser_t &parser = parser_t::principal_parser();
   	std::vector<completion_t> list1, list2;
	std::vector<completion_t> *in, *out;
    
	size_t i;
	int cmdsubst_ok = 1;
	int res = EXPAND_OK;
    
	if( (!(flags & ACCEPT_INCOMPLETE)) && expand_is_clean( input.c_str() ) )
	{
		completion_t data_to_push;
		data_to_push.completion = input;
		output.push_back(data_to_push);
		return EXPAND_OK;
	}
        
	if( EXPAND_SKIP_CMDSUBST & flags )
	{
		wchar_t *begin, *end;
		
		if( parse_util_locate_cmdsubst( input.c_str(),
                                       &begin,
                                       &end,
                                       1 ) != 0 )
		{
			parser.error( CMDSUBST_ERROR, -1, L"Command substitutions not allowed" );
			return EXPAND_ERROR;
		}
		completion_t data_to_push;
		data_to_push.completion = input;
		list1.push_back(data_to_push);
	}
	else
	{
        cmdsubst_ok = expand_cmdsubst2(parser, input, list1);
	}
    
	if( !cmdsubst_ok )
	{
		return EXPAND_ERROR;
	}
	else
	{
		in = &list1;
		out = &list2;
        
		for( i=0; i < in->size(); i++ )
		{
			/*
             We accept incomplete strings here, since complete uses
             expand_string to expand incomplete strings from the
             commandline.
             */
			int unescape_flags = UNESCAPE_SPECIAL | UNESCAPE_INCOMPLETE;            
			wcstring next = expand_unescape_string( in->at(i).completion, unescape_flags );

			if( EXPAND_SKIP_VARIABLES & flags )
			{
                for (size_t i=0; i < next.size(); i++) {
                    if (next.at(i) == VARIABLE_EXPAND) {
                        next[i] = L'$';
                    }
                }
           	    completion_t data_to_push;
		    data_to_push.completion = next; 
		    out->push_back(data_to_push);
			}
			else
			{
				if(!expand_variables2( parser, wcsdup(next.c_str()), *out, next.size() - 1 ))
				{
					return EXPAND_ERROR;
				}
			}
		}
        
        in->clear();
        
		in = &list2;
		out = &list1;
        
		for( i=0; i < in->size(); i++ )
		{
            wcstring next = in->at(i).completion;
            
			if( !expand_brackets( parser, wcsdup(next.c_str()), flags, *out ))
			{
				return EXPAND_ERROR;
			}
		}
		in->clear();
        
		in = &list1;
		out = &list2;
        
		for( i=0; i < in->size(); i++ )
		{
			wcstring next = in->at(i).completion;
            
            expand_tilde_internal(next);
            
            
			if( flags & ACCEPT_INCOMPLETE )
			{
				if( next[0] == PROCESS_EXPAND )
				{
					/*
                     If process expansion matches, we are not
                     interested in other completions, so we
                     short-circut and return
                     */
					expand_pid( wcsdup(next.c_str()), flags, output );
					return EXPAND_OK;
				}
				else
				{
      					completion_t data_to_push;
					data_to_push.completion = next; 
	             			out->push_back(data_to_push);
				}
			}
			else
			{
				if( !expand_pid( wcsdup(next.c_str()), flags, *out ) )
				{
					return EXPAND_ERROR;
				}
			}
		}
        
        in->clear();
        
		in = &list2;
		out = &list1;
        
		for( i=0; i < in->size(); i++ )
		{
			wcstring next_str = in->at(i).completion;
			int wc_res;
                        
			remove_internal_separator2( next_str, EXPAND_SKIP_WILDCARDS & flags );			
            const wchar_t *next = next_str.c_str();
            
			if( ((flags & ACCEPT_INCOMPLETE) && (!(flags & EXPAND_SKIP_WILDCARDS))) ||
               wildcard_has( next, 1 ) )
			{
				const wchar_t *start, *rest;
				std::vector<completion_t> *list = out;
                
				if( next[0] == '/' )
				{
					start = L"/";
					rest = &next[1];
				}
				else
				{
					start = L"";
					rest = next;
				}
                
				if( flags & ACCEPT_INCOMPLETE )
				{
					list = &output;
				}
                
                		wc_res = wildcard_expand_string(rest, start, flags, *list);
                
				if( !(flags & ACCEPT_INCOMPLETE) )
				{
                    
					switch( wc_res )
					{
						case 0:
						{
							if( !(flags & ACCEPT_INCOMPLETE) )
							{
								if( res == EXPAND_OK )
									res = EXPAND_WILDCARD_NO_MATCH;
								break;
							}
						}
                            
						case 1:
						{
							size_t j;
							res = EXPAND_WILDCARD_MATCH;
							sort_completions( *out );
                            
							for( j=0; j< out->size(); j++ )
							{
			         				output.push_back( out->at(j) );
							}
							out->clear();
							break;
						}
                            
						case -1:
						{
							return EXPAND_ERROR;
						}
                            
					}
				}
				
			}
			else
			{
				if( flags & ACCEPT_INCOMPLETE)
				{
				}
				else
				{
 					completion_t data_to_push;
					data_to_push.completion = next; 
    					output.push_back(data_to_push);
				}
			}
            
		}
	}
    
	return res;
}

/**
   The real expansion function. expand_one is just a wrapper around this one.
*/
/*
int expand_string( void *context,
				   wchar_t *str,
				   array_list_t *end_out,
				   int flags )
{
	array_list_t list1, list2;
	array_list_t *in, *out;
    parser_t &parser = parser_t::principal_parser();

	int i;
	int cmdsubst_ok = 1;
	int res = EXPAND_OK;
	int start_count = al_get_count( end_out );

	CHECK( str, EXPAND_ERROR );
	CHECK( end_out, EXPAND_ERROR );

	if( (!(flags & ACCEPT_INCOMPLETE)) && expand_is_clean( str ) )
	{
		halloc_register( context, str );
		al_push( end_out, str );
		return EXPAND_OK;
	}

	al_init( &list1 );
	al_init( &list2 );

	if( EXPAND_SKIP_CMDSUBST & flags )
	{
		wchar_t *begin, *end;
		
		if( parse_util_locate_cmdsubst( str,
										&begin,
										&end,
										1 ) != 0 )
		{
			parser.error( CMDSUBST_ERROR, -1, L"Command substitutions not allowed" );
			free( str );
			al_destroy( &list1 );
			al_destroy( &list2 );
			return EXPAND_ERROR;
		}
		al_push( &list1, str );
	}
	else
	{
		cmdsubst_ok = expand_cmdsubst( parser, str, &list1 );
	}

	if( !cmdsubst_ok )
	{
		al_destroy( &list1 );
		return EXPAND_ERROR;
	}
	else
	{
		in = &list1;
		out = &list2;

		for( i=0; i<al_get_count( in ); i++ )
		{
			wchar_t *next;
*/
			/*
			  We accept incomplete strings here, since complete uses
			  expand_string to expand incomplete strings from the
			  commandline.
			*/
/*			int unescape_flags = UNESCAPE_SPECIAL | UNESCAPE_INCOMPLETE;

			next = expand_unescape( parser, (wchar_t *)al_get( in, i ), unescape_flags );

			free( (void *)al_get( in, i ) );

			if( !next )
			{
				debug( 2, L"Got null string on line %d of file %s", __LINE__, __FILE__ );
				continue;				
			}

			if( EXPAND_SKIP_VARIABLES & flags )
			{
				wchar_t *tmp;
				for( tmp=next; *tmp; tmp++ )
					if( *tmp == VARIABLE_EXPAND )
						*tmp = L'$';
				al_push( out, next );
			}
			else
			{
				if(!expand_variables( parser, next, out, wcslen(next)-1 ))
				{
					al_destroy( in );
					al_destroy( out );
					return EXPAND_ERROR;
				}
			}
		}

		al_truncate( in, 0 );

		in = &list2;
		out = &list1;

		for( i=0; i<al_get_count( in ); i++ )
		{
			wchar_t *next = (wchar_t *)al_get( in, i );

			if( !next )
			{
				debug( 2, L"Got null string on line %d of file %s", __LINE__, __FILE__ );
				continue;				
			}	

			if( !expand_brackets( parser, next, flags, out ))
			{
				al_destroy( in );
				al_destroy( out );
				return EXPAND_ERROR;
			}
		}
		al_truncate( in, 0 );

		in = &list1;
		out = &list2;

		for( i=0; i<al_get_count( in ); i++ )
		{
			wchar_t *next = (wchar_t *)al_get( in, i );

			if( !next )
			{
				debug( 2, L"Got null string on line %d of file %s", __LINE__, __FILE__ );
				continue;				
			}

			if( !(next=expand_tilde_internal_compat( next ) ) )
			{
				al_destroy( in );
				al_destroy( out );
				return EXPAND_ERROR;
			}

			if( flags & ACCEPT_INCOMPLETE )
			{
				if( *next == PROCESS_EXPAND )
				{*/
					/*
					  If process expansion matches, we are not
					  interested in other completions, so we
					  short-circut and return
					*/
/*					expand_pid( next, flags, end_out );
					al_destroy( in );
					al_destroy( out );
					return EXPAND_OK;
				}
				else
				{
					al_push( out, next );
				}
			}
			else
			{
				if( !expand_pid( next, flags, out ) )
				{
					al_destroy( in );
					al_destroy( out );
					return EXPAND_ERROR;
				}
			}
		}
		al_truncate( in, 0 );

		in = &list2;
		out = &list1;

		for( i=0; i<al_get_count( in ); i++ )
		{
			wchar_t *next = (wchar_t *)al_get( in, i );
			int wc_res;

			if( !next )
			{
				debug( 2, L"Got null string on line %d of file %s", __LINE__, __FILE__ );
				continue;				
			}	

			remove_internal_separator( next, EXPAND_SKIP_WILDCARDS & flags );
			
			if( ((flags & ACCEPT_INCOMPLETE) && (!(flags & EXPAND_SKIP_WILDCARDS))) ||
				wildcard_has( next, 1 ) )
			{
				const wchar_t *start, *rest;
				array_list_t *list = out;

				if( next[0] == '/' )
				{
					start = L"/";
					rest = &next[1];
				}
				else
				{
					start = L"";
					rest = next;
				}

				if( flags & ACCEPT_INCOMPLETE )
				{
					list = end_out;
				}
					
				wc_res = wildcard_expand( rest, start, flags, list );

				free( next );

				if( !(flags & ACCEPT_INCOMPLETE) )
				{
									
					switch( wc_res )
					{
						case 0:
						{
							if( !(flags & ACCEPT_INCOMPLETE) )
							{
								if( res == EXPAND_OK )
									res = EXPAND_WILDCARD_NO_MATCH;
								break;
							}
						}
					
						case 1:
						{
							int j;
							res = EXPAND_WILDCARD_MATCH;
							sort_list( out );

							for( j=0; j<al_get_count( out ); j++ )
							{
								wchar_t *next = (wchar_t *)al_get( out, j );
								if( !next )
								{
									debug( 2, L"Got null string on line %d of file %s", __LINE__, __FILE__ );
									continue;				
								}	
								al_push( end_out, next );
							}
							al_truncate( out, 0 );
							break;
						}

						case -1:
						{
							al_foreach( out, &free );
							al_destroy( in );
							al_destroy( out );
							return EXPAND_ERROR;
						}
					
					}
				}
				
			}
			else
			{
				if( flags & ACCEPT_INCOMPLETE)
				{
					free( next );
				}
				else
				{
					al_push( end_out, next );
				}
			}

		}
		al_destroy( in );
		al_destroy( out );
	}

	if( context )
	{
		for( i=start_count; i<al_get_count( end_out ); i++ )
		{
			halloc_register( context, (void *)al_get( end_out, i ) );
		}
	}

	
	return res;

}
*/

wchar_t *expand_one( void *context, wchar_t *string, int flags )
{
	std::vector<completion_t> l;
	int res;
	wchar_t *one;

	CHECK( string, 0 );
	
	if( (!(flags & ACCEPT_INCOMPLETE)) &&  expand_is_clean( string ) )
	{
		halloc_register( context, string );
		return string;
	}
	
//	al_init( &l );
	res = expand_string2( string, l, flags );
	if( !res )
	{
		one = 0;
	}
	else
	{
		if(  l.size() != 1 )
		{
			one=0;
		}
		else
		{
			one = wcsdup( l.at(0).completion.c_str() );
//			al_set( &l, 0, 0 );
		}
	}

//	al_foreach( &l, &free );
//	al_destroy( &l );

    if (context)
        halloc_register( context, one );
	return one;
}
 

