/* atari.c - native GEM file selector for the Atari build of sz81.
 *
 * SDL's XBIOS driver owns the screen and the IKBD vectors, so the AES file
 * selector can only run once SDL's video has been shut down, which gives the
 * desktop's screen, mouse and keyboard back. Video is restarted afterwards
 * and sz81 rebuilds its scaled resources exactly as it does on a resolution
 * change. Under a multitasking AES SDL uses its GEM driver instead, which
 * lives alongside the AES, so the selector is simply called. Without an AES
 * (e.g. started from the AUTO folder) the caller falls back to sz81's own
 * load dialog.
 */

#include <stdlib.h>
#include <string.h>
#include <gem.h>
#include <support.h>
#include "sdl_engine.h"
#include "atari.h"

char *atari_file_request_retval = NULL;

static int aes_state = -1;	/* -1 = not checked, 0 = absent, 1 = present */

static void aes_exit(void) {
	appl_exit();
}

/* On exit: returns TRUE if SDL is running in a GEM window */

int atari_gem_driver(void) {
	char driver[16];

	return SDL_VideoDriverName(driver, sizeof(driver)) && !strcmp(driver, "gem");
}

int atari_aes_available(void) {
	if (aes_state < 0) {
		/* SDL's GEM driver has already registered with the AES */
		if (atari_gem_driver()) {
			aes_state = 1;
		} else {
			aes_state = appl_init() >= 0;
			if (aes_state) atexit(aes_exit);
		}
	}
	return aes_state;
}

/* On exit: returns the full path of the selected file
 *          else NULL if the user cancelled */

char *atari_file_request(const char *title) {
	static char fname[256];
	char path[256], name[64] = "", *p;
	short button = 0, ok;
	int gem = atari_gem_driver();
	SDL_Event event;

	/* fsel wants a DOS path ending in a wildcard */
	if (_unx2dos(load_file_dialog.dir, path, sizeof(path) - 8) < 0)
		strcpy(path, load_file_dialog.dir);
	p = path + strlen(path);
	if (p > path && p[-1] != '\\') *p++ = '\\';
	strcpy(p, zx80 ? "*.O" : "*.P");

	if (!gem) SDL_QuitSubSystem(SDL_INIT_VIDEO);

	/* SDL shows the mouse again on its way out; showing it once more would
	 * leave it drawn while the selector paints, smearing stale pixels */
	wind_update(BEG_UPDATE);
	graf_mouse(ARROW, NULL);
	/* fsel_exinput (with a title) arrived with AES 1.3, TOS 1.04 */
	if (gl_ap_version >= 0x0130)
		ok = fsel_exinput(path, name, &button, title);
	else
		ok = fsel_input(path, name, &button);
	wind_update(END_UPDATE);

	if (!gem) {
		if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
			fprintf(stderr, "%s: Cannot restart video: %s\n", __func__, SDL_GetError());
			exit(1);
		}
		sdl_video_setmode();
	}

	/* Keys and buttons were released while the AES had the IKBD */
	keyboard_buffer_reset(TRUE, 0, 0);
	key_repeat_manager(KRM_FUNC_RELEASE, NULL, 0);
	while (SDL_PollEvent(&event));

	if (!ok || !button || !*name)
		return NULL;

	/* Strip the wildcard to get the directory, and remember it */
	if ((p = strrchr(path, '\\')) != NULL) *p = 0;
	if (_dos2unx(path, load_file_dialog.dir, sizeof(load_file_dialog.dir)) < 0)
		strcpy(load_file_dialog.dir, path);

	strcpy(fname, load_file_dialog.dir);
	strcatdelimiter(fname);
	strcat(fname, name);

	return fname;
}
