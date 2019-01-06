#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>

#include <SDL.h>
#include <SDL_thread.h>

static const int VIDEO_PICTURE_QUEUE_SIZE = 3;
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

#define MAX_AUDIOQ_SIZE 200000
#define MAX_VIDEOQ_SIZE 500000

#define AV_SYNC_THRESHOLD 10
#define AV_NOSYNC_THRESHOLD 10.0

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct VideoPicture {
    SDL_Overlay *bmp;
    int width, height; /* source height & width */
    int allocated;
    double pts;
} VideoPicture;

typedef struct VideoState 
{
    double video_clock; // pts of last decoded frame / predicted pts of next decoded frame
    double audio_clock;
    double frame_timer;
    double frame_last_delay;
    double frame_last_pts;

    AVFormatContext *pFormatCtx;
    int videoStream, audioStream;
    struct SwrContext *au_convert_ctx;
    AVStream *audio_st;
    AVFrame *aFrame;
    PacketQueue audioq;
    uint8_t *audio_buf;
    unsigned int audio_buf_size;
    unsigned int audio_buf_index;
    AVPacket audio_pkt;
    uint8_t *audio_pkt_data;
    int audio_pkt_size;
    AVStream *video_st;
    PacketQueue videoq;
    struct SwsContext *img_convert_ctx;

    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;
    SDL_Surface *screen;

    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;
    char filename[1024];
} VideoState;

const int FF_QUIT_EVENT = SDL_QUIT + 1;
const int FF_ALLOC_EVENT = SDL_QUIT + 2;
const int FF_REFRESH_EVENT = SDL_QUIT + 3;

static unsigned sws_flags = SWS_BICUBIC;
static int quit = 0;

void packet_queue_init(PacketQueue *q);
int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block);
int packet_queue_put(PacketQueue *q, AVPacket *pkt);

void alloc_picture(void *userdata);
int audio_decode_frame(VideoState *is, uint8_t **pcm);

void schedule_refresh(VideoState *is, int delay);
void video_refresh_timer(void *userdata);

// int get_buffer(struct AVCodecContext *c, AVFrame *pic);
// void release_buffer(struct AVCodecContext *c, AVFrame *pic);

uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;
int our_get_buffer(struct AVCodecContext *c, AVFrame *pic, int flags)
{
    int ret = avcodec_default_get_buffer2(c, pic, flags);
    uint64_t *pts = av_malloc(sizeof(uint64_t));
    *pts = global_video_pkt_pts;
    pic->opaque = pts;
}
// void our_release_buffer(struct AVCodecContext *c, AVFrame *pic)
// {
//     if (pic)
//     {
//         av_freep(&pic->opaque);
//     }
//     avcodec_default_release_buffer(c, pic);
// }
double synchronize_video(VideoState *is, AVFrame *src_frame, double pts);
double get_audio_clock(VideoState *is);

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    VideoState *is = (VideoState *) userdata;
    AVCodecContext *aCodecCtx = is->audio_st->codec;
    int len1, audio_size;

    uint8_t *audio_buf = is->audio_buf;
    int audio_buf_size = is->audio_buf_size;
    int audio_buf_index = is->audio_buf_index;

    while (len > 0) 
    {
        if (quit)
        {
            return;
        }
        if (audio_buf_index >= audio_buf_size)
        {
            // 已经将全部解码后的数据送出去了, 再获取
            audio_buf_size = audio_decode_frame(is, &audio_buf);
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if (len1 > len)
        {
            len1 = len;
        }
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

static int queue_picture(VideoState *is, AVFrame *pFrame, double pts) 
{
    VideoPicture *vp;
    int dst_pix_fmt;
    AVPicture pict;

    /* wait until we have space for a new pic */
    SDL_LockMutex(is->pictq_mutex);
    while(is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !quit) {
        SDL_CondWait(is->pictq_cond, is->pictq_mutex);
    }
    SDL_UnlockMutex(is->pictq_mutex);

    if(quit)
        return -1;
    // windex is set to 0 initially
    vp = &is->pictq[is->pictq_windex];
    /* allocate or resize the buffer! */
    if(!vp->bmp || vp->width != is->video_st->codec->width || vp->height != is->video_st->codec->height) {
        SDL_Event event;
        vp->allocated = 0;
        /* we have to do it in the main thread */
        event.type = FF_ALLOC_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
        /* wait until we have a picture allocated */
        SDL_LockMutex(is->pictq_mutex);
        while(!vp->allocated && !quit) {
            SDL_CondWait(is->pictq_cond, is->pictq_mutex);
        }
        SDL_UnlockMutex(is->pictq_mutex);
        if(quit) {
            return -1;
        }
    }

    if (vp->bmp) {
        SDL_LockYUVOverlay(vp->bmp);
        /* point pict at the queue */
        pict.data[0] = vp->bmp->pixels[0];
        pict.data[1] = vp->bmp->pixels[2];
        pict.data[2] = vp->bmp->pixels[1];
        pict.linesize[0] = vp->bmp->pitches[0];
        pict.linesize[1] = vp->bmp->pitches[2];
        pict.linesize[2] = vp->bmp->pitches[1];
        if (is->img_convert_ctx != NULL)
        {
            sws_scale(is->img_convert_ctx, pFrame->data, pFrame->linesize, 0, is->video_st->codec->height, pict.data, pict.linesize);
        }
        SDL_UnlockYUVOverlay(vp->bmp);
        vp->pts = pts;
        /* now we inform our display thread that we have a pic ready */
        if(++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
            is->pictq_windex = 0;
        }
        SDL_LockMutex(is->pictq_mutex);
        is->pictq_size++;
        SDL_UnlockMutex(is->pictq_mutex);
    }
    return 0;
}

static int video_thread(void *arg) 
{
    VideoState *is = (VideoState *)arg;
    AVPacket pkt1, *packet = &pkt1;
    int frameFinished;
    AVFrame *pFrame;
    pFrame = av_frame_alloc();
    double pts;
    for(;;) {
        if(packet_queue_get(&is->videoq, packet, 1) < 0) {
            // means we quit getting packets
            break;
        }
        pts = 0;
        // Save global pts to be stored in pFrame in first call
        global_video_pkt_pts = packet->pts;
        // Decode video frame
        avcodec_decode_video2(is->video_st->codec, pFrame, &frameFinished, packet);
        if (packet->dts != AV_NOPTS_VALUE &&
                *(uint64_t *)pFrame->opaque != AV_NOPTS_VALUE)
        {
            pts = *(uint64_t *)pFrame->opaque;
        }
        else if (packet->dts != AV_NOPTS_VALUE)
        {
            pts = packet->dts;
        }
        else
        {
            pts = 0;
        }
        pts *= av_q2d(is->video_st->time_base);
        // Did we get a video frame?
        if(frameFinished) {
            if (!is->img_convert_ctx)
            {
                is->img_convert_ctx = sws_getCachedContext(is->img_convert_ctx, pFrame->width, pFrame->height, pFrame->format, 
                    pFrame->width, pFrame->height, AV_PIX_FMT_YUV420P, sws_flags, NULL, NULL, NULL);
            }
            pts = synchronize_video(is, pFrame, pts);
            if(queue_picture(is, pFrame, pts) < 0) {
                break;
            }
        }
        av_free_packet(packet);
    }
    av_free(pFrame);
    return 0;
}

static int stream_component_open(VideoState *is, int stream_index)
{
    AVFormatContext *pFormatCtx = is->pFormatCtx;
    AVCodecContext *codecCtx;
    AVCodec *codec;
    SDL_AudioSpec wanted_spec, spec;
    if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) 
    {
        return -1;
    }
    // Get a pointer to the codec context for the video stream
    codecCtx = pFormatCtx->streams[stream_index]->codec;
    if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) 
    {
        // Set audio settings from codec info
        wanted_spec.freq = codecCtx->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = 2;
        wanted_spec.silence = 0;
        wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = is;
        if (SDL_OpenAudio(&wanted_spec, &spec) < 0) 
        {
            fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
            return -1;
        }

        is->au_convert_ctx = swr_alloc(); 
        is->au_convert_ctx = swr_alloc_set_opts(is->au_convert_ctx, av_get_default_channel_layout(wanted_spec.channels), AV_SAMPLE_FMT_S16,wanted_spec.freq, av_get_default_channel_layout(codecCtx->channels), codecCtx->sample_fmt, codecCtx->sample_rate, 0, NULL);
        // printf("In [channels, channels_layout, sample_fmt, sample_rate]=[%d, %d, %d, %d]\n", codecCtx->channels, av_get_default_channel_layout(codecCtx->channels), codecCtx->sample_fmt, codecCtx->sample_rate);
        if (swr_init(is->au_convert_ctx) != 0)
        {
            printf("swr_init failed!\n");
            return -1;
        };
    }
    codec = avcodec_find_decoder(codecCtx->codec_id);
    if (!codec || (avcodec_open2(codecCtx, codec, NULL) < 0)) 
    {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }
    switch(codecCtx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            is->aFrame = av_frame_alloc();
            is->audioStream = stream_index;
            is->audio_st = pFormatCtx->streams[stream_index];
            is->audio_buf_size = 0;
            is->audio_buf_index = 0;
            memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
            packet_queue_init(&is->audioq);
            SDL_PauseAudio(0);
            break;

        case AVMEDIA_TYPE_VIDEO:
            is->frame_timer = (double) av_gettime() / 1000000.0;
            is->frame_last_delay = 40e-3;
            is->videoStream = stream_index;
            is->video_st = pFormatCtx->streams[stream_index];
            packet_queue_init(&is->videoq);
            // Convert the image into YUV format that SDL uses
            is->video_tid = SDL_CreateThread(video_thread, is);
            break;

        default:
            break;
    }
    codecCtx->get_buffer2 = our_get_buffer;
    // codecCtx->release_buffer = our_release_buffer;
}

