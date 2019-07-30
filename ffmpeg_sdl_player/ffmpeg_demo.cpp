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
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
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
	int length_size = (*extradata++ & 0x3) + 1; // retrieve length coded size, 用于指示表示编码数据长度所需字节数

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
		//触发事件
		SDL_PushEvent(&event);
		//延迟速度,每秒25帧
		SDL_Delay(40);
	}
	thread_exit_status = 0;
	//退出播放
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
/* sdl播放器                                                               */
/************************************************************************/
int sdl_player(const char *file_input) {

	av_register_all();

	avformat_network_init();

	//全局的上下文，保存视频文件的封装
	AVFormatContext* avformatContext = avformat_alloc_context();

	//打开文件
	//int avformat_open_input(AVFormatContext **ps, const char *url, ff_const59 AVInputFormat *fmt, AVDictionary **options);
	if (avformat_open_input(&avformatContext, file_input, NULL, NULL) != 0) {
		printf("avformat_open_input error \n");
		return  -1;
	}



	//内部实际上简单做了整个完整的解码流程
	if (avformat_find_stream_info(avformatContext, NULL) < 0) {
		printf("avformat_find_stream_info error \n");
		return  -1;
	}

	/*----------------用于将音视频数据格式通过av_log输出到指定的文件或者控制台,方便了解输入的视音频格式，删除该函数的调用没有任何影响---------------------*/
	/*-------------------------也可以avformat_find_stream_info后，通过AVFomatContext调用相关函数打印需要的信息----------------------*/
	printf("--------------- File Information ----------------\n");
	av_dump_format(avformatContext, 0, file_input, 0);
	printf("-------------------------------------------------\n");

	/*----------------用于打印流信息到控制台---------------------*/

	int videoIndex = -1;
	////遍历媒体流
	int i = 0;
	for (i = 0; i < avformatContext->nb_streams; i++) {
		if (avformatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
		}
	}

	//av_find_best_stream也可以找到需要的流
	//videoIndex = av_find_best_stream(avformatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

	if (videoIndex == -1) {
		printf("find AVMEDIA_TYPE_VIDEO error \n");
		return  -1;
	}

	//取出解封装后的压缩流上下文AVCodecContext
	AVCodecParameters *avcodePs = avformatContext->streams[videoIndex]->codecpar;
	//找到解码器
	AVCodec *avcodec = avcodec_find_decoder(avcodePs->codec_id);
	if (avcodec == NULL) {
		printf("avcodec_find_decoder error \n");
		return  -1;
	}

	//AVCodecContext *avcodecContext = avformatContext->streams[videoIndex]->codec;
	AVCodecContext *avcodecContext = avcodec_alloc_context3(avcodec);

	avcodec_parameters_to_context(avcodecContext, avcodePs);

	//初始化解码器
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

	//一帧缓存大小
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, avcodecContext->width, avcodecContext->height));

	avpicture_fill((AVPicture*)avframeYUV, out_buffer, AV_PIX_FMT_YUV420P, avcodecContext->width, avcodecContext->height);

		avpacket = (AVPacket *)av_malloc(sizeof(AVPacket));

	

	//图片处理器上下文
	SwsContext * swscontext = sws_getContext(avcodecContext->width, avcodecContext->height, avcodecContext->pix_fmt,
		avcodecContext->width, avcodecContext->height, AV_PIX_FMT_YUV420P, 4, NULL, NULL, NULL);

	FILE *fp_output_h264 = fopen("output_h264.h264", "wb+");
	FILE *fp_output_yuv = fopen("output_yuv.yuv", "wb+");

	//FILE *fp_in = fopen("out_yuv.yuv", "rb+");

	int screenWidth = avcodecContext->width, screenHeight = avcodecContext->height;

	int pix_picture = screenWidth * screenHeight * 3 / 2;
	//初始化SDL
	if (SDL_Init(SDL_INIT_VIDEO)) {
		printf("SDL_Init_Error %s\n", SDL_GetError());
		return -1;
	}

	//创建窗体
	SDL_Window *screen = SDL_CreateWindow("SDL_PLAYER", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

	if (!screen) {
		printf("SDL_CreateWindow_Error %s\n", SDL_GetError());
		return -1;
	}

	//创建渲染器
	SDL_Renderer *renderer = SDL_CreateRenderer(screen, -1, 0);

	SDL_RenderClear(renderer);

	//YUV排列方式
	Uint32 pixformat = SDL_PIXELFORMAT_IYUV;

	//创建纹理
	SDL_Texture *texture = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING, screenWidth, screenHeight);

	//视图的属性
	SDL_Rect sdlRect;

	//开始渲染
	unsigned char *buffer = (unsigned char *)malloc(pix_picture);

	//开启子线程
	SDL_CreateThread(refresh_video, NULL, NULL);
	SDL_Event event;

	//解封装
	int got_picture_ptr = 0, ret = 0, fram_count = 0;

	while (1) {
		//阻塞等待事件触发
		SDL_WaitEvent(&event);
		switch (event.type) {
		case REFRESH_VIDEO:
			while (1) {

				if (av_read_frame(avformatContext, avpacket) < 0)
					//if(avcodec_send_packet(avcodecContext,avpacket)<0)
					thread_exit_status = 1;
				//去除音频帧，不然会出现卡顿
				if (avpacket->stream_index == videoIndex)
					break;
			}

			//if (av_read_frame(avformatContext, avpacket) >= 0) {
				//if (avpacket->stream_index == videoIndex) {

					//fwrite(avpacket->data, 1, avpacket->size, fp_output_h264);//保存h264裸流，只适用ts
			h264_mp4toannexb(avformatContext, avpacket, fp_output_h264);//保存h264裸流，适用mp4,flv,avi,不适用ts,mkv
			//解码
			ret = avcodec_decode_video2(avcodecContext, avframe, &got_picture_ptr, avpacket);
			//avcodec_receive_frame(avcodecContext, avframe);

			if (ret < 0) {
				printf("avcodec_decode_video2 error \n");
				return  -1;
			}

			if (got_picture_ptr) {
				//裁剪黑边，没效果
				sws_scale(swscontext, (const uint8_t* const*)avframe->data, avframe->linesize, 0, avcodecContext->height,
					avframeYUV->data, avframeYUV->linesize);

				fwrite(avframe->data[0], 1, avcodecContext->width*avcodecContext->height, fp_output_yuv);
				fwrite(avframe->data[1], 1, avcodecContext->width*avcodecContext->height / 4, fp_output_yuv);
				fwrite(avframe->data[2], 1, avcodecContext->width*avcodecContext->height / 4, fp_output_yuv);


				//将数据读进Texture中,为什么是[0]?
				SDL_UpdateTexture(texture, NULL, avframeYUV->data[0], avframeYUV->linesize[0]);
				sdlRect.x = 0;
				sdlRect.y = 0;
				sdlRect.w = screenWidth;
				sdlRect.h = screenHeight;

				SDL_RenderClear(renderer);
				//将Texture加载到Renderer中
				SDL_RenderCopy(renderer, texture, NULL, &sdlRect);
				//渲染到屏幕
				SDL_RenderPresent(renderer);


				fram_count++;

				//printf("输出第%d帧:\n", fram_count);
			}
			//	}
				//av_free_packet(avpacket);
			av_packet_unref(avpacket);
			/*} else {
				thread_exit_status = 1;
			}*/

			//循环播放
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
/* mp4转flv                                                              */
/************************************************************************/
int remuxing(const char *input,const char *output) {
	int ret=0;
	AVFormatContext *ifmt_ctx=NULL,* ofmt_ctx=NULL;
	AVOutputFormat *ofmt = NULL;


	int stream_mapping_size = 0;
	int *stream_mapping = 0;
	int stream_index = 0;
	AVStream *out_stream = NULL;


	

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

	//创建flv流上下文
	ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output);
	if (ret <0) {
		printf("avformat_alloc_output_context2 failed %s \n", output);
		ret = AVERROR(ENOMEM);
		goto end;
	}

	//创建流个数数组
	stream_mapping_size = ifmt_ctx->nb_streams;
	stream_mapping = (int *)av_malloc_array(stream_mapping_size, sizeof(*stream_mapping));
	if (!stream_mapping) {
		printf("av_malloc_array failed %s \n", output);
		ret = AVERROR(ENOMEM);
		goto end;
	}

	ofmt = ofmt_ctx->oformat;
	//取出音频/视频/字幕流
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
		//创建flv流
		out_stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!out_stream) {
			printf("avformat_new_stream failed \ n");
			ret = AVERROR(ENOMEM);
			goto end;
		}

		//将mp4流参数拷贝到flv流中
		ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
		if (ret < 0) {
			printf("avcodec_parameters_copy failed \ n");
			ret = AVERROR(ENOMEM);
			goto end;
		}

		out_stream->codecpar->codec_tag = 0;
	}

	//输出flv的信息
	av_dump_format(ofmt_ctx, 0, output, 1);

	//打开输出文件进行写入操作
	if (!(ofmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&ofmt_ctx->pb, output, AVIO_FLAG_WRITE);
		if (ret < 0) {
			printf("avio_open failed \ n");
			ret = AVERROR(ENOMEM);
			goto end;
		}
	}

	//写入头部
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

		//拷贝AVPacket,转换pts和dts
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;

		//写入AVPacket
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


