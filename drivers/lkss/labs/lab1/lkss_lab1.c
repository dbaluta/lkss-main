// SPDX-License-Identifier: GPL-2.0
/*
 * lkss_lab1.c – LKSS Lab 1: LED blinking + button interrupt handlers
 *
 * Platform driver that demonstrates:
 *   • GPIO descriptor API   (devm_gpiod_get_index, gpiod_set/get_value)
 *   • Kernel timers         (timer_setup, mod_timer, del_timer_sync)
 *   • Edge-triggered IRQs   (gpiod_to_irq, devm_request_threaded_irq)
 *   • Sysfs attributes      (DEVICE_ATTR_RW, ATTRIBUTE_GROUPS, dev_groups)
 *   • Managed resources     (devm_* – automatic cleanup on unbind)
 *
 * Sysfs (under /sys/bus/platform/devices/lkss-lab1/):
 *
 *   led0, led1, led2
 *       Write "1" → start blinking (toggles every BLINK_MS milliseconds)
 *       Write "0" → stop blinking and turn LED off
 *       Read       → "1" if blinking, "0" if off
 *
 * Every button edge fires a threaded IRQ that prints to the kernel log:
 *       dmesg -w
 *
 * Hardware: FRDM-IMX93 EXT2 header (J601)
 *   LED0  – GPIO2_IO04 (green), pin  7  (active-high, 220 Ω series resistor)
 *   BTN1  – GPIO2_IO05, pin 29  (active-low, 10 kΩ external pull-up)
 *   BTN2  – GPIO2_IO06, pin 31  (active-low, 10 kΩ external pull-up)
 *   BTN3  – GPIO2_IO00, pin 27  (active-low, 10 kΩ external pull-up)
 *
 * Device Tree compatible: "lkss,lab1"
 *
 * Cross-compile:
 *   make -C /path/to/linux M=$(pwd) ARCH=arm64 \
 *        CROSS_COMPILE=aarch64-linux-gnu- modules
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/slab.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

#define NUM_LEDS    1
#define NUM_BUTTONS 3
#define BLINK_MS    500   /* LED toggle period in milliseconds */

/* ── Per-LED blink state ─────────────────────────────────────────────────── */

/*
 * Each LED owns a timer_list.  The timer callback toggles the GPIO and
 * re-arms itself with mod_timer() until blinking is set to false.
 *
 * The timer fires in softirq context, so only non-sleeping GPIO operations
 * are safe here.  Memory-mapped GPIO controllers on i.MX93 never sleep,
 * so gpiod_get_value / gpiod_set_value are fine.
 */
struct led_blink {
	struct gpio_desc  *gpiod;
	struct timer_list  timer;
	bool               blinking;
};

/* ── Per-button IRQ context ──────────────────────────────────────────────── */

/*
 * Passed as the 'data' argument to the threaded IRQ handler so it knows
 * which button fired without needing a global lookup.
 */
struct btn_ctx {
	struct device    *dev;
	struct gpio_desc *gpiod;
	int               index;
};

/* ── Driver private data ─────────────────────────────────────────────────── */

struct lkss_lab1 {
	struct led_blink  led[NUM_LEDS];
	struct gpio_desc *btn[NUM_BUTTONS];
	struct btn_ctx    btn_ctx[NUM_BUTTONS];
};

/* ── Timer callback: blink one LED ───────────────────────────────────────── */

static void led_blink_fn(struct timer_list *t)
{
	struct led_blink *lb = from_timer(lb, t, timer);

	gpiod_set_value(lb->gpiod, !gpiod_get_value(lb->gpiod));
	mod_timer(&lb->timer, jiffies + msecs_to_jiffies(BLINK_MS));
}

/* ── Button IRQ handler ──────────────────────────────────────────────────── */

/*
 * Threaded IRQ handler — runs in a kernel thread, not hard-IRQ context.
 * IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING catches both press and release.
 * The gpiod layer inverts the active-low line, so logical 1 == pressed.
 */
static irqreturn_t lkss_btn_irq(int irq, void *data)
{
	struct btn_ctx *ctx = data;
	int val = gpiod_get_value_cansleep(ctx->gpiod);

	dev_info(ctx->dev, "BTN%d %s\n",
		 ctx->index + 1, val ? "pressed" : "released");

	return IRQ_HANDLED;
}

/* ── Sysfs: led{0,1,2} ───────────────────────────────────────────────────── */

/*
 * DEFINE_LED_ATTR(N) generates led<N>_show, led<N>_store, and the
 * DEVICE_ATTR_RW declaration for the sysfs file "led<N>".
 *
 * show  → returns "1\n" if blinking, "0\n" if off
 * store → "1" arms the timer; "0" disarms it and drives the GPIO low
 */
