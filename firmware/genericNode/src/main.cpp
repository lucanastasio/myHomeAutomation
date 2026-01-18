#include <Arduino.h>
#include "mpcm.h"
#include "config.h"
#include "console.h"
#include <stdlib.h>

// Pins
#define ACTIVITY_LED_PIN LED_BUILTIN // D13
#define USER_LED_PIN A0                // avoid D12 (PB4 / RXD1) and D11 (TXD1)
#define BUTTON_PIN A3                  // external button to GND (with internal pull-up)

// Commands
#define CMD_TOGGLE 0x01

// Button debounce state
static uint8_t last_button_raw = HIGH;
static uint8_t debounced_button_state = HIGH;
static unsigned long last_button_debounce = 0;
static const unsigned long BUTTON_DEBOUNCE_MS = 20;

static unsigned long last_activity_millis = 0;

void setup() {
  // Serial1 for configuration and gateway console
  Serial1.begin(115200);

  // config and MPCM init
  config_init();
  mpcm_init();
  mpcm_set_address(config_get_address());

  // I/O
  pinMode(ACTIVITY_LED_PIN, OUTPUT);
  digitalWrite(ACTIVITY_LED_PIN, LOW);
  pinMode(USER_LED_PIN, OUTPUT);
  digitalWrite(USER_LED_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // seed rand() for backoff
  srand(millis());

  serial1_printf_P(PSTR("Node starting, addr=%u type=%u target=%u\n"), config_get_address(), config_get_type(), config_get_target());
}

// Simple command parser for Serial1 (Arduino APIs allowed here)
static void handle_serial1_commands(void) {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line.trim();
      if (line.length()) {
        if (line.startsWith("ADDR ")) {
          int v = line.substring(5).toInt();
          config_set_address((uint8_t)v);
          mpcm_set_address((uint8_t)v);
          serial1_printf_P(PSTR("Address set to %d\n"), v);
        } else if (line.startsWith("TYPE ")) {
          int v = line.substring(5).toInt();
          config_set_type((uint8_t)v);
          serial1_printf_P(PSTR("Type set to %d\n"), v);
        } else if (line.startsWith("TARGET ")) {
          int v = line.substring(7).toInt();
          config_set_target((uint8_t)v);
          serial1_printf_P(PSTR("Target set to %d\n"), v);
        } else if (line.startsWith("SEND ")) {
            // SEND <target> <cmd> [ACK] [BLOCK]
          int sp = line.indexOf(' ', 5);
          if (sp > 0) {
            int t = line.substring(5, sp).toInt();
              String rest = line.substring(sp+1);
              int sp2 = rest.indexOf(' ');
              int cmd;
              String opts;
              if (sp2 > 0) {
                cmd = rest.substring(0, sp2).toInt();
                opts = rest.substring(sp2+1);
              } else {
                cmd = rest.toInt();
                opts = "";
              }
              uint8_t request_ack = (opts.indexOf("ACK") >= 0) ? 1 : 0;
              uint8_t blocking = (opts.indexOf("BLOCK") >= 0) ? 1 : 0;
              int r = mpcm_start_send((uint8_t)t, (uint8_t)cmd, request_ack, blocking);
              serial1_printf_P(PSTR("SEND to %d cmd %d ACK=%d BLOCK=%d => %s\n"), t, cmd, request_ack, blocking, (r==0)?"OK":"FAIL");
          }
        } else if (line == "SHOW") {
          serial1_printf_P(PSTR("addr=%u type=%u target=%u\n"), config_get_address(), config_get_type(), config_get_target());
        } else if (line == "HELP" || line == "?") {
          serial1_printf_P(PSTR("Commands:\n"));
          serial1_printf_P(PSTR("  ADDR <n>      - set this node address\n"));
          serial1_printf_P(PSTR("  TYPE <n>      - set node type (0=gateway)\n"));
          serial1_printf_P(PSTR("  TARGET <n>    - set default target address\n"));
          serial1_printf_P(PSTR("  SEND <t> <c> [ACK] [BLOCK] - send cmd to t\n"));
          serial1_printf_P(PSTR("  SHOW          - show current config\n"));
          serial1_printf_P(PSTR("  HELP/?        - this help\n"));
        } else {
          serial1_printf_P(PSTR("Unknown cmd: %s\n"), line.c_str());
        }
      }
      line = "";
    } else {
      line += c;
    }
  }
}

void loop() {
  // Handle console commands
  handle_serial1_commands();

  // Button pressed -> send toggle command to configured target (debounced)
  uint8_t raw = digitalRead(BUTTON_PIN);
  if (raw != last_button_raw) {
    last_button_debounce = millis();
    last_button_raw = raw;
  }
  if ((millis() - last_button_debounce) > BUTTON_DEBOUNCE_MS) {
    if (raw != debounced_button_state) {
      debounced_button_state = raw;
        if (debounced_button_state == LOW) {
        uint8_t target = config_get_target();
        serial1_printf_P(PSTR("Button: sending toggle to %u\n"), target);
        int started = mpcm_start_send(target, MPCM_CMD_TOGGLE, 1, 0);
        serial1_printf_P(PSTR("send started: %s\n"), (started==0)?"OK":"BUSY");
      }
    }
  }

  // Progress TX state machine and print results when finishing
  mpcm_result_t txs = mpcm_tx_poll();
  if (txs == MPCM_RESULT_SUCCESS) {
    serial1_printf_P(PSTR("TX completed OK\n"));
  } else if (txs == MPCM_RESULT_FAILURE) {
    serial1_printf_P(PSTR("TX failed\n"));
  }

  // Poll for incoming MPCM frames (non-blocking)
  uint8_t from, cmd;
  if (mpcm_rx_poll(&from, &cmd)) {
    // indicate activity
    digitalWrite(ACTIVITY_LED_PIN, HIGH);
    last_activity_millis = millis();

    if (cmd == MPCM_CMD_TOGGLE) {
      // toggle user LED
      digitalWrite(USER_LED_PIN, !digitalRead(USER_LED_PIN));
    }

    // If gateway type (type==0) or any node prints bus activity to Serial1
    if (config_get_type() == 0) {
      serial1_printf_P(PSTR("RX from %u: cmd=%u\n"), from, cmd);
    }
  }

  // Mirror driver activity flag to activity LED
  if (mpcm_bus_active) {
    digitalWrite(ACTIVITY_LED_PIN, HIGH);
    last_activity_millis = millis();
  }

  // Turn off activity LED shortly after activity
  if ((millis() - last_activity_millis) > 50) {
    digitalWrite(ACTIVITY_LED_PIN, LOW);
  }
}

