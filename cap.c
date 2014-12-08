
/*
 * C Auxilary Preprocessor
 *
 * $Id: cap.c,v 1.60 2014/12/08 05:02:33 sjg Exp $
 *
 * (c) Stephen Geary, Jan 2011
 *
 * A preprocessor to enable template like extensions to the
 * C pro-processor language.
 *
 * It shoud be run before the standard C preprocessor and is
 * intended to have it's output processed by the standard C
 * preprocessor.  It does not replace the C proprocessor.
 *
 * The intention of this system to to enable extensions of
 * the the basic proprocessor using the hash (#) prefix to
 * trigger the extension.
 *
 */

#include <stdio.h>
#include <ctype.h>
#include <malloc.h>
#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>

#include <sys/stat.h>
#include <sys/fcntl.h>

#include <stdlib.h>

#include <sched.h>

#include <errno.h>


static char *cap_version = "$Revision: 1.60 $" ;


#define DEBUGVER



#ifdef DEBUGVER
   volatile static int debugme = 0 ;
#  define DBGLINEBASE() fprintf(stderr," Line %d " , __LINE__ )
#  define DBGLINE() if( debugme != 0 ) { DBGLINEBASE() ; fputc( EOLOUT, stderr ) ; }
#  define debugf(...)   { if( debugme != 0 ){  DBGLINEBASE() ; fprintf( stderr, __VA_ARGS__ ) ; } }
#  define debug_on()    { debugme = 1 ; }
#  define debug_off()   { debugme = 0 ; }
#else
#  define debugf(...)
#  define debug_on()
#  define debug_off()
#endif /* DEBUGVER */


#ifndef boolean
  typedef unsigned int boolean ;
#endif

#ifndef TRUE
#  define TRUE  1
#endif

#ifndef FALSE
#  define FALSE 0
#endif


#define toggle(b)   if( (b) == FALSE ){ (b) = TRUE ; }else{ (b) = FALSE ; }



static FILE *fin = NULL ;
static FILE *fout = NULL ;


#define FCLOSE(fs) \
                    if( (fs) != NULL ) \
                    { \
                        fclose( fs ) ; \
                    }

#define FPUT(c)     fputc( (int)(c), fout )

#define FPUTS(b)    fputs( (b), fout )


static boolean skip_is_on = FALSE ;

static boolean changes_made = FALSE ;

static char macrochar = '#' ;


#define BUFFLEN 1024

static char prebuff[BUFFLEN+1] ;
static char buff[BUFFLEN+1] ;
static char postbuff[BUFFLEN+1] ;

static int lastchar = -1 ;

#define OUTPUTBUFFS()   \
            { \
                if( prebuff[0] != '\0' ) \
                    fputs( prebuff, fout ) ; \
                if( buff[0] != '\0' ) \
                    fputs( buff, fout ) ; \
                if( postbuff[0] != '\0' ) \
                    fputs( postbuff, fout ) ; \
                if( lastchar != -1 ) \
                    fputc( lastchar, fout ) ; \
            }


#define OUTPUTBUFFS_NOLASTCHAR()    \
            { \
                if( prebuff[0] != '\0' ) \
                    fputs( prebuff, fout ) ; \
                if( buff[0] != '\0' ) \
                    fputs( buff, fout ) ; \
                if( postbuff[0] != '\0' ) \
                    fputs( postbuff, fout ) ; \
            }

#define OUTPUTBUFFS_GEN( p1, p2, p3, lc )   \
            { \
                if( (p1) != '\0' ) \
                    fputs( (p1), fout ) ; \
                if( (p2) != '\0' ) \
                    fputs( (p2), fout ) ; \
                if( (p3) != '\0' ) \
                    fputs( (p3), fout ) ; \
                if( (lc) != -1 ) \
                    fputc( (lc), fout ) ; \
            }



#define iswhitespace(c)     ( ( (c) == ' ' ) || ( (c) == '\t' ) )



static int iskeyword( char *str )
{
    int retv = 0 ;
    
    debugf( "iskeyword [%s]\n", str ) ;
    
    retv = ( buff[0] == macrochar ) ;
    
    if( retv )
    {
        /* need to avoid white spaces in comparisons
         * as we'd like spacing to be legal
         *
         * We assume that "keyword" is space trimmed
         * which it should be.
         */
        int i = 1 ;
        int j = 0 ;
        
        while( iswhitespace( buff[i] ) )
            i++ ;
        
        while( buff[i] == str[j] )
        {
            if( str[j] == 0 )
                break ;
            
            i++ ;
            j++ ;
        };
        
        if( buff[i] == str[j] )
        {
            retv = 1 ;
        }
        else
        {
            retv = 0 ;
        }
    }
    
    return retv ;
}


