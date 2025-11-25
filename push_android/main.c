#include <jni.h>

#include <pthread.h>
#include <math.h>

#include "rtp.h"
#include "acm.h"
#include "renderer.h"

#define W 960
#define H 540
#define FPS 30
#define BITRATE 1000
#define GOP FPS
#define ADDRESS "192.168.8.16" // replace with your actual localhost ip
#define PORT 1935
#define STREAM_ID "stream"

typedef struct Context {
    pthread_t thread;
    int preview;
    int publish;
    ANativeWindow *win;
    int w,h;
} Context;

void *thread_proc(void *arg) {
    Context *ctx = (Context *)arg;

    Acm_Context *acm_ctx = NULL;
    int ret = acm_open(&acm_ctx, FPS, FPS, W, H, 0);
    if (ret < 0) {
        rtp_log("ERROR: acm_open %d\n", ret);
        return NULL;
    }

    Rtp_Context *rtp_ctx = NULL;
    if (rtp_open(&rtp_ctx, ADDRESS, PORT, STREAM_ID, acm_ctx->w, acm_ctx->h, FPS, GOP, BITRATE, CODEC_HEVC_MEDIACODEC, CODEC_AAC, 44100, 2) < 0) {
        rtp_log("ERROR: cannot open rtp.\n");
        return NULL;
    }

    Renderer_Context *renderer_ctx = NULL;
    if (renderer_open(&renderer_ctx, ctx->win) < 0) {
        rtp_log("ERROR: cannot open renderer.\n");
        return NULL;
    }

    Yuv420p_Renderer_Context *yuv_renderer_ctx = NULL;
    if (yuv_renderer_open(&yuv_renderer_ctx, ctx->w, ctx->h) < 0) {
        rtp_log("ERROR: cannot open yuv renderer.\n");
        return NULL;
    }

    int pts = 0;
    while (ctx->preview) {
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        uint8_t *Y = NULL;
        uint8_t *U = NULL;
        uint8_t *V = NULL;
        if (acm_read_frame(acm_ctx, &Y, &U, &V) == 0) {
            yuv_renderer_render(yuv_renderer_ctx, Y, U, V, acm_ctx->w, acm_ctx->w/2, acm_ctx->w/2, acm_ctx->w, acm_ctx->h, ctx->w, ctx->h);
            renderer_swap_buffers(renderer_ctx);

            if (ctx->publish) {
                switch (rtp_ctx->video_codec_id) {
                    case CODEC_H264:
                        rtp_ctx->codec.h264.in.img.i_csp = X264_CSP_I420;
                        rtp_ctx->codec.h264.in.img.i_plane = 3;
                        rtp_ctx->codec.h264.in.i_pts = pts;
                        rtp_ctx->codec.h264.in.img.plane[0] = Y;
                        rtp_ctx->codec.h264.in.img.plane[1] = U;
                        rtp_ctx->codec.h264.in.img.plane[2] = V;
                        rtp_ctx->codec.h264.in.img.i_stride[0] = acm_ctx->w;
                        rtp_ctx->codec.h264.in.img.i_stride[1] = acm_ctx->w / 2;
                        rtp_ctx->codec.h264.in.img.i_stride[2] = acm_ctx->w / 2;
                        break;
                    case CODEC_HEVC:
                        rtp_ctx->codec.hevc.in->planes[0] = Y;
                        rtp_ctx->codec.hevc.in->planes[1] = U;
                        rtp_ctx->codec.hevc.in->planes[2] = V;
                        rtp_ctx->codec.hevc.in->stride[0] = acm_ctx->w;
                        rtp_ctx->codec.hevc.in->stride[1] = acm_ctx->w / 2;
                        rtp_ctx->codec.hevc.in->stride[2] = acm_ctx->w / 2;
                        break;
                    case CODEC_H264_MEDIACODEC:
                    case CODEC_HEVC_MEDIACODEC:
                        rtp_ctx->codec.media_codec.Y = Y;
                        rtp_ctx->codec.media_codec.U = U;
                        rtp_ctx->codec.media_codec.V = V;
                        rtp_ctx->codec.media_codec.Ys = acm_ctx->w;
                        rtp_ctx->codec.media_codec.Us = acm_ctx->w / 2;
                        rtp_ctx->codec.media_codec.Vs = acm_ctx->w / 2;
                    default: break;
                }

                PROFILE_CALL(rtp_encode_write, rtp_encode_write(rtp_ctx, pts++));
            }

            free(Y);
            free(U);
            free(V);
        }
    }

    yuv_renderer_close(&yuv_renderer_ctx);
    renderer_close(&renderer_ctx);
    rtp_close(&rtp_ctx);
    acm_close(&acm_ctx);
    free(ctx);
    return NULL;
}

JNIEXPORT jlong JNICALL Java_com_livsho_android_Engine_startPreview(JNIEnv *env, jobject thiz, jobject surface) {
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);
    if (!win) {
        return (jlong)0;
    }

    Context *ctx = (Context *)malloc(sizeof(Context));
    if (!ctx) {
        return (jlong)0;
    }

    ctx->preview = 1;
    ctx->publish = 0;
    ctx->win = win;

    pthread_create(&ctx->thread, NULL, thread_proc, ctx);
    pthread_detach(ctx->thread);

    return (jlong)(intptr_t)ctx;
}

JNIEXPORT void JNICALL Java_com_livsho_android_Engine_stopPreview(JNIEnv *env, jobject thiz, jlong handle) {
    Context *ctx = (Context *)(intptr_t)handle;
    if (!ctx) return;
    ctx->preview = 0;
}

JNIEXPORT void JNICALL Java_com_livsho_android_Engine_startPublishing(JNIEnv *env, jobject thiz, jlong handle) {
    Context *ctx = (Context *)(intptr_t)handle;
    if (!ctx) return;
    ctx->publish = 1;
}

JNIEXPORT void JNICALL Java_com_livsho_android_Engine_stopPublishing(JNIEnv *env, jobject thiz, jlong handle) {
    Context *ctx = (Context *)(intptr_t)handle;
    if (!ctx) return;
    ctx->publish = 0;
}

JNIEXPORT void JNICALL Java_com_livsho_android_Engine_set(JNIEnv *env, jobject thiz, jlong handle, jint w, jint h) {
    Context *ctx = (Context *)(intptr_t)handle;
    if (!ctx) return;
    ctx->w = w;
    ctx->h = h;
}
