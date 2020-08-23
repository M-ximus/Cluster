#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>

short int HOST_PORT = 0xDED;

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

  struct sockaddr_in broadcast_addr = {
    .sin_family = AF_INET,
    .sin_addr   = htonl(INADDR_BROADCAST),
    .sin_port   = htons(HOST_PORT)
  };

  errno = 0;
  int test_msg = 0xDED;
  ret = sendto(broadcast_sk, &test_msg, sizeof(test_msg), 0, (struct sockaddr*) &broadcast_addr, sizeof(broadcast_addr));
  if (ret < 0)
  {
    perror("Sendto error\n");
    exit(EXIT_FAILURE);
  }

  while(1){}

  return 0;
}
