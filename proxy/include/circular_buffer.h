#ifndef __CIRCULAR_BUFFER_H__
#define __CIRCULAR_BUFFER_H__

#include "common.h"

/** Fixed-size circular byte buffer used for streaming BGP messages. */
typedef struct cirular_buffer
{
    uint8_t  buf[MAX_BGP_MESSAGE_SIZE * 2];
    uint32_t actIdxRead;
    uint32_t actIdxWrite;
    uint32_t actLen;
} circBuf_t;

#define REMAINING_LEN(buf) (MAX_BGP_MESSAGE_SIZE * 2 - (buf).actLen)

void CircBuf_copy(circBuf_t *buf, char *dest, int n);
uint8_t CircBuf_read_8(circBuf_t *buf);
uint16_t CircBuf_read_16(circBuf_t *buf);
uint32_t CircBuf_read_32(circBuf_t *buf);
uint64_t CircBuf_read_64(circBuf_t *buf);
uint8_t CircBuf_get_8_without_reading(circBuf_t *buf);
uint16_t CircBuf_get_16_without_reading(circBuf_t *buf);
uint32_t CircBuf_get_32_without_reading(circBuf_t *buf);
void CircBuf_read_n(circBuf_t *buf, uint8_t *dest, uint32_t n);
void CircBuf_backward_cursor(circBuf_t *buf, uint32_t n);
void CircBuf_forward_cursor(circBuf_t *buf, uint32_t n);
void CircBuf_write(circBuf_t *buf, uint8_t *toWrite, uint32_t size);
void CircBuf_reset(circBuf_t *buf);
void CircBuf_print(circBuf_t *buf);
void CircBuf_debug(circBuf_t *buf);
void CircBuf_debug_stdout(circBuf_t *buf, uint32_t length, char *filename);
void CircBuf_debug_all_stdout(circBuf_t *buf, uint8_t *marker,
                              uint16_t length, uint8_t size,
                              uint64_t timeSec, uint64_t timeUsec,
                              uint8_t kafkaDump, char *filename);
void CircBuf_heal(circBuf_t *buf);
void CircBuf_get_without_reading(circBuf_t *buf, uint8_t *dest, uint16_t n);

#endif
