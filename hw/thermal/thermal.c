/*
 * device-node
 *
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
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
#include <glib.h>

#include <hw/thermal.h>
#include <hw/shared.h>

#define AP_PATH		"/sys/class/thermal/thermal_zone0/temp"

static struct event_data {
	ThermalUpdated updated_cb;
	void *data;
} edata = { 0, };

static guint timer;

static int thermal_get_info(device_thermal_e type, struct thermal_info *info)
{
	FILE *fp;
	char buf[32];
	size_t len;

	if (!info)
		return -EINVAL;

	fp = fopen(AP_PATH, "r");
	if (!fp) {
		_E("Failed to open thermal path(%d)", errno);
		return -errno;
	}

	len = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	if (len == 0) {
		_E("Failed to read thermal value(%d)", errno);
		return -errno;
	}
	buf[len] = '\0';
	info->temp = atoi(buf);
	info->temp /= 1000;
	info->adc = 0;

	_I("temp(%d) adc(%d)", info->temp, info->adc);
	return 0;
}

static gboolean thermal_timeout(gpointer data)
{
	struct thermal_info info;
	int ret;

	ret = thermal_get_info(DEVICE_THERMAL_AP, &info);
	if (ret < 0) {
		_E("Failed to read thermal state (%d)", ret);
		return G_SOURCE_CONTINUE;
	}

	if (edata.updated_cb)
		edata.updated_cb(&info, edata.data);

	return G_SOURCE_CONTINUE;
}

static int thermal_register_changed_event(ThermalUpdated updated_cb, void *data)
{
	if (timer)
		g_source_remove(timer);

	timer = g_timeout_add(10000, thermal_timeout, NULL);
	if (timer == 0) {
		_E("Failed to add timer for thermal");
		return -ENOENT;
	}

	edata.updated_cb = updated_cb;
	edata.data = data;

	return 0;
}

static int thermal_unregister_changed_event(ThermalUpdated updated_cb)
{
	if (timer) {
		g_source_remove(timer);
		timer = 0;
	}

	edata.updated_cb = NULL;
	edata.data = NULL;

	return 0;
}

static int thermal_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct thermal_device *thermal_dev;

	if (!info || !common)
		return -EINVAL;

	thermal_dev = calloc(1, sizeof(struct thermal_device));
	if (!thermal_dev)
		return -ENOMEM;

	thermal_dev->common.info = info;
	thermal_dev->register_changed_event
		= thermal_register_changed_event;
	thermal_dev->unregister_changed_event
		= thermal_unregister_changed_event;
	thermal_dev->get_info
		= thermal_get_info;

	*common = (struct hw_common *)thermal_dev;
	return 0;
}

static int thermal_close(struct hw_common *common)
{
	if (!common)
		return -EINVAL;

	free(common);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = THERMAL_HARDWARE_DEVICE_VERSION,
	.id = THERMAL_HARDWARE_DEVICE_ID,
	.name = "thermal",
	.open = thermal_open,
	.close = thermal_close,
};
