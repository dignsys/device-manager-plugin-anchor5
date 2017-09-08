/*
 * device-node
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <hw/usb_client.h>

#include "../shared.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define zalloc(amount) calloc(1, amount)

#define MAX_GADGET_STR_LEN 256
#define MAX_FUNCS 32

#define LEGACY_ROOTPATH        "/sys/class/usb_mode/usb0"

/* Device descriptor values */
#define LEGACY_ID_VENDOR_PATH       LEGACY_ROOTPATH"/idVendor"
#define LEGACY_ID_PRODUCT_PATH      LEGACY_ROOTPATH"/idProduct"
#define LEGACY_BCD_DEVICE_PATH      LEGACY_ROOTPATH"/bcdDevice"
#define LEGACY_CLASS_PATH           LEGACY_ROOTPATH"/bDeviceClass"
#define LEGACY_SUBCLASS_PATH        LEGACY_ROOTPATH"/bDeviceSubClass"
#define LEGACY_PROTOCOL_PATH        LEGACY_ROOTPATH"/bDeviceProtocol"

/* Strings */
#define LEGACY_IMANUFACTURER_PATH   LEGACY_ROOTPATH"/iManufacturer"
#define LEGACY_IPRODUCT_PATH        LEGACY_ROOTPATH"/iProduct"
#define LEGACY_ISERIAL_PATH         LEGACY_ROOTPATH"/iSerial"

/* Functions in each config */
#define LEGACY_CONFIG_1_PATH        LEGACY_ROOTPATH"/funcs_fconf"
#define LEGACY_CONFIG_2_PATH        LEGACY_ROOTPATH"/funcs_sconf"
/* should be single char */
#define LEGACY_FUNC_SEP             ","

/* ON/OFF switch */
#define LEGACY_ENABLE_PATH          LEGACY_ROOTPATH"/enable"
#define LEGACY_ENABLE               "1"
#define LEGACY_DISABLE              "0"

#define LEGACY_BMATTRIBUTES ((1 << 7) | (1 << 6))
#define LEGACY_MAX_POWER 500

/* +5 to be always big enough */
#define INT_BUF_SIZE (sizeof(int)*8 + 5)

static int get_int_from_file(char *path, int *_val)
{
	char buf[INT_BUF_SIZE];
	char *endptr;
	long int val;
	int ret;

	ret = sys_get_str(path, buf, sizeof(buf));
	if (ret)
		return ret;

	val = strtol(buf, &endptr, 0);
	if (val == LONG_MIN || val == LONG_MAX ||
	    buf[0] == '\0' || (*endptr != '\0' && *endptr != '\n')
	    || val > INT_MAX)
		return -EINVAL;

	*_val = (int)val;
	return 0;
}

static int legacy_read_gadget_attrs_strs(struct usb_gadget *gadget)
{
	int val;
	int ret;
	/* We assume that values received from kernel will be valid */
#define GET_VALUE_FROM_SYSFS(path, field, type)		\
	do {						\
		ret = get_int_from_file(path, &val);	\
		if (ret)				\
			return ret;			\
							\
		gadget->attrs.field = (type)val;	\
	} while (0)

	GET_VALUE_FROM_SYSFS(LEGACY_CLASS_PATH, bDeviceClass, uint8_t);
	GET_VALUE_FROM_SYSFS(LEGACY_SUBCLASS_PATH, bDeviceSubClass, uint8_t);
	GET_VALUE_FROM_SYSFS(LEGACY_PROTOCOL_PATH, bDeviceProtocol, uint8_t);
	GET_VALUE_FROM_SYSFS(LEGACY_ID_VENDOR_PATH, idVendor, uint16_t);
	GET_VALUE_FROM_SYSFS(LEGACY_ID_PRODUCT_PATH, idVendor, uint16_t);
	GET_VALUE_FROM_SYSFS(LEGACY_BCD_DEVICE_PATH, bcdDevice, uint16_t);
#undef GET_VALUE_FROM_SYSFS

#define GET_STRING_FROM_SYSFS(path, field)			\
	do {							\
		char buf[MAX_GADGET_STR_LEN];			\
								\
		ret = sys_get_str(path, buf, sizeof(buf));	\
		if (ret)					\
			goto err_##field;			\
								\
		gadget->strs[0].field = strdup(buf);		\
		if (!gadget->strs[0].field) {			\
			ret = -ENOMEM;				\
			goto err_##field;			\
		}						\
	} while (0)

	GET_STRING_FROM_SYSFS(LEGACY_IMANUFACTURER_PATH, manufacturer);
	GET_STRING_FROM_SYSFS(LEGACY_IPRODUCT_PATH, product);
	GET_STRING_FROM_SYSFS(LEGACY_ISERIAL_PATH, serial);
#undef GET_STRING_FROM_SYSFS

	return 0;

err_serial:
	free(gadget->strs[0].product);
err_product:
	free(gadget->strs[0].manufacturer);
err_manufacturer:
	return ret;
}

