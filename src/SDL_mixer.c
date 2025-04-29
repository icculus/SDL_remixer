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

// !!! FIXME: figure out `int` vs Sint64/Uint64 metrics in all of this.

#include "SDL_mixer_internal.h"

static const MIX_Decoder *decoders[] = {
    &MIX_Decoder_VOC,
    &MIX_Decoder_WAV,
    &MIX_Decoder_AIFF,
    &MIX_Decoder_VORBIS,
    &MIX_Decoder_STBVORBIS,
    &MIX_Decoder_OPUS,
    &MIX_Decoder_FLAC,
    &MIX_Decoder_DRFLAC,
    &MIX_Decoder_TIMIDITY,
    &MIX_Decoder_FLUIDSYNTH,
    &MIX_Decoder_WAVPACK,
    &MIX_Decoder_GME,
    &MIX_Decoder_XMP,
    &MIX_Decoder_MPG123,
    &MIX_Decoder_DRMP3,
    &MIX_Decoder_SINEWAVE,
    &MIX_Decoder_RAW
};

static const MIX_Decoder *available_decoders[SDL_arraysize(decoders)];
static int num_available_decoders = 0;

typedef struct MIX_TagList
{
    MIX_Track **tracks;
    size_t num_tracks;
    size_t num_allocated;
    SDL_RWLock *rwlock;
} MIX_TagList;

static SDL_AudioDeviceID audio_device = 0;
static SDL_PropertiesID track_tags = 0;
static MIX_Track *all_tracks = NULL;
static MIX_Track *fire_and_forget_pool = NULL;  // these are also listed in all_tracks.
static MIX_Audio *all_audios = NULL;
static SDL_Mutex *mixer_sync_lock = NULL;

static bool CheckInitialized(void)
{
    if (!audio_device) {
        return SDL_SetError("Audio not opened");
    }
    return true;
}

static bool CheckTrackParam(MIX_Track *track)
{
    if (!CheckInitialized()) {
        return false;
    } else if (!track) {
        return SDL_InvalidParamError("track");
    }
    return true;
}

static bool CheckTagParam(const char *tag)
{
    if (!CheckInitialized()) {
        return false;
    } else if (!tag) {
        return SDL_InvalidParamError("tag");
    }
    return true;
}

static bool CheckAudioParam(MIX_Audio *audio)
{
    if (!CheckInitialized()) {
        return false;
    } else if (!audio) {
        return SDL_InvalidParamError("audio");
    }
    return true;
}

static bool CheckTrackTagParam(MIX_Track *track, const char *tag)
{
    if (!CheckInitialized()) {
        return false;
    } else if (!track) {
        return SDL_InvalidParamError("track");
    } else if (!tag) {
        return SDL_InvalidParamError("tag");
    }
    return true;
}

// this protects global mixer state, like linked lists of the MIX_Tracks.
static void LockMixerState(void)
{
    // this is just a convenient lock to protect mixer state, but this doesn't specifically have anything to do with tags.
    SDL_LockProperties(track_tags);
}

static void UnlockMixerState(void)
{
    // this is just a convenient lock to protect mixer state, but this doesn't specifically have anything to do with tags.
    SDL_UnlockProperties(track_tags);
}

// this makes sure the mixer is not mixing at this very moment, so you can change multiple tracks atomically.
static void LockMixerSync(void)
{
    SDL_LockMutex(mixer_sync_lock);
}

static void UnlockMixerSync(void)
{
    SDL_UnlockMutex(mixer_sync_lock);
}

static void SDLCALL AudioIterationStart(void *userdata, SDL_AudioDeviceID devid, bool start)
{
    LockMixerSync();
}

static void SDLCALL AudioIterationEnd(void *userdata, SDL_AudioDeviceID devid, bool start)
{
    UnlockMixerSync();
}


// this assumes LockTrack(track) was called before this.
static void TrackStopped(MIX_Track *track)
{
    SDL_assert(track->state != MIX_STATE_STOPPED);  // shouldn't be already stopped at this point.
    track->state = MIX_STATE_STOPPED;
    if (track->stopped_callback) {
        track->stopped_callback(track->stopped_callback_userdata, track);
    }
    if (track->fire_and_forget) {
        SDL_assert(!track->stopped_callback);  // these shouldn't have stopped callbacks.
        SDL_assert(track->state == MIX_STATE_STOPPED);  // should not have changed, shouldn't have a stopped_callback, etc.
        SDL_assert(track->fire_and_forget_next == NULL);  // shouldn't be in the list at all right now.
        MIX_SetTrackAudio(track, NULL);
        LockMixerState();
        track->fire_and_forget_next = fire_and_forget_pool;
        fire_and_forget_pool = track;
        UnlockMixerState();
    }
}

static void ApplyFade(MIX_Track *track, float *pcm, int frames)
{
    // !!! FIXME: this is probably pretty naive.

    if (track->fade_direction == 0) {
        return;  // no fade is happening, early exit.
    }

    const int to_be_faded = (int) SDL_min(track->fade_frames, frames);
    const int total_fade_frames = (int) track->total_fade_frames;
    int fade_frame_position = total_fade_frames - track->fade_frames;

    // some hacks to avoid a branch on each sample frame. Might not be a good idea in practice.
    const float pctmult = (track->fade_direction < 0) ? 1.0f : -1.0f;
    const float pctsub = (track->fade_direction < 0) ? 1.0f : 0.0f;
    const float ftotal_fade_frames = (float) total_fade_frames;
    const int channels = track->output_spec.channels;

    for (int i = 0; i < to_be_faded; i++) {
        const float pct = (pctsub - (((float) fade_frame_position) / ftotal_fade_frames)) * pctmult;
        SDL_assert(pct >= 0.0f);
        SDL_assert(pct <= 1.0f);
        fade_frame_position++;

        // use this fade percentage for the entire sample frame.
        switch (channels) {   // !!! FIXME: profile this and see if this is a dumb idea.
            case 8: *(pcm++) *= pct; SDL_FALLTHROUGH;
            case 7: *(pcm++) *= pct; SDL_FALLTHROUGH;
            case 6: *(pcm++) *= pct; SDL_FALLTHROUGH;
            case 5: *(pcm++) *= pct; SDL_FALLTHROUGH;
            case 4: *(pcm++) *= pct; SDL_FALLTHROUGH;
            case 3: *(pcm++) *= pct; SDL_FALLTHROUGH;
            case 2: *(pcm++) *= pct; SDL_FALLTHROUGH;
            case 1: *(pcm++) *= pct; break;

            default:  // catch any other number of channels.
                for (int j = 0; j < channels; j++) {
                    *(pcm++) *= pct;
                }
                break;
        }
    }

    track->fade_frames -= to_be_faded;
    SDL_assert(track->fade_frames >= 0);
    if (track->fade_frames == 0) {
        track->fade_direction = 0;  // fade is done.
    }
}

