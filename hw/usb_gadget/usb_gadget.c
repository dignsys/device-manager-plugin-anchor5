/*
 * libdevice-node
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

#ifndef __HW_USB_GADGET_SIMPLE_TRANSLATOR_H__

#include <hw/usb_gadget.h>

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#define zalloc(amount) calloc(1, amount)

/* Based on slp-gadget and initial version of USB HAL by Taeyoung Kim */
#define DEFAULT_VID 0x04e8
#define DEFAULT_PID 0x6860
#define DEFAULT_BCD_DEVICE 0xffff

#define DEFAULT_LANG 0x409 /* US_en */
#define DEFAULT_MANUFACTURER "Samsung"
#define DEFAULT_PRODUCT "TIZEN"
#define DEFAULT_SERIAL "01234TEST"

#define DEFAULT_BMATTRIBUTES ((1 << 7) | (1 << 6))
#define DEFAULT_MAX_POWER 500

static void simple_cleanup_config(struct usb_configuration *config)
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

static void simple_cleanup_gadget(struct usb_gadget *gadget)
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
			simple_cleanup_config(gadget->configs[i]);

		free(gadget->configs);
	}

	if (gadget->funcs) {
		for (i = 0; gadget->funcs[i]; ++i)
			gadget->funcs[i]->free_func(gadget->funcs[i]);

		free(gadget->funcs);
	}

	free(gadget);
}

static int alloc_default_config(struct usb_configuration **_config)
{
	struct usb_configuration *config;

	config = zalloc(sizeof(*config));
	if (!config)
		goto out;

	config->strs = calloc(1, sizeof(*config->strs));
	if (!config->strs)
		goto free_config;

	config->attrs.bmAttributs = DEFAULT_BMATTRIBUTES;
	config->attrs.MaxPower = DEFAULT_MAX_POWER;

	*_config = config;

	return 0;

free_config:
	free(config);
out:
	return -ENOMEM;
}

static int alloc_default_gadget(struct usb_gadget **_gadget)
{
	struct usb_gadget *gadget;
	struct usb_gadget_strings *strs;
	struct usb_configuration **configs;

	gadget = zalloc(sizeof(*gadget));
	if (!gadget)
		goto out;

	gadget->attrs.idVendor = DEFAULT_VID;
	gadget->attrs.idProduct = DEFAULT_PID;
	gadget->attrs.bcdDevice = DEFAULT_BCD_DEVICE;

	strs = calloc(2, sizeof(*strs));
	if (!strs)
		goto free_gadget;

	strs[0].lang_code = 0x409;
	strs[0].manufacturer = strdup(DEFAULT_MANUFACTURER);
	strs[0].product = strdup(DEFAULT_PRODUCT);
	strs[0].serial = strdup(DEFAULT_SERIAL);

	if (!strs[0].manufacturer || !strs[0].product || !strs[0].serial)
		goto free_strs;

	gadget->strs = strs;

	/* slp-gadget use max 2 confiuration and NULL termination */
	configs = calloc(3, sizeof(*configs));
	if (!configs)
		goto free_strs;

	gadget->configs = configs;
	*_gadget = gadget;

	return 0;

free_strs:
	free(strs[0].manufacturer);
	free(strs[0].product);
	free(strs[0].serial);
	free(strs);
free_gadget:
	free(gadget);
out:
	return -ENOMEM;
}

static inline struct usb_function *find_func(struct usb_gadget *gadget,
					    int func_id)
{
	int i;

	for (i = 0; gadget->funcs[i] && gadget->funcs[i]->id != func_id; ++i);

	return gadget->funcs[i];
}

static int simple_id_to_gadget(struct usb_gadget_id *gadget_id,
			       struct usb_gadget **_gadget)
{
	struct usb_gadget *gadget;
	unsigned int n_configs = 0;
	/* zero terminates */
	int functions[2][sizeof(gadget_id->function_mask)*8];
	int n_functions;
	struct usb_function **funcs;
	int idx, i, j;
	int ret;

	if (!gadget_id || !_gadget)
		return -EINVAL;

	ret = alloc_default_gadget(&gadget);
	if (ret)
		goto out;

