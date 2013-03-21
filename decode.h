#ifndef DECODE_H
#define DECODE_H

#include "libavcodec/avcodec.h"
#include "libavutil/mathematics.h"
#include "libavformat/avformat.h"

struct decode_t
{
    AVCodecContext *pCtx;
    int  handle;
    uint8_t *pInbuf;
};


#endif /* DECODE_H */
