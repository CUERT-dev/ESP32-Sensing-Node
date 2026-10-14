#ifndef GP_PROTOCOL_H
#define GP_PROTOCOL_H

/*
 * gp_protocol - Portable command encoding and decoding, API revision 10.
 *
 * Hardware-independent C library.
 * Do not include ESP-IDF, STM32 HAL, UART, or TWAI driver headers here
 * or in gp_protocol.c.
 */

#include <stdint.h>
#include <stddef.h>

#define GP_PROTOCOL_REV        10

/* ---------- Result codes ---------- */
typedef enum {
    GP_OK = 0,      /* Operation succeeded or data was accepted. */
    GP_PENDING,     /* More input is needed, or the TX queue is busy. */
    GP_INVALID,     /* Malformed input or protocol constraint violation. */
    GP_BAD_ARG,     /* Required argument is NULL or otherwise invalid. */
    GP_NO_SPACE,    /* Output buffer capacity is insufficient. */
    GP_IO_ERROR     /* Transmission callback or I/O operation failed. */
} GpResult;

/* ---------- UART wire format ----------
 * Frame size: 14 bytes. Multibyte fields are little-endian.
 *
 * Byte 0..1   Start marker: A5 5A
 * Byte 2      Version: 2
 * Byte 3      Message kind: 1
 * Byte 4      Payload length: 7
 * Byte 5..6   Sequence: uint16_t
 * Byte 7..8   Throttle: uint16_t, range 0..1000
 * Byte 9..10  Steering: int16_t, range -1000..1000
 * Byte 11     Flags
 * Byte 12..13 CRC16-CCITT-FALSE over bytes 2..11.
 *
 * CRC16: polynomial 0x1021, initial value 0xFFFF,
 * no reflection, final XOR 0x0000.
 */
#define GP_UART_FRAME_LEN      14u
#define GP_UART_SOF0           0xA5u
#define GP_UART_SOF1           0x5Au
#define GP_UART_VERSION        2u
#define GP_UART_KIND           1u
#define GP_UART_PAYLOAD_LEN    7u

/* ---------- CAN wire format ----------
 * Classic CAN, 11-bit standard identifier, 500 kbit/s.
 *
 * Command ID: 0x105
 * DLC: 8
 * Byte 0..1   Sequence: uint16_t
 * Byte 2..3   Throttle: uint16_t, range 0..1000
 * Byte 4..5   Steering: int16_t, range -1000..1000
 * Byte 6      Flags
 * Byte 7      CRC8 over bytes 0..6.
 *
 * CRC8: polynomial 0x07, initial value 0x00,
 * no reflection, final XOR 0x00.
 */
#define GP_CAN_ID_COMMAND      0x105u
#define GP_CAN_DLC             8u

/* ---------- Command limits ---------- */
#define GP_THROTTLE_MAX        1000u
#define GP_STEERING_MAX        1000

/* ---------- Command flags ----------
 * Bit 0: Valid
 * Bit 1: Arm request
 * Bit 2: Deadman
 * Bits 3..7: Reserved; must be zero.
 */
#define GP_FLAG_VALID          0x01u
#define GP_FLAG_ARM            0x02u
#define GP_FLAG_DEADMAN        0x04u

/* Request bits accepted from the command source: bits 1 and 2. */
#define GP_FLAGS_REQUEST_MASK  (GP_FLAG_ARM | GP_FLAG_DEADMAN)

/* All defined flag bits: bits 0, 1, and 2. */
#define GP_FLAGS_ALL_MASK      (GP_FLAG_VALID | GP_FLAGS_REQUEST_MASK)

/* ---------- Decoded command ----------
 * Common representation used independently of UART or CAN encoding.
 */
typedef struct {
    uint16_t sequence;      /* Sequence number; wraps modulo 65536. */
    uint16_t throttle_pm;   /* Forward throttle demand: 0..1000. */
    int16_t  steering_pm;   /* Signed steering rate demand: -1000..1000. */
    uint8_t  flags;         /* Command flags defined above. */
} TargetSample;

/*
 * Invalid-stop rule:
 * When the valid flag is clear, throttle and steering must be zero
 * and flags must equal zero.
 *
 * The stop command still carries its sequence number and CRC.
 */

/* ---------- CAN frame ----------
 * Portable representation of one CAN frame.
 */
typedef struct {
    uint32_t id;            /* CAN identifier; command frames use 0x105. */
    uint8_t  dlc;           /* Data length; command frames use 8. */
    uint8_t  data[8];       /* CAN payload. */
} GpCanFrame;

/* ---------- Portable codec API ----------
 * No hardware access. Inputs are not modified.
 * Decoding functions leave the output command unchanged on failure.
 */

/*
 * Encode a command into a 14-byte UART frame.
 * written is set to zero on failure.
 * Returns GP_NO_SPACE if capacity is less than 14 bytes.
 */
GpResult GP_EncodeUart(const TargetSample *command,
                       uint8_t *out, size_t capacity, size_t *written);

/*
 * Decode exactly one 14-byte UART frame.
 * Validate the start marker, version, kind, payload length, CRC,
 * command ranges, and flags.
 */
GpResult GP_DecodeUart(const uint8_t *bytes,
                       size_t length, TargetSample *out);

/* Encode one command into a caller-owned CAN frame. */
GpResult GP_EncodeCan(const TargetSample *command, GpCanFrame *out);

/*
 * Decode a CAN command frame.
 * Validate the identifier, DLC, CRC, command ranges, and flags.
 */
GpResult GP_DecodeCan(const GpCanFrame *frame, TargetSample *out);

#endif /* GP_PROTOCOL_H */
