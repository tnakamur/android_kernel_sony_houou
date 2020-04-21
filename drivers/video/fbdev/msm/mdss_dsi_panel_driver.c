/* drivers/video/fbdev/msm/mdss_dsi_panel_driver.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
/*
 * Copyright (C) 2016 Sony Mobile Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/leds-qpnp-wled.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/pm_qos.h>
#include <linux/mdss_io_util.h>

#include "mdss.h"
#include "mdss_panel.h"
#include "mdss_dsi.h"
#include "mdss_debug.h"
#include "mdss_dsi_panel_driver.h"
#include "mdss_dsi_panel_debugfs.h"

static bool gpio_req;
static u32 down_period;

static struct fps_data fpsd, vpsd;

#define ADC_PNUM		2
#define ADC_RNG_MIN		0
#define ADC_RNG_MAX		1

static unsigned long lcdid_adc = 1505000;
static void vsync_handler(struct mdss_mdp_ctl *ctl, ktime_t t);

static struct mdss_mdp_vsync_handler vs_handle;
static bool display_onoff_state;

static int __init lcdid_adc_setup(char *str)
{
	unsigned long res;

	if (!*str)
		return 0;
	if (!kstrtoul(str, 0, &res))
		lcdid_adc = res;

	return 1;
}
__setup("lcdid_adc=", lcdid_adc_setup);

void mdss_dsi_panel_driver_detection(
		struct platform_device *pdev,
		struct device_node **np)
{
	u32 res[ADC_PNUM];
	int rc = 0;
	struct device_node *parent;
	struct device_node *next;
	u32 dev_index = 0;
	u32 dsi_index = 0;
	u32 adc_uv = 0;

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index", &dev_index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
						__func__, rc);
		return;
	}

	parent = of_get_parent(*np);

	adc_uv = lcdid_adc;
	pr_notice("%s: physical:%d\n", __func__, adc_uv);

	for_each_child_of_node(parent, next) {
		rc = of_property_read_u32(next, "somc,dsi-index", &dsi_index);
		if (rc)
			dsi_index = 0;
		if (dsi_index != dev_index)
			continue;

		rc = of_property_read_u32_array(next,
				"somc,lcd-id-adc", res, ADC_PNUM);
		if (rc)
			continue;
		if (adc_uv < res[ADC_RNG_MIN] || res[ADC_RNG_MAX] < adc_uv)
			continue;

		*np = next;
		break;
	}
}

static int mdss_dsi_panel_driver_vreg_name_to_config(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		struct dss_vreg *config, char *name)
{
	struct dss_vreg *vreg_config = ctrl_pdata->panel_power_data.vreg_config;
	int num_vreg = ctrl_pdata->panel_power_data.num_vreg;
	int i = 0;
	int valid = -EINVAL;

	for (i = 0; i < num_vreg; i++) {
		if (!strcmp(name, vreg_config[i].vreg_name)) {
			*config = vreg_config[i];
			valid = 0;
			break;
		}
	}

	return valid;
}

static int mdss_dsi_panel_driver_vreg_ctrl(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata, char *vreg, bool enable)
{
	struct dss_vreg vreg_config;
	struct mdss_panel_power_seq *pw_seq = NULL;
	int valid = 0;
	int wait = 0;
	int ret = 0;

	valid = mdss_dsi_panel_driver_vreg_name_to_config(
			ctrl_pdata, &vreg_config, vreg);

	if (!valid) {
		if (enable) {
			ret = msm_dss_enable_vreg(&vreg_config, 1, 1);
			pw_seq = &ctrl_pdata->spec_pdata->on_seq;
		} else {
			ret = msm_dss_enable_vreg(&vreg_config, 1, 0);
			pw_seq = &ctrl_pdata->spec_pdata->off_seq;
		}

		if (!strcmp(vreg, "vdd"))
			wait = pw_seq->disp_vdd;
		else if (!strcmp(vreg, "vddio"))
			wait = pw_seq->disp_vddio;
		else if (!strcmp(vreg, "lab"))
			wait = pw_seq->disp_vsp;
		else if (!strcmp(vreg, "ibb"))
			wait = pw_seq->disp_vsn;
		else if (!strcmp(vreg, "touch-avdd"))
			wait = pw_seq->touch_avdd;
		else
			wait = 0;

		if (!ret && wait)
			usleep_range(wait * 1000, wait * 1000 + 100);
	}

	return ret;
}

bool mdss_dsi_panel_driver_is_power_on(unsigned char state)
{
	bool ret = false;

	if (state & INCELL_POWER_STATE_ON)
		ret = true;

	pr_debug("%s: In-Cell %s state\n", __func__, (ret ? "on" : "off"));

	return ret;
}

bool mdss_dsi_panel_driver_is_power_lock(unsigned char state)
{
	bool ret = false;

	if (state & INCELL_LOCK_STATE_ON)
		ret = true;

	pr_debug("%s: In-Cell I/F %s state\n", __func__,
		(ret ? "Lock" : "Unlock"));

	return ret;
}

bool mdss_dsi_panel_driver_is_ewu(unsigned char state)
{
	bool ret = false;

	if (state & INCELL_EWU_STATE_ON)
		ret = true;

	pr_debug("%s: In-Cell I/F %s state\n", __func__,
		(ret ? "EWU" : "NORMAL"));

	return ret;
}

bool mdss_dsi_panel_driver_is_system_on(unsigned char state)
{
	bool ret = false;

	if (state & INCELL_SYSTEM_STATE_ON)
		ret = true;

	pr_debug("%s: In-Cell system %s state\n", __func__,
		(ret ? "resume" : "suspend"));

	return ret;
}

static bool mdss_dsi_panel_driver_is_seq_for_ewu(void)
{
	struct incell_ctrl *incell = incell_get_info();

	if (incell &&
		(incell->seq == POWER_ON_EWU_SEQ))
		return true;

	return false;
}

static bool mdss_dsi_panel_driver_is_incell_operation(void)
{
	struct incell_ctrl *incell = incell_get_info();

	if (incell &&
		(incell->incell_intf_operation == INCELL_TOUCH_RUN))
		return true;

	return false;
}

static int mdss_dsi_panel_calculation_sleep(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		int gpio, bool enable)
{
	struct mdss_panel_specific_pdata *spec_pdata = ctrl_pdata->spec_pdata;
	struct mdss_panel_power_seq *pw_seq = NULL;
	int wait = 0;

	if (mdss_dsi_panel_driver_is_seq_for_ewu() &&
		(gpio == spec_pdata->touch_reset_gpio) &&
		!enable) {
		if (&spec_pdata->ewu_seq)
			pw_seq = &spec_pdata->ewu_seq;
		else
			pw_seq = &spec_pdata->on_seq;
	} else {
		if (enable)
			pw_seq = &spec_pdata->on_seq;
		else
			pw_seq = &spec_pdata->off_seq;
	}

	if (gpio == spec_pdata->disp_dcdc_en_gpio)
		wait = pw_seq->disp_dcdc;
	else if (gpio == spec_pdata->touch_vddio_gpio)
		wait = pw_seq->touch_vddio;
	else if (gpio == spec_pdata->touch_reset_gpio)
		wait = pw_seq->touch_reset;
	else if (gpio == spec_pdata->touch_int_gpio)
		wait = pw_seq->touch_intn;
	else
		wait = 0;

	wait = wait * 1000;
	return wait;
}

static void mdss_dsi_panel_driver_gpio_output(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		int gpio, bool enable, int value)
{
	int wait;

	wait = mdss_dsi_panel_calculation_sleep(ctrl_pdata, gpio, enable);

	if (gpio_is_valid(gpio))
		gpio_direction_output(gpio, value);

	if (wait)
		usleep_range(wait, wait + 100);
}

static void mdss_dsi_panel_driver_set_gpio(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		int gpio, bool enable, int value)
{
	int wait = 0;

	wait = mdss_dsi_panel_calculation_sleep(ctrl_pdata, gpio, enable);

	if (gpio_is_valid(gpio))
		gpio_set_value(gpio, value);

	if (wait)
		usleep_range(wait, wait + 100);
}

int mdss_dsi_panel_driver_pinctrl_init(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	ctrl_pdata->pin_res.touch_state_active
		= pinctrl_lookup_state(ctrl_pdata->pin_res.pinctrl,
				MDSS_PINCTRL_STATE_TOUCH_ACTIVE);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.touch_state_active))
		pr_warn("%s: can not get touch active pinstate\n", __func__);

	ctrl_pdata->pin_res.touch_state_suspend
		= pinctrl_lookup_state(ctrl_pdata->pin_res.pinctrl,
				MDSS_PINCTRL_STATE_TOUCH_SUSPEND);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.touch_state_suspend))
		pr_warn("%s: can not get touch suspend pinstate\n", __func__);

	return 0;
}

int mdss_dsi_panel_driver_touch_pinctrl_set_state(
	struct mdss_dsi_ctrl_pdata *ctrl_pdata,
	bool active)
{
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	struct pinctrl_state *pin_state;
	int rc = -EFAULT;
	int wait = 0;

	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.pinctrl))
		return PTR_ERR(ctrl_pdata->pin_res.pinctrl);

	spec_pdata = ctrl_pdata->spec_pdata;

	pin_state = active ? ctrl_pdata->pin_res.touch_state_active
				: ctrl_pdata->pin_res.touch_state_suspend;

	if (!IS_ERR_OR_NULL(pin_state)) {
		rc = pinctrl_select_state(ctrl_pdata->pin_res.pinctrl,
				pin_state);
		if (!rc) {
			wait = mdss_dsi_panel_calculation_sleep(ctrl_pdata,
					spec_pdata->touch_int_gpio, active);
			if (wait)
				usleep_range(wait, wait + 100);
		} else {
			pr_err("%s: can not set %s pins\n", __func__,
			       active ? MDSS_PINCTRL_STATE_TOUCH_ACTIVE
			       : MDSS_PINCTRL_STATE_TOUCH_SUSPEND);
		}
	} else {
		pr_err("%s: invalid '%s' pinstate\n", __func__,
		       active ? MDSS_PINCTRL_STATE_TOUCH_ACTIVE
		       : MDSS_PINCTRL_STATE_TOUCH_SUSPEND);
	}

	return rc;
}

void mdss_dsi_panel_driver_state_change_off(struct incell_ctrl *incell)
{
	incell_state_change change_state = incell->change_state;
	unsigned char *state = &incell->state;

	pr_debug("%s: status:0x%x --->\n", __func__, (*state));

	if (change_state != INCELL_STATE_NONE)
		mdss_dsi_panel_driver_update_incell_bk(incell);

	switch (change_state) {
	case INCELL_STATE_NONE:
		pr_notice("%s: Not change off status\n", __func__);
		break;
	case INCELL_STATE_S_OFF:
		*state &= INCELL_SYSTEM_STATE_OFF;
		break;
	case INCELL_STATE_P_OFF:
		*state &= INCELL_POWER_STATE_OFF;
		break;
	case INCELL_STATE_SP_OFF:
		*state &= INCELL_POWER_STATE_OFF;
		*state &= INCELL_SYSTEM_STATE_OFF;
		break;
	default:
		pr_err("%s: offmode unknown\n", __func__);
		break;
	}

	pr_debug("%s: ---> status:0x%x\n", __func__, (*state));
}

void mdss_dsi_panel_driver_power_off_ctrl(struct incell_ctrl *incell)
{
	incell_pw_seq seq = POWER_OFF_EXECUTE;
	incell_state_change change_state = INCELL_STATE_NONE;
	unsigned char state = incell->state;
	incell_intf_mode intf_mode = incell->intf_mode;
	incell_worker_state worker_state = incell->worker_state;
	bool incell_intf_operation = incell->incell_intf_operation;

	if (worker_state == INCELL_WORKER_ON) {
		change_state = INCELL_STATE_P_OFF;
	} else if (incell_intf_operation == INCELL_TOUCH_RUN) {
		/* touch I/F running mode */
		if (intf_mode == INCELL_DISPLAY_HW_RESET) {
			if (!mdss_dsi_panel_driver_is_power_on(state)) {
				pr_err("%s: Already power off. state:0x%x\n",
						__func__, state);
				seq = POWER_OFF_SKIP;
			} else {
				change_state = INCELL_STATE_P_OFF;
			}
		} else {
			if (!mdss_dsi_panel_driver_is_power_on(state)) {
				pr_err("%s: Power off status. state:0x%x\n",
						__func__, state);
				seq = POWER_OFF_SKIP;
			} else if (mdss_dsi_panel_driver_is_ewu(state)) {
				pr_debug("%s: Skip power off for EWU seq\n",
						__func__);
				seq = POWER_OFF_SKIP;
			} else {
				change_state = INCELL_STATE_P_OFF;
			}
		}
	} else {
		if (worker_state == INCELL_WORKER_PENDING)
			incell_panel_power_worker_canceling(incell);

		/* touch I/F idling mode */
		if (mdss_dsi_panel_driver_is_power_lock(state)) {
			change_state = INCELL_STATE_S_OFF;
			seq = POWER_OFF_SKIP;
		} else if (!mdss_dsi_panel_driver_is_power_on(state)) {
			change_state = INCELL_STATE_S_OFF;
			seq = POWER_OFF_SKIP;
		} else if (mdss_dsi_panel_driver_is_ewu(state)) {
			change_state = INCELL_STATE_S_OFF;
			seq = POWER_OFF_SKIP;
		} else {
			change_state = INCELL_STATE_SP_OFF;
		}
	}

	pr_debug("%s: incell change state seq:%d change_state:%d\n",
				__func__, (int)seq, (int)change_state);
	incell->seq = seq;
	incell->change_state = change_state;
}