/************************************************************************/
/* 截取一段视频                                                          */
/************************************************************************/
int cut_video(double begin_seconds, double end_seconds, const char *input, const char *output) {

	int ret = 0;
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVOutputFormat *ofmt = NULL;


	int stream_mapping_size = 0;
	int *stream_mapping = 0;
	int stream_index = 0;

	int64_t *dts_start_from = NULL;
	int64_t *pts_start_from = NULL;

	int i = 0;

	av_register_all();

	ret = avformat_open_input(&ifmt_ctx, input, 0, 0);
	if (ret != 0) {
		printf("avformat_open_input failed %s \n", input);
		ret = AVERROR(ENOMEM);
		goto end;
	} else {
		dts_start_from = (int64_t *)malloc(sizeof(int64_t)*ifmt_ctx->nb_streams);
		pts_start_from = (int64_t *)malloc(sizeof(int64_t)*ifmt_ctx->nb_streams);
	}

	ret = avformat_find_stream_info(ifmt_ctx, 0);
	if (ret < 0) {
		printf("avformat_find_stream_info failed %s \n", input);
		ret = AVERROR(ENOMEM);
		goto end;
	}
	av_dump_format(ifmt_ctx, 0, input, 0);


	avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output);
	if (!ofmt_ctx) {
		printf( "Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		goto end;
	}

	ofmt = ofmt_ctx->oformat;
	
	for (i = 0; i < ifmt_ctx->nb_streams; i++) {
		AVStream *in_stream = ifmt_ctx->streams[i];
		AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
		if (!out_stream) {
			printf("avformat_new_stream failed \n");
			ret = AVERROR(ENOMEM);
			goto end;
		}

		ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
		if (ret < 0) {
			printf("avcodec_parameters_copy failed\n");
			ret = AVERROR(ENOMEM);
			goto end;
		}
		ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
		if (ret < 0) {
			printf("avcodec_copy_context failed\n");
			ret = AVERROR(ENOMEM);
			goto end;
		}

		out_stream->codec->codec_tag = 0;
		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
			out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}
	}

	av_dump_format(ofmt_ctx, 0, output, 1);

	if (!(ofmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&ofmt_ctx->pb, output, AVIO_FLAG_WRITE);
		if (ret < 0) {
			printf("avio_open failed \ n");
			ret = AVERROR(ENOMEM);
			goto end;
		}
	}

	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		printf("avformat_write_header failed\n");
		goto end;
	}

	//seek到指定位置
	ret = av_seek_frame(ifmt_ctx, -1, begin_seconds*AV_TIME_BASE, AVSEEK_FLAG_ANY);
	if (ret < 0) {
		printf("av_seek_frame failed\n");
		goto end;
	}

	memset(dts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);
	memset(pts_start_from, 0, sizeof(int64_t) * ifmt_ctx->nb_streams);


	AVPacket pkt;
	while (1) {
		AVStream *in_stream, *out_stream;

		//读数据
		ret = av_read_frame(ifmt_ctx, &pkt);
		in_stream = ifmt_ctx->streams[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];

		//超过截取时间，就结束读取
		if (av_q2d(in_stream->time_base) * pkt.pts > end_seconds) {
			av_free_packet(&pkt);
			break;
		}

		if (dts_start_from[pkt.stream_index] == 0) {
			dts_start_from[pkt.stream_index] = pkt.dts;
		}

		if (pts_start_from[pkt.stream_index] == 0) {
			pts_start_from[pkt.stream_index] = pkt.pts;
		}

		pkt.pts = av_rescale_q_rnd(pkt.pts - pts_start_from[pkt.stream_index], in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts - dts_start_from[pkt.stream_index], in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

		if (pkt.pts < 0) {
			pkt.pts = 0;
		}
		if (pkt.dts < 0) {
			pkt.dts = 0;
		}

		pkt.duration = (int)av_rescale_q((int64_t)pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;


		//写数据
		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
		if (ret < 0) {
			printf("av_interleaved_write_frame failed\n");
			break;
		}
		av_free_packet(&pkt);
	}

	free(dts_start_from);
	free(pts_start_from);

	av_write_trailer(ofmt_ctx);
end:
	avformat_close_input(&ifmt_ctx);

	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_closep(&ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);

	if (ret < 0 && ret != AVERROR_EOF) {
		//fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
		return 1;
	}
	return 0;
}


/************************************************************************/
/* yuv编码.h264														  */
/************************************************************************/

int encode_video(const char *input,const char *output) {
	const char *filename, *codec_name;
	const AVCodec *codec;
	AVCodecContext *c = NULL;
	int i, ret, x, y, got_output;
	FILE *f;
	AVFrame *frame;
	AVPacket pkt;
	uint8_t endcode[] = { 0, 0, 1, 0xb7 };

	filename = input;
	codec_name = output;

	//只需注册编解码器
	avcodec_register_all();

	//根据编解码器名字找到编解码器
	codec = avcodec_find_encoder_by_name(codec_name);
	if (!codec) {
		fprintf(stderr, "Codec not found\n");
		exit(1);
	}
	//创建编解码器上下文
	c = avcodec_alloc_context3(codec);
	if (!c) {
		fprintf(stderr, "Could not allocate video codec context\n");
		exit(1);
	}

	//码率
	c->bit_rate = 400000;

	//分辨率
	c->width = 320;
	c->height = 480;

	//时间基
	c->time_base = AVRational{ 1, 25 };

	//帧率
	c->framerate = AVRational{ 25, 1 };

	//组帧，也即每10帧一个关键帧
	c->gop_size = 10;
	//b帧数量
	c->max_b_frames = 1;
	//yuv格式
	c->pix_fmt = AV_PIX_FMT_YUV420P;


	//如果编解码器是h264,设置预压缩为慢速，提高压缩质量,格式有ultrafast、superfast、veryfast、faster、fast、medium、slow、slower、veryslow、placebo，
	if (codec->id == AV_CODEC_ID_H264)
		av_opt_set(c->priv_data, "preset", "slow", 0);

	//打开编码器
	if (avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "Could not open codec\n");
		exit(1);
	}


	f = fopen(filename, "wb");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", filename);
		exit(1);
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	frame->format = c->pix_fmt;
	frame->width = c->width;
	frame->height = c->height;

	ret = av_frame_get_buffer(frame, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate the video frame data\n");
		exit(1);
	}

	/* encode 1 second of video */
	for (i = 0; i < 25; i++) {
		av_init_packet(&pkt);
		pkt.data = NULL;    // packet data will be allocated by the encoder
		pkt.size = 0;

		fflush(stdout);

		/* make sure the frame data is writable */
		ret = av_frame_make_writable(frame);
		if (ret < 0)
			exit(1);

		/* prepare a dummy image */
		/* Y */
		for (y = 0; y < c->height; y++) {
			for (x = 0; x < c->width; x++) {
				frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
			}
		}

		/* Cb and Cr */
		for (y = 0; y < c->height / 2; y++) {
			for (x = 0; x < c->width / 2; x++) {
				frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
				frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
			}
		}

		frame->pts = i;

		/* encode the image */
		ret = avcodec_encode_video2(c, &pkt, frame, &got_output);
		if (ret < 0) {
			fprintf(stderr, "Error encoding frame\n");
			exit(1);
		}

		if (got_output) {
			printf("Write frame %3d (size=%5d)\n", i, pkt.size);
			fwrite(pkt.data, 1, pkt.size, f);
			av_packet_unref(&pkt);
		}
		printf("..................%d\n", got_output);
	}

	//编码器中会缓存一些数据，要重新取出，上面设定的编码速度越慢，编码器中缓存的Packet也会越多
	for (got_output = 1; got_output; i++) {
		
		fflush(stdout);

		ret = avcodec_encode_video2(c, &pkt, NULL, &got_output);
		if (ret < 0) {
			fprintf(stderr, "Error encoding frame\n");
			exit(1);
		}

		if (got_output) {
			printf("Write frame %3d (size=%5d)\n", i, pkt.size);
			fwrite(pkt.data, 1, pkt.size, f);
			av_packet_unref(&pkt);
		}

		printf(">>>>>>>>>>>>>>>>>>>>%d\n", got_output);
	}

	/* add sequence end code to have a real MPEG file */
	fwrite(endcode, 1, sizeof(endcode), f);
	fclose(f);

	avcodec_free_context(&c);
	av_frame_free(&frame);
}




