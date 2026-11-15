/* atari.c - Atari specifics of sz81: GEM file selector, menu bar and the
 * keyboard in a GEM window.
 *
 * File selector: SDL's XBIOS driver owns the screen and the IKBD vectors, so
 * the AES file selector can only run once SDL's video has been shut down,
 * which gives the desktop's screen, mouse and keyboard back. Video is
 * restarted afterwards and sz81 rebuilds its scaled resources exactly as it
 * does on a resolution change. Under a multitasking AES SDL uses its GEM
 * driver instead, which lives alongside the AES, so the selector is simply
 * called. Without an AES (e.g. started from the AUTO folder) the caller
 * falls back to sz81's own load dialog.
 *
 * GEM window: the AES reports key presses but no releases, so SDL's GEM
 * driver can only guess when a key goes up. A handler in the TOS keyboard
 * vector (atari_ikbd.S) sees every press and release instead; sz81's key
 * events are made from that while SDL's own are filtered out. The menu bar
 * needs our SDL build, whose GEM driver shows us every AES message first
 * (SDL_AtariGem_SetMessageHook), so we also get the messages of our own
 * windows: the keyboard window (View > Keyboard), the About window and the
 * the Hardware, Sound and GUI options dialogs (Options menu, CF-Lib
 * window dialogs).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <gem.h>
#include <cflib.h>
#include <support.h>
#include <mint/osbind.h>
#include <mint/cookie.h>
#include <mint/ostruct.h>
#include <mint/falcon.h>
#include <zlib.h>
#include "sdl_engine.h"
#include "atari.h"
#include "atari/sz81_rsc.h"
#include "atari/cftabs.h"
#include "atari/colsel.h"
#include "atari/aesfix.h"

/* Version of the Atari port, shown in the About window */
#define ATARI_VERSION "0.9"

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

/***************************************************************************
 * Resource file                                                           *
 ***************************************************************************/
/* SZ81.RSC next to the program holds the menu bar, the Settings dialog and
 * all texts of the GEM user interface (written by atari/mkrsc.py), so it
 * can be translated with a resource editor. It is only needed under a
 * multitasking AES, where SDL uses its GEM driver. */

static int rsc_loaded;

/* On exit: returns free string i of the resource file */
static char *rs(int i) {
	char **p = NULL;

	if (!rsc_loaded) return "";
	rsrc_gaddr(R_FRSTR, i, &p);
	return p && *p ? *p : "";
}

static int rs_alert(int i) {
	return form_alert(1, rs(i));
}

/***************************************************************************
 * IKBD hook                                                               *
 ***************************************************************************/
/* TOS 2.0+ calls the "kbdvec" (the long in front of the table that
 * Kbdvbase() returns) for every keyboard byte; FreeMiNT and SDL chain their
 * own keyboard handling into the same vector. Our handler goes in front,
 * notes make/break codes in two 128-byte tables and passes every byte on.
 * A second handler in the joystick vector (joyvec) keeps the state of the
 * joystick in port 1; SDL reads no joystick under its GEM driver.
 *
 * The handler runs in interrupts while any process may be active, so it is
 * copied into global memory. It is removed at exit (atexit), on fatal
 * signals and from the process termination vector etv_term (also after
 * bombs under plain TOS). */

extern char ikbd_hook_start[], ikbd_hook_end[], ikbd_hook_old[];
extern char ikbd_hook_handler[], ikbd_hook_down[], ikbd_hook_hit[];
extern char ikbd_hook_joy_old[], ikbd_hook_joy_handler[], ikbd_hook_joy[];
extern long ikbd_term_old;
void ikbd_term_stub(void);
long ikbd_remove_super(void);

#define HOOK_OFF(sym) ((sym) - ikbd_hook_start)

#define SC_CTRL   0x1d
#define SC_LSHIFT 0x2a
#define SC_RSHIFT 0x36
#define SC_ALT    0x38
#define SC_CAPS   0x3a
#define SC_ESC    0x01
#define SC_RETURN 0x1c
#define SC_ENTER  0x72

static char *hook_mem;			/* copy of the handler block */
static volatile unsigned char *kdown, *khit, *kjoy;
static long *kbdvec, *joyvec;
static long hook_handler, joy_handler;	/* our handlers in hook_mem */
static int hook_active, joy_active, term_hooked, mint;
static long cpu;
static unsigned short tos_version;
static SDLKey scan_sym[128];

static long super_tos_version(void) {
	char *sysbase = *(char **)0x4f2;
	char *os_beg = *(char **)(sysbase + 8);

	tos_version = *(unsigned short *)(os_beg + 2);
	return 0;
}

static void flush_caches(void) {
	/* The handler was copied as data: write it back from the data cache
	 * and drop stale instructions (68040 copyback cache in ARAnyM) */
#ifdef __mcoldfire__
	/* ColdFire V4e: push every line of the 4 ways x 512 sets */
	__asm__ volatile (
		"nop\n\t"
		"moveq #0,%%d0\n\t"
		"moveq #0,%%d1\n\t"
		"move.l %%d0,%%a0\n"
		"1:\tcpushl %%bc,(%%a0)\n\t"
		"lea 0x10(%%a0),%%a0\n\t"
		"addq.l #1,%%d1\n\t"
		"cmpi.w #512,%%d1\n\t"
		"bne.s 1b\n\t"
		"clr.l %%d1\n\t"
		"addq.l #1,%%d0\n\t"
		"move.l %%d0,%%a0\n\t"
		"cmpi.w #4,%%d0\n\t"
		"bne.s 1b\n\t"
		"nop" ::: "d0", "d1", "a0", "memory");
#else
	if (cpu >= 40)
		__asm__ volatile (".word 0xf4f8" ::: "memory");	/* cpusha bc */
	else if (cpu >= 20)
		/* movec cacr,d0 / movec d0,cacr as words: a 68000 build's
		 * assembler refuses them, the code only runs on a 68020+ */
		__asm__ volatile (".word 0x4e7a,0x0002\n\t"
			"or.w #0x0808,%%d0\n\t"
			".word 0x4e7b,0x0002" ::: "d0", "memory");
#endif
}

static long super_install(void) {
	*(long *)(hook_mem + HOOK_OFF(ikbd_hook_old)) = *kbdvec;
	*(long *)(hook_mem + HOOK_OFF(ikbd_hook_joy_old)) = *joyvec;
	flush_caches();
	*kbdvec = hook_handler;
	*joyvec = joy_handler;
	joy_active = TRUE;
	return 0;
}

/* Unlink a handler from the chain of handlers that starts at vec, old is
 * its XBRA link. On exit: returns TRUE if it was found */
static int unlink_handler(long *vec, long handler, long old) {
	long *p = vec;
	int n;

	for (n = 0; n < 32 && *p; n++) {
		long *x;

		if (*p == handler) {
			*p = old;
			return TRUE;
		}
		x = (long *)(*p - 12);	/* XBRA header in front of a handler */
		if (x[0] != 0x58425241L)	/* 'XBRA' */
			break;
		p = &x[2];
	}
	return FALSE;
}

/* Unlink our handlers from the kbdvec and joyvec chains (supervisor mode).
 * Also called by the etv_term stub. */
long ikbd_remove_super(void) {
	if (!hook_active) return 0;
	if (joy_active && unlink_handler(joyvec, joy_handler,
		*(long *)(hook_mem + HOOK_OFF(ikbd_hook_joy_old))))
		joy_active = FALSE;
	if (unlink_handler(kbdvec, hook_handler,
		*(long *)(hook_mem + HOOK_OFF(ikbd_hook_old))))
		hook_active = FALSE;
	/* Plain TOS has one global etv_term: put the old one back (MiNT keeps
	 * one per process, which ends with us) */
	if (term_hooked && !mint) {
		*(long *)0x408 = ikbd_term_old;
		term_hooked = FALSE;
	}
	return 0;
}

static void ikbd_remove(void) {
	if (hook_active) Supexec(ikbd_remove_super);
	if (term_hooked) {
		Setexc(0x102, (void (*)())ikbd_term_old);
		term_hooked = FALSE;
	}
	if (!hook_active && !joy_active && hook_mem) {
		Mfree(hook_mem);
		hook_mem = NULL;
	}
}

static void on_signal(int sig) {
	ikbd_remove();
	signal(sig, SIG_DFL);
	raise(sig);
}

/* The SDLKey for a scancode, from the keyboard's own unshifted table so
 * that the layout is respected */
static SDLKey key_for_scancode(int sc, int c) {
	switch (sc) {
		case 0x01: return SDLK_ESCAPE;
		case 0x0e: return SDLK_BACKSPACE;
		case 0x0f: return SDLK_TAB;
		case 0x1c: return SDLK_RETURN;
		case SC_CTRL: return SDLK_LCTRL;
		case SC_LSHIFT: return SDLK_LSHIFT;
		case SC_RSHIFT: return SDLK_RSHIFT;
		case SC_ALT: return SDLK_LALT;
		case 0x39: return SDLK_SPACE;
		case SC_CAPS: return SDLK_CAPSLOCK;
		case 0x47: return SDLK_HOME;
		case 0x48: return SDLK_UP;
		case 0x4b: return SDLK_LEFT;
		case 0x4d: return SDLK_RIGHT;
		case 0x50: return SDLK_DOWN;
		case 0x52: return SDLK_INSERT;
		case 0x53: return SDLK_DELETE;
		case 0x61: return SDLK_UNDO;
		case 0x62: return SDLK_HELP;
		case 0x72: return SDLK_KP_ENTER;
	}
	if (sc >= 0x3b && sc <= 0x44) return SDLK_F1 + sc - 0x3b;
	if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	if (c > ' ' && c < 127) return (SDLKey)c;
	return SDLK_UNKNOWN;
}

static int ikbd_install(void) {
	long size = ikbd_hook_end - ikbd_hook_start, v;
	char *unshift;
	int sc;
	static const int sigs[] = { SIGBUS, SIGSEGV, SIGILL, SIGFPE, SIGABRT,
		SIGTERM, SIGINT, SIGQUIT, SIGHUP, SIGTRAP };

	Supexec(super_tos_version);
	if (tos_version < 0x200) return FALSE;	/* no kbdvec before TOS 2.0 */
	cpu = 0;
	Getcookie(C__CPU, &cpu);
	mint = Getcookie(C_MiNT, &v) == C_FOUND;

	if (mint)
		hook_mem = (char *)Mxalloc(size, MX_PREFTTRAM | MX_MPROT | MX_GLOBAL);
	else
		hook_mem = (char *)Malloc(size);
	if ((long)hook_mem <= 0) {
		hook_mem = NULL;
		return FALSE;
	}
	memcpy(hook_mem, ikbd_hook_start, size);
	kdown = (volatile unsigned char *)(hook_mem + HOOK_OFF(ikbd_hook_down));
	khit = (volatile unsigned char *)(hook_mem + HOOK_OFF(ikbd_hook_hit));
	memset((void *)kdown, 0, 128);
	memset((void *)khit, 0, 128);
	kjoy = (volatile unsigned char *)(hook_mem + HOOK_OFF(ikbd_hook_joy));
	*kjoy = 0;
	hook_handler = (long)(hook_mem + HOOK_OFF(ikbd_hook_handler));
	joy_handler = (long)(hook_mem + HOOK_OFF(ikbd_hook_joy_handler));
	kbdvec = (long *)Kbdvbase() - 1;
	joyvec = (long *)&((_KBDVECS *)Kbdvbase())->joyvec;

	unshift = ((_KEYTAB *)Keytbl((void *)-1, (void *)-1, (void *)-1))->unshift;
	for (sc = 0; sc < 128; sc++)
		scan_sym[sc] = key_for_scancode(sc, (unsigned char)unshift[sc]);

	/* Removal paths first, then the hook */
	ikbd_term_old = (long)Setexc(0x102, (void (*)())ikbd_term_stub);
	term_hooked = TRUE;
	atexit(ikbd_remove);
	for (sc = 0; sc < (int)(sizeof(sigs) / sizeof(*sigs)); sc++)
		signal(sigs[sc], on_signal);
	hook_active = TRUE;
	Supexec(super_install);
	return TRUE;
}

/* Wait (up to two seconds) until no key is held, so that the key which
 * opened a dialog cannot reach it, e.g. the Return that ran LOAD "" */
static void wait_keys_released(void) {
	int tries, sc;

	if (!hook_active) return;
	for (tries = 0; tries < 100; tries++) {
		for (sc = 1; sc < 128 && !kdown[sc]; sc++);
		if (sc == 128) break;
		evnt_timer(20);
	}
}

/* Keys typed into the emulator are also queued for us by the BIOS and the
 * AES. Drop them, or a selector starts by typing them. Only keyboard events
 * are taken from the AES, so no message is lost. */
static void flush_keys(void) {
	short msg[8], d, key;

	while (Bconstat(2)) Bconin(2);
	while (evnt_multi(MU_KEYBD | MU_TIMER, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, msg, 0, &d, &d, &d, &d, &key, &d) & MU_KEYBD);
}

/***************************************************************************
 * Key events from the IKBD hook                                           *
 ***************************************************************************/

static unsigned char key_prev[128];	/* key state at the previous pump */

/* SDL's GEM driver guesses releases from the AES key repeat: drop its key
 * events and use the hook's. Events sz81 pushes itself bypass the filter. */
static int SDLCALL drop_sdl_keys(const SDL_Event *event) {
	return event->type != SDL_KEYDOWN && event->type != SDL_KEYUP;
}

static int is_modifier(int sc) {
	return sc == SC_LSHIFT || sc == SC_RSHIFT || sc == SC_CTRL ||
		sc == SC_ALT || sc == SC_CAPS;
}

static void set_mod_state(const unsigned char *down) {
	int mod = KMOD_NONE;

	if (down[SC_LSHIFT]) mod |= KMOD_LSHIFT;
	if (down[SC_RSHIFT]) mod |= KMOD_RSHIFT;
	if (down[SC_CTRL]) mod |= KMOD_LCTRL;
	if (down[SC_ALT]) mod |= KMOD_LALT;
	SDL_SetModState((SDLMod)mod);
}

