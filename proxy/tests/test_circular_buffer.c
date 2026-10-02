#include <assert.h>
#include <string.h>

#include "circular_buffer.h"

static void test_integer_reads_and_peeks(void)
{
    circBuf_t buf;
    uint8_t bytes[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    CircBuf_reset(&buf);
    CircBuf_write(&buf, bytes, sizeof(bytes));
    assert(CircBuf_get_8_without_reading(&buf) == 0x01);
    assert(CircBuf_get_16_without_reading(&buf) == 0x0102);
    assert(CircBuf_get_32_without_reading(&buf) == 0x01020304);
    assert(buf.actLen == sizeof(bytes));
    assert(CircBuf_read_64(&buf) == UINT64_C(0x0102030405060708));
    assert(buf.actLen == 0);
}

static void test_wraparound_and_cursor_movement(void)
{
    circBuf_t buf;
    uint8_t bytes[] = {0xaa, 0xbb, 0xcc, 0xdd};
    uint8_t copy[sizeof(bytes)] = {0};

    CircBuf_reset(&buf);
    buf.actIdxRead = MAX_BGP_MESSAGE_SIZE * 2 - 2;
    buf.actIdxWrite = buf.actIdxRead;
    CircBuf_write(&buf, bytes, sizeof(bytes));
    CircBuf_get_without_reading(&buf, copy, sizeof(copy));
    assert(memcmp(copy, bytes, sizeof(bytes)) == 0);

    assert(CircBuf_read_16(&buf) == 0xaabb);
    CircBuf_backward_cursor(&buf, 2);
    assert(CircBuf_read_32(&buf) == 0xaabbccdd);
    assert(buf.actLen == 0);
}

static void test_heal(void)
{
    circBuf_t buf;
    uint8_t bytes[20] = {1, 2, 3, 4};
    memset(bytes + 4, 0xff, 16);

    CircBuf_reset(&buf);
    CircBuf_write(&buf, bytes, sizeof(bytes));
    CircBuf_heal(&buf);
    assert(buf.actIdxRead == 4);
    assert(buf.actLen == 16);
}

int main(void)
{
    test_integer_reads_and_peeks();
    test_wraparound_and_cursor_movement();
    test_heal();
    return 0;
}
