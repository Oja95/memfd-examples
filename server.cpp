/*
 * Example memfd_create(2) server application.
 *
 * Copyright (C) 2015 Ahmed S. Darwish <darwish.07@gmail.com>
 *
 * SPDX-License-Identifier: Unlicense
 *
 * Kindly check attached README.md file for further details.
 */

#include <linux/memfd.h>
#include <sys/mman.h>

#include <sys/socket.h>
#include <linux/un.h>

#include <fcntl.h>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <ctime>
#include <vector>
#include <set>
#include <fstream>
#include <signal.h>
#include <sys/time.h>

#include "memfd.hpp"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
int new_memfd_region(char *unique_str) {
  char *shm;
  const int shm_size = 1024;
  int fd, ret;

  fd = memfd_create("Server memfd", MFD_ALLOW_SEALING);
  if (fd == -1) error("memfd_create()");

  ret = ftruncate(fd, shm_size);
  if (ret == -1) error("ftruncate()");

  ret = fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
  if (ret == -1) error("fcntl(F_SEAL_SHRINK)");

  shm = static_cast<char *>(mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (shm == MAP_FAILED) error("mmap()");

  sprintf(shm, "Connection accepted timestamp from server: %s", unique_str);

  ret = munmap(shm, shm_size);
  if (ret == -1) error("munmap()");
  ret = fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL);
  if (ret == -1) error("fcntl(F_SEAL_SEAL)");

  return fd;
}

void send_fd(int conn, int fd) {
  struct msghdr msgh{};
  struct iovec iov{};
  union {
      struct cmsghdr cmsgh;
      /* Space large enough to hold an 'int' */
      char control[CMSG_SPACE(sizeof(int))];
  } control_un{};

  if (fd == -1) {
    fprintf(stderr, "Cannot pass an invalid fd equaling -1\n");
    exit(EXIT_FAILURE);
  }

  /* We must transmit at least 1 byte of real data in order
   * to send some other ancillary data. */
  char placeholder = 'A';
  iov.iov_base = &placeholder;
  iov.iov_len = sizeof(char);

  msgh.msg_name = nullptr;
  msgh.msg_namelen = 0;
  msgh.msg_iov = &iov;
  msgh.msg_iovlen = 1;
  msgh.msg_control = control_un.control;
  msgh.msg_controllen = sizeof(control_un.control);

  /* Write the fd as ancillary data */
  control_un.cmsgh.cmsg_len = CMSG_LEN(sizeof(int));
  control_un.cmsgh.cmsg_level = SOL_SOCKET;
  control_un.cmsgh.cmsg_type = SCM_RIGHTS;
  *((int *) CMSG_DATA(CMSG_FIRSTHDR(&msgh))) = fd;

  int size = sendmsg(conn, &msgh, 0);
  if (size < 0) error("sendmsg()");
}

#define LOCAL_SOCKET_NAME    "/tmp/unix_socket"
#define MAX_CONNECT_BACKLOG  128
#define SIG_INTERVAL_NS 50000

void timespec_diff(const struct timespec *start, const struct timespec *stop,
                   struct timespec *result) {
  if ((stop->tv_nsec - start->tv_nsec) < 0) {
    result->tv_sec = stop->tv_sec - start->tv_sec - 1;
    result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
  } else {
    result->tv_sec = stop->tv_sec - start->tv_sec;
    result->tv_nsec = stop->tv_nsec - start->tv_nsec;
  }
}


static const char *const delim = " ";

timespec &readTimeFromSharedMemory(char *shm, timespec &temp) {
  auto string = strtok(shm, delim);
  if (string != nullptr) {
      temp.tv_sec = atol(string);
      string = strtok(nullptr, delim);
      if (string != nullptr) {
        temp.tv_nsec = atol(string);
      }
    }
  return temp;
}

static void start_server_and_send_memfd_to_clients() {
  int sock, conn, fd, ret;
  struct sockaddr_un address;
  socklen_t addrlen;

  sock = socket(PF_UNIX, SOCK_STREAM, 0);
  if (sock == -1) error("socket()");

  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  snprintf(address.sun_path, UNIX_PATH_MAX, LOCAL_SOCKET_NAME);

  ret = unlink(LOCAL_SOCKET_NAME);
  if (ret != 0 && ret != -ENOENT && ret != -EPERM) error("unlink()");

  ret = bind(sock, (struct sockaddr *) &address, sizeof(address));
  if (ret != 0) error("bind()");

  ret = listen(sock, MAX_CONNECT_BACKLOG);
  if (ret != 0) error("listen()");


  conn = accept(sock, (struct sockaddr *) &address, &addrlen);



  time_t now = time(nullptr);
  char *nowbuf = ctime(&now);
  nowbuf[strlen(nowbuf) - 1] = '\0';

  printf("[%s] New client connection!\n", nowbuf);

  fd = new_memfd_region(nowbuf);
  send_fd(conn, fd);

  sleep(1);

  int pid;
  int res = read(conn, &pid, sizeof(int));
  if (res < 0) {
    printf("Didn't get pid from domain socket!");
  }
  printf("%d\n", pid);

  char *shm;
  const int shm_size = 1024;

  shm = static_cast<char *>(mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (shm == MAP_FAILED) errorp("mmap");

  // Wait until the client has memory mapped the shared memory and is writing to the memory
  // TODO: Think of better synchronization mechanism
  sleep(1);
  printf("READY!\n");


  timespec current{}, newVal{}, diff{};
  timespec sleep{};
  while (true) {
    current = readTimeFromSharedMemory(shm, current);
    kill(pid, SIGALRM);

    while (true) {
      if (newVal.tv_nsec != current.tv_nsec || newVal.tv_sec != current.tv_sec) break;
      kill(pid, SIGALRM);
      newVal = readTimeFromSharedMemory(shm, newVal);
    }

    timespec_diff(&current, &newVal, &diff);

    if (diff.tv_nsec >= SIG_INTERVAL_NS) {
      kill(pid, SIGPROF);
    } else {
      long sleepTime = SIG_INTERVAL_NS - diff.tv_nsec - 20;
      if (sleepTime > 0) {
        sleep.tv_nsec = sleepTime;
        nanosleep(&sleep, nullptr);
      }
    }
    current = newVal;
  }

}

int main(int argc, char **argv) {
  start_server_and_send_memfd_to_clients();
  return 0;
}

#pragma clang diagnostic pop