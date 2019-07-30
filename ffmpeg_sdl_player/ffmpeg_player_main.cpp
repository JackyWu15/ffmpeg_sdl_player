#include <iostream>

extern "C" {
#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libswscale\swscale.h>
#include <libswresample\swresample.h>
#include "libsdl/SDL.h"
#include "libsdl/SDL_thread.h"
#include "libsdl/SDL_video.h"
#include <libavformat\avformat.h>
#include <libavutil\rational.h>
}

#include "AVPacketQueue.h"
#include "MediaPlayer.h"
#include "AVVideoDisplay.h"

using namespace std;


bool quit = false;
int thread_exit = 0;
int thread_pause = 0;


int main(int arv, char *argv[]) {
	int ret;

	//const char * fileName = "Titanic.ts";
	const char * fileName = "cuc_ieschool.mp4";

	MediaPlayer mediaPlayer;
	ret = mediaPlayer.openFile(fileName);
	if (ret) {
		fprintf(stdout,"\n ---------->openfile successful!\n");
	}


	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

	//开子线程
	SDL_CreateThread(decodec_thread, NULL, &mediaPlayer);

	//播放
	mediaPlayer.audio->audio_play();
	mediaPlayer.video->video_play(&mediaPlayer);

	AVStream *audioStream = mediaPlayer.pFormatCtx->streams[mediaPlayer.audio->stream_index];
	AVStream *videoStream = mediaPlayer.pFormatCtx->streams[mediaPlayer.video->stream_index];

	int audio_duration = audioStream->duration*av_q2d(audioStream->time_base);
	int video_duration = videoStream->duration*av_q2d(videoStream->time_base);

	cout << "audio 时长:" << audio_duration << ">>>video 时长:" << video_duration << endl;

	//sdl事件
	SDL_Event event;
	while (true) 
	{
		SDL_WaitEvent(&event);
		switch (event.type) {
		case FF_QUIT_EVENT:
		case SDL_QUIT:
			quit = 1;
			SDL_Quit();
			break;
			break;
		case FF_REFRESH_EVENT:
			if (thread_pause == 0) {
				sdl_refresh(&mediaPlayer);
			}
			break;
		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_SPACE) {
				thread_pause = !thread_pause;
				SDL_PauseAudio(thread_pause);
				if (thread_pause == 0) {
					SDL_AddTimer(1, sdl_refresh_event, &mediaPlayer);
				}
			}
			break;
		case SDL_WINDOWEVENT:
			SDL_GetWindowSize(mediaPlayer.video->window, &(mediaPlayer.video->screen_w), &(mediaPlayer.video->screen_h));
			break;
		case FF_STOP_EVENT:
				thread_exit = 1;
			break;
		default:
			break;
		}
	}


	getchar();
	return 0;
}