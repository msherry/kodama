#ifndef _FLV_H_
#define _FLV_H_

void flv_parse_header(void);
void flv_parse_tag(const unsigned char *packet_data,
        const int packet_len);

#endif
