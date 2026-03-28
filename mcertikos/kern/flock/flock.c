#include <kern/lib/types.h>
#include <kern/lib/debug.h>
#include <kern/lib/spinlock.h>
#include "import.h"
#include "export.h"

static int valid_pid(int pid)
{
    return 0 <= pid && pid < NUM_IDS;
}

static void update_priority_locked(struct inode *ip, int operation)
{
    if (operation & LOCK_PRIO_WRITER) {
        ip->flock_priority = FLOCK_PRIORITY_WRITER;
    } else if (operation & LOCK_PRIO_READER) {
        ip->flock_priority = FLOCK_PRIORITY_READER;
    }
}

static int has_conflict_locked(struct inode *ip, int lock_type, int pid)
{
    if (lock_type == LOCK_EX) {
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            return 1;
        }

        if (ip->shared_lock_count > ip->shared_lock_holders[pid]) {
            return 1;
        }

        return 0;
    }

    if (lock_type == LOCK_SH) {
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            return 1;
        }

        if (ip->flock_priority == FLOCK_PRIORITY_WRITER
            && ip->waiting_writers > 0
            && ip->shared_lock_holders[pid] == 0) {
            return 1;
        }

        return 0;
    }

    return 1;
}

/**
 * Check if acquiring a lock would conflict with existing locks
 * Returns: 1 if conflict exists, 0 if no conflict
 */
int flock_check_conflict(struct inode *ip, int operation, int pid)
{
    int conflict;
    int lock_type;

    if (!ip || !valid_pid(pid))
        return 1;

    lock_type = operation & LOCK_MODE_MASK;

    if (lock_type != LOCK_SH && lock_type != LOCK_EX) {
        return 1;
    }

    spinlock_acquire(&ip->lock_spinlock);

    conflict = has_conflict_locked(ip, lock_type, pid);

    spinlock_release(&ip->lock_spinlock);
    return conflict;
}

/**
 * acquire a file lock (shared or exclusive)
 * returns: 0 on success, -1 on failure
 */
int flock_acquire(struct inode *ip, int operation, int pid)
{
    int lock_type;

    if (!ip || !valid_pid(pid))
        return -1;

    lock_type = operation & LOCK_MODE_MASK;

    if (lock_type != LOCK_SH && lock_type != LOCK_EX)
        return -1;

    spinlock_acquire(&ip->lock_spinlock);

    update_priority_locked(ip, operation);

    if (lock_type == LOCK_EX) {
        ip->waiting_writers++;
    } else {
        ip->waiting_readers++;
    }

    while (has_conflict_locked(ip, lock_type, pid)) {
        if (operation & LOCK_NB) {
            if (lock_type == LOCK_EX) {
                ip->waiting_writers--;
            } else {
                ip->waiting_readers--;
            }
            spinlock_release(&ip->lock_spinlock);
            return -1;
        }

        thread_sleep(ip, &ip->lock_spinlock);
    }

    if (lock_type == LOCK_EX) {
        ip->waiting_writers--;
    } else {
        ip->waiting_readers--;
    }

    if (lock_type == LOCK_EX) {
        if (ip->exclusive_lock_pid == pid) {
            spinlock_release(&ip->lock_spinlock);
            return 0;
        }

        ip->shared_lock_count -= ip->shared_lock_holders[pid];
        ip->shared_lock_holders[pid] = 0;
        ip->exclusive_lock_pid = pid;
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    if (ip->exclusive_lock_pid == pid) {
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    ip->shared_lock_count++;
    ip->shared_lock_holders[pid]++;

    spinlock_release(&ip->lock_spinlock);
    return 0;
}

/**
 * Release any lock held by this process on this inode
 * Returns: 0 on success, -1 on failure
 */
int flock_release(struct inode *ip, int pid)
{
    int released = 0;

    if (!ip || !valid_pid(pid))
        return -1;

    spinlock_acquire(&ip->lock_spinlock);

    if (ip->exclusive_lock_pid == pid) {
        ip->exclusive_lock_pid = -1;
        released = 1;
    }

    while (ip->shared_lock_holders[pid] > 0) {
        ip->shared_lock_holders[pid]--;
        ip->shared_lock_count--;
        released = 1;
    }

    if (released) {
        thread_wakeup(ip);
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    spinlock_release(&ip->lock_spinlock);
    return -1;
}