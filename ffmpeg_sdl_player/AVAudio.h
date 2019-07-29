#pragma once

#include "AVPacketQueue.h"
extern "C" {

#include <libavformat\avformat.h>
#include <libavutil\rational.h>
#include "libswresample\swresample.h"
}



class AVAudio {
public:
	int stream_index ;
	AVStream *audioStream;
	AVCodec *audioCodec;
	AVCodecContext *audioCodecCtx;
	AVPacketQueue *audioQueue;

	double audio_duration;//音频时长
	const uint32_t BUFFER_SIZE;// 缓冲区的大小
	uint8_t *audio_buff;       // 缓冲区
	uint32_t audio_buff_size;  // 缓冲区的字节数
	uint32_t audio_buff_offset; // 缓冲区未发送数据的index



	bool audio_play();
	double get_audio_clock();

public:
	AVAudio();
	~AVAudio();
};

void audio_devices_callback(void *userdata, Uint8 *stream, int len);

int audio_decode_frame(AVAudio * audio, uint8_t * audio_buf, int buf_size);