#!/bin/sh

gcc -O0 -Wall -ggdb3 -I../include -o testmixer SDL_mixer.c decoder_wav.c decoder_raw.c decoder_aiff.c testmixer.c `pkg-config sdl3 --cflags --libs`

