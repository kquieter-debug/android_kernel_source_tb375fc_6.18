#include <linux/linkage.h>
#include <asm/assembler.h>

	.section .init.text, "ax"
SYM_FUNC_START(tb_fb_tail_probe)
	msr	daifset, #0xf
	mov_q	x30, 0x4e4f434145424254
	mov_q	x9, 0xffffe000
	msr	spsel, #1
	mov	sp, x9
	dc	civac, x0
	dsb	sy
	ldp	x1, x2, [x0]
	ldp	x3, x4, [x0, #16]
	ldp	x5, x6, [x0, #32]
	cmp	x1, x30
	b.ne	tb_fb_tail_mismatch
	cmp	x2, #1
	b.ne	tb_fb_tail_mismatch
	orr	x7, x3, x4
	orr	x7, x7, x5
	orr	x7, x7, x6
	cbnz	x7, tb_fb_tail_mismatch
	b	tb_fb_tail_pass
SYM_FUNC_END(tb_fb_tail_probe)

SYM_CODE_START(tb_fb_tail_pass)
	b	tb_fb_tail_pass
SYM_CODE_END(tb_fb_tail_pass)

SYM_CODE_START(tb_fb_tail_mismatch)
	b	tb_fb_tail_mismatch
SYM_CODE_END(tb_fb_tail_mismatch)
