#include "mms_decode.h"
#include "buffer.h"
#include "asf.h"

#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>

static char *device = "/dev/dsp";

static int play_init(decode_t *p_dc)
{
    int ret = 0;
	
    if ((ret = p_dc->handle = open(device, O_WRONLY, 0)) == -1)
    {
        return ret;
    }

    if ((ret = ioctl(p_dc->handle, SNDCTL_DSP_SETFMT, &p_dc->pCtx->bits_per_coded_sample)) == -1)
    {
        return ret;
    }

    if ((ret = ioctl(p_dc->handle, SNDCTL_DSP_SPEED, &p_dc->pCtx->sample_rate)) == -1)
    {
        return ret;
    }

    if ((ret = ioctl(p_dc->handle, SNDCTL_DSP_CHANNELS, &p_dc->pCtx->channels)) == -1)
    {
        return ret;
    }
        
    return 0;
}


static int pcm_play(int handle, uint8_t *buf, int size)
{
    int ret = 0;

    ret = write(handle, buf, size);

    return ret;
}

#define MAX_AUDIO_FRAME_SIZE 400000

int mms_decode_init(decode_t *p_dc, uint8_t *header, int hdr_size)
{
	assert(p_dc != NULL  && header != NULL);
    
    int i;
    var_buffer_t buffer;
    guid_t guid;
    uint64_t i_size;

	av_register_all();

    p_dc->pInbuf = NULL;    
	p_dc->pCtx = avcodec_alloc_context();
	if (p_dc->pCtx == NULL)
		return -1;
	
    var_buffer_initread(&buffer, (uint8_t *)header, hdr_size);
    var_buffer_getguid(&buffer, &guid);

    if (!CmpGuid(&guid, &asf_object_header_guid))
    {
        return -1;
    }

    var_buffer_getmemory(&buffer, NULL, 30-16);

    for (;;)
    {
        var_buffer_getguid(&buffer, &guid);
        i_size = var_buffer_get64(&buffer);

        if (CmpGuid(&guid, &asf_object_stream_properties_guid))
        {
            int i_stream_id;
            guid_t stream_type;

            var_buffer_getguid(&buffer, &stream_type);
            var_buffer_getmemory(&buffer, NULL, 32);

            i_stream_id = var_buffer_get8(&buffer)&0x7f;
            var_buffer_get8(&buffer);
            var_buffer_get32(&buffer);

            // get codec paramter
            var_buffer_get16(&buffer);
            p_dc->pCtx->channels = var_buffer_get16(&buffer);
            p_dc->pCtx->sample_rate = var_buffer_get32(&buffer);
            p_dc->pCtx->bit_rate = var_buffer_get32(&buffer) * 8;
            p_dc->pCtx->block_align = var_buffer_get16(&buffer);
            p_dc->pCtx->bits_per_coded_sample = var_buffer_get16(&buffer);
            p_dc->pCtx->extradata_size = var_buffer_get16(&buffer);
            p_dc->pCtx->extradata = (uint8_t *)malloc(p_dc->pCtx->extradata_size);

            for (i=0; i<p_dc->pCtx->extradata_size; i++)
            {
                p_dc->pCtx->extradata[i] = var_buffer_get8(&buffer);
            }

            break;
        }
        else
        {
            var_buffer_getmemory(&buffer, NULL, i_size-24);
        }

        if (var_buffer_readempty(&buffer)) break;
    }
    
    
    AVCodec *pInCodec=NULL;
    pInCodec = avcodec_find_decoder(CODEC_ID_WMAV2);


    if (pInCodec == NULL)
    {
        return -1;
    } 

    if (avcodec_open(p_dc->pCtx, pInCodec) < 0)
    {
        return -1;
    }   

    if (play_init(p_dc) < 0)
    {
        return -1;
    }

    p_dc->pInbuf = (uint8_t *)malloc(MAX_AUDIO_FRAME_SIZE);
    
	
    return 0;
}

int mms_decode(decode_t *p_dc, uint8_t *media, int media_size)
{

    int len, pkt_size, frame_cnt;
    int out_size = MAX_AUDIO_FRAME_SIZE;
    uint8_t *pkt_data = NULL;
    assert(p_dc != NULL && (media != NULL && media_size > 0));
    pkt_size = p_dc->pCtx->block_align;
    pkt_data = (uint8_t *)malloc(pkt_size);
    
    memcpy(pkt_data, media+(media_size-pkt_size), pkt_size);
    memset(p_dc->pInbuf, 0, out_size);
    
    len = avcodec_decode_audio2(p_dc->pCtx, (short*)p_dc->pInbuf, &out_size, pkt_data, pkt_size);

    free(pkt_data);

    if (len < 0) return -1;
    if (len > 0)
    {
        if(pcm_play(p_dc->handle, p_dc->pInbuf, out_size) < 0)
           printf("pcm_play error!\n");
    }

    return 0;
}

int mms_decode_free(decode_t *p_dc)
{
    if (p_dc->pCtx) 
    {
        av_free(p_dc->pCtx);
        p_dc->pCtx = NULL;
    }
    
    if (p_dc->pInbuf) 
    {
        free(p_dc->pInbuf);
        p_dc->pInbuf = NULL;
    }

    close(p_dc->handle);
    return 0;
}

