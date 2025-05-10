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

/*
  This is the source needed to decode an AIFF file into a waveform.
  It's pretty straightforward once you get going. The only
  externally-callable function is MIX_LoadAIFF_IO(), which is meant to
  act as identically to SDL_LoadWAV_IO() as possible.

  This file by TorbjÃÂ¶rn Andersson (torbjorn.andersson@eurotime.se)
  8SVX file support added by Marc Le Douarain (mavati@club-internet.fr)
  in december 2002.
*/

#ifdef DECODER_AIFF

#include "SDL_mixer_internal.h"

// !!! FIXME: there is a "streaming" AIFF decoder in SDL2_mixer, which is tangled up with
// !!! FIXME: the streaming WAV decoder. It has AIFF-C support, which this one doesn't,
// !!! FIXME: so in a perfect world we'd at least want to pull that in. But I'm not sure
// !!! FIXME: there's _any_ demand for AIFF in modern times.

/*********************************************/
/* Define values for AIFF (IFF audio) format */
/*********************************************/
#define FORM        0x4d524f46      /* "FORM" */

#define AIFF        0x46464941      /* "AIFF" */
#define SSND        0x444e5353      /* "SSND" */
#define COMM        0x4d4d4f43      /* "COMM" */

#define _8SVX       0x58565338      /* "8SVX" */
#define VHDR        0x52444856      /* "VHDR" */
#define BODY        0x59444F42      /* "BODY" */

/* This function was taken from libsndfile. I don't pretend to fully
 * understand it.
 */

static Uint32 SANE_to_Uint32 (Uint8 *sanebuf)
{
    /* Is the frequency outside of what we can represent with Uint32? */
    if ((sanebuf[0] & 0x80) || (sanebuf[0] <= 0x3F) || (sanebuf[0] > 0x40)
        || (sanebuf[0] == 0x40 && sanebuf[1] > 0x1C))
        return 0;

    return ((sanebuf[2] << 23) | (sanebuf[3] << 15) | (sanebuf[4] << 7)
        | (sanebuf[5] >> 1)) >> (29 - sanebuf[1]);
}

