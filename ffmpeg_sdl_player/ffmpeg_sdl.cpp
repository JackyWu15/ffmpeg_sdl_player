#define _CRT_SECURE_NO_WARNINGS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "libsdl/SDL.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h"
#include "libavutil/timestamp.h"
}

#ifndef AV_WB32
#   define AV_WB32(p, val) do {                 \
        uint32_t d = (val);                     \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])
#endif

static int alloc_and_copy(AVPacket *out,
	const uint8_t *sps_pps, uint32_t sps_pps_size,
	const uint8_t *in, uint32_t in_size) {
	uint32_t offset = out->size;
	uint8_t nal_header_size = offset ? 3 : 4;
	int err;

	err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);
	if (err < 0)
		return err;

	if (sps_pps)
		memcpy(out->data + offset, sps_pps, sps_pps_size);
	memcpy(out->data + sps_pps_size + nal_header_size + offset, in, in_size);
	if (!offset) {
		AV_WB32(out->data + sps_pps_size, 1);
	} else {
		(out->data + offset + sps_pps_size)[0] =
			(out->data + offset + sps_pps_size)[1] = 0;
		(out->data + offset + sps_pps_size)[2] = 1;
	}

	return 0;
}

int h264_extradata_to_annexb(const uint8_t *codec_extradata, const int codec_extradata_size, AVPacket *out_extradata, int padding) {
	uint16_t unit_size;
	uint64_t total_size = 0;
	uint8_t *out = NULL, unit_nb, sps_done = 0,
		sps_seen = 0, pps_seen = 0, sps_offset = 0, pps_offset = 0;
	const uint8_t *extradata = codec_extradata + 4;
	static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
	int length_size = (*extradata++ & 0x3) + 1; // retrieve length coded size, ����ָʾ��ʾ�������ݳ��������ֽ���

	sps_offset = pps_offset = -1;

	/* retrieve sps and pps unit(s) */
	unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
	if (!unit_nb) {
		goto pps;
	} else {
		sps_offset = 0;
		sps_seen = 1;
	}

	while (unit_nb--) {
		int err;

		unit_size = AV_RB16(extradata);
		total_size += unit_size + 4;
		if (total_size > INT_MAX - padding) {
			av_log(NULL, AV_LOG_ERROR,
				"Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
			av_free(out);
			return AVERROR(EINVAL);
		}
		if (extradata + 2 + unit_size > codec_extradata + codec_extradata_size) {
			av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
				"corrupted stream or invalid MP4/AVCC bitstream\n");
			av_free(out);
			return AVERROR(EINVAL);
		}
		if ((err = av_reallocp(&out, total_size + padding)) < 0)
			return err;
		memcpy(out + total_size - unit_size - 4, nalu_header, 4);
		memcpy(out + total_size - unit_size, extradata + 2, unit_size);
		extradata += 2 + unit_size;
	pps:
		if (!unit_nb && !sps_done++) {
			unit_nb = *extradata++; /* number of pps unit(s) */
			if (unit_nb) {
				pps_offset = total_size;
				pps_seen = 1;
			}
		}
	}

	if (out)
		memset(out + total_size, 0, padding);

	if (!sps_seen)
		av_log(NULL, AV_LOG_WARNING,
			"Warning: SPS NALU missing or invalid. "
			"The resulting stream may not play.\n");

	if (!pps_seen)
		av_log(NULL, AV_LOG_WARNING,
			"Warning: PPS NALU missing or invalid. "
			"The resulting stream may not play.\n");

	out_extradata->data = out;
	out_extradata->size = total_size;

	return length_size;
}

