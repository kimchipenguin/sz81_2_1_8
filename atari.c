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
 * needs our SDL build, whose GEM driver passes on the messages it does not
 * handle itself (SDL_AtariGem_SetMessageHook).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <gem.h>
#include <support.h>
#include <mint/osbind.h>
#include <mint/cookie.h>
#include <mint/ostruct.h>
#include <mint/falcon.h>
#include <zlib.h>
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

/***************************************************************************
 * IKBD hook                                                               *
 ***************************************************************************/
/* TOS 2.0+ calls the "kbdvec" (the long in front of the table that
 * Kbdvbase() returns) for every keyboard byte; FreeMiNT and SDL chain their
 * own keyboard handling into the same vector. Our handler goes in front,
 * notes make/break codes in two 128-byte tables and passes every byte on.
 *
 * The handler runs in interrupts while any process may be active, so it is
 * copied into global memory. It is removed at exit (atexit), on fatal
 * signals and from the process termination vector etv_term (also after
 * bombs under plain TOS). */

extern char ikbd_hook_start[], ikbd_hook_end[], ikbd_hook_old[];
extern char ikbd_hook_handler[], ikbd_hook_down[], ikbd_hook_hit[];
extern long ikbd_term_old;
void ikbd_term_stub(void);
long ikbd_remove_super(void);

#define HOOK_OFF(sym) ((sym) - ikbd_hook_start)

#define SC_CTRL   0x1d
#define SC_LSHIFT 0x2a
#define SC_RSHIFT 0x36
#define SC_ALT    0x38
#define SC_CAPS   0x3a

static char *hook_mem;			/* copy of the handler block */
static volatile unsigned char *kdown, *khit;
static long *kbdvec;
static long hook_handler;		/* our handler in hook_mem */
static int hook_active, term_hooked, mint;
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
	if (cpu >= 40)
		__asm__ volatile (".word 0xf4f8" ::: "memory");	/* cpusha bc */
	else if (cpu >= 20)
		/* movec cacr,d0 / movec d0,cacr as words: a 68000 build's
		 * assembler refuses them, the code only runs on a 68020+ */
		__asm__ volatile (".word 0x4e7a,0x0002\n\t"
			"or.w #0x0808,%%d0\n\t"
			".word 0x4e7b,0x0002" ::: "d0", "memory");
}

static long super_install(void) {
	*(long *)(hook_mem + HOOK_OFF(ikbd_hook_old)) = *kbdvec;
	flush_caches();
	*kbdvec = hook_handler;
	return 0;
}

/* Unlink our handler from the kbdvec chain (supervisor mode). Also called
 * by the etv_term stub. */