static int read_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    // 打开多媒体文件
    if (avformat_open_input(&is->pFormatCtx, is->filename, NULL, NULL) != 0)
    {
        printf("open input [file]=[%s] failed!\n", is->filename);
        return -1;
    }

    AVFormatContext *pFormatCtx = is->pFormatCtx;

    // 解析流讯息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        printf("find stream info failed!\n");
        return -1;
    }

    // 打印流讯息
    av_dump_format(pFormatCtx, 0, is->filename, 0);

    // 找到第一条视频流跟音频流
    is->videoStream = -1;
    is->audioStream = -1;
    int i;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && is->videoStream < 0)
        {
            is->videoStream = i;
        }

        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && is->audioStream < 0)
        {
            is->audioStream = i;
        }
    }

    if (is->videoStream == -1)
    {
        printf("find video stream failed!\n");
        return -1;
    }
    if (is->audioStream == -1)
    {
        printf("find audio stream failed!\n");
        return -1;
    }

    // 打开解码器
    if (is->videoStream != -1)
    {
        stream_component_open(is, is->videoStream);
    }

    if (is->audioStream != -1)
    {
        stream_component_open(is, is->audioStream);
    }

    AVPacket packet;

    for (;;) 
    {
        if (quit) 
        {
            break;
        }
        // seek stuff goes here
        if (is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE) {
            SDL_Delay(10);
            continue;
        }
    
        if(av_read_frame(is->pFormatCtx, &packet) < 0) {
            break;
        }
        // Is this a packet from the video stream?
        if(packet.stream_index == is->videoStream) {
            packet_queue_put(&is->videoq, &packet);
            // av_free_packet(&packet);
        } else if(packet.stream_index == is->audioStream) {
            packet_queue_put(&is->audioq, &packet);
        } else {
            av_free_packet(&packet);
        }
    }

    if (1)
    {
        SDL_Event event;
        event.type = FF_QUIT_EVENT;
        event.user.data1 = is;
        SDL_PushEvent(&event);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    // 注册所有的文件格式和编解码器的库
    av_register_all();

    if (argc != 2)
    {
        printf("plz intput file\n");
        return -1;
    }

    VideoState *is;
    is = av_mallocz(sizeof(VideoState));

    av_strlcpy(is->filename, argv[1], sizeof(is->filename));

    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("could not initialize SDL %s\n", SDL_GetError());
        return -1;
    }

    schedule_refresh(is, 40);

    is->parse_tid = SDL_CreateThread(read_thread, is);

    if(!is->parse_tid) {
        printf("can't create decoder_thread\n");
        av_free(is);
        return -1;
    }

    // 接收鼠标点击事件
    SDL_Event event;
    while (!quit)
    {
        usleep(100); //0.1ms
        while (SDL_PollEvent(&event) && !quit)
        {
            switch (event.type)
            {
                case SDL_QUIT:
                    printf("SDL_QUIT\n");
                    quit = 1;
                    // SDL_Quit();
                    // exit(0);
                    break;

                case FF_QUIT_EVENT:
                    printf("FF_QUIT_EVENT\n");
                    break;

                case FF_ALLOC_EVENT:
                    alloc_picture(event.user.data1);
                    break;

                case FF_REFRESH_EVENT:
                    video_refresh_timer(event.user.data1);
                    break;

                default:
                    break;
            }
        }
    }

    if (is)
    {
        if (is->audio_st)
        {
            if (is->audio_st->codec)
            {
                // 关闭audio codec
                avcodec_close(is->audio_st->codec);
            }

            if (is->video_st->codec)
            {
                // 关闭video codec
                avcodec_close(is->video_st->codec);
            }
        }
        if (is->pFormatCtx)
        {
            // 关闭视频文件
            avformat_close_input(&is->pFormatCtx);
        }

        if (is->img_convert_ctx)
        {
            // 释放sws
            sws_freeContext(is->img_convert_ctx);
        }

        if (is->au_convert_ctx)
        {
            // 释放swr
            swr_free(&is->au_convert_ctx);
        }

        if (is->screen)
        {
            SDL_FreeSurface(is->screen);
        }

        if (is->aFrame)
        {
            av_free(is->aFrame);
        }
    }

    // SDL_Quit();
}