	/*
	 * Currently all gadgets use inly single configuration but
	 * slp-gadget is capable to handle two of them
	 *
	 * Order of interfaces in configuration is significant
	 * so in this switch we sort our functions in a correct order
	 */
	switch (gadget_id->function_mask) {
	case USB_FUNCTION_SDB:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_SDB;
		functions[0][1] = 0;
		gadget->attrs.idProduct = 0x685d;
		break;
	case USB_FUNCTION_MTP:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_MTP;
		functions[0][1] = 0;
		gadget->attrs.idProduct = 0x6860;
		break;
	case USB_FUNCTION_RNDIS:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_RNDIS;
		functions[0][1] = 0;
		gadget->attrs.idProduct = 0x6863;
		break;
	case USB_FUNCTION_MTP | USB_FUNCTION_ACM | USB_FUNCTION_SDB:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_MTP;
		functions[0][1] = USB_FUNCTION_ACM;
		functions[0][2] = USB_FUNCTION_SDB;
		functions[0][3] = 0;
		gadget->attrs.idProduct = 0x6860;
		break;
	case USB_FUNCTION_MTP | USB_FUNCTION_ACM | USB_FUNCTION_SDB
		| USB_FUNCTION_DIAG:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_MTP;
		functions[0][1] = USB_FUNCTION_ACM;
		functions[0][2] = USB_FUNCTION_SDB;
		functions[0][3] = USB_FUNCTION_DIAG;
		functions[0][4] = 0;
		gadget->attrs.idProduct = 0x6860;
		break;
	case USB_FUNCTION_RNDIS | USB_FUNCTION_SDB:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_RNDIS;
		functions[0][1] = USB_FUNCTION_SDB;
		functions[0][2] = 0;
		gadget->attrs.idProduct = 0x6864;
		break;
	case USB_FUNCTION_RNDIS | USB_FUNCTION_SDB | USB_FUNCTION_ACM | USB_FUNCTION_DIAG:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_RNDIS;
		functions[0][1] = USB_FUNCTION_SDB;
		functions[0][2] = USB_FUNCTION_ACM;
		functions[0][3] = USB_FUNCTION_DIAG;
		functions[0][4] = 0;
		gadget->attrs.idProduct = 0x6864;
		break;
	case USB_FUNCTION_RNDIS | USB_FUNCTION_DIAG:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_RNDIS;
		functions[0][1] = USB_FUNCTION_DIAG;
		functions[0][2] = 0;
		gadget->attrs.idProduct = 0x6864;
		break;
	case USB_FUNCTION_ACM | USB_FUNCTION_SDB | USB_FUNCTION_DM:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_ACM;
		functions[0][1] = USB_FUNCTION_SDB;
		functions[0][2] = USB_FUNCTION_DM;
		functions[0][3] = 0;
		gadget->attrs.idProduct = 0x6860;
		break;
	case USB_FUNCTION_DIAG | USB_FUNCTION_ACM | USB_FUNCTION_RMNET:
		n_configs = 1;
		functions[0][0] = USB_FUNCTION_DIAG;
		functions[0][1] = USB_FUNCTION_ACM;
		functions[0][2] = USB_FUNCTION_RMNET;
		functions[0][3] = 0;
		gadget->attrs.idProduct = 0x685d;
		break;
	};

	if (n_configs > 2 || n_configs == 0) {
		ret = -EINVAL;
		goto free_gadget;
	}

	n_functions = __builtin_popcount(gadget_id->function_mask);

	funcs = calloc(n_functions + 1, sizeof(*funcs));
	if (!funcs) {
		ret = -ENOMEM;
		goto free_gadget;
	}

	gadget->funcs = funcs;

	idx = 0;
	for (i = 0; i < ARRAY_SIZE(_available_funcs); ++i) {
		int func_id = 1 << i;

		if (!(gadget_id->function_mask & func_id))
			continue;

		ret = _available_funcs[i]->clone(_available_funcs[i],
			gadget->funcs + idx);
		if (ret)
			goto free_functions;
		++idx;
	}

	for (j = 0; j < n_configs; ++j) {
		struct usb_configuration *config;
		int n_funcs_in_config;

		for (i = 0; functions[j][i]; ++i);
		n_funcs_in_config = i;

		ret = alloc_default_config(&config);
		if (ret)
			goto free_configs;

		gadget->configs[j] = config;
		config->funcs = calloc(n_funcs_in_config + 1,
						       sizeof(void *));
		if (!config->funcs)
			goto free_configs;

		for (i = 0; functions[j][i]; ++i)
			config->funcs[i] = find_func(gadget, functions[j][i]);
	}

	*_gadget = gadget;
	return 0;
free_configs:
free_functions:
free_gadget:
	simple_cleanup_gadget(gadget);
out:
	return ret;
}

static int simple_translator_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct usb_gadget_translator *simple_translator;

	if (!info || !common)
		return -EINVAL;

	simple_translator = zalloc(sizeof(*simple_translator));
	if (!simple_translator)
		return -ENOMEM;

	simple_translator->common.info = info;
	simple_translator->id_to_gadget = simple_id_to_gadget;
	simple_translator->cleanup_gadget = simple_cleanup_gadget;

	*common = &simple_translator->common;
	return 0;
}

static int simple_translator_close(struct hw_common *common)
{
	struct usb_gadget_translator *simple_translator;

	if (!common)
		return -EINVAL;

	simple_translator = container_of(common, struct usb_gadget_translator,
					 common);

	free(simple_translator);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = USB_GADGET_DEVICE_VERSION,
	.id = USB_GADGET_DEVICE_ID,
	.name = "simple_translator",
	.open = simple_translator_open,
	.close = simple_translator_close,
};

#endif
