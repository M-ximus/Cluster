#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include "common.h"
#include "sys/epoll.h"

#define _GNU_SOURCE

short int HOST_DISCOVERY_PORT = 0xDED;
short int CLIENT_DISCOVERY_PORT = 0xBEAF;
short int CLIENT_CONNECTION_PORT = 0xDEE;
const int MAGIC_NUMBER = 0x1234;

struct host_info{
    int fd;
    int num_threads;
    int status;
}

struct client_info{
    int listen_socket;
    struct host_info hosts[MAX_NUM_HOSTS];
    int curr_num_hosts;
};

int connect_hosts(struct client_info* handle);
int discovery(short int host_port, short int port, int magic);
int accept_host_connection(int listen_sock, struct client_info* handle);
int create_listen_port(short int port, struct client_info* handle);
int get_threads_info(struct client_info* handle);

int main()
{
    struct client_info handle;
    handle.curr_num_hosts = 0;

    int ret = create_listen_port(CLIENT_CONNECTION_PORT, &handle);
    if (ret < 0)
    {
        printf("[main] Create listening port error\n");
        exit(EXIT_FAILURE);
    }

    ret = discovery(HOST_DISCOVERY_PORT, CLIENT_DISCOVERY_PORT, MAGIC_NUMBER);
    if (ret < 0)
    {
        printf("[main] Discovery error\n");
        exit(EXIT_FAILURE);
    }

    ret = connect_hosts(&handle);
    if (ret < 0)
    {
        printf("[main] Connect ot hosts error\n");
        exit(EXIT_FAILURE);
    }

    printf("num of connections = %d\n", handle.curr_num_hosts);

    ret = get_threads_info(&handle);

    return 0;
}

int get_threads_info(struct client_info* handle)
{
    if (handle == NULL)
    {
        printf("[get_threads_info] Bad args\n");
        return E_BADARGS;
    }

    errno = 0;

    int epollfd = epoll_create(1);
    if (epollfd < 0)
    {
        perror("[get_threads_info] epoll create error\n");
        return E_ERROR;
    }

    struct epoll_event events[handle->curr_num_hosts];
    struct epoll_event inter_events[handle->curr_num_hosts];

    for (int i = 0; i < handle->curr_num_hosts; i++)
    {
        inter_events[i].events  = EPOLLIN | EPOLLHUP;
        inter_events[i].data.fd = handle->hosts[i].fd;

        int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, handle->hosts[i].fd, inter_events + i);
        if (ret < 0)
        {
            perror("[get_threads_info] epoll ctl add new fd error\n");
            return E_ERROR;
        }
    }

    int num_to_read = handle->curr_num_hosts;
    while (num_to_read != 0)
    {
        int ret = epoll_wait(epollfd, events, handle->curr_num_hosts, -1);
        if (ret < 0)
        {
            perror("[get_threads_info] epoll wait error\n");
            return E_ERROR;
        }

        
    }

}

int discovery(short int host_port, short int port, int magic)
{
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

  close(broadcast_sk); // to use it in the TCP conection

  return 0;
}

int accept_host_connection(int listen_sock, struct client_info* handle)
{
    if (handle == NULL)
    {
        printf("[accept_host_connection] Bad args\n");
        return E_BADARGS;
    }

    errno = 0;
    int new_fd = accept4(listen_sock, NULL, NULL, SOCK_NONBLOCK);
    if (new_fd < 0)
    {
        perror("[accept_host_connection] Accept4 new connection error\n");
        return E_ERROR;
    }

    handle->hosts[handle->curr_num_hosts].fd = new_fd;
    handle->curr_num_hosts++;

    return 0;
}

int create_listen_port(short int port, struct client_info* handle)
{
    if (handle == NULL)
    {
        printf("[create_listen_port] Bad args\n");
        return E_BADARGS;
    }

    errno = 0;
    int listen_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_sock < 0)
    {
        perror("[create_listen_port] create listening socket error\n");
        return E_ERROR;
    }

    int enable = 1;
    int ret = setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (ret < 0)
    {
        perror("[create_listen_port] Set Reuse addr error\n");
        return E_ERROR;
    }

    struct sockaddr_in listen_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(port)
    };

    errno = 0;
    ret = bind(listen_sock, (struct sockaddr*) &listen_addr, sizeof(listen_addr));
    if (ret < 0)
    {
        perror("[create_listen_port] bind listen host error\n");
        return E_ERROR;
    }

    errno = 0;
    ret = listen(listen_sock, MAX_NUM_HOSTS);
    if (ret < 0)
    {
        perror("[create_listen_port] start listen error\n");
        return E_ERROR;
    }

    handle->listen_socket = listen_sock;

    return 0;
}

int connect_hosts(struct client_info* handle)
{
    if (handle == NULL)
    {
        printf("[connect_hosts] Bad ptr to hosts\n");
        return E_BADARGS;
    }

    struct timeval waiting_time = {
        .tv_sec = 0,
        .tv_usec = 250000 /*0.25 sec*/
    };

    fd_set read_fds;

    do{
        FD_ZERO(&read_fds);
        FD_SET(handle->listen_socket, &read_fds);

        errno = 0;
        int ret = select(handle->listen_socket + 1, &read_fds, NULL, NULL, &waiting_time);
        if (ret < 0)
        {
            perror("[connect_hosts] select error\n");
            return E_ERROR;
        }

        printf("[connect_hosts] time before end %ld\n", waiting_time.tv_usec);

        if (ret == 0)
            break;

        ret = accept_host_connection(handle->listen_socket, hosts);
        if (ret < 0)
        {
            printf("[connect_hosts] accept connection %d error\n", hosts->curr_num_hosts);
            return E_ERROR;
        }

    } while (handle->curr_num_hosts < MAX_NUM_HOSTS);

    return handle->curr_num_hosts;
}
