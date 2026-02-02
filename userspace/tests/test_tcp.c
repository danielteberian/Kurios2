/* test_tcp.c - Test TCP socket operations */
#include <stdint.h>

/* Syscall numbers */
#define SYS_WRITE 1
#define SYS_EXIT 60
#define SYS_SOCKET 41
#define SYS_BIND 49
#define SYS_LISTEN 50
#define SYS_ACCEPT 43
#define SYS_CONNECT 42

/* Socket constants */
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t sin_zero[8];
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

static inline uint16_t htons(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

int main(void) {
    print("=== TCP Socket Test ===\n");

    /* Test 1: Create TCP socket */
    print("Test 1: socket(AF_INET, SOCK_STREAM)... ");
    long sock = syscall3(SYS_SOCKET, AF_INET, SOCK_STREAM, 0);
    if (sock >= 0) {
        print("PASS (fd=");
        if (sock >= 0 && sock <= 9) {
            char c = '0' + sock;
            syscall3(SYS_WRITE, 1, (long)&c, 1);
        }
        print(")\n");
    } else {
        print("FAIL\n");
        syscall1(SYS_EXIT, 1);
    }

    /* Test 2: Bind to loopback */
    print("Test 2: bind(127.0.0.1:8080)... ");
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr = htonl((127 << 24) | 1);  /* 127.0.0.1 */
    for (int i = 0; i < 8; i++) addr.sin_zero[i] = 0;

    if (syscall3(SYS_BIND, sock, (long)&addr, sizeof(addr)) == 0) {
        print("PASS\n");
    } else {
        print("FAIL\n");
    }

    /* Test 3: Listen */
    print("Test 3: listen(backlog=5)... ");
    if (syscall2(SYS_LISTEN, sock, 5) == 0) {
        print("PASS\n");
    } else {
        print("FAIL\n");
    }

    /* Test 4: Create client socket */
    print("Test 4: create client socket... ");
    long client = syscall3(SYS_SOCKET, AF_INET, SOCK_STREAM, 0);
    if (client >= 0) {
        print("PASS\n");
    } else {
        print("FAIL\n");
    }

    /* Note: Full connect/accept test would require threading or non-blocking I/O */
    print("\nTCP socket test complete!\n");
    print("Note: Full client-server test requires multi-threading\n");

    syscall1(SYS_EXIT, 0);
    return 0;
}
