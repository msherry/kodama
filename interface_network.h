#ifndef _INTERFACE_NETWORK_H_
#define _INTERFACE_NETWORK_H_

#include <glib.h>

#include "hybrid.h"
#include "kodama.h"

void setup_network_out(hybrid *h, gchar *xmit);
void setup_network_in(hybrid *h, gchar *recv);

#endif
