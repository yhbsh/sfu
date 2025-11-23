#include "avf.h"
#include "rtp.h"
#include "common.h"

int main(void) {
    RTP_Context *rtp_ctx = NULL;
    if (rtp_open(&rtp_ctx, ADDRESS, PORT, STREAM_ID, W, H, FPS, GOP, BITRATE, CODEC_HEVC, CODEC_AAC, 44100, 2) < 0) {
        return -1;
    }

    AVF_Context *ctx = NULL;
    if (avf_camera_open(&ctx, FPS, W, H) < 0) {
        return -1;
    }

    for (int i = 0; i < NUM_FRAMES; i++) {
        AVF_Frame frame = {0};
        avf_read_frame(ctx, &frame);

        switch (rtp_ctx->video_codec_id) {
            case CODEC_H264:
                rtp_ctx->codec.h264.in.img.i_csp = X264_CSP_I420;
                rtp_ctx->codec.h264.in.img.i_plane = 3;
                rtp_ctx->codec.h264.in.i_pts = i;
                rtp_ctx->codec.h264.in.img.plane[0] = frame.data[0];
                rtp_ctx->codec.h264.in.img.plane[1] = frame.data[1];
                rtp_ctx->codec.h264.in.img.plane[2] = frame.data[2];
                rtp_ctx->codec.h264.in.img.i_stride[0] = frame.stride[0];
                rtp_ctx->codec.h264.in.img.i_stride[1] = frame.stride[1];
                rtp_ctx->codec.h264.in.img.i_stride[2] = frame.stride[2];
                break;
            case CODEC_HEVC:
                rtp_ctx->codec.hevc.in->planes[0] = frame.data[0];
                rtp_ctx->codec.hevc.in->planes[1] = frame.data[1];
                rtp_ctx->codec.hevc.in->planes[2] = frame.data[2];
                rtp_ctx->codec.hevc.in->stride[0] = frame.stride[0];
                rtp_ctx->codec.hevc.in->stride[1] = frame.stride[1];
                rtp_ctx->codec.hevc.in->stride[2] = frame.stride[2];
                break;
            case CODEC_AV1:
                // TODO: implement
                break;
            default:
                break;
        }

        rtp_encode_write(rtp_ctx, i);
        avf_release_frame(&frame);
    }

    rtp_close(rtp_ctx);
    return 0;
}

