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
#include <algorithm>

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
#include "path.h"
#include "signal.h"
#include "complete.h"
#include "parse_tree.h"

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
   Error message on a function that calls itself immediately
*/
#define INFINITE_RECURSION_ERR_MSG _( L"The function calls itself immediately, which would result in an infinite loop.")

/**
   Error message on reaching maximum recursion depth
*/
#define OVERFLOW_RECURSION_ERR_MSG _( L"Maximum recursion depth reached. Accidental infinite loop?")

/**
   Error message used when the end of a block can't be located
*/
#define BLOCK_END_ERR_MSG _( L"Could not locate end of block. The 'end' command is missing, misspelled or a ';' is missing.")

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
#define WILDCARD_ERR_MSG _( L"No matches for wildcard '%ls'.")

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
#define INVALID_ELSE_ERR_MSG _( L"'%ls' builtin not inside of if block" )

/**
   Error when using 'else if' past a naked 'else'
*/
#define INVALID_ELSEIF_PAST_ELSE_ERR_MSG _( L"'%ls' used past terminating 'else'" )

/**
   Error when using end builtin outside of block
*/
#define INVALID_END_ERR_MSG _( L"'end' command outside of block")

/**
   Error message for Posix-style assignment: foo=bar
*/
#define COMMAND_ASSIGN_ERR_MSG _( L"Unknown command '%ls'. Did you mean 'set %ls %ls'? See the help section on the set command by typing 'help set'.")

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
    block_type_t type;

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
    { WHILE, L"while", WHILE_BLOCK },
    { FOR, L"for", FOR_BLOCK },
    { IF, L"if", IF_BLOCK },
    { FUNCTION_DEF, L"function", FUNCTION_DEF_BLOCK },
    { FUNCTION_CALL, 0, FUNCTION_CALL_BLOCK },
    { FUNCTION_CALL_NO_SHADOW, 0, FUNCTION_CALL_NO_SHADOW_BLOCK },
    { SWITCH, L"switch", SWITCH_BLOCK },
    { FAKE, 0, FAKE_BLOCK },
    { TOP, 0, TOP_BLOCK },
    { SUBST, 0, SUBST_BLOCK },
    { BEGIN, L"begin", BEGIN_BLOCK },
    { SOURCE, L".", SOURCE_BLOCK },
    { EVENT, 0, EVENT_BLOCK },
    { BREAKPOINT, L"breakpoint", BREAKPOINT_BLOCK },
    { (block_type_t)0, 0, 0 }
};

static bool job_should_skip_elseif(const job_t *job, const block_t *current_block);

// Given a file path, return something nicer. Currently we just "unexpand" tildes.
static wcstring user_presentable_path(const wcstring &path)
{
    return replace_home_directory_with_tilde(path);
}


parser_t::parser_t(enum parser_type_t type, bool errors) :
    parser_type(type),
    show_errors(errors),
    error_code(0),
    err_pos(0),
    current_tokenizer(NULL),
    current_tokenizer_pos(0),
    job_start_pos(0),
    eval_level(-1),
    block_io(shared_ptr<io_data_t>())
{
}

/* A pointer to the principal parser (which is a static local) */
static parser_t *s_principal_parser = NULL;

parser_t &parser_t::principal_parser(void)
{
    ASSERT_IS_NOT_FORKED_CHILD();
    ASSERT_IS_MAIN_THREAD();
    static parser_t parser(PARSER_TYPE_GENERAL, true);
    if (! s_principal_parser)
    {
        s_principal_parser = &parser;
    }
    return parser;
}

void parser_t::skip_all_blocks(void)
{
    /* Tell all blocks to skip */
    if (s_principal_parser)
    {
        //write(2, "Cancelling blocks\n", strlen("Cancelling blocks\n"));
        for (size_t i=0; i < s_principal_parser->block_count(); i++)
        {
            s_principal_parser->block_at_index(i)->skip = true;
        }
    }
}

void parser_t::push_block(block_t *new_current)
{
    const enum block_type_t type = new_current->type();
    new_current->src_lineno = parser_t::get_lineno();
    new_current->src_filename = parser_t::current_filename()?intern(parser_t::current_filename()):0;

    const block_t *old_current = this->current_block();
    if (old_current && old_current->skip)
        new_current->mark_as_fake();

    /*
      New blocks should be skipped if the outer block is skipped,
      except TOP ans SUBST block, which open up new environments. Fake
      blocks should always be skipped. Rather complicated... :-(
    */
    new_current->skip = old_current ? old_current->skip : 0;

    /*
      Type TOP and SUBST are never skipped
    */
    if (type == TOP || type == SUBST)
    {
        new_current->skip = 0;
    }

    /*
      Fake blocks and function definition blocks are never executed
    */
    if (type == FAKE || type == FUNCTION_DEF)
    {
        new_current->skip = 1;
    }

    new_current->job = 0;
    new_current->loop_status=LOOP_NORMAL;
    
    this->block_stack.push_back(new_current);

    if ((new_current->type() != FUNCTION_DEF) &&
            (new_current->type() != FAKE) &&
            (new_current->type() != TOP))
    {
        env_push(type == FUNCTION_CALL);
        new_current->wants_pop_env = true;
    }
}

void parser_t::pop_block()
{
    if (block_stack.empty())
    {
        debug(1,
              L"function %s called on empty block stack.",
              __func__);
        bugreport();
        return;
    }

    block_t *old = block_stack.back();
    block_stack.pop_back();

    if (old->wants_pop_env)
        env_pop();

    delete old;
}

const wchar_t *parser_t::get_block_desc(int block) const
{
    for (size_t i=0; block_lookup[i].desc; i++)
    {
        if (block_lookup[i].type == block)
        {
            return _(block_lookup[i].desc);
        }
    }
    return _(UNKNOWN_BLOCK);
}

const block_t *parser_t::block_at_index(size_t idx) const
{
    /* 0 corresponds to the last element in our vector */
    size_t count = block_stack.size();
    return idx < count ? block_stack.at(count - idx - 1) : NULL;
}

block_t *parser_t::block_at_index(size_t idx)
{
    size_t count = block_stack.size();
    return idx < count ? block_stack.at(count - idx - 1) : NULL;
}

const block_t *parser_t::current_block() const
{
    return block_stack.empty() ? NULL : block_stack.back();
}

block_t *parser_t::current_block()
{
    return block_stack.empty() ? NULL : block_stack.back();
}


/**
   Returns 1 if the specified command is a builtin that may not be used in a pipeline
*/
static int parser_is_pipe_forbidden(const wcstring &word)
{
    return contains(word,
                    L"exec",
                    L"case",
                    L"break",
                    L"return",
                    L"continue");
}

