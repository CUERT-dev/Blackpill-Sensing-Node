#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uart_sender.h"

typedef struct {
    uint8_t bytes[GP_UART_FRAME_LEN];
    size_t length;
    unsigned calls;
    GpResult callback_result;
} Recorder;

static GpResult recording_write(void *user,
                                const uint8_t *bytes,
                                size_t length)
{
    assert(user != NULL);
    assert(bytes != NULL);
    assert(length == GP_UART_FRAME_LEN);

    Recorder *r = (Recorder *)user;

    ++r->calls;
    r->length = length;

    /* Busy/failure consumes nothing in this test adapter. */
    if (r->callback_result != GP_OK) {
        return r->callback_result;
    }

    memcpy(r->bytes, bytes, length);
    return GP_OK;
}

static void test_init_arguments(void)
{
    UartSender sender = {0};
    assert(UartSender_Init(NULL, recording_write, NULL) == GP_BAD_ARG);
    assert(UartSender_Init(&sender, NULL, NULL) == GP_BAD_ARG);
    assert(UartSender_Init(&sender, recording_write, NULL) == GP_OK);
}

static void test_send_before_init(void)
{
    UartSender sender = {0};
    TargetSample command = {0};
    assert(UartSender_Send(&sender, &command) == GP_INVALID);
}

static void test_valid_frame_and_round_trip(void)
{
    UartSender sender = {0};
    Recorder r = {{0}, 0u, 0u, GP_OK};
    TargetSample command = {42u, 1000u, -1000, GP_FLAG_VALID | GP_FLAG_ARM | GP_FLAG_DEADMAN};
    TargetSample decoded = {0};

    assert(UartSender_Init(&sender, recording_write, &r) == GP_OK);
    assert(UartSender_Send(&sender, &command) == GP_OK);
    assert(r.calls == 1u);
    assert(r.length == GP_UART_FRAME_LEN);
    assert(r.bytes[0] == GP_UART_SOF0 && r.bytes[1] == GP_UART_SOF1);
    assert(r.bytes[2] == GP_UART_VERSION);
    assert(r.bytes[3] == GP_UART_KIND);
    assert(r.bytes[4] == GP_UART_PAYLOAD_LEN);
    assert(GP_DecodeUart(r.bytes, r.length, &decoded) == GP_OK);
    assert(decoded.sequence == command.sequence);
    assert(decoded.throttle_pm == command.throttle_pm);
    assert(decoded.steering_pm == command.steering_pm);
    assert(decoded.flags == command.flags);
}


static void test_independent_packet_vectors(void)
{
    static const uint8_t vector_normal[GP_UART_FRAME_LEN] = {
        0xA5u, 0x5Au, 0x02u, 0x01u, 0x07u, 0x34u, 0x12u,
        0xF4u, 0x01u, 0x06u, 0xFFu, 0x05u, 0x19u, 0x3Bu
    };
    static const uint8_t vector_maximum[GP_UART_FRAME_LEN] = {
        0xA5u, 0x5Au, 0x02u, 0x01u, 0x07u, 0xFFu, 0xFFu,
        0xE8u, 0x03u, 0xE8u, 0x03u, 0x07u, 0x9Fu, 0x6Cu
    };
    static const uint8_t vector_stop[GP_UART_FRAME_LEN] = {
        0xA5u, 0x5Au, 0x02u, 0x01u, 0x07u, 0x01u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0xE9u, 0xABu
    };
    UartSender sender = {0};
    Recorder r = {{0}, 0u, 0u, GP_OK};
    TargetSample normal = {0x1234u, 500u, -250, GP_FLAG_VALID | GP_FLAG_DEADMAN};
    TargetSample maximum = {0xFFFFu, 1000u, 1000, GP_FLAG_VALID | GP_FLAG_ARM | GP_FLAG_DEADMAN};
    TargetSample stop = {1u, 0u, 0, 0u};

    assert(UartSender_Init(&sender, recording_write, &r) == GP_OK);
    assert(UartSender_Send(&sender, &normal) == GP_OK);
    assert(r.length == sizeof vector_normal);
    assert(memcmp(r.bytes, vector_normal, sizeof vector_normal) == 0);

    assert(UartSender_Send(&sender, &maximum) == GP_OK);
    assert(r.length == sizeof vector_maximum);
    assert(memcmp(r.bytes, vector_maximum, sizeof vector_maximum) == 0);

    assert(UartSender_Send(&sender, &stop) == GP_OK);
    assert(r.length == sizeof vector_stop);
    assert(memcmp(r.bytes, vector_stop, sizeof vector_stop) == 0);
    assert(r.calls == 3u);
}

