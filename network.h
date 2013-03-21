#ifndef NETWORK_H
#define NETWORK_H

#define NI_MAXNUMERICHOST 64

int net_ConnectTCP(const char* host, int port);
int net_Write(int fd, uint8_t* buffer, int buf_sz);
int net_Close(int fd);

#endif
