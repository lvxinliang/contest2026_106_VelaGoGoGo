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
#ifndef __SUNXI_HCI_H__
#define __SUNXI_HCI_H__

#include "usb/uhc/uhc_inner.h"

#define sunxi_hci_getreg32(a) usb_readl(0, a)
#define sunxi_hci_putreg32(v, a) usb_writel(0, v, a)
#define sunxi_hci_modreg32(v,m,a) sunxi_hci_putreg32((sunxi_hci_getreg32(a) & ~(m)) | ((v) & (m)), (a))

#define USBC_Readb(reg) readb(reg)
#define USBC_Readw(reg) readw(reg)
#define USBC_Readl(reg) readl(reg)

#define USBC_Writeb(value, reg) writeb(value, reg)
#define USBC_Writew(value, reg) writew(value, reg)
#define USBC_Writel(value, reg) writel(value, reg)

#define USBC_REG_test_bit_b(bp, reg) (USBC_Readb(reg) & (1 << (bp)))
#define USBC_REG_test_bit_w(bp, reg) (USBC_Readw(reg) & (1 << (bp)))
#define USBC_REG_test_bit_l(bp, reg) (USBC_Readl(reg) & (1 << (bp)))

#define USBC_REG_set_bit_b(bp, reg) (USBC_Writeb((USBC_Readb(reg) | (1 << (bp))), (reg)))
#define USBC_REG_set_bit_w(bp, reg) (USBC_Writew((USBC_Readw(reg) | (1 << (bp))), (reg)))
#define USBC_REG_set_bit_l(bp, reg) (USBC_Writel((USBC_Readl(reg) | (1 << (bp))), (reg)))

#define USBC_REG_clear_bit_b(bp, reg) (USBC_Writeb((USBC_Readb(reg) & (~(1 << (bp)))), (reg)))
#define USBC_REG_clear_bit_w(bp, reg) (USBC_Writew((USBC_Readw(reg) & (~(1 << (bp)))), (reg)))
#define USBC_REG_clear_bit_l(bp, reg) (USBC_Writel((USBC_Readl(reg) & (~(1 << (bp)))), (reg)))

#define HCI0_USBC_NO 0
#define HCI1_USBC_NO 1
#define HCI2_USBC_NO 2
#define HCI3_USBC_NO 3

#define SUNXI_USB_EHCI_BASE_OFFSET 0x00
#define SUNXI_USB_OHCI_BASE_OFFSET 0x400
#define SUNXI_USB_PHY_BASE_OFFSET 0x800
#define SUNXI_USB_EHCI_LEN 0x58
#define SUNXI_USB_OHCI_LEN 0x58

#define SUNXI_USB_EHCI_TIME_INT 0x30
#define SUNXI_USB_EHCI_STANDBY_IRQ_STATUS 1
#define SUNXI_USB_EHCI_STANDBY_IRQ 2

#define SUNXI_USB_PMU_IRQ_ENABLE 0x800
#define SUNXI_HCI_CTRL_3 0X808
#define SUNXI_HCI_PHY_CTRL 0x810
#define SUNXI_HCI_PHY_TUNE 0x818
#define SUNXI_HCI_UTMI_PHY_STATUS 0x824
#define SUNXI_HCI_CTRL_3_REMOTE_WAKEUP 3
#define SUNXI_HCI_RC16M_CLK_ENBALE 2
#if defined(CONFIG_ARCH_SUN20IW2)
#define SUNXI_HCI_PHY_CTRL_SIDDQ 1
#else
#define SUNXI_HCI_PHY_CTRL_SIDDQ 3
#endif
#if defined(CONFIG_ARCH_SUN20IW2)
#define SUNXI_GPRCM_BASE (0x40050000)
#define USB_BIAS_CTRL (0x0064)
#define USB_BIAS_CTRL_EN (0x0001)
#endif

#define SUNXI_OTG_PHY_CTRL 0x410
#define SUNXI_OTG_PHY_CFG 0x420
#define SUNXI_OTG_PHY_STATUS 0x424
#define SUNXI_USBC_REG_INTUSBE 0x0050

