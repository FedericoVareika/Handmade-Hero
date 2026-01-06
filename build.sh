#!/bin/bash

mkdir -p build
gcc code/linux_handmade.c -o build/handmade_hero -g  $(sdl2-config --cflags --libs) -lm

