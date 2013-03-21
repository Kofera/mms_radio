#include "common.h"
#include "network.h"

int net_ConnectTCP(const char* host, int port)
{
    int sockfd;
	struct sockaddr_in serv_addr;
	struct hostent *server;
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	server = gethostbyname(host);         // "www.baidu.com"
	if (server == NULL)
	{
		return 0;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;

	//inet_aton("222.246.137.8", &serv_addr);

	memcpy( &SOCK_ADDR_IP(serv_addr), (char *)server->h_addr, server->h_length);
	serv_addr.sin_port = htons(port);
    
    printf("connect the server %s:%d !\n", inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port) );
    
	/* Now connect to the server */
	if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
	{
	//	msg_Err("ERROR connecting");
		return -1;
	}

	return sockfd;
}

int net_Write(int sockfd, uint8_t* buffer, int buf_sz)
{
  	int n = 0;
   
   #ifdef _WIN32
	n = send (sockfd, (char*)buffer, buf_sz, 0);
#else
	n = write(sockfd, buffer, buf_sz);
#endif

	if (n < 0) 
	{
      //  printf("ERROR writing to socket : %d\n", n);
	}

	return n;
}

int net_Close(int sockfd)
{
  return closesocket(sockfd);
}

