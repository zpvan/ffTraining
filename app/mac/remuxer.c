/**
 * AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
 * AVOutputFormat *ofmt = NULL;
 * AVPacket pkt;
 * 
 * avformat_open_input(&ifmt_ctx, input_filename, NULL, NULL);
 * 
 * avformat_find_stream_info(ifmt_ctx, NULL);
 * 
 * av_dump_format(ifmt_ctx, 0, input_filename, 0);
 * 
 * avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_filename);
 * 
 * ofmt = ofmt_ctx->oformat;
 * 
 * for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) 
 * {
 *   AVStream *in_stream = ifmt_ctx->streams[i];
 *   AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
 * 
 *   avcodec_copy_context(out_stream->codec, in_stream->codec);
 *   out_stream->codec->codec_tag = 0;
 *   if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
 *   {
 *     out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
 *   }
 * 
 *   av_dump_format(ofmt_ctx, 0, output_filename, 1);
 * 
 *   if (!(ofmt->flags & AVFMT_NOFILE))
 *   {
 *     avio_open(&ofmt_ctx->pb, output_filename, AVIO_FILE_WRITE);
 *   }
 * 
 *   avformat_write_header(ofmt_ctx, NULL);
 * 
 *   while (1)
 *   {
 *     AVStream *in_stream = NULL, *out_stream = NULL;
 *     av_read_frame(ifmt_ctx, &pkt);
 * 
 *     in_stream = ifmt_ctx->streams[pkt.stream_index];
 *     out_stream = ofmt_ctx->streams[pkt.stream_index];
 * 
 *     pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, 
 *       (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
 * 
 *     pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, 
 *       (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
 * 
 *     pkt.duration = pkt.pts = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
 *     
 *     pkt.pos = -1;
 * 
 *     av_interleaved_write_frame(ofmt_ctx, &pkt);
 *     // av_free_packet(&pkt);
 *   }
 * 
 *   avformat_write_tailer(ofmt_ctx);
 * 
 * end:
 *   avformat_close_input(&ifmt_ctx);
 *   if (!(ofmt->flags & AVFMT_NOFILE))
 *   {
 *     avio_closep(&ofmt_ctx->pb);
 *   }
 *   avformat_free_context(ofmt_ctx);
 * }
 */

#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>

const char *out_filename = "out.ts";

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

int main(int argc, char *argv[]) {
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVOutputFormat *ofmt = NULL;
    AVPacket pkt;
    int ret, i;
    int *stream_mapping = NULL;
    int stream_mapping_size = 0;
    int stream_index = 0;

    // 注册所有的文件格式和编解码器的库
    av_register_all();

    if (argc != 2) {
        printf("plz intput file\n");
        return -1;
    }

    // 打开多媒体文件
    if (avformat_open_input(&ifmt_ctx, argv[1], NULL, NULL) != 0) {
        printf("open input [file]=[%s] failed!\n", argv[1]);
        return -1;
    }

    // 解析流讯息
    if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) {
        printf("find stream info failed!\n");
        return -1;
    }

    // 打印流讯息
    av_dump_format(ifmt_ctx, 0, argv[1], 0);

    // 创建输出文件的上下文
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    stream_mapping_size = ifmt_ctx->nb_streams;
    // 创建int[]
    stream_mapping = av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        // 相当于 -c copy
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            goto end;
        }

        // 何用
        out_stream->codecpar->codec_tag = 0;
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    // 写文件头, 不写会crash
    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream  = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        log_packet(ifmt_ctx, &pkt, "in");

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(ofmt_ctx, &pkt, "out");

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    // 写文件尾
    av_write_trailer(ofmt_ctx);
end:

    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}