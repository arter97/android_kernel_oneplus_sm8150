 /***********************************************************
 * Description : OnePlus touchpanel driver
 * 
 * File        : touchpanel_common.h      
 *
 * Function    : touchpanel public interface  
 * 
 * Version     : V1.0 
 *
 ***********************************************************/
#ifndef _TOUCHPANEL_COMMON_H_
#define _TOUCHPANEL_COMMON_H_

/*********PART1:Head files**********************/
#include <linux/input/mt.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/oneplus/boot_mode.h>
#include <linux/workqueue.h>

#include "util_interface/touch_interfaces.h"
#include "tp_devices.h"

#define EFTM (250)
#define FW_UPDATE_COMPLETE_TIMEOUT  msecs_to_jiffies(40*1000)

/*********PART2:Define Area**********************/
#define TPD_USE_EINT
#define TYPE_B_PROTOCOL

#define PAGESIZE 512
#define MAX_GESTURE_COORD 6

#define UnkownGesture       0
#define DouTap              1	// double tap
#define UpVee               2	// V
#define DownVee             3	// ^
#define LeftVee             4	// >
#define RightVee            5	// <
#define Circle              6	// O
#define DouSwip             7	// ||
#define Left2RightSwip      8	// -->
#define Right2LeftSwip      9	// <--
#define Up2DownSwip         10	// |v
#define Down2UpSwip         11	// |^
#define Mgestrue            12	// M
#define Wgestrue            13	// W
#define SingleTap           15	// single tap
#define Sgestrue            14	// S

#define KEY_GESTURE_W               246
#define KEY_GESTURE_M               247
#define KEY_GESTURE_S               248
#define KEY_DOUBLE_TAP              KEY_WAKEUP
#define KEY_GESTURE_CIRCLE          250
#define KEY_GESTURE_TWO_SWIPE       251
#define KEY_GESTURE_UP_ARROW        252
#define KEY_GESTURE_LEFT_ARROW      253
#define KEY_GESTURE_RIGHT_ARROW     254
#define KEY_GESTURE_DOWN_ARROW      255
#define KEY_GESTURE_SWIPE_LEFT      KEY_F5
#define KEY_GESTURE_SWIPE_DOWN      KEY_F6
#define KEY_GESTURE_SWIPE_RIGHT     KEY_F7
#define KEY_GESTURE_SWIPE_UP        KEY_F8
#define KEY_GESTURE_SINGLE_TAP      KEY_F9

#define BIT0 (0x1 << 0)
#define BIT1 (0x1 << 1)
#define BIT2 (0x1 << 2)
#define BIT3 (0x1 << 3)
#define BIT4 (0x1 << 4)
#define BIT5 (0x1 << 5)
#define BIT6 (0x1 << 6)
#define BIT7 (0x1 << 7)

/* bit operation */
#define SET_BIT(data, flag) ((data) |= (flag))
#define CLR_BIT(data, flag) ((data) &= ~(flag))
#define CHK_BIT(data, flag) ((data) & (flag))
#define VK_TAB {KEY_MENU, KEY_HOMEPAGE, KEY_BACK, KEY_SEARCH}

#define TOUCH_BIT_CHECK           0x3FF	//max support 10 point report.using for detect non-valid points
#define MAX_FW_NAME_LENGTH        60
#define MAX_EXTRA_NAME_LENGTH     60
#define MAX_LIMIT_DATA_LENGTH 	  60

#define MAX_DEVICE_VERSION_LENGTH 16
#define MAX_DEVICE_MANU_LENGTH    16

#define SYNAPTICS_PREFIX    "SY_"
#define GOODIX_PREFIX       "GT_"
#define FOCAL_PREFIX        "FT_"

#define FW_UPDATE_DELAY        msecs_to_jiffies(2*1000)

/*********PART3:Struct Area**********************/
typedef enum {
	TYPE_PROPERTIES = 1,	/*using board_properties */
	TYPE_AREA_SEPRATE,	/*using same IC (button zone &&  touch zone are seprate) */
	TYPE_DIFF_IC,		/*using diffrent IC (button zone &&  touch zone are seprate) */
	TYPE_NO_NEED,		/*No need of virtual key process */
} vk_type;

typedef enum {
	AREA_NOTOUCH,
	AREA_EDGE,
	AREA_CRITICAL,
	AREA_NORMAL,
	AREA_CORNER,
} touch_area;

