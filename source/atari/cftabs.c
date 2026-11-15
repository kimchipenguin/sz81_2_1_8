/*
 * cftabs - tab cards ("Reiter") for CF-Lib dialogs, see cftabs.h
 *
 * The look follows SysGem 2: tabs with cut corners, the active tab is
 * taller and open to its page.  The page frame is drawn here too, so the
 * gap in its top line can follow the active tab; drawing order in the tree
 * does not matter.
 *
 * All drawing happens in USERDEFs, so it must not call the AES; positions
 * come straight from the object tree.
 */

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <osbind.h>
#include <gem.h>
#include <cflib.h>
#include "cftabs.h"

#define K_SHIFTS    (K_LSHIFT | K_RSHIFT)
#define CUT         3           /* size of the cut corners in pixels */
#define LOWER       2           /* inactive tabs start this much lower */

static CFTABS *all_tabs;
static KEY_CB old_keycb;
static short vh = -1;           /* own VDI workstation for the drawing */
static short cell_w, cell_h;    /* character cell of the system font */
static short use_3d, back_col;

/* ---------------------------------------------------------------- */
/* drawing                                                           */
/* ---------------------------------------------------------------- */

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

static void begin_draw(PARMBLK *pb)
{
	short clip[4];

	clip[0] = pb->pb_xc;
	clip[1] = pb->pb_yc;
	clip[2] = pb->pb_xc + pb->pb_wc - 1;
	clip[3] = pb->pb_yc + pb->pb_hc - 1;
	vs_clip(vh, 1, clip);
	vswr_mode(vh, MD_REPLACE);
	vsf_interior(vh, FIS_SOLID);
	vsf_perimeter(vh, 0);
	vsl_type(vh, SOLID);
	vsl_width(vh, 1);
}

static short paper(void)
{
	return use_3d ? back_col : G_WHITE;
}

/* a tab: G_BUTTON text in ub_parm, MagiC shortcut in the state */
static short __CDECL draw_tab(PARMBLK *pb)
{
	OBJECT *o = &pb->pb_tree[pb->pb_obj];
	const char *text = (const char *)pb->pb_parm;
	short sel = (pb->pb_currstate & OS_SELECTED) != 0;
	short x0 = pb->pb_x, x1 = pb->pb_x + pb->pb_w - 1;
	short y1 = pb->pb_y + pb->pb_h - 1;
	short top = sel ? pb->pb_y : pb->pb_y + LOWER;
	short p[12], len, tx, ty, us, d;

	begin_draw(pb);

	/* background of the dialog above a low tab, then the tab itself */
	box(x0, pb->pb_y, x1, top - 1, paper());
	box(x0 + 1, top + 1, x1 - 1, y1, paper());

	p[0] = x0;        p[1] = y1;
	p[2] = x0;        p[3] = top + CUT;
	p[4] = x0 + CUT;  p[5] = top;
	p[6] = x1 - CUT;  p[7] = top;
	p[8] = x1;        p[9] = top + CUT;
	p[10] = x1;       p[11] = y1;
	vsl_color(vh, G_BLACK);
	v_pline(vh, 6, p);
	/* fill the cut corners outside the outline */
	line(x0, top, x0 + CUT - 1, top, paper());
	line(x0, top + 1, x0, top + CUT - 1, paper());
	line(x1 - CUT + 1, top, x1, top, paper());
	line(x1, top + 1, x1, top + CUT - 1, paper());
	if (CUT > 2) {
		line(x0 + 1, top + 1, x0 + 1, top + 1, paper());
		line(x1 - 1, top + 1, x1 - 1, top + 1, paper());
	}

	if (use_3d) {
		line(x0 + 1, y1, x0 + 1, top + CUT, G_WHITE);
		line(x0 + 1, top + CUT, x0 + CUT, top + 1, G_WHITE);
		line(x0 + CUT, top + 1, x1 - CUT, top + 1, G_WHITE);
		line(x1 - 1, top + CUT, x1 - 1, y1, G_LBLACK);
	}

	/* the text, centred, with the shortcut underlined */
	len = (short)strlen(text);
	tx = x0 + (pb->pb_w - len * cell_w) / 2;
	ty = top + (y1 - top + 1 - cell_h) / 2 + (sel ? 0 : 1);
	vswr_mode(vh, MD_TRANS);
	vst_color(vh, G_BLACK);
	vst_effects(vh, (pb->pb_currstate & OS_DISABLED) ? TXT_LIGHT : 0);
	v_gtext(vh, tx, ty, (char *)text);
	vst_effects(vh, 0);
	vswr_mode(vh, MD_REPLACE);

	us = (o->ob_state >> 8) & 0xFF;
	if ((o->ob_state & OS_WHITEBAK) && us != 0xFF && (us & 0x0F) < len) {
		d = tx + (us & 0x0F) * cell_w;
		line(d, ty + cell_h - 1, d + cell_w - 1, ty + cell_h - 1, G_BLACK);
	}
	return 0;
}

