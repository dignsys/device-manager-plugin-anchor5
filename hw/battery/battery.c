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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <dirent.h>

#include <hw/battery.h>
#include "../shared.h"
#include "../udev.h"

#define BATTERY_ROOT_PATH "/sys/class/power_supply"

static struct uevent_data {
	BatteryUpdated updated_cb;
	void *data;
} udata = { 0, };

static int get_power_source(char **src)
{
	int ret, val;

	if (!src)
		return -EINVAL;

	ret = sys_get_int(BATTERY_ROOT_PATH"/"POWER_SOURCE_AC"/online", &val);
	if (ret >= 0 && val > 0) {
		*src = POWER_SOURCE_AC;
		return 0;
	}

	ret = sys_get_int(BATTERY_ROOT_PATH"/"POWER_SOURCE_USB"/online", &val);
	if (ret >= 0 && val > 0) {
		*src = POWER_SOURCE_USB;
		return 0;
	}

	ret = sys_get_int(BATTERY_ROOT_PATH"/"POWER_SOURCE_WIRELESS"/online", &val);
	if (ret >= 0 && val > 0) {
		*src = POWER_SOURCE_WIRELESS;
		return 0;
	}

	*src = POWER_SOURCE_NONE;

	return 0;
}

static void remove_not_string(char *str)
{
	char *t = str;

	while(*t != '\0') {
		if (*t == '\r' ||
			*t == '\n' ||
			*t == '\x0a')
			*t = '\0';
		else
			t++;
	}
}

static void uevent_delivered(struct udev_device *dev)
{
	struct battery_info info;
	char *val;
	int ret;

	_I("POWER_SUPPLY uevent is delivered");

	if (!udata.updated_cb) {
		_E("POWER_SUPPLY callback is NULL");
		return;
	}

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_NAME");
	if (!val)
		return;
	info.name = val;

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_STATUS");
	if (!val)
		return;
	info.status = val;

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_HEALTH");
	if (!val)
		return;
	info.health = val;

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_ONLINE");
	if (!val)
		return;
	info.online = atoi(val);

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_PRESENT");
	if (!val)
		return;
	info.present = atoi(val);

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_CAPACITY");
	if (!val)
		return;
	info.capacity = atoi(val);

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_CURRENT_NOW");
	if (!val)
		return;
	info.current_now = atoi(val); /* uA */

	val = (char *)udev_device_get_property_value(dev, "POWER_SUPPLY_CURRENT_AVG");
	if (!val)
		return;
	info.current_average = atoi(val); /* uA */

	ret = get_power_source(&val);
	if (ret < 0)
		return;
	info.power_source = val;

	udata.updated_cb(&info, udata.data);
}

static struct uevent_handler uh = {
	.subsystem = "power_supply",
	.uevent_func = uevent_delivered,
};

static int battery_register_changed_event(
		BatteryUpdated updated_cb, void *data)
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

static void battery_unregister_changed_event(
		BatteryUpdated updated_cb)
{
	unregister_kernel_event_control(&uh);
	uevent_control_kernel_stop();
	udata.updated_cb = NULL;
	udata.data = NULL;
}

static int battery_get_current_state(
		BatteryUpdated updated_cb, void *data)
{
	int ret, val;
	struct battery_info info;
	char *path;
	char status[32];
	char health[32];
	char *power_source;

	if (!updated_cb)
		return -EINVAL;

	info.name = BATTERY_HARDWARE_DEVICE_ID;

	path = BATTERY_ROOT_PATH"/battery/status";
	ret = sys_get_str(path, status, sizeof(status));
	if (ret < 0) {
		_E("Failed to get value of (%s, %d)", path, ret);
		return ret;
	}
	remove_not_string(status);
	info.status = status;

	path = BATTERY_ROOT_PATH"/battery/health";
	ret = sys_get_str(path, health, sizeof(health));
	if (ret < 0) {
		_E("Failed to get value of (%s, %d)", path, ret);
		return ret;
	}
	remove_not_string(health);
	info.health = health;

	ret = get_power_source(&power_source);
	if (ret < 0) {
		_E("Failed to get power source (%d)", ret);
		return ret;
	}
	remove_not_string(power_source);
	info.power_source = power_source;

	path = BATTERY_ROOT_PATH"/battery/online";
	ret = sys_get_int(path, &val);
	if (ret < 0) {
		_E("Failed to get value of (%s, %d)", path, ret);
		return ret;
	}
	info.online = val;

	path = BATTERY_ROOT_PATH"/battery/present";
	ret = sys_get_int(path, &val);
	if (ret < 0) {
		_E("Failed to get value of (%s, %d)", path, ret);
		return ret;
	}
	info.present = val;

	path = BATTERY_ROOT_PATH"/battery/capacity";
	ret = sys_get_int(path, &val);
	if (ret < 0) {
		_E("Failed to get value of (%s, %d)", path, ret);
		return ret;
	}
	info.capacity = val;

	path = BATTERY_ROOT_PATH"/battery/current_now";
	ret = sys_get_int(path, &val);
	if (ret < 0) {
		_E("Failed to get value of (%s, %d)", path, ret);
		return ret;
	}
	info.current_now = val;

	path = BATTERY_ROOT_PATH"/battery/current_avg";
	ret = sys_get_int(path, &val);
	if (ret < 0) {
		_E("Failed to get value of (%s, %d)", path, ret);
		return ret;
	}
	info.current_average = val;

	updated_cb(&info, data);

	return 0;
}

static int battery_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct battery_device *battery_dev;

	if (!info || !common)
		return -EINVAL;

	battery_dev = calloc(1, sizeof(struct battery_device));
	if (!battery_dev)
		return -ENOMEM;

	battery_dev->common.info = info;
	battery_dev->register_changed_event
		= battery_register_changed_event;
	battery_dev->unregister_changed_event
		= battery_unregister_changed_event;
	battery_dev->get_current_state
		= battery_get_current_state;

	*common = (struct hw_common *)battery_dev;
	return 0;
}

static int battery_close(struct hw_common *common)
{
	if (!common)
		return -EINVAL;

	free(common);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = BATTERY_HARDWARE_DEVICE_VERSION,
	.id = BATTERY_HARDWARE_DEVICE_ID,
	.name = "battery",
	.open = battery_open,
	.close = battery_close,
};