static void push_key(int sc, int pressed) {
	SDL_Event event;

	if (scan_sym[sc] == SDLK_UNKNOWN) return;
	memset(&event, 0, sizeof(event));
	event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
	event.key.keysym.scancode = sc;
	event.key.keysym.sym = scan_sym[sc];
	event.key.keysym.mod = SDL_GetModState();
	SDL_PushEvent(&event);
}

static int menu_pick;	/* menu item waiting to be acted on, 0 if none */

static int menu_shortcut(int sc);
static short our_top_window(void);
static short kb_handle, ab_handle;
static int ab_close_pending;
static int dlg_any_open(void);
static int dlg_is_window(short handle);
static void dlg_key(int sc, const unsigned char *down);
static int dlg_wants_shift(void);
static void joy_pump(int focus);

/* Turn the key changes since the last pump into SDL key events: modifier
 * presses first, so Shift+P reaches the ZX81 as Shift+P, and modifier
 * releases last. A key pressed and released between two pumps still gives
 * a press and a release. */
static void ikbd_pump(void) {
	unsigned char now[128], hit[128];
	int sc, focus;
	short top;

	/* The emulator keeps the keys while its keyboard window is on top. The
	 * About window takes them: Return, Enter and Escape close it. The
	 * Settings dialog gets them through CF-Lib, Shift too while a box of
	 * its Joystick page waits for a key */
	top = our_top_window();
	if (top >= 0 && top == ab_handle) {
		if (khit[SC_RETURN] || khit[SC_ENTER] || khit[SC_ESC]) ab_close_pending = TRUE;
		for (sc = 1; sc < 128; sc++)
			if (khit[sc] && kdown[SC_ALT] && scan_sym[sc] == SDLK_o) ab_close_pending = TRUE;
		focus = FALSE;
	} else if (top >= 0 && dlg_is_window(top)) {
		for (sc = 0; sc < 128; sc++)
			now[sc] = kdown[sc] ? 1 : 0;
		for (sc = 1; sc < 128; sc++)
			if (khit[sc] && (!is_modifier(sc) ||
				(dlg_wants_shift() && (sc == SC_LSHIFT || sc == SC_RSHIFT))))
				dlg_key(sc, now);
		focus = FALSE;
	} else {
		focus = (SDL_GetAppState() & SDL_APPINPUTFOCUS) != 0
			|| (top >= 0 && top == kb_handle);
	}
	for (sc = 0; sc < 128; sc++) {
		now[sc] = kdown[sc] ? 1 : 0;
		hit[sc] = khit[sc] ? 1 : 0;
		khit[sc] = 0;
		/* Keys typed into other applications' windows are not ours */
		if (!focus) now[sc] = hit[sc] = 0;
	}

	for (sc = 1; sc < 128; sc++) {
		if (is_modifier(sc) && hit[sc]) {
			if (key_prev[sc]) push_key(sc, FALSE);
			push_key(sc, TRUE);
		}
	}
	set_mod_state(now);

	for (sc = 1; sc < 128; sc++) {
		if (is_modifier(sc)) continue;
		if (hit[sc] && now[SC_CTRL] && menu_shortcut(sc)) {
			now[sc] = 0;
			continue;
		}
		if (hit[sc]) {
			if (key_prev[sc]) push_key(sc, FALSE);
			push_key(sc, TRUE);
			if (!now[sc]) push_key(sc, FALSE);
		} else if (key_prev[sc] && !now[sc]) {
			push_key(sc, FALSE);
		}
	}

	for (sc = 1; sc < 128; sc++) {
		if (is_modifier(sc) && !now[sc] && (key_prev[sc] || hit[sc]))
			push_key(sc, FALSE);
	}

	memcpy(key_prev, now, sizeof(key_prev));
	joy_pump(focus);
}

/* After a dialog: the keys used in it are not for the emulator */
static void ikbd_resync(void) {
	int sc;

	if (!hook_active) return;
	for (sc = 0; sc < 128; sc++) {
		khit[sc] = 0;
		key_prev[sc] = kdown[sc] ? 1 : 0;
	}
}

/***************************************************************************
 * Keyboard window                                                         *
 ***************************************************************************/
/* Shows the keyboard of the current machine. The pictures are EightyOne's
 * (data/kbzx80.bmp, kbzx81.bmp, 8-bit BMPs). Each colour becomes a one-plane
 * mask drawn with vrt_cpyfm in its own pen over a black fill, which works in
 * every screen depth. With more than 256 colours every colour of the picture
 * gets its own pen; with a palette the pens are the standard VDI colours, so
 * other applications' colours stay as they are. In monochrome the keys are
 * white on black: a label is white on the background and black on a key,
 * which a scan along each row tells apart.
 *
 * The keys can be clicked: a click is a key press for as long as the mouse
 * button is down, SHIFT stays down until the next key has been clicked.
 * Every key the emulated machine sees down (keyports[], whether typed,
 * clicked or from sz81's own virtual keyboard) is lit and stays so for a
 * second after it went up: yellow on the ZX81, dark blue on the ZX80 (white
 * lettering), inverted in monochrome. The colour covers the key face
 * colour only, so the labels stay readable. */

#define KB_MAX_PENS 64

static const struct {
	Uint8 r, g, b;
	short pen;
} kb_base[] = {
	{   0,   0,   0, G_BLACK },
	{ 255, 255, 255, G_WHITE },
	{ 252,   0,   0, G_RED },
	{  72,  42, 254, G_BLUE },
	{ 255, 201,  33, G_YELLOW },
};
#define KB_NBASE ((int)(sizeof(kb_base) / sizeof(*kb_base)))

static short kb_handle = -1;	/* window handle, -1 = closed */
static short vdi_handle;				/* our virtual workstation, 0 = not open */
static int vdi_planes;
static int kb_machine = -1;		/* KB_ZX80/KB_ZX81 of the loaded picture */

/* Only the ZX80 with its original 4K ROM has the ZX80 keyboard; the 8K ROM
 * upgrade came with the ZX81 layout */
#define KB_ZX81 0
#define KB_ZX80 1
#define KB_PICTURE() (zx80 == 1 ? KB_ZX80 : KB_ZX81)
static int kb_w, kb_h, kb_words;
static int kb_npens;
static short kb_pen[KB_MAX_PENS];
static unsigned short *kb_mask[KB_MAX_PENS];
static char kb_title[64];
static GRECT kb_pos;			/* where the window was when closed */
static int kb_pos_set;

/* The key faces in the pictures (x, y, w, h; measured from them), in the
 * rows of the ZX keyboard: 1-0, Q-P, A-NEWLINE, SHIFT-SPACE */
#define KB_KEYS 40
#define KB_SHIFT 30
#define KB_LIT_MS 1000
#define KB_CLICK_MS 80		/* shortest press of a click: the ROM must see it */
static const short kb_keys[2][KB_KEYS][4] = {
	{ /* kbzx81 */
		{ 5, 17, 36, 33 }, { 51, 17, 37, 33 }, { 97, 17, 38, 33 }, { 145, 17, 37, 33 }, { 190, 17, 38, 33 }, { 238, 17, 38, 33 }, { 286, 17, 37, 33 }, { 331, 17, 37, 33 }, { 380, 17, 37, 33 }, { 427, 17, 36, 33 },
		{ 30, 64, 36, 33 }, { 75, 64, 38, 33 }, { 122, 64, 38, 33 }, { 169, 64, 37, 33 }, { 215, 64, 37, 33 }, { 263, 64, 37, 33 }, { 311, 64, 37, 33 }, { 357, 64, 37, 33 }, { 402, 64, 38, 33 }, { 450, 64, 36, 33 },
		{ 40, 120, 37, 33 }, { 87, 120, 37, 33 }, { 134, 120, 38, 33 }, { 181, 120, 37, 33 }, { 227, 120, 38, 33 }, { 274, 120, 38, 33 }, { 320, 120, 37, 33 }, { 367, 120, 37, 33 }, { 415, 120, 37, 33 }, { 462, 120, 36, 33 },
		{ 19, 176, 36, 33 }, { 62, 176, 38, 33 }, { 111, 176, 37, 33 }, { 158, 176, 37, 33 }, { 205, 176, 38, 33 }, { 250, 176, 38, 33 }, { 297, 176, 36, 33 }, { 344, 176, 38, 33 }, { 391, 175, 37, 33 }, { 438, 175, 36, 33 },
	},
	{ /* kbzx80 */
		{ 4, 16, 36, 33 }, { 50, 16, 37, 33 }, { 96, 16, 38, 33 }, { 144, 16, 37, 33 }, { 189, 16, 38, 33 }, { 237, 16, 38, 33 }, { 285, 16, 37, 33 }, { 330, 16, 37, 33 }, { 379, 16, 37, 33 }, { 426, 16, 36, 33 },
		{ 28, 71, 36, 33 }, { 73, 71, 38, 33 }, { 120, 71, 38, 33 }, { 167, 71, 37, 33 }, { 213, 71, 37, 33 }, { 261, 71, 37, 33 }, { 309, 71, 37, 33 }, { 355, 71, 37, 33 }, { 400, 71, 38, 33 }, { 448, 71, 36, 33 },
		{ 38, 127, 37, 33 }, { 85, 127, 37, 33 }, { 132, 127, 38, 33 }, { 179, 127, 37, 33 }, { 225, 127, 38, 33 }, { 272, 127, 38, 33 }, { 318, 127, 37, 33 }, { 365, 127, 37, 33 }, { 413, 127, 37, 33 }, { 460, 127, 36, 33 },
		{ 17, 183, 36, 33 }, { 60, 183, 38, 33 }, { 109, 183, 37, 33 }, { 156, 183, 37, 33 }, { 203, 183, 38, 33 }, { 248, 183, 38, 33 }, { 295, 183, 36, 33 }, { 342, 183, 38, 33 }, { 389, 182, 37, 33 }, { 436, 182, 36, 33 },
	},
};

/* The PC keys sz81 reads for the ZX keys (sdl_main.c check_events) */
static const SDLKey kb_syms[KB_KEYS] = {
	SDLK_1, SDLK_2, SDLK_3, SDLK_4, SDLK_5, SDLK_6, SDLK_7, SDLK_8, SDLK_9, SDLK_0,
	SDLK_q, SDLK_w, SDLK_e, SDLK_r, SDLK_t, SDLK_y, SDLK_u, SDLK_i, SDLK_o, SDLK_p,
	SDLK_a, SDLK_s, SDLK_d, SDLK_f, SDLK_g, SDLK_h, SDLK_j, SDLK_k, SDLK_l, SDLK_RETURN,
	SDLK_LSHIFT, SDLK_z, SDLK_x, SDLK_c, SDLK_v, SDLK_b, SDLK_n, SDLK_m, SDLK_PERIOD, SDLK_SPACE,
};

static short kb_slot_base[KB_MAX_PENS];	/* kb_base entry of each mask */
static short kb_key_base[KB_KEYS];		/* kb_base entry of each key face */
static short kb_lit_pen;				/* pen of a lit key */
static unsigned char kb_lit[KB_KEYS];	/* drawn in yellow */
static Uint32 kb_lit_until[KB_KEYS];
static int kb_click = -1;				/* key held by the mouse */
static int kb_shift_latched;
static Uint32 kb_click_until;

/* The mouse, read once a frame for all our windows. mouse_click is a new
 * press of the left button: one that SDL's GEM driver got from the AES
 * since the last frame (button_hook(); XaAES gives a click only to a
 * program that waits for it, and a click shorter than a frame is not seen
 * by graf_mkstate()), or the button down now and not in the last frame.
 * The window that takes a click clears mouse_click. */
static short mouse_x, mouse_y, mouse_b, mouse_k, mouse_prev;
static int mouse_click;
static volatile int hook_click;
static short hook_x, hook_y;

/* Only in our SDL build, as SDL_AtariGem_SetMessageHook() */
extern void SDL_AtariGem_SetButtonHook(void (*hook)(short x, short y,
	short state, short kstate, short clicks)) __attribute__((weak));

static void button_hook(short x, short y, short state, short kstate, short clicks) {
	(void)state; (void)kstate; (void)clicks;
	hook_x = x;
	hook_y = y;
	hook_click = TRUE;
}

static void mouse_poll(void) {
	graf_mkstate(&mouse_x, &mouse_y, &mouse_b, &mouse_k);
	mouse_click = hook_click || ((mouse_b & 1) && !(mouse_prev & 1));
	if (hook_click) {
		mouse_x = hook_x;		/* where it was pressed */
		mouse_y = hook_y;
		hook_click = FALSE;
	}
	mouse_prev = mouse_b;
}

/* After a modal action, and when a window opens from the menu: a button
 * held now is no new press */
static void mouse_flush(void) {
	graf_mkstate(&mouse_x, &mouse_y, &mouse_b, &mouse_k);
	hook_click = FALSE;
	mouse_prev = 1;
	mouse_click = FALSE;
}

static void kb_free(void) {
	int i;

	for (i = 0; i < kb_npens; i++) free(kb_mask[i]);
	kb_npens = 0;
	kb_machine = -1;
}

static int kb_nearest_base(const SDL_Color *c) {
	long best = 0x7fffffffL, d;
	int i, found = 0;

	for (i = 0; i < KB_NBASE; i++) {
		d = (long)(c->r - kb_base[i].r) * (c->r - kb_base[i].r)
			+ (long)(c->g - kb_base[i].g) * (c->g - kb_base[i].g)
			+ (long)(c->b - kb_base[i].b) * (c->b - kb_base[i].b);
		if (d < best) {
			best = d;
			found = i;
		}
	}
	return found;
}

