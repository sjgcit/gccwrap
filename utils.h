
/*
 * Include file utils.h
 *
 * $Id: utils.h,v 1.3 2014/12/08 04:10:04 sjg Exp $
 *
 * (c) Stephen Geary, Dec 2014
 *
 * Utility Macros
 */


#ifndef __UTILS_H
#  define __UTILS_H


/**********************************************************************
 */

#if ( ! defined(TRUE) ) || ( ! defined(FALSE) )
#  undef TRUE
#  undef FALSE
#  define TRUE 1
#  define FALSE 0
#endif

/*********************************************************************
 */

/* A way to define a list structure that's compatible with even ancient
 * compilers.
 */
#define DEF_LISTNODE( _name, _stuff )    \
                        \
                        struct _name ## _s ; \
                        \
                        struct _name ## _s { \
                                struct _name ## _s  *next ; \
                                _stuff \
                            } ; \
                        \
                        typedef struct _name ## _s  _name ## _t ;
                        

/* This macro helps you walk through every element of a list
 *
 * You need to pass it two variables which are used for the
 * pointer to the current node and the next value that
 * the current node points to.
 *
 * This macro is all in the same scope level as the invoking code.
 */
#define LIST_WALK( _root, _v1, _v2, _code )  \
                        \
                        (_v1) = (_root) ; \
                        \
                        while( (_v1) != NULL ) \
                        { \
                            (_v2) = (_v1)->next ; \
                            \
                            _code \
                            \
                            (_v1) = (_v2) ; \
                        } ;

/* This version of List traversal uses a GCC feature called typeof()
 * to decalre variables inside a new scope bracket pair.
 * 
 * Even if you can use typeof() it's not always desireable to
 * have the current variable's scope limited, as you may need
 * to break out of the look ( in _code ) and then you'll exit
 * the list walk scope and curr will cease to be valid.
 */
#define LIST_WALK2( _root, _code )  \
                        { \
                            typeof((_root)) _curr = (_root) ; \
                            typeof((_root)) _next = NULL ; \
                            \
                            while( _curr != NULL ) \
                            { \
                                _next = _curr->next ; \
                                \
                                _code \
                                \
                                _curr = _next ; \
                            } ; \
                        }

/* This version requires only one variable for the current
 * node pointer.
 *
 * A temp variable that is void * holds the next value
 * and a "fake" pointer lets us copy directly to it.
 * This should introduce no overhead in a modern C compiler.
 *
 * Unlike LIST_WALK2() this version lets you define the
 * current pointer outside the scope of the macro.
 */
#define LIST_WALK3( _root, _curr, _code )  \
                        { \
                            void *_next = NULL ; \
                            void **_addrcurr = (void **)(&_curr) ; \
                            \
                            while( (_curr) != NULL ) \
                            { \
                                _next = (void *)( (_curr)->next ) ; \
                                \
                                _code \
                                \
                                *_addrcurr = _next ; \
                            } ; \
                        }


#endif /* __UTILS_H */
