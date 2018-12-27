/*
 * device-manager-plugin-artik
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <stdlib.h>
#include <glib.h>

#include <hw/led.h>
#include <hw/shared.h>

#include <peripheral_io.h>

#define GET_BRIGHTNESS(val)	 (((val) >> 24) & 0xFF)

#define GET_TYPE(a)	   (((a) >> 24) & 0xFF)
#define GET_RED_ONLY(a)   ((a) & 0xFF0000)
#define GET_GREEN_ONLY(a) ((a) & 0x00FF00)
#define GET_BLUE_ONLY(a)  ((a) & 0x0000FF)
#define GET_RED_BRT(a)	(uint8_t)(((a) >> 16) & 0xFF)
#define GET_GREEN_BRT(a)  (uint8_t)(((a) >> 8) & 0xFF)
#define GET_BLUE_BRT(a)   (uint8_t)((a) & 0xFF)

/**
 * GPIO Specific
 */
#define GPIO_I2C_BUS_INDEX 1
#define BLINKM_DEFAULT_ADDR 0x09

/**
 * LED operation command code
 * Stop script: 0x6f('o')
 * Set command: 0x6e('n')
 * Change address command: 0x41('A')
 */
#define SET_CMD_CODE	0x6e
#define STOP_SCRIPT_CMD 0x6f
#define CHANGE_ADDR_CMD 0x41

typedef enum _led_rgb_type {
	LED_RED,
	LED_GREEN,
	LED_BLUE,
} led_rgb_type_e;

struct gpio_rgb_play_color_info {
	unsigned int color;
	int time;
};

struct gpio_rgb_play_info {
	GList *play_list;
	int nr_play;
	int index;
	guint timer;
} play_info;

static uint8_t off_cmd[4] = { SET_CMD_CODE, 0x00, 0x00, 0x00 };

static void blinkm_led_stop_script(peripheral_i2c_h handle)
{
	uint8_t data[1] = {STOP_SCRIPT_CMD};
	uint32_t length = 1;

	peripheral_i2c_write(handle, data, length);
}

/**
 * LED set to black
 */
static void blinkm_led_off(peripheral_i2c_h handle)
{
	peripheral_i2c_write(handle, off_cmd, sizeof(off_cmd));
}

static void blinkm_led_init(peripheral_i2c_h handle)
{
	blinkm_led_stop_script(handle);
	blinkm_led_off(handle);
}

static void blinkm_led_set_color(peripheral_i2c_h handle, uint8_t *pkt_data, int len)
{
	peripheral_i2c_write(handle, pkt_data, len);
}

static int gpio_led_open_device(int device_index, peripheral_i2c_h *device_handle)
{
	int ret;

	if ((ret = peripheral_i2c_open(device_index, BLINKM_DEFAULT_ADDR, device_handle)) < PERIPHERAL_ERROR_NONE) {
		_E("Failed to open I2C");
	}
	return ret;
}

static int gpio_led_close_device(peripheral_i2c_h device_handle)
{
	int ret;

	if ((ret = peripheral_i2c_close(device_handle)) < PERIPHERAL_ERROR_NONE) {
		_E("Failed to close I2C");
	}
	return ret;
}

static int gpio_rgb_set_brightness(struct led_state *state)
{
	uint8_t cmd_pkt[4] = { SET_CMD_CODE, };
	peripheral_i2c_h handle;
	unsigned int color;
	int ret;

	if (!state)
		return -EINVAL;

	color = state->color;

	cmd_pkt[1] = GET_RED_BRT(color);
	cmd_pkt[2] = GET_GREEN_BRT(color);
	cmd_pkt[3] = GET_BLUE_BRT(color);

	_I("COLOR(%x) r(%x), g(%x), b(%x)", color, cmd_pkt[1], cmd_pkt[2], cmd_pkt[3]);

	if ((ret = gpio_led_open_device(GPIO_I2C_BUS_INDEX, &handle)) < PERIPHERAL_ERROR_NONE)
		return -EIO;

	blinkm_led_set_color(handle, cmd_pkt, sizeof(cmd_pkt));

	if ((ret = gpio_led_close_device(handle)) < PERIPHERAL_ERROR_NONE)
		return -EIO;

	return 0;
}

/* turn off led */
static int gpio_rgb_turn_off(struct led_state *state)
{
	struct led_state st = { LED_TYPE_MANUAL, };
	return gpio_rgb_set_brightness(&st);
}

/* release play list */
static void free_func(gpointer data)
{
	struct gpio_rgb_play_color_info *color = data;
	free(color);
}

static void release_play_info(void)
{
	if (play_info.play_list) {
		g_list_free_full(play_info.play_list, free_func);
		play_info.play_list = NULL;
	}
	play_info.nr_play = 0;
	play_info.index = 0;
	if (play_info.timer) {
		g_source_remove(play_info.timer);
		play_info.timer = 0;
	}

	gpio_rgb_turn_off(NULL);
}

static int gpio_rgb_init_led()
{
	peripheral_i2c_h handle;
	int ret;

	if ((ret = gpio_led_open_device(GPIO_I2C_BUS_INDEX, &handle)) < PERIPHERAL_ERROR_NONE)
		return ret;

	blinkm_led_init(handle);

	if ((ret = gpio_led_close_device(handle)) < PERIPHERAL_ERROR_NONE)
		return ret;

	return ret;
}

