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
#include <unistd.h>

#include <tee_client_api.h>

#include <iperfTZ_ta.h>

static int print_results(struct iptz_results *results, struct iptz_args *args)
{
  FILE *fp;

  printf("cycles = %" PRIu32 ", zcycles = %" PRIu32 ", bytes transmitted = %" PRIu32 ", worlds_time = %" PRIu32 ".%.3" PRIu32 " s, runtime = %" PRIu32 ".%.3" PRIu32 " s\n", results->cycles, results->zcycles, results->bytes_transmitted, results->worlds_sec, results->worlds_msec, results->runtime_sec, results->runtime_msec);

  fp = fopen("./iperfTZ-ca.csv", "a");
  if (fp == NULL) {
    perror("fopen");
    return errno;
  }
  fprintf(fp, "%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ".%.3" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n", args->blksize >> 10, args->socket_bufsize >> 10, results->bytes_transmitted, results->runtime_sec, results->runtime_msec, results->cycles, results->zcycles);
  fclose(fp);

  return 0;
}

static void init_args(struct iptz_args *args)
{
  args->blksize = TCP_WINDOW_DEFAULT;
  args->socket_bufsize = TCP_WINDOW_DEFAULT;
}

static int parse_args(struct iptz_args *args,
		      char *argv[],
		      int argc)
{
  int c;
  int errflg = 0;
  
  while ((c = getopt(argc, argv, "b:i:l:n:w:")) != -1) {
    switch (c) {
    case 'b':
      args->bitrate = strtoul(optarg, (char **)NULL, 10);
      break;
    case 'i':
      strncpy(args->ip, optarg, ISPERF_ADDRSTRLEN);
      break;
    case 'l':
      args->blksize = strtoul(optarg, (char **)NULL, 10);
      break;
    case 'n':
      args->transmit_bytes = strtoul(optarg, (char **)NULL, 10);
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
    fprintf(stderr, "usage: %s -b size -i IP -l size -n size -w size\n", argv[0]);
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
  uint32_t ret_orig;
  struct iptz_args *args;
  struct iptz_results *results;

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

  res = TEEC_InvokeCommand(&sess, IPERFTZ_TA_SEND, &op, &ret_orig);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_InvokeCommand failed with code %#" PRIx32 " origin %#" PRIx32, res, ret_orig);
    rc = EXIT_FAILURE;
  } else {
    rc = print_results(results, args);
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
