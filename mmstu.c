#include "common.h"
#include "access.h"

#include <errno.h>

#include "network.h"
#include "url.h"
#include "asf.h"
#include "buffer.h"

#include "mms.h"
#include "mmstu.h"
#include "mms_decode.h"

#undef MMS_DEBUG

/****************************************************************************
 * NOTES:
 *  MMSProtocole documentation found at http://get.to/sdp
 ****************************************************************************/

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
int   MMSTUOpen   ( access_t * );
void  MMSTUClose  ( access_t * );

static block_t *Block( access_t *);

static int  MMSOpen ( access_t *, url_t *, int );
static int  MMSStart( access_t *, uint32_t );
static int  MMSStop ( access_t * );
static void MMSClose( access_t * );


static int  mms_CommandRead( access_t *p_access, int i_command1, int i_command2 );
static int  mms_CommandSend( access_t *, int, uint32_t, uint32_t, uint8_t *, int );

static int  mms_HeaderMediaRead( access_t *, int );

static int  mms_ReceivePancket( access_t * );

static void KeepAliveStart( access_t * );
static void KeepAliveStop( access_t * );

/***********************************************************************
*  Functions Implementation
***********************************************************************/

static block_t *Block(access_t *p_access)
{
    while(mms_HeaderMediaRead( p_access, MMS_PACKET_MEDIA ) == MMS_PACKET_MEDIA)
    {
        mms_decode(p_access->dc, p_access->p_sys->p_media, p_access->p_sys->i_media);
    }
       
    return NULL;
}

