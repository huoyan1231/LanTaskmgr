#include "netinfo.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp_addr(const void *a, const void *b)
{
    const ltm_netaddr *x = (const ltm_netaddr *)a;
    const ltm_netaddr *y = (const ltm_netaddr *)b;
    if (x->has_gateway != y->has_gateway) {
        return x->has_gateway ? -1 : 1;
    }
    return 0;
}

int ltm_net_list_addresses(ltm_netaddr *out, int cap)
{
    ULONG                 size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *buf = NULL;
    IP_ADAPTER_ADDRESSES *ad;
    ULONG                 ret;
    int                   count = 0;
    int                   attempts = 0;

    if (out == NULL || cap <= 0) {
        return 0;
    }

    for (;;) {
        void *nb = ltm_realloc(buf, size);
        if (nb == NULL) {
            ltm_free(buf);
            return 0;
        }
        buf = (IP_ADAPTER_ADDRESSES *)nb;
        ret = GetAdaptersAddresses(AF_INET,
                                   GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                       GAA_FLAG_SKIP_DNS_SERVER,
                                   NULL, buf, &size);
        if (ret == ERROR_SUCCESS) {
            break;
        }
        if (ret != ERROR_BUFFER_OVERFLOW || ++attempts > 4) {
            ltm_free(buf);
            return 0;
        }
    }

    for (ad = buf; ad != NULL && count < cap; ad = ad->Next) {
        IP_ADAPTER_UNICAST_ADDRESS *ua;
        BOOL                        has_gw;

        if (ad->OperStatus != IfOperStatusUp) {
            continue;
        }
        if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK || ad->IfType == IF_TYPE_TUNNEL) {
            continue;
        }

        has_gw = (ad->FirstGatewayAddress != NULL);

        for (ua = ad->FirstUnicastAddress; ua != NULL && count < cap; ua = ua->Next) {
            const struct sockaddr_in *sin;
            unsigned long             host;
            unsigned                  b0, b1, b2, b3;

            if (ua->Address.lpSockaddr == NULL ||
                ua->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            sin = (const struct sockaddr_in *)ua->Address.lpSockaddr;
            host = ntohl(sin->sin_addr.S_un.S_addr);

            b0 = (unsigned)((host >> 24) & 0xff);
            b1 = (unsigned)((host >> 16) & 0xff);
            b2 = (unsigned)((host >> 8) & 0xff);
            b3 = (unsigned)(host & 0xff);

            if (b0 == 127 || host == 0) {
                continue;               /* loopback / unspecified   */
            }
            if (b0 == 169 && b1 == 254) {
                continue;               /* APIPA, nothing routes there */
            }

            _snwprintf_s(out[count].ip, LTM_COUNTOF(out[count].ip), _TRUNCATE,
                         L"%u.%u.%u.%u", b0, b1, b2, b3);
            ltm_strlcpy_w(out[count].adapter, LTM_COUNTOF(out[count].adapter),
                          (ad->FriendlyName != NULL) ? ad->FriendlyName : L"");
            out[count].has_gateway = has_gw;
            count++;
        }
    }

    ltm_free(buf);

    if (count > 1) {
        qsort(out, (size_t)count, sizeof(ltm_netaddr), cmp_addr);
    }
    return count;
}
