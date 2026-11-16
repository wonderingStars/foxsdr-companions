#define LOG_PREFIX "msg_coding"

#include <tetrapol/bit_utils.h>
#include <tetrapol/log.h>
#include <tetrapol/msg_coding.h>

#include <stdlib.h>

/**
  @param data_ptr Ponter to address structure, on return points to next address.
  @return true if another address follows, false otherwise.
  */
bool address_decode(address_t *address, const uint8_t **data_ptr)
{
    const uint8_t *data = *data_ptr;

    const bool li = get_bits(1, data, 0);
    address->cna = get_bits(3, data, 1);

    switch (address->cna) {
        case ADDRESS_CNA_NOT_SIGNIFICANT:
            address->len = 0;
            if (get_bits(4, data, 4) != 0) {
                LOG(WTF, "CND == 0 but address = 0x%0x", get_bits(4, data, 4));
            }
            ++data;
            break;

        case ADDRESS_CNA_RFSI:
            address->len = 9;
            for (int i = 0; i < 9; ++i) {
                address->rfsi.addr[i] = get_bits(4, data , 4 + 4*i);
            }
            data += 5;
            break;

        case ADDRESS_CNA_PABX:
            address->len = get_bits(4, data, 4);
            for (int i = 0; i < address->len; ++i) {
                address->pabx[i] = get_bits(4, data, 8 + 4*i);
            }
            data += 1 + (address->len + 1) / 2;
            break;

        case ADDRESS_CNA_X400:
        case ADDRESS_CNA_FUNCTIONAL:
        case ADDRESS_CNA_LONG:
        case ADDRESS_CNA_BINARY:
        case ADDRESS_CNA_ESCAPED_CODE:
            address->len = 0;
            LOG(ERR, "TODO: unsupported address CNA");
            break;

        default:
            address->len = 0;
            LOG(WTF, "unknown address CNA");
            break;
    }

    *data_ptr = data;

    return li;
}

void address_print(const address_t *address)
{
    LOGF("\t\tADDRESS CNA=%i ", address->cna);
    switch (address->cna) {
        case ADDRESS_CNA_NOT_SIGNIFICANT:
            LOGF("NOT_SIGNIFICANT\n");
            break;

        case ADDRESS_CNA_RFSI:
            LOGF("RFSI=%x%x%x-%x-%x%x-%x%x%x\n", address->rfsi.r[0],
                    address->rfsi.r[1], address->rfsi.r[2],
                    address->rfsi.f, address->rfsi.s[0],
                    address->rfsi.s[1], address->rfsi.i[0],
                    address->rfsi.i[1], address->rfsi.i[2]);
            break;

        case ADDRESS_CNA_PABX:
            LOGF("PABX=");
            for (int i = 0; i < address->len; ++i) {
                LOGF("%d", address->pabx[i]);
            }
            LOGF("\n");
            break;

        default:
            LOGF("TODO: (unsupported/unwknow)\n");
    }
}

/*
 * FoxSDR helper change: upstream looped "while (address_decode(...))" with no length limit. An address with an
 * unsupported CNA does not advance the data pointer, so a damaged or hostile message kept the loop (and the realloc
 * behind it) running for ever. The list is now limited to the bytes available and to ADDRESS_LIST_MAX entries.
 */
enum {
    ADDRESS_LIST_MAX = 8,
};

/** bytes taken by the address at data (PAS 0001-3-2 5.3.4), -1 if unsupported or not completely inside avail */
static int address_size(const uint8_t *data, int avail)
{
    if (avail < 1) {
        return -1;
    }
    int n;
    switch (get_bits(3, data, 1)) {
        case ADDRESS_CNA_NOT_SIGNIFICANT:
            n = 1;
            break;
        case ADDRESS_CNA_RFSI:
            n = 5;
            break;
        case ADDRESS_CNA_PABX:
            n = 1 + ((int)get_bits(4, data, 4) + 1) / 2;
            break;
        default:
            return -1;
    }
    return n <= avail ? n : -1;
}

int address_list_decode(address_list_t **ptr_addrs, const uint8_t *data, int len)
{
    address_list_t *addrs = *ptr_addrs;
    bool more;
    do {
        const int n = addrs ? (addrs->nadrs + 1) : 1;
        const int sz = address_size(data, len);
        if (n > ADDRESS_LIST_MAX || sz < 0) {
            LOG(ERR, "address list: unsupported or truncated address");
            *ptr_addrs = addrs;
            return addrs ? 0 : -1;
        }
        const int l = sizeof(address_list_t) + n * sizeof(address_t);
        address_list_t *p = realloc(addrs, l);
        if (!p) {
            *ptr_addrs = addrs;
            return -1;
        }
        addrs = p;
        addrs->nadrs = n;
        more = address_decode(&addrs->called_adr[addrs->nadrs-1], &data);
        len -= sz;
    } while (more);

    *ptr_addrs = addrs;

    return 0;
}