typedef enum {
	MODE_NORMAL,
	MODE_SLEEP,
	MODE_EDGE,
	MODE_GESTURE,
	MODE_CHARGE,
	MODE_GAME,
	MODE_PALM_REJECTION,
	MODE_FACE_DETECT,
	MODE_FACE_CALIBRATE,
	MODE_REFRESH_SWITCH,
	MODE_TOUCH_HOLD,
	MODE_TOUCH_AREA_SWITCH,
	MODE_LIMIT_SWITCH,
	MODE_GESTURE_SWITCH,
	MODE_FINGERPRINT_TEST,
} work_mode;

typedef enum {
	FW_NORMAL,		/*fw might update, depend on the fw id */
	FW_ABNORMAL,		/*fw abnormal, need update */
} fw_check_state;

typedef enum {
	FW_UPDATE_SUCCESS,
	FW_NO_NEED_UPDATE,
	FW_UPDATE_ERROR,
	FW_UPDATE_FATAL,
} fw_update_state;

typedef enum {
	TP_SUSPEND_EARLY_EVENT,
	TP_SUSPEND_COMPLETE,
	TP_RESUME_EARLY_EVENT,
	TP_RESUME_COMPLETE,
	TP_SPEEDUP_RESUME_COMPLETE,
} suspend_resume_state;

typedef enum IRQ_TRIGGER_REASON {
	IRQ_TOUCH = 0x01,
	IRQ_GESTURE = 0x02,
	IRQ_BTN_KEY = 0x04,
	IRQ_EXCEPTION = 0x08,
	IRQ_FW_CONFIG = 0x10,
	IRQ_FW_AUTO_RESET = 0x40,
	IRQ_FACE_STATE = 0x80,
	IRQ_IGNORE = 0x00,
} irq_reason;

typedef enum vk_bitmap {
	BIT_reserve = 0x08,
	BIT_BACK = 0x04,
	BIT_HOME = 0x02,
	BIT_MENU = 0x01,
} vk_bitmap;

typedef enum resume_order {
	TP_LCD_RESUME,
	LCD_TP_RESUME,
} tp_resume_order;

typedef enum suspend_order {
	TP_LCD_SUSPEND,
	LCD_TP_SUSPEND,
} tp_suspend_order;

struct Coordinate {
	int x;
	int y;
};

typedef enum interrupt_mode {
	BANNABLE,
	UNBANNABLE,
	INTERRUPT_MODE_MAX,
} tp_interrupt_mode;

typedef enum switch_mode_type {
	SEQUENCE,
	SINGLE,
} tp_switch_mode;

struct gesture_info {
	uint32_t gesture_type;
	uint32_t clockwise;
	struct Coordinate Point_start;
	struct Coordinate Point_end;
	struct Coordinate Point_1st;
	struct Coordinate Point_2nd;
	struct Coordinate Point_3rd;
	struct Coordinate Point_4th;
};

struct point_info {
	uint16_t x;
	uint16_t y;
	uint16_t z;
	uint8_t width_major;
	uint8_t touch_major;
	uint8_t status;
	touch_area type;
};

/* add haptic audio tp mask */
struct shake_point {
	uint16_t x;
	uint16_t y;
	uint8_t status;
};

struct corner_info {
	uint8_t id;
	bool flag;
	struct point_info point;
};

struct manufacture_info {
	char *version;
	char *manufacture;
};

struct panel_info {
	char *fw_name;		/*FW name */
	char *test_limit_name;	/*test limit name */
	const char *chip_name;	/*chip name the panel is controlled by */
	const char *project_name;	/*project_name */
	uint32_t TP_FW;		/*FW Version Read from IC */
	tp_dev tp_type;
	struct manufacture_info manufacture_info;	/*touchpanel device info */
};

struct hw_resource {
	//gpio
	int id1_gpio;
	int id2_gpio;
	int id3_gpio;

	int irq_gpio;		/*irq GPIO num */
	int reset_gpio;		/*Reset GPIO */

	int enable2v8_gpio;	/*vdd_2v8 enable GPIO */
	int enable1v8_gpio;	/*vcc_1v8 enable GPIO */

	//TX&&RX Num
	int TX_NUM;
	int RX_NUM;
	int key_TX;		/*the tx num occupied by touchkey */
	int key_RX;		/*the rx num occupied by touchkey */

	//power
	struct regulator *vdd_2v8;	/*power 2v8 */
	struct regulator *vcc_1v8;	/*power 1v8 */
	uint32_t vdd_volt;	/*avdd specific volt */

	//pinctrl
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_set_high;
	struct pinctrl_state *pin_set_low;
};

struct touch_major_limit {
	int width_range;
	int height_range;
};