static int legacy_find_func(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(_available_funcs); ++i)
		if (!strcmp(name, _available_funcs[i]->name))
			return i;

	return -1;
}

static struct usb_function *legacy_find_func_in_gadget(
	struct usb_gadget *gadget, const char *name)
{
	int i;

	for (i = 0; gadget->funcs[i]; ++i)
		if (!strcmp(name, gadget->funcs[i]->name))
			return gadget->funcs[i];
	return NULL;
}

static int legacy_alloc_config(int n_funcs, struct usb_configuration **_config)
{
	struct usb_configuration *config;

	config = zalloc(sizeof(*config));
	if (!config)
		goto out;

	config->strs = calloc(1, sizeof(*config->strs));
	if (!config->strs)
		goto free_config;

	config->funcs = calloc(n_funcs + 1, sizeof(*config->funcs));
	if (!config->funcs)
		goto free_strs;

	/* 
	 * We cannot read correct values
	 * so assume that they are always default
	 */
	config->attrs.bmAttributs = LEGACY_BMATTRIBUTES;
	config->attrs.MaxPower = LEGACY_MAX_POWER;

	*_config = config;

	return 0;
free_strs:
	free(config->strs);
free_config:
	free(config);
out:
	return -ENOMEM;
}

static int legacy_alloc_new_func(struct usb_gadget *gadget, const char *fname,
				 struct usb_function **_func)
{
	struct usb_function *func;
	int ret;

	ret = legacy_find_func(fname);
	if (ret < 0)
		return -ENOTSUP;

	ret = _available_funcs[ret]->clone(_available_funcs[ret], &func);
	if (ret)
		return ret;

	*_func = func;
	return 0;
}

static int legacy_read_config(struct usb_gadget *gadget,
			      char *cpath,
			      struct usb_configuration **_config)
{
	struct usb_configuration *config;
	char buf[MAX_GADGET_STR_LEN];
	char *begin = buf;
	char *fname;
	char *sep = LEGACY_FUNC_SEP;
	int i, f_cnt;
	int f_idx;
	int ret;

	ret = sys_get_str(cpath, buf, sizeof(buf));
	if (ret)
		return ret;

	/* Empty */
	if (buf[0] == '\0' || buf[0] == '\n')
		return 0;

	/* count number of functions in this config */
	f_cnt = 1;
	for (i = 0; buf[i] != '\0'; ++i)
		if (buf[i] == sep[0])
			++f_cnt;

	ret = legacy_alloc_config(f_cnt, &config);
	if (ret)
		return ret;

	f_idx = 0;
	for (fname = strsep(&begin, sep); fname; fname = strsep(&begin, sep)) {
		struct usb_function *func;

		func = legacy_find_func_in_gadget(gadget, fname);
		if (!func) {
			/* new function not added yet to gadget */
			ret = legacy_alloc_new_func(gadget, fname, &func);
			if (!ret)
				goto free_config;
		}

		config->funcs[f_idx++] = func;
	}

	*_config = config;
	return 0;
free_config:
	free(config->strs);
	free(config->funcs);
	free(config);
	return ret;
}

static int legacy_get_current_gadget(struct usb_client *usb,
				     struct usb_gadget **_gadget)
{
	struct usb_gadget *gadget;
	struct usb_gadget_strings *strs;
	struct usb_configuration **configs;
	struct usb_function **funcs;
	int i;
	int ret = -ENOMEM;

	gadget = zalloc(sizeof(*gadget));
	if (!gadget)
		goto out;

	strs = calloc(2, sizeof(*strs));
	if (!strs)
		goto free_gadget;

	strs[0].lang_code = 0x409;

	gadget->strs = strs;

	ret = legacy_read_gadget_attrs_strs(gadget);
	if (ret)
		goto free_strs;