/**
   Search the text for the end of the current block
*/
static const wchar_t *parser_find_end(const wchar_t * buff)
{
    int had_cmd=0;
    int count = 0;
    int error=0;
    int mark=0;

    CHECK(buff, 0);

    tokenizer_t tok(buff, 0);
    for (; tok_has_next(&tok) && !error; tok_next(&tok))
    {
        int last_type = tok_last_type(&tok);
        switch (last_type)
        {
            case TOK_STRING:
            {
                if (!had_cmd)
                {
                    if (wcscmp(tok_last(&tok), L"end")==0)
                    {
                        count--;
                    }
                    else if (parser_keywords_is_block(tok_last(&tok)))
                    {
                        count++;
                    }

                    if (count < 0)
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
                if (had_cmd)
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
        if (!count)
        {
            tok_next(&tok);
            mark = tok_get_pos(&tok);
            break;
        }

    }
    if (!count && !error)
    {

        return buff+mark;
    }
    return 0;

}


void parser_t::forbid_function(const wcstring &function)
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

void parser_t::error(int ec, size_t p, const wchar_t *str, ...)
{
    va_list va;

    CHECK(str,);

    error_code = ec;
    
    // note : p may be -1
    err_pos = static_cast<int>(p);

    va_start(va, str);
    err_buff = vformat_string(str, va);
    va_end(va);
}

/**
   Print profiling information to the specified stream
*/
static void print_profile(const std::vector<profile_item_t*> &items,
                          FILE *out)
{
    size_t pos;
    for (pos = 0; pos < items.size(); pos++)
    {
        const profile_item_t *me, *prev;
        size_t i;
        int my_time;

        me = items.at(pos);
        if (!me->skipped)
        {
            my_time=me->parse+me->exec;

            for (i=pos+1; i<items.size(); i++)
            {
                prev = items.at(i);
                if (prev->skipped)
                {
                    continue;
                }

                if (prev->level <= me->level)
                {
                    break;
                }

                if (prev->level > me->level+1)
                {
                    continue;
                }

                my_time -= prev->parse;
                my_time -= prev->exec;
            }

            if (me->cmd.size() > 0)
            {
                if (fwprintf(out, L"%d\t%d\t", my_time, me->parse+me->exec) < 0)
                {
                    wperror(L"fwprintf");
                    return;
                }

                for (i=0; i<me->level; i++)
                {
                    if (fwprintf(out, L"-") < 0)
                    {
                        wperror(L"fwprintf");
                        return;
                    }

                }
                if (fwprintf(out, L"> %ls\n", me->cmd.c_str()) < 0)
                {
                    wperror(L"fwprintf");
                    return;
                }

            }
            delete me;
        }
    }
}

void parser_t::destroy()
{
    if (profile)
    {
        /* Save profiling information. OK to not use CLO_EXEC here because this is called while fish is dying (and hence will not fork) */
        FILE *f = fopen(profile, "w");
        if (!f)
        {
            debug(1,
                  _(L"Could not write profiling information to file '%s'"),
                  profile);
        }
        else
        {
            if (fwprintf(f,
                         _(L"Time\tSum\tCommand\n"),
                         profile_items.size()) < 0)
            {
                wperror(L"fwprintf");
            }
            else
            {
                print_profile(profile_items, f);
            }

            if (fclose(f))
            {
                wperror(L"fclose");
            }
        }
    }

    lineinfo.clear();

    forbidden_function.clear();

}

/**
   Print error message to string if an error has occured while parsing

   \param target the buffer to write to
   \param prefix: The string token to prefix the each line with. Usually the name of the command trying to parse something.
*/
void parser_t::print_errors(wcstring &target, const wchar_t *prefix)
{
    CHECK(prefix,);

    if (error_code && ! err_buff.empty())
    {
        int tmp;

        append_format(target, L"%ls: %ls\n", prefix, err_buff.c_str());

        tmp = current_tokenizer_pos;
        current_tokenizer_pos = err_pos;

        append_format(target, L"%ls", this->current_line());

        current_tokenizer_pos=tmp;
    }
}

/**
   Print error message to stderr if an error has occured while parsing
*/
void parser_t::print_errors_stderr()
{
    if (error_code && ! err_buff.empty())
    {
        debug(0, L"%ls", err_buff.c_str());
        int tmp;

        tmp = current_tokenizer_pos;
        current_tokenizer_pos = err_pos;

        fwprintf(stderr, L"%ls", this->current_line());

        current_tokenizer_pos=tmp;
    }

}

void parser_t::eval_args(const wchar_t *line, std::vector<completion_t> &args)
{

    expand_flags_t eflags = 0;
    if (! show_errors)
        eflags |= EXPAND_NO_DESCRIPTIONS;
    if (this->parser_type != PARSER_TYPE_GENERAL)
        eflags |= EXPAND_SKIP_CMDSUBST;

    bool do_loop=1;

    if (! line) return;

    // PCA we need to suppress calling proc_push_interactive off of the main thread. I'm not sure exactly what it does.
    if (this->parser_type == PARSER_TYPE_GENERAL)
        proc_push_interactive(0);

    tokenizer_t tok(line, (show_errors ? 0 : TOK_SQUASH_ERRORS));

    /*
      eval_args may be called while evaulating another command, so we
      save the previous tokenizer and restore it on exit
    */
    scoped_push<tokenizer_t*> tokenizer_push(&current_tokenizer, &tok);
    scoped_push<int> tokenizer_pos_push(&current_tokenizer_pos, 0);

    error_code=0;

    for (; do_loop && tok_has_next(&tok) ; tok_next(&tok))
    {
        current_tokenizer_pos = tok_get_pos(&tok);
        switch (tok_last_type(&tok))
        {
            case TOK_STRING:
            {
                const wcstring tmp = tok_last(&tok);
                if (expand_string(tmp, args, eflags) == EXPAND_ERROR)
                {
                    err_pos=tok_get_pos(&tok);
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
                if (show_errors)
                    error(SYNTAX_ERROR,
                          tok_get_pos(&tok),
                          TOK_ERR_MSG,
                          tok_last(&tok));

                do_loop=0;
                break;
            }

            default:
            {
                if (show_errors)
                    error(SYNTAX_ERROR,
                          tok_get_pos(&tok),
                          UNEXPECTED_TOKEN_ERR_MSG,
                          tok_get_desc(tok_last_type(&tok)));

                do_loop=0;
                break;
            }
        }
    }

    if (show_errors)
        this->print_errors_stderr();

    if (this->parser_type == PARSER_TYPE_GENERAL)
        proc_pop_interactive();
}

void parser_t::stack_trace(size_t block_idx, wcstring &buff) const
{
    /*
      Check if we should end the recursion
    */
    if (block_idx >= this->block_count())
        return;
    
    const block_t *b = this->block_at_index(block_idx);

    if (b->type()==EVENT)
    {
        /*
          This is an event handler
        */
        const event_block_t *eb = static_cast<const event_block_t *>(b);
        wcstring description = event_get_desc(eb->event);
        append_format(buff, _(L"in event handler: %ls\n"), description.c_str());
        buff.append(L"\n");

        /*
          Stop recursing at event handler. No reason to believe that
          any other code is relevant.

          It might make sense in the future to continue printing the
          stack trace of the code that invoked the event, if this is a
          programmatic event, but we can't currently detect that.
        */
        return;
    }

    if (b->type() == FUNCTION_CALL || b->type()==SOURCE || b->type()==SUBST)
    {
        /*
          These types of blocks should be printed
        */

        int i;

        switch (b->type())
        {
            case SOURCE:
            {
                const source_block_t *sb = static_cast<const source_block_t*>(b);
                const wchar_t *source_dest = sb->source_file;
                append_format(buff, _(L"from sourcing file %ls\n"), user_presentable_path(source_dest).c_str());
                break;
            }
            case FUNCTION_CALL:
            {
                const function_block_t *fb = static_cast<const function_block_t*>(b);
                append_format(buff, _(L"in function '%ls'\n"), fb->name.c_str());
                break;
            }
            case SUBST:
            {
                append_format(buff, _(L"in command substitution\n"));
                break;
            }

            default: /* Can't get here */
                break;
        }

        const wchar_t *file = b->src_filename;

        if (file)
        {
            append_format(buff,
                          _(L"\tcalled on line %d of file %ls\n"),
                          b->src_lineno,
                          user_presentable_path(file).c_str());
        }
        else
        {
            append_format(buff,
                          _(L"\tcalled on standard input\n"));
        }

        if (b->type() == FUNCTION_CALL)
        {
            const function_block_t *fb = static_cast<const function_block_t *>(b);
            const process_t * const process = fb->process;
            if (process->argv(1))
            {
                wcstring tmp;

                for (i=1; process->argv(i); i++)
                {
                    if (i > 1)
                        tmp.push_back(L' ');
                    tmp.append(process->argv(i));
                }
                append_format(buff, _(L"\twith parameter list '%ls'\n"), tmp.c_str());
            }
        }

        append_format(buff, L"\n");
    }

    /*
      Recursively print the next block
    */
    parser_t::stack_trace(block_idx + 1, buff);
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

    const wchar_t *result = NULL;
    for (size_t block_idx = 0; block_idx < this->block_count(); block_idx++)
    {
        const block_t *b = this->block_at_index(block_idx);
        if (b->type() == FUNCTION_CALL)
        {
            const function_block_t *fb = static_cast<const function_block_t *>(b);
            result = fb->name.c_str();
            break;
        }
    }
    return result;
}


int parser_t::get_lineno() const
{
    int lineno;

    if (! current_tokenizer || ! tok_string(current_tokenizer))
        return -1;

    lineno = current_tokenizer->line_number_of_character_at_offset(current_tokenizer_pos);

    const wchar_t *function_name;
    if ((function_name = is_function()))
    {
        lineno += function_get_definition_offset(function_name);
    }

    return lineno;
}

int parser_t::line_number_of_character_at_offset(size_t idx) const
{
    if (! current_tokenizer)
        return -1;

    int result = current_tokenizer->line_number_of_character_at_offset(idx);
    //assert(result == parse_util_lineno(tok_string( current_tokenizer ), idx));
    return result;
}

const wchar_t *parser_t::current_filename() const
{
    /* We query a global array for the current file name, so it only makes sense to ask this on the principal parser. */
    ASSERT_IS_MAIN_THREAD();
    assert(this == &principal_parser());


    for (size_t i=0; i < this->block_count(); i++)
    {
        const block_t *b = this->block_at_index(i);
        if (b->type() == FUNCTION_CALL)
        {
            const function_block_t *fb = static_cast<const function_block_t *>(b);
            return function_get_definition_file(fb->name);
        }
    }
    return reader_current_filename();
}

/**
   Calculates the on-screen width of the specified substring of the
   specified string. This function takes into account the width and
   alignment of the tab character, but other wise behaves like
   repeatedly calling wcwidth.
*/
static int printed_width(const wchar_t *str, int len)
{
    int res=0;
    int i;

    CHECK(str, 0);

    for (i=0; str[i] && i<len; i++)
    {
        if (str[i] == L'\t')
        {
            res=(res+8)&~7;
        }
        else
        {
            res += fish_wcwidth(str[i]);
        }
    }
    return res;
}


const wchar_t *parser_t::current_line()
{
    int lineno=1;

    const wchar_t *file;
    const wchar_t *whole_str;
    const wchar_t *line;
    const wchar_t *line_end;
    int i;
    int offset;
    int current_line_width;
    const wchar_t *function_name=0;
    int current_line_start=0;

    if (!current_tokenizer)
    {
        return L"";
    }

    file = parser_t::current_filename();
    whole_str = tok_string(current_tokenizer);
    line = whole_str;

    if (!line)
        return L"";


    lineinfo.clear();

    /*
      Calculate line number, line offset, etc.
    */
    for (i=0; i<current_tokenizer_pos && whole_str[i]; i++)
    {
        if (whole_str[i] == L'\n')
        {
            lineno++;
            current_line_start=i+1;
            line = &whole_str[i+1];
        }
    }

//  lineno = current_tokenizer_pos;


    current_line_width=printed_width(whole_str+current_line_start,
                                     current_tokenizer_pos-current_line_start);

    if ((function_name = is_function()))
    {
        lineno += function_get_definition_offset(function_name);
    }

    /*
      Copy current line from whole string
    */
    line_end = wcschr(line, L'\n');
    if (!line_end)
        line_end = line+wcslen(line);

    line = wcsndup(line, line_end-line);

    /**
       If we are not going to print a stack trace, at least print the line number and filename
    */
    if (!get_is_interactive() || is_function())
    {
        int prev_width = my_wcswidth(lineinfo.c_str());
        if (file)
            append_format(lineinfo,
                          _(L"%ls (line %d): "),
                          file,
                          lineno);
        else
            append_format(lineinfo,
                          L"%ls: ",
                          _(L"Standard input"),
                          lineno);
        offset = my_wcswidth(lineinfo.c_str()) - prev_width;
    }
    else
    {
        offset=0;
    }

//  debug( 1, L"Current pos %d, line pos %d, file_length %d, is_interactive %d, offset %d\n", current_tokenizer_pos,  current_line_pos, wcslen(whole_str), is_interactive, offset);
    /*
      Skip printing character position if we are in interactive mode
      and the error was on the first character of the line.
    */
    if (!get_is_interactive() || is_function() || (current_line_width!=0))
    {
        // Workaround since it seems impossible to print 0 copies of a character using %*lc
        if (offset+current_line_width)
        {
            append_format(lineinfo,
                          L"%ls\n%*lc^\n",
                          line,
                          offset+current_line_width,
                          L' ');
        }
        else
        {
            append_format(lineinfo,
                          L"%ls\n^\n",
                          line);
        }
    }

    free((void *)line);
    parser_t::stack_trace(0, lineinfo);

    return lineinfo.c_str();
}

int parser_t::get_pos() const
{
    return tok_get_pos(current_tokenizer);
}

int parser_t::get_job_pos() const
{
    return job_start_pos;
}


void parser_t::set_pos(int p)
{
    tok_set_pos(current_tokenizer, p);
}

const wchar_t *parser_t::get_buffer() const
{
    return tok_string(current_tokenizer);
}


int parser_t::is_help(const wchar_t *s, int min_match)
{
    CHECK(s, 0);

    size_t len = wcslen(s);

    min_match = maxi(min_match, 3);

    return (wcscmp(L"-h", s) == 0) ||
           (len >= (size_t)min_match && (wcsncmp(L"--help", s, len) == 0));
}

job_t *parser_t::job_create()
{
    job_t *res = new job_t(acquire_job_id(), this->block_io);
    this->my_job_list.push_front(res);

    job_set_flag(res,
                 JOB_CONTROL,
                 (job_control_mode==JOB_CONTROL_ALL) ||
                 ((job_control_mode == JOB_CONTROL_INTERACTIVE) && (get_is_interactive())));
    return res;
}

bool parser_t::job_remove(job_t *j)
{
    job_list_t::iterator iter = std::find(my_job_list.begin(), my_job_list.end(), j);
    if (iter != my_job_list.end())
    {
        my_job_list.erase(iter);
        return true;
    }
    else
    {
        debug(1, _(L"Job inconsistency"));
        sanity_lose();
        return false;
    }
}

void parser_t::job_promote(job_t *job)
{
    signal_block();

    job_list_t::iterator loc = std::find(my_job_list.begin(), my_job_list.end(), job);
    assert(loc != my_job_list.end());

    /* Move the job to the beginning */
    my_job_list.splice(my_job_list.begin(), my_job_list, loc);
    signal_unblock();
}

job_t *parser_t::job_get(job_id_t id)
{
    job_iterator_t jobs(my_job_list);
    job_t *job;
    while ((job = jobs.next()))
    {
        if (id <= 0 || job->job_id == id)
            return job;
    }
    return NULL;
}

job_t *parser_t::job_get_from_pid(int pid)
{
    job_iterator_t jobs;
    job_t *job;
    while ((job = jobs.next()))
    {
        if (job->pgid == pid)
            return job;
    }
    return 0;
}

/**
   Parse options for the specified job

   \param p the process to parse options for
   \param j the job to which the process belongs to
   \param tok the tokenizer to read options from
   \param args the argument list to insert options into
   \param args unskip whether we should ignore current_block()->skip. Big hack because of our dumb handling of if statements.
*/
void parser_t::parse_job_argument_list(process_t *p,
                                       job_t *j,
                                       tokenizer_t *tok,
                                       std::vector<completion_t> &args,
                                       bool unskip)
{
    int is_finished=0;

    int proc_is_count=0;

    int matched_wildcard = 0, unmatched_wildcard = 0;

    wcstring unmatched;
    int unmatched_pos=0;

    /* The set of IO redirections that we construct for the process */
    io_chain_t process_io_chain;

    /*
      Test if this is the 'count' command. We need to special case
      count in the shell, since it should display a help message on
      'count -h', but not on 'set foo -h; count $foo'. This is an ugly
      workaround and a huge hack, but as near as I can tell, the
      alternatives are worse.
    */
    proc_is_count = (args.at(0).completion == L"count");

    while (1)
    {

        switch (tok_last_type(tok))
        {
            case TOK_PIPE:
            {
                wchar_t *end;

                if (p->type == INTERNAL_EXEC)
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(tok),
                          EXEC_ERR_MSG);
                    return;
                }

                errno = 0;
                p->pipe_write_fd = fish_wcstoi(tok_last(tok), &end, 10);
                if (p->pipe_write_fd < 0 || errno || *end)
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(tok),
                          ILLEGAL_FD_ERR_MSG,
                          tok_last(tok));
                    return;
                }

                p->set_argv(completions_to_wcstring_list(args));
                p->next = new process_t();

                tok_next(tok);

                /*
                  Don't do anything on failure. parse_job will notice
                  the error flag and report any errors for us
                */
                parse_job(p->next, j, tok);

                is_finished = 1;
                break;
            }

            case TOK_BACKGROUND:
            {
                job_set_flag(j, JOB_FOREGROUND, 0);
                // PCA note fall through, this is deliberate. The background modifier & terminates a command
            }

            case TOK_END:
            {
                if (!p->get_argv())
                    p->set_argv(completions_to_wcstring_list(args));
                if (tok_has_next(tok))
                    tok_next(tok);

                is_finished = 1;

                break;
            }

            case TOK_STRING:
            {
                int skip=0;

                if (job_get_flag(j, JOB_SKIP))
                {
                    skip = 1;
                }
                else if (current_block()->skip && ! unskip)
                {
                    /*
                      If this command should be skipped, we do not expand the arguments
                    */
                    skip=1;

                    /* But if this is in fact a case statement or an elseif statement, then it should be evaluated */
                    block_type_t type = current_block()->type();
                    if (type == SWITCH && args.at(0).completion == L"case" && p->type == INTERNAL_BUILTIN)
                    {
                        skip=0;
                    }
                    else if (job_get_flag(j, JOB_ELSEIF) && ! job_should_skip_elseif(j, current_block()))
                    {
                        skip=0;
                    }
                }
                else
                {
                    /* If this is an else if, and we should skip it, then don't expand any arguments */
                    if (job_get_flag(j, JOB_ELSEIF) && job_should_skip_elseif(j, current_block()))
                    {
                        skip = 1;
                    }
                }

                if (!skip)
                {
                    if ((proc_is_count) &&
                            (args.size() == 1) &&
                            (parser_t::is_help(tok_last(tok), 0)) &&
                            (p->type == INTERNAL_BUILTIN))
                    {
                        /*
                          Display help for count
                        */
                        p->count_help_magic = 1;
                    }

                    switch (expand_string(tok_last(tok), args, 0))
                    {
                        case EXPAND_ERROR:
                        {
                            err_pos=tok_get_pos(tok);
                            if (error_code == 0)
                            {
                                error(SYNTAX_ERROR,
                                      tok_get_pos(tok),
                                      _(L"Could not expand string '%ls'"),
                                      tok_last(tok));

                            }
                            break;
                        }

                        case EXPAND_WILDCARD_NO_MATCH:
                        {
                            unmatched_wildcard = 1;
                            if (unmatched.empty())
                            {
                                unmatched = tok_last(tok);
                                unmatched_pos = tok_get_pos(tok);
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
                int type = tok_last_type(tok);
                shared_ptr<io_data_t> new_io;
                wcstring target;
                bool has_target = false;
                wchar_t *end;

                /*
                  Don't check redirections in skipped part

                  Otherwise, bogus errors may be the result. (Do check
                  that token is string, though)
                */
                if (current_block()->skip && ! unskip)
                {
                    tok_next(tok);
                    if (tok_last_type(tok) != TOK_STRING)
                    {
                        error(SYNTAX_ERROR,
                              tok_get_pos(tok),
                              REDIRECT_TOKEN_ERR_MSG,
                              tok_get_desc(tok_last_type(tok)));
                    }

                    break;
                }


                errno = 0;
                int fd = fish_wcstoi(tok_last(tok),
                                     &end,
                                     10);
                if (fd < 0 || errno || *end)
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(tok),
                          ILLEGAL_FD_ERR_MSG,
                          tok_last(tok));
                }
                else
                {

                    tok_next(tok);

                    switch (tok_last_type(tok))
                    {
                        case TOK_STRING:
                        {
                            target = tok_last(tok);
                            has_target = expand_one(target, no_exec ? EXPAND_SKIP_VARIABLES : 0);

                            if (! has_target && error_code == 0)
                            {
                                error(SYNTAX_ERROR,
                                      tok_get_pos(tok),
                                      REDIRECT_TOKEN_ERR_MSG,
                                      tok_last(tok));

                            }
                            break;
                        }

                        default:
                            error(SYNTAX_ERROR,
                                  tok_get_pos(tok),
                                  REDIRECT_TOKEN_ERR_MSG,
                                  tok_get_desc(tok_last_type(tok)));
                    }

                    if (! has_target || target.empty())
                    {
                        if (error_code == 0)
                            error(SYNTAX_ERROR,
                                  tok_get_pos(tok),
                                  _(L"Invalid IO redirection"));
                        tok_next(tok);
                    }
                    else if (type == TOK_REDIRECT_FD)
                    {
                        if (target == L"-")
                        {
                            new_io.reset(new io_close_t(fd));
                        }
                        else
                        {
                            wchar_t *end;

                            errno = 0;

                            int old_fd = fish_wcstoi(target.c_str(), &end, 10);

                            if (old_fd < 0 || errno || *end)
                            {
                                error(SYNTAX_ERROR,
                                      tok_get_pos(tok),
                                      _(L"Requested redirection to something that is not a file descriptor %ls"),
                                      target.c_str());

                                tok_next(tok);
                            }
                            else
                            {
                                new_io.reset(new io_fd_t(fd, old_fd));
                            }
                        }
                    }
                    else
                    {
                        int flags = 0;
                        switch (type)
                        {
                            case TOK_REDIRECT_APPEND:
                                flags = O_CREAT | O_APPEND | O_WRONLY;
                                break;

                            case TOK_REDIRECT_OUT:
                                flags = O_CREAT | O_WRONLY | O_TRUNC;
                                break;

                            case TOK_REDIRECT_NOCLOB:
                                flags = O_CREAT | O_EXCL | O_WRONLY;
                                break;

                            case TOK_REDIRECT_IN:
                                flags = O_RDONLY;
                                break;

                        }
                        io_file_t *new_io_file = new io_file_t(fd, target, flags);
                        new_io.reset(new_io_file);
                    }
                }

                if (new_io.get() != NULL)
                {
                    process_io_chain.push_back(new_io);
                }

            }
            break;

            case TOK_ERROR:
            {
                error(SYNTAX_ERROR,
                      tok_get_pos(tok),
                      TOK_ERR_MSG,
                      tok_last(tok));

                return;
            }

            default:
                error(SYNTAX_ERROR,
                      tok_get_pos(tok),
                      UNEXPECTED_TOKEN_ERR_MSG,
                      tok_get_desc(tok_last_type(tok)));

                tok_next(tok);
                break;
        }

        if ((is_finished) || (error_code != 0))
            break;

        tok_next(tok);
    }

    if (!error_code)
    {
        if (unmatched_wildcard && !matched_wildcard)
        {
            job_set_flag(j, JOB_WILDCARD_ERROR, 1);
            proc_set_last_status(STATUS_UNMATCHED_WILDCARD);
            if (get_is_interactive() && !is_block)
            {
                int tmp;

                debug(1, WILDCARD_ERR_MSG, unmatched.c_str());
                tmp = current_tokenizer_pos;
                current_tokenizer_pos = unmatched_pos;

                fwprintf(stderr, L"%ls", parser_t::current_line());

                current_tokenizer_pos=tmp;
            }

        }
    }

    /* Store our IO chain. The existing chain should be empty. */
    assert(p->io_chain().empty());
    p->set_io_chain(process_io_chain);
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

process_t *parser_t::create_boolean_process(job_t *job, const parse_node_t &bool_statement, const parser_context_t &ctx)
{
    // Handle a boolean statement
    bool skip_job = false;
    assert(bool_statement.type == symbol_boolean_statement);
    switch (bool_statement.production_idx)
    {
        // These magic numbers correspond to productions for boolean_statement
        case 0:
            // AND. Skip if the last job failed.
            skip_job = (proc_get_last_status() != 0);
            break;
            
        case 1:
            // OR. Skip if the last job succeeded.
            skip_job = (proc_get_last_status() == 0);
            break;

        case 2:
            // NOT. Negate it.
            job_set_flag(job, JOB_NEGATE, !job_get_flag(job, JOB_NEGATE));
            break;
            
        default:
        {
            fprintf(stderr, "Unexpected production in boolean statement\n");
            PARSER_DIE();
            break;
        }
    }
    
    process_t *result = NULL;
    if (! skip_job)
    {
        const parse_node_t &subject = *ctx.tree.get_child(bool_statement, 1, symbol_statement);
        result = this->create_job_process(job, subject, ctx);
    }
    return result;
}

process_t *parser_t::create_for_process(job_t *job, const parse_node_t &header, const parse_node_t &statement, const parser_context_t &ctx)
{
    return NULL;
}

process_t *parser_t::create_while_process(job_t *job, const parse_node_t &header, const parse_node_t &statement, const parser_context_t &ctx)
{
    return NULL;
}

process_t *parser_t::create_begin_process(job_t *job, const parse_node_t &header, const parse_node_t &statement, const parser_context_t &ctx)
{
    return NULL;
}

process_t *parser_t::create_plain_process(job_t *job, const parse_node_t &statement, const parser_context_t &ctx)
{
    /* Get the decoration */
    assert(statement.type == symbol_plain_statement);
    
    /* Get the command. We expect to always get it here. */
    wcstring cmd;
    bool got_cmd = ctx.tree.command_for_plain_statement(statement, ctx.src, &cmd);
    assert(got_cmd);
    
    /* Expand it as a command */
    bool expanded = expand_one(cmd, EXPAND_SKIP_CMDSUBST | EXPAND_SKIP_VARIABLES);
    if (! expanded)
    {
        error(SYNTAX_ERROR,
              statement.source_start,
              ILLEGAL_CMD_ERR_MSG,
              cmd.c_str());
        return 0;
    }
    
    /* The list of arguments. The command is the first argument. TODO: count hack */
    const parse_node_t *unmatched_wildcard = NULL;
    wcstring_list_t argument_list = this->determine_arguments(statement, &unmatched_wildcard, ctx);
    argument_list.insert(argument_list.begin(), cmd);
    
    /* We were not able to expand any wildcards. Here is the first one that failed */
    if (unmatched_wildcard != NULL)
    {
        job_set_flag(job, JOB_WILDCARD_ERROR, 1);
        proc_set_last_status(STATUS_UNMATCHED_WILDCARD);
        error(EVAL_ERROR, unmatched_wildcard->source_start, WILDCARD_ERR_MSG, unmatched_wildcard->get_source(ctx.src).c_str());
    }
    
    /* The set of IO redirections that we construct for the process */
    const io_chain_t process_io_chain = this->determine_io_chain(statement, ctx);
    
    /* Determine the process type, which depends on the statement decoration (command, builtin, etc) */
    enum parse_statement_decoration_t decoration = ctx.tree.decoration_for_plain_statement(statement);
    enum process_type_t process_type = EXTERNAL;
    
    /* exec hack */
    if (decoration != parse_statement_decoration_command && cmd == L"exec")
    {
        /* Either 'builtin exec' or just plain 'exec', and definitely not 'command exec'. Note we don't allow overriding exec with a function. */
        process_type = INTERNAL_EXEC;
    }
    else if (decoration == parse_statement_decoration_command)
    {
        /* Always a command */
        process_type = EXTERNAL;
    }
    else if (decoration == parse_statement_decoration_builtin)
    {
        /* What happens if this builtin is not valid? */
        process_type = INTERNAL_BUILTIN;
    }
    else if (function_exists(cmd))
    {
        process_type = INTERNAL_FUNCTION;
    }
    else if (builtin_exists(cmd))
    {
        process_type = INTERNAL_BUILTIN;
    }
    else
    {
        process_type = EXTERNAL;
    }
    
    wcstring actual_cmd;
    if (process_type == EXTERNAL)
    {
        /* Determine the actual command. Need to support implicit cd here */
        bool has_command = path_get_path(cmd, &actual_cmd);
        
        if (! has_command)
        {
            /* TODO: support fish_command_not_found, implicit cd, etc. here */
        }
        
    }
    
    /* Return the process */
    process_t *result = new process_t();
    result->type = process_type;
    result->set_argv(argument_list);
    result->set_io_chain(process_io_chain);
    result->actual_cmd = actual_cmd;
    return result;
}

/* Determine the list of arguments, expanding stuff. If we have a wildcard and none could be expanded, return the unexpandable wildcard node by reference. */
wcstring_list_t parser_t::determine_arguments(const parse_node_t &statement, const parse_node_t **out_unmatched_wildcard_node, const parser_context_t &ctx)
{
    wcstring_list_t argument_list;
    
    /* Whether we failed to match any wildcards, and succeeded in matching any wildcards */
    bool unmatched_wildcard = false, matched_wildcard = false;
    
    /* First node that failed to expand as a wildcard (if any) */
    const parse_node_t *unmatched_wildcard_node = NULL;
    
    /* Get all argument nodes underneath the statement */
    const parse_node_tree_t::parse_node_list_t argument_nodes = ctx.tree.find_nodes(statement, symbol_argument);
    argument_list.reserve(argument_nodes.size());
    for (size_t i=0; i < argument_nodes.size(); i++)
    {
        const parse_node_t &arg_node = *argument_nodes.at(i);
        
        /* Expect all arguments to have source */
        assert(arg_node.has_source());
        const wcstring arg_str = arg_node.get_source(ctx.src);
        
        /* Expand this string */
        std::vector<completion_t> arg_expanded;
        int expand_ret = expand_string(arg_str, arg_expanded, 0);
        switch (expand_ret)
        {
            case EXPAND_ERROR:
            {
                error(SYNTAX_ERROR,
                      arg_node.source_start,
                      _(L"Could not expand string '%ls'"),
                      arg_str.c_str());
                break;
            }
                
            case EXPAND_WILDCARD_NO_MATCH:
            {
                /* Store the node that failed to expand */
                unmatched_wildcard = true;
                if (! unmatched_wildcard_node)
                {
                    unmatched_wildcard_node = &arg_node;
                }
                break;
            }
            
            case EXPAND_WILDCARD_MATCH:
            {
                matched_wildcard = true;
                break;
            }
            
            case EXPAND_OK:
            {
                break;
            }
        }
        
        /* Now copy over any expanded arguments */
        for (size_t i=0; i < arg_expanded.size(); i++)
        {
            argument_list.push_back(arg_expanded.at(i).completion);
        }
    }
    
    /* Return if we had a wildcard problem */
    if (unmatched_wildcard && ! matched_wildcard)
    {
        *out_unmatched_wildcard_node = unmatched_wildcard_node;
    }
    
    return argument_list;
}

io_chain_t parser_t::determine_io_chain(const parse_node_t &statement,const parser_context_t &ctx)
{
    io_chain_t result;
    
    /* Get all redirection nodes underneath the statement */
    const parse_node_tree_t::parse_node_list_t redirect_nodes = ctx.tree.find_nodes(statement, symbol_redirection);
    for (size_t i=0; i < redirect_nodes.size(); i++)
    {
        const parse_node_t &redirect_node = *redirect_nodes.at(i);
        
        int source_fd = -1; /* source fd */
        wcstring target; /* file path or target fd */
        enum token_type redirect_type = ctx.tree.type_for_redirection(redirect_node, ctx.src, &source_fd, &target);
        
        /* PCA: I can't justify this EXPAND_SKIP_VARIABLES flag. It was like this when I got here. */
        bool target_expanded = expand_one(target, no_exec ? EXPAND_SKIP_VARIABLES : 0);
        if (! target_expanded || target.empty())
        {
            /* Should improve this error message */
            error(SYNTAX_ERROR,
                  redirect_node.source_start,
                  _(L"Invalid redirection target: %ls"),
                  target.c_str());
        }
        
        
        /* Generate the actual IO redirection */
        shared_ptr<io_data_t> new_io;
        assert(redirect_type != TOK_NONE);
        switch (redirect_type)
        {
            case TOK_REDIRECT_FD:
            {
                if (target == L"-")
                {
                    new_io.reset(new io_close_t(source_fd));
                }
                else
                {
                    wchar_t *end = NULL;
                    errno = 0;
                    int old_fd = fish_wcstoi(target.c_str(), &end, 10);
                    if (old_fd < 0 || errno || *end)
                    {
                        error(SYNTAX_ERROR,
                              redirect_node.source_start,
                              _(L"Requested redirection to something that is not a file descriptor %ls"),
                              target.c_str());
                    }
                    else
                    {
                        new_io.reset(new io_fd_t(source_fd, old_fd));
                    }
                }
                break;
            }
            
            case TOK_REDIRECT_OUT:
            case TOK_REDIRECT_APPEND:
            case TOK_REDIRECT_IN:
            case TOK_REDIRECT_NOCLOB:
            {
                int oflags = oflags_for_redirection_type(redirect_type);
                io_file_t *new_io_file = new io_file_t(source_fd, target, oflags);
                new_io.reset(new_io_file);
                break;
            }
            
            default:
            {
                // Should be unreachable
                fprintf(stderr, "Unexpected redirection type %ld. aborting.\n", (long)redirect_type);
                PARSER_DIE();
                break;
            }
        }
        
        /* Append the new_io if we got one */
        if (new_io.get() != NULL)
        {
            result.push_back(new_io);
        }
    }
    return result;
}

/* Returns a process_t allocated with new. It's the caller's responsibility to delete it (!) */
process_t *parser_t::create_job_process(job_t *job, const parse_node_t &statement_node, const parser_context_t &ctx)
{
    assert(statement_node.type == symbol_statement);
    assert(statement_node.child_count == 1);
    
    // Get the "specific statement" which is boolean / block / if / switch / decorated
    const parse_node_t &specific_statement = *ctx.tree.get_child(statement_node, 0);
    
    process_t *result = NULL;
    
    switch (specific_statement.type)
    {
        case symbol_boolean_statement:
        {
            result = this->create_boolean_process(job, specific_statement, ctx);
            break;
        }
        
        case symbol_block_statement:
        {
            const parse_node_t &header = *ctx.tree.get_child(specific_statement, 0, symbol_block_header);
            const parse_node_t &specific_header = *ctx.tree.get_child(header, 0);
            switch (specific_header.type)
            {
                case symbol_for_header:
                    result = this->create_for_process(job, specific_header, specific_statement, ctx);
                    break;
                    
                case symbol_while_header:
                    result = this->create_while_process(job, specific_header, specific_statement, ctx);
                    break;
                
                case symbol_function_header:
                    // No process is associated with creating a function
                    // TODO: create the darn function!
                    result = NULL;
                    break;
                    
                case symbol_begin_header:
                    result = this->create_begin_process(job, specific_header, specific_statement, ctx);
                    break;
                
                default:
                    fprintf(stderr, "Unexpected header type\n");
                    PARSER_DIE();
                    break;
            }
            break;
        }
        
        case symbol_decorated_statement:
        {
            const parse_node_t &plain_statement = ctx.tree.find_child(specific_statement, symbol_plain_statement);
            result = this->create_plain_process(job, plain_statement, ctx);
            break;
        }
        
        default:
            fprintf(stderr, "'%ls' not handled by new parser yet\n", specific_statement.describe().c_str());
    }
    
    return result;
}

/**
   Fully parse a single job. Does not call exec on it, but any command substitutions in the job will be executed.

   \param p The process structure that should be used to represent the first process in the job.
   \param j The job structure to contain the parsed job
   \param tok tokenizer to read from
f
   \return 1 on success, 0 on error
*/
int parser_t::parse_job(process_t *p, job_t *j, tokenizer_t *tok)
{
    std::vector<completion_t> args; // The list that will become the argv array for the program
    int use_function = 1;   // May functions be considered when checking what action this command represents
    int use_builtin = 1;    // May builtins be considered when checking what action this command represents
    int use_command = 1;    // May commands be considered when checking what action this command represents
    int is_new_block=0;     // Does this command create a new block?
    bool unskip = false;    // Maybe we are an elseif inside an if block; if so we may want to evaluate this even if the if block is currently set to skip
    bool allow_bogus_command = false; // If we are an elseif that will not be executed, or an AND or OR that will have been short circuited, don't complain about non-existent commands

    const block_t *prev_block = current_block();
    scoped_push<int> tokenizer_pos_push(&current_tokenizer_pos, tok_get_pos(tok));

    while (args.empty())
    {
        wcstring nxt;
        bool has_nxt = false;
        bool consumed = false; // Set to one if the command requires a second command, like e.g. while does
        int mark;         // Use to save the position of the beginning of the token

        switch (tok_last_type(tok))
        {
            case TOK_STRING:
            {
                nxt = tok_last(tok);
                has_nxt = expand_one(nxt, EXPAND_SKIP_CMDSUBST | EXPAND_SKIP_VARIABLES);

                if (! has_nxt)
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(tok),
                          ILLEGAL_CMD_ERR_MSG,
                          tok_last(tok));

                    return 0;
                }
                break;
            }

            case TOK_ERROR:
            {
                error(SYNTAX_ERROR,
                      tok_get_pos(tok),
                      TOK_ERR_MSG,
                      tok_last(tok));

                return 0;
            }

            case TOK_PIPE:
            {
                const wchar_t *str = tok_string(tok);
                if (tok_get_pos(tok)>0 && str[tok_get_pos(tok)-1] == L'|')
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(tok),
                          CMD_OR_ERR_MSG,
                          tok_get_desc(tok_last_type(tok)));
                }
                else
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(tok),
                          CMD_ERR_MSG,
                          tok_get_desc(tok_last_type(tok)));
                }

                return 0;
            }

            default:
            {
                error(SYNTAX_ERROR,
                      tok_get_pos(tok),
                      CMD_ERR_MSG,
                      tok_get_desc(tok_last_type(tok)));

                return 0;
            }
        }

        mark = tok_get_pos(tok);

        if (contains(nxt,
                     L"command",
                     L"builtin",
                     L"not",
                     L"and",
                     L"or",
                     L"exec"))
        {
            int sw;
            int is_exec = nxt == L"exec";

            if (is_exec && (p != j->first_process))
            {
                error(SYNTAX_ERROR,
                      tok_get_pos(tok),
                      EXEC_ERR_MSG);
                return 0;
            }

            tok_next(tok);
            sw = parser_keywords_is_switch(tok_last(tok));

            if (sw == ARG_SWITCH)
            {
                tok_set_pos(tok, mark);
            }
            else
            {
                if (sw == ARG_SKIP)
                {
                    tok_next(tok);
                }

                consumed = true;

                if (nxt == L"command" || nxt == L"builtin")
                {
                    use_function = 0;
                    if (nxt == L"command")
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
                else if (nxt == L"not")
                {
                    job_set_flag(j, JOB_NEGATE, !job_get_flag(j, JOB_NEGATE));
                }
                else if (nxt == L"and")
                {
                    bool skip = (proc_get_last_status() != 0);
                    job_set_flag(j, JOB_SKIP, skip);
                    allow_bogus_command = skip;
                }
                else if (nxt == L"or")
                {
                    bool skip = (proc_get_last_status() == 0);
                    job_set_flag(j, JOB_SKIP, skip);
                    allow_bogus_command = skip;
                }
                else if (is_exec)
                {
                    use_function = 0;
                    use_builtin=0;
                    p->type=INTERNAL_EXEC;
                    tokenizer_pos_push.restore();
                }
            }
        }
        else if (nxt == L"while")
        {
            bool new_block = false;
            tok_next(tok);
            while_block_t *wb = NULL;

            if ((current_block()->type() != WHILE))
            {
                new_block = true;
            }
            else if ((wb = static_cast<while_block_t*>(current_block()))->status == WHILE_TEST_AGAIN)
            {
                wb->status = WHILE_TEST_FIRST;
            }
            else
            {
                new_block = true;
            }

            if (new_block)
            {
                while_block_t *wb = new while_block_t();
                wb->status = WHILE_TEST_FIRST;
                wb->tok_pos = mark;
                this->push_block(wb);
            }

            consumed = true;
            is_new_block=1;

        }
        else if (nxt == L"if")
        {
            tok_next(tok);

            if_block_t *ib = new if_block_t();
            this->push_block(ib);
            ib->tok_pos = mark;

            is_new_block=1;
            consumed = true;
        }
        else if (nxt == L"else")
        {
            /* Record where the else is for error reporting */
            const int else_pos = tok_get_pos(tok);
            /* See if we have any more arguments, that is, whether we're ELSE IF ... or just ELSE. */
            tok_next(tok);
            if (tok_last_type(tok) == TOK_STRING && current_block()->type() == IF)
            {
                const if_block_t *ib = static_cast<const if_block_t *>(current_block());

                /* If we've already encountered an else, complain */
                if (ib->else_evaluated)
                {
                    error(SYNTAX_ERROR,
                          else_pos,
                          INVALID_ELSEIF_PAST_ELSE_ERR_MSG,
                          L"else if");

                }
                else
                {

                    job_set_flag(j, JOB_ELSEIF, 1);
                    consumed = true;

                    /* We're at the IF. Go past it. */
                    tok_next(tok);

                    /* We want to execute this ELSEIF if the IF expression was evaluated, it failed, and so has every other ELSEIF (if any) */
                    unskip = (ib->if_expr_evaluated && ! ib->any_branch_taken);

                    /* But if we're not executing it, don't complain about its command if it doesn't exist */
                    if (! unskip)
                        allow_bogus_command = true;
                }
            }
        }

        /*
          Test if we need another command
        */
        if (consumed)
        {
            /*
              Yes we do, around in the loop for another lap, then!
            */
            continue;
        }

        if (use_function && (unskip || ! current_block()->skip))
        {
            bool nxt_forbidden=false;
            wcstring forbid;

            int is_function_call=0;

            /*
              This is a bit fragile. It is a test to see if we are
              inside of function call, but not inside a block in that
              function call. If, in the future, the rules for what
              block scopes are pushed on function invocation changes,
              then this check will break.
            */
            const block_t *current = this->block_at_index(0), *parent = this->block_at_index(1);
            if (current && parent && current->type() == TOP && parent->type() == FUNCTION_CALL)
                is_function_call = 1;

            /*
              If we are directly in a function, and this is the first
              command of the block, then the function we are executing
              may not be called, since that would mean an infinite
              recursion.
            */
            if (is_function_call && !current->had_command)
            {
                forbid = forbidden_function.empty() ? wcstring(L"") : forbidden_function.back();
                if (forbid == nxt)
                {
                    /* Infinite recursive loop */
                    nxt_forbidden = true;
                    error(SYNTAX_ERROR, tok_get_pos(tok), INFINITE_RECURSION_ERR_MSG);
                }
            }

            if (!nxt_forbidden && has_nxt && function_exists(nxt))
            {
                /*
                  Check if we have reached the maximum recursion depth
                */
                if (forbidden_function.size() > MAX_RECURSION_DEPTH)
                {
                    error(SYNTAX_ERROR, tok_get_pos(tok), OVERFLOW_RECURSION_ERR_MSG);
                }
                else
                {
                    p->type = INTERNAL_FUNCTION;
                }
            }
        }
        args.push_back(completion_t(nxt));
    }

    if (error_code == 0)
    {
        if (!p->type)
        {
            if (use_builtin &&
                    builtin_exists(args.at(0).completion))
            {
                p->type = INTERNAL_BUILTIN;
                is_new_block |= parser_keywords_is_block(args.at(0).completion);
            }
        }

        if ((!p->type || (p->type == INTERNAL_EXEC)))
        {
            /*
              If we are not executing the current block, allow
              non-existent commands.
            */
            if (current_block()->skip && ! unskip)
                allow_bogus_command = true; //note this may already be true for other reasons

            if (allow_bogus_command)
            {
                p->actual_cmd.clear();
            }
            else
            {
                int err;
                bool has_command = path_get_path(args.at(0).completion, &p->actual_cmd);
                err = errno;

                bool use_implicit_cd = false;
                if (! has_command)
                {
                    /* If the specified command does not exist, try using an implicit cd. */
                    wcstring implicit_cd_path;
                    use_implicit_cd = path_can_be_implicit_cd(args.at(0).completion, &implicit_cd_path);
                    if (use_implicit_cd)
                    {
                        args.clear();
                        args.push_back(completion_t(L"cd"));
                        args.push_back(completion_t(implicit_cd_path));

                        /* If we have defined a wrapper around cd, use it, otherwise use the cd builtin */
                        if (use_function && function_exists(L"cd"))
                            p->type = INTERNAL_FUNCTION;
                        else
                            p->type = INTERNAL_BUILTIN;
                    }
                }

                // Disabled pending discussion in https://github.com/fish-shell/fish-shell/issues/367
#if 0
                if (! has_command && ! use_implicit_cd)
                {
                    if (fish_openSUSE_dbus_hack_hack_hack_hack(&args))
                    {
                        has_command = true;
                        p->type = INTERNAL_BUILTIN;
                    }
                }
#endif

                /* Check if the specified command exists */
                if (! has_command && ! use_implicit_cd)
                {

                    const wchar_t *cmd = args.at(0).completion.c_str();

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

                    const wchar_t * const equals_ptr = wcschr(cmd, L'=');
                    if (equals_ptr != NULL)
                    {
                        /* Try to figure out if this is a pure variable assignment (foo=bar), or if this appears to be running a command (foo=bar ruby...) */

                        const wcstring name_str = wcstring(cmd, equals_ptr - cmd); //variable name, up to the =
                        const wcstring val_str = wcstring(equals_ptr + 1); //variable value, past the =

                        wcstring next_str;
                        if (tok_peek_next(tok, &next_str) == TOK_STRING && ! next_str.empty())
                        {
                            wcstring ellipsis_str = wcstring(1, ellipsis_char);
                            if (ellipsis_str == L"$")
                                ellipsis_str = L"...";

                            /* Looks like a command */
                            debug(0,
                                  _(L"Unknown command '%ls'. Did you mean to run %ls with a modified environment? Try 'env %ls=%ls %ls%ls'. See the help section on the set command by typing 'help set'."),
                                  cmd,
                                  next_str.c_str(),
                                  name_str.c_str(),
                                  val_str.c_str(),
                                  next_str.c_str(),
                                  ellipsis_str.c_str());
                        }
                        else
                        {
                            debug(0,
                                  COMMAND_ASSIGN_ERR_MSG,
                                  cmd,
                                  name_str.c_str(),
                                  val_str.c_str());
                        }
                    }
                    else if (cmd[0]==L'$' || cmd[0] == VARIABLE_EXPAND || cmd[0] == VARIABLE_EXPAND_SINGLE)
                    {

                        const env_var_t val_wstr = env_get_string(cmd+1);
                        const wchar_t *val = val_wstr.missing() ? NULL : val_wstr.c_str();
                        if (val)
                        {
                            debug(0,
                                  _(L"Variables may not be used as commands. Instead, define a function like 'function %ls; %ls $argv; end' or use the eval builtin instead, like 'eval %ls'. See the help section for the function command by typing 'help function'."),
                                  cmd+1,
                                  val,
                                  cmd,
                                  cmd);
                        }
                        else
                        {
                            debug(0,
                                  _(L"Variables may not be used as commands. Instead, define a function or use the eval builtin instead, like 'eval %ls'. See the help section for the function command by typing 'help function'."),
                                  cmd,
                                  cmd);
                        }
                    }
                    else if (wcschr(cmd, L'$'))
                    {
                        debug(0,
                              _(L"Commands may not contain variables. Use the eval builtin instead, like 'eval %ls'. See the help section for the eval command by typing 'help eval'."),
                              cmd,
                              cmd);
                    }
                    else if (err!=ENOENT)
                    {
                        debug(0,
                              _(L"The file '%ls' is not executable by this user"),
                              cmd?cmd:L"UNKNOWN");
                    }
                    else
                    {
                        /*
                         Handle unrecognized commands with standard
                         command not found handler that can make better
                         error messages
                         */

                        wcstring_list_t event_args;
                        event_args.push_back(args.at(0).completion);
                        event_fire_generic(L"fish_command_not_found", &event_args);
                    }

                    int tmp = current_tokenizer_pos;
                    current_tokenizer_pos = tok_get_pos(tok);

                    fwprintf(stderr, L"%ls", parser_t::current_line());

                    current_tokenizer_pos=tmp;

                    job_set_flag(j, JOB_SKIP, 1);

                    proc_set_last_status(err==ENOENT?STATUS_UNKNOWN_COMMAND:STATUS_NOT_EXECUTABLE);
                }
            }
        }

        if ((p->type == EXTERNAL) && !use_command)
        {
            error(SYNTAX_ERROR,
                  tok_get_pos(tok),
                  UNKNOWN_BUILTIN_ERR_MSG,
                  args.back().completion.c_str());
        }
    }


    if (is_new_block)
    {

        const wchar_t *end=parser_find_end(tok_string(tok) +
                                           current_tokenizer_pos);
        int make_sub_block = j->first_process != p;

        if (!end)
        {
            error(SYNTAX_ERROR,
                  tok_get_pos(tok),
                  BLOCK_END_ERR_MSG);

        }
        else
        {

            if (!make_sub_block)
            {
                int done=0;

                tokenizer_t subtok(end, 0);
                for (; ! done && tok_has_next(&subtok); tok_next(&subtok))
                {

                    switch (tok_last_type(&subtok))
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
                            error(SYNTAX_ERROR,
                                  current_tokenizer_pos,
                                  BLOCK_END_ERR_MSG);
                        }
                    }
                }
            }

            if (make_sub_block)
            {

                long end_pos = end-tok_string(tok);
                const wcstring sub_block(tok_string(tok) + current_tokenizer_pos, end_pos - current_tokenizer_pos);

                p->type = INTERNAL_BLOCK;
                args.at(0) =  completion_t(sub_block);

                tok_set_pos(tok, (int)end_pos);

                while (prev_block != this->current_block())
                {
                    parser_t::pop_block();
                }

            }
            else tok_next(tok);
        }

    }
    else tok_next(tok);

    if (!error_code)
    {
        if (p->type == INTERNAL_BUILTIN && parser_keywords_skip_arguments(args.at(0).completion))
        {
            if (!p->get_argv())
                p->set_argv(completions_to_wcstring_list(args));
        }
        else
        {
            parse_job_argument_list(p, j, tok, args, unskip);
        }
    }

    if (!error_code)
    {
        if (!is_new_block)
        {
            current_block()->had_command = true;
        }
    }

    if (error_code)
    {
        /*
          Make sure the block stack is consistent
        */
        while (prev_block != current_block())
        {
            parser_t::pop_block();
        }
    }
    return !error_code;
}

