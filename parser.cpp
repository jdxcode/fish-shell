/** \file parser.c

The fish parser. Contains functions for parsing and evaluating code.

*/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <pwd.h>
#include <dirent.h>
#include <signal.h>

#include "fallback.h"
#include "util.h"

#include "common.h"
#include "wutil.h"
#include "proc.h"
#include "parser.h"
#include "parser_keywords.h"
#include "tokenizer.h"
#include "exec.h"
#include "wildcard.h"
#include "function.h"
#include "builtin.h"
#include "env.h"
#include "expand.h"
#include "reader.h"
#include "sanity.h"
#include "env_universal.h"
#include "event.h"
#include "intern.h"
#include "parse_util.h"
#include "halloc.h"
#include "halloc_util.h"
#include "path.h"
#include "signal.h"
#include "complete.h"

/**
   Maximum number of block levels in code. This is not the same as
   maximum recursion depth, this only has to do with how many block
   levels are legal in the source code, not at evaluation.
*/
#define BLOCK_MAX_COUNT 64

/**
   Maximum number of function calls, i.e. recursion depth.
*/
#define MAX_RECURSION_DEPTH 128

/**
   Error message for unknown builtin
*/
#define UNKNOWN_BUILTIN_ERR_MSG _(L"Unknown builtin '%ls'")

/**
   Error message for improper use of the exec builtin
*/
#define EXEC_ERR_MSG _(L"This command can not be used in a pipeline")

/**
   Error message for tokenizer error. The tokenizer message is
   appended to this message.
*/
#define TOK_ERR_MSG _( L"Tokenizer error: '%ls'")

/**
   Error message for short circuit command error.
*/
#define COND_ERR_MSG _( L"An additional command is required" )

/**
   Error message on reaching maximum recusrion depth
*/
#define RECURSION_ERR_MSG _( L"Maximum recursion depth reached. Accidental infinite loop?")

/**
   Error message used when the end of a block can't be located
*/
#define BLOCK_END_ERR_MSG _( L"Could not locate end of block. The 'end' command is missing, misspelled or a ';' is missing.")

/**
   Error message on reaching maximum number of block calls
*/
#define BLOCK_ERR_MSG _( L"Maximum number of nested blocks reached.")

/**
   Error message when a non-string token is found when expecting a command name
*/
#define CMD_ERR_MSG _( L"Expected a command name, got token of type '%ls'")

/**
   Error message when a non-string token is found when expecting a command name
*/
#define CMD_OR_ERR_MSG _( L"Expected a command name, got token of type '%ls'. Did you mean 'COMMAND; or COMMAND'? See the help section for the 'or' builtin command by typing 'help or'.")

/**
   Error message when a non-string token is found when expecting a command name
*/
#define CMD_AND_ERR_MSG _( L"Expected a command name, got token of type '%ls'. Did you mean 'COMMAND; and COMMAND'? See the help section for the 'and' builtin command by typing 'help and'.")

/**
   Error message when encountering an illegal command name
*/
#define ILLEGAL_CMD_ERR_MSG _( L"Illegal command name '%ls'")

/**
   Error message when encountering an illegal file descriptor
*/
#define ILLEGAL_FD_ERR_MSG _( L"Illegal file descriptor '%ls'")

/**
   Error message for wildcards with no matches
*/
#define WILDCARD_ERR_MSG _( L"Warning: No match for wildcard '%ls'. The command will not be executed.")

/**
   Error when using case builtin outside of switch block
*/
#define INVALID_CASE_ERR_MSG _( L"'case' builtin not inside of switch block")

/**
   Error when using loop control builtins (break or continue) outside of loop
*/
#define INVALID_LOOP_ERR_MSG _( L"Loop control command while not inside of loop" )

/**
   Error when using return builtin outside of function definition
*/
#define INVALID_RETURN_ERR_MSG _( L"'return' builtin command outside of function definition" )

/**
   Error when using else builtin outside of if block
*/
#define INVALID_ELSE_ERR_MSG _( L"'else' builtin not inside of if block" )

/**
   Error when using end builtin outside of block
*/
#define INVALID_END_ERR_MSG _( L"'end' command outside of block")

/**
   Error message for Posix-style assignment
*/
#define COMMAND_ASSIGN_ERR_MSG _( L"Unknown command '%ls'. Did you mean 'set %ls %ls'? For information on assigning values to variables, see the help section on the set command by typing 'help set'.")

/**
   Error for invalid redirection token
*/
#define REDIRECT_TOKEN_ERR_MSG _( L"Expected redirection specification, got token of type '%ls'")

/**
   Error when encountering redirection without a command
*/
#define INVALID_REDIRECTION_ERR_MSG _( L"Encountered redirection when expecting a command name. Fish does not allow a redirection operation before a command.")

/**
   Error for evaluating null pointer
*/
#define EVAL_NULL_ERR_MSG _( L"Tried to evaluate null pointer." )

/**
   Error for evaluating in illegal scope
*/
#define INVALID_SCOPE_ERR_MSG _( L"Tried to evaluate commands using invalid block type '%ls'" )


/**
   Error for wrong token type
*/
#define UNEXPECTED_TOKEN_ERR_MSG _( L"Unexpected token of type '%ls'")

/**
   While block description
*/ 
#define WHILE_BLOCK N_( L"'while' block" )

/**
   For block description
*/
#define FOR_BLOCK N_( L"'for' block" )

/**   
   Breakpoint block 
*/
#define BREAKPOINT_BLOCK N_( L"Block created by breakpoint" ) 



/**
   If block description
*/
#define IF_BLOCK N_( L"'if' conditional block" )


/**
   Function definition block description
*/
#define FUNCTION_DEF_BLOCK N_( L"function definition block" )


/**
   Function invocation block description
*/
#define FUNCTION_CALL_BLOCK N_( L"function invocation block" )

/**
   Function invocation block description
*/
#define FUNCTION_CALL_NO_SHADOW_BLOCK N_( L"function invocation block with no variable shadowing" )


/**
   Switch block description
*/
#define SWITCH_BLOCK N_( L"'switch' block" )


/**
   Fake block description
*/
#define FAKE_BLOCK N_( L"unexecutable block" )


/**
   Top block description
*/
#define TOP_BLOCK N_( L"global root block" )


/**
   Command substitution block description
*/
#define SUBST_BLOCK N_( L"command substitution block" )


/**
   Begin block description
*/
#define BEGIN_BLOCK N_( L"'begin' unconditional block" )


/**
   Source block description
*/
#define SOURCE_BLOCK N_( L"Block created by the . builtin" )

/**
   Source block description
*/
#define EVENT_BLOCK N_( L"event handler block" )


/**
   Unknown block description
*/
#define UNKNOWN_BLOCK N_( L"unknown/invalid block" )


/**
   Datastructure to describe a block type, like while blocks, command substitution blocks, etc.
*/
struct block_lookup_entry
{

	/**
	   The block type id. The legal values are defined in parser.h.
	*/
	int type;

	/**
	   The name of the builtin that creates this type of block, if any. 
	*/
	const wchar_t *name;
	
	/**
	   A description of this block type
	*/
	const wchar_t *desc;
}
	;

/**
   List of all legal block types
*/
static const struct block_lookup_entry block_lookup[]=
{
	{
		WHILE, L"while", WHILE_BLOCK 
	}
	,
	{
		FOR, L"for", FOR_BLOCK 
	}
	,
	{
		IF, L"if", IF_BLOCK 
	}
	,
	{
		FUNCTION_DEF, L"function", FUNCTION_DEF_BLOCK 
	}
	,
	{
		FUNCTION_CALL, 0, FUNCTION_CALL_BLOCK 
	}
	,
	{
		FUNCTION_CALL_NO_SHADOW, 0, FUNCTION_CALL_NO_SHADOW_BLOCK 
	}
	,
	{
		SWITCH, L"switch", SWITCH_BLOCK 
	}
	,
	{
		FAKE, 0, FAKE_BLOCK 
	}
	,
	{
		TOP, 0, TOP_BLOCK 
	}
	,
	{
		SUBST, 0, SUBST_BLOCK 
	}
	,
	{
		BEGIN, L"begin", BEGIN_BLOCK 
	}
	,
	{
		SOURCE, L".", SOURCE_BLOCK 
	}
	,
	{
		EVENT, 0, EVENT_BLOCK 
	}
	,
	{
		BREAKPOINT, L"breakpoint", BREAKPOINT_BLOCK 
	}
	,
	{
		0, 0, 0
	}
};


parser_t::parser_t(enum parser_type_t type) :
    parser_type(type),
    error_code(0),
    err_pos(0),
    current_tokenizer(NULL),
    lineinfo(NULL),
    current_tokenizer_pos(0),
    job_start_pos(0),
    eval_level(-1),
    current_block(NULL),
    block_io(NULL)
{
    
}

parser_t &parser_t::principal_parser(void)
{
    ASSERT_IS_MAIN_THREAD();
    static parser_t parser(PARSER_TYPE_GENERAL);
    return parser;
}

/**
   Return the current number of block nestings
*/
/*
static int block_count( block_t *b )
{

	if( b==0)
		return 0;
	return( block_count(b->outer)+1);
}
*/

void parser_t::push_block( int type )
{
//    block_t zerod = {};
//	block_t *newv = new block_t(zerod);
    void *buffer = halloc( 0, sizeof( block_t ));
    bzero(buffer, sizeof(block_t));
    block_t *newv = new(buffer) block_t();
	
	newv->src_lineno = parser_t::get_lineno();
	newv->src_filename = parser_t::current_filename()?intern(parser_t::current_filename()):0;
	
	newv->outer = current_block;
	newv->type = (current_block && current_block->skip)?FAKE:type;
	
	/*
	  New blocks should be skipped if the outer block is skipped,
	  except TOP ans SUBST block, which open up new environments. Fake
	  blocks should always be skipped. Rather complicated... :-(
	*/
	newv->skip=current_block?current_block->skip:0;
	
	/*
	  Type TOP and SUBST are never skipped
	*/
	if( type == TOP || type == SUBST )
	{
		newv->skip = 0;
	}

	/*
	  Fake blocks and function definition blocks are never executed
	*/
	if( type == FAKE || type == FUNCTION_DEF )
	{
		newv->skip = 1;
	}
	
	newv->job = 0;
	newv->loop_status=LOOP_NORMAL;

	current_block = newv;

	if( (newv->type != FUNCTION_DEF) &&
		(newv->type != FAKE) &&
		(newv->type != TOP) )
	{
		env_push( type == FUNCTION_CALL );
        newv->wants_pop_env = true;
	}
}

