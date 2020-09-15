#define _GNU_SOURCE

#include <stdio.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "common.h"
#include <limits.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

const short int HOST_DISCOVERY_PORT = 0xDED;
const short int HOST_CONNECTION_PORT = 0xDEF;
const short int CLIENT_CONNECTION_PORT = 0xDEE;
const int MAGIC_NUMBER = 0x1234;

const unsigned int TIMEOUT = 5000;

typedef struct thread_info
{
    double sum;
    double start;
    double end;
    double delt;
    int num_cpu;
    int events;
} thread_info;

struct CPU_info
{
    int cache_line;
    int num_cpus;
    int num_threads;
};

long int give_num(const char* str_num);
int connect_to_client(long client_addr, short int client_port, short int my_port);
int wait_client(struct sockaddr_in* client_addr, short int port, int magic);
int send_num_thr(long int num, int sock);
int get_task(int fd, struct computing_task* task);
int send_buff_block(void* buff, size_t buff_size, int sock);
int prepare_threads(void* info, size_t info_size, int num_thr, double start, double end, double step);
int prepare_parasites(void* info, size_t info_size, int num_parasites, double par_start, double par_end, double par_step);
void* alloc_thread_info(size_t num_threads, size_t* size);
void* integral_thread(void* info);
double func(double x);
int cache_line_size();
int send_result(int fd, struct result res);
int set_keepalive(int fd);
int config_socket(int fd);
int set_performance_socket_settings(int fd);

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
    if (sock < 0)
    {
        printf("[main] Connection to client error\n");
        exit(EXIT_FAILURE);
    }

    ret = config_socket(sock);
    if (ret < 0)
    {
        printf("[main] Config new socket error\n");
        exit(EXIT_FAILURE);
    }

    printf("Connected to client\n");

    ret = send_buff_block(&num_threads, sizeof(num_threads), sock);
    if (ret < 0)
    {
        printf("[main] Send num threads error\n");
        exit(EXIT_FAILURE);
    }

    //printf("%d\n", ret);

    printf("[main] Num of threads was sended\n");

    struct computing_task task;
    ret = get_task(sock, &task);
    if (ret < 0)
    {
        printf("[main] geting task error\n");
        exit(EXIT_FAILURE);
    }

    printf("\tstart = %lg\n\tend = %lg\n\tprec = %lg\n", task.start, task.end, task.precision);

    int num_logic_cpus = get_nprocs();
    int num_parasites  = 0;
    if (num_logic_cpus > num_threads)
        num_parasites  = num_logic_cpus - num_threads;

    size_t thread_info_size = 0;
    void* info_arr = alloc_thread_info(num_threads + num_parasites, &thread_info_size);
    if (info_arr == NULL)
    {
        perror("[main] Allocation of thread info array error\n");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    pthread_t* arr_threads = (pthread_t*) calloc(num_threads + num_parasites, sizeof(pthread_t));
    if (arr_threads == NULL)
    {
        perror("[main] alloc threads array error\n");
        exit(EXIT_FAILURE);
    }

    ret = prepare_threads(info_arr, thread_info_size, num_threads, task.start, task.end, task.precision);
    if (ret < 0)
    {
        printf("[main] prepare_threads error\n");
        exit(EXIT_FAILURE);
    }

    ret = prepare_parasites(info_arr + (num_threads * thread_info_size),
        thread_info_size, num_parasites, task.start, (task.end - task.start) / num_threads, task.precision);
    if (ret < 0)
    {
        printf("[main] Preparing parasides error\n");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    int epollfd = epoll_create(1);
    if (epollfd < 0)
    {
        perror("[main] epoll creation error\n");
        exit(EXIT_FAILURE);
    }

    struct epoll_event event_ctr;
    event_ctr.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP;

    for(int i = 0; i < num_threads + num_parasites; i++)
    {
        if (num_parasites > 0)
            ((thread_info*)(info_arr + i * thread_info_size))->num_cpu = i;

        if (((thread_info*)(info_arr + i * thread_info_size))->events > 0)
        {
            event_ctr.data.fd = ((thread_info*)(info_arr + i * thread_info_size))->events;

            errno = 0;
            int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, ((thread_info*)(info_arr + i * thread_info_size))->events, &event_ctr);
            if (ret < 0)
            {
                perror("[main] Adding new event fd error\n");
                EXIT_FAILURE;
            }
        }

        errno = 0;
        int ret = pthread_create(arr_threads + i, NULL, integral_thread, (info_arr + i * thread_info_size));
        if (ret < 0)
        {
            perror("[main] Bad thread_start\n");
            exit(EXIT_FAILURE);
        }
    }

    event_ctr.events  = EPOLLHUP | EPOLLRDHUP;
    event_ctr.data.fd = sock;
    errno = 0;
    ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &event_ctr);
    if (ret < 0)
    {
        perror("[main] Adding socket fd error\n");
        exit(EXIT_FAILURE);
    }

    double sum = 0.0;
    struct epoll_event events[32]; // Max events
    int num_to_wait = num_threads;
    do{
        errno = 0;
        int num_events = epoll_wait(epollfd, events, 32, -1);
        if (num_events < 0)
        {
            perror("[main] epoll wait event fds and socket\n");
            exit(EXIT_FAILURE);
        }

        for(int curr_event = 0; curr_event < num_events; curr_event++)
        {
            if (events[curr_event].events & EPOLLHUP || events[curr_event].events & EPOLLRDHUP)
            {
                printf("[main] Epollhup on calculation\n");
                exit(EXIT_FAILURE);
            }

            for (int i = 0; i < num_threads; i++)
            {
                if (events[curr_event].data.fd != ((thread_info*)(info_arr + i * thread_info_size))->events)
                    continue;

                uint64_t readed = 0;
                errno = 0;
                ret = read(((thread_info*)(info_arr + i * thread_info_size))->events, &readed, sizeof(uint64_t));
                if (ret < 0)
                {
                    perror("[main] read from event fd error\n");
                    exit(EXIT_FAILURE);
                }

                sum += ((thread_info*)(info_arr + i * thread_info_size))->sum;

                ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, ((thread_info*)(info_arr + i * thread_info_size))->events, NULL);
                if (ret < 0)
                {
                    perror("[main] Deleting fd from epoll error\n");
                    exit(EXIT_FAILURE);
                }

                num_to_wait--;

                break;
            }
        }
    } while (num_to_wait != 0);
    close(epollfd);

    printf("Sum = %lg\n", sum);

    struct result send_res = {
        .sum = sum
    };

    ret = send_result(sock, send_res);
    if (ret < 0)
    {
        printf("[main] sending result error\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

int send_result(int fd, struct result res)
{
    if (fd < 0)
    {
        printf("[send_result] Bad args\n");
        return E_BADARGS;
    }

    printf("I'm sending %lg\n", res.sum);

    //uint64_t test = 0xABCDEF0123456789;
    errno = 0;
    int ret = send(fd, &res, sizeof(res), MSG_NOSIGNAL);
    if (ret < 0)
    {
        perror("[send_result] Sending result error\n");
        return E_ERROR;
    }

    if (ret < sizeof(res))
    {
        printf("[send_result] Not enough bytes were sent\n");
        return E_ERROR;
    }

    return 0;
}

int cache_line_size()
{
    errno = 0;
    FILE* cache_info = fopen("/sys/bus/cpu/devices/cpu0/cache/index0/coherency_line_size", "r");
    if (cache_info == NULL)
    {
        perror("[cache_line_size] Can't open /sys/bus/cpu/devices/cpu0/cache/index0/coherency_line_size\n");
        return E_ERROR;
    }

    int line_size = 0;
    int ret = fscanf(cache_info, "%d", &line_size);
    if (ret != 1)
    {
        perror("[cache_line_size] Can't scan coherency_line_size\n");
        return E_ERROR;
    }

    return line_size;
}

double func(double x)
{
    return x * x;
}

void* integral_thread(void* info)
{
    if (info == NULL)
        exit(EXIT_FAILURE);

    cpu_set_t cpu;
    pthread_t thread = pthread_self();
    int num_cpu = ((thread_info*)info)->num_cpu;

    if (num_cpu > 0)
    {
        CPU_ZERO(&cpu);
        CPU_SET(num_cpu, &cpu);

        errno = 0;
        int ret = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpu);
        if (ret < 0)
        {
            perror("[integral_thread] Bad cpu attach!\n");
            exit(EXIT_FAILURE);
        }
    }


    double delta = ((thread_info*)info)->delt;
    double end = ((thread_info*)(info))->end;

    double x = ((thread_info*)(info))->start + delta;

    for (; x < end; x += delta)// Check x and delta in asm version
        ((thread_info*)(info))->sum += func(x) * delta;

    ((thread_info*)(info))->sum += func(((thread_info*)(info))->start) * delta / 2;
    ((thread_info*)(info))->sum += func(((thread_info*)(info))->end) * delta / 2;

    if (((thread_info*)(info))->events > 0)
    {
        uint64_t end_endicator = 1;

        errno = 0;
        int ret = write(((thread_info*)(info))->events, &end_endicator, sizeof(uint64_t));
        if (ret < 0)
        {
            perror("[integral_thread] write to event fd error\n");
            EXIT_FAILURE; // too hard?
        }
    }

    return NULL;
}


void* alloc_thread_info(size_t num_threads, size_t* size)
{
    if (size == NULL)
        return NULL;

    int line_size = cache_line_size();
    if (line_size <= 0)
    {
        perror("[alloc_thread_info] Bad cache coherency\n");
        return NULL;
    }

    size_t info_size = sizeof(thread_info);
    if (info_size <= line_size)
        info_size = 2 * line_size; // free line if struct will consist 2 lines
    else
        info_size = (info_size / line_size + 1 + 1) * line_size; // free line

    *size = info_size;

    errno = 0;
    return malloc(num_threads * info_size);
}

int prepare_parasites(void* info, size_t info_size, int num_parasites, double par_start, double par_end, double par_step)
{
    if (info == NULL || num_parasites < 0 || par_start == NAN || par_end == NAN || par_step == NAN)
        return E_BADARGS;

    for (int i = 0; i < num_parasites; i++)
    {
        ((thread_info*)(info + i * info_size))->start   = par_start;
        ((thread_info*)(info + i * info_size))->end     = par_end;
        ((thread_info*)(info + i * info_size))->delt    = par_step;
        ((thread_info*)(info + i * info_size))->num_cpu = -1;
        ((thread_info*)(info + i * info_size))->events  = -1; // to detect
    }

    return 0;
}

int prepare_threads(void* info, size_t info_size, int num_thr, double start, double end, double step)
{
    if (info == NULL || num_thr < 0 || start == NAN || end == NAN || step == NAN)
        return E_BADARGS;

    double diap_step = (end - start) / num_thr;

    for (int i = 0; i < num_thr; i++)
    {
        ((thread_info*)(info + i * info_size))->start   = start + diap_step * i;
        ((thread_info*)(info + i * info_size))->end     = start + diap_step * (i + 1);
        ((thread_info*)(info + i * info_size))->delt    = step;
        ((thread_info*)(info + i * info_size))->num_cpu = -1;

        errno = 0;
        ((thread_info*)(info + i * info_size))->events  = eventfd(0, EFD_NONBLOCK);
        if (((thread_info*)(info + i * info_size))->events < 0)
        {
            perror("[prepare_threads] create event fd error\n");
            return E_ERROR;
        }
        //printf("%lg\n", start + diap_step * (i + 1));
    }

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
      perror("[wait_client] Socket creation error\n");
      return E_ERROR;
    }

    int broadcast_enable = 1;
    int ret = setsockopt(broadcast_sk, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
    if (ret < 0)
    {
      perror("[wait_client] Set broadcast error\n");
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
      perror("[wait_client] Bind error\n");
      return E_ERROR;
    }

    socklen_t client_addr_len = sizeof(struct sockaddr_in);
    int msg = 0;
    ssize_t num_bytes = 0;

    do {
      errno = 0;
      num_bytes = recvfrom(broadcast_sk, &msg, sizeof(msg), 0, (struct sockaddr*) client_addr, &client_addr_len);
      if (num_bytes < 0)
      {
        perror("[wait_client] Recv from error\n");
        return E_ERROR;
      }
    } while(num_bytes != sizeof(magic) || msg != magic);

    return 0;
}

int config_socket(int fd)
{
    if (fd < 0)
    {
        printf("[config_socket] Bad args\n");
        return E_BADARGS;
    }

    int ret = set_keepalive(fd);
    if (ret < 0)
    {
        printf("[config_socket] set keepalive error\n");
        return E_ERROR;
    }

    ret = set_performance_socket_settings(fd);
    if (ret < 0)
    {
        printf("[config_socket] set CORK and NODELAY error\n");
        return E_ERROR;
    }

    errno = 0;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &TIMEOUT, sizeof(TIMEOUT));
    if(ret < 0)
    {
        perror("[config_socket] set TIMEOUT error\n");
        return E_ERROR;
    }

    return 0;
}

