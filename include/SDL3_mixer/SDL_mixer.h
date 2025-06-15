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

/**
 * The current major version of SDL_mixer headers.
 *
 * If this were SDL_mixer version 3.2.1, this value would be 3.
 *
 * \since This macro is available since SDL_mixer 3.0.0.
 */
#define MIX_MAJOR_VERSION   3

/**
 * The current minor version of the SDL headers.
 *
 * If this were SDL_mixer version 3.2.1, this value would be 2.
 *
 * \since This macro is available since SDL_mixer 3.0.0.
 */
#define MIX_MINOR_VERSION   0

/**
 * The current micro (or patchlevel) version of the SDL headers.
 *
 * If this were SDL_mixer version 3.2.1, this value would be 1.
 *
 * \since This macro is available since SDL_mixer 3.0.0.
 */
#define MIX_MICRO_VERSION   0

/**
 * This is the version number macro for the current SDL_mixer version.
 *
 * \since This macro is available since SDL_mixer 3.0.0.
 *
 * \sa MIX_GetVersion
 */
#define MIX_VERSION SDL_VERSIONNUM(MIX_MAJOR_VERSION, MIX_MINOR_VERSION, MIX_MICRO_VERSION)

/**
 * Get the version of SDL_mixer that is linked against your program.
 *
 * If you are linking to SDL_mixer dynamically, then it is possible that the current
 * version will be different than the version you compiled against. This
 * function returns the current version, while MIX_VERSION is the version
 * you compiled with.
 *
 * This function may be called safely at any time, even before SDL_Init().
 *
 * \returns the version of the linked library.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 *
 * \sa MIX_VERSION
 */
extern SDL_DECLSPEC int SDLCALL MIX_GetVersion(void);


extern SDL_DECLSPEC bool MIX_Init(void);  // this will call SDL_Init(SDL_INIT_AUDIO), prepare optional decoders, etc.
extern SDL_DECLSPEC void MIX_Quit(void);  // this will call SDL_QuitSubSystem(SDL_INIT_AUDIO), clean up global state, destroy any MIX_Mixers and MIX_Audio objects that remain, etc.

extern SDL_DECLSPEC int SDLCALL MIX_GetNumAudioDecoders(void);
extern SDL_DECLSPEC const char * SDLCALL MIX_GetAudioDecoder(int index);  // "WAV", "MP3", etc.


// spec is optional, can be used as a hint as to how most your audio will be formatted. We will still accept any format, and passing a NULL spec is valid.
extern SDL_DECLSPEC MIX_Mixer * SDLCALL MIX_CreateMixerDevice(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec);  // we want a mixer that will generate audio directly to the audio device of our choice.
extern SDL_DECLSPEC MIX_Mixer * SDLCALL MIX_CreateMixer(const SDL_AudioSpec *spec);  // we want a mixer that will generate audio through an SDL_AudioStream we can consume from on-demand.
extern SDL_DECLSPEC void SDLCALL MIX_DestroyMixer(MIX_Mixer *mixer);
extern SDL_DECLSPEC SDL_PropertiesID SDLCALL MIX_GetMixerProperties(MIX_Mixer *mixer);

extern SDL_DECLSPEC bool SDLCALL MIX_GetMixerFormat(MIX_Mixer *mixer, SDL_AudioSpec *spec);   // what the device is actually expecting/what the mixer is generating.

// there is no difference between sounds and "music" now. They're all MIX_Audio objects.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadAudio_IO(MIX_Mixer *mixer, SDL_IOStream *io, bool predecode, bool closeio);
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadAudio(MIX_Mixer *mixer, const char *path, bool predecode);
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadAudioWithProperties(SDL_PropertiesID props);  // lets you specify things like "here's a path to MIDI instrument files outside of this file", etc.

#define MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER "SDL_mixer.audio.load.iostream"
#define MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN "SDL_mixer.audio.load.closeio"
#define MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN "SDL_mixer.audio.load.predecode"
#define MIX_PROP_AUDIO_LOAD_PREFERRED_MIXER_POINTER "SDL_mixer.audio.load.preferred_mixer"
#define MIX_PROP_AUDIO_LOAD_SKIP_METADATA_TAGS_BOOLEAN "SDL_mixer.audio.load.skip_metadata_tags"
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

