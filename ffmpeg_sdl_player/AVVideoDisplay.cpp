#include "AVVideoDisplay.h"


//触发刷新事件
uint32_t sdl_refresh_event(uint32_t interval, void *data) {
	SDL_Event event;
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = data;
	SDL_PushEvent(&event);
	return 0;
}

//渲染数据到屏幕
void sdl_refresh(void *user_data) {
	MediaPlayer *mediaPlayer = (MediaPlayer *)user_data;
	AVVideo *video = mediaPlayer->video;
	if (video->stream_index >= 0) {
		if (video->videoPacketQueue->avQueue.empty()) {
			SDL_AddTimer(1, sdl_refresh_event, mediaPlayer);
		} else {
			video->videoFrameQueue.popAVFrame(&video->bufferFrame);

			double current_pts = *(double *)video->bufferFrame->opaque;
			//上一帧结束到当前帧间结束延迟的时间
			double delay = current_pts - video->frame_last_pts;

			//太快或太慢都重置为上一帧的延迟时间
			if (delay <= 0 || delay >= 1.0) {
				delay = video->frame_last_delay;
			}

			video->frame_last_delay = delay;

			video->frame_last_pts = current_pts;

			//音频当前播放的时间
			double ref_clock = mediaPlayer->audio->get_audio_clock();
			//音频和视频差值
			double diff = current_pts - ref_clock;
			//最小同步时间
			double threshold = (delay > SYNC_THRESHOLD) ? delay : SYNC_THRESHOLD;


			if (fabs(diff) < NOSYNC_THRESHOLD) // 不同步
			{
				if (diff <= -threshold) { // 慢了
					delay = 0;
				} else if (diff >= threshold) { // 快了
					delay *= 2;
				}
			}

			video->frame_timer += delay;
			//实际延迟时间
			double actual_delay = video->frame_timer - static_cast<double>(av_gettime()) / 1000000.0;
			if (actual_delay <= 0.010)
				actual_delay = 0.010;

			SDL_AddTimer(static_cast<int>(actual_delay * 1000 + 0.5), sdl_refresh_event, mediaPlayer);


			SwsContext *sws_ctx = sws_getContext(
				video->videoCodecCtx->width,
				video->videoCodecCtx->height,
				video->videoCodecCtx->pix_fmt,
				video->displayFrame->width,
				video->displayFrame->height,
				(AVPixelFormat)video->displayFrame->format,
				SWS_BILINEAR,
				nullptr, nullptr, nullptr);

			sws_scale(sws_ctx,
				(uint8_t const * const *)video->bufferFrame->data,
				video->bufferFrame->linesize,
				0,
				video->videoCodecCtx->height,
				video->displayFrame->data,
				video->displayFrame->linesize);

			SDL_UpdateTexture(video->texture, NULL, video->displayFrame->data[0], video->displayFrame->linesize[0]);
			SDL_RenderClear(video->renderer);

			SDL_RenderCopy(video->renderer, video->texture, NULL, NULL);
			SDL_RenderPresent(video->renderer);

			sws_freeContext(sws_ctx);
			av_frame_unref(video->bufferFrame);
		}
	} else {
		SDL_AddTimer(100, sdl_refresh_event, mediaPlayer);
	}
}