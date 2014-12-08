
/*
 * wrap some low level file handling routines to allow
 * LD_PRELOAD to be used to redirect the input to gcc
 * for some files.
 *
 * $Id: wrap_open.c,v 1.75 2014/12/07 08:02:23 sjg Exp $
 *
 * (c) Stephen Geary, Dec 2014
 */

#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Must define __USE_GNU to get RTLD_NEXT
 * which is rather silly but we're stuck with it
 */
#define __USE_GNU

#include <fcntl.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <sys/types.h>

#include <inttypes.h>

#include <limits.h>

#include <sys/syscall.h>
#include <sys/types.h>

#include "utils.h"

/**********************************************************************
 */

#define DEBUGME

#include "debugme.h"

// #define COUNT_STATS

// #define FULL_CLOSE




/**********************************************************************
 */


typedef int ( *open_fn_t )( const char *pathname, int flags, ... ) ;

typedef int ( *close_fn_t )( int fd ) ;


/**********************************************************************
 */


#if defined( COUNT_STATS ) && defined( DEBUGME )
    static __thread unsigned int hashtab_hits = 0 ;
    
#   define INC_HASHTAB_HITS()       hashtab_hits++
#   define OUTPUT_HASHTAB_HITS()    SJGF( "hashtab_hits = %d", hashtab_hits )
#else
#   define INC_HASHTAB_HITS()
#   define OUTPUT_HASHTAB_HITS()
#endif


/*********************************************************************
 */


/* The command list is a char buffer, but note that it
 * is terminated by a DOUBLE nul char.
 *
 * strings ending with nul chars are embedded in it
 * but the final string is followed by another
 * nul char ( which means there are two nuls ! ).
 */
static __thread char *commandlist = NULL ;


static __thread char maintempfilename[PATH_MAX] ;


static __thread open_fn_t old_open = NULL ;

static __thread close_fn_t old_close = NULL ;


static __thread int supress_redirection = FALSE ;


/* Note that because we're creating a new statement block
 * You must declare anything you plan to use after the restore
 * macro BEFORE the save macro.  Otherwise you might end up
 * returning a variable which was declared out of scope.
 */
#define SAVE_REDIRECTION_STATE  { \
                                    int saved_state = supress_redirection ; \
                                    \
                                    supress_redirection = TRUE ; \
                                    {

/* Note the semi-colon at the start.
 * This prevents labels immediately preceeding the macro
 * from giving an error ( as C demands a statement, even an
 * empty one, after a lable ! ).
 */
#define RESTORE_REDIRECTION_STATE   ; } \
                                    supress_redirection = saved_state ; \
                                }


static __thread uint64_t rndseed = 0 ;


DEF_LISTNODE(   name,
                uint32_t             hash ;
                char                *realpath ;
                int                  realpathlen ;
                char                *tempfilename ;
                int                  fd ;
            )


#define INIT_NAME( np )     if( (np) != NULL ) \
                            { \
                                (np)->next = NULL ; \
                                (np)->hash = 0 ; \
                                (np)->realpath = NULL ; \
                                (np)->realpathlen = 0 ; \
                                (np)->tempfilename = NULL ; \
                            }

/**********************************************************************
 */

DEF_LISTNODE(   memblock,
                char        *baseptr ;
                uint32_t     capacity ;
                uint32_t     used ;
            )

#define MEMBLOCK_SIZE   4096

#define INIT_MEMBLOCK( mb )     if( (mb) != NULL ) \
                                { \
                                    (mb)->baseptr = NULL ; \
                                    (mb)->capacity = MEMBLOCK_SIZE ; \
                                    (mb)->used = 0 ; \
                                }

static __thread memblock_t  *memblockp = NULL ;



/**********************************************************************
 */

static memblock_t *new_memblock()
{
    memblock_t *mb = NULL ;
    
    mb = (memblock_t *)malloc( sizeof(memblock_t) ) ;
    
    if( mb == NULL )
        return NULL ;
        
    INIT_MEMBLOCK( mb ) ;
    
    mb->baseptr = (char *)malloc( MEMBLOCK_SIZE ) ;
    
    if( mb->baseptr == NULL )
    {
        free( mb ) ;
        
        return NULL ;
    }
    
    mb->capacity = MEMBLOCK_SIZE ;
    mb->used = 0 ;
    
    mb->next = memblockp ;
    memblockp = mb ;
    
    return mb ;
}


