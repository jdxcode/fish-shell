/** \file parser.h
	The fish parser. 	
*/

#ifndef FISH_PARSER_H
#define FISH_PARSER_H

#include <wchar.h>

#include "proc.h"
#include "util.h"
#include "parser.h"
#include "event.h"
#include "function.h"
#include <vector>
#include <deque>
#include <memory>

#define PARSER_TEST_ERROR 1
#define PARSER_TEST_INCOMPLETE 2

/**
   event_block_t represents a block on events of the specified type
*/
struct event_block_t
{
	/**
	   The types of events to block. This is interpreted as a bitset
	   whete the value is 1 for every bit corresponding to a blocked
	   event type. For example, if EVENT_VARIABLE type events should
	   be blocked, (type & 1<<EVENT_BLOCKED) should be set. 

	   Note that EVENT_ANY can be used to specify any event.
	*/
	unsigned int typemask;
};

typedef std::deque<event_block_t> event_block_list_t;

inline bool event_block_list_blocks_type(const event_block_list_t &ebls, int type) {
    for (event_block_list_t::const_iterator iter = ebls.begin(); iter != ebls.end(); iter++) {
        if( iter->typemask & (1<<EVENT_ANY ) )
            return true;
        if( iter->typemask & (1<<type) )
            return true;
    }
    return false;
}


/** Block state template, to replace the discriminated union */
struct block_state_base_t {
    public:
    virtual ~block_state_base_t() {}
};

template<typename T>
struct block_state_t : public block_state_base_t {
    T value;    
    block_state_t() : value() {}
};

/**
   block_t represents a block of commands. 
*/
typedef struct block
{
	int type; /**< Type of block. Can be one of WHILE, FOR, IF and FUNCTION */
	int skip; /**< Whether execution of the commands in this block should be skipped */
	int tok_pos; /**< The start index of the block */
	int had_command; /**< Set to non-zero once a command has been executed in this block */
	
	/**
	   Status for the current loop block. Can be any of the values from the loop_status enum.
	*/
	int loop_status;

	/**
	   The job that is currently evaluated in the specified block.
	*/
	job_t *job;

	/**
	   Block type-specific data
	*/
	std::auto_ptr<function_data_t> function_data;
	
#if 0
	union 
	{
		int while_state;  /**< True if the loop condition has not yet been evaluated*/
		wchar_t *for_variable; /**< Name of the variable to loop over */
		int if_state; /**< The state of the if block, can be one of IF_STATE_UNTESTED, IF_STATE_FALSE, IF_STATE_TRUE */
		wchar_t *switch_value; /**< The value to test in a switch block */
		const wchar_t *source_dest; /**< The name of the file to source*/
		event_t *event; /**<The event that triggered this block */
		wchar_t *function_call_name;
	} param1;
#endif

	/** First block type specific variable	*/
    block_state_base_t *state1_ptr;

    template<typename T>
    T& state1(void) {
        block_state_t<T> *state;
        if (state1_ptr == NULL) {
            state = new block_state_t<T>();
            state1_ptr = state;
        } else {
            state = dynamic_cast<block_state_t<T> *>(state1_ptr);
            if (state == NULL) {
                printf("Expected type %s, but instead got type %s\n", typeid(T).name(), typeid(*state1_ptr).name());
                abort();
            }
        }
        return state->value;
    }

	/** Second block type specific variable	*/
    block_state_base_t *state2_ptr;

    template<typename T>
    T& state2(void) {
        block_state_t<T> *state;
        if (state2_ptr == NULL) {
            state = new block_state_t<T>();
            state2_ptr = state;
        } else {
            state = dynamic_cast<block_state_t<T> *>(state2_ptr);
            assert(state != NULL);
        }
        return state->value;
    }

	/**
	   Name of file that created this block
	*/
	const wchar_t *src_filename;
	
	/**
	   Line number where this block was created
	*/
	int src_lineno;
    
    /** Whether we should pop the environment variable stack when we're popped off of the block stack */
    bool wants_pop_env;
	
	/** List of event blocks. */
	event_block_list_t event_blocks;
	
    /**
	   Next outer block 
	*/
	struct block *outer;
    
    /** Destructor */
    ~block()
    {
        if (state1_ptr != NULL)
            delete state1_ptr;
        if (state2_ptr != NULL)
            delete state2_ptr;
    }
} block_t;

