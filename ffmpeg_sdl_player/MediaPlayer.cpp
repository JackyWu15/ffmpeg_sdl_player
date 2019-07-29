#include "MediaPlayer.h"

extern "C" {
#include <libavutil\time.h>
#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libavutil\rational.h>

}
#include "libsdl\SDL_timer.h"

MediaPlayer::MediaPlayer() {
	this->pFormatCtx = nullptr;
	this->audio = new AVAudio();
	this->video = new AVVideo();
	

}


MediaPlayer::~MediaPlayer() {
	if (this->audio) {
		free(this->audio);
	}

	if (this->video) {
		free(this->video);
	}
}

//打开文件，音视频解码器准备
bool MediaPlayer::openFile(const char * fileName) {
	int ret = 0;

	av_register_all();

	ret = avformat_open_input(&pFormatCtx, fileName, nullptr, nullptr);
	if (ret < 0) {
		return false;
	}

	ret = avformat_find_stream_info(pFormatCtx, nullptr);
	if (ret < 0) {
		return false;
	}


	av_dump_format(pFormatCtx, 0, fileName, 0);

	for (int i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && this->audio->stream_index == -1) {
			this->audio->stream_index = i;
		}

		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && this->video->stream_index == -1) {
			this->video->stream_index = i;
		}

	}

	if (this->video->stream_index < 0 || this->audio->stream_index < 0) {
		return false;
	}

	//音频解码准备

	this->audio->audioStream = pFormatCtx->streams[this->audio->stream_index];
	AVCodecParameters *audioCodecpar = this->audio->audioStream->codecpar;

	this->audio->audioCodec = avcodec_find_decoder(audioCodecpar->codec_id);
	if (!this->audio->audioCodec) {
		return false;
	}

	this->audio->audioCodecCtx = avcodec_alloc_context3(this->audio->audioCodec);

	//旧版本
	//ret = avcodec_copy_context(this->audioCodecCtx, pFormatCtx->streams[this->audio->stream_index]->codec);
	//新版本
	ret = avcodec_parameters_to_context(this->audio->audioCodecCtx, audioCodecpar);
	if (ret <0) {
		return false;
	}

	ret = avcodec_open2(this->audio->audioCodecCtx, this->audio->audioCodec, nullptr);
	if (ret < 0) {
		return false;
	}

	//视频解码器准备
	this->video->videoStream = pFormatCtx->streams[this->video->stream_index];
	AVCodecParameters *videoCodecpar = this->video->videoStream->codecpar;

	this->video->videoCodec = avcodec_find_decoder(videoCodecpar->codec_id);
	if (!this->video->videoCodec) {
		return false;
	}

	this->video->videoCodecCtx = avcodec_alloc_context3(this->video->videoCodec);

	ret = avcodec_parameters_to_context(this->video->videoCodecCtx, videoCodecpar);
	if (ret < 0) {
		return false;
	}

	ret = avcodec_open2(this->video->videoCodecCtx, this->video->videoCodec, nullptr);
	if (ret < 0) {
		return false;
	}

	//当前时间
	this->video->frame_timer = static_cast<double>(av_gettime()) / 1000000.0;
	//上一帧的延迟
	this->video->frame_last_delay = 40e-3;
	return true;
}




//解码音频和视频数据                                                                   

int decodec_thread(void * data) {
	int ret = 0;
	AVPacket *pkt = av_packet_alloc();

	MediaPlayer *mediaPlayer = (MediaPlayer *)data;

	 while (true) {
		 //return 0 if OK, < 0 on error or end of file
		ret =  av_read_frame(mediaPlayer->pFormatCtx, pkt);
		if (ret < 0) {
			//文件末尾
			if (ret == AVERROR_EOF) {
				break;
			//非错误，继续执行
			}else if (mediaPlayer->pFormatCtx->pb->error == 0) {
				SDL_Delay(100);
				continue;
			//错误
			} else {
				break;
			}
		}

		if (pkt->stream_index == mediaPlayer->audio->stream_index) {
			mediaPlayer->audio->audioQueue->pushAVPacket(pkt);
		} else if (pkt->stream_index == mediaPlayer->video->stream_index) {
			mediaPlayer->video->videoPacketQueue->pushAVPacket(pkt);
		}
		av_packet_unref(pkt);
	 }

	 av_packet_free(&pkt);

	return 0;
}

