int rtp_open(const char *address, short port, const char *stream_id, int video_codec_id, int audio_codec_id, int fps, int width, int height, int sample_rate, int channels);
int rtp_write_nals(int fd, unsigned char **nal_units, int *nal_sizes, int nal_count, long long pts, long long dts, int is_keyframe, int stream_index);

#ifdef RTP_IMPL

#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int rtp_open(const char *address, short port, const char *stream_id, int video_codec_id, int audio_codec_id, int fps, int width, int height, int sample_rate, int channels) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
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

    if (connect(fd, (struct sockaddr *)&s, sizeof(s)) < 0) {
        fprintf(stderr, "connect failed\n");
        return -1;
    }

    char json[512];
    snprintf(json, sizeof(json),
             "{\"mode\":\"push\",\"stream_id\":\"%s\",\"video_codec_id\":%d,"
             "\"audio_codec_id\":%d,\"fps\":%d,\"width\":%d,\"height\":%d,"
             "\"sample_rate\":%d,\"channels\":%d}",
             stream_id, video_codec_id, audio_codec_id, fps,
             width, height, sample_rate, channels);

    uint32_t len = strlen(json);
    char h[4] = {0};
    h[0] = len >> 0;
    h[1] = len >> 8;
    h[2] = len >> 16;
    h[3] = len >> 24;

    if (write(fd, h, 4) < 0) {
        fprintf(stderr, "write hdr failed\n");
        return -1;
    }
    if (write(fd, json, len) < 0) {
        fprintf(stderr, "write json failed\n");
        return -1;
    }

    return fd;
}

int rtp_write_nals(int fd, unsigned char **units, int *sizes, int count, long long pts, long long dts, int is_key, int si) {
    int total = 0;
    for (int i = 0; i < count; i++) total += sizes[i];

    char h[28];
    memcpy(h + 0,  &pts, 8);
    memcpy(h + 8,  &dts, 8);
    memcpy(h + 16, &si, 4);
    memcpy(h + 20, &is_key, 4);
    memcpy(h + 24, &total, 4);

    if (write(fd, h, 28) < 0) {
        fprintf(stderr, "header write failed\n");
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (write(fd, units[i], sizes[i]) < 0) {
            fprintf(stderr, "payload write failed\n");
            return -1;
        }
    }

    return total;
}

#endif // RTP_IMPL
