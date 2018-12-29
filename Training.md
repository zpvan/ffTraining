媒体播放的概念

多媒体文件(.avi, .flv, .ts)被称做container

有些container可以包含很多支的video, audio, subtitle

编码格式(264, 265, aac)被称做codec

video的每张图被称做frame



player的一般flow



ffmpeg版本(an应用也调试通过)



ffmpeg的主要组成

AVFormatContext *pFormatCtx 扮演container, 入参可以是文件, 可以是url

AVCodecContext *pCodecCtx 扮演编解码器, 根据不同的codec创建不同的编解码器

AVFrame *pFrame 扮演帧, 且是未被压缩的.

AVPacket packet 扮演包, 是压缩过的.



举例子:

纸上谈兵系列

ffplay的flow

ffmpeg的flv demuxer分析



macOrAn实战系列

player4An

encoder4mac



相关知识:

nginx



opengl



opengrok