/**
   Do skipped execution of command. This means that only limited
   execution of block level commands such as end and switch should be
   preformed.

   \param j the job to execute

*/
void parser_t::skipped_exec(job_t * j)
{
    process_t *p;

    /* Handle other skipped guys */
    for (p = j->first_process; p; p=p->next)
    {
        if (p->type == INTERNAL_BUILTIN)
        {
            if ((wcscmp(p->argv0(), L"for")==0) ||
                    (wcscmp(p->argv0(), L"switch")==0) ||
                    (wcscmp(p->argv0(), L"begin")==0) ||
                    (wcscmp(p->argv0(), L"function")==0))
            {
                this->push_block(new fake_block_t());
            }
            else if (wcscmp(p->argv0(), L"end")==0)
            {
                const block_t *parent = this->block_at_index(1);
                if (parent && ! parent->skip)
                {
                    exec_job(*this, j);
                    return;
                }
                parser_t::pop_block();
            }
            else if (wcscmp(p->argv0(), L"else")==0)
            {
                if (current_block()->type() == IF)
                {
                    /* Evaluate this ELSE if the IF expression failed, and so has every ELSEIF (if any) expression thus far */
                    const if_block_t *ib = static_cast<const if_block_t*>(current_block());
                    if (ib->if_expr_evaluated && ! ib->any_branch_taken)
                    {
                        exec_job(*this, j);
                        return;
                    }
                }
            }
            else if (wcscmp(p->argv0(), L"case")==0)
            {
                if (current_block()->type() == SWITCH)
                {
                    exec_job(*this, j);
                    return;
                }
            }
        }
    }
    job_free(j);
}

