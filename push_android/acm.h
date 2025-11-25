#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>

#include <android/looper.h>

#include <libavutil/threadmessage.h>
#include <libyuv.h>

typedef struct {
    ACameraManager *mgr;
    ACameraDevice *dev;
    ACameraCaptureSession *sess;
    AImageReader *reader;
    ACaptureRequest *req;
    AVThreadMessageQueue *q;
    int sensor;
    int w, h;
} Acm_Context;

static void on_image(void *ctx, AImageReader *r) {
    Acm_Context *acm = (Acm_Context *)ctx;
    AImage *img = 0;
    if (!acm || !acm->q) return;
    if (AImageReader_acquireLatestImage(r, &img) != AMEDIA_OK) return;
    if (av_thread_message_queue_send(acm->q, &img, 0) < 0) AImage_delete(img);
}

static inline void acm_close(Acm_Context **out_ctx) {
    Acm_Context *ctx = *out_ctx;
    if (!ctx) return;

    if (ctx->sess) {
        ACameraCaptureSession_stopRepeating(ctx->sess);
        ACameraCaptureSession_abortCaptures(ctx->sess);
        ACameraCaptureSession_close(ctx->sess);
        ctx->sess = 0;
    }

    if (ctx->req) {
        ACaptureRequest_free(ctx->req);
        ctx->req = 0;
    }

    if (ctx->reader) {
        AImageReader_delete(ctx->reader);
        ctx->reader = 0;
    }

    if (ctx->dev) {
        ACameraDevice_close(ctx->dev);
        ctx->dev = 0;
    }

    if (ctx->mgr) {
        ACameraManager_delete(ctx->mgr);
        ctx->mgr = 0;
    }

    if (ctx->q) {
        av_thread_message_queue_free(&ctx->q);
        ctx->q = 0;
    }

    free(*out_ctx);
    *out_ctx = NULL;
}

static inline int acm_release_frame(AImage *img) {
    if (!img) return -1;
    AImage_delete(img);
    return 0;
}

