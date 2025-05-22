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

// !!! FIXME: remove '//' comments.
// !!! FIXME: document this whole thing.

typedef struct MIX_Mixer MIX_Mixer;
typedef struct MIX_Audio MIX_Audio;
typedef struct MIX_Track MIX_Track;
typedef struct MIX_Group MIX_Group;


#define SDL_MIXER_MAJOR_VERSION 3
#define SDL_MIXER_MINOR_VERSION 0
#define SDL_MIXER_MICRO_VERSION 0

/** This is the version number macro for the current SDL_mixer version. */
#define SDL_MIXER_VERSION \
    SDL_VERSIONNUM(SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_MICRO_VERSION)

/** This macro will evaluate to true if compiled with SDL_mixer at least X.Y.Z. */
#define SDL_MIXER_VERSION_ATLEAST(X, Y, Z) \
    ((SDL_MIXER_MAJOR_VERSION >= X) && \
     (SDL_MIXER_MAJOR_VERSION > X || SDL_MIXER_MINOR_VERSION >= Y) && \
     (SDL_MIXER_MAJOR_VERSION > X || SDL_MIXER_MINOR_VERSION > Y || SDL_MIXER_MICRO_VERSION >= Z))

/**
 * This function gets the version of the dynamically linked SDL_mixer library.
 *
 * \returns SDL_mixer version.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 */
extern SDL_DECLSPEC int SDLCALL MIX_Version(void);

extern SDL_DECLSPEC bool MIX_Init(void);  // this will call SDL_Init(SDL_INIT_AUDIO), prepare optional decoders, etc.
extern SDL_DECLSPEC void MIX_Quit(void);  // this will call SDL_QuitSubSystem(SDL_INIT_AUDIO), clean up global state, destroy any MIX_Mixers and MIX_Audio objects that remain, etc.

extern SDL_DECLSPEC int SDLCALL MIX_GetNumAudioDecoders(void);
extern SDL_DECLSPEC const char * SDLCALL MIX_GetAudioDecoder(int index);  // "WAV", "MP3", etc.


// spec is optional, can be used as a hint as to how most your audio will be formatted. We will still accept any format, and passing a NULL spec is valid.
extern SDL_DECLSPEC MIX_Mixer * SDLCALL MIX_CreateMixerDevice(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec);  // we want a mixer that will generate audio directly to the audio device of our choice.
extern SDL_DECLSPEC MIX_Mixer * SDLCALL MIX_CreateMixer(const SDL_AudioSpec *spec);  // we want a mixer that will generate audio through an SDL_AudioStream we can consume from on-demand.
extern SDL_DECLSPEC void SDLCALL MIX_DestroyMixer(MIX_Mixer *mixer);

extern SDL_DECLSPEC bool SDLCALL MIX_GetMixerSpec(MIX_Mixer *mixer, SDL_AudioSpec *spec);   // what the device is actually expecting/what the mixer is generating.

// there is no difference between sounds and "music" now. They're all MIX_Audio objects.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadAudio_IO(MIX_Mixer *mixer, SDL_IOStream *io, bool predecode, bool closeio);
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadAudio(MIX_Mixer *mixer, const char *path, bool predecode);
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadAudioWithProperties(SDL_PropertiesID props);  // lets you specify things like "here's a path to MIDI instrument files outside of this file", etc.

#define MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER "SDL_mixer.audio.load.iostream"
#define MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN "SDL_mixer.audio.load.closeio"
#define MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN "SDL_mixer.audio.load.predecode"
#define MIX_PROP_AUDIO_LOAD_PREFERRED_MIXER_POINTER "SDL_mixer.audio.load.preferred_mixer"
#define MIX_PROP_AUDIO_DECODER_STRING "SDL_mixer.audio.decoder"

#define MIX_PROP_METADATA_TITLE_STRING "SDL_mixer.metadata.title"
#define MIX_PROP_METADATA_ARTIST_STRING "SDL_mixer.metadata.artist"
#define MIX_PROP_METADATA_ALBUM_STRING "SDL_mixer.metadata.album"
#define MIX_PROP_METADATA_COPYRIGHT_STRING "SDL_mixer.metadata.copyright"
#define MIX_PROP_METADATA_TRACK_NUMBER "SDL_mixer.metadata.track"
#define MIX_PROP_METADATA_TOTAL_TRACKS_NUMBER "SDL_mixer.metadata.total_tracks"
#define MIX_PROP_METADATA_YEAR_NUMBER "SDL_mixer.metadata.year"
#define MIX_PROP_METADATA_DURATION_FRAMES_NUMBER "SDL_mixer.metadata.duration_frames"  /**< This is in sample frames and might be off by a little if the decoder only knew it by time. Unset if unknown. */
#define MIX_PROP_METADATA_DURATION_INFINITE_BOOLEAN "SDL_mixer.metadata.duration_infinite"   /**< If true, audio never runs out of audio to generate. This isn't necessarily always known to SDL_mixer itself, though. */

