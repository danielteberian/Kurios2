/* test_limits_enforce.c - Test resource limit enforcement */
#include <stdint.h>

#define SYS_WRITE 1
#define SYS_EXIT 60
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_SETRLIMIT 160

#define RLIMIT_NOFILE 7
#define O_RDONLY 0

struct rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

static inline long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                 : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2)
                 : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long n, long a1) {
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a1)
                 : "rcx", "r11", "memory");
    return ret;
}

void print(const char *str) {
    const char *s = str;
    int len = 0;
    while (*s++) len++;
    syscall3(SYS_WRITE, 1, (long)str, len);
}

int main(void) {
    print("=== RLIMIT_NOFILE Enforcement Test ===\n");

    /* Set limit to 10 open files */
    print("Setting RLIMIT_NOFILE to 10... ");
    struct rlimit rlim;
    rlim.rlim_cur = 10;
    rlim.rlim_max = 4096;
    syscall3(SYS_SETRLIMIT, RLIMIT_NOFILE, (long)&rlim, 0);
    print("done\n");

    /* Try to open files */
    print("Opening /dev/null multiple times:\n");
    int fds[15];
    int count = 0;

    for (int i = 0; i < 15; i++) {
        long fd = syscall3(SYS_OPEN, (long)"/dev/null", O_RDONLY, 0);
        if (fd >= 0) {
            fds[count++] = fd;
            print("  Open ");
            char c = '0' + (i + 1);
            syscall3(SYS_WRITE, 1, (long)&c, 1);
            print(": SUCCESS (fd=");
            if (fd >= 0 && fd <= 9) {
                char fc = '0' + fd;
                syscall3(SYS_WRITE, 1, (long)&fc, 1);
            }
            print(")\n");
        } else {
            print("  Open ");
            char c = '0' + (i + 1);
            syscall3(SYS_WRITE, 1, (long)&c, 1);
            print(": FAIL (limit reached)\n");
            break;
        }
    }

    /* Close files */
    print("\nClosing files...\n");
    for (int i = 0; i < count; i++) {
        syscall1(SYS_CLOSE, fds[i]);
    }

    if (count >= 7 && count <= 10) {
        print("\nTest PASSED: Limit enforced correctly\n");
        print("(Opened ");
        char c = '0' + count;
        syscall3(SYS_WRITE, 1, (long)&c, 1);
        print(" files before limit)\n");
    } else {
        print("\nTest FAILED: Unexpected behavior\n");
    }

    syscall1(SYS_EXIT, 0);
    return 0;
}