/** 
	Types of blocks
*/
enum block_type_t
{
	WHILE, /**< While loop block */
	FOR,  /**< For loop block */
	IF, /**< If block */
	FUNCTION_DEF, /**< Function definition block */
	FUNCTION_CALL, /**< Function invocation block */
	FUNCTION_CALL_NO_SHADOW, /**< Function invocation block with no variable shadowing */
	SWITCH, /**< Switch block */
	FAKE, /**< Fake block */
	SUBST, /**< Command substitution scope */
	TOP, /**< Outermost block */
	BEGIN, /**< Unconditional block */
	SOURCE, /**< Block created by the . (source) builtin */
	EVENT, /**< Block created on event notifier invocation */
	BREAKPOINT, /**< Breakpoint block */
}
;

/**
   Possible states for a loop
*/
enum loop_status 
{
	LOOP_NORMAL, /**< Current loop block executed as normal */
	LOOP_BREAK, /**< Current loop block should be removed */
	LOOP_CONTINUE, /**< Current loop block should be skipped */
};


/**
   Possible states for a while block
*/
enum while_status
{
	WHILE_TEST_FIRST, /**< This is the first command of the first lap of a while loop */
	WHILE_TEST_AGAIN, /**< This is not the first lap of the while loop, but it is the first command of the loop */
	WHILE_TESTED, /**< This is not the first command in the loop */
}
;



/**
   Errors that can be generated by the parser
*/
enum parser_error 
{
	/**
	   No error
	*/
	NO_ERR=0,
	/**
	   An error in the syntax 
	*/
	SYNTAX_ERROR,
	/**
	   Error occured while evaluating commands
	*/
	EVAL_ERROR,
	/**
	   Error while evaluating cmdsubst
	*/
	CMDSUBST_ERROR,
};

enum parser_type_t {
    PARSER_TYPE_NONE,
    PARSER_TYPE_GENERAL,
    PARSER_TYPE_FUNCTIONS_ONLY,
    PARSER_TYPE_COMPLETIONS_ONLY,
	PARSER_TYPE_ERRORS_ONLY
};

struct profile_item_t {
	/**
	   Time spent executing the specified command, including parse time for nested blocks.
	*/
	int exec;
	/**
	   Time spent parsing the specified command, including execution time for command substitutions.
	*/
	int parse;
	/**
	   The block level of the specified command. nested blocks and command substitutions both increase the block level.
	*/
	size_t level;
	/**
	   If the execution of this command was skipped.
	*/
	int skipped;
	/**
	   The command string.
	*/
	wcstring cmd;    
};

struct tokenizer;

class parser_t {
    private:
    enum parser_type_t parser_type;
    std::vector<block_t> blocks;
    
    /** Last error code */
    int error_code;
    
    /** Position of last error */
    int err_pos;
    
    /** Description of last error */
    wcstring err_buff;
    
    /** Pointer to the current tokenizer */
    tokenizer *current_tokenizer;
    
    /** String for representing the current line */
    wcstring lineinfo;
    
    /** This is the position of the beginning of the currently parsed command */
    int current_tokenizer_pos;
    
    /** List of called functions, used to help prevent infinite recursion */
    wcstring_list_t forbidden_function;
    
    /** String index where the current job started. */
    int job_start_pos;
    
    /**
       Keeps track of how many recursive eval calls have been made. Eval
       doesn't call itself directly, recursion happens on blocks and on
       command substitutions.
    */
    int eval_level;
    
    /* No copying allowed */
    parser_t(const parser_t&);
    parser_t& operator=(const parser_t&);
    
    /**
       Returns the name of the currently evaluated function if we are
       currently evaluating a function, null otherwise. This is tested by
       moving down the block-scope-stack, checking every block if it is of
       type FUNCTION_CALL.
    */
    const wchar_t *is_function() const;
    
    void parse_job_argument_list( process_t *p, job_t *j, tokenizer *tok, std::vector<completion_t>& );
    int parse_job( process_t *p, job_t *j, tokenizer *tok );
    void skipped_exec( job_t * j );
    void eval_job( tokenizer *tok );
    int parser_test_argument( const wchar_t *arg, wcstring *out, const wchar_t *prefix, int offset );
    void print_errors( wcstring &target, const wchar_t *prefix );
    void print_errors_stderr();
    
    public:
    std::vector<profile_item_t> profile_items;
    