struct wordstack_s {
    struct wordstack_s  *next ;
    char            *buff ;
    } ;

typedef struct wordstack_s  wordstack_t ;


/* wordstackp is a stack for storing copies of
 * previously read symbols.
 *
 * It is used in e.g. the "#def" directive.
 */
static wordstack_t *wordstackp = NULL ;



/* copy the buffer str to the indicated buffer
 */
void copybuff( char *dest )
{
    int i = 0 ;

    do
    {
        dest[i] = buff[i] ;
        i++ ;
    }
    while( ( i < BUFFLEN ) && ( buff[i] != '\0' ) ) ;
}



/* read a symbol from the input stream returning it's
 * delimiter and the buffer containing the word
 * terminated by a '\0'
 *
 * a symbol is anything like a variable or function name
 * it can start with and contain a digits or underscores
 *
 * So a -1 return means EOF
 *
 * and anything else is a valid termination character
 *
 * Note the need to defer the toggling of the inside_quotes
 * state until the next readsymbol operation starts.  This
 * is required or a stacked symbol could be read at the end
 * of a quotated section and incorrectly matched if we cleared
 * the inside_quotes flag early.  By using the delay
 * mechanism to toggle we can make the logic work simply for
 * calling code.
 */
static int inside_quotes = FALSE ;
static int quote_pending = FALSE ;

static int escape_pending = FALSE ;

/* the following is used to allow us to backtrack the last
 * character we read
 *
 * if pendingchar is -1 then there is no character saved for
 * reading
 */
static int pendingchar = -1 ;


void pendchar( int c )
{
    pendingchar = c ;
}


int nextchar()
{
    int retv = -1 ;

    if( pendingchar != -1 )
    {
        retv = pendingchar ;
        pendingchar = -1 ;
    }
    else
    {
        retv = fgetc( fin ) ;
    }

    return retv ;
}


/* readchar(c) reads input characters until it finds a
 * match to the one requested.
 *
 * it ignores ONLY isspace() characters
 *
 * a return of -1 is an error
 * a return matching the requested char is valid
 */
int readchar( int cwanted )
{
    int retv = -1 ;
    int c = 0 ;

    c = nextchar() ;

    while( ( c != -1 ) && !feof(fin) )
    {
        if( !iswhitespace(c) )
        {
            if( c == cwanted )
            {
                retv = c ;
            }

            break ;
        }

        c = nextchar() ;
    };

    return retv ;
}


#define issymbolchar(c)     ( ( (c) == '_' ) || isalnum((c)) )

/* reads the next symbol
 *
 * the default behavior is to ignore spaces and output
 * them to fout.
 *
 * symbols only contain alpha-numerics and underscore
 *
 * trailing and lead whitespace is stored in the
 * prebuff and postbuff buffers.
 */