/* Return whether we should skip the current block, if it is an elseif. */
static bool job_should_skip_elseif(const job_t *job, const block_t *current_block)
{
    if (current_block->type() != IF)
    {
        /* Not an IF block, so just honor the skip property */
        return current_block->skip;
    }
    else
    {
        /* We are an IF block */
        const if_block_t *ib = static_cast<const if_block_t *>(current_block);

        /* Execute this ELSEIF if the IF expression has been evaluated, it evaluated to false, and all ELSEIFs so far have evaluated to false. */
        bool execute_elseif = (ib->if_expr_evaluated && ! ib->any_branch_taken);

        /* Invert the sense */
        return ! execute_elseif;
    }
}

/**
   Evaluates a job from a node tree.
*/

void parser_t::eval_job(const parse_node_t &job_node, const parser_context_t &ctx)
{
    assert(job_node.type == symbol_job);
    this->job_start_pos = (int)job_node.source_start;
    
    // Get terminal modes
    struct termios tmodes = {};
    if (get_is_interactive())
    {
        if (tcgetattr(STDIN_FILENO, &tmodes))
        {
            // need real error handling here
            wperror(L"tcgetattr");
            return;
        }
    }
    
    /* Track whether we had an error */
    bool process_errored = false;
    
    /* Profiling support */
    long long t1 = 0, t2 = 0, t3 = 0;
    const bool do_profile = profile;
    profile_item_t *profile_item = NULL;
    if (do_profile)
    {
        profile_item = new profile_item_t();
        profile_item->skipped = 1;
        profile_items.push_back(profile_item);
        t1 = get_time();
    }
    
    job_t *j = this->job_create();
    job_set_flag(j, JOB_FOREGROUND, 1);
    job_set_flag(j, JOB_TERMINAL, job_get_flag(j, JOB_CONTROL));
    job_set_flag(j, JOB_TERMINAL, job_get_flag(j, JOB_CONTROL) \
                 && (!is_subshell && !is_event));
    job_set_flag(j, JOB_SKIP_NOTIFICATION, is_subshell \
                 || is_block \
                 || is_event \
                 || (!get_is_interactive()));

    current_block()->job = j;
    
    /* Tell the job what its command is */
    j->set_command(job_node.get_source(ctx.src));
    
    /* Construct process_t structures for every statement in the job */
    const parse_node_t *statement_node = ctx.tree.get_child(job_node, 0, symbol_statement);
    assert(statement_node != NULL);
    
    /* Create the process (may fail!) */
    j->first_process = this->create_job_process(j, *statement_node, ctx);
    if (j->first_process == NULL)
        process_errored = true;
    
    /* Construct process_ts for job continuations (pipelines), by walking the list until we hit the terminal (empty) job continuationf */
    const parse_node_t *job_cont = ctx.tree.get_child(job_node, 1, symbol_job_continuation);
    process_t *last_process = j->first_process;
    while (! process_errored && job_cont != NULL && job_cont->child_count > 0)
    {
        assert(job_cont->type == symbol_job_continuation);
        
        /* Get the statement node and make a process from it */
        const parse_node_t *statement_node = ctx.tree.get_child(*job_cont, 1, symbol_statement);
        assert(statement_node != NULL);
        
        /* Store the new process (and maybe with an error) */
        last_process->next = this->create_job_process(j, *statement_node, ctx);
        if (last_process->next == NULL)
            process_errored = true;

        /* Link the process and get the next continuation */
        last_process = last_process->next;
        job_cont = ctx.tree.get_child(*job_cont, 2, symbol_job_continuation);
    }
}

