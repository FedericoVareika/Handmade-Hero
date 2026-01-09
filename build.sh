#!/bin/bash

mkdir -p build
gcc -DHANDMADE_SLOW=1 -DHANDMADE_INTERNAL=1 \
    $(realpath code/linux_handmade.c) -o build/handmade_hero \
    -ffile-prefix-map=old=new -g  \
    $(sdl2-config --cflags --libs) -lm -Wall

