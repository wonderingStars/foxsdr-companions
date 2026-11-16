These headers are not from dumpvdl2 or libacars. They are small stand-ins (written for this helper, GPL-3.0-or-later)
so that the unmodified-as-far-as-possible dumpvdl2 sources, which are written for Linux, build with MSVC and
without glib, pthreads or sockets:

- glib.h, gmodule.h   a type name, and the four GByteArray calls asn1-format-icao-text.c uses
- unistd.h            empty
- sys/time.h          struct timeval (same layout as Winsock's, but no Winsock is linked or used)
- arpa/inet.h         ntohl/ntohs/htonl/htons as plain byte swaps (no network library is linked)
- pthread.h           type names only (the demodulator threads are not part of this helper)
- libacars/asn1/*.h   forwarders: libacars expects its own copy of the asn1c runtime, this helper uses the single
                      copy in src/asn1