    /** Get the "principal" parser, whatever that is */
    static parser_t &principal_parser();
    
    /** Create a parser of the given type */
    parser_t(enum parser_type_t type);
    
    /** The current innermost block, allocated with new */
    block_t *current_block;
    
    /** Global event blocks */
    event_block_list_t global_event_blocks;
    
    /** Current block level io redirections  */
    io_data_t *block_io;
    
    /**
      Evaluate the expressions contained in cmd.

      \param cmd the string to evaluate
      \param io io redirections to perform on all started jobs
      \param block_type The type of block to push on the block stack

      \return 0 on success, 1 otherwise
    */
    int eval( const wcstring &cmd, io_data_t *io, enum block_type_t block_type );
    
    /**
      Evaluate line as a list of parameters, i.e. tokenize it and perform parameter expansion and cmdsubst execution on the tokens.
      The output is inserted into output, and should be freed by the caller.

      \param line Line to evaluate
      \param output List to insert output to
    */
	/**
	  \param line Line to evaluate
	  \param output List to insert output to
	*/
	int eval_args( const wchar_t *line, std::vector<completion_t> &output );

    /**
       Sets the current evaluation error. This function should only be used by libraries that are called by 

       \param ec The new error code
       \param p The character offset at which the error occured
       \param str The printf-style error message filter
    */
    void error( int ec, int p, const wchar_t *str, ... );
    
    /**
       Returns a string describing the current parser pisition in the format 'FILENAME (line LINE_NUMBER): LINE'.
       Example: 

       init.fish (line 127): ls|grep pancake
    */
    const wchar_t *current_line();

    /**
       Returns the current line number
    */
    int get_lineno() const;

    /**
       Returns the current position in the latest string of the tokenizer.
    */
    int get_pos() const;

    /**
       Returns the position where the current job started in the latest string of the tokenizer.
    */
    int get_job_pos() const;

    /**
       Set the current position in the latest string of the tokenizer.
    */
    void set_pos( int p);

    /**
       Get the string currently parsed
    */
    const wchar_t *get_buffer() const;

    /**
       Create block of specified type
    */
    void push_block( int type);

    /**
       Remove the outermost block namespace
    */
    void pop_block();

    /**
       Return a description of the given blocktype
    */
    const wchar_t *get_block_desc( int block ) const;


    /**
       Test if the specified string can be parsed, or if more bytes need
       to be read first. The result will have the PARSER_TEST_ERROR bit
       set if there is a syntax error in the code, and the
       PARSER_TEST_INCOMPLETE bit set if the code contains unclosed
       blocks.

       \param buff the text buffer to test
       \param block_level if non-null, the block nesting level will be filled out into this array
       \param out if non-null, any errors in the command will be filled out into this buffer
       \param prefix the prefix string to prepend to each error message written to the \c out buffer
    */
    int test( const wchar_t * buff, int *block_level, wcstring *out, const wchar_t *prefix );

    /**
       Test if the specified string can be parsed as an argument list,
       e.g. sent to eval_args.  The result has the first bit set if the
       string contains errors, and the second bit is set if the string
       contains an unclosed block.
    */
    int test_args( const wchar_t * buff, wcstring *out, const wchar_t *prefix );

    /**
       Tell the parser that the specified function may not be run if not
       inside of a conditional block. This is to remove some possibilities
       of infinite recursion.
    */
    void forbid_function( const wcstring &function );
    /**
       Undo last call to parser_forbid_function().
    */
    void allow_function();

    /**
       Initialize static parser data
    */
    void init();

    /**
       Destroy static parser data
    */
    void destroy();

    /**
       This function checks if the specified string is a help option. 

       \param s the string to test
       \param min_match is the minimum number of characters that must match in a long style option, i.e. the longest common prefix between --help and any other option. If less than 3, 3 will be assumed.
    */
    int is_help( const wchar_t *s, int min_match ) const;

    /**
       Returns the file currently evaluated by the parser. This can be
       different than reader_current_filename, e.g. if we are evaulating a
       function defined in a different file than the one curently read.
    */
    const wchar_t *current_filename() const;

    /**
       Write a stack trace starting at the specified block to the specified wcstring
    */
    void stack_trace( block_t *b, wcstring &buff);

    int get_block_type( const wchar_t *cmd ) const;
    const wchar_t *get_block_command( int type ) const;
};


#endif