void parser_t::pop_block()
{
	block_t *old = current_block;
	if( !current_block )
	{
		debug( 1,
			   L"function %s called on empty block stack.",
			   __func__);
		bugreport();
		return;
	}
	
	current_block = current_block->outer;
    
    if (old->wants_pop_env)
        env_pop();
    
    old->~block_t();
    halloc_free(old);
}

const wchar_t *parser_t::get_block_desc( int block ) const
{
	int i;
	
	for( i=0; block_lookup[i].desc; i++ )
	{
		if( block_lookup[i].type == block )
		{
			return _( block_lookup[i].desc );
		}
	}
	return _(UNKNOWN_BLOCK);
}

/**
   Returns 1 if the specified command is a builtin that may not be used in a pipeline
*/
static int parser_is_pipe_forbidden( const wcstring &word )
{
	return contains( word,
					 L"exec",
					 L"case",
					 L"break",
					 L"return",
					 L"continue" );
}

/**
   Search the text for the end of the current block
*/
static const wchar_t *parser_find_end( const wchar_t * buff )
{
	tokenizer tok;
	int had_cmd=0;
	int count = 0;
	int error=0;
	int mark=0;

	CHECK( buff, 0 );

	for( tok_init( &tok, buff, 0 );
		 tok_has_next( &tok ) && !error;
		 tok_next( &tok ) )
	{
		int last_type = tok_last_type( &tok );
		switch( last_type )
		{
			case TOK_STRING:
			{
				if( !had_cmd )
				{
					if( wcscmp(tok_last(&tok), L"end")==0)
					{
						count--;
					}
					else if( parser_keywords_is_block( tok_last(&tok) ) )
					{
						count++;
					}

					if( count < 0 )
					{
						error = 1;
					}
					had_cmd = 1;
				}
				break;
			}

			case TOK_END:
			{
				had_cmd = 0;
				break;
			}

			case TOK_PIPE:
			case TOK_BACKGROUND:
			{
				if( had_cmd )
				{
					had_cmd = 0;
				}
				else
				{
					error = 1;
				}
				break;

			}

			case TOK_ERROR:
				error = 1;
				break;

			default:
				break;

		}
		if(!count)
		{
			tok_next( &tok );
			mark = tok_get_pos( &tok );
			break;
		}

	}

	tok_destroy( &tok );
	if(!count && !error){

		return buff+mark;
	}
	return 0;

}


void parser_t::forbid_function( const wcstring &function )
{
    forbidden_function.push_back(function);
}

void parser_t::allow_function()
{
/*
  if( al_peek( &forbidden_function) )
  debug( 2, L"Allow %ls\n", al_peek( &forbidden_function)  );
*/
    forbidden_function.pop_back();
}

void parser_t::error( int ec, int p, const wchar_t *str, ... )
{
	va_list va;

	CHECK( str, );
	
	error_code = ec;
	err_pos = p;

	va_start( va, str );
    err_buff = vformat_string(str, va);
	va_end( va );

}

/**
   Print profiling information to the specified stream
*/
static void print_profile( const std::vector<profile_item_t> &items,
						   size_t pos,
						   FILE *out )
{
	const profile_item_t *me, *prev;
	size_t i;
	int my_time;

	if( pos >= items.size() )
	{
		return;
	}
	
	me= &items.at(pos);
	if( !me->skipped )
	{
		my_time=me->parse+me->exec;

		for( i=pos+1; i<items.size(); i++ )
		{
			prev = &items.at(i);
			if( prev->skipped )
			{
				continue;
			}
			
			if( prev->level <= me->level )
			{
				break;
			}

			if( prev->level > me->level+1 )
			{
				continue;
			}

			my_time -= prev->parse;
			my_time -= prev->exec;
		}

		if( me->cmd.size() > 0 )
		{
			if( fwprintf( out, L"%d\t%d\t", my_time, me->parse+me->exec ) < 0 )
			{
				wperror( L"fwprintf" );
				return;
			}
			
			for( i=0; i<me->level; i++ )
			{
				if( fwprintf( out, L"-" ) < 0 )
				{
					wperror( L"fwprintf" );
					return;
				}

			}
			if( fwprintf( out, L"> %ls\n", me->cmd.c_str() ) < 0 )
			{
				wperror( L"fwprintf" );
				return;
			}

		}
	}
	print_profile( items, pos+1, out );
}

void parser_t::destroy()
{
	if( profile )
	{
		/*
		  Save profiling information
		*/
		FILE *f = fopen( profile, "w" );
		if( !f )
		{
			debug( 1,
				   _(L"Could not write profiling information to file '%s'"),
				   profile );
		}
		else
		{
			if( fwprintf( f,
						  _(L"Time\tSum\tCommand\n"),
						  profile_items.size() ) < 0 )
			{
				wperror( L"fwprintf" );
			}
			else
			{
				print_profile( profile_items, 0, f );
			}
			
			if( fclose( f ) )
			{
				wperror( L"fclose" );
			}
		}
	}

	if( lineinfo )
	{
		sb_destroy( lineinfo );
		free(lineinfo );
		lineinfo = 0;
	}

	forbidden_function.resize(0);

}

/**
   Print error message to string_buffer_t if an error has occured while parsing

   \param target the buffer to write to
   \param prefix: The string token to prefix the ech line with. Usually the name of the command trying to parse something.
*/
void parser_t::print_errors( string_buffer_t *target, const wchar_t *prefix )
{
	CHECK( target, );
	CHECK( prefix, );
	
	if( error_code && ! err_buff.empty() )
	{
		int tmp;

		sb_printf( target, L"%ls: %ls\n", prefix, err_buff.c_str() );

		tmp = current_tokenizer_pos;
		current_tokenizer_pos = err_pos;

		sb_printf( target, L"%ls", this->current_line() );

		current_tokenizer_pos=tmp;
	}
}

/**
   Print error message to stderr if an error has occured while parsing
*/
void parser_t::print_errors_stderr()
{
	if( error_code && ! err_buff.empty() )
	{
		debug( 0, L"%ls", err_buff.c_str() );
		int tmp;
		
		tmp = current_tokenizer_pos;
		current_tokenizer_pos = err_pos;
		
		fwprintf( stderr, L"%ls", this->current_line() );
		
		current_tokenizer_pos=tmp;
	}
	
}

int parser_t::eval_args( const wchar_t *line, std::vector<completion_t> &args )
{
	tokenizer tok;
	/*
	  eval_args may be called while evaulating another command, so we
	  save the previous tokenizer and restore it on exit
	*/
	tokenizer *previous_tokenizer=current_tokenizer;
	int previous_pos=current_tokenizer_pos;
	int do_loop=1;

	CHECK( line, 1 );
//	CHECK( args, 1 );
		
	proc_push_interactive(0);	
	current_tokenizer = &tok;
	current_tokenizer_pos = 0;
	
	tok_init( &tok, line, 0 );
	error_code=0;

	for(;do_loop && tok_has_next( &tok) ; tok_next( &tok ) )
	{
		current_tokenizer_pos = tok_get_pos( &tok );
		switch(tok_last_type( &tok ) )
		{
			case TOK_STRING:
			{
				wchar_t *tmp = wcsdup(tok_last( &tok ));
				
				if( !tmp )
				{
					DIE_MEM();
				}
				
				if( expand_string( tmp, args, 0 ) == EXPAND_ERROR )
				{
					err_pos=tok_get_pos( &tok );
					do_loop=0;
				}
				break;
			}
			
			case TOK_END:
			{
				break;
			}
			
			case TOK_ERROR:
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( &tok ),
					   TOK_ERR_MSG,
					   tok_last(&tok) );

				do_loop=0;
				break;
			}

			default:
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( &tok ),
					   UNEXPECTED_TOKEN_ERR_MSG,
					   tok_get_desc( tok_last_type(&tok)) );

				do_loop=0;
				break;
			}
		}
	}

	this->print_errors_stderr();
	
	tok_destroy( &tok );
	
	current_tokenizer=previous_tokenizer;
	current_tokenizer_pos = previous_pos;
	proc_pop_interactive();
	
	return 1;
}

void parser_t::stack_trace( block_t *b, string_buffer_t *buff)
{
	
	/*
	  Validate input
	*/
	CHECK( buff, );
		
	/*
	  Check if we should end the recursion
	*/
	if( !b )
		return;

	if( b->type==EVENT )
	{
		/*
		  This is an event handler
		*/
        wcstring description = event_get_desc( b->state1<event_t *>() );
		sb_printf( buff, _(L"in event handler: %ls\n"), description.c_str());
		sb_printf( buff,
				   L"\n" );

		/*
		  Stop recursing at event handler. No reason to belive that
		  any other code is relevant.

		  It might make sense in the future to continue printing the
		  stack trace of the code that invoked the event, if this is a
		  programmatic event, but we can't currently detect that.
		*/
		return;
	}
	
	if( b->type == FUNCTION_CALL || b->type==SOURCE || b->type==SUBST)
	{
		/*
		  These types of blocks should be printed
		*/

		int i;

		switch( b->type)
		{
			case SOURCE:
			{
                const wcstring &source_dest = b->state1<wcstring>();
				sb_printf( buff, _(L"in . (source) call of file '%ls',\n"), source_dest.c_str() );
				break;
			}
			case FUNCTION_CALL:
			{
                const wcstring &function_call_name = b->state1<wcstring>();
				sb_printf( buff, _(L"in function '%ls',\n"), function_call_name.c_str() );
				break;
			}
			case SUBST:
			{
				sb_printf( buff, _(L"in command substitution\n") );
				break;
			}
		}

		const wchar_t *file = b->src_filename;

		if( file )
		{
			sb_printf( buff,
					   _(L"\tcalled on line %d of file '%ls',\n"),
					   b->src_lineno,
					   file );
		}
		else
		{
			sb_printf( buff,
					   _(L"\tcalled on standard input,\n") );
		}
		
		if( b->type == FUNCTION_CALL )
		{			
            const process_t * const process = b->state2<process_t*>();
			if( process->argv(1) )
			{
				string_buffer_t tmp;
				sb_init( &tmp );
				
				for( i=1; process->argv(i); i++ )
				{
					sb_append( &tmp, i>1?L" ":L"", process->argv(i), NULL );
				}
				sb_printf( buff, _(L"\twith parameter list '%ls'\n"), (wchar_t *)tmp.buff );
				
				sb_destroy( &tmp );
			}
		}
		
		sb_printf( buff,
				   L"\n" );
	}

	/*
	  Recursively print the next block
	*/
	parser_t::stack_trace( b->outer, buff );
}