/* On exit: returns TRUE if the current machine's picture is loaded */
static int kb_load(void) {
	char filename[256];
	SDL_Surface *bmp;
	SDL_Palette *pal;
	short slot[256], base_of[256], rgb[3];
	long used[256], face_count[KB_NBASE];
	int i, x, y, s, face, base, pen, on_key;
	Uint8 *row;

	if (kb_machine == KB_PICTURE()) return TRUE;
	kb_free();

	strcpy(filename, PACKAGE_DATA_DIR);
	strcatdelimiter(filename);
	strcat(filename, KB_PICTURE() == KB_ZX80 ? "kbzx80.bmp" : "kbzx81.bmp");
	if ((bmp = SDL_LoadBMP(filename)) == NULL) return FALSE;
	pal = bmp->format->palette;
	if (bmp->format->BitsPerPixel != 8 || pal == NULL) {
		SDL_FreeSurface(bmp);
		return FALSE;
	}
	kb_w = bmp->w;
	kb_h = bmp->h;
	kb_words = (kb_w + 15) / 16;

	SDL_LockSurface(bmp);
	memset(used, 0, sizeof(used));
	for (y = 0; y < kb_h; y++) {
		row = (Uint8 *)bmp->pixels + y * bmp->pitch;
		for (x = 0; x < kb_w; x++) used[row[x]]++;
	}

	/* The key face colour is the most used one after black */
	memset(face_count, 0, sizeof(face_count));
	for (i = 0; i < 256 && i < pal->ncolors; i++)
		face_count[kb_nearest_base(&pal->colors[i])] += used[i];
	face = 1;
	for (i = 1; i < KB_NBASE; i++)
		if (face_count[i] > face_count[face]) face = i;

	/* Give every used colour a mask; black is the background fill */
	for (i = 0; i < 256; i++) {
		slot[i] = -1;
		if (!used[i] || i >= pal->ncolors) continue;
		base = base_of[i] = kb_nearest_base(&pal->colors[i]);
		if (vdi_planes > 8) {
			if (!pal->colors[i].r && !pal->colors[i].g && !pal->colors[i].b) continue;
			pen = 16 + kb_npens;
		} else if (vdi_planes == 1) {
			if (base == 0) continue;
			pen = G_WHITE;
		} else {
			if (base == 0) continue;
			pen = kb_base[base].pen;
		}
		for (s = 0; s < kb_npens && kb_pen[s] != pen; s++)
			;
		if (s == kb_npens) {
			if (s == KB_MAX_PENS) continue;
			kb_mask[s] = calloc(kb_words * 2L, kb_h);
			if (kb_mask[s] == NULL) break;
			kb_pen[s] = pen;
			kb_slot_base[s] = base;
			kb_npens++;
			if (vdi_planes > 8) {
				rgb[0] = pal->colors[i].r * 1000L / 255;
				rgb[1] = pal->colors[i].g * 1000L / 255;
				rgb[2] = pal->colors[i].b * 1000L / 255;
				vs_color(vdi_handle, pen, rgb);
			}
		}
		slot[i] = s;
	}

	for (y = 0; y < kb_h; y++) {
		row = (Uint8 *)bmp->pixels + y * bmp->pitch;
		on_key = FALSE;
		for (x = 0; x < kb_w; x++) {
			if ((s = slot[row[x]]) < 0) {
				on_key = FALSE;
				continue;
			}
			if (vdi_planes == 1) {
				if (base_of[row[x]] == face) on_key = TRUE;
				else if (on_key) continue;
			}
			kb_mask[s][y * kb_words + (x >> 4)] |= 0x8000 >> (x & 15);
		}
	}

	/* The face of a key is its most used colour other than black */
	for (i = 0; i < KB_KEYS; i++) {
		const short *k = kb_keys[KB_PICTURE()][i];
		long count[KB_NBASE];
		int b;

		memset(count, 0, sizeof(count));
		for (y = k[1]; y < k[1] + k[3] && y < kb_h; y++) {
			row = (Uint8 *)bmp->pixels + y * bmp->pitch;
			for (x = k[0]; x < k[0] + k[2] && x < kb_w; x++)
				if (slot[row[x]] >= 0) count[base_of[row[x]]]++;
		}
		kb_key_base[i] = 1;
		for (b = 1; b < KB_NBASE; b++)
			if (count[b] > count[kb_key_base[i]]) kb_key_base[i] = b;
		kb_lit[i] = FALSE;
		kb_lit_until[i] = 0;
	}
	/* Yellow on the ZX81; the ZX80's white lettering needs dark blue */
	if (vdi_planes > 8) {
		if (KB_PICTURE() == KB_ZX80) {
			rgb[0] = 0;
			rgb[1] = 0;
			rgb[2] = 450;
		} else {
			rgb[0] = 1000;
			rgb[1] = 900;
			rgb[2] = 0;
		}
		kb_lit_pen = 16 + KB_MAX_PENS;
		vs_color(vdi_handle, kb_lit_pen, rgb);
	} else {
		kb_lit_pen = KB_PICTURE() == KB_ZX80 ? G_LBLUE : G_YELLOW;
	}
	SDL_UnlockSurface(bmp);
	SDL_FreeSurface(bmp);

	kb_machine = KB_PICTURE();
	strncpy(kb_title, rs(kb_machine == KB_ZX80 ? FS_KB_ZX80 : FS_KB_ZX81), sizeof(kb_title) - 1);
	return TRUE;
}

/* Draw a picture made of masks with its top left corner at x, y; r is the
 * screen rectangle to draw, the clipping is already set to it */
static void masks_draw(unsigned short *const *mask, const short *pen, int n,
	int w, int h, int words, int x, int y, const GRECT *r) {
	MFDB src, dst;
	short pxy[8], col[2];
	int s;

	memset(&dst, 0, sizeof(dst));
	src.fd_w = words * 16;
	src.fd_h = h;
	src.fd_wdwidth = words;
	src.fd_stand = 0;
	src.fd_nplanes = 1;
	src.fd_r1 = src.fd_r2 = src.fd_r3 = 0;

	/* Source: the part of the picture under r */
	pxy[0] = r->g_x - x;
	pxy[1] = r->g_y - y;
	pxy[2] = pxy[0] + r->g_w - 1;
	pxy[3] = pxy[1] + r->g_h - 1;
	pxy[4] = r->g_x;
	pxy[5] = r->g_y;
	if (pxy[0] < 0) {
		pxy[4] -= pxy[0];
		pxy[0] = 0;
	}
	if (pxy[1] < 0) {
		pxy[5] -= pxy[1];
		pxy[1] = 0;
	}
	if (pxy[2] >= w) pxy[2] = w - 1;
	if (pxy[3] >= h) pxy[3] = h - 1;
	if (pxy[0] > pxy[2] || pxy[1] > pxy[3]) return;
	pxy[6] = pxy[4] + pxy[2] - pxy[0];
	pxy[7] = pxy[5] + pxy[3] - pxy[1];

	col[1] = G_WHITE;
	for (s = 0; s < n; s++) {
		src.fd_addr = mask[s];
		col[0] = pen[s];
		vrt_cpyfm(vdi_handle, MD_TRANS, pxy, &src, &dst, col);
	}
}

/* The lit keys inside r (already clipped to it): their face colour masks in
 * kb_lit_pen, in monochrome the key inverted */
static void kb_draw_lit(const GRECT *work, const GRECT *r) {
	GRECT kr;
	short pxy[4];
	int i, s;

	for (i = 0; i < KB_KEYS; i++) {
		if (!kb_lit[i]) continue;
		kr.g_x = work->g_x + kb_keys[kb_machine][i][0];
		kr.g_y = work->g_y + kb_keys[kb_machine][i][1];
		kr.g_w = kb_keys[kb_machine][i][2];
		kr.g_h = kb_keys[kb_machine][i][3];
		if (!rc_intersect(r, &kr)) continue;
		if (vdi_planes == 1) {
			pxy[0] = kr.g_x;
			pxy[1] = kr.g_y;
			pxy[2] = kr.g_x + kr.g_w - 1;
			pxy[3] = kr.g_y + kr.g_h - 1;
			vswr_mode(vdi_handle, MD_XOR);
			vsf_color(vdi_handle, G_BLACK);
			vr_recfl(vdi_handle, pxy);
			vswr_mode(vdi_handle, MD_REPLACE);
			continue;
		}
		for (s = 0; s < kb_npens; s++)
			if (kb_slot_base[s] == kb_key_base[i])
				masks_draw(&kb_mask[s], &kb_lit_pen, 1, kb_w, kb_h, kb_words,
					work->g_x, work->g_y, &kr);
	}
}

static void kb_draw(const GRECT *area) {
	GRECT work, r;
	short pxy[4];

	wind_get_grect(kb_handle, WF_WORKXYWH, &work);

	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get_grect(kb_handle, WF_FIRSTXYWH, &r);
	while (r.g_w > 0 && r.g_h > 0) {
		if (rc_intersect(area, &r) && rc_intersect(&work, &r)) {
			pxy[0] = r.g_x;
			pxy[1] = r.g_y;
			pxy[2] = r.g_x + r.g_w - 1;
			pxy[3] = r.g_y + r.g_h - 1;
			vs_clip(vdi_handle, 1, pxy);
			vswr_mode(vdi_handle, MD_REPLACE);
			vsf_interior(vdi_handle, FIS_SOLID);
			vsf_color(vdi_handle, G_BLACK);
			vr_recfl(vdi_handle, pxy);
			masks_draw(kb_mask, kb_pen, kb_npens, kb_w, kb_h, kb_words,
				work.g_x, work.g_y, &r);
			kb_draw_lit(&work, &r);
		}
		wind_get_grect(kb_handle, WF_NEXTXYWH, &r);
	}
	vs_clip(vdi_handle, 0, pxy);
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
}

static void kb_release_click(void);

static void kb_close(void) {
	if (kb_handle < 0) return;
	kb_release_click();
	wind_get_grect(kb_handle, WF_CURRXYWH, &kb_pos);
	kb_pos_set = TRUE;
	wind_close(kb_handle);
	wind_delete(kb_handle);
	kb_handle = -1;
}

static void ab_close(void);
static void ab_free(void);
static void dlg_close_all(void);

static void windows_exit(void) {
	kb_close();
	kb_free();
	ab_close();
	ab_free();
	dlg_close_all();
	if (vdi_handle) v_clsvwk(vdi_handle);
	vdi_handle = 0;
}

/* On exit: returns TRUE if our VDI workstation is open */
static int vdi_open(void) {
	short work_in[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 }, work_out[57], dummy;

	if (vdi_handle) return TRUE;
	vdi_handle = graf_handle(&dummy, &dummy, &dummy, &dummy);
	v_opnvwk(work_in, &vdi_handle, work_out);
	if (!vdi_handle) {
		rs_alert(AL_NOVDI);
		return FALSE;
	}
	vq_extnd(vdi_handle, 1, work_out);
	vdi_planes = work_out[4];
	atexit(windows_exit);
	return TRUE;
}

static void kb_open(void) {
	GRECT desk, outer;

	if (kb_handle >= 0) {
		wind_set(kb_handle, WF_TOP, 0, 0, 0, 0);
		return;
	}
	if (!vdi_open()) return;
	if (!kb_load()) {
		rs_alert(AL_NOKBPIC);
		return;
	}

	wind_get_grect(0, WF_WORKXYWH, &desk);
	if ((kb_handle = wind_create_grect(NAME | CLOSER | MOVER, &desk)) < 0) {
		rs_alert(AL_NOWINDOW);
		return;
	}
	wind_set_str(kb_handle, WF_NAME, kb_title);
	if (kb_pos_set) {
		outer = kb_pos;
	} else {
		/* Centred at the bottom of the desktop */
		outer.g_x = outer.g_y = 0;
		outer.g_w = kb_w;
		outer.g_h = kb_h;
		wind_calc_grect(WC_BORDER, NAME | CLOSER | MOVER, &outer, &outer);
		outer.g_x = desk.g_x + (desk.g_w - outer.g_w) / 2;
		outer.g_y = desk.g_y + desk.g_h - outer.g_h;
		if (outer.g_x < desk.g_x) outer.g_x = desk.g_x;
		if (outer.g_y < desk.g_y) outer.g_y = desk.g_y;
	}
	wind_open_grect(kb_handle, &outer);
}

/* Machine changed: show the other keyboard if its layout differs */
static void kb_check_machine(void) {
	GRECT work;

	if (kb_handle < 0 || kb_machine == KB_PICTURE()) return;
	kb_release_click();
	if (!kb_load()) {
		kb_close();
		return;
	}
	wind_set_str(kb_handle, WF_NAME, kb_title);
	wind_get_grect(kb_handle, WF_WORKXYWH, &work);
	kb_draw(&work);
}

/* A ZX key as an SDL key event, the way sz81 reads the PC keyboard */
static void kb_send(int key, int pressed) {
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
	event.key.keysym.sym = kb_syms[key];
	event.key.keysym.mod = SDL_GetModState();
	SDL_PushEvent(&event);
}

/* The mouse no longer holds a key: release it and a latched SHIFT */
static void kb_release_click(void) {
	if (kb_click >= 0) kb_send(kb_click, FALSE);
	kb_click = -1;
	if (kb_shift_latched) kb_send(KB_SHIFT, FALSE);
	kb_shift_latched = FALSE;
}

/* On exit: returns TRUE if the emulated machine sees the key down. The
 * half-rows of the matrix: 0 SHIFT-V, 1 A-G, 2 Q-T, 3 1-5 (bits 0-4 from
 * the left), 4 0-6, 5 P-Y, 6 NEWLINE-H, 7 SPACE-B (bits 0-4 from the right) */
static int kb_key_down(int key) {
	int row = key / 10, col = key % 10;
	int port = col < 5 ? 3 - row : 4 + row;
	int bit = col < 5 ? col : 9 - col;

	return !(keyports[port] & (1 << bit));
}

