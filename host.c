#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "common.h"
#include <limits.h>

const short int HOST_DISCOVERY_PORT = 0xDED;
const short int HOST_CONNECTION_PORT = 0xDEF;
const short int CLIENT_CONNECTION_PORT = 0xDEE;
const int MAGIC_NUMBER = 0x1234;

long int give_num(const char* str_num);
int connect_to_client(long client_addr, short int client_port, short int my_port);
int wait_client(struct sockaddr_in* client_addr, short int port, int magic);
int send_num_thr(long int num, int sock);
int get_task(int fd, struct computing_task* task);
int send_buff_block(void* buff, size_t buff_size, int sock);

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("[main] Bad num of args\n");
        exit(EXIT_FAILURE);
    }

    long int num_threads = give_num(argv[1]);
    if (num_threads < 0)
    {
        printf("[main] Get num threads error\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in client_addr;

    int ret = wait_client(&client_addr, HOST_DISCOVERY_PORT, MAGIC_NUMBER);
    if (ret < 0)
    {
        printf("[main] Wait client datagramm error\n");
        exit(EXIT_FAILURE);
    }

    int sock = connect_to_client(ntohl(client_addr.sin_addr.s_addr), CLIENT_CONNECTION_PORT, HOST_CONNECTION_PORT);

    printf("[main] Connected to client\n");

    ret = send_buff_block(&num_threads, sizeof(num_threads), sock);
    if (ret < 0)
    {
        printf("[main] Send num threads error\n");
        exit(EXIT_FAILURE);
    }

    printf("[main] Num of threads was sended\n");

    struct computing_task task;
    ret = get_task(sock, &task);
    if (ret < 0)
    {
        printf("[main] geting task error\n");
        exit(EXIT_FAILURE);
    }

    printf("\tstart = %lg\n\tend = %lg\n\tprec = %lg\n", task.start, task.end, task.precision);

    return 0;
}

int get_task(int fd, struct computing_task* task)
{
    if (fd < 0 || task == NULL)
    {
        printf("[get_task] Bad args\n");
        return E_ERROR;
    }

    errno = 0;

    int ret = recv(fd, task, sizeof(*task), MSG_WAITALL);
    if (ret < 0)
    {
        perror("[get_task] recv task error\n");
        return E_ERROR;
    }
    if (ret != sizeof(*task))
    {
        printf("[get_task] Bad size of recv task\n");
        return E_ERROR;
    }

    return 0;
}

int send_buff_block(void* buff, size_t buff_size, int sock)
{
    if (sock < 0 || buff == NULL)
    {
        printf("[send_num_thr] Bad args\n");
        return E_BADARGS;
    }

    errno = 0;
    size_t sended = 0;
    do{
        ssize_t ret = send(sock, buff + sended, buff_size - sended, 0); // flags?
        if (ret < 0)
        {
            perror("[send_buff_block] send error\n");
            return E_ERROR;
        }

        sended += ret;
    } while (sended != buff_size);

    return sended;
}

long int give_num(const char* str_num)
{
    long int in_num = 0;
    char *end_string;

    errno = 0;
    in_num = strtoll(str_num, &end_string, 10);
    if ((errno != 0 && in_num == 0) || (errno == ERANGE && (in_num == LLONG_MAX || in_num == LLONG_MIN))) {
        printf("Bad string");
        return -2;
    }

    if (str_num == end_string) {
        printf("No number");
        return -3;
    }

    if (*end_string != '\0') {
        printf("Garbage after number");
        return -4;
    }

    if (in_num < 0) {
        printf("I want unsigned num");
        return -5;
    }

    return in_num;
}

int connect_to_client(long client_addr, short int client_port, short int my_port)
{
    errno = 0;

    int new_socket = socket(AF_INET, SOCK_STREAM, 0); // SOCK_NONBLOCK?
    if (new_socket < 0)
    {
        perror("[connect_to_client] Socket creation error\n");
        return E_ERROR;
    }

    int enable = 1;
    int ret = setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (ret < 0)
    {
        perror("[connect_to_client] Set Reuse addr error\n");
        return E_ERROR;
    }

    struct sockaddr_in host = {
        .sin_family      = AF_INET,
        .sin_port        = htons(my_port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    struct sockaddr_in client = {
        .sin_family = AF_INET,
        .sin_port = htons(client_port),
        .sin_addr.s_addr = htonl(client_addr)
    };

    ret = bind(new_socket, (struct sockaddr*) &host, sizeof(host));
    if (ret < 0)
    {
        perror("[connect_to_client] Bind new socket error\n");
        return E_ERROR;
    }

    ret = connect(new_socket, (struct sockaddr*) &client, sizeof(client));
    if (ret < 0)
    {
        perror("[connect_to_client] Connect error\n");
        return E_ERROR;
    }

    return new_socket;
}

int wait_client(struct sockaddr_in* client_addr, short int port, int magic)
{
  if (client_addr == NULL)
    return E_BADARGS;

    errno = 0;
    int broadcast_sk = socket(AF_INET, SOCK_DGRAM, 0);
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
