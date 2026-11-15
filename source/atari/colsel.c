/*
 * colsel - a small colour selector popup for GEM programs, see colsel.h
 *
 * The popup saves the screen below it (CF-Lib save_background()), draws
 * with its own VDI workstation and keeps the screen locked
 * (BEG_UPDATE + BEG_MCTRL) while it is open, like a popup menu.
 */

#include <stdlib.h>
#include <gem.h>
#include <cflib.h>
#include "colsel.h"

#define PAD         3           /* popup border to the first swatch */
#define INSET       2           /* cell edge to swatch */

#define SC_UP       0x48
#define SC_DOWN     0x50
#define SC_LEFT     0x4B
#define SC_RIGHT    0x4D
#define SC_HOME     0x47
#define SC_RETURN   0x1C
#define SC_ENTER    0x72
#define SC_ESC      0x01
#define SC_UNDO     0x61

static short vh = -1;           /* VDI workstation: own, or the program's */
static short shared;            /* colsel_use_vdi(): not ours to close */
static short ncol;              /* colours shown */
static short use_3d, back_col, dark_col;
static short tall_pixels;       /* ST Medium: pixels twice as tall as wide */
static short wide_pixels;       /* TT Low: pixels twice as wide as tall */

typedef struct {
	GRECT r;                    /* whole popup */
	short gx, gy;               /* top left of the grid */
	short cols, rows, cw, ch;   /* grid and cell size */
} CSPOP;

/* ---------------------------------------------------------------- */
/* drawing helpers                                                   */
/* ---------------------------------------------------------------- */

/* the fill and line attributes colsel draws with (again before every
   drawing: a shared workstation is used by others meanwhile) */
static void prep(void)
{
	vsf_interior(vh, FIS_SOLID);
	vsf_perimeter(vh, 1);
	vswr_mode(vh, MD_REPLACE);
	vsl_type(vh, SOLID);
	vsl_width(vh, 1);
	vsl_ends(vh, 0, 0);
}

static void init(void)
{
	short work_out[57], ap1, ap2, ap3, ap4, d;

	if (vh >= 0 && ncol > 0)
		return;
	if (vh >= 0)
		vq_extnd(vh, 0, work_out);      /* shared: its sizes */
	else
		vh = open_vwork(work_out);
	ncol = work_out[13];
	if (ncol <= 0 || ncol > 256)
		ncol = 256;
	if (ncol < 2)
		ncol = 2;
	tall_pixels = work_out[4] > work_out[3] * 3 / 2;
	wide_pixels = work_out[3] > work_out[4] * 3 / 2;

	use_3d = 0;
	back_col = G_WHITE;
	dark_col = G_BLACK;
	if (appl_xgetinfo(13, &ap1, &ap2, &ap3, &ap4) && ap1 == 1 && ncol >= 16) {
		use_3d = 1;
		objc_sysvar(0, BACKGRCOL, 0, 0, &back_col, &d);
		dark_col = G_LBLACK;
	}
	prep();
}

static void clip(short x, short y, short w, short h)
{
	short p[4];

	p[0] = x;
	p[1] = y;
	p[2] = x + w - 1;
	p[3] = y + h - 1;
	vs_clip(vh, 1, p);
}

static void line(short x0, short y0, short x1, short y1, short col)
{
	short p[4];

	p[0] = x0; p[1] = y0; p[2] = x1; p[3] = y1;
	vsl_color(vh, col);
	v_pline(vh, 2, p);
}

static void box(short x0, short y0, short x1, short y1, short col)
{
	short p[4];

	if (x1 < x0 || y1 < y0)
		return;
	p[0] = x0; p[1] = y0; p[2] = x1; p[3] = y1;
	vsf_color(vh, col);
	vr_recfl(vh, p);
}

