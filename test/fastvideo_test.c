/* Host test for fastvideo.c: the output must equal the original 3-pass
 * conversion (1 bit -> 8 bit lookup -> scaled surface), also when only
 * the changed rows are drawn.  cc -O2 -I../source fastvideo_test.c ../source/fastvideo.c
 * with fv_tables.h = the lookup tables from sdl_main.c (see run_fv_test.sh) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fastvideo.h"
#include "fv_tables.h"

/* the original, from sdl_video.c: vga = 8 bit pixels, then scaled */
static void reference(const unsigned char *bmp, int width, int height, int stride,
	void *pixels, int bpp, int scale, uint32_t c0, uint32_t c1, int invert) {
	static unsigned char vga[1024 * 1024];
	uint64_t *v = (uint64_t *)vga;
	unsigned char *vp = vga;
	int x, y, offset = 0;
	int line = width * scale;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x += 8) {
			const unsigned char *p = bmp + y * stride + x / 8;
			*v++ = invert ? lookupv[*p] : lookup[*p];
		}
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			uint32_t c = *vp++ ? c1 : c0;
			int sx, sy;

			for (sy = 0; sy < scale; sy++)
				for (sx = 0; sx < scale; sx++) {
					size_t o = ((size_t)(y * scale + sy) * line) + x * scale + sx;

					if (bpp == 16) ((uint16_t *)pixels)[o] = c;
					else ((uint32_t *)pixels)[o] = c;
				}
		}
	}
	(void)offset;
}

int main(void) {
	int bad = 0, tests = 0;
	int iter;

	srand(12345);
	for (iter = 0; iter < 3000; iter++) {
		int width = 8 * (1 + rand() % 40), height = 1 + rand() % 60;
		int stride = width / 8 + 1;
		int bpp = (rand() & 1) ? 16 : 32, scale = 1 + rand() % 3, invert = rand() & 1;
		uint32_t c0 = rand(), c1 = rand();
		size_t psz = (size_t)width * scale * height * scale * (bpp / 8);
		unsigned char *bmp = calloc(1, stride * height), *pixbase = calloc(1, psz + 16), *ref = calloc(1, psz + 8);
		unsigned char *pix = pixbase + ((rand() & 1) ? 2 : 0);	/* also 2 byte aligned only (68000) */
		int frame;

		if (bpp == 16) { c0 &= 0xffff; c1 &= 0xffff; }
		fv_invalidate();
		for (frame = 0; frame < 6; frame++) {
			int n = frame == 0 ? stride * height : rand() % 4;	/* few changes */
			int clean = frame == 0 || (rand() % 5);
			int i, rows;

			for (i = 0; i < n; i++) bmp[rand() % (stride * height)] = rand();
			if (!clean) memset(pix, rand(), psz);	/* overlay scribbled on it */
			rows = fv_render(bmp, width, height, stride, pix, bpp, scale, c0, c1, invert, clean);
			reference(bmp, width, height, stride, ref, bpp, scale, c0, c1, invert);
			tests++;
			if (rows < 0 || memcmp(pix, ref, psz)) {
				if (bad++ < 5) printf("MISMATCH iter %d frame %d w%d h%d bpp%d s%d inv%d clean%d rows%d\n",
					iter, frame, width, height, bpp, scale, invert, clean, rows);
			}
			/* an unchanged bitmap on a clean surface must draw nothing */
			if (clean && fv_render(bmp, width, height, stride, pix, bpp, scale, c0, c1, invert, 1) != 0) {
				if (bad++ < 5) printf("REDRAW iter %d frame %d\n", iter, frame);
			}
		}
		free(bmp); free(pixbase); free(ref);
	}
	printf("%d tests, %d bad\n", tests, bad);
	return bad != 0;
}
