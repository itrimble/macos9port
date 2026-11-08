#include "st_int.h"

uint16_t st_crc16_xmodem(uint16_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t st_crc16_arc(uint16_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

uint32_t st_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

int st_huff_build(StHuff *h, const uint8_t *lengths, int nsym) {
    if (nsym < 0 || nsym > 1024) return -1;
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < nsym; i++) {
        if (lengths[i] > 16) return -1;
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    /* Reject oversubscribed code sets. */
    int left = 1;
    for (int len = 1; len <= 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    uint16_t offs[17];
    offs[1] = 0;
    for (int len = 1; len < 16; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < nsym; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    h->nsym = nsym;
    return 0;
}

int st_huff_read(const StHuff *h, StBits *b, int lsb) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 16; len++) {
        code |= (int)(lsb ? st_bits_lsb(b, 1) : st_bits_msb(b, 1));
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (st_bits_over(b)) return -1;
    }
    return -1;
}
