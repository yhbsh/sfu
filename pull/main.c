#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <raylib.h>

#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/threadmessage.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

#define W 1280
#define H 720

static inline char *json_get_string(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    const char *end = strchr(p, '"');
    if (!end) return NULL;
    size_t len = end - p;
    char *res = (char *) malloc(len + 1);
    if (!res) return NULL;
    memcpy(res, p, len);
    res[len] = 0;
    return res;
}

static inline int64_t json_get_int(const char *json, const char *key, int64_t def) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return def;
    p += strlen(pattern);
    const char *end = p;
    while (*end && (*end >= '0' && *end <= '9')) end++;
    if (end == p) return def;
    int64_t val = 0;
    for (const char *c = p; c < end; c++) val = val * 10 + (*c - '0');
    return val;
}

ssize_t read_exact(int fd, void *buf, size_t len) {
    size_t total_read = 0;
    uint8_t *ptr = (uint8_t *)buf;
    while (total_read < len) {
        ssize_t n = read(fd, ptr + total_read, len - total_read);
        if (n <= 0) return -1;
        total_read += n;
    }
    return total_read;
}

typedef struct {
    int fd;
    AVCodecContext *decoder;
    AVFrame *frame;
    AVFrame *rgb_frame;
    SwsContext *sws_ctx;
    bool keep_alive_running;
    bool reader_running;
    AVThreadMessageQueue *queue;

    const char *stream_id;
    const char *ip;
    int16_t port;
} MediaPull;

void *keep_alive_thread(void *arg) {
    MediaPull *ctx = (MediaPull *)arg;
    while (ctx->keep_alive_running) {
        uint8_t keep_byte = 0;
        ssize_t n = write(ctx->fd, &keep_byte, 1);
        if (n <= 0) break;
        usleep(5 * 1000000);
    }
    return NULL;
}

void *reader_thread(void *arg) {
    MediaPull *ctx = (MediaPull *)arg;
    ctx->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ctx->port);
    inet_pton(AF_INET, ctx->ip, &server_addr.sin_addr);
    if (connect(ctx->fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "ERROR: cannot connect to server. %s\n", strerror(errno));
        close(ctx->fd);
        exit(0);
    }

    char header_json[256];
    snprintf(header_json, sizeof(header_json), "{\"mode\":\"pull\",\"stream_id\":\"%s\"}", ctx->stream_id);
    uint32_t length = strlen(header_json);
    uint8_t header[4];
    header[0] = (length >> 0 * 8) & 0xFF;
    header[1] = (length >> 1 * 8) & 0xFF;
    header[2] = (length >> 2 * 8) & 0xFF;
    header[3] = (length >> 3 * 8) & 0xFF;
    write(ctx->fd, header, 4);
    write(ctx->fd, header_json, length);

    uint32_t json_length = 0;
    read_exact(ctx->fd, &json_length, sizeof(json_length));

    char *json_buf = (char *) calloc(json_length + 1, sizeof(char));
    read_exact(ctx->fd, json_buf, json_length);

    printf("json_buf = %s", json_buf);

    uint32_t video_codec_id = (uint32_t) json_get_int(json_buf, "video_codec_id", 0);
    uint32_t audio_codec_id = (uint32_t) json_get_int(json_buf, "audio_codec_id", 0);
    uint32_t fps = (uint32_t) json_get_int(json_buf, "fps", 30);
    uint32_t width = (uint32_t) json_get_int(json_buf, "width", 0);
    uint32_t height = (uint32_t) json_get_int(json_buf, "height", 0);
    char *video_extradata_b64 = json_get_string(json_buf, "video_extradata");
    char *audio_extradata_b64 = json_get_string(json_buf, "audio_extradata");

    const AVCodec *codec = avcodec_find_decoder(video_codec_id);
    if (!codec) {
        fprintf(stderr, "ERROR: cannot find decoder %s\n", avcodec_get_name(video_codec_id));
        exit(0);
    }

    ctx->decoder = avcodec_alloc_context3(codec);
    ctx->decoder->time_base = (AVRational){1, fps};
    ctx->decoder->framerate = (AVRational){fps, 1};
    int ret = avcodec_open2(ctx->decoder, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR: cannot open decoder. %s\n", av_err2str(ret));
        exit(0);
    }

    ctx->frame = av_frame_alloc();
    ctx->rgb_frame = av_frame_alloc();
    ctx->sws_ctx = NULL;

    while (ctx->reader_running) {
        uint8_t header_buf[28];
        read_exact(ctx->fd, header_buf, 28);

        int64_t pts = 0, dts = 0;
        int32_t stream_index = 0, flags = 0, size = 0;

        for (int i = 0; i < 8; i++) pts |= ((int64_t)header_buf[i]) << (i * 8);
        for (int i = 0; i < 8; i++) dts |= ((int64_t)header_buf[8 + i]) << (i * 8);
        for (int i = 0; i < 4; i++) stream_index |= ((int32_t)header_buf[16 + i]) << (i * 8);
        for (int i = 0; i < 4; i++) flags |= ((int32_t)header_buf[20 + i]) << (i * 8);
        for (int i = 0; i < 4; i++) size |= ((int32_t)header_buf[24 + i]) << (i * 8);

        if (size <= 0 || size > 100000000) {
            fprintf(stderr, "Invalid packet size: %d, stopping reader\n", size);
            break;
        }

        uint8_t *data = av_malloc(size);
        read_exact(ctx->fd, data, size);

        AVPacket *pkt = av_packet_alloc();
        pkt->data = data;
        pkt->size = size;
        pkt->pts = pts;
        pkt->dts = dts;
        pkt->stream_index = stream_index;
        pkt->flags = flags;

        av_thread_message_queue_send(ctx->queue, &pkt, 0);
    }

    ctx->reader_running = false;
    return NULL;
}

