#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdarg.h>
#include <stdint.h>

// Print to Serial1 using a format string stored in program memory.
// Usage: serial1_printf_P(PSTR("Value=%d\n"), val);
void serial1_printf_P(const char *fmt_p, ...);

#define SERIAL1_PRINTF_P(fmt, ...) serial1_printf_P(PSTR(fmt), ##__VA_ARGS__)

#endif // CONSOLE_H
