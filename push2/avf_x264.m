#define AVF_IMPL
#include <avf.h>

#define RTP_IMPL
#include <rtp.h>

#include <x264.h>

#include "common.h"

int main(void) {
    x264_param_t param;
    x264_param_default_preset(&param, PRESET, TUNE);
    param.i_width = W;
    param.i_height = H;
    param.i_fps_num = FPS;
    param.i_fps_den = 1;
    param.i_keyint_max = KEYFRAME_INTERVAL;
    param.b_repeat_headers = 1;
    param.b_annexb = 1;
    param.rc.i_rc_method = X264_RC_ABR;
    param.rc.i_bitrate = BITRATE;
    param.i_log_level = X264_LOG_NONE;
    x264_param_apply_profile(&param, H264_PROFILE);

    x264_t *encoder = x264_encoder_open(&param);
    if (!encoder) return 1;

    int fd = rtp_open(ADDRESS, PORT, STREAM_ID, 27, -1, FPS, W, H, -1, -1);
    if (fd < 0) return 1;

    AVFContext *ctx = NULL;
    if (avf_camera_open(&ctx, FPS, W, H) < 0) {
        return -1;
    }

    for (int i = 0; i < NUM_FRAMES; i++) {
        CVFrame frame = {0};
        avf_read_frame(ctx, &frame);

        x264_picture_t in, out;
        x264_picture_init(&in);

        in.img.i_csp = X264_CSP_I420;
        in.img.i_plane = 3;
        in.i_pts = i;

        in.img.plane[0] = frame.data[0];
        in.img.plane[1] = frame.data[1];
        in.img.plane[2] = frame.data[2];

        in.img.i_stride[0] = frame.stride[0];
        in.img.i_stride[1] = frame.stride[1];
        in.img.i_stride[2] = frame.stride[2];

        x264_nal_t *nals = NULL;
        int nals_cnt = 0;

        PROFILE_START(encode_frame);
        int ret = x264_encoder_encode(encoder, &nals, &nals_cnt, &in, &out);
        double encode_time = PROFILE_END(encode_frame);

        printf("  encode=%lf, size=%fkb\n", encode_time, (ret*8.0f)/1000);
        if (ret < 0 || nals_cnt == 0) continue;

        unsigned char *payloads[nals_cnt];
        int sizes[nals_cnt];
        int is_key = 0;

        for (int n = 0; n < nals_cnt; n++) {
            payloads[n] = nals[n].p_payload;
            sizes[n] = nals[n].i_payload;

            if (nals[n].i_type == NAL_SLICE_IDR) {
                is_key = 1;
            }

            printf("    NAL %03d | size=%04d | type=%d | key=%d", n, nals[n].i_payload, nals[n].i_type, (nals[n].i_type == NAL_SLICE_IDR));
            printf(" | ");
            for (int idx = 0; idx < 30; ++idx) {
                printf("%03x ", nals[n].p_payload[idx]);
            }
            printf("\n");
        }


        if (rtp_write_nals(fd, payloads, sizes, nals_cnt, i, i, is_key, 0) < 0) {
            x264_picture_clean(&in);
            x264_encoder_close(encoder);
            return 1;
        }

        avf_release_frame(&frame);
        x264_picture_clean(&in);
    }

    x264_encoder_close(encoder);
    return 0;
}

