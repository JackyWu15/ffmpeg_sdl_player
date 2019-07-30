#include "AVAudio.h"



extern bool quit;

AVAudio::AVAudio():BUFFER_SIZE(48000*4){
	this->stream_index = -1;
	this->audioStream = nullptr;
	this->audioCodec = nullptr;
	this->audioQueue = new AVPacketQueue();

	this->audio_buff = new uint8_t[BUFFER_SIZE];
	this->audio_buff_size = 0;
	this->audio_buff_offset = 0;
}


AVAudio::~AVAudio() {
	if (this->audio_buff) {
		delete[]this->audio_buff;
	}

}

//��Ƶ������ʼ������ʼ����
bool AVAudio::audio_play() {
	SDL_AudioSpec audioSpec;

	audioSpec.freq = this->audioCodecCtx->sample_rate;//������
	audioSpec.channels = this->audioCodecCtx->channels;//������
	audioSpec.format = AUDIO_S16SYS;//�з���16λ
	
	audioSpec.samples = this->audioCodecCtx->frame_size;//֡��С
	audioSpec.silence = 0;
	audioSpec.userdata = this;
	audioSpec.callback = audio_devices_callback;

	if (SDL_OpenAudio(&audioSpec, nullptr)<0) {
		return false;
	}

	//��ʼ����
	SDL_PauseAudio(0);

	return true;
}





//���͵��豸����
void audio_devices_callback(void *userdata, Uint8 *stream, int len) {
	AVAudio * audio = (AVAudio *)userdata;
	SDL_memset(stream, 0, len);

	int audio_size = 0;
	int len_left;
	printf(">>>>>>>>>>>>>>>>>>>%d", len);
	while (len>0) {
		//�����������ѷ����꣬�����������ݵ�������
		if (audio->audio_buff_offset >= audio->audio_buff_size) {
			audio_size = audio_decode_frame(audio, audio->audio_buff, sizeof(audio->audio_buff_size));

			if (audio_size <0) {
				audio->audio_buff_size = 0;
				memset(audio->audio_buff, 0, audio->audio_buff_size);
			} else {
				audio->audio_buff_size = audio_size;
			}
			audio->audio_buff_offset = 0;
		}

		//ʣ������
		len_left = audio->audio_buff_size - audio->audio_buff_offset;

		printf("audio len_left=%d,len=%d\n", len_left, len);
		//�����һ����ʣ�࣬��һ�ε����ݽ�������Ҫ�����ݣ���ôlen����Ϊ0������ѭ���ȴ��´λص���ͬʱ���������������ݣ��ȴ��´��жϣ�����Ǳ�=�˴����ݳ���
		if (len_left > len) {
			len_left = len;
		}

		SDL_MixAudio(stream, audio->audio_buff + audio->audio_buff_offset, len, SDL_MIX_MAXVOLUME);
		//����Ϊ0����������Ϊ�´���Ҫ�����ݳ���
		len -= len_left;
		//�´η��ͳ���
		stream += len_left;
		//��¼����Ǳ�
		audio->audio_buff_offset += len_left;
	}

}

//������Ƶ֡
int audio_decode_frame(AVAudio * audio, uint8_t * audio_buf, int buf_size) {
	int ret;
	AVFrame *frame = av_frame_alloc();
	AVPacket pkt;

	int data_size;
	if (quit) {
		return -1;
	}
	
	if (!audio->audioQueue->popAVPacket(&pkt, true)) {
		return -1;
	}
	//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	if (pkt.pts != AV_NOPTS_VALUE) {
		audio->audio_duration = av_q2d(audio->audioStream->time_base)*pkt.pts;
	}

	ret = avcodec_send_packet(audio->audioCodecCtx, &pkt);
	if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
		return -1;
	}

	ret = avcodec_receive_frame(audio->audioCodecCtx, frame);
	if (ret < 0 && ret != AVERROR_EOF) {
	return -1;
	}

	if (frame->channels > 0 && frame->channel_layout == 0) {
		frame->channel_layout = av_get_default_channel_layout(frame->channels);
	} else if (frame->channels == 0 && frame->channel_layout > 0) {
		frame->channels = av_get_channel_layout_nb_channels(frame->channel_layout);
	}


	//Դ��Ƶ������һ���������ǽ�������Ҫ����Ƶ��������������Ҫ�����ز���
	AVSampleFormat dst_format = AV_SAMPLE_FMT_S16;
	Uint64 dst_layout = av_get_default_channel_layout(frame->channels);
	SwrContext * swr_ctx;
	swr_ctx = swr_alloc_set_opts(
		nullptr,//�½���null������Ϊ�޸�
		dst_layout,
		dst_format,
		frame->sample_rate,
		frame->channel_layout,
		(AVSampleFormat)frame->format,
		frame->sample_rate,
		0,
		nullptr
	);

	if (!swr_ctx || swr_init(swr_ctx) < 0){
		return -1;
	}

	//ת����Ĳ�������
	uint64_t dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame->sample_rate) + frame->nb_samples, frame->sample_rate, frame->sample_rate, AVRounding(1));

	int dst_sample_rate = swr_convert(swr_ctx, &audio_buf, static_cast<int>(dst_nb_samples), (const uint8_t**)frame->data, frame->nb_samples);
	printf("dst_nb_samples >>>>>>>>>>>>>>>%d\n", dst_nb_samples);
	data_size = frame->channels * dst_sample_rate*av_get_bytes_per_sample(dst_format);

	//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
	audio->audio_duration += static_cast<double>(data_size)/ (2 * audio->audioStream->codecpar->channels * audio->audioStream->codecpar->sample_rate);
	printf("audio->audio_duration >>>>>>>>>>>>>>>%d\n", audio->audio_duration);


	av_frame_free(&frame);
	swr_free(&swr_ctx);

	return data_size;
}


//��ȡ��ǰ��Ƶpts���п��ܻ����л��в���������û���ţ����޳�
double AVAudio::get_audio_clock() {
	double left_size_pts = static_cast<double>(audio_buff_size - audio_buff_offset)/(audioStream->codec->sample_rate * audioCodecCtx->channels * 2);

	double pts = audio_duration - left_size_pts;

	return pts;
}