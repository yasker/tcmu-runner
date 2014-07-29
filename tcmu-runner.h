
#ifndef __TCMU_RUNNER_H
#define __TCMU_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

struct tcmu_device {
	int fd;
	void *map;
	size_t map_len;
	char name[16]; /* e.g. "uio14" */
	char cfgstring[256];

	void *hm_private; /* private ptr for handler module */
};

struct tcmu_handler {
	const char *name;	/* Human-friendly name */
	const char *subtype;	/* Name for cfgstring matching */

	/* Per-device added/removed callbacks */
	int (*open)(struct tcmu_device *dev);
	void (*close)(struct tcmu_device *dev);

	int (*cmd_submit)(struct tcmu_device *dev, char *cmd);
};

#ifdef __cplusplus
}
#endif

#endif
