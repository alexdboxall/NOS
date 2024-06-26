#include <syscall.h>
#include <errno.h>
#include <_syscallnum.h>
#include <thread.h>
#include <log.h>
#include <vfs.h>
#include <transfer.h>
#include <fcntl.h>
#include <process.h>
#include <filedes.h>

int SysReadWrite(size_t fd, size_t size, size_t buffer, size_t br_out, size_t write) {
	struct fd_table* table = GetFdTable(GetProcess());
	struct file* file;
	int res = GetFileFromFd(table, fd, &file);

	if (file == NULL || res != 0) {
		return res;
	}

	LogWriteSerial("SYSREADWRITE of size %d, write = %d\n", size, write);

	if (write && (file->flags & O_APPEND)) {
        file->seek_position = file->node->stat.st_size;
	}

	struct transfer io = CreateTransferWritingToUser(
		(uint8_t*) buffer, size, file->seek_position
	);
	io.direction = write ? TRANSFER_WRITE : TRANSFER_READ;
	
	if ((res = (write ? WriteFile : ReadFile)(file, &io))) {
		return res;
	}

	size_t br = size - io.length_remaining;
	file->seek_position += br;
	
	return WriteWordToUsermode((size_t*) br_out, br);
}