/* the page frame: a box whose top line is open under the active tab */
static short __CDECL draw_frame(PARMBLK *pb)
{
	CFTABS *t = (CFTABS *)pb->pb_parm;
	OBJECT *tr = pb->pb_tree;
	short x0 = pb->pb_x, y0 = pb->pb_y;
	short x1 = x0 + pb->pb_w - 1, y1 = y0 + pb->pb_h - 1;
	short tab = tabs_tab(t, t->active);
	short a0, a1;

	/* the active tab in screen coordinates: bar and frame are siblings */
	a0 = x0 - tr[t->pages].ob_x + tr[t->bar].ob_x + tr[tab].ob_x;
	a1 = a0 + tr[tab].ob_width - 1;

	begin_draw(pb);
	box(x0, y0, x1, y1, paper());
	line(x0, y0, x0, y1, G_BLACK);
	line(x0, y1, x1, y1, G_BLACK);
	line(x1, y0, x1, y1, G_BLACK);
	line(x0, y0, a0, y0, G_BLACK);
	line(a1, y0, x1, y0, G_BLACK);
	if (use_3d) {
		line(x0 + 1, y0 + 1, x0 + 1, y1 - 1, G_WHITE);
		if (a0 > x0 + 1)
			line(x0 + 1, y0 + 1, a0 + 1, y0 + 1, G_WHITE);
		line(a0 + 1, y0, a0 + 1, y0 + 1, G_WHITE);
		line(a1 - 1, y0, a1 - 1, y0 + 1, G_LBLACK);
		line(a1 - 1, y0 + 1, x1 - 1, y0 + 1, G_WHITE);
		line(x1 - 1, y0 + 2, x1 - 1, y1 - 1, G_LBLACK);
		line(x0 + 2, y1 - 1, x1 - 1, y1 - 1, G_LBLACK);
	}
	return 0;
}

/* ---------------------------------------------------------------- */
/* keys                                                              */
/* ---------------------------------------------------------------- */

static short enabled(const CFTABS *t, short i)
{
	return !(t->tree[tabs_tab(t, i)].ob_state & OS_DISABLED);
}

/* the shortcut letter of tab i, upper case, or 0 */
static char tab_letter(const CFTABS *t, short i)
{
	OBJECT *o = &t->tree[tabs_tab(t, i)];
	const char *text = (const char *)o->ob_spec.userblk->ub_parm;
	short us = (o->ob_state >> 8) & 0xFF;

	if (!(o->ob_state & OS_WHITEBAK) || us == 0xFF || (us & 0x0F) >= (short)strlen(text))
		return 0;
	return toupper((unsigned char)text[us & 0x0F]);
}

/* CF-Lib asks us first about every key of every dialog (form_do, mdial,
   wdial).  Returning 0 with *next_obj set ends the dialog step with that
   object, like a click on it. */
static _WORD tabs_key(OBJECT *tree, _WORD edit_obj, _WORD kstate, _WORD *kreturn, _WORD *next_obj)
{
	static _KEYTAB *keytab;
	CFTABS *t;
	short scan = (*kreturn >> 8) & 0xFF;
	short i, n;
	char ascii;

	for (t = all_tabs; t != NULL; t = t->next)
		if (t->tree == tree)
			break;
	if (t == NULL)
		return old_keycb ? old_keycb(tree, edit_obj, kstate, kreturn, next_obj) : 1;

	if ((kstate & K_CTRL) && scan == 0x0F) {       /* Control+(Shift+)Tab */
		i = t->active;
		for (n = 0; n < t->count; n++) {
			i = (kstate & K_SHIFTS) ? (i + t->count - 1) % t->count : (i + 1) % t->count;
			if (enabled(t, i))
				break;
		}
		*kreturn = 0;
		if (i == t->active)
			return 1;
		*next_obj = tabs_tab(t, i);
		return 0;
	}

	if ((kstate & K_ALT) && !(kstate & K_CTRL)) {   /* Alternate+letter */
		if (keytab == NULL)
			keytab = (_KEYTAB *)Keytbl((void *)-1, (void *)-1, (void *)-1);
		if (scan >= 0x78 && scan < 0x80)            /* Alternate+digit */
			ascii = keytab->caps[scan - 0x76];
		else if (scan < 0x80)
			ascii = keytab->caps[scan];
		else
			ascii = 0;
		for (i = 0; ascii && i < t->count; i++) {
			if (enabled(t, i) && tab_letter(t, i) == toupper((unsigned char)ascii)) {
				*kreturn = 0;
				if (i == t->active)
					return 1;
				*next_obj = tabs_tab(t, i);
				return 0;
			}
		}
	}

	return old_keycb ? old_keycb(tree, edit_obj, kstate, kreturn, next_obj) : 1;
}

