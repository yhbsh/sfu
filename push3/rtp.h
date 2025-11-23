#pragma once
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <x264.h>
#include <x265.h>

#include <profiler.h>

typedef enum {
    CODEC_H264 = 27,
    CODEC_HEVC = 173,
    CODEC_AV1 = 225,
    CODEC_AAC = 86018,
} RtpCodecID;

typedef struct {
    x264_t *x264;
    x264_picture_t in, out;
} H264;

typedef struct {
    x265_encoder *x265;
    x265_picture *in;
} Hevc;

typedef struct {
    // TODO: implement
} AV1;

typedef struct RtpContext {
    int fd;
    RtpCodecID video_codec_id;
    RtpCodecID audio_codec_id;
    union {
        H264 h264;
        Hevc hevc;
        AV1 av1;
    } codec;
} RtpContext;

static inline int rtp_open(RtpContext **out_ctx, const char *address, short port, const char *stream_id, int w, int h, int fps, int gop, int bitrate, int video_codec_id, int audio_codec_id, int sample_rate, int channels) {
    RtpContext *ctx = calloc(1, sizeof(RtpContext));
    *out_ctx = ctx;

    ctx->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx->fd < 0) {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    struct hostent *he = gethostbyname(address);
    if (!he) {
        fprintf(stderr, "gethostbyname failed\n");
        return -1;
    }

    struct sockaddr_in s;
    memset(&s, 0, sizeof(s));
    s.sin_family = AF_INET;
    s.sin_port = htons(port);
    memcpy(&s.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(ctx->fd, (struct sockaddr *)&s, sizeof(s)) < 0) {
        fprintf(stderr, "connect failed\n");
        return -1;
    }

    ctx->video_codec_id = video_codec_id;
    ctx->audio_codec_id = audio_codec_id;

    char json[512];
    snprintf(json, sizeof(json),
             "{\"mode\":\"push\",\"stream_id\":\"%s\",\"video_codec_id\":%d,"
             "\"audio_codec_id\":%d,\"fps\":%d,\"width\":%d,\"height\":%d,"
             "\"sample_rate\":%d,\"channels\":%d}",
             stream_id, video_codec_id, audio_codec_id, fps, w, h, sample_rate, channels);

    uint32_t len = strlen(json);
    char hdr[4] = {0};
    hdr[0] = len >> 0;
    hdr[1] = len >> 8;
    hdr[2] = len >> 16;
    hdr[3] = len >> 24;

    if (write(ctx->fd, hdr, 4) < 0) {
        fprintf(stderr, "write hdr failed\n");
        return -1;
    }

    if (write(ctx->fd, json, len) < 0) {
        fprintf(stderr, "write json failed\n");
        return -1;
    }

    switch (video_codec_id) {
    case CODEC_H264: {
        const char *preset = "ultrafast";
        const char *tune = "zerolatency";
        const char *profile = "high";

        x264_param_t param;
        x264_param_default_preset(&param, preset, tune);

        param.i_width = w;
        param.i_height = h;
        param.i_fps_num = fps;
        param.i_fps_den = 1;
        param.i_keyint_max = gop;
        param.b_repeat_headers = 1;
        param.b_annexb = 1;
        param.rc.i_rc_method = X264_RC_ABR;
        param.rc.i_bitrate = bitrate;
        param.i_log_level = X264_LOG_NONE;

        x264_param_apply_profile(&param, profile);

        ctx->codec.h264.x264 = x264_encoder_open(&param);
        if (!ctx->codec.h264.x264) return -1;

        x264_picture_init(&ctx->codec.h264.in);
        x264_picture_alloc(&ctx->codec.h264.in, X264_CSP_I420, w, h);
        break;
    }

    case CODEC_HEVC: {
        const char *preset = "ultrafast";
        const char *tune = "zerolatency";
        const char *profile = "main10";

        x265_param *param = x265_param_alloc();
        x265_param_default_preset(param, preset, tune);

        param->sourceWidth = w;
        param->sourceHeight = h;
        param->fpsNum = fps;
        param->fpsDenom = 1;
        param->keyframeMax = gop;
        param->bRepeatHeaders = 1;
        param->rc.rateControlMode = X265_RC_ABR;
        param->rc.bitrate = bitrate;
        param->internalCsp = X265_CSP_I420;
        param->logLevel = X265_LOG_NONE;

        x265_param_apply_profile(param, profile);

        ctx->codec.hevc.x265 = x265_encoder_open(param);
        if (!ctx->codec.hevc.x265) return -1;

        ctx->codec.hevc.in = x265_picture_alloc();
        x265_picture_init(param, ctx->codec.hevc.in);

        ctx->codec.hevc.in->stride[0] = w;
        ctx->codec.hevc.in->stride[1] = w / 2;
        ctx->codec.hevc.in->stride[2] = w / 2;

        ctx->codec.hevc.in->planes[0] = malloc(w * h);
        ctx->codec.hevc.in->planes[1] = malloc((w * h) / 4);
        ctx->codec.hevc.in->planes[2] = malloc((w * h) / 4);
        break;
    }

    case CODEC_AV1:
        // TODO: implement
        break;

    default: fprintf(stderr, "ERROR: unsupported codec %d\n", video_codec_id); return -1;
    }

    return 0;
}