int readsymbol()
{
    int retv = 0 ;

    int i = 0 ;
    int j = 0 ;
    int k = 0 ;
    int c = 0 ;

    prebuff[0]  = '\0' ;
    postbuff[0] = '\0' ;
    buff[0]     = '\0' ;

    if( quote_pending )
    {
        toggle(inside_quotes) ;
        toggle(quote_pending) ;
    }

    c = nextchar() ;

    while( ( i < BUFFLEN ) && ( j < BUFFLEN ) )
    {
        if( inside_quotes )
        {
            /* read chars until the buffer is full or we
             * find an non-escaped matching quote to end
             *
             * inside quotes we need to check for escaped
             * sequences.
             */

            if( escape_pending )
            {
                toggle(escape_pending) ;

                buff[i] = (char)c ;
                i++ ;
            }
            else
            {
                if( c == '"' )
                {
                    toggle(quote_pending) ;

                    break ;
                }

                if( c == '\\' )
                {
                    toggle(escape_pending) ;
                }

                buff[i] = (char)c ;
                i++ ;
            }

            /* go back to start of loop
             */

            c = nextchar() ;

            continue ;
        }


        /* note that this only happens if we are not inside_quotes
         */

        if( ( i == 0 ) && iswhitespace(c) )
        {
            prebuff[j] = (char)c ;
            j++ ;

            c = nextchar() ;

            continue ;
        }

        if( issymbolchar(c) )
        {
            buff[i] = (char)c ;
            i++ ;
        }
        else
        {
            if( (char)c == '"' )
            {
                toggle(quote_pending) ;
            }

            break ;
        }

        c = nextchar() ;
    };

    buff[i] = '\0' ;
    prebuff[j] = '\0' ;

    while( ( k < BUFFLEN ) && iswhitespace(c) )
    {
        postbuff[k] = (char)c ;
        k++ ;

        c = nextchar() ;
    };

    /* the last char read could be a valid char from the next symbol
     * so we have to check and allow it to be stored for the next character
     * reading.
     */

    if( issymbolchar(c) )
    {
        pendchar(c) ;
        c = (int)' ' ;
    }

    postbuff[k] = '\0' ;

    retv = c ;

    lastchar = c ;

    if( c != (int)'\n' )
    {
        debugf( "readsymbol() ::     buff = [ %s ][ %s ][ %s ][ %c ]\n", prebuff, buff, postbuff, c ) ;
    }
    else
    {
        debugf( "readsymbol() ::     buff = [ %s ][ %s ][ %s ][ \\n ]\n", prebuff, buff, postbuff ) ;
    }

    return retv ;
}


/* read everything up to the EOL into the buffer
 */
int read_to_eol()
{
    int retv = 0 ;
    int c = 0 ;
    int i = 0 ;

    while( ( i < BUFFLEN ) && ( c != -1 ) )
    {
        c = nextchar() ;

        if( c == (int)'\n' )
            break ;

        if( c != -1 )
        {
            buff[i] = (char)c ;

            i++ ;
        }
    };

    buff[i] = 0 ;

    return retv ;
}

/* this function pushes a copy of a buffer onto the stack
 *
 * basically it's a simply list for later checking
 *
 * the data structure supports these lists
 *
 * if checklen is nonzero then only copy the buffer if length
 * is greater than zero
 */

void stackcopybuffer( char *buffer, int checklen )
{
    wordstack_t *node = NULL ;
    int len = 0 ;

    len = strlen(buffer) ;

    if( ( checklen != 0 ) && ( len == 0 ) )
        return ;

    len++ ;

    node = (wordstack_t *)malloc( sizeof(wordstack_t) ) ;

    if( node == NULL )
        return ;

    node->buff = NULL ;
    node->next = wordstackp ;
    wordstackp = node ;

    node->buff = (char *)malloc( len ) ;

    if( node->buff == NULL )
        return ;

    memcpy( node->buff, buffer, len ) ;
}


#define stackcopy() stackcopybuffer(buff,1)

#define stackcopyall()  \
            { \
                stackcopybuffer(prebuff,0) ; \
                stackcopybuffer(buff,0) ; \
                stackcopybuffer(postbuff,0) ; \
            }


/* get a pointer to the buffer stored in the node
 * at the given index.
 *
 * 0 is stop of stack
 *
 * return NULL on error
 */
char *stackbuffat( int index )
{
    char *retp = NULL ;
    wordstack_t *curr = wordstackp ;
    wordstack_t *next = NULL ;
    int i = 0 ;

    if( index < 0 )
        return NULL ;

    if( wordstackp == NULL )
        return NULL ;

    while( ( curr != NULL ) && ( i != index ) )
    {
        i++ ;

        curr = curr->next ;
    };

    if( ( curr != NULL ) && ( i == index ) )
    {
        retp = curr->buff ;

        /* debugf( "stackbuffat( %d ) = [ %s ]\n", index, retp ) ;
         */
    }

    return retp ;
}


/* pop the tos, freeing all memory for that node
 */
void stackpop()
{
    wordstack_t *next = NULL ;

    if( wordstackp == NULL )
        return ;

    next = wordstackp->next ;

    free( wordstackp->buff ) ;
    free( wordstackp ) ;

    wordstackp = next ;
}


/* release all memory used by the stack
 * and reset the stack pointer
 */
void stackfree()
{
    wordstack_t *curr = wordstackp ;
    wordstack_t *next = NULL ;

    while( curr != NULL )
    {
        free( curr->buff ) ;
        next = curr->next ;
        free( curr ) ;
        curr = next ;
    };

    wordstackp = NULL ;
}


