/* fastvideo.h - the emulator picture straight from the 1 bit bitmap to a
 * 16 or 32 bit surface, in one pass, only the rows that changed. */
#ifndef FASTVIDEO_H
#define FASTVIDEO_H

#include <stdint.h>

/* Draws bmp (height rows of stride bytes, width pixels each, 1 bit per
 * pixel, left pixel in the top bit) into pixels, scaled by scale (1-3),
 * bpp 16 or 32, rows packed (pitch = width * scale pixels). A set bit is
 * c0, a clear one c1; invert swaps them. When clean is non-zero the
 * surface holds nothing but this picture, so rows that did not change
 * since the last call are left alone. Returns the number of source rows
 * drawn (0: nothing changed, the surface is as it was), or -1 when the
 * parameters are not supported (width not a multiple of 8, bpp, scale). */
int fv_render(const unsigned char *bmp, int width, int height, int stride,
	void *pixels, int bpp, int scale, uint32_t c0, uint32_t c1, int invert,
	int clean);

/* The surface was changed by someone else: draw everything next time */
void fv_invalidate(void);

#endif
