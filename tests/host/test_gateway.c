#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gateway.h"
#include "gp-protocol.h"

/* Mock CAN Context for Gateway Testing */
typedef struct {
    GpCanFrame last_frame;
    unsigned write_calls;
    GpResult return_result;
} MockCan;

static GpResult mock_can_write(void *user, const GpCanFrame *frame)
{
    assert(user != NULL);
    assert(frame != NULL);
    MockCan *mock = (MockCan *)user;
    mock->write_calls++;
    if (mock->return_result != GP_OK) {
        return mock->return_result;
    }
    memcpy(&mock->last_frame, frame, sizeof(GpCanFrame));
    return GP_OK;
}

/* UT-003a: GP_EncodeUart buffer insufficient capacity check */
static void test_ut003a_encode_uart_no_space(void)
{
    TargetSample cmd = {.sequence = 1, .throttle_pm = 500, .steering_pm = 0, .flags = GP_FLAG_VALID};
    uint8_t buf[13];
    size_t written = 99;
    GpResult res = GP_EncodeUart(&cmd, buf, sizeof(buf), &written);
    assert(res == GP_NO_SPACE);
    assert(written == 0);
    printf("PASS: UT-003a (GP_EncodeUart no space check)\n");
}

/* UT-003b: GP_DecodeUart valid 14-byte frame decode */
static void test_ut003b_decode_uart_valid(void)
{
    TargetSample in = {.sequence = 0x1234, .throttle_pm = 750, .steering_pm = -300, .flags = GP_FLAG_VALID | GP_FLAG_ARM};
    uint8_t raw[14];
    size_t written = 0;
    assert(GP_EncodeUart(&in, raw, sizeof(raw), &written) == GP_OK);
    assert(written == 14);

    TargetSample out = {0};
    assert(GP_DecodeUart(raw, sizeof(raw), &out) == GP_OK);
    assert(out.sequence == in.sequence);
    assert(out.throttle_pm == in.throttle_pm);
    assert(out.steering_pm == in.steering_pm);
    assert(out.flags == in.flags);
    printf("PASS: UT-003b (GP_DecodeUart valid frame)\n");
}

/* UT-003c: GP_DecodeUart CRC corruption detection */
static void test_ut003c_decode_uart_corrupted_crc(void)
{
    TargetSample in = {.sequence = 10, .throttle_pm = 200, .steering_pm = 100, .flags = GP_FLAG_VALID};
    uint8_t raw[14];
    size_t written = 0;
    assert(GP_EncodeUart(&in, raw, sizeof(raw), &written) == GP_OK);

    /* Flip a bit in CRC (byte 12) */
    raw[12] ^= 0x01;

    TargetSample out = {.sequence = 999};
    assert(GP_DecodeUart(raw, sizeof(raw), &out) == GP_INVALID);
    assert(out.sequence == 999); /* Untouched on error */
    printf("PASS: UT-003c (GP_DecodeUart CRC corruption rejection)\n");
}

/* UT-003d: GP_EncodeCan frame layout & CRC-8/SMBUS calculation */
static void test_ut003d_encode_can_golden_crc(void)
{
    TargetSample cmd = {.sequence = 1, .throttle_pm = 0, .steering_pm = 0, .flags = 0};
    GpCanFrame frame = {0};
    assert(GP_EncodeCan(&cmd, &frame) == GP_OK);
    assert(frame.id == GP_CAN_ID_COMMAND);
    assert(frame.dlc == GP_CAN_DLC);
    /* Golden vector: [01 00 00 00 00 00 00] -> CRC8 is 0xDF */
    assert(frame.data[7] == 0xDF);
    printf("PASS: UT-003d (GP_EncodeCan layout & CRC8 golden vector 0xDF)\n");
}

/* UT-003e: GP_DecodeCan frame decode */
static void test_ut003e_decode_can_valid(void)
{
    TargetSample cmd = {.sequence = 42, .throttle_pm = 600, .steering_pm = 250, .flags = GP_FLAG_VALID};
    GpCanFrame frame = {0};
    assert(GP_EncodeCan(&cmd, &frame) == GP_OK);

    TargetSample out = {0};
    assert(GP_DecodeCan(&frame, &out) == GP_OK);
    assert(out.sequence == 42);
    assert(out.throttle_pm == 600);
    assert(out.steering_pm == 250);
    assert(out.flags == GP_FLAG_VALID);
    printf("PASS: UT-003e (GP_DecodeCan valid frame)\n");
}

/* UT-003f: UartRx_Feed split-chunk stream feeding */
static void test_ut003f_uartrx_split_chunks(void)
{
    TargetSample in = {.sequence = 100, .throttle_pm = 400, .steering_pm = -100, .flags = GP_FLAG_VALID};
    uint8_t raw[14];
    size_t written = 0;
    assert(GP_EncodeUart(&in, raw, sizeof(raw), &written) == GP_OK);

    UartRx rx;
    UartRx_Init(&rx);

    size_t consumed = 0;
    TargetSample out = {0};

    /* Feed Chunk 1 (3 bytes) */
    assert(UartRx_Feed(&rx, raw, 3, &consumed, &out) == GP_PENDING);
    assert(consumed == 3);

    /* Feed Chunk 2 (7 bytes) */
    assert(UartRx_Feed(&rx, raw + 3, 7, &consumed, &out) == GP_PENDING);
    assert(consumed == 7);

    /* Feed Chunk 3 (remaining 4 bytes) */
    assert(UartRx_Feed(&rx, raw + 10, 4, &consumed, &out) == GP_OK);
    assert(consumed == 4);
    assert(out.sequence == 100);
    assert(out.throttle_pm == 400);
    printf("PASS: UT-003f (UartRx_Feed split-chunk assembly)\n");
}