/* check if the stack contains the currently buffered symbol
 *
 * this function return true (1) if it is and false (0) if not
 */
int symbolonstack()
{
    int retv = FALSE ;
    wordstack_t *curr = wordstackp ;

    while( curr != NULL )
    {
        if( curr->buff != NULL )
        {
            if( strcmp( buff, curr->buff ) == 0 )
            {
                return TRUE ;
            }
        }

        curr = curr->next ;
    };

    return retv ;
}


int process_macrochar()
{
    int retv = 0 ;
    
    macrochar = nextchar() ;
    
    return retv ;
}


int process_quote()
{
    int retv = 0 ;
    int c = 0 ;

    /* take all input from now until either EOF or
     * '#' at the start of a line and treat it as being part
     * of one define
     *
     * basically pads ' \' unto the end of all lines except the
     * last non-empty one.
     */
    
    c = nextchar() ;

    while( ( c != -1 ) && !feof(fin) )
    {
        if( c == (int)'\n' )
        {
            /* read the next char and see if it's a macrochar ( normally a hash )
             *
             * if it is we know this is the last line of the
             * quoted section and we don't output the ' \'
             */

            /* output pending empty lines
             */
            
            c = nextchar() ;

            if( c == (int)macrochar )
            {
                c = nextchar() ;

                if( c == (int)'\n' )
                {
                    FPUT( '\n' ) ;
                    FPUT( '\n' ) ;

                    return 0 ;
                }
                else
                {
                    /* not a single # followed by newline
                     */

                    FPUT( macrochar ) ;

                    continue ;
                }

            }
            else
            {
                FPUT( ' ' ) ;
                FPUT( '\\' ) ;
                FPUT( '\n' ) ;
                
                continue ;
            }
        }
        else
        {
            /* not an EOL
             */
            
            FPUT( c ) ;
        }
            
        c = nextchar() ;
    };

    return retv ;
}


/* treat everything until the next line starting with '#' as a
 * comment.
 *
 * wraps the comment in a common comment style.
 */
int process_comment()
{
    int retv = 0 ;
    int c = 0 ;

    fprintf( fout, "\n/*\n * " ) ;

    c = nextchar() ;

    while( ( c != -1 ) && !feof(fin) )
    {
        if( c == (int)macrochar )
        {
            c = nextchar() ;

            if( c == '\n' )
                break ;

            FPUT( macrochar ) ;

            continue ;
        }

        if( c == '\n' )
        {
            fprintf( fout, "\n *" ) ;

            /* if we don't check for the hash symbol coming next we
             * will add a space we don't want which sounds trivial
             * but will cause the comment never to be terminated
             * as the * and / will be separated by a space !
             */

            c = nextchar() ;
            pendchar(c) ;

            if( c != (int)macrochar )
                FPUT( ' ' ) ;
        }
        else
        {
            FPUT( c ) ;
        }

        c = nextchar() ;
    };

    fprintf( fout, "/\n" ) ;

    return retv ;
}


static int ends_in_continuation()
{
    int len = 0 ;
    
    len = strlen( buff ) ;
    
    if( len < 1 )
        /* No continuation mark possible
         */
        return 0 ;
    
    if( buff[len-1] == '\\' )
        /* a continuation mark
         */
        return 1 ;
    
    return 0 ;
}


/* process a redefine
 *
 * The C proeprocessor requires that you first undefine
 * a macro before redefining, but has no direct support
 * for doing that automatically.
 */
int process_redefine()
{
    int retv = 0 ;
    int i = 0 ;
    int c = 0 ;
    int newc = 0 ;

    /* first we need to read the definition part
     * which should be of the form <macroname>([<parametername>{,<parametername>}])
     *
     */

    c = readsymbol() ;

    fprintf( fout, "#undef %s%s%s\n", prebuff, buff, postbuff ) ;
    fprintf( fout, "#define %s%s%s", prebuff, buff, postbuff ) ;
    
    /* Now read to first EOL with no continuation before the new line
     */
    
    i = read_to_eol() ;
    
    while( ends_in_continuation() )
    {
        fputs( buff, fout ) ;
        fputc( (int)'\n', fout ) ;
    
        i = read_to_eol() ;
    };
    
    fputs( buff, fout ) ;
    fputc( (int)'\n', fout ) ;
    
    return retv ;
}