int  MMSTUOpen( access_t *p_access )
{
    access_sys_t   *p_sys;
    int             i_proto;
    int             i_status;

    /* Set up p_access */
    access_InitFields( p_access );
    
    //p_access->pf_read = NULL;
    p_access->pf_block = Block;
    p_access->dc = (decode_t *)malloc(sizeof(decode_t));
    p_access->dc->pCtx = NULL;
    p_access->dc->pInbuf = NULL;
    //p_access->pf_control = Control;
    //p_access->pf_seek = Seek;
    
    p_access->p_sys = p_sys = calloc( 1, sizeof( access_sys_t ) );
    if( !p_sys ) return VLC_ENOMEM;

    p_sys->i_timeout = 3000000;

    // vlc_mutex_init( &p_sys->lock_netwrite );

    /* *** Parse URL and get server addr/port and path *** */
    ct_UrlParse( &p_sys->url, p_access->psz_path, 0 );
    if( p_sys->url.psz_host == NULL || *p_sys->url.psz_host == '\0' )
    {
        msg_Err( p_access, "invalid server name" );
        ct_UrlClean( &p_sys->url );
	//  vlc_mutex_destroy( &p_sys->lock_netwrite );
        free( p_sys );
        return VLC_EGENERIC;
    }
    if( p_sys->url.i_port <= 0 )
    {
        p_sys->url.i_port = 1755;
    }
    

    /* *** connect to this server *** */
    /* look at  requested protocol (udp/tcp) */
    i_proto = MMS_PROTO_TCP;
    if( *p_access->psz_access )
    {
        if( !strncmp( p_access->psz_access, "mmsu", 4 ) )
        {
            i_proto = MMS_PROTO_UDP;
        }
        else if( !strncmp( p_access->psz_access, "mmst", 4 ) )
        {
            i_proto = MMS_PROTO_TCP;
        }
    }

    /* connect */
    if( i_proto == MMS_PROTO_AUTO )
    {   /* first try with TCP and then UDP*/
        if( ( i_status = MMSOpen( p_access, &p_sys->url, MMS_PROTO_TCP ) ) )
        {
          //  if( !p_access->b_die )
                i_status = MMSOpen( p_access, &p_sys->url, MMS_PROTO_UDP );
        }
    }
    else
    {
        i_status = MMSOpen( p_access, &p_sys->url, i_proto );
    }

    if( i_status )
    {
        msg_Err( p_access, "cannot connect to server" );
        ct_UrlClean( &p_sys->url );
	// vlc_mutex_destroy( &p_sys->lock_netwrite );
        free( p_sys );
        return VLC_EGENERIC;
    }

    msg_Dbg( p_access, "connected to %s:%d", p_sys->url.psz_host, p_sys->url.i_port );
    /*
     * i_flags_broadcast
     *  yy xx ?? ??
     *  broadcast    yy=0x02, xx= 0x00
     *  pre-recorded yy=0x01, xx= 0x80 if video, 0x00 no video
     */
    if( p_sys->i_packet_count <= 0 && p_sys->asfh.i_data_packets_count > 0 )
    {
        p_sys->i_packet_count = p_sys->asfh.i_data_packets_count;
    }
    if( p_sys->i_packet_count <= 0 || ( p_sys->i_flags_broadcast >> 24 ) == 0x02 )
    {
        p_sys->b_seekable = false;
    }
    else
    {
        p_sys->b_seekable = true;
        p_access->info.i_size =
            (uint64_t)p_sys->i_header +
            (uint64_t)p_sys->i_packet_count * (uint64_t)p_sys->i_packet_length;
    }
    p_sys->b_keep_alive = false;

    /* *** Start stream *** */
    if( MMSStart( p_access, 0xffffffff ) < 0 )
    {
        msg_Err( p_access, "cannot start stream" );
        MMSTUClose ( p_access );
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: free unused data structures
 *****************************************************************************/
void MMSTUClose( access_t *p_access )
{
    access_sys_t *p_sys = p_access->p_sys;
    
    KeepAliveStop( p_access );

    /* close connection with server */
    MMSClose( p_access );

    /* free memory */
    ct_UrlClean( &p_sys->url );

    free(p_access->dc);
    free( p_sys );
}


/****************************************************************************
 * MMSOpen : Open a connection with the server over mmst or mmsu
 ****************************************************************************/
static int MMSOpen( access_t  *p_access, url_t *p_url, int  i_proto )
{
    access_sys_t *p_sys = p_access->p_sys;
    int           b_udp = ( i_proto == MMS_PROTO_UDP ) ? 1 : 0;

    var_buffer_t buffer;
    char         tmp[4096];
    uint16_t     *p;
   //int          i_server_version;
   // int          i_tool_version;
   // int          i_update_player_url;
   // int          i_encryption_type;
    int          i;
    int          i_streams;
    int          i_first;
    char         *mediapath;
        
    /* *** Open a TCP connection with server *** */
    msg_Dbg( p_access, "waiting for connection..." );
      
    p_sys->i_handle_tcp = net_ConnectTCP( p_url->psz_host, p_url->i_port );
    if( p_sys->i_handle_tcp < 0 )
    {
        msg_Err( p_access, "failed to open a connection (tcp)" );
        return VLC_EGENERIC;
    }
    msg_Info(p_access, "connection(tcp) with \"%s:%d\" successful\n",
             p_url->psz_host,
             p_url->i_port );

    /* *** Bind port if UDP protocol is selected *** */
   if( b_udp )
    {
        //if( net_GetSockAddress( p_sys->i_handle_tcp, p_sys->sz_bind_addr,
//                                NULL ) )
//        {
//            net_Close( p_sys->i_handle_tcp );
//            return VLC_EGENERIC;
//        }
//
//        p_sys->i_handle_udp = net_ListenUDP1( (vlc_object_t *)p_access, p_sys->sz_bind_addr,
//                                              7000 );
//        if( p_sys->i_handle_udp < 0 )
//        {
//            msg_Err( p_access, "failed to open a connection (udp)" );
//            net_Close( p_sys->i_handle_tcp );
//            return VLC_EGENERIC;
//        }
//        msg_Dbg( p_access,
//                 "connection(udp) at \"%s:%d\" successful",
//                 p_sys->sz_bind_addr, 7000 );
    } 

    /* *** Init context for mms prototcol *** */
    GenerateGuid ( &p_sys->guid );    /* used to identify client by server */
    msg_Dbg( p_access,
             "generated guid: "GUID_FMT,
             GUID_PRINT( p_sys->guid ) );
    
    
    
    p_sys->i_command_level = 1;          /* updated after 0x1A command */
    p_sys->i_seq_num = 0;
    p_sys->i_media_packet_id_type  = 0x04;
    p_sys->i_header_packet_id_type = 0x02;
    p_sys->i_proto = i_proto;
    p_sys->i_packet_seq_num = 0;
    p_sys->p_header = NULL;
    p_sys->i_header = 0;
    p_sys->p_media = NULL;
    p_sys->i_media = 0;
    p_sys->i_media_used = 0;

    p_access->info.i_pos = 0;
    p_sys->i_buffer_tcp = 0;
    p_sys->i_buffer_udp = 0;
    p_sys->p_cmd = NULL;
    p_sys->i_cmd = 0;
    p_access->info.b_eof = false;

    /* *** send command 1 : connection request *** */
    var_buffer_initwrite( &buffer, 0 );
    var_buffer_add16( &buffer, 0x001c );
    var_buffer_add16( &buffer, 0x0003 );
    sprintf( tmp,
             "NSPlayer/7.0.0.1956; {"GUID_FMT"}; Host: %s",
             GUID_PRINT( p_sys->guid ),
             p_url->psz_host );
    var_buffer_addUTF16( &buffer, tmp );

    mms_CommandSend( p_access,
                     0x01,          /* connexion request */
                     0x00000000,    /* flags, FIXME */
                     0x0004000b,    /* ???? */
                     buffer.p_data,
                     buffer.i_data );

    if( mms_CommandRead( p_access, 0x01, 0 ) < 0 )
    {
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return VLC_EGENERIC;
    }

    int i_server_version = GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 32 );
    int i_tool_version = GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 36 );
    int i_update_player_url = GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 40 );
    int i_encryption_type = GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 44 );
    p = (uint16_t*)( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 48 );
#define GETUTF16( psz, size ) \
    { \
        int i; \
        psz = malloc( size + 1); \
        for( i = 0; i < size; i++ ) \
        { \
            psz[i] = p[i]; \
        } \
        psz[size] = '\0'; \
        p += ( size ); \
    }
    GETUTF16( p_sys->psz_server_version, i_server_version );
    GETUTF16( p_sys->psz_tool_version, i_tool_version );
    GETUTF16( p_sys->psz_update_player_url, i_update_player_url );
    GETUTF16( p_sys->psz_encryption_type, i_encryption_type );
