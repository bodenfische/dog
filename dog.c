/* dog - cat without libc. Linux x86_64 / aarch64. Public domain.
 * build: cc -O2 -static -nostdlib -ffreestanding -fno-builtin \
 *           -fno-stack-protector -fno-pie -no-pie dog.c -o dog */

#if defined(__x86_64__)
#define N_READ 0
#define N_WRITE 1
#define N_CLOSE 3
#define N_OPENAT 257
#define N_EXIT 231
static long sys4(long n, long a, long b, long c, long d) {
    long r;
    register long r10 __asm__("r10") = d;
    __asm__ volatile("syscall" : "=a"(r)
        : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
        : "rcx", "r11", "memory");
    return r;
}
__asm__(".globl _start\n_start:\n xor %ebp,%ebp\n mov %rsp,%rdi\n"
        " and $-16,%rsp\n call dog_main\n");
#elif defined(__aarch64__)
#define N_READ 63
#define N_WRITE 64
#define N_CLOSE 57
#define N_OPENAT 56
#define N_EXIT 94
static long sys4(long n, long a, long b, long c, long d) {
    register long x8 __asm__("x8") = n, x0 __asm__("x0") = a,
                  x1 __asm__("x1") = b, x2 __asm__("x2") = c,
                  x3 __asm__("x3") = d;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
        : "memory");
    return x0;
}
__asm__(".globl _start\n_start:\n mov x29,xzr\n mov x0,sp\n bl dog_main\n");
#else
#error "unsupported arch"
#endif

#define AT_FDCWD (-100)
#define EINTR 4

static unsigned long slen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }

static int wr(int fd, const char *p, unsigned long n) {
    while (n) {
        long r = sys4(N_WRITE, fd, (long)p, (long)n, 0);
        if (r == -EINTR) continue;
        if (r < 0) return (int)-r;
        p += r; n -= (unsigned long)r;
    }
    return 0;
}

static void say(const char *a, const char *b, const char *c) {
    wr(2, "dog: ", 5);
    if (a) wr(2, a, slen(a));
    if (b) wr(2, b, slen(b));
    if (c) wr(2, c, slen(c));
    wr(2, "\n", 1);
}

static void fail(const char *name, int e) {
    const char *m; char buf[24]; int i = 23;
    switch (e) {
    case 2:  m = "No such file or directory"; break;
    case 5:  m = "Input/output error"; break;
    case 13: m = "Permission denied"; break;
    case 20: m = "Not a directory"; break;
    case 21: m = "Is a directory"; break;
    case 28: m = "No space left on device"; break;
    case 32: m = "Broken pipe"; break;
    default:
        buf[i] = 0;
        do { buf[--i] = '0' + e % 10; e /= 10; } while (e && i > 0);
        say(name, ": error ", buf + i);
        return;
    }
    say(name, ": ", m);
}

static int copy(int fd, const char *name) {
    static char buf[65536];
    for (;;) {
        long r = sys4(N_READ, fd, (long)buf, sizeof buf, 0);
        if (r == -EINTR) continue;
        if (r < 0) { fail(name, (int)-r); return 1; }
        if (r == 0) return 0;
        int e = wr(1, buf, (unsigned long)r);
        if (e) { fail("stdout", e); return 1; }
    }
}

int dog_main(long *sp) {
    int argc = (int)sp[0], i = 1, rc = 0, files = 0, opts = 1;
    char **argv = (char **)(sp + 1);

    for (; i < argc; i++) {
        const char *a = argv[i];
        if (opts && a[0] == '-' && a[1]) {
            if (a[1] == '-' && !a[2]) { opts = 0; continue; }
            if (a[1] == 'u' && !a[2]) continue;          /* POSIX -u: no-op */
            say("invalid option -- ", a + 1, 0);
            say("usage: dog [-u] [file ...]", 0, 0);
            sys4(N_EXIT, 1, 0, 0, 0);
        }
        files = 1;
        if (a[0] == '-' && !a[1]) { rc |= copy(0, "stdin"); continue; }
        long fd = sys4(N_OPENAT, AT_FDCWD, (long)a, 0 /* O_RDONLY */, 0);
        if (fd < 0) { fail(a, (int)-fd); rc = 1; continue; }
        rc |= copy((int)fd, a);
        sys4(N_CLOSE, fd, 0, 0, 0);
    }
    if (!files) rc = copy(0, "stdin");
    sys4(N_EXIT, rc, 0, 0, 0);
    for (;;) {}
}