struct button_map {
	int width_x;		/*width of each key area */
	int height_y;		/*height of each key area */
	struct Coordinate coord_menu;	/*Menu centre coordinates */
	struct Coordinate coord_home;	/*Home centre coordinates */
	struct Coordinate coord_back;	/*Back centre coordinates */
};

struct resolution_info {
	uint32_t max_x;		/*touchpanel width */
	uint32_t max_y;		/*touchpanel height */
	uint32_t LCD_WIDTH;	/*LCD WIDTH        */
	uint32_t LCD_HEIGHT;	/*LCD HEIGHT       */
};

struct register_info {
	uint8_t reg_length;
	uint16_t reg_addr;
	uint8_t *reg_result;
};

struct black_gesture_test {
	bool flag;		/* indicate do black gesture test or not */
	char *message;		/* failure information if gesture test failed */
};

struct touchpanel_data {
	bool black_gesture_support;	/*black_gesture support feature */
	bool game_switch_support;	/*indicate game switch support or not */
	bool face_detect_support;	/*touch porximity function */
	bool lcd_refresh_rate_switch;	/*switch lcd refresh rate 60-90hz */
	bool touch_hold_support;	/*touchhold function for fingerprint */
	bool ctl_base_address;	/*change only for 18865 */
	bool tx_change_order;	/*add for dvt change tx order */

	bool charge_detect;
	bool charge_detect_support;
	bool module_id_support;	/*update firmware by lcd module id */
	bool i2c_ready;		/*i2c resume status */
	bool loading_fw;	/*touchpanel FW updating */
	bool is_incell_panel;	/*touchpanel is incell */
	bool is_noflash_ic;	/*noflash ic */
	bool has_callback;	/*whether have callback method to invoke common */
	bool use_resume_notify;	/*notify speed resume process */
	bool fw_update_app_support;	/*bspFwUpdate is used */
	bool in_test_process;	/*flag whether in test process */
	u8 vk_bitmap;		/*every bit declear one state of key "reserve(keycode)|home(keycode)|menu(keycode)|back(keycode)" */
	vk_type vk_type;	/*virtual_key type */

	uint32_t irq_flags;	/*irq setting flag */
	int irq;		/*irq num */

	int gesture_enable;	/*control state of black gesture */
	int fd_enable;
	int fd_calibrate;
	int lcd_refresh_rate;
	int touch_hold_enable;
	int touch_count;
	int glove_enable;	/*control state of glove gesture */
	int limit_enable;	/*control state of limit ebale */
	int is_suspended;	/*suspend/resume flow exec flag */
	int corner_delay_up;	/*corner mode flag */
	suspend_resume_state suspend_state;	/*detail suspend/resume state */

	int boot_mode;		/*boot up mode */
	int view_area_touched;	/*view area touched flag */
	int force_update;	/*force update flag */
	int max_num;		/*max muti-touch num supportted */
	int irq_slot;		/*debug use, for print all finger's first touch log */
	int firmware_update_type;	/*firmware_update_type: 0=check firmware version 1=force update; 2=for FAE debug */

	tp_resume_order tp_resume_order;
	tp_suspend_order tp_suspend_order;
	tp_interrupt_mode int_mode;	/*whether interrupt and be disabled */
	tp_switch_mode mode_switch_type;	/*used for switch mode */
	bool skip_reset_in_resume;	/*some incell ic is reset by lcd reset */
	bool skip_suspend_operate;	/*LCD and TP is in one chip,lcd power off in suspend at first,
					   can not operate i2c when tp suspend */
	bool ps_status;		/*save ps status, ps near = 1, ps far = 0 */
	int noise_level;	/*save ps status, ps near = 1, ps far = 0 */
	bool gesture_switch;	/*gesture mode close or open gesture */
	bool reject_point;	/*reject point for sensor */
	u8 limit_switch;	/*0 is phone up 1 is crosswise */
	u8 touchold_event;	/*0 is touchhold down 1 is up */

#if defined(TPD_USE_EINT)
	struct hrtimer timer;	/*using polling instead of IRQ */
#endif
#if defined(CONFIG_FB)
	struct notifier_block fb_notif;	/*register to control suspend/resume */
#endif

	struct mutex mutex;	/*mutex for lock i2c related flow */
	struct completion pm_complete;	/*completion for control suspend and resume flow */
	struct completion fw_complete;	/*completion for control fw update */
	struct completion resume_complete;	/*completion for control fw update */
	struct panel_info panel_data;	/*GPIO control(id && pinctrl && tp_type) */
	struct hw_resource hw_res;	/*hw resourc information */
	struct button_map button_map;	/*virtual_key button area */
	struct resolution_info resolution_info;	/*resolution of touchpanel && LCD */
	struct gesture_info gesture;	/*gesture related info */
	struct touch_major_limit touch_major_limit;	/*used for control touch major reporting area */

