/*
 * 
 *
 * $Id: my.c,v 1.2 2014/12/03 11:12:54 sjg Exp $
 *
 * (c) 
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#quote
#define wibble( a )
{
    printf( "This is %s\n", #a ) ;
}
#

void myfunc()
{
#comment
This is something that only the "cauxp" preprocessor
will be able to deal with.

It will cause an error if cap does not generate
proper output from this.
#

    wibble(first) ;
    wibble(second) ;
}


