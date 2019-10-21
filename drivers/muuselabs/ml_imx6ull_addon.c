#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/slab.h> //kmalloc()
#include <linux/sysfs.h>
#include <linux/uaccess.h> //copy_to/from_user()

#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/uaccess.h>


#define SNVS_LPCR                   0x38
#define SNVS_LPSR                   0x4C
#define SNVS_LPLR                   0x34
#define SNVS_LPGPR                  0x68
#define SNVS_LPGPR0                  0x90
#define SNVS_LPGPR1                  0x94
#define SNVS_LPGPR2                  0x98

#define SNVS_LPTDC2R                0xA0
#define SNVS_LPTDSR                 0xA4
#define SNVS_LPPGDR                 0x64
#define SNVS_BASE_ADDR   0x020CC000

dev_t dev = 0;
static struct class *dev_class;
static struct cdev ml_imx6ull_addon_cdev;
struct kobject *kobj_ref;

static int __init ml_imx6ull_addon_driver_init(void);
static void __exit ml_imx6ull_addon_driver_exit(void);

/*************** Driver Fuctions **********************/
static int ml_imx6ull_addon_open(struct inode *inode, struct file *file);
static int ml_imx6ull_addon_release(struct inode *inode, struct file *file);
static ssize_t ml_imx6ull_addon_read(struct file *filp, char __user *buf,
				     size_t len, loff_t *off);
static ssize_t ml_imx6ull_addon_write(struct file *filp, const char *buf,
				      size_t len, loff_t *off);

/*************** Sysfs Fuctions **********************/
/*
volatile int ml_imx6ull_addon_value = 0;

static ssize_t sysfs_show(struct kobject *kobj,
						  struct kobj_attribute *attr,
char *buf);
static ssize_t sysfs_store(struct kobject *kobj,
						   struct kobj_attribute *attr,
const char *buf, size_t count);
struct kobj_attribute ml_imx6ull_addon_attr = __ATTR(ml_imx6ull_addon_value,
0660, sysfs_show, sysfs_store);


static ssize_t sysfs_show(struct kobject *kobj,
struct kobj_attribute *attr, char *buf)
{
printk(KERN_INFO "Sysfs - Read!!!\n");
return sprintf(buf, "%d", ml_imx6ull_addon_value);
}

static ssize_t sysfs_store(struct kobject *kobj,
struct kobj_attribute *attr, const char *buf, size_t count)
{
printk(KERN_INFO "Sysfs - Write!!!\n");
sscanf(buf,"%d",&ml_imx6ull_addon_value);
return count;
}

*/

/* GPR register helper */
typedef union {
	struct {
		uint32_t LPGPR0;
		uint32_t LPGPR1;
		uint32_t LPGPR2;
	} raw;

	struct {
		uint8_t fast_boot_en:1;
		uint8_t boot_part:2;
	} __attribute__((packed)) fields;
} gpr_regs_t;

/* static will make sure it's initialized with zeros */
static const gpr_regs_t empty_gpr;

static void load_gpr(gpr_regs_t *target) {
	uint32_t __iomem *membase;
	membase = ioremap(SNVS_BASE_ADDR + SNVS_LPGPR0, 4);
	memcpy(target, membase, 3*4);
}

static void save_gpr(gpr_regs_t new, gpr_regs_t mask) {
	uint32_t __iomem *membase;
	gpr_regs_t old;
	load_gpr(&old);

	new.raw.LPGPR0 &= mask.raw.LPGPR0;
	new.raw.LPGPR1 &= mask.raw.LPGPR1;
	new.raw.LPGPR2 &= mask.raw.LPGPR2;

	new.raw.LPGPR0 |= (~mask.raw.LPGPR0) & old.raw.LPGPR0;
	new.raw.LPGPR1 |= (~mask.raw.LPGPR1) & old.raw.LPGPR1;
	new.raw.LPGPR2 |= (~mask.raw.LPGPR2) & old.raw.LPGPR2;

	membase = ioremap(SNVS_BASE_ADDR + SNVS_LPGPR0, 4);
	memcpy(membase, &new, 3*4);
}

