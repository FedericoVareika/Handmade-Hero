#!/bin/sh

common_flags_internal="-DHANDMADE_SLOW=1 -DHANDMADE_INTERNAL=1 -ffile-prefix-map=old=new -g -W -Og"
common_flags_external="-DHANDMADE_SLOW=0 -DHANDMADE_INTERNAL=0 -ffile-prefix-map=old=new -g -W -O3"

linker_flags="$(sdl2-config --cflags --libs) -lm -ldl"

mkdir -p build

gcc -std=gnu11 \
    $common_flags_internal \
    -shared \
    -o build/handmade.so \
    -fPIC $(realpath code/handmade.c) \
    $linker_flags 

gcc -std=gnu11 \
    $common_flags_internal \
    $(realpath code/linux_handmade.c) -o build/handmade_hero \
    $linker_flags -Wl,-rpath,$(realpath build)
 
echo $common_flags_internal | tr ' ' '\n' > compile_flags.txt

