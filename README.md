# XLoad for macOS

A macOS port of [XLoad](https://github.com/mlinton/XLoad), a command-line tool for managing the flash memory and programs on XFM/XFM2/XVA1 synthesizers.

The original Windows version uses the FTDI D2XX library. This port replaces that with a POSIX serial driver (`serial_posix.h`) that talks directly to the synthesizer's USB-serial port via macOS's built-in VCP driver — no extra libraries or drivers needed.

## Build

```bash
make
```

Requires only Xcode Command Line Tools (`xcode-select --install`).

## Usage

Run without arguments to open the interactive terminal:

```bash
./xload
```

Or pass a single command directly:

```bash
./xload "t XVA1_Tunings.bin"
./xload "get_bank ~/backup.bank"
./xload "* 5"
```

### Terminal commands

| Command | Description |
|---|---|
| `i` | Initialise program from default values |
| `i <file>` | Load program from file |
| `d` | Show all parameter values |
| `d <file>` | Save current program to file |
| `g <N>` | Get parameter N |
| `s <N> <V>` | Set parameter N to value V |
| `r <N>` | Read program N from EEPROM |
| `w <N>` | Write current program to EEPROM slot N |
| `n` | Show current program name |
| `n <name>` | Set current program name |
| `*` | Show current MIDI channel |
| `* <N>` | Set MIDI channel (0 = omni, 1–16) |
| `t <file>` | Write tunings file to device flash |
| `wave <file>` | Write wavetable file to device flash |
| `get_bank <file>` | Read full program bank from device |
| `put_bank <file>` | Write full program bank to device |
| `init_bank` | Initialise all EEPROM programs ⚠️ destructive |
| `. <file>` | Record audio to WAV file (experimental) |
| `h` | Help |
| `q` | Quit |

## Serial port

The tool auto-detects the first available `cu.usbserial-*` or `cu.usbmodem*` device. To override:

```bash
export XLOAD_PORT=/dev/cu.usbserial-XXXX
./xload
```