int h264_mp4toannexb(AVFormatContext *fmt_ctx, AVPacket *in, FILE *dst_fd) {

	AVPacket *out = NULL;
	AVPacket spspps_pkt;

	int len;
	uint8_t unit_type;
	int32_t nal_size;
	uint32_t cumul_size = 0;
	const uint8_t *buf;
	const uint8_t *buf_end;
	int            buf_size;
	int ret = 0, i;

	out = av_packet_alloc();

	buf = in->data;
	buf_size = in->size;
	buf_end = in->data + in->size;

	do {
		ret = AVERROR(EINVAL);
		if (buf + 4 /*s->length_size*/ > buf_end)
			goto fail;

		for (nal_size = 0, i = 0; i < 4/*s->length_size*/; i++)
			nal_size = (nal_size << 8) | buf[i];

		buf += 4; /*s->length_size;*/
		unit_type = *buf & 0x1f;

		if (nal_size > buf_end - buf || nal_size < 0)
			goto fail;

		/*
		if (unit_type == 7)
			s->idr_sps_seen = s->new_idr = 1;
		else if (unit_type == 8) {
			s->idr_pps_seen = s->new_idr = 1;
			*/
			/* if SPS has not been seen yet, prepend the AVCC one to PPS */
			/*
			if (!s->idr_sps_seen) {
				if (s->sps_offset == -1)
					av_log(ctx, AV_LOG_WARNING, "SPS not present in the stream, nor in AVCC, stream may be unreadable\n");
				else {
					if ((ret = alloc_and_copy(out,
										 ctx->par_out->extradata + s->sps_offset,
										 s->pps_offset != -1 ? s->pps_offset : ctx->par_out->extradata_size - s->sps_offset,
										 buf, nal_size)) < 0)
						goto fail;
					s->idr_sps_seen = 1;
					goto next_nal;
				}
			}
		}
		*/

		/* if this is a new IDR picture following an IDR picture, reset the idr flag.
		 * Just check first_mb_in_slice to be 0 as this is the simplest solution.
		 * This could be checking idr_pic_id instead, but would complexify the parsing. */
		 /*
		 if (!s->new_idr && unit_type == 5 && (buf[1] & 0x80))
			 s->new_idr = 1;

		 */
		 /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
		if (/*s->new_idr && */unit_type == 5 /*&& !s->idr_sps_seen && !s->idr_pps_seen*/) {

			h264_extradata_to_annexb(fmt_ctx->streams[in->stream_index]->codec->extradata,
				fmt_ctx->streams[in->stream_index]->codec->extradata_size,
				&spspps_pkt,
				AV_INPUT_BUFFER_PADDING_SIZE);

			if ((ret = alloc_and_copy(out,
				spspps_pkt.data, spspps_pkt.size,
				buf, nal_size)) < 0)
				goto fail;
			/*s->new_idr = 0;*/
		/* if only SPS has been seen, also insert PPS */
		}
		/*else if (s->new_idr && unit_type == 5 && s->idr_sps_seen && !s->idr_pps_seen) {
			if (s->pps_offset == -1) {
				av_log(ctx, AV_LOG_WARNING, "PPS not present in the stream, nor in AVCC, stream may be unreadable\n");
				if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
					goto fail;
			} else if ((ret = alloc_and_copy(out,
										ctx->par_out->extradata + s->pps_offset, ctx->par_out->extradata_size - s->pps_offset,
										buf, nal_size)) < 0)
				goto fail;
		}*/ else {
			if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
				goto fail;
			/*
			if (!s->new_idr && unit_type == 1) {
				s->new_idr = 1;
				s->idr_sps_seen = 0;
				s->idr_pps_seen = 0;
			}
			*/
		}


		len = fwrite(out->data, 1, out->size, dst_fd);
		if (len != out->size) {
			av_log(NULL, AV_LOG_DEBUG, "warning, length of writed data isn't equal pkt.size(%d, %d)\n",
				len,
				out->size);
		}
		fflush(dst_fd);

	next_nal:
		buf += nal_size;
		cumul_size += nal_size + 4;//s->length_size;
	} while (cumul_size < buf_size);

	/*
	ret = av_packet_copy_props(out, in);
	if (ret < 0)
		goto fail;

	*/
fail:
	av_packet_free(&out);

	return ret;
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

void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
	AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
	char buf1[AV_TS_MAX_STRING_SIZE] = { 0 };
	av_ts_make_string(buf1, pkt->pts);
	char buf2[AV_TS_MAX_STRING_SIZE] = { 0 };
	av_ts_make_string(buf1, pkt->dts);
	char buf3[AV_TS_MAX_STRING_SIZE] = { 0 };
	av_ts_make_string(buf1, pkt->duration);


	/*printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
		tag,
		av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
		av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
		av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
		pkt->stream_index);*/

	/*printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
		tag,
		buf1, av_ts2timestr(pkt->pts, time_base),
		buf2, av_ts2timestr(pkt->dts, time_base),
		buf3, av_ts2timestr(pkt->duration, time_base),
		pkt->stream_index);*/
}


