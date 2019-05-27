/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * iperfTZ: client application
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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <tee_client_api.h>

#include <iperfTZ_ta.h>

static int print_results(struct iptz_results *results,
			 struct iptz_args *args,
			 struct timespec *ta,
			 struct timespec *to)
{
  FILE *fp;

  printf("cycles = %" PRIu32 ", zcycles = %" PRIu32 ", bytes transmitted = %" PRIu32 ", worlds_time = %" PRIu32 ".%.3" PRIu32 " s, runtime = %" PRIu32 ".%.3" PRIu32 " s\n", results->cycles, results->zcycles, results->bytes_transmitted, results->worlds_sec, results->worlds_msec, results->runtime_sec, results->runtime_msec);

  fp = fopen("./iperfTZ-ca.csv", "a");
  if (fp == NULL) {
    perror("fopen");
    return errno;
  }
  /*
   * CSV format:
   * 1. Chunk size in KiB
   * 2. Socket buffer size in KiB
   * 3. Number of bytes transmitted
   * 4. Runtime in seconds
   * 5. Number of transmitted chunks
   * 6. Number of transmitted chunks in less than 1 millisecond
   * 7. Start time in seconds since epoch
   * 8. End time in seconds since epoch
   */  
  fprintf(fp, "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ".%.3" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%lli.%li,%lli.%li\n", args->blksize >> 10, args->socket_bufsize >> 10, results->bytes_transmitted, results->runtime_sec, results->runtime_msec, results->cycles, results->zcycles, (long long int)ta->tv_sec, ta->tv_nsec, (long long int)to->tv_sec, to->tv_nsec);
  fclose(fp);

  return 0;
}

static void init_args(struct iptz_args *args)
{
  args->blksize = TCP_WINDOW_DEFAULT;
  args->socket_bufsize = TCP_WINDOW_DEFAULT;
  args->protocol = IPERFTZ_TCP;
  args->reverse = 0;
}

static int parse_args(struct iptz_args *args,
		      char *argv[],
		      int argc)
{
  int c;
  int errflg = 0;
  unsigned long long br;
  
  while ((c = getopt(argc, argv, "b:i:l:n:ruw:")) != -1) {
    switch (c) {
    case 'b':
      br = strtoull(optarg, (char **)NULL, 10);
      if (br > UINT32_MAX)
	args->bitrate = UINT32_MAX;
      else
	args->bitrate = br;
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

int main(int argc, char *argv[])
{
  int rc;
  TEEC_Context ctx;
  TEEC_Operation op;
  TEEC_Result res;
  TEEC_Session sess;
  TEEC_SharedMemory args_sm, results_sm;
  TEEC_UUID uuid = IPERFTZ_TA_UUID;
  uint32_t command_id = IPERFTZ_TA_SEND;
  uint32_t ret_orig;
  struct iptz_args *args;
  struct iptz_results *results;
  struct timespec ta, to;

  res = TEEC_InitializeContext(NULL, &ctx);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_InitializeContext failed with code %#" PRIx32 "\n", res);
    return EXIT_FAILURE;
  }

  args_sm.size = sizeof(*args);
  args_sm.flags = TEEC_MEM_INPUT;
  res = TEEC_AllocateSharedMemory(&ctx, &args_sm);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_AllocateSharedMemory failed with code %#" PRIx32 "\n", res);
    rc = EXIT_FAILURE;
    goto shared_args_err;
  }
  
  results_sm.size = sizeof(*results);
  results_sm.flags = TEEC_MEM_OUTPUT;
  res = TEEC_AllocateSharedMemory(&ctx, &results_sm);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_AllocateSharedMemory failed with code %#" PRIx32 "\n", res);
    rc = EXIT_FAILURE;
    goto shared_results_err;
  }

  args = (struct iptz_args *)args_sm.buffer;
  init_args(args);
  rc = parse_args(args, argv, argc);
  if (rc != 0)
    goto session_err;

  if (args->reverse)
    command_id = IPERFTZ_TA_RECV;
  
  results = (struct iptz_results *)results_sm.buffer;
    
  res = TEEC_OpenSession(&ctx, &sess, &uuid,
			 TEEC_LOGIN_PUBLIC, NULL, NULL, &ret_orig);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_Opensession failed with code %#" PRIx32 " origin %#" PRIx32 "\n",
            res, ret_orig);
    rc = EXIT_FAILURE;
    goto session_err;
  }

  memset(&op, 0, sizeof(op));
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_WHOLE, TEEC_MEMREF_WHOLE,
				   TEEC_NONE, TEEC_NONE);
  op.params[0].memref.parent = &args_sm;
  op.params[0].memref.offset = 0;
  op.params[0].memref.size = args_sm.size;
  op.params[1].memref.parent = &results_sm;
  op.params[1].memref.offset = 0;
  op.params[1].memref.size = results_sm.size;

  clock_gettime(CLOCK_REALTIME, &ta);
  res = TEEC_InvokeCommand(&sess, command_id, &op, &ret_orig);
  clock_gettime(CLOCK_REALTIME, &to);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_InvokeCommand failed with code %#" PRIx32 " origin %#" PRIx32, res, ret_orig);
    rc = EXIT_FAILURE;
  } else {
    rc = print_results(results, args, &ta, &to);
  }

  TEEC_CloseSession(&sess);
 session_err:
  TEEC_ReleaseSharedMemory(&results_sm);
 shared_results_err:
  TEEC_ReleaseSharedMemory(&args_sm);
 shared_args_err:
  TEEC_FinalizeContext(&ctx);
  
  return rc;
}
