
#include <heap.h>
#include <assert.h>
#include <common.h>
#include <spinlock.h>
#include <semaphore.h>
#include <errno.h>
#include <thread.h>
#include <panic.h>
#include <log.h>
#include <irql.h>

struct blocking_buffer {
    uint8_t* buffer;
    int total_size;
    int used_size;
    int start_pos;
    int end_pos;
    struct semaphore* sem;
    struct semaphore* reverse_sem;
    struct spinlock lock;
};

struct blocking_buffer* BlockingBufferCreate(int size) {
    assert(size > 0);

    struct blocking_buffer* buffer = AllocHeap(sizeof(struct blocking_buffer));
    buffer->buffer = AllocHeap(size);
    buffer->total_size = size;
    buffer->used_size = 0;
    buffer->start_pos = 0;
    buffer->end_pos = 0;
    buffer->sem = CreateSemaphore("bb get", size, size);
    buffer->reverse_sem = CreateSemaphore("bb add", size, 0);
    InitSpinlock(&buffer->lock, "blocking buffer", IRQL_SCHEDULER);    
    return buffer;
}

void BlockingBufferDestroy(struct blocking_buffer* buffer) {
    FreeHeap(buffer->sem);
    FreeHeap(buffer->reverse_sem);
    FreeHeap(buffer->buffer);
    FreeHeap(buffer);
}

int BlockingBufferAdd(struct blocking_buffer* buffer, uint8_t c, bool block) {
    int res = AcquireSemaphore(buffer->reverse_sem, block ? -1 : 0);

    if (!block && res != 0) {
        return ENOBUFS;
    }

    AcquireSpinlock(&buffer->lock);

    assert(buffer->used_size != buffer->total_size);

    buffer->buffer[buffer->end_pos] = c;
    buffer->end_pos = (buffer->end_pos + 1) % buffer->total_size;
    buffer->used_size++;

    ReleaseSpinlock(&buffer->lock);
    ReleaseSemaphore(buffer->sem);
    return 0;
}

static uint8_t BlockingBufferGetAfterAcquisition(struct blocking_buffer* buffer) {
    AcquireSpinlock(&buffer->lock);

    uint8_t c = buffer->buffer[buffer->start_pos];
    buffer->start_pos = (buffer->start_pos + 1) % buffer->total_size;
    buffer->used_size--;

    ReleaseSpinlock(&buffer->lock);
    ReleaseSemaphore(buffer->reverse_sem);

    return c;
}

uint8_t BlockingBufferGet(struct blocking_buffer* buffer) {
    /*
     * Wait for there to be something to actually read.
     */
    AcquireSemaphore(buffer->sem, -1);
    return BlockingBufferGetAfterAcquisition(buffer);
}

int BlockingBufferTryGet(struct blocking_buffer* buffer, uint8_t* c) {
    assert(c != NULL);

    int result = AcquireSemaphore(buffer->sem, 0);
    if (result == 0) {
        *c = BlockingBufferGetAfterAcquisition(buffer);
        return 0;

    } else {
        return result;
    }
}