int mdss_dsi_panel_driver_power_off(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	struct mdss_panel_power_seq *pw_seq = NULL;
	struct incell_ctrl *incell = incell_get_info();
	int ret = 0;
	int rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		ret = -EINVAL;
		goto end;
	}

	if (incell)
		if (incell->seq == POWER_OFF_SKIP)
			return ret;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	spec_pdata = ctrl_pdata->spec_pdata;

	pw_seq = &spec_pdata->off_seq;

	if (spec_pdata->off_seq.rst_b_seq) {
		rc = mdss_dsi_panel_reset(pdata, 0);
		if (rc)
			pr_warn("%s: Panel reset failed. rc=%d\n",
					__func__, rc);
	}

	ret += mdss_dsi_panel_driver_reset_touch(pdata, 0);
	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "ibb", false);
	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "lab", false);

	mdss_dsi_panel_driver_set_gpio(ctrl_pdata,
		(spec_pdata->disp_dcdc_en_gpio), false, 0);

	if (!spec_pdata->off_seq.rst_b_seq) {
		rc = mdss_dsi_panel_reset(pdata, 0);
		if (rc)
			pr_warn("%s: Panel reset failed. rc=%d\n",
					__func__, rc);
	}

	mdss_dsi_panel_driver_touch_pinctrl_set_state(ctrl_pdata, false);

	mdss_dsi_panel_driver_set_gpio(ctrl_pdata,
		(spec_pdata->touch_vddio_gpio), false, 1);

	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "vddio", false);

	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "touch-avdd", false);

	if (ret)
		pr_err("%s: failed to disable vregs for %s\n",
				__func__, __mdss_dsi_pm_name(DSI_PANEL_PM));
	else
		pr_notice("@@@@ panel power off @@@@\n");

	if (mdss_dsi_pinctrl_set_state(ctrl_pdata, false))
		pr_debug("reset disable: pinctrl not enabled\n");

	if (spec_pdata->down_period)
		down_period = (u32)ktime_to_ms(ktime_get());

end:
	return ret;
}

void mdss_dsi_panel_driver_state_change_on(struct incell_ctrl *incell)
{
	incell_state_change change_state = incell->change_state;
	unsigned char *state = &incell->state;

	pr_debug("%s: status:0x%x --->\n", __func__, (*state));

	if (change_state != INCELL_STATE_NONE)
		mdss_dsi_panel_driver_update_incell_bk(incell);

	switch (change_state) {
	case INCELL_STATE_NONE:
		pr_notice("%s: Not change on status\n", __func__);
		break;
	case INCELL_STATE_S_ON:
		*state |= INCELL_SYSTEM_STATE_ON;
		break;
	case INCELL_STATE_P_ON:
		*state |= INCELL_POWER_STATE_ON;
		break;
	case INCELL_STATE_SP_ON:
		*state |= INCELL_SYSTEM_STATE_ON;
		*state |= INCELL_POWER_STATE_ON;
		break;
	default:
		pr_err("%s: onmode unknown\n", __func__);
		break;
	}

	pr_debug("%s: ---> status:0x%x\n", __func__, (*state));
}

