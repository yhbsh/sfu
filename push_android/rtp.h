#pragma once
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <x264.h>
#include <x265.h>

#ifdef __ANDROID__
#include <media/NDKMediaCodec.h>
#include <media/NDKMediaFormat.h>
#endif

#include "rtp_prof.h"
#include "rtp_log.h"

typedef enum {
    CODEC_H264,
    CODEC_H264_MEDIACODEC,
    CODEC_HEVC,
    CODEC_HEVC_MEDIACODEC,
    CODEC_AAC,
} RTP_Codec_ID;

typedef struct {
    x264_t *x264;
    x264_picture_t in, out;
} H264;

typedef struct {
    x265_encoder *x265;
    x265_picture *in;
} Hevc;

#ifdef __ANDROID__
typedef struct {
    AMediaCodec *codec;
    int w;
    int h;
    uint8_t *Y, *U, *V;
    int Ys, Us, Vs;
} MediaCodec;
#endif // __ANDROID__

typedef struct Rtp_Context {
    int fd;

    RTP_Codec_ID video_codec_id;
    RTP_Codec_ID audio_codec_id;

    union {
        H264 h264;
        Hevc hevc;
#ifdef __ANDROID__
        MediaCodec media_codec;
#endif // __ANDROID__
    } codec;
} Rtp_Context;

static inline int rtp_open(Rtp_Context **out_ctx, const char *address, short port, const char *stream_id, int w, int h, int fps, int gop, int bitrate, int video_codec_id, int audio_codec_id, int sample_rate, int channels) {
    Rtp_Context *ctx = calloc(1, sizeof(Rtp_Context));
    *out_ctx = ctx;

    ctx->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx->fd < 0) {
        rtp_log("socket failed\n");
        return -1;
    }

    struct hostent *he = gethostbyname(address);
    if (!he) {
        rtp_log("gethostbyname failed\n");
        return -1;
    }

    struct sockaddr_in s;
    memset(&s, 0, sizeof(s));
    s.sin_family = AF_INET;
    s.sin_port = htons(port);
    memcpy(&s.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(ctx->fd, (struct sockaddr *)&s, sizeof(s)) < 0) {
        rtp_log("connect failed\n");
        return -1;
    }

    ctx->video_codec_id = video_codec_id;
    ctx->audio_codec_id = audio_codec_id;

    int ff_video_codec_id = -1;
    switch(video_codec_id) {
        case CODEC_H264:
            ff_video_codec_id = 27;
            break;
        case CODEC_H264_MEDIACODEC:
            ff_video_codec_id = 27;
            break;
        case CODEC_HEVC:
            ff_video_codec_id = 173;
            break;
        case CODEC_HEVC_MEDIACODEC:
            ff_video_codec_id = 173;
            break;
        default:
            break;
    }

    int ff_audio_codec_id = -1;
    switch(audio_codec_id) {
        case CODEC_AAC:
            ff_audio_codec_id = 86018;
            break;
        default:
            break;
    }

    char json[512];
    snprintf(json, sizeof(json),
             "{\"mode\":\"push\",\"stream_id\":\"%s\",\"video_codec_id\":%d,"
             "\"audio_codec_id\":%d,\"fps\":%d,\"width\":%d,\"height\":%d,"
             "\"sample_rate\":%d,\"channels\":%d}",
             stream_id, ff_video_codec_id, ff_audio_codec_id, fps, w, h, sample_rate, channels);

    uint32_t len = strlen(json);
    char hdr[4] = {0};
    hdr[0] = len >> 0;
    hdr[1] = len >> 8;
    hdr[2] = len >> 16;
    hdr[3] = len >> 24;

    if (write(ctx->fd, hdr, 4) < 0) {
        rtp_log("write hdr failed\n");
        return -1;
    }

    if (write(ctx->fd, json, len) < 0) {
        rtp_log("write json failed\n");
        return -1;
    }

    switch (video_codec_id) {
    case CODEC_H264: {
            const char *preset = "fast";
            const char *tune = "zerolatency";
            const char *profile = "high10";
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
        } break;
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
        } break;
#ifdef __ANDROID__
    case CODEC_H264_MEDIACODEC: {
            AMediaCodec *codec = AMediaCodec_createEncoderByType("video/avc");
            if (!codec) {
                rtp_log("Failed to create H264 encoder (AMediaCodec_createEncoderByType)");
                return -1;
            }
    
            AMediaFormat *format = AMediaFormat_new();
            if (!format) {
                rtp_log("Failed to allocate AMediaFormat");
                AMediaCodec_delete(codec);
                return -1;
            }
    
            AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, w);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, h);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitrate*1000);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, gop / fps);
