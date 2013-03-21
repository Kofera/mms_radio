#ifndef _MMS_H_
#define _MMS_H_ 1

#define MMS_PROTO_AUTO  0
#define MMS_PROTO_TCP   1
#define MMS_PROTO_UDP   2
#define MMS_PROTO_HTTP  3

int  MOpen ( access_t * );
void MClose( access_t * );


/* mmst and mmsu */
int   MMSTUOpen   ( access_t * );
void  MMSTUClose  ( access_t * );

/* mmsh */
// int   MMSHOpen   ( access_t * );
// void  MMSHClose  ( access_t * );

#endif

