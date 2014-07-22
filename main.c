#define _BITS_UIO_H
#include <stdio.h>
#include <errno.h>
#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/genl/mngt.h>
#include <libnl3/netlink/genl/ctrl.h>
#include "../kernel/drivers/target/target_core_user.h"

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

int main()
{
	struct nl_sock *sock;
	int ret;
	struct pollfd pollfds[10];

	sock = setup_netlink();
	if (!sock) {
		printf("couldn't setup netlink\n");
		exit(1);
	}

	pollfds[0].fd = nl_socket_get_fd(sock);
	pollfds[0].events = POLLIN;

	while(1) {
		ret = poll(pollfds, 1, 1000);
		printf("ret %d\n", ret);

		if (ret == 1)
			ret = nl_recvmsgs_default(sock);
	}

	return 0;
}