// Load raw PCM data to a MIX_Audio. SDL_mixer will make a copy.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadRawAudio(MIX_Mixer *mixer, const void *data, size_t datalen, const SDL_AudioSpec *spec);

// Load raw PCM data to a MIX_Audio. SDL_mixer will NOT make a copy, it must live until the MIX_Audio is destroyed. if free_when_done, SDL_mixer will SDL_free(data) when the MIX_Audio is destroyed.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_LoadRawAudioNoCopy(MIX_Mixer *mixer, const void *data, size_t datalen, const SDL_AudioSpec *spec, bool free_when_done);

// just in case you need some audio to play, this will generate a sine wave forever when assigned to a playing Track.
extern SDL_DECLSPEC MIX_Audio * SDLCALL MIX_CreateSineWaveAudio(MIX_Mixer *mixer, int hz, float amplitude);


extern SDL_DECLSPEC SDL_PropertiesID SDLCALL MIX_GetAudioProperties(MIX_Audio *audio);  // we can store audio format-specific metadata in here (artist/album/etc info...)
extern SDL_DECLSPEC Sint64 MIX_GetAudioDuration(MIX_Audio *audio);
extern SDL_DECLSPEC bool MIX_GetAudioFormat(MIX_Audio *audio, SDL_AudioSpec *spec);

extern SDL_DECLSPEC void SDLCALL MIX_DestroyAudio(MIX_Audio *audio);  // reference-counted; if this is playing, it will be _actually_ destroyed when no longer in use.


// Tracks are your "channels" but they aren't static anymore. Just make as
// many as you like and destroy them as you like. If you want the old
// semantics, just make as many as you would have allocated "channels" and put
// them in an array somewhere.

extern SDL_DECLSPEC MIX_Track * SDLCALL MIX_CreateTrack(MIX_Mixer *mixer);
extern SDL_DECLSPEC void SDLCALL MIX_DestroyTrack(MIX_Track *track);  // will halt playback, if playing. Won't call Stopped callback, though. We assume you know.
extern SDL_DECLSPEC SDL_PropertiesID SDLCALL MIX_GetTrackProperties(MIX_Track *track);

extern SDL_DECLSPEC MIX_Mixer * SDLCALL MIX_GetTrackMixer(MIX_Track *track);

extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackAudio(MIX_Track *track, MIX_Audio *audio);  // Track will replace current audio with new one. If currently playing, will start playing new audio immediately.
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackAudioStream(MIX_Track *track, SDL_AudioStream *stream);  // insert anything you like into the mix. procedural audio, VoIP, data right from a microphone, etc. Will pull from AudioStream as needed instead of a MIX_Audio.
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackIOStream(MIX_Track *track, SDL_IOStream *io, bool closeio);

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


// positional audio...


/**
 * A set of per-channel gains for tracks using MIX_SetTrackStereo().
 *
 * When forcing a track to stereo, the app can specify a per-channel gain, to
 * further adjust the left or right outputs.
 *
 * When mixing audio that has been forced to stereo, each channel is modulated
 * by these values. A value of 1.0f produces no change, 0.0f produces silence.
 *
 * A simple panning effect would be to set `left` to the desired value and
 * `right` to `1.0f - left`.
 *
 * \since This struct is available since SDL_mixer 3.0.0.
 *
 * \sa MIX_SetTrackStereo
 */
typedef struct MIX_StereoGains
{
    float left;
    float right;
} MIX_StereoGains;

