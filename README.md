# OS_Semester_Project
A file-locking system simulating linux's flock syscall

# File Locking Project Changes

This document summarizes the code changes made for the file locking (flock) system and explains the new attributes and flags. It also answers the scheduling question about interrupts and shared files.

## 1) Data Structure Changes

### a) File Descriptor Structure (kern/fs/file.h)
Added fields in struct file:
- lock_type
  - Purpose: Tracks the lock state owned by this file descriptor.
  - Values (project convention):
    - 0: No lock
    - 1: Exclusive lock
    - 2: Shared lock
  - Why: A single process can open the same file multiple times. Each file descriptor may hold a lock, so we track it per descriptor.

- pid
  - Purpose: Records the process ID that owns the lock via this file descriptor.
  - Why: Helps validate ownership when releasing or upgrading locks.

- lock_spinlock
  - Purpose: A spinlock to protect lock-related fields inside struct file (lock_type, pid).
  - Why: Prevents races when multiple CPUs or threads manipulate descriptor state.

### b) Inode Structure (kern/fs/inode.h)
Added fields in struct inode:
- lock_spinlock
  - Purpose: Protects inode-level lock state shared across all processes and file descriptors.
  - Why: All flock operations must synchronize on a shared lock state for the file (inode).

- exclusive_lock_pid
  - Purpose: Stores the PID of the process holding the exclusive lock, or -1 if no exclusive lock.
  - Why: Needed to check conflicts and to release ownership safely.

- shared_lock_count
  - Purpose: Number of current shared lock holders.
  - Why: Shared locks allow multiple readers; count makes this efficient and safe.

## 2) Initialization Changes

### a) File Descriptor Allocation (kern/fs/file.c)
In file_alloc():
- lock_type initialized to 0
- pid initialized to -1
- lock_spinlock initialized

In file_close():
- lock_type reset to 0
- pid reset to -1

Purpose:
- Ensures clean lock state on creation and on release of descriptors.

### b) Inode Cache Initialization (kern/fs/inode.c)
In inode_init():
- Each cached inode gets its lock_spinlock initialized
- exclusive_lock_pid set to -1
- shared_lock_count set to 0

In inode_get():
- When a cached inode entry is reused, exclusive_lock_pid and shared_lock_count are reset

Purpose:
- Prevents stale lock state when inode cache entries are recycled.

## 3) Flock Module (kern/flock/)

### a) export.h
Defines the user-facing constants and APIs:
- LOCK_SH: Shared lock
- LOCK_EX: Exclusive lock
- LOCK_UN: Unlock
- LOCK_NB: Non-blocking flag

Exports:
- flock_acquire(inode, operation, pid)
- flock_release(inode, pid)
- flock_check_conflict(inode, operation, pid)

### b) import.h
Includes core dependencies:
- spinlock
- inode
- current process ID interface

### c) flock.c
Core lock logic:
- flock_check_conflict()
  - Checks whether a requested lock conflicts with existing locks
  - Exclusive lock conflicts with any other lock
  - Shared lock conflicts only with exclusive lock

- flock_acquire()
  - Acquires LOCK_SH or LOCK_EX
  - Returns success only if there is no conflict

- flock_release()
  - Releases exclusive lock if held by pid
  - Decrements shared_lock_count for shared locks

Notes:
- LOCK_NB is interpreted by masking: operation & ~LOCK_NB
- LOCK_UN is handled in syscall layer by calling flock_release()

## 4) Syscall Integration (kern/fs/sysfile.c + sysfile.h)
Added sys_flock() handler:
- Parses fd and operation
- Validates fd and inode
- Delegates to flock_acquire() or flock_release()
- Returns appropriate error codes

This connects user-space flock() calls to kernel lock management.

## 5) Flags and Attributes Summary

### File Descriptor Fields
- lock_type: per-descriptor lock state (0 none, 1 exclusive, 2 shared)
- pid: owner of the descriptor lock
- lock_spinlock: protects file descriptor lock fields

### Inode Fields
- lock_spinlock: protects inode lock state
- exclusive_lock_pid: PID holding exclusive lock, -1 if none
- shared_lock_count: count of shared lock holders

### Flock Flags
- LOCK_SH: shared lock
- LOCK_EX: exclusive lock
- LOCK_UN: unlock
- LOCK_NB: non-blocking flag, typically combined with LOCK_SH/LOCK_EX

## 6) Instructor Question: Interrupts + Shared Files

Question:
When interrupts are done in processes involving shared files, do we store them in the ready queue? If not, what is our approach, and how do we differentiate them from processes with non shared resources?

Answer (project-level policy):
- If a process blocks waiting for a file lock (shared or exclusive), it should NOT remain in the ready queue.
- Instead, it moves to a blocked or wait state, typically on a wait channel tied to the inode lock.

Approach:
- If LOCK_NB is set, we return immediately with failure (no blocking).
- If LOCK_NB is not set and the lock is not available, the process sleeps on the inode or a lock-specific wait queue.
- When a lock is released, we wake up blocked processes waiting on that inode.

Differentiation:
- Processes waiting on shared resources (file locks) are in a blocked queue tied to the resource.
- Processes that are runnable (no blocking) remain in the ready queue.
- We differentiate using the wait channel or blocking reason (e.g., wait on inode lock), not by a separate "shared vs non-shared" queue.

Key idea:
- Ready queue = runnable
- Blocked queue = waiting on lock or I/O
- Interrupt completion (or lock release) moves the process from blocked to ready

This design prevents busy-waiting and ensures fair scheduling.

