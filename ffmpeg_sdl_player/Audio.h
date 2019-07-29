
#ifndef AUDIO_H
#define AUDIO_H

#include "PacketQueue.h"
#include <algorithm>
#include <stdint.h>
extern "C"{

#include <libavformat\avformat.h>
#include <libavutil\rational.h>

}

/**
 * ����audioʱ����Ҫ�����ݷ�װ
 */
struct AudioState
{
	const uint32_t BUFFER_SIZE;// �������Ĵ�С

	PacketQueue audioq;

	double audio_clock; // audio clock
	AVStream *stream; // audio stream

	uint8_t *audio_buff;       // ��������ݵĻ���ռ�
	uint32_t audio_buff_size;  // buffer�е��ֽ���
	uint32_t audio_buff_index; // buffer��δ�������ݵ�index
	
	int stream_index;          // audio��index
	AVCodecContext *audio_ctx; // �Ѿ�����avcodec_open2��

	AudioState();              //Ĭ�Ϲ��캯��
	AudioState(AVCodecContext *audio_ctx, int audio_stream);
	
	~AudioState();

	/**
	* audio play
	*/
	bool audio_play();

	// get audio clock
	double get_audio_clock();
};

/**
 * ���豸����audio���ݵĻص�����
 */
void audio_callback(void* userdata, Uint8 *stream, int len);

/**
 * ����Avpacket�е�������䵽����ռ�
 */
int audio_decode_frame(AudioState *audio_state, uint8_t *audio_buf, int buf_size);


#endif