/**********************************************************************
 */

static char *memblock_alloc( size_t sz )
{
    char *retp = NULL ;
    
    if( sz > MEMBLOCK_SIZE )
        return NULL ;
    
    if( sz < ( memblockp->capacity-memblockp->used ) )
    {
        /* get a new block of memory from malloc
         */
        
        memblock_t *mb = NULL ;
        
        mb = new_memblock() ;
        
        if( mb == NULL )
            return NULL ;
    }
    
    retp = memblockp->baseptr + memblockp->used ;

    memblockp->used += sz ;
    
    return retp ;
}


/**********************************************************************
 */

static void memblock_freeall()
{
    if( memblockp == NULL )
    {
        return ;
    }
    
    memblock_t *mb = NULL ;
    memblock_t *curr = NULL ;

    LIST_WALK(  memblockp,
                curr,
                mb,
                
                if( curr->baseptr != NULL )
                {
                    free( curr->baseptr ) ;
                }
                
                free( curr ) ;
             )
}

/**********************************************************************
 */

#define HTABLOG     8

#define HTABSIZE    ( 1 << HTABLOG )

#define HTABMOD     ( HTABSIZE -1 )


struct htab_s ;

struct htab_s {
    name_t          *namep ;
    } ;

typedef struct htab_s htab_t ;
    

static __thread htab_t hashtab[HTABSIZE] ;


/**********************************************************************
 */


static uint32_t djb_hash( char *p )
{
    uint32_t h = 0 ;
    
    while( *p != 0 )
    {
        h = 33 * h ^ (uint32_t)(*p) ;
        p++ ;
    };
    
    return h;
}


/**********************************************************************
 */

static void seed_random_number()
{
    SAVE_REDIRECTION_STATE
    
    /* seed the random number generator
     *
     * first try using /dev/urandom
     *
     * if that fails for any reason we fall back
     * on a combination of thread id and time.
     */
    
    int retv = sizeof( rndseed ) ; ;
    
    FILE *fp = NULL ;
    
    fp = fopen( "/dev/urandom", "r" ) ;
    
    if( fp != NULL )
    {
        retv = fread( &rndseed, sizeof( rndseed ), 1, fp ) ;
        
        fclose( fp ) ;
    }
    
    if( ( fp == NULL ) || ( retv != sizeof( rndseed ) ) )
    {
        /* fallback on time and tid as seed
         *
         * The size of these is not guaranteed
         * but we can reasonably assume 32-bits will
         * be available for both.
         */
        
        time_t t ;
        
        time( &t ) ;
        
        rndseed = ( (uint64_t)t ) << 32 ;
        
        /* Next group of 4 bytes from a thread id
         */
        
        pid_t tid ;
        
        tid = syscall( SYS_gettid ) ;
        
        rndseed = rndseed | ( ( (uint64_t)tid  ) & 0x0ffff ) ;
    }
    
    RESTORE_REDIRECTION_STATE
}

/**********************************************************************
 */
static uint64_t get_random_number()
{
    SAVE_REDIRECTION_STATE ;
    
    /* This is a 64-bit hash algorithm that reportedly has
     * a VERY long period and whould suffice for random numbers.
     */

	rndseed ^= rndseed >> 12 ; // a
	rndseed ^= rndseed << 25 ; // b
	rndseed ^= rndseed >> 27 ; // c
	
	rndseed =  rndseed * UINT64_C(2685821657736338717) ;

    /* Note :
     *
     * In the relevent paper on the subject :
     *
     * MATHEMATICS OF COMPUTATION
     * Volume 68, Number 225, January 1999, Pages 249–260
     * S 0025-5718(99)00996-5
     * 
     * "TABLES OF LINEAR CONGRUENTIAL GENERATORS
     * OF DIFFERENT SIZES AND GOOD LATTICE STRUCTURE"
     * 
     * PIERRE L’ECUYER"
     * 
     * Several possible values for the multiplier which
     * result in good behavior are listed. These are :
     *
     *  1181783497276652981
     *  4292484099903637661
     *  7664345821815920749
     *  1865811235122147685
     *  2685821657736338717
     *  1803442709493370165
     */
    
    RESTORE_REDIRECTION_STATE ;
    
    return rndseed ;
}

/**********************************************************************
 */

