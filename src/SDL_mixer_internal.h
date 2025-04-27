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

typedef struct MIX_Decoder
{
    const char *name;
    bool (SDLCALL *init)(void);   // initialize the decoder (load external libraries, etc).
    bool (SDLCALL *init_audio)(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata);  // see if it's a supported format, init spec, set metadata in props, allocate static userdata and payload.
    bool (SDLCALL *init_track)(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, void **userdata);  // init decoder instance data for a single track.
    bool (SDLCALL *decode)(void *userdata, SDL_AudioStream *stream);
    bool (SDLCALL *seek)(void *userdata, Uint64 frame);
    void (SDLCALL *quit_track)(void *userdata);
    void (SDLCALL *quit_audio)(void *audio_userdata);
    void (SDLCALL *quit)(void);   // deinitialize the decoder (unload external libraries, etc).
} MIX_Decoder;

typedef enum MIX_TrackState
{
    MIX_STATE_STOPPED,
    MIX_STATE_PAUSED,
    MIX_STATE_PLAYING
} MIX_TrackState;

struct MIX_Audio
{
    SDL_AtomicInt refcount;
    SDL_PropertiesID props;
    SDL_AudioSpec spec;
    const MIX_Decoder *decoder;
    void *decoder_userdata;
    MIX_Audio *prev;  // double-linked list for all_audios.
    MIX_Audio *next;
};

struct MIX_Track
{
    Uint8 *input_buffer;  // a place to process audio as it progresses through the callback.
    size_t input_buffer_len;  // number of bytes allocated to input_buffer.
    MIX_Audio *input_audio;    // non-NULL if used with MIX_SetTrackAudioStream. Holds a reference.
    SDL_AudioStream *input_stream;  // used for both MIX_SetTrackAudio and MIX_SetTrackAudioStream. Maybe not owned by SDL_mixer!
    SDL_AudioStream *internal_stream;  // used with MIX_SetTrackAudio, where it is also assigned to input_stream. Owned by SDL_mixer!
    void *decoder_userdata;  // MIX_Decoder-specific data for this run, if any.
    SDL_AudioSpec output_spec;  // processed data we send to SDL is in this format.
    SDL_AudioStream *output_stream;  // the stream that is bound to the audio device.
    MIX_TrackState state;  // playing, paused, stopped.
    Uint64 position;   // sample frames played from start of file.
    Sint64 silence_frames;  // number of frames of silence to mix at the end of the track.
    Sint64 max_frames;   // consider audio at EOF after this many sample frames.
    bool fire_and_forget;  // true if this is a MIX_Track managed internally for fire-and-forget playback.
    Sint64 total_fade_frames;  // fade in or out for this many sample frames.
    Sint64 fade_frames;  // remaining frames to fade.
    int fade_direction;  // -1: fade out  0: don't fade  1: fade in
    int loops_remaining;  // seek to loop_start and continue this many more times at end of input. Negative to loop forever.
    int loop_start;      // sample frame position for loops to begin, so you can play an intro once and then loop from an internal point thereafter.
    SDL_PropertiesID tags;  // lookup tags to see if they are currently applied to this track (true or false).
    MIX_TrackMixCallback mix_callback;
    void *mix_callback_userdata;
    MIX_TrackStoppedCallback stopped_callback;
    void *stopped_callback_userdata;
    MIX_Track *prev;  // double-linked list for all_tracks.
    MIX_Track *next;
    MIX_Track *fire_and_forget_next;  // linked list for the fire-and-forget pool.
};


