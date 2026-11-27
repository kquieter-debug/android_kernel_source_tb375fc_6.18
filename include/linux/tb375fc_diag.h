/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_TB375FC_DIAG_H
#define _LINUX_TB375FC_DIAG_H

#include <linux/init.h>

#ifdef CONFIG_TB375FC_DIAG_FIRST_USB_LATCH
int __init tb375fc_diag_pin_first_usb(const char *init_path);
#endif

#endif
