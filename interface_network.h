#ifndef _INTERFACE_NETWORK_H_
#define _INTERFACE_NETWORK_H_

#include <glib.h>

#include "hybrid.h"
#include "kodama.h"

void setup_network_xmit(hybrid *h, gchar *xmit);
void setup_network_recv(hybrid *h);

#endif