// these are not (currently) available in the public API, and may change names or functionality, or be removed.
#define MIX_PROP_DECODER_NAME_STRING "SDL_mixer.decoder.name"
#define MIX_PROP_DECODER_FORMAT_NUMBER "SDL_mixer.decoder.format"
#define MIX_PROP_DECODER_CHANNELS_NUMBER "SDL_mixer.decoder.channels"
#define MIX_PROP_DECODER_FREQ_NUMBER "SDL_mixer.decoder.format"
#define MIX_PROP_DECODER_SINEWAVE_HZ_NUMBER "SDL_mixer.decoder.sinewave.hz"
#define MIX_PROP_DECODER_SINEWAVE_AMPLITUDE_FLOAT "SDL_mixer.decoder.sinewave.amplitude"
#define MIX_PROP_DECODER_WAVPACK_WVC_IOSTREAM_POINTER "SDL_mixer.decoder.wavpack.wvc_iostream"
#define MIX_PROP_DECODER_WAVPACK_WVC_PATH_STRING "SDL_mixer.decoder.wavpack.wvc_path"
#define MIX_PROP_DECODER_FLUIDSYNTH_SOUNDFONT_IOSTREAM_POINTER "SDL_mixer.decoder.fluidsynth.soundfont_iostream"
#define MIX_PROP_DECODER_FLUIDSYNTH_SOUNDFONT_PATH_STRING "SDL_mixer.decoder.fluidsynth.soundfont_path"
#define MIX_PROP_AUDIO_LOAD_PATH_STRING "SDL_mixer.audio.load.path"

#define MIX_DURATION_UNKNOWN -1
#define MIX_DURATION_INFINITE -2

// Clamp an IOStream to a subset of its available data...this is used to cut ID3 (etc) tags off
//  both ends of an audio file, making it look like the file just doesn't have those bytes.

typedef struct MIX_IoClamp
{
    SDL_IOStream *io;
    Sint64 start;
    Sint64 length;
    Sint64 pos;
} MIX_IoClamp;

extern SDL_IOStream *MIX_OpenIoClamp(MIX_IoClamp *clamp, SDL_IOStream *io);


// access to the RAW "decoder" from other parts of SDL_mixer, without having to set up properties or copy the payload.
extern void *MIX_RAW_InitFromMemoryBuffer(const void *data, const size_t datalen, const SDL_AudioSpec *spec, Sint64 *duration_frames, bool free_when_done);

// decoders that are mostly picking out the raw PCM payload from an uncompressed format can use the RAW decoder for most of their implementation.
extern bool SDLCALL MIX_RAW_init_track(void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID metadata_props, void **userdata);
extern bool SDLCALL MIX_RAW_decode(void *userdata, SDL_AudioStream *stream);
extern bool SDLCALL MIX_RAW_seek(void *userdata, Uint64 frame);
extern void SDLCALL MIX_RAW_quit_track(void *userdata);
extern void SDLCALL MIX_RAW_quit_audio(void *audio_userdata);

// Parse through an SDL_IOStream for tags (ID3, APE, MusicMatch, etc), and add metadata to props.
// !!! FIXME: see FIXME in the function's implementation; just ignore return values from this function for now.
extern bool MIX_ReadMetadataTags(SDL_IOStream *io, SDL_PropertiesID props, MIX_IoClamp *clamp);

// Various Ogg-based decoders use this (Vorbis, FLAC, Opus, etc).
void MIX_ParseOggComments(SDL_PropertiesID props, int freq, const char *vendor, const char * const *user_comments, int num_comments, Sint64 *loop_start, Sint64 *loop_end, Sint64 *loop_len);

// these might not all be available, but they are all declared here as if they are.
extern MIX_Decoder MIX_Decoder_VOC;
extern MIX_Decoder MIX_Decoder_WAV;
extern MIX_Decoder MIX_Decoder_AIFF;
extern MIX_Decoder MIX_Decoder_MPG123;
extern MIX_Decoder MIX_Decoder_DRMP3;
extern MIX_Decoder MIX_Decoder_VORBIS;
extern MIX_Decoder MIX_Decoder_STBVORBIS;
extern MIX_Decoder MIX_Decoder_OPUS;
extern MIX_Decoder MIX_Decoder_FLAC;
extern MIX_Decoder MIX_Decoder_DRFLAC;
extern MIX_Decoder MIX_Decoder_TIMIDITY;
extern MIX_Decoder MIX_Decoder_FLUIDSYNTH;
extern MIX_Decoder MIX_Decoder_WAVPACK;
extern MIX_Decoder MIX_Decoder_GME;
extern MIX_Decoder MIX_Decoder_SINEWAVE;
extern MIX_Decoder MIX_Decoder_RAW;

