#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>

short int HOST_PORT = 0xDED;
short int CLIENT_PORT = 0xBEAF;
const int MAGIC_NUMBER = 0x1234;

int main()
{
  errno = 0;
  int broadcast_sk = socket(PF_INET, SOCK_DGRAM, 0);
  if (broadcast_sk < 0)
  {
    perror("Socket creation error\n");
    exit(EXIT_FAILURE);
  }

  int broadcast_enable = 1;
  int ret = setsockopt(broadcast_sk, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
  if (ret < 0)
  {
    perror("Set broadcast error\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in client_addr = {
    .sin_family = AF_INET,
    .sin_addr   = htonl(INADDR_BROADCAST),
    .sin_port   = htons(CLIENT_PORT)
  };

  errno = 0;
  ret = bind(broadcast_sk, (struct sockaddr*) &client_addr, sizeof(client_addr));
  if (ret < 0)
  {
    perror("Bind client_addr error\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in host_addr = {
    .sin_family = AF_INET,
    .sin_addr   = htonl(INADDR_BROADCAST),
    .sin_port   = htons(HOST_PORT)
  };

  errno = 0;
  int msg = MAGIC_NUMBER;
  ret = sendto(broadcast_sk, &msg, sizeof(msg), 0, (struct sockaddr*) &host_addr, sizeof(host_addr));
  if (ret < 0)
  {
    perror("Sendto error\n");
    exit(EXIT_FAILURE);
  }

  while(1){}

  return 0;
}
