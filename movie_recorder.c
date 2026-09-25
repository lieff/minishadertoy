#include "movie_recorder.h"

#include "minih264e.h"
#include "minimp4.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MovieRecorder
{
    FILE *file;
    MP4E_mux_t *mux;
    mp4_h26x_writer_t writer;
    int mux_ready;

    H264E_persist_t *enc;
    H264E_scratch_t *scratch;
    int enc_ready;

    int orig_width, orig_height;   // framebuffer dimensions (RGBA stride)
    int enc_width, enc_height;     // encoder dimensions (multiple of 16)
    int fps;

    H264E_run_param_t run_param;
    H264E_io_yuv_t yuv;
    unsigned char *yuv_buf;
};

static int write_cb(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE *)token;
    fseek(f, (long)offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}

// BT.601 (limited range) RGBA -> YUV420p, flipping vertically because the
// OpenGL framebuffer origin is at the bottom-left. The source is cropped to
// the encoder size (enc_w x enc_h).
static void rgba_to_yuv(const unsigned char *rgba, int rgba_w, int rgba_h,
                        int enc_w, int enc_h,
                        unsigned char *Y, unsigned char *U, unsigned char *V)
{
    int x, y;
    for (y = 0; y < enc_h; y++)
    {
        int src_y = rgba_h - 1 - y;
        const unsigned char *src = rgba + (size_t)src_y * rgba_w * 4;
        unsigned char *yrow = Y + (size_t)y * enc_w;
        for (x = 0; x < enc_w; x++)
        {
            int r = src[x * 4 + 0];
            int g = src[x * 4 + 1];
            int b = src[x * 4 + 2];

            int yv = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            yrow[x] = (unsigned char)(yv < 0 ? 0 : (yv > 255 ? 255 : yv));

            if ((x & 1) == 0 && (y & 1) == 0)
            {
                int uv = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int vv = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                size_t uv_idx = (size_t)(y / 2) * (enc_w / 2) + (x / 2);
                U[uv_idx] = (unsigned char)(uv < 0 ? 0 : (uv > 255 ? 255 : uv));
                V[uv_idx] = (unsigned char)(vv < 0 ? 0 : (vv > 255 ? 255 : vv));
            }
        }
    }
}

MovieRecorder *movie_recorder_open(const char *filename, int width, int height, int fps, int bitrate_kbps)
{
    MovieRecorder *r = (MovieRecorder *)calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->orig_width = width;
    r->orig_height = height;
    r->enc_width = (width / 16) * 16;
    r->enc_height = (height / 16) * 16;
    if (r->enc_width < 16) r->enc_width = 16;
    if (r->enc_height < 16) r->enc_height = 16;
    if (fps <= 0) fps = 30;
    r->fps = fps;

    r->file = fopen(filename, "wb");
    if (!r->file)
        goto fail;

    r->mux = MP4E_open(0, 0, r->file, write_cb);
    if (!r->mux)
        goto fail;

    if (MP4E_STATUS_OK != mp4_h26x_write_init(&r->writer, r->mux, r->enc_width, r->enc_height, 0))
        goto fail;
    r->mux_ready = 1;

    {
        H264E_create_param_t cp;
        memset(&cp, 0, sizeof(cp));
        cp.width = r->enc_width;
        cp.height = r->enc_height;
        cp.gop = fps;
        cp.vbv_size_bytes = 0;
        cp.max_long_term_reference_frames = 0;
        cp.enableNEON = 0;
        cp.const_input_flag = 1;

        int sp = 0, ss = 0;
        if (H264E_STATUS_SUCCESS != H264E_sizeof(&cp, &sp, &ss))
            goto fail;

        r->enc = (H264E_persist_t *)malloc(sp);
        r->scratch = (H264E_scratch_t *)malloc(ss);
        if (!r->enc || !r->scratch)
            goto fail;

        if (H264E_STATUS_SUCCESS != H264E_init(r->enc, &cp))
            goto fail;
        r->enc_ready = 1;
    }

    r->yuv_buf = (unsigned char *)malloc((size_t)r->enc_width * r->enc_height * 3 / 2);
    if (!r->yuv_buf)
        goto fail;

    r->yuv.yuv[0] = r->yuv_buf;
    r->yuv.stride[0] = r->enc_width;
    r->yuv.yuv[1] = r->yuv_buf + (size_t)r->enc_width * r->enc_height;
    r->yuv.stride[1] = r->enc_width / 2;
    r->yuv.yuv[2] = r->yuv.yuv[1] + (size_t)(r->enc_width / 2) * (r->enc_height / 2);
    r->yuv.stride[2] = r->enc_width / 2;

    memset(&r->run_param, 0, sizeof(r->run_param));
    r->run_param.encode_speed = H264E_SPEED_FASTEST;
    r->run_param.frame_type = H264E_FRAME_TYPE_DEFAULT;
    if (bitrate_kbps > 0)
        r->run_param.desired_frame_bytes = (int)((long long)bitrate_kbps * 1000 / 8 / fps);
    else
        r->run_param.desired_frame_bytes = (r->enc_width * r->enc_height) / 16;
    r->run_param.qp_min = 10;
    r->run_param.qp_max = 51;

    return r;

fail:
    movie_recorder_close(r);
    return NULL;
}

int movie_recorder_add_frame(MovieRecorder *r, const unsigned char *rgba)
{
    unsigned char *coded = NULL;
    int coded_size = 0;

    if (!r || !r->enc_ready || !r->mux_ready)
        return 0;

    rgba_to_yuv(rgba, r->orig_width, r->orig_height,
                r->enc_width, r->enc_height,
                r->yuv.yuv[0], r->yuv.yuv[1], r->yuv.yuv[2]);

    if (H264E_STATUS_SUCCESS != H264E_encode(r->enc, r->scratch, &r->run_param,
            &r->yuv, &coded, &coded_size))
        return 0;

    if (MP4E_STATUS_OK != mp4_h26x_write_nal(&r->writer, coded, coded_size, 90000 / r->fps))
        return 0;

    return 1;
}

void movie_recorder_close(MovieRecorder *r)
{
    if (!r)
        return;

    if (r->mux_ready)
    {
        mp4_h26x_write_close(&r->writer);
        MP4E_close(r->mux);
        r->mux_ready = 0;
    }

    if (r->enc_ready)
    {
        free(r->enc);
        free(r->scratch);
        r->enc = NULL;
        r->scratch = NULL;
        r->enc_ready = 0;
    }

    free(r->yuv_buf);
    if (r->file)
        fclose(r->file);
    free(r);
}
