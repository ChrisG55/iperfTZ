/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * iperfTZ: server application
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

#include <iperfTZ_ta.h>

struct args {
  size_t blksize;
  size_t socket_bufsize;
  unsigned long int transmit_bytes;
  unsigned int protocol;
};

static void init_args(struct args *args)
{
  args->blksize = TCP_WINDOW_DEFAULT;
  args->socket_bufsize = TCP_WINDOW_DEFAULT;
  args->transmit_bytes = 0;
  args->protocol = ISPERF_TCP;
}

static char *init_buffer(struct args *args)
{
  char *buffer = (char *)calloc(args->blksize, sizeof(char));
  if (buffer == NULL)
    perror("calloc");

  return buffer;
}

static int parse_args(struct args *args,
		      char *argv[],
		      int argc)
{
  int c;
  int errflg = 0;

  while ((c = getopt(argc, argv, "l:n:uw:")) != -1) {
    switch (c) {
    case 'l':
      args->blksize = strtoul(optarg, (char **)NULL, 10);
      break;
    case 'n':
      args->transmit_bytes = strtoul(optarg, (char **)NULL, 10);
      break;
    case 'u':
      args->protocol = ISPERF_UDP;
      break;
    case 'w':
      args->socket_bufsize = strtoul(optarg, (char **)NULL, 10);
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
    fprintf(stderr, "usage: %s -l size -n size -u -w size\n", argv[0]);
    return EINVAL;
  }

  return 0;
}

static int print_results(int connection)
{
  FILE *fp;
  struct tcp_info info;
  int rc = 0;
  socklen_t tcpilen;

  tcpilen = sizeof(info);
  rc = getsockopt(connection, IPPROTO_TCP, TCP_INFO, &info, &tcpilen);
  if (rc == -1) {
    perror("getsockopt");
  } else {
    printf("TCP backoff: %u\nTCP retransmits: %" PRIu8 "\nTCP window scaling received from sender: %" PRIu8 " (implicit scale factor) [RFC 1323]\nTCP send MSS: %" PRIu32 " B\nTCP slow start size threshold (snd_ssthresh): %" PRIu32 " B (2147483647 == -1)\nTCP send congestion window: %" PRIu32 " (highest seq num + min({cwnd,rwnd})) [RFC 2581]\nTCP window scaling to send to receiver: %" PRIu8 " (implicit scale factor) [RFC 1323]\nTCP recv MSS: %" PRIu32 " B\nTCP current window clamp (rcv_ssthresh): %" PRIu32 " B\nTCP retransmitted packets out: %" PRIu32 "\nTCP smoothed round trip time: %" PRIu32 " us\nTCP smoothed round trip time medium deviation: %" PRIu32 " us\nTCP advertised MSS: %" PRIu32 " B\n", info.tcpi_backoff, info.tcpi_retransmits, info.tcpi_snd_wscale, info.tcpi_snd_mss, info.tcpi_snd_ssthresh, info.tcpi_snd_cwnd, info.tcpi_rcv_wscale, info.tcpi_rcv_mss, info.tcpi_rcv_ssthresh, info.tcpi_retrans, info.tcpi_rtt, info.tcpi_rttvar, info.tcpi_advmss);
  }

  fp = fopen("./iperfTZ.csv", "a");
  if (fp == NULL) {
    perror("fopen");
    return errno;
  }
  fprintf(fp, "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n", info.tcpi_rtt, info.tcpi_rttvar, info.tcpi_snd_mss, info.tcpi_rcv_mss, info.tcpi_advmss, info.tcpi_rcv_ssthresh);
  fclose(fp);

  return rc;
}

static void tcp_destroy(int sockfd, int connection)
{
  close(connection);
  close(sockfd);
}

static int tcp_setup(struct args *args, int *sockfd, int *connection)
{
  int val;
  struct sockaddr_in server_addr;
  in_port_t port = 5002;
  socklen_t addrlen;
  
  *sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (*sockfd == -1) {
    perror("socket");
    return *sockfd;
  }

  if (setsockopt(*sockfd, SOL_SOCKET, SO_RCVBUF, &args->socket_bufsize, sizeof(args->socket_bufsize)) == -1) {
    perror("setsockopt");
    return -1;
  }
  
  memset(&server_addr, 0, sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(port);
  if (bind(*sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
    perror("bind");
    return -1;
  }

  if (listen(*sockfd, 5) == -1) {
    perror("listen");
    return -1;
  }
  
  addrlen = sizeof(struct sockaddr);
  if ((*connection = accept(*sockfd, (struct sockaddr *)&server_addr, &addrlen)) == -1) {
    perror("accept");
    close(*sockfd);
    return -1;
  }

  if ((val = fcntl(*connection, F_GETFD, 0)) == -1) {
    perror("fcntl");
    tcp_destroy(*sockfd, *connection);
    return -1;
  }

  val |= O_NONBLOCK;
  
  if ((val = fcntl(*connection, F_SETFL, val)) == -1) {
    perror("fcntl");
    tcp_destroy(*sockfd, *connection);
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  char *buffer;
  int rc = EXIT_SUCCESS;
  int connection, sockfd;
  long long td;
  long long net_ns = 0;
  ssize_t bytes_transmitted = 0, n;
  struct timespec ta, ti, tj, to;
  struct args args;

  init_args(&args);
  rc = parse_args(&args, argv, argc);
  if (rc != 0)
    return rc;
  
  printf("Block size = %zd\nBuffer size = %zd\n", args.blksize, args.socket_bufsize);
  if (args.transmit_bytes > 0)
    printf("Bytes to transmit = %ld\n", args.transmit_bytes);
  
  buffer = init_buffer(&args);
  if (buffer == NULL)
    return EXIT_FAILURE;

  rc = tcp_setup(&args, &sockfd, &connection);
  if (rc != 0)
    goto cleanup;
  
  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    clock_gettime(CLOCK_REALTIME, &ti);
    n = read(connection, buffer, args.blksize);
    clock_gettime(CLOCK_REALTIME, &tj);
    if (n == -1) {
      switch (errno) {
      case EAGAIN:
	goto again;
      case ETIMEDOUT:
	puts("Transmission timeout occurred");
	rc = errno;
	goto cleanup;
      default:
	perror("read");
	rc = errno;
	goto cleanup;
      }
    }
    net_ns += (tj.tv_sec - ti.tv_sec) * 1000000000LL + tj.tv_nsec - ti.tv_nsec;
    bytes_transmitted += n;
  again:
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while (((args.transmit_bytes == 0) && (td < 10000000000LL)) ||
	   ((args.transmit_bytes > 0) && (bytes_transmitted < args.transmit_bytes)));
  
  printf("bytes transmitted: %zd B\nnet time: %lli ns\nruntime = %lli ns\n", bytes_transmitted, net_ns, td);

  // Drain the connection
  puts("Draining the connection for 2 seconds");
  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    n = read(connection, buffer, args.blksize);
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while ((td < 2000000000LL) || (n > 0));

  rc = print_results(connection);
  
 cleanup:
  free(buffer);
  tcp_destroy(sockfd, connection);
  
  return rc;
}
