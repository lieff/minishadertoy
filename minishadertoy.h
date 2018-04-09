#pragma once

#ifdef _DEBUG
void CheckGLErrors(const char *func, int line);
#define GLCHK glFinish(); CheckGLErrors(__FUNCTION__, __LINE__);
#else
#define GLCHK
#endif

typedef struct FBO
{
    const char *id;
    GLuint framebuffer;
    GLuint framebufferTex;
    int floatTex;
} FBO;

typedef struct SAMPLER
{
    int filter, wrap, vflip, srgb, internal;
} SAMPLER;

typedef struct SHADER_INPUT
{
    const char *id;
    GLuint tex;
    SAMPLER sampler;
} SHADER_INPUT;

typedef struct SHADER
{
    GLuint prog;
    GLuint shader;
    SHADER_INPUT inputs[4];
    FBO output;

    GLuint iResolution;
    GLuint iTime;
    GLuint iTimeDelta;
    GLuint iFrame;
    GLuint iMouse;
    GLuint iDate;
    GLuint iSampleRate;
    GLuint iChannelTime[4];
    GLuint iChannelResolution[4];
    GLuint iChannel[4];
} SHADER;
