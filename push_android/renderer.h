#pragma once

#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <GLES3/gl3.h>

typedef struct Renderer_Context {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLConfig config;
    ANativeWindow *window;
} Renderer_Context;

static inline int renderer_open(Renderer_Context **out, ANativeWindow *window) {
    if (!out || *out != NULL || !window) return 0;

    Renderer_Context *rc = (Renderer_Context *)calloc(1, sizeof(Renderer_Context));
    if (!rc) return 0;

    rc->window = window;
    rc->display = EGL_NO_DISPLAY;
    rc->surface = EGL_NO_SURFACE;
    rc->context = EGL_NO_CONTEXT;
    rc->config = 0;

    // 1. Get display
    rc->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (rc->display == EGL_NO_DISPLAY) {
        free(rc);
        return 0;
    }

    // 2. Initialize
    if (!eglInitialize(rc->display, NULL, NULL)) {
        free(rc);
        return 0;
    }

    // 3. Choose config
    EGLint attribs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};

    EGLint numConfigs = 0;
    if (!eglChooseConfig(rc->display, attribs, &rc->config, 1, &numConfigs) || numConfigs < 1) {
        eglTerminate(rc->display);
        free(rc);
        return 0;
    }

    // 4. Create surface
    rc->surface = eglCreateWindowSurface(rc->display, rc->config, rc->window, NULL);
    if (rc->surface == EGL_NO_SURFACE) {
        eglTerminate(rc->display);
        free(rc);
        return 0;
    }

    // 5. Create context
    EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

    rc->context = eglCreateContext(rc->display, rc->config, EGL_NO_CONTEXT, ctxAttribs);
    if (rc->context == EGL_NO_CONTEXT) {
        eglDestroySurface(rc->display, rc->surface);
        eglTerminate(rc->display);
        free(rc);
        return 0;
    }

    if (!eglMakeCurrent(rc->display, rc->surface, rc->surface, rc->context)) {
        eglDestroyContext(rc->display, rc->context);
        eglDestroySurface(rc->display, rc->surface);
        eglTerminate(rc->display);
        free(rc);
        return 0;
    }

    *out = rc;
    return 1;
}

static inline int renderer_swap_buffers(Renderer_Context *ctx) {
    if (!ctx || ctx->display == EGL_NO_DISPLAY || ctx->surface == EGL_NO_SURFACE) return 0;

    return eglSwapBuffers(ctx->display, ctx->surface) == EGL_TRUE;
}

static inline void renderer_close(Renderer_Context **out_ctx) {
    if (!out_ctx || !*out_ctx) return;
    Renderer_Context *ctx = *out_ctx;

    if (ctx->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (ctx->context != EGL_NO_CONTEXT) eglDestroyContext(ctx->display, ctx->context);

        if (ctx->surface != EGL_NO_SURFACE) eglDestroySurface(ctx->display, ctx->surface);

        eglTerminate(ctx->display);
    }

    free(*out_ctx);
    *out_ctx = NULL;
}

typedef struct {
    GLuint prog;
    GLuint vao, vbo;
    GLuint texY, texU, texV;
    GLint loc_texY, loc_texU, loc_texV;

    int lastOutW;
    int lastOutH;
} Yuv420p_Renderer_Context;

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static inline int yuv_renderer_open(Yuv420p_Renderer_Context **out, int w, int h) {
    if (!out || *out != NULL) return -1;

    Yuv420p_Renderer_Context *r = (Yuv420p_Renderer_Context *)calloc(1, sizeof(Yuv420p_Renderer_Context));
    if (!r) return -2;

    r->lastOutW = w;
    r->lastOutH = h;

    const char *vs_src = "#version 300 es\n"
                         "layout(location=0) in vec2 aPos;\n"
                         "layout(location=1) in vec2 aUV;\n"
                         "out vec2 vUV;\n"
                         "void main(){ vUV = aUV; gl_Position = vec4(aPos,0.0,1.0); }\n";

    const char *fs_src = "#version 300 es\n"
                         "precision mediump float;\n"
                         "in vec2 vUV;\n"
                         "out vec4 FragColor;\n"
                         "uniform sampler2D texY;\n"
                         "uniform sampler2D texU;\n"
                         "uniform sampler2D texV;\n"
                         "void main(){\n"
                         "   float y = texture(texY, vUV).r;\n"
                         "   float u = texture(texU, vUV).r - 0.5;\n"
                         "   float v = texture(texV, vUV).r - 0.5;\n"
                         "   float r = y + 1.402 * v;\n"
                         "   float g = y - 0.344136 * u - 0.714136 * v;\n"
                         "   float b = y + 1.772 * u;\n"
                         "   FragColor = vec4(r,g,b,1.0);\n"
                         "}\n";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        free(r);
        return -3;
    }

    r->prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!r->prog) {
        free(r);
        return -4;
    }

    r->loc_texY = glGetUniformLocation(r->prog, "texY");
    r->loc_texU = glGetUniformLocation(r->prog, "texU");
    r->loc_texV = glGetUniformLocation(r->prog, "texV");

    float verts[] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};

    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

    glGenTextures(1, &r->texY);
    glGenTextures(1, &r->texU);
    glGenTextures(1, &r->texV);

    GLuint ts[3] = {r->texY, r->texU, r->texV};
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, ts[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    *out = r;
    return 1;
}