/* ---------------------------------------------------------------- */
/* set up                                                            */
/* ---------------------------------------------------------------- */

static short count_children(OBJECT *tree, short parent)
{
	short n = 0, i;

	for (i = tree[parent].ob_head; i != parent && i >= 0; i = tree[i].ob_next)
		n++;
	return n;
}

static short nth_child(const OBJECT *tree, short parent, short n)
{
	short i = tree[parent].ob_head;

	while (n-- > 0)
		i = tree[i].ob_next;
	return i;
}

short tabs_tab(const CFTABS *t, short i)
{
	return nth_child(t->tree, t->bar, i);
}

short tabs_page(const CFTABS *t, short i)
{
	return nth_child(t->tree, t->pages, i);
}

short tabs_index(const CFTABS *t, short obj)
{
	short i, o = t->tree[t->bar].ob_head;

	for (i = 0; i < t->count; i++, o = t->tree[o].ob_next)
		if (o == obj)
			return i;
	return -1;
}

static void init_vdi(void)
{
	short work_out[57], attr[10], ap1, ap2, ap3, ap4, d;

	vh = open_vwork(work_out);
	vqt_attributes(vh, attr);
	cell_w = attr[8];
	cell_h = attr[9];
	vst_alignment(vh, TA_LEFT, TA_TOP, &d, &d);

	use_3d = 0;
	back_col = G_WHITE;
	if (appl_xgetinfo(13, &ap1, &ap2, &ap3, &ap4)) {
		if (ap1 == 1 && work_out[13] >= 16)
			use_3d = 1;
	} else if (mt_AESversion(aes_global) >= 0x340 && work_out[13] >= 4) {
		/* TOS 4.0x (AES 3.40) has 3D objects but no appl_getinfo(),
		   and draws them from 4 colours on */
		use_3d = 1;
	}
	if (use_3d)
		objc_sysvar(0, BACKGRCOL, 0, 0, &back_col, &d);
}

void tabs_show(CFTABS *t, short i)
{
	short n;

	t->active = i;
	for (n = 0; n < t->count; n++) {
		set_state(t->tree, tabs_tab(t, n), OS_SELECTED, n == i);
		set_flag(t->tree, tabs_page(t, n), OF_HIDETREE, n != i);
	}
}

/* make the page frame dw wider and dh taller; the dialog grows with it.
   Objects right of the frame move along, objects below it move down, and
   those of them that reach into the right half (OK, Cancel) keep their
   distance to the right edge.  The frame must be a child of the root. */
static void grow(OBJECT *tree, short pages, short bar, short dw, short dh)
{
	short fx1 = tree[pages].ob_x + tree[pages].ob_width;
	short fy1 = tree[pages].ob_y + tree[pages].ob_height;
	short half = tree[ROOT].ob_width / 2;
	short c;

	for (c = tree[ROOT].ob_head; c > ROOT; c = tree[c].ob_next) {
		if (c == pages || c == bar)
			continue;
		if (tree[c].ob_x >= fx1 ||
		    (tree[c].ob_y >= fy1 && tree[c].ob_x + tree[c].ob_width > half))
			tree[c].ob_x += dw;
		if (tree[c].ob_y >= fy1)
			tree[c].ob_y += dh;
	}
	tree[pages].ob_width += dw;
	tree[pages].ob_height += dh;
	tree[ROOT].ob_width += dw;
	tree[ROOT].ob_height += dh;
}

