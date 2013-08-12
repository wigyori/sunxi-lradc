/*
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
* along with this program; if not, write to the Free Software
*
* Based on sun4i-keyboard.c from AllWinner
* Fixes from linux-sunxi.org
* Sample code taken from tegra-kbc.c
*
* CONFIG_HAS_EARLYSUSPEND codes were ripped out for the time being
*
* Copyright (c) 2013 Zoltan HERPAI <wigyori@uid0.hu>
*
* ChangeLog
*
*
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/keyboard.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/timer.h>

// -----------------------------------------------------------------------------

#define  PRINT_SUSPEND_INFO

#define  KEY_MAX_CNT  		(13)

#define  KEY_BASSADDRESS	(0xf1c22800)
#define  LRADC_CTRL		(0x00)
#define  LRADC_INTC		(0x04)
#define  LRADC_INT_STA 		(0x08)
#define  LRADC_DATA0		(0x0c)
#define  LRADC_DATA1		(0x10)
#define	SW_INT_IRQNO_LRADC	31

#define  FIRST_CONCERT_DLY		(2<<24)
#define  CHAN				(0x3)
#define  ADC_CHAN_SELECT		(CHAN<<22)
#define  LRADC_KEY_MODE		(0)
#define  KEY_MODE_SELECT		(LRADC_KEY_MODE<<12)
#define  LEVELB_VOL			(0<<4)

#define  LRADC_HOLD_EN		(1<<6)

#define  LRADC_SAMPLE_32HZ		(3<<2)
#define  LRADC_SAMPLE_62HZ		(2<<2)
#define  LRADC_SAMPLE_125HZ		(1<<2)
#define  LRADC_SAMPLE_250HZ		(0<<2)


#define  LRADC_EN			(1<<0)

#define  LRADC_ADC1_UP_EN		(1<<12)
#define  LRADC_ADC1_DOWN_EN		(1<<9)
#define  LRADC_ADC1_DATA_EN		(1<<8)

#define  LRADC_ADC0_UP_EN		(1<<4)
#define  LRADC_ADC0_DOWN_EN		(1<<1)
#define  LRADC_ADC0_DATA_EN		(1<<0)

#define  LRADC_ADC1_UPPEND		(1<<12)
#define  LRADC_ADC1_DOWNPEND	(1<<9)
#define  LRADC_ADC1_DATAPEND		(1<<8)


#define  LRADC_ADC0_UPPEND 		(1<<4)
#define  LRADC_ADC0_DOWNPEND	(1<<1)
#define  LRADC_ADC0_DATAPEND		(1<<0)

#define EVB
//#define CUSTUM
#define ONE_CHANNEL
#define MODE_0V2
//#define MODE_0V15
//#define TWO_CHANNEL

// MODE_0V2
#ifdef MODE_0V2
//standard of key maping
//0.2V mode

#define REPORT_START_NUM			(5)
#define REPORT_KEY_LOW_LIMIT_COUNT		(3)
#define MAX_CYCLE_COUNTER			(100)
//#define REPORT_REPEAT_KEY_BY_INPUT_CORE
//#define REPORT_REPEAT_KEY_FROM_HW
#define INITIAL_VALUE				(0Xff)

static unsigned char keypad_mapindex[64] =
{
    0,0,0,0,0,0,0,0,               //key 1, 8个， 0-7
    1,1,1,1,1,1,1,                 //key 2, 7个， 8-14
    2,2,2,2,2,2,2,                 //key 3, 7个， 15-21
    3,3,3,3,3,3,                   //key 4, 6个， 22-27
    4,4,4,4,4,4,                   //key 5, 6个， 28-33
    5,5,5,5,5,5,                   //key 6, 6个， 34-39
    6,6,6,6,6,6,6,6,6,6,           //key 7, 10个，40-49
    7,7,7,7,7,7,7,7,7,7,7,7,7,7    //key 8, 17个，50-63
};
#endif // MODE_0V2

#ifdef MODE_0V15
//0.15V mode
static unsigned char keypad_mapindex[64] =
{
	0,0,0,                      //key1
	1,1,1,1,1,                  //key2
	2,2,2,2,2,
	3,3,3,3,
	4,4,4,4,4,
	5,5,5,5,5,
	6,6,6,6,6,
	7,7,7,7,
	8,8,8,8,8,
	9,9,9,9,9,
	10,10,10,10,
	11,11,11,11,
	12,12,12,12,12,12,12,12,12,12 //key13
};
#endif // MODE_0V15

#ifdef EVB
static unsigned int sun4i_scankeycodes[KEY_MAX_CNT]=
{
	[0 ] = KEY_VOLUMEUP,
	[1 ] = KEY_VOLUMEDOWN,
	[2 ] = KEY_MENU,
	[3 ] = KEY_SEARCH,
	[4 ] = KEY_HOME,
	[5 ] = KEY_ESC,
	[6 ] = KEY_ENTER,
	[7 ] = KEY_RESERVED,
	[8 ] = KEY_RESERVED,
	[9 ] = KEY_RESERVED,
	[10] = KEY_RESERVED,
	[11] = KEY_RESERVED,
	[12] = KEY_RESERVED,
};
#endif // MODE_EVB

struct sun4i_kbc {
        struct input_dev *input;
	struct timer_list timer;
	int irq;
	unsigned int rows;
	unsigned int cols;
	unsigned long delay;
	unsigned int debounce;
	void __iomem *mmio;
	struct clk *clk;
	struct input_dev *idev;
	const struct sun4i_kbc_platform_data *pdata;
	
};
	// taken from tegra-kbc
								
// -----------------------------------------------------------------------------

static volatile unsigned int key_val;
static struct input_dev *sun4ikbd_dev;
static unsigned char scancode;

static unsigned char key_cnt = 0;
static unsigned char cycle_buffer[REPORT_START_NUM] = {0};
static unsigned char transfer_code = INITIAL_VALUE;

static irqreturn_t sun4i_isr_key(int irq, void *dummy)
{
	unsigned int  reg_val;
	int judge_flag = 0;
	int loop = 0;

	#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
	    printk("Key Interrupt\n");
  	#endif
	
	reg_val  = readl(KEY_BASSADDRESS + LRADC_INT_STA);
	//writel(reg_val,KEY_BASSADDRESS + LRADC_INT_STA);
	if(reg_val&LRADC_ADC0_DOWNPEND)
	{
		#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
		    printk("key down\n");
		#endif
	}

	if(reg_val&LRADC_ADC0_DATAPEND)
	{
		key_val = readl(KEY_BASSADDRESS+LRADC_DATA0);
		if(key_val < 0x3f)
		{
		/*key_val = readl(KEY_BASSADDRESS + LRADC_DATA0);
		cancode = keypad_mapindex[key_val&0x3f];
#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
		printk("raw data: key_val == %u , scancode == %u \n", key_val, scancode);
#endif
		*/
		cycle_buffer[key_cnt%REPORT_START_NUM] = key_val&0x3f;
		if((key_cnt + 1) < REPORT_START_NUM)
		{
			//do not report key message

		}else{
			//scancode = cycle_buffer[(key_cnt-2)%REPORT_START_NUM];
			if(cycle_buffer[(key_cnt - REPORT_START_NUM + 1)%REPORT_START_NUM] \
			== cycle_buffer[(key_cnt - REPORT_START_NUM + 2)%REPORT_START_NUM])
			{
			key_val = cycle_buffer[(key_cnt - REPORT_START_NUM + 1)%REPORT_START_NUM];
			scancode = keypad_mapindex[key_val&0x3f];
			judge_flag = 1;

			}
			if((!judge_flag) && cycle_buffer[(key_cnt - REPORT_START_NUM + 4)%REPORT_START_NUM] \
			== cycle_buffer[(key_cnt - REPORT_START_NUM + 5)%REPORT_START_NUM])
			{
			key_val = cycle_buffer[(key_cnt - REPORT_START_NUM + 5)%REPORT_START_NUM];
			scancode = keypad_mapindex[key_val&0x3f];
			judge_flag = 1;

			}
			if(1 == judge_flag)
			{
#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
				printk("report data: key_val :%8d transfer_code: %8d , scancode: %8d\n",\
				key_val, transfer_code, scancode);
#endif

				if(transfer_code == scancode){
				//report repeat key value
#ifdef REPORT_REPEAT_KEY_FROM_HW
				input_report_key(sun4ikbd_dev, sun4i_scankeycodes[scancode], 0);
				input_sync(sun4ikbd_dev);
				input_report_key(sun4ikbd_dev, sun4i_scankeycodes[scancode], 1);
				input_sync(sun4ikbd_dev);
#else
				//do not report key value
#endif
				}else if(INITIAL_VALUE != transfer_code){
				//report previous key value up signal + report current key value down
				input_report_key(sun4ikbd_dev, sun4i_scankeycodes[transfer_code], 0);
				input_sync(sun4ikbd_dev);
				input_report_key(sun4ikbd_dev, sun4i_scankeycodes[scancode], 1);
				input_sync(sun4ikbd_dev);
				transfer_code = scancode;

				}else{
				//INITIAL_VALUE == transfer_code, first time to report key event
				input_report_key(sun4ikbd_dev, sun4i_scankeycodes[scancode], 1);
				input_sync(sun4ikbd_dev);
				transfer_code = scancode;
				}

			}

			}
			key_cnt++;
			if(key_cnt > 2 * MAX_CYCLE_COUNTER ){
			key_cnt -= MAX_CYCLE_COUNTER;
			}

		}
	}

	if(reg_val&LRADC_ADC0_UPPEND)
	{
		if(key_cnt > REPORT_START_NUM)
		{
			if(INITIAL_VALUE != transfer_code)
			{
#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
			printk("report data: key_val :%8d transfer_code: %8d \n",key_val, transfer_code);
#endif
			input_report_key(sun4ikbd_dev, sun4i_scankeycodes[transfer_code], 0);
			input_sync(sun4ikbd_dev);
			}

		}else if((key_cnt + 1) >= REPORT_KEY_LOW_LIMIT_COUNT){
			//rely on hardware first_delay work, need to be verified!
			if(cycle_buffer[0] == cycle_buffer[1]){
				key_val = cycle_buffer[0];
				scancode = keypad_mapindex[key_val&0x3f];
#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
				printk("report data: key_val :%8d scancode: %8d \n",key_val, scancode);
#endif
				input_report_key(sun4ikbd_dev, sun4i_scankeycodes[scancode], 1);
				input_sync(sun4ikbd_dev);
				input_report_key(sun4ikbd_dev, sun4i_scankeycodes[scancode], 0);
				input_sync(sun4ikbd_dev);
			}

		}

#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
		printk("key up \n");
#endif

		key_cnt = 0;
		judge_flag = 0;
		transfer_code = INITIAL_VALUE;
		for(loop = 0; loop < REPORT_START_NUM; loop++)
		{
			cycle_buffer[loop] = 0;
		}

	}

	writel(reg_val,KEY_BASSADDRESS + LRADC_INT_STA);
	return IRQ_HANDLED;
}


