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

#if defined(CONFIG_SUNXI_USBEHCI) || defined(CONFIG_USBHOST)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_USBHOST_DEFPRIO
#define CONFIG_USBHOST_DEFPRIO 50
#endif

#ifndef CONFIG_USBHOST_STACKSIZE
#ifdef CONFIG_USBHOST_HUB
#define CONFIG_USBHOST_STACKSIZE 1536
#else
#define CONFIG_USBHOST_STACKSIZE 1024
#endif
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Retained device driver handle */

static struct usbhost_connection_s *g_ehciconn;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ehci_waiter
 *
 * Description:
 *   Wait for USB devices to be connected to the EHCI root hub.
 *
 ****************************************************************************/

static int ehci_waiter(int argc, char *argv[])
{
  struct usbhost_hubport_s *hport;

  uinfo("ehci_waiter:  Running\n");
  for (; ; )
    {
      /* Wait for the device to change state */

      DEBUGVERIFY(CONN_WAIT(g_ehciconn, &hport));
      syslog(LOG_INFO, "ehci_waiter: %s\n",
             hport->connected ? "connected" : "disconnected");

      /* Did we just become connected? */

      if (hport->connected)
        {
          /* Yes.. enumerate the newly connected device */

          CONN_ENUMERATE(g_ehciconn, hport);
        }

    }

  /* Keep the compiler from complaining */

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sunxi_usbhost_initialize
 *
 * Description:
 *   Called at application startup time to initialize the USB host
 *   functionality.
 *   This function will start a thread that will monitor for device
 *   connection/disconnection events.
 *
 ****************************************************************************/

int sunxi_usbhost_initialize(void)
{
  int ret;

  /* First, register all of the class drivers needed to support the drivers
   * that we care about
   */

#ifdef CONFIG_USBHOST_HUB
  /* Initialize USB hub support */

  ret = usbhost_hub_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: usbhost_hub_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_USBHOST_MSC
  /* Register the USB host Mass Storage Class */

  ret = usbhost_msc_initialize();
  if (ret != OK)
    {
      syslog(LOG_ERR,
             "ERROR: Failed to register the mass storage class: %d\n", ret);
    }
#endif

#ifdef CONFIG_USBHOST_CDCACM
  /* Register the CDC/ACM serial class */

  ret = usbhost_cdcacm_initialize();
  if (ret != OK)
    {
      uerr("ERROR: Failed to register the CDC/ACM serial class\n");
    }
#endif

#ifdef CONFIG_USBHOST_HIDKBD
  /* Register the USB host HID keyboard class driver */

  ret = usbhost_kbdinit();
  if (ret != OK)
    {
      uerr("ERROR: Failed to register the KBD class\n");
    }
#endif

#ifdef CONFIG_USBHOST_UVC
  /* Register the USB host UVC Video Camera class driver */

  ret = usbhost_uvc_init();
  if (ret != OK)
    {
      syslog(LOG_ERR, "ERROR: Failed to register UVC class: %d\n", ret);
    }
#endif

  /* Then get an instance of the USB EHCI interface. */

#if defined (CONFIG_SELECT_SUNXI_UBSC0)
  /* enable usb host controller 0 */

  g_ehciconn = sunxi_ehci_initialize(0);
#elif defined(CONFIG_SELECT_SUNXI_UBSC1)
  /* enable usb host controller 1 */

  g_ehciconn = sunxi_ehci_initialize(1);

#else
#error "error: selected usb host controller id is invalid!!!"
#endif

  if (!g_ehciconn)
    {
      uerr("ERROR: sunxi_ehci_initialize failed\n");
      return -ENODEV;
    }

  /* Start a thread to handle device connection. */

  ret = kthread_create("EHCI Monitor", CONFIG_SUNXI_USBHOST_DEFPRIO,
                       CONFIG_SUNXI_USBHOST_STACKSIZE,
                       ehci_waiter, NULL);
  if (ret < 0)
    {
      uerr("ERROR: Failed to create ehci_waiter task: %d\n", ret);
      return -ENODEV;
    }

  return OK;
}

#endif /* CONFIG_SUNXI_USBEHCI || CONFIG_USBHOST */