/* a swatch: filled with colour, black outline with rounded corners */
static void swatch(short x, short y, short w, short h, short colour)
{
	short p[18], r = (w >= 11 && h >= 11) ? 2 : 1;
	short x1 = x + w - 1, y1 = y + h - 1;

	box(x + 1, y + 1, x1 - 1, y1 - 1, colour);
	p[0] = x + r;   p[1] = y;
	p[2] = x1 - r;  p[3] = y;
	p[4] = x1;      p[5] = y + r;
	p[6] = x1;      p[7] = y1 - r;
	p[8] = x1 - r;  p[9] = y1;
	p[10] = x + r;  p[11] = y1;
	p[12] = x;      p[13] = y1 - r;
	p[14] = x;      p[15] = y + r;
	p[16] = x + r;  p[17] = y;
	vsl_color(vh, G_BLACK);
	v_pline(vh, 9, p);
	/* inner corner pixels of the 2-pixel rounding */
	if (r == 2) {
		box(x + 1, y + 1, x + 1, y + 1, G_BLACK);
		box(x1 - 1, y + 1, x1 - 1, y + 1, G_BLACK);
		box(x + 1, y1 - 1, x + 1, y1 - 1, G_BLACK);
		box(x1 - 1, y1 - 1, x1 - 1, y1 - 1, G_BLACK);
	}
}

/* ---------------------------------------------------------------- */
/* the popup                                                         */
/* ---------------------------------------------------------------- */

static void layout(CSPOP *p)
{
	short c;

	p->cols = (ncol >= 256) ? 16 : (ncol >= 16) ? 4 : 2;
	p->rows = (ncol + p->cols - 1) / p->cols;
	c = (gl_desk.g_h >= 360) ? 16 : 12;
	while (c > 8 && p->rows * c + 2 * PAD > gl_desk.g_h)
		c--;
	p->ch = c;
	p->cw = tall_pixels ? 2 * c : wide_pixels ? c / 2 + 2 : c;
	while (p->cw > 8 && p->cols * p->cw + 2 * PAD > gl_desk.g_w)
		p->cw--;
	p->r.g_w = p->cols * p->cw + 2 * PAD;
	p->r.g_h = p->rows * p->ch + 2 * PAD;
}

/* place at (x, y); when there is no room below, the popup goes above
   y_above (if >= 0), else it is moved up */
static void place(CSPOP *p, short x, short y, short y_above)
{
	short dx = gl_desk.g_x + gl_desk.g_w, dy = gl_desk.g_y + gl_desk.g_h;

	if (y + p->r.g_h > dy && y_above >= 0 && y_above - p->r.g_h >= gl_desk.g_y)
		y = y_above - p->r.g_h;
	if (x + p->r.g_w > dx)
		x = dx - p->r.g_w;
	if (y + p->r.g_h > dy)
		y = dy - p->r.g_h;
	if (x < gl_desk.g_x)
		x = gl_desk.g_x;
	if (y < gl_desk.g_y)
		y = gl_desk.g_y;
	p->r.g_x = x;
	p->r.g_y = y;
	p->gx = x + PAD;
	p->gy = y + PAD;
}

static void draw_cell(const CSPOP *p, short i, short recessed)
{
	short x = p->gx + (i % p->cols) * p->cw;
	short y = p->gy + (i / p->cols) * p->ch;
	short x1 = x + p->cw - 1, y1 = y + p->ch - 1, d = 0;

	if (i < 0 || i >= ncol)
		return;
	box(x, y, x1, y1, back_col);
	if (recessed) {
		if (use_3d) {
			line(x, y1, x, y, dark_col);
			line(x, y, x1, y, dark_col);
			line(x + 1, y1, x1, y1, G_WHITE);
			line(x1, y1, x1, y + 1, G_WHITE);
			d = 1;                      /* the swatch sinks in */
		} else {
			short q[10];

			q[0] = x;  q[1] = y;
			q[2] = x1; q[3] = y;
			q[4] = x1; q[5] = y1;
			q[6] = x;  q[7] = y1;
			q[8] = x;  q[9] = y + 1;
			vsl_color(vh, G_BLACK);
			v_pline(vh, 5, q);
			line(x + 1, y1 - 1, x + 1, y + 1, G_BLACK);
			line(x + 1, y + 1, x1 - 1, y + 1, G_BLACK);
		}
	}
	swatch(x + INSET + d, y + INSET + d, p->cw - 2 * INSET, p->ch - 2 * INSET, i);
}

