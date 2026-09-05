/* Copyright (c) 2019-2025 Allwinner Technology Co., Ltd. ALL rights reserved.

 * Allwinner is a trademark of Allwinner Technology Co.,Ltd., registered in
 * the the People's Republic of China and other countries.
 * All Allwinner Technology Co.,Ltd. trademarks are used with permission.

 * DISCLAIMER
 * THIRD PARTY LICENCES MAY BE REQUIRED TO IMPLEMENT THE SOLUTION/PRODUCT.
 * IF YOU NEED TO INTEGRATE THIRD PART'S TECHNOLOGY (SONY, DTS, DOLBY, AVS OR MPEGLA, ETC.)
 * IN ALLWINNER'SDK OR PRODUCTS, YOU SHALL BE SOLELY RESPONSIBLE TO OBTAIN
 * ALL APPROPRIATELY REQUIRED THIRD PARTY LICENCES.
 * ALLWINNER SHALL HAVE NO WARRANTY, INDEMNITY OR OTHER OBLIGATIONS WITH RESPECT TO MATTERS
 * COVERED UNDER ANY REQUIRED THIRD PARTY LICENSE.
 * YOU ARE SOLELY RESPONSIBLE FOR YOUR USAGE OF THIRD PART'S TECHNOLOGY.


 * THIS SOFTWARE IS PROVIDED BY ALLWINNER"AS IS" AND TO THE MAXIMUM EXTENT
 * PERMITTED BY LAW, ALLWINNER EXPRESSLY DISCLAIMS ALL WARRANTIES OF ANY KIND,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING WITHOUT LIMITATION REGARDING
 * THE TITLE, NON-INFRINGEMENT, ACCURACY, CONDITION, COMPLETENESS, PERFORMANCE
 * OR MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * IN NO EVENT SHALL ALLWINNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS, OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __UHC_INNER_H__
#define __UHC_INNER_H__

#include <nuttx/config.h>

#include <sys/types.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include "hal_reset.h"
#include "hal_interrupt.h"
#include "hal_gpio.h"
#include "hal_clk.h"
#include "hal_time.h"
#include "hal_atomic.h"
#include "hal_log.h"
#include "hal_cache.h"
#include "hal_mem.h"
#include "hal_atomic.h"
#include "sunxi_hal_common.h"

#include <nuttx/list.h>
#include <nuttx/spinlock.h>
#include <nuttx/kthread.h>
#include <nuttx/usb/usbdev.h>
#include <nuttx/usb/usbhost.h>
#include <nuttx/usb/usbdev_trace.h>
#include <nuttx/kmalloc.h>
#include "usb_os_platform.h"

/* debug */
#define usb_debug(f, ...) 	syslog(LOG_DEBUG, 	f, ##__VA_ARGS__)
#define usb_info(f, ...) 	syslog(LOG_INFO, 	f, ##__VA_ARGS__)
#define usb_warn(f, ...) 	syslog(LOG_WARNING, f, ##__VA_ARGS__)
#define usb_err(f, ...) 	syslog(LOG_ERR, 	f, ##__VA_ARGS__)
#define usb_printf(f, ...) 	printf(		        f, ##__VA_ARGS__)

#define USB_ASSERT(cond) ({\
	if (!(cond)) usb_err(#cond);\
	hal_assert(cond);\
})
#ifndef DEBUGASSERT
#define DEBUGASSERT USB_ASSERT
#endif

#define usb_min(a, b)  ((a) < (b) ? (a) : (b))
#define usb_max(a,b)   ((a) < (b) ? (b) : (a))

/* addr translater */
#define usb_va2pa(va) __va_to_pa(va)
#define usb_pa2va(va) __pa_to_va(pa)

/* val */
#define USB_UNKNOWEN (0)

/* align */
#define USB_ALIGN_SIZE(size, align) (((size) + (align-1)) &  ~(align-1))

#define usb_container_of(ptr, type, member) \
	((type *)((char *)(ptr) - (unsigned long)(&((type *)0)->member)))