/* On exit: returns the key under the screen position, -1 if none */
static int kb_key_at(short mx, short my) {
	GRECT work;
	int i, x, y;
	const short *k;

	wind_get_grect(kb_handle, WF_WORKXYWH, &work);
	x = mx - work.g_x;
	y = my - work.g_y;
	for (i = 0; i < KB_KEYS; i++) {
		k = kb_keys[kb_machine][i];
		if (x >= k[0] && x < k[0] + k[2] && y >= k[1] && y < k[1] + k[3]) return i;
	}
	return -1;
}

/* Every frame: clicks on the keys and the yellow keys */
static void kb_poll(void) {
	short mx, my, mb;
	GRECT work, kr;
	Uint32 now;
	int i, lit;

	if (kb_handle < 0 || kb_machine < 0) return;
	now = SDL_GetTicks();

	mx = mouse_x;
	my = mouse_y;
	mb = mouse_b;
	if (mouse_click && kb_click < 0 &&
		wind_find(mx, my) == kb_handle && (i = kb_key_at(mx, my)) >= 0) {
		mouse_click = FALSE;
		if (i == KB_SHIFT) {
			/* SHIFT stays down until the next key */
			kb_shift_latched = !kb_shift_latched;
			kb_send(KB_SHIFT, kb_shift_latched);
		} else {
			kb_send(i, TRUE);
			kb_click = i;
			kb_click_until = now + KB_CLICK_MS;
		}
	}
	if (!(mb & 1) && kb_click >= 0 && (Sint32)(now - kb_click_until) >= 0)
		kb_release_click();

	wind_get_grect(kb_handle, WF_WORKXYWH, &work);
	for (i = 0; i < KB_KEYS; i++) {
		if (kb_key_down(i) || i == kb_click || (i == KB_SHIFT && kb_shift_latched))
			kb_lit_until[i] = now + KB_LIT_MS;
		lit = (Sint32)(kb_lit_until[i] - now) > 0;
		if (lit == kb_lit[i]) continue;
		kb_lit[i] = lit;
		kr.g_x = work.g_x + kb_keys[kb_machine][i][0];
		kr.g_y = work.g_y + kb_keys[kb_machine][i][1];
		kr.g_w = kb_keys[kb_machine][i][2];
		kr.g_h = kb_keys[kb_machine][i][3];
		kb_draw(&kr);
	}
}

/* On exit: returns the handle of the top window if it is one of ours,
 * else -1 */
static short our_top_window(void) {
	short top, dummy;

	if (kb_handle < 0 && ab_handle < 0 && !dlg_any_open()) return -1;
	wind_get(0, WF_TOP, &top, &dummy, &dummy, &dummy);
	return top;
}

/* The window messages of our own windows
 * On exit: returns TRUE if the message was for this window */
static int window_message(const short *message, short handle,
	void (*draw)(const GRECT *), void (*close)(void)) {
	if (handle < 0 || message[0] < WM_REDRAW || message[0] > WM_TOOLBAR
		|| message[3] != handle)
		return FALSE;

	switch (message[0]) {
		case WM_REDRAW:
			draw((const GRECT *)&message[4]);
			break;
		case WM_TOPPED:
			wind_set(handle, WF_TOP, 0, 0, 0, 0);
			break;
		case WM_BOTTOMED:
			wind_set(handle, WF_BOTTOM, 0, 0, 0, 0);
			break;
		case WM_MOVED:
			wind_set_grect(handle, WF_CURRXYWH, (const GRECT *)&message[4]);
			break;
		case WM_CLOSED:
			close();
			break;
	}
	return TRUE;
}

/***************************************************************************
 * About window                                                            *
 ***************************************************************************/
/* A photo of a ZX81 (data/zx81.bmp, made by atari/mkabout.py) at the bottom
 * left, the name in ZX81 block graphics at its top right, the text below
 * the name and an OK button at the bottom right. The photo is drawn like
 * the keyboard: with more than 256 colours each colour gets its own pen
 * (after the keyboard's), otherwise it is dithered to the first 16 (or
 * fewer) VDI pens. White is left to the dialog's background. */

#define AB_PEN0 128			/* first pen of the photo with > 256 colours */
#define AB_MAX_PENS 64
#define AB_QUAD 8			/* one block graphics quarter cell, in pixels */
#define AB_MARGIN 16
#define AB_PIC_X 8

/* "sz81" in ZX81 quarter cells */
#define AB_GLYPH_W 4
#define AB_GLYPH_H 7
static const char *const ab_glyphs[4][AB_GLYPH_H] = {
	{ "....", "....", ".###", "#...", ".##.", "...#", "###." },
	{ "....", "....", "####", "..#.", ".#..", "#...", "####" },
	{ ".##.", "#..#", "#..#", ".##.", "#..#", "#..#", ".##." },
	{ "..#.", ".##.", "..#.", "..#.", "..#.", "..#.", ".###" },
};
#define AB_NAME_W ((4 * AB_GLYPH_W + 3 * 2) * AB_QUAD)
#define AB_NAME_H (AB_GLYPH_H * AB_QUAD)

/* The lines are free strings FS_AB_LINE0.. of SZ81.RSC, the first two
 * followed by the version numbers, the fifth after the sz81 version. An
 * empty line is half a line high */
#define AB_NLINES 10
static char ab_text[AB_NLINES][80];
static char *ab_lines[AB_NLINES];

static void ab_texts(void) {
	int i;

	for (i = 0; i < AB_NLINES; i++) {
		const char *t = rs(FS_AB_LINE0 + i);

		if (i == 0) snprintf(ab_text[i], sizeof(ab_text[i]), "%s%s", t, VERSION);
		else if (i == 1) snprintf(ab_text[i], sizeof(ab_text[i]), "%s%s", t, ATARI_VERSION);
		else if (i == 4) snprintf(ab_text[i], sizeof(ab_text[i]), "%s%s", VERSION, t);
		else snprintf(ab_text[i], sizeof(ab_text[i]), "%s", t);
		ab_lines[i] = ab_text[i];
	}
}


enum { AB_ROOT, AB_LINE0, AB_OK = AB_LINE0 + AB_NLINES, AB_COUNT };

static OBJECT ab_tree[AB_COUNT];
static short ab_handle = -1;
static int ab_w, ab_h, ab_words, ab_npens;
static short ab_pen[AB_MAX_PENS];
static unsigned short *ab_mask[AB_MAX_PENS];
static int ab_pic_y, ab_name_x, ab_name_y;

static void ab_free(void) {
	int i;

	for (i = 0; i < ab_npens; i++) free(ab_mask[i]);
	ab_npens = 0;
}

/* On exit: returns the mask slot of a pen, -1 if out of memory */
static int ab_slot(short pen) {
	int s;

	for (s = 0; s < ab_npens; s++)
		if (ab_pen[s] == pen) return s;
	if (s == AB_MAX_PENS) return -1;
	if ((ab_mask[s] = calloc(ab_words * 2L, ab_h)) == NULL) return -1;
	ab_pen[s] = pen;
	ab_npens++;
	return s;
}

#define AB_WHITE(r, g, b) ((r) >= 240 && (g) >= 240 && (b) >= 240)

/* On exit: returns TRUE if the photo is loaded */
static int ab_load(void) {
	char filename[256];
	SDL_Surface *bmp;
	SDL_Palette *pal;
	SDL_Color *c;
	short slot[256], rgb[3], npens, pr[16], pg[16], pb[16];
	int *err, *cur, *nxt, *tmp;
	int i, x, y, s, r, g, b, best, p;
	long d, bestd;
	Uint8 *row;

	if (ab_npens) return TRUE;
	strcpy(filename, PACKAGE_DATA_DIR);
	strcatdelimiter(filename);
	strcat(filename, "zx81.bmp");
	if ((bmp = SDL_LoadBMP(filename)) == NULL) return FALSE;
	pal = bmp->format->palette;
	if (bmp->format->BitsPerPixel != 8 || pal == NULL) {
		SDL_FreeSurface(bmp);
		return FALSE;
	}
	ab_w = bmp->w;
	ab_h = bmp->h;
	ab_words = (ab_w + 15) / 16;
	SDL_LockSurface(bmp);

	if (vdi_planes > 8) {
		/* Every colour its own pen */
		for (i = 0; i < 256; i++) {
			slot[i] = -1;
			if (i >= pal->ncolors) continue;
			c = &pal->colors[i];
			if (AB_WHITE(c->r, c->g, c->b)) continue;
			if ((s = ab_slot(AB_PEN0 + ab_npens)) < 0) continue;
			rgb[0] = c->r * 1000L / 255;
			rgb[1] = c->g * 1000L / 255;
			rgb[2] = c->b * 1000L / 255;
			vs_color(vdi_handle, ab_pen[s], rgb);
			slot[i] = s;
		}
		for (y = 0; y < ab_h; y++) {
			row = (Uint8 *)bmp->pixels + y * bmp->pitch;
			for (x = 0; x < ab_w; x++)
				if ((s = slot[row[x]]) >= 0)
					ab_mask[s][y * ab_words + (x >> 4)] |= 0x8000 >> (x & 15);
		}
	} else {
		/* Floyd-Steinberg to the pens the screen has, errors in 1/16 */
		npens = vdi_planes >= 4 ? 16 : 1 << vdi_planes;
		for (i = 0; i < npens; i++) {
			vq_color(vdi_handle, i, 1, rgb);
			pr[i] = rgb[0] * 255L / 1000;
			pg[i] = rgb[1] * 255L / 1000;
			pb[i] = rgb[2] * 255L / 1000;
		}
		err = calloc((ab_w + 2) * 6L, sizeof(int));
		if (err == NULL) {
			SDL_UnlockSurface(bmp);
			SDL_FreeSurface(bmp);
			return FALSE;
		}
		cur = err;
		nxt = err + (ab_w + 2) * 3;
		for (y = 0; y < ab_h; y++) {
			row = (Uint8 *)bmp->pixels + y * bmp->pitch;
			memset(nxt, 0, (ab_w + 2) * 3 * sizeof(int));
			for (x = 0; x < ab_w; x++) {
				c = &pal->colors[row[x] < pal->ncolors ? row[x] : 0];
				p = (x + 1) * 3;
				r = c->r + cur[p] / 16;
				g = c->g + cur[p + 1] / 16;
				b = c->b + cur[p + 2] / 16;
				r = r < 0 ? 0 : r > 255 ? 255 : r;
				g = g < 0 ? 0 : g > 255 ? 255 : g;
				b = b < 0 ? 0 : b > 255 ? 255 : b;
				best = 0;
				bestd = 0x7fffffffL;
				for (i = 0; i < npens; i++) {
					d = (long)(r - pr[i]) * (r - pr[i]) + (long)(g - pg[i]) * (g - pg[i])
						+ (long)(b - pb[i]) * (b - pb[i]);
					if (d < bestd) {
						bestd = d;
						best = i;
					}
				}
				if (!AB_WHITE(pr[best], pg[best], pb[best]) && (s = ab_slot(best)) >= 0)
					ab_mask[s][y * ab_words + (x >> 4)] |= 0x8000 >> (x & 15);
				r -= pr[best];
				g -= pg[best];
				b -= pb[best];
				cur[p + 3] += r * 7; cur[p + 4] += g * 7; cur[p + 5] += b * 7;
				nxt[p - 3] += r * 3; nxt[p - 2] += g * 3; nxt[p - 1] += b * 3;
				nxt[p] += r * 5;     nxt[p + 1] += g * 5; nxt[p + 2] += b * 5;
				nxt[p + 3] += r;     nxt[p + 4] += g;     nxt[p + 5] += b;
			}
			tmp = cur;
			cur = nxt;
			nxt = tmp;
		}
		free(err);
	}
	SDL_UnlockSurface(bmp);
	SDL_FreeSurface(bmp);
	/* A white photo still counts as loaded */
	if (!ab_npens) ab_slot(G_WHITE);
	return TRUE;
}

static void ab_obj(int i, int next, int type, int flags, int x, int y, int w, int h) {
	memset(&ab_tree[i], 0, sizeof(OBJECT));
	ab_tree[i].ob_next = next;
	ab_tree[i].ob_head = ab_tree[i].ob_tail = -1;
	ab_tree[i].ob_type = type;
	ab_tree[i].ob_flags = flags;
	ab_tree[i].ob_x = x;
	ab_tree[i].ob_y = y;
	ab_tree[i].ob_width = w;
	ab_tree[i].ob_height = h;
}

/* The dialog in pixels, from the font size and the photo */
static void ab_layout(void) {
	short cw, ch, dummy;
	int i, y, text_x, text_w, text_h, bw, bh, w, h, content;

	graf_handle(&cw, &ch, &dummy, &dummy);
	text_w = AB_NAME_W;
	text_h = 0;
	for (i = 0; i < AB_NLINES; i++) {
		if ((int)strlen(ab_lines[i]) * cw > text_w) text_w = strlen(ab_lines[i]) * cw;
		text_h += ab_lines[i][0] ? ch : ch / 2;
	}
	bw = 9 * cw;
	bh = ch + 2 < 17 ? 17 : ch + 2;		/* as in the Settings dialog */
	text_x = AB_PIC_X + ab_w + AB_MARGIN;
	content = AB_NAME_H + ch / 2 + text_h + ch / 2 + bh + AB_MARGIN / 2;
	w = text_x + text_w + AB_MARGIN;
	h = AB_MARGIN + (content > ab_h ? content : ab_h);

	/* The name starts level with the top of the photo, which sits on the
	 * bottom edge; only a large font pushes the photo further down */
	ab_pic_y = h - ab_h;
	ab_name_x = text_x;
	ab_name_y = h - (content > ab_h ? content : ab_h);

	ab_obj(AB_ROOT, -1, G_BOX, OF_NONE, 0, 0, w, h);
	ab_tree[AB_ROOT].ob_spec.index = 0x00001100L;
	ab_tree[AB_ROOT].ob_head = AB_LINE0;
	ab_tree[AB_ROOT].ob_tail = AB_OK;
	y = ab_name_y + AB_NAME_H + ch / 2;
	for (i = 0; i < AB_NLINES; i++) {
		ab_obj(AB_LINE0 + i, AB_LINE0 + i + 1, G_STRING, OF_NONE, text_x, y,
			strlen(ab_lines[i]) * cw, ch);
		ab_tree[AB_LINE0 + i].ob_spec.free_string = (char *)ab_lines[i];
		y += ab_lines[i][0] ? ch : ch / 2;
	}
	/* An exit button as in the dialogs: 3D, MagiC style with the shortcut
	 * Alt+O (fix_dial() draws it where the AES cannot) */
	ab_obj(AB_OK, AB_ROOT, G_BUTTON, OF_SELECTABLE | OF_DEFAULT | OF_EXIT | OF_FL3DACT | OF_LASTOB,
		w - AB_MARGIN - bw, h - AB_MARGIN / 2 - bh, bw, bh);
	ab_tree[AB_OK].ob_state = OS_WHITEBAK;
	ab_tree[AB_OK].ob_spec.free_string = rs(FS_AB_OK);
	fix_dial(ab_tree);
	aesfix_userdefs(ab_tree);
}

