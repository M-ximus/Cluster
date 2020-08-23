#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "common.h"

const short int HOST_PORT = 0xDED;
const int MAGIC_NUMBER = 0x1234;

int wait_client(struct sockaddr_in* client_addr, short int port, int magic)
{
  if (client_addr == NULL)
    return E_BADARGS;

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

    struct sockaddr_in broadcast_addr = {
      .sin_family = AF_INET,
      .sin_addr   = htonl(INADDR_BROADCAST),
      .sin_port   = htons(port)
    };

    ret = bind(broadcast_sk, (struct sockaddr*) &broadcast_addr, sizeof(broadcast_addr));
    if (ret < 0)
    {
      perror("Bind error\n");
      return E_ERROR;
    }

    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    int msg = 0;
    ssize_t num_bytes = 0;

    do {
      errno = 0;
      num_bytes = recvfrom(broadcast_sk, &msg, sizeof(msg), 0, (struct sockadrr*) client_addr, &client_addr_len);
      if (num_bytes < 0)
      {
        perror("Recv from error\n");
        return E_ERROR;
      }
    } while(num_bytes != sizeof(magic) || msg != magic);

    return 0;
}

int main()
{
  struct sockaddr_in client_addr;

  int ret = wait_client(&client_addr, HOST_PORT, MAGIC_NUMBER);
  if (ret < 0)
  {
    printf("[main] Wait client datagramm error\n");
    exit(EXIT_FAILURE);
  }

  printf("%X\n", ntohs(client_addr.sin_port));

  return 0;
}