#undef GETUTF16
   
    msg_Dbg( p_access,
             "0x01 --> server_version:\"%s\" tool_version:\"%s\" update_player_url:\"%s\" encryption_type:\"%s\"",
             p_sys->psz_server_version,
             p_sys->psz_tool_version,
             p_sys->psz_update_player_url,
             p_sys->psz_encryption_type );
     
    /* *** should make an 18 command to make data timing *** */

    /* *** send command 2 : transport protocol selection *** */
    var_buffer_reinitwrite( &buffer, 0 );
    var_buffer_add32( &buffer, 0x00000000 );
    var_buffer_add32( &buffer, 0x000a0000 );
    var_buffer_add32( &buffer, 0x00000002 );
    if( b_udp )
    {
        sprintf( tmp,
                 "\\\\%s\\UDP\\%d",
                 p_sys->sz_bind_addr,
                 7000 ); // FIXME
    }
    else
    {
        sprintf( tmp, "\\\\192.168.0.1\\TCP\\1242"  );
    }
    
    var_buffer_addUTF16( &buffer, tmp );
    var_buffer_add16( &buffer, '0' );
    
    mms_CommandSend( p_access,
                     0x02,          /* connexion request */
                     0x00000000,    /* flags, FIXME */
                     0xffffffff,    /* ???? */
                     buffer.p_data,
                     buffer.i_data );

    /* *** response from server, should be 0x02 or 0x03 *** */
    mms_CommandRead( p_access, 0x02, 0x03 );
    
    if( p_sys->i_command == 0x03 )
    {
        msg_Err( p_access,
                 "%s protocol selection failed", b_udp ? "UDP" : "TCP" );
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return VLC_EGENERIC;
    }
    else if( p_sys->i_command != 0x02 )
    {
        msg_Warn( p_access, "received command isn't 0x02 in reponse to 0x02" );
    }

    /* *** send command 5 : media file name/path requested *** */
    var_buffer_reinitwrite( &buffer, 0 );
    var_buffer_add64( &buffer, 0 );

    /* media file path shouldn't start with / character */
    mediapath = p_url->psz_path;
    if ( *mediapath == '/' )
    {
        mediapath++;
    }
    var_buffer_addUTF16( &buffer, mediapath );

    mms_CommandSend( p_access,
                     0x05,
                     p_sys->i_command_level,
                     0xffffffff,
                     buffer.p_data,
                     buffer.i_data );

    /* *** wait for reponse *** */
    mms_CommandRead( p_access, 0x1a, 0x06 );

    /* test if server send 0x1A answer */
    if( p_sys->i_command == 0x1A )
    {
        msg_Err( p_access, "id/password requested (not yet supported)" );
        /*  FIXME */
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return VLC_EGENERIC;
    }
    if( p_sys->i_command != 0x06 )
    {
        msg_Err( p_access,
                 "unknown answer (0x%x instead of 0x06)",
                 p_sys->i_command );
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return( -1 );
    }
     
    /*  1 for file ok, 2 for authen ok */
    switch( GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE ) )
    {
        case 0x0001:
            msg_Dbg( p_access, "media file name/path accepted" );
            break;
        case 0x0002:
            msg_Dbg( p_access, "authentication accepted" );
            break;
        case -1:
        default:
        msg_Err( p_access, "error while asking for file %d",
                 GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE ) );
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return VLC_EGENERIC;
    }
   
    
    p_sys->i_flags_broadcast =
        GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 12 );
    p_sys->i_media_length =
        GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 24 );
    p_sys->i_packet_length =
        GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 44 );
    p_sys->i_packet_count =
        GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 48 );
    p_sys->i_max_bit_rate =
        GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 56 );
    p_sys->i_header_size =
        GetDWLE( p_sys->p_cmd + MMS_CMD_HEADERSIZE + 60 );

    msg_Dbg( p_access,
             "answer 0x06 flags:0x%8.8"PRIx32" media_length:%"PRIu32"s "
             "packet_length:%zu packet_count:%"PRIu32" max_bit_rate:%d "
             "header_size:%zu",
             p_sys->i_flags_broadcast,
             p_sys->i_media_length,
             p_sys->i_packet_length,
             p_sys->i_packet_count,
             p_sys->i_max_bit_rate,
             p_sys->i_header_size );
    

    /* *** send command 15 *** */

    var_buffer_reinitwrite( &buffer, 0 );
    var_buffer_add32( &buffer, 0 );
    var_buffer_add32( &buffer, 0x8000 );
    var_buffer_add32( &buffer, 0xffffffff );
    var_buffer_add32( &buffer, 0x00 );
    var_buffer_add32( &buffer, 0x00 );
    var_buffer_add32( &buffer, 0x00 );
    var_buffer_add64( &buffer, (((uint64_t)0x40ac2000)<<32) );
    var_buffer_add32( &buffer, p_sys->i_header_packet_id_type );
    var_buffer_add32( &buffer, 0x00 );
    mms_CommandSend( p_access, 0x15, p_sys->i_command_level, 0x00,
                     buffer.p_data, buffer.i_data );

    /* *** wait for reponse *** */
    /* Commented out because it fails on some stream (no 0x11 answer) */
#if 0
    mms_CommandRead( p_access, 0x11, 0 );

    if( p_sys->i_command != 0x11 )
    {
        msg_Err( p_access,
                 "unknown answer (0x%x instead of 0x11)",
                 p_sys->i_command );
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return( -1 );
    }