/************************************************************************/
/* sdl������                                                               */
/************************************************************************/
int sdl_player() {
	char *file_input  = (char *)malloc(64);
		//strcpy(file_input, "Titanic.ts");
		strcpy(file_input, "cuc_ieschool.ts");
	

	av_register_all();

	avformat_network_init();

	//ȫ�ֵ������ģ�������Ƶ�ļ��ķ�װ
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

	/*----------------���ڽ�����Ƶ���ݸ�ʽͨ��av_log�����ָ�����ļ����߿���̨,�����˽����������Ƶ��ʽ��ɾ���ú����ĵ���û���κ�Ӱ��---------------------*/
	/*-------------------------Ҳ����avformat_find_stream_info��ͨ��AVFomatContext������غ�����ӡ��Ҫ����Ϣ----------------------*/
	printf("--------------- File Information ----------------\n");
	av_dump_format(avformatContext, 0, file_input, 0);
	printf("-------------------------------------------------\n");

	/*----------------���ڴ�ӡ����Ϣ������̨---------------------*/

	int videoIndex = -1;
	////����ý����
	int i = 0;
	for (i = 0; i < avformatContext->nb_streams; i++) {
		if (avformatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
		}
	}

	//av_find_best_streamҲ�����ҵ���Ҫ����
	//videoIndex = av_find_best_stream(avformatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

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
	//printf("width = %d\n height=%d\n", avcodecContext->coded_width, avcodecContext->coded_height);

	//fclose(file_info);
	AVPacket *avpacket;
	AVFrame *avframe, *avframeYUV;

	avframe = av_frame_alloc();
	avframeYUV = av_frame_alloc();

	//һ֡�����С
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, avcodecContext->width, avcodecContext->height));

	avpicture_fill((AVPicture*)avframeYUV, out_buffer, AV_PIX_FMT_YUV420P, avcodecContext->width, avcodecContext->height),

		avpacket = (AVPacket *)av_malloc(sizeof(AVPacket));

	

	//ͼƬ������������
	SwsContext * swscontext = sws_getContext(avcodecContext->width, avcodecContext->height, avcodecContext->pix_fmt,
		avcodecContext->width, avcodecContext->height, AV_PIX_FMT_YUV420P, 4, NULL, NULL, NULL);

	FILE *fp_output_h264 = fopen("output_h264.h264", "wb+");
	FILE *fp_output_yuv = fopen("output_yuv.yuv", "wb+");

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
				if (avpacket->stream_index == videoIndex)
					break;
			}

			//if (av_read_frame(avformatContext, avpacket) >= 0) {
				//if (avpacket->stream_index == videoIndex) {

					//fwrite(avpacket->data, 1, avpacket->size, fp_output_h264);//����h264������ֻ����ts
			h264_mp4toannexb(avformatContext, avpacket, fp_output_h264);//����h264����������mp4,flv,avi,������ts,mkv
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

				fwrite(avframe->data[0], 1, avcodecContext->width*avcodecContext->height, fp_output_yuv);
				fwrite(avframe->data[1], 1, avcodecContext->width*avcodecContext->height / 4, fp_output_yuv);
				fwrite(avframe->data[2], 1, avcodecContext->width*avcodecContext->height / 4, fp_output_yuv);


				//�����ݶ���Texture��,Ϊʲô��[0]?
				SDL_UpdateTexture(texture, NULL, avframeYUV->data[0], avframeYUV->linesize[0]);
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

				//printf("�����%d֡:\n", fram_count);
			}
			//	}
				//av_free_packet(avpacket);
			av_packet_unref(avpacket);
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