/**
   Returns the name of the currently evaluated function if we are
   currently evaluating a function, null otherwise. This is tested by
   moving down the block-scope-stack, checking every block if it is of
   type FUNCTION_CALL.
*/
const wchar_t *parser_t::is_function() const
{
    // PCA: Have to make this a string somehow
    ASSERT_IS_MAIN_THREAD();
    wcstring result;
    
	block_t *b = current_block;
	while( 1 )
	{
		if( !b )
		{
			return NULL;
		}
		if( b->type == FUNCTION_CALL )
		{
            result = b->state1<wcstring>();
			return result.c_str();
		}
		b=b->outer;
	}
}


int parser_t::get_lineno() const
{
	const wchar_t *whole_str;
	const wchar_t *function_name;

	int lineno;
	
/*	static const wchar_t *prev_str = 0;
  static int i=0;
  static int lineno=1;
*/
	if( !current_tokenizer )
		return -1;
	
	whole_str = tok_string( current_tokenizer );

	if( !whole_str )
		return -1;
	
	lineno = parse_util_lineno( whole_str, current_tokenizer_pos );

	if( (function_name = is_function()) )
	{
		lineno += function_get_definition_offset( function_name );
	}

	return lineno;
}

const wchar_t *parser_t::current_filename() const
{
    /* We query a global array for the current file name, so it only makes sense to ask this on the principal parser. */
    ASSERT_IS_MAIN_THREAD();
    assert(this == &principal_parser());
    
	block_t *b = current_block;

	while( 1 )
	{
		if( !b )
		{
			return reader_current_filename();
		}
		if( b->type == FUNCTION_CALL )
		{
            wcstring function_call_name = b->state1<wcstring>();
			return function_get_definition_file(function_call_name.c_str());
		}
		b=b->outer;
	}
}

/**
   Calculates the on-screen width of the specified substring of the
   specified string. This function takes into account the width and
   allignment of the tab character, but other wise behaves like
   repeatedly calling wcwidth.
*/
static int printed_width( const wchar_t *str, int len )
{
	int res=0;
	int i;

	CHECK( str, 0 );

	for( i=0; str[i] && i<len; i++ )
	{
		if( str[i] == L'\t' )
		{
			res=(res+8)&~7;
		}
		else
		{
			res += wcwidth( str[i] );
		}
	}
	return res;
}


const wchar_t *parser_t::current_line()
{
	int lineno=1;

	const wchar_t *file;
	wchar_t *whole_str;
	wchar_t *line;
	wchar_t *line_end;
	int i;
	int offset;
	int current_line_width;
	const wchar_t *function_name=0;
	int current_line_start=0;

	if( !current_tokenizer )
	{
		return L"";
	}

	file = parser_t::current_filename();
	whole_str = tok_string( current_tokenizer );
	line = whole_str;

	if( !line )
		return L"";

	if( !lineinfo )
	{
		lineinfo = (string_buffer_t *)malloc( sizeof(string_buffer_t) );
		sb_init( lineinfo );
	}
	sb_clear( lineinfo );

	/*
	  Calculate line number, line offset, etc.
	*/
	for( i=0; i<current_tokenizer_pos && whole_str[i]; i++ )
	{
		if( whole_str[i] == L'\n' )
		{
			lineno++;
			current_line_start=i+1;
			line = &whole_str[i+1];
		}
	}

//	lineno = current_tokenizer_pos;
	

	current_line_width=printed_width( whole_str+current_line_start,
									  current_tokenizer_pos-current_line_start );

	if( (function_name = is_function()) )
	{
		lineno += function_get_definition_offset( function_name );
	}

	/*
	  Copy current line from whole string
	*/
	line_end = wcschr( line, L'\n' );
	if( !line_end )
		line_end = line+wcslen(line);

	line = wcsndup( line, line_end-line );

	/**
	   If we are not going to print a stack trace, at least print the line number and filename
	*/
	if( !is_interactive || is_function() )
	{
		int prev_width = my_wcswidth( (wchar_t *)lineinfo->buff );
		if( file )
			sb_printf( lineinfo,
					   _(L"%ls (line %d): "),
					   file,
					   lineno );
		else
			sb_printf( lineinfo,
					   L"%ls: ",
					   _(L"Standard input"),
					   lineno );
		offset = my_wcswidth( (wchar_t *)lineinfo->buff ) - prev_width;
	}
	else
	{
		offset=0;
	}

//	debug( 1, L"Current pos %d, line pos %d, file_length %d, is_interactive %d, offset %d\n", current_tokenizer_pos,  current_line_pos, wcslen(whole_str), is_interactive, offset);
	/*
	  Skip printing character position if we are in interactive mode
	  and the error was on the first character of the line.
	*/
	if( !is_interactive || is_function() || (current_line_width!=0) )
	{
		// Workaround since it seems impossible to print 0 copies of a character using %*lc
		if( offset+current_line_width )
		{
			sb_printf( lineinfo,
					   L"%ls\n%*lc^\n",
					   line,
					   offset+current_line_width,
					   L' ' );
		}
		else
		{
			sb_printf( lineinfo,
					   L"%ls\n^\n",
					   line );
		}
	}

	free( line );
	parser_t::stack_trace( current_block, lineinfo );

	return (wchar_t *)lineinfo->buff;
}

int parser_t::get_pos() const
{
	return tok_get_pos( current_tokenizer );
}

int parser_t::get_job_pos() const
{
	return job_start_pos;
}


void parser_t::set_pos( int p)
{
	tok_set_pos( current_tokenizer, p );
}

const wchar_t *parser_t::get_buffer() const
{
	return tok_string( current_tokenizer );
}


int parser_t::is_help( const wchar_t *s, int min_match ) const
{
	int len;

	CHECK( s, 0 );

	len = wcslen(s);

	min_match = maxi( min_match, 3 );
		
	return ( wcscmp( L"-h", s ) == 0 ) ||
		( len >= min_match && (wcsncmp( L"--help", s, len ) == 0) );
}

