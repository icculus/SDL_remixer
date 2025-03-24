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

static bool WAV_init(void)
static bool WAV_prepare_audio(const void *data, size_t datalen, SDL_AudioSpec *spec, SDL_PropertiesID props)
static bool WAV_init_track(const void *data, size_t datalen, const SDL_AudioSpec *spec, SDL_PropertiesID metadata_props, void **userdata)
static int  WAV_decode(void *userdata, void *buffer, size_t buflen)
static bool WAV_seek(void *userdata, Uint64 frame)
static void WAV_quit_track(void *userdata)
static void WAV_quit(void)

Mix_Decoder Mix_Decoder_WAV = {
    "WAV",
    WAV_init,
    WAV_init_track,
    WAV_decode,
    WAV_seek,
    WAV_quit_track,
    WAV_quit
};