#define COLOR_FormatYUV420Planar 19
#define COLOR_FormatYUV420PackedPlanar 20
#define COLOR_FormatYUV420SemiPlanar 21
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatYUV420Planar);
#define AVC_PROFILE_BASELINE 0x01
#define AVC_PROFILE_MAIN 0x02
#define AVC_PROFILE_HIGH 0x08
#define AVC_PROFILE_HIGH10 0x10
#define AVC_PROFILE_HIGH422 0x20
#define AVC_PROFILE_HIGH444 0x40
#define AVC_PROFILE_CONSTRAINED_BASELINE 0x10000;
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PROFILE, AVC_PROFILE_HIGH10);

// Level 1.x
#define AVC_LEVEL_1        0x01
#define AVC_LEVEL_1b       0x02
#define AVC_LEVEL_1_1      0x04
#define AVC_LEVEL_1_2      0x08
#define AVC_LEVEL_1_3      0x10

// Level 2.x
#define AVC_LEVEL_2        0x20
#define AVC_LEVEL_2_1      0x40
#define AVC_LEVEL_2_2      0x80

// Level 3.x
#define AVC_LEVEL_3        0x100
#define AVC_LEVEL_3_1      0x200
#define AVC_LEVEL_3_2      0x400

// Level 4.x
#define AVC_LEVEL_4        0x800
#define AVC_LEVEL_4_1      0x1000
#define AVC_LEVEL_4_2      0x2000

// Level 5.x
#define AVC_LEVEL_5        0x4000
#define AVC_LEVEL_5_1      0x8000
#define AVC_LEVEL_5_2      0x10000

// Level 6.x
#define AVC_LEVEL_6        0x20000
#define AVC_LEVEL_6_1      0x40000
#define AVC_LEVEL_6_2      0x80000
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_LEVEL, AVC_LEVEL_5_2);
            AMediaFormat_setInt32(format, "prepend-sps-pps-to-idr-frames", 1);
    
            media_status_t st = AMediaCodec_configure(codec, format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
            if (st != AMEDIA_OK) {
                rtp_log("AMediaCodec_configure failed: %d", st);
                AMediaFormat_delete(format);
                AMediaCodec_delete(codec);
                return -1;
            }
    
            AMediaFormat_delete(format);
    
            st = AMediaCodec_start(codec);
            if (st != AMEDIA_OK) {
                rtp_log("AMediaCodec_start failed: %d", st);
                AMediaCodec_delete(codec);
                return -1;
            }
    
            ctx->codec.media_codec.codec = codec;
            ctx->codec.media_codec.w = w;
            ctx->codec.media_codec.h = h;
    
            rtp_log("H264 MediaCodec encoder initialized: %dx%d, %d fps, %d bitrate\n", w, h, fps, bitrate);
        } break;
    case CODEC_HEVC_MEDIACODEC: {
            AMediaCodec *codec = AMediaCodec_createEncoderByType("video/hevc");
            AMediaFormat *format = AMediaFormat_new();
            AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/hevc");
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, w);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, h);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitrate*1000);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, gop / fps);
#define COLOR_FormatYUV420Planar 19
#define COLOR_FormatYUV420PackedPlanar 20
#define COLOR_FormatYUV420SemiPlanar 21
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatYUV420Planar);

#define HEVC_PROFILE_MAIN 1
#define HEVC_PROFILE_MAIN10 2
#define HEVC_PROFILE_MAIN_STILL 4
#define HEVC_PROFILE_MAIN_10HDR10 4096
#define HEVC_PROFILE_MAIN_10HDR10_PLUS 8192
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_PROFILE, HEVC_PROFILE_MAIN10);