void mdss_dsi_panel_driver_power_on_ctrl(struct incell_ctrl *incell)
{
	incell_pw_seq seq = POWER_ON_EXECUTE;
	incell_state_change change_state = INCELL_STATE_NONE;
	unsigned char state = incell->state;
	incell_intf_mode intf_mode = incell->intf_mode;
	incell_worker_state worker_state = incell->worker_state;
	bool incell_intf_operation = incell->incell_intf_operation;

	if (worker_state == INCELL_WORKER_ON) {
		change_state = INCELL_STATE_P_ON;
	} else if (incell_intf_operation == INCELL_TOUCH_RUN) {
		/* touch I/F running mode */
		if (intf_mode != INCELL_DISPLAY_HW_RESET) {
			pr_err("%s: Unknown I/F: %d\n",
					__func__, (int)intf_mode);
			seq = POWER_ON_SKIP;
		} else if (mdss_dsi_panel_driver_is_power_on(state)) {
			pr_err("%s: Already power on status. state:0x%x\n",
					__func__, state);
			seq = POWER_ON_SKIP;
		} else {
			change_state = INCELL_STATE_P_ON;
		}
	} else {
		/* touch I/F idling mode */
		if (worker_state == INCELL_WORKER_PENDING) {
			incell_panel_power_worker_canceling(incell);
			change_state = INCELL_STATE_S_ON;
			seq = POWER_ON_EWU_SEQ;
		} else if (mdss_dsi_panel_driver_is_power_lock(state)) {
			if (mdss_dsi_panel_driver_is_power_on(state)) {
				change_state = INCELL_STATE_S_ON;
				seq = POWER_ON_SKIP;
			} else {
				change_state = INCELL_STATE_SP_ON;
				seq = POWER_ON_EXECUTE;
			}
		} else if (mdss_dsi_panel_driver_is_ewu(state)) {
			if (mdss_dsi_panel_driver_is_power_on(state)) {
				change_state = INCELL_STATE_S_ON;
				seq = POWER_ON_EWU_SEQ;
			} else {
				change_state = INCELL_STATE_SP_ON;
				seq = POWER_ON_EXECUTE;
			}
		} else if (mdss_dsi_panel_driver_is_power_on(state)) {
			change_state = INCELL_STATE_S_ON;
			seq = POWER_ON_EWU_SEQ;
		} else {
			change_state = INCELL_STATE_SP_ON;
			seq = POWER_ON_EXECUTE;
		}
	}

	pr_debug("%s: incell change state seq:%d change_state:%d\n",
				__func__, (int)seq, (int)change_state);
	incell->seq = seq;
	incell->change_state = change_state;
}

static int mdss_dsi_panel_driver_ewu_seq(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	ctrl_pdata = container_of(pdata,
				struct mdss_dsi_ctrl_pdata, panel_data);
	if (!ctrl_pdata) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ret += mdss_dsi_panel_driver_reset_touch(pdata, 0);
	ret += mdss_dsi_panel_driver_reset_dual_display(ctrl_pdata);

	return ret;
}

int mdss_dsi_panel_driver_power_on(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	struct mdss_panel_power_seq *pw_seq = NULL;
	struct incell_ctrl *incell = incell_get_info();
	unsigned char state;
	int ret = 0;
	int wait;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	if (incell) {
		mdss_dsi_panel_driver_power_on_ctrl(incell);
		if (incell->seq != POWER_ON_EXECUTE) {
			if (incell->seq == POWER_ON_EWU_SEQ)
				ret = mdss_dsi_panel_driver_ewu_seq(pdata);
			return ret;
		}
		state = incell->state;
	}

	if (pdata->panel_info.pdest != DISPLAY_1)
		return 0;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	spec_pdata = ctrl_pdata->spec_pdata;

	pw_seq = &spec_pdata->on_seq;

	if (!gpio_req) {
		ret = mdss_dsi_request_gpios(ctrl_pdata);
		if (ret) {
			pr_err("gpio request failed\n");
			return ret;
		}
		gpio_req = true;
	}

	if (spec_pdata->down_period) {
		u32 kt = (u32)ktime_to_ms(ktime_get());

		kt = (kt < down_period) ? kt + ~down_period : kt - down_period;
		if (kt < spec_pdata->down_period)
			usleep_range((spec_pdata->down_period - kt) *
				1000,
				(spec_pdata->down_period - kt) *
				1000 + 100);
	}

	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "touch-avdd", true);
	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "vddio", true);

	mdss_dsi_panel_driver_gpio_output(ctrl_pdata,
		(spec_pdata->touch_vddio_gpio), true, 0);

	mdss_dsi_panel_driver_touch_pinctrl_set_state(ctrl_pdata, true);

	mdss_dsi_panel_driver_gpio_output(ctrl_pdata,
		(spec_pdata->disp_dcdc_en_gpio), true, 1);

	if (!spec_pdata->rst_after_pon) {
		if (!mdss_dsi_panel_driver_is_power_on(state)) {
			ret += mdss_dsi_panel_driver_reset_touch(pdata, 1);
			wait = pw_seq->touch_reset_first;
			usleep_range(wait * 1000, wait * 1000 + 100);
		}

		if (!pdata->panel_info.cont_splash_enabled &&
			!pdata->panel_info.mipi.lp11_init) {
			if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
				pr_debug("reset enable: pinctrl not enabled\n");
			ret = mdss_dsi_panel_reset(pdata, 1);
			if (ret)
				pr_err("%s: Panel reset failed. rc=%d\n",
						__func__, ret);
		}
	}

	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "lab", true);
	ret += mdss_dsi_panel_driver_vreg_ctrl(ctrl_pdata, "ibb", true);

	if (ret) {
		pr_err("%s: failed to enable vregs for %s\n",
			__func__, __mdss_dsi_pm_name(DSI_PANEL_PM));
		return ret;
	}

	pr_notice("@@@@ panel power on @@@@\n");

	if (spec_pdata->rst_after_pon) {
		if (!mdss_dsi_panel_driver_is_power_on(state)) {
			ret += mdss_dsi_panel_driver_reset_touch(pdata, 1);
			wait = pw_seq->touch_reset_first;
			usleep_range(wait * 1000, wait * 1000 + 100);
		}

		if (!pdata->panel_info.cont_splash_enabled &&
			!pdata->panel_info.mipi.lp11_init) {
			if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
				pr_debug("reset enable: pinctrl not enabled\n");
			ret = mdss_dsi_panel_reset(pdata, 1);
			if (ret)
				pr_err("%s: Panel reset failed. rc=%d\n",
						__func__, ret);
		}
	}

	return ret;
}

int mdss_dsi_panel_driver_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	int rc = 0;

	if (ctrl_pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	spec_pdata = ctrl_pdata->spec_pdata;

	if (gpio_is_valid(spec_pdata->disp_vddio_gpio)) {
		rc = gpio_request(spec_pdata->disp_vddio_gpio,
						"disp_vddio");
		if (rc) {
			pr_err("request disp vddio gpio failed, rc=%d\n",
				       rc);
			goto disp_vddio_gpio_err;
		}
	}

	if (gpio_is_valid(spec_pdata->disp_dcdc_en_gpio)) {
		rc = gpio_request(spec_pdata->disp_dcdc_en_gpio,
						"disp_dcdc_en_gpio");
		if (rc) {
			pr_err("request disp_dcdc_en gpio failed, rc=%d\n", rc);
			goto disp_dcdc_en_gpio_err;
		}
	}

	if (gpio_is_valid(spec_pdata->touch_vddio_gpio)) {
		rc = gpio_request(spec_pdata->touch_vddio_gpio,
						"touch_vddio");
		if (rc) {
			pr_err("request touch vddio gpio failed, rc=%d\n",
				       rc);
			goto touch_vddio_gpio_err;
		}
	}

	if (gpio_is_valid(spec_pdata->touch_reset_gpio)) {
		rc = gpio_request(spec_pdata->touch_reset_gpio,
						"touch_reset");
		if (rc) {
			pr_err("request touch reset gpio failed,rc=%d\n",
								rc);
			goto touch_reset_gpio_err;
		}
	}

	if (gpio_is_valid(spec_pdata->touch_int_gpio)) {
		rc = gpio_request(spec_pdata->touch_int_gpio,
						"touch_int");
		if (rc) {
			pr_err("request touch int gpio failed,rc=%d\n",
								rc);
			goto touch_int_gpio_err;
		}
	}

	return rc;

touch_int_gpio_err:
	if (gpio_is_valid(spec_pdata->touch_reset_gpio))
		gpio_free(spec_pdata->touch_reset_gpio);
touch_reset_gpio_err:
	if (gpio_is_valid(spec_pdata->touch_vddio_gpio))
		gpio_free(spec_pdata->touch_vddio_gpio);
touch_vddio_gpio_err:
	if (gpio_is_valid(spec_pdata->disp_dcdc_en_gpio))
		gpio_free(spec_pdata->disp_dcdc_en_gpio);
disp_dcdc_en_gpio_err:
	if (gpio_is_valid(spec_pdata->disp_vddio_gpio))
		gpio_free(spec_pdata->disp_vddio_gpio);
disp_vddio_gpio_err:
	return rc;
}

