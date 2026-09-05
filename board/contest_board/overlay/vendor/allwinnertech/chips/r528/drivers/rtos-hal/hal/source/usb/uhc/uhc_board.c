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

#ifndef CONFIG_SUNXI_USBHOST_VBUS_GPIO
#define CONFIG_SUNXI_USBHOST_VBUS_GPIO "PD18"
#endif

static int uhc_board_parse_gpio(const char *name)
{
	int pin = 0;
	int i;

	if (name[0] != 'P' || name[1] < 'B' || name[1] > 'G' ||
	    name[2] < '0' || name[2] > '9') {
		return -EINVAL;
	}

	for (i = 2; name[i] != '\0'; i++) {
		if (name[i] < '0' || name[i] > '9') {
			return -EINVAL;
		}

		pin = pin * 10 + name[i] - '0';
	}

	if (pin > 31) {
		return -EINVAL;
	}

	return (name[1] - 'A') * 32 + pin;
}

int uhc_board_check_usbcx_is_enable(int usbhwc_num)
{
	int value;
#if defined(CONFIG_DRIVER_SYSCONFIG)
    /* unsupport */
#else
	value = SUNXI_USB_ENABLE;
#endif
	return value;
}

int uhc_board_hci_get_config_param(struct sunxi_hci *sx_hci)
{
#ifdef CONFIG_DRIVER_SYSCONFIG
    /* unsupport */
#else
    struct uhc_board *bd = &sx_hci->board_config;
	int gpio;

	gpio = uhc_board_parse_gpio(CONFIG_SUNXI_USBHOST_VBUS_GPIO);
	if (gpio < 0) {
		uhc_err("invalid VBUS GPIO '%s', fallback to PD18\n",
			CONFIG_SUNXI_USBHOST_VBUS_GPIO);
		gpio = GPIOD(18);
	}

	bd->drv_vbus_gpio_valid = 1;
	bd->drv_vbus_gpio_set = gpio;
    bd->drv_vbus_type = USB_DRV_VBUS_TYPE_GIPO;
    bd->usb_driver_level = 3;
    bd->usb_irq_flag = 0;

    hal_gpio_set_direction(bd->drv_vbus_gpio_set, GPIO_DIRECTION_OUTPUT);
    hal_gpio_set_pull(bd->drv_vbus_gpio_set, GPIO_PULL_DOWN);
    hal_gpio_set_driving_level(bd->drv_vbus_gpio_set, GPIO_DRIVING_LEVEL3);
#endif

#if defined(CONFIG_ARCH_SUN252IW2)
	/*change sram owner from system to usb*/
	u32 temp = hal_readl(SUNXI_SYSCFG_BOOT_RAMMAP_REG);
	temp &= ~(1 << 27 | 1 << 25);
	hal_writel(temp, SUNXI_SYSCFG_BOOT_RAMMAP_REG);
#endif

    return 0;
}
