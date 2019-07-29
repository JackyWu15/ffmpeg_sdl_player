#pragma once

#include "libsdl/SDL.h"
#include "libsdl/SDL_thread.h"
#include <queue>
extern "C" {
#include <libavutil\rational.h>
#include "libavformat\avformat.h"
}


class AVFrameQueue {
public:
	
	static const int capacity = 30;//1ร๋30ึก

	std::queue<AVFrame*> queue;

	uint32_t nb_frames;

	SDL_mutex* mutex;
	SDL_cond * cond;


public:
	AVFrameQueue();
	~AVFrameQueue();
	bool pushAVFrame(const AVFrame* frame);
	bool popAVFrame(AVFrame **frame);
};