/* TAMPER registers helper */
typedef union {
	struct {
		uint32_t LPTDC2R;
		uint32_t LPTDSR;
		uint32_t LPTGF1CR;
		uint32_t LPTGF2CR;
	} raw;

	struct {
		// SNVS_LPTDC2R
		uint8_t xtal_en_en:1;
		uint8_t wl_host_wake_en:1;
		uint8_t play_en:1;
		uint8_t chg_sta_b_en:1;
		uint8_t xtal_int_b_en:1;
		uint8_t nfc_irq_out_en:1;
		uint8_t nfc_irq_in_en:1;
		uint8_t amp_fault_en:1;

		uint8_t reserved_1:8;

		uint8_t xtal_en_pol:1;
		uint8_t wl_host_wake_pol:1;
		uint8_t play_pol:1;
		uint8_t chg_sta_b_pol:1;
		uint8_t xtal_int_b_pol:1;
		uint8_t nfc_irq_out_pol:1;
		uint8_t nfc_irq_in_pol:1;
		uint8_t amp_fault_pol:1;

		uint8_t reserved_2:8;

		//SNVS_LPTDSR
		uint8_t xtal_en_status:1;
		uint8_t wl_host_wake_status:1;
		uint8_t play_status:1;
		uint8_t chg_sta_b_status:1;
		uint8_t xtal_int_b_status:1;
		uint8_t nfc_irq_out_status:1;
		uint8_t nfc_irq_in_status:1;
		uint8_t amp_fault_status:1;

		uint32_t reserved_3:24;

		// LPTGF1CR
		uint8_t chg_sta_b_filter_en:1;
		uint8_t chg_sta_b_filter_cfg:7;

		uint8_t play_filter_en:1;
		uint8_t play_filter_cfg:7;

		uint8_t wl_host_wake_filter_en:1;
		uint8_t wl_host_wake_filter_cfg:7;

		uint8_t xtal_en_filter_en:1;
		uint8_t xtal_en_filter_cfg:7;

		// LPTGF2CR
		uint8_t amp_fault_filter_en:1;
		uint8_t amp_fault_filter_cfg:7;

		uint8_t nfc_irq_in_filter_en:1;
		uint8_t nfc_irq_in_filter_cfg:7;

		uint8_t nfc_irq_out_filter_en:1;
		uint8_t nfc_irq_out_filter_cfg:7;

		uint8_t xtal_int_b_filter_en:1;
		uint8_t xtal_int_b_filter_cfg:7;
	} __attribute__((packed)) fields;
} tamper_regs_t;

/* static will make sure it's initialized with zeros */
static const tamper_regs_t empty_tamper;

static void load_tamper(tamper_regs_t *target) {
	uint32_t __iomem *membase;
	membase = ioremap(SNVS_BASE_ADDR + SNVS_LPTDC2R, 4);
	memcpy(target, membase, 4*4);
}

static void save_tamper(tamper_regs_t new, tamper_regs_t mask) {
	uint32_t __iomem *membase;
	tamper_regs_t old;
	load_tamper(&old);

	new.raw.LPTDC2R  &= mask.raw.LPTDC2R;
	new.raw.LPTDSR   &= mask.raw.LPTDSR;
	new.raw.LPTGF1CR &= mask.raw.LPTGF1CR;
	new.raw.LPTGF2CR &= mask.raw.LPTGF2CR;

	new.raw.LPTDC2R  |= (~mask.raw.LPTDC2R)  & old.raw.LPTDC2R;
	new.raw.LPTDSR   |= (~mask.raw.LPTDSR)   & old.raw.LPTDSR;
	new.raw.LPTGF1CR |= (~mask.raw.LPTGF1CR) & old.raw.LPTGF1CR;
	new.raw.LPTGF2CR |= (~mask.raw.LPTGF2CR) & old.raw.LPTGF2CR;

	membase = ioremap(SNVS_BASE_ADDR + SNVS_LPTDC2R, 4);
	memcpy(membase, &new, 4*4);
}

static ssize_t vbus_state_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	uint32_t __iomem *membase = ioremap(0x020c81c0, 4);
	uint32_t val = *membase;
	//printk(KERN_DEBUG "VBUS state 0x%08X!!!\n", val);
	return sprintf(buf, "%c", (val & 0x04) ? '1' : '0');
}

struct kobj_attribute vbus_state_attr =
	__ATTR(vbus_state, 0444, vbus_state_show, NULL);


static ssize_t gpr_state_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	gpr_regs_t gpr;
	load_gpr(&gpr);
	return sprintf(buf, "%08X %08X %08X", gpr.raw.LPGPR0, gpr.raw.LPGPR1, gpr.raw.LPGPR2);
}

struct kobj_attribute gpr_state_attr =
	__ATTR(gpr_state, 0444, gpr_state_show, NULL);

