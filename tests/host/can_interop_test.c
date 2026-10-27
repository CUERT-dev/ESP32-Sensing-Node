#include "gateway.h"
#include <assert.h>
#include <stdio.h>

/* These symbols are compiled from the actual sibling Bluepill source, with
 * only their names changed to allow both real codecs in the same executable. */
GpResult Bluepill_DecodeCan(const GpCanFrame *, TargetSample *);
GpResult Bluepill_EncodeCan(const TargetSample *, GpCanFrame *);
static GpCanFrame captured;
static GpResult sink(void *unused, const GpCanFrame *frame)
{ (void)unused; captured = *frame; return GP_OK; }

static void send_and_compare(Gateway *gateway, const TargetSample *input, size_t split)
{
    uint8_t bytes[GP_UART_FRAME_BYTES];
    size_t length = 0, consumed;
    assert(GP_EncodeUart(input, bytes, sizeof(bytes), &length) == GP_OK);
    UartRx rx;
    UartRx_Init(&rx);
    TargetSample parsed, received;
    if (split < length) {
        assert(UartRx_Feed(&rx, bytes, split, &consumed, &parsed) == GP_PENDING);
        assert(consumed == split);
    }
    size_t offset = split < length ? split : 0U;
    assert(UartRx_Feed(&rx, bytes + offset, length - offset, &consumed, &parsed) == GP_OK);
    assert(Gateway_Forward(gateway, &parsed, 0U) == GP_OK);
    assert(captured.id == 0x105U && captured.dlc == 8U);
    assert(Bluepill_DecodeCan(&captured, &received) == GP_OK);
    assert(received.sequence == input->sequence && received.throttle_pm == input->throttle_pm &&
           received.steering_pm == input->steering_pm && received.flags == input->flags);
    GpCanFrame reverse;
    assert(Bluepill_EncodeCan(input, &reverse) == GP_OK);
    assert(GP_DecodeCan(&reverse, &received) == GP_OK);
    assert(received.steering_pm == input->steering_pm && received.sequence == input->sequence);
    captured.data[7] ^= 1U;
    assert(Bluepill_DecodeCan(&captured, &received) == GP_INVALID);
}

int main(void)
{
    Gateway gateway;
    assert(Gateway_Init(&gateway, sink, NULL, 100U) == GP_OK);
    TargetSample s = {65530U, 0U, 0, GP_FLAG_VALID};
    send_and_compare(&gateway, &s, 1U); /* establish the real neutral baseline */
    const int16_t steering[] = {-1000, -250, 0, 1000};
    const uint16_t throttle[] = {0U, 1U, 600U, 1000U};
    for (size_t split = 1U; split < GP_UART_FRAME_BYTES; ++split) {
        for (size_t i = 0U; i < 4U; ++i) {
            ++s.sequence; /* includes uint16_t sequence rollover */
            s.throttle_pm = throttle[i];
            s.steering_pm = steering[i];
            s.flags = GP_FLAG_VALID | GP_FLAG_ARM | GP_FLAG_DEADMAN;
            send_and_compare(&gateway, &s, split);
        }
    }
    ++s.sequence;
    s.throttle_pm = 0U; s.steering_pm = 0; s.flags = 0U;
    send_and_compare(&gateway, &s, 5U); /* invalid-stop packet crosses unchanged */
    puts("PASS actual ESP UART/gateway -> actual Bluepill CAN codec; stream splits, limits, CRC, rollover and stop");
    return 0;
}
