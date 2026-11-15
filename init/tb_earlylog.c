// SPDX-License-Identifier: GPL-2.0
/*
 * TB375FC 早期启动阶段标记。FDT 就绪前只写内核缓冲区；之后尝试向
 * LK 保留的 framebuffer 画色条，不再访问未经验证的 pstore/SRAM 通道。
 * LK 正常初始化为 2944x1840、32bpp；扫描缓冲区仍须实机确认。
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/of_fdt.h>
#include <linux/sprintf.h>
#include <linux/tb_earlylog.h>
#include <linux/unaligned.h>
#include <asm/barrier.h>
#include <asm/fixmap.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>

/*
 * 跨复位 RAM beacon：同时写入多个保留区末页（framebuffer 尾、CCU、
 * ME log、APU coredump、TEE log、SLBC）。6.1 rescue 内核在
 * early_initcall 扫描同一组地址，并把结果输出到 /proc/tb_rescue。
 */
#define TB_BEACON_MAGIC_U64	0x4e4f434145424254ULL /* "TBBEACON" */
#define TB_BEACON_VERSION	1
#define TB_BEACON_MAX_ENTRIES	64
#define TB_BEACON_TARGETS	6

/* 多候选保留区末页：6.1 early_initcall 读取器扫描同一组地址。 */
static const phys_addr_t tb_beacon_pa[TB_BEACON_TARGETS] = {
	0xffffe000ULL,	/* framebuffer 末页（padding） */
	0x800ff000ULL,	/* mblock-31 mm_ccu_reserved 末页 */
	0x7fffe000ULL,	/* mblock-42 me_CSFFWLOG 末页 */
	0x8efff000ULL,	/* mblock-33 apu aee-coredump 末页 */
	0x23dc1f000ULL,	/* mblock-25 security_tee_log 末页 */
	0xfc16e000ULL,	/* mblock-44 slbc_shared 末页 */
};

#define TB_BF_PREINIT		0x1
#define TB_BF_PANIC		0x2
#define TB_BF_LOCK		0x4
#define TB_BF_BADSTACK		0x8

struct tb_beacon_entry {
	u32 stage;
	u32 flags;
	u64 caller;
	u64 a;
	u64 b;
};

struct tb_beacon {
	char magic[8];
	u32 version;
	u32 count;
	u64 last_stage;
	u64 current_initcall;
	u64 current_seq;
	u64 pad;
	struct tb_beacon_entry entries[TB_BEACON_MAX_ENTRIES];
};

static_assert(offsetof(struct tb_beacon, version) == 8);
static_assert(offsetof(struct tb_beacon, count) == 12);
static_assert(offsetof(struct tb_beacon, last_stage) == 16);
static_assert(offsetof(struct tb_beacon, current_initcall) == 24);
static_assert(offsetof(struct tb_beacon, current_seq) == 32);
static_assert(offsetof(struct tb_beacon, pad) == 40);
static_assert(offsetof(struct tb_beacon, entries) == 48);

extern void __noreturn tb_fb_tail_probe(const struct tb_beacon *bc);

#define TB_LOG_SIZE	0x1000
#define TB_FB_BASE	0xfc16f000ULL
#define TB_FB_WIDTH	2944
#define TB_FB_HEIGHT	1840
#define TB_BAR_WIDTH	1024
#define TB_BAR_HEIGHT	32
#define TB_BAR_STEP	48
#define TB_MAX_MARKS	48

static char tb_buf[TB_LOG_SIZE];
static unsigned int tb_pos;
static unsigned int tb_stage;
static phys_addr_t tb_fb;
static struct tb_beacon *tb_beacons[TB_BEACON_TARGETS];

/* 色条显示通道未验证，本轮只用 RAM beacon。 */
static bool tb_draw_enabled;

static void tb_draw_stage(unsigned int stage)
{
	static const u32 colors[] = {
		0x00ff0000, 0x0000ff00, 0x000000ff,
		0x00ffff00, 0x0000ffff, 0x00ff00ff,
	};
	unsigned int y, x, row = 64 + stage * TB_BAR_STEP;
	u32 color = colors[stage % ARRAY_SIZE(colors)];

	if (!tb_draw_enabled || !tb_fb || stage >= TB_MAX_MARKS ||
	    row + TB_BAR_HEIGHT > TB_FB_HEIGHT)
		return;

	for (y = row; y < row + TB_BAR_HEIGHT; y++) {
		phys_addr_t mapped = 0;

		for (x = 64; x < 64 + TB_BAR_WIDTH; x++) {
			phys_addr_t pa = tb_fb + ((u64)y * TB_FB_WIDTH + x) * 4;
			phys_addr_t page = pa & PAGE_MASK;
			void __iomem *pixel;

			if (page != mapped) {
				if (mapped) {
					dsb(sy);
					__set_fixmap(FIX_TB_LOG, 0, FIXMAP_PAGE_CLEAR);
				}
				__set_fixmap(FIX_TB_LOG, page, FIXMAP_PAGE_IO);
				mapped = page;
			}
			pixel = (void __iomem *)(__fix_to_virt(FIX_TB_LOG) +
						(pa & ~PAGE_MASK));
			writel_relaxed(color, pixel);
		}
		/* 换页或撤销映射前保证像素写入完成。 */
		dsb(sy);
		__set_fixmap(FIX_TB_LOG, 0, FIXMAP_PAGE_CLEAR);
	}
}

