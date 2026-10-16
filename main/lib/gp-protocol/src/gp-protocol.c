#include "gp-protocol.h"
#include <limits.h>

/*
 * Portable GP revision 10 packet codec.
 *
 * This file intentionally has no MCU, UART, CAN/TWAI, or RTOS dependencies.
 * All multibyte wire fields are serialized explicitly in little-endian order;
 * native C structure layouts are never transmitted directly.
 */

#define UART_CRC_OFFSET       12u
#define UART_CRC_INPUT_OFFSET  2u
#define UART_CRC_INPUT_LENGTH 10u
#define CAN_CRC_OFFSET         7u
#define CAN_CRC_INPUT_LENGTH   7u

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static int16_t read_i16_le(const uint8_t *p)
{
    uint16_t raw = read_u16_le(p);

    /* Convert the two's-complement wire representation without relying on
       an out-of-range unsigned-to-signed conversion. */
    if (raw <= (uint16_t)INT16_MAX) {
        return (int16_t)raw;
    }
    return (int16_t)(-1 - (int32_t)(UINT16_MAX - raw));
}

static void write_i16_le(uint8_t *p, int16_t value)
{
    write_u16_le(p, (uint16_t)value);
}

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xor-out 0. */
static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;

    for (i = 0; i < length; ++i) {
        unsigned bit;
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* CRC-8: poly 0x07, init 0, no reflection, xor-out 0. */
static uint8_t crc8_gp(const uint8_t *data, size_t length)
{
    uint8_t crc = 0u;
    size_t i;

    for (i = 0; i < length; ++i) {
        unsigned bit;
        crc ^= data[i];
        for (bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/*
 * Accept either:
 *   - a valid command (VALID set; only VALID/ARM/DEADMAN bits may be set), or
 *   - the mandated invalid-stop packet (flags and both demands are zero).
 */
static int sample_is_valid(const TargetSample *command)
{
    if (command->throttle_pm > GP_THROTTLE_MAX) {
        return 0;
    }

    if (command->steering_pm < -GP_STEERING_MAX ||
        command->steering_pm > GP_STEERING_MAX) {
        return 0;
    }

    if ((command->flags & (uint8_t)~GP_FLAGS_ALL_MASK) != 0u) {
        return 0;
    }

    if ((command->flags & GP_FLAG_VALID) == 0u) {
        return command->flags == 0u &&
               command->throttle_pm == 0u &&
               command->steering_pm == 0;
    }

    return 1;
}

GpResult GP_EncodeUart(const TargetSample *command,
                       uint8_t *out,
                       size_t capacity,
                       size_t *written)
{
    uint16_t crc;

    if (written != NULL) {
        *written = 0u;
    }

    if (command == NULL || out == NULL || written == NULL) {
        return GP_BAD_ARG;
    }

    if (!sample_is_valid(command)) {
        return GP_INVALID;
    }

    if (capacity < GP_UART_FRAME_LEN) {
        return GP_NO_SPACE;
    }

    out[0] = GP_UART_SOF0;
    out[1] = GP_UART_SOF1;
    out[2] = GP_UART_VERSION;
    out[3] = GP_UART_KIND;
    out[4] = GP_UART_PAYLOAD_LEN;

    write_u16_le(&out[5], command->sequence);
    write_u16_le(&out[7], command->throttle_pm);
    write_i16_le(&out[9], command->steering_pm);
    out[11] = command->flags;

    crc = crc16_ccitt_false(&out[UART_CRC_INPUT_OFFSET],
                            UART_CRC_INPUT_LENGTH);
    write_u16_le(&out[UART_CRC_OFFSET], crc);

    *written = GP_UART_FRAME_LEN;
    return GP_OK;
}

GpResult GP_DecodeUart(const uint8_t *bytes,
                       size_t length,
                       TargetSample *out)
{
    TargetSample decoded;
    uint16_t received_crc;
    uint16_t calculated_crc;

    if (bytes == NULL || out == NULL) {
        return GP_BAD_ARG;
    }

    if (length != GP_UART_FRAME_LEN) {
        return GP_INVALID;
    }

    if (bytes[0] != GP_UART_SOF0 ||
        bytes[1] != GP_UART_SOF1 ||
        bytes[2] != GP_UART_VERSION ||
        bytes[3] != GP_UART_KIND ||
        bytes[4] != GP_UART_PAYLOAD_LEN) {
        return GP_INVALID;
    }

    received_crc = read_u16_le(&bytes[UART_CRC_OFFSET]);
    calculated_crc = crc16_ccitt_false(&bytes[UART_CRC_INPUT_OFFSET],
                                        UART_CRC_INPUT_LENGTH);
    if (received_crc != calculated_crc) {
        return GP_INVALID;
    }

    decoded.sequence = read_u16_le(&bytes[5]);
    decoded.throttle_pm = read_u16_le(&bytes[7]);
    decoded.steering_pm = read_i16_le(&bytes[9]);
    decoded.flags = bytes[11];

    if (!sample_is_valid(&decoded)) {
        return GP_INVALID;
    }

    *out = decoded;
    return GP_OK;
}

GpResult GP_EncodeCan(const TargetSample *command, GpCanFrame *out)
{
    GpCanFrame encoded;

    if (command == NULL || out == NULL) {
        return GP_BAD_ARG;
    }

    if (!sample_is_valid(command)) {
        return GP_INVALID;
    }

    encoded.id = GP_CAN_ID_COMMAND;
    encoded.dlc = GP_CAN_DLC;
    write_u16_le(&encoded.data[0], command->sequence);
    write_u16_le(&encoded.data[2], command->throttle_pm);
    write_i16_le(&encoded.data[4], command->steering_pm);
    encoded.data[6] = command->flags;
    encoded.data[CAN_CRC_OFFSET] = crc8_gp(encoded.data, CAN_CRC_INPUT_LENGTH);

    *out = encoded;
    return GP_OK;
}

GpResult GP_DecodeCan(const GpCanFrame *frame, TargetSample *out)
{
    TargetSample decoded;

    if (frame == NULL || out == NULL) {
        return GP_BAD_ARG;
    }

    if (frame->id != GP_CAN_ID_COMMAND || frame->dlc != GP_CAN_DLC) {
        return GP_INVALID;
    }

    if (frame->id > 0x7FFu) {
        return GP_INVALID;
    }

    if (frame->data[CAN_CRC_OFFSET] !=
        crc8_gp(frame->data, CAN_CRC_INPUT_LENGTH)) {
        return GP_INVALID;
    }

    decoded.sequence = read_u16_le(&frame->data[0]);
    decoded.throttle_pm = read_u16_le(&frame->data[2]);
    decoded.steering_pm = read_i16_le(&frame->data[4]);
    decoded.flags = frame->data[6];

    if (!sample_is_valid(&decoded)) {
        return GP_INVALID;
    }

    *out = decoded;
    return GP_OK;
}
