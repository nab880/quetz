#ifndef QUETZ_IPC_LOCK_H
#define QUETZ_IPC_LOCK_H

#include <stdint.h>
#ifdef __linux__
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#else
#include <sched.h>
#endif

/* Shared-memory producer lock. No allocation or pthread operation: the Linux
 * user-mode caller may be handling SIGSEGV. It also arbitrates separate client
 * mappings (and forked processes), unlike a process-private mutex. Never hold
 * this lock while executing guest code or delivering a guest signal. */
static inline void quetz_ipc_lock(uint32_t *busy)
{
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(busy, &expected, 1u, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
#ifdef __linux__
        const struct timespec timeout = { 0, 1000000 };
        syscall(SYS_futex, busy, FUTEX_WAIT, 1, &timeout, NULL, 0);
#else
        sched_yield();
#endif
    }
}

static inline void quetz_ipc_unlock(uint32_t *busy)
{
    __atomic_store_n(busy, 0u, __ATOMIC_RELEASE);
#ifdef __linux__
    syscall(SYS_futex, busy, FUTEX_WAKE, 1, NULL, NULL, 0);
#endif
}
#endif