static void fill_random_name( char *newname )
{
    /* generate a 64-bit random integer
     * and take 12 x 32-bit chunks of that to
     * build up a printable character string
     * made up of the ASCII chars a-z and 1-6
     * with zero being excluded as it is too
     * easily confused with the capital letter O
     *
     * 4 bits of the random number will be unused.
     */
    
    int i = 0 ;

    uint64_t r = 0 ;
    
    unsigned char b = 0 ;
    
    r = get_random_number() ;
    
    while( i < 12 )
    {
        b = (unsigned char)( r & 31 ) ;
        
        if( b < 6 )
        {
            newname[i] = '1' + (char)b ;
        }
        else
        {
            newname[i] = 'a' + (char)( b - 6 ) ;
        }
        
        r = r >> 5 ;
        
        i++ ;
    };
    
    newname[12] = 0 ;
}

/**********************************************************************
 */

static char *make_temp_file( char *source )
{
    char *newname = NULL ;
    
    SAVE_REDIRECTION_STATE
    
    SJGF( "making temp file from %s", source ) ;

    int retv = 0 ;

    /* default return is to NULL
     * which flags any error to the caller.
     */
    
    /* can we even open the source for reading ?
     */
    
    if( access( source, R_OK ) != 0 )
    {
        SJGF( "Could not read %s", source ) ;
        
        goto err_exit ;
    }
    
    /* get a temp name
     */
    
    char *p = NULL ;
    
    newname = memblock_alloc( 13 + 11 ) ;
    
    if( newname == NULL )
    {
        SJG() ;
    
        goto err_exit ;
    }
    
    strcpy( newname, "/tmp/wrapo-" ) ;
    
    int i = 0 ;
    
    fill_random_name( newname + 11 ) ;
    
    while( ( i < 16 ) && ( access( newname, F_OK ) == 0 ) )
    {
        fill_random_name( newname+11 ) ;
        
        i++ ;
    }

#ifdef DEBUGME    
    if( i > 0 )
    {
        SJGF( "%d calls to fill_random_name()", i ) ;
    }
#endif
    
    if( i == 16 )
    {
        /* we consider trying to make a tempfile name
         * 16 times a fail !
         */
        
        SJG() ;
        
        newname[0] = 0 ;
        
        goto err_exit ;
    }
    
    SJGF( "Offered new temp name : %s", newname ) ;
    
    /* Have a new name so now invoke "Cap"
     */
    
    char cmd[3*PATH_MAX] ;
    
    /* LD_PRELOAD needs to be saved and cleared before
     * calling system() or system will start it's own
     * copy of this library, which would become a race.
     */
    
    char preload[PATH_MAX] ;
    
    preload[0] = 0 ;
    
    p = getenv( "LD_PRELOAD" ) ;
    
    strncpy( preload, p, PATH_MAX ) ;
    
    unsetenv( "LD_PRELOAD" ) ;
    
    /* Now pass through all the commands in the command list
     */
    
    p = commandlist ;
    
    char *src = source ;
    char *dest = newname ;
    
    do
    {
        snprintf( cmd, 3*PATH_MAX, "%s -o %s %s\n", p, dest, src ) ;
        
        SJGF( "CMD = %s", cmd ) ;
        
        retv = system( cmd ) ;
        
        if( retv == -1 )
        {
            SJGF( "CMD FAILED :: %s", cmd ) ;
            
            newname[0] = 0 ;
            
            break ;
        }
        
        /* read PAST the current command
         */
        
        while( *p != 0 )
        {
            p++ ;
        };
        
        p++ ;
        
        if( *p != 0 )
        {
            /* switch src and dest file names around
             */
            
            src = dest ;
            
            if( dest == newname )
            {
                dest = maintempfilename ;
            }
            else
            {
                dest = newname ;
            }
        }
        
    } while( *p != 0 ) ;
    
    /* Now may need to rename dest file
     * unless an error already occured
     */
    
    if( ( retv != -1 ) && ( dest == maintempfilename ) )
    {
        /* rename() should overwrite any existing file !
         * unless it's in use, which should not be the case.
         */
    
        retv = rename( maintempfilename, newname ) ;
    }
    
    if( retv == -1 )
    {
        /* signal an error
         */
        
        newname[0] = 0 ;
    }
    
    /* finally restore the LD_PRELOAD var
     */
    
    setenv( "LD_PRELOAD", preload, 1 ) ;
    
    SJGF( "Created %s ( for %s )", newname, source ) ;
    
err_exit:
    
    RESTORE_REDIRECTION_STATE
    
    return newname ;
}

