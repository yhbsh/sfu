#ifndef PIXEL_H
#define PIXEL_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- API DECLARATIONS --- */

void fill_pattern_radial(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts);
void fill_pattern_checker(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts);
void fill_pattern_vertical(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts);
void fill_pattern_horizontal(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts);
void fill_pattern_gradient(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts);
void fill_pattern_game_of_life(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame);
void fill_pattern_neon(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame);
void fill_pattern_water(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame);
void fill_pattern_fractal_noise(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame);
void fill_pattern_shader(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts);
void fill_pattern_shader2(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts);

#ifdef __cplusplus
}
#endif

#endif /* PIXEL_H */

#ifdef PIXEL_IMPL

#include <math.h>
#include <string.h>
#include <stdint.h>

static inline uint8_t px_clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static inline void px_fill_chroma_gray(int cw, int ch, uint8_t *U, int sU, uint8_t *V, int sV) {
    if (!U || !V) return;
    for (int j = 0; j < ch; ++j) {
        uint8_t *rowU = U + j * sU;
        uint8_t *rowV = V + j * sV;
        for (int i = 0; i < cw; ++i) {
            rowU[i] = 128;
            rowV[i] = 128;
        }
    }
}

void fill_pattern_radial(int width, int height, uint8_t *Y, const int sY, uint8_t *U, const int sU, uint8_t *V, const int sV, int pts) {
    const int cw = width / 2;
    const int ch = height / 2;

    const float t = (float)pts * 0.05f;
    const float cx = (width  - 1) * 0.5f;
    const float cy = (height - 1) * 0.5f;

    for (int y = 0; y < height; ++y) {
        uint8_t *rowY = Y + y * sY;
        for (int x = 0; x < width; ++x) {
            float fx = (float)x - cx;
            float fy = (float)y - cy;

            float r = sqrtf(fx * fx + fy * fy);
            float a = atan2f(fy, fx);

            float v = 0.5f
                    + 0.25f * sinf(0.035f * r - 3.0f * a + t)
                    + 0.25f * sinf(0.02f  * r + 5.0f * a - 1.7f * t);

            rowY[x] = px_clamp_u8((int)(v * 255.0f + 0.5f));
        }
    }

    if (U && V) {
        const float ccx = (cw - 1) * 0.5f;
        const float ccy = (ch - 1) * 0.5f;

        for (int j = 0; j < ch; ++j) {
            uint8_t *rowU = U + j * sU;
            uint8_t *rowV = V + j * sV;

            for (int i = 0; i < cw; ++i) {
                float fx = (float)i - ccx;
                float fy = (float)j - ccy;

                float r = sqrtf(fx * fx + fy * fy);
                float a = atan2f(fy, fx);

                float u_wave = 0.5f + 0.5f * sinf(2.0f * a + 0.0025f * r + 0.7f * t);
                float v_wave = 0.5f + 0.5f * cosf(3.0f * a - 0.0035f * r - 0.4f * t);

                int Uval = 128 + (int)((u_wave - 0.5f) * 180.0f);
                int Vval = 128 + (int)((v_wave - 0.5f) * 180.0f);

                rowU[i] = px_clamp_u8(Uval);
                rowV[i] = px_clamp_u8(Vval);
            }
        }
    }
}

void fill_pattern_checker(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts) {
    const int cw = width / 2;
    const int ch = height / 2;

    int block = 32 + ((pts >> 2) & 31);  /* block size changes over time */

    for (int y = 0; y < height; ++y) {
        uint8_t *rowY = Y + y * sY;
        for (int x = 0; x < width; ++x) {
            int check = ((x / block) ^ (y / block)) & 1;
            rowY[x] = check ? 240 : 30;
        }
    }

    px_fill_chroma_gray(cw, ch, U, sU, V, sV);
}

