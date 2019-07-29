#pragma once

#include "AVPacketQueue.h"
#include "AVFrameQueue.h"
extern "C" {
#include <libavformat\avformat.h>
#include <libavutil\rational.h>
#include <libavutil\time.h>
#include <libswscale\swscale.h>
}




class MediaPlayer;

class AVVideo {
public:
	int stream_index ;
	AVStream *videoStream;
	AVCodec *videoCodec;
	AVCodecContext *videoCodecCtx;
	AVPacketQueue *videoPacketQueue;
	AVFrameQueue videoFrameQueue;
	AVFrame *displayFrame;
	AVFrame *bufferFrame;
	SDL_Window *window;
	SDL_Texture *texture;
	SDL_Renderer *renderer;
	int screen_w;
	int screen_h;
	int play_state;

	double video_duration;
	double frame_timer;         
	double frame_last_pts;
	double frame_last_delay;


	void video_play(MediaPlayer *mediaPlayer);
	double synchronize(AVFrame *frame, double pts);


public:
	AVVideo();
	~AVVideo();
};


int video_decode_frame(void *data);
uint32_t sdl_refresh_event(uint32_t interval, void *opaque);
void sdl_refresh(void *user_data);

