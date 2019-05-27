/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * iperfTZ: REE application
 * Copyright (C) 2019  Christian Göttel
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <netinet/tcp.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iperfTZ_ta.h>

struct args {
  size_t blksize;
  size_t socket_bufsize;
  unsigned long int transmit_bytes;
  unsigned int protocol;
  unsigned long int bitrate;
  unsigned int reverse;
};

static void init_args(struct args *args)
{
  args->blksize = TCP_WINDOW_DEFAULT;
  args->socket_bufsize = TCP_WINDOW_DEFAULT;
  args->protocol = IPERFTZ_TCP;
  args->reverse = 0;
}

static int rand_fill(struct args *args, void *buffer) {
  FILE *f;

  f = fopen("/dev/urandom", "r");
  if (f == NULL) {
    perror("fopen");
    return -1;
  }
  if (fread(buffer, sizeof(char), args->blksize, f) < args->blksize) {
    perror("fread");
    return -1;
  }
  if (fclose(f) == EOF) {
    perror("fclose");
    return -1;
  }
  return 0;
}

static char *init_buffer(struct args *args)
{
  char *buffer = (char *)calloc(args->blksize, sizeof(char));
  if (buffer == NULL)
    perror("calloc");

  if (args->reverse == 1) {
    if (rand_fill(args, buffer) == -1) {
      free(buffer);
      buffer = NULL;
    }
  }
  
  return buffer;
}

static int parse_args(struct args *args,
		      char *argv[],
		      int argc)
{
  int c;
  int errflg = 0;
  
  while ((c = getopt(argc, argv, "b:i:l:n:ruw:")) != -1) {
    switch (c) {
    case 'b':
      args->bitrate = strtoul(optarg, (char **)NULL, 10);
      break;
    case 'i':
      strncpy(args->ip, optarg, IPERFTZ_ADDRSTRLEN);
      break;
    case 'l':
      args->blksize = strtoul(optarg, (char **)NULL, 10);
      break;
    case 'n':
      args->transmit_bytes = strtoul(optarg, (char **)NULL, 10);
      break;
    case 'r':
      args->reverse = 1;
      break;
    case 'u':
      args->protocol = IPERFTZ_UDP;
      break;
    case 'w':
      args->socket_bufsize = strtoul(optarg, (char **)NULL, 10);
      if (args->socket_bufsize > ((1L<<30)-(1<<14))) {
	fprintf(stderr, "TCP window exceeds TCP sequence number limit\n");
	errflg++;
      }
      break;
    case ':':
      fprintf(stderr, "Option -%c requires an operand\n", optopt);
      errflg++;
      break;
    case '?':
      fprintf(stderr, "Unrecognized option: '-%c'\n", optopt);
      errflg++;
    }
  }
  if (errflg) {
    errno = EINVAL;
    fprintf(stderr, "usage: %s -b size -i IP -l size -n size -ru -w size\n", argv[0]);
    return EINVAL;
  }

  return 0;
}

static int print_results(struct iptz_results *results, struct args *args)
{
  FILE *fp;

  printf("cycles = %" PRIu32 ", zcycles = %" PRIu32 ", bytes transmitted = %" PRIu32 ", worlds_time = %" PRIu32 ".%.3" PRIu32 " s, runtime = %" PRIu32 ".%.3" PRIu32 " s\n", results->cycles, results->zcycles, results->bytes_transmitted, results->worlds_sec, results->worlds_msec, results->runtime_sec, results->runtime_msec);

  fp = fopen("./iperfTZ-ree.csv", "a");
  if (fp == NULL) {
    perror("fopen");
    return errno;
  }
  fprintf(fp, "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ".%.3" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n", args->blksize >> 10, args->socket_bufsize >> 10, results->bytes_transmitted, results->runtime_sec, results->runtime_msec, results->cycles, results->zcycles);
  fclose(fp);

  return 0;
}

static int socket_setup(struct args *args, int *sockfd, int *connection)
{
  int fd, val;
  int sock_type = SOCK_STREAM;
  struct sockaddr_in server_addr;
  in_port_t port = 5002;

  if (args->protocol == IPERFTZ_UDP)
    sock_type = SOCK_DGRAM;
  
  *sockfd = socket(AF_INET, sock_type, 0);
  if (*sockfd == -1) {
    perror("socket");
    return *sockfd;
  }

  if (args->protocol == IPERFTZ_TCP) {
    if (setsockopt(*sockfd, SOL_SOCKET, SO_SNDBUF, &args->socket_bufsize, sizeof(args->socket_bufsize)) == -1) {
      perror("setsockopt");
      return -1;
    }
  }
  
  memset(&server_addr, 0, sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);
  if (bind(*sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
    perror("bind");
    return -1;
  }

  if (args->protocol == IPERFTZ_TCP) {
    if (tcp_connect(&server_addr, connection, *sockfd) != 0)
      return -1;
    fd = *connection;
  } else {
    fd = *sockfd;
  }

  if ((val = fcntl(fd, F_GETFD, 0)) == -1) {
    perror("fcntl");
    return -1;
  }

  val |= O_NONBLOCK;
  
  if ((val = fcntl(fd, F_SETFL, val)) == -1) {
    perror("fcntl");
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  char *buffer;
  int rc = EXIT_SUCCESS;
  int connection, sockfd;
  struct args args;
  struct iptz_results results;
  
  init_args(&args);
  rc = parse_args(&args, argv, argc);
  if (rc != 0)
    return rc;

  buffer = init_buffer(&args);
  if (buffer == NULL)
    return EXIT_FAILURE;

  rc = socket_setup(&args, &sockfd, &connection);
  if (rc != 0)
    goto cleanup;
  
  if (args->reverse)
    command_id = IPERFTZ_TA_RECV;
  
  res = TEEC_InvokeCommand(&sess, command_id, &op, &ret_orig);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_InvokeCommand failed with code %#" PRIx32 " origin %#" PRIx32, res, ret_orig);
    rc = EXIT_FAILURE;
  } else {
    rc = print_results(results, args);
  }

 cleanup:
  free(buffer);
  if (args.protocol == IPERFTZ_TCP)
    close(connection);
  close(sockfd);
  
  return rc;
}
