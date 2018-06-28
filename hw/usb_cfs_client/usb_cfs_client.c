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
#include <hw/systemd.h>
#include <hw/shared.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <usbg/usbg.h>
#include <unistd.h>

#include <unistd.h>

#define zalloc(amount) calloc(1, amount)

#define MAX_GADGET_STR_LEN 256
#define MAX_FUNCS 32

#define CONFIGFS_PATH "/sys/kernel/config"

#define CONFIGFS_GADGET_NAME "hal-gadget"
#define CONFIGFS_CONFIG_LABEL "hal-config"

#define NAME_INSTANCE_SEP '.'
#define MAX_INSTANCE_LEN 512

#define USB_FUNCS_PATH "/dev/usb-funcs/"

struct cfs_client {
	struct usb_client client;
	usbg_state *ctx;
	usbg_gadget *gadget;
	usbg_udc *udc;
};

/* Based on values in slp-gadget kernel module */
struct usbg_gadget_attrs default_g_attrs = {
	.bcdUSB = 0x0200,
	.idVendor = 0x04e8,
	.idProduct = 0x6860,
	.bcdDevice = 0x0100,
};

struct usbg_gadget_strs default_g_strs = {
	.manufacturer = "Samsung",
	.product = "TIZEN",
	.serial = "01234TEST",
};

static void cfs_free_config(struct usb_configuration *config)
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

static void cfs_free_gadget(struct usb_gadget *gadget)
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
			cfs_free_config(gadget->configs[i]);

		free(gadget->configs);
	}

	if (gadget->funcs) {
		for (i = 0; gadget->funcs[i]; ++i)
			gadget->funcs[i]->free_func(gadget->funcs[i]);

		free(gadget->funcs);
	}
}

static int cfs_read_gadget_attrs_strs(usbg_gadget *gadget,
				      struct usb_gadget *usb_gadget)
{
	struct usbg_gadget_attrs attrs;
	struct usbg_gadget_strs strs;
	int ret;

	ret = usbg_get_gadget_attrs(gadget, &attrs);
	if (ret)
		goto out;

	usb_gadget->attrs.bDeviceClass = attrs.bDeviceClass;
	usb_gadget->attrs.bDeviceSubClass = attrs.bDeviceSubClass;
	usb_gadget->attrs.bDeviceProtocol = attrs.bDeviceProtocol;
	usb_gadget->attrs.idVendor = attrs.idVendor;
	usb_gadget->attrs.idProduct = attrs.idProduct;
	usb_gadget->attrs.bcdDevice = attrs.bcdDevice;


	ret = usbg_get_gadget_strs(gadget, LANG_US_ENG, &strs);
	if (ret)
		goto out;

	usb_gadget->strs[0].manufacturer = strdup(strs.manufacturer);
	usb_gadget->strs[0].product = strdup(strs.product);
	usb_gadget->strs[0].serial = strdup(strs.serial);

	if (!usb_gadget->strs[0].manufacturer ||
	    !usb_gadget->strs[0].product ||
	    !usb_gadget->strs[0].serial) {
		ret = -ENOMEM;
		goto err_strs;
	}

	return 0;
err_strs:
	free(usb_gadget->strs[0].manufacturer);
	free(usb_gadget->strs[0].product);
	free(usb_gadget->strs[0].serial);
out:
	return ret;
}

static bool cfs_match_func(struct usb_function *f,
			 const char *name, const char *instance) {
	if (strcmp(name, usbg_get_function_type_str(USBG_F_FFS))) {
		/* Standard functions */
		if (!strcmp(name, f->name) && !strcmp(instance, f->instance))
			return true;
	} else {
		/* Function with service */
		const char *sep, *fname, *finst;
		int len;

		sep = strchr(instance, NAME_INSTANCE_SEP);
		if (!sep || strlen(sep + 1) < 1)
			return false;

		fname = instance;
		len = sep - instance;
		finst = sep + 1;

		if (strlen(f->name) == len
		    && !strncmp(f->name, fname, len)
		    && !strcmp(f->instance, finst))
			return true;
	}

	return false;
}


static int cfs_find_func(const char *name, const char *instance)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(_available_funcs); ++i)
		if (cfs_match_func(_available_funcs[i], name, instance))
			return i;

	return -ENOENT;
}

