/*
 * https://wayland-book.com/surfaces/shared-memory.html#allocating-a-shared-memory-pool
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static void randname(buf)
     char *buf;
{
  struct timespec ts;
  long r;
  int i;

  clock_gettime(CLOCK_REALTIME, &ts);
  r = ts.tv_nsec;
  for (i = 0; i < 6; ++i) {
    buf[i] = 'A'+(r&15)+(r&16)*2;
    r >>= 5;
  }
}

static int create_shm_file()
{
  int retries = 100;
  do {
    char name[] = "/wl_shm-XXXXXX";
    int fd;

    randname(name + sizeof(name) - 7);
    --retries;
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      shm_unlink(name);
      return fd;
    }
  } while (retries > 0 && errno == EEXIST);
  return -1;
}

int allocate_shm_file(size)
     size_t size;
{
  int fd = create_shm_file();
  int ret;

  if (fd < 0)
    return -1;

  do {
    ret = ftruncate(fd, size);
  } while (ret < 0 && errno == EINTR);
  if (ret < 0) {
    close(fd);
    return -1;
  }
  return fd;
}
