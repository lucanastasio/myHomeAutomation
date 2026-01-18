#include "../include/config.h"
#include <Arduino.h>
#include <EEPROM.h>

void config_init(void) {
    // If EEPROM is uninitialized (0xFF), set defaults
    if (EEPROM.read(EEPROM_ADDR_NODE_ADDRESS) == 0xFF) EEPROM.write(EEPROM_ADDR_NODE_ADDRESS, 0x01);
    if (EEPROM.read(EEPROM_ADDR_NODE_TYPE) == 0xFF) EEPROM.write(EEPROM_ADDR_NODE_TYPE, 0x01);
    if (EEPROM.read(EEPROM_ADDR_TARGET_ADDR) == 0xFF) EEPROM.write(EEPROM_ADDR_TARGET_ADDR, 0xFF);
}

uint8_t config_get_address(void) {
    return EEPROM.read(EEPROM_ADDR_NODE_ADDRESS);
}

void config_set_address(uint8_t a) {
    EEPROM.write(EEPROM_ADDR_NODE_ADDRESS, a);
}

uint8_t config_get_type(void) {
    return EEPROM.read(EEPROM_ADDR_NODE_TYPE);
}

void config_set_type(uint8_t t) {
    EEPROM.write(EEPROM_ADDR_NODE_TYPE, t);
}

uint8_t config_get_target(void) {
    return EEPROM.read(EEPROM_ADDR_TARGET_ADDR);
}

void config_set_target(uint8_t t) {
    EEPROM.write(EEPROM_ADDR_TARGET_ADDR, t);
}
