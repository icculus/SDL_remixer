/*
  SDL_mixer: An audio mixer library based on the SDL library
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

/* WIKI CATEGORY: SDLMixer */

/**
 * # CategorySDLMixer
 *
 * Header file for SDL_mixer library
 *
 * A simple library to play and mix sounds and musics
 */
#ifndef SDL_MIXER_H_
#define SDL_MIXER_H_

#include <SDL3/SDL.h>
#include <SDL3/SDL_begin_code.h>

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Mix_Audio Mix_Audio;
typedef struct Mix_Track Mix_Track;


// there is no separate "init" function. You open the audio device (presumably just the default audio device) and go.

// spec is optional, can be used as a hint as to how most your audio will be formatted. We will still accept any format, and passing a NULL spec is valid.
extern SDL_DECLSPEC bool SDLCALL Mix_OpenMixer(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec);  // this will call SDL_Init(SDL_INIT_AUDIO), open audio device.
extern SDL_DECLSPEC void SDLCALL Mix_CloseMixer(void);  // this will call SDL_QuitSubSystem(SDL_INIT_AUDIO).

extern SDL_DECLSPEC int SDLCALL Mix_GetNumAudioDecoders(void);
extern SDL_DECLSPEC const char * SDLCALL Mix_GetAudioDecoder(int index);  // "WAV", "MP3", etc.

extern SDL_DECLSPEC bool SDLCALL Mix_GetDeviceSpec(SDL_AudioSpec *spec);   // what the device is actually expecting.

// there is no difference between sounds and "music" now. They're all Mix_Audio objects.
extern SDL_DECLSPEC Mix_Audio * SDLCALL Mix_LoadAudio_IO(SDL_IOStream *io, bool predecode, bool closeio);
extern SDL_DECLSPEC Mix_Audio * SDLCALL Mix_LoadAudio(const char *path, bool predecode);
extern SDL_DECLSPEC Mix_Audio * SDLCALL Mix_LoadAudioWithProperties(SDL_PropertiesID props);  // lets you specify things like "here's a path to MIDI instrument files outside of this file", etc.

#define MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER "SDL_mixer.audio.load.iostream"
#define MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN "SDL_mixer.audio.load.closeio"
#define MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN "SDL_mixer.audio.load.predecode"
#define MIX_PROP_AUDIO_DECODER_STRING "SDL_mixer.audio.decoder"

// !!! FIXME: track number?
#define MIX_PROP_METADATA_TITLE_STRING "SDL_mixer.metadata.title"
#define MIX_PROP_METADATA_ARTIST_STRING "SDL_mixer.metadata.artist"
#define MIX_PROP_METADATA_ALBUM_STRING "SDL_mixer.metadata.album"
#define MIX_PROP_METADATA_COPYRIGHT_STRING "SDL_mixer.metadata.copyright"
#define MIX_PROP_METADATA_TRACKNUM_NUMBER "SDL_mixer.metadata.tracknum"
#define MIX_PROP_METADATA_DURATION_FRAMES_NUMBER "SDL_mixer.metadata.duration_frames"  // this is in sample frames and might be off by a little if the decoder only knew it by time. Unset if unknown.
#define MIX_PROP_METADATA_DURATION_INFINITE_BOOLEAN "SDL_mixer.metadata.duration_infinite"   // if true, audio never runs out of audio to generate. This isn't necessarily always known to SDL_mixer itself, though.

// Load raw PCM data to a Mix_Audio from an IOStream.
extern SDL_DECLSPEC Mix_Audio * SDLCALL Mix_LoadRawAudio_IO(SDL_IOStream *io, const SDL_AudioSpec *spec, bool closeio);

// Load raw PCM data to a Mix_Audio. If free_when_done==true, will be SDL_free()'d when the Mix_Audio is destroyed. Otherwise, it's never free'd by SDL_mixer.
extern SDL_DECLSPEC Mix_Audio * SDLCALL Mix_LoadRawAudio(const void *data, size_t datalen, const SDL_AudioSpec *spec, bool copy);

// just in case you need some audio to play, this will generate a sine wave forever when assigned to a playing Track.
extern SDL_DECLSPEC Mix_Audio * SDLCALL Mix_CreateSineWaveAudio(int hz, float amplitude);