/**
   Parse options for the specified job

   \param p the process to parse options for
   \param j the job to which the process belongs to
   \param tok the tokenizer to read options from
   \param args the argument list to insert options into
*/
void parser_t::parse_job_argument_list( process_t *p,
									 job_t *j,
									 tokenizer *tok,
									 std::vector<completion_t> &args )
{
	int is_finished=0;

	int proc_is_count=0;

	int matched_wildcard = 0, unmatched_wildcard = 0;

	wcstring unmatched;
	int unmatched_pos=0;

	/*
	  Test if this is the 'count' command. We need to special case
	  count in the shell, since it should display a help message on
	  'count -h', but not on 'set foo -h; count $foo'. This is an ugly
	  workaround and a huge hack, but as near as I can tell, the
	  alternatives are worse.
	*/
	proc_is_count = ( args.at(0).completion == L"count" );
	
	while( 1 )
	{

		switch( tok_last_type( tok ) )
		{
			case TOK_PIPE:
			{
				wchar_t *end;
				
				if( (p->type == INTERNAL_EXEC) )
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( tok ),
						   EXEC_ERR_MSG );
					return;
				}

				errno = 0;
				p->pipe_write_fd = wcstol( tok_last( tok ), &end, 10 );
				if( p->pipe_write_fd < 0 || errno || *end )
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( tok ),
						   ILLEGAL_FD_ERR_MSG,
						   tok_last( tok ) );
					return;
				}
				
                p->set_argv(completions_to_wcstring_list(args));
				p->next = new process_t();

				tok_next( tok );
				
				/*
				  Don't do anything on failiure. parse_job will notice
				  the error flag and report any errors for us
				*/
				parse_job( p->next, j, tok );

				is_finished = 1;
				break;
			}
			
			case TOK_BACKGROUND:
			{
				job_set_flag( j, JOB_FOREGROUND, 0 );
			}
			
			case TOK_END:
			{
				if( !p->get_argv() )
					p->set_argv(completions_to_wcstring_list(args));
				if( tok_has_next(tok))
					tok_next(tok);

				is_finished = 1;

				break;
			}

			case TOK_STRING:
			{
				int skip=0;

				if( job_get_flag( j, JOB_SKIP ) )
				{
					skip = 1;
				}
				else if( current_block->skip )
				{
					/*
					  If this command should be skipped, we do not expand the arguments
					*/
					skip=1;

					/*
					  But if this is in fact a case statement, then it should be evaluated
					*/

					if( (current_block->type == SWITCH) && args.at(0).completion == L"case" && p->type == INTERNAL_BUILTIN )
					{
						skip=0;
					}
				}

				if( !skip )
				{
					if( ( proc_is_count ) &&
					    (  args.size() == 1) &&
					    ( parser_t::is_help( tok_last(tok), 0) ) &&
					    ( p->type == INTERNAL_BUILTIN ) )
					{
						/*
						  Display help for count
						*/
						p->count_help_magic = 1;
					}

					switch( expand_string( tok_last( tok ), args, 0 ) )
					{
						case EXPAND_ERROR:
						{
							err_pos=tok_get_pos( tok );
							if( error_code == 0 )
							{
								error( SYNTAX_ERROR,
								       tok_get_pos( tok ),
								       _(L"Could not expand string '%ls'"),
								       tok_last(tok) );

							}
							break;
						}

						case EXPAND_WILDCARD_NO_MATCH:
						{
							unmatched_wildcard = 1;
							if( unmatched.empty() )
							{
                                unmatched = tok_last(tok);
								unmatched_pos = tok_get_pos( tok );
							}

							break;
						}

						case EXPAND_WILDCARD_MATCH:
						{
							matched_wildcard = 1;
							break;
						}

						case EXPAND_OK:
						{
							break;
						}

					}

				}

				break;
			}

			case TOK_REDIRECT_OUT:
			case TOK_REDIRECT_IN:
			case TOK_REDIRECT_APPEND:
			case TOK_REDIRECT_FD:
			case TOK_REDIRECT_NOCLOB:
			{
				int type = tok_last_type( tok );
                std::auto_ptr<io_data_t> new_io;
                wcstring target;
				bool has_target = false;
				wchar_t *end;
				
				/*
				  Don't check redirections in skipped part

				  Otherwise, bogus errors may be the result. (Do check
				  that token is string, though)
				*/
				if( current_block->skip )
				{
					tok_next( tok );
					if( tok_last_type( tok ) != TOK_STRING )
					{
						error( SYNTAX_ERROR,
							   tok_get_pos( tok ),
							   REDIRECT_TOKEN_ERR_MSG,
							   tok_get_desc( tok_last_type(tok)) );
					}

					break;
				}

				new_io.reset(new io_data_t);

				errno = 0;
				new_io->fd = wcstol( tok_last( tok ),
									 &end,
									 10 );
				if( new_io->fd < 0 || errno || *end )
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( tok ),
						   ILLEGAL_FD_ERR_MSG,
						   tok_last( tok ) );
				}
				else
				{
					
					tok_next( tok );

					switch( tok_last_type( tok ) )
					{
						case TOK_STRING:
						{
                            target = tok_last( tok );
                            has_target = expand_one(target, no_exec ? EXPAND_SKIP_VARIABLES : 0);

							if( ! has_target && error_code == 0 )
							{
								error( SYNTAX_ERROR,
									   tok_get_pos( tok ),
									   REDIRECT_TOKEN_ERR_MSG,
									   tok_last( tok ) );

							}
							break;
						}

						default:
							error( SYNTAX_ERROR,
								   tok_get_pos( tok ),
								   REDIRECT_TOKEN_ERR_MSG,
								   tok_get_desc( tok_last_type(tok)) );
					}

					if( ! has_target || target.size() == 0 )
					{
						if( error_code == 0 )
							error( SYNTAX_ERROR,
								   tok_get_pos( tok ),
								   _(L"Invalid IO redirection") );
						tok_next(tok);
					}
					else
					{

						switch( type )
						{
							case TOK_REDIRECT_APPEND:
								new_io->io_mode = IO_FILE;
								new_io->param2.flags = O_CREAT | O_APPEND | O_WRONLY;
								new_io->param1.filename = wcsdup(target.c_str()); // PCA LEAKS!
								break;

							case TOK_REDIRECT_OUT:
								new_io->io_mode = IO_FILE;
								new_io->param2.flags = O_CREAT | O_WRONLY | O_TRUNC;
								new_io->param1.filename = wcsdup(target.c_str()); // PCA LEAKS!
								break;

							case TOK_REDIRECT_NOCLOB:
								new_io->io_mode = IO_FILE;
								new_io->param2.flags = O_CREAT | O_EXCL | O_WRONLY;
								new_io->param1.filename = wcsdup(target.c_str()); // PCA LEAKS!
								break;

							case TOK_REDIRECT_IN:
								new_io->io_mode = IO_FILE;
								new_io->param2.flags = O_RDONLY;
								new_io->param1.filename = wcsdup(target.c_str()); // PCA LEAKS!
								break;

							case TOK_REDIRECT_FD:
							{
								if( target == L"-" )
								{
									new_io->io_mode = IO_CLOSE;
								}
								else
								{
									wchar_t *end;
									
									new_io->io_mode = IO_FD;
									errno = 0;
									
									new_io->param1.old_fd = wcstol( target.c_str(),
																	&end,
																	10 );
									
									if( ( new_io->param1.old_fd < 0 ) ||
										errno || *end )
									{
										error( SYNTAX_ERROR,
											   tok_get_pos( tok ),
											   _(L"Requested redirection to something that is not a file descriptor %ls"),
											   target.c_str() );

										tok_next(tok);
									}
								}
								break;
							}
						}
					
					}
				}

				j->io = io_add( j->io, new_io.release() );
				
			}
			break;

			case TOK_ERROR:
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( tok ),
					   TOK_ERR_MSG,
					   tok_last(tok) );

				return;
			}

			default:
				error( SYNTAX_ERROR,
					   tok_get_pos( tok ),
					   UNEXPECTED_TOKEN_ERR_MSG,
					   tok_get_desc( tok_last_type(tok)) );

				tok_next(tok);
				break;
		}

		if( (is_finished) || (error_code != 0) )
			break;

		tok_next( tok );
	}

	if( !error_code )
	{
		if( unmatched_wildcard && !matched_wildcard )
		{
			job_set_flag( j, JOB_WILDCARD_ERROR, 1 );
			proc_set_last_status( STATUS_UNMATCHED_WILDCARD );
			if( is_interactive && !is_block )
			{
				int tmp;

				debug( 1, WILDCARD_ERR_MSG, unmatched.c_str() );
				tmp = current_tokenizer_pos;
				current_tokenizer_pos = unmatched_pos;

				fwprintf( stderr, L"%ls", parser_t::current_line() );

				current_tokenizer_pos=tmp;
			}

		}
	}

	return;
}

/*
  static void print_block_stack( block_t *b )
  {
  if( !b )
  return;
  print_block_stack( b->outer );
	
  debug( 0, L"Block type %ls, skip: %d", parser_get_block_desc( b->type ), b->skip );	
  }
*/
	