static bool DecodeMore(MIX_Track *track, int bytes_needed)
{
    SDL_assert(track->input_audio != NULL);

    bool retval = true;
    while (SDL_GetAudioStreamAvailable(track->input_stream) < bytes_needed) {
        if (!track->input_audio->decoder->decode(track->decoder_userdata, track->input_stream)) {
            SDL_FlushAudioStream(track->input_stream);  // make sure we read _everything_ now.
            retval = false;
            break;
        }
    }

    return retval;
}

static int FillSilenceFrames(MIX_Track *track, void *buffer, int buflen)
{
    SDL_assert(track->silence_frames > 0);
    SDL_assert(buflen > 0);
    const int channels = track->output_spec.channels;
    const int max_silence_bytes = (int) (track->silence_frames * channels * sizeof (float));
    const int br = SDL_min(buflen, max_silence_bytes);
    if (br) {
        SDL_memset(buffer, '\0', br);
        track->silence_frames -= br / (channels * sizeof (float));
    }
    return br;
}

// This is where the magic happens. This feeds data from a single track to
//  SDL's mixer. This happens periodically from SDL's audio device thread.
// This function assumes LockTrack() was called by the caller (since SDL's
//  mixer locks the audio stream, it's the same thing).
static void SDLCALL MixerCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    if (additional_amount == 0) {
        return;  // nothing to actually do yet. This was a courtesy call; the stream still has enough buffered.
    }

    MIX_Track *track = (MIX_Track *) userdata;
    SDL_assert(stream == track->output_stream);

    if (track->state != MIX_STATE_PLAYING) {
        return;  // paused or stopped, don't make progress.
    }

    // !!! FIXME: maybe we should do a consistent buffer size, to make this easier for app callbacks
    // !!! FIXME:  and save some trouble on systems that want to do like 200 samples at a time.

    // do we need to grow our buffer?
    if (additional_amount > track->input_buffer_len) {
        void *ptr = SDL_realloc(track->input_buffer, additional_amount);
        if (!ptr) {   // uhoh.
            TrackStopped(track);  // not much to be done, we're out of memory!
            return;
        }
        track->input_buffer = (Uint8 *) ptr;
        track->input_buffer_len = additional_amount;
    }

    float *pcm = (float *) track->input_buffer;  // we always work in float32 format.
    int bytes_remaining = additional_amount;

    // Calling TrackStopped() might have a stopped_callback that restarts the track, so don't break the loop
    //  for simply being stopped, so we can generate audio without gaps. If not restarted, track->state will no longer be PLAYING.
    while ((track->state == MIX_STATE_PLAYING) && (bytes_remaining > 0)) {
        bool end_of_audio = false;
        int br = 0;   // bytes read.

        if (track->silence_frames > 0) {
            br = FillSilenceFrames(track, pcm, bytes_remaining);
        } else if (track->input_stream) {
            if (track->input_audio) {
                DecodeMore(track, bytes_remaining);
            }
            br = SDL_GetAudioStreamData(track->input_stream, pcm, bytes_remaining);
        }

        // if input_audio and input_stream are both NULL, there's nothing to play (maybe they changed out the input on us?), br will be zero and we'll go to end_of_audio=true.

        if (br <= 0) {  // if 0: EOF. if < 0: decoding/input failure, we're done by default. But maybe it'll loop and play the start again...!
            end_of_audio = true;
        } else {
            SDL_assert(track->input_stream != NULL);  // should have data bound if you landed here.

            // this (probably?) shouldn't be a partial read here. It's either we completely filled the buffer or exhausted the data.
            //  as such, this does mix_callback() as likely the entire buffer, or all we're getting before a finish callback would have to fire,
            //  even if the finish callback would restart the track. As such, the outer loop is mostly here to deal with looping tracks
            //  and finish callbacks that restart the track.

            // if this would put us past the end of maxframes, or a fadeout, clamp br and set end_of_audio=true so we can do looping, etc.
            Sint64 maxpos = -1;
            if (track->max_frames > 0) {
                maxpos = track->max_frames;
            }
            if (track->fade_direction < 0) {
                const Sint64 maxfadepos = (Sint64) (track->position + track->fade_frames);
                if ((maxpos < 0) || (maxfadepos < maxpos)) {
                    maxpos = maxfadepos;
                }
            }

            const int channels = track->output_spec.channels;

            int frames_read = br / (sizeof (float) * channels);
            if (maxpos >= 0) {
                const Uint64 newpos = track->position + frames_read;
                if (newpos >= maxpos) {  // we read past the end of the fade out or maxframes, we need to clamp.
                    br -= ((newpos - maxpos) * channels) * sizeof (float);
                    frames_read = br / (sizeof (float) * channels);
                    end_of_audio = true;
                }
            }

            // give the app a shot at the final buffer before sending it on for mixing
            const int samples = frames_read * channels;

            if (track->mix_callback) {
                track->mix_callback(track->mix_callback_userdata, track, &track->output_spec, pcm, samples);
            }

            ApplyFade(track, pcm, frames_read);

            const int put_bytes = samples * sizeof (float);
            SDL_PutAudioStreamData(stream, pcm, put_bytes);

            track->position += frames_read;
            bytes_remaining -= put_bytes;
        }

        // remember that the callback in TrackStopped() might restart this track,
        //  so we'll loop to see if we can fill in more audio without a gap even in that case.
        if (end_of_audio) {
            bool track_stopped = false;
            if (track->loops_remaining == 0) {
                if (track->silence_frames < 0) {
                    track->silence_frames = -track->silence_frames;  // time to start appending silence.
                } else {
                    track_stopped = true;  // out of data, no loops remain, no appended silence left, we're done.
                }
            } else {
                if (track->loops_remaining > 0) {  // negative means infinite loops, so don't decrement for that.
                    track->loops_remaining--;
                }
                if (!track->input_audio) {  // can't loop on a streaming input, you're done.
                    track_stopped = true;
                } else {
                    if (!track->input_audio->decoder->seek(track->decoder_userdata, track->loop_start)) {
                        track_stopped = true;  // uhoh, can't seek! Abandon ship!
                    } else {
                        track->position = track->loop_start;
                    }
                }
            }

            if (track_stopped) {
                TrackStopped(track);
            }
        }
    }
}

static void InitDecoders(void)
{
    for (int i = 0; i < SDL_arraysize(decoders); i++) {
        const MIX_Decoder *decoder = decoders[i];
        if (!decoder->init || decoder->init()) {
            available_decoders[num_available_decoders++] = decoder;
        }
    }
}

static void QuitDecoders(void)
{
    for (int i = 0; i < num_available_decoders; i++) {
        if (available_decoders[i]->quit) {
            available_decoders[i]->quit();
        }
        available_decoders[i] = NULL;
    }
    num_available_decoders = 0;
}

