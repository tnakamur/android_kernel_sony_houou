/* drivers/video/fbdev/msm/mdss_dsi_panel_driver.h
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

#ifndef MDSS_DSI_PANEL_DRIVER_H
#define MDSS_DSI_PANEL_DRIVER_H

#include <linux/incell.h>

#include "mdss_dsi.h"
#include "mdss_fb.h"
#include "mdss_mdp.h"

/* pcc data infomation */
#define PCC_STS_UD	0x01	/* update request */
#define UNUSED				0xff
#define CLR_DATA_REG_LEN_RENE_DEFAULT	2
#define CLR_DATA_REG_LEN_NOVA_DEFAULT	1
#define CLR_DATA_REG_LEN_NOVA_AUO	3
#define CLR_DATA_REG_LEN_RENE_SR	1
enum {
	CLR_DATA_UV_PARAM_TYPE_NONE,
	CLR_DATA_UV_PARAM_TYPE_RENE_DEFAULT,
	CLR_DATA_UV_PARAM_TYPE_NOVA_DEFAULT,
	CLR_DATA_UV_PARAM_TYPE_NOVA_AUO,
	CLR_DATA_UV_PARAM_TYPE_RENE_SR
};

/* color mode */
#define CLR_MODE_SELECT_SRGB		(100)
#define CLR_MODE_SELECT_DCIP3		(101)
#define CLR_MODE_SELECT_PANELNATIVE	(102)

/* fb_notifier call type */
#define FB_NOTIFIER_PRE		((bool)true)
#define FB_NOTIFIER_POST	((bool)false)

/* esd */
#define ESD_WITHOUT_XLOG_ENABLE_VALUE	(1)
#define ESD_WITHOUT_XLOG_DISABLE_VALUE	(0)

/* touch I/F data information for incell */
/* touch I/F or not */
#define INCELL_TOUCH_RUN	((bool)true)
#define INCELL_TOUCH_IDLE	((bool)false)

/* incell status */
#define INCELL_POWER_STATE_ON	BIT(0)
#define INCELL_EWU_STATE_ON	BIT(1)
#define INCELL_LOCK_STATE_ON	BIT(2)
#define INCELL_SYSTEM_STATE_ON	BIT(3)

#define INCELL_POWER_STATE_OFF	~INCELL_POWER_STATE_ON
#define INCELL_EWU_STATE_OFF	~INCELL_EWU_STATE_ON
#define INCELL_LOCK_STATE_OFF	~INCELL_LOCK_STATE_ON
#define INCELL_SYSTEM_STATE_OFF	~INCELL_SYSTEM_STATE_ON

/* SLE000-P0: Initial setting the case of booting by Kernel */
#define INCELL_INIT_STATE_KERNEL	(0x0f \
					& INCELL_POWER_STATE_OFF \
					& INCELL_EWU_STATE_OFF \
					& INCELL_LOCK_STATE_OFF \
					& INCELL_SYSTEM_STATE_OFF)

/* SLE100-P1: Initial setting the case of booting by LK */
#define INCELL_INIT_STATE_LK	(INCELL_POWER_STATE_ON \
			       | INCELL_SYSTEM_STATE_ON)

/* The conditions of status if incell_work needed or not */
#define INCELL_WORK_NEED_P_OFF		INCELL_POWER_STATE_ON
#define INCELL_WORK_NEED_P_ON		INCELL_SYSTEM_STATE_ON
#define INCELL_WORK_NEED_P_ON_EWU	(INCELL_SYSTEM_STATE_ON \
				       | INCELL_EWU_STATE_ON)

#define INCELL_BACKUP_NUM	10

/* status to adjust power for incell panel or not */
typedef enum {
	INCELL_WORKER_OFF,
	INCELL_WORKER_PENDING,
	INCELL_WORKER_ON,
} incell_worker_state;

/*
 * Incell status change mode
 *
 * SP means the below.
 * S : System
 * P : Power
 */
typedef enum {
	INCELL_STATE_NONE,
	INCELL_STATE_S_OFF,
	INCELL_STATE_P_OFF,
	INCELL_STATE_SP_OFF,
	INCELL_STATE_S_ON,
	INCELL_STATE_P_ON,
	INCELL_STATE_SP_ON,
} incell_state_change;

