#include "../include/mpcm.h"
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>

// Activity indicator
volatile uint8_t mpcm_bus_active = 0;

// Stored local address in RAM (also persisted by higher layers)
static uint8_t local_address = 0;

// TX state machine (store as smallest type)
enum tx_state_t { TX_IDLE=0, TX_PREP, TX_SEND_ADDR, TX_WAIT_ADDR_DONE, TX_SEND_DATA, TX_WAIT_DATA_DONE, TX_WAIT_ACK, TX_BACKOFF, TX_DONE, TX_FAIL };
static uint8_t tx_state = TX_IDLE;
static uint8_t tx_target = 0;
static uint8_t tx_cmd = 0;
static unsigned long tx_backoff_until = 0; // in microseconds
static int tx_attempts = 0;
static uint8_t tx_request_ack = 0;
static uint8_t tx_blocking = 0;
static unsigned long tx_ack_deadline = 0; // in microseconds
static unsigned long tx_prep_start_us = 0;
static uint8_t tx_force_collision = 0;

// RX state machine (smallest type)
enum rx_state_t { RX_IDLE=0, RX_WAIT_DATA };
static uint8_t rx_state = RX_IDLE;
static uint8_t rx_pending_from = 0;

// (ACK command defined in include/mpcm.h as enum MPCM_CMD_ACK)

// Helper: set baud for USART0 (double speed)
static void mpcm_set_baud(void) {
    // Using U2X0 double-speed, UBRR = F_CPU/(8*BAUD)-1
    UCSR0A |= (1<<U2X0);
    uint16_t ubrr = (uint16_t)(F_CPU / (8UL * MPCM_BAUD) - 1UL);
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr & 0xFF);
}

void mpcm_inject_collision(uint8_t enable) {
    tx_force_collision = enable ? 1 : 0;
}

void mpcm_init(void) {
    mpcm_set_baud();

    // 9-bit character size: UCSZ02 in UCSR0B, UCSZ01:0 in UCSR0C
    UCSR0B |= (1<<UCSZ02);
    UCSR0C |= (1<<UCSZ01) | (1<<UCSZ00);

    // Enable receiver and transmitter
    UCSR0B |= (1<<RXEN0) | (1<<TXEN0);

    // Enable MPCM (start in address-only reception state)
    UCSR0A |= (1<<MPCM0);

    // Configure INT0 for falling edge detection (collision = high->low)
    EICRA &= ~((1<<ISC00) | (1<<ISC01));
    EICRA |= (1<<ISC01); // ISC01=1, ISC00=0 => falling edge
    // Ensure INT0 interrupt is disabled in EIMSK; we'll only poll EIFR
    EIMSK &= ~(1<<INT0);
    // Clear any pending flag
    EIFR = (1<<INTF0);
}

void mpcm_set_address(uint8_t addr) {
    local_address = addr;
}

uint8_t mpcm_get_address(void) {
    return local_address;
}

// send one 9-bit frame (sets TXB80 according to tx9thbit)
static inline void mpcm_hw_send_frame_now(uint8_t byte, uint8_t tx9thbit) {
    if (tx9thbit)
        UCSR0B |= (1<<TXB80);
    else
        UCSR0B &= ~(1<<TXB80);
    UDR0 = byte;
}

int mpcm_start_send(uint8_t target, uint8_t cmd, uint8_t request_ack, uint8_t blocking) {
    if (tx_state != TX_IDLE && tx_state != TX_DONE && tx_state != TX_FAIL) return -1; // busy

    // initialize tx state machine
    tx_target = target;
    tx_cmd = cmd;
    tx_attempts = 0;
    tx_request_ack = request_ack ? 1 : 0;
    tx_blocking = blocking ? 1 : 0;
    tx_state = TX_PREP;

    if (tx_blocking) {
        // busy-wait progression for blocking mode (no delays)
        mpcm_result_t res = MPCM_RESULT_IN_PROGRESS;
        unsigned long start = micros();
        while (1) {
            mpcm_result_t s = mpcm_tx_poll();
            // allow RX to progress so ACKs can be received
            uint8_t dummy_from, dummy_cmd;
            mpcm_rx_poll(&dummy_from, &dummy_cmd);
            if (s == MPCM_RESULT_SUCCESS) { res = MPCM_RESULT_SUCCESS; break; }
            if (s == MPCM_RESULT_FAILURE) { res = MPCM_RESULT_FAILURE; break; }
            // timeout
            if ((micros() - start) > (2000UL * 1000UL)) { res = MPCM_RESULT_FAILURE; break; }
        }
        return (res == MPCM_RESULT_SUCCESS) ? 0 : -1;
    }

    return 0;
}

