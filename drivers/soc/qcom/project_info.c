/*
 * For OEM project information
 * such as project name, hardware ID
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/sys_soc.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/project_info.h>
#include <linux/soc/qcom/smem.h>
#include <linux/gpio.h>
#include <soc/qcom/socinfo.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/soc/qcom/smem.h>
#include <linux/pstore.h>

static struct component_info component_info_desc[COMPONENT_MAX];
static struct project_info *project_info_desc;

static ssize_t project_info_get(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t component_info_get(struct device *dev,
				  struct device_attribute *attr, char *buf);
static int op_aboard_read_gpio(void);

static DEVICE_ATTR(project_name, 0444, project_info_get, NULL);
static DEVICE_ATTR(hw_id, 0444, project_info_get, NULL);
static DEVICE_ATTR(secboot_status, 0444, project_info_get, NULL);

static uint8 get_secureboot_fuse_status(void)
{
	void __iomem *oem_config_base;
	uint8 secure_oem_config = 0;

	oem_config_base = ioremap(SECURE_BOOT1, 1);
	if (!oem_config_base)
		return -EINVAL;
	secure_oem_config = __raw_readb(oem_config_base);
	iounmap(oem_config_base);
	pr_debug("secure_oem_config 0x%x\n", secure_oem_config);

	return secure_oem_config;
}

static ssize_t project_info_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (project_info_desc == NULL)
		return -EINVAL;

	if (attr == &dev_attr_project_name)
		return snprintf(buf, BUF_SIZE, "%s\n",
					project_info_desc->project_name);
	if (attr == &dev_attr_hw_id)
		return snprintf(buf, BUF_SIZE, "%d\n",
					project_info_desc->hw_version);
	if (attr == &dev_attr_secboot_status)
		return snprintf(buf, BUF_SIZE, "%d\n",
				get_secureboot_fuse_status());

	return -EINVAL;
}

static struct attribute *project_info_sysfs_entries[] = {
	&dev_attr_project_name.attr,
	&dev_attr_hw_id.attr,
	&dev_attr_secboot_status.attr,
	NULL,
};

static struct attribute_group project_info_attr_group = {
	.attrs = project_info_sysfs_entries,
};

static DEVICE_ATTR(ddr, 0444, component_info_get, NULL);
static DEVICE_ATTR(ufs, 0444, component_info_get, NULL);

static char *get_component_version(enum COMPONENT_TYPE type)
{
	if (type >= COMPONENT_MAX) {
		pr_err("%s == type %d invalid\n", __func__, type);
		return "N/A";
	}

	return component_info_desc[type].version ? : "N/A";
}

static char *get_component_manufacture(enum COMPONENT_TYPE type)
{
	if (type >= COMPONENT_MAX) {
		pr_err("%s == type %d invalid\n", __func__, type);
		return "N/A";
	}

	return component_info_desc[type].manufacture ? : "N/A";
}

int push_component_info(enum COMPONENT_TYPE type,
			char *version, char *manufacture)
{
	if (type >= COMPONENT_MAX)
		return -ENOMEM;
	component_info_desc[type].version = version;
	component_info_desc[type].manufacture = manufacture;

	return 0;
}
EXPORT_SYMBOL(push_component_info);

static struct attribute *component_info_sysfs_entries[] = {
	&dev_attr_ddr.attr,
	&dev_attr_ufs.attr,
	NULL,
};

static struct attribute_group component_info_attr_group = {
	.attrs = component_info_sysfs_entries,
};

static ssize_t component_info_get(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	if (project_info_desc == NULL)
		return -EINVAL;

	if (attr == &dev_attr_ddr)
		return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
				get_component_version(DDR),
				get_component_manufacture(DDR));
	if (attr == &dev_attr_ufs)
		return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
				get_component_version(UFS),
				get_component_manufacture(UFS));

	return -EINVAL;
}

static int __init project_info_init_sysfs(void)
{
	struct kobject *project_info_kobj;
	struct kobject *component_info;
	int error = 0;

	project_info_kobj = kobject_create_and_add("project_info", NULL);
	if (!project_info_kobj)
		return -ENOMEM;
	error = sysfs_create_group(project_info_kobj, &project_info_attr_group);
	if (error) {
		pr_err
		    ("project_info_init_sysfs project_info_attr_group failure\n");
		return error;
	}

	component_info = kobject_create_and_add("component_info",
						project_info_kobj);
	pr_info("project_info_init_sysfs success\n");
	if (!component_info)
		return -ENOMEM;

	error = sysfs_create_group(component_info, &component_info_attr_group);
	if (error) {
		pr_err
		    ("project_info_init_sysfs project_info_attr_group failure\n");
		return error;
	}
	return 0;
}
late_initcall(project_info_init_sysfs);

struct ddr_manufacture {
	int id;
	char name[20];
};

// ddr id and ddr name
static char ddr_version[32] = { 0 };
static char ddr_manufacture[20] = { 0 };
static char ddr_manufacture_and_fw_verion[40] = { 0 };

struct ddr_manufacture ddr_manufacture_list[] = {
	{1, "Samsung "},
	{2, "Qimonda "},
	{3, "Elpida "},
	{4, "Etpon "},
	{5, "Nanya "},
	{6, "Hynix "},
	{7, "Mosel "},
	{8, "Winbond "},
	{9, "Esmt "},
	{255, "Micron"},
	{0, "Unknown"},
};

static void get_ddr_manufacture_name(void)
{
	uint32 i, length;

	length = ARRAY_SIZE(ddr_manufacture_list);
	if (project_info_desc) {
		for (i = 0; i < length; i++) {
			if (ddr_manufacture_list[i].id ==
			    project_info_desc->ddr_manufacture_info) {
				snprintf(ddr_manufacture,
					 sizeof(ddr_manufacture), "%s",
					 ddr_manufacture_list[i].name);
				break;
			}
		}
	}
}

static int __init init_project_info(void)
{
	int ddr_size = 0;
	size_t size;

	project_info_desc =
	    qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_PROJECT_INFO, &size);

	if (IS_ERR_OR_NULL(project_info_desc)) {
		pr_err("%s: get project_info failure\n", __func__);
		return -1;
	}
	pr_err
	    ("%s: project_name: %s hw_version: %d prj=%d rf_v1: %d rf_v2: %d: rf_v3: %d  paltform_id:%d\n",
	     __func__, project_info_desc->project_name,
	     project_info_desc->hw_version, project_info_desc->prj_version,
	     project_info_desc->rf_v1, project_info_desc->rf_v2,
	     project_info_desc->rf_v3, project_info_desc->platform_id);

	get_ddr_manufacture_name();

	/* approximate as ceiling of total pages */
	ddr_size = (totalram_pages() + (1 << 18) - 1) >> 18;

	snprintf(ddr_version, sizeof(ddr_version), "size_%dG_r_%d_c_%d",
		 ddr_size, project_info_desc->ddr_row,
		 project_info_desc->ddr_column);
	snprintf(ddr_manufacture_and_fw_verion,
		 sizeof(ddr_manufacture_and_fw_verion),
		 "%s%s %u.%u", ddr_manufacture,
		 project_info_desc->ddr_reserve_info == 0x05 ? "20nm" :
		 (project_info_desc->ddr_reserve_info == 0x06 ? "18nm" : " "),
		 project_info_desc->ddr_fw_version >> 16,
		 project_info_desc->ddr_fw_version & 0x0000FFFF);
	push_component_info(DDR, ddr_version, ddr_manufacture_and_fw_verion);

	return 0;
}