/*
 * RGU/WDT NONRST_REG2 的 [31:29] 阶段位（与 MTK AEE hangdet 同款语义）。
 * 该寄存器不复位，next LK 启动会打印 [RGU] NONRST_REG2，可跨复位回读。
 * 仅修改阶段位，其余位保持原值。
 */
#define TB_RGU_PA		0x1c00a000UL
#define TB_RGU_NONRST_REG2	0x24
#define TB_RGU_STAGE_OFS	29
#define TB_RGU_STAGE_MASK	0x7u

void tb_rgu_stage(unsigned int stage)
{
	phys_addr_t pa = TB_RGU_PA + TB_RGU_NONRST_REG2;
	phys_addr_t page = pa & PAGE_MASK;
	void __iomem *p;
	u32 v;

	__set_fixmap(FIX_TB_LOG, page, FIXMAP_PAGE_IO);
	p = (void __iomem *)(__fix_to_virt(FIX_TB_LOG) + (pa & ~PAGE_MASK));
	v = readl_relaxed(p);
	v = (v & ~(TB_RGU_STAGE_MASK << TB_RGU_STAGE_OFS)) |
	    ((stage & TB_RGU_STAGE_MASK) << TB_RGU_STAGE_OFS);
	writel_relaxed(v, p);
	dsb(sy);
	__set_fixmap(FIX_TB_LOG, 0, FIXMAP_PAGE_CLEAR);
}

static void __no_sanitize_address tb_beacon_flush(struct tb_beacon *bc)
{
	unsigned long va, end;

	if (!bc)
		return;

	va = (unsigned long)bc;
	end = va + sizeof(struct tb_beacon);
	for (; va < end; va += 64)
		asm volatile("dc cvac, %0" : : "r" (va) : "memory");
	dsb(sy);
}

static void __no_sanitize_address noinline
tb_beacon_add(u32 stage, u32 flags, u64 caller, u64 a, u64 b)
{
	int i;

	for (i = 0; i < TB_BEACON_TARGETS; i++) {
		struct tb_beacon *bc = tb_beacons[i];
		u32 idx;

		if (!bc)
			continue;

		idx = bc->count;
		if (idx < TB_BEACON_MAX_ENTRIES) {
			struct tb_beacon_entry *e = &bc->entries[idx];

			e->stage = stage;
			e->flags = flags;
			e->caller = caller;
			e->a = a;
			e->b = b;
			bc->count = idx + 1;
		}
		bc->last_stage = stage;
		tb_beacon_flush(bc);
	}
}

/*
 * early_fixmap_init() 之后调用：把各候选区末页固定映射到 fixmap，
 * 写入 beacon 头。此后每个 tb_mark 都会追加记录并 clean 到 PoC。
 */
void __init __no_sanitize_address tb_beacon_init(void)
{
	int i;

	for (i = 0; i < 1; i++) {
		struct tb_beacon *bc;

		__set_fixmap((enum fixed_addresses)(FIX_TB_BEACON + i),
			     tb_beacon_pa[i], FIXMAP_PAGE_NORMAL);
		bc = (struct tb_beacon *)__fix_to_virt(FIX_TB_BEACON + i);

		*(volatile u64 *)bc->magic = TB_BEACON_MAGIC_U64;
		bc->version = TB_BEACON_VERSION;
		bc->count = 0;
		bc->last_stage = 0;
		bc->current_initcall = 0;
		bc->current_seq = 0;
		bc->pad = 0;
		tb_beacons[i] = bc;
		tb_beacon_flush(bc);
		tb_fb_tail_probe(bc);
	}

	/* 记录 beacon 建立前已经过的 start_kernel/setup_arch 入口。 */
	tb_beacon_add(0, TB_BF_PREINIT, 0, tb_stage, 0);
}

void tb_beacon_panic(unsigned long caller)
{
	tb_beacon_add(0xff, TB_BF_PANIC, caller, tb_stage, tb_pos);
}

void tb_beacon_lock(unsigned long caller, const void *lock)
{
	tb_beacon_add(0xfe, TB_BF_LOCK, caller,
		      (u64)(unsigned long)lock, tb_stage);
}

void tb_beacon_bad_stack(unsigned long pc, unsigned long sp,
			 unsigned long esr, unsigned long far)
{
	(void)far;
	tb_beacon_add(0xfd, TB_BF_BADSTACK, pc, sp, esr);
}

