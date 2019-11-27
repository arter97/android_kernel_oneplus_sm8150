#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/task_work.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_FB
#include <linux/fb.h>
#include <linux/notifier.h>
#endif

#include "sec_drivers_s6sy761.h"

static struct dma_buf_s6sy761 *dma_buffer;
extern int tp_register_times;
extern struct touchpanel_data *g_tp;
/****************** Start of Log Tag Declear and level define*******************************/
#define TPD_DEVICE "sec-s6sy761"
// Even TPD_INFO is too spammy
#define TPD_INFO(a, arg...)		pr_debug("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG(a, arg...)		pr_debug("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DEBUG_NTAG(a, arg...)	pr_debug("[TP]"TPD_DEVICE ": " a, ##arg)
#define TPD_DETAIL(a, arg...)		pr_debug("[TP]"TPD_DEVICE ": " a, ##arg)
/******************** End of Log Tag Declear and level define*********************************/

/*************************** start of function delcare****************************************/
static inline void sec_mdelay(unsigned int ms)
{
	if (ms < 20)
		usleep_range(ms * 1000, ms * 1010);
	else
		msleep(ms);
}

static int sec_reset(void *chip_data);
static int sec_power_control(void *chip_data, bool enable);

/**************************** end of function delcare*****************************************/

/****** Start of other functions that work for touchpanel_operations callbacks***********/
static int sec_enable_black_gesture(struct chip_data_s6sy761 *chip_info,
				    bool enable)
{
	int ret = -1;
	int i = 0;

	TPD_INFO("%s, enable = %d\n", __func__, enable);

	if (enable) {
		for (i = 0; i < 20; i++) {
			touch_i2c_write_word(chip_info->client,
					     SEC_CMD_WAKEUP_GESTURE_MODE,
					     0xFFFF);
			touch_i2c_write_byte(chip_info->client,
					     SEC_CMD_SET_POWER_MODE, 0x01);
			sec_mdelay(10);
			ret =
			    touch_i2c_read_byte(chip_info->client,
						SEC_CMD_SET_POWER_MODE);
			if (0x01 == ret)
				break;
		}
	} else {
		for (i = 0; i < 20; i++) {
			touch_i2c_write_word(chip_info->client,
					     SEC_CMD_WAKEUP_GESTURE_MODE,
					     0x0000);
			touch_i2c_write_byte(chip_info->client,
					     SEC_CMD_SET_POWER_MODE, 0x00);
			sec_mdelay(10);
			ret =
			    touch_i2c_read_byte(chip_info->client,
						SEC_CMD_SET_POWER_MODE);
			if (0x00 == ret)
				break;
		}
		return 0;
	}

	if (i >= 5) {
		ret = -1;
		TPD_INFO("%s: enter black gesture failed\n", __func__);
	} else {
		TPD_INFO("%s: %d times enter black gesture success\n", __func__,
			 i);
	}
	return ret;
}

