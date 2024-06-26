#pragma once

/*
 * IMPLEMENTS STANDARD 
 */

#ifndef NULL
#define NULL	((void*) 0)
#endif

#define ENOSYS			1			// Not implemented
#define ENOMEM			2			// Not enough memory
#define ENODEV			3			// No such device
#define EALREADY		4			// Device is already registered
#define ENOTSUP			5			// Operation not supported
#define EDOM			6			// Parameter outside of domain
#define EINVAL			7			// Invalid argument
#define EEXIST			8			// File already exists
#define ENOENT			9			// No such file or directory
#define EIO				10			// Input / output error
#define EACCES			11			// Permission denied
#define ENOSPC			12			// No space left on device
#define ENAMETOOLONG	13			// Filename too long
#define ENOTDIR			14			// Not a directory
#define EISDIR			15			// Is a directory
#define ELOOP			16			// Too many loops in symbolic link resolution
#define EROFS			17			// Read-only filesystem
#define EAGAIN			18			// Resource temporarily unavailable
#define EWOULDBLOCK		EAGAIN		// Operation would block, but for historical reasons
									//			is often the same thing as EWOULDBLOCK
#define EFAULT          19          // Hardware interrupt / fault, etc.
#define EBADF           20          // Bad file descriptor
#define ENOTTY          21          // Not a terminal
#define ERANGE          22          // Result of out range
#define EILSEQ          23          // Illegal byte sequence
#define EMFILE          24          // Too many open files
#define ENFILE          25          // Too many open files in system
#define EPIPE           26          // Broken pipe
#define ESPIPE          27          // Illegal seek
#define ETIMEDOUT       28          // Operation timed out
#define ENOBUFS         29          // No buffer space
#define EBUSY			30			// Busy
#define ENOEXEC			31			// Not an executable file
#define ENOTRECOVERABLE	32			// Non returning operation has failed past point of no return
#define EINTR			33			// Interrupted by signal
#define ECANCELED		34			// Operation cancelled
#define EOVERFLOW		35			// Overflow
#ifndef COMPILE_KERNEL

int* __thread_local_errno_();
#define errno *__thread_local_errno_()

#endif