#endif

    /* *** now read header packet *** */
    /* XXX could be split over multiples packets */
    msg_Dbg( p_access, "reading header" );
    for( ;; )
    {
        if( mms_HeaderMediaRead( p_access, MMS_PACKET_HEADER ) < 0 )
        {
            msg_Err( p_access, "cannot receive header" );
            var_buffer_free( &buffer );
            MMSClose( p_access );
            return VLC_EGENERIC;
        }
        if( p_sys->i_header >= p_sys->i_header_size )
        {
            msg_Dbg( p_access,
                     "header complete(%zu)",
                     p_sys->i_header );
            break;
        }
        msg_Dbg( p_access,
                 "header incomplete (%zu/%zu), reading more",
                 p_sys->i_header,
                 p_sys->i_header_size );
    }

    /* *** parse header and get stream and their id *** */
    /* get all streams properties,
     *
     * TODO : stream bitrates properties(optional)
     *        and bitrate mutual exclusion(optional) */
     asf_HeaderParse ( &p_sys->asfh,
                           p_sys->p_header, p_sys->i_header );
     asf_StreamSelect( &p_sys->asfh,
                           p_sys->i_max_bit_rate,
                           true, true, false);

    /* *** now select stream we want to receive *** */
    /* TODO take care of stream bitrate TODO */
    i_streams = 0;
    i_first = -1;
    var_buffer_reinitwrite( &buffer, 0 );
    /* for now, select first audio and video stream */
    for( i = 1; i < 128; i++ )
    {

        if( p_sys->asfh.stream[i].i_cat != ASF_STREAM_UNKNOWN )
        {
            i_streams++;
            if( i_first != -1 )
            {
                var_buffer_add16( &buffer, 0xffff );
                var_buffer_add16( &buffer, i );
            }
            else
            {
                i_first = i;
            }
            if( p_sys->asfh.stream[i].i_selected )
            {
                var_buffer_add16( &buffer, 0x0000 );
                msg_Info( p_access,
                          "selecting stream[0x%x] %s (%d Kib/s)",
                          i,
                          ( p_sys->asfh.stream[i].i_cat == ASF_STREAM_AUDIO  ) ?
                                                  "audio" : "video" ,
                          p_sys->asfh.stream[i].i_bitrate / 1024);
            }
            else
            {
                var_buffer_add16( &buffer, 0x0002 );
                msg_Info( p_access,
                          "ignoring stream[0x%x] %s (%d Kib/s)",
                          i,
                          ( p_sys->asfh.stream[i].i_cat == ASF_STREAM_AUDIO  ) ?
                                    "audio" : "video" ,
                          p_sys->asfh.stream[i].i_bitrate / 1024);

            }
        }
    }

    if( i_streams == 0 )
    {
        msg_Err( p_access, "cannot find any stream" );
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return VLC_EGENERIC;
    }
    
    if (mms_decode_init(p_access->dc, p_access->p_sys->p_header, p_access->p_sys->i_header) < 0)
    {
        var_buffer_free(&buffer);
        MMSClose(p_access);

        return VLC_EGENERIC;
    }

    mms_CommandSend( p_access, 0x33,
                     i_streams,
                     0xffff | ( i_first << 16 ),
                     buffer.p_data, buffer.i_data );

    mms_CommandRead( p_access, 0x21, 0 );
    if( p_sys->i_command != 0x21 )
    {
        msg_Err( p_access,
                 "unknown answer (0x%x instead of 0x21)",
                 p_sys->i_command );
        var_buffer_free( &buffer );
        MMSClose( p_access );
        return VLC_EGENERIC;
    }


    var_buffer_free( &buffer );

    msg_Info( p_access, "connection successful" );

    return VLC_SUCCESS;
}

/****************************************************************************
 * MMSStart : Start streaming
 ****************************************************************************/
static int MMSStart( access_t  *p_access, uint32_t i_packet )
{
    access_sys_t        *p_sys = p_access->p_sys;
    var_buffer_t    buffer;

    /* *** start stream from packet 0 *** */
    var_buffer_initwrite( &buffer, 0 );
    var_buffer_add64( &buffer, 0 ); /* seek point in second */
    var_buffer_add32( &buffer, 0xffffffff );
    var_buffer_add32( &buffer, i_packet ); // begin from start
    var_buffer_add8( &buffer, 0xff ); // stream time limit
    var_buffer_add8( &buffer, 0xff ); //  on 3bytes ...
    var_buffer_add8( &buffer, 0xff ); //
    var_buffer_add8( &buffer, 0x00 ); // don't use limit
    var_buffer_add32( &buffer, p_sys->i_media_packet_id_type );

    mms_CommandSend( p_access, 0x07, p_sys->i_command_level, 0x0001ffff,
                     buffer.p_data, buffer.i_data );

    var_buffer_free( &buffer );

    mms_CommandRead( p_access, 0x05, 0 );

    if( p_sys->i_command != 0x05 )
    {
        msg_Err( p_access,
                 "unknown answer (0x%x instead of 0x05)",
                 p_sys->i_command );
        return -1;
    }
    else
    {
        /* get a packet */
        if( mms_HeaderMediaRead( p_access, MMS_PACKET_MEDIA ) < 0 )
            return -1;
        msg_Dbg( p_access, "streaming started" );
        
        return 0;
    }
}

/****************************************************************************
 * MMSStop : Stop streaming
 ****************************************************************************/
static int MMSStop( access_t  *p_access )
{
    access_sys_t *p_sys = p_access->p_sys;

    /* *** stop stream but keep connection alive *** */
    mms_CommandSend( p_access,
                     0x09,
                     p_sys->i_command_level,
                     0x001fffff,
                     NULL, 0 );
    return( 0 );
}