static int sec_enable_edge_limit(struct chip_data_s6sy761 *chip_info,
				 bool enable)
{
	int ret = -1;

	if (enable) {
		ret =
		    touch_i2c_write_word(chip_info->client, SEC_CMD_GRIP_SWITCH,
					 0x2000);
	} else {
		ret =
		    touch_i2c_write_word(chip_info->client, SEC_CMD_GRIP_SWITCH,
					 0x0000);
	}

	TPD_INFO("%s: state: %d %s!\n", __func__, enable,
		 ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_enable_charge_mode(struct chip_data_s6sy761 *chip_info,
				  bool enable)
{
	int ret = -1;

	if (enable) {
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SET_CMD_SET_CHARGER_MODE, 0x02);
	} else {
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SET_CMD_SET_CHARGER_MODE, 0x01);
	}

	TPD_INFO("%s: state: %d %s!\n", __func__, enable,
		 ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_enable_face_mode(struct chip_data_s6sy761 *chip_info,
				bool enable)
{
	int ret = -1;

	if (enable) {
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_HOVER_DETECT, 1);
	} else {
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_HOVER_DETECT, 0);
	}

	TPD_INFO("%s: state: %d %s!\n", __func__, enable,
		 ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_face_reduce_mode(struct chip_data_s6sy761 *chip_info,
				bool enable)
{
	int ret = -1;

	if (enable) {
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_TOUCHHOLD_CALIBRATE, 1);
	}

	TPD_INFO("%s: state: %d %s!\n", __func__, enable,
		 ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_enable_palm_reject(struct chip_data_s6sy761 *chip_info,
				  bool enable)
{
	int ret = -1;

	if (enable) {
		ret =
		    touch_i2c_write_word(chip_info->client, SEC_CMD_PALM_SWITCH,
					 0x0061);
	} else {
		ret =
		    touch_i2c_write_word(chip_info->client, SEC_CMD_PALM_SWITCH,
					 0x0041);
	}

	TPD_INFO("%s: state: %d %s!\n", __func__, enable,
		 ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_enable_game_mode(struct chip_data_s6sy761 *chip_info,
				bool enable)
{
	int ret = -1;

	if (enable)
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_GAME_FAST_SLIDE, 1);
	else
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_GAME_FAST_SLIDE, 0);

	TPD_INFO("%s: state: %d %s!\n", __func__, enable,
		 ret < 0 ? "failed" : "success");

	return ret;
}

static int sec_refresh_switch_mode(struct chip_data_s6sy761 *chip_info,
				   bool enable)
{
	int ret = -1;

	if (enable) {
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_REFRESH_RATE_SWITCH, 0x5A);
	} else {
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_REFRESH_RATE_SWITCH, 0x3C);
	}
	TPD_INFO("%s: refresh_switch: %s %s!\n", __func__,
		 enable == 0 ? "60HZ" : "90HZ", ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_touchhold_switch_mode(struct chip_data_s6sy761 *chip_info,
				     bool enable)
{
	int ret = -1;
	int i = 0;

	if (enable == 1) {
		for (i = 0; i < 10; i++) {
			ret =
			    touch_i2c_read_byte(chip_info->client,
						SEC_CMD_TOUCHHOLD_SWITCH);
			ret |= 0x01;
			ret =
			    touch_i2c_write_byte(chip_info->client,
						 SEC_CMD_TOUCHHOLD_SWITCH, ret);
			sec_mdelay(10);
			ret =
			    touch_i2c_read_byte(chip_info->client,
						SEC_CMD_TOUCHHOLD_SWITCH);
			if (ret == 1)
				break;
		}
	} else if (enable == 0) {
		ret =
		    touch_i2c_read_byte(chip_info->client,
					SEC_CMD_TOUCHHOLD_SWITCH);
		ret &= 0xFE;
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_TOUCHHOLD_SWITCH, ret);
	}
	TPD_INFO("%s: touchhold_enable: %d %s!\n", __func__, enable,
		 ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_toucharea_switch_mode(struct chip_data_s6sy761 *chip_info,
				     bool enable)
{
	int ret = -1;

	if (enable) {
		ret =
		    touch_i2c_read_byte(chip_info->client,
					SEC_CMD_TOUCHHOLD_SWITCH);
		ret |= 0x02;
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_TOUCHHOLD_SWITCH, ret);
		TPD_INFO("%s:cmd = 0x%x, touch area switch qualcom %s\n",
			 __func__, ret, ret < 0 ? "failed" : "success");
	} else {
		ret =
		    touch_i2c_read_byte(chip_info->client,
					SEC_CMD_TOUCHHOLD_SWITCH);
		ret &= 0xFD;
		ret =
		    touch_i2c_write_byte(chip_info->client,
					 SEC_CMD_TOUCHHOLD_SWITCH, ret);
		TPD_INFO("%s:cmd = 0x%x, touch area switch goodix %s\n",
			 __func__, ret, ret < 0 ? "failed" : "success");
	}

	return ret;
}

static int sec_limit_switch_mode(struct chip_data_s6sy761 *chip_info,
				 bool enable)
{
	int ret = -1;
	unsigned char buf[4] = { 0 };
	unsigned char cmd[3] = { 0 };

	if (g_tp->limit_switch == 1) {	//LANDSPACE
		ret = touch_i2c_write_byte(chip_info->client, 0x4B, 0);	//close wet mode
		cmd[0] = 0x01;
		ret = touch_i2c_write_block(chip_info->client, SEC_CMD_GRIPMODE_SWITCH, 3, cmd);	//change mode
		buf[1] = 0x64;
		buf[3] = 0x64;
		ret =
		    touch_i2c_write_block(chip_info->client,
					  SEC_CMD_LANDSPACE_CORNER, 4, buf);
	} else if (g_tp->limit_switch == 3) {
		ret = touch_i2c_write_byte(chip_info->client, 0x4B, 0);
		cmd[0] = 0x02;
		ret =
		    touch_i2c_write_block(chip_info->client,
					  SEC_CMD_GRIPMODE_SWITCH, 3, cmd);
		buf[1] = 0x64;
		buf[3] = 0x64;
		ret =
		    touch_i2c_write_block(chip_info->client,
					  SEC_CMD_LANDSPACE_CORNER, 4, buf);
	} else {		//portrait
		ret = touch_i2c_write_byte(chip_info->client, 0x4B, 1);	//open wet mode
		cmd[0] = 0;
		ret =
		    touch_i2c_write_block(chip_info->client,
					  SEC_CMD_GRIPMODE_SWITCH, 3, cmd);
		buf[1] = 0x64;
		buf[3] = 0x64;
		ret =
		    touch_i2c_write_block(chip_info->client,
					  SEC_CMD_PORTRAIT_CORNER, 4, buf);
	}
	return ret;

}

static int sec_gesture_switch_mode(struct chip_data_s6sy761 *chip_info,
				   bool enable)
{
	int ret = -1;

	if (enable) {
		ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_DISABLE_GESTURE_MODE, 1);	//disable gesture
	} else {
		ret = touch_i2c_write_byte(chip_info->client, SEC_CMD_DISABLE_GESTURE_MODE, 0);	//enable gesture
	}

	TPD_INFO("%s: gesture_switch: %s %s!\n", __func__,
		 enable == 0 ? "enable" : "disable",
		 ret < 0 ? "failed" : "success");
	return ret;
}

static int sec_wait_for_ready(struct chip_data_s6sy761 *chip_info, unsigned int ack)
{
	int rc = -1;
	int retry = 0, retry_cnt = 100;
	int8_t status = -1;
	u8 *tBuff = dma_buffer->tBuff;

	while (touch_i2c_read_block
	       (chip_info->client, SEC_READ_ONE_EVENT, SEC_EVENT_BUFF_SIZE,
		tBuff) > 0) {
		status = (tBuff[0] >> 2) & 0xF;
		if ((status == TYPE_STATUS_EVENT_INFO)
		    || (status == TYPE_STATUS_EVENT_VENDOR_INFO)) {
			if (tBuff[1] == ack) {
				rc = 0;
				break;
			}
		}

		if (retry++ > retry_cnt) {
			TPD_INFO
			    ("%s: Time Over, event_buf: %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X \n",
			     __func__, tBuff[0], tBuff[1], tBuff[2], tBuff[3],
			     tBuff[4], tBuff[5], tBuff[6], tBuff[7]);
			status =
			    touch_i2c_read_byte(chip_info->client,
						SEC_READ_BOOT_STATUS);
			if (status == SEC_STATUS_BOOT_MODE) {
				TPD_INFO
				    ("%s: firmware in bootloader mode,boot failed\n",
				     __func__);
			}
			break;
		}
		sec_mdelay(20);
	}

	return rc;
}

static int sec_enter_fw_mode(struct chip_data_s6sy761 *chip_info)
{
	int ret = -1;
	u8 *device_id = dma_buffer->device_id;
	u8 fw_update_mode_passwd[] = { 0x55, 0xAC };

	ret =
	    touch_i2c_write_block(chip_info->client, SEC_CMD_ENTER_FW_MODE,
				  sizeof(fw_update_mode_passwd),
				  fw_update_mode_passwd);
	sec_mdelay(20);
	if (ret < 0) {
		TPD_INFO("%s: write cmd to enter fw mode failed\n", __func__);
		return -1;
	}
	//need soft reset or hard reset
	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
		gpio_direction_output(chip_info->hw_res->reset_gpio, false);
		sec_mdelay(10);
		gpio_direction_output(chip_info->hw_res->reset_gpio, true);
	} else {
		ret =
		    touch_i2c_write_block(chip_info->client, SEC_CMD_SOFT_RESET,
					  0, NULL);
		if (ret < 0) {
			TPD_INFO("%s: write soft reset failed\n", __func__);
			return -1;
		}
	}
	sec_mdelay(100);

	ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);	//after reset, check bootloader again
	if (ret < 0) {
		TPD_INFO("%s: read boot status failed\n", __func__);
		return -1;
	}
	if (ret != SEC_STATUS_BOOT_MODE) {
		TPD_INFO("%s: read boot status, but no in boot mode(%d)\n",
			 __func__, ret);
		return -1;
	}

	sec_mdelay(10);

	ret =
	    touch_i2c_read_block(chip_info->client, SEC_READ_ID, 3, device_id);
	if (ret < 0) {
		TPD_INFO("%s: read 3 byte device id failed\n", __func__);
		return -1;
	}

	chip_info->boot_ver[0] = device_id[0];
	chip_info->boot_ver[1] = device_id[1];
	chip_info->boot_ver[2] = device_id[2];
	chip_info->flash_page_size = SEC_FW_BLK_DEFAULT_SIZE;
	if ((device_id[1] == 0x37) && (device_id[2] == 0x61))
		chip_info->flash_page_size = 512;

	return 0;
}

static u8 sec_checksum(u8 * data, int offset, int size)
{
	int i;
	u8 checksum = 0;

	for (i = 0; i < size; i++)
		checksum += data[i + offset];

	return checksum;
}

static int sec_flash_page_erase(struct chip_data_s6sy761 *chip_info,
				u32 page_idx, u32 page_num)
{
	int ret = -1;
	u8 tCmd[6];

	tCmd[0] = SEC_CMD_FLASH_ERASE;
	tCmd[1] = (u8) ((page_idx >> 8) & 0xFF);
	tCmd[2] = (u8) ((page_idx >> 0) & 0xFF);
	tCmd[3] = (u8) ((page_num >> 8) & 0xFF);
	tCmd[4] = (u8) ((page_num >> 0) & 0xFF);
	tCmd[5] = sec_checksum(tCmd, 1, 4);

	ret = touch_i2c_write(chip_info->client, tCmd, 6);

	return ret;
}