// Load raw PCM data to a MIX_Audio from an IOStream.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadRawAudio_IO(MIX_Mixer *mixer, SDL_IOStream *io, const SDL_AudioSpec *spec, bool closeio);

// Load raw PCM data to a MIX_Audio. If free_when_done==true, will be SDL_free()'d when the MIX_Audio is destroyed. Otherwise, it's never free'd by SDL_mixer.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadRawAudio(MIX_Mixer *mixer, const void *data, size_t datalen, const SDL_AudioSpec *spec, bool copy);

// just in case you need some audio to play, this will generate a sine wave forever when assigned to a playing Track.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_CreateSineWaveAudio(MIX_Mixer *mixer, int hz, float amplitude);


extern SDL_DECLSPEC SDL_PropertiesID SDLCALL MIX_GetAudioProperties(MIX_Audio *audio);  // we can store audio format-specific metadata in here (artist/album/etc info...)
extern SDL_DECLSPEC Sint64 MIX_GetAudioDuration(MIX_Audio *audio);

extern SDL_DECLSPEC void SDLCALL MIX_DestroyAudio(MIX_Audio *audio);  // reference-counted; if this is playing, it will be _actually_ destroyed when no longer in use.


// Tracks are your "channels" but they aren't static anymore. Just make as
// many as you like and destroy them as you like. If you want the old
// semantics, just make as many as you would have allocated "channels" and put
// them in an array somewhere.

extern SDL_DECLSPEC MIX_Track * SDLCALL MIX_CreateTrack(MIX_Mixer *mixer);
extern SDL_DECLSPEC void SDLCALL MIX_DestroyTrack(MIX_Track *track);  // will halt playback, if playing. Won't call Stopped callback, though. We assume you know.

extern SDL_DECLSPEC MIX_Mixer * SDLCALL MIX_GetTrackMixer(MIX_Track *track);

extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackAudio(MIX_Track *track, MIX_Audio *audio);  // Track will replace current audio with new one. If currently playing, will start playing new audio immediately.
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackAudioStream(MIX_Track *track, SDL_AudioStream *stream);  // insert anything you like into the mix. procedural audio, VoIP, data right from a microphone, etc. Will pull from AudioStream as needed instead of a MIX_Audio.

extern SDL_DECLSPEC bool SDLCALL MIX_TagTrack(MIX_Track *track, const char *tag);  // add an arbitrary tag to a MIX_Track. You can group audio this way. A MIX_Track can have multiple tags.
extern SDL_DECLSPEC void SDLCALL MIX_UntagTrack(MIX_Track *track, const char *tag);  // remove an arbitrary tag from a MIX_Track.

extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackPlaybackPosition(MIX_Track *track, Uint64 frames);  // set source playback position to X sample frames in. Must be fed from a MIX_Audio that can seek, other limitations.
extern SDL_DECLSPEC Sint64 SDLCALL MIX_GetTrackPlaybackPosition(MIX_Track *track);  // sample frames of audio that have been played from the start of this MIX_Track.

extern SDL_DECLSPEC bool SDLCALL MIX_TrackLooping(MIX_Track *track);
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_GetTrackAudio(MIX_Track *track);
extern SDL_DECLSPEC SDL_AudioStream * SDLCALL MIX_GetTrackAudioStream(MIX_Track *track);
extern SDL_DECLSPEC Sint64 MIX_GetTrackRemaining(MIX_Track *track);

