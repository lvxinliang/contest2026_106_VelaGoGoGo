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
#include "usb/uhc/uhc_inner.h"
#include "usb/uhc/sunxi-hci.h"

int uhc_platform_open_clk(struct uhc_platform *plat)
{
	struct sunxi_hci *sx_hci;
	struct platform_usb_config *hwinfo;
	hal_reset_type_t reset_type = HAL_SUNXI_RESET;
	hal_clk_type_t clk_type = HAL_SUNXI_CCU;
	hal_clk_status_t ret;

	sx_hci = uhc_container_of(plat, struct sunxi_hci, plat);
	hwinfo = &sx_hci->hwinfo;

	plat->reset_phy = hal_reset_control_get(reset_type, hwinfo->phy_rst);
	hal_reset_control_deassert(plat->reset_phy);
	hal_reset_control_put(plat->reset_phy);

	plat->reset_hci = hal_reset_control_get(reset_type, hwinfo->usb_rst);
	hal_reset_control_deassert(plat->reset_hci);
	hal_reset_control_put(plat->reset_hci);

	plat->phy_clk = hal_clock_get(clk_type, hwinfo->phy_clk);
	ret = hal_clock_enable(plat->phy_clk);
	if (ret) {
		hci_err(sx_hci, "couldn't enable usb_clk!\n");
		return -1;
	}

	plat->bus_clk = hal_clock_get(clk_type, hwinfo->usb_clk);
	ret = hal_clock_enable(plat->bus_clk);
	if (ret) {
		hci_err(sx_hci, "couldn't enable hci_clk!\n");
		return -1;
	}

	plat->ohci_clk = hal_clock_get(clk_type, hwinfo->ohci_clk);
	ret = hal_clock_enable(plat->ohci_clk);
	if (ret) {
		hci_err(sx_hci, "couldn't enable ohci_clk!\n");
		return -1;
	}

	return 0;
}

int uhc_platform_close_clk(struct uhc_platform *plat)
{
	hal_reset_type_t reset_type = HAL_SUNXI_RESET;
	hal_clk_type_t clk_type = HAL_SUNXI_CCU;
	hal_clk_status_t ret;
	struct sunxi_hci *sx_hci;
	struct platform_usb_config *hwinfo;

	sx_hci = uhc_container_of(plat, struct sunxi_hci, plat);
	hwinfo = &sx_hci->hwinfo;

	plat->reset_phy = hal_reset_control_get(reset_type, hwinfo->phy_rst);
	ret = hal_reset_control_assert(plat->reset_phy);
	if (ret) {
		hci_err(sx_hci, "couldn't disable hci_reset_phy!\n");
		return -1;
	}
	hal_reset_control_put(plat->reset_phy);

	plat->reset_hci = hal_reset_control_get(reset_type, hwinfo->usb_rst);
	ret = hal_reset_control_assert(plat->reset_hci);
	if (ret) {
		hci_err(sx_hci, "couldn't disable hci_reset_bus!\n");
		return -1;
	}
	hal_reset_control_put(plat->reset_hci);

	plat->phy_clk = hal_clock_get(clk_type, hwinfo->phy_clk);
	ret = hal_clock_disable(plat->phy_clk);
	if (ret) {
		hci_err(sx_hci, "couldn't disable phy_clk!\n");
		return -1;
	}

	plat->bus_clk = hal_clock_get(clk_type, hwinfo->usb_clk);
	ret = hal_clock_disable(plat->bus_clk);
	if (ret) {
		hci_err(sx_hci, "couldn't disable bus_clk!\n");
		return -1;
	}

	plat->ohci_clk = hal_clock_get(clk_type, hwinfo->ohci_clk);
	ret = hal_clock_disable(plat->ohci_clk);
	if (ret) {
		hci_err(sx_hci, "couldn't disable ohci_clk!\n");
		return -1;
	}

	return 0;
}