static bool SDLCALL AIFF_init_audio(SDL_IOStream *src, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    bool found_SSND = false;
    bool found_COMM = false;
    bool found_VHDR = false;
    bool found_BODY = false;
    Sint64 start = 0;

    Uint32 chunk_type = 0;
    Uint32 chunk_length = 0;
    Sint64 next_chunk = 0;

    /* AIFF magic header */
    Uint32 FORMchunk = 0;
    Uint32 AIFFmagic = 0;

    /* SSND chunk */
    Uint32 offset = 0;
    Uint32 blocksize = 0;

    /* COMM format chunk */
    Uint16 channels = 0;
    Uint32 numsamples = 0;
    Uint16 samplesize = 0;
    Uint8 sane_freq[10];
    Uint32 frequency = 0;

    /* VHDR chunk */
    Uint16 frequency16 = 0;

    if (!SDL_ReadU32LE(src, &FORMchunk) ||
        !SDL_ReadU32BE(src, &chunk_length)) {
        return false;
    }
    if (chunk_length == AIFF) { /* The FORMchunk has already been read */
        AIFFmagic    = chunk_length;
        chunk_length = FORMchunk;
        FORMchunk    = FORM;
    } else {
        if (!SDL_ReadU32LE(src, &AIFFmagic)) {
            return false;
        }
    }
    if ((FORMchunk != FORM) || ((AIFFmagic != AIFF) && (AIFFmagic != _8SVX))) {
        return SDL_SetError("Unrecognized file type (not AIFF nor 8SVX)");
    }

    /* TODO: Better sanity-checking. */

    do {
        if (!SDL_ReadU32LE(src, &chunk_type) ||
            !SDL_ReadU32BE(src, &chunk_length)) {
            return false;
        }
        next_chunk  = SDL_TellIO(src) + chunk_length;

        /* Paranoia to avoid infinite loops */
        if (chunk_length == 0) {
            break;
        }

        switch (chunk_type) {
            case SSND:
                found_SSND  = true;
                if (!SDL_ReadU32BE(src, &offset) ||
                    !SDL_ReadU32BE(src, &blocksize)) {
                    return false;
                }
                start = SDL_TellIO(src) + offset;
                (void)blocksize; /* unused. */
                break;

            case COMM:
                found_COMM  = true;
                if (!SDL_ReadU16BE(src, &channels) ||
                    !SDL_ReadU32BE(src, &numsamples) ||
                    !SDL_ReadU16BE(src, &samplesize)) {
                }
                if (SDL_ReadIO(src, sane_freq, sizeof(sane_freq)) != sizeof(sane_freq)) {
                    return SDL_SetError("Bad AIFF sample frequency");
                }
                frequency = SANE_to_Uint32(sane_freq);
                if (frequency == 0) {
                    return SDL_SetError("Bad AIFF sample frequency");
                }
                break;

            case VHDR:
                found_VHDR  = true;
                if (!SDL_ReadU32BE(src, NULL) ||
                    !SDL_ReadU32BE(src, NULL) ||
                    !SDL_ReadU32BE(src, NULL) ||
                    !SDL_ReadU16BE(src, &frequency16)) {
                    return false;
                }
                channels = 1;
                samplesize = 8;
                frequency = frequency16;
                break;

            case BODY:
                found_BODY  = true;
                numsamples  = chunk_length;
                start       = SDL_TellIO(src);
                break;

            default:
                break;
        }
        /* a 0 pad byte can be stored for any odd-length chunk */
        if (chunk_length&1) {
            next_chunk++;
        }
    } while ((((AIFFmagic == AIFF) && (!found_SSND || !found_COMM))
          || ((AIFFmagic == _8SVX) && (!found_VHDR || !found_BODY)))
          && SDL_SeekIO(src, next_chunk, SDL_IO_SEEK_SET) != 1);  // !!! FIXME: why 1?

    if ((AIFFmagic == AIFF) && !found_SSND) {
        return SDL_SetError("Bad AIFF (no SSND chunk)");
    }

    if ((AIFFmagic == AIFF) && !found_COMM) {
        return SDL_SetError("Bad AIFF (no COMM chunk)");
    }

    if ((AIFFmagic == _8SVX) && !found_VHDR) {
        return SDL_SetError("Bad 8SVX (no VHDR chunk)");
    }

    if ((AIFFmagic == _8SVX) && !found_BODY) {
        return SDL_SetError("Bad 8SVX (no BODY chunk)");
    }

    /* Decode the audio data format */
    SDL_zerop(spec);
    spec->freq = frequency;
    switch (samplesize) {
        case 8:
            spec->format = SDL_AUDIO_S8;
            break;
        case 16:
            spec->format = SDL_AUDIO_S16BE;
            break;
        default:
            return SDL_SetError("Unsupported AIFF samplesize");
    }
    spec->channels = (Uint8) channels;

    const size_t buflen = numsamples * SDL_AUDIO_FRAMESIZE(*spec);
    Uint8 *buffer = (Uint8 *) SDL_malloc(buflen);
    if (!buffer) {
        return false;
    }

    SDL_SeekIO(src, start, SDL_IO_SEEK_SET);
    if (SDL_ReadIO(src, buffer, buflen) != buflen) {
        SDL_free(buffer);
        return false;
    }

    *audio_userdata = MIX_RAW_InitFromMemoryBuffer(buffer, buflen, spec, duration_frames, true);
    if (!*audio_userdata) {
        SDL_free(buffer);
        return false;
    }

    return true;
}


// AIFF_init_audio parses metadata and finds the payload, and then it's just raw PCM data.
MIX_Decoder MIX_Decoder_AIFF = {
    "AIFF",
    NULL, // init
    AIFF_init_audio,
    MIX_RAW_init_track,
    MIX_RAW_decode,
    MIX_RAW_seek,
    MIX_RAW_quit_track,
    MIX_RAW_quit_audio,
    NULL  // quit
};

#endif