#define INBUF_SIZE 4096

#define WORD uint16_t
#define DWORD uint32_t
#define LONG int32_t

#pragma pack(2)
typedef struct tagBITMAPFILEHEADER {
	WORD  bfType;
	DWORD bfSize;
	WORD  bfReserved1;
	WORD  bfReserved2;
	DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;


typedef struct tagBITMAPINFOHEADER {
	DWORD biSize;
	LONG  biWidth;
	LONG  biHeight;
	WORD  biPlanes;
	WORD  biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG  biXPelsPerMeter;
	LONG  biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

void saveBMP(struct SwsContext *img_convert_ctx, AVFrame *frame, char *filename) {
	//1 先进行转换,  YUV420=>RGB24:
	int w = frame->width;
	int h = frame->height;


	int numBytes = avpicture_get_size(AV_PIX_FMT_BGR24, w, h);
	uint8_t *buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));


	AVFrame *pFrameRGB = av_frame_alloc();
	/* buffer is going to be written to rawvideo file, no alignment */
   /*
   if (av_image_alloc(pFrameRGB->data, pFrameRGB->linesize,
							 w, h, AV_PIX_FMT_BGR24, pix_fmt, 1) < 0) {
	   fprintf(stderr, "Could not allocate destination image\n");
	   exit(1);
   }
   */
	avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_BGR24, w, h);

	sws_scale(img_convert_ctx, frame->data, frame->linesize,
		0, h, pFrameRGB->data, pFrameRGB->linesize);

	//2 构造 BITMAPINFOHEADER
	BITMAPINFOHEADER header;
	header.biSize = sizeof(BITMAPINFOHEADER);


	header.biWidth = w;
	header.biHeight = h * (-1);
	header.biBitCount = 24;
	header.biCompression = 0;
	header.biSizeImage = 0;
	header.biClrImportant = 0;
	header.biClrUsed = 0;
	header.biXPelsPerMeter = 0;
	header.biYPelsPerMeter = 0;
	header.biPlanes = 1;

	//3 构造文件头
	BITMAPFILEHEADER bmpFileHeader = { 0, };
	//HANDLE hFile = NULL;
	DWORD dwTotalWriten = 0;
	DWORD dwWriten;

	bmpFileHeader.bfType = 0x4d42; //'BM';
	bmpFileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + numBytes;
	bmpFileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	FILE* pf = fopen(filename, "wb");
	fwrite(&bmpFileHeader, sizeof(BITMAPFILEHEADER), 1, pf);
	fwrite(&header, sizeof(BITMAPINFOHEADER), 1, pf);
	fwrite(pFrameRGB->data[0], 1, numBytes, pf);
	fclose(pf);


	//释放资源
	//av_free(buffer);
	av_freep(&pFrameRGB[0]);
	av_free(pFrameRGB);
}

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,char *filename) {
	FILE *f;
	int i;

	f = fopen(filename, "w");
	fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
	for (i = 0; i < ysize; i++)
		fwrite(buf + i * wrap, 1, xsize, f);
	fclose(f);
}

