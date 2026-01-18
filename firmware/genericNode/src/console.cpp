#include "../include/console.h"
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <avr/pgmspace.h>

void serial1_printf_P(const char *fmt_p, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt_p);
    vsnprintf_P(buf, sizeof(buf), fmt_p, ap);
    va_end(ap);
    Serial1.print(buf);
}
