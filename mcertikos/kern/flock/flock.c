#include <kern/lib/types.h>
#include <kern/lib/debug.h>
#include <kern/lib/spinlock.h>
#include "import.h"
#include "export.h"

/**
 * Check if acquiring a lock would conflict with existing locks
 * Returns: 1 if conflict exists, 0 if no conflict
 */
int flock_check_conflict(struct inode *ip, int operation, int pid)
{
    if (!ip)
        return 1;

    int lock_type = operation & ~LOCK_NB;  // remove non-blocking flag

    // acquire spinlock to check state atomically
    spinlock_acquire(&ip->lock_spinlock);

    if (lock_type == LOCK_EX) {
        // exclusive lock: conflicts with ANY existing lock
        // (except if this process already owns the exclusive lock)
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            spinlock_release(&ip->lock_spinlock);
            return 1;  // conflict: someone else has exclusive lock
        }
        if (ip->shared_lock_count > 0) {
            spinlock_release(&ip->lock_spinlock);
            return 1;  // conflict: shared locks exist
        }
    } else if (lock_type == LOCK_SH) {
        // shared lock: only conflicts with exclusive lock
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            spinlock_release(&ip->lock_spinlock);
            return 1;  // conflict: exclusive lock exists
        }
    }

    spinlock_release(&ip->lock_spinlock);
    return 0;  // no conflict
}

/**
 * acquire a file lock (shared or exclusive)
 * returns: 0 on success, -1 on failure
 */
int flock_acquire(struct inode *ip, int operation, int pid)
{
    if (!ip || pid < 0)
        return -1;

    int lock_type = operation & ~LOCK_NB;  // remove non-blocking flag

    if (lock_type != LOCK_SH && lock_type != LOCK_EX)
        return -1;

    spinlock_acquire(&ip->lock_spinlock);

    if (lock_type == LOCK_EX) {
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            // someone else has exclusive lock
            spinlock_release(&ip->lock_spinlock);
            return -1;
        }
        if (ip->shared_lock_count > 0) {
            // shared locks exist, can't acquire exclusive
            spinlock_release(&ip->lock_spinlock);
            return -1;
        }

        // grant exclusive lock
        ip->exclusive_lock_pid = pid;
        spinlock_release(&ip->lock_spinlock);
        return 0;

    } else if (lock_type == LOCK_SH) {
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            // Someone else has exclusive lock
            spinlock_release(&ip->lock_spinlock);
            return -1;
        }

        ip->shared_lock_count++;
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    spinlock_release(&ip->lock_spinlock);
    return -1;
}

/**
 * Release any lock held by this process on this inode
 * Returns: 0 on success, -1 on failure
 */
int flock_release(struct inode *ip, int pid)
{
    if (!ip || pid < 0)
        return -1;

    spinlock_acquire(&ip->lock_spinlock);

    if (ip->exclusive_lock_pid == pid) {
        ip->exclusive_lock_pid = -1;
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    if (ip->shared_lock_count > 0) {
        ip->shared_lock_count--;
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    spinlock_release(&ip->lock_spinlock);
    return -1;
}