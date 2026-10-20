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
#include "gp-protocol.h"

typedef struct {
    uint8_t frame[GP_UART_FRAME_LEN];
    size_t used;
} UartRx;

/*
 * The callback must be nonblocking. It must copy the entire frame into a
 * bounded, maintainer-owned TX queue before returning GP_OK.
 *
 * GP_PENDING means busy and nothing was accepted.
 * GP_IO_ERROR means a permanent/driver error.
 */
typedef GpResult (*CanWriteFn)(void *user, const GpCanFrame *frame);

typedef struct {
    CanWriteFn write;
    void *user;
    uint32_t max_queue_ms;
    uint16_t last_sequence;
    uint8_t have_sequence;
} Gateway;

void UartRx_Init(UartRx *rx);

GpResult UartRx_Feed(UartRx *rx,
                     const uint8_t *bytes,
                     size_t length,
                     size_t *consumed,
                     TargetSample *out);

GpResult Gateway_Init(Gateway *gateway,
                      CanWriteFn write,
                      void *user,
                      uint32_t max_queue_ms);

GpResult Gateway_Forward(Gateway *gateway,
                         const TargetSample *command,
                         uint32_t queued_ms);

void Gateway_Reset(Gateway *gateway);

#endif