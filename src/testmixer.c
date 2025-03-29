#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "SDL3_mixer/SDL_mixer.h"

//static SDL_Window *window = NULL;
//static SDL_Renderer *renderer = NULL;
static Mix_Track *track = NULL;

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

    Mix_Audio *audio = Mix_LoadAudio("sample.aiff", false);
    track = Mix_CreateTrack();

    Mix_SetTrackAudio(track, audio);
    Mix_PlayTrack(track, Mix_TrackMSToFrames(track, 9440), 3, 0, Mix_TrackMSToFrames(track, 6097), Mix_TrackMSToFrames(track, 30000), Mix_TrackMSToFrames(track, 3000));
//Sint64 maxFrames, int loops, Sint64 startpos, Sint64 loop_start, Sint64 fadeIn, Sint64 append_silence_frames);

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
    // SDL_mixer will clean up the track and audio.
    Mix_CloseAudio();
}