static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,struct SwsContext *img_convert_ctx, AVFrame *frame, int *frame_count, AVPacket *pkt, int last) {
	int len, got_frame;
	char buf[1024];

	len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
	if (len < 0) {
		fprintf(stderr, "Error while decoding frame %d\n", *frame_count);
		return len;
	}
	if (got_frame) {
		printf("Saving %sframe %3d\n", last ? "last " : "", *frame_count);
		fflush(stdout);

		/* the picture is allocated by the decoder, no need to free it */
		snprintf(buf, sizeof(buf), "%s-%d.bmp", outfilename, *frame_count);

		/*
		pgm_save(frame->data[0], frame->linesize[0],
				 frame->width, frame->height, buf);
		*/

		saveBMP(img_convert_ctx, frame, buf);

		(*frame_count)++;
	}
	if (pkt->data) {
		pkt->size -= len;
		pkt->data += len;
	}
	return 0;
}


/************************************************************************/
/* 视频转图片														  */
/************************************************************************/

int decode_video(const char *input, const char *output) {
	int ret;

	FILE *f;

	const char *filename, *outfilename;

	AVFormatContext *fmt_ctx = NULL;

	const AVCodec *codec;
	AVCodecContext *c = NULL;

	AVStream *st = NULL;
	int stream_index;

	int frame_count;
	AVFrame *frame;

	struct SwsContext *img_convert_ctx;

	//uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
	AVPacket avpkt;

	filename = input;
	outfilename = output;

	/* register all formats and codecs */
	av_register_all();

	/* open input file, and allocate format context */
	if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
		fprintf(stderr, "Could not open source file %s\n", filename);
		exit(1);
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		exit(1);
	}

	/* dump input information to stderr */
	av_dump_format(fmt_ctx, 0, filename, 0);

	av_init_packet(&avpkt);

	/* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
	//memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	//

	ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not find %s stream in input file '%s'\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO), filename);
		return ret;
	}

	stream_index = ret;
	st = fmt_ctx->streams[stream_index];

	/* find decoder for the stream */
	codec = avcodec_find_decoder(st->codecpar->codec_id);
	if (!codec) {
		fprintf(stderr, "Failed to find %s codec\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
		return AVERROR(EINVAL);
	}


	/* find the MPEG-1 video decoder */
	/*
	codec = avcodec_find_decoder(AV_CODEC_ID_MPEG1VIDEO);
	if (!codec) {
		fprintf(stderr, "Codec not found\n");
		exit(1);
	}
	*/

	c = avcodec_alloc_context3(NULL);
	if (!c) {
		fprintf(stderr, "Could not allocate video codec context\n");
		exit(1);
	}

	/* Copy codec parameters from input stream to output codec context */
	if ((ret = avcodec_parameters_to_context(c, st->codecpar)) < 0) {
		fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
		return ret;
	}


	/*
	if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
		c->flags |= AV_CODEC_FLAG_TRUNCATED; // we do not send complete frames
	*/

	/* For some codecs, such as msmpeg4 and mpeg4, width and height
	   MUST be initialized there because this information is not
	   available in the bitstream. */

	   /* open it */
	if (avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "Could not open codec\n");
		exit(1);
	}

	/*
	f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", filename);
		exit(1);
	}
	*/

	img_convert_ctx = sws_getContext(c->width, c->height,
		c->pix_fmt,
		c->width, c->height,
		AV_PIX_FMT_RGB24,
		SWS_BICUBIC, NULL, NULL, NULL);

	if (img_convert_ctx == NULL) {
		fprintf(stderr, "Cannot initialize the conversion context\n");
		exit(1);
	}

	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}

	frame_count = 0;
	while (av_read_frame(fmt_ctx, &avpkt) >= 0) {
		/*
		avpkt.size = fread(inbuf, 1, INBUF_SIZE, f);
		if (avpkt.size == 0)
			break;
		*/

		/* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
		   and this is the only method to use them because you cannot
		   know the compressed data size before analysing it.

		   BUT some other codecs (msmpeg4, mpeg4) are inherently frame
		   based, so you must call them with all the data for one
		   frame exactly. You must also initialize 'width' and
		   'height' before initializing them. */

		   /* NOTE2: some codecs allow the raw parameters (frame size,
			  sample rate) to be changed at any frame. We handle this, so
			  you should also take care of it */

			  /* here, we use a stream based decoder (mpeg1video), so we
				 feed decoder and see if it could decode a frame */
				 //avpkt.data = inbuf;
				 //while (avpkt.size > 0)
		if (avpkt.stream_index == stream_index) {
			if (decode_write_frame(outfilename, c, img_convert_ctx, frame, &frame_count, &avpkt, 0) < 0)
				exit(1);
		}

		av_packet_unref(&avpkt);
	}

	/* Some codecs, such as MPEG, transmit the I- and P-frame with a
	   latency of one frame. You must do the following to have a
	   chance to get the last frame of the video. */
	avpkt.data = NULL;
	avpkt.size = 0;
	decode_write_frame(outfilename, c, img_convert_ctx, frame, &frame_count, &avpkt, 1);

	fclose(f);

	avformat_close_input(&fmt_ctx);

	sws_freeContext(img_convert_ctx);
	avcodec_free_context(&c);
	av_frame_free(&frame);
}





