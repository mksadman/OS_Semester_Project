#include <stdio.h>
#include <string.h>

#define NUM_IDS 64

typedef struct {
    unsigned int lock_holder;
    volatile unsigned int lock;
} spinlock_t;

static void spinlock_acquire(spinlock_t *lk)
{
    (void) lk;
}

static void spinlock_release(spinlock_t *lk)
{
    (void) lk;
}

#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_UN 8
#define LOCK_NB 4
#define LOCK_PRIO_READER 16
#define LOCK_PRIO_WRITER 32
#define LOCK_MODE_MASK (LOCK_SH | LOCK_EX | LOCK_UN)

#define FLOCK_PRIORITY_READER 1
#define FLOCK_PRIORITY_WRITER 2

struct inode {
    spinlock_t lock_spinlock;
    int exclusive_lock_pid;
    int shared_lock_count;
    int waiting_readers;
    int waiting_writers;
    int flock_priority;
    int shared_lock_holders[NUM_IDS];
};

static int sleep_calls;
static int wakeup_calls;

static void thread_sleep(void *chan, spinlock_t *lk)
{
    (void) chan;
    (void) lk;
    sleep_calls++;
}

static void thread_wakeup(void *chan)
{
    (void) chan;
    wakeup_calls++;
}

static void inode_lock_state_init(struct inode *ip)
{
    memset(ip, 0, sizeof(*ip));
    ip->exclusive_lock_pid = -1;
    ip->flock_priority = FLOCK_PRIORITY_READER;
}

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

static int assert_ok(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL: %s\n", msg);
        return 0;
    }
    printf("PASS: %s\n", msg);
    return 1;
}

int main(void)
{
    struct inode ip;
    int pass = 1;
    int tries;

    printf("[Standalone flock test]\n");

    inode_lock_state_init(&ip);
    pass &= assert_ok(flock_acquire(&ip, LOCK_EX, 1) == 0,
                      "pid1 acquires exclusive lock");
    pass &= assert_ok(flock_acquire(&ip, LOCK_EX | LOCK_NB, 2) == -1,
                      "pid2 cannot acquire exclusive while pid1 holds");
    pass &= assert_ok(flock_acquire(&ip, LOCK_SH | LOCK_NB, 2) == -1,
                      "pid2 cannot acquire shared while pid1 holds exclusive");

    pass &= assert_ok(flock_release(&ip, 1) == 0,
                      "pid1 releases exclusive lock");
    pass &= assert_ok(flock_acquire(&ip, LOCK_EX | LOCK_NB, 2) == 0,
                      "pid2 acquires exclusive after release (progress)");
    pass &= assert_ok(flock_release(&ip, 2) == 0,
                      "pid2 releases exclusive lock");

    inode_lock_state_init(&ip);
    pass &= assert_ok(flock_acquire(&ip, LOCK_EX, 1) == 0,
                      "bounded waiting setup: pid1 holds lock");
    tries = 0;
    while (flock_acquire(&ip, LOCK_EX | LOCK_NB, 2) == -1) {
        tries++;
        if (tries == 5) {
            flock_release(&ip, 1);
        }
        if (tries > 100) {
            break;
        }
    }
    pass &= assert_ok(tries <= 100,
                      "pid2 obtains lock within bounded retry budget");
    pass &= assert_ok(flock_release(&ip, 2) == 0,
                      "pid2 releases lock after bounded-waiting test");

    inode_lock_state_init(&ip);
    pass &= assert_ok(flock_acquire(&ip, LOCK_SH | LOCK_PRIO_WRITER, 10) == 0,
                      "set writer-priority mode via shared lock");
    ip.waiting_writers = 1;
    pass &= assert_ok(flock_acquire(&ip, LOCK_SH | LOCK_NB, 11) == -1,
                      "writer-priority blocks new reader when writer waits");
    ip.waiting_writers = 0;
    pass &= assert_ok(flock_release(&ip, 10) == 0,
                      "release writer-priority setup lock");

    inode_lock_state_init(&ip);
    pass &= assert_ok(flock_acquire(&ip, LOCK_SH | LOCK_PRIO_READER, 10) == 0,
                      "set reader-priority mode via shared lock");
    ip.waiting_writers = 1;
    pass &= assert_ok(flock_acquire(&ip, LOCK_SH | LOCK_NB, 11) == 0,
                      "reader-priority admits new reader despite waiting writer");
    pass &= assert_ok(flock_release(&ip, 11) == 0,
                      "reader 11 releases shared lock");
    pass &= assert_ok(flock_release(&ip, 10) == 0,
                      "reader 10 releases shared lock");

    pass &= assert_ok(wakeup_calls > 0, "release path triggers wakeup");

    if (!pass) {
        printf("\nOverall: FAILED\n");
        return 1;
    }

    printf("\nOverall: PASSED\n");
    return 0;
}
