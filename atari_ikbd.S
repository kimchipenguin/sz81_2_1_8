| Keyboard vector handler for the Atari build (see atari.c)
|
| TOS 2.0+ calls the "kbdvec" (the long in front of the KBDVECS table) for
| every keyboard byte from the IKBD: d0 = scancode, bit 7 set when the key
| is released; a0 = keyboard IOREC. FreeMiNT hooks the same vector. This
| handler notes the key state and passes the byte on unchanged to the
| previous handler.
|
| The block from ikbd_hook_start to ikbd_hook_end is copied into global
| memory (it runs in interrupts while other processes are active), so all
| references inside it are PC-relative.

	.text
	.even
	.globl	_ikbd_hook_start, _ikbd_hook_end, _ikbd_hook_old
	.globl	_ikbd_hook_handler, _ikbd_hook_down, _ikbd_hook_hit

_ikbd_hook_start:
	.ascii	"XBRA"
	.ascii	"SZ81"
_ikbd_hook_old:
	.long	0
_ikbd_hook_handler:
	movem.l	%d1/%a1,-(%sp)
	move.w	%d0,%d1
	and.w	#0x7f,%d1
	lea	_ikbd_hook_down(%pc),%a1
	btst	#7,%d0
	seq	(%a1,%d1.w)		| down[key] = $ff on press, 0 on release
	bne.s	1f
	lea	_ikbd_hook_hit(%pc),%a1
	st	(%a1,%d1.w)		| hit[key]: pressed since sz81 looked
1:	movem.l	(%sp)+,%d1/%a1
	move.l	_ikbd_hook_old(%pc),-(%sp)	| chain: rts jumps to the old handler
	tst.l	(%sp)
	bne.s	2f
	addq.l	#4,%sp			| no old handler: just return
2:	rts

_ikbd_hook_down:
	.space	128
_ikbd_hook_hit:
	.space	128
	.even
_ikbd_hook_end:


| Termination vector (etv_term): removes the hook when the program ends in
| any way (also after bombs under plain TOS), then chains on.
	.globl	_ikbd_term_stub, _ikbd_term_old, _ikbd_remove_super

	.even
	.ascii	"XBRA"
	.ascii	"SZ81"
_ikbd_term_old:
	.long	0
_ikbd_term_stub:
	movem.l	%d0-%d7/%a0-%a6,-(%sp)
	jsr	_ikbd_remove_super
	movem.l	(%sp)+,%d0-%d7/%a0-%a6
	move.l	_ikbd_term_old,-(%sp)
	tst.l	(%sp)
	bne.s	3f
	addq.l	#4,%sp
3:	rts