static int sec_flash_page_write(struct chip_data_s6sy761 *chip_info,
				u32 page_idx, u8 * page_data)
{
	int ret;
	u8 tCmd[1 + 2 + SEC_FW_BLK_SIZE_MAX + 1];
	int flash_page_size = (int)chip_info->flash_page_size;

	tCmd[0] = SEC_CMD_FLASH_WRITE;
	tCmd[1] = (u8) ((page_idx >> 8) & 0xFF);
	tCmd[2] = (u8) ((page_idx >> 0) & 0xFF);

	memcpy(&tCmd[3], page_data, flash_page_size);
	tCmd[1 + 2 + flash_page_size] =
	    sec_checksum(tCmd, 1, 2 + flash_page_size);

	ret =
	    touch_i2c_write(chip_info->client, tCmd,
			    1 + 2 + flash_page_size + 1);
	return ret;
}

static bool sec_limited_flash_page_write(struct chip_data_s6sy761 *chip_info,
					 u32 page_idx, u8 * page_data)
{
	int ret = -1;
	u8 *tCmd = NULL;
	u8 copy_data[3 + SEC_FW_BLK_SIZE_MAX];
	int copy_left = (int)chip_info->flash_page_size + 3;
	int copy_size = 0;
	int copy_max = I2C_BURSTMAX - 1;
	int flash_page_size = (int)chip_info->flash_page_size;

	copy_data[0] = (u8) ((page_idx >> 8) & 0xFF);	/* addH */
	copy_data[1] = (u8) ((page_idx >> 0) & 0xFF);	/* addL */

	memcpy(&copy_data[2], page_data, flash_page_size);	/* DATA */
	copy_data[2 + flash_page_size] = sec_checksum(copy_data, 0, 2 + flash_page_size);	/* CS */

	while (copy_left > 0) {
		int copy_cur = (copy_left > copy_max) ? copy_max : copy_left;

		tCmd = kzalloc(copy_cur + 1, GFP_KERNEL);
		if (!tCmd)
			goto err_write;

		if (copy_size == 0)
			tCmd[0] = SEC_CMD_FLASH_WRITE;
		else
			tCmd[0] = SEC_CMD_FLASH_PADDING;

		memcpy(&tCmd[1], &copy_data[copy_size], copy_cur);

		ret = touch_i2c_write(chip_info->client, tCmd, 1 + copy_cur);
		if (ret < 0) {
			ret =
			    touch_i2c_write(chip_info->client, tCmd,
					    1 + copy_cur);
			if (ret < 0) {
				TPD_INFO("%s: failed, ret:%d\n", __func__, ret);
			}
		}

		copy_size += copy_cur;
		copy_left -= copy_cur;
		kfree(tCmd);
	}
	return ret;

 err_write:
	TPD_INFO("%s: failed to alloc.\n", __func__);
	return -ENOMEM;

}

static int sec_flash_write(struct chip_data_s6sy761 *chip_info, u32 mem_addr,
			   u8 * mem_data, u32 mem_size)
{
	int ret = -1;
	u32 page_idx = 0, size_copy = 0, flash_page_size = 0;
	u32 page_idx_start = 0, page_idx_end = 0, page_num = 0;
	u8 page_buf[SEC_FW_BLK_SIZE_MAX];

	if (mem_size == 0)
		return 0;

	flash_page_size = chip_info->flash_page_size;
	page_idx_start = mem_addr / flash_page_size;
	page_idx_end = (mem_addr + mem_size - 1) / flash_page_size;
	page_num = page_idx_end - page_idx_start + 1;

	ret = sec_flash_page_erase(chip_info, page_idx_start, page_num);
	if (ret < 0) {
		TPD_INFO("%s: fw erase failed, mem_addr= %08X, pagenum = %d\n",
			 __func__, mem_addr, page_num);
		return -EIO;
	}

	sec_mdelay(page_num + 10);

	size_copy = mem_size % flash_page_size;
	if (size_copy == 0)
		size_copy = flash_page_size;

	memset(page_buf, 0, flash_page_size);

	for (page_idx = page_num - 1;; page_idx--) {
		memcpy(page_buf, mem_data + (page_idx * flash_page_size),
		       size_copy);
		if (chip_info->boot_ver[0] == 0xB2) {
			ret =
			    sec_flash_page_write(chip_info,
						 (page_idx + page_idx_start),
						 page_buf);
			if (ret < 0) {
				sec_mdelay(50);
				ret =
				    sec_flash_page_write(chip_info,
							 (page_idx +
							  page_idx_start),
							 page_buf);
				if (ret < 0) {
					TPD_INFO
					    ("%s: fw write failed, page_idx = %u\n",
					     __func__, page_idx);
					goto err;
				}
			}
		} else {
			ret =
			    sec_limited_flash_page_write(chip_info,
							 (page_idx +
							  page_idx_start),
							 page_buf);
			if (ret < 0) {
				sec_mdelay(50);
				ret =
				    sec_limited_flash_page_write(chip_info,
								 (page_idx +
								  page_idx_start),
								 page_buf);
				if (ret < 0) {
					TPD_INFO
					    ("%s: fw write failed, page_idx = %u\n",
					     __func__, page_idx);
					goto err;
				}
			}

		}

		size_copy = flash_page_size;
		sec_mdelay(5);

		if (page_idx == 0)	/* end condition (page_idx >= 0)   page_idx type unsinged int */
			break;
	}

	return mem_size;
 err:
	return -EIO;
}

