#define _BITS_UIO_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <assert.h>

#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/genl/mngt.h>
#include <libnl3/netlink/genl/ctrl.h>
#include "../kernel/drivers/target/target_core_user.h"
#include "darray.h"

#define ARRAY_SIZE(X) (sizeof(X) / sizeof((X)[0]))

struct tcmu_device {
	int fd;
	void *map;
	size_t map_len;
	char name[256];
};

darray(struct tcmu_device) devices = darray_new();

static struct nla_policy tcmu_attr_policy[TCMU_ATTR_MAX+1] = {
	[TCMU_ATTR_DEVICE]	= { .type = NLA_STRING },
};

int add_device(char *dev_name);
void remove_device(char *dev_name);

static int parse_added_device(struct nl_cache_ops *unused, struct genl_cmd *cmd,
                         struct genl_info *info, void *arg)
{
	if (info->attrs[TCMU_ATTR_MINOR]) {
		char buf[32];

		snprintf(buf, sizeof(buf), "uio%d", nla_get_u32(info->attrs[TCMU_ATTR_MINOR]));
		add_device(buf);
	}

	return 0;
}

static int parse_removed_device(struct nl_cache_ops *unused, struct genl_cmd *cmd,
                         struct genl_info *info, void *arg)
{
	if (info->attrs[TCMU_ATTR_MINOR]) {
		char buf[32];

		snprintf(buf, sizeof(buf), "uio%d", nla_get_u32(info->attrs[TCMU_ATTR_MINOR]));
		remove_device(buf);
	}

	return 0;
}

static struct genl_cmd tcmu_cmds[] = {
	{
		.c_id		= TCMU_CMD_ADDED_DEVICE,
		.c_name		= "ADDED DEVICE",
		.c_msg_parser	= parse_added_device,
		.c_maxattr	= TCMU_ATTR_MAX,
		.c_attr_policy	= tcmu_attr_policy,
	},
	{
		.c_id		= TCMU_CMD_REMOVED_DEVICE,
		.c_name		= "REMOVED DEVICE",
		.c_msg_parser	= parse_removed_device,
		.c_maxattr	= TCMU_ATTR_MAX,
		.c_attr_policy	= tcmu_attr_policy,
	},
};

static struct genl_ops tcmu_ops = {
	.o_name		= "TCM-USER",
	.o_cmds		= tcmu_cmds,
	.o_ncmds	= ARRAY_SIZE(tcmu_cmds),
};

struct nl_sock *setup_netlink(void)
{
	struct nl_sock *sock;
	int ret;

	sock = nl_socket_alloc();
	if (!sock) {
		printf("couldn't alloc socket\n");
		exit(1);
	}

	nl_socket_disable_seq_check(sock);

	nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, genl_handle_msg, NULL);

	ret = genl_connect(sock);
	if (ret < 0) {
		printf("couldn't connect\n");
		exit(1);
	}

	ret = genl_register_family(&tcmu_ops);
	if (ret < 0) {
		printf("couldn't register family\n");
		exit(1);
	}

	ret = genl_ops_resolve(sock, &tcmu_ops);
	if (ret < 0) {
		printf("couldn't resolve ops, is target_core_user.ko loaded?\n");
		exit(1);
	}

	ret = genl_ctrl_resolve_grp(sock, "TCM-USER", "config");

	printf("multicast id %d\n", ret);

	ret = nl_socket_add_membership(sock, ret);
	if (ret < 0) {
		printf("couldn't add membership\n");
		exit(1);
	}

	return sock;
}

int is_uio(const struct dirent *dirent)
{
	int fd;
	char tmp_path[64];
	char buf[256];
	ssize_t ret;

	if (strncmp(dirent->d_name, "uio", 3))
		return 0;

	snprintf(tmp_path, sizeof(tmp_path), "/sys/class/uio/%s/name", dirent->d_name);

	fd = open(tmp_path, O_RDONLY);
	if (fd == -1) {
		printf("could not open %s!\n", tmp_path);
		return 0;
	}

	ret = read(fd, buf, sizeof(buf));
	if (ret <= 0 || ret >= sizeof(buf)) {
		printf("read of %s had issues\n", tmp_path);
		return 0;
	}

	/* we only want uio devices whose name is a format we expect */
	snprintf(tmp_path, sizeof(tmp_path), "tcm-user+%s/", "srv");
	if (strncmp(buf, tmp_path, strlen(tmp_path)))
		return 0;

	return 1;
}

