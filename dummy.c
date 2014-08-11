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

int block_size = 4096;

struct dummy_state {
	int fd;
	unsigned long long num_lbas;
	unsigned int block_size;
};

int dummy_open(struct tcmu_device *dev)
{
	struct dummy_state *state;
	long long size;

	state = calloc(1, sizeof(*state));
	if (!state)
		return -1;

	dev->hm_private = state;

	state->fd = open("test.img", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (state->fd == -1) {
		printf("could not open: %m\n");
		return -1;
	}

	state->block_size = tcmu_get_attribute(dev, "hw_block_size");
	if (state->block_size == -1) {
		printf("Could not get device block size\n");
		return -1;
	}

	size = tcmu_get_device_size(dev);
	if (size == -1) {
		printf("Could not get device size\n");
		return -1;
	}

	state->num_lbas = size / state->block_size;

	printf("handler lbas = %llu each block is %u\n", state->num_lbas, state->block_size);

	return 0;
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
	uint8_t cmd;
	void *buf;
	struct dummy_state *state;

	state = dev->hm_private;

	printf("handling a command!\n");

	cmd = cdb[0];

	printf("cmd = 0x%x\n", cmd);

	if (cmd == 0x28) { // READ 10
		int i;
		int remaining;
		int lba = be32toh(*((u_int32_t *)&cdb[2]));
		int length = be16toh(*((uint16_t *)&cdb[7])) * block_size;
		off_t ret;
		void *tmp_ptr;

		for (i = 0; i < 10; i++) {
			printf("%x ", cdb[i]);
		}
		printf("\n");
		printf("lba %d length %d\n", lba, length);

		buf = malloc(length);
		if (!buf)
			return false;
		memset(buf, 0, length);

		ret = lseek(state->fd, lba*block_size, SEEK_SET);
		if (ret == -1) {
			printf("lseek failed: %m\n");
			free(buf);
			return false;
		}

		ret = read(state->fd, buf, length);
		if (ret == -1) {
			printf("read failed: %m\n");
			free(buf);
			return false;
		}

		remaining = length;
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
	else {
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
