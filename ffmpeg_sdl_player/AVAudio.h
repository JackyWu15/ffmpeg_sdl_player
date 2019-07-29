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

	double audio_duration;//��Ƶʱ��
	const uint32_t BUFFER_SIZE;// �������Ĵ�С
	uint8_t *audio_buff;       // ������
	uint32_t audio_buff_size;  // ���������ֽ���
	uint32_t audio_buff_offset; // ������δ�������ݵ�index



	bool audio_play();
	double get_audio_clock();

public:
	AVAudio();
	~AVAudio();
};

void audio_devices_callback(void *userdata, Uint8 *stream, int len);

int audio_decode_frame(AVAudio * audio, uint8_t * audio_buf, int buf_size);