static void draw_popup(const CSPOP *p, short hl)
{
	short x = p->r.g_x, y = p->r.g_y;
	short x1 = x + p->r.g_w - 1, y1 = y + p->r.g_h - 1, q[10], i;

	hide_mouse();
	clip(x, y, p->r.g_w, p->r.g_h);
	box(x, y, x1, y1, back_col);
	q[0] = x;  q[1] = y;
	q[2] = x1; q[3] = y;
	q[4] = x1; q[5] = y1;
	q[6] = x;  q[7] = y1;
	q[8] = x;  q[9] = y;
	vsl_color(vh, G_BLACK);
	v_pline(vh, 5, q);
	if (use_3d) {
		line(x + 1, y1 - 1, x + 1, y + 1, G_WHITE);
		line(x + 1, y + 1, x1 - 1, y + 1, G_WHITE);
		line(x + 2, y1 - 1, x1 - 1, y1 - 1, dark_col);
		line(x1 - 1, y1 - 1, x1 - 1, y + 2, dark_col);
	}
	for (i = 0; i < ncol; i++)
		draw_cell(p, i, i == hl);
	vs_clip(vh, 0, q);
	show_mouse();
}

static void move_hl(const CSPOP *p, short *hl, short n)
{
	if (n == *hl)
		return;
	hide_mouse();
	clip(p->r.g_x, p->r.g_y, p->r.g_w, p->r.g_h);
	draw_cell(p, *hl, 0);
	draw_cell(p, n, 1);
	vs_clip(vh, 0, (short *)&p->r);
	show_mouse();
	*hl = n;
}

/* colour under (mx, my), -1 outside the grid */
static short cell_at(const CSPOP *p, short mx, short my)
{
	short c, r, i;

	if (mx < p->gx || my < p->gy)
		return -1;
	c = (mx - p->gx) / p->cw;
	r = (my - p->gy) / p->ch;
	if (c >= p->cols || r >= p->rows)
		return -1;
	i = r * p->cols + c;
	return (i < ncol) ? i : -1;
}

static void cell_rect(const CSPOP *p, short i, GRECT *g)
{
	g->g_x = p->gx + (i % p->cols) * p->cw;
	g->g_y = p->gy + (i / p->cols) * p->ch;
	g->g_w = p->cw;
	g->g_h = p->ch;
}

/* new cursor position for a key, -2 = not a cursor key */
static short key_move(const CSPOP *p, short hl, short start, short ks, short scan)
{
	if (hl < 0)
		hl = start;
	switch (scan) {
	case SC_LEFT:
		return (hl > 0) ? hl - 1 : ncol - 1;
	case SC_RIGHT:
		return (hl + 1 < ncol) ? hl + 1 : 0;
	case SC_UP:
		return (hl >= p->cols) ? hl - p->cols : hl;
	case SC_DOWN:
		return (hl + p->cols < ncol) ? hl + p->cols : hl;
	case SC_HOME:
		return (ks & (K_LSHIFT | K_RSHIFT)) ? ncol - 1 : 0;
	}
	return -2;
}