static ssize_t wake_state_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	tamper_regs_t tamper;
	load_tamper(&tamper);

	return sprintf(buf, "%08X %08X %08X %08X\n",
		   tamper.raw.LPTDC2R, tamper.raw.LPTDSR,
		   tamper.raw.LPTGF1CR, tamper.raw.LPTGF2CR);
}

struct kobj_attribute wake_state_attr =
	__ATTR(wake_state, 0444, wake_state_show, NULL);

/* configuration of specific wakeup sources */
#define WAKE_ATTR_DEF(__name__, __field__, __default_polarity__, __default_filter_en__, __default_filter_cfg__) \
	static ssize_t __name__ ## _show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) \
	{ \
		tamper_regs_t tamper; \
		load_tamper(&tamper); \
		return sprintf(buf, "%d %d %d %d\n", \
					   tamper.fields.__field__ ## _en, tamper.fields.__field__ ## _pol, \
					   tamper.fields.__field__ ## _filter_en, tamper.fields.__field__ ## _filter_cfg); \
	} \
	\
	static ssize_t __name__ ## _store(struct kobject *kobj, struct kobj_attribute *attr, \
	                      const char *buf, size_t count) \
	{ \
		int rv; \
		int en = 0; \
		int pol = __default_polarity__; \
		int filter_en = __default_filter_en__; \
		int filter_cfg = __default_filter_cfg__; \
		tamper_regs_t tamper = empty_tamper, mask = empty_tamper; \
		rv = sscanf(buf, "%d %d %d %d", &en, &pol, &filter_en, &filter_cfg); \
		if (rv > 0) mask.fields.__field__ ## _en = 1; \
		if (rv > 1) mask.fields.__field__ ## _pol = 1; \
		if (rv > 2) mask.fields.__field__ ## _filter_en = 1; \
		if (rv > 3) mask.fields.__field__ ## _filter_cfg = 0x7F; \
		tamper.fields.__field__ ## _en = en; \
		tamper.fields.__field__ ## _pol = pol; \
		tamper.fields.__field__ ## _filter_en = filter_en; \
		tamper.fields.__field__ ## _filter_cfg = filter_cfg; \
		save_tamper(tamper, mask); \
		return count; \
	}  \
	\
	struct kobj_attribute __name__ ## _attr = \
		__ATTR(__name__, 0660, __name__ ## _show, __name__ ## _store); \
	\
	static ssize_t __name__ ## _status_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) \
	{ \
		tamper_regs_t tamper; \
		load_tamper(&tamper); \
		return sprintf(buf, "%d\n", \
					   tamper.fields.__field__ ## _status); \
	} \
	\
	static ssize_t __name__ ## _status_store(struct kobject *kobj, struct kobj_attribute *attr, \
	                      const char *buf, size_t count) \
	{ \
		int clear_b = 0; \
		tamper_regs_t tamper = empty_tamper, mask = empty_tamper; \
		mask.fields.__field__ ## _status = 1; \
		sscanf(buf, "%d", &clear_b); \
		tamper.fields.__field__ ## _status = clear_b; \
		save_tamper(tamper, mask); \
		return count; \
	}  \
	\
	struct kobj_attribute __name__ ## _status_attr = \
		__ATTR(__name__ ## _status, 0660, __name__ ## _status_show, __name__ ## _status_store);

WAKE_ATTR_DEF(wake_on_charge, chg_sta_b, 1, 0, 0)
WAKE_ATTR_DEF(wake_on_rtc, xtal_int_b,   1, 0, 0)

/* alter the boot state */
static ssize_t fastboot_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
	gpr_regs_t gpr;
	load_gpr(&gpr);
	return sprintf(buf, "%d %d\n", gpr.fields.fast_boot_en, gpr.fields.boot_part);
}

static ssize_t fastboot_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
	int fast_boot_en, boot_part;
	gpr_regs_t gpr = empty_gpr, gpr_boot_mask = empty_gpr;
	gpr_boot_mask.fields.fast_boot_en = 1;
	gpr_boot_mask.fields.boot_part = 3;
	sscanf(buf, "%d %d", &fast_boot_en, &boot_part);
	gpr.fields.fast_boot_en = fast_boot_en;
	gpr.fields.boot_part = boot_part;
	save_gpr(gpr, gpr_boot_mask);
	return count;
}

struct kobj_attribute fastboot_state_attr =
	__ATTR(fastboot_config, 0660, fastboot_show, fastboot_store);

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = ml_imx6ull_addon_read,
	.write = ml_imx6ull_addon_write,
	.open = ml_imx6ull_addon_open,
	.release = ml_imx6ull_addon_release,
};

