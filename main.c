#include "common.h"
#include "access.h"
#include "mms.h"

#include <signal.h>

void sig_handle(int sig_no);

access_t *p_access; 

int main(int argc, char* argv[])
{
     
#ifdef _WIN32
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;

	wVersionRequested = MAKEWORD( 2, 2 );

	err = WSAStartup( wVersionRequested, &wsaData );
	if ( err != 0 ) {
		return 1;
	}

	if ( LOBYTE( wsaData.wVersion ) != 2 ||
		HIBYTE( wsaData.wVersion ) != 2 ) {
			WSACleanup( );
			return 1; 
	}
#else
#endif
     
    p_access = (access_t*)malloc(sizeof(access_t));
    
    p_access->psz_access = "mmst";
    p_access->psz_path = "mms://alive.rbc.cn/cfm1075";
   

    signal(SIGINT, sig_handle);
    signal(SIGTERM, sig_handle);
    if(MOpen(p_access) == VLC_SUCCESS)
        p_access->pf_block(p_access);
    

    MClose(p_access);
    return 0;
}


void sig_handle(int sig_no)
{
    switch (sig_no)
    {
        case SIGINT:
        case SIGTERM:
            MClose(p_access);
    }

    exit(0);
}