bool MIX_OpenMixer(SDL_AudioDeviceID devid, const SDL_AudioSpec *spec)
{
    if (CheckInitialized()) {
        return SDL_SetError("Audio is already open");
    }

    // This calls SDL_Init(SDL_INIT_AUDIO) for each open, because SDL initialization is reference-counted.
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        return false;
    }

    SDL_assert(!audio_device);
    audio_device = SDL_OpenAudioDevice(devid, spec);
    if (!audio_device) {
        goto failed;
    }

    // !!! FIXME: if we allow multiple opens, only the first one should init decoders.
    InitDecoders();

    SDL_assert(!track_tags);
    track_tags = SDL_CreateProperties();
    if (!track_tags) {
        goto failed;
    }

    SDL_assert(!mixer_sync_lock);
    mixer_sync_lock = SDL_CreateMutex();
    if (!mixer_sync_lock) {
        goto failed;
    }

    if (!SDL_SetAudioIterationCallbacks(audio_device, AudioIterationStart, AudioIterationEnd, NULL)) {
        goto failed;
    }

    return true;

failed:
    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    if (track_tags) {
        SDL_DestroyProperties(track_tags);
        track_tags = 0;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;

}

void MIX_CloseMixer(void)
{
    if (audio_device) {
        MIX_StopAllTracks(0);

        while (all_tracks) {
            MIX_DestroyTrack(all_tracks);
        }

        while (all_audios) {
            MIX_DestroyAudio(all_audios);
        }

        fire_and_forget_pool = NULL;  // these were also in all_tracks, so they're already destroyed.

        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_DestroyProperties(track_tags);
        track_tags = 0;
        SDL_DestroyMutex(mixer_sync_lock);
        mixer_sync_lock = NULL;

        QuitDecoders();  // !!! FIXME: if we allow multiple opens, the last one to close should call this. Or we could re-add MIX_Init().
    }
}

int MIX_GetNumAudioDecoders(void)
{
    return CheckInitialized() ? num_available_decoders : -1;
}

const char *MIX_GetAudioDecoder(int index)
{
    if (!CheckInitialized()) {
        return NULL;
    } else if ((index < 0) || (index >= num_available_decoders)) {
        SDL_InvalidParamError("index");
        return NULL;
    }
    return available_decoders[index]->name;
}

bool MIX_GetDeviceSpec(SDL_AudioSpec *spec)
{
    if (!CheckInitialized()) {
        return false;
    } else if (!spec) {
        return SDL_InvalidParamError("spec");
    }
    return SDL_GetAudioDeviceFormat(audio_device, spec, NULL);
}

static const MIX_Decoder *PrepareDecoder(SDL_IOStream *io, SDL_AudioSpec *spec, SDL_PropertiesID props, Sint64 *duration_frames, void **audio_userdata)
{
    const char *decoder_name = SDL_GetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, NULL);

    SDL_AudioSpec original_spec;
    SDL_copyp(&original_spec, spec);

    for (int i = 0; i < num_available_decoders; i++) {
        const MIX_Decoder *decoder = available_decoders[i];
        if (!decoder_name || (SDL_strcasecmp(decoder->name, decoder_name) == 0)) {
            if (decoder->init_audio(io, spec, props, duration_frames, audio_userdata)) {
                return decoder;
            } else if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) == -1) {   // note this seeks to offset 0, because we're using an IoClamp.
                SDL_SetError("Can't seek in stream to find proper decoder");
                return NULL;
            }
            SDL_copyp(spec, &original_spec);  // reset this, in case init_audio changed it and then failed.
        }
    }

    SDL_SetError("Audio data is in unknown/unsupported/corrupt format");
    return NULL;
}

static void *DecodeWholeFile(const MIX_Decoder *decoder, void *audio_userdata, const SDL_AudioSpec *spec, SDL_PropertiesID props, size_t *decoded_len)
{
    size_t bytes_decoded = 0;
    Uint8 *decoded = NULL;
    SDL_AudioStream *stream = SDL_CreateAudioStream(spec, spec);   // !!! FIXME: if we're decoding up front, we might as well convert to float here too, right?
    if (stream) {
        void *userdata = NULL;
        if (decoder->init_track(audio_userdata, spec, props, &userdata)) {
            while (decoder->decode(userdata, stream)) {
                // spin.
            }
            decoder->quit_track(userdata);

            SDL_FlushAudioStream(stream);
            const int available = SDL_GetAudioStreamAvailable(stream);
            decoded = (Uint8 *) SDL_malloc(available);
            if (decoded) {
                const int rc = SDL_GetAudioStreamData(stream, decoded, available);
                SDL_assert((rc < 0) || (rc == available));
                if (rc < 0) {
                    SDL_free(decoded);
                    decoded = NULL;
                } else {
                    bytes_decoded = (size_t) available;
                }
            }
        }
        SDL_DestroyAudioStream(stream);
    }

    *decoded_len = bytes_decoded;
    return decoded;
}

MIX_Audio *MIX_LoadAudioWithProperties(SDL_PropertiesID props)  // lets you specify things like "here's a path to MIDI instrument data outside of this file", etc.
{
    if (!CheckInitialized()) {
        return NULL;
    }

    SDL_IOStream *origio = (SDL_IOStream *) SDL_GetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, NULL);
    const bool closeio = SDL_GetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, false);
    const bool predecode = SDL_GetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN, false);
    void *audio_userdata = NULL;
    const MIX_Decoder *decoder = NULL;
    SDL_IOStream *io = NULL;
    MIX_IoClamp clamp;
    Sint64 duration_frames = MIX_DURATION_UNKNOWN;

    MIX_Audio *audio = (MIX_Audio *) SDL_calloc(1, sizeof (*audio));
    if (!audio) {
        goto failed;
    }

    audio->props = SDL_CreateProperties();
    if (!audio->props) {
        goto failed;
    }

    if (props && !SDL_CopyProperties(props, audio->props)) {
        goto failed;
    }

    // check for ID3/APE/MusicMatch/whatever tags here, in case they were slapped onto the edge of any random file format.
    // !!! FIXME: add a property to skip tag detection, for apps that know they don't have them, or don't want them, and want to save some CPU and i/o.
    if (origio) {
        io = MIX_OpenIoClamp(&clamp, origio);
        if (!io) {
            goto failed;
        }

        // !!! FIXME: currently we're ignoring return values from this function (see FIXME at the top of its code).
        MIX_ReadMetadataTags(io, audio->props, &clamp);
        if (SDL_SeekIO(io, 0, SDL_IO_SEEK_SET) < 0) {
            goto failed;
        }
    }

    // the decoder sets audio->spec to whatever it's actually providing, but we pass the current hardware setting in, in case that's useful for
    // things that generate audio in whatever format (for example, a MIDI decoder is going to generate PCM from "notes", so it can do it at any
    // sample rate, so it might as well do it at device format to avoid an unnecessary resample later).
    if (!MIX_GetDeviceSpec(&audio->spec)) {
        audio->spec.channels = 2;   // eh, just set a reasonable default...
        audio->spec.freq = 44100;
    }
    audio->spec.format = SDL_AUDIO_F32;  // we always want to favor float32 for our own purposes, regardless of the hardware settings.

    decoder = PrepareDecoder(io, &audio->spec, audio->props, &duration_frames, &audio_userdata);
    if (!decoder) {
        goto failed;
    }

    if (io) {
        SDL_CloseIO(io);  // IoClamp's close doesn't close the original stream, but we still need to free its resources here.
        io = NULL;
    }

    if (closeio) {
        SDL_CloseIO(origio);
        origio = NULL;
        SDL_ClearProperty(audio->props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER);
    }

    // set this before predecoding might change `decoder` to the RAW implementation.
    SDL_SetStringProperty(audio->props, MIX_PROP_AUDIO_DECODER_STRING, decoder->name);

    // if this is already raw data, predecoding is just going to make a copy of it, so skip it.
    if (predecode && (decoder->decode != MIX_RAW_decode) && (duration_frames != MIX_DURATION_INFINITE)) {
        size_t decoded_len = 0;
        void *decoded = DecodeWholeFile(decoder, audio_userdata, &audio->spec, audio->props, &decoded_len);
        if (!decoded) {
            goto failed;
        }
        decoder->quit_audio(audio_userdata);
        decoder = NULL;
        audio_userdata = MIX_RAW_InitFromMemoryBuffer(decoded, decoded_len, &audio->spec, &duration_frames, true);
        if (audio_userdata) {
            decoder = &MIX_Decoder_RAW;
        } else {
            goto failed;
        }
    }

    audio->decoder = decoder;
    audio->decoder_userdata = audio_userdata;

    if (duration_frames >= 0) {
        SDL_SetNumberProperty(audio->props, MIX_PROP_METADATA_DURATION_FRAMES_NUMBER, duration_frames);
    } else if (duration_frames == MIX_DURATION_INFINITE) {
        SDL_SetBooleanProperty(audio->props, MIX_PROP_METADATA_DURATION_INFINITE_BOOLEAN, true);
    }

    SDL_AtomicIncRef(&audio->refcount);

    LockMixerState();
    audio->next = all_audios;
    if (all_audios) {
        all_audios->prev = audio;
    }
    all_audios = audio;
    UnlockMixerState();

    return audio;

