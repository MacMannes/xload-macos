// serial_posix.h
//
// Drop-in replacement for FTDI D2XX (ftd2xx.h) using standard POSIX serial port
// calls. Works on macOS via the built-in VCP driver that exposes the FTDI chip
// as /dev/tty.usbserial-*.
//
// Device discovery order:
//   1. XLOAD_PORT environment variable  (e.g. export XLOAD_PORT=/dev/tty.usbserial-ABC)
//   2. First /dev/tty.usbserial-* found
//   3. First /dev/tty.usbmodem* found

#pragma once

#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <string>

// IOSSIOSPEED lets us set non-standard baud rates (e.g. 500000, 12000000)
// on macOS without needing to link IOKit.
#ifndef IOSSIOSPEED
#define IOSSIOSPEED _IOW('T', 2, speed_t)
#endif

// ---------------------------------------------------------------------------
// Types that mirror the D2XX / Windows types used in XLoad.cpp
// ---------------------------------------------------------------------------

typedef int          FT_HANDLE;
typedef int          FT_STATUS;
typedef unsigned long DWORD;
typedef void*        PVOID;

// FT_STATUS values
#define FT_OK                0
#define FT_INVALID_HANDLE    1
#define FT_DEVICE_NOT_FOUND  2
#define FT_IO_ERROR          3

// FT_OpenEx flags
#define FT_OPEN_BY_DESCRIPTION  2

// FT_SetDataCharacteristics constants
#define FT_BITS_8       8
#define FT_STOP_BITS_1  0
#define FT_PARITY_NONE  0

// Stub struct – only used inside the dead `if(0)` debug block in XLoad.cpp
typedef struct {
    DWORD     Flags;
    DWORD     Type;
    DWORD     ID;
    DWORD     LocId;
    char      SerialNumber[16];
    char      Description[64];
    FT_HANDLE ftHandle;
} FT_DEVICE_LIST_INFO_NODE;

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

// Stores the requested baud rate so FT_SetDataCharacteristics can re-apply
// it via IOSSIOSPEED after tcsetattr() (which would otherwise reset it).
static DWORD _ftposix_baud = 9600;

// Cached termios state.  We never call tcgetattr() after IOSSIOSPEED has been
// used, because macOS then returns a struct with an invalid baud field that
// makes the next tcsetattr() fail with EINVAL.  Instead we maintain our own
// copy and set a dummy standard speed (B9600) before each tcsetattr() call so
// the kernel accepts it, then immediately re-apply the real rate via IOSSIOSPEED.
static struct termios _ftposix_tty;
static bool          _ftposix_tty_init = false;



// Helper: tcsetattr() + IOSSIOSPEED in one step.
static FT_STATUS _ftposix_apply_tty(FT_HANDLE handle)
{
    // A standard baud placeholder keeps tcsetattr() happy.
    cfsetispeed(&_ftposix_tty, B9600);
    cfsetospeed(&_ftposix_tty, B9600);
    if (tcsetattr(handle, TCSANOW, &_ftposix_tty) < 0) {
        perror("[serial_posix] tcsetattr");
        return FT_IO_ERROR;
    }
    speed_t speed = (speed_t)_ftposix_baud;
    if (ioctl(handle, IOSSIOSPEED, &speed) < 0) {
        perror("[serial_posix] IOSSIOSPEED");
        return FT_IO_ERROR;
    }
    return FT_OK;
}