static inline int acm_open(Acm_Context **out_ctx, int min_fps, int max_fps, int w, int h, int want_front) {
    rtp_log("acm_open: min_fps=%d max_fps=%d req_w=%d req_h=%d want_front=%d\n", min_fps, max_fps, w, h, want_front);

    acm_close(out_ctx);

    Acm_Context *ctx = calloc(1, sizeof(Acm_Context));
    *out_ctx = ctx;

    av_thread_message_queue_alloc(&ctx->q, 1000 * 10, sizeof(AImage *));
    ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    ctx->mgr = ACameraManager_create();

    ACameraIdList *ids = 0;
    int ret = ACameraManager_getCameraIdList(ctx->mgr, &ids);
    if (ret != ACAMERA_OK) return -1;

    const char *id = 0;

    for (int i = 0; i < ids->numCameras; i++) {
        const char *cid = ids->cameraIds[i];

        ACameraMetadata *m = 0;
        ret = ACameraManager_getCameraCharacteristics(ctx->mgr, cid, &m);
        if (ret != ACAMERA_OK) continue;

        ACameraMetadata_const_entry facing;
        ret = ACameraMetadata_getConstEntry(m, ACAMERA_LENS_FACING, &facing);
        if (ret == ACAMERA_OK) {
            int f = facing.data.u8[0];
            rtp_log("  lensFacing value=%d (%s)\n", f, f == ACAMERA_LENS_FACING_FRONT ? "front" : f == ACAMERA_LENS_FACING_BACK ? "back" : "external");

            if (want_front) {
                if (f == ACAMERA_LENS_FACING_FRONT) {
                    rtp_log("  choosing this camera (front)\n");
                    id = strdup(cid);
                    ACameraMetadata_free(m);
                    break;
                } else {
                    rtp_log("  skipping (not front)\n");
                }
            } else {
                rtp_log("  want_front=0 so selecting first available camera\n");
                id = strdup(cid);
                ACameraMetadata_free(m);
                break;
            }
        }

        ACameraMetadata_free(m);
    }

    if (!id) {
        rtp_log("no suitable camera found; falling back to first\n");
        id = ids->cameraIds[0];
    }

    rtp_log("camera_id chosen = %s\n", id);

    ACameraMetadata *meta = 0;
    ret = ACameraManager_getCameraCharacteristics(ctx->mgr, id, &meta);
    if (ret != ACAMERA_OK) return -2;

    ACameraMetadata_const_entry orient = {0};
    ret = ACameraMetadata_getConstEntry(meta, ACAMERA_SENSOR_ORIENTATION, &orient);
    if (ret == ACAMERA_OK && orient.count > 0) {
        ctx->sensor = orient.data.i32[0];
        rtp_log("sensor orientation = %d\n", ctx->sensor);
    } else {
        ctx->sensor = 0;
        rtp_log("failed to read sensor orientation, defaulting to 0\n");
    }

    ACameraMetadata_const_entry cfgs;
    ret = ACameraMetadata_getConstEntry(meta, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &cfgs);
    if (ret != ACAMERA_OK) return -3;

    int req_w = w, req_h = h;
    int best_w = 0, best_h = 0;
    rtp_log("searching for exact match resolution %dx%d\n", req_w, req_h);
    for (int i = 0; i < cfgs.count; i += 4) {
        int32_t fmt = cfgs.data.i32[i];
        int32_t cw = cfgs.data.i32[i + 1];
        int32_t ch = cfgs.data.i32[i + 2];
        int32_t io = cfgs.data.i32[i + 3];

        if (io == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT && fmt == AIMAGE_FORMAT_YUV_420_888) {
            rtp_log("  cfg fmt=%d w=%d h=%d io=%d\n", fmt, cw, ch, io);
            if (cw == req_w && ch == req_h) {
                rtp_log("  exact match found\n");
                best_w = cw;
                best_h = ch;
                break;
            }
        }
    }

    if (best_w == 0 || best_h == 0) {
        rtp_log("no exact match; searching closest resolution\n");

        int target_area = req_w * req_h;
        int best_diff = INT_MAX;

        for (int i = 0; i < cfgs.count; i += 4) {
            int32_t fmt = cfgs.data.i32[i];
            int32_t cw = cfgs.data.i32[i + 1];
            int32_t ch = cfgs.data.i32[i + 2];
            int32_t io = cfgs.data.i32[i + 3];

            if (io == ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT && fmt == AIMAGE_FORMAT_YUV_420_888) {
                int area = cw * ch;
                int diff = abs(area - target_area);

                rtp_log("  candidate w=%d h=%d area=%d diff=%d\n", cw, ch, area, diff);

                if (diff < best_diff) {
                    best_diff = diff;
                    best_w = cw;
                    best_h = ch;
                    rtp_log("  new best resolution %dx%d diff=%d\n", best_w, best_h, best_diff);
                }
            }
        }
    }

    if (best_w == 0 || best_h == 0) {
        rtp_log("resolution selection failed\n");
        return -4;
    }

    w = best_w;
    h = best_h;

    rtp_log("chosen resolution = %dx%d\n", best_w, best_h);

    ACameraMetadata_const_entry fps;
    ret = ACameraMetadata_getConstEntry(meta, ACAMERA_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, &fps);
    if (ret != ACAMERA_OK) return -5;

    rtp_log("Normal FPS ranges:\n");
    for (int i = 0; i < fps.count; i += 2) {
        rtp_log("  [%d, %d]\n", fps.data.i32[i], fps.data.i32[i + 1]);
    }

    int chosen[2] = {0, 0};
    int found = 0;

    for (int i = fps.count - 2; i >= 0; i -= 2) {
        int lo = fps.data.i32[i];
        int hi = fps.data.i32[i + 1];

        if (hi <= max_fps && lo >= min_fps) {
            chosen[0] = lo;
            chosen[1] = hi;
            found = 1;
            break;
        }
    }

    if (!found) {
        rtp_log("no FPS range found for constraints (%d-%d)\n", min_fps, max_fps);
        return -6;
    }

    rtp_log("chosen FPS = [%d, %d]\n", chosen[0], chosen[1]);

    ACameraMetadata_free(meta);
    ACameraManager_deleteCameraIdList(ids);

    ACameraDevice_stateCallbacks dc = {.context = ctx, .onDisconnected = 0, .onError = 0};
    ret = ACameraManager_openCamera(ctx->mgr, id, &dc, &ctx->dev);
    if (ret != AMEDIA_OK) {
        return -1;
    }

    ret = AImageReader_new(w, h, AIMAGE_FORMAT_YUV_420_888, 4, &ctx->reader);
    if (ret != AMEDIA_OK) return -8;

    AImageReader_ImageListener il = {.context = ctx, .onImageAvailable = on_image};
    AImageReader_setImageListener(ctx->reader, &il);

    ANativeWindow *win = 0;
    ret = AImageReader_getWindow(ctx->reader, &win);
    if (ret != AMEDIA_OK) return -9;

    ACaptureSessionOutputContainer *cont = 0;
    ACaptureSessionOutputContainer_create(&cont);

    ACaptureSessionOutput *out = 0;
    ACaptureSessionOutput_create(win, &out);
    ACaptureSessionOutputContainer_add(cont, out);

    ACameraOutputTarget *tgt = 0;
    ACameraOutputTarget_create(win, &tgt);

    ret = ACameraDevice_createCaptureRequest(ctx->dev, TEMPLATE_RECORD, &ctx->req);
    if (ret != ACAMERA_OK) return -10;

    ret = ACaptureRequest_addTarget(ctx->req, tgt);
    if (ret != ACAMERA_OK) return -11;

    ACameraCaptureSession_stateCallbacks sc = {.context = ctx, .onActive = 0, .onReady = 0, .onClosed = 0};

    ret = ACameraDevice_createCaptureSession(ctx->dev, cont, &sc, &ctx->sess);
    if (ret != ACAMERA_OK) return -12;

    ret = ACaptureRequest_setEntry_i32(ctx->req, ACAMERA_CONTROL_AE_TARGET_FPS_RANGE, 2, chosen);
    if (ret != ACAMERA_OK) return -13;

    ret = ACameraCaptureSession_setRepeatingRequest(ctx->sess, 0, 1, &ctx->req, 0);
    if (ret != ACAMERA_OK) return -14;

    ctx->w = (ctx->sensor == 90 || ctx->sensor == 270) ? h : w;
    ctx->h = (ctx->sensor == 90 || ctx->sensor == 270) ? w : h;

    rtp_log("Camera opened successfully: %dx%d @ %d-%d fps\n", w, h, chosen[0], chosen[1]);
    return 0;
}

