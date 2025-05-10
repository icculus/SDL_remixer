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

#ifdef DECODER_VOC

#include "SDL_mixer_internal.h"

// Private data for VOC file
typedef struct vocstate {
    Uint32  rest;           // bytes remaining in current block
    Uint32  rate;           // rate code (byte) of this chunk
    bool    silent;         // sound or silence?
    Uint32  srate;          // rate code (byte) of silence
    Uint32  blockseek;      // start of current output block
    Uint32  samples;        // number of samples output
    Uint32  size;           // word length of data
    Uint8   channels;       // number of sound channels
    bool    has_extended;   // Has an extended block been read?
} vs_t;

// Size field
// SJB: note that the 1st 3 are sometimes used as sizeof(type)
#define ST_SIZE_BYTE    1
#define ST_SIZE_8BIT    1
#define ST_SIZE_WORD    2
#define ST_SIZE_16BIT   2
#define ST_SIZE_DWORD   4
#define ST_SIZE_32BIT   4
#define ST_SIZE_FLOAT   5
#define ST_SIZE_DOUBLE  6
#define ST_SIZE_IEEE    7   // IEEE 80-bit floats.

// Style field
#define ST_ENCODING_UNSIGNED    1 // unsigned linear: Sound Blaster
#define ST_ENCODING_SIGN2       2 // signed linear 2's comp: Mac
#define ST_ENCODING_ULAW        3 // U-law signed logs: US telephony, SPARC
#define ST_ENCODING_ALAW        4 // A-law signed logs: non-US telephony
#define ST_ENCODING_ADPCM       5 // Compressed PCM
#define ST_ENCODING_IMA_ADPCM   6 // Compressed PCM
#define ST_ENCODING_GSM         7 // GSM 6.10 33-byte frame lossy compression

#define VOC_TERM        0
#define VOC_DATA        1
#define VOC_CONT        2
#define VOC_SILENCE     3
#define VOC_MARKER      4
#define VOC_TEXT        5
#define VOC_LOOP        6
#define VOC_LOOPEND     7
#define VOC_EXTENDED    8
#define VOC_DATA_16     9

#define VOC_BAD_RATE  ~((Uint32)0)

static bool voc_check_header(SDL_IOStream *src)
{
    // VOC magic header
    Uint8  signature[20];  // "Creative Voice File\032"
    Uint16 datablockofs;

    SDL_SeekIO(src, 0, SDL_IO_SEEK_SET);

    if (SDL_ReadIO(src, signature, sizeof(signature)) != sizeof(signature)) {
        return false;
    }

    if (SDL_memcmp(signature, "Creative Voice File\032", sizeof(signature)) != 0) {
        return SDL_SetError("Unrecognized file type (not VOC)");
    }

    // get the offset where the first datablock is located
    if (SDL_ReadIO(src, &datablockofs, sizeof(Uint16)) != sizeof(Uint16)) {
        return false;
    }

    datablockofs = SDL_Swap16LE(datablockofs);

    if (SDL_SeekIO(src, datablockofs, SDL_IO_SEEK_SET) != datablockofs) {
        return false;
    }

    return true;  // success!
}

