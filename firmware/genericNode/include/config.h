#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// EEPROM storage addresses
#define EEPROM_ADDR_NODE_ADDRESS 0
#define EEPROM_ADDR_NODE_TYPE    1
#define EEPROM_ADDR_TARGET_ADDR  2

void config_init(void);
uint8_t config_get_address(void);
void config_set_address(uint8_t a);
uint8_t config_get_type(void);
void config_set_type(uint8_t t);
uint8_t config_get_target(void);
void config_set_target(uint8_t t);

#endif // CONFIG_H