/**
   Evaluates a job from the specified tokenizer. First calls
   parse_job to parse the job and then calls exec to execute it.

   \param tok The tokenizer to read tokens from
*/

void parser_t::eval_job(tokenizer_t *tok)
{
    ASSERT_IS_MAIN_THREAD();

    int start_pos = job_start_pos = tok_get_pos(tok);
    long long t1=0, t2=0, t3=0;


    profile_item_t *profile_item = NULL;
    bool skip = false;
    int job_begin_pos;
    const bool do_profile = profile;

    if (do_profile)
    {
        profile_item = new profile_item_t();
        profile_item->skipped = 1;
        profile_items.push_back(profile_item);
        t1 = get_time();
    }

    switch (tok_last_type(tok))
    {
        case TOK_STRING:
        {
            job_t *j = this->job_create();
            job_set_flag(j, JOB_FOREGROUND, 1);
            job_set_flag(j, JOB_TERMINAL, job_get_flag(j, JOB_CONTROL));
            job_set_flag(j, JOB_TERMINAL, job_get_flag(j, JOB_CONTROL) \
                         && (!is_subshell && !is_event));
            job_set_flag(j, JOB_SKIP_NOTIFICATION, is_subshell \
                         || is_block \
                         || is_event \
                         || (!get_is_interactive()));

            current_block()->job = j;

            if (get_is_interactive())
            {
                if (tcgetattr(0, &j->tmodes))
                {
                    tok_next(tok);
                    wperror(L"tcgetattr");
                    job_free(j);
                    break;
                }
            }

            j->first_process = new process_t();
            job_begin_pos = tok_get_pos(tok);

            if (parse_job(j->first_process, j, tok) &&
                    j->first_process->get_argv())
            {
                if (job_start_pos < tok_get_pos(tok))
                {
                    long stop_pos = tok_get_pos(tok);
                    const wchar_t *newline = wcschr(tok_string(tok)+start_pos, L'\n');
                    if (newline)
                        stop_pos = mini<long>(stop_pos, newline - tok_string(tok));

                    j->set_command(wcstring(tok_string(tok)+start_pos, stop_pos-start_pos));
                }
                else
                    j->set_command(L"");

                if (do_profile)
                {
                    t2 = get_time();
                    profile_item->cmd = j->command();
                    profile_item->skipped=current_block()->skip;
                }

                /* If we're an ELSEIF, then we may want to unskip, if we're skipping because of an IF */
                if (job_get_flag(j, JOB_ELSEIF))
                {
                    bool skip_elseif = job_should_skip_elseif(j, current_block());

                    /* Record that we're entering an elseif */
                    if (! skip_elseif)
                    {
                        /* We must be an IF block here */
                        assert(current_block()->type() == IF);
                        static_cast<if_block_t *>(current_block())->is_elseif_entry = true;
                    }

                    /* Record that in the block too. This is similar to what builtin_else does. */
                    current_block()->skip = skip_elseif;
                }

                skip = skip || current_block()->skip;
                skip = skip || job_get_flag(j, JOB_WILDCARD_ERROR);
                skip = skip || job_get_flag(j, JOB_SKIP);

                if (!skip)
                {
                    int was_builtin = 0;
                    if (j->first_process->type==INTERNAL_BUILTIN && !j->first_process->next)
                        was_builtin = 1;
                    scoped_push<int> tokenizer_pos_push(&current_tokenizer_pos, job_begin_pos);
                    exec_job(*this, j);

                    /* Only external commands require a new fishd barrier */
                    if (!was_builtin)
                        set_proc_had_barrier(false);
                }
                else
                {
                    this->skipped_exec(j);
                }

                if (do_profile)
                {
                    t3 = get_time();
                    profile_item->level=eval_level;
                    profile_item->parse = (int)(t2-t1);
                    profile_item->exec=(int)(t3-t2);
                }

                if (current_block()->type() == WHILE)
                {
                    while_block_t *wb = static_cast<while_block_t *>(current_block());
                    switch (wb->status)
                    {
                        case WHILE_TEST_FIRST:
                        {
                            // PCA I added the 'wb->skip ||' part because we couldn't reliably
                            // control-C out of loops like this: while test 1 -eq 1; end
                            wb->skip = wb->skip || proc_get_last_status()!= 0;
                            wb->status = WHILE_TESTED;
                        }
                        break;
                    }
                }

                if (current_block()->type() == IF)
                {
                    if_block_t *ib = static_cast<if_block_t *>(current_block());

                    if (ib->skip)
                    {
                        /* Nothing */
                    }
                    else if (! ib->if_expr_evaluated)
                    {
                        /* Execute the IF */
                        bool if_result = (proc_get_last_status() == 0);
                        ib->any_branch_taken = if_result;

                        /* Don't execute if the expression failed */
                        current_block()->skip = ! if_result;
                        ib->if_expr_evaluated = true;
                    }
                    else if (ib->is_elseif_entry && ! ib->any_branch_taken)
                    {
                        /* Maybe mark an ELSEIF branch as taken */
                        bool elseif_taken = (proc_get_last_status() == 0);
                        ib->any_branch_taken = elseif_taken;
                        current_block()->skip = ! elseif_taken;
                        ib->is_elseif_entry = false;
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
                job_free(j);

                proc_set_last_status(1);
            }
            current_block()->job = 0;
            break;
        }

        case TOK_END:
        {
            if (tok_has_next(tok))
                tok_next(tok);
            break;
        }

        case TOK_BACKGROUND:
        {
            const wchar_t *str = tok_string(tok);
            if (tok_get_pos(tok)>0 && str[tok_get_pos(tok)-1] == L'&')
            {
                error(SYNTAX_ERROR,
                      tok_get_pos(tok),
                      CMD_AND_ERR_MSG,
                      tok_get_desc(tok_last_type(tok)));
            }
            else
            {
                error(SYNTAX_ERROR,
                      tok_get_pos(tok),
                      CMD_ERR_MSG,
                      tok_get_desc(tok_last_type(tok)));
            }

            return;
        }

        case TOK_ERROR:
        {
            error(SYNTAX_ERROR,
                  tok_get_pos(tok),
                  TOK_ERR_MSG,
                  tok_last(tok));

            return;
        }

        default:
        {
            error(SYNTAX_ERROR,
                  tok_get_pos(tok),
                  CMD_ERR_MSG,
                  tok_get_desc(tok_last_type(tok)));

            return;
        }
    }

    job_reap(0);
}

static void push_all_children(std::vector<node_offset_t> *execution_stack, const parse_node_t &node)
{
    // push nodes in reverse order, so the first node ends up on top
    unsigned child_idx = node.child_count;
    while (child_idx--)
    {
        execution_stack->push_back(node.child_offset(child_idx));
    }
}

void parser_t::execute_next(std::vector<node_offset_t> *execution_stack, const parser_context_t &ctx)
{
    assert(execution_stack != NULL);
    assert(! execution_stack->empty());
    
    /* Get the offset of the next node and remove it from the stack */
    node_offset_t next_offset = execution_stack->back();
    execution_stack->pop_back();
    
    /* Get the node */
    assert(next_offset < ctx.tree.size());
    const parse_node_t &node = ctx.tree.at(next_offset);
    
    /* Do something with it */
    switch (node.type)
    {
        case symbol_job_list:
            // These correspond to the three productions of job_list
            switch (node.production_idx)
            {
                case 0: // empty
                    break;
                
                case 1: //job, job_list
                    push_all_children(execution_stack, node);
                    break;
                
                case 2: //blank line, job_list
                    execution_stack->push_back(node.child_offset(1));
                    break;
                    
                default: //if we get here, it means more productions have been added to job_list, which is bad
                    PARSER_DIE();
                    break;
            }
            break;
        
        case symbol_job: //statement, job_continuation
            push_all_children(execution_stack, node);
            break;
            
        case symbol_job_continuation:
            switch (node.production_idx)
            {
                case 0: //empty
                    break;
                    
                case 1: //pipe, statement, job_continuation
                    execution_stack->push_back(node.child_offset(2));
                    execution_stack->push_back(node.child_offset(1));
                    break;
                
                default:
                    PARSER_DIE();
                    break;
            }
        break;
            
    }
}

/* Executes the job list at the given node offset */
void parser_t::execute_job_list(node_offset_t idx, const parser_context_t &ctx)
{
    assert(idx < ctx.tree.size());
    
    const parse_node_t *job_list = &ctx.tree.at(idx);
    assert(job_list->type == symbol_job_list);
    while (job_list != NULL)
    {
        // These correspond to the three productions of job_list
        // Try pulling out a job
        const parse_node_t *job = NULL;
        switch (job_list->production_idx)
        {
            case 0: // empty
                job_list = NULL;
                break;
            
            case 1: //job, job_list
                job = ctx.tree.get_child(*job_list, 0, symbol_job);
                job_list = ctx.tree.get_child(*job_list, 1, symbol_job_list);
                break;
            
            case 2: //blank line, job_list
                job = NULL;
                job_list = ctx.tree.get_child(*job_list, 1, symbol_job_list);
                break;
                
            default: //if we get here, it means more productions have been added to job_list, which is bad
                PARSER_DIE();
        }
        
        if (job != NULL)
        {
            this->eval_job(*job, ctx);
        }
    }
}

int parser_t::eval2(const wcstring &cmd_str, const io_chain_t &io, enum block_type_t block_type)
{
    parser_context_t mut_ctx;
    mut_ctx.src = cmd_str;
    
    /* Parse the tree */
    if (! parse_t::parse(cmd_str, parse_flag_none, &mut_ctx.tree, NULL))
    {
        return 1;
    }
    
    /* Make a const version for safety's sake */
    const parser_context_t &ctx = mut_ctx;

    CHECK_BLOCK(1);
    
    /* Record the current chain so we can put it back later */
    scoped_push<io_chain_t> block_io_push(&block_io, io);
    scoped_push<wcstring_list_t> forbidden_function_push(&forbidden_function);
    const size_t forbid_count = forbidden_function.size();
    const block_t * const start_current_block = this->current_block();
    
    /* Do some stuff I haven't figured out yet */
    job_reap(0);
    
    /* Only certain blocks are allowed */
    if ((block_type != TOP) &&
            (block_type != SUBST))
    {
        debug(1,
              INVALID_SCOPE_ERR_MSG,
              parser_t::get_block_desc(block_type));
        bugreport();
        return 1;
    }

    eval_level++;

    this->push_block(new scope_block_t(block_type));

    error_code = 0;

    event_fire(NULL);
    
    /* Execute the top level job list */
    execute_job_list(0, ctx);

    parser_t::pop_block();

    while (start_current_block != this->current_block())
    {
        if (this->current_block() == NULL)
        {
            debug(0,
                  _(L"End of block mismatch. Program terminating."));
            bugreport();
            FATAL_EXIT();
            break;
        }

        if ((!error_code) && (!exit_status()) && (!proc_get_last_status()))
        {

            //debug( 2, L"Status %d\n", proc_get_last_status() );

            debug(1,
                  L"%ls", parser_t::get_block_desc(current_block()->type()));
            debug(1,
                  BLOCK_END_ERR_MSG);
            fwprintf(stderr, L"%ls", parser_t::current_line());

            const wcstring h = builtin_help_get(*this, L"end");
            if (h.size())
                fwprintf(stderr, L"%ls", h.c_str());
            break;

        }
        parser_t::pop_block();
    }

    this->print_errors_stderr();

    while (forbidden_function.size() > forbid_count)
        parser_t::allow_function();

    /*
      Restore previous eval state
    */
    eval_level--;

    int code=error_code;
    error_code=0;

    job_reap(0);

    return code;
}

int parser_t::eval(const wcstring &cmd_str, const io_chain_t &io, enum block_type_t block_type)
{
    const wchar_t * const cmd = cmd_str.c_str();
    size_t forbid_count;
    int code;
    const block_t *start_current_block = current_block();

    /* Record the current chain so we can put it back later */
    scoped_push<io_chain_t> block_io_push(&block_io, io);

    scoped_push<wcstring_list_t> forbidden_function_push(&forbidden_function);

    if (block_type == SUBST)
    {
        forbidden_function.clear();
    }

    CHECK_BLOCK(1);

    forbid_count = forbidden_function.size();

    job_reap(0);

    debug(4, L"eval: %ls", cmd);


    if ((block_type != TOP) &&
            (block_type != SUBST))
    {
        debug(1,
              INVALID_SCOPE_ERR_MSG,
              parser_t::get_block_desc(block_type));
        bugreport();
        return 1;
    }

    eval_level++;

    this->push_block(new scope_block_t(block_type));

    tokenizer_t local_tokenizer(cmd, 0);
    scoped_push<tokenizer_t *> tokenizer_push(&current_tokenizer, &local_tokenizer);
    scoped_push<int> tokenizer_pos_push(&current_tokenizer_pos, 0);

    error_code = 0;

    event_fire(NULL);

    while (tok_has_next(current_tokenizer) &&
            !error_code &&
            !sanity_check() &&
            !exit_status())
    {
        this->eval_job(current_tokenizer);
        event_fire(NULL);
    }

    parser_t::pop_block();

    while (start_current_block != current_block())
    {
        if (current_block() == NULL)
        {
            debug(0,
                  _(L"End of block mismatch. Program terminating."));
            bugreport();
            FATAL_EXIT();
            break;
        }

        if ((!error_code) && (!exit_status()) && (!proc_get_last_status()))
        {

            //debug( 2, L"Status %d\n", proc_get_last_status() );

            debug(1,
                  L"%ls", parser_t::get_block_desc(current_block()->type()));
            debug(1,
                  BLOCK_END_ERR_MSG);
            fwprintf(stderr, L"%ls", parser_t::current_line());

            const wcstring h = builtin_help_get(*this, L"end");
            if (h.size())
                fwprintf(stderr, L"%ls", h.c_str());
            break;

        }
        parser_t::pop_block();
    }

    this->print_errors_stderr();

    tokenizer_push.restore();

    while (forbidden_function.size() > forbid_count)
        parser_t::allow_function();

    /*
      Restore previous eval state
    */
    eval_level--;

    code=error_code;
    error_code=0;

    job_reap(0);

    return code;
}


/**
   \return the block type created by the specified builtin, or -1 on error.
*/
block_type_t parser_get_block_type(const wcstring &cmd)
{
    for (size_t i=0; block_lookup[i].desc; i++)
    {
        if (block_lookup[i].name && cmd == block_lookup[i].name)
        {
            return block_lookup[i].type;
        }
    }
    return (block_type_t)-1;
}

/**
   \return the block command that createa the specified block type, or null on error.
*/
const wchar_t *parser_get_block_command(int type)
{
    for (size_t i=0; block_lookup[i].desc; i++)
    {
        if (block_lookup[i].type == type)
        {
            return block_lookup[i].name;
        }
    }
    return NULL;
}

/**
   Test if this argument contains any errors. Detected errors include
   syntax errors in command substitutions, improperly escaped
   characters and improper use of the variable expansion operator.
*/
int parser_t::parser_test_argument(const wchar_t *arg, wcstring *out, const wchar_t *prefix, int offset)
{
    int err=0;

    wchar_t *paran_begin, *paran_end;
    wchar_t *arg_cpy;
    int do_loop = 1;

    CHECK(arg, 1);

    arg_cpy = wcsdup(arg);

    while (do_loop)
    {
        switch (parse_util_locate_cmdsubst(arg_cpy,
                                           &paran_begin,
                                           &paran_end,
                                           false))
        {
            case -1:
                err=1;
                if (out)
                {
                    error(SYNTAX_ERROR,
                          offset,
                          L"Mismatched parenthesis");
                    this->print_errors(*out, prefix);
                }
                free(arg_cpy);
                return err;

            case 0:
                do_loop = 0;
                break;

            case 1:
            {

                const wcstring subst(paran_begin + 1, paran_end);
                wcstring tmp;

                tmp.append(arg_cpy, paran_begin - arg_cpy);
                tmp.push_back(INTERNAL_SEPARATOR);
                tmp.append(paran_end+1);

//        debug( 1, L"%ls -> %ls %ls", arg_cpy, subst, tmp.buff );

                parse_error_list_t errors;
                err |= parse_util_detect_errors(subst, &errors);
                if (out && ! errors.empty())
                {
                    out->append(parse_errors_description(errors, subst, prefix));
                }

                free(arg_cpy);
                arg_cpy = wcsdup(tmp.c_str());

                break;
            }
        }
    }

    wcstring unesc;
    if (! unescape_string(arg_cpy, &unesc, UNESCAPE_SPECIAL))
    {
        if (out)
        {
            error(SYNTAX_ERROR,
                  offset,
                  L"Invalid token '%ls'", arg_cpy);
            print_errors(*out, prefix);
        }
        return 1;
    }
    else
    {
        /* Check for invalid variable expansions */
        const size_t unesc_size = unesc.size();
        for (size_t idx = 0; idx < unesc_size; idx++)
        {
            switch (unesc.at(idx))
            {
                case VARIABLE_EXPAND:
                case VARIABLE_EXPAND_SINGLE:
                {
                    wchar_t next_char = (idx + 1 < unesc_size ? unesc.at(idx + 1) : L'\0');

                    if (next_char != VARIABLE_EXPAND &&
                            next_char != VARIABLE_EXPAND_SINGLE &&
                            ! wcsvarchr(next_char))
                    {
                        err=1;
                        if (out)
                        {
                            expand_variable_error(*this, unesc, idx, offset);
                            print_errors(*out, prefix);
                        }
                    }

                    break;
                }
            }
        }
    }

    free(arg_cpy);

    return err;

}

int parser_t::test_args(const  wchar_t * buff, wcstring *out, const wchar_t *prefix)
{
    int do_loop = 1;
    int err = 0;

    CHECK(buff, 1);

    tokenizer_t tok(buff, 0);
    scoped_push<tokenizer_t*> tokenizer_push(&current_tokenizer, &tok);
    scoped_push<int> tokenizer_pos_push(&current_tokenizer_pos);

    for (; do_loop && tok_has_next(&tok); tok_next(&tok))
    {
        current_tokenizer_pos = tok_get_pos(&tok);
        switch (tok_last_type(&tok))
        {

            case TOK_STRING:
            {
                err |= parser_test_argument(tok_last(&tok), out, prefix, tok_get_pos(&tok));
                break;
            }

            case TOK_END:
            {
                break;
            }

            case TOK_ERROR:
            {
                if (out)
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(&tok),
                          TOK_ERR_MSG,
                          tok_last(&tok));
                    print_errors(*out, prefix);
                }
                err=1;
                do_loop=0;
                break;
            }

            default:
            {
                if (out)
                {
                    error(SYNTAX_ERROR,
                          tok_get_pos(&tok),
                          UNEXPECTED_TOKEN_ERR_MSG,
                          tok_get_desc(tok_last_type(&tok)));
                    print_errors(*out, prefix);
                }
                err=1;
                do_loop=0;
                break;
            }
        }
    }

    error_code=0;

    return err;
}

