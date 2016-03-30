#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#define P 17
#define Q 14
#define FRACTION (1<<(Q))

typedef int fp_t;

#if P + Q != 31
#error "FATAL ERROR: P + Q != 31."
#endif

#define INT_ADD(x, n) (x) + (n) * (FRACTION)
#define INT_SUB(x, n) (x) - (n) * (FRACTION)
#define INT_TO_FP(x) (x) * (FRACTION)
#define FP_TO_INT(x) ((x) >= 0 ? ((x) + (FRACTION) / 2) / (FRACTION) : ((x) - (FRACTION) / 2) / (FRACTION))
#define FP_MUL(x, y) ((int64_t)(x)) * (y) / (FRACTION)
#define FP_DIV(x, y) ((int64_t)(x)) * (FRACTION) / (y)

#endif /* threads/fixed-point.h */
