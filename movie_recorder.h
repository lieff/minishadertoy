#pragma once

// Minimal offline video recorder: encodes RGBA frames to H.264 (minih264e.h)
// and muxes them into an MP4 file (minimp4.h). Video only, no audio.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MovieRecorder MovieRecorder;

// Opens an output file and initializes the encoder/muxer.
// width/height are the framebuffer dimensions; they are rounded down to
// multiples of 16 internally.
// bitrate_kbps: target video bitrate in kilobits per second; 0 selects a
// default quality target (~0.5 bits/pixel). Returns NULL on failure.
MovieRecorder *movie_recorder_open(const char *filename, int width, int height, int fps, int bitrate_kbps);

// Adds one RGBA frame (width*height*4 bytes, bottom-up). Returns 0 on failure.
int movie_recorder_add_frame(MovieRecorder *r, const unsigned char *rgba);

// Finalizes the MP4 and releases all resources.
void movie_recorder_close(MovieRecorder *r);

#ifdef __cplusplus
}
#endif
