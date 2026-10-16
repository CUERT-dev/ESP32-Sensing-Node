#include "gateway.h"

#include <string.h>

static void parser_resync(UartRx *rx)
{
    /*
     * Preserve a trailing A5 because it may be the first byte
     * of the next start-of-frame marker.
     */
    if (rx->used > 0u &&
        rx->frame[rx->used - 1u] == GP_UART_SOF0) {
        rx->frame[0] = GP_UART_SOF0;
        rx->used = 1u;
    } else {
        rx->used = 0u;
    }
}

void UartRx_Init(UartRx *rx)
{
    if (rx != NULL) {
        memset(rx, 0, sizeof(*rx));
    }
}

GpResult UartRx_Feed(UartRx *rx,
                     const uint8_t *bytes,
                     size_t length,
                     size_t *consumed,
                     TargetSample *out)
{
    size_t i = 0u;

    if (consumed == NULL) {
        return GP_BAD_ARG;
    }

    *consumed = 0u;

    if (rx == NULL ||
        out == NULL ||
        (bytes == NULL && length != 0u)) {
        return GP_BAD_ARG;
    }

    if (length == 0u) {
        return GP_PENDING;
    }

    while (i < length) {
        uint8_t byte = bytes[i++];

        /*
         * Search for the first SOF byte.
         */
        if (rx->used == 0u) {
            if (byte == GP_UART_SOF0) {
                rx->frame[0] = byte;
                rx->used = 1u;
            }

            continue;
        }

        /*
         * The second SOF byte must follow.
         */
        if (rx->used == 1u) {
            if (byte == GP_UART_SOF1) {
                rx->frame[1] = byte;
                rx->used = 2u;
            } else if (byte == GP_UART_SOF0) {
                rx->frame[0] = byte;
                rx->used = 1u;
            } else {
                rx->used = 0u;
            }

            continue;
        }

        rx->frame[rx->used++] = byte;

        /*
         * Once the five-byte header is available,
         * reject unsupported versions, kinds, or lengths.
         */
        if (rx->used == 5u &&
            (rx->frame[2] != GP_UART_VERSION ||
             rx->frame[3] != GP_UART_KIND ||
             rx->frame[4] != GP_UART_PAYLOAD_LEN)) {

            parser_resync(rx);
            *consumed = i;

            return GP_INVALID;
        }

        /*
         * Decode one complete UART frame.
         */
        if (rx->used == GP_UART_FRAME_LEN) {
            TargetSample candidate;

            GpResult result = GP_DecodeUart(
                rx->frame,
                GP_UART_FRAME_LEN,
                &candidate
            );

            *consumed = i;

            if (result == GP_OK) {
                rx->used = 0u;
                *out = candidate;

                return GP_OK;
            }

            parser_resync(rx);

            return result;
        }
    }

    *consumed = i;

    return GP_PENDING;
}

static int is_valid_neutral_disarmed(
    const TargetSample *command)
{
    return ((command->flags & GP_FLAG_VALID) != 0u) &&
           (command->throttle_pm == 0u) &&
           (command->steering_pm == 0) &&
           ((command->flags & GP_FLAG_ARM) == 0u);
}

static int sequence_is_fresh(
    uint16_t candidate,
    uint16_t previous)
{
    /*
     * Unsigned subtraction naturally handles uint16_t wraparound.
     * A delta of 1..32767 represents a fresh sequence.
     */
    uint16_t delta = (uint16_t)(candidate - previous);

    return (delta >= 1u) && (delta <= 32767u);
}

GpResult Gateway_Init(Gateway *gateway,
                      CanWriteFn write,
                      void *user,
                      uint32_t max_queue_ms)
{
    if (gateway == NULL || write == NULL) {
        return GP_BAD_ARG;
    }

    if (max_queue_ms == 0u ||
        max_queue_ms >= 0x80000000u) {
        return GP_INVALID;
    }

    gateway->write = write;
    gateway->user = user;
    gateway->max_queue_ms = max_queue_ms;
    gateway->last_sequence = 0u;
    gateway->have_sequence = 0u;

    return GP_OK;
}

GpResult Gateway_Forward(Gateway *gateway,
                         const TargetSample *command,
                         uint32_t queued_ms)
{
    GpCanFrame frame;
    GpResult result;

    if (gateway == NULL ||
        command == NULL ||
        gateway->write == NULL) {
        return GP_BAD_ARG;
    }

    /*
     * Reject commands that have exceeded the permitted queue age.
     */
    if (queued_ms > gateway->max_queue_ms) {
        return GP_INVALID;
    }

    /*
     * Validate and encode the command.
     * Invalid-stop commands are permitted by the protocol
     * when their throttle, steering, and flags are all zero.
     */
    result = GP_EncodeCan(command, &frame);

    if (result != GP_OK) {
        return result;
    }

    /*
     * Establish the initial sequence baseline only from
     * a valid neutral command with ARM clear.
     */
    if (!gateway->have_sequence) {
        if (!is_valid_neutral_disarmed(command)) {
            return GP_INVALID;
        }
    } else {
        /*
         * Reject duplicate, old, and out-of-order commands.
         */
        if (!sequence_is_fresh(
                command->sequence,
                gateway->last_sequence)) {
            return GP_INVALID;
        }
    }

    /*
     * The callback must copy the complete frame into its
     * bounded TX queue before returning GP_OK.
     */
    result = gateway->write(gateway->user, &frame);

    /*
     * Update sequence tracking only after queue acceptance.
     * GP_PENDING leaves this command eligible for retry.
     */
    if (result == GP_OK) {
        gateway->last_sequence = command->sequence;
        gateway->have_sequence = 1u;
    }

    return result;
}

void Gateway_Reset(Gateway *gateway)
{
    if (gateway != NULL) {
        gateway->last_sequence = 0u;
        gateway->have_sequence = 0u;
    }
}