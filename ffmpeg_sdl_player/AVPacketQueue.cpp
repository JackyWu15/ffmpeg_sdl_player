#include "AVPacketQueue.h"

extern bool quit;

AVPacketQueue::AVPacketQueue() {
	this->nb_packets = 0;
	this->size = 0;

	this->mutex = SDL_CreateMutex();
	this->cond = SDL_CreateCond();
	quit = false;
}


AVPacketQueue::~AVPacketQueue() {
}


//AVPacket入队
bool AVPacketQueue::pushAVPacket(AVPacket * avPacket) {
	bool ret = false;
	AVPacket *pkt = av_packet_alloc();
	if (av_packet_ref(pkt, avPacket) < 0) {
		ret = false;
		return ret;
	}

	SDL_LockMutex(this->mutex);

	this->avQueue.push(*pkt);
	this->size +=pkt->size;
	this->nb_packets++;

	SDL_CondSignal(this->cond);
	SDL_UnlockMutex(this->mutex);
	ret = true;
	return ret;
}


//AVPacket出队
bool AVPacketQueue::popAVPacket(AVPacket * avPacket,bool block) {

	bool ret = false;

	SDL_LockMutex(this->mutex);
	while (true) {
		if (quit) {
			ret = false;
			break;
		}

		if (!this->avQueue.empty()) {
			
			AVPacket pkt = avQueue.front();
			if (av_packet_ref(avPacket, &pkt) < 0) {
				ret = false;
				break;
			}
			avQueue.pop();

			av_packet_unref(&pkt);
			this->nb_packets--;
			this->size -= pkt.size;
			ret = true;
			break;
		} else if(!block){
			ret = false;
			break;
		} else {
			SDL_CondWait(this->cond, this->mutex);
		}
	}

	SDL_UnlockMutex(this->mutex);

	return ret;
}
