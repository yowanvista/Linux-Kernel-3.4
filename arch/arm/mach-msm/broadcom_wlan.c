#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/wlan_plat.h>
#include <linux/mmc/host.h>

#include "devices.h"

#define WLAN_EN_GPIO	144
#define WLAN_RESET	127
#define GPIO_BT_RESET	146

#define PREALLOC_WLAN_SEC_NUM		4
#define PREALLOC_WLAN_BUF_NUM		160
#define PREALLOC_WLAN_SECTION_HEADER	24

#define WLAN_SECTION_SIZE_0	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1	(PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2	(PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3	(PREALLOC_WLAN_BUF_NUM * 1024)

#define WLAN_SKB_BUF_NUM	17

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wlan_mem_prealloc {
	void *mem_ptr;
	unsigned long size;
};

static struct wlan_mem_prealloc wlan_mem_array[PREALLOC_WLAN_SEC_NUM] = {
	{NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
	{NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)}
};

void* brcm_wlan_mem_prealloc(int section, unsigned long size)
{
	if (section == PREALLOC_WLAN_SEC_NUM)
		return wlan_static_skb;

	if ((section < 0) || (section > PREALLOC_WLAN_SEC_NUM))
		return NULL;

	if (wlan_mem_array[section].size < size)
		return NULL;

	return wlan_mem_array[section].mem_ptr;
}

#define DHD_SKB_HDRSIZE 	336
#define DHD_SKB_1PAGE_BUFSIZE	((PAGE_SIZE*1)-DHD_SKB_HDRSIZE)
#define DHD_SKB_2PAGE_BUFSIZE	((PAGE_SIZE*2)-DHD_SKB_HDRSIZE)
#define DHD_SKB_4PAGE_BUFSIZE	((PAGE_SIZE*4)-DHD_SKB_HDRSIZE)

static int brcm_init_wlan_mem(void)
{
	int i;
	int j;

	for (i = 0; i < 8; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_1PAGE_BUFSIZE);
		if (!wlan_static_skb[i])
			goto err_skb_alloc;
	}

	for (; i < 16; i++) {
		wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_2PAGE_BUFSIZE);
		if (!wlan_static_skb[i])
			goto err_skb_alloc;
	}

	wlan_static_skb[i] = dev_alloc_skb(DHD_SKB_4PAGE_BUFSIZE);
	if (!wlan_static_skb[i])
		goto err_skb_alloc;

	for (i = 0 ; i < PREALLOC_WLAN_SEC_NUM ; i++) {
		wlan_mem_array[i].mem_ptr =
				kmalloc(wlan_mem_array[i].size, GFP_KERNEL);

		if (!wlan_mem_array[i].mem_ptr)
			goto err_mem_alloc;
	}
	printk("%s: WIFI MEM Allocated\n", __FUNCTION__);
	return 0;

err_mem_alloc:
	pr_err("Failed to mem_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		kfree(wlan_mem_array[j].mem_ptr);

	i = WLAN_SKB_BUF_NUM;

err_skb_alloc:
	pr_err("Failed to skb_alloc for WLAN\n");
	for (j = 0 ; j < i ; j++)
		dev_kfree_skb(wlan_static_skb[j]);

	return -ENOMEM;
}

/* Customized Locale table : OPTIONAL feature */
#define WLC_CNTRY_BUF_SZ	4
typedef struct cntry_locales_custom {
	char iso_abbrev[WLC_CNTRY_BUF_SZ];
	char custom_locale[WLC_CNTRY_BUF_SZ];
	int  custom_locale_rev;
} cntry_locales_custom_t;

static cntry_locales_custom_t brcm_wlan_translate_custom_table[] = {
/* Table should be filled out based on custom platform regulatory requirement */
	{"",   "XY", 4},  /* universal */
	{"US", "US", 69}, /* input ISO "US" to : US regrev 69 */
	{"CA", "US", 69}, /* input ISO "CA" to : US regrev 69 */
	{"EU", "EU", 5},  /* European union countries */
	{"AT", "EU", 5},
	{"BE", "EU", 5},
	{"BG", "EU", 5},
	{"CY", "EU", 5},
	{"CZ", "EU", 5},
	{"DK", "EU", 5},
	{"EE", "EU", 5},
	{"FI", "EU", 5},
	{"FR", "EU", 5},
	{"DE", "EU", 5},
	{"GR", "EU", 5},
	{"HU", "EU", 5},
	{"IE", "EU", 5},
	{"IT", "EU", 5},
	{"LV", "EU", 5},
	{"LI", "EU", 5},
	{"LT", "EU", 5},
	{"LU", "EU", 5},
	{"MT", "EU", 5},
	{"NL", "EU", 5},
	{"PL", "EU", 5},
	{"PT", "EU", 5},
	{"RO", "EU", 5},
	{"SK", "EU", 5},
	{"SI", "EU", 5},
	{"ES", "EU", 5},
	{"SE", "EU", 5},
	{"GB", "EU", 5},  /* input ISO "GB" to : EU regrev 05 */
	{"IL", "IL", 0},
	{"CH", "CH", 0},
	{"TR", "TR", 0},
	{"NO", "NO", 0},
	{"KR", "XY", 3},
	{"AU", "XY", 3},
	{"CN", "XY", 3},  /* input ISO "CN" to : XY regrev 03 */
	{"TW", "XY", 3},
	{"AR", "XY", 3},
	{"MX", "XY", 3}
};

