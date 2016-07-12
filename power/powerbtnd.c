/**
 * A daemon to simulate power button of Android
 *
 * Copyright (C) 2011-2012 The Android-x86 Open Source Project
 *
 * by Chih-Wei Huang <cwhuang@linux.org.tw>
 *
 * Licensed under GPLv2 or later
 *
 **/

#define LOG_TAG "powerbtn"

#include <sys/stat.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <cutils/log.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <cutils/properties.h>

const int MAX_POWERBTNS = 30;

int openfds(struct pollfd pfds[])
{
	int cnt = 0;
	const char *dirname = "/dev/input";
	DIR *dir;
	if ((dir = opendir(dirname))) {
		int fd;
		struct dirent *de;
		while ((de = readdir(dir))) {
			if (de->d_name[0] != 'e') // eventX
				continue;
			char name[PATH_MAX];
			snprintf(name, PATH_MAX, "%s/%s", dirname, de->d_name);
			fd = open(name, O_RDWR | O_NONBLOCK);
			if (fd < 0) {
				ALOGE("could not open %s, %s", name, strerror(errno));
				continue;
			}
			name[sizeof(name) - 1] = '\0';
			if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), &name) < 1) {
				ALOGE("could not get device name for %s, %s", name, strerror(errno));
				name[0] = '\0';
			}

			// TODO: parse /etc/excluded-input-devices.xml
			if (!strcmp(name, "Power Button") ||
// other devices I'm planning to handle
                            !strcmp(name, "Dell WMI hotkeys") ||
                            !strcmp(name, "DELL Wireless hotkeys") ||
                            !strcmp(name, "Sleep Button") ||
			    !strcmp(name, "Dell Venue 11 Pro Tablet") ||
			    !strcmp(name, "Lid Switch")) {
				ALOGI("open %s(%s) ok fd=%d", de->d_name, name, fd);
				pfds[cnt].events = POLLIN;
				pfds[cnt++].fd = fd;
				if (cnt < MAX_POWERBTNS)
					continue;
				else
					break;
			}
			else
			{
				ALOGI("skip %s(%s) ok fd=%d", de->d_name, name, fd);
			}
			close(fd);
		}
		closedir(dir);
	}

	return cnt;
}

void send_power(int ufd, int down)
{
	struct input_event iev;
	iev.type  = EV_KEY;
	iev.code  = KEY_POWER;
	iev.value = down;
	write(ufd, &iev, sizeof(iev));
	iev.type  = EV_SYN;
	iev.code  = SYN_REPORT;
	iev.value = 0;
	write(ufd, &iev, sizeof(iev));
}

void simulate_powerkey(int ufd, int longpress)
{
	if (longpress)
	{
		send_power(ufd, 1);
		sleep(2);
		send_power(ufd, 0);
// for some reason --longpress has same effect as no --longpress
//		system("input keyevent --longpress 26");
	}
	else
	{
		system("input keyevent 26");
	}
// this always is treated as longpress by framework, so, the above is a way to make it work
//	send_power(ufd, 1);
//	if (longpress)
//		sleep(2);
//	send_power(ufd, 0);
}

int main()
{
	struct pollfd pfds[MAX_POWERBTNS];
	int cnt = openfds(pfds);
	int timeout = -1;
	int shortpress = 1;
	char prop[PROPERTY_VALUE_MAX];

	int ufd = open("/dev/uinput", O_WRONLY | O_NDELAY);
	if (ufd >= 0) {
		struct uinput_user_dev ud;
		memset(&ud, 0, sizeof(ud));
		strcpy(ud.name, "Android Power Button");
		write(ufd, &ud, sizeof(ud));
		ioctl(ufd, UI_SET_EVBIT, EV_KEY);
		ioctl(ufd, UI_SET_KEYBIT, KEY_POWER);
		ioctl(ufd, UI_DEV_CREATE, 0);
	} else {
		ALOGE("could not open uinput device: %s", strerror(errno));
		return -1;
	}

	property_get("poweroff.doubleclick", prop, NULL);

	for (;;) {
		int i;
		int pollres = poll(pfds, cnt, timeout) ;
		ALOGV("pollres=%d %d\n", pollres, timeout);
		if (pollres < 0) {
			ALOGE("poll error: %s", strerror(errno));
			break;
		}
		if (pollres == 0) {
			ALOGI("timeout, send one power key");
			simulate_powerkey(ufd, 1);
			timeout = -1;
			shortpress = 1;
			continue;
		}
		for (i = 0; i < cnt; ++i) {
			if (pfds[i].revents & POLLIN) {
				struct input_event iev;
				size_t res = read(pfds[i].fd, &iev, sizeof(iev));
				if (res < sizeof(iev)) {
					ALOGW("insufficient input data(%zd)? fd=%d", res, pfds[i].fd);
					continue;
				}

				ALOGD("type=%d scancode=%d value=%d from fd=%d", iev.type, iev.code, iev.value, pfds[i].fd);
				if (iev.type == EV_KEY && iev.code == KEY_POWER && !iev.value) {
					if (prop[0] != '1' || timeout > 0) {
						simulate_powerkey(ufd, !shortpress);
						timeout = -1;
					} else {
						timeout = 2000; // 2 seconds
					}
				} else if (iev.type == EV_SYN && iev.code == SYN_REPORT && iev.value) {
					ALOGI("got a resuming event");
					shortpress = 0;
					timeout = 2000; // 2 seconds
				}
			}
		}
	}

	return 0;
}