/* How to send power sequence */
typedef enum {
	POWER_OFF_EXECUTE,
	POWER_OFF_SKIP,
	POWER_ON_EXECUTE,
	POWER_ON_SKIP,
	POWER_ON_EWU_SEQ,
} incell_pw_seq;

/* control parameters for incell panel */
struct incell_ctrl {
	unsigned char state;
	unsigned char backups[INCELL_BACKUP_NUM];

	incell_state_change change_state;
	incell_pw_seq seq;

	bool incell_intf_operation;
	incell_intf_mode intf_mode;

	incell_worker_state worker_state;
	struct work_struct incell_work;
};

#define DEFAULT_FPS_LOG_INTERVAL 100
#define DEFAULT_FPS_ARRAY_SIZE 120

typedef enum FPS_TYPE {
	FPSD,
	VPSD
} fps_type;

/* fps mode */
typedef enum FPS_PANEL_MODE {
	FPS_MODE_SUSRES,
	FPS_MODE_DYNAMIC,
} fps_panel_mode;

typedef enum FPS_MODE_TYPE {
	FPS_MODE_OFF_RR_OFF,
	FPS_MODE_ON_RR_OFF,
	FPS_MODE_OFF_RR_ON,
	FPS_MODE_ON_RR_ON,
	FPS_MODE_MAX,
} fps_mode_type;

struct fps_array {
	u32 frame_nbr;
	u32 time_delta;
};

struct fps_data {
	struct mutex fps_lock;
	u32 log_interval;
	u32 interval_ms;
	struct timespec timestamp_last;
	u32 frame_counter_last;
	u32 frame_counter;
	u32 fpks;
	struct timespec fpks_ts_last;
	u16 fa_last_array_pos;
	struct fps_array fa[DEFAULT_FPS_ARRAY_SIZE];
	u16 fps_array_cnt;
	bool vps_en;
};

struct mdss_dsi_fps_mode {
	bool enable;
	fps_panel_mode mode;
	fps_mode_type type;
};

struct mdss_panel_power_seq {
	int seq_num;
	int *rst_seq;
	bool rst_b_seq;

	int disp_vdd;
	int disp_vddio;
	int disp_vsp;
	int disp_vsn;
	int disp_dcdc;
	int touch_avdd;
	int touch_vddio;
	int touch_reset;
	int touch_reset_first;
	int touch_intn;
};

struct mdss_pcc_color_tbl {
	u32 color_type;
	u32 area_num;
	u32 u_min;
	u32 u_max;
	u32 v_min;
	u32 v_max;
	u32 r_data;
	u32 g_data;
	u32 b_data;
} __packed;

struct mdss_pcc_data {
	struct mdss_pcc_color_tbl *color_tbl;
	u32 tbl_size;
	u8 tbl_idx;
	u8 pcc_sts;
	u32 u_data;
	u32 v_data;
	int param_type;
};

struct mdss_panel_specific_pdata {
	int disp_vddio_gpio;
	int disp_dcdc_en_gpio;
	int touch_vddio_gpio;
	int touch_reset_gpio;
	int touch_int_gpio;

	int color_mode;
	bool pcc_enable;
	bool srgb_pcc_enable;
	bool vivid_pcc_enable;
	bool hdr_pcc_enable;
	struct dsi_panel_cmds pre_uv_read_cmds;
	struct dsi_panel_cmds uv_read_cmds;
	struct mdss_pcc_data pcc_data;
	struct mdss_pcc_data srgb_pcc_data;
	struct mdss_pcc_data vivid_pcc_data;
	struct mdss_pcc_data hdr_pcc_data;

	struct mdss_panel_power_seq on_seq;
	struct mdss_panel_power_seq off_seq;
	bool rst_after_pon;
	u32 down_period;

	struct mdss_panel_power_seq ewu_seq;

	int (*pcc_setup)(struct mdss_panel_data *pdata);

	struct mdss_dsi_fps_mode fps_mode;
	struct dsi_panel_cmds fps_cmds[FPS_MODE_MAX];