#define KEY_USB_ENABLE "usb_used"
#define KEY_USB_DRVVBUS_TYPE "usb_drv_vbus_type"
#define KEY_USB_DRVVBUS_GPIO "usb_drv_vbus_gpio"
#define KEY_USB_REGULATOR_IO "usb_regulator_io"
#define KEY_USB_REGULATOR_IO_VOL "usb_regulator_vol"
#define KEY_USB_WAKEUP_SUSPEND "usb_wakeup_suspend"
#define KEY_USB_HSIC_USBED "usb_hsic_used"
#define KEY_USB_HSIC_CTRL "usb_hsic_ctrl"
#define KEY_USB_HSIC_RDY_GPIO "usb_hsic_rdy_gpio"
#define KEY_USB_HSIC_REGULATOR_IO "usb_hsic_regulator_io"
#define KEY_WAKEUP_SOURCE "wakeup-source"
#define KEY_USB_PORT_TYPE "usb_port_type"
#define KEY_USB_DRIVER_LEVEL "usbh_driver_level"
#define KEY_USB_IRQ_FLAG "usbh_irq_flag"

enum sunxi_usbc_used {
	SUNXI_USB_DISABLE = 0,
	SUNXI_USB_ENABLE,
};

enum sunxi_usbc_type {
	SUNXI_USB_UNKNOWN = 0,
	SUNXI_USB_EHCI,
	SUNXI_USB_OHCI,
	SUNXI_USB_XHCI,
};

enum usb_drv_vbus_type {
	USB_DRV_VBUS_TYPE_NULL = 0,
	USB_DRV_VBUS_TYPE_GIPO,
	USB_DRV_VBUS_TYPE_AXP,
};

struct uhc_board {
	int drv_vbus_type;
	unsigned int drv_vbus_gpio_valid;
	int drv_vbus_gpio_set;
	int usb_driver_level;
	int usb_irq_flag;
};

struct uhc_platform {
	hal_clk_t bus_clk;
	hal_clk_t phy_clk;
	hal_clk_t ohci_clk;

	struct reset_control *reset_hci;
	struct reset_control *reset_phy;
};

struct sunxi_hci {
	int usbcx_num; /* usb controller number */
	char name[32]; /* hci name */
	unsigned int status;
#define SUNXI_HCI_STATUS_INIT (0)
#define sunxi_hci_status_check_init(sunxi_hci) \
	(!!((sunxi_hci)->status & (0x1 << SUNXI_HCI_STATUS_INIT)))
#define sunxi_hci_status_set_init(sunxi_hci) ((sunxi_hci)->status |= (0x1 << SUNXI_HCI_STATUS_INIT))
#define sunxi_hci_status_clear_init(sunxi_hci) \
	((sunxi_hci)->status &= ~(0x1 << SUNXI_HCI_STATUS_INIT))

#define SUNXI_HCI_STATUS_START (1)
#define sunxi_hci_status_check_start(sunxi_hci) \
	(!!((sunxi_hci)->status & (0x1 << SUNXI_HCI_STATUS_START)))
#define sunxi_hci_status_set_start(sunxi_hci) \
	((sunxi_hci)->status |= (0x1 << SUNXI_HCI_STATUS_START))
#define sunxi_hci_status_clear_start(sunxi_hci) \
	((sunxi_hci)->status &= ~(0x1 << SUNXI_HCI_STATUS_START))

	struct platform_usb_config hwinfo; /* usbcx hardware information */
	struct platform_usb_config otginfo;
	struct uhc_board board_config; /* board configuration */
	struct uhc_platform plat; /* platform handler */

	unsigned int hci_base;
	unsigned int hci_reg_length;

	int (*open_clock)(struct sunxi_hci *sx_hci);
	int (*close_clock)(struct sunxi_hci *sx_hci);
	int (*set_power)(struct sunxi_hci *sx_hci, int ison);
	int (*port_configure)(struct sunxi_hci *sx_hci, u32 enable);
	int (*usb_passby)(struct sunxi_hci *sx_hci, u32 enable);
	int (*phy_ctrl)(struct sunxi_hci *sx_hci, u32 enable);
};

/* board */
int uhc_board_check_usbcx_is_enable(int usbhwc_num);
int uhc_board_hci_get_config_param(struct sunxi_hci *sx_hci);
/* platform */
int uhc_platform_open_clk(struct uhc_platform *plat);
int uhc_platform_close_clk(struct uhc_platform *plat);
/* hci */
int sunxi_hci_open_clock(struct sunxi_hci *sx_hci);
int sunxi_hci_close_clock(struct sunxi_hci *sx_hci);
int sunxi_hci_set_vbus(struct sunxi_hci *sx_hci, int is_on);
int sunxi_hci_usb_passby(struct sunxi_hci *sx_hci, u32 enable);

#endif