static short popup(short x, short y, short y_above, short colour)
{
	CSPOP p;
	MFDB buf;
	GRECT m1;
	short msg[8], ev, mx, my, mb, ks, key, clicks, d;
	short hl, mc, n, drag, result = -1, done = 0, start;

	init();
	prep();
	layout(&p);
	place(&p, x, y, y_above);
	start = (colour >= 0 && colour < ncol) ? colour : 0;
	hl = (colour >= 0 && colour < ncol) ? colour : -1;

	wind_update(BEG_UPDATE);
	wind_update(BEG_MCTRL);
	graf_mouse(ARROW, NULL);
	buf.fd_addr = NULL;
	save_background(&p.r, &buf);
	draw_popup(&p, hl);

	graf_mkstate(&mx, &my, &mb, &ks);
	drag = mb & 1;              /* opened with the button still down */

	while (!done) {
		mc = cell_at(&p, mx, my);
		if (mc >= 0)
			cell_rect(&p, mc, &m1);         /* wait until it leaves the cell */
		else {
			m1.g_x = p.gx;                  /* ... or enters the grid */
			m1.g_y = p.gy;
			m1.g_w = p.cols * p.cw;
			m1.g_h = p.rows * p.ch;
		}
		ev = evnt_multi(MU_KEYBD | MU_BUTTON | MU_M1, 1, 1, drag ? 0 : 1,
		                mc >= 0, m1.g_x, m1.g_y, m1.g_w, m1.g_h,
		                0, 0, 0, 0, 0, msg, 0,
		                &mx, &my, &mb, &ks, &key, &clicks);

		if (ev & MU_M1) {
			n = cell_at(&p, mx, my);
			if (n >= 0)
				move_hl(&p, &hl, n);
		}
		if (ev & MU_KEYBD) {
			n = key_move(&p, hl, start, ks, (key >> 8) & 0xFF);
			if (n >= 0)
				move_hl(&p, &hl, n);
			else switch ((key >> 8) & 0xFF) {
			case SC_RETURN:
			case SC_ENTER:
				result = hl;
				done = 1;
				break;
			case SC_ESC:
			case SC_UNDO:
				done = 1;
				break;
			}
		}
		if ((ev & MU_BUTTON) && !done) {
			n = cell_at(&p, mx, my);
			if (drag) {
				/* released: over a colour picks it, elsewhere the popup
				   stays open for a click */
				drag = 0;
				if (n >= 0) {
					result = n;
					done = 1;
				}
			} else {
				result = n;             /* a click outside: -1 */
				done = 1;
				evnt_button(1, 1, 0, &mx, &my, &mb, &d);
			}
		}
	}

	restore_background(&p.r, &buf);
	wind_update(END_MCTRL);
	wind_update(END_UPDATE);
	return result;
}

/* ---------------------------------------------------------------- */
/* interface                                                         */
/* ---------------------------------------------------------------- */

short colsel_count(void)
{
	init();
	return ncol;
}

short colsel_popup(short x, short y, short colour)
{
	return popup(x, y, -1, colour);
}

/* colour button: frame, swatch, popup arrow */
static short __CDECL draw_button(PARMBLK *pb)
{
	short x = pb->pb_x, y = pb->pb_y, w = pb->pb_w, h = pb->pb_h;
	short x1 = x + w - 1, y1 = y + h - 1, aw = h, q[10], ax, ay, as;
	short dis = pb->pb_currstate & OS_DISABLED;

	prep();
	clip(pb->pb_xc, pb->pb_yc, pb->pb_wc, pb->pb_hc);
	box(x, y, x1, y1, use_3d ? back_col : G_WHITE);
	q[0] = x;  q[1] = y;
	q[2] = x1; q[3] = y;
	q[4] = x1; q[5] = y1;
	q[6] = x;  q[7] = y1;
	q[8] = x;  q[9] = y;
	vsl_color(vh, G_BLACK);
	v_pline(vh, 5, q);
	if (use_3d) {
		line(x + 1, y1 - 1, x + 1, y + 1, G_WHITE);
		line(x + 1, y + 1, x1 - 1, y + 1, G_WHITE);
		line(x + 2, y1 - 1, x1 - 1, y1 - 1, dark_col);
		line(x1 - 1, y1 - 1, x1 - 1, y + 2, dark_col);
	}
	/* the colour fills the whole left part, up to the arrow */
	box(x + 1, y + 1, x1 - aw, y1 - 1, (short)pb->pb_parm);
	line(x1 - aw + 1, y + 1, x1 - aw + 1, y1 - 1, G_BLACK);

	/* popup arrow: a small triangle pointing down */
	as = (aw - 4) / 2;
	if (as > 4)
		as = 4;
	ax = x1 - aw / 2;
	ay = y + (h - as) / 2;
	q[0] = ax - as; q[1] = ay;
	q[2] = ax + as; q[3] = ay;
	q[4] = ax;      q[5] = ay + as;
	q[6] = ax - as; q[7] = ay;
	vsf_color(vh, (dis && use_3d) ? dark_col : G_BLACK);
	vsl_color(vh, (dis && use_3d) ? dark_col : G_BLACK);
	v_fillarea(vh, 4, q);

	vs_clip(vh, 0, q);
	return pb->pb_currstate & ~OS_SELECTED;
}

