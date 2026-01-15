#!/bin/sh

common_flags_internal="-DHANDMADE_SLOW=1 -DHANDMADE_INTERNAL=1 -ffile-prefix-map=old=new -g -W -Og"
common_flags_external="-DHANDMADE_SLOW=0 -DHANDMADE_INTERNAL=0 -ffile-prefix-map=old=new -g -W"

linker_flags="$(sdl2-config --cflags --libs) -lm"

mkdir -p build
gcc -std=c99 \
    $common_flags_internal \
    $(realpath code/linux_handmade.c) -o build/handmade_hero \
    $linker_flags
 
