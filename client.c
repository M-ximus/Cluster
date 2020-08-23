#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "common.h"

short int HOST_PORT = 0xDED;
short int CLIENT_PORT = 0xBEAF;
const int MAGIC_NUMBER = 0x1234;

int discovery(short int host_port, short int port, int magic)
{
  errno = 0;
  int broadcast_sk = socket(PF_INET, SOCK_DGRAM, 0);
  if (broadcast_sk < 0)
  {
    perror("Socket creation error\n");
    return E_ERROR;
  }

  int broadcast_enable = 1;
  int ret = setsockopt(broadcast_sk, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
  if (ret < 0)
  {
    perror("Set broadcast error\n");
    return E_ERROR;
  }

  struct sockaddr_in client_addr = {
    .sin_family = AF_INET,
    .sin_addr   = htonl(INADDR_BROADCAST),
    .sin_port   = htons(port)
  };

  errno = 0;
  ret = bind(broadcast_sk, (struct sockaddr*) &client_addr, sizeof(client_addr));
  if (ret < 0)
  {
    perror("Bind client_addr error\n");
    return E_ERROR;
  }

  struct sockaddr_in host_addr = {
    .sin_family = AF_INET,
    .sin_addr   = htonl(INADDR_BROADCAST),
    .sin_port   = htons(host_port)
  };

  errno = 0;
  int msg = magic;
  ret = sendto(broadcast_sk, &msg, sizeof(msg), 0, (struct sockaddr*) &host_addr, sizeof(host_addr));
  if (ret < 0)
  {
    perror("Sendto error\n");
    return E_ERROR;
  }

  return 0;
}

int main()
{
  int ret = discovery(HOST_PORT, CLIENT_PORT, MAGIC_NUMBER);
  if (ret < 0)
  {
    printf("[main] Discovery error\n");
    exit(EXIT_FAILURE);
  }

  while(1){}

  return 0;
}
