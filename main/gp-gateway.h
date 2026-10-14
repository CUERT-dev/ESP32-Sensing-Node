#ifndef GATEWAY_H
#define GATEWAY_H

/*
 * gateway - ESP32 UART command-to-CAN forwarding (API revision 10).
 *
 * Depends on gp_protocol.h. Contains no UART or TWAI driver calls;
 * hardware interaction is handled through the maintainer-provided
 * nonblocking CanWriteFn callback.
 */

#include <stdint.h>
#include <stdbool.h>
#include "gp-protocol.h"


/* ---------- UART stream parser (ESP32 side) ----------
 * Maintains parser state across calls so a UART frame can be received
 * in multiple chunks. Treat the structure fields as private.
 */
typedef struct {
    uint8_t buf[GP_UART_FRAME_LEN];
    uint8_t count;
} UartRx;

/* Reset the parser and discard any partially received frame. */
void UartRx_Init(UartRx *rx);

/*
 * Feed the parser the next chunk of received UART bytes.
 *
 * Parameters:
 *   rx       Persistent parser state.
 *   bytes    Input byte buffer; may be NULL only when length is zero.
 *   length   Number of available input bytes.
 *   consumed Receives the number of bytes consumed by this call.
 *            The caller advances its buffer offset accordingly and
 *            processes any remaining bytes in subsequent calls.
 *   out      Receives the decoded command only when GP_OK is returned.
 *            Otherwise, its contents remain unchanged.
 *
 * Processing:
 *   Stops after one complete packet or one malformed packet.
 *   Handles frames split across chunks and multiple frames in a chunk.
 *
 * Returns:
 *   GP_OK       A complete, valid command was decoded.
 *   GP_PENDING  Input was consumed, but no complete command is available.
 *   GP_INVALID  A malformed packet was detected; resynchronization to
 *               the A5 5A start marker is performed.
 *   GP_BAD_ARG  A required argument is NULL. If consumed is usable,
 *               it is set to zero.
 *
 * For length == 0, bytes may be NULL and GP_PENDING is returned.
 */
GpResult UartRx_Feed(UartRx *rx, const uint8_t *bytes,size_t length, size_t *consumed, TargetSample *out);
    
    
/*
 * Nonblocking CAN transmission callback provided by the maintainer.
 *
 * Parameters:
 *   user  Application-provided context passed to Gateway_Init().
 *   frame Pointer to the CAN frame to be queued.
 *
 * Returns:
 *   GP_OK       The entire frame was copied into a bounded,
 *               maintainer-owned TX queue.
 *   GP_PENDING  The queue is busy; no data was consumed.
 *   GP_IO_ERROR The queue operation failed.
 *
 * The callback must copy the frame before returning. It must not retain
 * the supplied pointer for later DMA or transmission.
 *
 * Confirm with Ali whether the frame-based callback shown here is
 * preferred over the alternative byte-buffer callback.
 *
 * GP_OK confirms queue acceptance only; it does not confirm CAN delivery
 * or physical motor movement.
 */
typedef GpResult (*CanWriteFn)(void *user, const GpCanFrame *frame);

typedef struct {
    CanWriteFn write;
    void      *user;
    uint32_t   max_queue_ms;    /* Maximum permitted command queue age. */
    uint16_t   last_sequence;   /* Last accepted sequence; valid only if
                                   has_baseline is true. */
    bool       has_baseline;    /* Whether a sequence baseline is set. */
} Gateway;

/*
 * Initialize the gateway's forwarding state.
 *
 * Stores the CAN callback, its application context, and the maximum
 * permitted queue age. Rejects a NULL gateway or callback, a zero age
 * limit, or a limit >= 2^31 ms with GP_BAD_ARG.
 *
 * Starts without an established sequence baseline.
 */
GpResult Gateway_Init(Gateway *gateway, CanWriteFn write,
                      void *user, uint32_t max_queue_ms);

/*
 * Validate and forward one decoded command.
 *
 * Parameters:
 *   gateway   Persistent gateway state.
 *   command   Decoded command to validate and forward.
 *   queued_ms Elapsed time since the ESP32 byte chunk was enqueued.
 *
 * Rejects commands that exceed the configured age limit or have
 * duplicate/old sequence numbers. A sequence is considered fresh when
 * its unsigned 16-bit delta from the last accepted sequence is 1..32767.
 *
 * The initial sequence baseline is accepted only from a valid neutral
 * command with the arm request cleared.
 *
 * The sequence baseline is updated only when the CAN callback returns
 * GP_OK. GP_PENDING leaves the command eligible for a bounded retry.
 * This function must never block while waiting for transmission.
 *
 * A well-formed invalid neutral stop command with a fresh sequence must
 * still be forwarded so the receiving controller can stop safely.
 */
GpResult Gateway_Forward(Gateway *gateway,
                         const TargetSample *command, uint32_t queued_ms);

/*
 * Clear sequence-tracking state while preserving the configured callback,
 * callback context, and queue-age limit. The application maintainer is
 * responsible for purging its own application queues.
 */
void Gateway_Reset(Gateway *gateway);

#endif /* GATEWAY_H */