short tabs_init(CFTABS *t, OBJECT *tree, short bar, short pages, short active)
{
	USERBLK *ub;
	short i, o, x;

	t->tree = tree;
	t->bar = bar;
	t->pages = pages;
	t->base = NULL;
	t->count = count_children(tree, bar);
	if (t->count < 1 || t->count != count_children(tree, pages))
		return 0;
	if (active < 0 || active >= t->count)
		active = 0;

	ub = malloc((t->count + 1) * sizeof(USERBLK));
	if (ub == NULL)
		return 0;
	if (vh < 0)
		init_vdi();

	/* tab widths from their texts (translations), the first tab keeps its
	   x; the frame grows when the tabs don't fit */
	x = tree[tabs_tab(t, 0)].ob_x;
	for (i = 0; i < t->count; i++) {
		o = tabs_tab(t, i);
		tree[o].ob_x = x;
		tree[o].ob_width = ((short)strlen(tree[o].ob_spec.free_string) + 2) * cell_w;
		x += tree[o].ob_width;
	}
	x += cell_w;
	if (x > tree[pages].ob_width)
		grow(tree, pages, bar, x - tree[pages].ob_width, 0);

	/* the bar sits right on top of the frame, the tabs fill its height */
	tree[bar].ob_x = tree[pages].ob_x;
	tree[bar].ob_y = tree[pages].ob_y - tree[bar].ob_height;
	tree[bar].ob_width = tree[pages].ob_width;
	for (i = 0; i < t->count; i++) {
		o = tabs_tab(t, i);
		tree[o].ob_y = 0;
		tree[o].ob_height = tree[bar].ob_height;
		ub[i].ub_code = draw_tab;
		ub[i].ub_parm = (long)tree[o].ob_spec.free_string;
		tree[o].ob_type = (tree[o].ob_type << 8) | G_USERDEF;
		tree[o].ob_spec.userblk = &ub[i];
	}
	ub[i].ub_code = draw_frame;
	ub[i].ub_parm = (long)t;
	tree[pages].ob_type = (tree[pages].ob_type << 8) | G_USERDEF;
	tree[pages].ob_spec.userblk = &ub[i];

	tabs_show(t, active);

	t->next = all_tabs;
	all_tabs = t;
	if (t->next == NULL)
		old_keycb = set_formdo_keycb(tabs_key);
	return 1;
}

/* number of objects in a tree */
static short tree_size(const OBJECT *tree)
{
	short n = 0;

	while (!(tree[n].ob_flags & OF_LASTOB))
		n++;
	return n + 1;
}

OBJECT *tabs_init_trees(CFTABS *t, OBJECT *tree, short bar, short pages,
                        OBJECT **page_trees, short count, short active)
{
	OBJECT *m, *o;
	short *base, nmain, total, n, i, k, j, pw = 0, ph = 0, dw = 0, dh = 0;

	if (count < 1 || tree[pages].ob_head >= 0 || count_children(tree, bar) != count)
		return NULL;
	nmain = tree_size(tree);
	total = nmain;
	for (i = 0; i < count; i++) {
		total += tree_size(page_trees[i]);
		if (page_trees[i][ROOT].ob_width > pw)
			pw = page_trees[i][ROOT].ob_width;
		if (page_trees[i][ROOT].ob_height > ph)
			ph = page_trees[i][ROOT].ob_height;
	}
	m = malloc((long)total * sizeof(OBJECT));
	base = malloc((count + 1) * sizeof(short));
	if (m == NULL || base == NULL) {
		free(m);
		free(base);
		return NULL;
	}

	memcpy(m, tree, nmain * sizeof(OBJECT));
	m[nmain - 1].ob_flags &= ~OF_LASTOB;

	/* room for the largest page plus a character all around */
	if (pw + 2 * gl_wchar > m[pages].ob_width)
		dw = pw + 2 * gl_wchar - m[pages].ob_width;
	if (ph + 2 * gl_hchar > m[pages].ob_height)
		dh = ph + 2 * gl_hchar - m[pages].ob_height;
	if (dw || dh)
		grow(m, pages, bar, dw, dh);

	/* append the pages, links moved by their new position */
	j = nmain;
	for (i = 0; i < count; i++) {
		n = tree_size(page_trees[i]);
		base[i] = j;
		memcpy(&m[j], page_trees[i], n * sizeof(OBJECT));
		for (k = 0; k < n; k++) {
			o = &m[j + k];
			if (o->ob_next >= 0)
				o->ob_next += j;
			if (o->ob_head >= 0)
				o->ob_head += j;
			if (o->ob_tail >= 0)
				o->ob_tail += j;
			o->ob_flags &= ~OF_LASTOB;
		}
		/* the page root: an invisible box, centred in the frame */
		o = &m[j];
		o->ob_next = (i < count - 1) ? j + n : pages;
		o->ob_type = G_IBOX;
		o->ob_spec.index = 0;
		o->ob_state = OS_NORMAL;
		o->ob_x = (m[pages].ob_width - o->ob_width) / 2;
		o->ob_y = (m[pages].ob_height - o->ob_height) / 2;
		j += n;
	}
	base[count] = j;
	m[total - 1].ob_flags |= OF_LASTOB;
	m[pages].ob_head = base[0];
	m[pages].ob_tail = base[count - 1];

	if (!tabs_init(t, m, bar, pages, active)) {
		free(m);
		free(base);
		return NULL;
	}
	t->base = base;
	return m;
}

