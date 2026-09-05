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
#include "usb/uhc/sunxi-hci.h"
#include "usb/common/usb_phy.h"

static void USBC_SelectPhyToHci(struct sunxi_hci *sx_hci)
{
	int reg_value = 0;
	reg_value = USBC_Readl(sx_hci->otginfo.pbase + SUNXI_OTG_PHY_CFG);
	reg_value &= ~(0x01);
	USBC_Writel(reg_value, (sx_hci->otginfo.pbase + SUNXI_OTG_PHY_CFG));
}

#if defined(CONFIG_ARCH_SUN20IW2)
static void USBC_Enable_Bias(void)
{
	int reg_value = 0;
	reg_value = USBC_Readl(SUNXI_GPRCM_BASE + USB_BIAS_CTRL);
	reg_value |= USB_BIAS_CTRL_EN;
	USBC_Writel(reg_value, SUNXI_GPRCM_BASE + USB_BIAS_CTRL);
}
#endif

static void USBC_Clean_SIDDP(struct sunxi_hci *sx_hci)
{
	int reg_value = 0;
	reg_value = USBC_Readl(sx_hci->hwinfo.pbase + SUNXI_HCI_PHY_CTRL);
	reg_value &= ~(0x01 << SUNXI_HCI_PHY_CTRL_SIDDQ);
	USBC_Writel(reg_value, (sx_hci->hwinfo.pbase + SUNXI_HCI_PHY_CTRL));
}

int sunxi_hci_open_clock(struct sunxi_hci *sx_hci)
{
	if (uhc_platform_open_clk(&sx_hci->plat)) {
		hci_err(sx_hci, "open clock failed\n");
		return -1;
	}

#if defined(CONFIG_ARCH_SUN20IW2)
	/* USB BIAS enable, set 0 if usb disable to save power at sleep */
	USBC_Enable_Bias();
#endif
	USBC_Clean_SIDDP(sx_hci);

	/* otg and hci0 Controller Shared phy in SUN50I */
	if (sx_hci->usbcx_num == HCI0_USBC_NO)
		USBC_SelectPhyToHci(sx_hci);

	usb_phy_init(sx_hci->hwinfo.pbase + SUNXI_USB_PHY_BASE_OFFSET, sx_hci->usbcx_num);

#if defined(CONFIG_ARCH_SUN251IW1)
	/* Increase USB disconnection detection voltage */
	usb_disconnect_detect_vol(sx_hci->hwinfo.pbase + SUNXI_USB_PHY_BASE_OFFSET, 0x6);
	usb_phy_range_set(sx_hci->hwinfo.pbase + SUNXI_USB_PHY_BASE_OFFSET, 0x348);
#endif
	return 0;
}

int sunxi_hci_close_clock(struct sunxi_hci *sx_hci)
{
	if (uhc_platform_close_clk(&sx_hci->plat)) {
		hci_err(sx_hci, "close clock failed\n");
		return -1;
	}
	return 0;
}

int sunxi_hci_set_vbus(struct sunxi_hci *sx_hci, int is_on)
{
	hci_debug(sx_hci, "set power %s\n", is_on ? "ON" : "OFF");

	if (sx_hci->board_config.drv_vbus_type == USB_DRV_VBUS_TYPE_GIPO) {
		if (sx_hci->board_config.drv_vbus_gpio_set != -1) {
			hal_gpio_set_data(sx_hci->board_config.drv_vbus_gpio_set, is_on);
		} else if (sx_hci->board_config.drv_vbus_type == USB_DRV_VBUS_TYPE_AXP) {
			hci_err(sx_hci, "unsupport axp-mode set power\n");
		}
	}
	return 0;
}

int sunxi_hci_usb_passby(struct sunxi_hci *sx_hci, u32 enable)
{
	hal_spinlock_t passby_lock = { 0 };
	unsigned long reg_value = 0;
	unsigned long flags;

	flags = hal_spin_lock_irqsave(&passby_lock);

	reg_value = USBC_Readl(sx_hci->hwinfo.pbase + SUNXI_USB_PMU_IRQ_ENABLE);
	if (enable) {
		reg_value |= (1 << 10); /* AHB Master interface INCR8 enable */
		reg_value |= (1 << 9); /* AHB Master interface burst type INCR4 enable */
		reg_value |= (1 << 8); /* AHB Master interface INCRX align enable */
		reg_value |= (1 << 0); /* ULPI bypass enable */
	} else if (!enable) {
		reg_value &= ~(1 << 10); /* AHB Master interface INCR8 disable */
		reg_value &= ~(1 << 9); /* AHB Master interface burst type INCR4 disable */
		reg_value &= ~(1 << 8); /* AHB Master interface INCRX align disable */
		reg_value &= ~(1 << 0); /* ULPI bypass disable */
	}
	USBC_Writel(reg_value, (sx_hci->hwinfo.pbase + SUNXI_USB_PMU_IRQ_ENABLE));

	hal_spin_unlock_irqrestore(&passby_lock, flags);
	return 0;
}