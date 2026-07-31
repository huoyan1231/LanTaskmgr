/*
 * netinfo.h - enumerate the IPv4 addresses this machine can be reached on.
 */
#ifndef LTM_NETINFO_H
#define LTM_NETINFO_H

#include "common.h"

#define LTM_MAX_ADDRS 16

typedef struct ltm_netaddr {
    WCHAR ip[16];       /* dotted quad */
    WCHAR adapter[96];  /* friendly name, for the UI */
    BOOL  has_gateway;  /* adapters with a gateway are listed first */
} ltm_netaddr;

/* Fills `out` with up to `cap` usable LAN addresses, best candidate first.
 * Link-local (169.254.x), loopback and disconnected adapters are skipped.
 * Returns the number written. */
int ltm_net_list_addresses(ltm_netaddr *out, int cap);

#endif /* LTM_NETINFO_H */