/* process a macro definition
 *
 * unlike quote creates the macro definition and substitutes all
 * the symbols used as paramater markers with '(<paramname>)'
 * which is the safe macro expansion version.
 *
 * Note that no attempt is made to parse the code so ANY token
 * matching the sequence will be converted.
 */
int process_def()
{
    int retv = 0 ;
    int i = 0 ;
    int c = 0 ;
    int newc = 0 ;

    /* first we need to read the definition part
     * which should be of the form <macroname>([<parametername>{,<parametername>}])
     *
     */

    c = readsymbol() ;

    if( c != (int)'(' )
        /* this is a syntax error
         */
        return -1 ;

    fprintf( fout, "#define %s%s%s(", prebuff, buff, postbuff ) ;

    i = 0 ;

    c = readsymbol() ;

    while( c == (int)',' )
    {
        OUTPUTBUFFS() ;

        /* need to keep a copy of buff
         */

        stackcopy() ;

        c = readsymbol() ;
    };

    OUTPUTBUFFS() ;

    stackcopy() ;

    /* definition has been read and output
     *
     * now output the text replacing the paramameter values until
     * a macrochar ( normally a hash ) is read or EOF and adding
     * the required ' \' EOL sequences 
     */

    c = readsymbol() ;

    while( c != -1 )
    {
        if( !inside_quotes )
        {
            if( c == (int)macrochar )
            {
                c = nextchar() ;

                if( c == '\n' )
                {
                    FPUT( '\n' ) ;
                    break ;
                }

                pendchar(c) ;

                c = (int)macrochar ;
            }

            if( symbolonstack() )
            {
                fputs( prebuff, fout ) ;
                FPUT( '(' ) ;
                fputs( buff, fout ) ;
                FPUT( ')' ) ;
                fputs( postbuff, fout ) ;
            }
            else
            {
                fputs( prebuff, fout ) ;
                fputs( buff, fout ) ;
                fputs( postbuff, fout ) ;
            }

            newc = readsymbol() ;

            if( ( c == (int)'\n' ) && ( newc != (int)macrochar ) )
            {
                FPUT( ' ' ) ;
                FPUT( '\\' ) ;
                FPUT( '\n' ) ;
            }
            else
            {
                FPUT( c ) ;
            }

            c = newc ;

            continue ;
        }

        /* the following only happens if we are inside quotes
         */

        OUTPUTBUFFS_NOLASTCHAR() ;

        if( c == (int)'\n' )
        {
            FPUT( ' ' ) ;
            FPUT( '\\' ) ;
        }

        FPUT( c ) ;

        c = readsymbol() ;
    };

    /* tidy up
     */

    stackfree() ;

    return retv ;
}


/* output a set of constants with the given pre- and post- identifiers
 * on the given list of name.
 *
 * type 0 flags are from 0 incremented by 1 ( a sequence ) output as
 *        sequences of the last constant+1.
 *
 * type 1 flags are power of two staring from 1
 *
 * type 2 flags are from 0 incremented by 1, but output as exlicit values.
 *
 * type 3 flags are from 0 decremented by 1, as explicit values
 *
 * all the values are made relative to the base one so it is easy
 * to change later
 */
int process_constants( int type )
{
    int retv = 0 ;
    int i = 0 ;
    int c = 0 ;
    char *pre = NULL ;
    char *post = NULL ;
    char *base = NULL ;


    c = readsymbol() ;
    stackcopy() ;
    pre = wordstackp->buff ;

    c = readsymbol() ;
    stackcopy() ;
    post = wordstackp->buff ;

    c = readsymbol() ;
    stackcopy() ;
    base = wordstackp->buff ;

    if( ( type == 0 ) || ( type == 2 ) )
    {
        fprintf( fout, "#define %s_%s_%s\t\t0\n", pre, base, post ) ;

        i = 1 ;
    }

    if( type == 1 )
    {
        fprintf( fout, "#define %s_%s_%s\t\t0x01\n", pre, base, post ) ;

        i = 2 ;
    }

    if( type == 3 )
    {
        fprintf( fout, "#define %s_%s_%s\t\t0\n", pre, base, post ) ;

        i = -1 ;
    }

    while( ( c != -1 ) && ( (char)c != macrochar ) )
    {
        c = readsymbol() ;

        if( strlen(buff) > 0 )
        {
            if( type == 0 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t%s_%s_%s + %d\n", pre, buff, post, pre, base, post, i ) ;

                i++ ;

                continue ;
            }

            if( type == 1 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t0x0%X\n", pre, buff, post, i ) ;

                i *= 2 ;

                continue ;
            }

            if( type == 2 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t%d\n", pre, buff, post, i ) ;

                i++ ;

                continue ;
            }

            if( type == 3 )
            {
                fprintf( fout, "#define %s_%s_%s\t\t%d\n", pre, buff, post, i ) ;

                i-- ;

                continue ;
            }
        }
    };

    return retv ;
}



