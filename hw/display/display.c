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

#include <hw/display.h>
#include "../shared.h"

#ifndef BACKLIGHT_PATH
//#define BACKLIGHT_PATH  "/sys/class/backlight/s6e36w1x01-bl"
#define BACKLIGHT_PATH  "/sys/class/backlight/s6e8fa0"
#endif

#define MAX_BRIGHTNESS_TEMP 100

static int brightness_temp;

static int get_max_brightness(int *val)
{
	static int max = -1;
	int r;

	if (!val)
		return -EINVAL;

	if (max < 0) {
		r = sys_get_int(BACKLIGHT_PATH"/max_brightness", &max);
		if (r < 0) {
			max = MAX_BRIGHTNESS_TEMP;
//			return r;
		}
	}

	*val = max;
	return 0;
}

static int display_get_brightness(int *brightness)
{
	int r, v, max;

	if (!brightness) {
		_E("wrong parameter");
		return -EINVAL;
	}

	r = get_max_brightness(&max);
	if (r < 0) {
		_E("fail to get max brightness (errno:%d)", r);
		return r;
	}

	r = sys_get_int(BACKLIGHT_PATH"/brightness", &v);
	if (r < 0) {
		_E("fail to get brightness (errno:%d)", r);
		v = brightness_temp;
//		return r;
	}

	*brightness = v * 100.f / max;
	return 0;
}

static int display_set_brightness(int brightness)
{
	int r, v, max;

	if (brightness < 0 || brightness > 100) {
		_E("wrong parameter");
		return -EINVAL;
	}

	r = get_max_brightness(&max);
	if (r < 0) {
		_E("fail to get max brightness (errno:%d)", r);
		return r;
	}

	v = brightness/100.f*max;
	r = sys_set_int(BACKLIGHT_PATH"/brightness", v);
	if (r < 0) {
		_E("fail to set brightness (errno:%d)", r);
		brightness_temp = v;
//		return r;
	}

	return 0;
}

static int display_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct display_device *display_dev;

	if (!info || !common)
		return -EINVAL;

	display_dev = calloc(1, sizeof(struct display_device));
	if (!display_dev)
		return -ENOMEM;

	display_dev->common.info = info;
	display_dev->get_brightness = display_get_brightness;
	display_dev->set_brightness = display_set_brightness;

	*common = (struct hw_common *)display_dev;
	return 0;
}

static int display_close(struct hw_common *common)
{
	if (!common)
		return -EINVAL;

	free(common);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = DISPLAY_HARDWARE_DEVICE_VERSION,
	.id = DISPLAY_HARDWARE_DEVICE_ID,
	.name = "Display",
	.open = display_open,
	.close = display_close,
};