// Polling function to drive TX state machine, non-blocking
mpcm_result_t mpcm_tx_poll(void) {
    switch (tx_state) {
        case TX_IDLE: {
            return MPCM_RESULT_IDLE;
        }
        case TX_PREP: {
            // Wait until RX line (PD0) has been idle for one frame time before driving bus.
            if (tx_prep_start_us == 0) {
                tx_prep_start_us = micros();
            }
            // Check RX pin (PD0) - high means idle
            if (PIND & (1<<PD0)) {
                if ((micros() - tx_prep_start_us) >= MPCM_FRAME_TIME_US) {
                    // bus idle for at least one frame time -> start transmit
                    EIFR = (1<<INTF0); // clear any pending collision flag
                    UCSR0B &= ~(1<<RXEN0); // disable reception while transmitting
                    mpcm_bus_active = 1;
                    tx_attempts++;
                    tx_prep_start_us = 0;
                    tx_state = TX_SEND_ADDR;
                }
            } else {
                // bus active, restart countdown
                tx_prep_start_us = micros();
            }
            return MPCM_RESULT_IN_PROGRESS;
        }
        case TX_SEND_ADDR: {
            // send address with 9th bit = 1
            if (UCSR0A & (1<<UDRE0)) {
                mpcm_hw_send_frame_now(tx_target, 1);
                // move to wait for byte to be transmitted
                tx_state = TX_WAIT_ADDR_DONE;
            }
            return MPCM_RESULT_IN_PROGRESS;
        }
        case TX_WAIT_ADDR_DONE: {
            // detect collision via INT0 flag (EIFR) or wait for UDRE/ TXC
            if ((EIFR & (1<<INTF0)) || tx_force_collision) {
                // collision
                if (EIFR & (1<<INTF0)) EIFR = (1<<INTF0); // clear
                tx_force_collision = 0;
                tx_state = TX_BACKOFF;
                // enable receiver
                UCSR0B |= (1<<RXEN0);
                mpcm_bus_active = 0;
                tx_backoff_until = micros() + ((unsigned long)(rand() & 0xFF) * MPCM_FRAME_TIME_US);
                return MPCM_RESULT_IN_PROGRESS;
            }
            if (UCSR0A & (1<<TXC0)) {
                // address byte sent
                UCSR0A |= (1<<TXC0); // clear
                tx_state = TX_SEND_DATA;
            }
            return MPCM_RESULT_IN_PROGRESS;
        }
        case TX_SEND_DATA: {
            if (UCSR0A & (1<<UDRE0)) {
                mpcm_hw_send_frame_now(tx_cmd, tx_request_ack ? 1 : 0);
                tx_state = TX_WAIT_DATA_DONE;
            }
            return MPCM_RESULT_IN_PROGRESS;
        }
        case TX_WAIT_DATA_DONE: {
            if ((EIFR & (1<<INTF0)) || tx_force_collision) {
                // collision
                if (EIFR & (1<<INTF0)) EIFR = (1<<INTF0);
                tx_force_collision = 0;
                tx_state = TX_BACKOFF;
                UCSR0B |= (1<<RXEN0);
                mpcm_bus_active = 0;
                tx_backoff_until = micros() + ((unsigned long)(rand() & 0xFF) * MPCM_FRAME_TIME_US);
                return MPCM_RESULT_IN_PROGRESS;
            }
            if (UCSR0A & (1<<TXC0)) {
                // done
                UCSR0A |= (1<<TXC0);
                UCSR0B |= (1<<RXEN0);
                mpcm_bus_active = 0;
                if (tx_request_ack) {
                    tx_state = TX_WAIT_ACK;
                    tx_ack_deadline = micros() + MPCM_ACK_TIMEOUT_US; // ACK timeout
                    return MPCM_RESULT_IN_PROGRESS;
                } else {
                    tx_state = TX_DONE;
                    return MPCM_RESULT_SUCCESS;
                }
            }
            return MPCM_RESULT_IN_PROGRESS;
        }
        case TX_WAIT_ACK: {
            // While waiting for ACK, let higher-level rx_poll detect ACK and set TX_DONE,
            // but also we can timeout here.
            if ((long)(micros() - tx_ack_deadline) >= 0) {
                // timeout
                tx_state = TX_FAIL;
                return MPCM_RESULT_FAILURE;
            }
            return MPCM_RESULT_IN_PROGRESS;
        }
        case TX_BACKOFF: {
            if (micros() >= tx_backoff_until) {
                if (tx_attempts > MPCM_MAX_TX_ATTEMPTS) {
                    tx_state = TX_FAIL;
                    UCSR0B |= (1<<RXEN0);
                    mpcm_bus_active = 0;
                    return MPCM_RESULT_FAILURE;
                }
                // try again
                tx_state = TX_PREP;
            }
            return MPCM_RESULT_IN_PROGRESS;
        }
        case TX_DONE: {
            tx_state = TX_IDLE;
            return MPCM_RESULT_IDLE;
        }
        case TX_FAIL: {
            tx_state = TX_IDLE;
            return MPCM_RESULT_FAILURE;
        }
    }
    return MPCM_RESULT_IDLE;
}