static inline void yuv_renderer_render(Yuv420p_Renderer_Context *r, const uint8_t *Y, const uint8_t *U, const uint8_t *V, int strideY, int strideU, int strideV, int capW, int capH, int outW, int outH) {
    if (!r) return;

// rotation in degrees: 0, 90, 180, 270
int rotation = 0;   // set this depending on device orientation

if (outW != r->lastOutW || outH != r->lastOutH) {
    rtp_log("=== yuv_renderer_render: output size changed: old=%dx%d new=%dx%d ===\n",
            r->lastOutW, r->lastOutH, outW, outH);

    float aspect_video  = (float)capW / (float)capH;
    float aspect_screen = (float)outW / (float)outH;

    float scaleX = 1.0f;
    float scaleY = 1.0f;

    // Aspect-fit scaling
    if (aspect_screen > aspect_video) {
        // screen is wider: scale X down
        scaleX = aspect_video / aspect_screen;
    } else {
        // screen is taller: scale Y down
        scaleY = aspect_screen / aspect_video;
    }

    // Base quad (before rotation)
    float vx0 = -scaleX, vy0 = -scaleY;  // bottom-left
    float vx1 =  scaleX, vy1 = -scaleY;  // bottom-right
    float vx2 = -scaleX, vy2 =  scaleY;  // top-left
    float vx3 =  scaleX, vy3 =  scaleY;  // top-right

    float base[16];

    // Apply rotation by remapping vertex order
    switch (rotation) {
        case 0:
            base[0]=vx0; base[1]=vy0; base[2]=0; base[3]=1;
            base[4]=vx1; base[5]=vy1; base[6]=1; base[7]=1;
            base[8]=vx2; base[9]=vy2; base[10]=0; base[11]=0;
            base[12]=vx3; base[13]=vy3; base[14]=1; base[15]=0;
            break;

        case 90:
            base[0]=vx2; base[1]=vy2; base[2]=0; base[3]=1;
            base[4]=vx0; base[5]=vy0; base[6]=1; base[7]=1;
            base[8]=vx3; base[9]=vy3; base[10]=0; base[11]=0;
            base[12]=vx1; base[13]=vy1; base[14]=1; base[15]=0;
            break;

        case 180:
            base[0]=vx3; base[1]=vy3; base[2]=0; base[3]=1;
            base[4]=vx2; base[5]=vy2; base[6]=1; base[7]=1;
            base[8]=vx1; base[9]=vy1; base[10]=0; base[11]=0;
            base[12]=vx0; base[13]=vy0; base[14]=1; base[15]=0;
            break;

        case 270:
            base[0]=vx1; base[1]=vy1; base[2]=0; base[3]=1;
            base[4]=vx3; base[5]=vy3; base[6]=1; base[7]=1;
            base[8]=vx0; base[9]=vy0; base[10]=0; base[11]=0;
            base[12]=vx2; base[13]=vy2; base[14]=1; base[15]=0;
            break;
    }

    rtp_log("scale: %f %f   rotation=%d\n", scaleX, scaleY, rotation);
    rtp_log("vertex quad rotated: [%f %f] [%f %f] [%f %f] [%f %f]\n",
            base[0], base[1], base[4], base[5], base[8], base[9], base[12], base[13]);

    glViewport(0, 0, outW, outH);

    r->lastOutW = outW;
    r->lastOutH = outH;
}

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->texY);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, strideY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, capW, capH, 0, GL_RED, GL_UNSIGNED_BYTE, Y);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->texU);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, strideU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, capW / 2, capH / 2, 0, GL_RED, GL_UNSIGNED_BYTE, U);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, r->texV);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, strideV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, capW / 2, capH / 2, 0, GL_RED, GL_UNSIGNED_BYTE, V);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glUseProgram(r->prog);
    glUniform1i(r->loc_texY, 0);
    glUniform1i(r->loc_texU, 1);
    glUniform1i(r->loc_texV, 2);

    glBindVertexArray(r->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static inline void yuv_renderer_close(Yuv420p_Renderer_Context **out) {
    if (!out || !*out) return;

    Yuv420p_Renderer_Context *r = *out;

    // Program
    if (r->prog) glDeleteProgram(r->prog);

    // Buffers + VAO
    if (r->vbo) glDeleteBuffers(1, &r->vbo);
    if (r->vao) glDeleteVertexArrays(1, &r->vao);

    // Textures
    if (r->texY) glDeleteTextures(1, &r->texY);
    if (r->texU) glDeleteTextures(1, &r->texU);
    if (r->texV) glDeleteTextures(1, &r->texV);

    free(r);
    *out = NULL;
}
