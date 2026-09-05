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

static int sunxi_ehci_open_clock(struct sunxi_hci *sx_ehci)
{
	if (sx_ehci && sx_ehci->open_clock)
		return sx_ehci->open_clock(sx_ehci);
	hci_err(sx_ehci, "ops open_clock is null\n");
	return -1;
}

static int sunxi_ehci_close_clock(struct sunxi_hci *sx_ehci)
{
	if (sx_ehci && sx_ehci->close_clock)
		return sx_ehci->close_clock(sx_ehci);
	hci_err(sx_ehci, "ops close_clock is null\n");
	return -1;
}

static int sunxi_ehci_set_power(struct sunxi_hci *sx_ehci, int ison)
{
	if (sx_ehci && sx_ehci->set_power)
		return sx_ehci->set_power(sx_ehci, ison);
	hci_err(sx_ehci, "ops set_power is null\n");
	return -1;
}

__attribute__((unused))
static int sunxi_ehci_port_configure(struct sunxi_hci *sx_ehci, u32 enable)
{
	if (sx_ehci && sx_ehci->port_configure)
		return sx_ehci->port_configure(sx_ehci, enable);
	hci_err(sx_ehci, "ops port_configure is null\n");
	return -1;
}

static int sunxi_ehci_usb_passby(struct sunxi_hci *sx_ehci, u32 enable)
{
	if (sx_ehci && sx_ehci->usb_passby)
		return sx_ehci->usb_passby(sx_ehci, enable);
	hci_err(sx_ehci, "ops usb_passby is null\n");
	return -1;
}

__attribute__((unused))
static int sunxi_ehci_phy_ctrl(struct sunxi_hci *sx_ehci, u32 enable)
{
	if (sx_ehci && sx_ehci->phy_ctrl)
		return sx_ehci->phy_ctrl(sx_ehci, enable);
	hci_err(sx_ehci, "ops phy_ctrl is null\n");
	return -1;
}

static void sunxi_ehci_startup(struct sunxi_hci *sx_ehci)
{
	sunxi_ehci_open_clock(sx_ehci);
	sunxi_ehci_usb_passby(sx_ehci, true);
	sunxi_ehci_set_power(sx_ehci, true);
	sunxi_hci_status_set_start(sx_ehci);
}

__attribute__((unused))
static void sunxi_ehci_stop(struct sunxi_hci *sx_ehci)
{
	sunxi_hci_status_clear_start(sx_ehci);
	sunxi_ehci_set_power(sx_ehci, false);
	sunxi_ehci_usb_passby(sx_ehci, false);
	sunxi_ehci_close_clock(sx_ehci);
}

static int sunxi_ehci_sxhci_bringup(struct sunxi_hci *sx_ehci)
{
	/* ehci start to work */
	sunxi_ehci_startup(sx_ehci);
    return 0;
}

int sunxi_ehci_sxhci_initial(struct sunxi_hci *sx_ehci, int usbhwc_num)
{
	struct platform_usb_config *ehci_table = platform_get_ehci_table();
	struct platform_usb_config *otg_table = platform_get_otg_table();

	if (!sx_ehci) {
		uhc_err("usbc%d get sunxi ehci structure failed\n", usbhwc_num);
		return -1;
	}
	if (sunxi_hci_status_check_init(sx_ehci)) {
		hci_err(sx_ehci, "usbc%d is already init\n", usbhwc_num);
		return -1;
	}
	memset(sx_ehci, 0, sizeof(struct sunxi_hci));
	sunxi_hci_status_set_init(sx_ehci);

	/* check config is enable usbx controller or not ? */
	if (uhc_board_check_usbcx_is_enable(usbhwc_num) != SUNXI_USB_ENABLE) {
		uhc_err("usbc%d is disable\n", usbhwc_num);
		goto usbc_is_disable;
	}

	/* record usbc num */
	sx_ehci->usbcx_num = usbhwc_num;
    /* set hci name */
    snprintf(sx_ehci->name, sizeof(sx_ehci->name), "%s", ehci_table[usbhwc_num].name);
    /* get chip hardware information, such as: baseaddr irqnum clk/reset-num */
	memcpy(&sx_ehci->hwinfo, &ehci_table[usbhwc_num], sizeof(struct platform_usb_config));
	memcpy(&sx_ehci->otginfo, otg_table, sizeof(struct platform_usb_config));
	sx_ehci->hci_base = sx_ehci->hwinfo.pbase;
	sx_ehci->hci_reg_length = SUNXI_USB_EHCI_LEN;
    hci_debug(sx_ehci, "hci_base 0x%x\n", sx_ehci->hci_base);

    /* get board configuration */
    uhc_board_hci_get_config_param(sx_ehci);
	hci_debug(sx_ehci, "boardinfo: usb_drv_vbus_type %d\n",
		  sx_ehci->board_config.drv_vbus_type);
	hci_debug(sx_ehci, "boardinfo: usb_drv_vbus_gpio %d\n",
		  sx_ehci->board_config.drv_vbus_gpio_set);
	hci_debug(sx_ehci, "boardinfo: usbh_driver_level %d\n",
		  sx_ehci->board_config.usb_driver_level);
	hci_debug(sx_ehci, "boardinfo: usbh_irq_flag %d\n", sx_ehci->board_config.usb_irq_flag);

	/* set ops */
	sx_ehci->open_clock = sunxi_hci_open_clock;
	sx_ehci->close_clock = sunxi_hci_close_clock;
	sx_ehci->set_power = sunxi_hci_set_vbus;
	sx_ehci->port_configure = NULL;
	sx_ehci->usb_passby = sunxi_hci_usb_passby;
	sx_ehci->phy_ctrl = NULL;

    /* usb host controller bringup */
    if (sunxi_ehci_sxhci_bringup(sx_ehci)) {
        hci_err(sx_ehci, "sunxi ehci bringup failed\n");
        goto sxhci_bringup_failed;
    }

    return 0;

sxhci_bringup_failed:
usbc_is_disable:
	sunxi_hci_status_clear_init(sx_ehci);
	return -1;
}