/* timer callback to change colors which are stored in play_list */
static gboolean gpio_rgb_timer_expired(gpointer data)
{
	struct gpio_rgb_play_color_info *color;
	struct led_state state = {LED_TYPE_MANUAL, };
	int ret;

	if (play_info.timer) {
		g_source_remove(play_info.timer);
		play_info.timer = 0;
	}

	color = g_list_nth_data(play_info.play_list, play_info.index);
	if (!color) {
		_E("Failed to get (%d)th item from the play list", play_info.index);
		goto out;
	}

	play_info.timer = g_timeout_add(color->time, gpio_rgb_timer_expired, data);
	if (play_info.timer == 0) {
		_E("Failed to add timeout for LED blinking");
		goto out;
	}

	state.color = color->color;

	ret = gpio_rgb_set_brightness(&state);
	if (ret < 0) {
		_E("Failed to set brightness (%d)", ret);
		goto out;
	}

	play_info.index++;
	if (play_info.index == play_info.nr_play)
		play_info.index = 0;

	return G_SOURCE_CONTINUE;

out:
	release_play_info();

	return G_SOURCE_REMOVE;
}

/* insert color info to the play_list */
static int gpio_rgb_insert_play_list(unsigned color, int on, int off)
{
	struct gpio_rgb_play_color_info *on_info, *off_info;

	if (color == 0)
		return -EINVAL;
	on_info = calloc(1, sizeof(struct gpio_rgb_play_color_info));
	if (!on_info)
		return -ENOMEM;
	off_info = calloc(1, sizeof(struct gpio_rgb_play_color_info));
	if (!off_info) {
		free(on_info);
		return -ENOMEM;
	}

	on_info->color = color;
	on_info->time = on;
	play_info.play_list = g_list_append(play_info.play_list, on_info);

	off_info->color = 0;
	off_info->time = off;
	play_info.play_list = g_list_append(play_info.play_list, off_info);

	return 0;
}

/* insert color info to the play list and start to play */
static int gpio_rgb_set_brightness_blink(struct led_state *state)
{
	unsigned int val;
	int ret;

	val = GET_RED_ONLY(state->color);
	if (val) {
		ret = gpio_rgb_insert_play_list(val, state->duty_on, state->duty_off);
		if (ret < 0)
			_E("Failed to insert color info to list (%d)", ret);
	}
	val = GET_GREEN_ONLY(state->color);
	if (val) {
		ret = gpio_rgb_insert_play_list(val, state->duty_on, state->duty_off);
		if (ret < 0)
			_E("Failed to insert color info to list (%d)", ret);
	}
	val = GET_BLUE_ONLY(state->color);
	if (val) {
		ret = gpio_rgb_insert_play_list(val, state->duty_on, state->duty_off);
		if (ret < 0)
			_E("Failed to insert color info to list (%d)", ret);
	}

	play_info.nr_play = g_list_length(play_info.play_list);
	play_info.index = 0;

	gpio_rgb_timer_expired(NULL);
	return 0;
}

static int gpio_rgb_turn_on(struct led_state *state)
{
	if (state->type == LED_TYPE_MANUAL)
		return gpio_rgb_set_brightness(state);

	return gpio_rgb_set_brightness_blink(state);
}

static int gpio_rgb_set_state(struct led_state *state)
{
	if (!state)
		return -EINVAL;

	switch (state->type) {
	case LED_TYPE_BLINK:
	case LED_TYPE_MANUAL:
		break;
	default:
		_E("Not suppoted type (%d)", state->type);
		return -ENOTSUP;
	}
	release_play_info();

	if (GET_TYPE(state->color) == 0)
		return gpio_rgb_turn_off(state);

	return gpio_rgb_turn_on(state);
}

static int led_open(struct hw_info *info,
		const char *id, struct hw_common **common)
{
	struct led_device *led_dev;
	size_t len;

	if (!info || !id || !common)
		return -EINVAL;

	led_dev = calloc(1, sizeof(struct led_device));
	if (!led_dev)
		return -ENOMEM;

	led_dev->common.info = info;

	len = strlen(id) + 1;
	if (!strncmp(id, LED_ID_NOTIFICATION, len)) {
		if (gpio_rgb_init_led()) {
			free(led_dev);
			return -EIO;
		} else
			led_dev->set_state = gpio_rgb_set_state;

	} else {
		free(led_dev);
		return -ENOTSUP;
	}

	*common = (struct hw_common *)led_dev;
	return 0;
}

static int led_close(struct hw_common *common)
{
	if (!common)
		return -EINVAL;

	free(common);
	return 0;
}

HARDWARE_MODULE_STRUCTURE = {
	.magic = HARDWARE_INFO_TAG,
	.hal_version = HARDWARE_INFO_VERSION,
	.device_version = LED_HARDWARE_DEVICE_VERSION,
	.id = LED_HARDWARE_DEVICE_ID,
	.name = "I2C RGB LED",
	.open = led_open,
	.close = led_close,
};
