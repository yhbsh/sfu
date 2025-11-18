#pragma once
#include <AVFoundation/AVFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/threadmessage.h>
#include <libswscale/swscale.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AVFContext AVFContext;

typedef struct CVFrame {
    uint8_t *data[3]; // Y, U, V planes (YUV420P)
    int stride[3];
    int width;
    int height;
    int pixfmt; // always AV_PIX_FMT_YUV420P
    void *priv; // internal: AVFrame*
} CVFrame;

int avf_camera_open(AVFContext **out_ctx, int fps, int target_w, int target_h);
int avf_screen_open(AVFContext **out_ctx, int fps, int target_w, int target_h);

int avf_read_frame(AVFContext *ctx, CVFrame *out_frame);
void avf_release_frame(CVFrame *frame);
void avf_close(AVFContext *ctx);

#ifdef __cplusplus
}
#endif

// ============================================================================
//                           IMPLEMENTATION
// ============================================================================
#ifdef AVF_IMPL

typedef struct MsgPacket {
    CVPixelBufferRef buf;
} MsgPacket;

struct AVFContext {
    AVCaptureSession *session;
    AVThreadMessageQueue *queue;

    int running;
    int target_w;
    int target_h;

    SwsContext *sws;
    AVFrame *scaled; // reusable output frame
};

@interface FrameDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, assign) AVFContext *ctx;
@end

@implementation FrameDelegate
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    CVPixelBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;

    CVPixelBufferRetain(imageBuffer);

    MsgPacket pkt;
    pkt.buf = imageBuffer;

    av_thread_message_queue_send(self.ctx->queue, &pkt, 0);
}
@end

// ---------------------------------------------------------------------------
// Utility to prepare scaling frame
// ---------------------------------------------------------------------------
static int init_scaled_frame(AVFContext *ctx) {
    ctx->scaled = av_frame_alloc();

    ctx->scaled->format = AV_PIX_FMT_YUV420P;
    ctx->scaled->width = ctx->target_w;
    ctx->scaled->height = ctx->target_h;

    if (av_frame_get_buffer(ctx->scaled, 32) < 0) return -1;

    return 0;
}

// ---------------------------------------------------------------------------
// Open camera
// ---------------------------------------------------------------------------
static int avf_open_common(AVFContext **out_ctx, AVCaptureInput *input, int fps, int target_w, int target_h) {
    AVFContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return -1;

    ctx->target_w = target_w;
    ctx->target_h = target_h;

    if (av_thread_message_queue_alloc(&ctx->queue, 8, sizeof(MsgPacket)) < 0) {
        free(ctx);
        return -1;
    }

    if (init_scaled_frame(ctx) < 0) {
        av_thread_message_queue_free(&ctx->queue);
        free(ctx);
        return -1;
    }

    AVCaptureVideoDataOutput *output = [[AVCaptureVideoDataOutput alloc] init];
    output.videoSettings = @{(NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8Planar)};

    FrameDelegate *delegate = [[FrameDelegate alloc] init];
    delegate.ctx = ctx;

    dispatch_queue_t q = dispatch_queue_create("avf.capture.queue", DISPATCH_QUEUE_SERIAL);
    [output setSampleBufferDelegate:delegate queue:q];

    AVCaptureSession *session = [[AVCaptureSession alloc] init];
    ctx->session = session;

    [session beginConfiguration];

    if ([session canAddInput:input]) [session addInput:input];

    if ([session canAddOutput:output]) [session addOutput:output];

    AVCaptureConnection *conn = [output connectionWithMediaType:AVMediaTypeVideo];

    if (conn && [conn isVideoRotationAngleSupported:0]) conn.videoRotationAngle = 0;

    [session commitConfiguration];

    ctx->running = 1;
    [session startRunning];

    *out_ctx = ctx;
    return 0;
}