void fill_pattern_vertical(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts) {
    const int cw = width / 2;
    const int ch = height / 2;

    int bands = 8;
    for (int y = 0; y < height; ++y) {
        uint8_t *rowY = Y + y * sY;
        for (int x = 0; x < width; ++x) {
            int band = (x * bands) / width;
            rowY[x] = (uint8_t)(band * (255 / (bands - 1)));
        }
    }

    px_fill_chroma_gray(cw, ch, U, sU, V, sV);
}

void fill_pattern_horizontal(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts) {
    const int cw = width / 2;
    const int ch = height / 2;

    int bands = 8;

    for (int y = 0; y < height; ++y) {
        uint8_t *rowY = Y + y * sY;
        int band = (y * bands) / height;
        uint8_t v = (uint8_t)(band * (255 / (bands - 1)));

        for (int x = 0; x < width; ++x)
            rowY[x] = v;
    }

    px_fill_chroma_gray(cw, ch, U, sU, V, sV);
}

void fill_pattern_gradient(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts) {
    const int cw = width / 2;
    const int ch = height / 2;

    for (int y = 0; y < height; ++y) {
        uint8_t *rowY = Y + y * sY;
        for (int x = 0; x < width; ++x) {
            float fx = (float)x / (width  - 1);
            float fy = (float)y / (height - 1);
            float v = 0.5f * (fx + fy);
            rowY[x] = px_clamp_u8((int)(v * 255.0f + 0.5f));
        }
    }

    px_fill_chroma_gray(cw, ch, U, sU, V, sV);
}

static inline double neon_modulate_intensity(double radius, double angle, double phase, double freq) {
    return 128 + 127 * sin(radius * freq + angle + phase);
}

static inline double neon_complex_wave(int x, int y, double phase, double freq) {
    double r = sqrt((double)(x * x + y * y));
    double a = atan2((double)y, (double)x);
    return neon_modulate_intensity(r, a, phase, freq);
}

void fill_pattern_neon(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame) {
    const double frequency_base = 0.08;
    const double color_shift    = frame * 0.04;

    int cw = width  / 2;
    int ch = height / 2;

    /* fill Y neutral */
    for (int y = 0; y < height; y++) {
        memset(Y + y * sY, 128, width);
    }

    for (int y = 0; y < ch; y++) {
        uint8_t *u_plane = U + y * sU;
        uint8_t *v_plane = V + y * sV;

        for (int x = 0; x < cw; x++) {
            int cx = x - cw / 2;
            int cy = y - ch / 2;

            double u_wave = neon_complex_wave(cx,  cy,  color_shift,  frequency_base);
            double v_wave = neon_complex_wave(-cx, -cy, -color_shift, frequency_base);

            u_plane[x] = (uint8_t)(u_wave);
            v_plane[x] = (uint8_t)(128 + (int)(127 * cos(v_wave)));
        }
    }
}

