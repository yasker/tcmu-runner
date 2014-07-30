#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/drivers/target/target_core_user.h"
#include "tcmu-runner.h"

int block_size = 4096;

int dummy_open(struct tcmu_device *dev)
{
	return 0;
}

void dummy_close(struct tcmu_device *dev)
{
}

/*
 * Return true if handled, false if not
 */
bool dummy_cmd_submit(struct tcmu_device *dev, uint8_t *cdb, struct iovec *iovec)
{
	uint8_t cmd;

	printf("handling a command!\n");

	cmd = cdb[0];

	printf("cmd = 0x%x\n", cmd);

	if (cmd == 0x28) { // READ 10
		int i;
		int data_bytes;
		int remaining;

		for (i = 0; i < 10; i++) {
			printf("%x ", cdb[i]);
		}
		printf("\n");

		/* clients must lookup block size from configfs */
		data_bytes = block_size * cdb[8];

		remaining = data_bytes;

		while (remaining) {
			memset(iovec->iov_base, 0, iovec->iov_len);

			remaining -= iovec->iov_len;
			iovec++;
		}

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