static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt) {
	const enum AVSampleFormat *p = codec->sample_fmts;

	while (*p != AV_SAMPLE_FMT_NONE) {
		if (*p == sample_fmt)
			return 1;
		p++;
	}
	return 0;
}

/* just pick the highest supported samplerate */
static int select_sample_rate(const AVCodec *codec) {
	const int *p;
	int best_samplerate = 0;

	if (!codec->supported_samplerates)
		return 44100;

	p = codec->supported_samplerates;
	while (*p) {
		if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
			best_samplerate = *p;
		p++;
	}
	return best_samplerate;
}

/* select layout with the highest channel count */
static int select_channel_layout(const AVCodec *codec) {
	const uint64_t *p;
	uint64_t best_ch_layout = 0;
	int best_nb_channels = 0;

	if (!codec->channel_layouts)
		return AV_CH_LAYOUT_STEREO;

	p = codec->channel_layouts;
	while (*p) {
		int nb_channels = av_get_channel_layout_nb_channels(*p);

		if (nb_channels > best_nb_channels) {
			best_ch_layout = *p;
			best_nb_channels = nb_channels;
		}
		p++;
	}
	return best_ch_layout;
}

/************************************************************************/
/* 编码AAC													  */
/************************************************************************/

int encode_audio(const char *output) {
	const char *filename;
	const AVCodec *codec;
	AVCodecContext *c = NULL;
	AVFrame *frame;
	AVPacket pkt;
	int i, j, k, ret, got_output;
	FILE *f;
	uint16_t *samples;
	float t, tincr;

	filename = output;

	/* register all the codecs */
	avcodec_register_all();

	/* find the MP2 encoder */
	codec = avcodec_find_encoder(AV_CODEC_ID_MP2);
	if (!codec) {
		fprintf(stderr, "Codec not found\n");
		exit(1);
	}

	c = avcodec_alloc_context3(codec);
	if (!c) {
		fprintf(stderr, "Could not allocate audio codec context\n");
		exit(1);
	}

	/* put sample parameters */
	c->bit_rate = 64000;

	/* check that the encoder supports s16 pcm input */
	c->sample_fmt = AV_SAMPLE_FMT_S16;
	if (!check_sample_fmt(codec, c->sample_fmt)) {
		fprintf(stderr, "Encoder does not support sample format %s",
			av_get_sample_fmt_name(c->sample_fmt));
		exit(1);
	}

	/* select other audio parameters supported by the encoder */
	c->sample_rate = select_sample_rate(codec);
	c->channel_layout = select_channel_layout(codec);
	c->channels = av_get_channel_layout_nb_channels(c->channel_layout);

	/* open it */
	if (avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "Could not open codec\n");
		exit(1);
	}

	f = fopen(filename, "wb");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", filename);
		exit(1);
	}

	/* frame containing input raw audio */
	frame = av_frame_alloc();
	if (!frame) {
		fprintf(stderr, "Could not allocate audio frame\n");
		exit(1);
	}

	frame->nb_samples = c->frame_size;
	frame->format = c->sample_fmt;
	frame->channel_layout = c->channel_layout;

	/* allocate the data buffers */
	ret = av_frame_get_buffer(frame, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate audio data buffers\n");
		exit(1);
	}

	/* encode a single tone sound */
	t = 0;
	tincr = 2 * M_PI * 440.0 / c->sample_rate;
	for (i = 0; i < 200; i++) {
		av_init_packet(&pkt);
		pkt.data = NULL; // packet data will be allocated by the encoder
		pkt.size = 0;

		/* make sure the frame is writable -- makes a copy if the encoder
		 * kept a reference internally */
		ret = av_frame_make_writable(frame);
		if (ret < 0)
			exit(1);
		samples = (uint16_t*)frame->data[0];

		for (j = 0; j < c->frame_size; j++) {
			samples[2 * j] = (int)(sin(t) * 10000);

			for (k = 1; k < c->channels; k++)
				samples[2 * j + k] = samples[2 * j];
			t += tincr;
		}
		/* encode the samples */
		ret = avcodec_encode_audio2(c, &pkt, frame, &got_output);
		if (ret < 0) {
			fprintf(stderr, "Error encoding audio frame\n");
			exit(1);
		}
		if (got_output) {
			fwrite(pkt.data, 1, pkt.size, f);
			av_packet_unref(&pkt);
		}
	}

	/* get the delayed frames */
	for (got_output = 1; got_output; i++) {
		ret = avcodec_encode_audio2(c, &pkt, NULL, &got_output);
		if (ret < 0) {
			fprintf(stderr, "Error encoding frame\n");
			exit(1);
		}

		if (got_output) {
			fwrite(pkt.data, 1, pkt.size, f);
			av_packet_unref(&pkt);
		}
	}
	fclose(f);

	av_frame_free(&frame);
	avcodec_free_context(&c);
}

int main1(int argc,char *argv[]){
	sdl_player("cuc_ieschool.mp4");

	//remuxing("cuc_ieschool.mp4", "cuc_ieschool_remux.flv");

	//cut_video(3, 15, "cuc_ieschool.mp4", "cuc_ieschool_cut.mp4");

	//encode_video("1.h264", "libx264");

	//decode_video("cuc_ieschool.mp4", "./");

	//encode_audio("1.aac");

	return 0;
}

