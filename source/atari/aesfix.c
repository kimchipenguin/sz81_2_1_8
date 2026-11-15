/*
 * sz81 - workarounds for old AES versions (TOS 1.x - 4.x)
 *
 * 1. USERDEF stack.  CF-Lib draws the MagiC objects (group frames, radio
 *    buttons, buttons with shortcuts) as USERDEFs, and its routines call
 *    v_gtext() and vqt_extent(), which in GEMlib reserve about 2.1 KB of
 *    stack each.  The TOS 1.04 AES calls USERDEFs on its own small stack;
 *    that overflowed and overwrote the AES variables (in Pong: a crash as soon as the
 *    New game dialog was drawn).  So every USERDEF of our trees is sent
 *    through a small routine that switches to a stack of our own, calls the
 *    original CF-Lib routine and switches back.  ub_parm is left alone,
 *    because CF-Lib reads it directly.
 *
 * 2. Window gadgets.  CF-Lib opens its dialog and alert windows with a
 *    BACKDROP gadget ($2000), a bit the AES of TOS 1.x - 4.x does not know.
 *    The linker sends every window created through CF-Lib through a filter
 *    (-Wl,--wrap=mt_wind_create_grect, see the Makefile) that removes the
 *    unknown bits where appl_getinfo() is missing.
 */

#include <stddef.h>
#include <gem.h>
#include "aesfix.h"

/* ---------------------------------------------------------------- */
/* 1. USERDEFs on our own stack                                      */
/* ---------------------------------------------------------------- */

#define UD_STACK    8192
#define UD_MAX      48

typedef short __CDECL (*UDCODE)(PARMBLK *);

static struct {
	USERBLK *blk;
	UDCODE code;
} ud[UD_MAX];
static short n_ud;

/* used from the assembler routine below, so with fixed names */
long l9_udstack[UD_STACK / 4] __asm__("l9_udstack") __attribute__((used));
short l9_ud_dispatch(PARMBLK *p) __asm__("l9_ud_dispatch");
short __CDECL l9_udtramp(PARMBLK *p) __asm__("l9_udtramp");

/* the AES calls this with the PARMBLK pointer on the stack and wants the
   result in D0; it moves to our stack and calls l9_ud_dispatch()
   (written so that it also assembles for the ColdFire) */
__asm__(
	"	.text\n"
	"	.even\n"
	"l9_udtramp:\n"
	"	move.l	4(%sp),%a0\n"
	"	move.l	%sp,%a1\n"
	"	lea	l9_udstack+8192,%sp\n"
	"	move.l	%a1,-(%sp)\n"
	"	move.l	%a0,-(%sp)\n"
	"	jsr	l9_ud_dispatch\n"
	"	addq.l	#4,%sp\n"
	"	move.l	(%sp),%a1\n"
	"	move.l	%a1,%sp\n"
	"	rts\n"
);

short l9_ud_dispatch(PARMBLK *p)
{
	USERBLK *b = p->pb_tree[p->pb_obj].ob_spec.userblk;
	short i;

	for (i = 0; i < n_ud; i++)
		if (ud[i].blk == b)
			return ud[i].code(p);
	return 0;
}

void aesfix_userdefs(OBJECT *tree)
{
	USERBLK *b;
	short i = 0;

	if (tree == NULL)
		return;
	do {
		if ((tree[i].ob_type & 0xFF) == G_USERDEF) {
			b = tree[i].ob_spec.userblk;
			/* several objects can share one USERBLK (menu lines) */
			if (b->ub_code != l9_udtramp && n_ud < UD_MAX) {
				ud[n_ud].blk = b;
				ud[n_ud].code = b->ub_code;
				n_ud++;
				b->ub_code = l9_udtramp;
			}
		}
	} while (!(tree[i++].ob_flags & OF_LASTOB));
}

/* ---------------------------------------------------------------- */
/* 2. window gadgets                                                 */
/* ---------------------------------------------------------------- */

/* appl_getinfo() exists in AES 4.0 and up, and where "?AGI" says so */
static int new_aes(void)
{
	return _AESversion >= 0x400 || appl_find("?AGI") == 0;
}

short __real_mt_wind_create_grect(short kind, const GRECT *r, short *global);
short __wrap_mt_wind_create_grect(short kind, const GRECT *r, short *global);

short __wrap_mt_wind_create_grect(short kind, const GRECT *r, short *global)
{
	if (!new_aes())
		kind &= 0x0FFF;
	return __real_mt_wind_create_grect(kind, r, global);
}