// helper type used in parser::test below
struct block_info_t
{
    int position; //tokenizer position
    block_type_t type; //type of the block
};

void parser_t::get_backtrace(const wcstring &src, const parse_error_list_t &errors, wcstring *output) const
{
    assert(output != NULL);
    if (! errors.empty())
    {
        const parse_error_t err = errors.at(0);
        
        // Determine which line we're on
        assert(err.source_start <= src.size());
        size_t which_line = 1 + std::count(src.begin(), src.begin() + err.source_start, L'\n');
        
        const wchar_t *filename = this->current_filename();
        if (filename)
        {
            append_format(*output, _(L"fish: line %lu of %ls:\n"), which_line, user_presentable_path(filename).c_str());
        }
        else
        {
            output->append(L"fish: ");
        }
        
        // Don't include the caret if we're interactive, this is the first line of text, and our source is at its beginning, because then it's obvious
        bool skip_caret = (get_is_interactive() && which_line == 1 && err.source_start == 0);
        
        output->append(err.describe(src, skip_caret));
        output->push_back(L'\n');
        
        this->stack_trace(0, *output);
    }
}

block_t::block_t(block_type_t t) :
    block_type(t),
    made_fake(false),
    skip(),
    had_command(),
    tok_pos(),
    loop_status(),
    job(),
    src_filename(),
    src_lineno(),
    wants_pop_env(false),
    event_blocks()
{
}