void fill_pattern_game_of_life(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame) {
    const int w = width;
    const int h = height;
    static uint8_t grid[1080][1920];
    static uint8_t age[1080][1920];      // age of each living cell
    static uint8_t next[1080][1920];
    static int initialized = 0;

    if (!initialized) {
        memset(grid, 0, sizeof(grid));
        memset(age,  0, sizeof(age));
        // Random seed (or you can load a cool pattern like a Gosper gun, R-pentomino, etc.)
        for (int y = 1; y < h-1; y++) {
            for (int x = 1; x < w-1; x++) {
                if (rand() % 7 == 0) {          // ~14% density looks good
                    grid[y][x] = 1;
                    age[y][x] = rand() & 127;
                }
            }
        }
        initialized = 1;
    }

    // === 1. Evolve full-resolution Game of Life ===
    for (int y = 1; y < h-1; y++) {
        for (int x = 1; x < w-1; x++) {
            int alive = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (dx || dy)
                        alive += grid[y+dy][x+dx];

            if (grid[y][x]) {
                next[y][x] = (alive == 2 || alive == 3);
                age[y][x] = next[y][x] ? age[y][x] + 1 : 0;
            } else {
                next[y][x] = (alive == 3);
                age[y][x] = next[y][x] ? 1 : 0;
            }
        }
    }
    memcpy(grid, next, sizeof(grid));

    // === 2. Color cycling parameters ===
    float time = frame * 0.008f;  // adjust speed
    float hue_speed = 0.23f + 0.1f * sinf(frame * 0.0003f);  // slowly drift

    // === 3. Render full-res Y + 2×2 subsampled U/V ===
    for (int y = 0; y < h; y++) {
        uint8_t *py = Y + y * sY;

        for (int x = 0; x < w; x++) {
            int alive = grid[y][x];
            int a = age[y][x];

            // Luma: bright when alive, slight glow from neighbors
            int neighbors = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (grid[y+dy][x+dx]) neighbors++;
            int luma = alive ? 200 + 55 : 20 + neighbors * 10;
            if (luma > 255) luma = 255;
            py[x] = luma;

            // Chroma only on even rows/columns (YUV420)
            if ((y & 1) == 0 && (x & 1) == 0) {
                float hue, sat, val;

                // Option A: age → hue (classic rainbow fire)
                hue = 0.16f + hue_speed * time + a * 0.02f;

                // Option B: density → hue (psychedelic waves)
                // hue = 0.16f + hue_speed * time + neighbors * 0.07f;

                // Option C: position-based (stable colorful soup)
                // hue = (x * 0.001f + y * 0.0013f + time * 0.3f);

                sat = alive ? 0.9f + 0.1f * sinf(a * 0.2f) : 0.3f;
                val = alive ? 1.0f : 0.1f;

                hue = fmodf(hue, 1.0f);
                if (hue < 0) hue += 1.0f;

                // HSL → RGB → YUV conversion (compact version)
                float c = val * sat;
                float h2 = hue * 6.0f;
                float x2 = c * (1.0f - fabsf(fmodf(h2, 2.0f) - 1.0f));
                float r, g, b;
                if      (h2 < 1) { r = c; g = x2; b = 0; }
                else if (h2 < 2) { r = x2; g = c; b = 0; }
                else if (h2 < 3) { r = 0; g = c; b = x2; }
                else if (h2 < 4) { r = 0; g = x2; b = c; }
                else if (h2 < 5) { r = x2; g = 0; b = c; }
                else             { r = c; g = 0; b = x2; }

                float m = val - c;
                r += m; g += m; b += m;

                // RGB → YUV (BT.601 limited range)
                int Yval = (int)( 16 + 219 * (0.299f*r + 0.587f*g + 0.114f*b));
                int Uval = (int)(128 + 224 * (-0.147f*r - 0.289f*g + 0.436f*b));
                int Vval = (int)(128 + 224 * (0.439f*r - 0.368f*g - 0.071f*b));

                U[(y>>1) * sU + (x>>1)] = Uval;
                V[(y>>1) * sV + (x>>1)] = Vval;
            }
        }
    }
}

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define MAX_W 1920
#define MAX_H 1080

typedef struct {
    uint8_t state[MAX_H][MAX_W];
    uint8_t age[MAX_H][MAX_W];
    uint8_t next[MAX_H][MAX_W];
    int w, h;
} LifeLayer;

static LifeLayer L1, L2, L3;

static void init_layer(LifeLayer *L, int w, int h) {
    L->w = w;
    L->h = h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int r = rand() & 15;
            L->state[y][x] = (r == 0);
            L->age[y][x] = L->state[y][x] ? 40 : 0;
        }
}

static void update_layer(LifeLayer *L) {
    int w = L->w, h = L->h;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int live = 0;
            for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int ny = y + dy, nx = x + dx;
                if (ny >= 0 && ny < h && nx >= 0 && nx < w)
                    live += L->state[ny][nx];
            }

            L->next[y][x] = L->state[y][x] ?
                (live == 2 || live == 3) :
                (live == 3);
        }
    }

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t a = L->next[y][x];
            L->state[y][x] = a;

            if (a) {
                if (L->age[y][x] < 250) L->age[y][x] += 6;
            } else {
                if (L->age[y][x] > 0) L->age[y][x] -= 2;
            }
        }
}

static uint8_t clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

