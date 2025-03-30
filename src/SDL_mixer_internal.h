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

#include "SDL3_mixer/SDL_mixer.h"

typedef struct Mix_Decoder
{
    const char *name;
    bool (SDLCALL *init)(void);   // initialize the decoder (load external libraries, etc).
    bool (SDLCALL *init_audio)(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, void **audio_userdata);  // see if it's a supported format, init spec, set metadata in props, allocate static userdata and payload.
    bool (SDLCALL *init_track)(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata);  // init decoder instance data for a single track.
    int  (SDLCALL *decode)(void *userdata, void *buffer, size_t buflen);
    bool (SDLCALL *seek)(void *userdata, Uint64 frame);
    void (SDLCALL *quit_track)(void *userdata);
    void (SDLCALL *quit_audio)(void *audio_userdata);
    void (SDLCALL *quit)(void);   // deinitialize the decoder (unload external libraries, etc).
} Mix_Decoder;

typedef enum Mix_TrackState
{
    MIX_STATE_STOPPED,
    MIX_STATE_PAUSED,
    MIX_STATE_PLAYING
} Mix_TrackState;

struct Mix_Audio
{
    SDL_AtomicInt refcount;
    SDL_PropertiesID props;
    void *buffer;
    size_t buflen;
    SDL_AudioSpec spec;
    const Mix_Decoder *decoder;
    void *decoder_userdata;
    Mix_Audio *prev;  // double-linked list for all_audios.
    Mix_Audio *next;
};

struct Mix_Track
{
    Uint8 *input_buffer;  // a place to process audio as it progresses through the callback.
    size_t input_buffer_len;  // number of bytes allocated to input_buffer.
    Mix_Audio *input_audio;    // non-NULL if used with Mix_SetTrackAudioStream. Holds a reference.
    SDL_AudioStream *input_stream;  // non-NULL if used with Mix_SetTrackAudioStream. Not owned by SDL_mixer!
    void *decoder_userdata;  // Mix_Decoder-specific data for this run, if any.
    SDL_AudioSpec input_spec;  // data from input_stream or input_audio is in this format.
    SDL_AudioSpec output_spec;  // processed data we send to SDL is in this format.
    SDL_AudioStream *output_stream;  // the stream that is bound to the audio device.
    Mix_TrackState state;  // playing, paused, stopped.
    Uint64 position;   // sample frames played from start of file.
    Sint64 silence_frames;  // number of frames of silence to mix at the end of the track.
    Sint64 max_frames;   // consider audio at EOF after this many sample frames.
    bool fire_and_forget;  // true if this is a Mix_Track managed internally for fire-and-forget playback.
    Sint64 total_fade_frames;  // fade in or out for this many sample frames.
    Sint64 fade_frames;  // remaining frames to fade.
    int fade_direction;  // -1: fade out  0: don't fade  1: fade in
    int loops_remaining;  // seek to loop_start and continue this many more times at end of input. Negative to loop forever.
    int loop_start;      // sample frame position for loops to begin, so you can play an intro once and then loop from an internal point thereafter.
    SDL_PropertiesID tags;  // lookup tags to see if they are currently applied to this track (true or false).
    Mix_TrackMixCallback mix_callback;
    void *mix_callback_userdata;
    Mix_TrackFinishedCallback finished_callback;
    void *finished_callback_userdata;
    Mix_Track *prev;  // double-linked list for all_tracks.
    Mix_Track *next;
    Mix_Track *fire_and_forget_next;  // linked list for the fire-and-forget pool.
};

// these are not (currently) available in the public API, and may change names or functionality, or be removed.
#define MIX_PROP_DECODER_NAME_STRING "SDL_mixer.decoder.name"
#define MIX_PROP_DECODER_FORMAT_NUMBER "SDL_mixer.decoder.format"
#define MIX_PROP_DECODER_CHANNELS_NUMBER "SDL_mixer.decoder.channels"
#define MIX_PROP_DECODER_FREQ_NUMBER "SDL_mixer.decoder.format"
#define MIX_PROP_DECODER_SINEWAVE_HZ_NUMBER "SDL_mixer.decoder.sinewave.hz"
#define MIX_PROP_DECODER_SINEWAVE_AMPLITUDE_FLOAT "SDL_mixer.decoder.sinewave.amplitude"
#define MIX_PROP_DECODER_SINEWAVE_SAMPLE_RATE_NUMBER "SDL_mixer.decoder.sinewave.sample_rate"

// access to the RAW "decoder" from other parts of SDL_mixer, without having to set up properties or copy the payload.
extern void *Mix_RAW_InitFromMemoryBuffer(const void *data, const size_t datalen, const SDL_AudioSpec *spec, bool free_when_done);

// decoders that are mostly picking out the raw PCM payload from an uncompressed format can use the RAW decoder for most of their implementation.
extern bool SDLCALL Mix_RAW_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID metadata_props, void **userdata);
extern int SDLCALL Mix_RAW_decode(void *userdata, void *buffer, size_t buflen);
extern bool SDLCALL Mix_RAW_seek(void *userdata, Uint64 frame);
extern void SDLCALL Mix_RAW_quit_track(void *userdata);
extern void SDLCALL Mix_RAW_quit_audio(void *audio_userdata);


// these might not all be available, but they are all declared here as if they are.
extern Mix_Decoder Mix_Decoder_VOC;
extern Mix_Decoder Mix_Decoder_WAV;
extern Mix_Decoder Mix_Decoder_AIFF;
extern Mix_Decoder Mix_Decoder_SINEWAVE;
extern Mix_Decoder Mix_Decoder_RAW;