extern SDL_DECLSPEC Uint64 SDLCALL MIX_TrackMSToFrames(MIX_Track *track, Uint64 ms);
extern SDL_DECLSPEC Uint64 SDLCALL MIX_TrackFramesToMS(MIX_Track *track, Uint64 frames);
extern SDL_DECLSPEC Uint64 SDLCALL MIX_AudioMSToFrames(MIX_Audio *audio, Uint64 ms);
extern SDL_DECLSPEC Uint64 SDLCALL MIX_AudioFramesToMS(MIX_Audio *audio, Uint64 frames);
extern SDL_DECLSPEC Uint64 SDLCALL MIX_MSToFrames(int sample_rate, Uint64 ms);
extern SDL_DECLSPEC Uint64 SDLCALL MIX_FramesToMS(int sample_rate, Uint64 frames);


// operations that deal with actual mixing/playback...

// play a MIX_Track.
// if (maxFrames >= 0), it halts/loops after X sample frames of playback.
// if (loops >= 0), it loops this many times then halts (so 0==play once, 1==play twice). if < 0, loop forever.
// if (fadeIn > 0), it fades in from silence over X milliseconds. If looping, only first iteration fades in.
extern SDL_DECLSPEC bool SDLCALL MIX_PlayTrack(MIX_Track *track, SDL_PropertiesID options);

#define MIX_PROP_PLAY_LOOPS_NUMBER "SDL_mixer.play.loops"
#define MIX_PROP_PLAY_MAX_FRAMES_NUMBER "SDL_mixer.play.max_frames"
#define MIX_PROP_PLAY_MAX_MILLISECONDS_NUMBER "SDL_mixer.play.max_milliseconds"
#define MIX_PROP_PLAY_START_FRAME_NUMBER "SDL_mixer.play.start_frame"
#define MIX_PROP_PLAY_START_MILLISECOND_NUMBER "SDL_mixer.play.start_millisecond"
#define MIX_PROP_PLAY_LOOP_START_FRAME_NUMBER "SDL_mixer.play.loop_start_frame"
#define MIX_PROP_PLAY_LOOP_START_MILLISECOND_NUMBER "SDL_mixer.play.loop_start_millisecond"
#define MIX_PROP_PLAY_FADE_IN_FRAMES_NUMBER "SDL_mixer.play.fade_in_frames"
#define MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER "SDL_mixer.play.fade_in_milliseconds"
#define MIX_PROP_PLAY_APPEND_SILENCE_FRAMES_NUMBER "SDL_mixer.play.append_silence_frames"
#define MIX_PROP_PLAY_APPEND_SILENCE_MILLISECONDS_NUMBER "SDL_mixer.play.append_silence_milliseconds"

extern SDL_DECLSPEC bool SDLCALL MIX_PlayTag(MIX_Mixer *mixer, const char *tag, SDL_PropertiesID options);  // play everything with this tag.


// Play a loaded audio file once from start to finish, have SDL_mixer manage a MIX_Track internally for it. This is for fire-and-forget sounds that need _zero_ adjustment, including pausing.
extern SDL_DECLSPEC bool SDLCALL MIX_PlayAudio(MIX_Mixer *mixer, MIX_Audio *audio);

// halt playing audio. If (fadeOut > 0), fade out over X milliseconds before halting. if <= 0, halt immediately.
extern SDL_DECLSPEC bool SDLCALL MIX_StopTrack(MIX_Track *track, Sint64 fadeOut);  // halt a playing MIX_Track. No-op if not playing.
extern SDL_DECLSPEC bool SDLCALL MIX_StopAllTracks(MIX_Mixer *mixer, Sint64 fadeOut);  // halt anything that's playing.
extern SDL_DECLSPEC bool SDLCALL MIX_StopTag(MIX_Mixer *mixer, const char *tag, Sint64 fadeOut);  // halt all playing MIX_Tracks with a matching tag.

// Pausing is not stopping (so no stopped callback, fire-and-forget sources don't destruct, resuming doesn't rewind audio to start).
extern SDL_DECLSPEC bool SDLCALL MIX_PauseTrack(MIX_Track *track);  // pause a playing MIX_Track. No-op if not playing.
extern SDL_DECLSPEC bool SDLCALL MIX_PauseAllTracks(MIX_Mixer *mixer);  // pause anything that's playing.
extern SDL_DECLSPEC bool SDLCALL MIX_PauseTag(MIX_Mixer *mixer, const char *tag);  // pause all playing MIX_Tracks with a matching tag.

