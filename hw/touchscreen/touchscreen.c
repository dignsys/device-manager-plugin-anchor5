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

#include <hw/touchscreen.h>
#include <hw/shared.h>

#define INPUT_PATH      "/sys/class/input/"
#define KEY_CAPABILITIES_PATH  "/device/capabilities/key"
#define ENABLED_PATH           "/device/enabled"
#define TOUCHSCREEN_CAPABILITY 400

#define TURNON_TOUCHSCREEN     1
#define TURNOFF_TOUCHSCREEN    0

static char *touchscreen_node;

static int touchscreen_probe(void)
{
	DIR *d;
	struct dirent *dir;
	char buf[PATH_MAX];
	int val, ret = -ENOTSUP;

	d = opendir(INPUT_PATH);
	if (!d)
		return -errno;

	while ((dir = readdir(d))) {
		if (dir->d_name[0] == '.')
			continue;
		snprintf(buf, sizeof(buf), "%s%s%s", INPUT_PATH,
				dir->d_name, KEY_CAPABILITIES_PATH);

		ret = sys_get_int(buf, &val);
		if (ret < 0 || val != TOUCHSCREEN_CAPABILITY)
			continue;

		snprintf(buf, sizeof(buf), "%s%s%s", INPUT_PATH,
				dir->d_name, ENABLED_PATH);

		touchscreen_node = strndup(buf, strlen(buf));
		if (touchscreen_node) {
			_I("touchscreen node (%s)", touchscreen_node);
			ret = 0;
		} else {
			_E("strndup() failed");
			ret = -ENOMEM;
		}
		break;
	}
	closedir(d);

	return ret;
}

static int touchscreen_get_state(enum touchscreen_state *state)
{
	int ret, val;

	if (!touchscreen_node || !(*touchscreen_node))
		return -ENOENT;

	if (!state)
		return -EINVAL;

	ret = sys_get_int(touchscreen_node, &val);
	if (ret < 0) {
		_E("Failed to get touchscreen state (%d)", ret);
		return ret;
	}

	switch (val) {
	case TURNOFF_TOUCHSCREEN:
		*state = TOUCHSCREEN_OFF;
		break;
	case TURNON_TOUCHSCREEN:
		*state = TOUCHSCREEN_ON;
		break;
	default:
		_E("Failed to get touchscreen state (%d)", val);
		return -EINVAL;
	}

	return 0;
}

static int touchscreen_set_state(enum touchscreen_state state)
{
	int ret, val;

	if (!touchscreen_node || !(*touchscreen_node))
		return -ENOENT;

	switch (state) {
	case TOUCHSCREEN_OFF:
		val = TURNOFF_TOUCHSCREEN;
		break;
	case TOUCHSCREEN_ON:
		val = TURNON_TOUCHSCREEN;
		break;
	default:
		_E("Invalid input (%d)", state);
		return -EINVAL;
	}

	ret = sys_set_int(touchscreen_node, val);
	if (ret < 0)
		_E("Failed to change touchscreen state (%d)", ret);

	return ret;
}

static int touchscreen_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct touchscreen_device *touchscreen_dev;

	if (!info || !common)
		return -EINVAL;

	if (touchscreen_probe() < 0)
		return -ENOTSUP;

	touchscreen_dev = calloc(1, sizeof(struct touchscreen_device));
	if (!touchscreen_dev)
		return -ENOMEM;

	touchscreen_dev->common.info = info;
	touchscreen_dev->get_state = touchscreen_get_state;
	touchscreen_dev->set_state = touchscreen_set_state;

	*common = (struct hw_common *)touchscreen_dev;
	return 0;
}

static int touchscreen_close(struct hw_common *common)
{
	if (!common)
		return -EINVAL;

	free(common);
	free(touchscreen_node);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = TOUCHSCREEN_HARDWARE_DEVICE_VERSION,
	.id = TOUCHSCREEN_HARDWARE_DEVICE_ID,
	.name = "touchscreen",
	.open = touchscreen_open,
	.close = touchscreen_close,
};
