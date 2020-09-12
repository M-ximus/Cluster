#ifndef COMMON_HEADER
#define COMMON_HEADER

#define MAX_NUM_HOSTS 25

enum errors{
  E_BADARGS = -1,
  E_ERROR = -2,
};

struct computing_task{
    double start;
    double end;
    double precision;
};

#endif
