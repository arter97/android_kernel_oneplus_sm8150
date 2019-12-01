#ifndef __BOOT_MODE_H_
#define __BOOT_MODE_H_

enum oem_boot_mode {
	MSM_BOOT_MODE__NORMAL,
	MSM_BOOT_MODE__FASTBOOT,
	MSM_BOOT_MODE__RECOVERY,
	MSM_BOOT_MODE__AGING,
	MSM_BOOT_MODE__FACTORY,
	MSM_BOOT_MODE__RF,
	MSM_BOOT_MODE__WLAN,
	MSM_BOOT_MODE__MOS,
	MSM_BOOT_MODE__CHARGE,
};

static inline enum oem_boot_mode get_boot_mode(void)
{
	return MSM_BOOT_MODE__NORMAL;
}

extern int is_second_board_absent;
extern int oem_project;

#endif
