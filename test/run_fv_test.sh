#!/bin/zsh
# Host test of source/fastvideo.c against the original conversion.
# fv_tables.h holds the lookup tables of source/sdl_main.c (copied).
cd ${0:A:h} && cc -O2 -Wall -I../source -o /tmp/fvtest fastvideo_test.c ../source/fastvideo.c && /tmp/fvtest
