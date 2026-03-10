# XLoad for macOS

This repository includes a port of the XLoad tool to macOS. The codebase has been modified to compile using `clang++` while maintaining compatibility.

## Changes Made

- **File Encodings:** The original `XLoad.cpp` and `ftd2xx.h` files were converted from UTF-16LE/Latin-1 to standard UTF-8.
- **POSIX API Alternatives:** The Windows-specific terminal APIs (`<windows.h>`, `Sleep`, `GetAsyncKeyState`, and `SetConsoleTextAttribute`) were replaced with POSIX-compliant equivalents using `<unistd.h>`, `<termios.h>`, and `<fcntl.h>`.
- **FTDI macOS Compatibility:** A missing `WinTypes.h` shim file was added to the `FTDI/` directory to define the required Windows types (`DWORD`, `HANDLE`, `BOOL`, etc.) on UNIX-like platforms. Path separators in `#include` directives were converted to forward slashes.
- **Compiler Warnings:** Addressed compiler warnings arising from mismatched types on macOS (such as `ULONG` format specifiers for `printf` and `std::min` parameter types).
- **Makefile:** A `Makefile` was included to simplify compilation via the command line.

## How to Compile and Run on macOS

### Prerequisites

You need the official FTDI D2XX drivers for macOS:

1. Download the latest **D2XX Drivers for macOS** from the [FTDI official website](https://ftdichip.com/drivers/d2xx-drivers/).
2. Mount the downloaded `.dmg` image.
3. Extract the `libftd2xx.X.X.X.dylib` file from the image (e.g., `release/build/libftd2xx.1.4.30.dylib`) and place it in the `XLoad/FTDI/` directory, renaming it to `libftd2xx.dylib`.

*(Alternatively, you can install the library system-wide in `/usr/local/lib/` and update the `LDFLAGS` in the `Makefile` accordingly.)*

### Compilation

Open a terminal, navigate to the `XLoad` source folder, and run `make`:

```bash
cd XLoad
make
```

### Running the Tool

After a successful build, you can run the executable directly from the source directory:

```bash
./xload
```

To view the available commands:
```bash
./xload -h
```

### Troubleshooting

- **Library not loaded:** If you receive a `dyld: Library not loaded` error when trying to run `./xload`, ensure that `libftd2xx.dylib` is properly placed in the `XLoad/FTDI/` directory. The binary is configured to look for the library relative to its execution path using an `rpath`.
- **Permissions:** If your device is claimed by Apple's built-in serial port driver (preventing `xload` from accessing it), you may need to install the `D2xxHelper` tool, also available on the FTDI driver download page.