static void test_invalid_command_not_written(void)
{
    UartSender sender = {0};
    Recorder r = {{0}, 0u, 0u, GP_OK};
    TargetSample bad_throttle = {1u, 1001u, 0, GP_FLAG_VALID};
    TargetSample bad_steering = {1u, 0u, 1001, GP_FLAG_VALID};
    TargetSample bad_flags = {1u, 0u, 0, 0x80u};
    TargetSample bad_stop = {1u, 1u, 0, 0u};

    assert(UartSender_Init(&sender, recording_write, &r) == GP_OK);
    assert(UartSender_Send(&sender, &bad_throttle) == GP_INVALID);
    assert(UartSender_Send(&sender, &bad_steering) == GP_INVALID);
    assert(UartSender_Send(&sender, &bad_flags) == GP_INVALID);
    assert(UartSender_Send(&sender, &bad_stop) == GP_INVALID);
    assert(r.calls == 0u);
}

static void test_stop_and_callback_results(void)
{
    UartSender sender = {0};
    Recorder r = {{0}, 0u, 0u, GP_OK};
    TargetSample stop = {65535u, 0u, 0, 0u};

    assert(UartSender_Init(&sender, recording_write, &r) == GP_OK);
    assert(UartSender_Send(&sender, &stop) == GP_OK);
    assert(r.calls == 1u && r.length == GP_UART_FRAME_LEN);
    assert(r.bytes[5] == 0xFFu && r.bytes[6] == 0xFFu);
    assert(r.bytes[7] == 0u && r.bytes[8] == 0u);
    assert(r.bytes[9] == 0u && r.bytes[10] == 0u && r.bytes[11] == 0u);

    r.callback_result = GP_PENDING;
    assert(UartSender_Send(&sender, &stop) == GP_PENDING);
    r.callback_result = GP_IO_ERROR;
    assert(UartSender_Send(&sender, &stop) == GP_IO_ERROR);
    assert(r.calls == 3u);
}

static void test_null_send_arguments(void)
{
    UartSender sender = {0};
    Recorder r = {{0}, 0u, 0u, GP_OK};
    TargetSample command = {0u, 0u, 0, 0u};
    assert(UartSender_Init(&sender, recording_write, &r) == GP_OK);
    assert(UartSender_Send(NULL, &command) == GP_BAD_ARG);
    assert(UartSender_Send(&sender, NULL) == GP_BAD_ARG);
    assert(r.calls == 0u);
}
static void test_failed_reinit_and_missing_callback(void)
{
    UartSender sender = {0};
    Recorder r = {{0}, 0u, 0u, GP_OK};
    TargetSample command = {0};

    assert(UartSender_Init(
        &sender, recording_write, &r) == GP_OK);

    assert(UartSender_Init(
        &sender, NULL, &r) == GP_BAD_ARG);

    assert(!sender.initialized);
    assert(sender.write == NULL);
    assert(sender.user == NULL);

    assert(UartSender_Send(
        &sender, &command) == GP_INVALID);

    /* Deliberately malformed state checks the callback guard. */
    sender.initialized = true;

    assert(UartSender_Send(
        &sender, &command) == GP_INVALID);

    assert(r.calls == 0u);
}

int main(void)
{
    test_init_arguments();
    test_send_before_init();
    test_failed_reinit_and_missing_callback();
    test_valid_frame_and_round_trip();
    test_independent_packet_vectors();
    test_invalid_command_not_written();
    test_stop_and_callback_results();
    test_null_send_arguments();
    puts("uart_sender tests: PASS");
    return 0;
}