/**********************************************************************
 */


void __attribute__ ((constructor)) my_init()
{
    supress_redirection = TRUE ;
    
    SJGF( "In my_init()" ) ;
    
    INIT_DEBUGME() ;
    
    char *p = NULL ;
    
    if( old_open == NULL )
    {
        old_open = ( open_fn_t )dlsym( RTLD_NEXT, "open" ) ;
    }

    if( old_close == NULL )
    {
        old_close = ( close_fn_t )dlsym( RTLD_NEXT, "close" ) ;
    }
    
    if( ( old_open == NULL ) || ( old_close == NULL ) )
    {
        return ;
    }
    
    seed_random_number() ;
    
    /* back to business
     */
    
    supress_redirection = FALSE ;
    
    int i = 0 ;
    
    for( i = 0 ; i < HTABSIZE ; i++ )
    {
        hashtab[i].namep = NULL ;
    }
    
    /* we "grab" one temp filename for use in
     * executing commands ( rather than having
     * mutiple temp filenames for every single
     * command sequence
     */
     
    maintempfilename[0] = 0 ;
    
    /* get a temp name
     */
    
    strcpy( maintempfilename, "/tmp/wrapo-" ) ;
    
    fill_random_name( maintempfilename + 11 ) ;
    
    i = 0 ;
    
    while( ( i < 16 ) && ( access( maintempfilename, F_OK ) == 0 ) )
    {
        fill_random_name( maintempfilename+11 ) ;
        
        i++ ;
    }
    
    if( i == 16 )
    {
        /* we consider trying to make a tempfile name
         * 16 times a fail !
         */
        
        SJG() ;
        
        supress_redirection = TRUE ;
        
        return ;
    }
    
    /* We need to check for a command list in the
     * environment variable WRAP_OPEN_COMMAND
     *
     * Command are comma seperated.
     *
     * This will be used to build command lines
     * with this form :
     *
     *   <command> -o <tempfile> <inputfile>
     *
     * <tempfile> is supplied by the shared library
     * and <inputfile> is the file supplied by the
     * OS to the open() command.
     */
    
    commandlist == NULL ;
    
    p = getenv( "WRAP_OPEN_COMMAND" ) ;
    
    if( p != NULL )
    {
        int len = 0 ;
        
        len = strlen( p ) ;
        
        if( len < 1 )
        {
            /* cannot copy the commands so we're stopped
             */
        
            supress_redirection = TRUE ;
        }
        else
        {
            /* need an extra char for the extra nul
             */
            commandlist = (char *)malloc( len + 2 ) ;
            
            if( commandlist == NULL )
            {
                /* cannot copy the commands so we're stopped
                 */
            
                supress_redirection = TRUE ;
            }
            else
            {
                /* replace commas with nul chars
                 * effectively splitting the list
                 */
            
                i = 0 ;
                
                while( i < len+1 )
                {
                    if( p[i] == ',' )
                    {
                        commandlist[i] = 0 ;
                    }
                    else
                    {
                        commandlist[i] = p[i] ;
                    }
                
                    i++ ;
                };
                
                /* add the final nul
                 */
                
                commandlist[len+1] = 0 ;
            }
        }
    }
    else
    {
        /* As there is no command we should simply turn
         * off redirection as it serves no purpose.
         */
        
        supress_redirection = TRUE ;
    }
    
    memblockp = new_memblock() ;
    
    if( memblockp == NULL )
    {
        supress_redirection = TRUE ;
    }
}

/**********************************************************************
 */

void __attribute__ ((destructor)) my_fini()
{
    SJGF( "In my_fini()" ) ;
    
    supress_redirection = TRUE ;
    
    name_t *np = NULL ;
    name_t *curr = NULL ;

    int retv = 0 ;
    
    int i = 0 ;
    
    for( i = 0 ; i < HTABSIZE ; i++ )
    {
        LIST_WALK(  hashtab[i].namep,
                    curr,
                    np,

                    /* delete the temp file
                     */
                    
                    if( curr->tempfilename[0] != 0 )
                    {
                        retv = remove( curr->tempfilename ) ;
                        
                        SJGF( "remove( %s ) = %s", curr->tempfilename ,curr->realpath ) ;
                    }
                    
                    /* malloc'd by the realpath() function
                     */
                    free( curr->realpath ) ;
                    
                    free( curr ) ;
                 )
    }
    
    remove( maintempfilename ) ;
    
    free( commandlist ) ;
    
    memblock_freeall() ;
    
    OUTPUT_HASHTAB_HITS() ;
}