#define HEVC_MAIN_TIER_LEVEL_1      0x1
#define HEVC_HIGH_TIER_LEVEL_1      0x2
#define HEVC_MAIN_TIER_LEVEL_2      0x4
#define HEVC_HIGH_TIER_LEVEL_2      0x8
#define HEVC_MAIN_TIER_LEVEL_2_1    0x10
#define HEVC_HIGH_TIER_LEVEL_2_1    0x20
#define HEVC_MAIN_TIER_LEVEL_3      0x40
#define HEVC_HIGH_TIER_LEVEL_3      0x80
#define HEVC_MAIN_TIER_LEVEL_3_1    0x100
#define HEVC_HIGH_TIER_LEVEL_3_1    0x200
#define HEVC_MAIN_TIER_LEVEL_4      0x400
#define HEVC_HIGH_TIER_LEVEL_4      0x800
#define HEVC_MAIN_TIER_LEVEL_4_1    0x1000
#define HEVC_HIGH_TIER_LEVEL_4_1    0x2000
#define HEVC_MAIN_TIER_LEVEL_5      0x4000
#define HEVC_HIGH_TIER_LEVEL_5      0x8000
#define HEVC_MAIN_TIER_LEVEL_5_1    0x10000
#define HEVC_HIGH_TIER_LEVEL_5_1    0x20000
#define HEVC_MAIN_TIER_LEVEL_5_2    0x40000
#define HEVC_HIGH_TIER_LEVEL_5_2    0x80000
#define HEVC_MAIN_TIER_LEVEL_6      0x100000
#define HEVC_HIGH_TIER_LEVEL_6      0x200000
#define HEVC_MAIN_TIER_LEVEL_6_1    0x400000
#define HEVC_HIGH_TIER_LEVEL_6_1    0x800000
#define HEVC_MAIN_TIER_LEVEL_6_2    0x1000000
#define HEVC_HIGH_TIER_LEVEL_6_2    0x2000000
            AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_LEVEL, HEVC_HIGH_TIER_LEVEL_4);
            AMediaFormat_setInt32(format, "prepend-sps-pps-to-idr-frames", 1);
            AMediaFormat_setInt32(format, "prepend-vps-sps-pps-to-idr-frames", 1);
    
            media_status_t st = AMediaCodec_configure(codec, format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
            if (st != AMEDIA_OK) {
                rtp_log("AMediaCodec_configure failed: %d", st);
                AMediaFormat_delete(format);
                AMediaCodec_delete(codec);
                return -1;
            }
    
            AMediaFormat_delete(format);
    
            st = AMediaCodec_start(codec);
            if (st != AMEDIA_OK) {
                rtp_log("AMediaCodec_start failed: %d", st);
                AMediaCodec_delete(codec);
                return -1;
            }
    
            ctx->codec.media_codec.codec = codec;
            ctx->codec.media_codec.w = w;
            ctx->codec.media_codec.h = h;
    
            rtp_log("H264 MediaCodec encoder initialized: %dx%d, %d fps, %d bitrate\n", w, h, fps, bitrate);
        } break;
#endif // __ANDROID__
    default: 
        rtp_log("ERROR: unsupported codec %d\n", video_codec_id); 
        return -1;
    }

    return 0;
}

static inline int rtp_write_nals(Rtp_Context *ctx, unsigned char **units, int *sizes, int count, long long pts, long long dts, int is_key, int si) {
    int size = 0;
    for (int i = 0; i < count; i++) size += sizes[i];

    char h[28];
    memcpy(h + 0, &pts, 8);
    memcpy(h + 8, &dts, 8);
    memcpy(h + 16, &si, 4);
    memcpy(h + 20, &is_key, 4);
    memcpy(h + 24, &size, 4);

    if (write(ctx->fd, h, 28) < 0) {
        rtp_log("header write failed\n");
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (write(ctx->fd, units[i], sizes[i]) < 0) {
            rtp_log("payload write failed\n");
            return -1;
        }
    }

    return size;
}