// ---------------------------------------------------------------------------
// Helper: find the best available serial port.
//
// channel_hint: 'A', 'B', ... – used to pick among multiple ports exposed
//   by a multi-interface FTDI chip (e.g. FT2232).  On macOS the two channels
//   of such a device appear as consecutive tty.usbserial-XXXXXXX0 /
//   tty.usbserial-XXXXXXX1 entries (channel A = index 0, B = index 1, etc.).
//   If channel_hint is '\0' (unknown) the first port is returned.
// ---------------------------------------------------------------------------
static std::string _ftposix_find_port(char channel_hint = '\0')
{
    const char* env = getenv("XLOAD_PORT");
    if (env && env[0] != '\0')
        return env;

    // On macOS prefer cu.* (call-out) over tty.* (call-in):
    // tty.* devices wait for a DCD carrier signal that embedded targets
    // never assert, which causes open() to fail with ENXIO.
    const char* patterns[] = {
        "/dev/cu.usbserial-*",
        "/dev/cu.usbmodem*",
        "/dev/tty.usbserial-*",
        "/dev/tty.usbmodem*",
        nullptr
    };

    // Normalise hint to upper-case letter; map to 0-based index.
    int want_index = 0;
    if (channel_hint >= 'a' && channel_hint <= 'z')
        channel_hint = (char)(channel_hint - 'a' + 'A');
    if (channel_hint >= 'A' && channel_hint <= 'Z')
        want_index = channel_hint - 'A';   // A→0, B→1, C→2, …

    for (int i = 0; patterns[i]; ++i) {
        glob_t g;
        memset(&g, 0, sizeof(g));
        if (glob(patterns[i], 0, nullptr, &g) == 0 && g.gl_pathc > 0) {
            // Pick the port at want_index, clamped to the number available.
            size_t idx = (want_index < (int)g.gl_pathc) ? (size_t)want_index
                                                        : g.gl_pathc - 1;
            std::string result = g.gl_pathv[idx];
            globfree(&g);
            return result;
        }
        globfree(&g);
    }
    return "";
}

// ---------------------------------------------------------------------------
// D2XX API implementation
// ---------------------------------------------------------------------------

// Dead-code stubs (used only inside the if(0) debug block)
static FT_STATUS FT_CreateDeviceInfoList(DWORD* numDevs)
{
    *numDevs = 0;
    return FT_OK;
}
static FT_STATUS FT_GetDeviceInfoList(FT_DEVICE_LIST_INFO_NODE*, DWORD*)
{
    return FT_OK;
}

// FT_OpenEx – uses the description's trailing letter (e.g. "… Device B") to
// select the right channel on multi-interface FTDI adapters, then opens the
// discovered port.
static FT_STATUS FT_OpenEx(PVOID desc, int /*flags*/, FT_HANDLE* handle)
{
    // Extract channel hint from the last non-space word of the description.
    // E.g. "Digilent Adept USB Device B" → hint = 'B'
    char channel_hint = '\0';
    if (desc) {
        const char* s = (const char*)desc;
        size_t len = strlen(s);
        // Walk backwards past trailing spaces, then grab the last token.
        size_t end = len;
        while (end > 0 && s[end - 1] == ' ') --end;
        if (end > 0) {
            size_t start = end - 1;
            while (start > 0 && s[start - 1] != ' ') --start;
            // Single uppercase letter → treat as channel name.
            if (end - start == 1 && s[start] >= 'A' && s[start] <= 'Z')
                channel_hint = s[start];
        }
    }

    std::string port = _ftposix_find_port(channel_hint);
    if (port.empty()) {
        fprintf(stderr,
            "[serial_posix] No serial port found.\n"
            "  Connect the device or set: export XLOAD_PORT=/dev/tty.usbserial-XXXX\n");
        return FT_DEVICE_NOT_FOUND;
    }

    fprintf(stderr, "[serial_posix] Opening port: %s\n", port.c_str());
    int fd = open(port.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("[serial_posix] open");
        return FT_DEVICE_NOT_FOUND;
    }

    *handle = fd;
    return FT_OK;
}

// FT_SetBaudRate – stores the rate; it will be applied by the next
// _ftposix_apply_tty() call (i.e. from FT_SetDataCharacteristics).
static FT_STATUS FT_SetBaudRate(FT_HANDLE handle, DWORD baud_rate)
{
    _ftposix_baud = baud_rate;
    (void)handle;
    return FT_OK;
}

