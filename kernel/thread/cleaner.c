
#include <thread.h>
#include <virtual.h>
#include <threadlist.h>
#include <irql.h>
#include <heap.h>
#include <semaphore.h>
#include <log.h>
#include <panic.h>
#include <physical.h>

static struct thread_list terminated_list;
static struct semaphore* cleaner_semaphore;

static void DestroyThread(struct thread* thr) {
    UnmapVirt(thr->kernel_stack_top - thr->kernel_stack_size, thr->kernel_stack_size);
    FreeHeap(thr->name);
    FreeHeap(thr);
}

static void CleanerThread(void*) {
    while (true) {
        AcquireSemaphore(cleaner_semaphore, -1);

        LockScheduler();
        struct thread* thr = terminated_list.head;
        assert(thr != NULL);
        ThreadListDeleteTop(&terminated_list);
        UnlockScheduler();

        DestroyThread(thr);
    }
}

static void NotifyCleaner(void*) {
    ReleaseSemaphore(cleaner_semaphore);    
}

void TerminateThreadLockHeld(struct thread* thr) {
    assert(thr != NULL);
    EXACT_IRQL(IRQL_SCHEDULER);

    if (thr == GetThread()) {
        ThreadListInsert(&terminated_list, thr);

        BlockThread(THREAD_STATE_TERMINATED);
        DeferUntilIrql(IRQL_STANDARD, NotifyCleaner, NULL);
        
    } else {
        /*
         * We can't terminate it directly, as it may be on any queue somewhere 
         * else. Instead, we will terminate it next time it gets schedueld.
         */
        thr->needs_termination = true;
    }
}

/**
 * Terminates a thread. The scheduler lock should not be already held.
 */
void TerminateThread(struct thread* thr) {
    MAX_IRQL(IRQL_SCHEDULER);

    LockScheduler();
    TerminateThreadLockHeld(thr);
    UnlockScheduler();

    if (thr == GetThread()) {
        Panic(PANIC_IMPOSSIBLE_RETURN);
    }
}

/**
 * Creates the cleaner thread. This must be called before any calls to 
 * `TerminateThread` are made.
 */
void InitCleaner(void) {
    ThreadListInit(&terminated_list, NEXT_INDEX_TERMINATED);
    cleaner_semaphore = CreateSemaphore("cleaner", SEM_BIG_NUMBER, SEM_BIG_NUMBER);
    CreateThread(CleanerThread, NULL, GetVas(), "cleaner");
}