static int cfs_alloc_new_func(struct usb_gadget *gadget, const char *fname,
			      const char *instance, struct usb_function **_func)
{
	struct usb_function *func;
	int ret;

	ret = cfs_find_func(fname, instance);
	if (ret < 0)
		return -ENOTSUP;

	ret = _available_funcs[ret]->clone(_available_funcs[ret], &func);
	if (ret)
		return ret;

	*_func = func;
	return 0;
}

static int cfs_read_funcs(usbg_gadget *gadget, struct usb_gadget *usb_gadget)
{
	usbg_function *func;
	int i;
	int ret;

	i = 0;
	usbg_for_each_function(func, gadget) {
		char *func_name = (char *)usbg_get_function_type_str(
					    usbg_get_function_type(func));
		char *instance = (char *)usbg_get_function_instance(func);

		ret = cfs_alloc_new_func(usb_gadget, func_name, instance,
					 usb_gadget->funcs + i);
		if (ret < 0)
			goto clean_prev;
		++i;
	}

	return 0;
clean_prev:
	while (i >= 0) {
		usb_gadget->funcs[i]->free_func(usb_gadget->funcs[i]);
		--i;
	}

	return ret;
}

static struct usb_function *cfs_find_func_in_gadget(
	struct usb_gadget *gadget, const char *name, const char *instance)
{
	int i;

	for (i = 0; gadget->funcs[i]; ++i)
		if (cfs_match_func(gadget->funcs[i], name, instance))
			return gadget->funcs[i];

	return NULL;
}

static int cfs_alloc_config(int n_funcs, struct usb_configuration **_config)
{
	struct usb_configuration *config;

	config = zalloc(sizeof(*config));
	if (!config)
		goto out;

	config->strs = calloc(2, sizeof(*config->strs));
	if (!config->strs)
		goto free_config;

	config->funcs = calloc(n_funcs + 1, sizeof(*config->funcs));
	if (!config->funcs)
		goto free_strs;

	*_config = config;

	return 0;
free_strs:
	free(config->strs);
free_config:
	free(config);
out:
	return -ENOMEM;
}

static int cfs_read_config(usbg_config *config, struct usb_gadget *gadget,
			   struct usb_configuration *usb_config)
{
	usbg_binding *b;
	usbg_function *func;
	char *name, *instance;
	struct usbg_config_attrs c_attrs;
	struct usbg_config_strs c_strs;
	int i = 0;
	int ret;

	usbg_for_each_binding(b, config) {
		func = usbg_get_binding_target(b);

		name = (char *)usbg_get_function_type_str(
			usbg_get_function_type(func));
		instance = (char *)usbg_get_function_instance(func);

		usb_config->funcs[i] = cfs_find_func_in_gadget(gadget,
							       name, instance);
		if (!usb_config->funcs[i]) {
			return -ENOTSUP;
		}
		++i;
	}

	ret = usbg_get_config_attrs(config, &c_attrs);
	if (ret)
		return ret;

	usb_config->attrs.MaxPower = c_attrs.bMaxPower*2;
	usb_config->attrs.bmAttributs = c_attrs.bmAttributes;

	ret = usbg_get_config_strs(config, LANG_US_ENG, &c_strs);
	if (ret) {
		usb_config->strs[0].lang_code = 0;
	} else {
		usb_config->strs[0].lang_code = LANG_US_ENG;
		usb_config->strs[0].config_str = strdup(c_strs.configuration);
		if (!usb_config->strs[0].config_str)
			return -ENOMEM;
	}

	return 0;
}

static int cfs_count_bindings(usbg_config *config)
{
	usbg_binding *b;
	int i = 0;

	usbg_for_each_binding(b, config) ++i;

	return i;
}

static int cfs_read_configs(usbg_gadget *gadget, struct usb_gadget *usb_gadget)
{
	usbg_config *config;
	int i = 0;
	int n_funcs;
	int ret;

	usbg_for_each_config(config, gadget) {
		n_funcs = cfs_count_bindings(config);

		ret = cfs_alloc_config(n_funcs, usb_gadget->configs + i);
		if (ret)
			goto clean_prev;
		ret = cfs_read_config(config, usb_gadget,
				      usb_gadget->configs[i]);
		if (ret)
			goto free_current;

		++i;
	}

	return 0;
free_current:
	free(usb_gadget->configs[i]->strs);
	free(usb_gadget->configs[i]->funcs);
	free(usb_gadget->configs[i]);
clean_prev:
	while (i >= 0)
		cfs_free_config(usb_gadget->configs[i--]);
	return ret;
}