void video_display(VideoState *is) {
    SDL_Rect rect;
    VideoPicture *vp;
    AVPicture pict;
    float aspect_ratio;
    int w, h, x, y;
    int i;
    SDL_Surface *screen = is->screen;
    vp = &is->pictq[is->pictq_rindex];
    if(vp->bmp) {
        if(is->video_st->codec->sample_aspect_ratio.num == 0) {
            aspect_ratio = 0;
        } else {
            aspect_ratio = av_q2d(is->video_st->codec->sample_aspect_ratio) *
            is->video_st->codec->width / is->video_st->codec->height;
        }
        if(aspect_ratio <= 0.0) {
            aspect_ratio = (float)is->video_st->codec->width /
            (float)is->video_st->codec->height;
        }
        h = screen->h;
        w = ((int)rint(h * aspect_ratio)) & -3;
        if(w > screen->w) {
            w = screen->w;
            h = ((int)rint(w / aspect_ratio)) & -3;
        }
        x = (screen->w - w) / 2;
        y = (screen->h - h) / 2;
        rect.x = x;
        rect.y = y;
        rect.w = w;
        rect.h = h;
        SDL_DisplayYUVOverlay(vp->bmp, &rect);
    }
}

void video_refresh_timer(void *userdata) {
    VideoState *is = (VideoState *)userdata;
    VideoPicture *vp;
    double actual_delay, delay, sync_threshold, ref_clock, diff;

    if(is->video_st) {
        if(is->pictq_size == 0) {
            schedule_refresh(is, 1);
        } else {
            vp = &is->pictq[is->pictq_rindex];

            delay = vp->pts - is->frame_last_pts; /* the pts from last time */
            if (delay <= 0 || delay >= 1.0)
            {
                /* if incorrect delay, use previous one */
                delay = is->frame_last_delay;
            }
            /* save for next time */
            is->frame_last_delay = delay;
            is->frame_last_pts = vp->pts;

            /* update delay to sync to audio */
            ref_clock = get_audio_clock(is);
            diff = vp->pts - ref_clock;

            /* skip or repeat the frame. Take delay into account FFPlay still doesn't "know if this is the best guess." */
            sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
            if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
                delay = 0;
            } else if (diff >= sync_threshold) {
                delay = 2 * delay;
            }
            is->frame_timer += delay;
            /* compute the REAL delay */
            actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
            if (actual_delay < 0.010)
            {
                /* Really it should skip the picture instead */
                actual_delay = 0.010;
            }

            /* Timing code goes here */
            schedule_refresh(is, 80);
            /* show the picture! */
            video_display(is);
            /* update queue for next picture! */
            if(++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
                is->pictq_rindex = 0;
            }
            SDL_LockMutex(is->pictq_mutex);
            is->pictq_size--;
            SDL_CondSignal(is->pictq_cond);
            SDL_UnlockMutex(is->pictq_mutex);
        }
    } else {
        schedule_refresh(is, 100);
    }
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0; /* 0 means stop timer */
}