extern SDL_DECLSPEC SDL_PropertiesID SDLCALL Mix_GetAudioProperties(Mix_Audio *audio);  // we can store audio format-specific metadata in here (artist/album/etc info...)

extern SDL_DECLSPEC void SDLCALL Mix_DestroyAudio(Mix_Audio *audio);  // reference-counted; if this is playing, it will be _actually_ destroyed when no longer in use.


// Tracks are your "channels" but they aren't static anymore. Just make as
// many as you like and destroy them as you like. If you want the old
// semantics, just make as many as you would have allocated "channels" and put
// them in an array somewhere.

extern SDL_DECLSPEC Mix_Track * SDLCALL Mix_CreateTrack(void);
extern SDL_DECLSPEC void SDLCALL Mix_DestroyTrack(Mix_Track *track);  // will halt playback, if playing. Won't call Finished callback, though. We assume you know.

extern SDL_DECLSPEC bool SDLCALL Mix_SetTrackAudio(Mix_Track *track, Mix_Audio *audio);  // Track will replace current audio with new one. If currently playing, will start playing new audio immediately.
extern SDL_DECLSPEC bool SDLCALL Mix_SetTrackAudioStream(Mix_Track *track, SDL_AudioStream *stream);  // insert anything you like into the mix. procedural audio, VoIP, data right from a microphone, etc. Will pull from AudioStream as needed instead of a Mix_Audio.

extern SDL_DECLSPEC bool SDLCALL Mix_TagTrack(Mix_Track *track, const char *tag);  // add an arbitrary tag to a Mix_Track. You can group audio this way. A Mix_Track can have multiple tags.
extern SDL_DECLSPEC void SDLCALL Mix_UntagTrack(Mix_Track *track, const char *tag);  // remove an arbitrary tag from a Mix_Track.

extern SDL_DECLSPEC bool SDLCALL Mix_SetTrackPlaybackPosition(Mix_Track *track, Uint64 frames);  // set source playback position to X sample frames in. Must be fed from a Mix_Audio that can seek, other limitations.
extern SDL_DECLSPEC Uint64 SDLCALL Mix_GetTrackPlaybackPosition(Mix_Track *track);  // sample frames of audio that have been played from the start of this Mix_Track.

extern SDL_DECLSPEC Uint64 SDLCALL Mix_TrackMSToFrames(Mix_Track *track, Uint64 ms);
extern SDL_DECLSPEC Uint64 SDLCALL Mix_TrackFramesToMS(Mix_Track *track, Uint64 frames);
extern SDL_DECLSPEC Uint64 SDLCALL Mix_AudioMSToFrames(Mix_Audio *audio, Uint64 ms);
extern SDL_DECLSPEC Uint64 SDLCALL Mix_AudioFramesToMS(Mix_Audio *audio, Uint64 frames);
extern SDL_DECLSPEC Uint64 SDLCALL Mix_MSToFrames(int sample_rate, Uint64 ms);
extern SDL_DECLSPEC Uint64 SDLCALL Mix_FramesToMS(int sample_rate, Uint64 frames);


// operations that deal with actual mixing/playback...

// play a Mix_Track.
// if (maxFrames >= 0), it halts/loops after X sample frames of playback.
// if (loops >= 0), it loops this many times then halts (so 0==play once, 1==play twice). if < 0, loop forever.
// if (fadeIn > 0), it fades in from silence over X milliseconds. If looping, only first iteration fades in.
extern SDL_DECLSPEC bool SDLCALL Mix_PlayTrack(Mix_Track *track, Sint64 maxFrames, int loops, Sint64 startpos, Sint64 loop_start, Sint64 fadeIn, Sint64 append_silence_frames);
extern SDL_DECLSPEC bool SDLCALL Mix_PlayTag(const char *tag, Sint64 maxTicks, int loops, Sint64 fadeIn);  // play everything with this tag.

// Play a loaded audio file once from start to finish, have SDL_mixer manage a Mix_Track internally for it. This is for fire-and-forget sounds that need _zero_ adjustment, including pausing.
extern SDL_DECLSPEC bool SDLCALL Mix_PlayOnce(Mix_Audio *audio);


