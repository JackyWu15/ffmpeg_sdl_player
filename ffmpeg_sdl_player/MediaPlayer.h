#pragma once

#include "AVAudio.h"
#include "AVVideo.h"
extern "C" {
#include "libavformat/avformat.h"
}

int decodec_thread(void *data);

class MediaPlayer {

public:
	AVFormatContext *pFormatCtx;

	AVAudio *audio;

	AVVideo *video;
	


public:
	bool openFile(const char *fileName);


public:
	MediaPlayer();
	~MediaPlayer();
};