void mdss_dsi_panel_driver_gpio_free(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_panel_specific_pdata *spec_pdata = NULL;

	if (ctrl_pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	spec_pdata = ctrl_pdata->spec_pdata;

	if (gpio_is_valid(spec_pdata->touch_int_gpio))
		gpio_free(spec_pdata->touch_int_gpio);

	if (gpio_is_valid(spec_pdata->touch_reset_gpio))
		gpio_free(spec_pdata->touch_reset_gpio);

	if (gpio_is_valid(spec_pdata->touch_vddio_gpio))
		gpio_free(spec_pdata->touch_vddio_gpio);

	if (gpio_is_valid(spec_pdata->disp_dcdc_en_gpio))
		gpio_free(spec_pdata->disp_dcdc_en_gpio);

	if (gpio_is_valid(spec_pdata->disp_vddio_gpio))
		gpio_free(spec_pdata->disp_vddio_gpio);
}

void mdss_dsi_panel_driver_parse_gpio_params(struct platform_device *ctrl_pdev,
		struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_panel_specific_pdata *spec_pdata = NULL;

	if (ctrl_pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}

	spec_pdata = ctrl_pdata->spec_pdata;

	spec_pdata->disp_vddio_gpio = of_get_named_gpio(
			ctrl_pdev->dev.of_node,
			"qcom,platform-vddio-gpio", 0);

	if (!gpio_is_valid(spec_pdata->disp_vddio_gpio))
		pr_err("%s:%d, disp vddio gpio not specified\n",
						__func__, __LINE__);

	spec_pdata->touch_vddio_gpio = of_get_named_gpio(
			ctrl_pdev->dev.of_node,
			"qcom,platform-touch-vddio-gpio", 0);

	if (!gpio_is_valid(spec_pdata->touch_vddio_gpio))
		pr_err("%s:%d, touch vddio gpio not specified\n",
						__func__, __LINE__);

	spec_pdata->touch_reset_gpio = of_get_named_gpio(
			ctrl_pdev->dev.of_node,
			"qcom,platform-touch-reset-gpio", 0);

	if (!gpio_is_valid(spec_pdata->touch_reset_gpio))
		pr_err("%s:%d, touch reset gpio not specified\n",
						__func__, __LINE__);

	spec_pdata->touch_int_gpio = of_get_named_gpio(
			ctrl_pdev->dev.of_node,
			"qcom,platform-touch-int-gpio", 0);

	if (!gpio_is_valid(spec_pdata->touch_int_gpio))
		pr_err("%s:%d, touch int gpio not specified\n",
						__func__, __LINE__);

	spec_pdata->disp_dcdc_en_gpio = of_get_named_gpio(
			ctrl_pdev->dev.of_node,
			"somc,disp-dcdc-en-gpio", 0);

	if (!gpio_is_valid(spec_pdata->disp_dcdc_en_gpio))
		pr_err("%s:%d, disp dcdc en gpio not specified\n",
						__func__, __LINE__);
}


static void mdss_dsi_panel_set_gpio_seq(
		int gpio, int seq_num, const int *seq)
{
	int i;

	for (i = 0; i + 1 < seq_num; i += 2) {
		gpio_set_value(gpio, seq[i]);
		usleep_range(seq[i + 1] * 1000, seq[i + 1] * 1000 + 100);
		pr_debug("%s: enable=%d, wait=%dms\n",
			__func__, seq[i], seq[i+1]);
	}
}

int mdss_dsi_panel_driver_reset_panel(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	struct mdss_panel_power_seq *pw_seq = NULL;
	int rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (!gpio_is_valid(ctrl_pdata->rst_gpio)) {
		pr_debug("%s:%d, panel reset line not configured\n",
			   __func__, __LINE__);
		return rc;
	}

	pr_debug("%s: enable = %d\n", __func__, enable);
	pinfo = &(ctrl_pdata->panel_data.panel_info);


	if (!gpio_req) {
		rc = mdss_dsi_request_gpios(ctrl_pdata);
		if (rc) {
			pr_err("gpio request failed\n");
			return rc;
		}
		gpio_req = true;
	}

	if (mdss_dsi_panel_driver_is_seq_for_ewu() && enable)
		pw_seq = &ctrl_pdata->spec_pdata->ewu_seq ?
				&ctrl_pdata->spec_pdata->ewu_seq :
				&ctrl_pdata->spec_pdata->on_seq;
	else
		pw_seq = (enable) ? &ctrl_pdata->spec_pdata->on_seq :
					&ctrl_pdata->spec_pdata->off_seq;

	mdss_dsi_panel_set_gpio_seq(ctrl_pdata->rst_gpio,
				pw_seq->seq_num, pw_seq->rst_seq);

	return rc;
}

int mdss_dsi_panel_driver_reset_touch(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	int rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	spec_pdata = ctrl_pdata->spec_pdata;

	if (!gpio_is_valid(spec_pdata->touch_reset_gpio)) {
		pr_debug("%s:%d, touch reset line not configured\n",
			   __func__, __LINE__);
		return rc;
	}

	pr_debug("%s: enable = %d\n", __func__, enable);
	pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (enable) {
		if (!gpio_req) {
			rc = mdss_dsi_request_gpios(ctrl_pdata);
			if (rc) {
				pr_err("gpio request failed\n");
				return rc;
			}
			gpio_req = true;
		}

		mdss_dsi_panel_driver_gpio_output(ctrl_pdata,
			(spec_pdata->touch_reset_gpio), true, 1);
	} else {
		mdss_dsi_panel_driver_set_gpio(ctrl_pdata,
			(spec_pdata->touch_reset_gpio), false, 0);
	}

	return rc;
}

int mdss_dsi_panel_driver_reset_touch_ctrl(struct mdss_panel_data *pdata, bool en)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	int rc = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	spec_pdata = ctrl_pdata->spec_pdata;

	gpio_set_value(spec_pdata->touch_reset_gpio, 0);
	usleep_range(spec_pdata->off_seq.touch_reset * 1000,
			spec_pdata->off_seq.touch_reset * 1000);
	gpio_set_value(spec_pdata->touch_reset_gpio, 1);

	return rc;
}

static bool mdss_dsi_panel_driver_split_display_enabled(void)
{
	/*
	 * currently the only supported mode is split display.
	 * So, if both controllers are initialized, then assume that
	 * split display mode is enabled.
	 */
	return ctrl_list[DSI_CTRL_LEFT] && ctrl_list[DSI_CTRL_RIGHT];
}

int mdss_dsi_panel_driver_reset_dual_display(
			struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_dsi_ctrl_pdata *mctrl_pdata = NULL;
	int ret = 0;

	if (!mdss_dsi_panel_driver_split_display_enabled()) {
		if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
			pr_debug("reset enable: pinctrl not enabled\n");
		ret = mdss_dsi_panel_reset(&(ctrl_pdata->panel_data), 1);
	} else if (ctrl_pdata->ndx == DSI_CTRL_1) {
		mctrl_pdata = mdss_dsi_get_other_ctrl(ctrl_pdata);
		if (!mctrl_pdata) {
			pr_warn("%s: Unable to get other control\n",
				__func__);
			ret = -EINVAL;
		} else {
			if (mdss_dsi_pinctrl_set_state(mctrl_pdata, true))
				pr_debug("other reset pinctrl not enabled\n");
			ret = mdss_dsi_panel_reset(&(mctrl_pdata->panel_data), 1);
		}
	} else {
		pr_debug("%s: reset pinctrl not yet\n", __func__);
	}

	return ret;
}

static int mdss_dsi_property_read_u32_var(struct device_node *np,
		char *name, u32 **out_data, int *num)
{
	struct property *prop = of_find_property(np, name, NULL);
	const __be32 *val;
	u32 *out;
	int s;

	if (!prop) {
		pr_debug("%s:%d, unable to read %s", __func__, __LINE__, name);
		return -EINVAL;
	}
	if (!prop->value) {
		pr_debug("%s:%d, no data of %s", __func__, __LINE__, name);
		return -ENODATA;
	}

	*num = prop->length / sizeof(u32);
	if (!*num || *num % 2) {
		pr_debug("%s:%d, error reading %s, length found = %d\n",
			__func__, __LINE__, name, *num);
		return -ENODATA;
	}
	*out_data = kzalloc(prop->length, GFP_KERNEL);
	if (!*out_data) {
		pr_err("%s:no mem assigned: kzalloc fail\n", __func__);
		*num = 0;
		return -ENOMEM;
	}

	val = prop->value;
	out = *out_data;
	s = *num;
	while (s--)
		*out++ = be32_to_cpup(val++);
	return 0;
}