void fill_pattern_game_of_life3(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame) {
    int cw = width;
    int ch = height;

    if (frame == 0) {
        init_layer(&L1, cw/2, ch/2);
        init_layer(&L2, cw/3, ch/3);
        init_layer(&L3, cw/4, ch/4);
    }

    update_layer(&L1);
    update_layer(&L2);
    update_layer(&L3);

    for (int y = 0; y < height; y++) {
        uint8_t *row = Y + y * sY;

        for (int x = 0; x < width; x++) {
            int v = 0;

            int x1 = (x * L1.w) / width;
            int y1 = (y * L1.h) / height;
            v += L1.age[y1][x1];

            int x2 = (x * L2.w) / width;
            int y2 = (y * L2.h) / height;
            v += L2.age[y2][x2] / 2;

            int x3 = (x * L3.w) / width;
            int y3 = (y * L3.h) / height;
            v += L3.age[y3][x3] / 3;

            v = (v * 3) / 4;

            int gv = v;
            gv += (rand() & 7) - 3;

            int blur = 0;
            if (x > 0) blur += row[x - 1];
            if (y > 0) blur += (Y + (y - 1)*sY)[x];
            gv = (gv*3 + blur/2) / 4;

            row[x] = clamp8(gv);
        }
    }

    int shift = (frame / 3) & 255;

    for (int y = 0; y < ch / 2; y++) {
        uint8_t *u = U + y * sU;
        uint8_t *v = V + y * sV;

        for (int x = 0; x < cw / 2; x++) {
            u[x] = 128 + (L1.age[y % L1.h][x % L1.w] / 5) + (shift >> 2);
            v[x] = 128 + (L2.age[y % L2.h][x % L2.w] / 6) - (shift >> 3);
        }
    }
}

void fill_pattern_game_of_life2(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame) {
    int cw = width  / 2;
    int ch = height / 2;

    static uint8_t state[1080][1920];     // live/dead (0–1)
    static uint8_t age[1080][1920];       // age counter for shading
    static uint8_t next_state[1080][1920];

    if (frame == 0) {
        // Clustered random initialization for nicer patterns
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int r = (rand() & 7); // 0–7
                state[y][x] = (r == 0); // sparsely populate
                age[y][x] = state[y][x] ? 32 : 0;
            }
        }
    } else {
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int live = 0;
                for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int ny = y + dy, nx = x + dx;
                    if (ny >= 0 && ny < ch && nx >= 0 && nx < cw) {
                        live += state[ny][nx];
                    }
                }

                if (state[y][x]) {
                    next_state[y][x] = (live == 2 || live == 3);
                } else {
                    next_state[y][x] = (live == 3);
                }
            }
        }

        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                uint8_t alive = next_state[y][x];
                state[y][x] = alive;

                if (alive) {
                    if (age[y][x] < 250)
                        age[y][x] += 5; // aging for shading
                } else {
                    if (age[y][x] > 0)
                        age[y][x] -= 3; // fade out slowly
                }
            }
        }
    }

    // Render pattern into full Y plane
    for (int y = 0; y < height; y++) {
        uint8_t *rowY = Y + y * sY;

        int sy = (y * ch) / height;
        for (int x = 0; x < width; x++) {
            int sx = (x * cw) / width;

            // Convert age (0–255) to interesting shading
            // Boost contrast slightly
            uint8_t val = age[sy][sx];
            uint8_t shaded =
                (val < 64)  ? (val * 2) :
                (val < 128) ? (64 + (val - 64) * 3 / 2) :
                              (128 + (val - 128) * 2);

            rowY[x] = shaded;
        }
    }

    // U plane: add a subtle blue tint in young cells
    for (int y = 0; y < ch; y++) {
        uint8_t *rowU = U + y * sU;
        for (int x = 0; x < cw; x++) {
            rowU[x] = 128 + (age[y][x] / 6); // faint chroma variation
        }
    }

    // V plane: faint red tint for older cells
    for (int y = 0; y < ch; y++) {
        uint8_t *rowV = V + y * sV;
        for (int x = 0; x < cw; x++) {
            rowV[x] = 128 + (age[y][x] / 10);
        }
    }
}

