/*
 * cftabs - tab cards ("Reiter") for CF-Lib dialogs
 *
 * A tab dialog is one object tree with two parts:
 *
 *   bar    a G_IBOX whose children are the tabs: G_BUTTONs with TOUCHEXIT,
 *          WHITEBAK and the shortcut position in the high byte of ob_state
 *          (MagiC style, 0xFF = no shortcut).
 *   pages  a G_BOX (the frame around the pages) whose children are the
 *          pages, usually G_IBOXes of the same size.  Child n of the bar
 *          belongs to child n of the page box.  Or: an empty G_BOX, and
 *          every page in its own tree, see tabs_init_trees().
 *
 * The tab widths come from their texts (so translations fit), only the
 * x of the first tab is used; the frame grows when the tabs need more room.
 *
 * tabs_init() turns the tabs and the page frame into USERDEFs that draw the
 * tab look, places the bar on top of the page frame and hides all pages but
 * the active one.  Clicks and shortcuts on a tab reach the exit callback of
 * the window dialog (or come back from do_mdial()); pass the object to
 * tabs_wdial() / tabs_mdial(), which switch the page when it was a tab.
 *
 * Keys: Alternate + the underlined letter selects a tab, Control+Tab and
 * Control+Shift+Tab go to the next and the previous tab.
 *
 * Needs the CF-Lib patch that skips edit fields and shortcuts inside hidden
 * boxes (see readme.md).
 */

#ifndef CFTABS_H
#define CFTABS_H

#include <gem.h>
#include <cflib.h>

typedef struct cftabs {
	OBJECT *tree;
	short bar;              /* parent of the tabs */
	short pages;            /* parent of the pages (the frame) */
	short count;            /* number of tabs and pages */
	short active;           /* index of the page shown */
	short *base;            /* tabs_init_trees(): first object of each page */
	struct cftabs *next;    /* all tab dialogs, for the key handler */
} CFTABS;

/* set up a tab dialog; call after fix_dial() and before the dialog is
   opened; returns 0 if bar and pages don't fit together */
short tabs_init(CFTABS *t, OBJECT *tree, short bar, short pages, short active);

/* the same with every page in its own tree (easier to translate): tree
   has an empty page frame, page_trees[i] belongs to tab i.  All trees
   must have been through fix_dial().  Returns a new tree that holds the
   dialog and all pages (use it for the dialog), or NULL.  The page trees
   are copied, but share their texts (TEDINFOs, strings) with the copy.
   The frame and the dialog grow when a page is too big. */
OBJECT *tabs_init_trees(CFTABS *t, OBJECT *tree, short bar, short pages,
                        OBJECT **page_trees, short count, short active);

/* object obj of page tree page in the merged tree, -1 without pages */
short tabs_obj(const CFTABS *t, short page, short obj);

/* the page and the object number in its page tree for a merged object;
   returns 0 (page = -1, page_obj = obj) for objects outside the pages */
short tabs_which(const CFTABS *t, short obj, short *page, short *page_obj);

/* index of the tab object obj, or -1 */
short tabs_index(const CFTABS *t, short obj);

/* object number of tab / page number i */
short tabs_tab(const CFTABS *t, short i);
short tabs_page(const CFTABS *t, short i);

/* show page i, no drawing (for a dialog that is not open) */
void tabs_show(CFTABS *t, short i);

/* if obj (from the exit callback) is a tab: show its page, redraw, move
   the cursor to the first edit field of the page and return 1 (keep the
   dialog open); otherwise return 0 */
short tabs_wdial(CFTABS *t, WDIALOG *wd, short obj);
short tabs_mdial(CFTABS *t, MDIAL *md, short obj);

/* free the VDI workstation of the tab drawing */
void tabs_exit(void);

#endif
