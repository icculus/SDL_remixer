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

static bool SDLCALL WAV_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    Uint8 *buffer = NULL;
    Uint32 buflen = 0;

    // in theory, a compressed .wav (ADPCM, etc) could be better to stream per-track, but these are probably rare in general,
    //  and this way centralizes _all_ the work in SDL, where it's more likely to receive fixes and improvements.
    if (!SDL_LoadWAV_IO(io, false, spec, &buffer, &buflen)) {
        return false;
    }

    *audio_userdata = MIX_RAW_InitFromMemoryBuffer(buffer, (size_t) buflen, spec, duration_frames, true);
    if (!*audio_userdata) {
        SDL_free(buffer);
        return false;
    }

    // let's take a moment to try and pull WAV-specific metadata out, if we can. If this fails, we don't care.
    if (SDL_SeekIO(io, 12, SDL_IO_SEEK_SET) == 12) {   // skip RIFF and WAVE headers. We should be at the first chunk now.
        const Uint32 ID3_ = 0x20334449;  // "ID3 "
        const Uint32 id3_ = 0x20336469;  // "id3 "

        Uint32 chunk = 0;
        while (SDL_ReadU32LE(io, &chunk)) {
            Uint32 chunklen = 0;
            if (!SDL_ReadU32LE(io, &chunklen)) {
                break;
            }

            const Uint32 nextchunkpos = SDL_TellIO(io) + chunklen;

            if ((chunk == ID3_) || (chunk == id3_)) {
                MIX_IoClamp clamp;
                SDL_IOStream *clamped_io = MIX_OpenIoClamp(&clamp, io);
                if (clamped_io) {
                    clamp.length = chunklen;
                    MIX_ReadMetadataTags(clamped_io, props, &clamp);
                    SDL_CloseIO(clamped_io);
                }
                break;
            }

            if (SDL_SeekIO(io, nextchunkpos, SDL_IO_SEEK_SET) < 0) {
                break;
            }
        }
    }

    return true;
}

MIX_Decoder MIX_Decoder_WAV = {
    "WAV",
    NULL,  // init
    WAV_init_audio,
    MIX_RAW_init_track,
    MIX_RAW_decode,
    MIX_RAW_seek,
    MIX_RAW_quit_track,
    MIX_RAW_quit_audio,
    NULL  // quit
};