// Read next block header, save info, leave position at start of data
static bool voc_get_block(SDL_IOStream *src, vs_t *v, SDL_AudioSpec *spec)
{
    Uint8 bits24[3];
    Uint8 uc, block;
    Uint32 sblen;
    Uint16 new_rate_short;
    Uint32 new_rate_long;
    Uint8 trash[6];
    Uint16 period;
    unsigned int i;

    v->silent = 0;
    while (v->rest == 0) {
        if (SDL_ReadIO(src, &block, sizeof(block)) != sizeof(block)) {
            return true;  // assume that's the end of the file.
        }

        if (block == VOC_TERM) {
            return true;
        }

        if (SDL_ReadIO(src, bits24, sizeof(bits24)) != sizeof(bits24)) {
            return true;  // assume that's the end of the file.
        }

        // Size is an 24-bit value. Ugh.
        sblen = (Uint32)((bits24[0]) | (bits24[1] << 8) | (bits24[2] << 16));

        switch(block) {
            case VOC_DATA:
                if (SDL_ReadIO(src, &uc, sizeof(uc)) != sizeof(uc)) {
                    return false;
                }

                // When DATA block preceeded by an EXTENDED
                // block, the DATA blocks rate value is invalid
                if (!v->has_extended) {
                    if (uc == 0) {
                        return SDL_SetError("VOC Sample rate is zero?");
                    }

                    if ((v->rate != VOC_BAD_RATE) && (uc != v->rate)) {
                        return SDL_SetError("VOC sample rate codes differ");
                    }

                    v->rate = uc;
                    spec->freq = (Uint16)(1000000.0/(256 - v->rate));
                    v->channels = 1;
                }

                if (SDL_ReadIO(src, &uc, sizeof(uc)) != sizeof(uc)) {
                    return false;
                }

                if (uc != 0) {
                    return SDL_SetError("VOC decoder only interprets 8-bit data");
                }

                v->has_extended = false;
                v->rest = sblen - 2;
                v->size = ST_SIZE_BYTE;
                return true;

            case VOC_DATA_16:
                if (SDL_ReadIO(src, &new_rate_long, sizeof(new_rate_long)) != sizeof(new_rate_long)) {
                    return false;
                }
                new_rate_long = SDL_Swap32LE(new_rate_long);
                if (new_rate_long == 0) {
                    return SDL_SetError("VOC Sample rate is zero?");
                }
                if ((v->rate != VOC_BAD_RATE) && (new_rate_long != v->rate)) {
                    return SDL_SetError("VOC sample rate codes differ");
                }
                v->rate = new_rate_long;
                spec->freq = (int)new_rate_long;

                if (SDL_ReadIO(src, &uc, sizeof(uc)) != sizeof(uc)) {
                    return false;
                }

                switch (uc) {
                    case 8:  v->size = ST_SIZE_BYTE; break;
                    case 16: v->size = ST_SIZE_WORD; break;
                    default:
                        return SDL_SetError("VOC with unknown data size");
                }

                if (SDL_ReadIO(src, &v->channels, sizeof(Uint8)) != sizeof(Uint8)) {
                    return false;
                }

                if (SDL_ReadIO(src, trash, 6) != 6) {
                    return false;
                }

                v->rest = sblen - 12;
                return true;

            case VOC_CONT:
                v->rest = sblen;
                return true;

            case VOC_SILENCE:
                if (SDL_ReadIO(src, &period, sizeof(period)) != sizeof(period)) {
                    return false;
                }
                period = SDL_Swap16LE(period);

                if (SDL_ReadIO(src, &uc, sizeof(uc)) != sizeof(uc)) {
                    return false;
                }
                if (uc == 0) {
                    return SDL_SetError("VOC silence sample rate is zero");
                }

                // Some silence-packed files have gratuitously
                // different sample rate codes in silence.
                // Adjust period.
                if ((v->rate != VOC_BAD_RATE) && (uc != v->rate))
                    period = (Uint16)((period * (256 - uc))/(256 - v->rate));
                else
                    v->rate = uc;
                v->rest = period;
                v->silent = 1;
                return true;

            case VOC_LOOP:  // !!! FIXME: is this meant to repeat a prior data/silence block?
            case VOC_LOOPEND:
                for (i = 0; i < sblen; i++) {  // skip repeat loops.
                    if (SDL_ReadIO(src, trash, sizeof(Uint8)) != sizeof(Uint8)) {
                        return false;
                    }
                }
                break;

            case VOC_EXTENDED:
                // An Extended block is followed by a data block
                // Set this byte so we know to use the rate
                // value from the extended block and not the
                // data block.
                v->has_extended = true;
                if (SDL_ReadIO(src, &new_rate_short, sizeof(new_rate_short)) != sizeof(new_rate_short)) {
                    return false;
                }
                new_rate_short = SDL_Swap16LE(new_rate_short);
                if (new_rate_short == 0) {
                   return SDL_SetError("VOC sample rate is zero");
                }
                if ((v->rate != VOC_BAD_RATE) && (new_rate_short != v->rate)) {
                   return SDL_SetError("VOC sample rate codes differ");
                }
                v->rate = new_rate_short;

                if (SDL_ReadIO(src, &uc, sizeof(uc)) != sizeof(uc)) {
                    return false;
                }

                if (uc != 0) {
                    return SDL_SetError("VOC decoder only interprets 8-bit data");
                }

                if (SDL_ReadIO(src, &uc, sizeof(uc)) != sizeof(uc)) {
                    return false;
                }

                if (uc) { // Stereo
                    spec->channels = 2;
                // VOC_EXTENDED may be read before spec->channels inited:
                } else {
                    spec->channels = 1;
                }

                // Needed number of channels before finishing compute for rate
                spec->freq = (256000000L / (65536L - v->rate)) / spec->channels;
                // An extended block must be followed by a data
                // block to be valid so loop back to top so it
                // can be grabbed.
                continue;

            case VOC_MARKER:
                if (SDL_ReadIO(src, trash, 2) != 2) {
                    return false;
                }
                SDL_FALLTHROUGH;

            // !!! FIXME: should we publish text blocks as metadata?
            default:  // text block or other unsupported/unimportant stuff.
                for (i = 0; i < sblen; i++) {
                    if (SDL_ReadIO(src, trash, sizeof(Uint8)) != sizeof(Uint8)) {
                        return false;
                    }
                }

                if (block == VOC_TEXT) {
                    continue;    // get next block
                }
        }
    }

    return true;
}

