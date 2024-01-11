#include <syscall.h>
#include <log.h>
#include <errno.h>
#include <_syscallnum.h>

typedef int (*system_call_t)(size_t, size_t, size_t, size_t, size_t);

const char* syscall_names[_SYSCALL_NUM_ENTRIES] = {
	[SYSCALL_YIELD] = "yield",
	[SYSCALL_TERMINATE] = "terminate",
	[SYSCALL_MAPVIRT] = "map virt",
	[SYSCALL_UNMAPVIRT] = "unmap virt",
	[SYSCALL_OPEN] = "open",
	[SYSCALL_READWRITE] = "read/write",
	[SYSCALL_CLOSE] = "close",
	[SYSCALL_SEEK] = "seek",
	[SYSCALL_DUP] = "dup",
	[SYSCALL_EXIT] = "exit",
	[SYSCALL_REMOVE] = "remove",
	[SYSCALL_MPROTECT] = "mprotect",
	[SYSCALL_PREPEXEC] = "prepare for exec"
};

static const system_call_t system_call_table[_SYSCALL_NUM_ENTRIES] = {
	[SYSCALL_YIELD] = SysYield,
	[SYSCALL_TERMINATE] = SysTerminate,
	[SYSCALL_MAPVIRT] = SysMapVirt,
	[SYSCALL_UNMAPVIRT] = SysUnmapVirt,
	[SYSCALL_OPEN] = SysOpen,
	[SYSCALL_READWRITE] = SysReadWrite,
	[SYSCALL_CLOSE] = SysClose,
	[SYSCALL_SEEK] = SysSeek,
	[SYSCALL_DUP] = SysDup,
	[SYSCALL_EXIT] = SysExit,
	[SYSCALL_REMOVE] = SysRemove,
	[SYSCALL_MPROTECT] = SysMprotect,
	[SYSCALL_PREPEXEC] = SysPrepExec,
};

int HandleSystemCall(int call, size_t a, size_t b, size_t c, size_t d, size_t e) {
	if (call >= _SYSCALL_NUM_ENTRIES) {
		return ENOSYS;
	}

	return system_call_table[call](a, b, c, d, e);
}