static int cfs_count_configs(usbg_gadget *gadget)
{
	usbg_config *config;
	int i = 0;

	usbg_for_each_config(config, gadget) ++i;

	return i;
}

static int cfs_count_functions(usbg_gadget *gadget)
{
	usbg_function *func;
	int i = 0;

	usbg_for_each_function(func, gadget) ++i;

	return i;
}

static int cfs_get_current_gadget(struct usb_client *usb,
				     struct usb_gadget **_usb_gadget)
{
	struct cfs_client *cfs_client;
	struct usb_gadget *usb_gadget;
	struct usb_gadget_strings *strs;
	struct usb_configuration **usb_configs;
	struct usb_function **usb_funcs;
	int n_funcs, n_configs;
	int i;
	int ret = -ENOMEM;

	if (!usb)
		return -EINVAL;

	cfs_client = container_of(usb, struct cfs_client,
				  client);

	usb_gadget = zalloc(sizeof(*usb_gadget));
	if (!usb_gadget)
		goto out;

	/*
	 * Currently there is no interface in libusbg which
	 * allows to list all string languages.
	 * That's why we do this only for USA english
	 */
	strs = calloc(2, sizeof(*strs));
	if (!strs)
		goto free_gadget;

	strs[0].lang_code = LANG_US_ENG;

	usb_gadget->strs = strs;

	ret = cfs_read_gadget_attrs_strs(cfs_client->gadget, usb_gadget);
	if (ret)
		goto free_strs;


	n_funcs = cfs_count_functions(cfs_client->gadget);
	usb_funcs = calloc(n_funcs + 1, sizeof(*usb_funcs));
	if (!usb_funcs)
		goto free_strs_with_content;

	usb_gadget->funcs = usb_funcs;

	ret = cfs_read_funcs(cfs_client->gadget, usb_gadget);
	if (ret)
		goto free_funcs;

	n_configs = cfs_count_configs(cfs_client->gadget);
	usb_configs = calloc(n_configs + 1, sizeof(*usb_configs));
	if (!usb_configs)
		goto free_funcs_with_content;

	usb_gadget->configs = usb_configs;

	ret = cfs_read_configs(cfs_client->gadget, usb_gadget);
	if (ret)
		goto free_configs;

	*_usb_gadget = usb_gadget;
	return 0;

free_configs:
	free(usb_configs);
free_funcs_with_content:
	for (i = 0; usb_gadget->funcs[i]; ++i)
		usb_gadget->funcs[i]->free_func(usb_gadget->funcs[i]);
free_funcs:
	free(usb_funcs);
free_strs_with_content:
	for (i = 0; usb_gadget->strs[i].lang_code; ++i) {
		free(usb_gadget->strs[i].manufacturer);
		free(usb_gadget->strs[i].product);
		free(usb_gadget->strs[i].serial);
	}
free_strs:
	free(usb_gadget->strs);
free_gadget:
	free(usb_gadget);
out:
	return ret;
}

static bool cfs_is_function_supported(struct usb_client *usb,
					 struct usb_function *func)
{
	bool res;
	int ret;

	switch (func->function_group) {
	case USB_FUNCTION_GROUP_SIMPLE:
		ret = usbg_lookup_function_type(func->name);
		res = ret >= 0;
		break;
	case USB_FUNCTION_GROUP_WITH_SERVICE:
		/* TODO: Check if socket is available */
		res = true;
		break;
	default:
		res = false;
	}

	return res;
}

static bool cfs_is_gadget_supported(struct usb_client *usb,
				       struct usb_gadget *gadget)
{
	int i, j;

	if (!gadget || !gadget->configs || !gadget->funcs)
		return false;

	/*
	 * TODO
	 * Here is a good place to ensure that serial is immutable
	 */

