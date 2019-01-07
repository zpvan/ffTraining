#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>

#include <SDL.h>
#include <SDL_thread.h>

AVFilterGraph *filter_graph = NULL;
AVFilterContext *buffersink_ctx = NULL;
AVFilterContext *buffersrc_ctx = NULL;
AVFrame *pFrame_out = NULL;

AVFormatContext *pFormatCtx = NULL;
AVCodecContext *pCodecCtx = NULL;
AVCodec *pCodec = NULL;
AVFrame *pFrame = NULL;
struct SwsContext *img_convert_ctx = NULL;

int videoStream = -1;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;

int init_filters(const char *filters_descr);

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

    // 初始化filter
    const char *filters_descr = "drawtext=fontfile=./../data/Keyboard.ttf:fontcolor=green:fontsize=30:text='haha'";
    if (init_filters(filters_descr) < 0)
    {
        printf("init_filters failed!\n");
        return -1;
    }

    pFrame_out = av_frame_alloc();
    if (pFrame_out == NULL)
    {
        printf("alloc out av frame failed!\n");
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
                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame(buffersrc_ctx, pFrame) < 0)
                {
                    printf("av_buffersrc_add_frame failded!\n");
                    break;
                }
                /* pull filtered pictures from the filtergraph */
                if (av_buffersink_get_frame(buffersink_ctx, pFrame_out) < 0)
                {
                    printf("av_buffersink_get_frame failed!\n");
                    break;
                }
                SDL_UpdateYUVTexture(texture, NULL, pFrame_out->data[0], pFrame_out->linesize[0], pFrame_out->data[1], pFrame_out->linesize[1], pFrame_out->data[2], pFrame_out->linesize[2]);

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);

                av_frame_unref(pFrame_out);
            }
            av_frame_unref(pFrame);
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

    avfilter_graph_free(&filter_graph);

    // 退出SDL
    SDL_Quit();

    return 0;
}

int init_filters(const char *filters_descr)
{
    int ret;
    // 注册所有AVFilter
    avfilter_register_all();
    
    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    char args[512];
    AVFilter *buffersrc = avfilter_get_by_name("buffer");
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
            pCodecCtx->time_base.num, pCodecCtx->time_base.den,
            pCodecCtx->sample_aspect_ratio.num, pCodecCtx->sample_aspect_ratio.den);
    // 为FilterGraph分配内存
    filter_graph = avfilter_graph_alloc();
    // 创建并向FilterGraph中添加一个Filter
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0)
    {
        printf("avfilter_graph_create_filter in failed! [%d]\n", ret);
        return ret;
    }

    /* buffer video sink: to terminate the filter chain. */
    AVFilter *buffersink = avfilter_get_by_name("buffersink");
    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};
    AVBufferSinkParams *buffersink_params;
    buffersink_params = av_buffersink_params_alloc();
    buffersink_params->pixel_fmts = pix_fmts;
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, buffersink_params, filter_graph);
    av_free(buffersink_params);
    if (ret < 0)
    {
        printf("avfilter_graph_create_filter out failed! [%d]\n", ret);
        return ret;
    }

    /* Endpoints for the filter graph. */
    AVFilterInOut *outputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    AVFilterInOut *inputs = avfilter_inout_alloc();
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    // 将一串通过字符串描述的Graph添加到FilterGraph中
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr, &inputs, &outputs, NULL)) < 0)
    {
        printf("avfilter_graph_parse_ptr failed! [%d]\n", ret);
        return ret;
    }

    // 检查FilterGraph的配置
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
    {
        printf("avfilter_graph_config failed! [%d]\n", ret);
        return ret;
    }

    return 0;
}