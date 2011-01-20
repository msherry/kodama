#ifndef _INTERFACE_TCP_H_
#define _INTERFACE_TCP_H_

void setup_tcp_connection(char *host, int port);
void tcp_connect(void);
void send_imo_message(const unsigned char *msg, int msg_len);

#endif
