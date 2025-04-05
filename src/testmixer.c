#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "SDL3_mixer/SDL_mixer.h"

//static SDL_Window *window = NULL;
//static SDL_Renderer *renderer = NULL;
static Mix_Track *track = NULL;

static void SDLCALL LogMetadata(void *userdata, SDL_PropertiesID props, const char *name)
{
    const char *audiofname = (const char *) userdata;

    switch (SDL_GetPropertyType(props, name)) {
        case SDL_PROPERTY_TYPE_INVALID:
            SDL_Log("%s metadata: %s [invalid type]", audiofname, name);
            break;

        case SDL_PROPERTY_TYPE_POINTER:
            SDL_Log("%s metadata: %s [pointer=%p]", audiofname, name, SDL_GetPointerProperty(props, name, NULL));
            break;

        case SDL_PROPERTY_TYPE_STRING:
            SDL_Log("%s metadata: %s [string=\"%s\"]", audiofname, name, SDL_GetStringProperty(props, name, ""));
            break;

        case SDL_PROPERTY_TYPE_NUMBER:
            SDL_Log("%s metadata: %s [number=%" SDL_PRIs64 "]", audiofname, name, SDL_GetNumberProperty(props, name, 0));
            break;

        case SDL_PROPERTY_TYPE_FLOAT:
            SDL_Log("%s metadata: %s [float=%f]", audiofname, name, SDL_GetFloatProperty(props, name, 0.0f));
            break;

        case SDL_PROPERTY_TYPE_BOOLEAN:
            SDL_Log("%s metadata: %s [boolean=%s]", audiofname, name, SDL_GetBooleanProperty(props, name, false) ? "true" : "false");
            break;

        default:
            SDL_Log("%s metadata: %s [unknown type]", audiofname, name);
            break;
    }
}


SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_SetAppMetadata("Test SDL_mixer", "1.0", "org.libsdl.testmixer");

    if (!SDL_Init(SDL_INIT_VIDEO)) {   // it's safe to SDL_INIT_AUDIO, but SDL_mixer will do it for us.
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
//    } else if (!SDL_CreateWindowAndRenderer("testmixer", 640, 480, 0, &window, &renderer)) {
//        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
//        return SDL_APP_FAILURE;
    } else if (!Mix_OpenAudio(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL)) {
        SDL_Log("Couldn't create mixer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    const char *audiofname = "sample.mp3";
    Mix_Audio *audio = Mix_LoadAudio(audiofname, false);
    if (!audio) {
        SDL_Log("Failed to load audio: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_EnumerateProperties(Mix_GetAudioProperties(audio), LogMetadata, (void *) audiofname);

    track = Mix_CreateTrack();
    Mix_SetTrackAudio(track, audio);
    Mix_PlayTrack(track, Mix_TrackMSToFrames(track, 9440), 3, 0, Mix_TrackMSToFrames(track, 6097), Mix_TrackMSToFrames(track, 30000), Mix_TrackMSToFrames(track, 3000));
//Sint64 maxFrames, int loops, Sint64 startpos, Sint64 loop_start, Sint64 fadeIn, Sint64 append_silence_frames);

    // we cheat here with PlayOnce, since the sinewave decoder produces infinite audio.
    //Mix_PlayOnce(Mix_CreateSineWaveAudio(300, 0.25f));

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
//    SDL_RenderClear(renderer);
//    SDL_RenderPresent(renderer);
    return Mix_Playing(track) ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    // SDL will clean up the window/renderer for us.
    // SDL_mixer will clean up the tracks and audio.
    Mix_CloseAudio();
}

