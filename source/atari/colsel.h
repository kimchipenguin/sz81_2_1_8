/*
 * colsel - a small colour selector popup for GEM programs (CF-Lib)
 *
 * The popup works like a popup menu without text: a grid of colour
 * swatches with rounded corners, 16 per row with 256 colours or more,
 * 4 per row with 16 colours (2 per row with 4 or 2 colours).  The field
 * under the mouse or the keyboard cursor is shown recessed.
 *
 * Mouse: click a colour; or keep the button down when the popup opens and
 *        release it over a colour.  A click outside closes the popup.
 * Keys:  cursor keys move, Home goes to colour 0, Return/Enter picks the
 *        colour, Escape/Undo closes the popup.
 *
 * The colours are VDI colour indices, 0 .. colsel_count() - 1.
 *
 * Colour buttons: colsel_button() turns any object (usually a G_BUTTON
 * with TOUCHEXIT) into a button that shows its colour and a popup arrow.
 * Call colsel_do() when the button is clicked or its shortcut pressed.
 */

#ifndef COLSEL_H
#define COLSEL_H

#include <gem.h>

/* number of colours the popup shows (2 .. 256) */
short colsel_count(void);

/* open the popup with its top left corner at (x, y) (moved onto the
   screen if needed); colour is marked at the start; returns the colour
   picked or -1 */
short colsel_popup(short x, short y, short colour);

/* make obj a colour button that shows colour; call after fix_dial() and
   before aesfix_userdefs(); returns 0 when out of memory */
short colsel_button(OBJECT *tree, short obj, short colour);

/* colour of a colour button / set it (no redraw) */
short colsel_get(OBJECT *tree, short obj);
void colsel_set(OBJECT *tree, short obj, short colour);

/* open the popup below the colour button obj; returns 1 when the colour
   changed (redraw the button then, e.g. with redraw_wdobj()) */
short colsel_do(OBJECT *tree, short obj);

/* draw on the program's VDI workstation instead of one of colsel's own
   (call before anything else of colsel; under fVDI a second workstation
   did not draw) */
void colsel_use_vdi(short handle);

/* close the VDI workstation of the module (not a shared one) */
void colsel_exit(void);

#endif