short tabs_obj(const CFTABS *t, short page, short obj)
{
	if (t->base == NULL || page < 0 || page >= t->count)
		return -1;
	return t->base[page] + obj;
}

short tabs_which(const CFTABS *t, short obj, short *page, short *page_obj)
{
	short i;

	obj &= 0x7FFF;
	if (t->base != NULL)
		for (i = 0; i < t->count; i++)
			if (obj >= t->base[i] && obj < t->base[i + 1]) {
				*page = i;
				*page_obj = obj - t->base[i];
				return 1;
			}
	*page = -1;
	*page_obj = obj;
	return 0;
}

void tabs_exit(void)
{
	if (all_tabs != NULL)
		set_formdo_keycb(old_keycb);
	all_tabs = NULL;
	if (vh >= 0)
		v_clsvwk(vh);
	vh = -1;
}

/* ---------------------------------------------------------------- */
/* switching                                                         */
/* ---------------------------------------------------------------- */

/* parent of obj, -1 for the root */
static short parent(const OBJECT *tree, short obj)
{
	short next;

	if (obj <= ROOT)
		return -1;
	for (;;) {
		next = tree[obj].ob_next;
		if (next < 0)
			return -1;
		if (tree[next].ob_tail == obj)
			return next;
		obj = next;
	}
}

/* first edit field on page i that can take the cursor, or 0 */
static short first_edit(const CFTABS *t, short i)
{
	OBJECT *tree = t->tree;
	short page = tabs_page(t, i);
	short o = 0, p, ok;

	do {
		o++;
		if (!(tree[o].ob_flags & OF_EDITABLE) || (tree[o].ob_state & OS_DISABLED))
			continue;
		ok = 0;
		for (p = o; p > ROOT; p = parent(tree, p)) {
			if (tree[p].ob_flags & OF_HIDETREE)
				break;
			if (p == page)
				ok = 1;
		}
		if (ok && p <= ROOT)
			return o;
	} while (!(tree[o].ob_flags & OF_LASTOB));
	return 0;
}

/* index of the tab to switch to, or -1 if obj is no (usable) tab */
static short wanted(CFTABS *t, short obj)
{
	short i = tabs_index(t, obj & 0x7FFF);

	if (i < 0)
		return -1;
	if (!enabled(t, i))
		return t->active;
	return i;
}

short tabs_wdial(CFTABS *t, WDIALOG *wd, short obj)
{
	short i = wanted(t, obj);

	if (i < 0)
		return 0;
	if (i != t->active) {
		tabs_show(t, i);
		/* no cursor while the page is drawn, then the first edit field
		   of the new page (CF-Lib skips fields on hidden pages) */
		wd->edit_obj = 0;
		wd->edit_idx = 0;
		if (wd->mode == WD_OPEN) {
			redraw_wdobj(wd, t->pages);
			redraw_wdobj(wd, t->bar);
			change_wdedit(wd, first_edit(t, i));
		} else
			wd->edit_obj = first_edit(t, i);
	}
	return 1;
}

short tabs_mdial(CFTABS *t, MDIAL *md, short obj)
{
	short i = wanted(t, obj);

	if (i < 0)
		return 0;
	if (i != t->active) {
		tabs_show(t, i);
		md->edit_obj = 0;
		md->edit_idx = 0;
		redraw_mdobj(md, t->pages);
		redraw_mdobj(md, t->bar);
		change_mdedit(md, first_edit(t, i));
	}
	return 1;
}
