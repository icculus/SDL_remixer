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

// This file supports playing MIDI files with timidity

#ifdef DECODER_MIDI_TIMIDITY

#include "SDL_mixer_internal.h"

#include "timidity/timidity.h"

// Config file should contain any other directory that needs
//  to be added to the search path. The library adds the path
//  of the config file to its search path, too.
#if defined(SDL_PLATFORM_WIN32)
static const char *timidity_cfgs[] = { "C:\\TIMIDITY\\TIMIDITY.CFG" };
#else  // unix:
static const char *timidity_cfgs[] = { "/etc/timidity.cfg", "/etc/timidity/timidity.cfg", "/etc/timidity/freepats.cfg" };
#endif

typedef struct TIMIDITY_AudioUserData
{
    const Uint8 *data;
    size_t datalen;
} TIMIDITY_AudioUserData;

typedef struct TIMIDITY_UserData
{
    const TIMIDITY_AudioUserData *payload;
    Sint32 samples[4096 * 2];   // !!! FIXME: there's a hardcoded thing about buffer_size in our copy of timidity that needs to be fixed; it's hardcoded to this at the moment.
    MidiSong *song;
    int freq;
} TIMIDITY_UserData;


static bool SDLCALL TIMIDITY_init(void)
{
    const char *cfg = SDL_getenv("TIMIDITY_CFG");  // see if the user had one.
    if (cfg) {
        return (Timidity_Init(cfg) == 0); // env or user override: no other tries
    }

    for (int i = 0; i < SDL_arraysize(timidity_cfgs); i++) {
        if (Timidity_Init(timidity_cfgs[i]) == 0) {
            return true;
        }
    }

    return (Timidity_Init(NULL) == 0); // library's default cfg.
}

static void SDLCALL TIMIDITY_quit(void)
{
    Timidity_Exit();
}

static bool SDLCALL TIMIDITY_init_audio(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    // just load the bare minimum from the IOStream to verify it's a MIDI file.
    char magic[4];
    if (SDL_ReadIO(io, magic, 4) != 4) {
        return false;
    } else if (SDL_memcmp(magic, "MThd", 4) != 0) {
        return SDL_SetError("Not a MIDI audio stream");
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

    spec->format = SDL_AUDIO_S32;   // timidity wants to do Sint32, and converts to other formats internally from Sint32.
    spec->channels = 2;
    // Use the device's current sample rate, already set in spec->freq

    Sint64 song_length_in_frames = -1;
    MidiSong *song = NULL;
    SDL_IOStream *iomem = SDL_IOFromConstMem(data, datalen);
    if (iomem) {
        song = Timidity_LoadSong(iomem, spec);
        if (song) {  // !!! FIXME: this is just to verify Timidity likes this file, but it's not strictly necessary.
            song_length_in_frames = MIX_MSToFrames(spec->freq, Timidity_GetSongLength(song));
            Timidity_FreeSong(song);
        }
        SDL_CloseIO(iomem);
    }

    if (!song) {
        SDL_free(data);
        return false;
    }

    TIMIDITY_AudioUserData *payload = (TIMIDITY_AudioUserData *) SDL_calloc(1, sizeof (*payload));
    if (!payload) {
        SDL_free(data);
        return false;
    }

    payload->data = data;
    payload->datalen = datalen;
    
    *duration_frames = song_length_in_frames;
    *audio_userdata = payload;

    return true;
}

bool SDLCALL TIMIDITY_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata)
{
    TIMIDITY_UserData *d = (TIMIDITY_UserData *) SDL_calloc(1, sizeof (*d));
    if (!d) {
        return false;
    }

    const TIMIDITY_AudioUserData *payload = (const TIMIDITY_AudioUserData *) audio_userdata;
    SDL_IOStream *io = SDL_IOFromConstMem(payload->data, payload->datalen);
    if (!io) {
        SDL_free(d);
        return false;
    }
    d->song = Timidity_LoadSong(io, spec);
    SDL_CloseIO(io);

    if (!d->song) {
        SDL_free(d);
        return SDL_SetError("Timidity_LoadSong failed");
    }

    Timidity_SetVolume(d->song, 800);  // !!! FIXME: maybe my test patches are really quiet?
    Timidity_Start(d->song);

    d->payload = payload;
    d->freq = spec->freq;

    *userdata = d;
    return true;
}

bool SDLCALL TIMIDITY_decode(void *userdata, SDL_AudioStream *stream)
{
    TIMIDITY_UserData *d = (TIMIDITY_UserData *) userdata;
    //Sint32 samples[256];  // !!! FIXME: there's a hardcoded thing about buffer_size in our copy of timidity that needs to be fixed; it's hardcoded at the moment, so we use d->samples.
    const int amount = Timidity_PlaySome(d->song, d->samples, sizeof (d->samples));
    if (amount <= 0) {
        return false;  // EOF or error, we're done either way.
    }

//{ static SDL_IOStream *io = NULL; if (!io) { io = SDL_IOFromFile("decoded.raw", "wb"); } if (io) { SDL_WriteIO(io, samples, amount); SDL_FlushIO(io); } }

    SDL_PutAudioStreamData(stream, d->samples, amount);
    return true;
}

bool SDLCALL TIMIDITY_seek(void *userdata, Uint64 frame)
{
    TIMIDITY_UserData *d = (TIMIDITY_UserData *) userdata;
    const Uint32 ticks = (Uint32) MIX_FramesToMS(d->freq, frame);
    Timidity_Seek(d->song, ticks);  // !!! FIXME: this returns void, what happens if we seek past EOF?
    return true;
}

void SDLCALL TIMIDITY_quit_track(void *userdata)
{
    TIMIDITY_UserData *d = (TIMIDITY_UserData *) userdata;
    Timidity_Stop(d->song);
    Timidity_FreeSong(d->song);
    SDL_free(d);
}

void SDLCALL TIMIDITY_quit_audio(void *audio_userdata)
{
    TIMIDITY_AudioUserData *d = (TIMIDITY_AudioUserData *) audio_userdata;
    SDL_free((void *) d->data);
    SDL_free(d);
}

MIX_Decoder MIX_Decoder_TIMIDITY = {
    "TIMIDITY",
    TIMIDITY_init,
    TIMIDITY_init_audio,
    TIMIDITY_init_track,
    TIMIDITY_decode,
    TIMIDITY_seek,
    TIMIDITY_quit_track,
    TIMIDITY_quit_audio,
    TIMIDITY_quit
};

#endif

