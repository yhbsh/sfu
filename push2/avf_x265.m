#define AVF_IMPL
#include <avf.h>

#define RTP_IMPL
#include <rtp.h>

#include <x265.h>

#include "common.h"

int main(void) {
    x265_param *param = x265_param_alloc();
    x265_param_default_preset(param, PRESET, TUNE);
    param->sourceWidth  = W;
    param->sourceHeight = H;
    param->fpsNum       = FPS;
    param->fpsDenom     = 1;
    param->keyframeMax  = KEYFRAME_INTERVAL;
    param->bRepeatHeaders = 1;
    param->rc.rateControlMode = X265_RC_ABR;
    param->rc.bitrate         = BITRATE;
    param->internalCsp = X265_CSP_I420;
    x265_param_apply_profile(param, HEVC_PROFILE);

    x265_encoder *encoder = x265_encoder_open(param);
    if (!encoder) return 1;

    int fd = rtp_open(ADDRESS, PORT, STREAM_ID, 173, -1, FPS, W, H, -1, -1);
    if (fd < 0) return 1;

    AVFContext *ctx = NULL;
    if (avf_camera_open(&ctx, FPS, W, H) < 0) return -1;

    for (int i = 0; i < NUM_FRAMES; ++i) {
        CVFrame frame = {0};
        avf_read_frame(ctx, &frame);

        x265_picture *in  = x265_picture_alloc();
        x265_picture_init(param, in);

        in->planes[0] = frame.data[0];
        in->planes[1] = frame.data[1];
        in->planes[2] = frame.data[2];

        in->stride[0] = frame.stride[0];
        in->stride[1] = frame.stride[1];
        in->stride[2] = frame.stride[2];

        x265_nal *nals = NULL;
        uint32_t nal_count = 0;

        PROFILE_START(encode_frame);
        int ret = x265_encoder_encode(encoder, &nals, &nal_count, in, NULL);
        double encode_time = PROFILE_END(encode_frame);

        int total = 0;
        for (int n = 0; n < nal_count; n++) total += nals[n].sizeBytes;
        printf("  encode=%lf, size=%fkb\n", encode_time, (total*8.0f)/1000);
        if (ret < 0 || nal_count == 0) continue;

        unsigned char *payloads[nal_count];
        int sizes[nal_count];
        int is_key = 0;

        for (uint32_t n = 0; n < nal_count; n++) {
            payloads[n] = nals[n].payload;
            sizes[n] = nals[n].sizeBytes;

            is_key = (nals[n].type == NAL_UNIT_CODED_SLICE_IDR_W_RADL) || (nals[n].type == NAL_UNIT_CODED_SLICE_IDR_N_LP) || nals[n].type == NAL_UNIT_CODED_SLICE_CRA;

            printf("    NAL %03d | size=%04d | type=%d | key=%d | ", n, sizes[n], nals[n].type, is_key);
            for (int idx = 0; idx < 30 && idx < sizes[n]; idx++) {
                printf("%03x ", payloads[n][idx]);
            }
            printf("\n");
        }

        if (rtp_write_nals(fd, payloads, sizes, nal_count, i, i, is_key, 0) < 0) {
            x265_encoder_close(encoder);
            return 1;
        }
    }

    x265_encoder_close(encoder);
    x265_param_free(param);

    return 0;
}
