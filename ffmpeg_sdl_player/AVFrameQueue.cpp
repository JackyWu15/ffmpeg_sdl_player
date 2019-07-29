#include "AVFrameQueue.h"



AVFrameQueue::AVFrameQueue() {
	this->nb_frames = 0;
	this->mutex = SDL_CreateMutex();
	this->cond = SDL_CreateCond();
}


AVFrameQueue::~AVFrameQueue() {
}


//AVFrame入队
bool AVFrameQueue::pushAVFrame(const AVFrame * frame) {
	bool ret = true;
	AVFrame * p_frame = av_frame_alloc();
	if (av_frame_ref(p_frame, frame)<0) {
		ret = false;
		return ret;
	}

	//上一个指向的是一个局部的变量，这里重新分配pts空间
	p_frame->opaque = (void *)new double(*(double*)frame->opaque);

	SDL_LockMutex(this->mutex);
	this->queue.push(p_frame);

	nb_frames++;
	SDL_CondSignal(this->cond);
	SDL_UnlockMutex(this->mutex);

	return ret;
}


//AVFrame出队
bool AVFrameQueue::popAVFrame(AVFrame ** frame) {
	bool ret = true;

	SDL_LockMutex(this->mutex);

	while (true) {
		if (!this->queue.empty()) {
			AVFrame *p_frame = this->queue.front();
			if (av_frame_ref(*frame, p_frame) < 0) {
				ret = false;
				break;
			}

			this->queue.pop();
			av_frame_free(&p_frame);
			nb_frames--;
			ret = true;
			break;
		} else {
			SDL_CondWait(this->cond, this->mutex);
		}
	}


	SDL_UnlockMutex(this->mutex);
	return ret;
}
