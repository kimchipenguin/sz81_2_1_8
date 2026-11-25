/* z80regs.h - the Z80 state kept in CPU registers (68000 family)
 *
 * The hottest variables live in registers, as GCC global register
 * variables: d3-d7 and a5 are callee-saved, so the libraries keep them, and
 * every file that includes this header leaves them alone. They have no
 * address (sdl_loadsave.c copies them). Include it before any code that
 * uses them. -DNO_Z80_REGVARS turns it off.
 */
#ifndef Z80REGS_H
#define Z80REGS_H

#if defined(PLATFORM_ATARI) && defined(__m68k__) && !defined(NO_Z80_REGVARS)
#define Z80_REGVARS
register unsigned short pc asm("d7");
register unsigned char a asm("d6");
register unsigned char f asm("d5");
register unsigned char h asm("d4");
register unsigned char l asm("d3");
register unsigned long tstates asm("a5");
#else
extern unsigned short pc;
extern unsigned char a, f, h, l;
extern unsigned long tstates;
#endif

#endif
