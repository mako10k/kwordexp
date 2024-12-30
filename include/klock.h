#ifndef __KLOCK_H__
#define __KLOCK_H__

typedef int klock_t;

int klock(klock_t *pkl);
int kwake(klock_t *pkl);

#endif // __KLOCK_H__