#include "protocol.h"
#include <unistd.h>

/*Calculates the CRC32 checksum of a data buffer.
used to detect accidental changes to raw data (integrity check)*/
uint32_t crc32(const void *buf, size_t len) {
    static uint32_t table[256];
    static int have_table = 0;

    /*Lazy initialization: Build the CRC lookup table on the first call
    using the standard IEEE 802.3 polynomial (0xEDB88320)*/
    if (!have_table) {
        for (int i = 0; i < 256; i++) {
            uint32_t rem = i;
            for (int j = 0; j < 8; j++) rem = (rem >> 1) ^ ((rem & 1) ? 0xEDB88320 : 0);
            table[i] = rem;
        }
        have_table = 1;
    }
    uint32_t crc = 0xFFFFFFFF;
    const unsigned char *p = buf;
    //Process data byte by byte using the lookup table
    for (size_t i = 0; i < len; i++) crc = (crc >> 8) ^ table[(crc & 0xFF) ^ p[i]];
    //Finalize the CRC by inverting bits
    return crc ^ 0xFFFFFFFF;
}

/*Sends a data packet with a custom protocol header.
Frame Format: [Length (4 bytes)] + [Checksum (4 bytes)] + [Payload (N bytes)]
This solves TCP "sticky packet" issues.*/
int send_packet(int sock, void *data, size_t len) {
    uint32_t length = len;
    //Calculate checksum for data integrity
    uint32_t checksum = crc32(data, len);
    //1. Send the length of the payload first
    if (write(sock, &length, 4) != 4) return -1;
    //2. Send the checksum
    if (write(sock, &checksum, 4) != 4) return -1;
    //3. Send the actual data payload
    if (write(sock, data, len) != (ssize_t)len) return -1;
    return 0;
}

/*Receives a packet following the custom frame format.
Returns the number of bytes read, or negative values on error.*/
int recv_packet(int sock, void *buf, size_t buf_size) {
    uint32_t length, checksum;
    //1. Read the length header (4 bytes) to know how much data to expect
    if (read(sock, &length, 4) != 4) return -1;
    //Security check: Prevent buffer overflow if the incoming packet is too large
    if (length > buf_size) return -1;
    //2. Read the checksum header (4 bytes)
    if (read(sock, &checksum, 4) != 4) return -1;
    //3. Read the exact amount of data specified by 'length'
    if (read(sock, buf, length) != (ssize_t)length) return -1;
    /*4. integrity check: Re-calculate CRC32 and compare with the received checksum
    Returns -2 if data corruption is detected*/
    if (crc32(buf, length) != checksum) return -2;
    return length;
}