#include <time.h>

#define PROFILE_START(name) clock_t name##_start = clock()
#define PROFILE_END(name) (1000.0 * (double)(clock() - name##_start) / CLOCKS_PER_SEC)
#define PROFILE_PRINT(label, ms) printf("%s took %.3f ms\n", label, ms)

#define W 960
#define H 540
#define FPS 30
#define NUM_FRAMES 500
#define BITRATE 500
#define PRESET "ultrafast"
#define TUNE "zerolatency"
#define H264_PROFILE "high"
#define HEVC_PROFILE "main10"
#define KEYFRAME_INTERVAL FPS

#define ADDRESS "localhost"
#define PORT 1935
#define STREAM_ID "stream"

#include <pixel.h>
#define fill_pattern fill_pattern_game_of_life2
