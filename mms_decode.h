#ifndef MMS_DECODE_H
#define MMS_DECODE_H
#include "common.h"
#include "decode.h"

int mms_decode_init(decode_t *p_dc, uint8_t *header, int hdr_size);
int mms_decode(decode_t *p_dc, uint8_t *media, int media_size);
int mms_decode_free(decode_t *p_dc);



#endif /* MMS_DECODE_H */
