#ifndef COMMON_HEADER
#define COMMON_HEADER

#define MAX_NUM_HOSTS 25

enum errors
{
    E_ERROR = -1,
    E_CACHE_INFO = -2,
    E_BADARGS = -3,
};

struct computing_task{
    double start;
    double end;
    double precision;
};

#endif