int mdss_dsi_panel_driver_parse_dt(struct device_node *np,
		struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	u32 tmp = 0;
	int rc = 0;
	const char *panel_mode;
	const char *rst_seq;

	if (ctrl_pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		goto error;
	}

	spec_pdata = ctrl_pdata->spec_pdata;

	spec_pdata->pcc_enable = of_property_read_bool(np, "somc,mdss-dsi-pcc-enable");
	if (spec_pdata->pcc_enable) {
		mdss_dsi_parse_dcs_cmds(np, &spec_pdata->pre_uv_read_cmds,
			"somc,mdss-dsi-pre-uv-command", NULL);

		mdss_dsi_parse_dcs_cmds(np, &spec_pdata->uv_read_cmds,
			"somc,mdss-dsi-uv-command", NULL);

		rc = of_property_read_u32(np,
			"somc,mdss-dsi-uv-param-type", &tmp);
		spec_pdata->pcc_data.param_type =
			(!rc ? tmp : CLR_DATA_UV_PARAM_TYPE_NONE);

		rc = of_property_read_u32(np,
			"somc,mdss-dsi-pcc-table-size", &tmp);
		spec_pdata->pcc_data.tbl_size =
			(!rc ? tmp : 0);

		spec_pdata->pcc_data.color_tbl =
			kzalloc(spec_pdata->pcc_data.tbl_size *
				sizeof(struct mdss_pcc_color_tbl),
				GFP_KERNEL);
		if (!spec_pdata->pcc_data.color_tbl) {
			pr_err("no mem assigned: kzalloc fail\n");
			return -ENOMEM;
		}
		rc = of_property_read_u32_array(np,
			"somc,mdss-dsi-pcc-table",
			(u32 *)spec_pdata->pcc_data.color_tbl,
			spec_pdata->pcc_data.tbl_size *
			sizeof(struct mdss_pcc_color_tbl) /
			sizeof(u32));
		if (rc) {
			spec_pdata->pcc_data.tbl_size = 0;
			kzfree(spec_pdata->pcc_data.color_tbl);
			spec_pdata->pcc_data.color_tbl = NULL;
			pr_err("%s:%d, Unable to read pcc table",
				__func__, __LINE__);
		}
		spec_pdata->pcc_data.pcc_sts |= PCC_STS_UD;
	}

	spec_pdata->srgb_pcc_enable = of_property_read_bool(np,
			"somc,mdss-dsi-srgb-pcc-enable");
	if (spec_pdata->srgb_pcc_enable) {
		rc = of_property_read_u32(np,
			"somc,mdss-dsi-srgb-pcc-table-size", &tmp);
		spec_pdata->srgb_pcc_data.tbl_size =
			(!rc ? tmp : 0);

		spec_pdata->srgb_pcc_data.color_tbl =
			kzalloc(spec_pdata->srgb_pcc_data.tbl_size *
				sizeof(struct mdss_pcc_color_tbl),
				GFP_KERNEL);
		if (!spec_pdata->srgb_pcc_data.color_tbl) {
			pr_err("no mem assigned: kzalloc fail\n");
			return -ENOMEM;
		}
		rc = of_property_read_u32_array(np,
			"somc,mdss-dsi-srgb-pcc-table",
			(u32 *)spec_pdata->srgb_pcc_data.color_tbl,
			spec_pdata->srgb_pcc_data.tbl_size *
			sizeof(struct mdss_pcc_color_tbl) /
			sizeof(u32));
		if (rc) {
			spec_pdata->srgb_pcc_data.tbl_size = 0;
			kzfree(spec_pdata->srgb_pcc_data.color_tbl);
			spec_pdata->srgb_pcc_data.color_tbl = NULL;
			pr_err("%s:%d, Unable to read sRGB pcc table",
				__func__, __LINE__);
		}
	}

	spec_pdata->vivid_pcc_enable = of_property_read_bool(np,
			"somc,mdss-dsi-vivid-pcc-enable");
	if (spec_pdata->vivid_pcc_enable) {
		rc = of_property_read_u32(np,
			"somc,mdss-dsi-vivid-pcc-table-size", &tmp);
		spec_pdata->vivid_pcc_data.tbl_size =
			(!rc ? tmp : 0);

		spec_pdata->vivid_pcc_data.color_tbl =
			kzalloc(spec_pdata->vivid_pcc_data.tbl_size *
				sizeof(struct mdss_pcc_color_tbl),
				GFP_KERNEL);
		if (!spec_pdata->vivid_pcc_data.color_tbl) {
			pr_err("no mem assigned: kzalloc fail\n");
			return -ENOMEM;
		}
		rc = of_property_read_u32_array(np,
			"somc,mdss-dsi-vivid-pcc-table",
			(u32 *)spec_pdata->vivid_pcc_data.color_tbl,
			spec_pdata->vivid_pcc_data.tbl_size *
			sizeof(struct mdss_pcc_color_tbl) /
			sizeof(u32));
		if (rc) {
			spec_pdata->vivid_pcc_data.tbl_size = 0;
			kzfree(spec_pdata->vivid_pcc_data.color_tbl);
			spec_pdata->vivid_pcc_data.color_tbl = NULL;
			pr_err("%s:%d, Unable to read Vivid pcc table",
				__func__, __LINE__);
		}
	}

	spec_pdata->hdr_pcc_enable = of_property_read_bool(np,
			"somc,mdss-dsi-hdr-pcc-enable");
	if (spec_pdata->hdr_pcc_enable) {
		rc = of_property_read_u32(np,
			"somc,mdss-dsi-hdr-pcc-table-size", &tmp);
		spec_pdata->hdr_pcc_data.tbl_size =
			(!rc ? tmp : 0);

		spec_pdata->hdr_pcc_data.color_tbl =
			kzalloc(spec_pdata->hdr_pcc_data.tbl_size *
				sizeof(struct mdss_pcc_color_tbl),
				GFP_KERNEL);
		if (!spec_pdata->hdr_pcc_data.color_tbl) {
			pr_err("no mem assigned: kzalloc fail\n");
			return -ENOMEM;
		}
		rc = of_property_read_u32_array(np,
			"somc,mdss-dsi-hdr-pcc-table",
			(u32 *)spec_pdata->hdr_pcc_data.color_tbl,
			spec_pdata->hdr_pcc_data.tbl_size *
			sizeof(struct mdss_pcc_color_tbl) /
			sizeof(u32));
		if (rc) {
			spec_pdata->hdr_pcc_data.tbl_size = 0;
			kzfree(spec_pdata->hdr_pcc_data.color_tbl);
			spec_pdata->hdr_pcc_data.color_tbl = NULL;
			pr_err("%s:%d, Unable to read HDR pcc table",
				__func__, __LINE__);
		}
	}

	(void)mdss_dsi_property_read_u32_var(np,
		"qcom,mdss-dsi-reset-sequence",
		(u32 **)&spec_pdata->on_seq.rst_seq,
		&spec_pdata->on_seq.seq_num);

	rst_seq = of_get_property(np, "somc,pw-on-rst-seq", NULL);
	if (!rst_seq) {
		spec_pdata->rst_after_pon = true;
	} else if (!strcmp(rst_seq, "after_power_on")) {
		spec_pdata->rst_after_pon = true;
	} else if (!strcmp(rst_seq, "before_power_on")) {
		spec_pdata->rst_after_pon = false;
	} else {
		spec_pdata->rst_after_pon = true;
	}

	if (of_find_property(np, "somc,pw-off-rst-b-seq", NULL)) {
		spec_pdata->off_seq.rst_b_seq = true;

		(void)mdss_dsi_property_read_u32_var(np,
			"somc,pw-off-rst-b-seq",
			(u32 **)&spec_pdata->off_seq.rst_seq,
			&spec_pdata->off_seq.seq_num);
	} else {
		(void)mdss_dsi_property_read_u32_var(np,
			"somc,pw-off-rst-seq",
			(u32 **)&spec_pdata->off_seq.rst_seq,
			&spec_pdata->off_seq.seq_num);
	}


	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-vddio", &tmp);
	spec_pdata->on_seq.disp_vddio = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-vsp", &tmp);
	spec_pdata->on_seq.disp_vsp = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-vsn", &tmp);
	spec_pdata->on_seq.disp_vsn = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-dcdc", &tmp);
	spec_pdata->on_seq.disp_dcdc = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-vddio", &tmp);
	spec_pdata->off_seq.disp_vddio = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-vsp", &tmp);
	spec_pdata->off_seq.disp_vsp = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-vsn", &tmp);
	spec_pdata->off_seq.disp_vsn = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-dcdc", &tmp);
	spec_pdata->off_seq.disp_dcdc = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-touch-avdd", &tmp);
	spec_pdata->on_seq.touch_avdd = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-touch-vddio", &tmp);
	spec_pdata->on_seq.touch_vddio = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-touch-reset", &tmp);
	spec_pdata->on_seq.touch_reset = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-touch-reset-first", &tmp);
	spec_pdata->on_seq.touch_reset_first = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-on-touch-int-n", &tmp);
	spec_pdata->on_seq.touch_intn = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-touch-avdd", &tmp);
	spec_pdata->off_seq.touch_avdd = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-touch-vddio", &tmp);
	spec_pdata->off_seq.touch_vddio = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-touch-reset", &tmp);
	spec_pdata->off_seq.touch_reset = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-wait-after-off-touch-int-n", &tmp);
	spec_pdata->off_seq.touch_intn = !rc ? tmp : 0;

	rc = of_property_read_u32(np,
			"somc,pw-down-period", &tmp);
	spec_pdata->down_period = !rc ? tmp : 0;

	rc = mdss_dsi_property_read_u32_var(np,
		"somc,ewu-rst-seq",
		(u32 **)&spec_pdata->ewu_seq.rst_seq,
		&spec_pdata->ewu_seq.seq_num);
	if (rc) {
		spec_pdata->ewu_seq.rst_seq = NULL;
		spec_pdata->ewu_seq.seq_num = 0;
		pr_debug("%s: Unable to read ewu sequence\n", __func__);
	}

	rc = of_property_read_u32(np,
		"somc,ewu-wait-after-touch-reset", &tmp);
	spec_pdata->ewu_seq.touch_reset = !rc ? tmp : 0;

	spec_pdata->fps_mode.enable = of_property_read_bool(np,
					"somc,fps-mode-enable");
	if (spec_pdata->fps_mode.enable) {
		panel_mode = of_get_property(np,
					"somc,fps-mode-panel-mode", NULL);

		if (!panel_mode) {
			pr_err("%s:%d, Panel mode not specified\n",
							__func__, __LINE__);
			goto error;
		}

		if (!strncmp(panel_mode, "susres_mode", 11)) {
			spec_pdata->fps_mode.mode = FPS_MODE_SUSRES;
		} else if (!strncmp(panel_mode, "dynamic_mode", 12)) {
			spec_pdata->fps_mode.mode = FPS_MODE_DYNAMIC;
		} else {
			pr_err("%s: Unable to read fps panel mode\n", __func__);
			goto error;
		}

		mdss_dsi_parse_dcs_cmds(np, &spec_pdata->fps_cmds[FPS_MODE_OFF_RR_OFF],
								"somc,fps-mode-off-rr-off", NULL);
		mdss_dsi_parse_dcs_cmds(np, &spec_pdata->fps_cmds[FPS_MODE_OFF_RR_ON],
								"somc,fps-mode-off-rr-on", NULL);
		mdss_dsi_parse_dcs_cmds(np, &spec_pdata->fps_cmds[FPS_MODE_ON_RR_OFF],
								"somc,fps-mode-on-rr-off", NULL);
		mdss_dsi_parse_dcs_cmds(np, &spec_pdata->fps_cmds[FPS_MODE_ON_RR_ON],
								"somc,fps-mode-on-rr-on", NULL);

		spec_pdata->fps_mode.type = FPS_MODE_OFF_RR_OFF;
	}

	return 0;

error:
	return -EINVAL;
}

