/** \file parser.h
	The fish parser. 	
*/

/**
   block_t represents a block of commands. 
*/
typedef struct block
{
	int type; /**< Type of block. Can be one of WHILE, FOR, IF and FUNCTION */
	int skip; /**< Whether execution of the commands in this block should be skipped */
	int tok_pos; /**< The start index of the block */

	/**
	   Status for the current loop block. Can be anu of the values from the loop_status enum.
	*/
	int loop_status;

	/**
	   First block type specific variable
	*/
	union 
	{
		int while_state;  /**< True if the loop condition has not yet been evaluated*/
		wchar_t *for_variable; /**< Name of the variable to loop over */
		int if_state; /**< The state of the if block */
		wchar_t *switch_value; /**< The value to test in a switch block */
		wchar_t *function_name; /**< The name of the function to define */
	};	

	/**
	   Second block type specific variable
	*/
	union
	{
		array_list_t for_vars; /**< List of values for a for block */	
		int switch_taken; /**< Whether a switch match has already been found */
		wchar_t *function_description; /**< The description of the function to define */
	};

	/**
	   Third block type specific variable
	*/
	union
	{
		int function_is_binding; /**< Whether a function is a keybinding */
	};

    /**
	   Next outer block 
	*/
	struct block *outer; 
} block_t;

/** 
	Types of blocks
*/
enum block_type
{
	WHILE, /**< While loop block */
	FOR,  /**< For loop block */
	IF, /**< If block */
	FUNCTION_DEF, /**< Function definition block */
	FUNCTION_CALL, /**< Function invocation block */
	SWITCH, /**< Switch block */
	FAKE, /**< Fake block */
	SUBST, /**< Command substitution scope */
	TOP, /**< Outermost block */
	BEGIN, /**< Unconditional block */
	AND, /**< And block */
	OR, /**< Or block */
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
	NO_ERR=0,
	SYNTAX_ERROR,
	EVAL_ERROR,
	OOM,
	STACK_ERROR,
	SUBSHELL_ERROR
}
;

/** The current innermost block */
extern block_t *current_block;

/** The current error code */
extern int error_code;

/**
   Current block level io redirections 
*/
extern io_data_t *block_io;

/**
  Finds the full path of an executable in a newly allocated string.
  
  \param cmd The name of the executable.
  \return 0 if the command can not be found, the path of the command otherwise.
*/
wchar_t *get_filename( const wchar_t *cmd );

/**
  Evaluate the expressions contained in cmd.

  \param cmd the string to evaluate
  \param out buffer to insert output to. May be null.
  \param the type of block to push onto the scope stack
  \param block_type The type of block to push on the block stack
  \return 0 on success.
*/
int eval( const wchar_t *cmd, io_data_t *io, int block_type );

/**
  Evaluate line as a list of parameters, i.e. tokenize it and perform parameter expansion and subshell execution on the tokens.
  The output is inserted into output, and should be freed by the caller.

  \param line Line to evaluate
  \param output List to insert output to
*/
int eval_args( const wchar_t *line,
				array_list_t *output );

/**
   Sets the current error

   \param ec The new error code
   \param str The new error message
   \param p The character offset at which the error occured
*/
void error( int ec, const wchar_t *str, int p );

/**
   Tests if the specified commands parameters should be interpreted as another command, which will be true if the command is either 'command', 'exec', 'if', 'while' or 'builtin'.  

   \param cmd The command name to test
   \return 1 of the command parameter is a command, 0 otherwise
*/

int parser_is_subcommand( const wchar_t *cmd );

/**
   Tests if the specified command is a reserved word, i.e. if it is
   the name of one of the builtin functions that change the block or
   command scope, like 'for', 'end' or 'command' or 'exec'. These
   functions may not be overloaded, so their names are reserved.

   \param cmd The command name to test
   \return 1 of the command parameter is a command, 0 otherwise
*/
int parser_is_reserved( wchar_t *word);

/**
   Returns a string describing the current parser pisition in the format 'FILENAME (line LINE_NUMBER): LINE'.
   Example: 

   init.fish (line 127): ls|grep pancake
*/
wchar_t *parser_current_line();

/**
   Returns the current position in the latest string of the tokenizer.
*/
int parser_get_pos();

/**
   Returns the position where the current job started in the latest string of the tokenizer.
*/
int parser_get_job_pos();

/**
   Set the current position in the latest string of the tokenizer.
*/
void parser_set_pos( int p);

/**
   Get the string currently parsed
*/
const wchar_t *parser_get_buffer();

/**
   Create block of specified type
*/
void parser_push_block( int type);

/**
   Remove the outermost block namespace
*/
void parser_pop_block();

/**
   Return a description of the given blocktype
*/
wchar_t *parser_get_block_desc( int block );


/**
   Test if the specified string can be parsed, or if more bytes need to be read first. 
   The result has the first bit set if the string contains errors, and the second bit is set if the string contains an unclosed block.
*/
int parser_test( wchar_t * buff, int babble );

/**
   Returns the full path of the specified directory. If the \c in is a
   full path to an existing directory, a copy of the string is
   returned. If \c in is a directory relative to one of the
   directories i the CDPATH, the full path is returned. If no
   directory can be found, 0 is returned.
*/
wchar_t *parser_cdpath_get( wchar_t *in );

/**
   Tell the parser that the specified function may not be run if not
   inside of a conditional block. This is to remove some possibilities
   of infinite recursion.
*/
void parser_forbid_function( wchar_t *function );
/**
   Undo last call to parser_forbid_function().
*/
void parser_allow_function();

/**
   Initialize the parser
*/
void parser_init();

/**
   Destroy the parser
*/
void parser_destroy();

/**
   This function checks if the specified string is a help option. 

   \param s the string to test
   \param min_match is the minimum number of characters that must match in a long style option, i.e. the longest common prefix between --help and any other option. If less than 3, 3 will be assumed.
*/
int parser_is_help( wchar_t *s, int min_match );

