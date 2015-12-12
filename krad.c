/*
 * Basic Linux Kernel module using GPIO interrupts.
 *
 * Author:
 * 	Stefan Wendler (devnull@kaltpost.de)
 *      Brendan Whitfield (bcw7044@rit.edu)
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h> 
#include <linux/time.h>
#include <linux/hw_random.h>
#include <linux/mutex.h>


//the number of pulses to record
#define PULSE_BUFFER_SIZE 50


/* Define a GPIO for the Geiger counter */
static int geiger_pulse_pin = 17; // listens for incoming pulses from the gieger counter

/* Later on, the assigned IRQ numbers for the buttons are stored here */
static int geiger_irq = -1;

/* circular buffer of random pulse times */
DEFINE_MUTEX(pulses_lock);
static struct timespec pulses[PULSE_BUFFER_SIZE];
static int pulses_head = 0;
static int pulses_tail = 0;


/*
	buffer functions
	NOTE: acquire the pulses_lock before using these functions
	NOTE: it is up to you to check that you don't overfill the buffer
*/
static int pulses_size(void)
{
	if(pulses_head == pulses_tail)
		return 0;
	else if(pulses_head < pulses_tail)
		return pulses_tail - pulses_head + 1;
	else //if(pulses_head > pulses_tail)
		return (PULSE_BUFFER_SIZE - pulses_head) + pulses_tail + 1;
}

//pops an element from the head (front) of the buffer
static struct timespec pulses_pop(void)
{
	int head = pulses_head;
	++pulses_head;
	pulses_head = pulses_head % PULSE_BUFFER_SIZE;
	return pulses[head];
}

//pushes an element onto the tail (back) of the buffer
static void pulses_push(struct timespec t)
{
	++pulses_tail;
	pulses_tail = pulses_tail % PULSE_BUFFER_SIZE;
	pulses[pulses_tail] = t;
}

/*
	end buffer functions
*/


static int geiger_data_present(struct hwrng* rng, int wait)
{
	int bytes = 0;
	mutex_lock(&pulses_lock);
	bytes = pulses_size() * sizeof(struct timespec);
	mutex_unlock(&pulses_lock);
	return bytes;
}

static int geiger_data_read(struct hwrng* rng, u32 *data)
{
	int bytes = 0;
	mutex_lock(&pulses_lock);
	if(pulses_size() > 0)
	{
		*data = (u32) pulses_pop().tv_nsec;
		bytes = 4;
	}
	mutex_unlock(&pulses_lock);
	return bytes;
}

/*
static int geiger_read(struct hwrng* rng, void* data, size_t max, bool wait)
{
	return 0;
}
*/

static struct hwrng geiger_rng = {
	"Geiger Counter",
	NULL,
	NULL,
	geiger_data_present,
	geiger_data_read,
	//geiger_read,
	NULL,
	0,
	1
};


/*
 * The interrupt service routine called on geiger pulses
 */
static irqreturn_t geiger_isr(int irq, void *data)
{
	if(irq == geiger_irq)
	{
		mutex_lock(&pulses_lock);
		if(pulses_size() < PULSE_BUFFER_SIZE)
			pulses_push(CURRENT_TIME);
		mutex_unlock(&pulses_lock);
	}

	return IRQ_HANDLED;
}

/*
 * Module init function
 */
static int __init krad_init(void)
{
	int ret = 0;

	printk(KERN_INFO "%s\n", __func__);

	// register Geiger pulse gpio
	ret = gpio_request_one(geiger_pulse_pin, GPIOF_IN, "Geiger Pulse");

	if (ret) {
		printk(KERN_ERR "Unable to request GPIO for the Geiger Counter: %d\n", ret);
		goto fail1;
	}

	//printk(KERN_INFO "Current button1 value: %d\n", gpio_get_value(buttons[0].gpio));
	
	ret = gpio_to_irq(geiger_pulse_pin);

	if(ret < 0) {
		printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
		goto fail2;
	}

	geiger_irq = ret;

	printk(KERN_INFO "Successfully requested Geiger Pulse IRQ # %d\n", geiger_irq);

	ret = request_irq(geiger_irq, geiger_isr, IRQF_TRIGGER_RISING, "krad#geiger", NULL);

	if(ret) {
		printk(KERN_ERR "Unable to request IRQ: %d\n", ret);
		goto fail2;
	}

	ret = hwrng_register(&geiger_rng);

	if(ret) {
		printk(KERN_ERR "Unable to register hardware RNG device: %d\n", ret);
		goto fail3;
	}

	printk(KERN_INFO "Successfully registered new hardware RNG device\n");

	// finished successfully
	return 0;


	// failure cases
fail3:
	free_irq(geiger_irq, NULL);
fail2: 
	gpio_free(geiger_pulse_pin);
fail1:
	return ret;
}

/**
 * Module exit function
 */
static void __exit krad_exit(void)
{
	printk(KERN_INFO "%s\n", __func__);

	// unregister the hwrng
	hwrng_unregister(&geiger_rng);

	// free irqs
	free_irq(geiger_irq, NULL);
	
	// unregister
	gpio_free(geiger_pulse_pin);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brendan Whitfield");
MODULE_DESCRIPTION("Module for using a geiger counter as a hardware RNG");

module_init(krad_init);
module_exit(krad_exit);


