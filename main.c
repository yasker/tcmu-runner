#define _BITS_UIO_H
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/genl/mngt.h>
#include <libnl3/netlink/genl/ctrl.h>
#include "../kernel/drivers/target/target_core_user.h"
#include "darray.h"

#define ARRAY_SIZE(X) (sizeof(X) / sizeof((X)[0]))

static struct nla_policy tcmu_attr_policy[TCMU_ATTR_MAX+1] = {
	[TCMU_ATTR_DEVICE]	= { .type = NLA_STRING },
};

static int parse_added_device(struct nl_cache_ops *unused, struct genl_cmd *cmd,
                         struct genl_info *info, void *arg)
{
	printf("device added!\n");

	if (info->attrs[TCMU_ATTR_DEVICE])
		printf("sup! %s\n", nla_get_string(info->attrs[TCMU_ATTR_DEVICE]));
	else
		printf("DOH\n");

	return 0;
}

static int parse_removed_device(struct nl_cache_ops *unused, struct genl_cmd *cmd,
                         struct genl_info *info, void *arg)
{
	printf("device removed!\n");

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

int open_devices(int **dev_fds)
{
	struct dirent **dirent_list;
	int ret;
	int i;
	int *fds;

	ret = scandir("/dev", &dirent_list, is_uio, alphasort);

	if (ret == -1)
		return ret;

	fds = calloc(ret, sizeof(int));
	if (!fds)
		return -1;

	for (i = 0; i < ret; i++) {
		char tmp_path[64];

		snprintf(tmp_path, sizeof(tmp_path), "/dev/%s", dirent_list[i]->d_name);

		printf("dev %s\n", tmp_path);
		fds[i] = open(tmp_path, O_RDWR);

		/* TODO: mmap here */

		free(dirent_list[i]);
	}

	free(dirent_list);

	*dev_fds = fds;

	return ret;
}

void handle_device_event(void)
{
	printf("handle device event\n");
}

int main()
{
	struct nl_sock *sock;
	int ret;
	int *dev_fds;
	struct pollfd *pollfds;
	int i;
	int num_poll_fds;

	sock = setup_netlink();
	if (!sock) {
		printf("couldn't setup netlink\n");
		exit(1);
	}

	ret = open_devices(&dev_fds);
	if (ret < 0) {
		printf("couldn't open devices\n");
		exit(1);
	}

	num_poll_fds = ret + 1;

	/* +1 for netlink socket */
	pollfds = calloc(num_poll_fds, sizeof(struct pollfd));
	if (!pollfds) {
		printf("couldn't alloc pollfds\n");
		exit(1);
	}

	pollfds[0].fd = nl_socket_get_fd(sock);
	pollfds[0].events = POLLIN;

	for (i = 0; i < num_poll_fds-1; i++) {
		pollfds[i+1].fd = dev_fds[i];
		pollfds[i+1].events = POLLIN;
	}

	while (1) {
		ret = poll(pollfds, num_poll_fds, -1);
		printf("ret %d\n", ret);

		for (i = 0; i < num_poll_fds; i++) {
			if (pollfds[i].revents) {
				if (i == 0)
					ret = nl_recvmsgs_default(sock);
				else
					handle_device_event();
			}
		}
	}

	return 0;
}