int avf_camera_open(AVFContext **out_ctx, int fps, int target_w, int target_h) {
    @autoreleasepool {

        AVCaptureDevice *device = nil;
        AVCaptureDeviceDiscoverySession *discovery = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[ AVCaptureDeviceTypeBuiltInWideAngleCamera ] mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];

        NSArray<AVCaptureDevice *> *devices = [discovery devices];
        if (devices.count == 0) return -1;
        device = devices.firstObject;

        NSError *err = nil;
        AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
        if (!input) return -1;

        [device lockForConfiguration:&err];
        if (!err) {
            device.activeVideoMinFrameDuration = CMTimeMake(1, fps);
            device.activeVideoMaxFrameDuration = CMTimeMake(1, fps);
        }
        [device unlockForConfiguration];

        return avf_open_common(out_ctx, input, fps, target_w, target_h);
    }
}

int avf_screen_open(AVFContext **out_ctx, int fps, int target_w, int target_h) {
    @autoreleasepool {
        CGDirectDisplayID displayID = CGMainDisplayID();
        AVCaptureScreenInput *input = [[AVCaptureScreenInput alloc] initWithDisplayID:displayID];
        if (!input) return -1;

        input.minFrameDuration = CMTimeMake(1, fps);
        input.capturesCursor = YES;
        input.capturesMouseClicks = YES;

        return avf_open_common(out_ctx, input, fps, target_w, target_h);
    }
}

// ---------------------------------------------------------------------------
// Frame read + scale
// ---------------------------------------------------------------------------
int avf_read_frame(AVFContext *ctx, CVFrame *out_frame) {
    if (!ctx || !out_frame || !ctx->running) return -1;

    MsgPacket pkt;
    if (av_thread_message_queue_recv(ctx->queue, &pkt, 0) < 0) return -1;

    CVPixelBufferRef buf = pkt.buf;
    if (!buf) return -1;

    CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);

    int src_w = (int)CVPixelBufferGetWidth(buf);
    int src_h = (int)CVPixelBufferGetHeight(buf);

    uint8_t *src_data[3] = {0};
    int src_stride[3] = {0};

    size_t planes = CVPixelBufferGetPlaneCount(buf);
    for (size_t i = 0; i < planes && i < 3; i++) {
        src_data[i] = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(buf, i);
        src_stride[i] = (int)CVPixelBufferGetBytesPerRowOfPlane(buf, i);
    }

    if (!ctx->sws) {
        ctx->sws = sws_getContext(src_w, src_h, AV_PIX_FMT_YUV420P, ctx->target_w, ctx->target_h, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
        if (!ctx->sws) {
            CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferRelease(buf);
            return -1;
        }
    }

    uint8_t *dst_data[3];
    int dst_stride[3];

    for (int i = 0; i < 3; i++) {
        dst_data[i] = ctx->scaled->data[i];
        dst_stride[i] = ctx->scaled->linesize[i];
    }

    sws_scale(ctx->sws, (const uint8_t *const *)src_data, src_stride, 0, src_h, dst_data, dst_stride);

    CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferRelease(buf);

    out_frame->width = ctx->target_w;
    out_frame->height = ctx->target_h;
    out_frame->pixfmt = AV_PIX_FMT_YUV420P;

    out_frame->data[0] = ctx->scaled->data[0];
    out_frame->data[1] = ctx->scaled->data[1];
    out_frame->data[2] = ctx->scaled->data[2];

    out_frame->stride[0] = ctx->scaled->linesize[0];
    out_frame->stride[1] = ctx->scaled->linesize[1];
    out_frame->stride[2] = ctx->scaled->linesize[2];

    out_frame->priv = ctx->scaled;

    return 0;
}

// ---------------------------------------------------------------------------
// Frame release (no-op: frames are reused)
// ---------------------------------------------------------------------------
void avf_release_frame(CVFrame *frame) {
    // caller should not free anything; AVFrame is managed internally
    (void)frame;
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------
void avf_close(AVFContext *ctx) {
    if (!ctx) return;
    ctx->running = 0;

    if (ctx->session) [ctx->session stopRunning];

    if (ctx->sws) sws_freeContext(ctx->sws);

    if (ctx->scaled) av_frame_free(&ctx->scaled);

    av_thread_message_queue_free(&ctx->queue);

    if (ctx->session) [ctx->session release];

    free(ctx);
}

#endif // AVF_IMPL