// FT_SetDataCharacteristics – configures the port for raw 8N1.
// We initialise _ftposix_tty from scratch (no tcgetattr) to avoid
// the EINVAL that macOS raises when tcsetattr is called after IOSSIOSPEED
// has set a non-standard baud rate.
static FT_STATUS FT_SetDataCharacteristics(FT_HANDLE handle,
                                            int /*bits*/,
                                            int /*stop_bits*/,
                                            int /*parity*/)
{
    memset(&_ftposix_tty, 0, sizeof(_ftposix_tty));
    cfmakeraw(&_ftposix_tty);
    _ftposix_tty.c_cflag |=  CS8 | CREAD | CLOCAL;
    _ftposix_tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    _ftposix_tty.c_cc[VMIN]  = 1;
    _ftposix_tty.c_cc[VTIME] = 0;
    _ftposix_tty_init = true;
    return _ftposix_apply_tty(handle);
}

// FT_SetTimeouts – maps D2XX timeout semantics onto termios VMIN/VTIME.
//   UINT_MAX (0xFFFFFFFF) → block indefinitely (VMIN=1, VTIME=0)
//   0                     → non-blocking          (VMIN=0, VTIME=0)
//   N ms                  → wait up to N ms        (VMIN=0, VTIME in 100 ms units)
// Updates the cached _ftposix_tty directly to avoid a tcgetattr() call
// (which would return an invalid baud field after IOSSIOSPEED).
static FT_STATUS FT_SetTimeouts(FT_HANDLE handle,
                                 DWORD read_timeout_ms,
                                 DWORD /*write_timeout_ms*/)
{
    if (!_ftposix_tty_init) {
        // Port not yet configured – nothing to do.
        return FT_OK;
    }

    if (read_timeout_ms == 0xFFFFFFFFUL) {
        _ftposix_tty.c_cc[VMIN]  = 1;
        _ftposix_tty.c_cc[VTIME] = 0;
    } else if (read_timeout_ms == 0) {
        _ftposix_tty.c_cc[VMIN]  = 0;
        _ftposix_tty.c_cc[VTIME] = 0;
    } else {
        _ftposix_tty.c_cc[VMIN]  = 0;
        // VTIME is in units of 100 ms; round up, minimum 1
        cc_t t = (cc_t)((read_timeout_ms + 99) / 100);
        _ftposix_tty.c_cc[VTIME] = (t == 0) ? 1 : t;
    }
    return _ftposix_apply_tty(handle);
}

// FT_SetLatencyTimer – no equivalent in POSIX; silently ignored.
static FT_STATUS FT_SetLatencyTimer(FT_HANDLE /*handle*/, DWORD /*latency*/)
{
    return FT_OK;
}

// FT_SetUSBParameters – no equivalent in POSIX; silently ignored.
static FT_STATUS FT_SetUSBParameters(FT_HANDLE /*handle*/,
                                      DWORD /*in_size*/,
                                      DWORD /*out_size*/)
{
    return FT_OK;
}

// FT_Write – wraps write(); a single call should be sufficient for the
// small payloads XLoad uses, but we loop to be safe.
static FT_STATUS FT_Write(FT_HANDLE handle, void* buf, DWORD count, DWORD* written)
{
    ssize_t total = 0;
    const uint8_t* ptr = (const uint8_t*)buf;
    while (total < (ssize_t)count) {
        ssize_t n = write(handle, ptr + total, (size_t)(count - total));
        if (n < 0) {
            perror("[serial_posix] write");
            *written = (DWORD)total;
            return FT_IO_ERROR;
        }
        total += n;
    }
    *written = (DWORD)total;
    return FT_OK;
}

// FT_Read – loops until the requested number of bytes has arrived or a
// timeout/error occurs.
static FT_STATUS FT_Read(FT_HANDLE handle, void* buf, DWORD count, DWORD* bytes_read)
{
    ssize_t total = 0;
    uint8_t* ptr = (uint8_t*)buf;
    while (total < (ssize_t)count) {
        ssize_t n = read(handle, ptr + total, (size_t)(count - total));
        if (n < 0) {
            perror("[serial_posix] read");
            *bytes_read = (DWORD)total;
            return FT_IO_ERROR;
        }
        if (n == 0)   // timeout (VTIME elapsed with VMIN=0)
            break;
        total += n;
    }
    *bytes_read = (DWORD)total;
    return FT_OK;
}

// FT_Close
static FT_STATUS FT_Close(FT_HANDLE handle)
{
    close(handle);
    return FT_OK;
}