failed:
    if (decoder) {
        decoder->quit_audio(audio_userdata);
    }

    if (audio) {
        if (audio->props) {
            SDL_DestroyProperties(audio->props);
        }
        SDL_free(audio);
    }

    if (io) {
        SDL_CloseIO(io);  // IoClamp's close doesn't close the original stream, but we still need to free its resources here.
    }

    if (origio && closeio) {
        SDL_CloseIO(origio);
    }

    return NULL;
}

MIX_Audio *MIX_LoadAudio_IO(SDL_IOStream *io, bool predecode, bool closeio)
{
    if (!io) {
        SDL_InvalidParamError("io");
        return NULL;
    }

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, io);
    SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN, predecode);
    SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, closeio);
    MIX_Audio *audio = MIX_LoadAudioWithProperties(props);
    SDL_DestroyProperties(props);
    return audio;
}

MIX_Audio *MIX_LoadAudio(const char *path, bool predecode)
{
    if (!path) {
        SDL_InvalidParamError("path");
        return NULL;
    }

    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    MIX_Audio *retval = NULL;
    if (io) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, MIX_PROP_AUDIO_LOAD_PATH_STRING, path);
        SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, io);
        SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN, predecode);
        SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, true);
        retval = MIX_LoadAudioWithProperties(props);
        SDL_DestroyProperties(props);
    }

    return retval;
}

MIX_Audio *MIX_LoadRawAudio_IO(SDL_IOStream *io, const SDL_AudioSpec *spec, bool closeio)
{
    if (!io) {
        SDL_InvalidParamError("io");
        return NULL;
    }

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, "RAW");
    SDL_SetNumberProperty(props, MIX_PROP_DECODER_FORMAT_NUMBER, (Sint64) spec->format);
    SDL_SetNumberProperty(props, MIX_PROP_DECODER_CHANNELS_NUMBER, (Sint64) spec->channels);
    SDL_SetNumberProperty(props, MIX_PROP_DECODER_FREQ_NUMBER, (Sint64) spec->freq);
    SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, io);
    SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, closeio);
    MIX_Audio *audio = MIX_LoadAudioWithProperties(props);
    SDL_DestroyProperties(props);
    return audio;
}

MIX_Audio *MIX_LoadRawAudio(const void *data, size_t datalen, const SDL_AudioSpec *spec, bool free_when_done)
{
    if (!CheckInitialized()) {
        return NULL;
    } else if (!data) {
        SDL_InvalidParamError("data");
        return NULL;
    } else if (!spec) {
        SDL_InvalidParamError("spec");
        return NULL;
    }

    MIX_Audio *audio = (MIX_Audio *) SDL_calloc(1, sizeof (*audio));
    if (!audio) {
        return NULL;
    }

    audio->props = SDL_CreateProperties();
    if (!audio->props) {
        SDL_free(audio);
        return NULL;
    }

    SDL_SetStringProperty(audio->props, MIX_PROP_AUDIO_DECODER_STRING, "RAW");

    Sint64 duration_frames = MIX_DURATION_UNKNOWN;
    audio->decoder_userdata = MIX_RAW_InitFromMemoryBuffer(data, datalen, spec, &duration_frames, free_when_done);
    if (!audio->decoder_userdata) {
        SDL_DestroyProperties(audio->props);
        SDL_free(audio);
        return NULL;
    }

    SDL_assert(duration_frames >= 0);
    SDL_SetNumberProperty(audio->props, MIX_PROP_METADATA_DURATION_FRAMES_NUMBER, duration_frames);

    audio->decoder = &MIX_Decoder_RAW;
    SDL_copyp(&audio->spec, spec);
    SDL_AtomicIncRef(&audio->refcount);

    LockMixerState();
    audio->next = all_audios;
    if (all_audios) {
        all_audios->prev = audio;
    }
    all_audios = audio;
    UnlockMixerState();

    return audio;
}

MIX_Audio *MIX_CreateSineWaveAudio(int hz, float amplitude)
{
    if (!CheckInitialized()) {
        return NULL;
    } else if (hz <= 0) {
        SDL_InvalidParamError("hz");
        return NULL;
    } else if ((amplitude < 0.0f) || (amplitude > 1.0f)) {
        SDL_InvalidParamError("amplitude");
        return NULL;
    }

    const SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        return NULL;
    }

    SDL_SetStringProperty(props, MIX_PROP_AUDIO_DECODER_STRING, "SINEWAVE");
    SDL_SetNumberProperty(props, MIX_PROP_DECODER_SINEWAVE_HZ_NUMBER, hz);
    SDL_SetFloatProperty(props, MIX_PROP_DECODER_SINEWAVE_AMPLITUDE_FLOAT, amplitude);
    MIX_Audio *audio = MIX_LoadAudioWithProperties(props);
    SDL_DestroyProperties(props);
    return audio;
}

SDL_PropertiesID MIX_GetAudioProperties(MIX_Audio *audio)
{
    if (!CheckAudioParam(audio)) {
        return 0;
    }

    if (audio->props == 0) {
        audio->props = SDL_CreateProperties();
    }
    return audio->props;
}

