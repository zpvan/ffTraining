#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>

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

int audioStream = -1;

AVCodecContext *aCodecCtx = NULL;
AVCodec *aCodec = NULL;
AVFrame *aFrame = NULL;

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

void packet_queue_init(PacketQueue *q);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);
int quit = 0;
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
int decode_interrupt_cb(void);

PacketQueue audioq;

void audio_callback(void *userdata, Uint8 *stream, int len);
int audio_decode_frame(AVCodecContext *aCodecCtx, AVFrame *aFrame, uint8_t **pcm);

#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

struct SwrContext *au_convert_ctx; 

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

    // 找到第一条视频流跟音频流
    int i;
    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0) {
            videoStream = i;
        }

        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0) {
            audioStream = i;
        }
    }
    if (videoStream == -1) {
        printf("find video stream failed!\n");
        return -1;
    }
    if (audioStream == -1) {
        printf("find audio stream failed!\n");
        return -1;
    }

    // 获取视频编解码器上下文
    pCodecCtx = pFormatCtx->streams[videoStream]->codec;

    // 获取音频编解码器上下文
    aCodecCtx = pFormatCtx->streams[audioStream]->codec;

    // SDL音频播放的配置参数
    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    // wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.channels = 2;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;

    // 配置重采样
    au_convert_ctx = swr_alloc(); 
    au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, av_get_default_channel_layout(wanted_spec.channels), AV_SAMPLE_FMT_S16,wanted_spec.freq, av_get_default_channel_layout(aCodecCtx->channels), aCodecCtx->sample_fmt, aCodecCtx->sample_rate, 0, NULL);
    // printf("In [channels, channels_layout, sample_fmt, sample_rate]=[%d, %d, %d, %d]\n", aCodecCtx->channels, av_get_default_channel_layout(aCodecCtx->channels), aCodecCtx->sample_fmt, aCodecCtx->sample_rate);
    if (swr_init(au_convert_ctx) != 0) {
        printf("swr_init failed!\n");
        return -1;
    }

    // 打开音频播放设备
    SDL_AudioSpec spec;
    if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        printf("SDL_OpenAudio failed %s\n", SDL_GetError());
        return -1;
    }

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

    window = SDL_CreateWindow("My Video Window",
                          SDL_WINDOWPOS_UNDEFINED,
                          SDL_WINDOWPOS_UNDEFINED,
                          pCodecCtx->width, pCodecCtx->height,
                          SDL_WINDOW_OPENGL);
    if (!window) {
        printf("SDL: could not create window - exiting\n");
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
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
    if (pFrame == NULL) {
        printf("alloc av frame failed!\n");
        return -1;
    }

    // 读取媒体文件里边的视频包
    int frameFinished;
    AVPacket packet;

    // 接收鼠标点击事件
    SDL_Event event;

    // 打开音频解码器
    aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
    if (!aCodec) {
        printf("unsupported audio [codec]=[%d]\n", aCodecCtx->codec_id);
        return -1;
    }

    if (avcodec_open2(aCodecCtx, aCodec, NULL) < 0) {
        printf("could not open audio codec\n");
        return -1;
    }

    // 分配音频帧的内存空间
    aFrame = av_frame_alloc();

    packet_queue_init(&audioq);
    SDL_PauseAudio(0);

    // 设置检查是否需要退出被阻塞的函数
    // avio_set_interrupt_cb(decode_interrupt_cb);

    i = 0;
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // 检查packet是否是video
        if (packet.stream_index == videoStream) {
            // 解码视频帧
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // 如果拿到视频帧
            if (frameFinished) {
                SDL_UpdateYUVTexture(texture, NULL, pFrame->data[0], pFrame->linesize[0], pFrame->data[1], pFrame->linesize[1],pFrame->data[2], pFrame->linesize[2]);

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }
        }

        if (packet.stream_index == audioStream) {
            packet_queue_put(&audioq, &packet);
            // audio不能释放packet, 因为还没解码
        } else {
            // video释放packet, 它是在av_read_frame里边分配内存packet.data
            av_free_packet(&packet);
        }
        

        SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT:
                quit = 1;
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

    // 退出SDL
    SDL_Quit();

    // 释放swr
    swr_free(&au_convert_ctx);

    return 0;
}

void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    AVPacketList *pkt1;
    if (av_dup_packet(pkt) < 0) {
        return -1;
    }
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1) {
        return -1;
    }
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt) {
        q->first_pkt = pkt1;
    } else {
        q->last_pkt->next = pkt1;
    }
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (quit) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt) {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }

    SDL_UnlockMutex(q->mutex);
    return ret;
}

int decode_interrupt_cb(void) {
    printf("decode_interrupt_cb\n");
    return quit;
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
    AVCodecContext *aCodecCtx = (AVCodecContext *) userdata;
    int len1, audio_size;

    static uint8_t *audio_buf = 0;
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)  {
        if (quit) {
            return;
        }
        if (audio_buf_index >= audio_buf_size) {
            // 已经将全部解码后的数据送出去了, 再获取
            audio_buf_size = audio_decode_frame(aCodecCtx, aFrame, &audio_buf);
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len) {
            len1 = len;
        }
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

int audio_decode_frame(AVCodecContext *aCodecCtx, AVFrame *aFrame, uint8_t **pcm) {
    static AVPacket pkt;

    int data_size;
    int got_output = 0;
    
    if (*pcm) {
        av_free(*pcm);
    }
    if (quit) {
        return -1;
    }
    if (packet_queue_get(&audioq, &pkt, 1) < 0) {
        return -1;
    }
    avcodec_decode_audio4(aCodecCtx, aFrame, &got_output, &pkt);
    if (got_output) {
        // 通道数 * 单通道样本数 * 样本大小
        int out_size = 2 * aFrame->nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
        *pcm = av_malloc(out_size);
        uint8_t **out = pcm;
        int len = swr_convert(au_convert_ctx, out, aFrame->nb_samples + 256, (const uint8_t **)aFrame->extended_data, aFrame->nb_samples);
        int org_size = aFrame->channels * aFrame->nb_samples * av_get_bytes_per_sample(aCodecCtx->sample_fmt);
        int resample_size = len * 2 * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
        return resample_size;
    }
    if (pkt.data) {
        av_free_packet(&pkt);
    }
    return 0;
}