/* bench.c - headless Z80/ZX81 core benchmark for Atari ST.
 *
 * Links only the emulator core (z80.c, common.c, sound.c, loadp.c) with
 * stubs for the SDL front-end hooks, boots the ROM, runs a fixed number
 * of emulated frames as fast as possible and reports the speed relative
 * to a real ZX81/ZX80 (50 frames per second).  The final screen bitmap
 * is written to BENCH.PBM so the emulation can be checked visually.
 *
 * usage: bench [frames] [zx80]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../common.h"
#include "../sound.h"
#include "../z80.h"
#include "../sdl.h"
#include "../sdl_engine.h"
#include "../loadp.h"

/* No SDL here: keep a plain main() even where SDL_main.h renames it */
#undef main

extern void mainloop(void);
extern void setDisplayBoundaries(void);

static long frames_wanted = 100, frames_done;

/* Front-end hooks the core calls */
void update_scrn(void) { }

void check_events(void)
{
	if (++frames_done >= frames_wanted)
		interrupted = INTERRUPT_EMULATOR_EXIT;
}

/* frame_pause() spins on this until the timer says the next frame is due;
 * the benchmark runs unthrottled, so every frame is due at once. */
void SDL_Delay(Uint32 ms) { signal_int_flag = 1; }

int sdl_load_file(int parameter, int method) { return 1; }
int sdl_save_file(int parameter, int method) { return 1; }
int sdl_filetype_casecmp(char *filename, char *filetype) { return 1; }
int sdl_sound_init(int freq, int *stereo, int *sixteenbit) { return 1; }
void sdl_sound_frame(unsigned char *data, int len) { }
void sdl_sound_end(void) { }

static int load(const char *name, unsigned char *buf, size_t len)
{
	FILE *f = fopen(name, "rb");
	size_t n = 0;

	if (f) {
		n = fread(buf, 1, len, f);
		fclose(f);
	}
	if (n != len) {
		printf("cannot read %s\n", name);
		return -1;
	}
	return 0;
}

static void dump_screen(const char *name)
{
	FILE *f = fopen(name, "wb");
	int y;

	if (!f)
		return;
	fprintf(f, "P4\n%d %d\n", disp.width, disp.height);
	for (y = 0; y < disp.height; y++)
		fwrite(scrnbmp + y * disp.stride_byte, 1, disp.width / 8, f);
	fclose(f);
}

int main(int argc, char *argv[])
{
	clock_t t0, t1;
	long hundredths;

	if (argc > 1)
		frames_wanted = atol(argv[1]);
	zx80 = argc > 2 && !strcmp(argv[2], "zx80");

	sdl_emulator.model = &zx80;
	sdl_emulator.wrx = HIRESWRX;
	sdl_emulator.chrgen = CHRGENSINCLAIR;
	sdl_emulator.ramsize = 16;
	sdl_emulator.speed = 20;
	sdl_emulator.state = 1;

	if (load("zx81.rom", sdl_zx81rom.data, 8192) || load("zx80.rom", sdl_zx80rom.data, 4096))
		return 1;

	initdisplay();
	setDisplayBoundaries();
	initmem();

	printf("sz81 core bench: %s, %ld frames\n", zx80 ? "ZX80" : "ZX81", frames_wanted);
	t0 = clock();
	mainloop();
	t1 = clock();

	hundredths = (long)(t1 - t0) * 100 / CLOCKS_PER_SEC;
	if (hundredths < 1)
		hundredths = 1;
	printf("%ld frames in %ld.%02ld s\n", frames_done, hundredths / 100, hundredths % 100);
	/* real machine: 50 frames/s -> percent = frames * 200 / hundredths */
	printf("speed: %ld.%ld%% of real time\n",
		frames_done * 200 / hundredths, frames_done * 2000 / hundredths % 10);
	dump_screen("bench.pbm");
	return 0;
}
