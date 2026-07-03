/* Terminal control for perch.
 *
 * Every value crosses the FFI as int64 or const char *, so the Raven side
 * speaks only Int and CStr. POSIX uses termios and poll; Windows uses the
 * console API with virtual terminal processing switched on, so the same
 * ANSI sequences work on both sides. */

#include <stdint.h>
#include <string.h>

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static DWORD orig_in_mode;
static DWORD orig_out_mode;
static UINT orig_in_cp;
static UINT orig_out_cp;
static int saved = 0;

/* Put the terminal in raw mode: no line buffering, no echo, no signal keys,
 * VT sequences in and out, UTF-8 code pages. Returns 0 on success, -1 when
 * stdin or stdout is not a console. */
int64_t rp_raw_enable(void) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD in_mode, out_mode;
    if (!GetConsoleMode(hin, &in_mode) || !GetConsoleMode(hout, &out_mode)) return -1;
    if (!saved) {
        orig_in_mode = in_mode;
        orig_out_mode = out_mode;
        orig_in_cp = GetConsoleCP();
        orig_out_cp = GetConsoleOutputCP();
        saved = 1;
    }
    in_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT
                 | ENABLE_QUICK_EDIT_MODE);
    in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS
               | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT;
    out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(hin, in_mode) || !SetConsoleMode(hout, out_mode)) return -1;
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    return 0;
}

int64_t rp_raw_disable(void) {
    if (!saved) return 0;
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(hin, orig_in_mode);
    SetConsoleMode(hout, orig_out_mode);
    SetConsoleCP(orig_in_cp);
    SetConsoleOutputCP(orig_out_cp);
    return 0;
}

int64_t rp_cols(void) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(hout, &info)) return -1;
    return (int64_t)(info.srWindow.Right - info.srWindow.Left + 1);
}

int64_t rp_rows(void) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(hout, &info)) return -1;
    return (int64_t)(info.srWindow.Bottom - info.srWindow.Top + 1);
}

/* Read one input byte. timeout_ms < 0 blocks. Returns the byte value,
 * -1 on timeout, -2 on end of input.
 *
 * On a console we consume input records one at a time and keep only key-down
 * bytes; with VT input enabled an arrow key arrives as its escape sequence
 * spread over several records. Mouse and resize records are dropped here
 * (mouse reporting arrives as VT byte sequences when it is enabled). On a
 * pipe we fall back to a plain blocking read. */
int64_t rp_read_byte(int64_t timeout_ms) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(hin, &mode)) {
        unsigned char b;
        DWORD got = 0;
        if (!ReadFile(hin, &b, 1, &got, NULL) || got == 0) return -2;
        return (int64_t)b;
    }
    ULONGLONG deadline = 0;
    if (timeout_ms >= 0) deadline = GetTickCount64() + (ULONGLONG)timeout_ms;
    for (;;) {
        DWORD wait = INFINITE;
        if (timeout_ms >= 0) {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) return -1;
            wait = (DWORD)(deadline - now);
        }
        DWORD r = WaitForSingleObject(hin, wait);
        if (r == WAIT_TIMEOUT) return -1;
        if (r != WAIT_OBJECT_0) return -2;
        INPUT_RECORD rec;
        DWORD got = 0;
        if (!ReadConsoleInputA(hin, &rec, 1, &got) || got == 0) return -2;
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            unsigned char c = (unsigned char)rec.Event.KeyEvent.uChar.AsciiChar;
            if (c != 0) return (int64_t)c;
        }
    }
}

void rp_write(const char *s) {
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = (DWORD)strlen(s);
    DWORD written = 0;
    WriteFile(hout, s, n, &written, NULL);
}

void rp_flush(void) {
    /* WriteFile to the console is unbuffered; nothing to do. The function
     * stays so the Raven API keeps one shape on every platform. */
}

#else /* POSIX */

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;
static int saved = 0;

/* Raw mode: no line buffering, no echo, no signal keys, no flow control.
 * Output processing stays on so \r\n behaves. Returns 0 on success, -1 when
 * stdin is not a terminal. */
int64_t rp_raw_enable(void) {
    struct termios t;
    if (tcgetattr(STDIN_FILENO, &t) != 0) return -1;
    if (!saved) {
        orig_termios = t;
        saved = 1;
    }
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return -1;
    return 0;
}

int64_t rp_raw_disable(void) {
    if (!saved) return 0;
    return tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios) == 0 ? 0 : -1;
}

int64_t rp_cols(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) return -1;
    return (int64_t)ws.ws_col;
}

int64_t rp_rows(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_row == 0) return -1;
    return (int64_t)ws.ws_row;
}

/* Read one input byte. timeout_ms < 0 blocks. Returns the byte value,
 * -1 on timeout, -2 on end of input. */
int64_t rp_read_byte(int64_t timeout_ms) {
    struct pollfd p;
    p.fd = STDIN_FILENO;
    p.events = POLLIN;
    p.revents = 0;
    int t = timeout_ms < 0 ? -1 : (int)timeout_ms;
    int r = poll(&p, 1, t);
    if (r == 0) return -1;
    if (r < 0) return -2;
    unsigned char b;
    ssize_t got = read(STDIN_FILENO, &b, 1);
    if (got <= 0) return -2;
    return (int64_t)b;
}

void rp_write(const char *s) {
    size_t n = strlen(s);
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(STDOUT_FILENO, s + off, n - off);
        if (w <= 0) return;
        off += (size_t)w;
    }
}

void rp_flush(void) {
    /* write(2) is unbuffered; kept for API symmetry. */
}

#endif

/* One byte as a single-character C string. The buffer is static: the Raven
 * wrapper copies it into a GC string right away and must not free it. */
static char byte_buf[2];

const char *rp_byte_str(int64_t b) {
    byte_buf[0] = (char)(b & 0xFF);
    byte_buf[1] = 0;
    return byte_buf;
}
