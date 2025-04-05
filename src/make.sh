#!/bin/sh

gcc -O0 -Wall -ggdb3 -I../include -o testmixer SDL_mixer.c SDL_mixer_metadata_tags.c decoder_wav.c decoder_raw.c decoder_aiff.c decoder_voc.c decoder_sinewave.c decoder_drmp3.c testmixer.c `pkg-config sdl3 --cflags --libs`

