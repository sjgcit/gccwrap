
/*
 * debugme.c
 *
 * Routines to give advanced support for debugging.
 *
 * $Id: debugme.c,v 1.3 2014/12/07 06:19:41 sjg Exp $
 *
 * (c) Stephen Geary, Dec 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>


#define DEBUGME

#include "debugme.h"



/*************************************
 */

static int debugme_state = DEBUGME_OFF ;

/*************************************
 */

int debugme_get_state()
{
    return debugme_state ;
}

/*************************************
 */

void debugme_turnon()
{
    debugme_state = DEBUGME_ON ;
}

/*************************************
 */

void debugme_turnoff()
{
    debugme_state = DEBUGME_OFF ;
}

/*************************************
 */

void debugme_init()
{
    char *p = NULL ;
    
    /* Check what value, if any, the environment
     * variable DEBUGME_STATE is in.
     *
     * '1' turns on debugging reports.
     */
    
    p = getenv( "DEBUGME_STATE" ) ;
    
    if( p == NULL )
    {
        debugme_state = DEBUGME_OFF ;
        
        return ;
    }
    
    if( strcmp( p, "0" ) == 0 )
    {
        debugme_state = DEBUGME_ON ;
        
        return ;
    }
    
    if( strcmp( p, "1" ) == 0 )
    {
        debugme_state = DEBUGME_ON ;
        
        return ;
    }
    
    /* The default setting is OFF.
     *
     * The logic is that having a debug capable version of
     * an application does NOT equate to wanting debug
     * reports all the time.  So it should be turned only
     * when needed e.g. to help generate info for a
     * bug report.
     */
    
    debugme_state = DEBUGME_OFF ;
}

/*************************************
 */


