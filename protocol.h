#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

uint32_t crc32(const void *buf, size_t len);
int send_packet(int sock, void *data, size_t len);
int recv_packet(int sock, void *buf, size_t buf_size);

#endif