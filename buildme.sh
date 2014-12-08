#!/bin/bash

export PATH=.:$PATH

rm silly

rm *.o

rm gcc-silly

rm clang-silly

export DEBUGME_STATE=1

gccwrap cap -o gcc-silly silly.c my.c 2>log-gcc.txt

clangwrap cap -o clang-silly silly.c my.c 2>log-clang.txt