	/* There will be no more functions than bits in int */
	funcs = calloc(MAX_FUNCS, sizeof(*funcs));
	if (!funcs)
		goto free_strs_with_content;

	gadget->funcs = funcs;

	/* slp-gadget use max 2 confiuration and NULL termination */
	configs = calloc(3, sizeof(*configs));
	if (!configs)
		goto free_funcs;

	gadget->configs = configs;

	ret = legacy_read_config(gadget, LEGACY_CONFIG_1_PATH, configs + 0);
	if (ret)
		goto free_configs;

	ret = legacy_read_config(gadget, LEGACY_CONFIG_2_PATH, configs + 1);
	if (ret)
		goto free_config_1;

	*_gadget = gadget;
	return 0;

free_config_1:
	free(configs[0]->funcs);
	free(configs[0]->strs);
	free(configs[0]);
free_configs:
	free(configs);
	for (i = 0; gadget->funcs[i]; ++i)
		gadget->funcs[i]->free_func(gadget->funcs[i]);
free_funcs:
	free(funcs);
free_strs_with_content:
	free(gadget->strs[0].manufacturer);
	free(gadget->strs[0].product);
	free(gadget->strs[0].serial);
free_strs:
	free(gadget->strs);
free_gadget:
	free(gadget);
out:
	return ret;
}

static bool legacy_is_function_supported(struct usb_client *usb,
					 struct usb_function *func)
{
	int ret;

	/*
	 * TODO
	 * Instead of only checking whether we know this function
	 * we should also parse sysfs to check if it is build into
	 * slp-gadget.
	 */
	ret = legacy_find_func(func->name);

	return ret >= 0;
}

static bool legacy_is_gadget_supported(struct usb_client *usb,
				       struct usb_gadget *gadget)
{
	int i, j;

	if (!gadget || !gadget->configs || !gadget->funcs)
		return false;

	/*
	 * TODO
	 * Here is a good place to ensure that serial is immutable
	 */
	if (gadget->strs) {
		/* only strings in US_en are allowed */
		if (gadget->strs[0].lang_code != 0x409 ||
		    gadget->strs[1].lang_code)
			return false;
	}

	for (j = 0; gadget->configs[j]; ++j) {
		struct usb_configuration *config = gadget->configs[j];

		if (config->strs && config->strs[0].lang_code)
			return false;

		if (!config->funcs)
			return false;

		for (i = 0; config->funcs[i]; ++i)
			if (!legacy_is_function_supported(usb, config->funcs[i]))
				return false;
	}

	if (j == 0 || j > 2)
		return false;

	return true;
}

/* TODO. Maybe move this to sys ? */
static int legacy_set_int_hex(char *path, int val)
{
	char buf[MAX_GADGET_STR_LEN];
	int r;

	if (!path)
		return -EINVAL;

	snprintf(buf, sizeof(buf), "%x", val);
	r = sys_set_str(path, buf);
	if (r < 0)
		return r;

	return 0;
}

static int legacy_set_gadget_attrs(struct usb_gadget_attrs *attrs)
{
	int ret;

	ret = sys_set_int(LEGACY_CLASS_PATH, attrs->bDeviceClass);
	if (ret)
		return ret;

	ret = sys_set_int(LEGACY_SUBCLASS_PATH, attrs->bDeviceSubClass);
	if (ret)
		return ret;

	ret = sys_set_int(LEGACY_PROTOCOL_PATH, attrs->bDeviceProtocol);
	if (ret)
		return ret;

	ret = legacy_set_int_hex(LEGACY_ID_VENDOR_PATH, attrs->idVendor);
	if (ret)
		return ret;

	ret = legacy_set_int_hex(LEGACY_ID_PRODUCT_PATH, attrs->idProduct);
	if (ret)
		return ret;

	ret = legacy_set_int_hex(LEGACY_BCD_DEVICE_PATH, attrs->bcdDevice);

	return ret;
}

static int legacy_set_gadget_strs(struct usb_gadget_strings *strs)
{
	int ret = 0;

	/*
	 * TODO
	 * Here is a good place to ensure that serial is immutable
	 */	

	if (strs->manufacturer) {
		ret = sys_set_str(LEGACY_IMANUFACTURER_PATH,
				  strs->manufacturer);
		if (ret)
			return ret;
	}

	if (strs->product) {
		ret = sys_set_str(LEGACY_IPRODUCT_PATH,
				  strs->product);
		if (ret)
			return ret;
	}

	return ret;
}