/**
 * Force a track to stereo output, with optionally left/right panning.
 *
 * This will cause the output of the track to convert to stereo, and then mix
 * it only onto the Front Left and Front Right speakers, regardless of the
 * speaker configuration. The left and right channels are modulated by
 * `gains`, which can be used to produce panning effects. This function may
 * be called to adjust the gains at any time.
 *
 * If `gains` is not NULL, this track will be switched into forced-stereo
 * mode. If `gains` is NULL, this will disable spatialization (both the
 * forced-stereo mode of this function and full 3D spatialization of
 * MIX_SetTrack3DPosition()).
 *
 * Negative gains are clamped to zero; there is no clamp for maximum, so one
 * could set the value > 1.0f to make a channel louder.
 *
 * The track's 3D position, reported by MIX_GetTrack3DPosition(), will be
 * reset to (0, 0, 0).
 *
 * \param track the track to adjust.
 * \param gains the per-channel gains, or NULL to disable spatialization.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 */
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrackStereo(MIX_Track *track, const MIX_StereoGains *gains);


typedef struct MIX_Point3D
{
    float x;
    float y;
    float z;
} MIX_Point3D;

// spatializes audio. Right handed coordinate system like OpenGL/OpenAL. Listener is always at 0,0,0. Audio will collapse down to mono and be positioned with distance attenuation. position=NULL turns off spatialization (default).
extern SDL_DECLSPEC bool SDLCALL MIX_SetTrack3DPosition(MIX_Track *track, const MIX_Point3D *position);
extern SDL_DECLSPEC bool SDLCALL MIX_GetTrack3DPosition(MIX_Track *track, MIX_Point3D *position);  // will return (0,0,0) if MIX_SetTrack3DPosition was given a NULL (or was never called for the track).


// groups...
extern SDL_DECLSPEC MIX_Group * SDLCALL MIX_CreateGroup(MIX_Mixer *mixer);
extern SDL_DECLSPEC void SDLCALL MIX_DestroyGroup(MIX_Group *group);
extern SDL_DECLSPEC SDL_PropertiesID SDLCALL MIX_GetGroupProperties(MIX_Group *group);
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

/**
 * An opaque object that represents an audio decoder.
 *
 * Most apps won't need this, as SDL_mixer's usual interfaces will decode
 * audio as needed. However, if one wants to decode an audio file into a
 * memory buffer without playing it, this interface offers that.
 *
 * These objects are created with MIX_CreateAudioDecoder() or
 * MIX_CreateAudioDecoder_IO(), and then can use MIX_DecodeAudio() to retrieve
 * the raw PCM data.
 *
 * \since This struct is available since SDL_mixer 3.0.0.
 */
typedef struct MIX_AudioDecoder MIX_AudioDecoder;

/**
 * Create a MIX_AudioDecoder from a path on the filesystem.
 *
 * Most apps won't need this, as SDL_mixer's usual interfaces will decode
 * audio as needed. However, if one wants to decode an audio file into a
 * memory buffer without playing it, this interface offers that.
 *
 * This function allows properties to be specified. This is intended to supply
 * file-specific settings, such as where to find SoundFonts for a MIDI file,
 * etc. In most cases, the caller should pass a zero to specify no extra
 * properties.
 *
 * When done with the audio decoder, it can be destroyed with
 * MIX_DestroyAudioDecoder().
 *
 * This function requires SDL_mixer to have been initialized with a successful
 * call to MIX_Init(), but does not need an actual MIX_Mixer to have been
 * created.
 *
 * \params path the filesystem path of the audio file to decode.
 * \params props file-specific properties needed for decoding. May be zero.
 * \returns an audio decoder, ready to decode.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 *
 * \sa MIX_CreateAudioDecoder_IO
 * \sa MIX_DecodeAudio
 * \sa MIX_DestroyAudioDecoder
 */
extern SDL_DECLSPEC MIX_AudioDecoder * SDLCALL MIX_CreateAudioDecoder(const char *path, SDL_PropertiesID props);