	/* No real restrictions for strings */
	for (j = 0; gadget->configs && gadget->configs[j]; ++j) {
		struct usb_configuration *config = gadget->configs[j];

		if (!config->funcs)
			return false;

		for (i = 0; config->funcs[i]; ++i)
			if (!cfs_is_function_supported(usb, config->funcs[i]))
				return false;
	}

	if (j == 0)
		return false;

	return true;
}

static int cfs_set_gadget_attrs(struct cfs_client *cfs_client,
				struct usb_gadget_attrs *attrs)
{
	int ret;
	struct usbg_gadget_attrs gadget_attrs;

	ret = usbg_get_gadget_attrs(cfs_client->gadget, &gadget_attrs);
	if (ret)
		return ret;

	gadget_attrs.bDeviceClass = attrs->bDeviceClass;
	gadget_attrs.bDeviceSubClass = attrs->bDeviceSubClass;
	gadget_attrs.bDeviceProtocol = attrs->bDeviceProtocol;
	gadget_attrs.idVendor = attrs->idVendor;
	gadget_attrs.idProduct = attrs->idProduct;
	gadget_attrs.bcdDevice = attrs->bcdDevice;

	ret = usbg_set_gadget_attrs(cfs_client->gadget, &gadget_attrs);

	return ret;
}

static int cfs_set_gadget_strs(struct cfs_client *cfs_client,
				  struct usb_gadget_strings *strs)
{
	int ret = 0;

	/*
	 * TODO
	 * Here is a good place to ensure that serial is immutable
	 */
#define SET_STR(FIELD, STR_ID)				\
	if (strs->FIELD) {				\
		ret = usbg_set_gadget_str(cfs_client->gadget,	\
					  STR_ID,		\
					  strs->lang_code,	\
					  strs->FIELD);		\
		if (ret)					\
			return ret;				\
	}

	SET_STR(manufacturer, USBG_STR_MANUFACTURER);
	SET_STR(product, USBG_STR_PRODUCT);
	SET_STR(serial, USBG_STR_SERIAL_NUMBER);
#undef SET_STR
	return ret;
}

static int cfs_ensure_dir(char *path)
{
	int ret;

	ret = mkdir(path, 0770);
	if (ret < 0)
		ret = errno == EEXIST ? 0 : errno;

	return ret;
}

static int cfs_prep_ffs_service(const char *name, const char *instance,
				const char *dev_name, const char *socket_name)
{
	char buf[PATH_MAX];
	size_t left;
	char *pos;
	int ret;

	/* TODO: Add some good error handling */

	left = sizeof(buf);
	pos = buf;
	ret = snprintf(pos, left, "%s", USB_FUNCS_PATH);
	if (ret < 0 || ret >= left) {
		_E("Function path too long");
		return -ENAMETOOLONG;
	} else {
		left -= ret;
		pos += ret;
	}
	ret = cfs_ensure_dir(buf);
	if (ret < 0) {
		_E("Could not create directory %s", buf);
		return ret;
	}

	ret = snprintf(pos, left, "/%s", name);
	if (ret < 0 || ret >= left) {
		_E("Path too long");
		return -ENAMETOOLONG;
	} else {
		left -= ret;
		pos += ret;
	}
	ret = cfs_ensure_dir(buf);
	if (ret < 0) {
		_E("Could not create directory %s", buf);
		return ret;
	}

	ret = snprintf(pos, left, "/%s", instance);
	if (ret < 0 || ret >= left) {
		_E("Path too long");
		return -ENAMETOOLONG;
	} else {
		left -= ret;
		pos += ret;
	}
	ret = cfs_ensure_dir(buf);
	if (ret < 0) {
		_E("Could not create directory %s", buf);
		return ret;
	}

	ret = mount(dev_name, buf, "functionfs", 0, NULL);
	if (ret < 0) {
		_E("Could not mount %s: %m", dev_name);
		return ret;
	}

	ret = systemd_start_socket(socket_name);
	if (ret < 0) {
		_E("Could not start socket: %d", ret);
		goto umount_ffs;
	}

	return 0;
umount_ffs:
	umount(buf);
	return ret;
}