static int sec_block_read(struct chip_data_s6sy761 *chip_info, u32 mem_addr,
			  int mem_size, u8 * buf)
{
	int ret;
	u8 cmd[5];

	if (mem_size >= 64 * 1024) {
		TPD_INFO("%s: mem size over 64K\n", __func__);
		return -EIO;
	}

	cmd[0] = (u8) SEC_CMD_FLASH_READ_ADDR;
	cmd[1] = (u8) ((mem_addr >> 24) & 0xff);
	cmd[2] = (u8) ((mem_addr >> 16) & 0xff);
	cmd[3] = (u8) ((mem_addr >> 8) & 0xff);
	cmd[4] = (u8) ((mem_addr >> 0) & 0xff);

	ret = touch_i2c_write(chip_info->client, cmd, 5);
	if (ret < 0) {
		TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
		return -EIO;
	}

	udelay(10);
	cmd[0] = (u8) SEC_CMD_FLASH_READ_SIZE;
	cmd[1] = (u8) ((mem_size >> 8) & 0xff);
	cmd[2] = (u8) ((mem_size >> 0) & 0xff);

	ret = touch_i2c_write(chip_info->client, cmd, 3);
	if (ret < 0) {
		TPD_INFO("%s: send command failed, %02X\n", __func__, cmd[0]);
		return -EIO;
	}

	udelay(10);
	cmd[0] = (u8) SEC_CMD_FLASH_READ_DATA;

	ret = touch_i2c_read(chip_info->client, cmd, 1, buf, mem_size);
	if (ret < 0) {
		TPD_INFO("%s: memory read failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int sec_memory_read(struct chip_data_s6sy761 *chip_info, u32 mem_addr,
			   u8 * mem_data, u32 mem_size)
{
	int ret;
	int retry = 3;
	int read_size = 0;
	int unit_size;
	int max_size = I2C_BURSTMAX;
	int read_left = (int)mem_size;
	u8 *tmp_data;

	tmp_data = kmalloc(max_size, GFP_KERNEL | GFP_DMA);
	if (!tmp_data) {
		TPD_INFO("%s: failed to kmalloc\n", __func__);
		return -ENOMEM;
	}

	while (read_left > 0) {
		unit_size = (read_left > max_size) ? max_size : read_left;
		retry = 3;
		do {
			ret =
			    sec_block_read(chip_info, mem_addr, unit_size,
					   tmp_data);
			if (retry-- == 0) {
				TPD_INFO
				    ("%s: fw read fail mem_addr=%08X, unit_size=%d\n",
				     __func__, mem_addr, unit_size);
				kfree(tmp_data);
				return -1;
			}

			memcpy(mem_data + read_size, tmp_data, unit_size);
		} while (ret < 0);

		mem_addr += unit_size;
		read_size += unit_size;
		read_left -= unit_size;
	}

	kfree(tmp_data);

	return read_size;
}

static int sec_chunk_update(struct chip_data_s6sy761 *chip_info, u32 addr,
			    u32 size, u8 * data)
{
	int ii = 0, ret = 0;
	u8 *mem_rb = NULL;
	u32 write_size = 0;
	u32 fw_size = size;

	write_size = sec_flash_write(chip_info, addr, data, fw_size);
	if (write_size != fw_size) {
		TPD_INFO("%s: fw write failed\n", __func__);
		ret = -1;
		goto err_write_fail;
	}

	mem_rb = vzalloc(fw_size);
	if (!mem_rb) {
		TPD_INFO("%s: vzalloc failed\n", __func__);
		ret = -1;
		goto err_write_fail;
	}

	if (sec_memory_read(chip_info, addr, mem_rb, fw_size) >= 0) {
		for (ii = 0; ii < fw_size; ii++) {
			if (data[ii] != mem_rb[ii])
				break;
		}

		if (fw_size != ii) {
			TPD_INFO("%s: fw verify fail at data[%d](%d, %d)\n",
				 __func__, ii, data[ii], mem_rb[ii]);
			ret = -1;
			goto out;
		}
	} else {
		ret = -1;
		goto out;
	}

	TPD_INFO("%s: verify done(%d)\n", __func__, ret);

 out:
	vfree(mem_rb);
 err_write_fail:
	sec_mdelay(10);

	return ret;
}

static int sec_read_calibration_report(struct chip_data_s6sy761 *chip_info)
{
	int ret;
	u8 buf[5];

	buf[0] = SEC_CMD_READ_CALIBRATION_REPORT;

	ret = touch_i2c_read(chip_info->client, &buf[0], 1, &buf[1], 4);
	if (ret < 0) {
		TPD_INFO("%s: failed to read, ret = %d\n", __func__, ret);
		return ret;
	}

	TPD_INFO("%s: count:%d, pass count:%d, fail count:%d, status:0x%X\n",
		 __func__, buf[1], buf[2], buf[3], buf[4]);

	return buf[4];
}

static int sec_execute_force_calibration(struct chip_data_s6sy761 *chip_info)
{
	int rc = -1;

	if (touch_i2c_write_block
	    (chip_info->client, SEC_CMD_FACTORY_PANELCALIBRATION, 0,
	     NULL) < 0) {
		TPD_INFO("%s: Write Cal commend failed!\n", __func__);
		return rc;
	}

	sec_mdelay(1000);
	rc = sec_wait_for_ready(chip_info, SEC_VENDOR_ACK_OFFSET_CAL_DONE);

	return rc;
}

static void handleFourCornerPoint(struct Coordinate *point, int n)
{
	int i = 0;
	struct Coordinate left_most = point[0], right_most =
	    point[0], top_most = point[0], down_most = point[0];

	if (n < 4)
		return;

	for (i = 0; i < n; i++) {
		if (right_most.x < point[i].x) {	//xmax
			right_most = point[i];
		}
		if (left_most.x > point[i].x) {	//xmin
			left_most = point[i];
		}
		if (down_most.y < point[i].y) {	//ymax
			down_most = point[i];
		}
		if (top_most.y > point[i].y) {	//ymin
			top_most = point[i];
		}
	}
	point[0] = top_most;
	point[1] = left_most;
	point[2] = down_most;
	point[3] = right_most;
}

/****** End of other functions that work for touchpanel_operations callbacks*************/

/********* Start of implementation of touchpanel_operations callbacks********************/
static int sec_reset(void *chip_data)
{
	int ret = -1;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	TPD_INFO("%s is called\n", __func__);
	if (chip_info->is_power_down) {	//power off state, no need reset
		return 0;
	}

	disable_irq_nosync(chip_info->client->irq);

	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {	//rsted by rst pin
		TPD_INFO("reset by pull down rst pin");
		gpio_direction_output(chip_info->hw_res->reset_gpio, false);
		sec_mdelay(5);
		gpio_direction_output(chip_info->hw_res->reset_gpio, true);
	} else {		//otherwise by soft reset
		touch_i2c_write_block(chip_info->client, SEC_CMD_SOFT_RESET, 0,
				      NULL);
	}

	sec_mdelay(RESET_TO_NORMAL_TIME);
	sec_wait_for_ready(chip_info, SEC_ACK_BOOT_COMPLETE);
	ret =
	    touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
	TPD_INFO("%s: write sense on %s\n", __func__,
		 (ret < 0) ? "failed" : "success");

	enable_irq(chip_info->client->irq);

	return 0;
}

static int sec_get_vendor(void *chip_data, struct panel_info *panel_data)
{
	int len = 0;
	char manu_temp[MAX_DEVICE_MANU_LENGTH] = "SEC_";
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	len = strlen(panel_data->fw_name);
	if ((len > 3) && (panel_data->fw_name[len - 3] == 'i') &&
	    (panel_data->fw_name[len - 2] == 'm')
	    && (panel_data->fw_name[len - 1] == 'g')) {
		panel_data->fw_name[len - 3] = 'b';
		panel_data->fw_name[len - 2] = 'i';
		panel_data->fw_name[len - 1] = 'n';
	}
	chip_info->tp_type = panel_data->tp_type;
	strlcat(manu_temp, panel_data->manufacture_info.manufacture,
		MAX_DEVICE_MANU_LENGTH);
	strncpy(panel_data->manufacture_info.manufacture, manu_temp,
		MAX_DEVICE_MANU_LENGTH);
	TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n",
		 chip_info->tp_type, panel_data->fw_name);

	return 0;
}

static int sec_get_chip_info(void *chip_data)
{
	return 0;
}

static int sec_power_control(void *chip_data, bool enable)
{
	int ret = 0;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	TPD_INFO("%s enable :%d\n", __func__, enable);
	if (true == enable) {
		tp_powercontrol_1v8(chip_info->hw_res, true);
		tp_powercontrol_2v8(chip_info->hw_res, true);
		if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
			TPD_INFO("Set the reset_gpio \n");
			gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
		}
		msleep(RESET_TO_NORMAL_TIME);
		sec_wait_for_ready(chip_info, SEC_ACK_BOOT_COMPLETE);
		ret =
		    touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON,
					  0, NULL);
		TPD_INFO("%s: write sense on %s\n", __func__,
			 (ret < 0) ? "failed" : "success");
		chip_info->is_power_down = false;
	} else {
		tp_powercontrol_2v8(chip_info->hw_res, false);
		msleep(5);
		tp_powercontrol_1v8(chip_info->hw_res, false);
		if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
			TPD_INFO("Set the reset_gpio \n");
			gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
		}

		chip_info->is_power_down = true;
	}

	return ret;
}