/****************************************************************************
 * MMSClose : Close streaming and connection
 ****************************************************************************/
 static void MMSClose( access_t  *p_access )
{
    access_sys_t        *p_sys = p_access->p_sys;

    msg_Dbg( p_access, "Connection closed" );

    /* *** tell server that we will disconnect *** */
    mms_CommandSend( p_access,
                     0x0d,
                     p_sys->i_command_level,
                     0x00000001,
                     NULL, 0 );

    /* *** close sockets *** */
    net_Close( p_sys->i_handle_tcp );
    if( p_sys->i_proto == MMS_PROTO_UDP )
    {
        net_Close( p_sys->i_handle_udp );
    }
   
    mms_decode_free(p_access->dc); 

    FREENULL( p_sys->p_cmd );
    FREENULL( p_sys->p_media );
    FREENULL( p_sys->p_header );

    FREENULL( p_sys->psz_server_version );
    FREENULL( p_sys->psz_tool_version );
    FREENULL( p_sys->psz_update_player_url );
    FREENULL( p_sys->psz_encryption_type );
}

/****************************************************************************
 *
 * MMS specific functions
 *
 ****************************************************************************/
static int mms_CommandSend( access_t *p_access, int i_command,
                            uint32_t i_prefix1, uint32_t i_prefix2,
                            uint8_t *p_data, int i_data_old )
{
    var_buffer_t buffer;
    access_sys_t *p_sys = p_access->p_sys;
    int i_data_by8, i_ret;
    int i_data = i_data_old;

    while( i_data & 0x7 ) i_data++;
    i_data_by8 = i_data >> 3;

    /* first init buffer */
    var_buffer_initwrite( &buffer, 0 );

    var_buffer_add32( &buffer, 0x00000001 );    /* start sequence */
    var_buffer_add32( &buffer, 0xB00BFACE );
    /* size after protocol type */
    var_buffer_add32( &buffer, i_data + MMS_CMD_HEADERSIZE - 16 );
    var_buffer_add32( &buffer, 0x20534d4d );    /* protocol "MMS " */
    var_buffer_add32( &buffer, i_data_by8 + 4 );
    var_buffer_add32( &buffer, p_sys->i_seq_num ); p_sys->i_seq_num++;
    var_buffer_add64( &buffer, 0 );
    var_buffer_add32( &buffer, i_data_by8 + 2 );
    var_buffer_add32( &buffer, 0x00030000 | i_command ); /* dir | command */
    var_buffer_add32( &buffer, i_prefix1 );    /* command specific */
    var_buffer_add32( &buffer, i_prefix2 );    /* command specific */

    /* specific command data */
    if( p_data && i_data > 0 )
    {
        var_buffer_addmemory( &buffer, p_data, i_data_old );
    }

    /* Append padding to the command data */
    var_buffer_add64( &buffer, 0 );

    /* send it */
    // vlc_mutex_lock( &p_sys->lock_netwrite );
    i_ret = net_Write( p_sys->i_handle_tcp, buffer.p_data,
                       buffer.i_data - ( 8 - ( i_data - i_data_old ) ) );
    // vlc_mutex_unlock( &p_sys->lock_netwrite );

    if( i_ret != buffer.i_data - ( 8 - ( i_data - i_data_old ) ) )
    {
        var_buffer_free( &buffer );
        msg_Err( p_access, "failed to send command" );
        return VLC_EGENERIC;
    }

    var_buffer_free( &buffer );
    return VLC_SUCCESS;
}

static int NetFillBuffer( access_t *p_access )
{
    access_sys_t    *p_sys = p_access->p_sys;
    int             i_ret;
    fd_set readfds;
    struct timeval timeout;

    /* FIXME when using udp */
    int i_tcp, i_udp;
    int i_tcp_read, i_udp_read;
    int i_try;

    i_tcp = MMS_BUFFER_SIZE/2 - p_sys->i_buffer_tcp;  // MMS_BUFFER_SIZE > p_sys->i_buffer_tcp(5500) * 2 

    if( p_sys->i_proto == MMS_PROTO_UDP )
    {
        i_udp = MMS_BUFFER_SIZE/2 - p_sys->i_buffer_udp;
    }
    else
    {
        i_udp = 0;  /* there isn't udp socket */
    }

    if( ( i_udp <= 0 ) && ( i_tcp <= 0 ) )
    {
        msg_Warn( p_access, "nothing to read %d:%d", (int)i_tcp, (int)i_udp );
        return 0;
    }
    else
    {
        /* msg_Warn( p_access, "ask for tcp:%d udp:%d", i_tcp, i_udp ); */
    }

    i_try = 0;
    /* Find if some data is available */
    do
    {
        i_try++;

        /* Initialize file descriptor set */
       // memset (ufd, 0, sizeof (ufd));
       // nfd = 0;
       FD_ZERO(&readfds);
       timeout.tv_sec = 0;
       timeout.tv_usec = 500000;
       
       if( p_sys->i_proto == MMS_PROTO_TCP )
           FD_SET(p_sys->i_handle_tcp, &readfds);
       else
           FD_SET(p_sys->i_handle_udp, &readfds); 


        if( (i_try * (timeout.tv_sec*1000000+timeout.tv_usec)) > p_sys->i_timeout )
        {
            msg_Err(p_access, "no data received");
            return -1;
        }

        if( i_try > 3 && (p_sys->i_buffer_tcp > 0 || p_sys->i_buffer_udp > 0) )
        {
            return -1;
        }

       // if( !vlc_object_alive (p_access) )
//            return -1;

        //msg_Dbg( p_access, "NetFillBuffer: trying again (select)" );

    } while( !(i_ret = select(p_sys->i_handle_tcp+1, &readfds, (fd_set*)0, (fd_set*)0, &timeout)) ||
             (i_ret < 0 && errno == EINTR) );

    if( i_ret < 0 )
    {
        msg_Err( p_access, "network select error (%m)");
        return i_ret;
    }

    i_tcp_read = i_udp_read = 0;

    if( ( i_tcp > 0 ) && FD_ISSET(p_sys->i_handle_tcp, &readfds))
    {
        i_tcp_read =
            recv( p_sys->i_handle_tcp,
                  p_sys->buffer_tcp + p_sys->i_buffer_tcp,
                  i_tcp + MMS_BUFFER_SIZE/2, 0 );
    }

    if( i_udp > 0 && FD_ISSET(p_sys->i_handle_udp, &readfds))
    {
        i_udp_read = recv( p_sys->i_handle_udp,
                           p_sys->buffer_udp + p_sys->i_buffer_udp,
                           i_udp + MMS_BUFFER_SIZE/2, 0 );
    }


#ifdef MMS_DEBUG
    if( p_sys->i_proto == MMS_PROTO_UDP )
    {
        msg_Dbg( p_access, "filling buffer TCP:%d+%d UDP:%d+%d",
                 p_sys->i_buffer_tcp, i_tcp_read,
                 p_sys->i_buffer_udp, i_udp_read );
    }
    else
    {
        msg_Dbg( p_access, "filling buffer TCP:%d+%d",
                 p_sys->i_buffer_tcp, i_tcp_read );
    }
#endif

    if( i_tcp_read > 0 ) p_sys->i_buffer_tcp += i_tcp_read;
    if( i_udp_read > 0 ) p_sys->i_buffer_udp += i_udp_read;

    return i_tcp_read + i_udp_read;
}

