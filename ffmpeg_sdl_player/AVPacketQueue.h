#pragma once


#include "libsdl/SDL.h"
#include "libsdl/SDL_thread.h"
#include <queue>

using namespace std;
extern "C" {

#include <libavcodec\avcodec.h>

}
class AVPacketQueue {
public:
	queue<AVPacket> avQueue;
	SDL_mutex *mutex;
	Uint32    size;
	Uint32    nb_packets;
	SDL_cond  *cond;


	bool pushAVPacket(AVPacket *avPacket);
	bool popAVPacket(AVPacket *avPacket,bool block);


public:
	AVPacketQueue();
	~AVPacketQueue();
};

