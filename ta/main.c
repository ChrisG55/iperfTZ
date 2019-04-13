/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * iperfTZ: trusted application
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
#include <string.h>

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <tee_tcpsocket.h>
#include <__tee_tcpsocket_defines_extensions.h>

#include <iperfTZ_ta.h>

#define BUFFER_SIZE (128 * 1024)
#define INET_ADDRSTRLEN 16

static void init_results(struct iptz_results *results)
{
  results->cycles = 0;
  results->worlds_sec = 0;
  results->worlds_msec = 0;
}

static TEE_Result tcp_connect(TEE_tcpSocket_Setup *setup,
			      TEE_iSocketHandle *ctx,
			      uint32_t commandCode)
{
  uint32_t bufsz = BUFFER_SIZE;
  uint32_t buflen = sizeof(bufsz);
  const char *ip = "127.0.0.1";
  uint32_t protocolError;
  TEE_Result res;

  setup->ipVersion = TEE_IP_VERSION_DC;
  setup->server_addr = (char *)TEE_Malloc(INET_ADDRSTRLEN, TEE_MALLOC_FILL_ZERO);
  if (setup->server_addr == NULL)
    return TEE_ERROR_OUT_OF_MEMORY;
  TEE_MemMove(setup->server_addr, ip, INET_ADDRSTRLEN);
  setup->server_port = 5002U;

  res = TEE_tcpSocket->open(ctx, setup, &protocolError);
  if (res != TEE_SUCCESS) {
    EMSG("open() failed for TCP. Return code: %#0" PRIX32
	 ", protocol error: %#0" PRIX32, res, protocolError);
    goto socket_err;
  }

  res = TEE_tcpSocket->ioctl(*ctx, commandCode, &bufsz, &buflen);
  if (res != TEE_SUCCESS) {
    EMSG("ioctl() failed for TCP. Return code: %#0" PRIX32
	 ", bufsz = %" PRIX32, res, bufsz);
    goto socket_err;
  }

  return TEE_SUCCESS;

 socket_err:
  TEE_Free(setup->server_addr);
  return res;
}

static TEE_Result iperfTZ_run(uint32_t param_types, TEE_Param params[4])
{
  TEE_iSocket *socket = NULL;
  TEE_iSocketHandle socketCtx;
  TEE_Result res;
  TEE_tcpSocket_Setup tcpSetup;
  TEE_Time t0, t1;
  char buffer[BUFFER_SIZE];
  uint32_t bufsz = BUFFER_SIZE;
  uint32_t bytes = 0;
  struct iptz_results *results;
  uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
					     TEE_PARAM_TYPE_MEMREF_OUTPUT,
					     TEE_PARAM_TYPE_NONE,
					     TEE_PARAM_TYPE_NONE);
  if (param_types != exp_param_types)
    return TEE_ERROR_BAD_PARAMETERS;

  results = (struct iptz_results *)params[1].memref.buffer;
  
  TEE_GenerateRandom(buffer, bufsz);

  socket = TEE_tcpSocket;
  res = tcp_connect(&tcpSetup, &socketCtx, TEE_TCP_SET_SENDBUF);
  if (res != TEE_SUCCESS)
    return res;
  
  init_results(results);

  while ((bytes < BUFFER_SIZE) && (res == TEE_SUCCESS)) {
    uint32_t diff;
    
    bufsz = BUFFER_SIZE - bytes;
    TEE_GetSystemTime(&t0);
    res = socket->send(socketCtx, buffer + bytes, &bufsz, 1);
    TEE_GetSystemTime(&t1);
    
    diff = t1.millis - t0.millis;
    if (diff > t1.millis) {
      results->worlds_sec += t1.seconds - t0.seconds - 1;
      results->worlds_msec += t1.millis + (0xffffffff - diff);
    } else {
      results->worlds_sec += t1.seconds - t0.seconds;
      results->worlds_msec += diff;
    }

    while (results->worlds_msec >= 1000) {
      results->worlds_sec++;
      results->worlds_msec -= 1000;
    }
    
    bytes += bufsz;
    results->cycles++;
  }

  socket->close(socketCtx);

  TEE_Free(tcpSetup.server_addr);
  
  if (res != TEE_SUCCESS)
    EMSG("send() failed for socket. Return code: %#0" PRIX32, res);
  
  return res;
}

/*
 * Called when the instance of the TA is created. This is the first call in
 * the TA.
 */
TEE_Result TA_CreateEntryPoint(void)
{
	DMSG("has been called");

	return TEE_SUCCESS;
}

/*
 * Called when the instance of the TA is destroyed if the TA has not
 * crashed or panicked. This is the last call in the TA.
 */
void TA_DestroyEntryPoint(void)
{
	DMSG("has been called");
}

/*
 * Called when a new session is opened to the TA. *sess_ctx can be updated
 * with a value to be able to identify this session in subsequent calls to the
 * TA. In this function you will normally do the global initialization for the
 * TA.
 */
TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
		TEE_Param __maybe_unused params[4],
		void __maybe_unused **sess_ctx)
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE);

	DMSG("has been called");

	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Unused parameters */
	(void)&params;
	(void)&sess_ctx;

	/* If return value != TEE_SUCCESS the session will not be created. */
	return TEE_SUCCESS;
}

/*
 * Called when a session is closed, sess_ctx hold the value that was
 * assigned by TA_OpenSessionEntryPoint().
 */
void TA_CloseSessionEntryPoint(void __maybe_unused *sess_ctx)
{
	(void)&sess_ctx; /* Unused parameter */
	DMSG("has been called");
}

/*
 * Called when a TA is invoked. sess_ctx hold that value that was
 * assigned by TA_OpenSessionEntryPoint(). The rest of the paramters
 * comes from normal world.
 */
TEE_Result TA_InvokeCommandEntryPoint(void __maybe_unused *sess_ctx,
			uint32_t cmd_id,
			uint32_t param_types, TEE_Param params[4])
{
	(void)&sess_ctx; /* Unused parameter */

	switch (cmd_id) {
	case IPERFTZ_TA_RUN:
		return iperfTZ_run(param_types, params);
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}