static void *brcm_wlan_get_country_code(char *ccode)
{
	int size = ARRAY_SIZE(brcm_wlan_translate_custom_table);
	int i;

	if (!ccode)
		return NULL;

	for (i = 0; i < size; i++)
		if (strcmp(ccode, brcm_wlan_translate_custom_table[i].iso_abbrev) == 0)
			return &brcm_wlan_translate_custom_table[i];
	return &brcm_wlan_translate_custom_table[0];
}

static int brcm_wlan_set_carddetect(int onoff)
{
	struct mmc_host *mmc = platform_get_drvdata(&msm_device_sdc1);

	printk(KERN_INFO"msm_device_sdc1.name= %d\n", msm_device_sdc1.id);

	mmc_detect_change(mmc, msecs_to_jiffies(60));

	printk(KERN_ERR "wlan_carddetect_en = %d ~~~\n", onoff);
	return 0;
}

int brcm_wlan_power(int on)
{
	printk ("Before %s: WLAN_EN_GPIO value before set is %d on=%d WLAN_RESET=%d \n", __func__, gpio_get_value (WLAN_EN_GPIO),on,gpio_get_value (WLAN_RESET));
	if(on)
	{
		if (gpio_tlmm_config (GPIO_CFG(WLAN_EN_GPIO, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE))
		{
				printk (KERN_ERR "%s: Unable configure WLAN_EN_GPIO\n", __func__);
				return -1;
		}

		gpio_set_value (WLAN_EN_GPIO, on);
	}
	else
	{
		if(!gpio_get_value(GPIO_BT_RESET))
		{
			printk("Switching OFF Enable pin \n");
			if (gpio_tlmm_config (GPIO_CFG(WLAN_EN_GPIO, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE))
			{
					printk (KERN_ERR "%s: Unable configure WLAN_EN_GPIO\n", __func__);
					return -1;
			}

			gpio_set_value (WLAN_EN_GPIO, on);
		}
		else
		{
			printk("BT is Enabled\n");
		}
	}
	printk ("%s: WLAN_EN_GPIO value before set is %d on=%d \n", __func__, gpio_get_value (WLAN_EN_GPIO),on);
	if (gpio_tlmm_config (GPIO_CFG(WLAN_RESET, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE))
	{
			printk (KERN_ERR "%s: Unable configure WLAN_RESET \n", __func__);
			return -1;
	}

	mdelay (100);
	gpio_set_value (WLAN_RESET, 0);
	mdelay (100);
	printk("Just Resetting WIFI ONCE,before POWER ON\n");
	gpio_set_value (WLAN_RESET, on);
	mdelay (10);
	printk ("After %s: WLAN_EN_GPIO value before set is %d on=%d WLAN_RESET=%d \n", __func__, gpio_get_value (WLAN_EN_GPIO),on,gpio_get_value (WLAN_RESET));
	return 0;
}

int brcm_wlan_reset(int on)
{
	printk("Reseting WLAN...\n");
	if (gpio_tlmm_config (GPIO_CFG(WLAN_RESET, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_UP, GPIO_CFG_8MA), GPIO_CFG_ENABLE))
	{
		printk (KERN_ERR "%s: Unable configure WLAN_RESET \n", __func__);
		return -1;
	}

	gpio_set_value (WLAN_RESET, on);
	return 0;
}

static struct resource brcm_wlan_resources[] = {
	[0] = {
		.name   = "bcm4329_wlan_irq",
		.start  = MSM_GPIO_TO_INT(111),
		.end    = MSM_GPIO_TO_INT(111),
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct wifi_platform_data brcm_wlan_control = {
	.set_power		= brcm_wlan_power,
	.set_reset		= brcm_wlan_reset,
	.set_carddetect		= brcm_wlan_set_carddetect,
	.mem_prealloc		= brcm_wlan_mem_prealloc,
	.get_country_code	= brcm_wlan_get_country_code,
};

static struct platform_device brcm_device_wlan = {
	.name			= "bcm4329_wlan",
	.id			= 1,
	.num_resources		= ARRAY_SIZE(brcm_wlan_resources),
	.resource		= brcm_wlan_resources,
	.dev			= {
		.platform_data = &brcm_wlan_control,
	},
};

int __init brcm_wlan_init(void)
{
	printk(KERN_INFO "%s: start\n", __func__);

	brcm_init_wlan_mem();

	return platform_device_register(&brcm_device_wlan);
}
