#ifndef GATEWAY_H
#define GATEWAY_H

/*
 * ESP32-side UART stream parser and command-to-CAN forwarding logic.
 *
 * This component does not create RTOS tasks or configure UART/TWAI hardware.
 * The application/maintainer owns peripheral setup, UART reads, CAN TX queues,
 * and scheduling.
 */

#include <stddef.h>
#include <stdint.h>
#include "gp_protocol.h"

/*
 * The callback must be nonblocking. It must copy the entire frame into a
 * bounded, maintainer-owned TX queue before returning GP_OK.
 *
 * GP_PENDING means busy and nothing was accepted.
 * GP_IO_ERROR means a permanent/driver error.
 */
#endif