static fw_check_state sec_fw_check(void *chip_data,
				   struct resolution_info *resolution_info,
				   struct panel_info *panel_data)
{
	int ret = 0;
	unsigned char *data = dma_buffer->fw_data;
	bool valid_fw_integrity = false;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	ret = touch_i2c_read_byte(chip_info->client, SEC_READ_FIRMWARE_INTEGRITY);	//judge whether fw is right
	if (ret < 0) {
		TPD_INFO("%s: failed to do integrity check (%d)\n", __func__,
			 ret);
	} else {
		if (ret & 0x80) {
			valid_fw_integrity = true;
		} else {
			valid_fw_integrity = false;
			TPD_INFO("invalid firmware integrity (%d)\n", ret);
		}
	}

	ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
	if (ret < 0) {
		TPD_INFO("%s: failed to read boot status\n", __func__);
	} else {
		data[0] = 0;
		ret =
		    touch_i2c_read_block(chip_info->client, SEC_READ_TS_STATUS,
					 4, &data[1]);
		if (ret < 0) {
			TPD_INFO("%s: failed to read touch status\n", __func__);
		}
	}
	if ((((data[0] == SEC_STATUS_APP_MODE)
	      && (data[2] == TOUCH_SYSTEM_MODE_FLASH)) || (ret < 0))
	    && (valid_fw_integrity == false)) {
		TPD_INFO("%s: fw id abnormal, need update\n", __func__);
		return FW_ABNORMAL;
	}

	data[0] = 0;
	touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, data);
	panel_data->TP_FW =
	    (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	if (panel_data->manufacture_info.version)
		sprintf(panel_data->manufacture_info.version, "0x%x",
			panel_data->TP_FW);

	// ret = touch_i2c_write_block(chip_info->client, SEC_CMD_SENSE_ON, 0, NULL);
	// TPD_INFO("%s: write sense on %s\n", (ret < 0) ? "failed" : "success");
	return FW_NORMAL;
}

