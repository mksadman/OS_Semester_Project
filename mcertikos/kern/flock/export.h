#ifndef _KERN_FLOCK_EXPORT_H_
#define _KERN_FLOCK_EXPORT_H_

#ifdef _KERN_

#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_UN 8
#define LOCK_NB 4
#define LOCK_PRIO_READER 16
#define LOCK_PRIO_WRITER 32
#define LOCK_MODE_MASK (LOCK_SH | LOCK_EX | LOCK_UN)

#define FLOCK_PRIORITY_READER 1
#define FLOCK_PRIORITY_WRITER 2

struct inode;

int flock_acquire(struct inode *ip, int lock_type, int pid);
int flock_release(struct inode *ip, int pid);
int flock_check_conflict(struct inode *ip, int lock_type, int pid);

#endif  /*_KERN_*/

#endif /* _KERN_FLOCK_EXPORT_H_  */