int mpcm_rx_poll(uint8_t *from, uint8_t *cmd) {
    // Non-blocking polling receive
    if (rx_state == RX_IDLE) {
        if (UCSR0A & (1<<RXC0)) {
            uint8_t rxb8 = (UCSR0B & (1<<RXB80)) ? 1 : 0;
            uint8_t data = UDR0;
            if (rxb8) {
                uint8_t addr = data;
                if (addr == 0xFF || addr == local_address || local_address == 0) {
                    // match -> wait for data frame
                    rx_pending_from = addr;
                    // temporarily clear MPCM to receive data
                    UCSR0A &= ~(1<<MPCM0);
                    rx_state = RX_WAIT_DATA;
                }
            }
        }
        return 0;
    } else if (rx_state == RX_WAIT_DATA) {
        if (UCSR0A & (1<<RXC0)) {
            uint8_t rxb8 = (UCSR0B & (1<<RXB80)) ? 1 : 0;
            uint8_t data = UDR0;
            // restore MPCM
            UCSR0A |= (1<<MPCM0);
            rx_state = RX_IDLE;
            if (rxb8) {
                // Received a data frame with 9th bit set — this indicates the sender requested ACK.
                // We will respond with an ACK frame to the sender.
                // Start a non-blocking send of the ACK command back to rx_pending_from.
                // (Ignore if we're currently transmitting.)
                if (tx_state == TX_IDLE || tx_state == TX_DONE || tx_state == TX_FAIL) {
                    // send ACK without requesting an ACK back
                    mpcm_start_send(rx_pending_from, MPCM_CMD_ACK, 0, 0);
                }
                // Do not report this as application data
                return 0;
            } else {
                // regular data frame
                *from = rx_pending_from;
                *cmd = data;
                // If this is an ACK for an outstanding transmit, consume it here
                if (tx_state == TX_WAIT_ACK && *from == tx_target && *cmd == MPCM_CMD_ACK) {
                    tx_state = TX_DONE;
                    return 0; // swallow ACK from application
                }
                return 1;
            }
        }
        return 0;
    }
    return 0;
}
