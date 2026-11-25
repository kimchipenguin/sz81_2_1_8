/* fastvideo.c - see fastvideo.h */

#include <stdlib.h>
#include <string.h>
#include "fastvideo.h"

static struct {
	int width, height, stride, bpp, scale, invert;
	uint32_t c0, c1;
	void *pixels;
} key;
static int have_key, valid;
static unsigned char *prev;	/* the bitmap as drawn */
static size_t prev_size;
/* One output row of a source byte: 8 pixels * scale * 4 bytes at most */
static uint32_t tab[256 * 24];

void fv_invalidate(void) {
	valid = 0;
}

static void build_table(int bpp, int scale, int invert, uint32_t c0, uint32_t c1) {
	int b, k, s;

	for (b = 0; b < 256; b++) {
		if (bpp == 16) {
			uint16_t *e = (uint16_t *)(tab + b * 24);

			for (k = 0; k < 8; k++) {
				uint16_t v = (((b >> (7 - k)) & 1) ^ invert) ? (uint16_t)c0 : (uint16_t)c1;

				for (s = 0; s < scale; s++) *e++ = v;
			}
		} else {
			uint32_t *e = tab + b * 24;

			for (k = 0; k < 8; k++) {
				uint32_t v = (((b >> (7 - k)) & 1) ^ invert) ? c0 : c1;

				for (s = 0; s < scale; s++) *e++ = v;
			}
		}
	}
}

int fv_render(const unsigned char *bmp, int width, int height, int stride,
	void *pixels, int bpp, int scale, uint32_t c0, uint32_t c1, int invert,
	int clean) {
	int nb, esz, ew, rowbytes, y, x, r, drawn = 0;
	int align4 = ((uintptr_t)pixels & 3) == 0;
	size_t need;

	if ((width & 7) || (bpp != 16 && bpp != 32) || scale < 1 || scale > 3 ||
		width <= 0 || height <= 0 || stride < width / 8)
		return -1;

	/* Anything that changes the picture without a new bitmap */
	if (!have_key || key.width != width || key.height != height ||
		key.stride != stride || key.bpp != bpp || key.scale != scale ||
		key.invert != !!invert || key.c0 != c0 || key.c1 != c1 ||
		key.pixels != pixels) {
		key.width = width; key.height = height; key.stride = stride;
		key.bpp = bpp; key.scale = scale; key.invert = !!invert;
		key.c0 = c0; key.c1 = c1; key.pixels = pixels;
		have_key = 1;
		valid = 0;
		build_table(bpp, scale, !!invert, c0, c1);
	}
	need = (size_t)stride * height;
	if (need > prev_size) {
		free(prev);
		prev = malloc(need);
		prev_size = prev ? need : 0;
		valid = 0;
		if (!prev) return -1;
	}
	if (!clean) valid = 0;

	nb = width / 8;
	esz = 8 * scale * (bpp / 8);		/* bytes per source byte */
	ew = esz / 4;				/* in 32 bit words */
	rowbytes = width * scale * (bpp / 8);	/* one output row */

	for (y = 0; y < height; y++) {
		const unsigned char *src = bmp + (size_t)y * stride;
		unsigned char *dst = (unsigned char *)pixels + (size_t)y * scale * rowbytes;

		if (valid && memcmp(prev + (size_t)y * stride, src, nb) == 0)
			continue;
		if (align4) {
			uint32_t *d = (uint32_t *)dst;

			for (x = 0; x < nb; x++) {
				const uint32_t *e = tab + src[x] * 24;
				int n = ew;

				do { *d++ = *e++; } while (--n);
			}
		} else {
			uint16_t *d = (uint16_t *)dst;

			for (x = 0; x < nb; x++) {
				const uint16_t *e = (const uint16_t *)(tab + src[x] * 24);
				int n = esz / 2;

				do { *d++ = *e++; } while (--n);
			}
		}
		for (r = 1; r < scale; r++)
			memcpy(dst + (size_t)r * rowbytes, dst, rowbytes);
		memcpy(prev + (size_t)y * stride, src, nb);
		drawn++;
	}
	valid = clean ? 1 : 0;	/* not clean: something is drawn over it */
	return drawn;
}