	void (*crash_counter_reset)(void);
	void (*blackscreen_det)(void);
	void (*fff_time_update)(struct mdss_panel_specific_pdata *spec_pdata);
	void (*black_screen_off)(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
	bool resume_started;

	u32 esd_enable_without_xlog;
};

void mdss_dsi_panel_driver_init(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
void mdss_dsi_panel_driver_fps_data_update(
		struct msm_fb_data_type *mfd, fps_type type);
struct fps_data mdss_dsi_panel_driver_get_fps_data(void);
struct fps_data mdss_dsi_panel_driver_get_vps_data(void);
ssize_t mdss_dsi_panel_driver_vsyncs_per_ksecs_store(struct device *dev,
			 const char *buf, size_t count);
ssize_t mdss_dsi_panel_driver_fps_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
ssize_t mdss_dsi_panel_driver_fps_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf);
void mdss_dsi_panel_driver_detection(struct platform_device *pdev,
		struct device_node **np);
int mdss_dsi_panel_driver_pinctrl_init(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int mdss_dsi_panel_driver_touch_pinctrl_set_state(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata, bool active);
int mdss_dsi_panel_driver_power_off(struct mdss_panel_data *pdata);
int mdss_dsi_panel_driver_power_on(struct mdss_panel_data *pdata);
void mdss_dsi_panel_driver_off(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
void mdss_dsi_panel_driver_post_on(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int mdss_dsi_panel_driver_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
void mdss_dsi_panel_driver_gpio_free(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
void mdss_dsi_panel_driver_parse_gpio_params(struct platform_device *ctrl_pdev,
		struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int mdss_dsi_panel_driver_reset_panel(struct mdss_panel_data *pdata,
		int enable);
int mdss_dsi_panel_driver_reset_touch(struct mdss_panel_data *pdata,
		int enable);
int mdss_dsi_panel_driver_reset_dual_display(
			struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int mdss_dsi_panel_driver_parse_dt(struct device_node *np,
		struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int mdss_dsi_panel_pcc_setup(struct mdss_panel_data *pdata);
void mdss_dsi_panel_driver_fb_notifier_call_chain(
		struct msm_fb_data_type *mfd, int blank, bool type);
void mdss_dsi_panel_driver_check_splash_enable(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata);
void mdss_dsi_panel_driver_unblank(struct mdss_dsi_ctrl_pdata *ctrl_pdata);

/* For incell driver */
struct incell_ctrl *incell_get_info(void);
void incell_panel_power_worker_canceling(struct incell_ctrl *incell);
void incell_driver_init(void);
bool mdss_dsi_panel_driver_is_power_lock(unsigned char state);
bool mdss_dsi_panel_driver_is_power_on(unsigned char state);
bool mdss_dsi_panel_driver_is_ewu(unsigned char state);
bool mdss_dsi_panel_driver_is_system_on(unsigned char state);
void mdss_dsi_panel_driver_state_change_off(struct incell_ctrl *incell);
void mdss_dsi_panel_driver_power_off_ctrl(struct incell_ctrl *incell);
void mdss_dsi_panel_driver_state_change_on(struct incell_ctrl *incell);
void mdss_dsi_panel_driver_power_on_ctrl(struct incell_ctrl *incell);
struct mdss_panel_specific_pdata *mdss_panel2spec_pdata(
	struct mdss_panel_data *pdata);
void mdss_dsi_panel_driver_dump_incell_sts(struct incell_ctrl *incell);
void mdss_dsi_panel_driver_update_incell_bk(struct incell_ctrl *incell);
int mdss_dsi_panel_driver_reset_touch_ctrl(struct mdss_panel_data *pdata, bool en);

/* Qualcomm original function */
int mdss_dsi_pinctrl_set_state(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
		bool active);
int mdss_dsi_request_gpios(struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int mdss_dsi_parse_dcs_cmds(struct device_node *np,
		struct dsi_panel_cmds *pcmds, char *cmd_key, char *link_key);
void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
			      struct dsi_panel_cmds *pcmds, u32 flags);

static inline struct mdss_dsi_ctrl_pdata *mdss_dsi_get_master_ctrl(
					struct mdss_panel_data *pdata)
{
	int dsi_master = DSI_CTRL_0;

	if (pdata->panel_info.dsi_master == DISPLAY_2)
		dsi_master = DSI_CTRL_1;
	else
		dsi_master = DSI_CTRL_0;

	return mdss_dsi_get_ctrl_by_index(dsi_master);
}

#endif /* MDSS_DSI_PANEL_DRIVER_H */
