/*
 * serial_export.c — records the lines a board prints on a serial port into a CSV file.
 * Usage, output format, build and common errors: see README.md.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Longest line we keep in one piece. A longer line is cut into several CSV rows. */
#define LINE_MAX_LEN 1024

/*
 * Set to 1 by Ctrl+C. It is written by the Ctrl+C handler, which Windows runs on
 * another thread, and read by the main loop: `volatile` stops the compiler from
 * caching it, and InterlockedExchange makes the write safe between threads.
 */
static volatile LONG g_stop = 0;

/*
 * Called by Windows when the user presses Ctrl+C. By default Ctrl+C kills the
 * program immediately; returning TRUE instead lets the main loop finish cleanly
 * and close the CSV file.
 */
static BOOL WINAPI on_ctrl_c(DWORD type) {
    (void)type;
    InterlockedExchange(&g_stop, 1);
    return TRUE;
}

/*
 * Opens a COM port and configures it like the board's side of the link.
 * Returns INVALID_HANDLE_VALUE on failure, with the Win32 error code in *err.
 */
static HANDLE open_serial(const char* port, DWORD baud, DWORD* err) {
    /* Windows only accepts "COM10" and above in the "\\.\COM10" form;
     * it also works for COM1-COM9, so we always use it. */
    char path[32];
    snprintf(path, sizeof(path), "\\\\.\\%s", port);

    /* A serial port is opened like a file. Share mode 0 = exclusive access,
     * which is why the Serial Monitor and this program can't run together. */
    HANDLE h = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        *err = GetLastError();
        return h;
    }

    /* The DCB holds the line settings. Both ends must agree, otherwise the
     * received bytes are garbage. 8 data bits, no parity, 1 stop bit ("8N1")
     * is the Arduino default. */
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        *err = GetLastError();
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    /* DTR is the signal "a program on the PC has opened the port". Boards with
     * native USB (Portenta, Pico) wait for it in while(!Serial) before sending
     * anything, so without it we would receive nothing. */
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)) {
        *err = GetLastError();
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    /* Without timeouts, ReadFile would wait forever for data, and Ctrl+C would
     * not be noticed while the board is silent. With these values, ReadFile
     * returns after ~100 ms even if nothing arrived. */
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &timeouts);

    return h;
}

/* PC local time, e.g. "2026-09-29 19:14:19.853" (millisecond precision). */
static void format_timestamp(char* out, size_t size) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

/*
 * Writes one CSV row: timestamp,"line"
 * CSV rule: a quote inside a quoted field is written twice (" becomes "").
 * fflush() forces the row onto the disk right away instead of waiting in a
 * memory buffer, so a crash loses nothing that was already received.
 */
static void write_csv_line(FILE* f, const char* line) {
    char ts[32];
    format_timestamp(ts, sizeof(ts));
    fprintf(f, "%s,\"", ts);
    for (const char* p = line; *p; p++) {
        if (*p == '"') {
            fputc('"', f);
        }
        fputc(*p, f);
    }
    fputs("\"\n", f);
    fflush(f);
}

int main(int argc, char** argv) {
    /* --- 1. Read the command-line arguments --- */
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port> <baud> <output.csv> [seconds]\n", argv[0]);
        return 1;
    }
    const char* port = argv[1];
    DWORD baud = (DWORD)strtoul(argv[2], NULL, 10);
    const char* out_path = argv[3];
    DWORD duration_ms = (argc >= 5) ? (DWORD)strtoul(argv[4], NULL, 10) * 1000 : 0; /* 0 = no limit */

    if (baud == 0) {
        fprintf(stderr, "Invalid baud rate: %s\n", argv[2]);
        return 1;
    }

    /* --- 2. Open the serial port and the output file --- */
    DWORD err = 0;
    HANDLE serial = open_serial(port, baud, &err);
    if (serial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s (Win32 error %lu).\n", port, err);
        if (err == ERROR_ACCESS_DENIED) {
            fprintf(stderr, "The port is already open elsewhere: close the Arduino Serial Monitor.\n");
        } else if (err == ERROR_FILE_NOT_FOUND) {
            fprintf(stderr, "No such port: check the COM number in Device Manager.\n");
        }
        return 1;
    }

    FILE* out = fopen(out_path, "w");
    if (!out) {
        perror(out_path);
        CloseHandle(serial);
        return 1;
    }
    fputs("timestamp,line\n", out);

    SetConsoleCtrlHandler(on_ctrl_c, TRUE);
    printf("Reading %s at %lu baud -> %s  (Ctrl+C to stop)\n", port, baud, out_path);

    /* --- 3. Read loop ---
     * The serial port does not deliver lines, it delivers bytes, in chunks of
     * any size: one ReadFile can return half a line, or the end of one line and
     * the start of the next. So we copy bytes into `line` one by one, and only
     * when we see '\n' (end of line) do we write the complete line to the CSV. */
    char line[LINE_MAX_LEN];
    size_t len = 0;           /* number of characters currently in `line` */
    unsigned long count = 0;  /* lines written so far, shown at the end */
    DWORD start = GetTickCount();

    while (!g_stop) {
        if (duration_ms && GetTickCount() - start >= duration_ms) {
            break;
        }

        char buf[256];
        DWORD n = 0;  /* bytes actually received; can be 0 after a timeout */
        if (!ReadFile(serial, buf, sizeof(buf), &n, NULL)) {
            fprintf(stderr, "Read error %lu (board unplugged?)\n", GetLastError());
            break;
        }

        for (DWORD i = 0; i < n; i++) {
            char c = buf[i];
            /* Boards often end lines with "\r\n": drop the \r, keep \n as the separator. */
            if (c == '\r') {
                continue;
            }
            /* End of line, or line too long for the buffer: write what we have. */
            if (c == '\n' || len == LINE_MAX_LEN - 1) {
                line[len] = '\0';
                if (len > 0) {  /* skip empty lines */
                    write_csv_line(out, line);
                    puts(line); /* echo to the console, like the Serial Monitor */
                    count++;
                }
                len = 0;
                if (c == '\n') {
                    continue;
                }
                /* Line was too long: c is the first character of the next piece. */
            }
            line[len++] = c;
        }
    }

    /* --- 4. Clean up --- */
    fclose(out);
    CloseHandle(serial);
    printf("Stopped. %lu lines written to %s\n", count, out_path);
    return 0;
}
