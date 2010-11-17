#ifndef _INTERFACE_UDP_H_
#define _INTERFACE_UDP_H_

#include <glib.h>

void setup_udp_network_xmit(hybrid *h, gchar *host, int port, hybrid_side side);
void setup_udp_network_recv(hybrid *h, int port, hybrid_side side);

#endif
