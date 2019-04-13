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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tee_client_api.h>

#include <iperfTZ_ta.h>

static void print_results(struct iptz_results *results)
{
  printf("cycles = %" PRIu32 ", worlds_time = %" PRIu32 ".%.3" PRIu32 " s\n", results->cycles, results->worlds_sec, results->worlds_msec);
}

int main(void)
{
  int rc = EXIT_SUCCESS;
  TEEC_Result res;
  TEEC_Context ctx;
  TEEC_Session sess;
  TEEC_Operation op;
  TEEC_SharedMemory results_sm;
  TEEC_UUID uuid = IPERFTZ_TA_UUID;
  uint32_t ret_orig;
  struct iptz_results *results;

  res = TEEC_InitializeContext(NULL, &ctx);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_InitializeContext failed with code %#" PRIx32 "\n", res);
    return EXIT_FAILURE;
  }

  results_sm.size = sizeof(*results);
  results_sm.flags = TEEC_MEM_OUTPUT;
  res = TEEC_AllocateSharedMemory(&ctx, &results_sm);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_AllocateSharedMemory failed with code %#" PRIx32 "\n", res);
    rc = EXIT_FAILURE;
    goto shared_mem_err;
  }
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
  op.paramTypes = TEEC_PARAM_TYPES(TEEC_NONE, TEEC_MEMREF_WHOLE,
				   TEEC_NONE, TEEC_NONE);
  op.params[1].memref.parent = &results_sm;
  op.params[1].memref.offset = 0;
  op.params[1].memref.size = results_sm.size;

  res = TEEC_InvokeCommand(&sess, IPERFTZ_TA_RUN, &op, &ret_orig);
  if (res != TEEC_SUCCESS) {
    fprintf(stderr, "TEEC_InvokeCommand failed with code %#" PRIx32 " origin %#" PRIx32, res, ret_orig);
    rc = EXIT_FAILURE;
  } else {
    print_results(results);
  }

  TEEC_CloseSession(&sess);
 session_err:
  TEEC_ReleaseSharedMemory(&results_sm);
 shared_mem_err:
  TEEC_FinalizeContext(&ctx);

  return rc;
}