block_t::~block_t()
{
}

/* Various block constructors */

if_block_t::if_block_t() :
    block_t(IF),
    if_expr_evaluated(false),
    is_elseif_entry(false),
    any_branch_taken(false),
    else_evaluated(false)
{
}

event_block_t::event_block_t(const event_t &evt) :
    block_t(EVENT),
    event(evt)
{
}

function_block_t::function_block_t(const process_t *p, const wcstring &n, bool shadows) :
    block_t(shadows ? FUNCTION_CALL : FUNCTION_CALL_NO_SHADOW),
    process(p),
    name(n)
{
}

source_block_t::source_block_t(const wchar_t *src) :
    block_t(SOURCE),
    source_file(src)
{
}

for_block_t::for_block_t(const wcstring &var) :
    block_t(FOR),
    variable(var),
    sequence()
{
}

while_block_t::while_block_t() :
    block_t(WHILE),
    status(0)
{
}

switch_block_t::switch_block_t(const wcstring &sv) :
    block_t(SWITCH),
    switch_taken(false),
    switch_value(sv)
{
}

fake_block_t::fake_block_t() :
    block_t(FAKE)
{
}

function_def_block_t::function_def_block_t() :
    block_t(FUNCTION_DEF),
    function_data()
{
}

scope_block_t::scope_block_t(block_type_t type) :
    block_t(type)
{
    assert(type == BEGIN || type == TOP || type == SUBST);
}

breakpoint_block_t::breakpoint_block_t() :
    block_t(BREAKPOINT)
{
}

