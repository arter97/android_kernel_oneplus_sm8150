#include <linux/module.h>
#include <linux/oneplus/boot_mode.h>

int is_second_board_absent;
EXPORT_SYMBOL(is_second_board_absent);

int oem_project;
EXPORT_SYMBOL(oem_project);

static int __init get_second_board_absent_init(char *str)
{
	is_second_board_absent = simple_strtol(str, NULL, 0);
	return 0;
}
__setup("androidboot.sec_bd_abs=", get_second_board_absent_init);

static int __init get_oem_project_init(char *str)
{
	oem_project = simple_strtol(str, NULL, 0);
	return 0;
}
__setup("androidboot.project_name=", get_oem_project_init);