/************************************************************************/
/* mp4תflv                                                                     */
/************************************************************************/
int remuxing() {
	int ret=0;
	AVFormatContext *ifmt_ctx=NULL,* ofmt_ctx=NULL;
	AVOutputFormat *ofmt = NULL;


	int stream_mapping_size = 0;
	int *stream_mapping = 0;
	int stream_index = 0;
	AVStream *out_stream = NULL;


	char *input = (char *)malloc(64);
	char *output = (char *)malloc(64);
	strcpy(input, "cuc_ieschool.mp4");
	strcpy(output, "cuc_ieschool_remux.flv");

	av_register_all();

	ret = avformat_open_input(&ifmt_ctx, input, 0, 0);
	if (ret != 0) {
		printf("avformat_open_input failed %s \n", input);
		ret = AVERROR(ENOMEM);
		goto end;
	}

	ret = avformat_find_stream_info(ifmt_ctx, 0);
	if (ret < 0) {
		printf("avformat_find_stream_info failed %s \n", input);
		ret = AVERROR(ENOMEM);
		goto end;
	}
	av_dump_format(ifmt_ctx, 0,input,0);

	//����flv��������
	ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output);
	if (ret <0) {
		printf("avformat_alloc_output_context2 failed %s \n", output);
		ret = AVERROR(ENOMEM);
		goto end;
	}

	//��������������
	stream_mapping_size = ifmt_ctx->nb_streams;
	stream_mapping = (int *)av_malloc_array(stream_mapping_size, sizeof(*stream_mapping));
	if (!stream_mapping) {
		printf("av_malloc_array failed %s \n", output);
		ret = AVERROR(ENOMEM);
		goto end;
	}

	ofmt = ofmt_ctx->oformat;
	//ȡ����Ƶ/��Ƶ/��Ļ��
	printf("stream_mapping_size>>>>>>>>>>>>>>>%d\n", stream_mapping_size);
	for (int i = 0; i < stream_mapping_size; i++) {
		AVStream *in_stream = ifmt_ctx->streams[i];
		AVCodecParameters *in_codecpar = in_stream->codecpar;
		if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO && in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO && in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
			stream_mapping[i] = -1;
			continue;
		}

		stream_mapping[i] = stream_index++;
		printf("stream_index>>>>>>>>>>>>>>>%d\n", stream_index);
		//����flv��
		out_stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!out_stream) {
			printf("avformat_new_stream failed \ n");
			ret = AVERROR(ENOMEM);
			goto end;
		}

		//��mp4������������flv����
		ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
		if (ret < 0) {
			printf("avcodec_parameters_copy failed \ n");
			ret = AVERROR(ENOMEM);
			goto end;
		}

		out_stream->codecpar->codec_tag = 0;
	}

	//���flv����Ϣ
	av_dump_format(ofmt_ctx, 0, output, 1);

	//������ļ�����д�����
	if (!(ofmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&ofmt_ctx->pb, output, AVIO_FLAG_WRITE);
		if (ret < 0) {
			printf("avio_open failed \ n");
			ret = AVERROR(ENOMEM);
			goto end;
		}
	}

	//д��ͷ��
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		printf("avformat_write_header\n");
		goto end;
	}

	AVPacket pkt;
	while (1) {
		AVStream *in_stream, *out_stream;
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0) {
			break;
		}

		in_stream = ifmt_ctx->streams[pkt.stream_index];
		if (pkt.stream_index >= stream_mapping_size || stream_mapping[pkt.stream_index] < 0) {
			av_packet_unref(&pkt);
			continue;
		}
		pkt.stream_index = stream_mapping[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];

		//����AVPacket
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;

		//д��AVPacket
		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
		if (ret < 0) {
			printf("av_interleaved_write_frame failed \ n");
			break;;
		}

		av_packet_unref(&pkt);

	}

	av_write_trailer(ofmt_ctx);

end:
	avformat_close_input(&ifmt_ctx);

	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_closep(&ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);

	av_freep(&stream_mapping);


	return ret;

	
}


int main(int argc,char *argv[]){
	//sdl_player();

	remuxing();

	return 0;
}