static int  mms_ParseCommand( access_t *p_access,
                              uint8_t *p_data,
                              size_t i_data,
                              int *pi_used )
{
 #define GET32( i_pos ) \
    ( p_sys->p_cmd[i_pos] + ( p_sys->p_cmd[i_pos +1] << 8 ) + \
      ( p_sys->p_cmd[i_pos + 2] << 16 ) + \
      ( p_sys->p_cmd[i_pos + 3] << 24 ) )

    access_sys_t        *p_sys = p_access->p_sys;
    uint32_t    i_length;
    uint32_t    i_id;

    free( p_sys->p_cmd );
    p_sys->i_cmd = i_data;
    p_sys->p_cmd = malloc( i_data );
    memcpy( p_sys->p_cmd, p_data, i_data );

    *pi_used = i_data; /* by default */

    if( i_data < MMS_CMD_HEADERSIZE )
    {
        msg_Warn( p_access, "truncated command (header incomplete)" );
        p_sys->i_command = 0;
        return -1;
    }
    i_id =  GetDWLE( p_data + 4 );
    i_length = GetDWLE( p_data + 8 ) + 16;

    if( i_id != 0xb00bface || i_length < 16 )
    {
        /*msg_Err( p_access,
                 "incorrect command header (0x%"PRIx32")", i_id );*/
        p_sys->i_command = 0;
        return -1;
    }

    if( i_length > p_sys->i_cmd )
    {
        msg_Warn( p_access,
                  "truncated command (missing %zu bytes)",
                   (size_t)i_length - i_data  );
        p_sys->i_command = 0;
        return -1;
    }
    else if( i_length < p_sys->i_cmd )
    {
        p_sys->i_cmd = i_length;
        *pi_used = i_length;
    }
   
    msg_Dbg( p_access,
             "recv command start_sequence:0x%8.8x command_id:0x%8.8x length:%d len8:%d sequence 0x%8.8x len8_II:%d dir_comm:0x%8.8x",
             GET32( 0 ),
             GET32( 4 ),
             GET32( 8 ),
             /* 12: protocol type "MMS " */
             GET32( 16 ),
             GET32( 20 ),
             /* 24: unknown (0) */
             /* 28: unknown (0) */
             GET32( 32 ),
             GET32( 36 )
             /* 40: switches */
             /* 44: extra */ );

    p_sys->i_command = GET32( 36 ) & 0xffff;
#undef GET32

    return MMS_PACKET_CMD;
}

