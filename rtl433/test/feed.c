/*
 * feed.c -- small test tool for the rtl433 helper tests (not shipped). GPL-2.0-or-later, written for this helper.
 *
 *   feed chunks <file> <bytes>      copy <file> to standard output in writes of <bytes> bytes, flushing each
 *   feed oddbyte <file>             copy <file> to standard output, then one stray byte
 *   feed garbage <bytes> <seed>     <bytes> pseudo-random bytes (deterministic) to standard output
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc >= 4 && !strcmp(argv[1], "chunks")) {
        FILE *f = fopen(argv[2], "rb");
        long n  = atol(argv[3]);
        if (!f || n < 1)
            return 2;
        char *buf = malloc((size_t)n);
        size_t got;
        while ((got = fread(buf, 1, (size_t)n, f)) > 0) {
            fwrite(buf, 1, got, stdout);
            fflush(stdout);
        }
        fclose(f);
        free(buf);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "oddbyte")) {
        FILE *f = fopen(argv[2], "rb");
        if (!f)
            return 2;
        int c;
        while ((c = fgetc(f)) != -1)
            fputc(c, stdout);
        fputc(0x7f, stdout);
        fclose(f);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "garbage")) {
        unsigned long n = strtoul(argv[2], NULL, 10);
        uint32_t s      = (uint32_t)strtoul(argv[3], NULL, 10) | 1u;
        for (unsigned long k = 0; k < n; ++k) {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            fputc((int)(s >> 11) & 0xff, stdout);
        }
        return 0;
    }
    fprintf(stderr, "usage: feed chunks <file> <bytes> | oddbyte <file> | garbage <bytes> <seed>\n");
    return 2;
}
