#ifndef MPCM_H
#define MPCM_H

#include <stdint.h>

// Baudrate for MPCM on USART0
#define MPCM_BAUD 1250000UL

// Serial frame parameters
#define MPCM_BITS_PER_FRAME 10UL
// Duration of one frame in microseconds (approx)
#define MPCM_FRAME_TIME_US ((MPCM_BITS_PER_FRAME * 1000000UL) / MPCM_BAUD)

// Retries and timeouts
#define MPCM_MAX_TX_ATTEMPTS 5
// ACK timeout in microseconds (default 200 ms)
#define MPCM_ACK_TIMEOUT_US (200UL * 1000UL)

// Commands
typedef enum {
	MPCM_CMD_TOGGLE = 0x01,
	MPCM_CMD_ACK = 0xA5,
	MPCM_CMD_RESERVED_BROADCAST = 0xFF
} mpcm_cmd_t;

// Initialize USART0 in 9-bit + MPCM mode (receiver enabled)
void mpcm_init(void);

// Set/get local address used for MPCM
void mpcm_set_address(uint8_t addr);
uint8_t mpcm_get_address(void);

// Return codes for poll functions
typedef enum {
	MPCM_RESULT_IDLE = 0,
	MPCM_RESULT_IN_PROGRESS = 1,
	MPCM_RESULT_SUCCESS = 2,
	MPCM_RESULT_FAILURE = -1
} mpcm_result_t;

// Non-blocking transmit API (state machine)
// Start a transmit of (target, cmd).
// If request_ack!=0 the ninth bit in the command frame will be set to request an ACK.
// If blocking!=0 the function will block until the transfer completes (or fails).
// Returns 0 if started (or completed if blocking), -1 if busy or failed to start.
int mpcm_start_send(uint8_t target, uint8_t cmd, uint8_t request_ack, uint8_t blocking);
// Call frequently to progress transmission. Returns mpcm_result_t.
mpcm_result_t mpcm_tx_poll(void);

// Non-blocking receive API (state machine)
// Call frequently; when a frame is available returns 1 and fills from/cmd, else 0.
int mpcm_rx_poll(uint8_t *from, uint8_t *cmd);

// Inject or clear artificial collision (gateway use only)
void mpcm_inject_collision(uint8_t enable);

// Flag set while driver is driving the bus (activity indicator)
extern volatile uint8_t mpcm_bus_active;

#endif // MPCM_H
