/* Stand-in for arpa/inet.h: byte swaps only. No network library is used or linked. */
#ifndef FOXSDR_COMPAT_ARPA_INET_H
#define FOXSDR_COMPAT_ARPA_INET_H
#include <stdint.h>
static inline uint32_t foxsdr_bswap32(uint32_t v) {
	return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) | ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}
static inline uint16_t foxsdr_bswap16(uint16_t v) {
	return (uint16_t)(((v & 0x00ffu) << 8) | ((v & 0xff00u) >> 8));
}
/* this helper only targets little-endian hosts (x86-64 / ARM64 Windows) */
#define ntohl(x) foxsdr_bswap32((uint32_t)(x))
#define htonl(x) foxsdr_bswap32((uint32_t)(x))
#define ntohs(x) foxsdr_bswap16((uint16_t)(x))
#define htons(x) foxsdr_bswap16((uint16_t)(x))
#endif
