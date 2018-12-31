媒体播放的概念

多媒体文件(.avi, .flv, .ts)被称做container

有些container可以包含很多支的video, audio, subtitle

编码格式(264, 265, aac)被称做codec

video的每张图被称做frame



player的一般flow



ffmpeg的主要组成

AVFormatContext *pFormatCtx 扮演container, 入参可以是文件, 可以是url

AVStream *pStream 扮演video/audio/subtitle未解码的流

AVCodecContext *pCodecCtx 扮演编解码器, 根据不同的codec创建不同的编解码器

AVFrame *pFrame 扮演帧, 且是未被压缩的.

AVPacket packet 扮演包, 是压缩过的.



ffmpeg版本(an应用也调试通过)

3.4 release

https://github.com/FFmpeg/FFmpeg/releases/tag/n3.4



准备工作:

下载ffmpeg     

wget http://ffmpeg.org/releases/ffmpeg-3.4.tar.bz2

或者git clone git@github.com:FFmpeg/FFmpeg.git 

git 下载的都是最新的源码, 不确定性比较高. 最好还是搞个release版本来玩玩先.



介绍./configure的参数

./configure --help > help.cfg

还可以在./ffbuild/config.log查看log



我个人觉得可能要修改的参数

mac编译脚本

export PREFIX=./mac

--prefix=$PREFIX  安装路径

mac 默认出不来ffplay, 需要做下列步骤

brew install automake fdk-aac git libtool libvorbis libvpx opus sdl sdl2 shtool yasm texi2html theora wget x264 xvid lame libass



an编译脚本

export NDK=/Users/knox/Documents/envOrTool/android/android-ndk-r14b

export PLATFORM=$NDK/platforms/android-21/arch-arm

export TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64

export CPU=armv7-a

export PREFIX=./android/$CPU



举例子:

纸上谈兵系列

ffplay的flow

ffmpeg的flv demuxer分析



macOrAn实战系列

player4mac

简单播放器, 播放flv

gcc -o kplay kplay.c -I./include/ -L./lib/ -lavcodec -lavformat -lswscale -lz -lm

可以将视频帧保存成RGB格式的ppm





player4An



相关知识:

nginx



opengl



opengrok

https://github.com/oracle/opengrok/releases/tag/1.1-rc21