static inline int rtp_write_nals(RtpContext *ctx, unsigned char **units, int *sizes, int count, long long pts, long long dts, int is_key, int si) {
    int size = 0;
    for (int i = 0; i < count; i++) size += sizes[i];

    char h[28];
    memcpy(h + 0, &pts, 8);
    memcpy(h + 8, &dts, 8);
    memcpy(h + 16, &si, 4);
    memcpy(h + 20, &is_key, 4);
    memcpy(h + 24, &size, 4);

    if (write(ctx->fd, h, 28) < 0) {
        fprintf(stderr, "header write failed\n");
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (write(ctx->fd, units[i], sizes[i]) < 0) {
            fprintf(stderr, "payload write failed\n");
            return -1;
        }
    }

    return size;
}

static inline int rtp_encode_write(RtpContext *ctx, int pts) {
    switch (ctx->video_codec_id) {
    case CODEC_H264: {
        x264_nal_t *nals = NULL;
        int nals_cnt = 0;

        int ret = PROFILE_CALL(x264_encoder_encode, x264_encoder_encode(ctx->codec.h264.x264, &nals, &nals_cnt, &ctx->codec.h264.in, &ctx->codec.h264.out));
        if (ret < 0 || nals_cnt == 0) return -1;

        unsigned char *payloads[nals_cnt];
        int sizes[nals_cnt];
        int is_key = 0;

        for (int n = 0; n < nals_cnt; n++) {
            payloads[n] = nals[n].p_payload;
            sizes[n] = nals[n].i_payload;

            if (nals[n].i_type == NAL_SLICE_IDR) is_key = 1;

            printf("    NAL %03d | size=%04d | type=%d | key=%d | ", n, sizes[n], nals[n].i_type, is_key);
            for (int idx = 0; idx < 30 && idx < sizes[n]; idx++) printf("%03x ", payloads[n][idx]);
            printf("\n\n");
        }

        if (rtp_write_nals(ctx, payloads, sizes, nals_cnt, pts, pts, is_key, 0) < 0) return 1;
        break;
    }

    case CODEC_HEVC: {
        x265_nal *nals = NULL;
        uint32_t nal_count = 0;

        int ret = PROFILE_CALL(x265_encoder_encode, x265_encoder_encode(ctx->codec.hevc.x265, &nals, &nal_count, ctx->codec.hevc.in, NULL));
        if (ret < 0 || nal_count == 0) return -1;

        unsigned char *payloads[nal_count];
        int sizes[nal_count];
        int is_key = 0;

        for (uint32_t n = 0; n < nal_count; n++) {
            payloads[n] = nals[n].payload;
            sizes[n] = nals[n].sizeBytes;

            is_key = (nals[n].type == NAL_UNIT_CODED_SLICE_IDR_W_RADL) || (nals[n].type == NAL_UNIT_CODED_SLICE_IDR_N_LP) || (nals[n].type == NAL_UNIT_CODED_SLICE_CRA);

            printf("    NAL %03d | size=%04d | type=%d | key=%d | ", n, sizes[n], nals[n].type, is_key);
            for (int idx = 0; idx < 30 && idx < sizes[n]; idx++) printf("%03x ", payloads[n][idx]);
            printf("\n\n");
        }

        if (rtp_write_nals(ctx, payloads, sizes, nal_count, pts, pts, is_key, 0) < 0) return -1;
        break;
    }

    case CODEC_AV1:
        // TODO: implement
        break;

    default: fprintf(stderr, "ERROR: unsupported codec %d\n", ctx->video_codec_id); break;
    }
    return 0;
}

static inline void rtp_close(RtpContext *ctx) {
    switch (ctx->video_codec_id) {
    case CODEC_H264:
        x264_picture_clean(&ctx->codec.h264.in);
        x264_encoder_close(ctx->codec.h264.x264);
        break;

    case CODEC_HEVC:
        free(ctx->codec.hevc.in->planes[0]);
        free(ctx->codec.hevc.in->planes[1]);
        free(ctx->codec.hevc.in->planes[2]);
        x265_encoder_close(ctx->codec.hevc.x265);
        x265_picture_free(ctx->codec.hevc.in);
        // TODO: free params as well
        break;

    case CODEC_AV1:
        // TODO: implement
        break;

    default: fprintf(stderr, "ERROR: unsupported codec %d\n", ctx->video_codec_id); break;
    }
}
