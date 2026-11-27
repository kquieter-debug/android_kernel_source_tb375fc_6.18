/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>

/* Deliberate EL1 stop for the offline B diagnostic arm. Keep SP untouched. */
SYM_FUNC_START(tb375fc_first_usb_latch)
	msr	daifset, #0xf
	movz	x30, #0xf365
	movk	x30, #0x0256, lsl #16
	movk	x30, #0x60e3, lsl #32
	dsb	sy
	.globl	tb375fc_first_usb_stop
	.type	tb375fc_first_usb_stop, %notype
tb375fc_first_usb_stop:
	b	tb375fc_first_usb_stop
SYM_FUNC_END(tb375fc_first_usb_latch)