/**
   Fully parse a single job. Does not call exec on it, but any command substitutions in the job will be executed.

   \param p The process structure that should be used to represent the first process in the job.
   \param j The job structure to contain the parsed job
   \param tok tokenizer to read from
f
   \return 1 on success, 0 on error
*/
int parser_t::parse_job( process_t *p,
					  job_t *j,
					  tokenizer *tok )
{
    std::vector<completion_t> args; // The list that will become the argc array for the program
	int use_function = 1;   // May functions be considered when checking what action this command represents
	int use_builtin = 1;    // May builtins be considered when checking what action this command represents
	int use_command = 1;    // May commands be considered when checking what action this command represents
	int is_new_block=0;     // Does this command create a new block?

	block_t *prev_block = current_block;
	int prev_tokenizer_pos = current_tokenizer_pos;	

	current_tokenizer_pos = tok_get_pos( tok );

	while( args.size() == 0 )
	{
		wcstring nxt;
        bool has_nxt = false;
		int consumed = 0; // Set to one if the command requires a second command, like e.g. while does
		int mark;         // Use to save the position of the beginning of the token
		
		switch( tok_last_type( tok ))
		{
			case TOK_STRING:
			{
                nxt = tok_last( tok );
                has_nxt = expand_one(nxt, EXPAND_SKIP_CMDSUBST | EXPAND_SKIP_VARIABLES);
				
				if( ! has_nxt)
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( tok ),
						   ILLEGAL_CMD_ERR_MSG,
						   tok_last( tok ) );
					
					current_tokenizer_pos = prev_tokenizer_pos;	
					return 0;
				}
				break;
			}

			case TOK_ERROR:
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( tok ),
					   TOK_ERR_MSG,
					   tok_last(tok) );

				current_tokenizer_pos = prev_tokenizer_pos;	
				return 0;
			}

			case TOK_PIPE:
			{
				wchar_t *str = tok_string( tok );
				if( tok_get_pos(tok)>0 && str[tok_get_pos(tok)-1] == L'|' )
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( tok ),
						   CMD_OR_ERR_MSG,
						   tok_get_desc( tok_last_type(tok) ) );
				}
				else
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( tok ),
						   CMD_ERR_MSG,
						   tok_get_desc( tok_last_type(tok) ) );
				}

				current_tokenizer_pos = prev_tokenizer_pos;	
				return 0;
			}

			default:
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( tok ),
					   CMD_ERR_MSG,
					   tok_get_desc( tok_last_type(tok) ) );

				current_tokenizer_pos = prev_tokenizer_pos;	
				return 0;
			}
		}
		
		mark = tok_get_pos( tok );

		if( contains( nxt,
					  L"command",
					  L"builtin",
					  L"not",
					  L"and",
					  L"or",
					  L"exec" ) )
		{			
			int sw;
			int is_exec = nxt == L"exec";
			
			if( is_exec && (p != j->first_process) )
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( tok ),
					   EXEC_ERR_MSG );
				current_tokenizer_pos = prev_tokenizer_pos;	
				return 0;
			}

			tok_next( tok );
			sw = parser_keywords_is_switch( tok_last( tok ) );
			
			if( sw == ARG_SWITCH )
			{
				tok_set_pos( tok, mark);
			}
			else
			{
				if( sw == ARG_SKIP )
				{
					tok_next( tok );
				}
								
				consumed=1;

				if( nxt == L"command" || nxt == L"builtin" )
				{
					use_function = 0;
					if( nxt == L"command" )
					{
						use_builtin = 0;
						use_command = 1;
					}					
					else
					{
						use_builtin = 1;						
						use_command = 0;					
					}
				}
				else if( nxt == L"not" )
				{
					job_set_flag( j, JOB_NEGATE, !job_get_flag( j, JOB_NEGATE ) );
				}
				else if( nxt == L"and" )
				{
					job_set_flag( j, JOB_SKIP, proc_get_last_status());
				}
				else if( nxt == L"or" )
				{
					job_set_flag( j, JOB_SKIP, !proc_get_last_status());
				}
				else if( is_exec )
				{
					use_function = 0;
					use_builtin=0;
					p->type=INTERNAL_EXEC;
					current_tokenizer_pos = prev_tokenizer_pos;	
				}
			}
		}
		else if( nxt == L"while" )
		{
			int new_block = 0;
			tok_next( tok );

			if( ( current_block->type != WHILE ) )
			{
				new_block = 1;
			}
			else if( current_block->state1<int>() == WHILE_TEST_AGAIN )
			{
				current_block->state1<int>() = WHILE_TEST_FIRST;
			}
			else
			{
				new_block = 1;
			}

			if( new_block )
			{
				this->push_block( WHILE );
				current_block->state1<int>() = WHILE_TEST_FIRST;
				current_block->tok_pos = mark;
			}

			consumed=1;
			is_new_block=1;

		}
		else if( nxt == L"if" )
		{
			tok_next( tok );

			this->push_block( IF );

			current_block->state1<int>()=0;
			current_block->tok_pos = mark;

			is_new_block=1;
			consumed=1;			
		}

		/*
		  Test if we need another command
		*/
		if( consumed )
		{
			/*
			  Yes we do, around in the loop for another lap, then!
			*/
			continue;
		}
		
		if( use_function && !current_block->skip )
		{
			int nxt_forbidden=0;
			wcstring forbid;

			int is_function_call=0;

			/*
			  This is a bit fragile. It is a test to see if we are
			  inside of function call, but not inside a block in that
			  function call. If, in the future, the rules for what
			  block scopes are pushed on function invocation changes,
			  then this check will break.
			*/
			if( ( current_block->type == TOP ) && 
				( current_block->outer ) && 
				( current_block->outer->type == FUNCTION_CALL ) )
				is_function_call = 1;
			
			/*
			  If we are directly in a function, and this is the first
			  command of the block, then the function we are executing
			  may not be called, since that would mean an infinite
			  recursion.
			*/
			if( is_function_call && !current_block->had_command )
			{
                forbid = forbidden_function.empty() ? wcstring(L"") : forbidden_function.back();
				nxt_forbidden = (forbid == nxt);
			}
		
			if( !nxt_forbidden && has_nxt && function_exists( nxt.c_str() ) )
			{
				/*
				  Check if we have reached the maximum recursion depth
				*/
				if( forbidden_function.size() > MAX_RECURSION_DEPTH )
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( tok ),
						   RECURSION_ERR_MSG );
				}
				else
				{
					p->type = INTERNAL_FUNCTION;
				}
			}
		}
		args.push_back(completion_t(nxt));
	}

	if( error_code == 0 )
	{
		if( !p->type )
		{
			if( use_builtin &&
				builtin_exists(args.at(0).completion))
			{
				p->type = INTERNAL_BUILTIN;
				is_new_block |= parser_keywords_is_block( args.at( 0 ).completion );
			}
		}
		
		if( (!p->type || (p->type == INTERNAL_EXEC) ) )
		{
			/*
			  If we are not executing the current block, allow
			  non-existent commands.
			*/
			if( current_block->skip )
			{
				p->actual_cmd = wcsdup(L"");
			}
			else
			{
				int err;
				p->actual_cmd = path_get_path( args.at(0).completion.c_str() );
				err = errno;
				
				/*
				  Check if the specified command exists
				*/
				if( p->actual_cmd == NULL )
				{

					int tmp;
						const wchar_t *cmd = args.at( 0 ).completion.c_str();
						
						/* 
						   We couldn't find the specified command.
						   
						   What we want to happen now is that the
						   specified job won't get executed, and an
						   error message is printed on-screen, but
						   otherwise, the parsing/execution of the
						   file continues. Because of this, we don't
						   want to call error(), since that would stop
						   execution of the file. Instead we let
						   p->actual_command be 0 (null), which will
						   cause the job to silently not execute. We
						   also print an error message and set the
						   status to 127 (This is the standard number
						   for this, used by other shells like bash
						   and zsh).
						*/
						if( wcschr( cmd, L'=' ) )
						{
							wchar_t *cpy = wcsdup( cmd );
							wchar_t *valpart = wcschr( cpy, L'=' );
							*valpart++=0;
							
							debug( 0,
								   COMMAND_ASSIGN_ERR_MSG,
								   cmd,
								   cpy,
								   valpart);
							free(cpy);
							
						}
						else if(cmd[0]==L'$')
						{
							
							const env_var_t val_wstr = env_get_string( cmd+1 );
							const wchar_t *val = val_wstr.missing() ? NULL : val_wstr.c_str();	
							if( val )
							{
								debug( 0,
									   _(L"Variables may not be used as commands. Instead, define a function like 'function %ls; %ls $argv; end'. See the help section for the function command by typing 'help function'." ),
									   cmd+1,
									   val,
									   cmd );
							}
							else
							{
								debug( 0,
									   _(L"Variables may not be used as commands. Instead, define a function. See the help section for the function command by typing 'help function'." ),
									   cmd );
							}			
						}
						else if(wcschr( cmd, L'$' ))
						{
							debug( 0,
								   _(L"Commands may not contain variables. Use the eval builtin instead, like 'eval %ls'. See the help section for the eval command by typing 'help eval'." ),
								   cmd,
								   cmd );
						}
						else if( err!=ENOENT )
						{
							debug( 0,
								   _(L"The file '%ls' is not executable by this user"),
								   cmd?cmd:L"UNKNOWN" );
						}
						else
						{			
							debug( 0,
								   _(L"Unknown command '%ls'"),
								   cmd?cmd:L"UNKNOWN" );
						}
						
						tmp = current_tokenizer_pos;
						current_tokenizer_pos = tok_get_pos(tok);
						
						fwprintf( stderr, L"%ls", parser_t::current_line() );
						
						current_tokenizer_pos=tmp;

						job_set_flag( j, JOB_SKIP, 1 );
						event_fire_generic(L"fish_command_not_found", (wchar_t *)( args.at( 0 ).completion.c_str() ) );
						proc_set_last_status( err==ENOENT?STATUS_UNKNOWN_COMMAND:STATUS_NOT_EXECUTABLE );
				}
			}
		}
		
		if( (p->type == EXTERNAL) && !use_command )
		{
			error( SYNTAX_ERROR,
				   tok_get_pos( tok ),
				   UNKNOWN_BUILTIN_ERR_MSG,
				   args.back().completion.c_str() );
		}
	}
	
	
	if( is_new_block )
	{
		
		const wchar_t *end=parser_find_end( tok_string( tok ) +
											current_tokenizer_pos );
		tokenizer subtok;
		int make_sub_block = j->first_process != p;

		if( !end )
		{
			error( SYNTAX_ERROR,
				   tok_get_pos( tok ),
				   BLOCK_END_ERR_MSG );
			
		}
		else
		{
			
			if( !make_sub_block )
			{
				int done=0;
			
				for( tok_init( &subtok, end, 0 ); 
					 !done && tok_has_next( &subtok ); 
					 tok_next( &subtok ) )
				{
				
					switch( tok_last_type( &subtok ) )
					{
						case TOK_END:
							done = 1;
							break;
						
						case TOK_REDIRECT_OUT:
						case TOK_REDIRECT_NOCLOB:
						case TOK_REDIRECT_APPEND:
						case TOK_REDIRECT_IN:
						case TOK_REDIRECT_FD:
						case TOK_PIPE:
						{
							done = 1;
							make_sub_block = 1;
							break;
						}
					
						case TOK_STRING:
						{
							break;
						}
					
						default:
						{
							done = 1;
							error( SYNTAX_ERROR,
								   current_tokenizer_pos,
								   BLOCK_END_ERR_MSG );
						}
					}
				}
			
				tok_destroy( &subtok );
			}
		
			if( make_sub_block )
			{
			
				int end_pos = end-tok_string( tok );
                const wcstring sub_block(tok_string( tok ) + current_tokenizer_pos, end_pos - current_tokenizer_pos);
			
				p->type = INTERNAL_BLOCK;
				args.at( 0 ) =  completion_t(sub_block);
			
				tok_set_pos( tok,
							 end_pos );

				while( prev_block != current_block )
				{
					parser_t::pop_block();
				}

			}
			else tok_next( tok );
		}
		
	}
	else tok_next( tok );

	if( !error_code )
	{
		if( p->type == INTERNAL_BUILTIN && parser_keywords_skip_arguments(args.at(0).completion))
		{			
			if( !p->get_argv() )
                p->set_argv(completions_to_wcstring_list(args));
		}
		else
		{
			parse_job_argument_list(p, j, tok, args);
		}
	}

	if( !error_code )
	{
		if( !is_new_block )
		{
			current_block->had_command = 1;
		}
	}

	if( error_code )
	{
		/*
		  Make sure the block stack is consistent
		*/
		while( prev_block != current_block )
		{
			parser_t::pop_block();
		}
	}
	current_tokenizer_pos = prev_tokenizer_pos;	
	return !error_code;
}

/**
   Do skipped execution of command. This means that only limited
   execution of block level commands such as end and switch should be
   preformed.

   \param j the job to execute

*/
void parser_t::skipped_exec( job_t * j )
{
	process_t *p;

	for( p = j->first_process; p; p=p->next )
	{
		if( p->type == INTERNAL_BUILTIN )
		{
			if(( wcscmp( p->argv0(), L"for" )==0) ||
			   ( wcscmp( p->argv0(), L"switch" )==0) ||
			   ( wcscmp( p->argv0(), L"begin" )==0) ||
			   ( wcscmp( p->argv0(), L"function" )==0))
			{
				this->push_block( FAKE );
			}
			else if( wcscmp( p->argv0(), L"end" )==0)
			{
				if(!current_block->outer->skip )
				{
					exec( *this, j );
					return;
				}
				parser_t::pop_block();
			}
			else if( wcscmp( p->argv0(), L"else" )==0)
			{
				if( (current_block->type == IF ) &&
					(current_block->state1<int>() != 0))
				{
					exec( *this, j );
					return;
				}
			}
			else if( wcscmp( p->argv0(), L"case" )==0)
			{
				if( (current_block->type == SWITCH ) )
				{
					exec( *this, j );
					return;
				}
			}
		}
	}
	job_free( j );
}