int media_pull_init(MediaPull *ctx) {
    ctx->keep_alive_running = true;
    ctx->reader_running = true;
    av_thread_message_queue_alloc(&ctx->queue, 1024, sizeof(AVPacket *));

    // av_log_set_level(AV_LOG_TRACE);

    pthread_t tid_reader;
    pthread_create(&tid_reader, NULL, reader_thread, ctx);
    pthread_detach(tid_reader);

    pthread_t tid_keep;
    pthread_create(&tid_keep, NULL, keep_alive_thread, ctx);
    pthread_detach(tid_keep);

    return 0;
}

int media_pull_decode_render(MediaPull *ctx, Texture texture, int width, int height) {
    static int64_t start_time_ms = 0;

    AVPacket *pkt = NULL;
    if (av_thread_message_queue_recv(ctx->queue, &pkt, AV_THREAD_MESSAGE_NONBLOCK) < 0 || pkt->stream_index != 0) {
        BeginDrawing();
        DrawTexture(texture, 0, 0, WHITE);
        EndDrawing();
        av_packet_free(&pkt);
        return 0;
    }

    if (avcodec_send_packet(ctx->decoder, pkt) == 0) {
        while (avcodec_receive_frame(ctx->decoder, ctx->frame) == 0) {
            if (!ctx->sws_ctx) {
                ctx->sws_ctx = sws_getContext(ctx->frame->width, ctx->frame->height, ctx->frame->format, width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
            }

            int64_t pkt_pts = pkt->pts;
            int64_t pkt_dts = pkt->dts;
            int64_t frame_pts = ctx->frame->pts;
            int64_t frame_pts_ms = av_rescale_q(frame_pts, ctx->decoder->time_base, (AVRational){1, 1000});

            static bool go_fast = false;
            static bool go_super_fast = false;

            const int upper_threshold = 2;       // start going fast if queue >= 3
            const int lower_threshold = 1;       // go slow again if queue <= 1
            const int super_threshold = 4;       // start going super fast if queue >= 10
            const int super_lower_threshold = 3; // stop super fast if queue <= 8

            int queue_len = av_thread_message_queue_nb_elems(ctx->queue);

            if (!go_super_fast && queue_len >= super_threshold) {
                go_super_fast = true;
            } else if (go_super_fast && queue_len <= super_lower_threshold) {
                go_super_fast = false;
            }

            if (!go_fast && queue_len >= upper_threshold && !go_super_fast) {
                go_fast = true;
            } else if (go_fast && queue_len <= lower_threshold) {
                go_fast = false;
            }

            int64_t sleep_us = 1e6 * ctx->decoder->framerate.den / ctx->decoder->framerate.num;

            if (go_super_fast) {
                sleep_us *= 0.35; // 4x speed
            } else if (go_fast) {
                sleep_us *= 0.7;  // 2x speed
            }

            av_usleep(sleep_us);

            printf("Packet: pts=%" PRId64 " dts=%" PRId64 " size=%d | Frame: pts=%" PRId64 " ms=%" PRId64 " width=%d height=%d format=%s key_frame=%d | Sleeping: %" PRId64 " ms Queue Size: %d\n", 
                    pkt_pts, 
                    pkt_dts, 
                    pkt->size, 
                    frame_pts, 
                    frame_pts_ms, 
                    ctx->frame->width, ctx->frame->height, av_get_pix_fmt_name(ctx->frame->format), 
                    ctx->frame->flags & AV_FRAME_FLAG_KEY, 
                    sleep_us / 1000, av_thread_message_queue_nb_elems(ctx->queue));

            sws_scale_frame(ctx->sws_ctx, ctx->rgb_frame, ctx->frame);
            UpdateTexture(texture, ctx->rgb_frame->data[0]);
            BeginDrawing();
            DrawTexture(texture, 0, 0, WHITE);
            EndDrawing();
        }
    }

    av_packet_free(&pkt);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 3) {
        printf("USAGE: %s [domain] [stream_id]\n", argv[0]);
        return 0;
    }

    const char *domain = (argc >= 2) ? argv[1] : "localhost";
    const char *stream_id = (argc >= 3) ? argv[2] : "camera_stream";
    struct hostent *he = gethostbyname(domain);
    if (!he) {
        printf("Failed to resolve domain: %s\n", domain);
        return -1;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, he->h_addr_list[0], ip, sizeof(ip));

    MediaPull ctx = {
        .stream_id = stream_id,
        .ip = ip,
        .port = 1935,
    };

    if (media_pull_init(&ctx) < 0) return -1;

    SetTraceLogLevel(LOG_NONE);
    InitWindow(W, H, "WINDOW");
    SetTargetFPS(60);

    Image img = {.data = NULL, .width = W, .height = H, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Texture2D texture = LoadTextureFromImage(img);

    while (!WindowShouldClose()) {
        media_pull_decode_render(&ctx, texture, W, H);
    }

    return 0;
}