#define DEFINE_LED_ATTR(N)						\
static ssize_t led##N##_show(struct device *dev,			\
			     struct device_attribute *attr, char *buf)	\
{									\
	struct lkss_lab1 *p = dev_get_drvdata(dev);			\
	return sysfs_emit(buf, "%d\n", p->led[N].blinking ? 1 : 0);	\
}									\
static ssize_t led##N##_store(struct device *dev,			\
			      struct device_attribute *attr,		\
			      const char *buf, size_t count)		\
{									\
	struct lkss_lab1 *p = dev_get_drvdata(dev);			\
	int val;							\
	if (kstrtoint(buf, 0, &val))					\
		return -EINVAL;						\
	if (val) {							\
		if (!p->led[N].blinking) {				\
			p->led[N].blinking = true;			\
			mod_timer(&p->led[N].timer,			\
				  jiffies + msecs_to_jiffies(BLINK_MS)); \
		}							\
	} else {							\
		p->led[N].blinking = false;				\
		del_timer_sync(&p->led[N].timer);			\
		gpiod_set_value(p->led[N].gpiod, 0);			\
	}								\
	return count;							\
}									\
static DEVICE_ATTR_RW(led##N)

DEFINE_LED_ATTR(0);

static struct attribute *lkss_lab1_attrs[] = {
	&dev_attr_led0.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lkss_lab1);

/* ── Probe ───────────────────────────────────────────────────────────────── */

static int lkss_lab1_probe(struct platform_device *pdev)
{
	struct device    *dev = &pdev->dev;
	struct lkss_lab1 *priv;
	int i, irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	/* ── LEDs: outputs, initially off, timer initialised ── */
	for (i = 0; i < NUM_LEDS; i++) {
		priv->led[i].gpiod = devm_gpiod_get_index(dev, "led", i,
							   GPIOD_OUT_LOW);
		if (IS_ERR(priv->led[i].gpiod)) {
			dev_err(dev, "failed to get LED %d: %ld\n",
				i, PTR_ERR(priv->led[i].gpiod));
			return PTR_ERR(priv->led[i].gpiod);
		}
		gpiod_set_consumer_name(priv->led[i].gpiod, "lkss-led");
		timer_setup(&priv->led[i].timer, led_blink_fn, 0);
	}

	/* ── Buttons: inputs, edge-triggered threaded IRQs ── */
	for (i = 0; i < NUM_BUTTONS; i++) {
		priv->btn[i] = devm_gpiod_get_index(dev, "button", i,
						     GPIOD_IN);
		if (IS_ERR(priv->btn[i])) {
			dev_err(dev, "failed to get button %d: %ld\n",
				i, PTR_ERR(priv->btn[i]));
			return PTR_ERR(priv->btn[i]);
		}
		gpiod_set_consumer_name(priv->btn[i], "lkss-button");

		priv->btn_ctx[i].dev   = dev;
		priv->btn_ctx[i].gpiod = priv->btn[i];
		priv->btn_ctx[i].index = i;

		irq = gpiod_to_irq(priv->btn[i]);
		if (irq < 0) {
			dev_err(dev, "no IRQ for button %d\n", i);
			return irq;
		}

		ret = devm_request_threaded_irq(dev, irq,
						NULL,
						lkss_btn_irq,
						IRQF_TRIGGER_RISING  |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"lkss-button",
						&priv->btn_ctx[i]);
		if (ret) {
			dev_err(dev, "failed to request IRQ %d (button %d): %d\n",
				irq, i, ret);
			return ret;
		}
	}

	dev_info(dev, "probed: LED0 (blink), BTN1/BTN2/BTN3 (IRQ)\n");
	return 0;
}

/* ── Remove ──────────────────────────────────────────────────────────────── */

/*
 * Stop all running timers before the device-managed resources (GPIO
 * descriptors, IRQs) are released by the devm layer.
 */
static void lkss_lab1_remove(struct platform_device *pdev)
{
	struct lkss_lab1 *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < NUM_LEDS; i++) {
		priv->led[i].blinking = false;
		del_timer_sync(&priv->led[i].timer);
		gpiod_set_value(priv->led[i].gpiod, 0);
	}
}

/* ── Device Tree match table ─────────────────────────────────────────────── */

static const struct of_device_id lkss_lab1_of_match[] = {
	{ .compatible = "lkss,lab1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lkss_lab1_of_match);

/* ── Platform driver ─────────────────────────────────────────────────────── */

static struct platform_driver lkss_lab1_driver = {
	.probe  = lkss_lab1_probe,
	.remove_new = lkss_lab1_remove,
	.driver = {
		.name           = "lkss-lab1",
		.of_match_table = lkss_lab1_of_match,
		.dev_groups     = lkss_lab1_groups,
	},
};
module_platform_driver(lkss_lab1_driver);

MODULE_AUTHOR("LKSS Lab <lkss@nxp.com>");
MODULE_DESCRIPTION("LKSS Lab 1: GPIO LED blinking via sysfs + button IRQ handlers");
MODULE_LICENSE("GPL");
