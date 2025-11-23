#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aom/aom_encoder.h>
#include <aom/aomcx.h>

#include "profiler.h"
#include "common.h"

static inline void aom_write_ivf_header(unsigned char *header, const aom_codec_enc_cfg_t *cfg, int frame_count) {
    uint32_t fps_num = cfg->g_timebase.den;
    uint32_t fps_den = cfg->g_timebase.num;

    header[0] = 'D';
    header[1] = 'K';
    header[2] = 'I';
    header[3] = 'F';
    header[4] = 0;
    header[5] = 0;
    header[6] = 32;
    header[7] = 0;
    header[8] = 'A';
    header[9] = 'V';
    header[10] = '0';
    header[11] = '1';

    header[12] = cfg->g_w & 0xFF;
    header[13] = (cfg->g_w >> 8) & 0xFF;
    header[14] = cfg->g_h & 0xFF;
    header[15] = (cfg->g_h >> 8) & 0xFF;

    header[16] = (fps_num >> 0) & 0xFF;
    header[17] = (fps_num >> 8) & 0xFF;
    header[18] = (fps_num >> 16) & 0xFF;
    header[19] = (fps_num >> 24) & 0xFF;

    header[20] = (fps_den >> 0) & 0xFF;
    header[21] = (fps_den >> 8) & 0xFF;
    header[22] = (fps_den >> 16) & 0xFF;
    header[23] = (fps_den >> 24) & 0xFF;

    header[24] = (frame_count >> 0) & 0xFF;
    header[25] = (frame_count >> 8) & 0xFF;
    header[26] = (frame_count >> 16) & 0xFF;
    header[27] = (frame_count >> 24) & 0xFF;
}

static inline void write_ivf_frame(FILE *fp, const aom_codec_cx_pkt_t *pkt) {
    uint32_t frame_size = pkt->data.frame.sz;
    uint64_t pts = pkt->data.frame.pts;

    unsigned char header[12];
    header[0] = (frame_size >> 0) & 0xFF;
    header[1] = (frame_size >> 8) & 0xFF;
    header[2] = (frame_size >> 16) & 0xFF;
    header[3] = (frame_size >> 24) & 0xFF;

    header[4] = (pts >> 0) & 0xFF;
    header[5] = (pts >> 8) & 0xFF;
    header[6] = (pts >> 16) & 0xFF;
    header[7] = (pts >> 24) & 0xFF;
    header[8] = (pts >> 32) & 0xFF;
    header[9] = (pts >> 40) & 0xFF;
    header[10] = (pts >> 48) & 0xFF;
    header[11] = (pts >> 56) & 0xFF;

    fwrite(header, 1, 12, fp);
    fwrite(pkt->data.frame.buf, 1, frame_size, fp);
}

int main() {
    aom_codec_enc_cfg_t cfg;
    if (aom_codec_enc_config_default(aom_codec_av1_cx(), &cfg, 0)) {
        printf("config error\n");
        return 1;
    }

    cfg.g_w = W;
    cfg.g_h = H;
    cfg.g_timebase.num = 1;
    cfg.g_timebase.den = FPS;
    cfg.g_threads = 32;
    cfg.g_lag_in_frames = 0;
    cfg.kf_mode = AOM_KF_AUTO;
    cfg.kf_min_dist = GOP;
    cfg.kf_max_dist = GOP;
    cfg.rc_end_usage = AOM_VBR;
    cfg.rc_target_bitrate = BITRATE;

    aom_codec_ctx_t codec;
    if (aom_codec_enc_init(&codec, aom_codec_av1_cx(), &cfg, 0)) {
        fprintf(stderr, "Failed to initialize encoder\n");
        return 1;
    }

    aom_codec_control(&codec, AOME_SET_CPUUSED, 8);
    aom_codec_control(&codec, AV1E_SET_ROW_MT, 1);
    aom_codec_control(&codec, AV1E_SET_TILE_ROWS, 0);
    aom_codec_control(&codec, AV1E_SET_TILE_COLUMNS, 2);
    aom_codec_control(&codec, AV1E_SET_ENABLE_WARPED_MOTION, 0);
    aom_codec_control(&codec, AV1E_SET_ENABLE_GLOBAL_MOTION, 0);
    aom_codec_control(&codec, AV1E_SET_ENABLE_REF_FRAME_MVS, 0);
    aom_codec_control(&codec, AV1E_SET_ENABLE_OBMC, 0);
    aom_codec_control(&codec, AV1E_SET_ENABLE_INTRABC, 0);
    aom_codec_control(&codec, AV1E_SET_ENABLE_SMOOTH_INTRA, 0);
    aom_codec_control(&codec, AV1E_SET_ENABLE_CDEF, 0);
    aom_codec_control(&codec, AV1E_SET_ENABLE_RESTORATION, 0);
    aom_codec_control(&codec, AV1E_SET_DELTAQ_MODE, 0);

    aom_image_t img;
    if (!aom_img_alloc(&img, AOM_IMG_FMT_I420, W, H, 1)) {
        fprintf(stderr, "Failed to allocate image\n");
        return 1;
    }

    FILE *fp = fopen("file.ivf", "wb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    unsigned char header[32];
    aom_write_ivf_header(header, &cfg, 0);
    fwrite(header, 1, 32, fp);

    int frame_count = 0;

    for (int i = 0; i < NUM_FRAMES; i++) {
        PROFILE_CALL(fill_pattern, fill_pattern(W, H, img.planes[0], W, img.planes[1], W / 2, img.planes[2], W / 2, i));

        int ret = PROFILE_CALL(aom_codec_encode, aom_codec_encode(&codec, &img, i, 1, 0));
        if (ret < 0) continue;

        const aom_codec_cx_pkt_t *pkt;
        aom_codec_iter_t iter = NULL;
        while ((pkt = aom_codec_get_cx_data(&codec, &iter))) {
            if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
                write_ivf_frame(fp, pkt);
                printf("frame=%03d, size=%05.02f kb key=%d\n", frame_count, (pkt->data.frame.sz * 8.0f) / 1000, (pkt->data.frame.flags & AOM_FRAME_IS_KEY) != 0);
                frame_count++;
            }
        }
    }

    aom_codec_encode(&codec, NULL, NUM_FRAMES, 1, 0);
    const aom_codec_cx_pkt_t *pkt;
    aom_codec_iter_t iter = NULL;
    while ((pkt = aom_codec_get_cx_data(&codec, &iter))) {
        if (pkt->kind == AOM_CODEC_CX_FRAME_PKT) {
            write_ivf_frame(fp, pkt);
            frame_count++;
        }
    }

    fseek(fp, 24, SEEK_SET);
    uint32_t fc = frame_count;
    fwrite(&fc, 4, 1, fp);

    fclose(fp);

    printf("frame_count = %d\n", frame_count);
    return 0;
}