struct aborad_data {
	int aboard_gpio_0;
	int aboard_gpio_1;
	int support_aboard_gpio_0;
	int support_aboard_gpio_1;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct device *dev;
};

static struct aborad_data *data = NULL;

static int op_aboard_request_named_gpio(const char *label, int *gpio)
{
	struct device *dev = data->dev;
	struct device_node *np = dev->of_node;
	int rc = of_get_named_gpio(np, label, 0);

	if (rc < 0) {
		dev_err(dev, "failed to get '%s'\n", label);
		*gpio = rc;
		return rc;
	}
	*gpio = rc;

	rc = devm_gpio_request(dev, *gpio, label);
	if (rc) {
		dev_err(dev, "failed to request gpio %d\n", *gpio);
		return rc;
	}

	dev_info(dev, "%s gpio: %d\n", label, *gpio);
	return 0;
}

static int op_aboard_read_gpio(void)
{
	int gpio0 = 0;
	int gpio1 = 0;

	if (data == NULL || IS_ERR_OR_NULL(project_info_desc)) {
		return 0;
	}
	if (data->support_aboard_gpio_0 == 1)
		gpio0 = gpio_get_value(data->aboard_gpio_0);
	if (data->support_aboard_gpio_1 == 1)
		gpio1 = gpio_get_value(data->aboard_gpio_1);

	if (data->support_aboard_gpio_0 == 1
	    && data->support_aboard_gpio_1 == 1) {
		pr_err("%s: gpio0=%d gpio1=%d\n", __func__, gpio0, gpio1);

		if (gpio0 == 0 && gpio1 == 0) {
			project_info_desc->a_board_version = 0;
		} else if (gpio0 == 0 && gpio1 == 1) {
			project_info_desc->a_board_version = 1;
		} else if (gpio0 == 1 && gpio1 == 0) {
			project_info_desc->a_board_version = 2;
		} else {
			project_info_desc->a_board_version = 3;
		}
	} else if (data->support_aboard_gpio_0 == 1) {
		pr_err("%s: gpio0=%d\n", __func__, gpio0);
		project_info_desc->a_board_version = (gpio0 == 1 ? 0 : 3);

	} else if (data->support_aboard_gpio_1 == 1) {
		pr_err("%s: gpio1=%d\n", __func__, gpio1);
		project_info_desc->a_board_version = (gpio1 == 1 ? 0 : 3);
	}

	return 0;

}