static void conv_uv_data(char *data, int param_type, int *u_data, int *v_data)
{
	switch (param_type) {
	case CLR_DATA_UV_PARAM_TYPE_RENE_DEFAULT:
		*u_data = ((data[0] & 0x0F) << 2) |
			/* 4bit of data[0] higher data. */
			((data[1] >> 6) & 0x03);
			/* 2bit of data[1] lower data. */
		*v_data = (data[1] & 0x3F);
			/* Remainder 6bit of data[1] is effective as v_data. */
		break;
	case CLR_DATA_UV_PARAM_TYPE_NOVA_DEFAULT:
	case CLR_DATA_UV_PARAM_TYPE_RENE_SR:
		/* 6bit is effective as u_data */
		*u_data = data[0] & 0x3F;
		/* 6bit is effective as v_data */
		*v_data = data[1] & 0x3F;
		break;
	case CLR_DATA_UV_PARAM_TYPE_NOVA_AUO:
		/* 6bit is effective as u_data */
		*u_data = data[0] & 0x3F;
		/* 6bit is effective as v_data */
		*v_data = data[2] & 0x3F;
		break;
	default:
		pr_err("%s: Failed to conv type:%d\n", __func__, param_type);
		break;
	}
}

static int get_uv_param_len(int param_type, bool *short_response)
{
	int ret = 0;

	*short_response = false;
	switch (param_type) {
	case CLR_DATA_UV_PARAM_TYPE_RENE_DEFAULT:
		ret = CLR_DATA_REG_LEN_RENE_DEFAULT;
		break;
	case CLR_DATA_UV_PARAM_TYPE_NOVA_DEFAULT:
		ret = CLR_DATA_REG_LEN_NOVA_DEFAULT;
		break;
	case CLR_DATA_UV_PARAM_TYPE_NOVA_AUO:
		ret = CLR_DATA_REG_LEN_NOVA_AUO;
		break;
	case CLR_DATA_UV_PARAM_TYPE_RENE_SR:
		ret = CLR_DATA_REG_LEN_RENE_SR;
		*short_response = true;
		break;
	default:
		pr_err("%s: Failed to get param len\n", __func__);
		break;
	}

	return ret;
}

static void get_uv_data(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		int *u_data, int *v_data)
{
	struct dsi_cmd_desc *cmds = ctrl_pdata->spec_pdata->uv_read_cmds.cmds;
	int param_type = ctrl_pdata->spec_pdata->pcc_data.param_type;
	char buf[MDSS_DSI_LEN];
	char *pos = buf;
	int len;
	int i;
	bool short_response;
	struct dcs_cmd_req cmdreq;

	len = get_uv_param_len(param_type, &short_response);

	for (i = 0; i < ctrl_pdata->spec_pdata->uv_read_cmds.cmd_cnt; i++) {
		memset(&cmdreq, 0, sizeof(cmdreq));
		cmdreq.cmds = cmds;
		cmdreq.cmds_cnt = 1;
		cmdreq.flags = CMD_REQ_RX | CMD_REQ_COMMIT;
		cmdreq.rlen = short_response ? 1 : len;
		cmdreq.rbuf = ctrl_pdata->rx_buf.data;
		cmdreq.cb = NULL;

		mdss_dsi_cmdlist_put(ctrl_pdata, &cmdreq);

		memcpy(pos, ctrl_pdata->rx_buf.data, len);
		pos += len;
		cmds++;
	}
	conv_uv_data(buf, param_type, u_data, v_data);
}

static int find_color_area(struct mdp_pcc_cfg_data *pcc_config,
	struct mdss_pcc_data *pcc_data)
{
	int i;
	int ret = 0;

	for (i = 0; i < pcc_data->tbl_size; i++) {
		if (pcc_data->u_data < pcc_data->color_tbl[i].u_min)
			continue;
		if (pcc_data->u_data > pcc_data->color_tbl[i].u_max)
			continue;
		if (pcc_data->v_data < pcc_data->color_tbl[i].v_min)
			continue;
		if (pcc_data->v_data > pcc_data->color_tbl[i].v_max)
			continue;
		break;
	}
	pcc_data->tbl_idx = i;
	if (i >= pcc_data->tbl_size) {
		ret = -EINVAL;
		goto exit;
	}

	pcc_config->r.r = pcc_data->color_tbl[i].r_data;
	pcc_config->g.g = pcc_data->color_tbl[i].g_data;
	pcc_config->b.b = pcc_data->color_tbl[i].b_data;
exit:
	return ret;
}

static int find_color_area_for_srgb(struct mdss_pcc_data *pcc_data)
{
	int i;
	int ret = 0;

	for (i = 0; i < pcc_data->tbl_size; i++) {
		if (pcc_data->u_data < pcc_data->color_tbl[i].u_min)
			continue;
		if (pcc_data->u_data > pcc_data->color_tbl[i].u_max)
			continue;
		if (pcc_data->v_data < pcc_data->color_tbl[i].v_min)
			continue;
		if (pcc_data->v_data > pcc_data->color_tbl[i].v_max)
			continue;
		break;
	}
	pcc_data->tbl_idx = i;
	if (i >= pcc_data->tbl_size) {
		ret = -EINVAL;
	}

	return ret;
}

static int find_color_area_for_vivid(struct mdss_pcc_data *pcc_data)
{
	return find_color_area_for_srgb(pcc_data);
}

static int find_color_area_for_hdr(struct mdss_pcc_data *pcc_data)
{
	return find_color_area_for_srgb(pcc_data);
}

