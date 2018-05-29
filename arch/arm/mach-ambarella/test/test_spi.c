/*
 * Filename : test_spi.c
 *
 * History:
 *    2016/01/19 - [Chien-Yang Chen] Create
 *
 * Copyright (c) 2016 Ambarella, Inc.
 *
 * This file and its contents ("Software") are protected by intellectual
 * property rights including, without limitation, U.S. and/or foreign
 * copyrights. This Software is also the confidential and proprietary
 * information of Ambarella, Inc. and its licensors. You may not use, reproduce,
 * disclose, distribute, modify, or otherwise prepare derivative works of this
 * Software or any portion thereof except pursuant to a signed license agreement
 * or nondisclosure agreement with Ambarella, Inc. or its authorized affiliates.
 * In the absence of such an agreement, you agree to promptly notify and return
 * this Software to Ambarella, Inc.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
 * MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL AMBARELLA, INC. OR ITS AFFILIATES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; COMPUTER FAILURE OR MALFUNCTION; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/ambpriv_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <plat/spi.h>

static u32 bus_addr = 0x00010000;
module_param(bus_addr, int, 0644);
MODULE_PARM_DESC(bus_addr, " bus and cs: bit16~bit31: bus, bit0~bit15: cs");

struct test_spi {
	u32 spi_bus;
	u32 spi_cs;
} test_spi_obj;


static int test_spi_write_reg(struct ambpriv_device *ambdev, u32 subaddr, u32 data)
{
	u8 pbuf[4];
        u8 chip_id = 0x81;
	amba_spi_cfg_t config;
	amba_spi_write_t write;
	struct test_spi *test_spi = ambpriv_get_drvdata(ambdev);

        pbuf[0] = chip_id;
	pbuf[2] = subaddr & 0xff;
	pbuf[1] = (subaddr & 0xff00) >> 8;
	pbuf[3] = data & 0xff;

	config.cfs_dfs = 8;//bits
	config.baud_rate = 1000000;
	config.cs_change = 0;
	config.spi_mode = SPI_MODE_3 | SPI_LSB_FIRST;

	write.buffer = pbuf;
	write.n_size = 4;
	write.cs_id = test_spi->spi_cs;
	write.bus_id = test_spi->spi_bus;

	ambarella_spi_write(&config, &write);

	return 0;
}

static int test_spi_read_reg(struct ambpriv_device *ambdev, u32 subaddr, u32 *data)
{
	u8 pbuf[3], tmp;
        u8 chip_id = 0x82;
	amba_spi_cfg_t config;
	amba_spi_write_then_read_t write;
	struct test_spi *test_spi = ambpriv_get_drvdata(ambdev);

        pbuf[0] = chip_id;
	pbuf[2] = subaddr & 0xff;
	//pbuf[2] = ((subaddr & 0xff00) >> 8) | 0x80;
	pbuf[1] = ((subaddr & 0xff00) >> 8);

	config.cfs_dfs = 8;//bits
	config.baud_rate = 1000000;
	config.cs_change = 0;
	config.spi_mode = SPI_MODE_3 | SPI_LSB_FIRST;

	write.w_buffer = pbuf;
	write.w_size = 3;
	write.r_buffer = &tmp;
	write.r_size = 1;
	write.cs_id = test_spi->spi_cs;
	write.bus_id = test_spi->spi_bus;

	ambarella_spi_write_then_read(&config, &write);

	*data = tmp;

	return 0;
}

#define IMX117_MDSEL1_REG       0x0004
#define IMX117_DGAIN_REG        0x0011
static int test_spi_drv_probe(struct ambpriv_device *ambdev)
{
        u32 data;
	struct test_spi *test_spi = &test_spi_obj;

	test_spi->spi_bus = bus_addr >> 16;
	test_spi->spi_cs = bus_addr & 0xffff;
	ambpriv_set_drvdata(ambdev, test_spi);

        data = 0x0;
        test_spi_read_reg(ambdev, 0x0, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", 0x0, data);

        data = 0xaa;
        test_spi_write_reg(ambdev, 0x0, data);
        printk("test_spi write addr 0x%x, data=0x%x\n", 0x0, data);

        data = 0x0;
        test_spi_read_reg(ambdev, 0x0, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", 0x0, data);


        data = 0x0;
        test_spi_read_reg(ambdev, IMX117_MDSEL1_REG, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", IMX117_MDSEL1_REG, data);

        data = 0xff;
        test_spi_write_reg(ambdev, IMX117_MDSEL1_REG, data);
        printk("test_spi write addr 0x%x, data=0x%x\n", IMX117_MDSEL1_REG, data);

        data = 0x0;
        test_spi_read_reg(ambdev, IMX117_MDSEL1_REG, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", IMX117_MDSEL1_REG, data);

        data = 0x0;
        test_spi_read_reg(ambdev, 0x0005, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", 0x0005, data);

        data = 0x0;
        test_spi_read_reg(ambdev, 0x0006, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", 0x0006, data);

        data = 0x0;
        test_spi_read_reg(ambdev, 0x0007, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", 0x0007, data);



        data = 0x0;
        test_spi_read_reg(ambdev, IMX117_DGAIN_REG, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", IMX117_DGAIN_REG, data);

        data = 0xff;
        test_spi_write_reg(ambdev, IMX117_DGAIN_REG, data);
        printk("test_spi write addr 0x%x, data=0x%x\n", IMX117_DGAIN_REG, data);

        data = 0x0;
        test_spi_read_reg(ambdev, IMX117_DGAIN_REG, &data);
        printk("test_spi read addr 0x%x, data=0x%x\n", IMX117_DGAIN_REG, data);


        return 0;
}

static int test_spi_drv_remove(struct ambpriv_device *ambdev)
{
        printk("%s\n", __func__);
	return 0;
}

static struct ambpriv_driver test_spi_driver = {
	.probe = test_spi_drv_probe,
	.remove = test_spi_drv_remove,
	.driver = {
		.name = "ambarella,test_spi",
		.owner = THIS_MODULE,
	}
};

static struct ambpriv_device *test_spi_device;
static int __init test_spi_init(void)
{
	int rval = 0;

	test_spi_device = ambpriv_create_bundle(&test_spi_driver, NULL, -1, NULL, -1);

	if (IS_ERR(test_spi_device))
		rval = PTR_ERR(test_spi_device);

	return rval;
}

static void __exit test_spi_exit(void)
{
	ambpriv_device_unregister(test_spi_device);
	ambpriv_driver_unregister(&test_spi_driver);
}

late_initcall(test_spi_init);
module_exit(test_spi_exit);

MODULE_DESCRIPTION("test spi");
MODULE_AUTHOR("cychen, <cychen@ambarella.com>");
MODULE_LICENSE("GPL");

