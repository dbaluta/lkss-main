// SPDX-License-Identifier: GPL-2.0
/*
 * lkss_lab4.c – LKSS Lab 4: PWM control via sysfs
 *
 * Platform driver that demonstrates the Linux PWM consumer API.
 * It requests a single PWM channel (TPM3_CH0 / GPIO_IO04 / EXT2 pin 7
 * on the FRDM-IMX93 board) and exposes three sysfs knobs:
 *
 *   period_ns   – PWM period in nanoseconds (read/write, default from DT)
 *   duty_ns     – active pulse width in nanoseconds (read/write)
 *   enable      – write "1" to start, "0" to stop (read returns current state)
 *
 * Usage example (1 kHz, 25 % duty cycle):
 *   echo 1000000 > /sys/bus/platform/devices/lkss-lab4/period_ns
 *   echo 250000  > /sys/bus/platform/devices/lkss-lab4/duty_ns
 *   echo 1       > /sys/bus/platform/devices/lkss-lab4/enable
 *
 * Hardware: FRDM-IMX93 EXT2 header (J601)
 *   PWM out – GPIO_IO04 / TPM3_CH0, pin 7 (active-high)
 *
 * Device Tree compatible: "lkss,lab4"
 *
 * Cross-compile:
 *   make -C /path/to/linux M=$(pwd) ARCH=arm64 \
 *        CROSS_COMPILE=aarch64-linux-gnu- modules
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/mutex.h>

/* ── Driver private data ─────────────────────────────────────────────────── */

struct lkss_lab4 {
	struct pwm_device *pwm;
	struct pwm_state   state;
	struct mutex       lock;
};

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int apply_state(struct lkss_lab4 *priv)
{
	return pwm_apply_might_sleep(priv->pwm, &priv->state);
}

/* ── Sysfs: period_ns ────────────────────────────────────────────────────── */

static ssize_t period_ns_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct lkss_lab4 *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%llu\n", priv->state.period);
}

static ssize_t period_ns_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct lkss_lab4 *priv = dev_get_drvdata(dev);
	u64 val;
	int ret;

	if (kstrtoull(buf, 0, &val) || val == 0)
		return -EINVAL;

	mutex_lock(&priv->lock);
	priv->state.period = val;
	if (priv->state.duty_cycle > val)
		priv->state.duty_cycle = val;
	ret = apply_state(priv);
	mutex_unlock(&priv->lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(period_ns);

/* ── Sysfs: duty_ns ──────────────────────────────────────────────────────── */

static ssize_t duty_ns_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct lkss_lab4 *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%llu\n", priv->state.duty_cycle);
}

static ssize_t duty_ns_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct lkss_lab4 *priv = dev_get_drvdata(dev);
	u64 val;
	int ret;

	if (kstrtoull(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&priv->lock);
	if (val > priv->state.period) {
		mutex_unlock(&priv->lock);
		return -EINVAL;
	}
	priv->state.duty_cycle = val;
	ret = apply_state(priv);
	mutex_unlock(&priv->lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(duty_ns);

/* ── Sysfs: enable ───────────────────────────────────────────────────────── */

static ssize_t enable_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct lkss_lab4 *priv = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", priv->state.enabled ? 1 : 0);
}

static ssize_t enable_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct lkss_lab4 *priv = dev_get_drvdata(dev);
	int val, ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&priv->lock);
	priv->state.enabled = !!val;
	ret = apply_state(priv);
	mutex_unlock(&priv->lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(enable);

static struct attribute *lkss_lab4_attrs[] = {
	&dev_attr_period_ns.attr,
	&dev_attr_duty_ns.attr,
	&dev_attr_enable.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lkss_lab4);

/* ── Probe ───────────────────────────────────────────────────────────────── */

static int lkss_lab4_probe(struct platform_device *pdev)
{
	struct device    *dev = &pdev->dev;
	struct lkss_lab4 *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	priv->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(priv->pwm))
		return dev_err_probe(dev, PTR_ERR(priv->pwm),
				     "failed to get PWM\n");

	/*
	 * pwm_init_state() reads period and polarity from the DT "pwms"
	 * property (1 000 000 ns = 1 kHz, normal polarity) so we don't
	 * hard-code them here.  Start with 50 % duty cycle, output off.
	 */
	pwm_init_state(priv->pwm, &priv->state);
	priv->state.duty_cycle = priv->state.period / 2;
	priv->state.enabled    = false;

	dev_info(dev, "probed: TPM3_CH0 (GPIO_IO04 / EXT2 pin 7), period=%lluns\n",
		 priv->state.period);
	return 0;
}

/* ── Remove ──────────────────────────────────────────────────────────────── */

static void lkss_lab4_remove(struct platform_device *pdev)
{
	struct lkss_lab4 *priv = platform_get_drvdata(pdev);

	mutex_lock(&priv->lock);
	priv->state.enabled = false;
	pwm_apply_might_sleep(priv->pwm, &priv->state);
	mutex_unlock(&priv->lock);
}

/* ── Device Tree match table ─────────────────────────────────────────────── */

static const struct of_device_id lkss_lab4_of_match[] = {
	{ .compatible = "lkss,lab4" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lkss_lab4_of_match);

/* ── Platform driver ─────────────────────────────────────────────────────── */

static struct platform_driver lkss_lab4_driver = {
	.probe      = lkss_lab4_probe,
	.remove_new = lkss_lab4_remove,
	.driver = {
		.name           = "lkss-lab4",
		.of_match_table = lkss_lab4_of_match,
		.dev_groups     = lkss_lab4_groups,
	},
};
module_platform_driver(lkss_lab4_driver);

MODULE_AUTHOR("LKSS Lab <lkss@nxp.com>");
MODULE_DESCRIPTION("LKSS Lab 4: PWM control via sysfs (TPM3_CH0 / GPIO_IO04)");
MODULE_LICENSE("GPL");