long ikbd_remove_super(void) {
	long *p = kbdvec;
	int n;

	if (!hook_active) return 0;
	for (n = 0; n < 32 && *p; n++) {
		long *x;

		if (*p == hook_handler) {
			*p = *(long *)(hook_mem + HOOK_OFF(ikbd_hook_old));
			hook_active = FALSE;
			break;
		}
		x = (long *)(*p - 12);	/* XBRA header in front of a handler */
		if (x[0] != 0x58425241L)	/* 'XBRA' */
			break;
		p = &x[2];
	}
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
	if (!hook_active && hook_mem) {
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
	hook_handler = (long)(hook_mem + HOOK_OFF(ikbd_hook_handler));
	kbdvec = (long *)Kbdvbase() - 1;

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

/* Turn the key changes since the last pump into SDL key events: modifier
 * presses first, so Shift+P reaches the ZX81 as Shift+P, and modifier
 * releases last. A key pressed and released between two pumps still gives
 * a press and a release. */
static void ikbd_pump(void) {
	unsigned char now[128], hit[128];
	int sc, focus;

	focus = (SDL_GetAppState() & SDL_APPINPUTFOCUS) != 0;
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
 * Menu bar                                                                *
 ***************************************************************************/

enum {
	M_ROOT, M_BAR, M_ACTIVE, M_T_DESK, M_T_FILE, M_T_VIEW, M_SCREEN,
	M_DESKBOX, M_ABOUT, M_DESKSEP, M_ACC1, M_ACC2, M_ACC3, M_ACC4, M_ACC5, M_ACC6,
	M_FILEBOX, M_OPEN, M_FILESEP1, M_SCREENSHOT, M_FILESEP2, M_QUIT,
	M_VIEWBOX, M_INVERT,
	M_COUNT
};

static OBJECT menu[M_COUNT];
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
};

#define DESK_W 21
#define FILE_W 22
#define VIEW_W 12

static char s_t_desk[] = " sz81 ";
static char s_t_file[] = " File ";
static char s_t_view[] = " View ";
static char s_about[DESK_W + 1], s_desksep[DESK_W + 1], s_acc[6][DESK_W + 1];
static char s_open[FILE_W + 1], s_screenshot[FILE_W + 1], s_quit[FILE_W + 1];
static char s_filesep[FILE_W + 1];
static char s_invert[VIEW_W + 1];

static void menu_obj(int i, int next, int head, int tail, int type, int state,
	long spec, int x, int y, int w, int h) {
	menu[i].ob_next = next;
	menu[i].ob_head = head;
	menu[i].ob_tail = tail;
	menu[i].ob_type = type;
	menu[i].ob_flags = OF_NONE;
	menu[i].ob_state = state;
	menu[i].ob_spec.index = spec;
	menu[i].ob_x = x;
	menu[i].ob_y = y;
	menu[i].ob_width = w;
	menu[i].ob_height = h;
}

/* A menu entry: two spaces for the check mark, the text, the shortcut at
 * the right edge */
static void menu_item(int i, int next, char *s, int w, int y, const char *text,
	const char *key) {
	sprintf(s, "  %-*s%s ", w - 3 - (int)strlen(key), text, key);
	menu_obj(i, next, -1, -1, G_STRING, OS_NORMAL, 0, 0, y, w, 1);
	menu[i].ob_spec.free_string = s;
}

static void menu_separator(int i, int next, char *s, int w, int y) {
	memset(s, '-', w);
	s[w] = 0;
	menu_obj(i, next, -1, -1, G_STRING, OS_DISABLED, 0, 0, y, w, 1);
	menu[i].ob_spec.free_string = s;
}

static void menu_title(int i, int next, char *s, int x) {
	menu_obj(i, next, -1, -1, G_TITLE, OS_NORMAL, 0L, x, 0, strlen(s), 0x0301);
	menu[i].ob_spec.free_string = s;
}

/* Sizes are in characters as in a resource file (low byte characters, high
 * byte extra pixels); rsrc_obfix turns them into pixels */
static void menu_build(void) {
	short x, y, w, h;
	char name[24];
	int i;

	menu_obj(M_ROOT, -1, M_BAR, M_SCREEN, G_IBOX, OS_NORMAL, 0L, 0, 0, 80, 25);
	menu_obj(M_BAR, M_SCREEN, M_ACTIVE, M_ACTIVE, G_BOX, OS_NORMAL, 0x00001100L, 0, 0, 80, 0x0201);
	menu_obj(M_ACTIVE, M_BAR, M_T_DESK, M_T_VIEW, G_IBOX, OS_NORMAL, 0L, 2, 0, 18, 0x0301);
	menu_title(M_T_DESK, M_T_FILE, s_t_desk, 0);
	menu_title(M_T_FILE, M_T_VIEW, s_t_file, 6);
	menu_title(M_T_VIEW, M_ACTIVE, s_t_view, 12);
	menu_obj(M_SCREEN, M_ROOT, M_DESKBOX, M_VIEWBOX, G_IBOX, OS_NORMAL, 0L, 0, 0x0301, 80, 19);

	menu_obj(M_DESKBOX, M_FILEBOX, M_ABOUT, M_ACC6, G_BOX, OS_NORMAL, 0x00FF1100L, 2, 0, DESK_W, 8);
	menu_item(M_ABOUT, M_DESKSEP, s_about, DESK_W, 0, "About sz81...", "");
	menu_separator(M_DESKSEP, M_ACC1, s_desksep, DESK_W, 1);
	for (i = 0; i < 6; i++) {
		sprintf(name, "Desk Accessory %d", i + 1);
		menu_item(M_ACC1 + i, i < 5 ? M_ACC1 + i + 1 : M_DESKBOX, s_acc[i], DESK_W, 2 + i, name, "");
	}

	menu_obj(M_FILEBOX, M_VIEWBOX, M_OPEN, M_QUIT, G_BOX, OS_NORMAL, 0x00FF1100L, 8, 0, FILE_W, 5);
	menu_item(M_OPEN, M_FILESEP1, s_open, FILE_W, 0, "Open...", "^O");
	menu_separator(M_FILESEP1, M_SCREENSHOT, s_filesep, FILE_W, 1);
	menu_item(M_SCREENSHOT, M_FILESEP2, s_screenshot, FILE_W, 2, "Save screenshot", "^T");
	menu_separator(M_FILESEP2, M_QUIT, s_filesep, FILE_W, 3);
	menu_item(M_QUIT, M_FILEBOX, s_quit, FILE_W, 4, "Quit", "^Q");

	menu_obj(M_VIEWBOX, M_SCREEN, M_INVERT, M_INVERT, G_BOX, OS_NORMAL, 0x00FF1100L, 14, 0, VIEW_W, 1);
	menu_item(M_INVERT, M_VIEWBOX, s_invert, VIEW_W, 0, "Invert", "");
	menu[M_INVERT].ob_flags |= OF_LASTOB;

	for (i = 0; i < M_COUNT; i++)
		rsrc_obfix(menu, i);

	/* The bar spans the whole screen */
	wind_get(0, WF_CURRXYWH, &x, &y, &w, &h);
	menu[M_ROOT].ob_width = menu[M_BAR].ob_width = menu[M_SCREEN].ob_width = x + w;
}

static void menu_hide(void) {
	if (menu_shown) {
		menu_bar(menu, MENU_REMOVE);
		menu_shown = FALSE;
	}
}

/* Called by SDL's GEM driver from inside its event pump: only note the
 * choice here, it is carried out from check_events */
static int menu_message(const short *message) {
	if (message[0] == MN_SELECTED) {
		menu_pick = message[4];
		menu_tnormal(menu, message[3], 1);
	}
	return 0;
}

/* On exit: returns TRUE if Control and this key are a menu shortcut */
static int menu_shortcut(int sc) {
	int i;

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
	static int invert = -1;

	if (menu_shown && invert != sdl_emulator.invert) {
		invert = sdl_emulator.invert;
		menu_icheck(menu, M_INVERT, invert ? 1 : 0);
	}
}

static void menu_do(int item) {
	switch (item) {
		case M_ABOUT:
			form_alert(1, "[1][sz81 " VERSION "|ZX80/ZX81 emulator|"
				"|Atari port][  OK  ]");
			ikbd_resync();
			break;
		case M_OPEN:
			/* As F3: the selector, then a reset and the load */
			toggle_ldfile_state();
			break;
		case M_SCREENSHOT:
			/* As PrtScn: a BMP in the local scnsht folder */
			save_screenshot();
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
	}
}

/***************************************************************************
 * Called every frame from check_events                                    *
 ***************************************************************************/

void atari_poll(void) {
	static int state = -1;	/* -1 = first call, 0 = nothing to do, 1 = GEM */
	int item;

	if (state < 0) {
		state = atari_gem_driver();
		if (state && SDL_AtariGem_SetMessageHook) {
			menu_build();
			if (menu_bar(menu, MENU_INSTALL) > 0) {
				menu_shown = TRUE;
				atexit(menu_hide);
			}
			SDL_AtariGem_SetMessageHook(menu_message);
		}
		if (state && ikbd_install())
			SDL_SetEventFilter(drop_sdl_keys);
	}
	if (!state) return;

	if (hook_active) ikbd_pump();
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
 * time into IDAT chunks, so even a big window needs only small buffers. */

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
	ok = ok && deflateInit(&z, Z_DEFAULT_COMPRESSION) == Z_OK;
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
