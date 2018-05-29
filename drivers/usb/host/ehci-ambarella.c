/*
* linux/drivers/usb/host/ehci-ambarella.c
* driver for High speed (USB2.0) USB host controller on Ambarella processors
*
* History:
*	2010/08/11 - [Cao Rongrong] created file
*
* Copyright (C) 2008 by Ambarella, Inc.
* http://www.ambarella.com
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA  02111-1307, USA.
*/
#include <linux/platform_device.h>
#include <linux/of.h>
#include <mach/hardware.h>
#include <plat/rct.h>

extern int usb_disabled(void);

struct ehci_ambarella_priv {
	struct usb_phy *phy;
	u32 nports;
};

static struct hc_driver __read_mostly ehci_ambarella_hc_driver;

static int ambarella_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct ehci_ambarella_priv *priv;
	int ret = 0;

	/* registers start at offset 0x0 */
	ehci->caps = hcd->regs;

	ret = ehci_setup(hcd);
	if (ret)
		return ret;

	/* EHCI will still detect 2 ports even though usb port1 is configured
	 * as device port, so we fake the port number manually and report
	 * it to EHCI.*/
	priv = (struct ehci_ambarella_priv *)hcd_to_ehci(hcd)->priv;
	ehci->hcs_params &= ~0xf;
	ehci->hcs_params |= priv->nports;

	return 0;
}

static const struct ehci_driver_overrides ehci_ambarella_overrides __initdata = {
	.reset = ambarella_ehci_setup,
	.extra_priv_size = sizeof(struct ehci_ambarella_priv),
};

static int ehci_ambarella_drv_probe(struct platform_device *pdev)
{
	struct ehci_ambarella_priv *priv;
	struct usb_hcd *hcd;
	struct usb_phy *phy;
	struct resource *res;
	int irq, ret;

	/* Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;
	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	ehci_init_driver(&ehci_ambarella_hc_driver, &ehci_ambarella_overrides);

	hcd = usb_create_hcd(&ehci_ambarella_hc_driver, &pdev->dev, "AmbUSB");
	if (!hcd)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		ret = irq;
		goto amb_ehci_err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto amb_ehci_err;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = devm_ioremap(&pdev->dev, hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto amb_ehci_err;
	}

	priv = (struct ehci_ambarella_priv *)hcd_to_ehci(hcd)->priv;

	/* get the PHY device */
	phy = devm_usb_get_phy_by_phandle(&pdev->dev, "amb,usbphy", 0);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		dev_err(&pdev->dev, "Can't get USB PHY %d\n", ret);
		goto amb_ehci_err;
	}

	ret = of_property_read_u32(phy->dev->of_node,
				"amb,host-phy-num", &priv->nports);
	if (ret < 0){
		dev_err(&pdev->dev, "Can't get host phy num %d\n", ret);
		goto amb_ehci_err;
	}

	usb_phy_init(phy);
	priv->phy = phy;

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
        boss_set_irq_owner(irq, BOSS_IRQ_OWNER_LINUX, 1);
#endif
	ret = usb_add_hcd(hcd, irq, IRQF_TRIGGER_HIGH);
	if (ret < 0)
		goto amb_ehci_err;

	platform_set_drvdata(pdev, hcd);

	return 0;

amb_ehci_err:
	usb_put_hcd(hcd);
	return ret;
}

static int ehci_ambarella_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ehci_ambarella_priv *priv;

	priv = (struct ehci_ambarella_priv *)hcd_to_ehci(hcd)->priv;

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM

static int ehci_ambarella_drv_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct ehci_ambarella_priv *priv;

	priv = (struct ehci_ambarella_priv *)hcd_to_ehci(hcd)->priv;

	usb_phy_init(priv->phy);

	ehci_resume(hcd, false);

	return 0;
}

static int ehci_ambarella_drv_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool do_wakeup = device_may_wakeup(dev);

	return ehci_suspend(hcd, do_wakeup);;
}

static struct dev_pm_ops ambarella_ehci_pmops = {
	.suspend	= ehci_ambarella_drv_suspend,
	.resume		= ehci_ambarella_drv_resume,
	.freeze         = ehci_ambarella_drv_suspend,
	.thaw        	= ehci_ambarella_drv_resume,
};

#define AMBARELLA_EHCI_PMOPS &ambarella_ehci_pmops

#else
#define AMBARELLA_EHCI_PMOPS NULL
#endif


static struct of_device_id ambarella_ehci_dt_ids[] = {
	{ .compatible = "ambarella,ehci", },
	{ },
};

static struct platform_driver ehci_hcd_ambarella_driver = {
	.probe		= ehci_ambarella_drv_probe,
	.remove		= ehci_ambarella_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver = {
		.name	= "ambarella-ehci",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(ambarella_ehci_dt_ids),
		.pm	= AMBARELLA_EHCI_PMOPS,
	}
};

MODULE_ALIAS("platform:ambarella-ehci");