int add_device(char *dev_name)
{
	struct tcmu_device dev;
	char str_buf[64];
	int fd;
	int ret;

	snprintf(dev.name, sizeof(dev.name), "%s", dev_name);
	snprintf(str_buf, sizeof(str_buf), "/dev/%s", dev_name);
	printf("dev %s\n", str_buf);

	dev.fd = open(str_buf, O_RDWR);
	if (dev.fd == -1) {
		printf("could not open %s\n", dev.name);
		return -1;
	}

	snprintf(str_buf, sizeof(str_buf), "/sys/class/uio/%s/maps/map0/size", dev.name);
	fd = open(str_buf, O_RDONLY);
	if (fd == -1) {
		printf("could not open %s\n", dev.name);
		close(dev.fd);
		return -1;
	}

	ret = read(fd, str_buf, sizeof(str_buf));
	close(fd);
	if (ret <= 0) {
		printf("could not read size of map0\n");
		close(dev.fd);
		return -1;
	}

	dev.map_len = strtoull(str_buf, NULL, 0);
	if (dev.map_len == ULLONG_MAX) {
		printf("could not get map length\n");
		close(dev.fd);
		return -1;
	}

	dev.map = mmap(NULL, dev.map_len, PROT_READ|PROT_WRITE, MAP_SHARED, dev.fd, 0);
	if (dev.map == MAP_FAILED) {
		printf("could not mmap: %m\n");
		close(dev.fd);
	}

	darray_append(devices, dev);

	return 0;
}

void remove_device(char *dev_name)
{
	struct tcmu_device *dev;
	int i = 0;
	bool found = false;
	int ret;

	darray_foreach(dev, devices) {
		if (strncmp(dev->name, dev_name, strnlen(dev->name, sizeof(dev->name))))
			i++;
		else {
			found = true;
			break;
		}
	}

	if (!found) {
		printf("could not remove device %s: not found\n", dev_name);
		return;
	}

	ret = munmap(dev->map, dev->map_len);
	if (ret < 0)
		printf("munmap of %s failed\n", dev->name);

	close(dev->fd);

	darray_remove(devices, i);
}

int open_devices(void)
{
	struct dirent **dirent_list;
	int num_devs;
	int num_good_devs = 0;
	int i;

	num_devs = scandir("/dev", &dirent_list, is_uio, alphasort);

	if (num_devs == -1)
		return -1;

	for (i = 0; i < num_devs; i++) {
		int ret = add_device(dirent_list[i]->d_name);
		free(dirent_list[i]);
		if (ret < 0)
			continue;

		num_good_devs++;
	}

	free(dirent_list);

	return num_good_devs;
}

int handle_device_event(struct tcmu_device *dev)
{
	printf("handle device event for %s\n", dev->name);

	return 0;
}

int event_poll(struct nl_sock *sock)
{
	int ret;
	struct pollfd *pollfds;
	int i = 0;
	int num_poll_fds;
	struct tcmu_device *dev;

	num_poll_fds = darray_size(devices) + 1;

	/* +1 for netlink socket */
	pollfds = calloc(num_poll_fds, sizeof(struct pollfd));
	if (!pollfds) {
		printf("couldn't alloc pollfds\n");
		exit(1);
	}

	/* polling 0..n device fds followed by the one netlink fd */
	darray_foreach(dev, devices) {
		pollfds[i].fd = dev->fd;
		pollfds[i].events = POLLIN;
		i++;
	}
	pollfds[i].fd = nl_socket_get_fd(sock);
	pollfds[i].events = POLLIN;
	assert(i == num_poll_fds-1);

	int events = poll(pollfds, num_poll_fds, -1);
	printf("%d fds active\n", events);

	for (i = 0; events && i < num_poll_fds; i++) {
		if (pollfds[i].revents) {
			if (i == num_poll_fds-1) {
				ret = nl_recvmsgs_default(sock);
				if (ret < 0)
					printf("nl_recvmsgs error %d\n", ret);
			} else {
				ret = handle_device_event(&darray_item(devices, i));
				if (ret < 0)
					printf("handle_device_event error %d\n", ret);
			}

			events--;
		}
	}

	free(pollfds);

	return 0;
}

int main()
{
	struct nl_sock *nl_sock;
	int ret;

	nl_sock = setup_netlink();
	if (!nl_sock) {
		printf("couldn't setup netlink\n");
		exit(1);
	}

	ret = open_devices();
	printf("%d devices found\n", ret);
	if (ret < 0) {
		printf("couldn't open devices\n");
		exit(1);
	}

	while (1) {
		ret = event_poll(nl_sock);
		if (ret < 0) {
			printf("event poll returned %d", ret);
			exit(1);
		}
	}

	return 0;
}
