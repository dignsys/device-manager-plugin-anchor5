/*
 * device-node
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <dirent.h>

#include <hw/external_connection.h>
#include "../shared.h"
#include "../udev.h"

#define SWITCH_ROOT_PATH "/sys/devices/virtual/switch"

static struct switch_device {
	char *type;
	char *name;
	int state;
} switch_devices[] = {
	{ EXTERNAL_CONNECTION_USB,       "usb_cable", 0 },
	{ EXTERNAL_CONNECTION_DOCK,      "dock"     , 0 },
	{ EXTERNAL_CONNECTION_HEADPHONE, "earjack"  , 0 },
};

static struct uevent_data {
	ConnectionUpdated updated_cb;
	void *data;
} udata = { 0, };

static void uevent_delivered(struct udev_device *dev)
{
	struct connection_info info;
	char *name, *state;
	int i;

	_I("Switch uevent is delivered");

	name = (char *)udev_device_get_property_value(dev, "SWITCH_NAME");
	if (!name)
		return;

	state = (char *)udev_device_get_property_value(dev, "SWITCH_STATE");
	if (!state)
		return;

	for (i = 0 ; i < ARRAY_SIZE(switch_devices) ; i++) {
		if (strncmp(name, switch_devices[i].name, strlen(name) + 1))
			continue;

		switch_devices[i].state = atoi(state);

		info.name = switch_devices[i].type;
		info.state = state;
		info.flags = 0;

		if (udata.updated_cb)
			udata.updated_cb(&info, udata.data);
		else
			_E("callback is NULL");
	}
}

static struct uevent_handler uh = {
	.subsystem = "switch",
	.uevent_func = uevent_delivered,
};

static int external_connection_register_changed_event(
		ConnectionUpdated updated_cb, void *data)
{
	int ret;

	ret = uevent_control_kernel_start();
	if (ret < 0) {
		_E("Failed to register uevent handler (%d)", ret);
		return ret;
	}

	ret = register_kernel_event_control(&uh);
	if (ret < 0)
		_E("Failed to register kernel event control (%d)", ret);

	if (udata.updated_cb == NULL) {
		udata.updated_cb = updated_cb;
		udata.data = data;
	} else
		_E("update callback is already registered");

	return ret;
}

static void external_connection_unregister_changed_event(
		ConnectionUpdated updated_cb)
{
	unregister_kernel_event_control(&uh);
	uevent_control_kernel_stop();
	udata.updated_cb = NULL;
	udata.data = NULL;
}

static int read_switch_state(char *path)
{
	char node[128], val[8];
	FILE *fp;

	if (!path)
		return -EINVAL;

	snprintf(node, sizeof(node), "%s/%s/state",
			SWITCH_ROOT_PATH, path);

	fp = fopen(node, "r");
	if (!fp) {
		_E("Failed to open (%s)", path);
		return -ENOMEM;
	}

	if (!fgets(val, sizeof(val), fp)) {
		_E("Failed to read (%s)", path);
		fclose(fp);
		return -ENOENT;
	}

	fclose(fp);

	return atoi(val);
}

static int external_connection_get_current_state(
		ConnectionUpdated updated_cb, void *data)
{
	int ret, i;
	struct connection_info info;
	char buf[8];

	if (!updated_cb)
		return -EINVAL;

	for (i = 0 ; i < ARRAY_SIZE(switch_devices) ; i++) {
		ret = read_switch_state(switch_devices[i].name);
		if (ret < 0) {
			_E("Failed to get value of (%s, ret:%d)",
					switch_devices[i].name, ret);
			continue;
		}

		info.name = switch_devices[i].type;
		snprintf(buf, sizeof(buf), "%d", ret);
		info.state = buf;

		updated_cb(&info, data);
	}

	return 0;
}

static int external_connection_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct external_connection_device *external_connection_dev;

	if (!info || !common)
		return -EINVAL;

	external_connection_dev = calloc(1, sizeof(struct external_connection_device));
	if (!external_connection_dev)
		return -ENOMEM;

	external_connection_dev->common.info = info;
	external_connection_dev->register_changed_event
		= external_connection_register_changed_event;
	external_connection_dev->unregister_changed_event
		= external_connection_unregister_changed_event;
	external_connection_dev->get_current_state
		= external_connection_get_current_state;

	*common = (struct hw_common *)external_connection_dev;
	return 0;
}

static int external_connection_close(struct hw_common *common)
{
	if (!common)
		return -EINVAL;

	free(common);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = EXTERNAL_CONNECTION_HARDWARE_DEVICE_VERSION,
	.id = EXTERNAL_CONNECTION_HARDWARE_DEVICE_ID,
	.name = "external_connection",
	.open = external_connection_open,
	.close = external_connection_close,
};
