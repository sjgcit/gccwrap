/*
 * Test laucher to execvp() and allow automatic
 * setting of LD_PRELOAD=<launcherdir>/wrap_open.so 
 *
 * $Id: gccwrap.c,v 1.7 2014/12/12 06:17:29 sjg Exp $
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
    
    TURN_ON_DEBUG() ;
    
    if( argc < 2 )
    {
        errorf( "Not enough arguments\n\nFormat is %s <command-list> <compiler-args>\n\n", argv[0] ) ;
        
        fflush( stderr ) ;
    
        return -1 ;
    }
    
    char path[PATH_MAX] ;
    
    char *q = NULL ;
    
    if( argv[0][0] != '/' )
    {
        /* Not a proper path so need to find wrap_open.so
         * somewhere on PATH
         */
        
        SJGF( "Trying PATH search...( %s )\n", *argv ) ;
        
        q = getenv( "PATH" ) ;
        
        SJGF( "PATH = %s\n", q ) ;
        
        if( q == NULL )
        {
            errorf( "Cannot locate wrap_open.so\n" ) ;
            
            return -1 ;
        }
        
        while( *q != 0 )
        {
            p = path ;
            
            while( *q != ':' )
            {
                *p++ = *q++ ;
            };
            
            if( *p != '/' )
                *p++ = '/' ;
            
            strcpy( p, "wrap_open.so" ) ;
            
            SJGF( "Trying path = [%s]\n", path ) ;
            
            if( access( path, F_OK ) == 0 )
            {
                SJGF( "Found wrap_open.so ..." ) ;
                
                break ;
            }
            
            q++ ;
        };
    }
    else
    {
        /* presumably a proper path so just use it
         */
        
        SJGF( "Using path from %s ...", argv[0] ) ;
        
        q = *argv ;
        
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
    }
    
    
    SJGF( "path = %s\n", path ) ;
    
    retv = setenv( "LD_PRELOAD", path, 1 ) ;
    
    if( retv != 0 )
    {
        errorf( "Could not set LD_PRELOAD.\n" ) ;
    
        return -1 ;
    }
    
    retv = setenv( "WRAP_OPEN_COMMAND", argv[1], 1 ) ;
    
    if( retv != 0 )
    {
        errorf( "Could not set WRAP_OPEN_COMMAND.\n" ) ;
    
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
    
    errorf( "Could not execvp( %s ... )\n", argv[1] ) ;
    
    return retv ;
}



