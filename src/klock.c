#include "../include/klock.h"
#include <linux/futex.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
          int *uaddr2, int val3) {
  return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

int klock(klock_t *pkl) {
  while (true) {
    if (__atomic_exchange_n(pkl, 1, __ATOMIC_ACQUIRE) == 0)
      return 0;
    futex(pkl, FUTEX_WAIT, 1, NULL, NULL, 0);
  }
}

int kwake(klock_t *pkl) {
  __atomic_store_n(pkl, 0, __ATOMIC_RELEASE);
  int ret = futex(pkl, FUTEX_WAKE, 1, NULL, NULL, 0);
  if (ret == -1)
    return -1;
  return 0;
}