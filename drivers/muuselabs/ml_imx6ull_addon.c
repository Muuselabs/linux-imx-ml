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
		printk(KERN_INFO "Cannot add the device to the system\n");
		goto r_class;
	}

	/*Creating struct class*/
	if ((dev_class = class_create(THIS_MODULE, "ml_imx6ull_addon_class"))
	    == NULL) {
		printk(KERN_INFO "Cannot create the struct class\n");
		goto r_class;
	}

	/*Creating device*/
	if ((device_create(dev_class, NULL, dev, NULL,
			   "ml_imx6ull_addon_device"))
	    == NULL) {
		printk(KERN_INFO "Cannot create the Device 1\n");
		goto r_device;
	}

	/*Creating a directory in /sys/kernel/ */
	kobj_ref = kobject_create_and_add("muuselabs", kernel_kobj);

	/*Creating sysfs file for ml_imx6ull_addon_value*/
	if (sysfs_create_file(kobj_ref, &vbus_state_attr.attr)) {
		printk(KERN_INFO "Cannot create sysfs file......\n");
		goto r_sysfs;
	}
	printk(KERN_DEBUG "Muuselabs i.MX6ULL add-on driver loaded\n");
	return 0;

r_sysfs:
	kobject_put(kobj_ref);
	sysfs_remove_file(kernel_kobj, &vbus_state_attr.attr);

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