static void ab_draw(const GRECT *area) {
	GRECT work, r;
	short pxy[4];
	int i, x, y, gx, gy;

	wind_get_grect(ab_handle, WF_WORKXYWH, &work);
	ab_tree[AB_ROOT].ob_x = work.g_x;
	ab_tree[AB_ROOT].ob_y = work.g_y;

	wind_update(BEG_UPDATE);
	graf_mouse(M_OFF, NULL);
	wind_get_grect(ab_handle, WF_FIRSTXYWH, &r);
	while (r.g_w > 0 && r.g_h > 0) {
		if (rc_intersect(area, &r) && rc_intersect(&work, &r)) {
			objc_draw_grect(ab_tree, AB_ROOT, MAX_DEPTH, &r);
			pxy[0] = r.g_x;
			pxy[1] = r.g_y;
			pxy[2] = r.g_x + r.g_w - 1;
			pxy[3] = r.g_y + r.g_h - 1;
			vs_clip(vdi_handle, 1, pxy);
			masks_draw(ab_mask, ab_pen, ab_npens, ab_w, ab_h, ab_words,
				work.g_x + AB_PIC_X, work.g_y + ab_pic_y, &r);

			vswr_mode(vdi_handle, MD_REPLACE);
			vsf_interior(vdi_handle, FIS_SOLID);
			vsf_color(vdi_handle, G_BLACK);
			vsf_perimeter(vdi_handle, 0);
			for (i = 0; i < 4; i++) {
				for (gy = 0; gy < AB_GLYPH_H; gy++) {
					for (gx = 0; gx < AB_GLYPH_W; gx++) {
						if (ab_glyphs[i][gy][gx] != '#') continue;
						x = work.g_x + ab_name_x + (i * (AB_GLYPH_W + 2) + gx) * AB_QUAD;
						y = work.g_y + ab_name_y + gy * AB_QUAD;
						pxy[0] = x;
						pxy[1] = y;
						pxy[2] = x + AB_QUAD - 1;
						pxy[3] = y + AB_QUAD - 1;
						vr_recfl(vdi_handle, pxy);
					}
				}
			}
		}
		wind_get_grect(ab_handle, WF_NEXTXYWH, &r);
	}
	vs_clip(vdi_handle, 0, pxy);
	graf_mouse(M_ON, NULL);
	wind_update(END_UPDATE);
}

static void ab_close(void) {
	if (ab_handle < 0) return;
	wind_close(ab_handle);
	wind_delete(ab_handle);
	ab_handle = -1;
	ab_free();
	ikbd_resync();
}

static void ab_open(void) {
	GRECT desk, outer;

	if (ab_handle >= 0) {
		wind_set(ab_handle, WF_TOP, 0, 0, 0, 0);
		return;
	}
	if (!vdi_open()) return;
	if (!ab_load()) {
		rs_alert(AL_NOABPIC);
		return;
	}
	/* The tree once: the photo and the texts stay the same */
	if (!ab_tree[AB_ROOT].ob_width) {
		ab_texts();
		ab_layout();
	}

	wind_get_grect(0, WF_WORKXYWH, &desk);
	if ((ab_handle = wind_create_grect(NAME | CLOSER | MOVER, &desk)) < 0) {
		ab_free();
		rs_alert(AL_NOWINDOW);
		return;
	}
	wind_set_str(ab_handle, WF_NAME, rs(FS_AB_TITLE));
	outer.g_x = outer.g_y = 0;
	outer.g_w = ab_tree[AB_ROOT].ob_width;
	outer.g_h = ab_tree[AB_ROOT].ob_height;
	wind_calc_grect(WC_BORDER, NAME | CLOSER | MOVER, &outer, &outer);
	outer.g_x = desk.g_x + (desk.g_w - outer.g_w) / 2;
	outer.g_y = desk.g_y + (desk.g_h - outer.g_h) / 2;
	if (outer.g_x < desk.g_x) outer.g_x = desk.g_x;
	if (outer.g_y < desk.g_y) outer.g_y = desk.g_y;
	ab_close_pending = FALSE;
	wind_open_grect(ab_handle, &outer);
}

/* Every frame: the OK button and the keys that close the window */
static void ab_poll(void) {
	if (ab_handle < 0) return;
	if (ab_close_pending) {
		ab_close_pending = FALSE;
		ab_close();
		return;
	}
	if (!mouse_click || our_top_window() != ab_handle) return;
	if (objc_find(ab_tree, AB_ROOT, MAX_DEPTH, mouse_x, mouse_y) != AB_OK) return;
	mouse_click = FALSE;
	wind_update(BEG_MCTRL);
	if (graf_watchbox(ab_tree, AB_OK, OS_SELECTED, OS_NORMAL)) {
		wind_update(END_MCTRL);
		mouse_flush();
		ab_close();
		return;
	}
	wind_update(END_MCTRL);
	mouse_flush();
}

/***************************************************************************
 * Settings dialog (Options > Hardware..., Sound..., GUI options...,       *
 * Joystick...)                                                            *
 ***************************************************************************/
/* sz81's own options pages (F2) as one CF-Lib window dialog with tab cards
 * (atari/cftabs.c). T_SETTINGS has the title (object 1, which CF-Lib makes
 * the window name), the tab bar, an empty page frame and Cancel/OK. Every
 * page is a tree of its own (T_HARDWARE, T_SOUND, T_GUIOPTS, T_JOYSTICK);
 * tabs_init_trees() merges them into a copy of T_SETTINGS, where the page
 * objects get new numbers: PG(page, object). fix_dial() turns the MagiC
 * objects into user objects where the AES lacks them; aesfix_userdefs()
 * lets the user objects draw on a stack of our own (TOS 1.04).
 *
 * SDL's GEM driver owns evnt_multi, so the dialog is fed by hand: window
 * messages from gem_message, keys from the IKBD hook (ikbd_pump queues
 * them while the dialog is on top; CF-Lib gives them to the top window),
 * clicks by polling graf_mkstate. OK applies the settings of all pages as
 * sz81's own Save does; a change that resets the emulated machine asks
 * first. */

#define DLG_KEYS 16

#define PAGE_HW 0
#define PAGE_SOUND 1
#define PAGE_GUI 2
#define PAGE_JOY 3
#define NPAGES 4

#define PG(page, o) tabs_obj(&st_tabs, (page), (o))
#define HW(o) PG(PAGE_HW, (o))
#define SD(o) PG(PAGE_SOUND, (o))
#define GU(o) PG(PAGE_GUI, (o))
#define JS(o) PG(PAGE_JOY, (o))

extern _WORD cf_vdi_handle;

static const int page_trees[NPAGES] = { T_HARDWARE, T_SOUND, T_GUIOPTS, T_JOYSTICK };
static CFTABS st_tabs;
static OBJECT *st_tree;		/* T_SETTINGS with all pages, NULL before st_init */
static WDIALOG *st_wd;

static int dlg_keys[DLG_KEYS], dlg_nkeys;	/* keys typed into the dialog */
static short dlg_kstate;

static short button_height(void) {
	return gl_hchar + 2 < 17 ? 17 : gl_hchar + 2;
}

/* A page tree is laid out in the resource for an 8x16 font (mkrsc.py), and
 * is used as it is with such a font. With a smaller one its lines grow to
 * 16 pixels, as the 3D buttons and radio buttons need that much: the
 * positions and the frames scale, the other objects keep their height in
 * the middle of their larger cell (buttons fill it). */
static void scale_page(OBJECT *t, short parent) {
	short o;

	for (o = t[parent].ob_head; o != parent && o >= 0; o = t[o].ob_next) {
		short cell = t[o].ob_height * 16 / gl_hchar;

		t[o].ob_y = t[o].ob_y * 16 / gl_hchar;
		if (t[o].ob_head >= 0 || (t[o].ob_type & 0xFF) == G_BUTTON)
			t[o].ob_height = cell;
		else
			t[o].ob_y += (cell - t[o].ob_height) / 2;
		if (t[o].ob_head >= 0) scale_page(t, o);
	}
	if (parent == ROOT) t[ROOT].ob_height = t[ROOT].ob_height * 16 / gl_hchar;
}

/* Before fix_dial(): the title, the tab bar (tabs_init_trees() puts it on
 * the frame), the page frame with a character around the pages, all pages
 * as high as the highest, and the exit buttons below the frame */
static void layout_settings(OBJECT *t, OBJECT **pages) {
	short bh = button_height(), y, i, h, maxh = 0;

	t[ST_TITLE].ob_x = gl_wchar;
	t[ST_TITLE].ob_y = gl_hchar;
	t[ST_TITLE].ob_width = t[ROOT].ob_width - 2 * gl_wchar;
	t[ST_TITLE].ob_height = gl_hchar + 2;
	y = t[ST_TITLE].ob_y + t[ST_TITLE].ob_height + gl_hchar / 2;

	t[ST_BAR].ob_height = gl_hchar + 6;
	t[ST_PAGES].ob_x = gl_wchar;
	t[ST_PAGES].ob_y = y + t[ST_BAR].ob_height;
	t[ST_PAGES].ob_width = t[ROOT].ob_width - 2 * gl_wchar;
	for (i = 0; i < NPAGES; i++) {
		if (gl_hchar < 16) scale_page(pages[i], ROOT);
		h = pages[i][ROOT].ob_height;
		if (h > maxh) maxh = h;
	}
	for (i = 0; i < NPAGES; i++) {
		pages[i][ROOT].ob_x = pages[i][ROOT].ob_y = 0;
		pages[i][ROOT].ob_width = t[ST_PAGES].ob_width - 2 * gl_wchar;
		pages[i][ROOT].ob_height = maxh;
	}
	t[ST_PAGES].ob_height = maxh + 2 * gl_hchar;

	y = t[ST_PAGES].ob_y + t[ST_PAGES].ob_height + gl_hchar;
	t[ST_CANCEL].ob_y = t[ST_OK].ob_y = y;
	t[ST_CANCEL].ob_height = t[ST_OK].ob_height = bh;
	t[ROOT].ob_height = y + bh + gl_hchar;
}

static OBJECT *pops;		/* T_POPUPS: all popups, box PU_... */

/* On exit: returns the number of entries of a popup */
static int pop_entries(int pop) {
	int i, n = 0;

	for (i = pops[pop].ob_head; i != pop && i >= 0; i = pops[i].ob_next) n++;
	return n;
}

/* The box shows entry n of its popup (limited to the entries there are) */
static void pop_show(OBJECT *t, int box, int pop, int n) {
	char s[32];
	int last = pop_entries(pop) - 1;

	get_string(pops, pops[pop].ob_head + (n < 0 ? 0 : n > last ? last : n), s);
	set_string(t, box, s);
}

/* The entry of a popup that its box shows */
static int pop_choice(OBJECT *t, int box, int pop) {
	char s[16], u[16];
	int i;

	get_string(t, box, s);
	for (i = pops[pop].ob_head; i != pop && i >= 0; i = pops[i].ob_next) {
		get_string(pops, i, u);
		if (!strcmp(s, u)) return i - pops[pop].ob_head;
	}
	return 0;
}

/* Radio buttons first..first+n-1 */
static void radio_set(OBJECT *t, int first, int n, int on) {
	int i;

	for (i = 0; i < n; i++)
		set_state(t, first + i, OS_SELECTED, i == on);
}

static int radio_get(OBJECT *t, int first, int n) {
	int i;

	for (i = 0; i < n; i++)
		if (get_state(t, first + i, OS_SELECTED)) return i;
	return 0;
}

/* An edit field as a number; FALSE if it is empty or out of lo..hi */
static int field_int(OBJECT *t, int obj, int lo, int hi, int *v) {
	char s[16], *end;
	long n;

	get_string(t, obj, s);
	n = strtol(s, &end, 10);
	if (end == s || *end || n < lo || n > hi) return FALSE;
	*v = (int)n;
	return TRUE;
}

static void field_set_int(OBJECT *t, int obj, int v) {
	char s[16];

	sprintf(s, "%d", v);
	set_string(t, obj, s);
}

/* sz81's own dialogs keep copies of the settings, so OK waits for them
 * to be closed. On exit: returns TRUE if one is open */
static int sz81_dialog_open(void) {
	int comp = get_active_component();

	if (comp == COMP_VKEYB || comp == COMP_CTB || comp == COMP_EMU) return FALSE;
	rs_alert(AL_SZ81DIALOG);
	ikbd_resync();
	return TRUE;
}

/* The machine is reset after a change: Cancel or Reset */
static int ask_reset(int alert) {
	int answer = rs_alert(alert);

	ikbd_resync();
	return answer == 2;
}

/* A wrong value on a page: the alert, then that page */
static void page_error(int page, int alert) {
	rs_alert(alert);
	ikbd_resync();
	if (st_tabs.active != page)
		tabs_wdial(&st_tabs, st_wd, tabs_tab(&st_tabs, page));
}

/* ------------------------------------------------------------ Hardware */

