#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>


#define DEBUG 0


#define MICRO_SEC	1000000
#define NANO_SEC 	(MICRO_SEC*1000)


#define DEFAULT_FREQ		1000
#define DEFAULT_DUTY_CYCLE	50


/**
 * @brief represents a single pwm channel
 */
struct pwm_channel {
	struct hrtimer tm1, tm2;
	ktime_t t1;
	int freq;
	int dc;
	int gpio;

	// linked list of channels
	struct list_head chan_list;
};


/**
 * @brief linked list of all the channels in the system
 */
static LIST_HEAD(_channels);
static struct mutex _lock;


/**
 * @brief callback for timer 1 - responsible for the rising slope
 *
 * @param t timer
 *
 * @return _RESTART 
 */
enum hrtimer_restart cb1(struct hrtimer *t) {
	struct pwm_channel *ch = container_of(t, struct pwm_channel, tm1);
	ktime_t now;
	int ovr;

	now = hrtimer_cb_get_time(t);
	ovr = hrtimer_forward(t, now, ch->t1);
	
#if DEBUG == 1
	printk(KERN_EMERG "up [%d]\n", ovr);
#endif

	gpio_set_value(ch->gpio, 1);

	if (ch->dc < 100) {
		unsigned long t_ns = ((MICRO_SEC * 10 * ch->dc) / (ch->freq));
		ktime_t t2 = ktime_set( 0, t_ns );
		hrtimer_start(&ch->tm2, t2, HRTIMER_MODE_REL);
	}

	return HRTIMER_RESTART;
}


/**
 * @brief callback for timer 2 - responsible for falling slope
 *
 * @param t timer 
 *
 * @return _NORESTART
 */
enum hrtimer_restart cb2(struct hrtimer *t) {
	struct pwm_channel *ch = container_of(t, struct pwm_channel, tm2);
#if DEBUG == 1
	printk(KERN_INFO "down\n");
#endif
	gpio_set_value(ch->gpio, 0);
	return HRTIMER_NORESTART;
}


void init_channel(struct pwm_channel *a_ch) {
	unsigned long t_ns = (NANO_SEC)/a_ch->freq;

	hrtimer_init(&a_ch->tm1, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_init(&a_ch->tm2, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	a_ch->t1 = ktime_set( 0, t_ns );
	a_ch->tm1.function = &cb1;
	a_ch->tm2.function = &cb2;

	gpio_request(a_ch->gpio, "soft_pwm_gpio");
	gpio_direction_output(a_ch->gpio, 1);

	hrtimer_start(&a_ch->tm1, a_ch->t1, HRTIMER_MODE_REL);
}


void deinit_channel(struct pwm_channel *a_ch) {
	if (hrtimer_active(&a_ch->tm1) || hrtimer_is_queued(&a_ch->tm1)) {
		hrtimer_cancel(&a_ch->tm1);
	}

	if (hrtimer_active(&a_ch->tm2) || hrtimer_is_queued(&a_ch->tm2)) {
		hrtimer_cancel(&a_ch->tm2);
	}

	gpio_free(a_ch->gpio);
}


/* ========================================================================== */


static ssize_t export_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len);
static ssize_t unexport_store(struct class *class, struct class_attribute *attr, const char *buf, size_t len);


static struct class_attribute soft_pwm_class_attrs[] = {
	__ATTR(export, 0222, NULL, export_store),
	__ATTR(unexport, 0222, NULL, unexport_store),
	__ATTR_NULL,
};


static struct class soft_pwm_class = {
	.name = "soft_pwm",
	.owner = THIS_MODULE,
	.class_attrs = soft_pwm_class_attrs,
};


static ssize_t duty_cycle_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len);
static ssize_t duty_cycle_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t frequency_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len);
static ssize_t frequency_show(struct device *dev, struct device_attribute *attr, char *buf);

static DEVICE_ATTR(duty_cycle, 0644, duty_cycle_show, duty_cycle_store);
static DEVICE_ATTR(frequency, 0644, frequency_show, frequency_store);


static struct attribute *soft_pwm_dev_attrs[] = {
	&dev_attr_duty_cycle.attr,
	&dev_attr_frequency.attr,
	NULL,
};


static struct attribute_group soft_pwm_dev_attr_group = {
	.attrs = (struct attribute **)soft_pwm_dev_attrs,
};


/* ========================================================================== */

static ssize_t duty_cycle_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len) {
	/* return 0; */
}


static ssize_t duty_cycle_show(struct device *dev,
				   struct device_attribute *attr, char *buf) {

	/* struct nt3h1201_data *chip =  dev_get_drvdata(dev); */
	/* u8 data[NFC_PAGE_SIZE] = {0x00}; */
	/* _read_page(chip->client, NFC_PAGE_CONFIG, data); */
	/* return sprintf(buf, "%d\n", data[NTAG_CONF_BYTE_NC_REG] & (0x01 << NC_REG_PTHRU_ON_OFF)); */
}


