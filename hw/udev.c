/*
 * device-manager
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
#include <stdbool.h>
#include <errno.h>
#include <libudev.h>
#include <glib.h>
#include <string.h>
#include <hw/shared.h>
#include "udev.h"

#define EVENT_KERNEL       "kernel"
#define EVENT_UDEV         "udev"

#define UDEV_MONITOR_SIZE   (128*1024)

struct uevent_info {
	struct udev_monitor *mon;
	GIOChannel *ch;
	guint eventid;
	GList *event_list;
};


/* Uevent */
static struct udev *udev;
static struct uevent_info kevent; /* kernel */
static struct uevent_info uevent; /* udev */

static gboolean uevent_control_cb(GIOChannel *channel,
		GIOCondition cond, void *data)
{
	struct uevent_info *info = data;
	struct udev_device *dev;
	struct uevent_handler *l;
	GList *elem;
	const char *subsystem;
	int len;

	if (!info) {
		_E("data is invalid");
		return TRUE;
	}

	dev = udev_monitor_receive_device(info->mon);
	if (!dev)
		return TRUE;

	subsystem = udev_device_get_subsystem(dev);
	if (!subsystem)
		goto out;

	len = strlen(subsystem);

	for (elem = info->event_list ; elem ; elem = g_list_next(elem)) {
		l = elem->data;
		if (!l)
			continue;
		if (!strncmp(l->subsystem, subsystem, len) &&
		    l->uevent_func)
			l->uevent_func(dev);
	}

out:
	udev_device_unref(dev);
	return TRUE;
}

static int uevent_control_stop(struct uevent_info *info)
{
	struct udev_device *dev;

	if (!info)
		return -EINVAL;

	if (info->eventid) {
		g_source_remove(info->eventid);
		info->eventid = 0;
	}
	if (info->ch) {
		g_io_channel_unref(info->ch);
		info->ch = NULL;
	}
	if (info->mon) {
		dev = udev_monitor_receive_device(info->mon);
		if (dev)
			udev_device_unref(dev);
		udev_monitor_unref(info->mon);
		info->mon = NULL;
	}
	if (udev)
		udev = udev_unref(udev);
	return 0;
}

static int uevent_control_start(const char *type,
		struct uevent_info *info)
{
	struct uevent_handler *l;
	GList *elem;
	int fd;
	int ret;

	if (!info)
		return -EINVAL;

	if (info->mon) {
		_E("%s uevent control routine is alreay started", type);
		return -EINVAL;
	}

	if (!udev) {
		udev = udev_new();
		if (!udev) {
			_E("error create udev");
			return -EINVAL;
		}
	} else
		udev = udev_ref(udev);

	info->mon = udev_monitor_new_from_netlink(udev, type);
	if (info->mon == NULL) {
		_E("error udev_monitor create");
		goto stop;
	}

	_I("Set udev monitor buffer size %d", UDEV_MONITOR_SIZE);
	ret = udev_monitor_set_receive_buffer_size(info->mon,
			UDEV_MONITOR_SIZE);
	if (ret != 0) {
		_E("fail to set receive buffer size");
		goto stop;
	}

	for (elem = info->event_list ; elem ; elem = g_list_next(elem)) {
		l = elem->data;
		ret = udev_monitor_filter_add_match_subsystem_devtype(
				info->mon,
				l->subsystem, NULL);
		if (ret < 0) {
			_E("error apply subsystem filter");
			goto stop;
		}
	}

	ret = udev_monitor_filter_update(info->mon);
	if (ret < 0)
		_E("error udev_monitor_filter_update");

	fd = udev_monitor_get_fd(info->mon);
	if (fd == -1) {
		_E("error udev_monitor_get_fd");
		goto stop;
	}

	info->ch = g_io_channel_unix_new(fd);
	info->eventid = g_io_add_watch(info->ch,
			G_IO_IN, uevent_control_cb, info);
	if (info->eventid == 0) {
		_E("Failed to add channel watch");
		goto stop;
	}

	if (udev_monitor_enable_receiving(info->mon) < 0) {
		_E("error unable to subscribe to udev events");
		goto stop;
	}

	return 0;
stop:
	uevent_control_stop(info);
	return -EINVAL;
}

int uevent_control_kernel_start(void)
{
	return uevent_control_start(EVENT_KERNEL, &kevent);
}

void uevent_control_kernel_stop(void)
{
	uevent_control_stop(&kevent);
}

int uevent_control_udev_start(void)
{
	return uevent_control_start(EVENT_UDEV, &uevent);
}

void uevent_control_udev_stop(void)
{
	uevent_control_stop(&uevent);
}

static int register_uevent_control(struct uevent_info *info,
		struct uevent_handler *uh)
{
	struct uevent_handler *l;
	GList *elem;
	int r;
	bool matched = false;
	int len;

	if (!info || !uh || !uh->subsystem)
		return -EINVAL;

	/* if udev is not initialized, it just will be added list */
	if (!udev || !info->mon)
		goto add_list;

	len = strlen(uh->subsystem);
	/* check if the same subsystem is already added */
	for (elem = info->event_list; elem ; elem = g_list_next(elem)) {
		l = elem->data;
		if (!strncmp(l->subsystem, uh->subsystem, len)) {
			matched = true;
			break;
		}
	}

	/* the first request to add subsystem */
	if (!matched) {
		r = udev_monitor_filter_add_match_subsystem_devtype(info->mon,
				uh->subsystem, NULL);
		if (r < 0) {
			_E("fail to add %s subsystem : %d", uh->subsystem, r);
			return -EPERM;
		}
	}

	r = udev_monitor_filter_update(info->mon);
	if (r < 0)
		_E("fail to update udev monitor filter : %d", r);

add_list:
	info->event_list = g_list_append(info->event_list, uh);
	return 0;
}

static int unregister_uevent_control(struct uevent_info *info,
		const struct uevent_handler *uh)
{
	struct uevent_handler *l;
	GList *n, *next;
	int len;

	if (!info || !uh || !uh->subsystem)
		return -EINVAL;

	len = strlen(uh->subsystem);
	for (n = info->event_list, next = g_list_next(n) ;
			n ; n = next, next = g_list_next(n)) {
		l = n->data;
		if (!strncmp(l->subsystem, uh->subsystem, len) &&
		    l->uevent_func == uh->uevent_func) {
			info->event_list = g_list_delete_link(info->event_list, n);
			return 0;
		}
	}

	return -ENOENT;
}

int register_kernel_event_control(struct uevent_handler *uh)
{
	return register_uevent_control(&kevent, uh);
}

void unregister_kernel_event_control(struct uevent_handler *uh)
{
	unregister_uevent_control(&kevent, uh);
}

int register_udev_event_control(struct uevent_handler *uh)
{
	return register_uevent_control(&uevent, uh);
}

void unregister_udev_event_control(struct uevent_handler *uh)
{
	unregister_uevent_control(&uevent, uh);
}
