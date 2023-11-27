//--------------------------------------------------------------------------------------------------
// System Programming                       Network Lab                                  Fall 2021
//
/// @file
/// @brief Client-side implementation of Network Lab
///
/// @author <Ji Ho Kang>
/// @studid <20XX-XXXXX>
///
/// @section changelog Change Log
/// 2020/11/18 Hyunik Kim created
/// 2021/11/23 Jaume Mateu Cuadrat cleanup, add milestones
///
/// @section license_section License
/// Copyright (c) 2020-2021, Computer Systems and Platforms Laboratory, SNU
/// All rights reserved.
///
/// Redistribution and use in source and binary forms, with or without modification, are permitted
/// provided that the following conditions are met:
///
/// - Redistributions of source code must retain the above copyright notice, this list of condi-
///   tions and the following disclaimer.
/// - Redistributions in binary form must reproduce the above copyright notice, this list of condi-
///   tions and the following disclaimer in the documentation and/or other materials provided with
///   the distribution.
///
/// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
/// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED  TO, THE IMPLIED  WARRANTIES OF MERCHANTABILITY
/// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
/// CONTRIBUTORS  BE LIABLE FOR ANY DIRECT,  INDIRECT, INCIDENTAL, SPECIAL,  EXEMPLARY,  OR CONSE-
/// QUENTIAL DAMAGES  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
/// LOSS OF USE, DATA,  OR PROFITS; OR BUSINESS INTERRUPTION)  HOWEVER CAUSED AND ON ANY THEORY OF
/// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
/// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
/// DAMAGE.
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "net.h"
#include "burger.h"


/// @brief client task for connection thread
void *thread_task(void *data)
{
  struct addrinfo *ai, *ai_it;
  size_t read, sent, buflen;
  int serverfd = -1, choice, ret, res;
  char *buffer;
  pthread_t tid;

  tid = pthread_self();

  buffer = (char *)malloc(BUF_SIZE);
  buflen = BUF_SIZE;

  // Connect to McDonald's server
  // Get the socket list
  ai = getsocklist(IP, PORT, AF_INET, SOCK_STREAM, 0, &res);

  if (res != 0) {
    fprintf(stderr, "%s\n", gai_strerror(res));
    goto err;
  }

  // Create the sockets and connect
  ai_it = ai;

  while (ai_it != NULL) {
    serverfd = socket(ai_it->ai_family, ai_it->ai_socktype, ai_it->ai_protocol);

    if (serverfd != -1) {
      // success
      if (connect(serverfd, ai_it->ai_addr, ai_it->ai_addrlen) == 0) break;

      // fail
      close(serverfd);
    }

    ai_it = ai_it->ai_next;
  }

  freeaddrinfo(ai);
  if (ai_it == NULL) {
    printf("[Thread %lu] No available socket remaining\n", tid);
    goto err;
  }

  // Read welcome message from the server
  read = get_line(serverfd, &buffer, &buflen);
  if (read <= 0) {
    printf("[Thread %lu] Error: cannot read data from server or too many customers\n", tid);
    goto err;
  }

  printf("[Thread %lu] From server: %s", tid, buffer);

  // Randomly choose which burger to order
  choice = rand() % BURGER_TYPE_MAX;
  ret = strlen(burger_names[choice]);
  printf("[Thread %lu] To server: Can I have a %s burger?\n", tid, burger_names[choice]);

  memset(buffer, 0, BUF_SIZE);
  strncpy(buffer, burger_names[choice], ret);
  buffer[ret] = '\n';

  // Send burger name to the server
  sent = put_line(serverfd, buffer, strlen(buffer));
  if (sent < 0) {
    printf("[Thread %lu] Error: cannot send data to server\n", tid);
    goto err;
  }

  // Get final message from the server
  memset(buffer, 0, BUF_SIZE);
  read = get_line(serverfd, &buffer, &buflen);
  if (read <= 0) {
    printf("[Thread %lu] Error: cannot read data from server\n", tid);
    goto err;
  }

  printf("[Thread %lu] From server: %s", tid, buffer);

err:
  close(serverfd);
  free(buffer);
  pthread_exit(NULL);
}

/// @brief program entry point
int main(int argc, char const *argv[])
{
  int i;
  int num_threads;
  pthread_t *threads;

  if (argc != 2) {
    printf("usage ./client <num_threads>\n");
    return 0;
  }

  // Create the threads equal to the num of threads passed in the argument
  num_threads = strtol(argv[1], NULL, 10);

  if (num_threads <= 0) return 0;

  threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));

  for (i = 0; i < num_threads; i++)
    pthread_create(&threads[i], NULL, thread_task, NULL);

  // Join all the threads before leaving
  for (i = 0; i < num_threads; i++)
    pthread_join(threads[i], NULL);

  free(threads);

  return 0;
}