static void RefAudio(MIX_Audio *audio)
{
    if (audio) {
        SDL_AtomicIncRef(&audio->refcount);
    }
}

static void UnrefAudio(MIX_Audio *audio)
{
    if (audio && SDL_AtomicDecRef(&audio->refcount)) {
        LockMixerState();
        if (audio->prev == NULL) {
            all_audios = audio->next;
        }
        if (audio->prev) {
            audio->prev->next = audio->next;
        }
        if (audio->next) {
            audio->next->prev = audio->prev;
        }
        UnlockMixerState();

        if (audio->decoder) {
            audio->decoder->quit_audio(audio->decoder_userdata);
        }
        if (audio->props) {
            SDL_DestroyProperties(audio->props);
        }
        SDL_free(audio);
    }
}

void MIX_DestroyAudio(MIX_Audio *audio)
{
    if (CheckAudioParam(audio)) {
        UnrefAudio(audio);
    }
}

MIX_Track *MIX_CreateTrack(void)
{
    if (!CheckInitialized()) {
        return NULL;
    }

    MIX_Track *track = (MIX_Track *) SDL_calloc(1, sizeof (*track));
    if (!track) {
        return NULL;
    }

    track->tags = SDL_CreateProperties();
    if (!track->tags) {
        SDL_free(track);
        return NULL;
    }

    // just _something_ so there's a valid input spec until real data is assigned.
    const SDL_AudioSpec spec = { SDL_AUDIO_F32, 1, 48000 };
    track->output_stream = SDL_CreateAudioStream(&spec, NULL);
    if (!track->output_stream) {
        SDL_DestroyProperties(track->tags);
        SDL_free(track);
        return NULL;
    }

    SDL_SetAudioStreamGetCallback(track->output_stream, MixerCallback, track);

    LockMixerState();
    track->next = all_tracks;
    if (all_tracks) {
        all_tracks->prev = track;
    }
    all_tracks = track;
    UnlockMixerState();

    if (!SDL_BindAudioStream(audio_device, track->output_stream)) {
        char *err = SDL_strdup(SDL_GetError());   // save this off in case destruction changes it.
        MIX_DestroyTrack(track);  // just destroy it normally, it was otherwise initialized.
        if (!err) {
            SDL_OutOfMemory();
        } else {
            SDL_SetError("%s", err);
            SDL_free(err);
        }
        return NULL;
    }

    return track;
}

static void SDLCALL UntagWholeTrack(void *userdata, SDL_PropertiesID props, const char *tag)
{
    MIX_Track *track = (MIX_Track *) userdata;
    SDL_assert(track->tags == props);
    if (SDL_GetBooleanProperty(props, tag, false)) {  // these still exist in track->tags once untagged, so only bother if set to true.
        MIX_UntagTrack(track, tag);
    }
}

void MIX_DestroyTrack(MIX_Track *track)
{
    if (!CheckTrackParam(track)) {
        return;
    }

    LockMixerState();
    if (track->prev == NULL) {
        all_tracks = track->next;
    }
    if (track->prev) {
        track->prev->next = track->next;
    }
    if (track->next) {
        track->next->prev = track->prev;
    }
    // we don't check the fire-and-forget pool because that is only free'd, with all_tracks, when closing the mixer.
    UnlockMixerState();

    SDL_UnbindAudioStream(track->output_stream);
    SDL_DestroyAudioStream(track->output_stream);

    if (track->input_audio) {
        track->input_audio->decoder->quit_track(track->decoder_userdata);
    }

    SDL_DestroyAudioStream(track->internal_stream);

    UnrefAudio(track->input_audio);
    SDL_EnumerateProperties(track->tags, UntagWholeTrack, track);
    SDL_DestroyProperties(track->tags);
    SDL_free(track->input_buffer);
    SDL_free(track);
}

static void LockTrack(MIX_Track *track)
{
    SDL_assert(track != NULL);
    SDL_assert(track->output_stream != NULL);
    SDL_LockAudioStream(track->output_stream);
}

static void UnlockTrack(MIX_Track *track)
{
    SDL_assert(track != NULL);
    SDL_assert(track->output_stream != NULL);
    SDL_UnlockAudioStream(track->output_stream);
}

bool MIX_SetTrackAudio(MIX_Track *track, MIX_Audio *audio)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    SDL_AudioSpec spec;
    if (audio) {
        SDL_copyp(&spec, &audio->spec);
    } else {
        // make this reasonable, but in theory we shouldn't touch it again.
        spec.freq = 44100;
        spec.channels = 2;
    }
    spec.format = SDL_AUDIO_F32;  // we always work in float32.

    LockTrack(track);

    if (audio && (track->internal_stream == NULL)) {
        track->internal_stream = SDL_CreateAudioStream(&audio->spec, &spec);
        if (!track->internal_stream) {
            UnlockTrack(track);
            return false;
        }
    }

    if (track->input_audio) {
        track->input_audio->decoder->quit_track(track->decoder_userdata);
        UnrefAudio(track->input_audio);
    }

    track->input_audio = NULL;
    track->input_stream = NULL;

    bool retval = true;
    if (audio) {
        retval = audio->decoder->init_track(audio->decoder_userdata, &audio->spec, audio->props, &track->decoder_userdata);
        if (retval) {
            RefAudio(audio);
            SDL_SetAudioStreamFormat(track->internal_stream, &audio->spec, &spec);   // input is from decoded audio, output is to output_stream
            SDL_SetAudioStreamFormat(track->output_stream, &spec, NULL);   // input is from internal_stream, output is to device, so leave that side NULL for SDL to control.
            SDL_copyp(&track->output_spec, &spec);
            track->input_audio = audio;
            track->input_stream = track->internal_stream;
            track->position = 0;
        }
    }

    UnlockTrack(track);

    return retval;
}

bool MIX_SetTrackAudioStream(MIX_Track *track, SDL_AudioStream *stream)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    LockTrack(track);

    if (track->input_audio) {
        track->input_audio->decoder->quit_track(track->decoder_userdata);
        UnrefAudio(track->input_audio);
        track->input_audio = NULL;
    }

    SDL_GetAudioStreamFormat(stream, &track->output_spec, NULL);
    track->output_spec.format = SDL_AUDIO_F32;  // we always work in float32.
    SDL_SetAudioStreamFormat(stream, NULL, &track->output_spec);                 // input is whatever, output is whatever in float format.
    SDL_SetAudioStreamFormat(track->output_stream, &track->output_spec, NULL);   // input is whatever in float format, output is whatever the audio device wants.
    track->input_stream = stream;
    track->position = 0;
    UnlockTrack(track);

    return true;
}

static void SDLCALL CleanupTagList(void *userdata, void *value)
{
    MIX_TagList *list = (MIX_TagList *) value;
    SDL_DestroyRWLock(list->rwlock);
    SDL_free(list->tracks);
    SDL_free(list);
}

