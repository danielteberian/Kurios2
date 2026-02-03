/* syscall.h - System Call Interface */
#ifndef _SYSCALL_SYSCALL_H
#define _SYSCALL_SYSCALL_H

#include <stdint.h>

/*
 * System Call Numbers
 *
 * These are the syscall numbers passed in RAX.
 * Convention follows Linux x86_64 ABI for familiarity.
 */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSTAT       6
#define SYS_LSEEK       8
#define SYS_MMAP        9
#define SYS_MPROTECT    10
#define SYS_MUNMAP      11
#define SYS_MSYNC       26
#define SYS_BRK         12
#define SYS_SIGACTION   13
#define SYS_SIGPROCMASK 14
#define SYS_SIGRETURN   15
#define SYS_IOCTL       16
#define SYS_ACCESS      21
#define SYS_PIPE        22
#define SYS_POLL        7
#define SYS_SELECT      23
#define SYS_SCHED_YIELD 24
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_NANOSLEEP   35
#define SYS_ALARM       37
#define SYS_GETPID      39
#define SYS_SOCKET      41
#define SYS_CONNECT     42
#define SYS_ACCEPT      43
#define SYS_SENDTO      44
#define SYS_RECVFROM    45
#define SYS_BIND        49
#define SYS_LISTEN      50
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61      /* waitpid - wait for child process */
#define SYS_KILL        62      /* kill - send signal */
#define SYS_UNAME       63
#define SYS_FCNTL       72
#define SYS_TRUNCATE    76
#define SYS_FTRUNCATE   77
#define SYS_GETDENTS    78
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_FCHDIR      81
#define SYS_RENAME      82
#define SYS_MKDIR       83
#define SYS_RMDIR       84
#define SYS_LINK        86
#define SYS_UNLINK      87
#define SYS_SYMLINK     88
#define SYS_READLINK    89
#define SYS_UMASK       95
#define SYS_GETTIMEOFDAY 96
#define SYS_GETRUSAGE   98
#define SYS_TIMES       100
#define SYS_GETRLIMIT   97
#define SYS_GETUID      102
#define SYS_SYSLOG      103
#define SYS_GETGID      104
#define SYS_SETUID      105
#define SYS_SETGID      106
#define SYS_GETEUID     107
#define SYS_GETEGID     108
#define SYS_SETREUID    113
#define SYS_SETREGID    114
#define SYS_SETRESUID   117
#define SYS_SETRESGID   119
#define SYS_GETRESUID   118
#define SYS_GETRESGID   120
#define SYS_GETPPID     110
#define SYS_SETSID      112
#define SYS_GETPGID     121
#define SYS_SETPGID     109
#define SYS_GETSID      124
#define SYS_SETRLIMIT   160
#define SYS_SYNC        162
#define SYS_FSYNC       74
#define SYS_FDATASYNC   75
#define SYS_CHMOD       90
#define SYS_FCHMOD      91
#define SYS_CHOWN       92
#define SYS_FCHOWN      93
#define SYS_LCHOWN      94
#define SYS_MKNOD         133
#define SYS_CLOCK_GETTIME 228
#define SYS_CLOCK_GETRES  229
#define SYS_GETRANDOM     318

/* IPC - POSIX Shared Memory */
#define SYS_SHM_OPEN      29
#define SYS_SHM_UNLINK    30

/* IPC - POSIX Semaphores */
#define SYS_SEM_OPEN      269
#define SYS_SEM_CLOSE     270
#define SYS_SEM_UNLINK    271
#define SYS_SEM_WAIT      272
#define SYS_SEM_POST      273
#define SYS_SEM_TRYWAIT   274
#define SYS_SEM_GETVALUE  275

/* IPC - POSIX Message Queues */
#define SYS_MQ_OPEN       240
#define SYS_MQ_UNLINK     241
#define SYS_MQ_TIMEDSEND  242
#define SYS_MQ_TIMEDRECEIVE 243
#define SYS_MQ_NOTIFY     244
#define SYS_MQ_GETSETATTR 245

/* File type bits for mknod mode */
#define S_IFMT      0170000     /* File type mask */
#define S_IFSOCK    0140000     /* Socket */
#define S_IFLNK     0120000     /* Symbolic link */
#define S_IFREG     0100000     /* Regular file */
#define S_IFBLK     0060000     /* Block device */
#define S_IFDIR     0040000     /* Directory */
#define S_IFCHR     0020000     /* Character device */
#define S_IFIFO     0010000     /* FIFO */

/* Permission bits */
#define S_ISUID     04000       /* Set-user-ID */
#define S_ISGID     02000       /* Set-group-ID */
#define S_ISVTX     01000       /* Sticky bit */
#define S_IRWXU     00700       /* Owner: rwx */
#define S_IRUSR     00400       /* Owner: read */
#define S_IWUSR     00200       /* Owner: write */
#define S_IXUSR     00100       /* Owner: execute */
#define S_IRWXG     00070       /* Group: rwx */
#define S_IRGRP     00040       /* Group: read */
#define S_IWGRP     00020       /* Group: write */
#define S_IXGRP     00010       /* Group: execute */
#define S_IRWXO     00007       /* Other: rwx */
#define S_IROTH     00004       /* Other: read */
#define S_IWOTH     00002       /* Other: write */
#define S_IXOTH     00001       /* Other: execute */

/* File type test macros */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* getrandom() flags */
#define GRND_NONBLOCK     0x01   /* Don't block if no entropy */
#define GRND_RANDOM       0x02   /* Use /dev/random instead of /dev/urandom */

/* waitpid options */
#define WNOHANG         1       /* Don't block waiting */
#define WUNTRACED       2       /* Also return stopped children */
#define WCONTINUED      8       /* Also return continued children */