static int cfs_set_gadget_config(struct cfs_client *cfs_client,
				    int config_id,
				    struct usb_configuration *usb_config)
{
	struct usbg_config_attrs cattrs = {
		.bmAttributes = usb_config->attrs.bmAttributs,
		.bMaxPower = usb_config->attrs.MaxPower/2,
	};
	usbg_config *config;
	int i;
	int ret;

	if (!usb_config->funcs || !usb_config->funcs[0])
		return -EINVAL;

	config = usbg_get_config(cfs_client->gadget, config_id, NULL);
	if (config) {
		ret = usbg_rm_config(config, USBG_RM_RECURSE);
		if (ret) {
			_E("Could not remove config %d", config_id);
			return ret;
		}
	}

	ret = usbg_create_config(cfs_client->gadget, config_id,
				 CONFIGFS_CONFIG_LABEL, &cattrs, NULL, &config);
	if (ret) {
		_E("Could not create config %d", config_id);
		return ret;
	}

	for (i = 0; usb_config->strs && usb_config->strs[i].lang_code; ++i) {
		ret = usbg_set_config_string(config, usb_config->strs[i].lang_code,
					     usb_config->strs[i].config_str);
		if (ret) {
			_E("Could not set config string");
			return ret;
		}
	}

	for (i = 0; usb_config->funcs && usb_config->funcs[i]; ++i) {
		struct usb_function *usb_func = usb_config->funcs[i];
		char instance[MAX_INSTANCE_LEN];
		int type;
		usbg_function *func;

		switch (usb_func->function_group) {
		case USB_FUNCTION_GROUP_SIMPLE:
			_I("Adding simple function %s.%s", usb_func->name, usb_func->instance);
			type = usbg_lookup_function_type(usb_func->name);
			if (strlen(usb_func->instance) >= MAX_INSTANCE_LEN)
				return -ENAMETOOLONG;
			strncpy(instance, usb_func->instance, MAX_INSTANCE_LEN);
			instance[MAX_INSTANCE_LEN - 1] = '\0';
			break;
		case USB_FUNCTION_GROUP_WITH_SERVICE:
			_I("Adding function %s.%s with service", usb_func->name, usb_func->instance);
			type = USBG_F_FFS;
			ret = snprintf(instance, sizeof(instance), "%s%c%s",
				       usb_func->name, NAME_INSTANCE_SEP,
				       usb_func->instance);
			if (ret < 0 || ret >= sizeof(instance))
				return -ENAMETOOLONG;
			break;
		default:
			return -EINVAL;
		}


		func = usbg_get_function(cfs_client->gadget, type, instance);
		if (!func) {
			ret = usbg_create_function(cfs_client->gadget,
						   type,
						   instance,
						   NULL, &func);
			if (ret) {
				_E("Could not create function %d %s: %d", type, instance, ret);
				return ret;
			}

			if (usb_func->function_group ==
			    USB_FUNCTION_GROUP_WITH_SERVICE) {
				struct usb_function_with_service *fws;

				fws = container_of(usb_func,
						   struct usb_function_with_service,
						   func);
				ret = cfs_prep_ffs_service(usb_func->name,
							   usb_func->instance,
							   instance,
							   fws->service);
				if (ret) {
					_E("Could not prepare ffs servicef for %s.%s", type, instance);
					return ret;
				}
			}

		}

		ret = usbg_add_config_function(config, NULL, func);
		if (ret) {
			_E("Could not add function to config");
			return ret;
		}
	}

	return ret;
}

static int cfs_cleanup_left_configs(struct cfs_client *cfs_client,
				    int last_config)
{
	usbg_config *lconfig, *config;
	int ret;

	lconfig = usbg_get_config(cfs_client->gadget, last_config, NULL);
	for (config = usbg_get_next_config(lconfig);
	     config;
	     config = usbg_get_next_config(lconfig)) {
		ret = usbg_rm_config(config, USBG_RM_RECURSE);
		if (ret)
			return ret;
	}

	return 0;
}

static int cfs_reconfigure_gadget(struct usb_client *usb,
				  struct usb_gadget *gadget)
{
	struct cfs_client *cfs_client;
	int i;
	int ret;

	if (!usb)
		return -EINVAL;

	cfs_client = container_of(usb, struct cfs_client,
				  client);

