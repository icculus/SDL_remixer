#!/bin/sh

gcc -O0 -Wall -ggdb3 -I../include -DVORBIS_DYNAMIC='"libvorbisfile.so.3"' -DOPUS_DYNAMIC='"libopusfile.so.0"' -o testmixer SDL_mixer.c SDL_mixer_metadata_tags.c decoder_wav.c decoder_raw.c decoder_aiff.c decoder_voc.c decoder_sinewave.c decoder_drmp3.c decoder_vorbis.c decoder_opus.c testmixer.c `pkg-config sdl3 --cflags --libs` `pkg-config opus --cflags`

