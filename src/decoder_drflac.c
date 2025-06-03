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

#ifdef DECODER_FLAC_DRFLAC

#include "SDL_mixer_internal.h"

#define DR_FLAC_IMPLEMENTATION
#if defined(__GNUC__) && (__GNUC__ >= 4) && \
  !(defined(_WIN32) || defined(__EMX__))
#define DRFLAC_API __attribute__((visibility("hidden")))
#elif defined(__APPLE__)
#define DRFLAC_API __private_extern__
#else
#define DRFLAC_API /* just in case.. */
#endif
#define DR_FLAC_NO_STDIO
#define DRFLAC_ASSERT(expression) SDL_assert((expression))
#define DRFLAC_COPY_MEMORY(dst, src, sz) SDL_memcpy((dst), (src), (sz))
#define DRFLAC_MOVE_MEMORY(dst, src, sz) SDL_memmove((dst), (src), (sz))
#define DRFLAC_ZERO_MEMORY(p, sz) SDL_memset((p), 0, (sz))
#define DRFLAC_MALLOC(sz) SDL_malloc((sz))
#define DRFLAC_REALLOC(p, sz) SDL_realloc((p), (sz))
#define DRFLAC_FREE(p) SDL_free((p))
#include "dr_libs/dr_flac.h"


typedef struct DRFLAC_AudioData
{
    size_t framesize;
    bool loop;
    Sint64 loop_start;
    Sint64 loop_end;
    Sint64 loop_len;
} DRFLAC_AudioData;

typedef struct DRFLAC_TrackData
{
    const DRFLAC_AudioData *adata;
    drflac *decoder;
} DRFLAC_TrackData;


static size_t DRFLAC_IoRead(void *context, void *buf, size_t size)
{
    return SDL_ReadIO((SDL_IOStream *) context, buf, size);
}

static drflac_bool32 DRFLAC_IoSeek(void *context, int offset, drflac_seek_origin origin)
{
    // SDL_IOWhence and drflac_seek_origin happen to match up.
    return (SDL_SeekIO((SDL_IOStream *) context, offset, (SDL_IOWhence) origin) < 0) ? DRFLAC_FALSE : DRFLAC_TRUE;
}


typedef struct DRFLAC_Metadata {
    char *vendor;
    char **comments;
    int num_comments;
} DRFLAC_Metadata;

static void FreeMetadata(DRFLAC_Metadata *metadata)
{
    for (int i = 0; i < metadata->num_comments; i++) {
        SDL_free(metadata->comments[i]);
    }
    SDL_free(metadata->comments);
    SDL_free(metadata->vendor);
}


#define MIX_PROP_DRFLAC_METADATA_POINTER "SDL_mixer.decoder_drflac.metadata"

static void DRFLAC_OnMetadata(void *context, drflac_metadata *pMetadata)
{
    if (pMetadata->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
        SDL_IOStream *io = (SDL_IOStream *) context;
        DRFLAC_Metadata *metadata = (DRFLAC_Metadata *) SDL_GetPointerProperty(SDL_GetIOProperties(io), MIX_PROP_DRFLAC_METADATA_POINTER, NULL);
        if (!metadata) {
            return;  // oh well.
        }

        if (!metadata->vendor) {
            metadata->vendor = (char *) SDL_malloc(pMetadata->data.vorbis_comment.vendorLength + 1);
            if (metadata->vendor) {
                SDL_memcpy(metadata->vendor, pMetadata->data.vorbis_comment.vendor, pMetadata->data.vorbis_comment.vendorLength);
                metadata->vendor[pMetadata->data.vorbis_comment.vendorLength] = '\0';
            }
        }

        const char *comment;
        drflac_uint32 commentlen = 0;
        drflac_vorbis_comment_iterator iter;
        drflac_init_vorbis_comment_iterator(&iter, pMetadata->data.vorbis_comment.commentCount, pMetadata->data.vorbis_comment.pComments);

        while ((comment = drflac_next_vorbis_comment(&iter, &commentlen)) != NULL) {
            void *ptr = SDL_realloc(metadata->comments, sizeof (char *) * (metadata->num_comments + 1));
            if (ptr) {
                metadata->comments = (char **) ptr;
                char *str = (char *) SDL_malloc(commentlen + 1);
                if (str) {
                    SDL_memcpy(str, comment, commentlen);
                    str[commentlen] = '\0';
                    metadata->comments[metadata->num_comments++] = str;
                }
            }
        }
    }
}