/* Send a command to the shell to process the following
 * block of text
 *
 * The shell command is everything up to EOL following the
 * #command directive
 *
 * Input to the command is send via a pipe.  Output from
 * the command is recieved via another pipe.
 * The command recieves input on it's stdin and sends
 * output to stdout.
 *
 * The parent will have to wait until the child dies (!)
 * before it can continue, so we have to watch for that.
 */

#define PARENT_READ readpipe[0]
#define CHILD_WRITE readpipe[1]
#define CHILD_READ  writepipe[0]
#define PARENT_WRITE    writepipe[1]

int process_command()
{
    int retv = 0 ;

    int writepipe[2] = { -1, -1 } ;
    int readpipe[2] = { -1, -1 } ;

    pid_t childpid ;

    int c = 0 ;

    FILE *fproc = NULL ;


    /* get the command
     */
    retv = read_to_eol() ;

    if( retv < 0 )
        return retv ;

    /* open the pipes
     */

    retv = pipe( writepipe ) ;
    if( retv < 0 )
    {
        return -1 ;
    }

    retv = pipe( readpipe ) ;
    if( retv < 0 )
    {
        close( writepipe[0] ) ;
        close( writepipe[1] ) ;
        return -1 ;
    }

    /* now fork a child
     */

    childpid = fork() ;

    if( childpid == 0 )
    {
        /* In child
         */

        close( PARENT_WRITE ) ;
        close( PARENT_READ ) ;

        dup2( CHILD_READ, 0 ) ;
        dup2( CHILD_WRITE, 1 ) ;

        close( CHILD_READ ) ;
        close( CHILD_WRITE ) ;

        /* now start a command
         */

        retv = execlp( buff, buff, NULL ) ;

        /* if we got here there was an error and we exit anyway
         */

        exit(-1) ;
    }
    else
    {
        /* In parent
         */

        close( CHILD_READ ) ;
        close( CHILD_WRITE ) ;

        /* send input to child
         */

        fproc = fdopen( PARENT_WRITE, "w" ) ;

        if( fproc == NULL )
            goto write_error ;

        c = nextchar() ;

        while( c != -1 )
        {
            if( c == (int)macrochar )
            {
                c = nextchar() ;

                if( c == (int)'\n' )
                {
                    break ;
                }

                fputc( (int)macrochar, fproc ) ;

                continue ;
            }

            fputc( c , fproc ) ;

            c = nextchar() ;
        };

        /* fputc( (int)macrochar, fproc ) ;
         */

        fclose( fproc ) ;

write_error:

        /* read output from command run by child
         */

        fproc = fdopen( PARENT_READ, "r" ) ;

        c = fgetc( fproc ) ;

        while( ( c != -1 ) && !feof(fproc) )
        {
            FPUT(c) ;

            c = fgetc( fproc ) ;
        };

        fclose( fproc ) ;

        /* wait for child to die
         */

        childpid = wait( &retv ) ;

        /* close pipes !
         */
        close( PARENT_READ ) ;
        close( PARENT_WRITE ) ;
    }
    

    return retv ;
}



/* process checks the keyword we read in and if it finds a valid
 * word it does our extension processing
 *
 * This returns 0 if a keyword was found and processed and
 * -1 if processing failed or no keyword was found.
 */