/* The AES of TOS 1.04 (and older) calls USERDEFs on its own small stack;
   the GEMlib VDI bindings need about 2 KB of it, which overwrites AES
   variables.  So the button switches to a stack of its own first.  This
   makes colsel safe without aesfix.c; when aesfix_userdefs() wraps the
   button as well, there are simply two switches. */
#define BUTTON_STACK 4096

long colsel_stack[BUTTON_STACK / 4] __asm__("colsel_stack") __attribute__((used));
short colsel_ondraw(PARMBLK *p) __asm__("colsel_ondraw");
short __CDECL colsel_tramp(PARMBLK *p) __asm__("colsel_tramp");

/* called by the AES with the PARMBLK pointer on the stack, result in D0
   (written so that it also assembles for the ColdFire) */
__asm__(
	"	.text\n"
	"	.even\n"
	"colsel_tramp:\n"
	"	move.l	4(%sp),%a0\n"
	"	move.l	%sp,%a1\n"
	"	lea	colsel_stack+4096,%sp\n"
	"	move.l	%a1,-(%sp)\n"
	"	move.l	%a0,-(%sp)\n"
	"	jsr	colsel_ondraw\n"
	"	addq.l	#4,%sp\n"
	"	move.l	(%sp),%a1\n"
	"	move.l	%a1,%sp\n"
	"	rts\n"
);

short colsel_ondraw(PARMBLK *p)
{
	return draw_button(p);
}

short colsel_button(OBJECT *tree, short obj, short colour)
{
	USERBLK *ub;

	init();
	ub = malloc(sizeof(*ub));
	if (ub == NULL)
		return 0;
	ub->ub_code = colsel_tramp;     /* on its own stack, see above */
	ub->ub_parm = colour;
	tree[obj].ob_type = (tree[obj].ob_type & 0xFF00) | G_USERDEF;
	tree[obj].ob_spec.userblk = ub;
	return 1;
}

short colsel_get(OBJECT *tree, short obj)
{
	return (short)tree[obj].ob_spec.userblk->ub_parm;
}

void colsel_set(OBJECT *tree, short obj, short colour)
{
	tree[obj].ob_spec.userblk->ub_parm = colour;
}

short colsel_do(OBJECT *tree, short obj)
{
	short x, y, old = colsel_get(tree, obj), c;

	objc_offset(tree, obj, &x, &y);
	c = popup(x, y + tree[obj].ob_height + 1, y - 1, old);
	if (c < 0 || c == old)
		return 0;
	colsel_set(tree, obj, c);
	return 1;
}

void colsel_use_vdi(short handle)
{
	if (vh >= 0 || handle <= 0)
		return;                         /* too late, or none */
	vh = handle;
	shared = 1;
}

void colsel_exit(void)
{
	if (vh >= 0 && !shared)
		v_clsvwk(vh);
	vh = -1;
	ncol = 0;
	shared = 0;
}