static fw_update_state sec_fw_update(void *chip_data, const struct firmware *fw,
				     bool force)
{
	int i = 0, ret = 0;
	u8 *buf = dma_buffer->fw_buf;
	u8 *fd = NULL;
	uint8_t cal_status = 0;
	sec_fw_chunk *fw_ch = NULL;
	sec_fw_header *fw_hd = NULL;
	uint32_t fw_version_in_bin = 0, fw_version_in_ic = 0;
	uint32_t config_version_in_bin = 0, config_version_in_ic = 0;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	if (!chip_info) {
		TPD_INFO("Chip info is NULL\n");
		return 0;
	}

	TPD_INFO("%s is called, force update:%d\n", __func__, force);

	fd = (u8 *) (fw->data);
	fw_hd = (sec_fw_header *) (fw->data);
	buf[3] = (fw_hd->img_ver >> 24) & 0xff;
	buf[2] = (fw_hd->img_ver >> 16) & 0xff;
	buf[1] = (fw_hd->img_ver >> 8) & 0xff;
	buf[0] = (fw_hd->img_ver >> 0) & 0xff;
	fw_version_in_bin =
	    (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	touch_i2c_read_block(chip_info->client, SEC_READ_IMG_VERSION, 4, buf);
	fw_version_in_ic =
	    (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	TPD_INFO("img version in bin is 0x%04x, img version in ic is 0x%04x\n",
		 fw_version_in_bin, fw_version_in_ic);

	buf[3] = (fw_hd->para_ver >> 24) & 0xff;
	buf[2] = (fw_hd->para_ver >> 16) & 0xff;
	buf[1] = (fw_hd->para_ver >> 8) & 0xff;
	buf[0] = (fw_hd->para_ver >> 0) & 0xff;
	config_version_in_bin =
	    (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	touch_i2c_read_block(chip_info->client, SEC_READ_CONFIG_VERSION, 4,
			     buf);
	config_version_in_ic =
	    (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	TPD_INFO
	    ("config version in bin is 0x%04x, config version in ic is 0x%04x\n",
	     config_version_in_bin, config_version_in_ic);

	ret = touch_i2c_read_byte(chip_info->client, SEC_READ_BOOT_STATUS);
	if (ret == SEC_STATUS_BOOT_MODE) {
		force = 1;
		TPD_INFO("%s: still in bootloader mode, will do force update\n",
			 __func__);
	}

	if (!force) {
		if (fw_version_in_bin == fw_version_in_ic) {
			return FW_NO_NEED_UPDATE;
		}
	}

	if (sec_enter_fw_mode(chip_info)) {
		TPD_INFO("%s: enter fw mode failed\n", __func__);
		return FW_UPDATE_ERROR;
	}

	if (fw_hd->signature != SEC_FW_HEADER_SIGN) {
		TPD_INFO("%s: firmware header error(0x%08x)\n", __func__,
			 fw_hd->signature);
		return FW_UPDATE_ERROR;
	}

	fd += sizeof(sec_fw_header);
	for (i = 0; i < fw_hd->num_chunk; i++) {
		fw_ch = (sec_fw_chunk *) fd;
		TPD_INFO("update %d chunk(addr: 0x%08x, size: 0x%08x)\n", i,
			 fw_ch->addr, fw_ch->size);
		if (fw_ch->signature != SEC_FW_CHUNK_SIGN) {
			TPD_INFO("%s: firmware chunk error(0x%08x)\n", __func__,
				 fw_ch->signature);
			return FW_UPDATE_ERROR;
		}
		fd += sizeof(sec_fw_chunk);
		ret = sec_chunk_update(chip_info, fw_ch->addr, fw_ch->size, fd);
		if (ret < 0) {
			TPD_INFO("update chunk failed\n");
			return FW_UPDATE_ERROR;
		}
		fd += fw_ch->size;
	}

	sec_reset(chip_info);
	cal_status = sec_read_calibration_report(chip_info);	//read out calibration result
	if ((cal_status == 0) || (cal_status == 0xFF)
	    || ((config_version_in_ic != config_version_in_bin)
		&& (config_version_in_ic != 0xFFFFFFFF))) {
		TPD_INFO("start calibration.\n");
		ret = sec_execute_force_calibration(chip_info);
		if (ret < 0) {
			TPD_INFO("calibration failed once, try again.\n");
			ret = sec_execute_force_calibration(chip_info);
		}
		TPD_INFO("calibration %s\n", (ret < 0) ? "failed" : "success");
	}
	TPD_INFO("%s: update success\n", __func__);
	return FW_UPDATE_SUCCESS;
}

static u8 sec_trigger_reason(void *chip_data, int gesture_enable,
			     int is_suspended)
{
	int ret = 0;
	int event_id = 0;
	u8 left_event_cnt = 0;
	struct sec_event_status *p_event_status = NULL;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	ret =
	    touch_i2c_read_block(chip_info->client, SEC_READ_ONE_EVENT,
				 SEC_EVENT_BUFF_SIZE, chip_info->first_event);
	if (ret < 0) {
		TPD_DETAIL("%s: read one event failed\n", __func__);
		return IRQ_IGNORE;
	}

	TPD_DEBUG
	    ("first event: 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x\n",
	     chip_info->first_event[0], chip_info->first_event[1],
	     chip_info->first_event[2], chip_info->first_event[3],
	     chip_info->first_event[4], chip_info->first_event[5],
	     chip_info->first_event[6], chip_info->first_event[7]);

	if (chip_info->first_event[0] == 0) {
		TPD_DETAIL("%s: event buffer is empty\n", __func__);
		return IRQ_IGNORE;
	}

	left_event_cnt = chip_info->first_event[7] & 0x3F;
	if ((left_event_cnt > MAX_EVENT_COUNT - 1) || (left_event_cnt == 0xFF)) {
		TPD_INFO("%s: event buffer overflow, do clear the buffer\n",
			 __func__);
		ret =
		    touch_i2c_write_block(chip_info->client,
					  SEC_CMD_CLEAR_EVENT_STACK, 0, NULL);
		if (ret < 0) {
			TPD_INFO("%s: clear event buffer failed\n", __func__);
		}
		return IRQ_IGNORE;
	}

	event_id = chip_info->first_event[0] & 0x3;
	if (event_id == SEC_STATUS_EVENT) {
		/* watchdog reset -> send SENSEON command */
		p_event_status =
		    (struct sec_event_status *)chip_info->first_event;
		if ((p_event_status->stype == TYPE_STATUS_EVENT_INFO)
		    && (p_event_status->status_id == SEC_ACK_BOOT_COMPLETE)
		    && (p_event_status->status_data_1 == 0x20)) {

			ret =
			    touch_i2c_write_block(chip_info->client,
						  SEC_CMD_SENSE_ON, 0, NULL);
			if (ret < 0) {
				TPD_INFO("%s: write sense on failed\n",
					 __func__);
			}
			return IRQ_FW_AUTO_RESET;
		}

		/* event queue full-> all finger release */
		if ((p_event_status->stype == TYPE_STATUS_EVENT_ERR)
		    && (p_event_status->status_id ==
			SEC_ERR_EVENT_QUEUE_FULL)) {
			TPD_INFO("%s: IC Event Queue is full\n", __func__);
			tp_touch_btnkey_release();
		}

		if ((p_event_status->stype == TYPE_STATUS_EVENT_ERR)
		    && (p_event_status->status_id == SEC_ERR_EVENT_ESD)) {
			TPD_INFO("%s: ESD detected. run reset\n", __func__);
			return IRQ_EXCEPTION;
		}

		if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO)
		    && (p_event_status->status_id == SEC_STATUS_EARDETECTED)) {
			chip_info->proximity_status =
			    p_event_status->status_data_1;
			TPD_INFO("%s: face detect status %d\n", __func__,
				 chip_info->proximity_status);
			return IRQ_FACE_STATE;
		}

		if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO)
		    && (p_event_status->status_id == SEC_STATUS_TOUCHHOLD)) {
			if (p_event_status->status_data_1 == 1) {
				g_tp->touchold_event = 1;
				gf_opticalfp_irq_handler(1);
			} else if (p_event_status->status_data_1 == 0) {
				g_tp->touchold_event = 0;
				gf_opticalfp_irq_handler(0);
			}
			TPD_INFO("%s: touch_hold status %d\n", __func__,
				 p_event_status->status_data_1);
			return IRQ_IGNORE;
		}

		if ((p_event_status->stype == TYPE_STATUS_EVENT_INFO)
		    && (p_event_status->status_id == SEC_TS_ACK_WET_MODE)) {
			chip_info->wet_mode = p_event_status->status_data_1;
			TPD_INFO("%s: water wet mode %d\n", __func__,
				 chip_info->wet_mode);
			return IRQ_IGNORE;
		}
		if ((p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO)
		    && (p_event_status->status_id ==
			SEC_TS_VENDOR_ACK_NOISE_STATUS_NOTI)) {
			chip_info->touch_noise_status =
			    ! !p_event_status->status_data_1;
			TPD_INFO("%s: TSP NOISE MODE %s[%d]\n", __func__,
				 chip_info->touch_noise_status ==
				 0 ? "OFF" : "ON",
				 p_event_status->status_data_1);
			return IRQ_IGNORE;
		}
	} else if (event_id == SEC_COORDINATE_EVENT) {
		return IRQ_TOUCH;
	} else if (event_id == SEC_GESTURE_EVENT) {
		return IRQ_GESTURE;
	}

	return IRQ_IGNORE;
}

static int sec_get_touch_points(void *chip_data, struct point_info *points,
				int max_num)
{
	int i = 0;
	int t_id = 0;
	int ret = -1;
	int left_event = 0;
	struct sec_event_coordinate *p_event_coord = NULL;
	uint32_t obj_attention = 0;
	u8 *event_buff = dma_buffer->event_buff;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	p_event_coord = (struct sec_event_coordinate *)chip_info->first_event;
	t_id = (p_event_coord->tid - 1);
	if ((t_id < max_num)
	    && ((p_event_coord->tchsta == SEC_COORDINATE_ACTION_PRESS)
		|| (p_event_coord->tchsta == SEC_COORDINATE_ACTION_MOVE))) {
		points[t_id].x =
		    (p_event_coord->x_11_4 << 4) | (p_event_coord->x_3_0);
		points[t_id].y =
		    (p_event_coord->y_11_4 << 4) | (p_event_coord->y_3_0);
		points[t_id].z = p_event_coord->z & 0x3F;
		points[t_id].width_major = p_event_coord->major;
		points[t_id].touch_major = p_event_coord->major;
		points[t_id].status = 1;

		if (points[t_id].z <= 0) {
			points[t_id].z = 1;
		}
		obj_attention = obj_attention | (1 << t_id);	//set touch bit
	}

	left_event = chip_info->first_event[7] & 0x3F;
	if (left_event == 0) {
		return obj_attention;
	} else if (left_event > max_num - 1) {
		TPD_INFO("%s: read left event beyond max touch points\n",
			 __func__);
		left_event = max_num - 1;
	}
	ret =
	    touch_i2c_read_block(chip_info->client, SEC_READ_ALL_EVENT,
				 SEC_EVENT_BUFF_SIZE * left_event, &event_buff[0]);
	if (ret < 0) {
		TPD_INFO("%s: i2c read all event failed\n", __func__);
		return obj_attention;
	}

	for (i = 0; i < left_event; i++) {
		p_event_coord =
		    (struct sec_event_coordinate *)&event_buff[i *
							       SEC_EVENT_BUFF_SIZE];
		t_id = (p_event_coord->tid - 1);
		if ((t_id < max_num)
		    && ((p_event_coord->tchsta == SEC_COORDINATE_ACTION_PRESS)
			|| (p_event_coord->tchsta ==
			    SEC_COORDINATE_ACTION_MOVE))) {
			points[t_id].x =
			    (p_event_coord->x_11_4 << 4) | (p_event_coord->
							    x_3_0);
			points[t_id].y =
			    (p_event_coord->y_11_4 << 4) | (p_event_coord->
							    y_3_0);
			points[t_id].z = p_event_coord->z & 0x3F;
			points[t_id].width_major = p_event_coord->major;
			points[t_id].touch_major = p_event_coord->major;
			points[t_id].status = 1;

			if (points[t_id].z <= 0) {
				points[t_id].z = 1;
			}
			obj_attention = obj_attention | (1 << t_id);	//set touch bit
		}
	}

	return obj_attention;
}

