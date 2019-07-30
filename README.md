# ffmpeg_sdl_player
vs版 这是一个virsual studio版的ffmpeg播放器，其中包含两部部分：

1，ffmpeg_player_main：
这个文件入口主要完整实现了ffmpeg同时解码音频和视频，并用sdl进行渲染和播放的流程，其中关键点是对音频和视频播放时进行同步的处理。

2，ffmpeg_demo:
这个文件包含了几个小处理功能：
    单纯解码视频进行播放；
    MP4转FLV；
    截取视频中的某一段；
    yuv编码成.h264；
    PCM编码成AAC；
    视频帧转图片；

