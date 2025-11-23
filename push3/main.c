#include "rtp.h"
#include "common.h"

int main(void) {
    RtpContext *rtp_ctx = NULL;
    RtpCodecID video_codec_id = CODEC_HEVC;

    if (rtp_open(&rtp_ctx, ADDRESS, PORT, STREAM_ID, W, H, FPS, GOP, BITRATE, video_codec_id, CODEC_AAC, 44100, 2) < 0) {
        return -1;
    }

    for (int i = 0; i < NUM_FRAMES; ++i) {
        switch (video_codec_id) {
            case CODEC_H264:
                PROFILE_CALL(fill_pattern, fill_pattern(W, H, 
                            rtp_ctx->codec.h264.in.img.plane[0], rtp_ctx->codec.h264.in.img.i_stride[0], 
                            rtp_ctx->codec.h264.in.img.plane[1], rtp_ctx->codec.h264.in.img.i_stride[1], 
                            rtp_ctx->codec.h264.in.img.plane[2], rtp_ctx->codec.h264.in.img.i_stride[2], i));
                break;
            case CODEC_HEVC:
                PROFILE_CALL(fill_pattern, fill_pattern(W, H, 
                            rtp_ctx->codec.hevc.in->planes[0], rtp_ctx->codec.hevc.in->stride[0], 
                            rtp_ctx->codec.hevc.in->planes[1], rtp_ctx->codec.hevc.in->stride[1], 
                            rtp_ctx->codec.hevc.in->planes[2], rtp_ctx->codec.hevc.in->stride[2], i));
                break;
            case CODEC_AV1:
                // TODO: implement
                break;
            default:
                break;
        }

        rtp_encode_write(rtp_ctx, i);
    }

    rtp_close(rtp_ctx);

    return 0;
}
