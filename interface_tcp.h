#ifndef _INTERFACE_TCP_H_
#define _INTERFACE_TCP_H_

struct imo_message;

void setup_tcp_connection(char *host, int port);
void tcp_connect(void);
void send_imo_message(struct imo_message *msg);

#endif
