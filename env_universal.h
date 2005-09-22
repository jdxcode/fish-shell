/** \file env_universl.h
	Universal variable client library
*/

#ifndef ENV_UNIVERSAL_HH
#define ENV_UNIVERSAL_HH

#include "env_universal_common.h"

/**
   Data about the universal variable server.
*/
extern connection_t env_universal_server;

/**
   Update flag. Set to 1 whenever an update has occured.
*/
extern int env_universal_update;

/**
   Initialize the envuni library
*/
void env_universal_init();
/*
  Free memory used by envuni
*/
void env_universal_destroy();

/**
   Get the value of a universal variable
*/
wchar_t *env_universal_get( const wchar_t *name );

/**
   Get the export flag of the variable with the specified
   name. Returns 0 if the variable doesn't exist.
*/
int env_universal_get_export( const wchar_t *name );

/**
   Set the value of a universal variable
*/
void env_universal_set( const wchar_t *name, const wchar_t *val, int export );
/**
   Erase a universal variable
*/
void env_universal_remove( const wchar_t *name );

int env_universal_read_all();

void env_universal_get_names( array_list_t *l,
							  int show_exported,
							  int show_unexported );

void env_universal_barrier();

#endif