/* RAM size, frameskip and speed are popups (handle_popup) whose entries
 * are in the order of hw_ram_sizes, 0..MAX_FRAMESKIP and 200/100/66/50%;
 * a click on a label steps to the next entry */
#define HW_RAM_N 10

static const int hw_ram_sizes[HW_RAM_N] = { 1, 2, 3, 4, 16, 24, 32, 40, 48, 56 };
static const int hw_models[3] = { MODEL_ZX80, MODEL_ZX80_8K, MODEL_ZX81 };
static const int hw_chrgens[3] = { CHRGENSINCLAIR, CHRGENQS, CHRGENCHR16 };

/* zx80 is 0 = ZX81, 1 = ZX80 4K, 2 = ZX80 8K; the radios are ZX80 4K,
 * ZX80 8K, ZX81 */
static void hw_fill(OBJECT *t) {
	int i, ram = 4;

	radio_set(t, HW(HW_ZX80), 3, *sdl_emulator.model == MODEL_ZX81 ? 2 :
		*sdl_emulator.model == MODEL_ZX80_8K ? 1 : 0);
	for (i = 0; i < HW_RAM_N; i++)
		if (hw_ram_sizes[i] == sdl_emulator.ramsize) ram = i;
	pop_show(t, HW(HW_RAM), PU_RAM, ram);
	set_state(t, HW(HW_M1NOT), OS_SELECTED, sdl_emulator.m1not != 0);
	pop_show(t, HW(HW_FSKIP), PU_FSKIP, sdl_emulator.frameskip);
	pop_show(t, HW(HW_SPEED), PU_SPEED, sdl_emulator.speed / 10 - 1);
	set_state(t, HW(HW_WRX), OS_SELECTED, sdl_emulator.wrx != HIRESDISABLED);
	radio_set(t, HW(HW_UDGNONE), 3, sdl_emulator.chrgen == CHRGENQS ? 1 :
		sdl_emulator.chrgen == CHRGENCHR16 ? 2 : 0);
}

/* On exit: returns TRUE if the new hardware resets the machine */
static int hw_resets(OBJECT *t) {
	return hw_models[radio_get(t, HW(HW_ZX80), 3)] != *sdl_emulator.model ||
		hw_ram_sizes[pop_choice(t, HW(HW_RAM), PU_RAM)] != sdl_emulator.ramsize ||
		(get_state(t, HW(HW_M1NOT), OS_SELECTED) ? 1 : 0) != sdl_emulator.m1not;
}

static void hw_apply(OBJECT *t) {
	int speed = (pop_choice(t, HW(HW_SPEED), PU_SPEED) + 1) * 10;

	/* sz81's component executive notices the changes and resets the
	 * machine or switches the graphics */
	*sdl_emulator.model = hw_models[radio_get(t, HW(HW_ZX80), 3)];
	sdl_emulator.ramsize = hw_ram_sizes[pop_choice(t, HW(HW_RAM), PU_RAM)];
	sdl_emulator.m1not = get_state(t, HW(HW_M1NOT), OS_SELECTED) ? 1 : 0;
	sdl_emulator.frameskip = pop_choice(t, HW(HW_FSKIP), PU_FSKIP);
	sdl_emulator.wrx = get_state(t, HW(HW_WRX), OS_SELECTED) ? HIRESWRX : HIRESDISABLED;
	sdl_emulator.chrgen = hw_chrgens[radio_get(t, HW(HW_UDGNONE), 3)];
	if (speed != sdl_emulator.speed) {
		sdl_emulator.speed = speed;
		if (sdl_sound.state) sound_framesiz_init();
	}
}

/* CF-Lib calls the exit routine while the mouse button is still down. A
 * popup that opens then takes the release for a click and closes at once,
 * so the popups wait for the release first */
static void wait_button_up(void) {
	short mx, my, mb, ks;

	for (;;) {
		graf_mkstate(&mx, &my, &mb, &ks);
		if (!(mb & 1)) break;
		evnt_timer(10);
	}
}

/* pobj is an object of the page tree. On exit: returns TRUE if handled */
static int hw_touch(OBJECT *t, short pobj) {
	switch (pobj) {
		case HW_RAM: case HW_RAMLBL: case HW_FSKIP: case HW_FSKLBL:
		case HW_SPEED: case HW_SPDLBL:
			wait_button_up();
			break;
	}
	switch (pobj) {
		case HW_RAM:
		case HW_RAMLBL:
			handle_popup(t, HW(HW_RAM), pops, PU_RAM, pobj == HW_RAM ? POP_OPEN : POP_CYCLE);
			return TRUE;
		case HW_FSKIP:
		case HW_FSKLBL:
			handle_popup(t, HW(HW_FSKIP), pops, PU_FSKIP, pobj == HW_FSKIP ? POP_OPEN : POP_CYCLE);
			return TRUE;
		case HW_SPEED:
		case HW_SPDLBL:
			handle_popup(t, HW(HW_SPEED), pops, PU_SPEED, pobj == HW_SPEED ? POP_OPEN : POP_CYCLE);
			return TRUE;
	}
	return FALSE;
}

/* ------------------------------------------------------------ Sound */

/* The radios are None, Quicksilva, ZON X-81, VSYNC */
static const int sd_devices[4] = { DEVICE_NONE, DEVICE_QUICKSILVA, DEVICE_ZONX, DEVICE_VSYNC };

static void sd_fill(OBJECT *t) {
	int i, dev = 0;

	for (i = 0; i < 4; i++)
		if (sd_devices[i] == sdl_sound.device) dev = i;
	radio_set(t, SD(SD_NONE), 4, dev);
	set_state(t, SD(SD_ACB), OS_SELECTED, sdl_sound.stereo != 0);
	field_set_int(t, SD(SD_VOL), sdl_sound.volume);
}

static int sd_check(OBJECT *t) {
	int volume;

	if (field_int(t, SD(SD_VOL), 0, 128, &volume)) return TRUE;
	page_error(PAGE_SOUND, AL_BADVOLUME);
	return FALSE;
}

/* As runopts_is_a_reset_scheduled(): sz81 resets the machine for a new
 * device and for stereo on or off with an AY board.
 * On exit: returns TRUE if the new sound settings reset the machine */
static int sd_resets(OBJECT *t) {
	int device = sd_devices[radio_get(t, SD(SD_NONE), 4)];
	int stereo = get_state(t, SD(SD_ACB), OS_SELECTED) ? TRUE : FALSE;
	int ay = device == DEVICE_QUICKSILVA || device == DEVICE_ZONX;

	return device != sdl_sound.device || (ay && stereo != sdl_sound.stereo);
}

static void sd_apply(OBJECT *t) {
	sdl_sound.device = sd_devices[radio_get(t, SD(SD_NONE), 4)];
	sdl_sound.stereo = get_state(t, SD(SD_ACB), OS_SELECTED) ? TRUE : FALSE;
	field_int(t, SD(SD_VOL), 0, 128, &sdl_sound.volume);
}

/* ------------------------------------------------------------ GUI options */

/* Ink and paper are colour buttons (atari/colsel.c), which pick one of
 * the VDI colours. sz81's colours are RGB values: the buttons start with
 * the nearest VDI colour, and only a colour picked anew replaces the RGB
 * value, so the exact colours of a palette file stay as they are */
static short gu_ink_pen, gu_paper_pen;	/* the colours the buttons started with */

