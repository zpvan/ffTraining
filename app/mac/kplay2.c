#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <SDL.h>
#include <SDL_thread.h>

AVFormatContext *pFormatCtx = NULL;
AVCodecContext *pCodecCtx = NULL;
AVCodec *pCodec = NULL;
AVFrame *pFrame = NULL;
struct SwsContext *img_convert_ctx = NULL;

int videoStream = -1;

SDL_Surface *screen = NULL;
SDL_Overlay *bmp = NULL;

static unsigned sws_flags = SWS_BICUBIC;

int main(int argc, char *argv[]) {
    // 注册所有的文件格式和编解码器的库
    av_register_all();

    if (argc != 2) {
        printf("plz intput file\n");
        return -1;
    }

    // 打开多媒体文件
    if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0) {
        printf("open input [file]=[%s] failed!\n", argv[1]);
        return -1;
    }

    // 解析流讯息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("find stream info failed!\n");
        return -1;
    }

    // 打印流讯息
    av_dump_format(pFormatCtx, 0, argv[1], 0);

    // 找到第一条视频流
    int i;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }
    if (videoStream == -1) {
        printf("find video stream failed!\n");
        return -1;
    }

    // 获取视频编解码器上下文
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    // 找到对应的视频解码器
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL) {
        printf("unsupported [codec]=[%d]!\n", pCodecCtx->codec_id);
        return -1;
    }

    // 打开解码器
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
        printf("could not open codec\n");
        return -1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf("could not initialize SDL %s\n", SDL_GetError());
        return -1;
    }

    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
    if (!screen) {
        printf("SDL: could not set video mode - exiting\n");
        return -1;
    }

    bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height,SDL_YV12_OVERLAY, screen);

    // 分配视频帧内存空间
    pFrame = av_frame_alloc();
    if (pFrame == NULL) {
        printf("alloc av frame failed!\n");
        return -1;
    }

    // 读取媒体文件里边的视频包
    int frameFinished;
    AVPacket packet;

    // 配置窗口的位置
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = pCodecCtx->width;
    rect.h = pCodecCtx->height;

    // 接收鼠标点击事件
    SDL_Event event;

    i = 0;
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // 检查packet是否是video
        if (packet.stream_index == videoStream) {
            // 解码视频帧
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // 如果拿到视频帧
            if (frameFinished) {
                SDL_LockYUVOverlay(bmp);
                
                // 像素pixel  间距pitch
                AVPicture pict;
                pict.data[0] = bmp->pixels[0];
                pict.data[1] = bmp->pixels[2];
                pict.data[2] = bmp->pixels[1];

                pict.linesize[0] = bmp->pitches[0];
                pict.linesize[1] = bmp->pitches[2];
                pict.linesize[2] = bmp->pitches[1];
                // 解码后的视频帧转换成YUV420P格式
                img_convert_ctx = sws_getCachedContext(img_convert_ctx, pFrame->width, pFrame->height, pFrame->format, pFrame->width, pFrame->height, AV_PIX_FMT_YUV420P, sws_flags, NULL, NULL, NULL);
                if (img_convert_ctx != NULL) {
                    sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pict.data, pict.linesize);
                }

                SDL_UnlockYUVOverlay(bmp);

                SDL_DisplayYUVOverlay(bmp, &rect);
            }
        }
        // 释放packet, 它是在av_read_frame里边分配内存packet.data
        av_free_packet(&packet);

        SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                SDL_Quit();
                exit(0);
                break;

            default:
                break;
        }
    }

    // 释放yuv image
    av_free(pFrame);

    // 关闭codec
    avcodec_close(pCodecCtx);

    // 关闭视频文件
    avformat_close_input(&pFormatCtx);

    // 释放sws
    sws_freeContext(img_convert_ctx);

    // 退出SDL
    SDL_Quit();

    return 0;
}