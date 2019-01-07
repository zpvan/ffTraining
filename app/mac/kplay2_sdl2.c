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

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;

static unsigned sws_flags = SWS_BICUBIC;

int main(int argc, char *argv[])
{
    // 注册所有的文件格式和编解码器的库
    av_register_all();

    if (argc != 2)
    {
        printf("plz intput file\n");
        return -1;
    }

    // 打开多媒体文件
    if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0)
    {
        printf("open input [file]=[%s] failed!\n", argv[1]);
        return -1;
    }

    // 解析流讯息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        printf("find stream info failed!\n");
        return -1;
    }

    // 打印流讯息
    av_dump_format(pFormatCtx, 0, argv[1], 0);

    // 找到第一条视频流
    int i;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
            break;
        }
    }
    if (videoStream == -1)
    {
        printf("find video stream failed!\n");
        return -1;
    }

    // 获取视频编解码器上下文
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    // 找到对应的视频解码器
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL)
    {
        printf("unsupported [codec]=[%d]!\n", pCodecCtx->codec_id);
        return -1;
    }

    // 打开解码器
    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        printf("could not open codec\n");
        return -1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("could not initialize SDL %s\n", SDL_GetError());
        return -1;
    }

    window = SDL_CreateWindow("My Video Window",
                          SDL_WINDOWPOS_UNDEFINED,
                          SDL_WINDOWPOS_UNDEFINED,
                          pCodecCtx->width, pCodecCtx->height,
                          SDL_WINDOW_OPENGL);
    if (!window)
    {
        printf("SDL: could not create window - exiting\n");
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
    {
        printf("SDL: could not create renderer - exiting\n");
        return -1;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
    void *pixels;
    int pitch;
    SDL_LockTexture(texture, NULL, &pixels, &pitch);
    memset(pixels, 0, pitch * pCodecCtx->height);
    SDL_UnlockTexture(texture);

    // 分配视频帧内存空间
    pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        printf("alloc av frame failed!\n");
        return -1;
    }

    // 读取媒体文件里边的视频包
    int frameFinished;
    AVPacket packet;

    // 接收鼠标点击事件
    SDL_Event event;

    i = 0;
    while(av_read_frame(pFormatCtx, &packet) >= 0)
    {
        // 检查packet是否是video
        if (packet.stream_index == videoStream)
        {
            // 解码视频帧
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // 如果拿到视频帧
            if (frameFinished)
            {
                SDL_UpdateYUVTexture(texture, NULL, pFrame->data[0], pFrame->linesize[0], pFrame->data[1], pFrame->linesize[1],pFrame->data[2], pFrame->linesize[2]);

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }
        }
        // 释放packet, 它是在av_read_frame里边分配内存packet.data
        av_free_packet(&packet);

        SDL_PollEvent(&event);
        switch (event.type)
        {
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

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    // 退出SDL
    SDL_Quit();

    return 0;
}