void schedule_refresh(VideoState *is, int delay) {
    SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

void alloc_picture(void *userdata) {
    VideoState *is = (VideoState *)userdata;
    VideoPicture *vp;
    vp = &is->pictq[is->pictq_windex];
    if(vp->bmp) {
        // we already have one make another, bigger/smaller
        SDL_FreeYUVOverlay(vp->bmp);
    }
    if (!is->screen)
    {
        is->screen = SDL_SetVideoMode(is->video_st->codec->width, is->video_st->codec->height, 0, 0);
        if (!is->screen)
        {
            printf("SDL: could not set video mode - exiting\n");
            return;
        }
    }
    // Allocate a place to put our YUV image on that screen
    vp->bmp = SDL_CreateYUVOverlay(is->video_st->codec->width, is->video_st->codec->height, SDL_YV12_OVERLAY, is->screen);
    vp->width = is->video_st->codec->width;
    vp->height = is->video_st->codec->height;
    SDL_LockMutex(is->pictq_mutex);
    vp->allocated = 1;
    SDL_CondSignal(is->pictq_cond);
    SDL_UnlockMutex(is->pictq_mutex);
}

double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

    double frame_delay;

    if (pts != 0)
    {
        /* if we have pts, set video clock to it */
        is->video_clock = pts;
    }
    else
    {
        /* if we aren't given a pts, set it to the clock */
        pts = is->video_clock;
    }
    /* update the video clock */
    frame_delay = av_q2d(is->video_st->codec->time_base);
    /* if we are repeating a frame, adjust clock accordingly */
    frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
    is->video_clock += frame_delay;
    return pts;
}

int audio_decode_frame(VideoState *is, uint8_t **pcm)
{
    AVCodecContext *aCodecCtx = is->audio_st->codec;
    AVFrame *aFrame = is->aFrame;
    AVPacket pkt;

    int data_size;
    int got_output = 0;
    
    if (*pcm)
    {
        av_free(*pcm);
    }
    if (quit)
    {
        return -1;
    }
    if (packet_queue_get(&is->audioq, &pkt, 1) < 0)
    {
        return -1;
    }
    /* if update, update the audio clock w/pts */
    if (pkt.pts != AV_NOPTS_VALUE)
    {
        is->audio_clock = av_q2d(is->audio_st->time_base) * pkt.pts;
    }

    avcodec_decode_audio4(aCodecCtx, aFrame, &got_output, &pkt);
    if (got_output)
    {
        double pts;
        double *pts_ptr;
        int n;
        // 通道数 * 单通道样本数 * 样本大小
        int out_size = 2 * aFrame->nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
        *pcm = av_malloc(out_size);
        uint8_t **out = pcm;
        int len = swr_convert(is->au_convert_ctx, out, aFrame->nb_samples + 256, (const uint8_t **)aFrame->extended_data, aFrame->nb_samples);
        int org_size = aFrame->channels * aFrame->nb_samples * av_get_bytes_per_sample(aCodecCtx->sample_fmt);
        int resample_size = len * 2 * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

        pts = is->audio_clock;
        *pts_ptr =pts;
        n = 2 * is->audio_st->codec->channels;
        is->audio_clock += (double) data_size / (double) (n * is->audio_st->codec->sample_rate);

        return resample_size;
    }
    if (pkt.data)
    {
        av_free_packet(&pkt);
    }
    return 0;
}

double get_audio_clock(VideoState *is) {
    double pts;
    int hw_buf_size, bytes_per_sec, n;

    pts = is->audio_clock; /* maintained in the audio thread */
    hw_buf_size = is->audio_buf_size - is->audio_buf_index;
    bytes_per_sec = 0;
    n = is->audio_st->codec->channels * 2;
    if (is->audio_st)
    {
        bytes_per_sec = is->audio_st->codec->sample_rate * n;
    }
    if (bytes_per_sec)
    {
        pts -= (double) hw_buf_size / bytes_per_sec;
    }
    return pts;
}

void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;)
    {
        if (quit)
        {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1)
        {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
            {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } 
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait(q->cond, q->mutex);
        }
    }

    SDL_UnlockMutex(q->mutex);
    return ret;
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;
    if (av_dup_packet(pkt) < 0)
    {
        return -1;
    }
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
    {
        return -1;
    }
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
    {
        q->first_pkt = pkt1;
    }
    else
    {
        q->last_pkt->next = pkt1;
    }
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}