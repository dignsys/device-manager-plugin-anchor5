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


#ifndef __UDEV_H__
#define __UDEV_H__

#include <libudev.h>

struct uevent_handler {
	const char *subsystem;
	void (*uevent_func)(struct udev_device *dev);
	void *data;
};

int uevent_control_kernel_start(void);
void uevent_control_kernel_stop(void);

int uevent_control_udev_start(void);
void uevent_control_udev_stop(void);

int register_kernel_event_control(struct uevent_handler *uh);
void unregister_kernel_event_control(struct uevent_handler *uh);

int register_udev_event_control(struct uevent_handler *uh);
void  unregister_udev_event_control(struct uevent_handler *uh);

#endif /* __UDEV_H__ */