int mdss_dsi_panel_pcc_setup(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_pcc_data *pcc_data = NULL;
	struct mdss_pcc_data *srgb_pcc_data = NULL;
	struct mdss_pcc_data *vivid_pcc_data = NULL;
	struct mdss_pcc_data *hdr_pcc_data = NULL;
	struct mdp_pcc_cfg_data pcc_config;
	int ret;
	u32 raw_u_data = 0, raw_v_data = 0;
	struct mdss_panel_specific_pdata *spec_pdata = NULL;
	u8 idx = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	spec_pdata = ctrl_pdata->spec_pdata;

	if (!ctrl_pdata->spec_pdata->pcc_enable) {
		if (pdata->panel_info.dsi_master == pdata->panel_info.pdest)
			pr_notice("%s (%d): pcc isn't enabled.\n",
				__func__, __LINE__);
		goto exit;
	}

	pcc_data = &ctrl_pdata->spec_pdata->pcc_data;

	mdss_dsi_op_mode_config(DSI_CMD_MODE, pdata);
	if (ctrl_pdata->spec_pdata->pre_uv_read_cmds.cmds)
		mdss_dsi_panel_cmds_send(
			ctrl_pdata, &ctrl_pdata->spec_pdata->pre_uv_read_cmds, CMD_REQ_COMMIT);
	if (ctrl_pdata->spec_pdata->uv_read_cmds.cmds) {
		get_uv_data(ctrl_pdata, &pcc_data->u_data, &pcc_data->v_data);
		raw_u_data = pcc_data->u_data;
		raw_v_data = pcc_data->v_data;
	}
	if (pcc_data->u_data == 0 && pcc_data->v_data == 0) {
		pr_notice("%s (%d): u,v is flashed 0.\n",
			__func__, __LINE__);
		goto exit;
	}
	if (!pcc_data->color_tbl) {
		if (pdata->panel_info.dsi_master == pdata->panel_info.pdest)
			pr_notice("%s (%d): color_tbl isn't found.\n",
				__func__, __LINE__);
		goto exit;
	}

	memset(&pcc_config, 0, sizeof(struct mdp_pcc_cfg_data));
	ret = find_color_area(&pcc_config, pcc_data);
	if (ret) {
		pr_err("%s: failed to find color area.\n", __func__);
		goto exit;
	}

	if (spec_pdata->srgb_pcc_enable) {
		srgb_pcc_data = &spec_pdata->srgb_pcc_data;
		srgb_pcc_data->u_data = pcc_data->u_data;
		srgb_pcc_data->v_data = pcc_data->v_data;
		ret = find_color_area_for_srgb(srgb_pcc_data);
		if (ret) {
			pr_err("%s: failed to find color area.\n", __func__);
			goto exit;
		}
		idx = srgb_pcc_data->tbl_idx;
		pr_notice("SRGB : %s (%d): raw_ud=%d raw_vd=%d ct=%d "
			"area=%d ud=%d vd=%d r=0x%08X g=0x%08X b=0x%08X\n",
			__func__, __LINE__,
			raw_u_data, raw_v_data,
			srgb_pcc_data->color_tbl[idx].color_type,
			srgb_pcc_data->color_tbl[idx].area_num,
			srgb_pcc_data->u_data, srgb_pcc_data->v_data,
			srgb_pcc_data->color_tbl[idx].r_data,
			srgb_pcc_data->color_tbl[idx].g_data,
			srgb_pcc_data->color_tbl[idx].b_data);
	}

	if (spec_pdata->vivid_pcc_enable) {
		vivid_pcc_data = &spec_pdata->vivid_pcc_data;
		vivid_pcc_data->u_data = pcc_data->u_data;
		vivid_pcc_data->v_data = pcc_data->v_data;
		ret = find_color_area_for_vivid(vivid_pcc_data);
		if (ret) {
			pr_err("%s: failed to find color area.\n", __func__);
			goto exit;
		}
		idx = vivid_pcc_data->tbl_idx;
		pr_notice("Vivid : %s (%d): raw_ud=%d raw_vd=%d ct=%d "
			"area=%d ud=%d vd=%d r=0x%08X g=0x%08X b=0x%08X\n",
			__func__, __LINE__,
			raw_u_data, raw_v_data,
			vivid_pcc_data->color_tbl[idx].color_type,
			vivid_pcc_data->color_tbl[idx].area_num,
			vivid_pcc_data->u_data, vivid_pcc_data->v_data,
			vivid_pcc_data->color_tbl[idx].r_data,
			vivid_pcc_data->color_tbl[idx].g_data,
			vivid_pcc_data->color_tbl[idx].b_data);
	}

	if (spec_pdata->hdr_pcc_enable) {
		hdr_pcc_data = &spec_pdata->hdr_pcc_data;
		hdr_pcc_data->u_data = pcc_data->u_data;
		hdr_pcc_data->v_data = pcc_data->v_data;
		ret = find_color_area_for_hdr(hdr_pcc_data);
		if (ret) {
			pr_err("%s: failed to find color area.\n", __func__);
			goto exit;
		}
		idx = hdr_pcc_data->tbl_idx;
		pr_notice("HDR : %s (%d): raw_ud=%d raw_vd=%d ct=%d "
			"area=%d ud=%d vd=%d r=0x%08X g=0x%08X b=0x%08X\n",
			__func__, __LINE__,
			raw_u_data, raw_v_data,
			hdr_pcc_data->color_tbl[idx].color_type,
			hdr_pcc_data->color_tbl[idx].area_num,
			hdr_pcc_data->u_data, hdr_pcc_data->v_data,
			hdr_pcc_data->color_tbl[idx].r_data,
			hdr_pcc_data->color_tbl[idx].g_data,
			hdr_pcc_data->color_tbl[idx].b_data);
	}

	pr_notice("%s (%d): raw_ud=%d raw_vd=%d "
		"ct=%d area=%d ud=%d vd=%d r=0x%08X g=0x%08X b=0x%08X\n",
		__func__, __LINE__,
		raw_u_data, raw_v_data,
		pcc_data->color_tbl[pcc_data->tbl_idx].color_type,
		pcc_data->color_tbl[pcc_data->tbl_idx].area_num,
		pcc_data->u_data, pcc_data->v_data,
		pcc_data->color_tbl[pcc_data->tbl_idx].r_data,
		pcc_data->color_tbl[pcc_data->tbl_idx].g_data,
		pcc_data->color_tbl[pcc_data->tbl_idx].b_data);

exit:
	return 0;
}

struct mdss_panel_specific_pdata *mdss_panel2spec_pdata(
					struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata;

	ctrl_pdata = container_of(pdata,
			struct mdss_dsi_ctrl_pdata, panel_data);
	return ctrl_pdata->spec_pdata;
}

static u32 ts_diff_ms(struct timespec lhs, struct timespec rhs)
{
	struct timespec tdiff;
	s64 nsec;
	u32 msec;

	tdiff = timespec_sub(lhs, rhs);
	nsec = timespec_to_ns(&tdiff);
	msec = (u32)nsec;
	do_div(msec, NSEC_PER_MSEC);

	return msec;
}

static struct fps_data *mdss_dsi_panel_driver_get_fps_address(fps_type type)
{
	switch (type) {
	case FPSD:
		return &fpsd;
	case VPSD:
		return &vpsd;
	default:
		pr_err("%s: select Failed!\n", __func__);
		return NULL;
	}
}

static void update_fps_data(struct fps_data *fps)
{
	if (mutex_trylock(&fps->fps_lock)) {
		u32 fpks = 0;
		u32 ms_since_last = 0;
		u32 num_frames;
		struct timespec tlast = fps->timestamp_last;
		struct timespec tnow;
		u32 msec;

		getrawmonotonic(&tnow);
		msec = ts_diff_ms(tnow, tlast);
		fps->timestamp_last = tnow;

		fps->interval_ms = msec;
		fps->frame_counter++;
		num_frames = fps->frame_counter - fps->frame_counter_last;

		fps->fa[fps->fps_array_cnt].frame_nbr = fps->frame_counter;
		fps->fa[fps->fps_array_cnt].time_delta = msec;
		fps->fa_last_array_pos = fps->fps_array_cnt;
		(fps->fps_array_cnt)++;
		if (fps->fps_array_cnt >= DEFAULT_FPS_ARRAY_SIZE)
			fps->fps_array_cnt = 0;

		ms_since_last = ts_diff_ms(tnow, fps->fpks_ts_last);

		if (num_frames > 1 && ms_since_last >= fps->log_interval) {
			fpks = (num_frames * 1000000) / ms_since_last;
			fps->fpks_ts_last = tnow;
			fps->frame_counter_last = fps->frame_counter;
			fps->fpks = fpks;
		}
		mutex_unlock(&fps->fps_lock);
	}
}

static void mdss_dsi_panel_driver_fps_data_init(fps_type type)
{
	struct fps_data *fps = mdss_dsi_panel_driver_get_fps_address(type);

	if (!fps) {
		pr_err("%s: select Failed!\n", __func__);
		return;
	}

	fps->frame_counter = 0;
	fps->frame_counter_last = 0;
	fps->log_interval = DEFAULT_FPS_LOG_INTERVAL;
	fps->fpks = 0;
	fps->fa_last_array_pos = 0;
	fps->vps_en = false;
	getrawmonotonic(&fps->timestamp_last);
	mutex_init(&fps->fps_lock);
}

void mdss_dsi_panel_driver_fps_data_update(
		struct msm_fb_data_type *mfd, fps_type type)
{
	struct fps_data *fps = mdss_dsi_panel_driver_get_fps_address(type);

	if (!fps) {
		pr_err("%s: select Failed!\n", __func__);
		return;
	}

	if (mfd->index == 0)
		update_fps_data(fps);
}

struct fps_data mdss_dsi_panel_driver_get_fps_data(void)
{
	return fpsd;
}

struct fps_data mdss_dsi_panel_driver_get_vps_data(void)
{
	return vpsd;
}

static void vsync_handler(struct mdss_mdp_ctl *ctl, ktime_t t)
{
	struct msm_fb_data_type *mfd = ctl->mfd;

	mdss_dsi_panel_driver_fps_data_update(mfd, VPSD);
}

static void mdss_dsi_panel_driver_vsync_handler_init(void)
{
	vs_handle.vsync_handler = NULL;
}