int set_keepalive(int fd)
{
    const int COUNTS_TO_DIE = 4;
    const int IDLE_TIME     = 1;
    const int INTERVAL      = 1; // in secs

    if (fd < 0)
    {
        printf("[set_keepalive] Bad args\n");
        return E_BADARGS;
    }

    errno = 0; // for all operations;

    int enable = 1;

    int ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
    if (ret < 0)
    {
        perror("[set_keepalive] set SO_KEEPALIVE error\n");
        return E_ERROR;
    }

    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &COUNTS_TO_DIE, sizeof(COUNTS_TO_DIE));
    if (ret < 0)
    {
        perror("[set_keepalive] set COUNTS_TO_DIE error\n");
        return E_ERROR;
    }

    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &IDLE_TIME, sizeof(IDLE_TIME));
    if (ret < 0)
    {
        perror("[set_keepalive] set IDLE_TIME error\n");
        return E_ERROR;
    }

    ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &INTERVAL, sizeof(INTERVAL));
    if (ret < 0)
    {
        perror("[set_keepalive] set INTERVAL error\n");
        return E_ERROR;
    }

    return 0;
}

int set_performance_socket_settings(int fd)
{
    if (fd < 0)
    {
        printf("[set_performance_socket_settings] Bad args\n");
        return E_BADARGS;
    }

    errno = 0;

    int disable = 0;

    int ret = setsockopt(fd, IPPROTO_TCP, TCP_CORK, &disable, sizeof(disable));
    if (ret < 0)
    {
        perror("[set_performance_socket_settings] Disable CORK error\n");
        return E_ERROR;
    }

    int enable = 1;
    ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
    if (ret < 0)
    {
        perror("[set_performance_socket_settings] Enable NODELAY error\n");
        return E_ERROR;
    }

    return 0;
}