static bool SDLCALL DRFLAC_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's a FLAC file (if it's an Ogg stream, we'll let libFLAC try to parse it out).
    //bool is_ogg_stream = false;
    char magic[4];
    if (SDL_ReadIO(io, magic, 4) != 4) {
        return false;
    } else if (SDL_memcmp(magic, "OggS", 4) == 0) {
        //is_ogg_stream = true;  // MAYBE flac, might be vorbis, etc.
    } else if (SDL_memcmp(magic, "fLaC", 4) != 0) {
        return SDL_SetError("Not a FLAC audio stream");
    }

    // Go back and do a proper load now to get metadata.
    if (SDL_SeekIO(io, SDL_IO_SEEK_SET, 0) == -1) {
        return false;
    }

    // open upfront to make sure data is usable and pull in metadata.
    DRFLAC_Metadata metadata;
    SDL_zero(metadata);
    SDL_SetPointerProperty(SDL_GetIOProperties(io), MIX_PROP_DRFLAC_METADATA_POINTER, &metadata);  // need to hang this pointer somewhere, just during init_audio.
    drflac *decoder = drflac_open_with_metadata(DRFLAC_IoRead, DRFLAC_IoSeek, DRFLAC_OnMetadata, io, NULL);
    if (!decoder) {
        return false;  // probably not a FLAC file.
    }

    DRFLAC_AudioData *adata = (DRFLAC_AudioData *) SDL_calloc(1, sizeof(*adata));
    if (!adata) {
        drflac_close(decoder);
        SDL_ClearProperty(SDL_GetIOProperties(io), MIX_PROP_DRFLAC_METADATA_POINTER);
        return false;
    }

    MIX_ParseOggComments(props, (int) decoder->sampleRate, metadata.vendor, (const char * const *) metadata.comments, metadata.num_comments, &adata->loop_start, &adata->loop_end, &adata->loop_len);
    FreeMetadata(&metadata);

    spec->format = SDL_AUDIO_F32;
    spec->channels = (int) decoder->channels;
    spec->freq = (int) decoder->sampleRate;

    *duration_frames = (decoder->totalPCMFrameCount == 0) ? MIX_DURATION_UNKNOWN : (Sint64) decoder->totalPCMFrameCount;

    adata->loop = ((adata->loop_end > 0) && (adata->loop_end <= *duration_frames) && (adata->loop_start < adata->loop_end));
    if (adata->loop) {
        *duration_frames = MIX_DURATION_INFINITE;  // if looping, stream is infinite.
    }

    drflac_close(decoder);
    SDL_ClearProperty(SDL_GetIOProperties(io), MIX_PROP_DRFLAC_METADATA_POINTER);

    adata->framesize = SDL_AUDIO_FRAMESIZE(*spec);

    *audio_userdata = adata;

    return true;
}

static bool SDLCALL DRFLAC_init_track(void *audio_userdata, SDL_IOStream *io, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **track_userdata)
{
    const DRFLAC_AudioData *adata = (const DRFLAC_AudioData *) audio_userdata;
    DRFLAC_TrackData *tdata = (DRFLAC_TrackData *) SDL_calloc(1, sizeof (*tdata));
    if (!tdata) {
        return false;
    }

    tdata->decoder = drflac_open(DRFLAC_IoRead, DRFLAC_IoSeek, io, NULL);
    if (!tdata->decoder) {
        SDL_free(tdata);
        return false;
    }

    tdata->adata = adata;
    *track_userdata = tdata;

    return true;
}

static bool SDLCALL DRFLAC_decode(void *track_userdata, SDL_AudioStream *stream)
{
    DRFLAC_TrackData *tdata = (DRFLAC_TrackData *) track_userdata;
    const int framesize = tdata->adata->framesize;
    float samples[256];
    const drflac_uint64 rc = drflac_read_pcm_frames_f32(tdata->decoder, sizeof (samples) / framesize, samples);
    if (!rc) {
        return false;  // done decoding.
    }
    SDL_PutAudioStreamData(stream, samples, rc * framesize);
    return true;
}

static bool SDLCALL DRFLAC_seek(void *track_userdata, Uint64 frame)
{
    DRFLAC_TrackData *tdata = (DRFLAC_TrackData *) track_userdata;
    return !!drflac_seek_to_pcm_frame(tdata->decoder, (drflac_uint64) frame);
}

static void SDLCALL DRFLAC_quit_track(void *track_userdata)
{
    DRFLAC_TrackData *tdata = (DRFLAC_TrackData *) track_userdata;
    drflac_close(tdata->decoder);
    SDL_free(tdata);
}

static void SDLCALL DRFLAC_quit_audio(void *audio_userdata)
{
    DRFLAC_AudioData *adata = (DRFLAC_AudioData *) audio_userdata;
    SDL_free(adata);
}

MIX_Decoder MIX_Decoder_DRFLAC = {
    "DRFLAC",
    NULL,  // init
    DRFLAC_init_audio,
    DRFLAC_init_track,
    DRFLAC_decode,
    DRFLAC_seek,
    DRFLAC_quit_track,
    DRFLAC_quit_audio,
    NULL  // quit
};

#endif

