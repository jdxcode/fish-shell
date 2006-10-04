/** \file screen.h High level library for handling the terminal screen

	The screen library allows the interactive reader to write its
	output to screen efficiently by keeping an inetrnal representation
	of the current screen contents and trying to find the most
	efficient way for transforming that to the desired screen content.
*/
#ifndef FISH_SCREEN_H
#define FISH_SCREEN_H

/**
   The struct representing the current and desired screen contents.
*/
typedef struct
{
	/*
	  The internal representation of the desired screen contents.
	*/
	array_list_t desired;
	/**
	  The internal representation of the actual screen contents.
	*/
	array_list_t actual;
	/**
	   The desired cursor position.
	 */
	int desired_cursor[2];
	/**
	   The actual cursor position.
	*/
	int actual_cursor[2];
	/**
	   A stringbuffer containing the prompt which was last printed to
	   the screen.
	*/
	string_buffer_t actual_prompt;

	/*
	  The actual width of the screen at the time of the last screen
	  write.
	*/
	int actual_width;	
}
	screen_t;

/**
   A struct representing a single line of a screen. Consists of two
   array_lists, which must always be of the same length.
*/
typedef struct
{
	/**
	   The text contents of the line. Each element of the array
	   represents on column of output. Because some characters are two
	   columns wide, it is perfectly possible for some of the comumns
	   to be empty. 
	*/
	array_list_t text;
	/**
	   Highlight information for the line
	*/
	array_list_t color;
}
	line_t;

/**
   Initialize a new screen struct
*/
void s_init( screen_t *s );

/**
   Free all memory used by the specified screen struct
*/
void s_destroy( screen_t *s );

/**
   This is the main function for the screen putput library. It is used
   to define the desired contents of the screen. The screen command
   will use it's knowlege of the current contents of the screen in
   order to render the desired output using as few terminal commands
   as possible.
*/
void s_write( screen_t *s, 
			  wchar_t *prompt, 
			  wchar_t *commandline,
			  int *colors, 
			  int cursor_pos );

/** 
	This function resets the screen buffers internal knowledge about
	the contents of the screen. Use this function when some other
	function than s_write has written to the screen.
*/
void s_reset( screen_t *s );

#endif