static ssize_t frequency_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len) {
	/* return 0; */
}


static ssize_t frequency_show(struct device *dev,
				   struct device_attribute *attr, char *buf) {

	/* struct nt3h1201_data *chip =  dev_get_drvdata(dev); */
	/* u8 data[NFC_PAGE_SIZE] = {0x00}; */
	/* _read_page(chip->client, NFC_PAGE_CONFIG, data); */
	/* return sprintf(buf, "%d\n", data[NTAG_CONF_BYTE_NC_REG] & (0x01 << NC_REG_PTHRU_ON_OFF)); */
}


ssize_t export_store(struct class *class, 
		struct class_attribute *attr, 
		const char *buf, 
		size_t len) {

	struct list_head *p = NULL;
	struct pwm_channel *ch = NULL;
	struct device *d = NULL;

	int found = 0;
	unsigned long gpio = 0;
	int rv = len;

	mutex_lock(&_lock);

	if (kstrtol(buf, 10, &gpio)) {
		printk(KERN_ERR "invalid data [%s]\n", buf);
		rv = -EINVAL;
		goto label_export_cleanup;
	}

	list_for_each (p, &_channels) {
		ch = list_entry(p, struct pwm_channel, chan_list);
		if (ch->gpio == gpio) {
			found = 1;
			break;
		}
	}

	if (found) {
		// channel for the given gpio already exists
		// ignore the request
		printk(KERN_ERR "channel for the gpio already allocated\n");
		goto label_export_cleanup;
	}

	// create the channel
	if (!(ch = kmalloc(sizeof(struct pwm_channel), GFP_KERNEL))) {
		printk(KERN_ERR "Unable to allocate memory for the channel\n");
		rv = -ENOMEM;
		goto label_export_cleanup;
	}

	// initialize the channel 
	ch->gpio = gpio;
	ch->freq = DEFAULT_FREQ;
	ch->dc = DEFAULT_DUTY_CYCLE;
	INIT_LIST_HEAD(&ch->chan_list);

	// create sysfs entries
	if (!(d = 
		device_create(&soft_pwm_class, NULL, MKDEV(0,0), ch, "pwm-%d", (int)gpio))) {
		printk(KERN_ERR "Unable to create the pwm-%d device\n", (int)gpio);
		rv = -ENOMEM;				
		goto label_export_cleanup;
	}

	if (sysfs_create_group(&d->kobj, &soft_pwm_dev_attr_group)) {
		printk(KERN_ERR "Unable to create attributes for channel %d\n", (int)gpio);
		rv = -ENODEV;				
		goto label_export_cleanup;
	}

	// ... and finally - initialize channel's timers
	init_channel(ch);
	list_add(&ch->chan_list, &_channels);
	goto label_export_exit;

label_export_cleanup:
	if (d) device_unregister(d);
	if (ch) kfree(ch);

label_export_exit:
	mutex_unlock(&_lock);
	return rv;
}


static int _match_channel(struct device *dev, const void *data) {
	return dev_get_drvdata(dev) == data;
}


ssize_t unexport_store(struct class *class, 
		struct class_attribute *attr, 
		const char *buf, 
		size_t len) {

	struct list_head *p = NULL;
	struct pwm_channel *ch = NULL;
	struct device *d = NULL;

	int found = 0;
	long gpio = 0;

	if (kstrtol(buf, 10, &gpio)) {
		printk(KERN_ERR "invalid data [%s]\n", buf);
		return -EINVAL;
	}

	list_for_each (p, &_channels) {
		ch = list_entry(p, struct pwm_channel, chan_list);
		if (ch->gpio == gpio) {
			found = 1;
			break;
		}
	}

	if (!found) {
		// channel for the given gpio doesn't exists
		// ignore the request
		return len;
	}

	if ((d = class_find_device(&soft_pwm_class, NULL, ch, _match_channel))) {
		put_device(d);
		device_unregister(d);
	}

	deinit_channel(ch);
	list_del(&ch->chan_list);
	kfree(ch);
	
	return len;
}


/* ========================================================================== */


static int __init pwm_init(void) {
#if DEBUG == 1
	printk(KERN_INFO "installing soft pwm module\n");
#endif

	mutex_init(&_lock);
	return class_register(&soft_pwm_class);
}


static void __exit pwm_exit(void) {
#if DEBUG == 1
	printk(KERN_INFO "deinstalling soft pwm module\n");
#endif

	class_unregister(&soft_pwm_class);
}


module_init(pwm_init);
module_exit(pwm_exit);

MODULE_AUTHOR("tw");
MODULE_DESCRIPTION("HR PWM TESTS");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
