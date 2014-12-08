/*
 * Test laucher to execvp() and allow automatic
 * setting of LD_PRELOAD=<launcherdir>/wrap_open.so 
 *
 * $Id: gccwrap.c,v 1.5 2014/12/06 10:38:16 sjg Exp $
 *
 * (c) 
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>


// #define DEBUGME

#include "debugme.h"

/**********************************************************************
 */
 
int main( int argc, char **argv )
{
    int retv = 0 ;
    
    char *p = NULL ;
    
    if( argc < 2 )
    {
        fprintf( stderr, "Not enough arguments\n\nFormat is wrap_open <command> <command-args>\n\n" ) ;
        
        fflush( stderr ) ;
    
        return -1 ;
    }
    
    char path[PATH_MAX] ;
    
    char *q = *argv ;
    
    p = path ;
    
    while( *q != 0 )
    {
        *p = *q ;
        p++ ;
        q++ ;
    };
    
    while( ( p != path ) && ( *p != '/' ) )
    {
        p-- ;
    };
    
    if( p == path )
    {
        *p++ = '.' ;
        *p++ = '/' ;
    }
    else
    {
        p++ ;
    }
    
    strcpy( p, "wrap_open.so" ) ;
    
    SJGF( stderr, "path = %s\n", path ) ;
    
    retv = setenv( "LD_PRELOAD", path, 1 ) ;
    
    if( retv != 0 )
    {
        fprintf( stderr, "Could not set LD_PRELOAD.\n" ) ;
    
        return -1 ;
    }
    
    retv = setenv( "WRAP_OPEN_COMMAND", argv[1], 1 ) ;
    
    if( retv != 0 )
    {
        fprintf( stderr, "Could not set WRAP_OPEN_COMMAND.\n" ) ;
    
        return -1 ;
    }
    
    /* Now execvp() using "gcc", "clang" or "cc" as argv[0]
     */
    
#ifdef TARGET_GCC
    argv[1] = "gcc" ;
#elif TARGET_CLANG
    argv[1] = "clang" ;
#else
    argv[1] = "cc" ;
#endif
    
    retv = execvp( argv[1], argv+1 ) ;
    
    fprintf( stderr, "Could not execvp( %s ... )\n", argv[1] ) ;
    
    return retv ;
}