/**
   Evaluates a job from the specified tokenizer. First calls
   parse_job to parse the job and then calls exec to execute it.

   \param tok The tokenizer to read tokens from
*/

void parser_t::eval_job( tokenizer *tok )
{
	job_t *j;

	int start_pos = job_start_pos = tok_get_pos( tok );
	long long t1=0, t2=0, t3=0;
    
    
    profile_item_t *profile_item = NULL;
	int skip = 0;
	int job_begin_pos, prev_tokenizer_pos;
	
	if( profile )
	{
        profile_items.resize(profile_items.size() + 1);
        profile_item = &profile_items.back();
        profile_item->cmd = L"";
        profile_item->skipped = 1;        
		t1 = get_time();
	}

	switch( tok_last_type( tok ) )
	{
		case TOK_STRING:
		{
			j = job_create();
			job_set_flag( j, JOB_FOREGROUND, 1 );
			job_set_flag( j, JOB_TERMINAL, job_get_flag( j, JOB_CONTROL ) );
			job_set_flag( j, JOB_TERMINAL, job_get_flag( j, JOB_CONTROL ) \
							               && (!is_subshell && !is_event));
			job_set_flag( j, JOB_SKIP_NOTIFICATION, is_subshell \
							                        || is_block \
													|| is_event \
													|| (!is_interactive));
			
			current_block->job = j;
			
			if( is_interactive )
			{
				if( tcgetattr (0, &j->tmodes) )
				{
					tok_next( tok );
					wperror( L"tcgetattr" );
					job_free( j );
					break;
				}
			}

			j->first_process = new process_t();

			job_begin_pos = tok_get_pos( tok );
			
			if( parse_job( j->first_process, j, tok ) &&
				j->first_process->get_argv() )
			{
				if( job_start_pos < tok_get_pos( tok ) )
				{
					int stop_pos = tok_get_pos( tok );
					wchar_t *newline = wcschr(  tok_string(tok)+start_pos,
												L'\n' );
					if( newline )
						stop_pos = mini( stop_pos, newline - tok_string(tok) );

					j->command = wcstring(tok_string(tok)+start_pos, stop_pos-start_pos);
				}
				else
					j->command = L"";

				if( profile )
				{
					t2 = get_time();
					profile_item->cmd = wcsdup( j->command_cstr() );
					profile_item->skipped=current_block->skip;
				}
				
				skip |= current_block->skip;
				skip |= job_get_flag( j, JOB_WILDCARD_ERROR );
				skip |= job_get_flag( j, JOB_SKIP );
				
				if(!skip )
				{
					int was_builtin = 0;
//					if( j->first_process->type==INTERNAL_BUILTIN && !j->first_process->next)
//						was_builtin = 1;
					prev_tokenizer_pos = current_tokenizer_pos;
					current_tokenizer_pos = job_begin_pos;		
					exec( *this, j );
					current_tokenizer_pos = prev_tokenizer_pos;
					
					/* Only external commands require a new fishd barrier */
					if( !was_builtin )
						proc_had_barrier=0;
				}
				else
				{
					this->skipped_exec( j );
				}

				if( profile )
				{
					t3 = get_time();
					profile_item->level=eval_level;
					profile_item->parse = t2-t1;
					profile_item->exec=t3-t2;
				}

				if( current_block->type == WHILE )
				{

					switch( current_block->state1<int>() )
					{
						case WHILE_TEST_FIRST:
						{
							current_block->skip = proc_get_last_status()!= 0;
							current_block->state1<int>()=WHILE_TESTED;
						}
						break;
					}
				}

				if( current_block->type == IF )
				{
					if( (!current_block->state1<int>()) &&
						(!current_block->skip) )
					{
						current_block->skip = proc_get_last_status()!= 0;
						current_block->state1<int>()++;
					}
				}

			}
			else
			{
				/*
				  This job could not be properly parsed. We free it
				  instead, and set the status to 1. This should be
				  rare, since most errors should be detected by the
				  ahead of time validator.
				*/
				job_free( j );

				proc_set_last_status( 1 );
			}
			current_block->job = 0;
			break;
		}

		case TOK_END:
		{
			if( tok_has_next( tok ))
				tok_next( tok );
			break;
		}

		case TOK_BACKGROUND:
		{
			wchar_t *str = tok_string( tok );
			if( tok_get_pos(tok)>0 && str[tok_get_pos(tok)-1] == L'&' )
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( tok ),
					   CMD_AND_ERR_MSG,
					   tok_get_desc( tok_last_type(tok) ) );
			}
			else
			{
				error( SYNTAX_ERROR,
					   tok_get_pos( tok ),
					   CMD_ERR_MSG,
					   tok_get_desc( tok_last_type(tok) ) );
			}

			return;
		}

		case TOK_ERROR:
		{
			error( SYNTAX_ERROR,
				   tok_get_pos( tok ),
				   TOK_ERR_MSG,
				   tok_last(tok) );

			return;
		}

		default:
		{
			error( SYNTAX_ERROR,
				   tok_get_pos( tok ),
				   CMD_ERR_MSG,
				   tok_get_desc( tok_last_type(tok)) );

			return;
		}
	}

	job_reap( 0 );

}

int parser_t::eval( const wcstring &cmdStr, io_data_t *io, enum block_type_t block_type )
{
    const wchar_t * const cmd = cmdStr.c_str();
	size_t forbid_count;
	int code;
	tokenizer *previous_tokenizer=current_tokenizer;
	block_t *start_current_block = current_block;
	io_data_t *prev_io = block_io;
    std::vector<wcstring> prev_forbidden = forbidden_function;

	if( block_type == SUBST )
	{
		forbidden_function.resize(0);
	}
	
	CHECK_BLOCK( 1 );
	
	forbid_count = forbidden_function.size();

	block_io = io;

	job_reap( 0 );

	debug( 4, L"eval: %ls", cmd );

	if( !cmd )
	{
		debug( 1,
			   EVAL_NULL_ERR_MSG );
		bugreport();
		return 1;
	}

	if( (block_type != TOP) &&
		(block_type != SUBST))
	{
		debug( 1,
			   INVALID_SCOPE_ERR_MSG,
			   parser_t::get_block_desc( block_type ) );
		bugreport();
		return 1;
	}

	eval_level++;

	this->push_block( block_type );

	current_tokenizer = (tokenizer *)malloc( sizeof(tokenizer));
	tok_init( current_tokenizer, cmd, 0 );

	error_code = 0;

	event_fire( NULL );

	while( tok_has_next( current_tokenizer ) &&
		   !error_code &&
		   !sanity_check() &&
		   !exit_status() )
	{
		this->eval_job( current_tokenizer );
		event_fire( NULL );
	}

	int prev_block_type = current_block->type;

	parser_t::pop_block();

	while( start_current_block != current_block )
	{
		if( current_block == 0 )
		{
			debug( 0,
				   _(L"End of block mismatch. Program terminating.") );
			bugreport();
			FATAL_EXIT();
			break;
		}

		if( (!error_code) && (!exit_status()) && (!proc_get_last_status()) )
		{

			//debug( 2, L"Status %d\n", proc_get_last_status() );

			debug( 1,
				   L"%ls", parser_t::get_block_desc( current_block->type ) );
			debug( 1,
				   BLOCK_END_ERR_MSG );
			fwprintf( stderr, L"%ls", parser_t::current_line() );

			const wcstring h = builtin_help_get( *this, L"end" );
			if( h.size() )
				fwprintf( stderr, L"%ls", h.c_str() );
			break;

		}
		prev_block_type = current_block->type;
		parser_t::pop_block();
	}

	this->print_errors_stderr();

	tok_destroy( current_tokenizer );
	free( current_tokenizer );

    while (forbidden_function.size() > forbid_count)
		parser_t::allow_function();

	/*
	  Restore previous eval state
	*/
	forbidden_function = prev_forbidden;
	current_tokenizer=previous_tokenizer;
	block_io = prev_io;
	eval_level--;

	code=error_code;
	error_code=0;

	job_reap( 0 );

	return code;
}


/**
   \return the block type created by the specified builtin, or -1 on error.
*/
int parser_get_block_type( const wcstring &cmd )
{
	int i;
	
	for( i=0; block_lookup[i].desc; i++ )
	{
		if( block_lookup[i].name && cmd == block_lookup[i].name )
		{
			return block_lookup[i].type;
		}
	}
	return -1;
}

/**
   \return the block command that createa the specified block type, or null on error.
*/
const wchar_t *parser_get_block_command( int type )
{
	int i;
	
	for( i=0; block_lookup[i].desc; i++ )
	{
		if( block_lookup[i].type == type )
		{
			return block_lookup[i].name;
		}
	}
	return 0;
}

