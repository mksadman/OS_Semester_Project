#include <kern/lib/types.h>
#include <kern/lib/debug.h>
#include <kern/lib/spinlock.h>
#include "import.h"
#include "export.h"

/**
 * Internal helper: check conflict without acquiring spinlock
 * Caller must already hold ip->lock_spinlock
 */
static int has_conflict_locked(struct inode *ip, int lock_type, int pid)
{
    if (lock_type == LOCK_EX) {
        if ((ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) ||
            ip->shared_lock_count > 0) {
            return 1;
        }
    } else if (lock_type == LOCK_SH) {
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            return 1;
        }
    }
    return 0;
}

/**
 * Check if acquiring a lock would conflict with existing locks
 * Returns: 1 if conflict exists, 0 if no conflict
 */
int flock_check_conflict(struct inode *ip, int operation, int pid)
{
    if (!ip)
        return 1;

    int lock_type = operation & ~LOCK_NB;  // remove non-blocking flag

    spinlock_acquire(&ip->lock_spinlock);

    if (lock_type == LOCK_EX) {

        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            spinlock_release(&ip->lock_spinlock);
            return 1;  
        }
        if (ip->shared_lock_count > 0) {
            spinlock_release(&ip->lock_spinlock);
            return 1;  
        }
    } 
    else if (lock_type == LOCK_SH) {

        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            spinlock_release(&ip->lock_spinlock);
            return 1; 
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

    while (has_conflict_locked(ip, lock_type, pid)) {
        if (operation & LOCK_NB) {
            spinlock_release(&ip->lock_spinlock);
            return -1;
        } 
        else {
            thread_sleep(ip, &ip->lock_spinlock);
        }
    }

    if (lock_type == LOCK_EX) {
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {
            spinlock_release(&ip->lock_spinlock);
            return -1;
        }
        if (ip->shared_lock_count > 0) {
            spinlock_release(&ip->lock_spinlock);
            return -1;
        }

        ip->exclusive_lock_pid = pid;
        spinlock_release(&ip->lock_spinlock);
        return 0;

    } else if (lock_type == LOCK_SH) {
        if (ip->exclusive_lock_pid != -1 && ip->exclusive_lock_pid != pid) {

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
        thread_wakeup(ip);
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    if (ip->shared_lock_count > 0) {
        ip->shared_lock_count--;

        if (ip->shared_lock_count==0){
            thread_wakeup(ip);
        }
        
        spinlock_release(&ip->lock_spinlock);
        return 0;
    }

    spinlock_release(&ip->lock_spinlock);
    return -1;
}