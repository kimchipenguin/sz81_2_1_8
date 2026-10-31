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
|
| Only instructions and addressing modes that the 68000 and ColdFire share
| are used, so the same source serves the FireBee build.

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
	lea	-12(%sp),%sp
	movem.l	%d1-%d2/%a1,(%sp)
	moveq	#0x7f,%d1
	and.l	%d0,%d1
	moveq	#0,%d2
	btst	#7,%d0
	bne.s	1f
	moveq	#-1,%d2
1:	lea	_ikbd_hook_down(%pc),%a1
	move.b	%d2,(%a1,%d1.l)		| down[key] = $ff on press, 0 on release
	tst.b	%d2
	beq.s	2f
	lea	_ikbd_hook_hit(%pc),%a1
	move.b	%d2,(%a1,%d1.l)		| hit[key]: pressed since sz81 looked
2:	movem.l	(%sp),%d1-%d2/%a1
	lea	12(%sp),%sp
	move.l	_ikbd_hook_old(%pc),-(%sp)	| chain: rts jumps to the old handler
	tst.l	(%sp)
	bne.s	3f
	addq.l	#4,%sp			| no old handler: just return
3:	rts

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
	lea	-60(%sp),%sp
	movem.l	%d0-%d7/%a0-%a6,(%sp)
	jsr	_ikbd_remove_super
	movem.l	(%sp),%d0-%d7/%a0-%a6
	lea	60(%sp),%sp
	move.l	_ikbd_term_old,-(%sp)
	tst.l	(%sp)
	bne.s	4f
	addq.l	#4,%sp
4:	rts