/**
   Test if this argument contains any errors. Detected errors include
   syntax errors in command substitutions, improperly escaped
   characters and improper use of the variable expansion operator.
*/
int parser_t::parser_test_argument( const wchar_t *arg, string_buffer_t *out, const wchar_t *prefix, int offset )
{
	wchar_t *unesc;
	wchar_t *pos;
	int err=0;
	
	wchar_t *paran_begin, *paran_end;
	wchar_t *arg_cpy;
	int do_loop = 1;
	
	CHECK( arg, 1 );
		
	arg_cpy = wcsdup( arg );
	
	while( do_loop )
	{
		switch( parse_util_locate_cmdsubst(arg_cpy,
										   &paran_begin,
										   &paran_end,
										   0 ) )
		{
			case -1:
				err=1;
				if( out )
				{
					error( SYNTAX_ERROR,
						   offset,
						   L"Mismatched parans" );
					this->print_errors( out, prefix);
				}
				free( arg_cpy );
				return 1;
				
			case 0:
				do_loop = 0;
				break;

			case 1:
			{
				
				wchar_t *subst = wcsndup( paran_begin+1, paran_end-paran_begin-1 );
				string_buffer_t tmp;
				sb_init( &tmp );
				
				sb_append_substring( &tmp, arg_cpy, paran_begin - arg_cpy);
				sb_append_char( &tmp, INTERNAL_SEPARATOR);
				sb_append( &tmp, paran_end+1);
				
//				debug( 1, L"%ls -> %ls %ls", arg_cpy, subst, tmp.buff );
	
				err |= parser_t::test( subst, 0, out, prefix );
				
				free( subst );
				free( arg_cpy );
				arg_cpy = (wchar_t *)tmp.buff;
				
				/*
				  Do _not_ call sb_destroy on this stringbuffer - it's
				  buffer is used as the new 'arg_cpy'. It is free'd at
				  the end of the loop.
				*/
				break;
			}			
		}
	}

	unesc = unescape( arg_cpy, 1 );
	if( !unesc )
	{
		if( out )
		{
			error( SYNTAX_ERROR,
				   offset,
				   L"Invalid token '%ls'", arg_cpy );
			print_errors( out, prefix);
		}
		return 1;
	}
	else
	{	
		/*
		  Check for invalid variable expansions
		*/
		for( pos = unesc; *pos; pos++ )
		{
			switch( *pos )
			{
				case VARIABLE_EXPAND:
				case VARIABLE_EXPAND_SINGLE:
				{
					wchar_t n = *(pos+1);
						
					if( n != VARIABLE_EXPAND &&
						n != VARIABLE_EXPAND_SINGLE &&
						!wcsvarchr(n) )
					{
						err=1;
						if( out )
						{
							expand_variable_error( *this, unesc, pos-unesc, offset );
							print_errors( out, prefix);
						}
					}
				
					break;
				}
			}		
		}
	}

	free( arg_cpy );
		
	free( unesc );
	return err;
	
}

int parser_t::test_args(const  wchar_t * buff,
					 string_buffer_t *out, const wchar_t *prefix )
{
	tokenizer tok;
	tokenizer *previous_tokenizer = current_tokenizer;
	int previous_pos = current_tokenizer_pos;
	int do_loop = 1;
	int err = 0;
	
	CHECK( buff, 1 );
		
	current_tokenizer = &tok;
	
	for( tok_init( &tok, buff, 0 );
		 do_loop && tok_has_next( &tok );
		 tok_next( &tok ) )
	{
		current_tokenizer_pos = tok_get_pos( &tok );
		switch( tok_last_type( &tok ) )
		{
			
			case TOK_STRING:
			{
				err |= parser_test_argument( tok_last( &tok ), out, prefix, tok_get_pos( &tok ) );
				break;
			}
			
			case TOK_END:
			{
				break;
			}
			
			case TOK_ERROR:
			{
				if( out )
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( &tok ),
						   TOK_ERR_MSG,
						   tok_last(&tok) );
					print_errors( out, prefix );
				}
				err=1;
				do_loop=0;
				break;
			}
			
			default:
			{
				if( out )
				{
					error( SYNTAX_ERROR,
						   tok_get_pos( &tok ),
						   UNEXPECTED_TOKEN_ERR_MSG,
						   tok_get_desc( tok_last_type(&tok)) );
					print_errors( out, prefix );
				}
				err=1;				
				do_loop=0;
				break;
			}			
		}
	}
	
	tok_destroy( &tok );
	
	current_tokenizer=previous_tokenizer;
	current_tokenizer_pos = previous_pos;
	
	error_code=0;
	
	return err;
}

