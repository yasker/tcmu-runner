#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/drivers/target/target_core_user.h"
#include "tcmu-runner.h"

int block_size = 4096;

int handle_one_command(struct tcmu_mailbox *mb, struct tcmu_cmd_entry *ent)
{
	uint8_t cmd;
	uint8_t *cdb;

	printf("handling a command!\n");

	cdb = (void *)mb + ent->req.cdb_off;

	cmd = cdb[0];

	printf("cmd = 0x%x\n", cmd);

	if (cmd == 0x28) { // READ 10
		int i;
		int data_bytes;
		int remaining;
		struct iovec *iov;

		for (i = 0; i < 10; i++) {
			printf("%x ", cdb[i]);
		}
		printf("\n");

		/* clients must lookup block size from configfs */
		data_bytes = block_size * cdb[8];
		printf("iov_cnt %lu data bytes %d\n", ent->req.iov_cnt, data_bytes);

		remaining = data_bytes;
		iov = &ent->req.iov[0];

		while (remaining) {
			memset((void *) mb + (size_t) iov->iov_base, 0, iov->iov_len);

			remaining -= iov->iov_len;
			iov++;
		}

		ent->rsp.scsi_status = 0;
	}
	else {
		printf("unknown command %x\n", cdb[0]);
	}

	return 0;
}

void poke_kernel(int fd)
{
	uint32_t buf = 0xabcdef12;

	printf("poke kernel\n");
	write(fd, &buf, 4);
}

int handle_device_event(struct tcmu_device *dev)
{
	struct tcmu_cmd_entry *ent;
	struct tcmu_mailbox *mb = dev->map;
	int did_some_work = 0;

	ent = (void *) mb + mb->cmdr_off + mb->cmd_tail;

	printf("ent addr1 %p mb %p cmd_tail %u cmd_head %u\n", ent, mb, mb->cmd_tail, mb->cmd_head);

	while (ent != (void *)mb + mb->cmdr_off + mb->cmd_head) {

		if (tcmu_hdr_get_op(&ent->hdr) == TCMU_OP_CMD) {
			printf("handling a command entry, len %d\n", tcmu_hdr_get_len(&ent->hdr));
			handle_one_command(mb, ent);
		}
		else {
			printf("handling a pad entry, len %d\n", tcmu_hdr_get_len(&ent->hdr));
		}

		mb->cmd_tail = (mb->cmd_tail + tcmu_hdr_get_len(&ent->hdr)) % mb->cmdr_size;
		ent = (void *) mb + mb->cmdr_off + mb->cmd_tail;
		printf("ent addr2 %p\n", ent);
		did_some_work = 1;
	}

	if (did_some_work)
		poke_kernel(dev->fd);

	return 0;
}

int dummy_open(struct tcmu_device *dev)
{
	return 0;
}

void dummy_close(struct tcmu_device *dev)
{
}

int dummy_cmd_submit(struct tcmu_device *dev, char *cmd)
{
	return 0;
}


struct tcmu_handler handler_struct = {
	.name = "Dummy Handler",
	.subtype = "dummy",

	.open = dummy_open,
	.close = dummy_close,
	.cmd_submit = dummy_cmd_submit,
};