ssize_t mdss_dsi_panel_driver_vsyncs_per_ksecs_store(struct device *dev,
			 const char *buf, size_t count)
{
	int ret = count;
	long vps_en;
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	struct mdss_mdp_ctl *ctl = mdata->ctl_off;

	if (kstrtol(buf, 10, &vps_en)) {
		dev_err(dev, "%s: Error, buf = %s\n", __func__, buf);
		ret = -EINVAL;
		goto exit;
	}

	vs_handle.vsync_handler = (mdp_vsync_handler_t)vsync_handler;
	vs_handle.cmd_post_flush = false;

	if (vps_en) {
		vs_handle.enabled = false;
		if (!vpsd.vps_en && (ctl->ops.add_vsync_handler)) {
			ctl->ops.add_vsync_handler(ctl, &vs_handle);
			vpsd.vps_en = true;
			pr_notice("%s: vsyncs_per_ksecs is valid\n", __func__);
		}
	} else {
		vs_handle.enabled = true;
		if (vpsd.vps_en && (ctl->ops.remove_vsync_handler)) {
			ctl->ops.remove_vsync_handler(ctl, &vs_handle);
			vpsd.vps_en = false;
			fpsd.fpks = 0;
			pr_notice("%s: vsyncs_per_ksecs is invalid\n", __func__);
		}
	}
exit:
	return ret;
}

void mdss_dsi_panel_fps_mode_set(struct mdss_dsi_ctrl_pdata *ctrl_pdata, int mode_type)
{
	struct mdss_panel_specific_pdata *spec_pdata = ctrl_pdata->spec_pdata;

	switch (mode_type) {
	case FPS_MODE_OFF_RR_OFF:
	case FPS_MODE_ON_RR_OFF:
	case FPS_MODE_OFF_RR_ON:
	case FPS_MODE_ON_RR_ON:
		spec_pdata->fps_mode.type =  mode_type;
		break;
	default:
		pr_err("%s: invalid value for fps mode type = %d\n",
			__func__, mode_type);
		return;
	}

	if ((ctrl_pdata->panel_data.panel_info.mipi.mode == DSI_CMD_MODE) &&
			(spec_pdata->fps_cmds[spec_pdata->fps_mode.type].cmd_cnt)) {
		pr_info("%s: change fps mode %d.\n", __func__, spec_pdata->fps_mode.type);
		mdss_dsi_panel_cmds_send(ctrl_pdata,
				&spec_pdata->fps_cmds[spec_pdata->fps_mode.type],
				CMD_REQ_COMMIT);
	} else {
		pr_err("%s: change fps isn't supported\n", __func__);
	}
}

static void mdss_dsi_panel_driver_fps_mode_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_panel_specific_pdata *spec_pdata = ctrl_pdata->spec_pdata;

	if (ctrl_pdata->panel_data.panel_info.mipi.mode == DSI_CMD_MODE) {
		if (spec_pdata->fps_cmds[spec_pdata->fps_mode.type].cmd_cnt) {
			pr_info("%s: fps mode %d.\n", __func__, spec_pdata->fps_mode.type);
			mdss_dsi_panel_cmds_send(ctrl_pdata,
					&spec_pdata->fps_cmds[spec_pdata->fps_mode.type],
					CMD_REQ_COMMIT);
		}
	}
}

static int mdss_dsi_panel_driver_fps_mode_check_state
	(struct mdss_dsi_ctrl_pdata *ctrl_pdata, int mode_type)
{
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	struct msm_fb_data_type *mfd = mdata->ctl_off->mfd;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);
	struct mdss_panel_specific_pdata *spec_pdata;

	spec_pdata = ctrl_pdata->spec_pdata;

	if (!mdp5_data || !mdp5_data->ctl || !mdp5_data->ctl->power_state)
		goto error;

	if (!display_onoff_state) {
		pr_err("%s: Disp-On is not yet completed. Please retry\n", __func__);
		goto error;
	}

	if (spec_pdata->fps_mode.mode == FPS_MODE_DYNAMIC)
		mdss_dsi_panel_fps_mode_set(ctrl_pdata, mode_type);

	return 0;

error:
	return -EINVAL;
}

ssize_t mdss_dsi_panel_driver_fps_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int mode_type, rc;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = dev_get_drvdata(dev);

	if (!ctrl_pdata || !ctrl_pdata->spec_pdata) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	if (!ctrl_pdata->spec_pdata->fps_mode.enable) {
		pr_err("%s: change fps not enabled\n", __func__);
		return -EINVAL;
	}

	rc = kstrtoint(buf, 10, &mode_type);
	if (rc < 0) {
		pr_err("%s: Error, buf = %s\n", __func__, buf);
		return rc;
	}

	if (mode_type == ctrl_pdata->spec_pdata->fps_mode.type) {
		pr_notice("%s: fps mode is already %d\n", __func__,
			mode_type);
		return count;
	}

	rc = mdss_dsi_panel_driver_fps_mode_check_state(ctrl_pdata, mode_type);
	if (rc) {
		pr_err("%s: Error, rc = %d\n", __func__, rc);
		return rc;
	}

	return count;
}

ssize_t mdss_dsi_panel_driver_fps_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = dev_get_drvdata(dev);
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	struct msm_fb_data_type *mfd = mdata->ctl_off->mfd;
	struct mdss_overlay_private *mdp5_data = mfd_to_mdp5_data(mfd);

	if (!mdp5_data || !mdp5_data->ctl || !mdp5_data->ctl->power_state)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "%d\n", ctrl_pdata->spec_pdata->fps_mode.type);
}

void mdss_dsi_panel_driver_check_splash_enable(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (ctrl_pdata->panel_data.panel_info.cont_splash_enabled)
		display_onoff_state = true;
	else
		display_onoff_state = false;
}

void mdss_dsi_panel_driver_fb_notifier_call_chain(
		struct msm_fb_data_type *mfd, int blank, bool type)
{
	struct fb_event event;

	if ((mfd->panel_info->type == MIPI_VIDEO_PANEL) ||
	    (mfd->panel_info->type == MIPI_CMD_PANEL)) {
		if (!mdss_dsi_panel_driver_is_incell_operation()) {
			event.info = mfd->fbi;
			event.data = &blank;

			if (type == FB_NOTIFIER_PRE)
				fb_notifier_call_chain(
					FB_EXT_EARLY_EVENT_BLANK, &event);
			else
				fb_notifier_call_chain(
					FB_EXT_EVENT_BLANK, &event);
		}
	}
}

void mdss_dsi_panel_driver_init(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	ctrl_pdata->spec_pdata->pcc_setup = mdss_dsi_panel_pcc_setup;
	ctrl_pdata->spec_pdata->color_mode = CLR_MODE_SELECT_DCIP3;
	ctrl_pdata->spec_pdata->esd_enable_without_xlog
				= ESD_WITHOUT_XLOG_DISABLE_VALUE;
	mdss_dsi_panel_driver_fps_data_init(FPSD);
	mdss_dsi_panel_driver_fps_data_init(VPSD);
	mdss_dsi_panel_driver_vsync_handler_init();
}

void mdss_dsi_panel_driver_off(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	struct mdss_mdp_ctl *ctl = mdata->ctl_off;

	if (ctrl_pdata->spec_pdata->black_screen_off)
		ctrl_pdata->spec_pdata->black_screen_off(ctrl_pdata);

	vs_handle.vsync_handler = (mdp_vsync_handler_t)vsync_handler;
	vs_handle.cmd_post_flush = false;
	vs_handle.enabled = true;
	if (vpsd.vps_en && (ctl->ops.remove_vsync_handler)) {
		ctl->ops.remove_vsync_handler(ctl, &vs_handle);
		vpsd.vps_en = false;
		fpsd.fpks = 0;
		pr_notice("%s: vsyncs_per_ksecs is invalid\n", __func__);
	}
	display_onoff_state = false;
}

void mdss_dsi_panel_driver_post_on(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mdss_panel_data *pdata;

	pdata = &(ctrl_pdata->panel_data);

	if (!pdata || pdata->panel_info.pdest != DISPLAY_1)
		return;

	if (ctrl_pdata->spec_pdata->fps_mode.enable)
		mdss_dsi_panel_driver_fps_mode_cmds_send(ctrl_pdata);
	else
		pr_notice("%s: change fps is not supported.\n", __func__);

	display_onoff_state = true;
}

void mdss_dsi_panel_driver_unblank(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	if (ctrl_pdata->spec_pdata->pcc_data.pcc_sts & PCC_STS_UD) {
		ctrl_pdata->spec_pdata->pcc_setup(&ctrl_pdata->panel_data);
		ctrl_pdata->spec_pdata->pcc_data.pcc_sts &= ~PCC_STS_UD;
	}
}

void mdss_dsi_panel_driver_dump_incell_sts(struct incell_ctrl *incell)
{
	int num;

	pr_err("%s: sts current:0x%x\n", __func__, (int)(incell->state));
	for (num = 0 ; num < INCELL_BACKUP_NUM ; num++)
		pr_err("%s: back ups %d :0x%x\n", __func__,
				num, (int)(incell->backups[num]));
}

void mdss_dsi_panel_driver_update_incell_bk(struct incell_ctrl *incell)
{
	int num;

	for (num = INCELL_BACKUP_NUM - 1 ; num > 0 ; num--)
		incell->backups[num] = incell->backups[num - 1];

	incell->backups[0] = incell->state;
}