static short gu_pen(Uint32 rgb) {
	short i, n = colsel_count(), best = 0, c[3];
	long d, best_d = 0x7fffffffL;

	for (i = 0; i < n; i++) {
		long r, g, b;

		vq_color(cf_vdi_handle, i, 1, c);
		r = (long)((rgb >> 16) & 0xff) - c[0] * 255L / 1000;
		g = (long)((rgb >> 8) & 0xff) - c[1] * 255L / 1000;
		b = (long)(rgb & 0xff) - c[2] * 255L / 1000;
		d = r * r + g * g + b * b;
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}

static Uint32 gu_rgb(short pen) {
	short c[3];

	vq_color(cf_vdi_handle, pen, 1, c);
	return (Uint32)(c[0] * 255L / 1000) << 16 | (Uint32)(c[1] * 255L / 1000) << 8
		| (Uint32)(c[2] * 255L / 1000);
}

static void gu_fill(OBJECT *t) {
	field_set_int(t, GU(GU_DELAY), sdl_key_repeat.delay);
	field_set_int(t, GU(GU_INTERVAL), sdl_key_repeat.interval);
	gu_ink_pen = gu_pen(colours.emu_fg & 0xffffff);
	gu_paper_pen = gu_pen(colours.emu_bg & 0xffffff);
	colsel_set(t, GU(GU_INK), gu_ink_pen);
	colsel_set(t, GU(GU_PAPER), gu_paper_pen);
}

static int gu_check(OBJECT *t) {
	int v;

	if (field_int(t, GU(GU_DELAY), 80, 520, &v) &&
		field_int(t, GU(GU_INTERVAL), 80, 520, &v)) return TRUE;
	page_error(PAGE_GUI, AL_BADREPEAT);
	return FALSE;
}

static void gu_apply(OBJECT *t) {
	short ink = colsel_get(t, GU(GU_INK)), paper = colsel_get(t, GU(GU_PAPER));

	field_int(t, GU(GU_DELAY), 80, 520, &sdl_key_repeat.delay);
	field_int(t, GU(GU_INTERVAL), 80, 520, &sdl_key_repeat.interval);
	if (ink != gu_ink_pen) {
		colours.emu_fg = gu_rgb(ink);
		video.redraw = TRUE;
	}
	if (paper != gu_paper_pen) {
		colours.emu_bg = gu_rgb(paper);
		video.redraw = TRUE;
	}
}

/* A colour button, or its label: the popup */
static int gu_touch(short pobj) {
	short box;

	if (pobj == GU_INK || pobj == GU_INKLBL) box = GU(GU_INK);
	else if (pobj == GU_PAPER || pobj == GU_PAPERLBL) box = GU(GU_PAPER);
	else return FALSE;
	wait_button_up();
	if (colsel_do(st_tree, box)) redraw_wdobj(st_wd, box);
	ikbd_resync();
	return TRUE;
}

/* ------------------------------------------------------------ Joystick */

/* As in the Jupiter Ace emulator: a cross of boxes shows the keys that the
 * joystick in port 1 presses; click a box, then press a key. sz81 sees the
 * joystick as one fire button (0) and two axes, whose ends are buttons 1 to
 * 4: left, right, up, down (see SDL_JOYAXISMOTION in sdl_input.c). The keys
 * are sz81 control remappings of these buttons, which go into its rcfile.
 * The order here: up, down, left, right, fire */
#define JS_N 5
#define JS_COMPS (COMP_EMU | COMP_VKEYB)

static const short js_objs[JS_N] = { JS_UP, JS_DOWN, JS_LEFT, JS_RIGHT, JS_FIRE };
static const int js_ids[JS_N] = { 3, 4, 1, 2, 0 };
static const unsigned char js_bits[JS_N] = { 0x01, 0x02, 0x04, 0x08, 0x80 };	/* IKBD */
static const SDLKey js_default[JS_N] = { SDLK_7, SDLK_6, SDLK_5, SDLK_8, SDLK_0 };
static SDLKey js_keys[JS_N];		/* the keys in the dialog */
static char js_names[JS_N][4];		/* the texts of the boxes */
static int js_capture = -1;			/* the box waiting for a key, or -1 */
static unsigned char joy_prev;		/* the joystick at the previous pump */

/* On exit: returns the index of the remapping of joystick button id in
 * ctrl_remaps[], or -1 */
static int js_remap(int id) {
	int i;

	for (i = 0; i < MAX_CTRL_REMAPS; i++)
		if (ctrl_remaps[i].device == DEVICE_JOYSTICK && ctrl_remaps[i].id == id &&
			(ctrl_remaps[i].components & COMP_EMU)) return i;
	return -1;
}

/* On exit: returns the key of joystick button id, SDLK_UNKNOWN if none */
static SDLKey js_key(int id) {
	int r = js_remap(id);

	if (r < 0 || ctrl_remaps[r].remap_device != DEVICE_KEYBOARD ||
		ctrl_remaps[r].remap_id <= 0) return SDLK_UNKNOWN;
	return (SDLKey)ctrl_remaps[r].remap_id;
}

static void js_set(int id, SDLKey key) {
	int r = js_remap(id);

	if (r < 0) {
		for (r = 0; r < MAX_CTRL_REMAPS && ctrl_remaps[r].device != UNDEFINED; r++);
		if (r == MAX_CTRL_REMAPS) return;
	}
	ctrl_remaps[r].components = JS_COMPS;
	ctrl_remaps[r].protected = FALSE;
	ctrl_remaps[r].device = DEVICE_JOYSTICK;
	ctrl_remaps[r].id = id;
	ctrl_remaps[r].remap_device = DEVICE_KEYBOARD;
	ctrl_remaps[r].remap_id = key;
	ctrl_remaps[r].remap_mod_id = UNDEFINED;
}

/* At the start: without any joystick keys (the first run, or an rcfile of
 * an older version) the cursor keys 7 6 5 8 and 0 */
static void js_defaults(void) {
	int i;

	for (i = 0; i < JS_N; i++)
		if (js_remap(js_ids[i]) >= 0) return;
	for (i = 0; i < JS_N; i++)
		js_set(js_ids[i], js_default[i]);
}

/* Keys of the ZX80/ZX81 keyboard. On exit: returns the key or SDLK_UNKNOWN */
static SDLKey js_zx_key(SDLKey k) {
	if ((k >= SDLK_a && k <= SDLK_z) || (k >= SDLK_0 && k <= SDLK_9) ||
		k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_PERIOD || k == SDLK_LSHIFT)
		return k;
	if (k == SDLK_KP_ENTER) return SDLK_RETURN;
	if (k == SDLK_RSHIFT) return SDLK_LSHIFT;
	return SDLK_UNKNOWN;
}

/* Box i shows its key */
static void js_show(int i) {
	SDLKey k = js_keys[i];

	if ((k >= SDLK_a && k <= SDLK_z) || (k >= SDLK_0 && k <= SDLK_9) || k == SDLK_PERIOD)
		sprintf(js_names[i], "%c", k >= SDLK_a && k <= SDLK_z ? k - SDLK_a + 'A' : k);
	else if (k == SDLK_SPACE) strcpy(js_names[i], "SPC");
	else if (k == SDLK_RETURN) strcpy(js_names[i], "NL");
	else if (k == SDLK_LSHIFT) strcpy(js_names[i], "SHF");
	else sprintf(js_names[i], "%.3s", rs(FS_JS_NONE));
}

/* The hint below the boxes, centred in the frame; a string is drawn
 * without its background, so the page is drawn again if it is open */
static void js_hint(int text) {
	OBJECT *o = &st_tree[JS(JS_HINT)];

	o->ob_spec.free_string = rs(text);
	o->ob_width = strlen(o->ob_spec.free_string) * gl_wchar;
	o->ob_x = (st_tree[JS(JS_FRAME)].ob_width - o->ob_width) / 2;
	if (dlg_any_open()) redraw_wdobj(st_wd, ST_PAGES);
}

static void js_fill(OBJECT *t) {
	int i;

	for (i = 0; i < JS_N; i++) {
		js_keys[i] = js_key(js_ids[i]);
		js_show(i);
		set_state(t, JS(js_objs[i]), OS_SELECTED, FALSE);
	}
	js_capture = -1;
	js_hint(FS_JS_HINT);
}

static void js_apply(void) {
	int i;

	for (i = 0; i < JS_N; i++)
		js_set(js_ids[i], js_keys[i]);
}

/* Box i waits for a key: pressed, and the hint says so */
static void js_wait(int i) {
	if (js_capture >= 0 && js_capture != i) {
		set_state(st_tree, JS(js_objs[js_capture]), OS_SELECTED, FALSE);
		redraw_wdobj(st_wd, JS(js_objs[js_capture]));
	}
	js_capture = i;
	set_state(st_tree, JS(js_objs[i]), OS_SELECTED, TRUE);
	redraw_wdobj(st_wd, JS(js_objs[i]));
	js_hint(FS_JS_PRESS);
}

/* The wait is over: key for the box, or SDLK_UNKNOWN to leave it as it was */
static void js_end(SDLKey key) {
	int i = js_capture;

	if (i < 0) return;
	js_capture = -1;
	if (key != SDLK_UNKNOWN) {
		js_keys[i] = key;
		js_show(i);
	}
	set_state(st_tree, JS(js_objs[i]), OS_SELECTED, FALSE);
	redraw_wdobj(st_wd, JS(js_objs[i]));
	js_hint(FS_JS_HINT);
}

/* A key while a box waits: Escape and Undo end the wait, a key of the
 * ZX keyboard is taken, other keys are ignored */
static void js_typed(int sc, short kstate) {
	SDLKey k;

	if (sc == SC_ESC || sc == 0x61) {
		js_end(SDLK_UNKNOWN);
		return;
	}
	if (kstate & (K_CTRL | K_ALT)) return;
	if ((k = js_zx_key(scan_sym[sc])) != SDLK_UNKNOWN) js_end(k);
}

/* On exit: returns the box of a page object, or -1 */
static int js_box(short pobj) {
	int i;

	for (i = 0; i < JS_N; i++)
		if (js_objs[i] == pobj) return i;
	return -1;
}

/* From ikbd_pump: the joystick as SDL joystick button events, which sz81
 * remaps to keys. Only while sz81 has the keys; else all go up */
static void joy_pump(int focus) {
	unsigned char now = focus && joy_active ? *kjoy : 0;
	SDL_Event event;
	int i;

	if (now == joy_prev) return;
	for (i = 0; i < JS_N; i++) {
		if (!((now ^ joy_prev) & js_bits[i])) continue;
		if (js_key(js_ids[i]) == SDLK_UNKNOWN) continue;
		memset(&event, 0, sizeof(event));
		event.type = now & js_bits[i] ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
		event.jbutton.which = 0;
		event.jbutton.button = js_ids[i];
		event.jbutton.state = now & js_bits[i] ? SDL_PRESSED : SDL_RELEASED;
		SDL_PushEvent(&event);
	}
	joy_prev = now;
}

/* ------------------------------------------------------------ driver */

/* The first time: the trees. On exit: returns FALSE if out of memory */
static int st_init(void) {
	static int failed;
	OBJECT *t, *pages[NPAGES];
	int i;

	if (st_tree) return TRUE;
	if (failed) {
		rs_alert(AL_NOMEM);
		return FALSE;
	}
	rsrc_gaddr(R_TREE, T_SETTINGS, &t);
	for (i = 0; i < NPAGES; i++)
		rsrc_gaddr(R_TREE, page_trees[i], &pages[i]);
	layout_settings(t, pages);
	fix_dial(t);
	for (i = 0; i < NPAGES; i++)
		fix_dial(pages[i]);
	/* From here on the merged tree, with the page objects at PG() */
	st_tree = tabs_init_trees(&st_tabs, t, ST_BAR, ST_PAGES, pages, NPAGES, PAGE_HW);
	colsel_use_vdi(cf_vdi_handle);
	if (!st_tree || !colsel_button(st_tree, GU(GU_INK), 0) ||
		!colsel_button(st_tree, GU(GU_PAPER), 0)) {
		st_tree = NULL;
		failed = TRUE;
		rs_alert(AL_NOMEM);
		return FALSE;
	}
	for (i = 0; i < JS_N; i++)
		st_tree[JS(js_objs[i])].ob_spec.free_string = js_names[i];
	aesfix_userdefs(st_tree);

	rsrc_gaddr(R_TREE, T_POPUPS, &pops);
	fix_popup(pops, 1);
	/* fix_popup() makes the entries G_SHORTCUTs where the AES has them,
	 * which then get shortcuts; the RAM sizes have too few different
	 * letters for one each, so they stay plain strings */
	for (i = pops[PU_RAM].ob_head; i != PU_RAM && i >= 0; i = pops[i].ob_next)
		if ((pops[i].ob_type & 0xFF) == G_SHORTCUT)
			pops[i].ob_type = (pops[i].ob_type & 0xFF00) | G_STRING;
	aesfix_userdefs(pops);
	return TRUE;
}

static int dlg_any_open(void) {
	return st_wd != NULL && (st_wd->mode & WD_OPEN);
}

/* On exit: returns TRUE if the dialog exists (open or not) */
static int dlg_any(void) {
	return st_wd != NULL;
}

/* On exit: returns TRUE if window handle is the open dialog */
static int dlg_is_window(short handle) {
	return dlg_any_open() && st_wd->win_handle == handle;
}

/* On exit: returns TRUE if a box of the Joystick page waits for a key */
static int dlg_wants_shift(void) {
	return dlg_any_open() && js_capture >= 0;
}

/* OK: all pages. On exit: returns TRUE if the settings were taken */
static int st_apply(void) {
	int hw;

	if (!sd_check(st_tree) || !gu_check(st_tree)) return FALSE;
	hw = hw_resets(st_tree);
	if ((hw || sd_resets(st_tree)) && !ask_reset(hw ? AL_HWRESET : AL_SNDRESET))
		return FALSE;
	hw_apply(st_tree);
	sd_apply(st_tree);
	gu_apply(st_tree);
	js_apply();
	return TRUE;
}

/* CF-Lib calls this for exit and touchexit objects, the Undo key and the
 * closer. On exit: returns TRUE to close the dialog */
static int st_exit(WDIALOG *wd, short obj) {
	short page, pobj;

	tabs_which(&st_tabs, obj, &page, &pobj);
	if (js_capture >= 0 && !(page == PAGE_JOY && js_box(pobj) >= 0))
		js_end(SDLK_UNKNOWN);
	if (tabs_wdial(&st_tabs, wd, obj)) return FALSE;	/* a tab */
	obj &= 0x7FFF;		/* bit 15: double click */
	switch (page) {
		case PAGE_HW:
			if (hw_touch(st_tree, pobj)) return FALSE;
			break;
		case PAGE_GUI:
			if (gu_touch(pobj)) return FALSE;
			break;
		case PAGE_JOY:
			if (js_box(pobj) >= 0) {
				js_wait(js_box(pobj));
				return FALSE;
			}
			break;
	}
	if (page >= 0) return FALSE;
	if (obj == ST_OK) {
		if (!sz81_dialog_open() && st_apply()) {
			rcfile.rewrite = TRUE;
			return TRUE;
		}
		set_state(st_tree, ST_OK, OS_SELECTED, FALSE);
		redraw_wdobj(wd, ST_OK);
		return FALSE;
	}
	return TRUE;		/* Cancel, Undo, Escape, the closer */
}

/* Opens the dialog on a page, or shows the page if it is open */
static void dlg_open(int page) {
	if (dlg_any_open()) {
		wind_set(st_wd->win_handle, WF_TOP, 0, 0, 0, 0);
		if (page != st_tabs.active) {
			js_end(SDLK_UNKNOWN);
			tabs_wdial(&st_tabs, st_wd, tabs_tab(&st_tabs, page));
		}
		return;
	}
	if (!st_init()) return;
	tabs_show(&st_tabs, page);
	hw_fill(st_tree);
	sd_fill(st_tree);
	gu_fill(st_tree);
	js_fill(st_tree);
	if (!st_wd && !(st_wd = create_wdial(st_tree, NULL, 0, WOCB_NULL, st_exit))) {
		rs_alert(AL_NOMEM);
		return;
	}
	/* The cursor in the first edit field of the page */
	st_wd->edit_obj = page == PAGE_SOUND ? SD(SD_VOL) : page == PAGE_GUI ? GU(GU_DELAY) : 0;
	st_wd->edit_idx = 0;
	dlg_nkeys = 0;
	mouse_flush();	/* the button that chose the menu entry */
	/* Centred the first time, then where it was */
	open_wdial(st_wd, -1, -1);
	if (st_wd->win_handle <= 0)
		rs_alert(AL_NOWINDOW);
}

static void dlg_close_all(void) {
	if (!st_wd) return;
	close_wdial(st_wd);
	delete_wdial(st_wd);
	st_wd = NULL;
	js_capture = -1;
}

/* From ikbd_pump while the dialog is on top */
static void dlg_key(int sc, const unsigned char *down) {
	if (dlg_nkeys >= DLG_KEYS) return;
	dlg_kstate = (down[SC_LSHIFT] ? K_LSHIFT : 0) | (down[SC_RSHIFT] ? K_RSHIFT : 0)
		| (down[SC_CTRL] ? K_CTRL : 0) | (down[SC_ALT] ? K_ALT : 0);
	dlg_keys[dlg_nkeys++] = sc;
}

/* Every frame: the keys typed into the dialog and a mouse click on it.
 * Keys go to CF-Lib as the AES reports them, scan code and ASCII (none
 * with Alt); Escape acts as Undo (Cancel), Enter as Return. While a box
 * of the Joystick page waits, the keys are for it */
static void dlg_poll(void) {
	static _KEYTAB *keytab;
	short top;
	int i, sc, key;

	if (!dlg_any_open()) {
		dlg_nkeys = 0;
		return;
	}
	if (!keytab) keytab = (_KEYTAB *)Keytbl((void *)-1, (void *)-1, (void *)-1);
	for (i = 0; i < dlg_nkeys && dlg_any_open(); i++) {
		sc = dlg_keys[i];
		if (js_capture >= 0) {
			js_typed(sc, dlg_kstate);
			continue;
		}
		if (is_modifier(sc)) continue;
		if (sc == SC_ESC) {
			key = 0x6100;
		} else if (sc == SC_ENTER || sc == SC_RETURN) {
			key = 0x1c0d;
		} else if (dlg_kstate & K_ALT) {
			key = sc << 8;
		} else {
			key = (sc << 8) | (unsigned char)
				((dlg_kstate & (K_LSHIFT | K_RSHIFT)) ? keytab->shift : keytab->unshift)[sc];
		}
		key_wdial(key, dlg_kstate);
	}
	dlg_nkeys = 0;
	if (!dlg_any_open()) return;

	if (mouse_click) {
		top = our_top_window();
		if (dlg_is_window(top) && wind_find(mouse_x, mouse_y) == top) {
			mouse_click = FALSE;
			wind_update(BEG_MCTRL);
			click_wdial(1, mouse_x, mouse_y, mouse_k, 1);
			wind_update(END_MCTRL);
			ikbd_resync();
			mouse_flush();
		}
	}
}

/***************************************************************************
 * Menu bar                                                                *
 ***************************************************************************/

static OBJECT *menu;		/* tree T_MENU of SZ81.RSC */
static int menu_shown;

/* Only in our SDL build (see the top of this file); weak so that sz81 still
 * links against a stock SDL, which then shows no menu bar */
extern void SDL_AtariGem_SetMessageHook(int (*hook)(const short *message))
	__attribute__((weak));

/* Control key shortcuts, also read from the IKBD hook */
static const struct {
	SDLKey key;
	int item;
} shortcuts[] = {
	{ SDLK_o, M_OPEN },
	{ SDLK_t, M_SCREENSHOT },
	{ SDLK_q, M_QUIT },
	{ SDLK_k, M_KEYBOARD },
};

/* The bar spans the whole screen */
static void menu_build(void) {
	short x, y, w, h;

	rsrc_gaddr(R_TREE, T_MENU, &menu);
	wind_get(0, WF_CURRXYWH, &x, &y, &w, &h);
	menu[ROOT].ob_width = menu[M_BAR].ob_width = menu[M_SCREEN].ob_width = x + w;
}

static void menu_hide(void) {
	if (menu_shown) {
		menu_bar(menu, MENU_REMOVE);
		menu_shown = FALSE;
	}
}

/* Called by SDL's GEM driver from inside its event pump for every message.
 * A menu choice is only noted here, it is carried out from check_events.
 * On exit: returns TRUE if the message is ours, SDL then ignores it */
static int gem_message(const short *message) {
	if (message[0] == MN_SELECTED) {
		menu_pick = message[4];
		menu_tnormal(menu, message[3], 1);
		return TRUE;
	}
	return window_message(message, kb_handle, kb_draw, kb_close)
		|| window_message(message, ab_handle, ab_draw, ab_close)
		|| (dlg_any() && message_wdial((short *)message));
}

/* On exit: returns TRUE if Control and this key are a menu shortcut */
static int menu_shortcut(int sc) {
	int i;

	if (!menu) return FALSE;

	for (i = 0; i < (int)(sizeof(shortcuts) / sizeof(*shortcuts)); i++) {
		if (scan_sym[sc] == shortcuts[i].key) {
			menu_pick = shortcuts[i].item;
			return TRUE;
		}
	}
	return FALSE;
}

/* Keep the View menu's check marks in step with the settings, which
 * sz81's own keys (F8) and dialogs change too */
static void menu_update_checks(void) {
	static int invert = -1, keyboard = -1;

	if (!menu_shown) return;
	if (invert != sdl_emulator.invert) {
		invert = sdl_emulator.invert;
		menu_icheck(menu, M_INVERT, invert ? 1 : 0);
	}
	if (keyboard != (kb_handle >= 0)) {
		keyboard = kb_handle >= 0;
		menu_icheck(menu, M_KEYBOARD, keyboard);
	}
}

static void menu_do(int item) {
	switch (item) {
		case M_ABOUT:
			ab_open();
			break;
		case M_OPEN:
			/* As F3: the selector, then a reset and the load */
			toggle_ldfile_state();
			break;
		case M_LOADSTATE:
			/* As F5: sz81's own slot dialog */
			toggle_sstate_state(SSTATE_MODE_LOAD);
			break;
		case M_SAVESTATE:
			/* As F4 */
			toggle_sstate_state(SSTATE_MODE_SAVE);
			break;
		case M_SCREENSHOT:
			/* As PrtScn: a BMP in the local scnsht folder */
			save_screenshot();
			break;
		case M_RESET:
			/* As F12, which works only when no sz81 dialog is open */
			if (get_active_component() != COMP_VKEYB &&
				get_active_component() != COMP_CTB &&
				get_active_component() != COMP_EMU)
				break;
			if (rs_alert(AL_RESET) == 2)
				emulator_reset();
			ikbd_resync();
			break;
		case M_QUIT:
			/* The desktop would take the Q of ^Q as a key of its own */
			wait_keys_released();
			flush_keys();
			emulator_exit();
			break;
		case M_INVERT:
			/* As F8 */
			sdl_emulator.invert = !sdl_emulator.invert;
			rcfile.rewrite = TRUE;
			refresh_screen = 1;
			break;
		case M_HARDWARE:
			dlg_open(PAGE_HW);
			break;
		case M_SOUND:
			dlg_open(PAGE_SOUND);
			break;
		case M_GUIOPTS:
			dlg_open(PAGE_GUI);
			break;
		case M_JOYSTICK:
			dlg_open(PAGE_JOY);
			break;
		case M_KEYBOARD:
			if (kb_handle >= 0) kb_close();
			else kb_open();
			ikbd_resync();
			break;
	}
}

/***************************************************************************
 * Called every frame from check_events                                    *
 ***************************************************************************/

/* The window title names the machine; zx80 is 0 = ZX81, 1 = ZX80 with the
 * 4K ROM, 2 = ZX80 with the 8K ROM */
static void update_title(void) {
	static int machine = -1;
	static const char *const names[] = { "ZX81", "ZX80 4K", "ZX80 8K" };
	char title[32];

	if (machine == zx80 || zx80 < 0 || zx80 > 2) return;
	machine = zx80;
	sprintf(title, "sz81 - %s", names[machine]);
	SDL_WM_SetCaption(title, "sz81");
}

static int cf_inited;

/* SDL's GEM driver and CF-Lib's init_app() both call appl_init. The link
 * wraps it (-Wl,--wrap=mt_appl_init in Makefile.atari), so that only the
 * first call registers with the AES and later ones get the same id: then
 * SDL_Quit's one appl_exit ends the registration. XaAES kills a process
 * that calls appl_exit a second time, MyAES counts the calls */
short __real_mt_appl_init(short *global);
short __wrap_mt_appl_init(short *global);

short __wrap_mt_appl_init(short *global) {
	static short apid = -1;

	if (apid < 0) return apid = __real_mt_appl_init(global);
	return apid;
}

/* CF-Lib's parts of exit_gem() but appl_exit, which SDL does */
void exit_userdef(void);

/* Called before SDL_Quit, while sz81 is still registered with the AES:
 * undoes init_app and our rsrc_load */
void atari_gem_exit(void) {
	if (!cf_inited) return;
	cf_inited = FALSE;
	tabs_exit();
	colsel_exit();
	if (rsc_loaded) rsrc_free();
	rsc_loaded = FALSE;
	if (gl_gdos) vst_unload_fonts(cf_vdi_handle, 0);
	v_clsvwk(cf_vdi_handle);
	exit_userdef();
}

void atari_poll(void) {
	static int state = -1;	/* -1 = first call, 0 = nothing to do, 1 = GEM */
	int item;

	if (state < 0) {
		state = atari_gem_driver();
		if (state) {
			/* CF-Lib's globals, VDI workstation and user objects. Its
			 * appl_init gets SDL's id (see __wrap_mt_appl_init) */
			init_app(NULL);
			cf_inited = TRUE;
			rsc_loaded = rsrc_load("sz81.rsc") != 0;
			if (!rsc_loaded)
				form_alert(1, "[3][sz81.rsc is missing. It belongs|"
					"next to the program. sz81 runs|without its menu bar.][  OK  ]");
			/* Under a multitasking AES: our name in the application list,
			 * and AP_TERM is understood (SDL's GEM driver quits on it) */
			if (_AESnumapps != 1)
				menu_register(gl_apid, rsc_loaded ? rs(FS_APPNAME) : "  sz81 Emulator");
			if (_AESversion >= 0x400)
				shel_write(SWM_NEWMSG, NM_APTERM, 0, NULL, NULL);
		}
		if (state && rsc_loaded && SDL_AtariGem_SetMessageHook) {
			menu_build();
			if (menu_bar(menu, MENU_INSTALL) > 0) {
				menu_shown = TRUE;
				atexit(menu_hide);
			}
			SDL_AtariGem_SetMessageHook(gem_message);
			if (SDL_AtariGem_SetButtonHook)
				SDL_AtariGem_SetButtonHook(button_hook);
		}
		if (state && ikbd_install())
			SDL_SetEventFilter(drop_sdl_keys);
		if (state) js_defaults();
	}
	if (!state) return;

	if (hook_active) ikbd_pump();
	kb_check_machine();
	mouse_poll();
	kb_poll();
	ab_poll();
	dlg_poll();
	update_title();
	menu_update_checks();

	if ((item = menu_pick) != 0) {
		menu_pick = 0;
		menu_do(item);
	}
}

/***************************************************************************
 * File selector                                                           *
 ***************************************************************************/
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
	wait_keys_released();
	flush_keys();

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
	ikbd_resync();

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

/***************************************************************************
 * PNG screenshots                                                         *
 ***************************************************************************/
/* A plain 24-bit RGB PNG made with zlib. The rows are compressed one at a
 * time into IDAT chunks, so even a big window needs only small buffers.
 * deflate gets a 4 KB window and memLevel 5: about 32 KB of RAM instead of
 * the 260 KB the defaults take, and the files grow by only a few percent. */

static void png_be32(unsigned char *p, unsigned long v) {
	p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static int png_chunk(FILE *fp, const char *type, const unsigned char *data, unsigned long len) {
	unsigned char head[8], crc[4];
	unsigned long sum;

	png_be32(head, len);
	memcpy(head + 4, type, 4);
	sum = crc32(0L, head + 4, 4);
	if (len) sum = crc32(sum, data, len);
	png_be32(crc, sum);
	return fwrite(head, 8, 1, fp) == 1 && (!len || fwrite(data, len, 1, fp) == 1) &&
		fwrite(crc, 4, 1, fp) == 1;
}

static Uint32 png_pixel(SDL_Surface *s, int x, int y) {
	Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;

	switch (s->format->BytesPerPixel) {
		case 1: return *p;
		case 2: return *(Uint16 *)p;
		case 4: return *(Uint32 *)p;
	}
	return 0;
}

/* On exit: returns 0 on success else -1 */

int atari_save_png(SDL_Surface *surface, const char *filename) {
	static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	unsigned char ihdr[13], out[16384], *row;
	int x, y, w = surface->w, h = surface->h, ok, flush;
	z_stream z;
	FILE *fp;

	if ((row = malloc(1 + 3 * w)) == NULL) return -1;
	if ((fp = fopen(filename, "wb")) == NULL) {
		free(row);
		return -1;
	}
	png_be32(ihdr, w);
	png_be32(ihdr + 4, h);
	ihdr[8] = 8;	/* bits per channel */
	ihdr[9] = 2;	/* RGB */
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	ok = fwrite(signature, 8, 1, fp) == 1 && png_chunk(fp, "IHDR", ihdr, 13);

	memset(&z, 0, sizeof(z));
	ok = ok && deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
		12, 5, Z_DEFAULT_STRATEGY) == Z_OK;
	if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
	for (y = 0; ok && y < h; y++) {
		row[0] = 0;	/* no filter */
		for (x = 0; x < w; x++)
			SDL_GetRGB(png_pixel(surface, x, y), surface->format,
				&row[1 + 3 * x], &row[2 + 3 * x], &row[3 + 3 * x]);
		z.next_in = row;
		z.avail_in = 1 + 3 * w;
		flush = y == h - 1 ? Z_FINISH : Z_NO_FLUSH;
		do {
			z.next_out = out;
			z.avail_out = sizeof(out);
			if (deflate(&z, flush) == Z_STREAM_ERROR) ok = FALSE;
			if (sizeof(out) - z.avail_out)
				ok = ok && png_chunk(fp, "IDAT", out, sizeof(out) - z.avail_out);
		} while (ok && (z.avail_in || (flush == Z_FINISH && z.avail_out == 0)));
	}
	if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
	deflateEnd(&z);
	ok = ok && png_chunk(fp, "IEND", NULL, 0);
	ok = (fclose(fp) == 0) && ok;
	free(row);
	if (!ok) remove(filename);
	return ok ? 0 : -1;
}

/***************************************************************************
 * Audio                                                                   *
 ***************************************************************************/
/* SDL's XBIOS sound driver (Falcon, ARAnyM) only counts itself available
 * when it can take the sound lock, which another program holds for as long
 * as it uses the sound (also while paused), and SDL looks for a driver only
 * at SDL_Init. Look again before opening, so the sound can be chosen again
 * once the other program has quit. */

void atari_audio_probe(void) {
	char name[16];

	if (!SDL_AudioDriverName(name, sizeof(name))) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		SDL_InitSubSystem(SDL_INIT_AUDIO);
	}
}