/* UT-003g: UartRx_Feed resynchronization after corrupted byte */
static void test_ut003g_uartrx_resync(void)
{
    UartRx rx;
    UartRx_Init(&rx);

    uint8_t garbage[] = {0xFF, 0x00, 0x12};
    size_t consumed = 0;
    TargetSample out = {0};

    GpResult res = UartRx_Feed(&rx, garbage, sizeof(garbage), &consumed, &out);
    assert(res == GP_PENDING || res == GP_INVALID);
    assert(consumed > 0);
    printf("PASS: UT-003g (UartRx_Feed resync to SOF)\n");
}

/* UT-003h: Gateway_Init parameter validation */
static void test_ut003h_gateway_init_bounds(void)
{
    Gateway gw;
    MockCan mock = {0};

    assert(Gateway_Init(NULL, mock_can_write, &mock, 50) == GP_BAD_ARG);
    assert(Gateway_Init(&gw, NULL, &mock, 50) == GP_BAD_ARG);
    assert(Gateway_Init(&gw, mock_can_write, &mock, 0) == GP_INVALID);
    assert(Gateway_Init(&gw, mock_can_write, &mock, 0x80000000u) == GP_INVALID);
    assert(Gateway_Init(&gw, mock_can_write, &mock, 50) == GP_OK);
    printf("PASS: UT-003h (Gateway_Init bounds validation)\n");
}

/* UT-003i: Gateway_Forward initial sequence baseline requires neutral */
static void test_ut003i_gateway_initial_baseline(void)
{
    Gateway gw;
    MockCan mock = {.return_result = GP_OK};
    assert(Gateway_Init(&gw, mock_can_write, &mock, 50) == GP_OK);

    /* Armed command before baseline accepted must be rejected */
    TargetSample armed_cmd = {.sequence = 1, .throttle_pm = 100, .steering_pm = 0, .flags = GP_FLAG_VALID | GP_FLAG_ARM};
    assert(Gateway_Forward(&gw, &armed_cmd, 10) == GP_INVALID);
    assert(mock.write_calls == 0);

    /* Valid neutral with ARM low establishes baseline */
    TargetSample neutral_cmd = {.sequence = 1, .throttle_pm = 0, .steering_pm = 0, .flags = GP_FLAG_VALID};
    assert(Gateway_Forward(&gw, &neutral_cmd, 10) == GP_OK);
    assert(mock.write_calls == 1);
    printf("PASS: UT-003i (Gateway_Forward initial baseline requires neutral)\n");
}

/* UT-003j: Gateway_Forward sequence freshness window */
static void test_ut003j_gateway_sequence_freshness(void)
{
    Gateway gw;
    MockCan mock = {.return_result = GP_OK};
    assert(Gateway_Init(&gw, mock_can_write, &mock, 50) == GP_OK);

    TargetSample neutral = {.sequence = 100, .throttle_pm = 0, .steering_pm = 0, .flags = GP_FLAG_VALID};
    assert(Gateway_Forward(&gw, &neutral, 5) == GP_OK);

    /* Duplicate sequence (delta 0) rejected */
    TargetSample dup = {.sequence = 100, .throttle_pm = 200, .steering_pm = 0, .flags = GP_FLAG_VALID | GP_FLAG_ARM};
    assert(Gateway_Forward(&gw, &dup, 5) == GP_INVALID);

    /* Fresh sequence (delta 1) accepted */
    TargetSample fresh = {.sequence = 101, .throttle_pm = 200, .steering_pm = 0, .flags = GP_FLAG_VALID | GP_FLAG_ARM};
    assert(Gateway_Forward(&gw, &fresh, 5) == GP_OK);
    printf("PASS: UT-003j (Gateway_Forward sequence freshness window)\n");
}

/* UT-003k: Gateway_Forward queue residence timeout rejection */
static void test_ut003k_gateway_queue_timeout(void)
{
    Gateway gw;
    MockCan mock = {.return_result = GP_OK};
    assert(Gateway_Init(&gw, mock_can_write, &mock, 50) == GP_OK);

    TargetSample neutral = {.sequence = 200, .throttle_pm = 0, .steering_pm = 0, .flags = GP_FLAG_VALID};
    assert(Gateway_Forward(&gw, &neutral, 5) == GP_OK);

    /* Command with queued_ms (60ms) exceeding max_queue_ms (50ms) must be dropped */
    TargetSample stale = {.sequence = 201, .throttle_pm = 100, .steering_pm = 0, .flags = GP_FLAG_VALID};
    assert(Gateway_Forward(&gw, &stale, 60) == GP_INVALID);
    printf("PASS: UT-003k (Gateway_Forward queue residence timeout)\n");
}

int main(void)
{
    printf("=== Running ESP32 Gateway Host Unit Tests (UT-003a..k) ===\n");
    test_ut003a_encode_uart_no_space();
    test_ut003b_decode_uart_valid();
    test_ut003c_decode_uart_corrupted_crc();
    test_ut003d_encode_can_golden_crc();
    test_ut003e_decode_can_valid();
    test_ut003f_uartrx_split_chunks();
    test_ut003g_uartrx_resync();
    test_ut003h_gateway_init_bounds();
    test_ut003i_gateway_initial_baseline();
    test_ut003j_gateway_sequence_freshness();
    test_ut003k_gateway_queue_timeout();
    printf("=== All ESP32 Gateway Host Unit Tests PASSED ===\n");
    return 0;
}