// assumes inputs are valid and track_tags's lock is held.
static MIX_TagList *CreateTagList(const char *tag)
{
    SDL_assert(track_tags != 0);

    SDL_LockProperties(track_tags);

    MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);  // check that something didn't beat us here while we waited on the lock.
    if (!list) {
        list = (MIX_TagList *) SDL_calloc(1, sizeof (*list));
        if (list) {
            list->num_allocated = 4;
            list->tracks = (MIX_Track **) SDL_calloc(list->num_allocated, sizeof (*list->tracks));
            list->rwlock = SDL_CreateRWLock();
            if (!list->tracks || !list->rwlock) {
                SDL_free(list->tracks);
                if (list->rwlock) {
                    SDL_DestroyRWLock(list->rwlock);
                }
                SDL_free(list);
                list = NULL;
            }
        }

        if (list && !SDL_SetPointerPropertyWithCleanup(track_tags, tag, list, CleanupTagList, NULL)) {
            SDL_DestroyRWLock(list->rwlock);
            SDL_free(list->tracks);
            SDL_free(list);
            list = NULL;
        }
    }

    SDL_UnlockProperties(track_tags);

    return list;
}

bool MIX_TagTrack(MIX_Track *track, const char *tag)
{
    if (!CheckTrackTagParam(track, tag)) {
        return false;
    }

    SDL_LockProperties(track->tags);
    if (!SDL_GetBooleanProperty(track->tags, tag, false)) {
        if (!SDL_SetBooleanProperty(track->tags, tag, true)) {
            SDL_UnlockProperties(track->tags);
            return false;
        }

        SDL_assert(track_tags != 0);
        MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);
        if (!list) {
            list = CreateTagList(tag);
            if (!list) {
                SDL_UnlockProperties(track->tags);
                SDL_SetBooleanProperty(track->tags, tag, false);
                return false;
            }
        }

        SDL_LockRWLockForWriting(list->rwlock);
        if (list->num_tracks >= list->num_allocated) {
            void *ptr = SDL_realloc(list->tracks, sizeof (*list->tracks) * (list->num_allocated * 2));
            if (!ptr) {
                SDL_UnlockRWLock(list->rwlock);
                SDL_SetBooleanProperty(track->tags, tag, false);
                SDL_UnlockProperties(track->tags);
                return false;
            }
            list->tracks = ptr;
            list->num_allocated *= 2;
        }
        list->tracks[list->num_tracks++] = track;
        SDL_UnlockRWLock(list->rwlock);
    }
    SDL_UnlockProperties(track->tags);

    return true;
}


void MIX_UntagTrack(MIX_Track *track, const char *tag)
{
    if (CheckTrackTagParam(track, tag)) {
        return;  // do nothing.
    }

    SDL_LockProperties(track->tags);
    if (SDL_GetBooleanProperty(track->tags, tag, false)) {  // if tag isn't there, nothing to do.
        if (SDL_SetBooleanProperty(track->tags, tag, false)) {
            SDL_assert(track_tags != 0);
            MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);
            SDL_assert(list != NULL);  // shouldn't be NULL, there's definitely an track with this tag!

            SDL_LockRWLockForWriting(list->rwlock);
            for (size_t i = 0; i < list->num_tracks; i++) {
                if (list->tracks[i] == track) {
                    const size_t cpy = (list->num_tracks - i) * sizeof (*list->tracks);
                    if (cpy) {
                        SDL_memmove(&list->tracks[i], list->tracks[i+1], cpy);
                    }
                    list->tracks[--list->num_tracks] = NULL;
                    break;
                }
            }
            SDL_UnlockRWLock(list->rwlock);
        }
    }
    SDL_UnlockProperties(track->tags);
}

bool MIX_SetTrackPlaybackPosition(MIX_Track *track, Uint64 frames)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    bool retval = true;

    // !!! FIXME: should it be legal to seek past the end of an track (so it just stops immediately, or maybe stops on next callback)?
    LockTrack(track);
    if (!track->input_audio) {
        if (track->input_stream) {  // can't seek a stream that was set up with MIX_SetTrackAudioStream.
            retval = SDL_SetError("Can't seek a streaming track");
        } else {
            retval = SDL_SetError("No audio currently assigned to this track");
        }
    } else {
        retval = track->input_audio->decoder->seek(track->decoder_userdata, frames);
        if (retval) {
            track->position = frames;
        }
    }
    UnlockTrack(track);

    return retval;
}

Uint64 MIX_GetTrackPlaybackPosition(MIX_Track *track)
{
    Uint64 retval = 0;
    if (CheckTrackParam(track)) {
        LockTrack(track);
        retval = track->position;
        UnlockTrack(track);
    }
    return retval;
}

Uint64 MIX_MSToFrames(int sample_rate, Uint64 ms)
{
    return (Uint64) ((((double) ms) / 1000.0) * ((double) sample_rate));
}

Uint64 MIX_FramesToMS(int sample_rate, Uint64 frames)
{
    return (Uint64) ((((double) frames) / ((double) sample_rate)) * 1000.0);
}

Uint64 MIX_TrackMSToFrames(MIX_Track *track, Uint64 ms)
{
    Uint64 retval = 0;
    if (CheckTrackParam(track)) {
        LockTrack(track);
        SDL_AudioSpec spec;
        if (track->input_stream) {
            SDL_GetAudioStreamFormat(track->input_stream, &spec, NULL);
        } else {
            spec.freq = 0;
        }
        UnlockTrack(track);
        if (spec.freq) {
            retval = MIX_MSToFrames(spec.freq, ms);
        }
    }
    return retval;
}

Uint64 MIX_TrackFramesToMS(MIX_Track *track, Uint64 frames)
{
    Uint64 retval = 0;
    if (CheckTrackParam(track)) {
        LockTrack(track);
        SDL_AudioSpec spec;
        if (track->input_stream) {
            SDL_GetAudioStreamFormat(track->input_stream, &spec, NULL);
        } else {
            spec.freq = 0;
        }
        UnlockTrack(track);
        if (spec.freq) {
            retval = MIX_FramesToMS(spec.freq, frames);
        }
    }
    return retval;
}

Uint64 MIX_AudioMSToFrames(MIX_Audio *audio, Uint64 ms)
{
    if (!CheckAudioParam(audio)) {
        return 0;
    }
    return MIX_MSToFrames(audio->spec.freq, ms);
}

Uint64 MIX_AudioFramesToMS(MIX_Audio *audio, Uint64 frames)
{
    if (!CheckAudioParam(audio)) {
        return 0;
    }
    return MIX_FramesToMS(audio->spec.freq, frames);
}