/* When SDL_OpenAudio fails, say why */

void atari_audio_diagnose(void) {
	char name[16];
	long lock;

	if (SDL_AudioDriverName(name, sizeof(name))) {
		fprintf(stderr, "Atari audio: SDL driver %s could not open\n", name);
		return;
	}
	lock = Locksnd();
	if (lock == 1) {
		Unlocksnd();
		fprintf(stderr, "Atari audio: no SDL audio driver, but the XBIOS sound lock is free\n");
	} else {
		fprintf(stderr, "Atari audio: no SDL audio driver; Locksnd() = %ld (%s)\n", lock,
			lock == SNDLOCKED ? "another program is using the sound, "
			"quit it and choose the sound device again" : "no XBIOS sound");
	}
}

/***************************************************************************
 * Palettes                                                                *
 ***************************************************************************/
/* A palette file holds two RGB values written 0xRRGGBB, paper first, then
 * ink; anything else in the file is ignored. Returns TRUE if both were found */

static int pal_read(const char *filename, Uint32 rgb[2]) {
	FILE *fp;
	char line[256], *p;
	int n = 0, i, d;
	Uint32 v;

	if ((fp = fopen(filename, "r")) == NULL) return FALSE;
	while (n < 2 && fgets(line, sizeof(line), fp)) {
		for (p = line; n < 2 && (p = strstr(p, "0x")) != NULL; ) {
			p += 2;
			for (v = 0, i = 0; i < 6; i++, p++) {
				if (*p >= '0' && *p <= '9') d = *p - '0';
				else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
				else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
				else break;
				v = v << 4 | d;
			}
			if (i == 6) rgb[n++] = v;
		}
	}
	fclose(fp);
	return n == 2;
}

/* Loads palettes/<program name>.pal from sz81's folder, else
 * palettes/default.pal, else the built-in ZX80/81 colours. program is the
 * loaded file's path, or NULL at startup */
void atari_palette_load(const char *program) {
	static const Uint32 zx[2] = {0xf0f0e8, 0x202018};
	char filename[512], *e;
	const char *b;
	Uint32 rgb[2];
	int l;

	strcpy(filename, startdir);
	strcatdelimiter(filename);
	strcat(filename, "palettes");
	strcatdelimiter(filename);
	l = strlen(filename);
	if (program != NULL) {
		b = strrchr(program, '/');
		if (strrchr(program, '\\') > b) b = strrchr(program, '\\');
		b = b ? b + 1 : program;
		if (l + strlen(b) + 5 < sizeof(filename)) {
			strcpy(filename + l, b);
			if ((e = strrchr(filename + l, '.')) != NULL) *e = 0;
			strcat(filename, ".pal");
			if (pal_read(filename, rgb)) goto found;
		}
	}
	strcpy(filename + l, "default.pal");
	if (!pal_read(filename, rgb)) memcpy(rgb, zx, sizeof(rgb));
found:
	colours.emu_bg = rgb[0];
	colours.emu_fg = rgb[1];
	video.redraw = TRUE;
}
