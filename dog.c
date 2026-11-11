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

/* flags */
static int f_n, f_b, f_s, f_E, f_T, f_v;

/* line state, carried across files like GNU cat */
static int bol = 1, prev_empty, pend_cr;
static unsigned long lineno;

/* output buffer */
static char ob[65536];
static unsigned long on;

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

static void flush(void);

static void say(const char *a, const char *b, const char *c) {
    flush();                                /* keep stdout/stderr in order */
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

static void flush(void) {
    unsigned long n = on;
    on = 0;
    if (n) {
        int e = wr(1, ob, n);
        if (e) { fail("stdout", e); sys4(N_EXIT, 1, 0, 0, 0); }
    }
}

static void put(char c) {
    if (on == sizeof ob) flush();
    ob[on++] = c;
}

static void put_number(void) {              /* GNU format: "%6lu\t" */
    char t[24]; int i = 24; unsigned long n = ++lineno;
    do { t[--i] = '0' + n % 10; n /= 10; } while (n);
    for (int pad = 6 - (24 - i); pad > 0; pad--) put(' ');
    while (i < 24) put(t[i++]);
    put('\t');
}

static void process(const char *p, long n) {
    for (; n > 0; p++, n--) {
        char c = *p;
        if (pend_cr) {                      /* GNU -E: CR before LF is ^M */
            pend_cr = 0;
            if (c == '\n') { put('^'); put('M'); }
            else put('\r');
        }
        if (bol) {
            if (c == '\n') {                /* blank line */
                if (f_s && prev_empty) continue;
                prev_empty = 1;
                if (f_n && !f_b) put_number();
                if (f_E) put('$');
                put('\n');
                continue;
            }
            prev_empty = 0;
            if (f_n || f_b) put_number();
            bol = 0;
        }
        if (c == '\n') { if (f_E) put('$'); bol = 1; put(c); }
        else if (c == '\t') { if (f_T) { put('^'); put('I'); } else put(c); }
        else if (f_v) {                     /* ^X and M-X notation */
            unsigned char u = (unsigned char)c;
            if (u >= 128) { put('M'); put('-'); u -= 128; }
            if (u < 32) { put('^'); put((char)(u + 64)); }
            else if (u == 127) { put('^'); put('?'); }
            else put((char)u);
        }
        else if (c == '\r' && f_E) pend_cr = 1;
        else put(c);
    }
}

static int copy(int fd, const char *name) {
    static char buf[65536];
    int plain = !(f_n | f_b | f_s | f_E | f_T | f_v);
    for (;;) {
        long r = sys4(N_READ, fd, (long)buf, sizeof buf, 0);
        if (r == -EINTR) continue;
        if (r < 0) { fail(name, (int)-r); return 1; }
        if (r == 0) {
            if (pend_cr) { pend_cr = 0; put('\r'); }
            return 0;
        }
        if (plain) {
            int e = wr(1, buf, (unsigned long)r);
            if (e) { fail("stdout", e); sys4(N_EXIT, 1, 0, 0, 0); }
        } else {
            process(buf, r);
        }
    }
}

static const char help[] =
    "usage: dog [-AbeEnstTuv] [file ...]\n"
    "  -A, --show-all           same as -vET\n"
    "  -b, --number-nonblank    number non-blank lines, overrides -n\n"
    "  -e                       same as -vE\n"
    "  -E, --show-ends          show $ at end of each line\n"
    "  -n, --number             number all lines\n"
    "  -s, --squeeze-blank      squeeze repeated blank lines\n"
    "  -t                       same as -vT\n"
    "  -T, --show-tabs          show TABs as ^I\n"
    "  -u                       ignored\n"
    "  -v, --show-nonprinting   show nonprinting as ^X / M-X\n"
    "      --help               show this help\n"
    "With no file, or -, read stdin.\n";

static const struct { const char *name; char flag; } longopts[] = {
    { "show-all", 'A' },        { "number-nonblank", 'b' },
    { "show-ends", 'E' },       { "number", 'n' },
    { "squeeze-blank", 's' },   { "show-tabs", 'T' },
    { "show-nonprinting", 'v' },
};

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static void usage(void) {
    say("usage: dog [-AbeEnstTuv] [file ...]", 0, 0);
    sys4(N_EXIT, 1, 0, 0, 0);
}

static int setflag(char c) {                /* 0 = ok, 1 = unknown */
    switch (c) {
    case 'n': f_n = 1; break;
    case 'b': f_b = 1; break;
    case 's': f_s = 1; break;
    case 'E': f_E = 1; break;
    case 'T': f_T = 1; break;
    case 'v': f_v = 1; break;
    case 'A': f_v = f_E = f_T = 1; break;
    case 'e': f_v = f_E = 1; break;
    case 't': f_v = f_T = 1; break;
    case 'u': break;                        /* POSIX -u: no-op */
    default: return 1;
    }
    return 0;
}

int dog_main(long *sp) {
    int argc = (int)sp[0], i, rc = 0, files = 0, opts;
    char **argv = (char **)(sp + 1);

    /* pass 1: options (anywhere before "--", like GNU) */
    for (i = 1, opts = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!opts || a[0] != '-' || !a[1]) continue;
        if (a[1] == '-') {
            unsigned k;
            if (!a[2]) { opts = 0; continue; }
            if (streq(a + 2, "help")) {
                wr(1, help, sizeof help - 1);
                sys4(N_EXIT, 0, 0, 0, 0);
            }
            for (k = 0; k < sizeof longopts / sizeof *longopts; k++)
                if (streq(a + 2, longopts[k].name)) break;
            if (k == sizeof longopts / sizeof *longopts) {
                say("unrecognized option '", a, "'");
                usage();
            }
            setflag(longopts[k].flag);
            continue;
        }
        for (a++; *a; a++) {
            if (setflag(*a)) {
                char o[2] = { *a, 0 };
                say("invalid option -- '", o, "'");
                usage();
            }
        }
    }

    /* pass 2: files */
    for (i = 1, opts = 1; i < argc; i++) {
        const char *a = argv[i];
        if (opts && a[0] == '-' && a[1]) {
            if (a[1] == '-' && !a[2]) opts = 0;
            continue;
        }
        files = 1;
        if (a[0] == '-' && !a[1]) { rc |= copy(0, "stdin"); continue; }
        long fd = sys4(N_OPENAT, AT_FDCWD, (long)a, 0 /* O_RDONLY */, 0);
        if (fd < 0) { fail(a, (int)-fd); rc = 1; continue; }
        rc |= copy((int)fd, a);
        sys4(N_CLOSE, fd, 0, 0, 0);
    }
    if (!files) rc = copy(0, "stdin");
    flush();
    sys4(N_EXIT, rc, 0, 0, 0);
    for (;;) {}
}