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


// 视频开始播放，视频进度根据音频来播放，传MediaPlayer                   

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


	//窗口
	window = SDL_CreateWindow("FFmpeg Decode", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	//视图
	SDL_Rect sdlRect;
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	//渲染器
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	SDL_RenderClear(renderer);

	//纹理
	Uint32 pixformat = SDL_PIXELFORMAT_IYUV;
	texture = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);

	//一帧缓存
	int pic_size = avpicture_get_size((AVPixelFormat)displayFrame->format, screen_w, screen_h);
	uint8_t *pic_buffer = (uint8_t *)malloc(pic_size);

	//关联缓冲区
	avpicture_fill((AVPicture*)displayFrame, pic_buffer, (AVPixelFormat)displayFrame->format, displayFrame->width, displayFrame->height);

	//解码帧数据
	SDL_CreateThread(video_decode_frame, "", this);

	//刷新界面，40ms刷一次，即1/25帧率
	SDL_AddTimer(40, sdl_refresh_event, mediaPlayer);
}


//同步时间
double AVVideo::synchronize(AVFrame * frame, double pts) {
	double frame_delay;
	if (pts != 0) {
		video_duration = pts;
	} else {
		pts = video_duration;
	}

	//实际帧延迟的时间
	frame_delay = av_q2d(this->videoStream->codec->time_base);//一帧时间
	frame_delay += frame->repeat_pict*(frame_delay*0.5);//重复帧

	//正常的时间 = 应该显示的时间 + 实际帧延迟的时间
	video_duration += frame_delay;

	return pts;
}

//解码视频帧
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

		//帧显示的时间
		pts *= av_q2d(video->videoStream->time_base);
		//有可能存在调用av_frame_get_best_effort_timestamp得不到一个正确的PTS，将当前计算的duration进行赋值
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



	