// halt playing audio. If (fadeOut > 0), fade out over X milliseconds before halting. if <= 0, halt immediately.
extern SDL_DECLSPEC bool SDLCALL Mix_HaltTrack(Mix_Track *track, Sint64 fadeOut);  // halt a playing Mix_Track. No-op if not playing.
extern SDL_DECLSPEC bool SDLCALL Mix_HaltAllTracks(Sint64 fadeOut);  // halt anything that's playing.
extern SDL_DECLSPEC bool SDLCALL Mix_HaltTag(const char *tag, Sint64 fadeOut);  // halt all playing Mix_Tracks with a matching tag.

// Pausing is not halting (so no finished callback, fire-and-forget sources don't destruct, resuming doesn't rewind audio to start).
extern SDL_DECLSPEC bool SDLCALL Mix_PauseTrack(Mix_Track *track);  // pause a playing Mix_Track. No-op if not playing.
extern SDL_DECLSPEC bool SDLCALL Mix_PauseAllTracks(void);  // pause anything that's playing.
extern SDL_DECLSPEC bool SDLCALL Mix_PauseTag(const char *tag);  // pause all playing Mix_Tracks with a matching tag.

// Resuming is the opposite of pausing. You can't resume a source that isn't paused.
extern SDL_DECLSPEC bool SDLCALL Mix_ResumeTrack(Mix_Track *track);  // resume a playing Mix_Track. No-op if not paused.
extern SDL_DECLSPEC bool SDLCALL Mix_ResumeAllTracks(void);  // resume anything that's playing.
extern SDL_DECLSPEC bool SDLCALL Mix_ResumeTag(const char *tag);  // resume all playing Mix_Tracks with a matching tag.

extern SDL_DECLSPEC bool SDLCALL Mix_Playing(Mix_Track *track);  // true if source is playing.
extern SDL_DECLSPEC bool SDLCALL Mix_Paused(Mix_Track *track);  // true if source is paused.


// volume control...

extern SDL_DECLSPEC bool SDLCALL Mix_SetMasterGain(float gain);  // one knob that adjusts all playing sounds. Modulates with per-Mix_Track gain.
extern SDL_DECLSPEC float SDLCALL Mix_GetMasterGain(void);

extern SDL_DECLSPEC bool SDLCALL Mix_SetTrackGain(Mix_Track *track, float gain);  // Change gain for this one Mix_Track.
extern SDL_DECLSPEC float SDLCALL Mix_GetTrackGain(Mix_Track *track);
extern SDL_DECLSPEC bool SDLCALL Mix_SetTagGain(const char *tag, float gain);  // Change gain for all Mix_Tracks with this tag.


// frequency ratio ...
extern SDL_DECLSPEC bool SDLCALL Mix_SetTrackFrequencyRatio(Mix_Track *track, float ratio);  // speed up or slow down track playback. 1.0f is normal speed, 2.0f is double speed 0.5f is half speed, etc.
extern SDL_DECLSPEC float SDLCALL Mix_GetTrackFrequencyRatio(Mix_Track *track);


// channel maps...
extern SDL_DECLSPEC bool SDLCALL Mix_SetTrackOutputChannelMap(Mix_Track *track, const int *chmap, int count);


// hooks...

typedef void (SDLCALL *Mix_TrackFinishedCallback)(void *userdata, Mix_Track *track);
extern SDL_DECLSPEC bool SDLCALL Mix_SetFinishedCallback(Mix_Track *track, Mix_TrackFinishedCallback cb, void *userdata);  // if set, is called when an track halts for any reason except destruction.

extern SDL_DECLSPEC bool SDLCALL Mix_SetPostMix(SDL_AudioPostmixCallback mix_func, void *userdata);  // just calls the standard SDL postmix callback.

typedef void (SDLCALL *Mix_TrackMixCallback)(void *userdata, Mix_Track *track, const SDL_AudioSpec *spec, float *pcm, int samples);
extern SDL_DECLSPEC bool SDLCALL Mix_SetTrackMix(Mix_Track *track, Mix_TrackMixCallback cb, void *userdata);  // is called as data is to be mixed, so you can view (and edit) the source's data. Always in float32 format!

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_MIXER_H_ */