/* waitpid macros - extract info from status */
#define WIFEXITED(status)       (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)     (((status) >> 8) & 0xff)
#define WIFSIGNALED(status)     (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status)        ((status) & 0x7f)
#define WIFSTOPPED(status)      (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)        (((status) >> 8) & 0xff)
#define WIFCONTINUED(status)    ((status) == 0xffff)

/* mmap flags */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_FAILED      ((void *)-1)

/* msync flags */
#define MS_ASYNC        1       /* Sync memory asynchronously */
#define MS_SYNC         4       /* Synchronous memory sync */
#define MS_INVALIDATE   2       /* Invalidate cached data */

/* fcntl commands */
#define F_DUPFD         0       /* Duplicate fd */
#define F_GETFD         1       /* Get fd flags */
#define F_SETFD         2       /* Set fd flags */
#define F_GETFL         3       /* Get file status flags */
#define F_SETFL         4       /* Set file status flags */

/* Note: FD_CLOEXEC defined in fd_table.h, O_NONBLOCK/O_APPEND in vfs.h */

/* access() modes */
#define F_OK            0       /* Test for existence */
#define X_OK            1       /* Test for execute permission */
#define W_OK            2       /* Test for write permission */
#define R_OK            4       /* Test for read permission */

/* clock_gettime clocks */
#define CLOCK_REALTIME          0
#define CLOCK_MONOTONIC         1
#define CLOCK_PROCESS_CPUTIME   2
#define CLOCK_THREAD_CPUTIME    3
#define CLOCK_MONOTONIC_RAW     4
#define CLOCK_REALTIME_COARSE   5
#define CLOCK_MONOTONIC_COARSE  6

/* timespec structure */
typedef struct timespec {
    int64_t tv_sec;     /* Seconds */
    int64_t tv_nsec;    /* Nanoseconds */
} timespec_t;

/* timeval structure */
typedef struct timeval {
    int64_t tv_sec;     /* Seconds */
    int64_t tv_usec;    /* Microseconds */
} timeval_t;

/* timezone structure */
typedef struct timezone {
    int32_t tz_minuteswest;
    int32_t tz_dsttime;
} timezone_t;

/* utsname structure for uname() */
#define UTSNAME_LENGTH 65
typedef struct utsname {
    char sysname[UTSNAME_LENGTH];
    char nodename[UTSNAME_LENGTH];
    char release[UTSNAME_LENGTH];
    char version[UTSNAME_LENGTH];
    char machine[UTSNAME_LENGTH];
    char domainname[UTSNAME_LENGTH];
} utsname_t;

/* Directory entry for getdents */
typedef struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];  /* Null-terminated filename */
} linux_dirent64_t;

/* d_type values */
#define DT_UNKNOWN      0
#define DT_FIFO         1
#define DT_CHR          2
#define DT_DIR          4
#define DT_BLK          6
#define DT_REG          8
#define DT_LNK          10
#define DT_SOCK         12

/* Maximum syscall number */
#define SYS_MAX         256

/*
 * Syscall Register Convention (x86_64)
 *
 * Input:
 *   RAX = syscall number
 *   RDI = arg1
 *   RSI = arg2
 *   RDX = arg3
 *   R10 = arg4 (not RCX, which is clobbered by SYSCALL)
 *   R8  = arg5
 *   R9  = arg6
 *
 * Output:
 *   RAX = return value (negative for error)
 *
 * Preserved by SYSCALL:
 *   RCX = return RIP (saved by CPU)
 *   R11 = return RFLAGS (saved by CPU)
 *
 * The kernel must preserve: RBX, RBP, R12-R15
 */

/*
 * Syscall frame - saved registers on kernel stack during syscall
 */
typedef struct {
    /* Callee-saved registers (we must preserve these) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;

    /* Syscall arguments (caller-saved, but we save for C handler) */
    uint64_t r9;        /* arg6 */
    uint64_t r8;        /* arg5 */
    uint64_t r10;       /* arg4 */
    uint64_t rdx;       /* arg3 */
    uint64_t rsi;       /* arg2 */
    uint64_t rdi;       /* arg1 */

    /* Syscall number */
    uint64_t rax;

    /* Saved by SYSCALL instruction (we push these) */
    uint64_t rcx;       /* User RIP */
    uint64_t r11;       /* User RFLAGS */

    /* User stack pointer (we save this) */
    uint64_t rsp;       /* User RSP */
} __attribute__((packed)) syscall_frame_t;

/*
 * Syscall handler function type
 * Takes up to 6 arguments, returns int64_t (negative = error)
 */
typedef int64_t (*syscall_handler_t)(uint64_t arg1, uint64_t arg2,
                                     uint64_t arg3, uint64_t arg4,
                                     uint64_t arg5, uint64_t arg6);

/*
 * Initialize syscall infrastructure
 * Configures MSRs for SYSCALL/SYSRET and registers handlers
 */
void syscall_init(void);

/*
 * Register a syscall handler
 *
 * @param num     Syscall number
 * @param handler Handler function
 * @return 0 on success, -1 if invalid number or already registered
 */
int syscall_register(int num, syscall_handler_t handler);

/*
 * Main syscall dispatcher (called from assembly)
 * This function is called with the syscall frame on the stack.
 *
 * @param frame  Pointer to saved registers
 * @return Return value to put in RAX
 */
int64_t syscall_dispatch(syscall_frame_t *frame);

/*
 * Assembly entry point (in syscall_entry.asm)
 */
extern void syscall_entry(void);

#ifdef DEBUG_TESTS
/*
 * Run syscall infrastructure tests
 */
void syscall_run_tests(void);
#endif

#endif /* _SYSCALL_SYSCALL_H */