static inline double water_modulate(double radius, double angle, double phase, double freq) {
    return 128.0 + 127.0 * sin(radius * freq + angle + phase);
}

static inline double water_complex_wave(int x, int y, double phase, double freq) {
    double r = sqrt((double)(x * x + y * y));
    double a = atan2((double)y, (double)x);
    return water_modulate(r, a, phase, freq);
}

void fill_pattern_water(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame) {
    const double frequency_base = 0.05;
    const double phase_shift    = frame * 0.03;
    const double color_shift    = frame * 0.02;

    int cw = width  / 2;
    int ch = height / 2;

    /* Optional: fill Y with flat neutral */
    for (int y = 0; y < height; y++) {
        memset(Y + y * sY, 128, width);
    }

    for (int y = 0; y < ch; y++) {
        uint8_t *u_plane = U + y * sU;
        uint8_t *v_plane = V + y * sV;

        for (int x = 0; x < cw; x++) {

            /* center coordinates */
            int cx = x - cw / 2;
            int cy = y - ch / 2;

            /* waveforms */
            double u_wave = water_complex_wave(cx,  cy,  color_shift,  frequency_base);
            double v_wave = water_complex_wave(-cx, -cy, -color_shift, frequency_base);

            /* final chroma */
            u_plane[x] = (uint8_t)(128 + (int)(127 * cos(u_wave)));
            v_plane[x] = (uint8_t)(128 + (int)(127 * sin(v_wave)));
        }
    }
}

static inline double fractal_modulate_intensity(double radius, double angle, double phase, double freq, int max_radius_effect) {
    return 128.0 + 127.0 * sin(radius * freq + max_radius_effect * angle + phase);
}

static inline double fractal_complex_wave(int x, int y, double phase, double freq, int max_radius_effect) {
    double r = sqrt((double)(x * x + y * y));
    double a = atan2((double)y, (double)x);
    return fractal_modulate_intensity(r, a, phase, freq, max_radius_effect);
}

void fill_pattern_fractal_noise(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int frame) {
    const int max_radius_effect = 3;
    const double frequency_base = 0.04;
    const double phase_shift    = frame * 0.02;
    const double color_shift    = frame * 0.03;

    int cw = width  / 2;
    int ch = height / 2;

    /* ----- Y plane fractal pattern ----- */
    for (int y = 0; y < height; y++) {
        uint8_t *rowY = Y + y * sY;

        for (int x = 0; x < width; x++) {
            int cx = x - width  / 2;
            int cy = y - height / 2;

            double val = fractal_complex_wave(cx, cy,
                                              phase_shift,
                                              frequency_base,
                                              max_radius_effect);

            rowY[x] = (uint8_t)val;
        }
    }

    /* ----- U + V fractal chroma waves ----- */
    for (int y = 0; y < ch; y++) {
        uint8_t *u_plane = U + y * sU;
        uint8_t *v_plane = V + y * sV;

        for (int x = 0; x < cw; x++) {
            int cx = x - cw / 2;
            int cy = y - ch / 2;

            double u_wave = fractal_complex_wave(cx,  cy,
                                                 color_shift,
                                                 frequency_base,
                                                 max_radius_effect);

            double v_wave = fractal_complex_wave(-cx, -cy,
                                                 -color_shift,
                                                 frequency_base,
                                                 max_radius_effect);

            u_plane[x] = (uint8_t)(128 + (int)(127 * cos(u_wave)));
            v_plane[x] = (uint8_t)(128 + (int)(127 * sin(v_wave)));
        }
    }
}

static float clamp01(float x) {
    return x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
}

static void rgb_to_yuv(float r, float g, float b, uint8_t *y, uint8_t *u, uint8_t *v) {
    float Yf = 0.299f*r + 0.587f*g + 0.114f*b;
    float Uf = -0.169f*r - 0.331f*g + 0.500f*b + 0.5f;
    float Vf =  0.500f*r - 0.419f*g - 0.081f*b + 0.5f;
    *y = (uint8_t)(clamp01(Yf) * 255.f);
    *u = (uint8_t)(clamp01(Uf) * 255.f);
    *v = (uint8_t)(clamp01(Vf) * 255.f);
}

