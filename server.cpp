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

#include "memfd.hpp"


static int new_memfd_region(char *unique_str) {
  char *shm;
  const int shm_size = 1024;
  int fd, ret;

  fd = memfd_create("Server memfd", MFD_ALLOW_SEALING);
  if (fd == -1) error("memfd_create()");

  ret = ftruncate(fd, shm_size);
  if (ret == -1) error("ftruncate()");

  ret = fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
  if (ret == -1) error("fcntl(F_SEAL_SHRINK)");

  shm = static_cast<char *>(mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (shm == MAP_FAILED) error("mmap()");

  sprintf(shm, "Secure zero-copy message from server: %s", unique_str);

  /* Seal writes too, but unmap our shared mappings beforehand */
  ret = munmap(shm, shm_size);
  if (ret == -1) error("munmap()");
//  ret = fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE);
//  if (ret == -1)
//    error("fcntl(F_SEAL_WRITE)");

  ret = fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL);
  if (ret == -1) error("fcntl(F_SEAL_SEAL)");

  return fd;
}

/* Pass file descriptor @fd to the client, which is
 * connected to us through the Unix domain socket @conn */
static void send_fd(int conn, int fd) {
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
#define SIG_INTERVAL_NS 10000

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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
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
  sleep(1);

  timespec current{}, temp{}, diff{};

  while (true) {
    auto string = strtok(shm, delim);
    if (string != nullptr) {
      temp.tv_sec = std::atol(string);
      string = strtok(nullptr, delim);
      if (string != nullptr) {
        temp.tv_nsec = std::atol(string);
      }
    }

    timespec_diff(&current, &temp, &diff);
    // TODO: This assumes under 1 second interval atm
    if (diff.tv_nsec >= SIG_INTERVAL_NS) {
      current = temp;
      kill(pid, SIGPROF);
//      printf("%li %li SIGPROF\n", temp.tv_sec, temp.tv_nsec);
    }
  }

//  std::ofstream fout("/tmp/res.log");
//
//  for (auto &&item : collector) {
//    fout << item << "\n";
//  }

//  fout.flush();
//  fout.close();
//  close(conn);
//  close(fd);
}
#pragma clang diagnostic pop

int main(int argc, char **argv) {
  start_server_and_send_memfd_to_clients();
  return 0;
}
