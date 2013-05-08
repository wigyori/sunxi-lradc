/*
 * Copyright (c) 2013 Oliver Schinagl
 * http://www.linux-sunxi.org
 *
 * Oliver Schinagl <oliver@schinagl.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * This driver exposes the Allwinner security ID, a 128 bit eeprom, in byte
 * sized chunks.
 */

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#define DRV_NAME "sunxi-sid"

/* There are 4 32-bit keys */
#define SID_KEYS 4
/* Each key is 4 bytes long */
#define SID_SIZE (SID_KEYS * 4)

/* We read the entire key, due to a 32 bit read alignment requirement. Since we
 * want to return the requested byte, this resuls in somewhat slower code and
 * uses 4 times more reads as needed but keeps code simpler. Since the SID is
 * only very rarly probed, this is not really an issue.
 */
static u8 sunxi_sid_read_byte(const void __iomem *sid_reg_base,
			      const unsigned int offset)
{
	u32 sid_key;

	if (offset >= SID_SIZE)
		return 0;

	sid_key = ioread32be(sid_reg_base + round_down(offset, 4));
	sid_key >>= (offset % 4) * 8;

	return sid_key; /* Only return the last byte */
}

static ssize_t sid_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	u8 sid[SID_SIZE];
	struct platform_device *pdev;
	void __iomem *sid_reg_base;
	int i;
	int ret;

	pdev = to_platform_device(kobj_to_dev(kobj));
	sid_reg_base = (void __iomem *)platform_get_drvdata(pdev);

	printk("0x%p, 0x%p, 0x%p, 0x%p\n", kobj, kobj_to_dev(kobj), pdev, sid_reg_base);

	for (i = 0; i < SID_SIZE; i++) {
		sid[i] = sunxi_sid_read_byte(sid_reg_base, i);
	}
	ret = scnprintf(buf, sizeof(u8) * i, "%s\n", sid);

	return i;
}

static ssize_t sid_read(struct file *fd, struct kobject *kobj,
			struct bin_attribute *attr, char *buf,
			loff_t pos, size_t size)
{
	struct platform_device *pdev;
	void __iomem *sid_reg_base;
	int i;

	pdev = to_platform_device(kobj_to_dev(kobj));
	sid_reg_base = (void __iomem *)platform_get_drvdata(pdev);
	printk("0x%p, 0x%p, 0x%p, 0x%p\n", kobj, kobj_to_dev(kobj), pdev, sid_reg_base);

	if (pos < 0 || pos >= SID_SIZE)
		return 0;
	if (size > SID_SIZE - pos)
		size = SID_SIZE - pos;

	for (i = 0; i < size; i++)
		buf[i] = sunxi_sid_read_byte(sid_reg_base, pos + i);

	return i;
}

static const struct of_device_id sunxi_sid_of_match[] = {
	{ .compatible = "allwinner,sun4i-sid", },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, sunxi_sid_of_match);

static struct kobj_attribute sid_attr = {
	.attr = { .name = "eeprom-txt", .mode = S_IRUGO, },
	.show = sid_show,
};

static struct attribute *sunxi_sid_attrs[] = {
	&sid_attr.attr,
	NULL,
};

static struct bin_attribute sid_bin_attr = {
	.attr = { .name = "eeprom", .mode = S_IRUGO, },
	.size = SID_SIZE,
	.read = sid_read,
};

static struct bin_attribute *sunxi_sid_bin_attrs[] = {
	&sid_bin_attr,
	NULL,
};

static const struct attribute_group sunxi_sid_group = {
	.bin_attrs = sunxi_sid_bin_attrs,
	.attrs = sunxi_sid_attrs,
};

static const struct attribute_group *sunxi_sid_groups[] = {
	&sunxi_sid_group,
	NULL,
};

static int sunxi_sid_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s driver unloaded\n", DRV_NAME);

	return 0;
}

static int __init sunxi_sid_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *sid_reg_base;
	u8 entropy[SID_SIZE];
	unsigned int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sid_reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sid_reg_base))
		return PTR_ERR(sid_reg_base);
	platform_set_drvdata(pdev, sid_reg_base);

	for (i = 0; i < SID_SIZE; i++)
		entropy[i] = sunxi_sid_read_byte(sid_reg_base, i);
	add_device_randomness(entropy, SID_SIZE);
	dev_dbg(&pdev->dev, "%s loaded\n", DRV_NAME);
	printk("0x%p, 0x%p\n", pdev, sid_reg_base);

	return 0;
}

static struct platform_driver sunxi_sid_driver = {
	.probe = sunxi_sid_probe,
	.remove = sunxi_sid_remove,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_sid_of_match,
		.groups = sunxi_sid_groups,
	},
};
module_platform_driver(sunxi_sid_driver);

MODULE_AUTHOR("Oliver Schinagl <oliver@schinagl.nl>");
MODULE_DESCRIPTION("Allwinner sunxi security id driver");
MODULE_LICENSE("GPL");
