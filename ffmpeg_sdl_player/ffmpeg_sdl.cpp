#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "libsdl/SDL.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
}



#define REFRESH_VIDEO (SDL_USEREVENT+1)
#define BREAK_EVENT (SDL_USEREVENT+2)

int thread_exit_status = 0;

int refresh_video(void *opaque) {
	thread_exit_status = 0;
	while (thread_exit_status == 0) {
		SDL_Event event;
		event.type = REFRESH_VIDEO;
		//�����¼�
		SDL_PushEvent(&event);
		//�ӳ��ٶ�,ÿ��25֡
		SDL_Delay(40);
	}
	thread_exit_status = 0;
	//�˳�����
	SDL_Event event;
	event.type = BREAK_EVENT;
	SDL_PushEvent(&event);
	return 0;
}
int main(int argc,char *argv[]){
	char *file_input = argv[1];
	if (file_input == NULL) {
		file_input = (char *)malloc(64);
		strcpy(file_input, "Titanic.ts");
	}
	

	av_register_all();

	avformat_network_init();

	//ͳ��ȫ�ֵ������ģ�������Ƶ�ļ��ķ�װ
	AVFormatContext* avformatContext = avformat_alloc_context();

	//���ļ�
	//int avformat_open_input(AVFormatContext **ps, const char *url, ff_const59 AVInputFormat *fmt, AVDictionary **options);
	if (avformat_open_input(&avformatContext, file_input, NULL, NULL) != 0) {
		printf("avformat_open_input error \n");
		return  -1;
	}


	//�ڲ�ʵ���ϼ��������������Ľ�������
	if (avformat_find_stream_info(avformatContext, NULL) < 0) {
		printf("avformat_find_stream_info error \n");
		return  -1;
	}


	int videoIndex = -1;
	//����ý����
	int i = 0;
	for (i = 0; i < avformatContext->nb_streams; i++) {
		if (avformatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
		}
	}

	if (videoIndex == -1) {
		printf("find AVMEDIA_TYPE_VIDEO error \n");
		return  -1;
	}

	//ȡ�����װ���ѹ����������AVCodecContext
	AVCodecParameters *avcodePs = avformatContext->streams[videoIndex]->codecpar;
	//�ҵ�������
	AVCodec *avcodec = avcodec_find_decoder(avcodePs->codec_id);
	if (avcodec == NULL) {
		printf("avcodec_find_decoder error \n");
		return  -1;
	}

	//AVCodecContext *avcodecContext = avformatContext->streams[videoIndex]->codec;
	AVCodecContext *avcodecContext = avcodec_alloc_context3(avcodec);

	avcodec_parameters_to_context(avcodecContext, avcodePs);

	//��ʼ��������
	if (avcodec_open2(avcodecContext, avcodec, NULL) != 0) {
		printf("avcodec_open2 error \n");
		return  -1;
	}



	//FILE *file_info = fopen("info.txt", "w+");

	//fprintf(file_info, "width = %d;\nheight=%d\n", avcodecContext->coded_width, avcodecContext->coded_height);
	printf("width = %d\n height=%d\n", avcodecContext->coded_width, avcodecContext->coded_height);

	//fclose(file_info);
	AVPacket *avpacket;
	AVFrame *avframe, *avframeYUV;


	avframe = av_frame_alloc();
	avframeYUV = av_frame_alloc();

	//һ֡�����С
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, avcodecContext->width, avcodecContext->height));

	avpicture_fill((AVPicture*)avframeYUV, out_buffer, AV_PIX_FMT_YUV420P, avcodecContext->width, avcodecContext->height),

		avpacket = (AVPacket *)av_malloc(sizeof(AVPacket));

	printf("--------------- File Information ----------------\n");
	//���ڽ�����Ƶ���ݸ�ʽͨ��av_log�����ָ�����ļ����߿���̨,�����˽����������Ƶ��ʽ��ɾ���ú����ĵ���û���κ�Ӱ�죻
	//�ٵ���avformat_find_stream_info��Ҳ����ͨ��AVFomatContext������غ�����ӡ��Ҫ����Ϣ��
	av_dump_format(avformatContext, 0, file_input, 0);
	
	printf("-------------------------------------------------\n");

	//ͼƬ������������
	SwsContext * swscontext = sws_getContext(avcodecContext->width, avcodecContext->height, avcodecContext->pix_fmt,
		avcodecContext->width, avcodecContext->height, AV_PIX_FMT_YUV420P, 4, NULL, NULL, NULL);

	//FILE *fp_output_h264 = fopen("output_h264.h264", "wb+");
	//FILE *fp_output_yuv = fopen("output_yuv.yuv", "wb+");

	//FILE *fp_in = fopen("out_yuv.yuv", "rb+");

	int screenWidth = avcodecContext->width, screenHeight = avcodecContext->height;

	int pix_picture = screenWidth * screenHeight * 3 / 2;
	//��ʼ��SDL
	if (SDL_Init(SDL_INIT_VIDEO)) {
		printf("SDL_Init_Error %s\n", SDL_GetError());
		return -1;
	}

	//��������
	SDL_Window *screen = SDL_CreateWindow("SDL_PLAYER", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

	if (!screen) {
		printf("SDL_CreateWindow_Error %s\n", SDL_GetError());
		return -1;
	}

	//������Ⱦ��
	SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, 0);

	//YUV���з�ʽ
	Uint32 pixformat = SDL_PIXELFORMAT_IYUV;

	//��������
	SDL_Texture *texture = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, screenWidth, screenHeight);

	//��ͼ������
	SDL_Rect sdlRect;

	//��ʼ��Ⱦ
	unsigned char *buffer = (unsigned char *)malloc(pix_picture);

	//�������߳�
	SDL_Thread *refresh_thread = SDL_CreateThread(refresh_video, NULL, NULL);
	SDL_Event event;

	//���װ
	int got_picture_ptr = 0, ret = 0, fram_count = 0;

	while (1) {
		//�����ȴ��¼�����
		SDL_WaitEvent(&event);
		switch (event.type) {
		case REFRESH_VIDEO:
			while (1) {

				if (av_read_frame(avformatContext, avpacket) < 0)
				//if(avcodec_send_packet(avcodecContext,avpacket)<0)
					thread_exit_status = 1;
				//ȥ����Ƶ֡����Ȼ����ֿ���
				if(avpacket->stream_index==videoIndex)
					break;
			}

			//if (av_read_frame(avformatContext, avpacket) >= 0) {
				//if (avpacket->stream_index == videoIndex) {
					//����h264����
				//	fwrite(avpacket->data, 1, avpacket->size, fp_output_h264);

					//����
					ret = avcodec_decode_video2(avcodecContext, avframe, &got_picture_ptr, avpacket);
					//avcodec_receive_frame(avcodecContext, avframe);

					if (ret < 0) {
						printf("avcodec_decode_video2 error \n");
						return  -1;
					}

					if (got_picture_ptr) {
						//�ü��ڱߣ�ûЧ��
						sws_scale(swscontext, (const uint8_t* const*)avframe->data, avframe->linesize, 0, avcodecContext->height,
							avframeYUV->data, avframeYUV->linesize);

						//fwrite(avframe->data[0], 1, avcodecContext->width*avcodecContext->height, fp_output_yuv);
						//fwrite(avframe->data[1], 1, avcodecContext->width*avcodecContext->height / 4, fp_output_yuv);
						//fwrite(avframe->data[2], 1, avcodecContext->width*avcodecContext->height / 4, fp_output_yuv);
						

						//�����ݶ���Texture��,Ϊʲô��[0]?
						SDL_UpdateTexture(texture, NULL, avframeYUV->data[0],avframeYUV->linesize[0]);
						sdlRect.x = 0;
						sdlRect.y = 0;
						sdlRect.w = screenWidth;
						sdlRect.h = screenHeight;

						SDL_RenderClear(renderer);
						//��Texture���ص�Renderer��
						SDL_RenderCopy(renderer, texture, NULL, &sdlRect);
						//��Ⱦ����Ļ
						SDL_RenderPresent(renderer);


						fram_count++;

						printf("�����%d֡:\n", fram_count);
					}
			//	}
				av_free_packet(avpacket);
			/*} else {
				thread_exit_status = 1;
			}*/
			
			//ѭ������
			/*if (fread(buffer, 1, pix_picture, fp_in) != pix_picture) {
				fseek(fp_in, 0, SEEK_SET);
				fread(buffer, 1, pix_picture, fp_in);
			}*/
			
			break;
		case SDL_WINDOWEVENT:
			SDL_GetWindowSize(screen, &screenWidth, &screenHeight);
			break;
		case SDL_QUIT:
			thread_exit_status = 1;
			break;
		case BREAK_EVENT:
			return 0;
			break;
		}
	}

	SDL_Quit();
	//fclose(fp_in);
//	free(buffer);


	//fclose(fp_output_h264);
	//fclose(fp_output_yuv);

	sws_freeContext(swscontext);
	av_frame_free(&avframeYUV);
	av_frame_free(&avframe);
	
	avcodec_close(avcodecContext);
	avformat_close_input(&avformatContext);
	return 0;
}

