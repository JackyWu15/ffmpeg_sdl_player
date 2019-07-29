#include "AVVideo.h"


AVVideo::AVVideo() {
	this->stream_index = -1;
	this->videoStream = nullptr;
	this->videoCodec = nullptr;
	this->videoPacketQueue = new AVPacketQueue();
	
}


AVVideo::~AVVideo() {
	delete this->videoPacketQueue;
	av_frame_free(&bufferFrame);
	av_free(displayFrame->data[0]);
	av_frame_free(&displayFrame);

}


// ��Ƶ��ʼ���ţ���Ƶ���ȸ�����Ƶ�����ţ���MediaPlayer                   

void AVVideo::video_play(MediaPlayer *mediaPlayer) {
	screen_w = videoCodecCtx->width;
	screen_h = videoCodecCtx->height;
	printf("c_w = %d,c_h = %d", screen_w, screen_h);

	play_state = 0;

	this->bufferFrame = av_frame_alloc();
	this->displayFrame = av_frame_alloc();
	displayFrame->format = AV_PIX_FMT_YUV420P;
	displayFrame->width = screen_w;
	displayFrame->height = screen_h;


	//����
	window = SDL_CreateWindow("FFmpeg Decode", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	//��ͼ
	SDL_Rect sdlRect;
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	//��Ⱦ��
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	SDL_RenderClear(renderer);

	//����
	Uint32 pixformat = SDL_PIXELFORMAT_IYUV;
	texture = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);

	//һ֡����
	int pic_size = avpicture_get_size((AVPixelFormat)displayFrame->format, screen_w, screen_h);
	uint8_t *pic_buffer = (uint8_t *)malloc(pic_size);

	//����������
	avpicture_fill((AVPicture*)displayFrame, pic_buffer, (AVPixelFormat)displayFrame->format, displayFrame->width, displayFrame->height);

	//����֡����
	SDL_CreateThread(video_decode_frame, "", this);

	//ˢ�½��棬40msˢһ�Σ���1/25֡��
	SDL_AddTimer(40, sdl_refresh_event, mediaPlayer);
}


//ͬ��ʱ��
double AVVideo::synchronize(AVFrame * frame, double pts) {
	double frame_delay;
	if (pts != 0) {
		video_duration = pts;
	} else {
		pts = video_duration;
	}

	//ʵ��֡�ӳٵ�ʱ��
	frame_delay = av_q2d(this->videoStream->codec->time_base);//һ֡ʱ��
	frame_delay += frame->repeat_pict*(frame_delay*0.5);//�ظ�֡

	//������ʱ�� = Ӧ����ʾ��ʱ�� + ʵ��֡�ӳٵ�ʱ��
	video_duration += frame_delay;

	return pts;
}

//������Ƶ֡
int video_decode_frame(void * data) {
	AVVideo *video = (AVVideo *)data;
	AVPacket pkt;
	AVFrame *frame = av_frame_alloc();

	
	double pts;
	while (true) {
		video->videoPacketQueue->popAVPacket(&pkt, true);
		int ret = 0;
		ret = avcodec_send_packet(video->videoCodecCtx, &pkt);
		if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
			continue;

		ret = avcodec_receive_frame(video->videoCodecCtx, frame);
		if (ret < 0 && ret != AVERROR_EOF)
			continue;

		if ((pts = av_frame_get_best_effort_timestamp(frame)) == AV_NOPTS_VALUE)
			pts = 0;

		//֡��ʾ��ʱ��
		pts *= av_q2d(video->videoStream->time_base);
		//�п��ܴ��ڵ���av_frame_get_best_effort_timestamp�ò���һ����ȷ��PTS������ǰ�����duration���и�ֵ
		pts = video->synchronize(frame, pts);

		frame->opaque = &pts;


		if (video->videoFrameQueue.nb_frames >= AVFrameQueue::capacity) {
			SDL_Delay(500 * 2);
		}

		video->videoFrameQueue.pushAVFrame(frame);

		av_frame_unref(frame);


	}


	av_frame_free(&frame);


	return 0;
}



	