int parser_t::test( const  wchar_t * buff,
				 int *block_level, 
				 string_buffer_t *out,
				 const wchar_t *prefix )
{
    ASSERT_IS_MAIN_THREAD();
    
	tokenizer tok;
	/* 
	   Set to one if a command name has been given for the currently
	   parsed process specification 
	*/
	int had_cmd=0; 
	int count = 0;
	int err=0;
	int unfinished = 0;
	
	tokenizer *previous_tokenizer=current_tokenizer;
	int previous_pos=current_tokenizer_pos;
	static int block_pos[BLOCK_MAX_COUNT];
	static int block_type[BLOCK_MAX_COUNT];
	int res = 0;
	
	/*
	  Set to 1 if the current command is inside a pipeline
	*/
	int is_pipeline = 0;

	/*
	  Set to one if the currently specified process can not be used inside a pipeline
	*/
	int forbid_pipeline = 0;

	/* 
	   Set to one if an additional process specification is needed 
	*/
	int needs_cmd=0; 

	/*
	  halloc context used for calls to expand() and other memory
	  allocations. Free'd at end of this function.
	*/
	void *context;

	/*
	  Counter on the number of arguments this function has encountered
	  so far. Is set to -1 when the count is unknown, i.e. after
	  encountering an argument that contains substitutions that can
	  expand to more/less arguemtns then 1.
	*/
	int arg_count=0;
	
	/*
	  The currently validated command.
	*/
    wcstring command;
    bool has_command = false;
		
	CHECK( buff, 1 );

	if( block_level )
	{
		int i;
		int len = wcslen(buff);
		for( i=0; i<len; i++ )
		{
			block_level[i] = -1;
		}
		
	}
		
	context = halloc( 0, 0 );
	current_tokenizer = &tok;

	for( tok_init( &tok, buff, 0 );
		 ;
		 tok_next( &tok ) )
	{
		current_tokenizer_pos = tok_get_pos( &tok );

		int last_type = tok_last_type( &tok );
		int end_of_cmd = 0;
		
		switch( last_type )
		{
			case TOK_STRING:
			{
				if( !had_cmd )
				{
					int is_else;
					int mark = tok_get_pos( &tok );
					had_cmd = 1;
					arg_count=0;
					
                    command = tok_last(&tok);
                    has_command = expand_one(command, EXPAND_SKIP_CMDSUBST | EXPAND_SKIP_VARIABLES);
					if( !has_command )
					{
                        command = L"";
						err=1;
						if( out )
						{
							error( SYNTAX_ERROR,
								   tok_get_pos( &tok ),
								   ILLEGAL_CMD_ERR_MSG,
								   tok_last( &tok ) );
							
							print_errors( out, prefix );
						}
						break;
					}
					
					if( needs_cmd )
					{
						/*
						  end is not a valid command when a followup
						  command is needed, such as after 'and' or
						  'while'
						*/
						if( contains( command,
									  L"end" ) )
						{
							err=1;
							if( out )
							{
								error( SYNTAX_ERROR,
									   tok_get_pos( &tok ),
									   COND_ERR_MSG );
								
								print_errors( out, prefix );
							}
						}
						
						needs_cmd=0;
					}
					
					/*
					  Decrement block count on end command
					*/
					if( command == L"end")
					{
						tok_next( &tok );
						count--;
						tok_set_pos( &tok, mark );
					}

					is_else = (command == L"else");
					
					/*
					  Store the block level. This needs to be done
					  _after_ checking for end commands, but _before_
					  shecking for block opening commands.
					*/
					if( block_level )
					{
						block_level[tok_get_pos( &tok )] = count + (is_else?-1:0);
					}
					
					/*
					  Handle block commands
					*/
					if( parser_keywords_is_block( command ) )
					{
						if( count >= BLOCK_MAX_COUNT )
						{
							error( SYNTAX_ERROR,
								   tok_get_pos( &tok ),
								   BLOCK_ERR_MSG );

							print_errors( out, prefix );
						}
						else
						{
							block_type[count] = parser_get_block_type( command );
							block_pos[count] = current_tokenizer_pos;
							tok_next( &tok );
							count++;
							tok_set_pos( &tok, mark );
						}
					}

					/*
					  If parser_keywords_is_subcommand is true, the command
					  accepts a second command as it's first
					  argument. If parser_skip_arguments is true, the
					  second argument is optional.
					*/
					if( parser_keywords_is_subcommand( command ) && !parser_keywords_skip_arguments(command ) )
					{
						needs_cmd = 1;
						had_cmd = 0;
					}
					
					if( contains( command,
								  L"or",
								  L"and" ) )
					{
						/*
						  'or' and 'and' can not be used inside pipelines
						*/
						if( is_pipeline )
						{
							err=1;
							if( out )
							{
								error( SYNTAX_ERROR,
									   tok_get_pos( &tok ),
									   EXEC_ERR_MSG );

								print_errors( out, prefix );

							}
						}
					}
					
					/*
					  There are a lot of situations where pipelines
					  are forbidden, including when using the exec
					  builtin.
					*/
					if( parser_is_pipe_forbidden( command ) )
					{
						if( is_pipeline )
						{
							err=1;
							if( out )
							{
								error( SYNTAX_ERROR,
									   tok_get_pos( &tok ),
									   EXEC_ERR_MSG );

								print_errors( out, prefix );

							}
						}
						forbid_pipeline = 1;
					}

					/*
					  Test that the case builtin is only used directly in a switch block
					*/
					if( command == L"case" )
					{
						if( !count || block_type[count-1]!=SWITCH )
						{
							err=1;

							if( out )
							{
								error( SYNTAX_ERROR,
									   tok_get_pos( &tok ),
									   INVALID_CASE_ERR_MSG );

								print_errors( out, prefix);
								const wcstring h = builtin_help_get( *this, L"case" );
								if( h.size() )
									sb_printf( out, L"%ls", h.c_str() );
							}
						}
					}

					/*
					  Test that the return bultin is only used within function definitions
					*/
					if( command == L"return" )
					{
						int found_func=0;
						int i;
						for( i=count-1; i>=0; i-- )
						{
							if( block_type[i]==FUNCTION_DEF )
							{
								found_func=1;
								break;
							}
						}

						if( !found_func )
						{
							/*
							  Peek to see if the next argument is
							  --help, in which case we'll allow it to
							  show the help.
							*/

							int old_pos = tok_get_pos( &tok );
							int is_help = 0;
							
							tok_next( &tok );
							if( tok_last_type( &tok ) == TOK_STRING )
							{
                                wcstring first_arg = tok_last(&tok);
                                if (expand_one(first_arg, EXPAND_SKIP_CMDSUBST) && parser_t::is_help( first_arg.c_str(), 3))
								{
									is_help = 1;
								}
							}
							
							tok_set_pos( &tok, old_pos );
							
							if( !is_help )
							{
								err=1;

								if( out )
								{
									error( SYNTAX_ERROR,
										   tok_get_pos( &tok ),
										   INVALID_RETURN_ERR_MSG );
									print_errors( out, prefix );
								}
							}
						}


					}
					

					/*
					  Test that break and continue are only used within loop blocks
					*/
					if( contains( command, L"break", L"continue" ) )
					{
						int found_loop=0;
						int i;
						for( i=count-1; i>=0; i-- )
						{
							if( (block_type[i]==WHILE) ||
								(block_type[i]==FOR) )
							{
								found_loop=1;
								break;
							}
						}

						if( !found_loop )
						{
							/*
							  Peek to see if the next argument is
							  --help, in which case we'll allow it to
							  show the help.
							*/

							int old_pos = tok_get_pos( &tok );
							int is_help = 0;
							
							tok_next( &tok );
							if( tok_last_type( &tok ) == TOK_STRING )
							{
                                wcstring first_arg = tok_last( &tok );
								if( expand_one(first_arg, EXPAND_SKIP_CMDSUBST) && parser_t::is_help( first_arg.c_str(), 3 ) ) 
								{
									is_help = 1;
								}
							}
							
							tok_set_pos( &tok, old_pos );
							
							if( !is_help )
							{
								err=1;

								if( out )
								{
									error( SYNTAX_ERROR,
										   tok_get_pos( &tok ),
										   INVALID_LOOP_ERR_MSG );
									print_errors( out, prefix );
								}
							}
						}
						
					}

					/*
					  Test that else is only used directly in an if-block
					*/
					if( command == L"else" )
					{
						if( !count || block_type[count-1]!=IF )
						{
							err=1;
							if( out )
							{
								error( SYNTAX_ERROR,
									   tok_get_pos( &tok ),
									   INVALID_ELSE_ERR_MSG );

								print_errors( out, prefix );
							}
						}

					}

					/*
					  Test that end is not used when not inside any block
					*/
					if( count < 0 )
					{
						err = 1;
						if( out )
						{
							error( SYNTAX_ERROR,
								   tok_get_pos( &tok ),
								   INVALID_END_ERR_MSG );
							print_errors( out, prefix );
							const wcstring h = builtin_help_get( *this, L"end" );
							if( h.size() )
								sb_printf( out, L"%ls", h.c_str() );
						}
					}
					
				}
				else
				{
					err |= parser_test_argument( tok_last( &tok ), out, prefix, tok_get_pos( &tok ) );
					
					/*
					  If possible, keep track of number of supplied arguments
					*/
					if( arg_count >= 0 && expand_is_clean( tok_last( &tok ) ) )
					{
						arg_count++;
					}
					else
					{
						arg_count = -1;
					}
					
					if( has_command )
					{
						
						/*
						  Try to make sure the second argument to 'for' is 'in'
						*/
						if( command == L"for"  )
						{
							if( arg_count == 1 )
							{
								
								if( wcsvarname( tok_last( &tok )) )
								{
									
									err = 1;
										
									if( out )
									{
										error( SYNTAX_ERROR,
											   tok_get_pos( &tok ),
											   BUILTIN_FOR_ERR_NAME,
											   L"for",
											   tok_last( &tok ) );
										
										print_errors( out, prefix );
									}									
								}
															
							}
							else if( arg_count == 2 )
							{
								if( wcscmp( tok_last( &tok ), L"in" ) != 0 )
								{
									err = 1;
									
									if( out )
									{
										error( SYNTAX_ERROR,
											   tok_get_pos( &tok ),
											   BUILTIN_FOR_ERR_IN,
											   L"for" );
										
										print_errors( out, prefix );
									}
								}
							}
						}
					}

				}
				
				break;
			}

			case TOK_REDIRECT_OUT:
			case TOK_REDIRECT_IN:
			case TOK_REDIRECT_APPEND:
			case TOK_REDIRECT_FD:
			case TOK_REDIRECT_NOCLOB:
			{
				if( !had_cmd )
				{
					err = 1;
					if( out )
					{
						error( SYNTAX_ERROR,
							   tok_get_pos( &tok ),
							   INVALID_REDIRECTION_ERR_MSG );
						print_errors( out, prefix );
					}
				}
				break;
			}

			case TOK_END:
			{
				if( needs_cmd && !had_cmd )
				{
					err = 1;
					if( out )
					{
						error( SYNTAX_ERROR,
							   tok_get_pos( &tok ),
							   CMD_ERR_MSG,
							   tok_get_desc( tok_last_type(&tok)));
						print_errors( out, prefix );
					}
				}
				needs_cmd=0;
				had_cmd = 0;
				is_pipeline=0;
				forbid_pipeline=0;
				end_of_cmd = 1;
				
				break;
			}

			case TOK_PIPE:
			{
				if( !had_cmd )
				{
					err=1;
					if( out )
					{
						if( tok_get_pos(&tok)>0 && buff[tok_get_pos(&tok)-1] == L'|' )
						{
							error( SYNTAX_ERROR,
								   tok_get_pos( &tok ),
								   CMD_OR_ERR_MSG,
								   tok_get_desc( tok_last_type(&tok) ) );
							
						}
						else
						{
							error( SYNTAX_ERROR,
								   tok_get_pos( &tok ),
								   CMD_ERR_MSG,
								   tok_get_desc( tok_last_type(&tok)));
						}
						
						print_errors( out, prefix );
					}
				}
				else if( forbid_pipeline )
				{
					err=1;
					if( out )
					{
						error( SYNTAX_ERROR,
							   tok_get_pos( &tok ),
							   EXEC_ERR_MSG );
						
						print_errors( out, prefix );
					}
				}
				else
				{
					needs_cmd=1;
					is_pipeline=1;
					had_cmd=0;
					end_of_cmd = 1;
					
				}
				break;
			}
			
			case TOK_BACKGROUND:
			{
				if( !had_cmd )
				{
					err = 1;
					if( out )
					{
						if( tok_get_pos(&tok)>0 && buff[tok_get_pos(&tok)-1] == L'&' )
						{
							error( SYNTAX_ERROR,
								   tok_get_pos( &tok ),
								   CMD_AND_ERR_MSG,
								   tok_get_desc( tok_last_type(&tok) ) );
							
						}
						else
						{
							error( SYNTAX_ERROR,
								   tok_get_pos( &tok ),
								   CMD_ERR_MSG,
								   tok_get_desc( tok_last_type(&tok)));
						}
						
						print_errors( out, prefix );
					}
				}
				
				had_cmd = 0;
				end_of_cmd = 1;
				
				break;
			}

			case TOK_ERROR:
			default:
				if( tok_get_error( &tok ) == TOK_UNTERMINATED_QUOTE )
				{
					unfinished = 1;
				}
				else
				{
					err = 1;
					if( out )
					{
						error( SYNTAX_ERROR,
							   tok_get_pos( &tok ),
							   TOK_ERR_MSG,
							   tok_last(&tok) );
						
						
						print_errors( out, prefix );
					}
				}
				
				break;
		}

		if( end_of_cmd )
		{
			if( has_command && command == L"for" )
			{
				if( arg_count >= 0 && arg_count < 2 )
				{
					/*
					  Not enough arguments to the for builtin
					*/
					err = 1;
					
					if( out )
					{
						error( SYNTAX_ERROR,
							   tok_get_pos( &tok ),
							   BUILTIN_FOR_ERR_COUNT,
							   L"for",
							   arg_count );
						
						print_errors( out, prefix );
					}
				}
				
			}
			
		}
		
		if( !tok_has_next( &tok ) )
			break;
		
	}

	if( needs_cmd )
	{
		err=1;
		if( out )
		{
			error( SYNTAX_ERROR,
				   tok_get_pos( &tok ),
				   COND_ERR_MSG );

			print_errors( out, prefix );
		}
	}


	if( out && count>0 )
	{
		const wchar_t *cmd;
		
		error( SYNTAX_ERROR,
			   block_pos[count-1],
			   BLOCK_END_ERR_MSG );

		print_errors( out, prefix );

		cmd = parser_get_block_command(  block_type[count -1] );
		if( cmd )
		{
			const wcstring h = builtin_help_get( *this, cmd );
			if( h.size() )
			{
				sb_printf( out, L"%ls", h.c_str() );
			}
		}
		
		
	}

	/*
	  Fill in the unset block_level entries. Until now, only places
	  where the block level _changed_ have been filled out. This fills
	  in the rest.
	*/

	if( block_level )
	{
		int last_level = 0;
		int i, j;
		int len = wcslen(buff);
		for( i=0; i<len; i++ )
		{
			if( block_level[i] >= 0 )
			{
				last_level = block_level[i];
				/*
				  Make all whitespace before a token have the new
				  level. This avoid using the wrong indentation level
				  if a new line starts with whitespace.
				*/
				for( j=i-1; j>=0; j-- )
				{
					if( !wcschr( L" \n\t\r", buff[j] ) )
						break;
					block_level[j] = last_level;
				}
			}
			block_level[i] = last_level;
		}

		/*
		  Make all trailing whitespace have the block level that the
		  validator had at exit. This makes sure a new line is
		  correctly indented even if it is empty.
		*/
		for( j=len-1; j>=0; j-- )
		{
			if( !wcschr( L" \n\t\r", buff[j] ) )
				break;
			block_level[j] = count;
		}
		

	}		

	/*
	  Calculate exit status
	*/
	if( count!= 0 )
		unfinished = 1;

	if( err )
		res |= PARSER_TEST_ERROR;

	if( unfinished )
		res |= PARSER_TEST_INCOMPLETE;

	/*
	  Cleanup
	*/
	
	halloc_free( context );
	
	tok_destroy( &tok );

	current_tokenizer=previous_tokenizer;
	current_tokenizer_pos = previous_pos;
	
	error_code=0;
	

	return res;
	
}