void fill_pattern_shader(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts) {
    float t = pts*0.010;

    for (int y = 0; y < height; y++) {
        uint8_t *rowY = Y + y * sY;
        for (int x = 0; x < width; x++) {
            float u = (x - width * 0.5f) / height;
            float v = (y - height * 0.5f) / height;

            float r = sqrtf(u*u + v*v);
            float a = atan2f(v, u);

            float s = sinf(8.f*r - t*4.f)
                    + sinf(a*6.f + t*3.f)
                    + cosf(r*12.f + a*3.f - t*2.f);

            float brightness = s * 0.25f + 0.5f;

            float R = brightness * (0.6f + 0.4f * sinf(t + r*5.f));
            float G = brightness * (0.6f + 0.4f * sinf(t*1.3f + a*4.f));
            float B = brightness * (0.6f + 0.4f * sinf(t*0.7f + r*2.f - a*3.f));

            uint8_t yv, uv, vv;
            rgb_to_yuv(R, G, B, &yv, &uv, &vv);
            rowY[x] = yv;
        }
    }

    for (int y = 0; y < height/2; y++) {
        uint8_t *rowU = U + y * sU;
        uint8_t *rowV = V + y * sV;

        for (int x = 0; x < width/2; x++) {
            int px = x*2;
            int py = y*2;

            float u = (px - width * 0.5f) / height;
            float v = (py - height * 0.5f) / height;

            float r = sqrtf(u*u + v*v);
            float a = atan2f(v, u);

            float s = sinf(8.f*r - t*4.f)
                    + sinf(a*6.f + t*3.f)
                    + cosf(r*12.f + a*3.f - t*2.f);

            float brightness = s * 0.25f + 0.5f;

            float R = brightness * (0.6f + 0.4f * sinf(t + r*5.f));
            float G = brightness * (0.6f + 0.4f * sinf(t*1.3f + a*4.f));
            float B = brightness * (0.6f + 0.4f * sinf(t*0.7f + r*2.f - a*3.f));

            uint8_t yv, uv, vv;
            rgb_to_yuv(R, G, B, &yv, &uv, &vv);

            rowU[x] = uv;
            rowV[x] = vv;
        }
    }
}

void fill_pattern_shader2(int width, int height, uint8_t *Y, int sY, uint8_t *U, int sU, uint8_t *V, int sV, int pts) {
    double t = pts * 0.03;
    for (int y = 0; y < height; y++) {
        uint8_t *rowY = Y + y * sY;
        uint8_t *rowU = U + (y/2) * sU;
        uint8_t *rowV = V + (y/2) * sV;
        for (int x = 0; x < width; x++) {
            double nx = (double)x / width;
            double ny = (double)y / height;
            double r = 0.5 + 0.5 * sin(10.0 * (nx + t) + 5.0 * sin(3.0 * (ny + t)));
            double g = 0.5 + 0.5 * sin(12.0 * (ny + t) + 4.0 * sin(2.0 * (nx - t)));
            double b = 0.5 + 0.5 * sin(15.0 * (nx + ny + t) + 7.0 * sin(1.5 * (nx - ny - t)));

            uint8_t R = (uint8_t)(r * 255.0);
            uint8_t G = (uint8_t)(g * 255.0);
            uint8_t B = (uint8_t)(b * 255.0);

            uint8_t yv  = (uint8_t)( 0.299*R + 0.587*G + 0.114*B);
            uint8_t uv  = (uint8_t)(-0.169*R - 0.331*G + 0.500*B + 128);
            uint8_t vv  = (uint8_t)( 0.500*R - 0.419*G - 0.081*B + 128);

            rowY[x] = yv;
            if ((x & 1) == 0) {
                rowU[x/2] = uv;
                rowV[x/2] = vv;
            }
        }
    }
}

#endif /* PIXEL_IMPL */