static int legacy_set_gadget_config(char *cpath,
				    struct usb_configuration *config)
{
	char buf[MAX_GADGET_STR_LEN];
	int left = sizeof(buf);
	char *pos = buf;
	int ret;
	int i;

	if (!config) {
		buf[0] = '\n';
		buf[1] = '\0';
		goto empty_config;
	}

	for (i = 0; config->funcs[i]; ++i) {
		ret = snprintf(pos, left, "%s" LEGACY_FUNC_SEP,
			       config->funcs[i]->name);
		if (ret >= left)
			return -EOVERFLOW;

		pos += ret;
		left -= ret;
	}

	/* eliminate last separator */
	*(pos - 1) = '\0';

empty_config:
	return sys_set_str(cpath, buf);
}

static int legacy_reconfigure_gadget(struct usb_client *usb,
				     struct usb_gadget *gadget)
{
	int ret;

	if (!usb || !gadget || !legacy_is_gadget_supported(usb, gadget))
		return -EINVAL;

	ret = legacy_set_gadget_attrs(&gadget->attrs);
	if (ret)
		return ret;

	if (gadget->strs) {
		ret = legacy_set_gadget_strs(gadget->strs + 0);
		if (ret)
			return ret;
	}

	ret = legacy_set_gadget_config(LEGACY_CONFIG_1_PATH, gadget->configs[0]);
	if (ret)
		return ret;

	ret = legacy_set_gadget_config(LEGACY_CONFIG_2_PATH, gadget->configs[1]);

	return ret;
}

static int legacy_enable(struct usb_client *usb)
{
	return sys_set_str(LEGACY_ENABLE_PATH,
			   LEGACY_ENABLE);
}

static int legacy_disable(struct usb_client *usb)
{
	return sys_set_str(LEGACY_ENABLE_PATH,
			   LEGACY_DISABLE);	
}

static void legacy_free_config(struct usb_configuration *config)
{
	int i;

	if (!config)
		return;

	if (config->strs) {
		for (i = 0; config->strs[i].lang_code; ++i)
			free(config->strs[i].config_str);

		free(config->strs);
	}

	/*
	 * Each function will be free later,
	 * for now we cleanup only pointers.
	 */
	if (config->funcs)
		free(config->funcs);

	free(config);
}

static void legacy_free_gadget(struct usb_gadget *gadget)
{
	int i;

	if (!gadget)
		return;

	if (gadget->strs) {
		for (i = 0; gadget->strs[i].lang_code; ++i) {
			free(gadget->strs[i].manufacturer);
			free(gadget->strs[i].product);
			free(gadget->strs[i].serial);
		}
		free(gadget->strs);
	}

	if (gadget->configs) {
		for (i = 0; gadget->configs[i]; ++i)
			legacy_free_config(gadget->configs[i]);

		free(gadget->configs);
	}

	if (gadget->funcs) {
		for (i = 0; gadget->funcs[i]; ++i)
			gadget->funcs[i]->free_func(gadget->funcs[i]);

		free(gadget->funcs);
	}
}

static int legacy_gadget_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct usb_client *legacy;

	if (!info || !common)
		return -EINVAL;

	legacy = zalloc(sizeof(*legacy));
	if (!legacy)
		return -ENOMEM;

	legacy->common.info = info;
	legacy->get_current_gadget = legacy_get_current_gadget;
	legacy->reconfigure_gadget = legacy_reconfigure_gadget;
	legacy->is_gadget_supported = legacy_is_gadget_supported;
	legacy->is_function_supported = legacy_is_function_supported;
	legacy->enable = legacy_enable;
	legacy->disable = legacy_disable;
	legacy->free_gadget = legacy_free_gadget;

	*common = &legacy->common;
	return 0;
}

static int legacy_gadget_close(struct hw_common *common)
{
	struct usb_client *legacy;

	if (!common)
		return -EINVAL;

	legacy = container_of(common, struct usb_client,
					 common);

	free(legacy);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = USB_CLIENT_HARDWARE_DEVICE_VERSION,
	.id = USB_CLIENT_HARDWARE_DEVICE_ID,
	.name = "legacy-gadget",
	.open = legacy_gadget_open,
	.close = legacy_gadget_close,
};