int process()
{
    int retv = -1 ;

    /* for safety
     */
    buff[BUFFLEN] = '\0' ;

    /* debugf( "buff = [%s]\n", buff ) ;
     */
    
    if( iskeyword("skipoff" ) )
    {
        skip_is_on = FALSE ;

        changes_made = TRUE ;

        return 0 ;
    }
    
    /* NOTE :
     *
     * The following check for skip_is_on must only be made
     * AFTER checking for a skipoff directive.
     *
     * If it's done before that then we never check for skipoff
     * and we could skip forever !
     */

    if( skip_is_on )
    {
        return -1 ;
    }

    if( iskeyword("skipon") )
    {
        skip_is_on = TRUE ;

        changes_made = TRUE ;

        return 0 ;
    }

    if( iskeyword("macrochar") )
    {
        changes_made = TRUE ;
        
        retv = process_macrochar() ;
        
        return retv ;
    }


#ifdef DEBUGVER
    if( iskeyword("testextension") )
    {
        /* a test extension
         */

        fprintf( fout, "\n/* Preprocessor extension found in code\n */\n\n" ) ;

        changes_made = TRUE ;

        return 0 ;
    }
#endif /* DEBUGVER */


    if( iskeyword("debugon") )
    {
        /* turn on debug reporting from caps
         */
        debug_on() ;

        changes_made = TRUE ;

        return 0 ;
    }


    if( iskeyword("debugoff") )
    {
        /* turn off debug reporting from caps
         */
        debug_off() ;

        changes_made = TRUE ;

        return 0 ;
    }


    if( iskeyword("quote") )
    {
        retv = process_quote() ;

        changes_made = TRUE ;

        return retv ;
    }


    if( iskeyword("comment") )
    {
        retv = process_comment() ;

        changes_made = TRUE ;

        return retv ;
    }

    if( iskeyword("def") )
    {
        retv = process_def() ;

        changes_made = TRUE ;

        return retv ;
    }


    if( iskeyword("constants") )
    {
        retv = process_constants(0) ;

        changes_made = TRUE ;

        return retv ;
    }


    if( iskeyword("flags") )
    {
        retv = process_constants(1) ;

        changes_made = TRUE ;

        return retv ;
    }


    if( iskeyword("constants-values") )
    {
        retv = process_constants(2) ;

        changes_made = TRUE ;

        return retv ;
    }


    if( iskeyword("constants-negative") )
    {
        retv = process_constants(3) ;

        changes_made = TRUE ;

        return retv ;
    }


    if( iskeyword("command") )
    {
        retv = process_command() ;

        changes_made = TRUE ;

        return retv ;
    }
    
    if( iskeyword("redefine") )
    {
        retv = process_redefine() ;

        changes_made = TRUE ;

        return retv ;
    }
    
    return retv ;
}



int main_process()
{
    int retv = 0 ;
    int c = 0 ;
    int i = 0 ;
    int j = 0 ;
    
    int leadingspaces = 0 ;
    
    /* blank chars is needed because a blank might be a character
     * other than a space ( e.g. a tab ) and we want to output that
     * character, not just a space.  So we have to record blank chars
     */
    char blankchars[BUFFLEN] ;
            
    if( fin == NULL )
    {
        return 0 ;
    }

    while( ( c != -1 ) && ( !feof(fin) ) )
    {
        c = nextchar() ;
        
        if( c == -1 )
            break ;

        if( c != (int)macrochar )
        {
            /* not a macrochar ( normally hash ) as first char on line
             * then output everything until we
             * we reach EOL or EOF
             */

            FPUT(c) ;

            while( ( c != '\n' ) && ( c != -1 ) && ( !feof(fin) ) )
            {
                c = nextchar() ;

                if( c == -1 )
                    break ;

                FPUT(c) ;
            };

            if( c == -1 )
                break ;
        }
        else
        {
            /* a possible preprocessor directive
             * check if it is one of our extension keywords
             *
             * If it is pass processing to the extension module
             * and if not then output the directive
             */

            /* read characters into a buffer until EOL, EOF or a space
             * check this string againsts the key word lists
             *
             * Note that isspace() also checks for EOL
             *
             * As we permit leading spaces after the hash and before the
             * directive we need to first check for this.
             */

            *buff = macrochar ;
            
            i = 1 ;
            
            c = nextchar() ;
            
            leadingspaces = 0 ;
            
            while( iswhitespace((char)c) )
            {
                blankchars[ leadingspaces++ ] = (char)c ;
                
                c = nextchar() ;
            };

            while( ( i < BUFFLEN ) && ( c != -1 ) && ( !isspace((char)c) ) )
            {
                buff[i] = (char)c ;
                i++ ;

                c = nextchar() ;
            };

            buff[i] = '\0' ;

            if( ( i == BUFFLEN ) || ( c == -1 ) )
            {
                /* ran out of room in buffer or EOF
                 * so we can treat that as not being a keyword
                 */
                 
                FPUT( macrochar ) ;
                
                for( j = 0 ; j < leadingspaces ; j++ )
                {
                    FPUT( blankchars[j] ) ;
                }
                
                leadingspaces = 0 ;

                j = 1 ;

                while( j < i )
                {
                    FPUT( buff[j] ) ;
                    j++ ;
                };

                if( c != -1 )
                {
                    FPUT( c ) ;
                }
            }
            else
            {
                /* a space or EOL terminated the sequence
                 *
                 * in either case we check for a keyword and we output it as given
                 * if no keyword is found.
                 */

                retv = process() ;

                if( retv != 0 )
                {
                    /* ouput the buffer if we did not recognize the word
                     */

                    FPUT( macrochar ) ;
            
                    for( j = 0 ; j < leadingspaces ; j++ )
                    {
                        FPUT( blankchars[j] ) ;
                    }
                
                    leadingspaces = 0 ;

                    j = 1 ;

                    while( j < i )
                    {
                        FPUT( buff[j] ) ;
                        j++ ;
                    };
                }

                if( isspace(c) )
                {
                    /* if not EOF then we still have a character we read ahead
                     * that must be output
                     */
                    FPUT( c ) ;
                }
            }
        }
    };
    
    return 0 ;
}


