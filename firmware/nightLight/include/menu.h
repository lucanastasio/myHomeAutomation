/* Copyright (c) 2024 Luca Anastasio
 *
 * Single button, single LED menu
 *
 */

#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include <avr/eeprom.h>
#include "board.h"
#include "debug.h"
// #include <EEPROM.h>

// Menu Configuration

#define MENU_EXIT_TIMEOUT_MS 16000      // 16 seconds timeout
#define MENU_LONG_PRESS_MS 800          // 800ms long press
#define MENU_NAV_BLINK_INTERVAL_MS 400  // 1.25Hz for navigation
#define MENU_EDIT_BLINK_INTERVAL_MS 200 // 2.5Hz for editing
#define MENU_BLINK_PAUSE_MS 1000        // 1 second pause between blinks
#define MENU_BLINK_INTENSITY 31         // 25% duty cycle for blinks
#define MENU_EEPROM_ADDR 0              // settings offset address in EEPROM, just in case

// Struct for menu settings
typedef struct
{
    uint8_t mode, e_bright, n_bright, n_thresh, n_timeout;
} menu_settings_t;

menu_settings_t settings;
static uint8_t *settings_ptr = (uint8_t *)&settings;

#define MENU_OPTIONS_COUNT sizeof(menu_settings_t)

// Setting types (0 and 1 forbidden, 2 = boolean, 3 = three values, etc.)
static const uint8_t setting_types[MENU_OPTIONS_COUNT] = {3, 5, 5, 3, 3};

enum
{
    menu_state_off,
    menu_state_nav,
    menu_state_edit
};

static uint8_t menu_state = menu_state_off;

enum
{
    button_press_none,
    button_press_short,
    button_press_long
};

static inline void settings_init(void)
{
    for (uint8_t i = 0; i < sizeof(menu_settings_t); i++)
    {
        EEAR = i + MENU_EEPROM_ADDR;
        sbi(EECR, EERE);
        settings_ptr[i] = (EEDR == 0xFF) ? 0 : EEDR;
    }
}

static inline void settings_save(void)
{
    for (uint8_t i = 0; i < sizeof(menu_settings_t); i++)
    {
        while (bit_is_set(EECR, EEPE))
            /* wait */;
        EEAR = i + MENU_EEPROM_ADDR;
        EEDR = settings_ptr[i];
        sbi(EECR, EEMWE);
        sbi(EECR, EEPE);
    }
}

static inline void menu_update(millis_t millis_now)
{
    static uint8_t menu_index = 0;
    static millis_t led_blink_start = 0;
    static uint8_t blink_count = 0;

    uint8_t button_press = button_press_none;

    static millis_t last_press_time;
    static millis_t press_start = 0;
    static uint8_t button_held = 0;
    if (USRBTN_PRESSED())
    {
        last_press_time = millis_now;
        if (!press_start)
        {
            press_start = millis_now;
        }
        else if (millis_now - press_start > MENU_LONG_PRESS_MS && !button_held)
        {
            button_held = 1;
            button_press = button_press_long;
        }
    }
    else
    {
        if (press_start && !button_held)
        {
            button_press = button_press_short;
        }
        press_start = 0;
        button_held = 0;
    }

    switch (menu_state)
    {
    case menu_state_off:
        if (button_press == button_press_long)
        {
            menu_index = 0;
            menu_state = menu_state_nav;
            DEBUG("Entering menu\n");
        }
        break;
    case menu_state_nav:
        if (button_press == button_press_long)
        {
            menu_state = menu_state_edit;
            DEBUG("Entering edit mode\n");
        }
        else if (button_press == button_press_short)
        {
            menu_index = (menu_index >= MENU_OPTIONS_COUNT - 1) ? 0 : menu_index + 1;
            DEBUG("Index changed to %1u\n", menu_index);
        }
        break;
    case menu_state_edit:
        if (button_press == button_press_long)
        {
            menu_state = menu_state_nav;
            DEBUG("Returning to navigation\n");
        }
        else if (button_press == button_press_short)
        {
            if (settings_ptr[menu_index] >= setting_types[menu_index] - 1)
            {
                settings_ptr[menu_index] = 0;
            }
            else
            {
                settings_ptr[menu_index]++;
            }
            DEBUG("Setting at index %1u changed to value %1u\n", menu_index, settings_ptr[menu_index]);
        }
        break;
    default:
        break;
    }

    if (menu_state != menu_state_off)
    {
        // Timeout: exit menu
        if ((millis_now - last_press_time > MENU_EXIT_TIMEOUT_MS))
        {
            menu_state = menu_state_off;
            SET_LED(0);
            settings_save();
            DEBUG("Exiting menu\n");
        }

        // Handle LED blinking
        const millis_t blink_interval = (menu_state == menu_state_edit) ? MENU_EDIT_BLINK_INTERVAL_MS : MENU_NAV_BLINK_INTERVAL_MS;
        if (blink_count == 0 && millis_now - led_blink_start > MENU_BLINK_PAUSE_MS)
        {
            led_blink_start = millis_now;
            blink_count = (((menu_state == menu_state_nav) ? menu_index : settings_ptr[menu_index]) + 1) << 1;
        }
        else if (blink_count != 0 && millis_now - led_blink_start > blink_interval)
        {
            SET_LED((--blink_count & 0x01) ? MENU_BLINK_INTENSITY : 0);
            led_blink_start = millis_now;
        }
    }
}

#endif // MENU_H