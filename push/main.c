#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/threadmessage.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

typedef struct {
    int fd;
    const char *address;
    const char *stream_id;
    int16_t port;

    int vindex;
    int aindex;

    AVFormatContext *ifmt;

    AVCodecContext *video_decoder;
    AVCodecContext *video_encoder;
    SwsContext *sws;
    int video_encoder_id;
    int fps;
    int vbitrate;
    int gop_size;
    int max_b_frames;

    AVCodecContext *audio_decoder;
    AVCodecContext *audio_encoder;
    SwrContext *swr;
    int audio_encoder_id;
    int abitrate;
    int sample_rate;
    int channels;
    AVChannelLayout ch_layout;

    AVThreadMessageQueue *queue;
    pthread_t writer;
    volatile sig_atomic_t stop;
} Media;

static void *writer_proc(void *arg) {
    Media *ctx = (Media *)arg;
    ctx->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx->fd < 0) {
        perror("socket");
        return NULL;
    }

    struct hostent *he = gethostbyname(ctx->address);
    if (!he) {
        fprintf(stderr, "gethostbyname failed for %s\n", ctx->address);
        close(ctx->fd);
        return NULL;
    }

    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(ctx->port);
    memcpy(&serv.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(ctx->fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect");
        close(ctx->fd);
        return NULL;
    }

    char json[512];
    snprintf(json, sizeof(json),
             "{\"mode\":\"push\",\"stream_id\":\"%s\",\"video_codec_id\":%d,"
             "\"audio_codec_id\":%d,\"fps\":%d,\"width\":%d,\"height\":%d,"
             "\"sample_rate\":%d,\"channels\":%d}",
             ctx->stream_id, ctx->video_encoder ? ctx->video_encoder_id : AV_CODEC_ID_H264,
             ctx->audio_encoder ? ctx->audio_encoder_id : AV_CODEC_ID_AAC,
             ctx->video_encoder ? ctx->fps : 0,
             ctx->video_encoder ? ctx->video_encoder->width : 0,
             ctx->video_encoder ? ctx->video_encoder->height : 0,
             ctx->audio_encoder ? ctx->sample_rate : 0,
             ctx->audio_encoder ? ctx->channels : 0);

    uint32_t len = strlen(json);
    uint8_t hdr[4] = {
        (len) & 0xFF, (len >> 8) & 0xFF, (len >> 16) & 0xFF, (len >> 24) & 0xFF};
    if (write(ctx->fd, hdr, 4) != 4 || write(ctx->fd, json, len) != (ssize_t)len) {
        fprintf(stderr, "Failed to send stream header\n");
        close(ctx->fd);
        return NULL;
    }

    while (!ctx->stop) {
        AVPacket *pkt = NULL;
        if (av_thread_message_queue_recv(ctx->queue, &pkt, 0) < 0) continue;
        if (!pkt) continue;

        uint8_t header[28];
        int64_t pts = pkt->pts, dts = pkt->dts;
        int32_t si = pkt->stream_index, fl = pkt->flags, sz = pkt->size;
        for (int i = 0; i < 8; i++) header[i] = (pts >> (i * 8)) & 0xFF;
        for (int i = 0; i < 8; i++) header[8 + i] = (dts >> (i * 8)) & 0xFF;
        for (int i = 0; i < 4; i++) header[16 + i] = (si >> (i * 8)) & 0xFF;
        for (int i = 0; i < 4; i++) header[20 + i] = (fl >> (i * 8)) & 0xFF;
        for (int i = 0; i < 4; i++) header[24 + i] = (sz >> (i * 8)) & 0xFF;

        if (write(ctx->fd, header, 28) != 28 ||
            write(ctx->fd, pkt->data, pkt->size) != pkt->size) {
            fprintf(stderr, "[Writer] Error sending packet\n");
            av_packet_unref(pkt);
            av_packet_free(&pkt);
            continue;
        }

        fprintf(stderr, "[Writer] Sent packet: stream=%d, size=%d, pts=%lld\n",
                pkt->stream_index, pkt->size, pkt->pts);

        av_packet_unref(pkt);
        av_packet_free(&pkt);
    }

    close(ctx->fd);
    return NULL;
}