	struct work_struct speed_up_work;	/*using for speedup resume */
	struct workqueue_struct *speedup_resume_wq;	/*using for touchpanel speedup resume wq */

	struct work_struct read_delta_work;	/*using for read delta */
	struct workqueue_struct *delta_read_wq;

	struct work_struct async_work;
	struct workqueue_struct *async_workqueue;
	struct work_struct fw_update_work;	/*using for fw update */
	struct wakeup_source source;

	struct device *dev;	/*used for i2c->dev */
	struct i2c_client *client;
	struct spi_device *s_client;
	struct input_dev *input_dev;
	struct input_dev *kpd_input_dev;

	struct touchpanel_operations *ts_ops;	/*call_back function */
	struct proc_dir_entry *prEntry_tp;	/*struct proc_dir_entry of "/proc/touchpanel" */
	struct register_info reg_info;	/*debug node for register length */

	void *chip_data;	/*Chip Related data */
	void *private_data;	/*Reserved Private data */
};

struct touchpanel_operations {
	int (*get_chip_info) (void *chip_data);	/*return 0:success;other:failed */
	int (*mode_switch) (void *chip_data, work_mode mode, bool flag);	/*return 0:success;other:failed */
	int (*get_touch_points) (void *chip_data, struct point_info * points, int max_num);	/*return point bit-map */
	int (*get_gesture_info) (void *chip_data, struct gesture_info * gesture);	/*return 0:success;other:failed */
	int (*get_vendor) (void *chip_data, struct panel_info * panel_data);	/*distingush which panel we use, (TRULY/OFLIM/BIEL/TPK) */
	int (*reset) (void *chip_data);	/*Reset Touchpanel */
	int (*reinit_device) (void *chip_data);
	 fw_check_state(*fw_check) (void *chip_data, struct resolution_info * resolution_info, struct panel_info * panel_data);	/*return < 0 :failed; 0 sucess */
	 fw_update_state(*fw_update) (void *chip_data, const struct firmware * fw, bool force);	/*return 0 normal; return -1:update failed; */
	int (*power_control) (void *chip_data, bool enable);	/*return 0:success;other:abnormal, need to jump out */
	 u8(*trigger_reason) (void *chip_data, int gesture_enable, int is_suspended);	/*clear innterrupt reg && detect irq trigger reason */
	 u8(*get_keycode) (void *chip_data);	/*get touch-key code */
	int (*fw_handle) (void *chip_data);	/*return 0 normal; return -1:update failed; */
	void (*resume_prepare) (void *chip_data);	/*using for operation before resume flow,
							   eg:incell 3320 need to disable gesture to release inter pins for lcd resume */
	void (*exit_esd_mode) (void *chip_data);	/*add for s4322 exit esd mode */
	void (*register_info_read) (void *chip_data, uint16_t register_addr, uint8_t * result, uint8_t length);	/*add for read registers */
	void (*write_ps_status) (void *chip_data, int ps_status);	/*when detect iron plate, if ps is near ,enter iron plate mode;if ps is far, can not enter; exit esd mode when ps is far */
	void (*specific_resume_operate) (void *chip_data);	/*some ic need specific opearation in resuming */
	int (*get_usb_state) (void);	/*get current usb state */
	int (*irq_handle_unlock) (void *chip_info);	/*irq handler without mutex */
	int (*async_work) (void *chip_info);	/*async work */
	int (*get_face_state) (void *chip_info);	/*get face detect state */
};

struct invoke_method {
	void (*invoke_common) (void);
	void (*async_work) (void);
};

/*********PART3:function or variables for other files**********************/
struct touchpanel_data *common_touch_data_alloc(void);

int common_touch_data_free(struct touchpanel_data *pdata);
int register_common_touch_device(struct touchpanel_data *pdata);

void tp_i2c_suspend(struct touchpanel_data *ts);
void tp_i2c_resume(struct touchpanel_data *ts);

int tp_powercontrol_1v8(struct hw_resource *hw_res, bool on);
int tp_powercontrol_2v8(struct hw_resource *hw_res, bool on);

void tp_touch_btnkey_release(void);
extern bool tp_judge_ic_match(char *tp_ic_name);
/* add haptic audio tp mask */
extern int msm_drm_notifier_call_chain(unsigned long val, void *v);
extern int gf_opticalfp_irq_handler(int event);

#endif
