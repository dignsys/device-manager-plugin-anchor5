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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define BUF_MAX		255

static int sys_read_buf(char *file, char *buf, int len)
{
	int fd, r;

	if (!file || !buf || len < 0)
		return -EINVAL;

	fd = open(file, O_RDONLY);
	if (fd == -1)
		return -ENOENT;

	r = read(fd, buf, len);
	close(fd);
	if ((r >= 0) && (r < len))
		buf[r] = '\0';
	else
		return -EIO;

	return 0;
}

static int sys_write_buf(char *file, char *buf)
{
	int fd, r;

	if (!file || !buf)
		return -EINVAL;

	fd = open(file, O_WRONLY);
	if (fd == -1)
		return -EPERM;

	r = write(fd, buf, strlen(buf));
	close(fd);
	if (r < 0)
		return -EIO;

	return 0;
}

int sys_get_int(char *fname, int *val)
{
	char buf[BUF_MAX];
	int r;

	if (!fname || !val)
		return -EINVAL;

	r = sys_read_buf(fname, buf, sizeof(buf));
	if (r < 0)
		return r;

	*val = atoi(buf);
	return 0;
}

int sys_get_str(char *fname, char *str, int len)
{
	int r;

	if (!fname || !str || len < 0)
		return -EINVAL;

	r = sys_read_buf(fname, str, len);
	if (r < 0)
		return r;

	return 0;
}

int sys_set_int(char *fname, int val)
{
	char buf[BUF_MAX];
	int r;

	if (!fname)
		return -EINVAL;

	snprintf(buf, sizeof(buf), "%d", val);
	r = sys_write_buf(fname, buf);
	if (r < 0)
		return r;

	return 0;
}

int sys_set_str(char *fname, char *val)
{
	int r;

	if (!fname || !val)
		return -EINVAL;

	r = sys_write_buf(fname, val);
	if (r < 0)
		return r;

	return 0;
}