static int ml_imx6ull_addon_open(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "Device File Opened...!!!\n");
	return 0;
}

static int ml_imx6ull_addon_release(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "Device File Closed...!!!\n");
	return 0;
}

static ssize_t ml_imx6ull_addon_read(struct file *filp, char __user *buf,
				     size_t len, loff_t *off)
{
	printk(KERN_DEBUG "Read function\n");
	return 0;
}
static ssize_t ml_imx6ull_addon_write(struct file *filp, const char __user *buf,
				      size_t len, loff_t *off)
{
	printk(KERN_DEBUG "Write Function\n");
	return 0;
}

#define REG_ATTR(__name__) \
	if (sysfs_create_file(kobj_ref, &__name__ ## _attr.attr)) { \
		printk(KERN_ERR "Cannot create sysfs file for " #__name__ "......\n"); \
		goto r_sysfs; \
	}
#define UNREG_ATTR(__name__) \
	do { \
		sysfs_remove_file(kernel_kobj, &__name__ ## _attr.attr); \
	} while(0);

static int __init ml_imx6ull_addon_driver_init(void)
{
	/*Allocating Major number*/
	if ((alloc_chrdev_region(&dev, 0, 1, "ml_imx6ull_addon_Dev")) < 0) {
		printk(KERN_INFO "Cannot allocate major number\n");
		return -1;
	}

	/*Creating cdev structure*/
	cdev_init(&ml_imx6ull_addon_cdev, &fops);
	ml_imx6ull_addon_cdev.owner = THIS_MODULE;
	ml_imx6ull_addon_cdev.ops = &fops;

	/*Adding character device to the system*/
	if ((cdev_add(&ml_imx6ull_addon_cdev, dev, 1)) < 0) {
		printk(KERN_ERR "Cannot add the device to the system\n");
		goto r_class;
	}

	/*Creating struct class*/
	if ((dev_class = class_create(THIS_MODULE, "ml_imx6ull_addon_class"))
	    == NULL) {
		printk(KERN_ERR "Cannot create the struct class\n");
		goto r_class;
	}

	/*Creating device*/
	if ((device_create(dev_class, NULL, dev, NULL,
			   "ml_imx6ull_addon_device"))
	    == NULL) {
		printk(KERN_ERR "Cannot create the Device 1\n");
		goto r_device;
	}

	/*Creating a directory in /sys/kernel/ */
	kobj_ref = kobject_create_and_add("muuselabs", kernel_kobj);

	/*Creating sysfs file for ml_imx6ull_addon_value*/
	REG_ATTR(vbus_state);
	REG_ATTR(wake_state);
	REG_ATTR(gpr_state);
	REG_ATTR(fastboot_state);
	REG_ATTR(wake_on_charge);
	REG_ATTR(wake_on_charge_status);
	REG_ATTR(wake_on_rtc);
	REG_ATTR(wake_on_rtc_status);

	printk(KERN_DEBUG "Muuselabs i.MX6ULL add-on driver loaded\n");
	return 0;

r_sysfs:
	kobject_put(kobj_ref);
	UNREG_ATTR(vbus_state);
	UNREG_ATTR(wake_state);
	UNREG_ATTR(gpr_state);
	UNREG_ATTR(fastboot_state);
	UNREG_ATTR(wake_on_charge);
	UNREG_ATTR(wake_on_charge_status);
	UNREG_ATTR(wake_on_rtc);
	UNREG_ATTR(wake_on_rtc_status);

r_device:
	class_destroy(dev_class);
r_class:
	unregister_chrdev_region(dev, 1);
	cdev_del(&ml_imx6ull_addon_cdev);
	return -1;
}

void __exit ml_imx6ull_addon_driver_exit(void)
{
	kobject_put(kobj_ref);
	sysfs_remove_file(kernel_kobj, &vbus_state_attr.attr);
	sysfs_remove_file(kernel_kobj, &wake_state_attr.attr);
	sysfs_remove_file(kernel_kobj, &gpr_state_attr.attr);
	sysfs_remove_file(kernel_kobj, &fastboot_state_attr.attr);
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&ml_imx6ull_addon_cdev);
	unregister_chrdev_region(dev, 1);
	printk(KERN_DEBUG "Muuselabs i.MX6ULL add-on driver unloaded\n");
}

module_init(ml_imx6ull_addon_driver_init);
module_exit(ml_imx6ull_addon_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pieter Palmers <pieter@muuselabs.com>");
MODULE_DESCRIPTION("SysFs driver for IMX VBUS detect");
MODULE_VERSION("1.0");