static int  mms_ParsePacket( access_t *p_access,
                             uint8_t *p_data, size_t i_data,
                             int *pi_used )
{
    access_sys_t        *p_sys = p_access->p_sys;
    int i_packet_seq_num;
    size_t i_packet_length;
    uint32_t i_packet_id;

    *pi_used = i_data; /* default */
    if( i_data <= 8 )
    {
        msg_Warn( p_access, "truncated packet (header incomplete)" );
        return -1;
    }

    i_packet_id = p_data[4];
    i_packet_seq_num = GetDWLE( p_data );
    i_packet_length = GetWLE( p_data + 6 );

    msg_Info( p_access, 
    "------->i_packet_length=%d, i_data=%d", i_packet_length, i_data );

    if( i_packet_length > i_data || i_packet_length <= 8)
    {
        msg_Dbg( p_access,
                 "truncated packet (Declared %d bytes, Actual %d bytes)\n",
                 i_packet_length, i_data  );
        *pi_used = 0;
        return -1;
    }
    else if( i_packet_length < i_data )
    {
        *pi_used = i_packet_length;
    }

    if( i_packet_id == 0xff )
    {
        msg_Warn( p_access,
                  "receive MMS UDP pair timing" );
        return( MMS_PACKET_UDP_TIMING );
    }

    if( i_packet_id != p_sys->i_header_packet_id_type &&
        i_packet_id != p_sys->i_media_packet_id_type )
    {
        msg_Warn( p_access, "incorrect Packet Id Type (0x%x)", i_packet_id );
        return -1;
    }

    /* we now have a media or a header packet */
    if( i_packet_seq_num != p_sys->i_packet_seq_num )
    {
#if 0
        /* FIXME for udp could be just wrong order ? */
        msg_Warn( p_access,
                  "detected packet lost (%d != %d)",
                  i_packet_seq_num,
                  p_sys->i_packet_seq_num );
#endif
    }
    p_sys->i_packet_seq_num = i_packet_seq_num + 1;

    if( i_packet_id == p_sys->i_header_packet_id_type )
    {
        if( p_sys->p_header )
        {
            p_sys->p_header = realloc( p_sys->p_header,
                                      p_sys->i_header + i_packet_length - 8 );
            memcpy( &p_sys->p_header[p_sys->i_header],
                    p_data + 8, i_packet_length - 8 );
            p_sys->i_header += i_packet_length - 8;

        }
        else
        {
            uint8_t* p_packet = malloc( i_packet_length - 8 ); // don't bother with preheader
            memcpy( p_packet, p_data + 8, i_packet_length - 8 );
            p_sys->p_header = p_packet;
            p_sys->i_header = i_packet_length - 8;
        }
        msg_Dbg( p_access,
                 "receive header packet (%d bytes)",
                 i_packet_length - 8 ); 

        return MMS_PACKET_HEADER;
    }
    else
    {
        uint8_t* p_packet = malloc( i_packet_length - 8 ); // don't bother with preheader
        memcpy( p_packet, p_data + 8, i_packet_length - 8 );
        FREENULL( p_sys->p_media );
        p_sys->p_media = p_packet;
        p_sys->i_media = i_packet_length - 8;
        p_sys->i_media_used = 0;
        msg_Dbg( p_access,
                 "receive media packet (%d bytes)",
                 i_packet_length - 8 ); 

        return MMS_PACKET_MEDIA;
    }
}

static int mms_ReceivePacket( access_t *p_access )
{
    access_sys_t *p_sys = p_access->p_sys;
    int i_packet_tcp_type;
    int i_packet_udp_type;

    for( ;; )
    {
        bool b_refill = true;

        /* first if we need to refill buffer */
        if( p_sys->i_buffer_tcp >= MMS_CMD_HEADERSIZE )
        {
            if( GetDWLE( p_sys->buffer_tcp + 4 ) == 0xb00bface  )
            {
                if( GetDWLE( p_sys->buffer_tcp + 8 ) + 16 <=
                    (uint32_t)p_sys->i_buffer_tcp )
                {
                    b_refill = false;
                }
            }
            else if( GetWLE( p_sys->buffer_tcp + 6 ) <= p_sys->i_buffer_tcp )
            {
                b_refill = false;
            }
        }
        if( p_sys->i_proto == MMS_PROTO_UDP && p_sys->i_buffer_udp >= 8 &&
            GetWLE( p_sys->buffer_udp + 6 ) <= p_sys->i_buffer_udp )
        {
            b_refill = false;
        }

        if( b_refill && NetFillBuffer( p_access ) < 0 )
        {
            msg_Warn( p_access, "cannot fill buffer" );
            return -1;
        }

        i_packet_tcp_type = -1;
        i_packet_udp_type = -1;

        if( p_sys->i_buffer_tcp > 0 )
        {
            int i_used;

            if( GetDWLE( p_sys->buffer_tcp + 4 ) == 0xb00bface )
            {
                i_packet_tcp_type =
                    mms_ParseCommand( p_access, p_sys->buffer_tcp,
                                      p_sys->i_buffer_tcp, &i_used );

            }
            else
            {
                i_packet_tcp_type =
                    mms_ParsePacket( p_access, p_sys->buffer_tcp,
                                     p_sys->i_buffer_tcp, &i_used );
            }
            if( i_used > 0 && i_used < MMS_BUFFER_SIZE )
            {
                memmove( p_sys->buffer_tcp, p_sys->buffer_tcp + i_used,
                         MMS_BUFFER_SIZE - i_used );
            }
            p_sys->i_buffer_tcp -= i_used;
        }
        else if( p_sys->i_buffer_udp > 0 )
        {
            int i_used;

            i_packet_udp_type =
                mms_ParsePacket( p_access, p_sys->buffer_udp,
                                 p_sys->i_buffer_udp, &i_used );

            if( i_used > 0 && i_used < MMS_BUFFER_SIZE )
            {
                memmove( p_sys->buffer_udp, p_sys->buffer_udp + i_used,
                         MMS_BUFFER_SIZE - i_used );
            }
            p_sys->i_buffer_udp -= i_used;
        }

        if( i_packet_tcp_type == MMS_PACKET_CMD && p_sys->i_command == 0x1b )
        {
            mms_CommandSend( p_access, 0x1b, 0, 0, NULL, 0 );
            i_packet_tcp_type = -1;
        }

        if( i_packet_tcp_type != -1 )
        {
            return i_packet_tcp_type;
        }
        else if( i_packet_udp_type != -1 )
        {
            return i_packet_udp_type;
        }
    }
}