// Resuming is the opposite of pausing. You can't resume a source that isn't paused.
extern SDL_DECLSPEC bool SDLCALL MIX_ResumeTrack(MIX_Track *track);  // resume a playing MIX_Track. No-op if not paused.
extern SDL_DECLSPEC bool SDLCALL MIX_ResumeAllTracks(MIX_Mixer *mixer);  // resume anything that's playing.
extern SDL_DECLSPEC bool SDLCALL MIX_ResumeTag(MIX_Mixer *mixer, const char *tag);  // resume all playing MIX_Tracks with a matching tag.

extern SDL_DECLSPEC bool SDLCALL MIX_TrackPlaying(MIX_Track *track);  // true if source is playing.
extern SDL_DECLSPEC bool SDLCALL MIX_TrackPaused(MIX_Track *track);  // true if source is paused.


// volume control...

extern SDL_DECLSPEC bool SDLCALL MIX_SetMasterGain(MIX_Mixer *mixer, float gain);  // one knob that adjusts all playing sounds. Modulates with per-MIX_Track gain.
extern SDL_DECLSPEC float SDLCALL MIX_GetMasterGain(MIX_Mixer *mixer);

extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackGain(MIX_Track *track, float gain);  // Change gain for this one MIX_Track.
extern SDL_DECLSPEC float SDLCALL MIX_GetTrackGain(MIX_Track *track);
extern SDL_DECLSPEC bool SDLCALL MIX_SetTagGain(MIX_Mixer *mixer, const char *tag, float gain);  // Change gain for all MIX_Tracks with this tag.


// frequency ratio ...
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackFrequencyRatio(MIX_Track *track, float ratio);  // speed up or slow down track playback. 1.0f is normal speed, 2.0f is double speed 0.5f is half speed, etc.
extern SDL_DECLSPEC float SDLCALL MIX_GetTrackFrequencyRatio(MIX_Track *track);


// channel maps...
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackOutputChannelMap(MIX_Track *track, const int *chmap, int count);

// groups...
extern SDL_DECLSPEC MIX_Group * SDLCALL MIX_CreateGroup(MIX_Mixer *mixer);
extern SDL_DECLSPEC void SDLCALL MIX_DestroyGroup(MIX_Group *group);
extern SDL_DECLSPEC MIX_Mixer * SDLCALL MIX_GetGroupMixer(MIX_Group *group);
extern SDL_DECLSPEC bool MIX_SetTrackGroup(MIX_Track *track, MIX_Group *group);  // both track and group must be on same MIX_Mixer!


// hooks...
typedef void (SDLCALL *MIX_TrackStoppedCallback)(void *userdata, MIX_Track *track);
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackStoppedCallback(MIX_Track *track, MIX_TrackStoppedCallback cb, void *userdata);  // if set, is called when an track halts for any reason except destruction.

typedef void (SDLCALL *MIX_TrackMixCallback)(void *userdata, MIX_Track *track, const SDL_AudioSpec *spec, float *pcm, int samples);
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackRawCallback(MIX_Track *track, MIX_TrackMixCallback cb, void *userdata);  // is called as data is decoded, so you can view (and edit) the source's data unchanged. Always in float32 format!
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackCookedCallback(MIX_Track *track, MIX_TrackMixCallback cb, void *userdata);  // is called as data is to be mixed, with all its transformations (gain, fade, frequency ratio, etc), so you can view (and edit) the source's data. Always in float32 format!

typedef void (SDLCALL *MIX_GroupMixCallback)(void *userdata, MIX_Group *group, const SDL_AudioSpec *spec, float *pcm, int samples);
extern SDL_DECLSPEC bool SDLCALL MIX_SetGroupPostMixCallback(MIX_Group *group, MIX_GroupMixCallback cb, void *userdata);  // is called when a group of tracks has been mixed, before it mixes with other groups. This is the mixed data of all "cooked" tracks in the group.
typedef void (SDLCALL *MIX_PostMixCallback)(void *userdata, MIX_Mixer *mixer, const SDL_AudioSpec *spec, float *pcm, int samples);
extern SDL_DECLSPEC bool SDLCALL MIX_SetPostMixCallback(MIX_Mixer *mixer, MIX_PostMixCallback cb, void *userdata);  // called as mixed data is going to the audio hardware to be played!

// can only call this if mixer came from MIX_CreateMixer() instead of MIX_CreateMixerDevice(). `buflen` must be a multiple of the output frame size! Will run as fast as you let it!
extern SDL_DECLSPEC bool MIX_Generate(MIX_Mixer *mixer, float *buffer, int buflen);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_MIXER_H_ */
