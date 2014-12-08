#!/bin/sh

gcc -O2 -o wrap_open.so -shared -fPIC  wrap_open.c debugme.c -ldl


gcc -O2 -o gccwrap -DTARGET_GCC gccwrap.c debugme.c

gcc -O2 -o clangwrap -DTARGET_CLANG gccwrap.c debugme.c

