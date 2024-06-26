#pragma once

#include <common.h>
#include <sys/types.h>
#include <linkedlist.h>

struct fd_table;
struct vnode;

struct process {
    pid_t pid;
    pid_t pgid;
    pid_t parent;
    struct vas* vas;
    struct tree* children;
    struct tree* threads;
    struct semaphore* lock;
    struct semaphore* killed_children_semaphore;
    struct fd_table* fd_table;
    int retv;
    bool terminated;
    struct vnode* cwd;
};

void InitProcess(void);
struct process* CreateProcess(pid_t parent_pid);
struct process* ForkProcess(size_t user_stub_addr);
pid_t WaitProcess(pid_t pid, int* status, int flags);
void KillProcess(int retv);

struct linked_list* GetProcessesFromPgid(pid_t pgid);
struct process* GetProcessFromPid(pid_t pid);
struct process* GetProcess(void);
pid_t GetPid(struct process* prcss);

struct fd_table* GetFdTable(struct process* prcss); 

void AddThreadToProcess(struct process* prcss, struct thread* thr);
struct process* CreateProcessWithEntryPoint(pid_t parent, void(*entry_point)(void*), void* arg);

struct thread* GetArbitraryThreadFromProcess(struct process* p);