/*              EZ ITT AZ INIT
*
*
*
*
*
*/
/*static int __init sun4ikbd_init(void)
{

	sun4ikbd_dev = input_allocate_device();
	if (!sun4ikbd_dev) {
		printk(KERN_ERR "sun4ikbd: not enough memory for input device\n");
		err = -ENOMEM;
		goto fail1;
	}
}
*/

static int sun4i_keyboard_probe(struct platform_device *pdev)
{
        struct input_dev *input;
        struct sun4i_kbc *sun4i_kbd;
        struct resource *res;
        int irq, i, error;
	//int i;
	//int err = 0;

#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
	printk("sun4i_keyboard_probe\n");
#endif

	// code from opencores-kbd
	

        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        if (!res) {
                dev_err(&pdev->dev, "missing board memory resource\n");
                return -EINVAL;
        }

        irq = platform_get_irq(pdev, 0);
        if (irq < 0) {
                dev_err(&pdev->dev, "missing board IRQ resource\n");
                return -EINVAL;
        }

        sun4i_kbd = kzalloc(sizeof(*sun4i_kbd), GFP_KERNEL);
        input = input_allocate_device();
        if (!sun4i_kbd || !input) {
                dev_err(&pdev->dev, "failed to allocate device structures\n");
                error = -ENOMEM;
                goto err_free_mem;
        }


	sun4ikbd_dev->name = "sun4i-keyboard";
	sun4ikbd_dev->phys = "sun4ikbd/input0";
	sun4ikbd_dev->id.bustype = BUS_HOST;
	sun4ikbd_dev->id.vendor = 0x0001;
	sun4ikbd_dev->id.product = 0x0001;
	sun4ikbd_dev->id.version = 0x0100;

#ifdef REPORT_REPEAT_KEY_BY_INPUT_CORE
	sun4ikbd_dev->evbit[0] = BIT_MASK(EV_KEY)|BIT_MASK(EV_REP);
	printk("REPORT_REPEAT_KEY_BY_INPUT_CORE is defined, support report repeat key value. \n");
#else
	sun4ikbd_dev->evbit[0] = BIT_MASK(EV_KEY);
#endif

	for (i = 0; i < KEY_MAX_CNT; i++)
		set_bit(sun4i_scankeycodes[i], sun4ikbd_dev->keybit);

#ifdef ONE_CHANNEL
	writel(LRADC_ADC0_DOWN_EN|LRADC_ADC0_UP_EN|LRADC_ADC0_DATA_EN,KEY_BASSADDRESS + LRADC_INTC);
	writel(FIRST_CONCERT_DLY|LEVELB_VOL|KEY_MODE_SELECT|LRADC_HOLD_EN|ADC_CHAN_SELECT|LRADC_SAMPLE_62HZ|LRADC_EN,KEY_BASSADDRESS + LRADC_CTRL);
	//writel(FIRST_CONCERT_DLY|LEVELB_VOL|KEY_MODE_SELECT|ADC_CHAN_SELECT|LRADC_SAMPLE_62HZ|LRADC_EN,KEY_BASSADDRESS + LRADC_CTRL);

#else
#endif


	// original IRQ request code - has been replaced with IRQ placing above
	/*if (request_irq(SW_INT_IRQNO_LRADC, sun4i_isr_key, 0, "sun4ikbd", NULL)){
		err = -EBUSY;
		printk("request irq failure. \n");
		goto fail2;
	}
	*/
	
	error = input_register_device(sun4ikbd_dev);
	if (error)
		goto fail3;

	return 0;
 fail3:
	free_irq(sun4i_kbd->irq, sun4i_isr_key);
 fail2:
	input_free_device(sun4ikbd_dev);
 fail1:
	return error;
 err_free_mem:
    input_free_device(input);
    kfree(sun4i_kbd);
			 
#ifdef CONFIG_KEYBOARD_SUN4I_KEYBOARD_DEBUG
	printk("sun4ikbd_init failed. \n");
#endif

 return error;
}