/**********************************************************************
 */

/* These macros just make the code easier to manage
 */

#define cmp2(a,b)       ( ( fn[len-3] == (a) ) && ( fn[len-2] == (b) ) )

#define cmp3(a,b,c)     ( ( fn[len-3] == (a) ) && ( fn[len-2] == (b) ) && ( fn[len-1] == (c) ) )


static int is_source_file( const char *fn )
{
    int retb = FALSE ;
    
    int len = 0 ;
    
    len = strlen( fn ) ;
    
    if( len < 3 )
        return FALSE ;
    
    if(    ( fn[len-2] == '.' )
        && (
                ( fn[len-1] == 'c' )
            ||  ( fn[len-1] == 'h' )
            ||  ( fn[len-1] == 'm' )
            ||  ( fn[len-1] == 'M' )
            ||  ( fn[len-1] == 'C' )
            ||  ( fn[len-1] == 'H' )
           )
       )
    {
        /* One of the standard single character extensions
         * for C-like source files
         *
         *  .c
         *  .h
         *  .m
         *  .M
         *  .C
         *  .H
         */
        
        return TRUE ;
    }
    
    if( len < 4 )
        return FALSE ;
    
    if(    ( fn[len-3] == '.' )
        && (
                cmp2( 'm', 'm' )
             || cmp2( 'c', 'c' )
             || cmp2( 'h', 'h' )
             || cmp2( 'h', 'p' )
           )
       )
    {
        /* One of the standard two character extensions
         * for C-like source files
         *
         *  .mm
         *  ,cc
         *  .hh
         *  .hp
         */
        
        return TRUE ;
    }
    
    if( len < 5 )
        return FALSE ;
    
    if(    ( fn[len-4] == '.' )
        && (
                cmp3( 'c', 'x', 'x' )
             || cmp3( 'c', 'p', 'p' )
             || cmp3( 'C', 'P', 'P' )
             || cmp3( 'c', '+', '+' )
             || cmp3( 'h', 'x', 'x' )
             || cmp3( 'h', 'p', 'p' )
             || cmp3( 'H', 'P', 'P' )
             || cmp3( 'h', '+', '+' )
             || cmp3( 't', 'c', 'c' )
             || cmp3( 'i', 'n', 'c' )
           )
       )
    {
        /* One of the standard three character extensions
         * for C-like source files
         *
         *  .cxx
         *  .cpp
         *  .CPP
         *  .c++
         *  .hxx
         *  .hpp
         *  .HPP
         *  .h++
         *  .tcc
         *  .inc
         *
         * Note : ".inc" is used by some programmers for files which
         * are included as pseudo-templates.
         */
        
        return TRUE ;
    }
    
    // SJGF( "%s NOT recognised as source file", fn ) ;
    
    return retb ;
}
    
/**********************************************************************
 */

