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

  ret = bind(broadcast_sk, (struct sockaddr*) &broadcast_addr, sizeof(broadcast_addr));
  if (ret < 0)
  {
    perror("Bind error\n");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  int msg = 0;

  errno = 0;
  ssize_t num_bytes = recvfrom(broadcast_sk, &msg, sizeof(msg), 0, (struct sockadrr*) &client_addr, &client_addr_len);
  if (num_bytes < 0)
  {
    perror("Recv from error\n");
    exit(EXIT_FAILURE);
  }

  printf("%X", msg);

  return 0;
}