static inline int acm_read_frame(Acm_Context *ctx, uint8_t **out_y, uint8_t **out_u, uint8_t **out_v) {
    AImage *image;
    if (av_thread_message_queue_recv(ctx->q, &image, 0) < 0) return -1;

    int width, height;
    AImage_getWidth(image, &width);
    AImage_getHeight(image, &height);

    uint8_t *plane_y = NULL, *plane_u = NULL, *plane_v = NULL;
    int stride_y = 0, stride_u = 0, stride_v = 0;
    int pixel_stride_u = 0, pixel_stride_v = 0;
    int data_len = 0;

    AImage_getPlaneData(image, 0, &plane_y, &data_len);
    if (!plane_y || data_len <= 0) {
        acm_release_frame(image);
        return -1;
    }
    AImage_getPlaneRowStride(image, 0, &stride_y);

    AImage_getPlaneData(image, 1, &plane_u, &data_len);
    if (!plane_u || data_len <= 0) {
        acm_release_frame(image);
        return -1;
    }
    AImage_getPlaneRowStride(image, 1, &stride_u);
    AImage_getPlanePixelStride(image, 1, &pixel_stride_u);

    AImage_getPlaneData(image, 2, &plane_v, &data_len);
    if (!plane_v || data_len <= 0) {
        acm_release_frame(image);
        return -1;
    }
    AImage_getPlaneRowStride(image, 2, &stride_v);
    AImage_getPlanePixelStride(image, 2, &pixel_stride_v);

    uint8_t *src_y = malloc(width * height);
    uint8_t *src_u = malloc((width / 2) * (height / 2));
    uint8_t *src_v = malloc((width / 2) * (height / 2));
    if (!src_y || !src_u || !src_v) {
        free(src_y);
        free(src_u);
        free(src_v);
        acm_release_frame(image);
        return -1;
    }

    int chroma_pixel_stride = pixel_stride_u > pixel_stride_v ? pixel_stride_u : pixel_stride_v;

    if (Android420ToI420(plane_y, stride_y, plane_u, stride_u, plane_v, stride_v, chroma_pixel_stride, src_y, width, src_u, width / 2, src_v, width / 2, width, height) != 0) {
        free(src_y);
        free(src_u);
        free(src_v);
        acm_release_frame(image);
        return -1;
    }

    // Select libyuv rotation mode
    enum RotationMode mode = kRotate0;
    switch (ctx->sensor) {
    case 90: mode = kRotate90; break;
    case 180: mode = kRotate180; break;
    case 270: mode = kRotate270; break;
    default: mode = kRotate0; break;
    }

    int rot_w = (mode == kRotate90 || mode == kRotate270) ? height : width;
    int rot_h = (mode == kRotate90 || mode == kRotate270) ? width : height;

    uint8_t *dst_y = malloc(rot_w * rot_h);
    uint8_t *dst_u = malloc((rot_w / 2) * (rot_h / 2));
    uint8_t *dst_v = malloc((rot_w / 2) * (rot_h / 2));
    if (!dst_y || !dst_u || !dst_v) {
        free(src_y);
        free(src_u);
        free(src_v);
        free(dst_y);
        free(dst_u);
        free(dst_v);
        acm_release_frame(image);
        return -1;
    }

    if (I420Rotate(src_y, width, src_u, width / 2, src_v, width / 2, dst_y, rot_w, dst_u, rot_w / 2, dst_v, rot_w / 2, width, height, mode) != 0) {
        free(src_y);
        free(src_u);
        free(src_v);
        free(dst_y);
        free(dst_u);
        free(dst_v);
        acm_release_frame(image);
        return -1;
    }

    free(src_y);
    free(src_u);
    free(src_v);

    *out_y = dst_y;
    *out_u = dst_u;
    *out_v = dst_v;

    acm_release_frame(image);
    return 0;
}