static int oem_aboard_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct device *dev = &pdev->dev;

	data = kzalloc(sizeof(struct aborad_data), GFP_KERNEL);
	if (!data) {
		pr_err("%s: failed to allocate memory\n", __func__);
		rc = -ENOMEM;
		goto exit;
	}

	data->dev = dev;
	rc = op_aboard_request_named_gpio("oem,aboard-gpio-0",
					  &data->aboard_gpio_0);
	if (rc) {
		pr_err("%s: oem,aboard-gpio-0 fail\n", __func__);
	} else {
		data->support_aboard_gpio_0 = 1;
	}

	rc = op_aboard_request_named_gpio("oem,aboard-gpio-1",
					  &data->aboard_gpio_1);
	if (rc) {
		pr_err("%s: oem,aboard-gpio-1 fail\n", __func__);
	} else {
		data->support_aboard_gpio_1 = 1;
	}

	data->pinctrl = devm_pinctrl_get((data->dev));
	if (IS_ERR_OR_NULL(data->pinctrl)) {
		rc = PTR_ERR(data->pinctrl);
		pr_err("%s pinctrl error!\n", __func__);
		goto err_pinctrl_get;
	}

	data->pinctrl_state_active =
	    pinctrl_lookup_state(data->pinctrl, "oem_aboard_active");

	if (IS_ERR_OR_NULL(data->pinctrl_state_active)) {
		rc = PTR_ERR(data->pinctrl_state_active);
		pr_err("%s pinctrl state active error!\n", __func__);
		goto err_pinctrl_lookup;
	}

	if (data->pinctrl) {
		rc = pinctrl_select_state(data->pinctrl,
					  data->pinctrl_state_active);
	}
	if (data->support_aboard_gpio_0 == 1)
		gpio_direction_input(data->aboard_gpio_0);
	if (data->support_aboard_gpio_1 == 1)
		gpio_direction_input(data->aboard_gpio_1);
	op_aboard_read_gpio();
	pr_err("%s: probe ok!\n", __func__);
	return 0;

 err_pinctrl_lookup:
	devm_pinctrl_put(data->pinctrl);
 err_pinctrl_get:
	data->pinctrl = NULL;
	kfree(data);
 exit:
	pr_err("%s: probe Fail!\n", __func__);

	return rc;
}

static const struct of_device_id aboard_of_match[] = {
	{.compatible = "oem,aboard",},
	{}
};

MODULE_DEVICE_TABLE(of, aboard_of_match);

static struct platform_driver aboard_driver = {
	.driver = {
		   .name = "op_aboard",
		   .owner = THIS_MODULE,
		   .of_match_table = aboard_of_match,
		   },
	.probe = oem_aboard_probe,
};

static int __init init_project(void)
{
	int ret;

	ret = init_project_info();

	return ret;
}

static int __init init_aboard(void)
{
	int ret;

	ret = platform_driver_register(&aboard_driver);
	if (ret)
		pr_err("aboard_driver register failed: %d\n", ret);

	return ret;
}

subsys_initcall(init_project);
late_initcall(init_aboard);