bool MIX_PlayTrack(MIX_Track *track, Sint64 maxFrames, int loops, Sint64 startpos, Sint64 loop_start, Sint64 fadeIn, Sint64 append_silence_frames)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    bool retval = true;

    LockTrack(track);
    if (!track->input_audio && !track->input_stream) {
        retval = SDL_SetError("No audio currently assigned to this track");
    } else if (!track->input_audio && (startpos != 0)) {
        retval = SDL_SetError("Playing an input stream (not MIX_Audio) with a non-zero startpos");  // !!! FIXME: should we just read off this many frames right now instead?
    } else if (track->input_audio && (!track->input_audio->decoder->seek(track->decoder_userdata, startpos))) {
        retval = false;
    } else {
        track->max_frames = maxFrames;
        track->loops_remaining = loops;
        track->loop_start = loop_start;
        track->total_fade_frames = (fadeIn > 0) ? fadeIn : 0;
        track->fade_frames = track->total_fade_frames;
        track->fade_direction = (fadeIn > 0) ? 1 : 0;
        track->silence_frames = (append_silence_frames > 0) ? -append_silence_frames : 0;  // negative means "there is still actual audio data to play", positive means "we're done with actual data, feed silence now." Zero means no silence (left) to feed.
        track->state = MIX_STATE_PLAYING;
        track->position = startpos;
    }

    UnlockTrack(track);

    return retval;
}

bool MIX_PlayTag(const char *tag, Sint64 maxTicks, int loops, Sint64 fadeIn)
{
    if (!CheckTagParam(tag)) {
        return false;
    }

    MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);
    if (!list) {
        return true;  // nothing is using this tag, do nothing (but not an error).
    }

    bool retval = true;
    SDL_LockRWLockForReading(list->rwlock);
    const size_t total = list->num_tracks;
    for (size_t i = 0; i < total; i++) {
        MIX_Track *track = list->tracks[i];
        LockTrack(track);
        if (!MIX_PlayTrack(track,
                           (maxTicks > 0) ? MIX_TrackMSToFrames(track, maxTicks) : -1,
                           loops, 0, 0,
                           (fadeIn > 0) ? MIX_TrackMSToFrames(track, fadeIn) : -1, 0)) {
            retval = false;
        }
        UnlockTrack(track);
    }

    SDL_UnlockRWLock(list->rwlock);

    return retval;
}

bool MIX_PlayAudio(MIX_Audio *audio)
{
    if (!CheckAudioParam(audio)) {
        return false;
    }

    // grab an existing fire-and-forget track from the available pool.
    LockMixerState();
    MIX_Track *track = fire_and_forget_pool;
    if (track) {
        fire_and_forget_pool = track->fire_and_forget_next;
        track->fire_and_forget_next = NULL;
    }
    UnlockMixerState();

    if (!track) {  // make a new item if the pool was empty.
        track = MIX_CreateTrack();
        if (!track) {
            return false;
        }
        track->fire_and_forget = true;
    }

    MIX_SetTrackAudio(track, audio);
    return MIX_PlayTrack(track, -1, 0, 0, 0, 0, 0);
}

static void StopTrack(MIX_Track *track, Sint64 fadeOut)
{
    LockTrack(track);
    if (track->state != MIX_STATE_STOPPED) {
        if (fadeOut <= 0) {  // stop immediately.
            TrackStopped(track);
        } else {
            track->total_fade_frames = fadeOut;
            track->fade_frames = track->total_fade_frames;
            track->fade_frames = fadeOut;
            track->fade_direction = -1;
        }
    }
    UnlockTrack(track);
}

bool MIX_StopTrack(MIX_Track *track, Sint64 fadeOut)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    StopTrack(track, fadeOut);
    return true;
}

bool MIX_StopAllTracks(Sint64 fadeOut)
{
    if (!CheckInitialized()) {
        return false;
    }

    LockMixerSync();

    for (MIX_Track *track = all_tracks; track != NULL; track = track->next) {
        StopTrack(track, (fadeOut > 0) ? MIX_TrackMSToFrames(track, fadeOut) : -1);
    }

    UnlockMixerSync();

    return true;
}

bool MIX_StopTag(const char *tag, Sint64 fadeOut)
{
    if (!CheckTagParam(tag)) {
        return false;
    }

    MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);
    if (!list) {
        return true;  // nothing is using this tag, do nothing (but not an error).
    }

    SDL_LockRWLockForReading(list->rwlock);

    const size_t total = list->num_tracks;
    for (size_t i = 0; i < total; i++) {
        StopTrack(list->tracks[i], (fadeOut > 0) ? MIX_TrackMSToFrames(list->tracks[i], fadeOut) : -1);
    }

    SDL_UnlockRWLock(list->rwlock);

    return true;
}

static void PauseTrack(MIX_Track *track)
{
    LockTrack(track);
    if (track->state == MIX_STATE_PLAYING) {
        track->state = MIX_STATE_PAUSED;
    }
    UnlockTrack(track);
}

bool MIX_PauseTrack(MIX_Track *track)
{
    if (!CheckTrackParam(track)) {
        return false;
    }
    PauseTrack(track);
    return true;
}

bool MIX_PauseAllTracks(void)
{
    if (!CheckInitialized()) {
        return false;
    }

    LockMixerSync();

    for (MIX_Track *track = all_tracks; track != NULL; track = track->next) {
        PauseTrack(track);
    }

    UnlockMixerSync();

    return true;
}

bool MIX_PauseTag(const char *tag)
{
    if (!CheckTagParam(tag)) {
        return false;
    }

    MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);
    if (!list) {
        return true;  // nothing is using this tag, do nothing (but not an error).
    }

    LockMixerSync();
    SDL_LockRWLockForReading(list->rwlock);

    const size_t total = list->num_tracks;
    for (size_t i = 0; i < total; i++) {
        PauseTrack(list->tracks[i]);
    }

    SDL_UnlockRWLock(list->rwlock);
    UnlockMixerSync();

    return true;
}

static void ResumeTrack(MIX_Track *track)
{
    LockTrack(track);
    if (track->state == MIX_STATE_PAUSED) {
        track->state = MIX_STATE_PLAYING;
    }
    UnlockTrack(track);
}

bool MIX_ResumeTrack(MIX_Track *track)
{
    if (!CheckTrackParam(track)) {
        return false;
    }
    ResumeTrack(track);
    return true;
}

bool MIX_ResumeAllTracks(void)
{
    if (!CheckInitialized()) {
        return false;
    }

    LockMixerSync();

    for (MIX_Track *track = all_tracks; track != NULL; track = track->next) {
        ResumeTrack(track);
    }

    UnlockMixerSync();
    return true;
}

bool MIX_ResumeTag(const char *tag)
{
    if (!CheckTagParam(tag)) {
        return false;
    }

    MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);
    if (!list) {
        return true;  // nothing is using this tag, do nothing (but not an error).
    }

    LockMixerSync();
    SDL_LockRWLockForReading(list->rwlock);

    const size_t total = list->num_tracks;
    for (size_t i = 0; i < total; i++) {
        ResumeTrack(list->tracks[i]);
    }

    SDL_UnlockRWLock(list->rwlock);
    UnlockMixerSync();

    return true;
}

bool MIX_TrackPlaying(MIX_Track *track)
{
    if (!CheckTrackParam(track)) {
        return false;
    }
    LockTrack(track);
    const bool retval = (track->state == MIX_STATE_PLAYING);
    UnlockTrack(track);
    return retval;
}