static void version()
{
    char ver[128] = "$Revision: 1.60 $" ;
    
    /* Skip the RCS string preceeding the version number
     */
    
    int i = 11 ;
    
    while( isdigit( ver[i] ) || ( ver[i] == '.' ) )
        i++ ;
    
    ver[i] = 0 ;
    
    printf( "CAP - C Auxilary Preprocessor - version %s\n", ver+11 ) ;
}


static int init_main( int argc, char **argv )
{
    int retv = 0 ;
    int i ;

    fin     = NULL ;
    fout    = stdout ;

    int input_files = 0 ;


    inside_quotes = FALSE ;
    quote_pending = FALSE ;

    escape_pending = FALSE ;

    pendingchar = -1 ;


    i = 1 ;

    while( i < argc )
    {
        if( ( strcmp(argv[i],"-V") == 0 ) || ( strcmp(argv[i],"--version") == 0 ) )
        {
            i++ ;
            
            version() ;
            
            continue ;
        }
        
        if( strcmp(argv[i],"-m") == 0 )
        {
            /* Set the character used to denote a macro
             * default char is a hash ( # ) and this lets
             * you use something else.
             */
            
            i++ ;
            
            if( argc <= i )
            {
                // not enough arguments
                
                return -1 ;
            }
            
            macrochar = *( argv[i] ) ;
            
            i++ ;
            
            continue ;
        }
    
        if( strcmp(argv[i],"-o") == 0 )
        {
            i++ ;

            if( i > argc )
                return -1 ;

            if( fout != stdout )
            {
                FCLOSE( fout ) ;
                fout = NULL ;
            }

            if( strcmp( argv[i], "-" ) == 0 )
            {
                fout = stdout ;
            }
            else
            {
                fout = fopen( argv[i], "w" ) ;

                if( fout == NULL )
                    return -1 ;
            }

            if( fout == NULL )
                return -1 ;
            
            i++ ;
            
            continue ;
        }

        /* This has to be a filename ( or a mistake )
         */

        if( fin != stdin )
        {
            FCLOSE( fin ) ;
            fin = NULL ;
        }

        if( strcmp( argv[i], "-" ) == 0 )
        {
            fin = stdin ;
        }
        else
        {
            fin = fopen( argv[i] , "r" ) ;
        }

        if( fin == NULL )
            return -1 ;

        input_files++ ;

        retv = main_process() ;

        if( retv != 0 )
            return -1 ;

        i++ ;
    };

    if( input_files == 0 )
    {
        retv = main_process() ;
    }

    return retv ;
}

static int deinit_main()
{
    int retv = 0 ;

    /* close file channels
     */

    fflush( fout ) ;

    if( fin != stdin )
    {
        FCLOSE( fin ) ;
    }

    if( fout != stdout )
    {
        FCLOSE( fout ) ;
    }

    return retv ;
}


int main( int argc, char **argv )
{
    int retv = 0 ;

    debug_on() ;
    
    retv = init_main( argc, argv ) ;
    
    debug_off() ;

    if( retv != 0 )
        goto fini_error ;

fini_error:

    deinit_main() ;

    return retv ;
}


