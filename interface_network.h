#ifndef _INTERFACE_NETWORK_H_
#define _INTERFACE_NETWORK_H_

#include <glib.h>

#include "hybrid.h"
#include "kodama.h"

void setup_network_xmit(hybrid *h, gchar *host, hybrid_side side);
void setup_network_recv(hybrid *h, hybrid_side side);

#endif
