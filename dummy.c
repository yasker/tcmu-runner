#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <endian.h>

#include "../kernel/drivers/target/target_core_user.h"
#include "tcmu-runner.h"

struct dummy_state {
	int fd;
	unsigned long long num_lbas;
	unsigned int block_size;
};

int dummy_open(struct tcmu_device *dev)
{
	struct dummy_state *state;
	long long size;
	char *config;

	state = calloc(1, sizeof(*state));
	if (!state)
		return -1;

	dev->hm_private = state;

	state->block_size = tcmu_get_attribute(dev, "hw_block_size");
	if (state->block_size == -1) {
		printf("Could not get device block size\n");
		goto err;
	}

	size = tcmu_get_device_size(dev);
	if (size == -1) {
		printf("Could not get device size\n");
		goto err;
	}

	state->num_lbas = size / state->block_size;

	config = strchr(dev->cfgstring, '/');
	if (!config) {
		printf("no configuration found in cfgstring\n");
		goto err;
	}
	config += 1; /* get past '\' */

	state->fd = open(config, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (state->fd == -1) {
		printf("could not open %s: %m\n", config);
		goto err;
	}

	return 0;

err:
	free(state);
	return -1;
}

void dummy_close(struct tcmu_device *dev)
{
	struct dummy_state *state;

	state = dev->hm_private;

	close(state->fd);

	free(state);
}

/*
 * Return true if handled, false if not
 */
bool dummy_cmd_submit(struct tcmu_device *dev, uint8_t *cdb, struct iovec *iovec)
{
	struct dummy_state *state = dev->hm_private;
	uint8_t cmd;
	int i;
	int remaining;
	int lba = be32toh(*((u_int32_t *)&cdb[2]));
	int length = be16toh(*((uint16_t *)&cdb[7])) * state->block_size;
	size_t ret;

	cmd = cdb[0];

	for (i = 0; i < 10; i++) {
		printf("%x ", cdb[i]);
	}
	printf("\n");

	ret = lseek(state->fd, lba * state->block_size, SEEK_SET);
	if (ret == -1) {
		printf("lseek failed: %m\n");
		return false;
	}

	remaining = length;

	if (cmd == 0x28) { // READ 10
		void *buf;
		void *tmp_ptr;

		ret = lseek(state->fd, lba * state->block_size, SEEK_SET);
		if (ret == -1) {
			printf("lseek failed: %m\n");
			return false;
		}

		/* Using this buf DTRT even if seek is beyond EOF */
		buf = malloc(length);
		if (!buf)
			return false;
		memset(buf, 0, length);

		ret = read(state->fd, buf, length);
		if (ret == -1) {
			printf("read failed: %m\n");
			free(buf);
			return false;
		}

		tmp_ptr = buf;

		while (remaining) {
			unsigned int to_copy;

			to_copy = (remaining > iovec->iov_len) ? iovec->iov_len : remaining;

			memcpy(iovec->iov_base, tmp_ptr, to_copy);

			tmp_ptr += to_copy;
			remaining -= iovec->iov_len;
			iovec++;
		}

		free(buf);

		return true;
	}
	else if (cmd == 0x2a) { // WRITE 10

		while (remaining) {
			unsigned int to_copy;

			to_copy = (remaining > iovec->iov_len) ? iovec->iov_len : remaining;

			ret = write(state->fd, iovec->iov_base, to_copy);
			if (ret == -1) {
				printf("Could not write: %m\n");
				return false;
			}

			remaining -= to_copy;
			iovec++;
		}

		return true;
	} else {
		printf("unknown command %x\n", cdb[0]);

		return false;
	}
}


struct tcmu_handler handler_struct = {
	.name = "Dummy Handler",
	.subtype = "dummy",

	.open = dummy_open,
	.close = dummy_close,
	.cmd_submit = dummy_cmd_submit,
};