/*
* original exit crap
*/

/*
static void __exit sun4ikbd_exit(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	 unregister_early_suspend(&keyboard_data->early_suspend);
#endif
	free_irq(SW_INT_IRQNO_LRADC, sun4i_isr_key);
	input_unregister_device(sun4ikbd_dev);
}

        kbc = kzalloc(sizeof(*kbc), GFP_KERNEL);
        input_dev = input_allocate_device();
        if (!kbc || !input_dev) {
                err = -ENOMEM;
                goto err_free_mem;
        }
*/

/* new DTS exit code
*/

static int sun4i_keyboard_remove(struct platform_device *pdev)
{
        struct sun4i_kbc *kbc = platform_get_drvdata(pdev);
        struct resource *res;

        platform_set_drvdata(pdev, NULL);

        free_irq(kbc->irq, pdev);
        clk_put(kbc->clk);

        input_unregister_device(kbc->idev);
        iounmap(kbc->mmio);
        res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
        release_mem_region(res->start, resource_size(res));

        /*
 *          * If we do not have platform data attached to the device we
 *                   * allocated it ourselves and thus need to free it.
 *                            */
        if (!pdev->dev.platform_data)
                kfree(kbc->pdata);

        kfree(kbc);

        return 0;
}

static const struct of_device_id sun4i_keyboard_of_match[] = {
        { .compatible = "allwinner,sun4i-keyboard", },
        { },
};


								
static struct platform_driver sun4i_keyboard_driver = {
        .probe          = sun4i_keyboard_probe,
        .remove         = sun4i_keyboard_remove,
        .driver = {
                .name   = "sun4i-keyboard",
                .owner  = THIS_MODULE,
                .of_match_table = sun4i_keyboard_of_match,
        },
};
module_platform_driver(sun4i_keyboard_driver);

MODULE_AUTHOR("Zoltan HERPAI <wigyori@uid0.hu>");
MODULE_DESCRIPTION("sun4i-keyboard driver");
MODULE_LICENSE("GPL");