	if (!usb || !gadget || !cfs_is_gadget_supported(usb, gadget))
		return -EINVAL;

	ret = cfs_set_gadget_attrs(cfs_client, &gadget->attrs);
	if (ret)
		goto out;

	for (i = 0; gadget->strs && gadget->strs[i].lang_code > 0; ++i) {
		ret = cfs_set_gadget_strs(cfs_client, gadget->strs + i);
		if (ret)
			goto out;
	}

	for (i = 0; gadget->configs && gadget->configs[i]; ++i) {
		ret = cfs_set_gadget_config(cfs_client, i + 1,
					    gadget->configs[i]);
		if (ret)
			goto out;
	}

	/* Workaround for enabling extcon notification on artik */
	ret = usbg_enable_gadget(cfs_client->gadget, cfs_client->udc);
	if (ret) {
		_E("Could not enable gadget");
		goto out;
	}

	ret = cfs_cleanup_left_configs(cfs_client, i);

	/* TODO
	 * Cleanup things which are left after previous gadget
	 */
out:
	return ret;
}

static int cfs_enable(struct usb_client *usb)
{
	struct cfs_client *cfs_client;

	if (!usb)
		return -EINVAL;

	cfs_client = container_of(usb, struct cfs_client,
				  client);

	return usbg_enable_gadget(cfs_client->gadget, cfs_client->udc);
}

static int cfs_disable(struct usb_client *usb)
{
	struct cfs_client *cfs_client;

	if (!usb)
		return -EINVAL;

	cfs_client = container_of(usb, struct cfs_client,
				  client);

	return usbg_disable_gadget(cfs_client->gadget);
}

static int cfs_gadget_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct cfs_client *cfs_client;
	int ret;

	_I("Opening configfs gadget");

	if (!info || !common)
		return -EINVAL;

	/* used exclusively with slp usb_client*/
	if (!access("/sys/class/usb_mode/usb0/enable", F_OK))
		return -ENOENT;

	cfs_client = zalloc(sizeof(*cfs_client));
	if (!cfs_client)
		return -ENOMEM;

	ret = usbg_init(CONFIGFS_PATH, &cfs_client->ctx);
	if (ret) {
		_E("Could not init usbg");
		goto err_usbg_init;
	}

	cfs_client->udc = usbg_get_first_udc(cfs_client->ctx);
	if (!cfs_client->udc) {
		_E("No UDC found by usbg");
		ret = -ENODEV;
		goto err_no_udc;
	}

	ret = usbg_create_gadget(cfs_client->ctx, CONFIGFS_GADGET_NAME,
				 &default_g_attrs, &default_g_strs,
				 &cfs_client->gadget);
	if (ret) {
		_E("Could not create gadget");
		goto err_create_gadget;
	}

	_I("Gadget created");

	cfs_client->client.common.info = info;
	cfs_client->client.get_current_gadget = cfs_get_current_gadget;
	cfs_client->client.reconfigure_gadget = cfs_reconfigure_gadget;
	cfs_client->client.is_gadget_supported = cfs_is_gadget_supported;
	cfs_client->client.is_function_supported = cfs_is_function_supported;
	cfs_client->client.enable = cfs_enable;
	cfs_client->client.disable = cfs_disable;
	cfs_client->client.free_gadget = cfs_free_gadget;

	*common = &cfs_client->client.common;
	return 0;

err_create_gadget:
err_no_udc:
	usbg_cleanup(cfs_client->ctx);
err_usbg_init:
	free(cfs_client);

	return ret;
}

static int cfs_gadget_close(struct hw_common *common)
{
	struct cfs_client *cfs_client;

	if (!common)
		return -EINVAL;

	cfs_client = container_of(common, struct cfs_client,
				  client.common);

	/*
	 * For now we don't check for errors
	 * but we should somehow handle them
	 */
	usbg_rm_gadget(cfs_client->gadget, USBG_RM_RECURSE);
	usbg_cleanup(cfs_client->ctx);
	free(cfs_client);

	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = USB_CLIENT_HARDWARE_DEVICE_VERSION,
	.id = USB_CFS_CLIENT_HARDWARE_DEVICE_ID,
	.name = "cfs-gadget",
	.open = cfs_gadget_open,
	.close = cfs_gadget_close,
};
