

#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

#ifdef __AVR_ATmega328P__
    #include <LibPrintf.h>
    #define DEBUG_INIT()    Serial.begin(115200)
    #define DEBUG(s, ...)      printf(s, ##__VA_ARGS__)
#else
    #define DEBUG_INIT()
    #define DEBUG(s, ...)
#endif

#endif