/**
 * Create a MIX_AudioDecoder from an SDL_IOStream.
 *
 * Most apps won't need this, as SDL_mixer's usual interfaces will decode
 * audio as needed. However, if one wants to decode an audio file into a
 * memory buffer without playing it, this interface offers that.
 *
 * This function allows properties to be specified. This is intended to supply
 * file-specific settings, such as where to find SoundFonts for a MIDI file,
 * etc. In most cases, the caller should pass a zero to specify no extra
 * properties.
 *
 * If `closeio` is true, then `io` will be closed when this decoder is done
 * with it. If this function fails and `closeio` is true, then `io` will be
 * closed before this function returns.
 *
 * When done with the audio decoder, it can be destroyed with
 * MIX_DestroyAudioDecoder().
 *
 * This function requires SDL_mixer to have been initialized with a successful
 * call to MIX_Init(), but does not need an actual MIX_Mixer to have been
 * created.
 *
 * \params io the i/o stream from which to decode data.
 * \params closeio if true, close the stream when done.
 * \params props file-specific properties needed for decoding. May be zero.
 * \returns an audio decoder, ready to decode.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 *
 * \sa MIX_CreateAudioDecoder_IO
 * \sa MIX_DecodeAudio
 * \sa MIX_DestroyAudioDecoder
 */
extern SDL_DECLSPEC MIX_AudioDecoder * SDLCALL MIX_CreateAudioDecoder_IO(SDL_IOStream *io, bool closeio, SDL_PropertiesID props);

/**
 * Destroy the specified audio decoder.
 *
 * Destroying a NULL MIX_AudioDecoder is a legal no-op.
 *
 * \param audiodecoder the audio to destroy.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 */
extern SDL_DECLSPEC void SDLCALL MIX_DestroyAudioDecoder(MIX_AudioDecoder *audiodecoder);

/**
 * Get the properties associated with a MIX_AudioDecoder.
 *
 * SDL_mixer offers some properties of its own, but this can also be a
 * convenient place to store app-specific data.
 *
 * A SDL_PropertiesID is created the first time this function is called for a
 * given MIX_AudioDecoder, if necessary.
 *
 * The file-specific metadata exposed through this function is identical to
 * those available through MIX_GetAudioProperties(). Please refer to that
 * function's documentation for details.
 *
 * \param audiodecoder the audio decoder to query.
 * \returns a valid property ID on success or 0 on failure; call
 *          SDL_GetError() for more information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 *
 * \sa MIX_GetAudioProperties
 */
extern SDL_DECLSPEC SDL_PropertiesID SDLCALL MIX_GetAudioDecoderProperties(MIX_AudioDecoder *audiodecoder);

/**
 * Query the initial audio format of a MIX_AudioDecoder.
 *
 * Note that some audio files can change format in the middle; some explicitly
 * support this, but a more common example is two MP3 files concatenated
 * together. In many cases, SDL_mixer will correctly handle these sort of
 * files, but this function will only report the initial format a file uses.
 *
 * \param audiodecoder the audio decoder to query.
 * \param spec on success, audio format details will be stored here.
 * \returns true on success or false on failure; call SDL_GetError() for more
 *          information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 */
extern SDL_DECLSPEC bool SDLCALL MIX_GetAudioDecoderFormat(MIX_AudioDecoder *audiodecoder, SDL_AudioSpec *spec);

/**
 * Decode more audio from a MIX_AudioDecoder.
 *
 * Data is decoded on demand in whatever format is requested. The format is
 * permitted to change between calls.
 *
 * This function will return the number of bytes decoded, which may be less
 * than requested if there was an error or end-of-file. A return value of zero
 * means the entire file was decoded, -1 means an unrecoverable error happened.
 *
 * \param audiodecoder the decoder from which to retrieve more data.
 * \param buffer the memory buffer to store decoded audio.
 * \param buflen the maximum number of bytes to store to `buffer`.
 * \param spec the format that audio data will be stored to `buffer`.
 * \returns number of bytes decoded, or -1 on error; call SDL_GetError() for
 *          more information.
 *
 * \threadsafety It is safe to call this function from any thread.
 *
 * \since This function is available since SDL_mixer 3.0.0.
 */
extern SDL_DECLSPEC int SDLCALL MIX_DecodeAudio(MIX_AudioDecoder *audiodecoder, void *buffer, int buflen, const SDL_AudioSpec *spec);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_MIXER_H_ */