int open( const char *pathname, int flags, ... )
{
    int has_mode = FALSE ;
    mode_t mode = 0 ;
    
    char *p = NULL ;
    
    const char *pathtoopen = pathname ;
    
    name_t *np = NULL ;
    
    int fd = -1 ;
    
    int len = 0 ;

    SJGF( "open( %s )", pathname ) ;
    
    if( old_open == NULL )
    {
        /* we're screwed ...
         */
        
        return -1 ;
    }

    if( flags & O_CREAT )
    {
        va_list ap ;
        
        va_start( ap, flags ) ;
        
        mode = va_arg( ap, mode_t ) ;
        
        has_mode = TRUE ;
        
        va_end( ap ) ;
        
        // SJG() ;
    }
    
    if( ( flags & O_WRONLY ) || ( flags & O_RDWR ) )
    {
        /* we don't interfere with operations that write to a file
         */
        
        goto invoke_original_open ;
    }
    
    if( supress_redirection )
    {
        SJG() ;
        
        goto invoke_original_open ;
    }
    
    /* Any source file in /usr is also not worth
     * pursuing
     */
    
    if( strncmp( pathname, "/usr/", 5 ) == 0 )
    {
        SJG() ;
        
        goto invoke_original_open ;
    }
    
    // SJG() ;
    
    supress_redirection = TRUE ;
    
    /* What type of file is it ?
     */
    
    if( ! is_source_file( pathname ) )
    {
        // SJG() ;
    
        goto stop_supression ;
    }
    
    /* process with cap
     */
    
    // SJGF( "%s", pathname ) ;
    
    struct name_s *ns = NULL ;
    
    ns = (struct name_s *)malloc( sizeof( struct name_s ) ) ;
    
    if( ns == NULL )
        goto stop_supression ;
    
    INIT_NAME( ns ) ;
    
    ns->realpath = realpath( pathname, NULL ) ;
    
    if( ns->realpath == NULL )
    {
        free( ns ) ;
    
        goto stop_supression ;
    }
    
    len = strlen( ns->realpath ) ;
    
    uint32_t h ;
    
    h = djb_hash( ns->realpath ) ;
    
    SJGF( "hash = %x :: len = %d", h, len ) ;
    
    ns->hash = h ;
    ns->realpathlen = len ;
    
    /* try and find the filename in the hash table
     */
    
    int k = ( h & HTABMOD ) ;
    
    name_t *np2 = NULL ;
    
    LIST_WALK(  hashtab[k].namep,
                np,
                np2,
                
                if( ( np->hash == h ) && ( np->realpathlen == len ) )
                {
                    if( memcmp( ns->realpath, np->realpath, len ) == 0 )
                    {
                        // SJG() ;
                        
                        INC_HASHTAB_HITS() ;
                    
                        break ;
                    }
                }
             )

    // SJG() ;
    
    /* need to add entry to list and create a new temp file
     * which is the processed version of the source file
     */
    
    if( np == NULL )
    {
        SJG() ;
        
        /* allocates using memblock_alloc()
         */
        ns->tempfilename = make_temp_file( ns->realpath ) ;
        
        if( ( ns->tempfilename == NULL ) || ( (ns->tempfilename)[0] == 0 ) )
        {
            /* did not work so release that memory
             */
            
            SJG() ;
            
            free( ns ) ;
            
            goto stop_supression ;
        }
    }
    else
    {
        // SJG() ;
        
        /* duplicate the tempfilename
         */
        
        int tnlen = strlen( np->tempfilename ) ;
        
        ns->tempfilename = memblock_alloc( tnlen+1 ) ;
        
        if( ns->tempfilename != NULL )
        {
            strcpy( ns->tempfilename, np->tempfilename ) ;
        }
        else
        {
            free( ns ) ;
        
            goto stop_supression ;
        }
    }
    
    /* Add new node to hashtable
     */
    
    ns->next = hashtab[k].namep ;
    
    hashtab[k].namep = ns ;
    
    np = ns ;
    
    pathtoopen = np->tempfilename ;
    
    /* Now make our wrapping calls and create temp files
     * to deal with the request for that file.
     */
    
    /* Finally unflag our supression of the redirect
     */

stop_supression:

    supress_redirection = FALSE ;
    
    // SJG() ;
    
invoke_original_open:
    
    /* Pass on the request
     */

    if( has_mode )
    {
        // SJG() ;
    
        fd = old_open( pathtoopen, flags, mode ) ;
    }
    else
    {
        // SJG() ;
    
        fd = old_open( pathtoopen, flags ) ;
    }
    
    SJGF( "Opened %s as %d", pathtoopen, fd ) ;
    
    if( np != NULL )
    {
        np->fd = fd ;
    }
    
    return fd ;
}

/**********************************************************************
 */

int close( int fd )
{
    int retv = 0 ;
    
    SAVE_REDIRECTION_STATE
    
    SJGF( "close( %d )", fd ) ;

    fsync( fd ) ;
    
#ifdef FULL_CLOSE
    int i = 0 ;
    
    name_t *np = NULL ;
    name_t *np2 = NULL ;
    
    for( i = 0 ; i < HTABSIZE ; i++ )
    {
        LIST_WALK( hashtab[i].namep,
                    np,
                    np2,
    
                    if( np->fd == fd )
                    {
                        break ;
                    }
                 )
        
        if( np != NULL )
        {
            break ;
        }
    }
    
    if( np != NULL )
    {
        np->fd = -1 ;
        
        SJGF( "Closing %s", np->realpath ) ;
    }
#endif
    
    retv = old_close( fd ) ;
    
    RESTORE_REDIRECTION_STATE
    
    return retv ;
}