bool MIX_TrackPaused(MIX_Track *track)
{
    if (!CheckTrackParam(track)) {
        return false;
    }
    LockTrack(track);
    const bool retval = (track->state == MIX_STATE_PAUSED);
    UnlockTrack(track);
    return retval;
}

bool MIX_SetTrackStoppedCallback(MIX_Track *track, MIX_TrackStoppedCallback cb, void *userdata)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    LockTrack(track);
    track->stopped_callback = cb;
    track->stopped_callback_userdata = userdata;
    UnlockTrack(track);

    return true;
}

bool MIX_SetMasterGain(float gain)
{
    if (!CheckInitialized()) {
        return false;
    }
    return SDL_SetAudioDeviceGain(audio_device, gain);
}

float MIX_GetMasterGain(void)
{
    if (!CheckInitialized()) {
        return 1.0f;
    }
    return SDL_GetAudioDeviceGain(audio_device);
}

static bool SetTrackGain(MIX_Track *track, float gain)
{
    // don't have to LockTrack, as SDL_SetAudioStreamGain will do that.
    //LockTrack(track);
    const bool retval = SDL_SetAudioStreamGain(track->output_stream, gain);
    //UnlockTrack(track);
    return retval;
}

bool MIX_SetTrackGain(MIX_Track *track, float gain)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    if (gain < 0.0f) {
        gain = 0.0f;  // !!! FIXME: this clamps, but should it fail instead?
    }

    return SetTrackGain(track, gain);
}

float MIX_GetTrackGain(MIX_Track *track)
{
    if (!CheckTrackParam(track)) {
        return 1.0f;
    }

    // don't have to LockTrack, as SDL_GetAudioStreamGain will do that.
    //LockTrack(track);
    const float retval = SDL_GetAudioStreamGain(track->output_stream);
    //UnlockTrack(track);

    return retval;
}

bool MIX_SetTagGain(const char *tag, float gain)
{
    if (!CheckTagParam(tag)) {
        return false;
    }

    if (gain < 0.0f) {
        gain = 0.0f;  // !!! FIXME: this clamps, but should it fail instead?
    }

    MIX_TagList *list = (MIX_TagList *) SDL_GetPointerProperty(track_tags, tag, NULL);
    if (!list) {
        return true;  // nothing is using this tag, do nothing (but not an error).
    }

    LockMixerSync();
    SDL_LockRWLockForReading(list->rwlock);

    const size_t total = list->num_tracks;
    for (size_t i = 0; i < total; i++) {
        SetTrackGain(list->tracks[i], gain);
    }

    SDL_UnlockRWLock(list->rwlock);
    UnlockMixerSync();

    return true;;
}

static bool SetTrackFrequencyRatio(MIX_Track *track, float ratio)
{
    // don't have to LockTrack, as SDL_SetAudioStreamFrequencyRatio will do that.
    //LockTrack(track);
    const bool retval = SDL_SetAudioStreamFrequencyRatio(track->output_stream, ratio);
    //UnlockTrack(track);
    return retval;
}

bool MIX_SetTrackFrequencyRatio(MIX_Track *track, float ratio)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    ratio = SDL_clamp(ratio, 0.01f, 100.0f);   // !!! FIXME: this clamps, but should it fail instead?

    return SetTrackFrequencyRatio(track, ratio);
}

float MIX_GetTrackFrequencyRatio(MIX_Track *track)
{
    if (!CheckTrackParam(track)) {
        return 1.0f;
    }

    // don't have to LockTrack, as SDL_GetAudioStreamFrequencyRatio will do that.
    //LockTrack(track);
    const float retval = SDL_GetAudioStreamFrequencyRatio(track->output_stream);
    //UnlockTrack(track);

    return retval;
}

bool MIX_SetTrackOutputChannelMap(MIX_Track *track, const int *chmap, int count)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    // don't have to LockTrack, as SDL_SetAudioStreamOutputChannelMap will do that.
    //LockTrack(track);
    const bool retval = SDL_SetAudioStreamOutputChannelMap(track->output_stream, chmap, count);
    //UnlockTrack(track);

    return retval;
}

bool MIX_SetPostMixCallback(SDL_AudioPostmixCallback mix_func, void *userdata)
{
    if (!CheckInitialized()) {
        return false;
    }
    return SDL_SetAudioPostmixCallback(audio_device, mix_func, userdata);
}

bool MIX_SetTrackMixCallback(MIX_Track *track, MIX_TrackMixCallback cb, void *userdata)
{
    if (!CheckTrackParam(track)) {
        return false;
    }

    LockTrack(track);
    track->mix_callback = cb;
    track->mix_callback_userdata = userdata;
    UnlockTrack(track);
    return true;
}


// Clamp an IOStream to a subset of its available data.
static Sint64 MIX_IoClamp_size(void *userdata)
{
    return ((const MIX_IoClamp *) userdata)->length;
}

static Sint64 MIX_IoClamp_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    MIX_IoClamp *clamp = (MIX_IoClamp *) userdata;

    if (whence == SDL_IO_SEEK_CUR) {
        offset += clamp->pos;
    } else if (whence == SDL_IO_SEEK_END) {
        offset += clamp->length;
    }

    if (offset < 0) {
        SDL_SetError("Seek before start of data");
        return -1;
    } else if (offset > clamp->length) {
        offset = clamp->length;
    }

    if (clamp->pos != offset) {
        const Sint64 ret = SDL_SeekIO(clamp->io, clamp->start + offset, SDL_IO_SEEK_SET);
        if (ret < 0) {
            return ret;
        }
        clamp->pos = offset;
    }

    return offset;
}

static size_t MIX_IoClamp_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    MIX_IoClamp *clamp = (MIX_IoClamp *) userdata;
    const size_t remaining = (size_t)(clamp->length - clamp->pos);
    const size_t ret = SDL_ReadIO(clamp->io, ptr, SDL_min(size, remaining));
    clamp->pos += ret;
    return ret;
}

SDL_IOStream *MIX_OpenIoClamp(MIX_IoClamp *clamp, SDL_IOStream *io)
{
    /* Don't use SDL_GetIOSize() here -- see SDL bug #4026 */
    SDL_zerop(clamp);
    clamp->io = io;
    clamp->start = SDL_TellIO(io);
    clamp->length = SDL_SeekIO(io, 0, SDL_IO_SEEK_END) - clamp->start;
    clamp->pos = 0;
    if (clamp->start < 0 || clamp->length < 0 || (SDL_SeekIO(io, clamp->start, SDL_IO_SEEK_SET) < 0)) {
        SDL_SetError("Error seeking in datastream");
        return NULL;
    }

    SDL_IOStreamInterface iface;
    SDL_INIT_INTERFACE(&iface);
    iface.size = MIX_IoClamp_size;
    iface.seek = MIX_IoClamp_seek;
    iface.read = MIX_IoClamp_read;
    return SDL_OpenIO(&iface, clamp);
}

