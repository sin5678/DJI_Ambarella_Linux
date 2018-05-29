/*
 * Filename : test_i2c.c
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
#include <linux/i2c.h>

struct test_i2c {
        void *control_data;
} test_i2c_obj;

static int test_i2c_write_reg(struct test_i2c *test_i2c, u32 subaddr, u32 data)
{
	int rval;
	struct i2c_client *client;
	struct i2c_msg msgs[1];
	u8 pbuf[3];

	client = test_i2c->control_data;

        pbuf[0] = (subaddr &0xff00) >> 8;
        pbuf[1] = subaddr & 0xff;;
	pbuf[2] = data;

	msgs[0].len = 3;
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].buf = pbuf;

	rval = i2c_transfer(client->adapter, msgs, 1);
	if (rval < 0) {
                printk("%s faile(%d): [0x%x]\n", __func__, rval, subaddr);
		return rval;
	}

	return 0;
}

static int test_i2c_read_reg(struct test_i2c *test_i2c, u32 subaddr, u32 *data)
{
	int rval = 0;
	struct i2c_client *client;
	struct i2c_msg msgs[2];
	u8 pbuf[2];
	u8 pbuf0[2];

	client = test_i2c->control_data;

        pbuf0[0] = (subaddr &0xff00) >> 8;
        pbuf0[1] = subaddr & 0xff;;

	msgs[0].len = 2;
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].buf = pbuf0;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].buf = pbuf;
	msgs[1].len = 1;

	rval = i2c_transfer(client->adapter, msgs, 2);
	if (rval < 0){
                printk("%s faile(%d): [0x%x]\n", __func__, rval, subaddr);
		return rval;
	}

	*data = pbuf[0];

	return 0;
}

/*
 * This test does simple I2C registers read/write on IMX377 sensor.
 *
 * Test env:
 *   A9S EVK Cheetah V11 + Sony IMX377 on I2C0@100KHz.
 *
 * Sony IMX377 config:
 *   The HW reset and sensor clock needs to apply to sensor.
 *
 * Test result:
 *      [    0.928779] test_i2c read addr 0x3123, data=0x1
 *      [    0.933697] test_i2c write addr 0x3123, data=0x3
 *      [    0.938817] test_i2c read addr 0x3123, data=0x3
 */

#define SENSOR_READ_PLRD10_ADDR     0x3123
static int test_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
        u32 data;

        test_i2c_obj.control_data = client;

        data = 0;
        test_i2c_read_reg(&test_i2c_obj, SENSOR_READ_PLRD10_ADDR, &data);
        printk("test_i2c read addr 0x%x, data=0x%x\n", SENSOR_READ_PLRD10_ADDR, data);

        data = 0x3;
        test_i2c_write_reg(&test_i2c_obj, SENSOR_READ_PLRD10_ADDR, data);
        printk("test_i2c write addr 0x%x, data=0x%x\n", SENSOR_READ_PLRD10_ADDR, data);

        data = 0;
        test_i2c_read_reg(&test_i2c_obj, SENSOR_READ_PLRD10_ADDR, &data);
        printk("test_i2c read addr 0x%x, data=0x%x\n", SENSOR_READ_PLRD10_ADDR, data);

        return 0;
}

static int test_i2c_remove(struct i2c_client *client)
{
        printk("test_i2c_remove\n");
	return 0;
}


static const struct of_device_id test_i2c_dt_ids[] = {
	{ .compatible = "test_i2c", },
	{ }
};
MODULE_DEVICE_TABLE(of, test_i2c_dt_ids);


static const struct i2c_device_id test_i2c_idtable[] = {
	{ "test_i2c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, test_i2c_idtable);

static struct i2c_driver i2c_driver_test_i2c = {
	.driver = {
		.name	= "test_i2c",
                .of_match_table = test_i2c_dt_ids,
	},

	.id_table	= test_i2c_idtable,
	.probe		= test_i2c_probe,
	.remove		= test_i2c_remove,

};

static int __init test_i2c_init(void)
{
	int rval;

	rval = i2c_add_driver(&i2c_driver_test_i2c);
	if (rval < 0)
		return rval;

	return 0;
}

static void __exit test_i2c_exit(void)
{
	i2c_del_driver(&i2c_driver_test_i2c);
}

late_initcall(test_i2c_init);
module_exit(test_i2c_exit);

MODULE_DESCRIPTION("test i2c");
MODULE_AUTHOR("cychen, <cychen@ambarella.com>");
MODULE_LICENSE("GPL");

