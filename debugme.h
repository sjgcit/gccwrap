
/*
 * Include file debugme.h
 *
 * $Id: debugme.h,v 1.6 2014/12/06 11:31:26 sjg Exp $
 *
 * (c) Stephen Geary, Dec 2014
 *
 * Some simple reporting routines for debugging.
 *
 * To activate the debugging code define DEBUGME before
 * including this file.
 *
 * Also include an error reporting macro.
 */



#ifndef __DEBUGME_H
#  define __DEBUGME_H

/****************************************************
 */

#define errorf(...) { \
                        fprintf( stderr, "%6d :: %16s  ", __LINE__, __func__ ) ; \
                        fprintf( stderr, __VA_ARGS__ ) ; \
                        fprintf( stderr, "\n" ) ; \
                        fflush( stderr ) ; \
                    }


#define DEBUGME_OFF   0
#define DEBUGME_ON    1

/****************************************************
 */

#ifndef DEBUGME
#  define SJG()
#  define SJGF(...)

#  define TURN_ON_DEBUG()
#  define TURN_OFF_DEBUG()

#  define INIT_DEBUGME()

#else /* DEBUGME */

#  include <stdio.h>

#  include <sys/syscall.h>
#  include <sys/types.h>

  extern void debugme_turnon() ;

  extern void debugme_turnoff() ;

  extern void debugme_init() ;
  
  extern int debugme_get_state() ;

#  define TURN_ON_DEBUG()   debugme_turnon()
#  define TURN_OFF_DEBUG()  debugme_turnoff()

#  define INIT_DEBUGME()    debugme_init()

#  define SJG_TIDF()    if( debugme_get_state() == DEBUGME_ON ) \
                        { \
                            pid_t tid ; \
                            \
                            tid = getpid() ; \
                            \
                            fprintf( stderr, "%x :: ", tid ) ; \
                            \
                            tid = syscall( SYS_gettid ) ; \
                            \
                            fprintf( stderr, "%x :: ", tid ) ; \
                        }

#  define SJG()     if( debugme_get_state() == DEBUGME_ON ) \
                    { \
                        SJG_TIDF() ; \
                        fprintf( stderr, "%6d :: %16s\n", __LINE__, __func__ ) ; \
                        fflush( stderr ) ; \
                    }

#  define SJGF(...) if( debugme_get_state() == DEBUGME_ON ) \
                    { \
                        SJG_TIDF() ; \
                        errorf( __VA_ARGS__ ) ; \
                    }
#endif/* DEBUGME */

/****************************************************
 */


#endif /* __DEBUGME_H */


/****************************************************
 */

