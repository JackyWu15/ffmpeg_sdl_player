#pragma once


extern "C" {
#include <libavformat\avformat.h>
#include <libavutil\rational.h>
#include <libavutil\time.h>
#include <libswscale\swscale.h>
}

#include "AVVideo.h"
#include "MediaPlayer.h"

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)
#define FF_PAUSE_EVENT (SDL_USEREVENT+2)
#define FF_STOP_EVENT (SDL_USEREVENT+3)

static const double SYNC_THRESHOLD = 0.01;
static const double NOSYNC_THRESHOLD = 10.0;



uint32_t sdl_refresh_event(uint32_t interval, void *data);
void sdl_refresh(void *user_data);
