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
 * /