/* test_rlimits.c - Test resource limits syscalls */
#include <stdint.h>

/* Syscall numbers */
#define SYS_WRITE 1
#define SYS_EXIT 60
#define SYS_GETRLIMIT 97
#define SYS_SETRLIMIT 160

/* Resource limits */
#define RLIMIT_NOFILE 7
#define RLIMIT_NPROC 6
#define RLIMIT_STACK 3

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
    struct rlimit rlim;

    print("=== Resource Limits Test ===\n");

    /* Test 1: Get RLIMIT_NOFILE */
    print("Test 1: getrlimit(RLIMIT_NOFILE)... ");
    if (syscall3(SYS_GETRLIMIT, RLIMIT_NOFILE, (long)&rlim, 0) == 0) {
        if (rlim.rlim_cur == 1024 && rlim.rlim_max == 4096) {
            print("PASS (soft=1024, hard=4096)\n");
        } else {
            print("FAIL (unexpected values)\n");
        }
    } else {
        print("FAIL (syscall failed)\n");
    }

    /* Test 2: Get RLIMIT_NPROC */
    print("Test 2: getrlimit(RLIMIT_NPROC)... ");
    if (syscall3(SYS_GETRLIMIT, RLIMIT_NPROC, (long)&rlim, 0) == 0) {
        if (rlim.rlim_cur == 256 && rlim.rlim_max == 512) {
            print("PASS (soft=256, hard=512)\n");
        } else {
            print("FAIL (unexpected values)\n");
        }
    } else {
        print("FAIL (syscall failed)\n");
    }

    /* Test 3: Set RLIMIT_NOFILE soft limit */
    print("Test 3: setrlimit(RLIMIT_NOFILE, soft=512)... ");
    rlim.rlim_cur = 512;
    rlim.rlim_max = 4096;
    if (syscall3(SYS_SETRLIMIT, RLIMIT_NOFILE, (long)&rlim, 0) == 0) {
        /* Verify */
        syscall3(SYS_GETRLIMIT, RLIMIT_NOFILE, (long)&rlim, 0);
        if (rlim.rlim_cur == 512) {
            print("PASS\n");
        } else {
            print("FAIL (value not set)\n");
        }
    } else {
        print("FAIL (syscall failed)\n");
    }

    /* Test 4: Try to set soft > hard (should fail) */
    print("Test 4: setrlimit(soft > hard)... ");
    rlim.rlim_cur = 5000;
    rlim.rlim_max = 4096;
    if (syscall3(SYS_SETRLIMIT, RLIMIT_NOFILE, (long)&rlim, 0) < 0) {
        print("PASS (correctly rejected)\n");
    } else {
        print("FAIL (should have failed)\n");
    }

    print("\nResource limits test complete!\n");

    syscall1(SYS_EXIT, 0);
    return 0;
}