int main(int argc, char **argv) {
    avdevice_register_all();
    const char *addr = (argc >= 2) ? argv[1] : "livsho.com";
    const char *stream_id = (argc >= 3) ? argv[2] : "stream";

    Media ctx = {0};

	ctx.vindex = -1;
	ctx.aindex = -1;
    ctx.address = addr;
    ctx.stream_id = stream_id;
    ctx.port = 1935;
    ctx.video_encoder_id = AV_CODEC_ID_H264;
    ctx.audio_encoder_id = AV_CODEC_ID_AAC;
    ctx.vbitrate = 1000 * 1000;
    ctx.abitrate = 128000;
    ctx.fps = 30;
    ctx.gop_size = 12;
    ctx.max_b_frames = 0;
    ctx.sample_rate = 48000;
    ctx.channels = 2;
    av_channel_layout_default(&ctx.ch_layout, 2);

    const AVInputFormat *ifmt = av_find_input_format("avfoundation");
	AVDictionary *options = NULL;
	av_dict_set(&options, "framerate", "30", 0);
    int ret = avformat_open_input(&ctx.ifmt, "0", ifmt, &options);
    if (ret < 0) {
        fprintf(stderr, "ERROR: cannot open input. %s\n", av_err2str(ret));
        return -1;
    }

    if (ctx.ifmt && avformat_find_stream_info(ctx.ifmt, NULL) >= 0) {
        for (unsigned i = 0; i < ctx.ifmt->nb_streams; i++) {
            enum AVMediaType type = ctx.ifmt->streams[i]->codecpar->codec_type;
            if (type == AVMEDIA_TYPE_VIDEO) ctx.vindex = i;
            if (type == AVMEDIA_TYPE_AUDIO) ctx.aindex = i;
        }
    }

    if (ctx.vindex >= 0) {
        AVCodecParameters *vpar = ctx.ifmt->streams[ctx.vindex]->codecpar;
        const AVCodec *vdec = avcodec_find_decoder(vpar->codec_id);
        ctx.video_decoder = avcodec_alloc_context3(vdec);
        avcodec_parameters_to_context(ctx.video_decoder, vpar);
        avcodec_open2(ctx.video_decoder, vdec, NULL);
        const AVCodec *venc = avcodec_find_encoder(ctx.video_encoder_id);
        ctx.video_encoder = avcodec_alloc_context3(venc);
        ctx.video_encoder->width = vpar->width;
        ctx.video_encoder->height = vpar->height;
        ctx.video_encoder->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx.video_encoder->color_range = AVCOL_RANGE_MPEG;
        ctx.video_encoder->time_base = (AVRational){1, ctx.fps};
        ctx.video_encoder->framerate = (AVRational){ctx.fps, 1};
        ctx.video_encoder->bit_rate = ctx.vbitrate;
        ctx.video_encoder->gop_size = ctx.gop_size;
        ctx.video_encoder->max_b_frames = ctx.max_b_frames;
        if ((ret = avcodec_open2(ctx.video_encoder, venc, NULL)) < 0) {
			fprintf(stderr, "ERROR: cannot open video encoder. %s\n", av_err2str(ret));
			return -1;
		}

		ctx.sws = sws_getContext(ctx.video_encoder->width, ctx.video_encoder->height, ctx.video_decoder->pix_fmt, 
				                 ctx.video_encoder->width, ctx.video_encoder->height, ctx.video_encoder->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
	}

    AVAudioFifo *afifo = NULL;
    int enc_frame_size = 0;
    if (ctx.aindex >= 0) {
        AVCodecParameters *apar = ctx.ifmt->streams[ctx.aindex]->codecpar;
        const AVCodec *adec = avcodec_find_decoder(apar->codec_id);
        ctx.audio_decoder = avcodec_alloc_context3(adec);
        avcodec_parameters_to_context(ctx.audio_decoder, apar);
        avcodec_open2(ctx.audio_decoder, adec, NULL);
        const AVCodec *aenc = avcodec_find_encoder(ctx.audio_encoder_id);
        ctx.audio_encoder = avcodec_alloc_context3(aenc);
        ctx.audio_encoder->sample_rate = ctx.sample_rate;
        ctx.audio_encoder->ch_layout = ctx.ch_layout;
        ctx.audio_encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
        ctx.audio_encoder->bit_rate = ctx.abitrate;
        ctx.audio_encoder->time_base = (AVRational){1, ctx.sample_rate};
        avcodec_open2(ctx.audio_encoder, aenc, NULL);
        swr_alloc_set_opts2(&ctx.swr, &ctx.audio_encoder->ch_layout, ctx.audio_encoder->sample_fmt, ctx.audio_encoder->sample_rate, &ctx.audio_decoder->ch_layout, ctx.audio_decoder->sample_fmt, ctx.audio_decoder->sample_rate, 0, NULL);
        if (ctx.swr) swr_init(ctx.swr);
        enc_frame_size = ctx.audio_encoder->frame_size;
        afifo = av_audio_fifo_alloc(ctx.audio_encoder->sample_fmt, ctx.audio_encoder->ch_layout.nb_channels, enc_frame_size * 8);
    }

    if (av_thread_message_queue_alloc(&ctx.queue, 1024, sizeof(AVPacket *)) < 0) return 1;

    pthread_create(&ctx.writer, NULL, writer_proc, &ctx);
    int64_t vpts = 0, apts = 0;

    while (!ctx.stop) {
        AVPacket *ipkt = av_packet_alloc();
        int ret = av_read_frame(ctx.ifmt, ipkt);
        if (ret < 0) {
            av_packet_free(&ipkt);
            continue;
        }

        if (ctx.video_decoder && ipkt->stream_index == ctx.vindex) {
            ret = avcodec_send_packet(ctx.video_decoder, ipkt);
            while (ret >= 0) {
                AVFrame *iframe = av_frame_alloc();
                ret = avcodec_receive_frame(ctx.video_decoder, iframe);
                if (ret < 0) {
                    av_frame_free(&iframe);
                    break;
                }
                AVFrame *oframe = av_frame_alloc();
                oframe->format = ctx.video_encoder->pix_fmt;
                oframe->width = ctx.video_encoder->width;
                oframe->height = ctx.video_encoder->height;
                av_frame_get_buffer(oframe, 32);

                if (ctx.sws) {
                    ret = sws_scale_frame(ctx.sws, oframe, iframe);
					if (ret < 0) {
						fprintf(stderr, "ERROR: cannot scale frame\n");
						break;
					}
                }

                oframe->pts = vpts++;
                ret = avcodec_send_frame(ctx.video_encoder, oframe);
                while (ret >= 0) {
                    AVPacket *opkt = av_packet_alloc();

                    ret = avcodec_receive_packet(ctx.video_encoder, opkt);
                    if (ret < 0) break;

                    opkt->stream_index = 0;
                    av_thread_message_queue_send(ctx.queue, &opkt, 0);
                }

                av_frame_free(&iframe);
                av_frame_free(&oframe);
            }
        } else if (ctx.audio_decoder && ipkt->stream_index == ctx.aindex) {
            ret = avcodec_send_packet(ctx.audio_decoder, ipkt);
            while (ret >= 0) {
                AVFrame *aframe = av_frame_alloc();
                ret = avcodec_receive_frame(ctx.audio_decoder, aframe);
                if (ret < 0) {
                    av_frame_free(&aframe);
                    break;
                }

                int max_out = av_rescale_rnd(swr_get_delay(ctx.swr, ctx.audio_decoder->sample_rate) + aframe->nb_samples, ctx.audio_encoder->sample_rate, ctx.audio_decoder->sample_rate, AV_ROUND_UP);
                AVFrame *tmp = av_frame_alloc();
                tmp->ch_layout = ctx.audio_encoder->ch_layout;
                tmp->sample_rate = ctx.audio_encoder->sample_rate;
                tmp->format = ctx.audio_encoder->sample_fmt;
                tmp->nb_samples = max_out > 0 ? max_out : enc_frame_size;
                av_frame_get_buffer(tmp, 0);
                int out_samples = swr_convert(ctx.swr, tmp->data, tmp->nb_samples, (const uint8_t **)aframe->data, aframe->nb_samples);
                tmp->nb_samples = out_samples;
                av_audio_fifo_write(afifo, (void **)tmp->data, tmp->nb_samples);
                while (av_audio_fifo_size(afifo) >= enc_frame_size) {
                    AVFrame *oframe = av_frame_alloc();
                    oframe->ch_layout = ctx.audio_encoder->ch_layout;
                    oframe->sample_rate = ctx.audio_encoder->sample_rate;
                    oframe->format = ctx.audio_encoder->sample_fmt;
                    oframe->nb_samples = enc_frame_size;
                    av_frame_get_buffer(oframe, 0);
                    av_audio_fifo_read(afifo, (void **)oframe->data, enc_frame_size);
                    oframe->pts = apts;
                    apts += enc_frame_size;
                    ret = avcodec_send_frame(ctx.audio_encoder, oframe);
                    av_frame_free(&oframe);
                    while (ret >= 0) {
                        AVPacket *aopkt = av_packet_alloc();
                        ret = avcodec_receive_packet(ctx.audio_encoder, aopkt);
                        if (ret < 0) break;

                        aopkt->stream_index = 1;
                        av_thread_message_queue_send(ctx.queue, &aopkt, 0);
                    }
                }

                av_frame_free(&aframe);
                av_frame_free(&tmp);
            }
        }

		usleep(1e3*8);

        av_packet_unref(ipkt);
        av_packet_free(&ipkt);
    }

    return 0;
}
