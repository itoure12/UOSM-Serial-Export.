# serial_export

Records everything a board prints on a serial port into a CSV file.

A board (Arduino, Portenta, the UOSM CAN debugger...) prints text lines over USB, which
Windows exposes as a serial port (COM3, COM6...). The Arduino Serial Monitor only displays
those lines; `serial_export` also saves them, one CSV row per line, with the PC time at
which each line arrived. The CSV opens directly in Excel.


## Testing without the CAN debugger

You don't need the car or the CAN debugger to try `serial_export`. Any Arduino can play
the role of the CAN debugger: to `serial_export`, both are just a board printing text
lines on a serial port.

`arduino_test/SerialRandomDemo/` is an Arduino sketch that prints a random value every
500 ms at 115200 baud:

```
random value = 31
random value = 61
```

To use it:

1. Open `arduino_test/SerialRandomDemo/SerialRandomDemo.ino` in the Arduino IDE.
2. Select the board (Tools > Board, e.g. Arduino Portenta H7 (M7 core)) and its port
   (Tools > Port).
3. Upload the sketch.
4. Close the Serial Monitor if it is open (only one program can use the port).
5. Run `serial_export` on that port, e.g. `serial_export COM6 115200 test.csv 10`.

Note: the sketch simulates the CAN debugger as a **data source**, not its content. It
prints random values, not CAN frames. To test something that parses CAN frames, change
the `Serial.print` lines in the sketch so they print lines in the CAN debugger's format.

If the upload fails on a Portenta, double-press its reset button: the green LED pulses,
the board is in bootloader mode, and the upload goes through.

## How it works

1. Open the COM port with the same settings as the board (baud rate, 8N1).
2. Read the bytes as they arrive and reassemble them into full lines.
3. Write each full line to the CSV, prefixed with a timestamp, and echo it to the console.

## Usage

```
serial_export <port> <baud> <output.csv> [seconds]
```

| Argument | Meaning |
|---|---|
| `port` | COM port of the board, e.g. `COM6`. Find it in Device Manager (under "Ports (COM & LPT)"), or in the Tools > Port menu of the Arduino IDE. |
| `baud` | Must match `Serial.begin(...)` on the board, e.g. `115200`. |
| `output.csv` | File to create. An existing file with that name is **overwritten**. |
| `seconds` | Optional: stop automatically after this many seconds. Without it, the program runs until Ctrl+C. |

Examples:

```
serial_export COM6 115200 capture.csv       (stop with Ctrl+C)
serial_export COM6 115200 capture.csv 10    (stop after 10 s)
```

## Output format

```
timestamp,line
2026-09-29 19:14:19.853,"random value = 95"
2026-09-29 19:14:20.367,"random value = 90"
```

- The line is in quotes because it can contain commas (e.g. a CAN frame), which would
  otherwise split it into several Excel columns.
- The file is saved after every line, so nothing received is lost if the program crashes
  or the board is unplugged.

## Build

Any gcc for Windows works, e.g. the one bundled with CLion:

```
"C:\Program Files\JetBrains\CLion 2026.2.3\bin\mingw\bin\gcc.exe" serial_export.c -Wall -Wextra -o serial_export.exe
```

Do not commit `serial_export.exe`: rebuild it from `serial_export.c`.

## Common errors

| Error | Cause |
|---|---|
| Win32 error 5 (access denied) | Another program has the port open, usually the Arduino Serial Monitor. Only one program can use a serial port at a time. |
| Win32 error 2 (not found) | Wrong COM number, or the board is not plugged in. |



## Limitations

Windows only: it uses the Win32 serial API (`CreateFile`, `DCB`, `ReadFile`).