/* cache */
#ifdef CACHE_LINE_SIZE
#define USB_CACHELINE_SIZE CACHELINE_LEN
#else
#define USB_CACHELINE_SIZE 64
//#error "error: no found the vaild CACHELINE_LEN size"
#endif
#define USB_LEN_ALIGN_CACHE_LINE_SIZE(len) USB_ALIGN_SIZE(len, USB_CACHELINE_SIZE)

#define __CACHE_OPS_BEFORE_CHECK(addr, len) \
	do { \
		if (((addr) & (USB_CACHELINE_SIZE - 1)) || ((len) % USB_CACHELINE_SIZE) || !len) \
			usb_printf("warn cache-ops: addr %lx len %lu\n", (unsigned long)addr, \
				   (unsigned long)len); \
	} while (0)

#define usb_dcache_clean(addr, len) \
	do { \
		__CACHE_OPS_BEFORE_CHECK(addr, len); \
		hal_dcache_clean((unsigned long)addr, (unsigned long)len); \
	} while (0)
#define usb_dcache_invalidate(addr, len) \
	do { \
		__CACHE_OPS_BEFORE_CHECK(addr, len); \
		hal_dcache_invalidate((unsigned long)addr, (unsigned long)len); \
	} while (0)
#define usb_dcache_clean_invalidate(addr, len) \
	do { \
		__CACHE_OPS_BEFORE_CHECK(addr, len); \
		hal_dcache_clean_invalidate((unsigned long)addr, (unsigned long)len); \
	} while (0)

/* register access */
#define usb_readl(o, r) readl(r)
#define usb_readw(o, r) readw(r)
#define usb_readb(o, r) readb(r)
#define usb_writel(o, v, r) writel(v, r)
#define usb_writew(o, v, r) writew(v, r)
#define usb_writeb(o, v, r) writeb(v, r)

/* debug */
#if defined(CONFIG_SUNXI_USBHOST_LOGDEBUG_DEBUG)
#define uhc_debug usb_debug
#define hci_debug(hci, f, ...) \
	usb_debug("{%s}%s %d: " f, \
        (hci)->name ? (hci)->name : (char *)"NULL", \
        __func__, __LINE__, ##__VA_ARGS__)
#else
#define uhc_debug(f, ...)
#define hci_debug(hci, f, ...)
#endif

#if defined(CONFIG_SUNXI_USBHOST_LOGDEBUG_INFO)
#define uhc_info usb_info
#define hci_info(hci, f, ...) \
	usb_info("{%s}%s %d: " f, \
        (hci)->name ? (hci)->name : (char *)"NULL", \
        __func__, __LINE__, ##__VA_ARGS__)
#else
#define uhc_info(f, ...)
#define hci_info(hci, f, ...)
#endif

#if defined(CONFIG_SUNXI_USBHOST_LOGDEBUG_WARN)
#define uhc_warn usb_warn
#define hci_warn(hci, f, ...) \
	usb_warn("{%s}%s %d: " f, \
        (hci)->name ? (hci)->name : (char *)"NULL", \
        __func__, __LINE__, ##__VA_ARGS__)
#else
#define uhc_warn(f, ...)
#define hci_warn(hci, f, ...)
#endif

#define uhc_err usb_err
#define hci_err(hci, f, ...) \
	usb_err("{%s}%s %d: " f, \
        (hci)->name ? (hci)->name : (char *)"NULL", \
		 __func__, __LINE__, ##__VA_ARGS__)

#define uhc_printf usb_printf
#define hci_printf(hci, f, ...) \
        usb_printf("{%s}%s %d: " f, \
        (hci)->name ? (hci)->name : (char *)"NULL", \
	    __func__, __LINE__, ##__VA_ARGS__)

#define uhc_container_of usb_container_of

/**
 * ehci driver
 */
struct usbhost_connection_s *sunxi_ehci_initialize(int controller);

#ifdef HAVE_USBHOST_TRACE
const char *usbhost_trformat1(uint16_t id);
const char *usbhost_trformat2(uint16_t id);
#endif

#endif
