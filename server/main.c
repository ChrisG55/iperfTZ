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
  unsigned long int bitrate;
  unsigned int reverse;
};

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

static void init_args(struct args *args)
{
  args->blksize = TCP_WINDOW_DEFAULT;
  args->socket_bufsize = TCP_WINDOW_DEFAULT;
  args->transmit_bytes = 0;
  args->protocol = ISPERF_TCP;
  args->reverse = 0;
  args->bitrate = 0;
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

  while ((c = getopt(argc, argv, "b:l:n:ruw:")) != -1) {
    switch (c) {
    case 'b':
      args->bitrate = strtoul(optarg, (char **)NULL, 10);
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
    fprintf(stderr, "usage: %s -b rate -l size -n size -ru -w size\n", argv[0]);
    return EINVAL;
  }

  return 0;
}

static int tcp_connect(struct sockaddr_in *server_addr,
		       int *connection,
		       int sockfd)
{
  socklen_t addrlen;
  
  if (listen(sockfd, 5) == -1) {
    perror("listen");
    return -1;
  }
  
  addrlen = sizeof(struct sockaddr);
  if ((*connection = accept(sockfd, (struct sockaddr *)server_addr, &addrlen)) == -1) {
    perror("accept");
    close(sockfd);
    return -1;
  }

  return 0;
}

static int tcp_print_results(int connection)
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

static int tcp_recv(struct args *args, int connection, char *buffer)
{
  ssize_t bytes_transmitted = 0;
  ssize_t n;
  long long net_ns = 0;
  struct timespec ta, ti, tj, to;
  long long td;
  
  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    clock_gettime(CLOCK_REALTIME, &ti);
    n = read(connection, buffer, args->blksize);
    clock_gettime(CLOCK_REALTIME, &tj);
    if (n == -1) {
      switch (errno) {
      case EAGAIN:
	goto again;
      case ETIMEDOUT:
	puts("Transmission timeout occurred");
	return errno;
      default:
	perror("read");
	return errno;
      }
    }
    net_ns += (tj.tv_sec - ti.tv_sec) * 1000000000LL + tj.tv_nsec - ti.tv_nsec;
    bytes_transmitted += n;
  again:
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while (((args->transmit_bytes == 0) && (td < 10000000000LL)) ||
	   ((args->transmit_bytes > 0) && (bytes_transmitted < args->transmit_bytes)));
  
  printf("bytes transmitted: %zd B\nnet time: %lli ns\nruntime = %lli ns\n", bytes_transmitted, net_ns, td);

  // Drain the connection
  puts("Draining the connection for 2 seconds");
  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    n = read(connection, buffer, args->blksize);
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while ((td < 2000000000LL) || (n > 0));

  return 0;
}  

static int tcp_send(struct args *args, int connection, char *buffer)
{
  ssize_t bytes_transmitted = 0;
  ssize_t n;
  long long net_ns = 0;
  struct timespec ta, ti, tj, to;
  long long td = 1;

  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    ssize_t bytes = 0;

    if ((args->bitrate == 0) || (args->bitrate > ((bytes_transmitted + args->blksize) * 8 / td * 1000000000LL))) {
      clock_gettime(CLOCK_REALTIME, &ti);
      do {
	n = write(connection, buffer, args->blksize);
	bytes += n;
      } while ((bytes < args->blksize) && (n != -1));
      clock_gettime(CLOCK_REALTIME, &tj);
      if ((n == -1) && (errno == EAGAIN)) {
	goto again;
      } else if (n == -1) {
	perror("write");
	return errno;
      }
      net_ns += (tj.tv_sec - ti.tv_sec) * 1000000000LL + tj.tv_nsec - ti.tv_nsec;
      bytes_transmitted += bytes;
    }

  again:
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while (((args->transmit_bytes == 0) && (td < 10000000000LL)) ||
	   ((args->transmit_bytes > 0) && (bytes_transmitted < args->transmit_bytes)));

  printf("bytes transmitted: %zd B\nnet time: %lli ns\nruntime = %lli ns\n", bytes_transmitted, net_ns, td);

  return 0;
}

static int socket_setup(struct args *args, int *sockfd, int *connection)
{
  int fd, val;
  int sock_type = SOCK_STREAM;
  struct sockaddr_in server_addr;
  in_port_t port = 5002;

  if (args->protocol == ISPERF_UDP)
    sock_type = SOCK_DGRAM;
  
  *sockfd = socket(AF_INET, sock_type, 0);
  if (*sockfd == -1) {
    perror("socket");
    return *sockfd;
  }

  if (args->protocol == ISPERF_TCP) {
    if (setsockopt(*sockfd, SOL_SOCKET, SO_RCVBUF, &args->socket_bufsize, sizeof(args->socket_bufsize)) == -1) {
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

  if (args->protocol == ISPERF_TCP) {
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

static int udp_send(struct args *args, int sockfd, char *buffer)
{
  socklen_t addrlen;
  ssize_t bytes_transmitted = 0;
  struct sockaddr_in client_addr;
  ssize_t n;
  long long net_ns = 0;
  struct timespec ta, ti, tj, to;
  long long td = 1;

  /* Wait for some datagrams before sending */
  do {
    n = recvfrom(sockfd, buffer, args->blksize, 0, (struct sockaddr *)&client_addr, &addrlen);
  } while (n <= 0);
  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    ssize_t bytes = 0;

    if ((args->bitrate == 0) || (args->bitrate > ((bytes_transmitted + args->blksize) * 8 / td * 1000000000LL))) {
      clock_gettime(CLOCK_REALTIME, &ti);
      do {
	n = sendto(sockfd, buffer, args->blksize, 0, (struct sockaddr *)&client_addr, addrlen);
	bytes += n;
      } while ((bytes < args->blksize) && (n != -1));
      clock_gettime(CLOCK_REALTIME, &tj);
      if ((n == -1) && (errno == EAGAIN)) {
	goto again;
      } else {
	perror("sendto");
	return errno;
      }
      net_ns += (tj.tv_sec - ti.tv_sec) * 1000000000LL + tj.tv_nsec - ti.tv_nsec;
      bytes_transmitted += bytes;
    }

  again:
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while (((args->transmit_bytes == 0) && (td < 10000000000LL)) ||
	   ((args->transmit_bytes > 0) && (bytes_transmitted < args->transmit_bytes)));

  printf("bytes transmitted: %zd B\nnet time: %lli ns\nruntime = %lli ns\n", bytes_transmitted, net_ns, td);

  return 0;
}

static int udp_recv(struct args *args, int sockfd, char *buffer)
{
  socklen_t addrlen;
  ssize_t bytes_transmitted = 0;
  struct sockaddr_in client_addr;
  ssize_t n;
  long long net_ns = 0;
  struct timespec ta, ti, tj, to;
  long long td;

  do {
    n = recvfrom(sockfd, buffer, args->blksize, MSG_PEEK, (struct sockaddr *)&client_addr, &addrlen);
  } while (n <= 0);
  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    clock_gettime(CLOCK_REALTIME, &ti);
    n = recvfrom(sockfd, buffer, args->blksize, 0, (struct sockaddr *)&client_addr, &addrlen);
    clock_gettime(CLOCK_REALTIME, &tj);
    if (n == -1) {
      switch (errno) {
      case EAGAIN:
	goto again;
      case ETIMEDOUT:
	puts("Transmission timeout occurred");
	return errno;
      default:
	perror("recvfrom");
	return errno;
      }
    }
    net_ns += (tj.tv_sec - ti.tv_sec) * 1000000000LL + tj.tv_nsec - ti.tv_nsec;
    bytes_transmitted += n;
  again:
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while (((args->transmit_bytes == 0) && (td < 10000000000LL)) ||
	   ((args->transmit_bytes > 0) && (bytes_transmitted < args->transmit_bytes)));
  
  printf("bytes transmitted: %zd B\nnet time: %lli ns\nruntime = %lli ns\n", bytes_transmitted, net_ns, td);

  // Drain the connection
  puts("Draining the connection for 2 seconds");
  clock_gettime(CLOCK_REALTIME, &ta);
  do {
    n = recvfrom(sockfd, buffer, args->blksize, 0, (struct sockaddr *)&client_addr, &addrlen);
    clock_gettime(CLOCK_REALTIME, &to);
    td = (to.tv_sec - ta.tv_sec) * 1000000000LL + to.tv_nsec - ta.tv_nsec;
  } while ((td < 2000000000LL) || (n > 0));

  return 0;
}

int main(int argc, char *argv[])
{
  char *buffer;
  int rc = EXIT_SUCCESS;
  int connection, sockfd;
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

  rc = socket_setup(&args, &sockfd, &connection);
  if (rc != 0)
    goto cleanup;
  
  if (args.protocol == ISPERF_TCP) {
    if (args.reverse == 0)
      rc = tcp_recv(&args, connection, buffer);
    else
      rc = tcp_send(&args, connection, buffer);
    if (rc == 0)
      rc = tcp_print_results(connection);
  } else {
    if (args.reverse == 0)
      rc = udp_recv(&args, sockfd, buffer);
    else
      rc = udp_send(&args, sockfd, buffer);
  }
  
 cleanup:
  free(buffer);
  if (args.protocol == ISPERF_TCP)
    close(connection);
  close(sockfd);
  
  return rc;
}