static inline int rtp_encode_write(Rtp_Context *ctx, int pts) {
    switch (ctx->video_codec_id) {
    case CODEC_H264: {
            x264_nal_t *nals = NULL;
            int nals_cnt = 0;

            int ret = x264_encoder_encode(ctx->codec.h264.x264, &nals, &nals_cnt, &ctx->codec.h264.in, &ctx->codec.h264.out);
            if (ret < 0 || nals_cnt == 0) return -1;

            unsigned char *payloads[nals_cnt];
            int sizes[nals_cnt];
            int is_key = 0;

            for (int n = 0; n < nals_cnt; n++) {
                payloads[n] = nals[n].p_payload;
                sizes[n] = nals[n].i_payload;

                if (nals[n].i_type == NAL_SLICE_IDR) is_key = 1;

#ifdef __APPLE__
                rtp_log("    NAL %03d | size=%04d | type=%d | key=%d | ", n, sizes[n], nals[n].i_type, is_key);
                for (int idx = 0; idx < 30 && idx < sizes[n]; idx++) rtp_log("%03x ", payloads[n][idx]);
                rtp_log("\n");
#endif // __APPLE__
            }

            if (rtp_write_nals(ctx, payloads, sizes, nals_cnt, pts, pts, is_key, 0) < 0) return 1;
        } break;
    case CODEC_HEVC: {
            x265_nal *nals = NULL;
            uint32_t nal_count = 0;

            int ret = x265_encoder_encode(ctx->codec.hevc.x265, &nals, &nal_count, ctx->codec.hevc.in, NULL);
            if (ret < 0 || nal_count == 0) return -1;

            unsigned char *payloads[nal_count];
            int sizes[nal_count];
            int is_key = 0;

            for (uint32_t n = 0; n < nal_count; n++) {
                payloads[n] = nals[n].payload;
                sizes[n] = nals[n].sizeBytes;

                is_key = (nals[n].type == NAL_UNIT_CODED_SLICE_IDR_W_RADL) || (nals[n].type == NAL_UNIT_CODED_SLICE_IDR_N_LP) || (nals[n].type == NAL_UNIT_CODED_SLICE_CRA);

#ifdef __APPLE__
                rtp_log("    NAL %03d | size=%04d | type=%d | key=%d | ", n, sizes[n], nals[n].type, is_key);
                for (int idx = 0; idx < 30 && idx < sizes[n]; idx++) rtp_log("%03x ", payloads[n][idx]);
                rtp_log("\n");
#endif // __APPLE__
            }

            if (rtp_write_nals(ctx, payloads, sizes, nal_count, pts, pts, is_key, 0) < 0) return -1;
        } break;
#ifdef __ANDROID__
    case CODEC_H264_MEDIACODEC: 
    case CODEC_HEVC_MEDIACODEC: {
            AMediaCodec *codec = ctx->codec.media_codec.codec;
            int w  = ctx->codec.media_codec.w;
            int h  = ctx->codec.media_codec.h;
        
            uint8_t *Y = ctx->codec.media_codec.Y;
            uint8_t *U = ctx->codec.media_codec.U;
            uint8_t *V = ctx->codec.media_codec.V;
        
            int Ys = ctx->codec.media_codec.Ys;
            int Us = ctx->codec.media_codec.Us;
            int Vs = ctx->codec.media_codec.Vs;
        
            ssize_t index = AMediaCodec_dequeueInputBuffer(codec, 10000);
            if (index >= 0) {
                size_t cap;
                uint8_t *buf = AMediaCodec_getInputBuffer(codec, index, &cap);
                uint8_t *dst = buf;
        
                for (int i = 0; i < h; i++) {
                    memcpy(dst, Y + i * Ys, w);
                    dst += w;
                }

                int h2 = h / 2;
                int w2 = w / 2;

                for (int i = 0; i < h2; i++) {
                    memcpy(dst, U + i * Us, w2);
                    dst += w2;
                }
                for (int i = 0; i < h2; i++) {
                    memcpy(dst, V + i * Vs, w2);
                    dst += w2;
                }
        
                size_t total = (dst - buf);
        
                AMediaCodec_queueInputBuffer(codec, index, 0, total, pts, 0);
            }
        
            AMediaCodecBufferInfo info;
            for (;;) {
                ssize_t outIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, 0);
                if (outIndex < 0) break;
        
                size_t packetSize;
                uint8_t *packet = AMediaCodec_getOutputBuffer(codec, outIndex, &packetSize);

                unsigned char *payloads[1] = {packet};
                int sizes[1] = {info.size};
                int is_key = (info.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME) != 0;

                if (rtp_write_nals(ctx, payloads, sizes, 1, pts, pts, is_key, 0) < 0) {
                    break;
                }

                AMediaCodec_releaseOutputBuffer(codec, outIndex, false);
            }
        } break;
#endif // __ANDROID__
    default: 
        rtp_log("ERROR: unsupported codec %d\n", ctx->video_codec_id); 
        break;
    }
    return 0;
}

static inline void rtp_close(Rtp_Context **out_ctx) {
    Rtp_Context *ctx = *out_ctx;
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
#ifdef __ANDROID
    case CODEC_H264_MEDIACODEC:
        AMediaCodec_stop(ctx->codec.media_codec.codec);
        AMediaCodec_delete(ctx->codec.media_codec.codec);
        break;
    case CODEC_HEVC_MEDIACODEC:
        AMediaCodec_stop(ctx->codec.media_codec.codec);
        AMediaCodec_delete(ctx->codec.media_codec.codec);
        break;
#endif // __ANDROID__
    default: 
        rtp_log("ERROR: unsupported codec %d\n", ctx->video_codec_id); 
        break;
    }

    free(*out_ctx);
    *out_ctx = NULL;
}