static int sec_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
	int ret = -1;
	uint8_t *coord = dma_buffer->coord;
	struct Coordinate limitPoint[4];
	struct sec_gesture_status *p_event_gesture = NULL;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	p_event_gesture = (struct sec_gesture_status *)chip_info->first_event;
	if (p_event_gesture->coordLen > 18) {
		p_event_gesture->coordLen = 18;
	}

	ret =
	    touch_i2c_read_block(chip_info->client, SEC_READ_GESTURE_EVENT,
				 p_event_gesture->coordLen, coord);
	if (ret < 0) {
		TPD_INFO("%s: read gesture data failed\n", __func__);
	}

	switch (p_event_gesture->gestureId)	//judge gesture type
	{
	case GESTURE_RIGHT:
		gesture->gesture_type = Left2RightSwip;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_LEFT:
		gesture->gesture_type = Right2LeftSwip;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_DOWN:
		gesture->gesture_type = Up2DownSwip;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_UP:
		gesture->gesture_type = Down2UpSwip;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_DOUBLECLICK:
		gesture->gesture_type = DouTap;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end = gesture->Point_start;
		break;

	case GESTURE_UP_V:
		gesture->gesture_type = UpVee;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_DOWN_V:
		gesture->gesture_type = DownVee;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_LEFT_V:
		gesture->gesture_type = LeftVee;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_RIGHT_V:
		gesture->gesture_type = RightVee;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		break;

	case GESTURE_O:
		gesture->gesture_type = Circle;
		gesture->clockwise = (p_event_gesture->data == 0) ? 1 : 0;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		limitPoint[0].x = (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);	//ymin
		limitPoint[0].y = (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		limitPoint[1].x = (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);	//xmin
		limitPoint[1].y = (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		limitPoint[2].x = (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);	//ymax
		limitPoint[2].y = (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
		limitPoint[3].x = (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);	//xmax
		limitPoint[3].y = (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[15] << 4) | ((coord[17] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[16] << 4) | ((coord[17] >> 0) & 0x0F);
		handleFourCornerPoint(&limitPoint[0], 4);
		gesture->Point_1st = limitPoint[0];	//ymin
		gesture->Point_2nd = limitPoint[1];	//xmin
		gesture->Point_3rd = limitPoint[2];	//ymax
		gesture->Point_4th = limitPoint[3];	//xmax
		break;

	case GESTURE_DOUBLE_LINE:
		gesture->gesture_type = DouSwip;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_2nd.x =
		    (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
		gesture->Point_2nd.y =
		    (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
		break;

	case GESTURE_M:
		gesture->gesture_type = Mgestrue;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		gesture->Point_2nd.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_2nd.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_3rd.x =
		    (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
		gesture->Point_3rd.y =
		    (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
		break;

	case GESTURE_W:
		gesture->gesture_type = Wgestrue;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		gesture->Point_2nd.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_2nd.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_3rd.x =
		    (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
		gesture->Point_3rd.y =
		    (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[12] << 4) | ((coord[14] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[13] << 4) | ((coord[14] >> 0) & 0x0F);
		break;

	case GESTURE_SINGLE_TAP:
		gesture->gesture_type = SingleTap;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		break;

	case GESTURE_S:
		gesture->gesture_type = Sgestrue;
		gesture->Point_start.x =
		    (coord[0] << 4) | ((coord[2] >> 4) & 0x0F);
		gesture->Point_start.y =
		    (coord[1] << 4) | ((coord[2] >> 0) & 0x0F);
		gesture->Point_1st.x =
		    (coord[3] << 4) | ((coord[5] >> 4) & 0x0F);
		gesture->Point_1st.y =
		    (coord[4] << 4) | ((coord[5] >> 0) & 0x0F);
		gesture->Point_2nd.x =
		    (coord[6] << 4) | ((coord[8] >> 4) & 0x0F);
		gesture->Point_2nd.y =
		    (coord[7] << 4) | ((coord[8] >> 0) & 0x0F);
		gesture->Point_end.x =
		    (coord[9] << 4) | ((coord[11] >> 4) & 0x0F);
		gesture->Point_end.y =
		    (coord[10] << 4) | ((coord[11] >> 0) & 0x0F);
		break;

	default:
		gesture->gesture_type = UnkownGesture;
		break;
	}

	TPD_INFO
	    ("%s, gesture_id: 0x%x, gesture_type: %d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n",
	     __func__, p_event_gesture->gestureId, gesture->gesture_type,
	     gesture->clockwise, gesture->Point_start.x, gesture->Point_start.y,
	     gesture->Point_end.x, gesture->Point_end.y, gesture->Point_1st.x,
	     gesture->Point_1st.y, gesture->Point_2nd.x, gesture->Point_2nd.y,
	     gesture->Point_3rd.x, gesture->Point_3rd.y, gesture->Point_4th.x,
	     gesture->Point_4th.y);

	return 0;
}

static int sec_mode_switch(void *chip_data, work_mode mode, bool flag)
{
	int ret = -1;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	if (chip_info->is_power_down) {
		sec_power_control(chip_info, true);
	}

	switch (mode) {
	case MODE_NORMAL:
		ret = 0;
		break;

	case MODE_SLEEP:
		ret = sec_power_control(chip_info, false);
		if (ret < 0) {
			TPD_INFO("%s: power down failed\n", __func__);
		}
		break;

	case MODE_GESTURE:
		ret = sec_enable_black_gesture(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: sec enable gesture failed.\n", __func__);
			return ret;
		}
		break;

	case MODE_EDGE:
		ret = sec_enable_edge_limit(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: sec enable edg limit failed.\n",
				 __func__);
			return ret;
		}
		break;

	case MODE_CHARGE:
		ret = sec_enable_charge_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable charge mode : %d failed\n",
				 __func__, flag);
		}
		break;

	case MODE_FACE_DETECT:
		ret = sec_enable_face_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable face detect mode : %d failed\n",
				 __func__, flag);
		}
		break;

	case MODE_FACE_CALIBRATE:
		ret = sec_face_reduce_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable face reduce mode : %d failed\n",
				 __func__, flag);
		}
		break;
	case MODE_PALM_REJECTION:
		ret = sec_enable_palm_reject(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable palm rejection: %d failed\n",
				 __func__, flag);
		}
		break;

	case MODE_GAME:
		ret = sec_enable_game_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable game mode: %d failed\n", __func__,
				 flag);
		}
		break;

	case MODE_REFRESH_SWITCH:
		ret = sec_refresh_switch_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: swhitch refresh rate mode: %d failed\n",
				 __func__, flag);
		}
		break;

	case MODE_TOUCH_HOLD:
		ret = sec_touchhold_switch_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: open touchhold mode: %d failed\n",
				 __func__, flag);
		}
		break;

	case MODE_TOUCH_AREA_SWITCH:
		ret = sec_toucharea_switch_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: switch touchhold area: %d failed\n",
				 __func__, flag);
		}
		break;

	case MODE_LIMIT_SWITCH:
		ret = sec_limit_switch_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: limit switch: %d failed\n", __func__,
				 flag);
		}
		break;

	case MODE_GESTURE_SWITCH:
		ret = sec_gesture_switch_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: switch gestrue mode: %d failed\n",
				 __func__, flag);
		}
		break;

	default:
		TPD_INFO("%s: Wrong mode.\n", __func__);
	}

	return ret;
}

