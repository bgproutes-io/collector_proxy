#include "../include/circular_buffer.h"
#include "../include/utils.h"

void CircBuf_copy(circBuf_t *buf, char *dest, int n)
{
    int idx = buf->actIdxRead;

    for (int i = 0; i < n; i++)
        dest[i] = buf->buf[idx++ % (MAX_BGP_MESSAGE_SIZE * 2)];
}

uint8_t CircBuf_read_8(circBuf_t *buf)
{
    buf->actLen -= 1;
    uint8_t val = buf->buf[buf->actIdxRead++ % (MAX_BGP_MESSAGE_SIZE * 2)];
    buf->actIdxRead %= MAX_BGP_MESSAGE_SIZE * 2;
    return val;
}

uint8_t CircBuf_get_8_without_reading(circBuf_t *buf)
{
    return buf->buf[buf->actIdxRead % (MAX_BGP_MESSAGE_SIZE * 2)];
}

uint16_t CircBuf_read_16(circBuf_t *buf)
{
    uint16_t val = 0;
    val += buf->buf[buf->actIdxRead++ % (MAX_BGP_MESSAGE_SIZE * 2)] * 256;
    val += buf->buf[buf->actIdxRead++ % (MAX_BGP_MESSAGE_SIZE * 2)];
    buf->actIdxRead %= MAX_BGP_MESSAGE_SIZE * 2;
    buf->actLen -= 2;
    return val;
}

uint16_t CircBuf_get_16_without_reading(circBuf_t *buf)
{
    uint16_t val = 0;
    val += buf->buf[buf->actIdxRead % (MAX_BGP_MESSAGE_SIZE * 2)] * 256;
    val += buf->buf[(buf->actIdxRead + 1) % (MAX_BGP_MESSAGE_SIZE * 2)];
    return val;
}

uint32_t CircBuf_read_32(circBuf_t *buf)
{
    uint32_t val = 0;
    for (int i = 3; i >= 0; i--)
        val += buf->buf[buf->actIdxRead++ % (MAX_BGP_MESSAGE_SIZE * 2)] *
               my_pow(2, (uint64_t)i * 8);
    buf->actIdxRead %= MAX_BGP_MESSAGE_SIZE * 2;
    buf->actLen -= 4;
    return val;
}

uint32_t CircBuf_get_32_without_reading(circBuf_t *buf)
{
    uint32_t val = 0;
    uint32_t idx = 0;
    for (int i = 3; i >= 0; i--) {
        val += buf->buf[(buf->actIdxRead + idx) %
                       (MAX_BGP_MESSAGE_SIZE * 2)] *
               my_pow(2, (uint64_t)i * 8);
        idx++;
    }
    return val;
}

uint64_t CircBuf_read_64(circBuf_t *buf)
{
    uint64_t val = 0;
    for (int i = 7; i >= 0; i--)
        val += buf->buf[buf->actIdxRead++ % (MAX_BGP_MESSAGE_SIZE * 2)] *
               my_pow(2, (uint64_t)i * 8);
    buf->actIdxRead %= MAX_BGP_MESSAGE_SIZE * 2;
    buf->actLen -= 8;
    return val;
}

void CircBuf_write(circBuf_t *buf, uint8_t *toWrite, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++)
        buf->buf[buf->actIdxWrite++ % (MAX_BGP_MESSAGE_SIZE * 2)] = toWrite[i];
    buf->actIdxRead %= MAX_BGP_MESSAGE_SIZE * 2;
    buf->actLen += size;
}

void CircBuf_read_n(circBuf_t *buf, uint8_t *dest, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        dest[i] = buf->buf[buf->actIdxRead++ % (MAX_BGP_MESSAGE_SIZE * 2)];
    buf->actIdxRead %= MAX_BGP_MESSAGE_SIZE * 2;
    buf->actLen -= n;
}

void CircBuf_backward_cursor(circBuf_t *buf, uint32_t n)
{
    buf->actIdxRead = (buf->actIdxRead - n) % (MAX_BGP_MESSAGE_SIZE * 2);
    buf->actLen += n;
}

void CircBuf_forward_cursor(circBuf_t *buf, uint32_t n)
{
    buf->actIdxRead = (buf->actIdxRead + n) % (MAX_BGP_MESSAGE_SIZE * 2);
    buf->actLen -= n;
}

void CircBuf_reset(circBuf_t *buf)
{
    memset(buf->buf, 0, MAX_BGP_MESSAGE_SIZE * 2);
    buf->actIdxRead = 0;
    buf->actIdxWrite = 0;
    buf->actLen = 0;
}

void CircBuf_print(circBuf_t *buf)
{
    printf("Act Length is %d\n", buf->actLen);
    for (uint32_t i = 0; i < buf->actLen; i++)
        printf("%02X ", buf->buf[(i + buf->actIdxRead) %
                                  (MAX_BGP_MESSAGE_SIZE * 2)]);
    printf("\n");
}

void CircBuf_debug(circBuf_t *buf)
{
    int actIdx = 0;
    printf("Actual Buffer length = %d\n", buf->actLen);
    for (uint32_t i = 0; i < buf->actLen; i++) {
        printf("%02X ", buf->buf[(i + buf->actIdxRead) %
                                  (MAX_BGP_MESSAGE_SIZE * 2)]);
        if ((++actIdx % 16) == 0)
            printf("\n");
    }
}

void CircBuf_debug_stdout(circBuf_t *buf, uint32_t length, char *filename)
{
    FILE *lastUpd = filename ? fopen(filename, "w") : stdout;
    for (uint32_t i = 0; i < length; i++) {
        fprintf(lastUpd, "%02X ", buf->buf[(i + buf->actIdxRead) %
                                             (MAX_BGP_MESSAGE_SIZE * 2)]);
        if (((i + 1) % 16) == 0)
            fprintf(lastUpd, "\n");
    }
    if (filename)
        fclose(lastUpd);
}

void CircBuf_debug_all_stdout(circBuf_t *buf, uint8_t *marker,
                              uint16_t length, uint8_t size,
                              uint64_t timeSec, uint64_t timeUsec,
                              uint8_t kafkaDump, char *filename)
{
    FILE *lastUpd = filename ? fopen(filename, "w") : stdout;
    fprintf(lastUpd, "%016lx %016lx %02X\n", timeSec, timeUsec, kafkaDump);
    for (int i = 0; i < 16; i++)
        fprintf(lastUpd, "%02X ", marker[i]);
    fprintf(lastUpd, "\n%02X %02X %02X ", length / 256, length % 256, size);

    int actIdx = 19;
    for (int i = 0; i < length - 19; i++) {
        fprintf(lastUpd, "%02X ", buf->buf[(i + buf->actIdxRead) %
                                             (MAX_BGP_MESSAGE_SIZE * 2)]);
        if ((++actIdx % 16) == 0)
            fprintf(lastUpd, "\n");
    }
    if (filename)
        fclose(lastUpd);
}

void CircBuf_get_without_reading(circBuf_t *buf, uint8_t *dest, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++)
        dest[i] = buf->buf[(buf->actIdxRead + i) %
                           (MAX_BGP_MESSAGE_SIZE * 2)];
}

static bool is_bgp_header(uint8_t *marker)
{
    for (int i = 0; i < 16; i++) {
        if (marker[i] != 255)
            return False;
    }
    return True;
}

void CircBuf_heal(circBuf_t *buf)
{
    uint8_t marker[16];
    while (buf->actLen >= 16) {
        CircBuf_get_without_reading(buf, marker, 16);
        if (is_bgp_header(marker))
            return;
        CircBuf_read_8(buf);
    }
}
