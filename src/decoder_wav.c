/*
  SDL_mixer:  An audio mixer library based on the SDL library
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "SDL_mixer_internal.h"

static bool SDLCALL WAV_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, void **audio_userdata)
{
    Uint8 *buffer = NULL;
    Uint32 buflen = 0;

    // this is obviously wrong.
    if (!SDL_LoadWAV_IO(io, false, spec, &buffer, &buflen)) {
        return false;
    }

    *audio_userdata = Mix_RAW_InitFromMemoryBuffer(buffer, (size_t) buflen, spec);
    if (!*audio_userdata) {
        SDL_free(buffer);
        return false;
    }

    return true;
}

Mix_Decoder Mix_Decoder_WAV = {
    "WAV",
    NULL,  // init
    WAV_init_audio,
    Mix_RAW_init_track,
    Mix_RAW_decode,
    Mix_RAW_seek,
    Mix_RAW_quit_track,
    Mix_RAW_quit_audio,
    NULL  // quit
};

