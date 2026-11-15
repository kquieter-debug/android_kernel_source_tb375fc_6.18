/* SPDX-License-Identifier: GPL-2.0 */
/* TB375FC 6.18 移植调试专用：早期阶段标记。 */
#ifndef _LINUX_TB_EARLYLOG_H
#define _LINUX_TB_EARLYLOG_H

struct pt_regs;

void tb_mark(const char *msg);
void tb_earlylog_map(void);
void tb_rgu_stage(unsigned int stage);
void tb_beacon_init(void);
void tb_beacon_panic(unsigned long caller);
void tb_beacon_lock(unsigned long caller, const void *lock);
void tb_beacon_bad_stack(unsigned long pc, unsigned long sp,
			 unsigned long esr, unsigned long far);
void tb_initcall(void *fn);
void __noreturn tb_panic_stop(const char *text, unsigned long caller);
void __noreturn tb_lock_stop(unsigned long caller, const void *lock);
void __noreturn tb_bad_stack_stop(struct pt_regs *regs);

#endif /* _LINUX_TB_EARLYLOG_H */
