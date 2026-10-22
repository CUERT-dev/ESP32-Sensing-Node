#include "gateway.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static GpCanFrame captured_frame;
static unsigned int captured_count;

static GpResult capture_can(void *user, const GpCanFrame *frame)
{
    (void)user;
    captured_frame = *frame;
    ++captured_count;
    return GP_OK;
}

int main(void)
{
    UartRx rx;
    Gateway gateway;
    TargetSample input = {
        .sequence = 1u,
        .throttle_pm = 0u,
        .steering_pm = 0,
        .flags = GP_FLAG_VALID,
    };
    TargetSample parsed;
    TargetSample decoded_can;
    uint8_t uart_bytes[GP_UART_FRAME_LEN];
    size_t written = 0u;
    size_t consumed = 0u;

    assert(GP_EncodeUart(&input, uart_bytes, sizeof(uart_bytes), &written) == GP_OK);
    assert(written == GP_UART_FRAME_LEN);

    UartRx_Init(&rx);
    assert(UartRx_Feed(&rx, uart_bytes, written, &consumed, &parsed) == GP_OK);
    assert(consumed == written);
    assert(parsed.sequence == input.sequence);

    assert(Gateway_Init(&gateway, capture_can, NULL, 100u) == GP_OK);
    assert(Gateway_Forward(&gateway, &parsed, 0u) == GP_OK);
    assert(captured_count == 1u);
    assert(GP_DecodeCan(&captured_frame, &decoded_can) == GP_OK);
    assert(decoded_can.sequence == input.sequence);
    assert(decoded_can.flags == input.flags);

    return 0;
}