static int mms_ReceiveCommand( access_t *p_access )
{
    access_sys_t *p_sys = p_access->p_sys;

    for( ;; )
    {
        int i_used;
        int i_status;

        if( NetFillBuffer( p_access ) < 0 )
        {
            msg_Warn( p_access, "cannot fill buffer" );
            return VLC_EGENERIC;
        }
        if( p_sys->i_buffer_tcp > 0 )
        {
            i_status = mms_ParseCommand( p_access, p_sys->buffer_tcp,
                                         p_sys->i_buffer_tcp, &i_used );
            if( i_used < MMS_BUFFER_SIZE )
            {
                memmove( p_sys->buffer_tcp, p_sys->buffer_tcp + i_used,
                         MMS_BUFFER_SIZE - i_used );
            }
            p_sys->i_buffer_tcp -= i_used;

            if( i_status < 0 )
            {
                return VLC_EGENERIC;
            }

            if( p_sys->i_command == 0x1b )
            {
                mms_CommandSend( p_access, 0x1b, 0, 0, NULL, 0 );
            }
            else
            {
                break;
            }
        }
        else
        {
            return VLC_EGENERIC;
        }
    }

    return VLC_SUCCESS;
}

#define MMS_RETRY_MAX       10
#define MMS_RETRY_SLEEP     500

static int mms_CommandRead( access_t *p_access, int i_command1,
                            int i_command2 )
{
    access_sys_t *p_sys = p_access->p_sys;
    int i_count;
    int i_status;

    for( i_count = 0; i_count < MMS_RETRY_MAX; )
    {
        i_status = mms_ReceiveCommand( p_access );
        if( i_status < 0 || p_sys->i_command == 0 )
        {
            i_count++;
#ifdef _WIN32
            Sleep( MMS_RETRY_SLEEP / 1000 ); /*msleep*/
#else
            usleep( MMS_RETRY_SLEEP );
#endif
        }
        else if( i_command1 == 0 && i_command2 == 0)
        {
            return VLC_SUCCESS;
        }
        else if( p_sys->i_command == i_command1 ||
                 p_sys->i_command == i_command2 )
        {
            return VLC_SUCCESS;
        }
        else
        {
            switch( p_sys->i_command )
            {
                case 0x03:
                    msg_Warn( p_access, "socket closed by server" );
                    p_access->info.b_eof = true;
                    return VLC_EGENERIC;
                case 0x1e:
                    msg_Warn( p_access, "end of media stream" );
                    p_access->info.b_eof = true;
                    return VLC_EGENERIC;
                default:
                    break;
            }
        }
    }
    p_access->info.b_eof = true;
    msg_Warn( p_access, "failed to receive command (aborting)" );

    return VLC_EGENERIC;
}


static int mms_HeaderMediaRead( access_t *p_access, int i_type )
{
    access_sys_t *p_sys = p_access->p_sys;
    int          i_count;

    for( i_count = 0; i_count < MMS_RETRY_MAX; )
    {
        int i_status;

        //if( !vlc_object_alive (p_access) )
//            return -1;

        i_status = mms_ReceivePacket( p_access );
        if( i_status < 0 )
        {
            i_count++;
            msg_Warn( p_access, "cannot receive header (%d/%d)",
                      i_count, MMS_RETRY_MAX );
#ifdef _WIN32
            Sleep( MMS_RETRY_SLEEP/1000 ); /*msleep*/
#else
            usleep( MMS_RETRY_SLEEP );
#endif
        }
        else if( i_status == i_type || i_type == MMS_PACKET_ANY )
        {
            return i_type;
        }
        else if( i_status == MMS_PACKET_CMD )
        {
            switch( p_sys->i_command )
            {
                case 0x03:
                    msg_Warn( p_access, "socket closed by server" );
                    p_access->info.b_eof = true;
                    return -1;
                case 0x1e:
                    msg_Warn( p_access, "end of media stream" );
                    p_access->info.b_eof = true;
                    return -1;
                case 0x20:
                    /* XXX not too dificult to be done EXCEPT that we
                     * need to restart demuxer... and I don't see how we
                     * could do that :p */
                    msg_Err( p_access,
                             "reinitialization needed --> unsupported" );
                    p_access->info.b_eof = true;
                    return -1;
                default:
                    break;
            }
        }
    }

    msg_Err( p_access, "cannot receive %s (aborting)",
             ( i_type == MMS_PACKET_HEADER ) ? "header" : "media data" );
    p_access->info.b_eof = true;
    return -1;
}

#define  CLOCK_FREQ   INT64_C(1000000) 

static void *KeepAliveThread( void *p_data )
{
    access_t *p_access = p_data;

    for( ;; )
    {
        /* Send keep-alive every ten seconds */
       // int canc = vlc_savecancel();

        mms_CommandSend( p_access, 0x1b, 0, 0, NULL, 0 );

       // vlc_restorecancel( canc );
#ifdef _WIN32
        Sleep( 10 * CLOCK_FREQ ); /*msleep*/
#else
        sleep(10);
#endif 
    }
    assert(0);
}

static void KeepAliveStart( access_t *p_access )
{
    access_sys_t *p_sys = p_access->p_sys;
    if( p_sys->b_keep_alive )
        return;

   /* p_sys->b_keep_alive = !vlc_clone( &p_sys->keep_alive,
                                      KeepAliveThread, p_access,
                                      VLC_THREAD_PRIORITY_LOW ); */
}

static void KeepAliveStop( access_t *p_access )
{
    access_sys_t *p_sys = p_access->p_sys;
    if( !p_sys->b_keep_alive )
        return;

    //vlc_cancel( p_sys->keep_alive );
//    vlc_join( p_sys->keep_alive, NULL );
    p_sys->b_keep_alive = false;
}