static int sec_get_face_detect(void *chip_data)
{
	int state = -1;
	struct chip_data_s6sy761 *chip_info =
	    (struct chip_data_s6sy761 *)chip_data;

	if (chip_info->proximity_status == 0x2E) {
		state = 0;	//far
	} else if (chip_info->proximity_status == 0) {
		state = 1;	//near
	}
	return state;
}

#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
extern unsigned int upmu_get_rgs_chrdet(void);
static int sec_get_usb_state(void)
{
	return upmu_get_rgs_chrdet();
}
#else
static int sec_get_usb_state(void)
{
	return 0;
}
#endif

static struct touchpanel_operations sec_ops = {
	.get_vendor = sec_get_vendor,
	.get_chip_info = sec_get_chip_info,
	.reset = sec_reset,
	.power_control = sec_power_control,
	.fw_check = sec_fw_check,
	.fw_update = sec_fw_update,
	.trigger_reason = sec_trigger_reason,
	.get_touch_points = sec_get_touch_points,
	.get_gesture_info = sec_get_gesture_info,
	.mode_switch = sec_mode_switch,
	.get_usb_state = sec_get_usb_state,
	.get_face_state = sec_get_face_detect,
};

/********* End of implementation of touchpanel_operations callbacks**********************/

/*********** Start of I2C Driver and Implementation of it's callbacks*************************/
static int sec_tp_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct chip_data_s6sy761 *chip_info = NULL;
	struct touchpanel_data *ts = NULL;
	int ret = -1;

	TPD_INFO("%s  is called\n", __func__);
	if (tp_register_times > 0) {
		TPD_INFO("TP driver have success loaded %d times, exit\n",
			 tp_register_times);
		return -1;
	}
	/* 1. alloc chip info */
	chip_info = kzalloc(sizeof(struct chip_data_s6sy761), GFP_KERNEL | GFP_DMA);
	if (chip_info == NULL) {
		TPD_INFO("chip info kzalloc error\n");
		ret = -ENOMEM;
		return ret;
	}

	dma_buffer = kmalloc(sizeof(struct dma_buf_s6sy761), GFP_KERNEL | GFP_DMA);
	if (dma_buffer == NULL) {
		TPD_INFO("dma buffer kzalloc error\n");
		ret = -ENOMEM;
		return ret;
	}

	/* 2. Alloc common ts */
	ts = common_touch_data_alloc();
	if (ts == NULL) {
		TPD_INFO("ts kzalloc error\n");
		goto ts_malloc_failed;
	}

	/* 3. bind client and dev for easy operate */
	chip_info->client = client;
	ts->client = client;
	ts->irq = client->irq;
	i2c_set_clientdata(client, ts);
	ts->dev = &client->dev;
	ts->chip_data = chip_info;
	chip_info->hw_res = &ts->hw_res;
	/* 4. file_operations callbacks binding */
	ts->ts_ops = &sec_ops;

	/* 5. register common touch device */
	ret = register_common_touch_device(ts);
	if (ret < 0) {
		goto err_register_driver;
	}
	//ts->tp_resume_order = LCD_TP_RESUME;

	TPD_INFO("%s, probe normal end\n", __func__);
	return 0;

 err_register_driver:
	common_touch_data_free(ts);
	ts = NULL;

 ts_malloc_failed:
	kfree(chip_info);
	chip_info = NULL;
	ret = -1;

	TPD_INFO("%s, probe error\n", __func__);
	return ret;
}

static int sec_tp_remove(struct i2c_client *client)
{
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	TPD_INFO("%s is called\n", __func__);

	kfree(ts);

	return 0;
}

static int sec_i2c_suspend(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called\n", __func__);
	tp_i2c_suspend(ts);

	return 0;
}

static int sec_i2c_resume(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s is called\n", __func__);
	tp_i2c_resume(ts);

	return 0;
}

static void sec_tp_shutdown(struct i2c_client *client)
{
	struct touchpanel_data *ts = i2c_get_clientdata(client);

	TPD_INFO("%s is called\n", __func__);
	if (!ts->ts_ops->power_control) {
		TPD_INFO("tp power_control NULL!\n");
		return;
	}
	ts->ts_ops->power_control(ts->chip_data, false);
}

static const struct i2c_device_id tp_id[] = {
	{TPD_DEVICE, 0},
	{},
};

static struct of_device_id tp_match_table[] = {
	{.compatible = TPD_DEVICE,},
	{},
};

static const struct dev_pm_ops tp_pm_ops = {
#ifdef CONFIG_FB
	.suspend = sec_i2c_suspend,
	.resume = sec_i2c_resume,
#endif
};

static struct i2c_driver tp_i2c_driver = {
	.probe = sec_tp_probe,
	.remove = sec_tp_remove,
	.shutdown = sec_tp_shutdown,
	.id_table = tp_id,
	.driver = {
		   .name = TPD_DEVICE,
		   .owner = THIS_MODULE,
		   .of_match_table = tp_match_table,
		   .pm = &tp_pm_ops,
		   },
};

/******************* End of I2C Driver and It's dev_pm_ops***********************/

/***********************Start of module init and exit****************************/
static int __init tp_driver_init(void)
{
	TPD_INFO("%s is called\n", __func__);
	if (i2c_add_driver(&tp_i2c_driver) != 0) {
		TPD_INFO("unable to add i2c driver.\n");
		return -1;
	}
	return 0;
}

static void __exit tp_driver_exit(void)
{
	i2c_del_driver(&tp_i2c_driver);
}

module_init(tp_driver_init);
module_exit(tp_driver_exit);
/***********************End of module init and exit*******************************/

MODULE_AUTHOR("Samsung Driver");
MODULE_DESCRIPTION("Samsung Electronics TouchScreen driver");
MODULE_LICENSE("GPL v2");