/* 每个 initcall 前更新 header 中的滚动槽，只 clean 头部 cache line。 */
void __no_sanitize_address tb_initcall(void *fn)
{
	int i;

	for (i = 0; i < TB_BEACON_TARGETS; i++) {
		struct tb_beacon *bc = tb_beacons[i];

		if (!bc)
			continue;
		bc->current_initcall = (u64)(unsigned long)fn;
		bc->current_seq++;
		asm volatile("dc cvac, %0" : : "r" (bc) : "memory");
	}
	dsb(sy);
}

/* setup_machine_fdt() 后调用；MTK ATAG 是小端原生结构，不是 FDT cells。 */
void __init tb_earlylog_map(void)
{
	const u8 *tag;
	unsigned int stage;
	int node, len;
	u64 base;
	u32 found, size;

	if (!initial_boot_params)
		return;
	node = fdt_path_offset(initial_boot_params, "/chosen");
	if (node < 0)
		return;
	tag = fdt_getprop(initial_boot_params, node, "atag,videolfb", &len);
	if (!tag || len < 24)
		return;
	base = get_unaligned_le64(tag);
	found = get_unaligned_le32(tag + 8);
	size = get_unaligned_le32(tag + 16);
	/* 仅允许操作本设备已取证的缓冲区，避免错误 ATAG 导致任意物理写。 */
	if (base != TB_FB_BASE || !found ||
	    size < TB_FB_WIDTH * TB_FB_HEIGHT * 4)
		return;

	tb_fb = base;
	for (stage = 0; stage < tb_stage && stage < TB_MAX_MARKS; stage++)
		tb_draw_stage(stage);
}

/* WDT 文本提供 PC/LR：PC 的槽位编码阶段，LR 保存 panic 调用地址。 */
void __noreturn noinline tb_panic_stop(const char *text, unsigned long caller)
{
	tb_beacon_panic(caller);
	asm volatile(
		"mov x0, %0\n"
		"mov x1, %1\n"
		"mov x2, %2\n"
		"mov x3, %3\n"
		"mov x4, %4\n"
		"mov x30, x1\n"
		"adr x16, 2f\n"
		"and x17, x2, #63\n"
		"add x16, x16, x17, lsl #2\n"
		"br x16\n"
		"2:\n"
		".rept 64\n"
		"b .\n"
		".endr\n"
		:
		: "r" (0x544250414e494331UL), "r" (caller),
		  "r" ((unsigned long)tb_stage), "r" ((unsigned long)tb_pos),
		  "r" (text)
		: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
		  "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
		  "x16", "x17", "x30", "memory");
	__builtin_unreachable();
}

/* 独立 PC 槽区分取锁故障；LR 指向 _raw_spin_lock_irqsave 的调用方。 */
void __noreturn noinline tb_lock_stop(unsigned long caller, const void *lock)
{
	tb_beacon_lock(caller, lock);
	asm volatile(
		"mov x0, %0\n"
		"mov x1, %1\n"
		"mov x2, %2\n"
		"mov x30, x0\n"
		"adr x16, 2f\n"
		"and x17, x2, #63\n"
		"add x16, x16, x17, lsl #2\n"
		"br x16\n"
		"2:\n"
		".rept 64\n"
		"b .\n"
		".endr\n"
		:
		: "r" (caller), "r" (lock), "r" ((unsigned long)tb_stage)
		: "x0", "x1", "x2", "x16", "x17", "x30", "memory");
	__builtin_unreachable();
}

/*
 * __bad_stack 判定内核栈越界后立即停驻，避免在已溢出的栈上继续 panic 打印。
 * TF-A WDT 现场可回读：PC=本槽位，LR=异常 PC，SP=被判越界的 SP，
 * SP_EL0=ESR_EL1，TTBR0=FAR_EL1。以 EL1h 停驻，此后不再访问内存。
 */
void __noreturn noinline tb_bad_stack_stop(struct pt_regs *regs)
{
	unsigned long pc = regs->pc;
	unsigned long sp = regs->sp;
	unsigned long esr = read_sysreg(esr_el1);
	unsigned long far = read_sysreg(far_el1);

	tb_beacon_bad_stack(pc, sp, esr, far);
	asm volatile(
		"msr daifset, #0xf\n"
		"msr sp_el0, %2\n"
		"msr ttbr0_el1, %3\n"
		"msr spsel, #1\n"
		"mov sp, %1\n"
		"mov x30, %0\n"
		"adr x16, 2f\n"
		"br x16\n"
		"2:\n"
		".rept 64\n"
		"b .\n"
		".endr\n"
		:
		: "r" (pc), "r" (sp), "r" (esr), "r" (far)
		: "x16", "x30", "memory");
	__builtin_unreachable();
}

void tb_mark(const char *msg)
{
	unsigned int stage = tb_stage++;

	if (tb_pos + 64 < TB_LOG_SIZE)
		tb_pos += scnprintf(tb_buf + tb_pos, TB_LOG_SIZE - tb_pos,
				    "TBLOG %u %s\n", stage + 1, msg);
	/* 跨复位通道：stage、调用点、文本地址。 */
	tb_beacon_add(stage + 1, 0, (u64)_RET_IP_,
		      (u64)(unsigned long)msg, 0);
	tb_draw_stage(stage);
}