static Uint32 voc_read(SDL_IOStream *src, vs_t *v, Uint8 *buf, SDL_AudioSpec *spec)
{
    Sint64 done = 0;

    if (v->rest == 0) {
        if (!voc_get_block(src, v, spec) || (v->rest == 0)) {
            return 0;
        }
    }

    if (v->silent) {
        // Fill in silence
        SDL_memset(buf, (v->size == ST_SIZE_WORD) ? 0x00 : 0x80, v->rest);
        done = v->rest;
        v->rest = 0;
    } else {
        done = SDL_ReadIO(src, buf, v->rest);
        if (done <= 0) {
            return 0;
        }

        v->rest = (Uint32)(v->rest - done);
        if (v->size == ST_SIZE_WORD) {
            done >>= 1;
        }
    }

    return (Uint32)done;
}

static bool SDLCALL VOC_init_audio(SDL_IOStream *src, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    if (!voc_check_header(src)) {
        return false;
    }

    SDL_zerop(spec);

    vs_t v;
    SDL_zero(v);
    v.rate = VOC_BAD_RATE;

    if (!voc_get_block(src, &v, spec)) {
        return false;
    } else if (v.rate == VOC_BAD_RATE) {
        return SDL_SetError("VOC data had no sound!");
    } else if (v.size == 0) {
        return SDL_SetError("VOC data had invalid word size!");
    }

    spec->format = ((v.size == ST_SIZE_WORD) ? SDL_AUDIO_S16LE : SDL_AUDIO_U8);
    if (spec->channels == 0) {
        spec->channels = v.channels;
    }

    size_t buflen = (size_t) v.rest;
    Uint8 *buffer = (buflen == 0) ? NULL : (Uint8 *) SDL_malloc(buflen);
    if (!buffer) {
        return false;
    }

    Uint8 *fillptr = buffer;
    while (voc_read(src, &v, fillptr, spec)) {
        if (!voc_get_block(src, &v, spec)) {
            SDL_free(buffer);
            return false;
        }

        buflen += v.rest;

        Uint8 *ptr = (Uint8 *) SDL_realloc(buffer, buflen);
        if (!ptr) {
            SDL_free(buffer);
            return false;
        }

        buffer = ptr;
        fillptr = ptr + (buflen - v.rest);
    }

    *audio_userdata = MIX_RAW_InitFromMemoryBuffer(buffer, buflen, spec, duration_frames, true);
    if (!*audio_userdata) {
        SDL_free(buffer);
        return false;
    }

    return true;
}

// !!! FIXME: this is the VOC decoder from SDL_mixer2, updated for SDL_mixer3. It was a "chunk" decoder, which means
// !!! FIXME:  it predecodes all audio upfront. VOC files were small and uncomplex (and rare in modern times), so this
// !!! FIXME:  isn't a big deal, but SDL_sound's VOC decoder can decode VOCs on the fly, so it might be worth stealing
// !!! FIXME:  that implementation later.
MIX_Decoder MIX_Decoder_VOC = {
    "VOC",
    NULL, // init
    VOC_init_audio,
    MIX_RAW_init_track,
    MIX_RAW_decode,
    MIX_RAW_seek,
    MIX_RAW_quit_track,
    MIX_RAW_quit_audio,
    NULL  // quit
};

#endif

