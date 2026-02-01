/* syscall.h - Syscall numbers and wrappers */
#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* System call numbers (Linux x86_64 compatible) */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSEEK       8
#define SYS_MMAP        9
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_IOCTL       16
#define SYS_PIPE        22
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_GETPID      39
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61
#define SYS_KILL        62
#define SYS_UNAME       63
#define SYS_GETDENTS    78
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_MKDIR       83
#define SYS_RMDIR       84
#define SYS_UNLINK      87

/* Open flags */
#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_CREAT         0x0040
#define O_TRUNC         0x0200
#define O_APPEND        0x0400
#define O_DIRECTORY     0x10000

/* Wait options */
#define WNOHANG         1

/* Wait macros */
#define WIFEXITED(status)    (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)  (((status) >> 8) & 0xff)
#define WIFSIGNALED(status)  (((status) & 0x7f) != 0)
#define WTERMSIG(status)     ((status) & 0x7f)

/* Seek whence */
#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

/* Error codes */
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EEXIST          17
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define EMFILE          24
#define ENOSPC          28
#define EPIPE           32
#define ENOTEMPTY       39
#define ENOSYS          38
#define ECHILD          10

/* Types */
typedef int64_t ssize_t;
typedef int32_t pid_t;

/* Directory entry structure */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

#define DT_UNKNOWN      0
#define DT_DIR          4
#define DT_REG          8

/* Syscall wrappers (implemented in syscall.S) */
long syscall0(long num);
long syscall1(long num, long arg1);
long syscall2(long num, long arg1, long arg2);
long syscall3(long num, long arg1, long arg2, long arg3);
long syscall4(long num, long arg1, long arg2, long arg3, long arg4);
long syscall6(long num, long arg1, long arg2, long arg3, long arg4, long arg5, long arg6);

/* Convenience wrappers */
static inline void _exit(int status) {
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

static inline ssize_t read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (long)buf, count);
}

static inline ssize_t write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}

static inline int open(const char *path, int flags) {
    return syscall2(SYS_OPEN, (long)path, flags);
}

static inline int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

static inline pid_t fork(void) {
    return syscall0(SYS_FORK);
}

static inline int execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}

static inline pid_t waitpid(pid_t pid, int *status, int options) {
    return syscall4(SYS_WAIT4, pid, (long)status, options, 0);
}

static inline pid_t getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline int chdir(const char *path) {
    return syscall1(SYS_CHDIR, (long)path);
}

static inline char *getcwd(char *buf, size_t size) {
    long ret = syscall2(SYS_GETCWD, (long)buf, size);
    return ret < 0 ? NULL : buf;
}

static inline int getdents64(int fd, void *dirp, size_t count) {
    return syscall3(SYS_GETDENTS, fd, (long)dirp, count);
}

static inline int mkdir(const char *path, int mode) {
    return syscall2(SYS_MKDIR, (long)path, mode);
}

static inline int unlink(const char *path) {
    return syscall1(SYS_UNLINK, (long)path);
}

static inline int dup2(int oldfd, int newfd) {
    return syscall2(SYS_DUP2, oldfd, newfd);
}

static inline int pipe(int pipefd[2]) {
    return syscall1(SYS_PIPE, (long)pipefd);
}

#endif /* _SYSCALL_H */
