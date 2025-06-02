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

#ifdef DECODER_GME

#include "SDL_mixer_internal.h"

#include <gme/gme.h>

#ifdef GME_DYNAMIC
#define MIX_LOADER_DYNAMIC GME_DYNAMIC
#endif

#define MIX_LOADER_FUNCTIONS \
    MIX_LOADER_FUNCTION(true,gme_err_t,gme_open_data,(void const*, long, Music_Emu**, int)) \
    MIX_LOADER_FUNCTION(true,const char*,gme_identify_header,(void const*)) \
    MIX_LOADER_FUNCTION(true,gme_err_t,gme_start_track,(Music_Emu*, int)) \
    MIX_LOADER_FUNCTION(true,int,gme_track_ended,(Music_Emu const*)) \
    MIX_LOADER_FUNCTION(true,int,gme_voice_count,(Music_Emu const*)) \
    MIX_LOADER_FUNCTION(true,gme_err_t,gme_track_info,(Music_Emu const*, gme_info_t**, int)) \
    MIX_LOADER_FUNCTION(true,void,gme_free_info,(gme_info_t*)) \
    MIX_LOADER_FUNCTION(true,gme_err_t,gme_seek_samples,(Music_Emu*, int)) \
    MIX_LOADER_FUNCTION(true,gme_err_t,gme_play,(Music_Emu*, int count, short out[])) \
    MIX_LOADER_FUNCTION(true,void,gme_delete,(Music_Emu*)) \
    MIX_LOADER_FUNCTION(false,void,gme_set_autoload_playback_limit,(Music_Emu*, int)) \

#define MIX_LOADER_MODULE gme
#include "SDL_mixer_loader.h"


typedef struct GME_AudioData
{
    const Uint8 *data;
    size_t datalen;
} GME_AudioData;

typedef struct GME_TrackData
{
    const GME_AudioData *adata;
    Music_Emu *emu;
} GME_TrackData;


static bool SDLCALL GME_init(void)
{
    return LoadModule_gme();
}

static void SDLCALL GME_quit(void)
{
    UnloadModule_gme();
}

static bool SDLCALL GME_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's a supported file.
    char magic[4];
    if (SDL_ReadIO(io, magic, 4) != 4) {
        return false;
    }

    const char *ext = gme.gme_identify_header(magic);
    if (!ext || !*ext) {
        return SDL_SetError("Not a libgme-supported audio stream");
    }

    // now rewind, load the whole thing to memory, and use that buffer for future processing.
    if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) {
        return false;
    }
    size_t datalen = 0;
    Uint8 *data = (Uint8 *) SDL_LoadFile_IO(io, &datalen, false);
    if (!data) {
        return false;
    }

    Music_Emu *emu = NULL;
    gme_err_t err = gme.gme_open_data(data, (long)datalen, &emu, gme_info_only);
    if (err) {
        SDL_free(data);
        return SDL_SetError("gme_open_data failed: %s", err);
    }

    *duration_frames = -1;

    gme_info_t *info = NULL;
    err = gme.gme_track_info(emu, &info, 0);
    if (!err) {  // if this fails, oh well.
        #define SET_GME_METADATA(gmestr, mixerprop) { \
            if (info->gmestr && *info->gmestr) { \
                SDL_SetStringProperty(props, "SDL_mixer.metadata.gme." #gmestr, info->gmestr); \
                if (mixerprop) { SDL_SetStringProperty(props, mixerprop, info->gmestr); } \
            } \
        }
        SET_GME_METADATA(song, MIX_PROP_METADATA_TITLE_STRING);
        SET_GME_METADATA(author, MIX_PROP_METADATA_ARTIST_STRING);
        SET_GME_METADATA(game, MIX_PROP_METADATA_ALBUM_STRING);
        SET_GME_METADATA(copyright, MIX_PROP_METADATA_COPYRIGHT_STRING);
        SET_GME_METADATA(system, NULL);
        SET_GME_METADATA(comment, NULL);
        SET_GME_METADATA(dumper, NULL);
        #undef SET_GME_METADATA

        if ((info->intro_length >= 0) && (info->loop_length > 0)) {
            *duration_frames = MIX_DURATION_INFINITE;
        } else if (info->length >= 0) {
            *duration_frames = (Sint64) MIX_MSToFrames(spec->freq, info->length);
        }

        gme.gme_free_info(info);
    }

    gme.gme_delete(emu);

    GME_AudioData *adata = (GME_AudioData *) SDL_calloc(1, sizeof (*adata));
    if (!adata) {
        SDL_free(data);
        return false;
    }

    adata->data = data;
    adata->datalen = datalen;

    // libgme only outputs Sint16 stereo data.
    spec->format = SDL_AUDIO_S16;
    spec->channels = 2;
    // libgme generates in whatever sample rate, so use the current device spec->freq.

    *audio_userdata = adata;

    return true;
}

bool SDLCALL GME_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **track_userdata)
{
    GME_TrackData *tdata = (GME_TrackData *) SDL_calloc(1, sizeof (*tdata));
    if (!tdata) {
        return false;
    }

    const GME_AudioData *adata = (const GME_AudioData *) audio_userdata;

    tdata->adata = adata;

    gme_err_t err = gme.gme_open_data(adata->data, (long)adata->datalen, &tdata->emu, spec->freq);
    if (err) {
        SDL_free(tdata);
        return SDL_SetError("gme_open_data failed: %s", err);
    }

    // Set this flag BEFORE calling the gme_start_track() to fix an inability to loop forever
    if (gme.gme_set_autoload_playback_limit) {
        gme.gme_set_autoload_playback_limit(tdata->emu, 0);
    }

    err = gme.gme_start_track(tdata->emu, 0);
    if (err) {
        gme.gme_delete(tdata->emu);
        SDL_free(tdata);
        return SDL_SetError("gme_start_track failed: %s", err);
    }

    *track_userdata = tdata;

    return true;
}

bool SDLCALL GME_decode(void *userdata, SDL_AudioStream *stream)
{
    GME_TrackData *tdata = (GME_TrackData *) userdata;
    //const GME_AudioData *adata = tdata->adata;

    if (gme.gme_track_ended(tdata->emu)) {
        return false;  // all done.
    }

    Sint16 samples[256];
    gme_err_t err = gme.gme_play(tdata->emu, SDL_arraysize(samples), (short*) samples);
    if (err != NULL) {
        return SDL_SetError("GME: %s", err);  // i guess we're done.
    }

    SDL_PutAudioStreamData(stream, samples, sizeof (samples));

    return true;  // had more data to decode.
}

bool SDLCALL GME_seek(void *userdata, Uint64 frame)
{
    GME_TrackData *tdata = (GME_TrackData *) userdata;
    gme_err_t err = gme.gme_seek_samples(tdata->emu, (int) frame);
    return err ? SDL_SetError("gme_seek_samples failed: %s", err) : true;
}

void SDLCALL GME_quit_track(void *userdata)
{
    GME_TrackData *tdata = (GME_TrackData *) userdata;
    gme.gme_delete(tdata->emu);
    SDL_free(tdata);
}

void SDLCALL GME_quit_audio(void *audio_userdata)
{
    GME_AudioData *tdata = (GME_AudioData *) audio_userdata;
    SDL_free((void *) tdata->data);
    SDL_free(tdata);
}

MIX_Decoder MIX_Decoder_GME = {
    "GME",
    GME_init,
    GME_init_audio,
    GME_init_track,
    GME_decode,
    GME_seek,
    GME_quit_track,
    GME_quit_audio,
    GME_quit
};

#endif
