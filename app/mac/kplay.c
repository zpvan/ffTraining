#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

AVFormatContext *pFormatCtx = NULL;
AVCodecContext *pCodecCtx = NULL;
AVCodec *pCodec = NULL;
AVFrame *pFrame = NULL;
AVFrame *pFrameRGB = NULL;
struct SwsContext *img_convert_ctx = NULL;

int videoStream = -1;

static unsigned sws_flags = SWS_BICUBIC;

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame);

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

    // 分配视频帧内存空间
    pFrame = av_frame_alloc();
    if (pFrame == NULL) {
        printf("alloc av frame failed!\n");
        return -1;
    }

    // 分配RGB的视频帧内存空间
    pFrameRGB = av_frame_alloc();
    if (pFrameRGB == NULL) {
        printf("alloc rgb av frame failed!\n");
        return -1;
    }

    // 申请放置原始数据的内存空间
    uint8_t *buffer;
    int numBytes;
    numBytes = avpicture_get_size(AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

    // 把帧和新申请的内存结合起来
    avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

    // 读取媒体文件里边的视频包
    int frameFinished;
    AVPacket packet;

    i = 0;
    while (av_read_frame(pFormatCtx, &packet) >= 0) {
        // 检查packet是否是video
        if (packet.stream_index == videoStream) {
            // 解码视频帧
            avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

            // 如果拿到视频帧
            if (frameFinished) {
                // 解码后的视频帧转换成RGB格式
                // img_convert((AVPicture *)pFrameRGB, AV_PIX_FMT_RGB24, (AVPicture *)pFrame, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
                img_convert_ctx = sws_getCachedContext(img_convert_ctx, pFrame->width, pFrame->height, pFrame->format, pFrame->width, pFrame->height, AV_PIX_FMT_RGB24, sws_flags, NULL, NULL, NULL);
                if (img_convert_ctx != NULL) {
                    sws_scale(img_convert_ctx, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
                }

                // 保存视频帧到硬盘
                if (++i > 30 && i < 35) {
                    SaveFrame(pFrameRGB, pCodecCtx->width, pCodecCtx->height, i);
                }
            }
        }
        // 释放packet, 它是在av_read_frame里边分配内存packet.data
        av_free_packet(&packet);
    }

    // 释放RGB image
    av_free(buffer);
    av_free(pFrameRGB);

    // 释放yuv image
    av_free(pFrame);

    // 关闭codec
    avcodec_close(pCodecCtx);

    // 关闭视频文件
    avformat_close_input(&pFormatCtx);

    // 释放sws
    sws_freeContext(img_convert_ctx);

    return 0;
}

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
    FILE *pFile;
    char szFilename[32];
    int y;

    // 打开文件
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile = fopen(szFilename, "wb");
    if (pFile == NULL) {
        printf("open file failed!\n");
        return;
    }

    // 写文件头
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // 写像素数据
    for (y = 0; y < height; ++y) {
        fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);
    }

    // 关闭文件
    fclose(pFile);
}