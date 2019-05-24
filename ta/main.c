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
#include <tee_udpsocket.h>

#include <iperfTZ_ta.h>

static void init_results(struct iptz_results *results)
{
  results->cycles = 0;
  results->zcycles = 0;
  results->bytes_transmitted = 0;
  results->worlds_sec = 0;
  results->worlds_msec = 0;
  results->runtime_sec = 0;
  results->runtime_msec = 1;
}

static TEE_Result tcp_connect(TEE_tcpSocket_Setup *setup,
			      TEE_iSocketHandle *ctx,
			      struct iptz_args *args,
			      uint32_t commandCode)
{
  uint32_t buflen = sizeof(args->socket_bufsize);
  uint32_t bufsz = args->socket_bufsize;
  uint32_t protocolError;
  TEE_Result res;

  setup->ipVersion = TEE_IP_VERSION_DC;
  setup->server_addr = args->ip;
  setup->server_port = 5002U;

  res = TEE_tcpSocket->open(ctx, setup, &protocolError);
  if (res != TEE_SUCCESS) {
    EMSG("open() failed for TCP. Return code: %#0" PRIX32
	 ", protocol error: %#0" PRIX32, res, protocolError);
    return res;
  }

  res = TEE_tcpSocket->ioctl(*ctx, commandCode, &bufsz, &buflen);
  if (res != TEE_SUCCESS) {
    EMSG("ioctl() failed for TCP. Return code: %#0" PRIX32
	 ", socket_bufsize = %" PRIX32, res, bufsz);
    return res;
  }

  return TEE_SUCCESS;
}

static TEE_Result udp_connect(TEE_udpSocket_Setup *setup,
			      TEE_iSocketHandle *ctx,
			      struct iptz_args *args)
{
  TEE_Result res;
  uint32_t protocolError;
  
  setup->ipVersion = TEE_IP_VERSION_DC;
  setup->server_addr = args->ip;
  setup->server_port = 5002U;

  res = TEE_udpSocket->open(ctx, setup, &protocolError);
  if (res != TEE_SUCCESS) {
    EMSG("open() failed for UDP. Return code: %#0" PRIX32
	 ", protocol error: %#0" PRIX32, res, protocolError);
    return res;
  }

  return TEE_SUCCESS;
}

static char *init_buffer(struct iptz_args *args)
{
  char *buffer;
  buffer = (char *)TEE_Malloc(args->blksize, TEE_MALLOC_FILL_ZERO);
  if (buffer == NULL)
    return buffer;
  
  TEE_GenerateRandom(buffer, args->blksize);
  return buffer;
}

static TEE_Result iperfTZ_recv(uint32_t param_types, TEE_Param params[4])
{
  TEE_iSocket *socket = NULL;
  TEE_iSocketHandle socketCtx;
  TEE_Result res;
  TEE_tcpSocket_Setup tcpSetup;
  TEE_udpSocket_Setup udpSetup;
  TEE_Time ta, ti, to;
  struct iptz_args *args;
  char *buffer;
  uint32_t buflen;
  uint32_t diff;
  struct iptz_results *results;
  uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					     TEE_PARAM_TYPE_MEMREF_OUTPUT,
					     TEE_PARAM_TYPE_NONE,
					     TEE_PARAM_TYPE_NONE);
  if (param_types != exp_param_types)
    return TEE_ERROR_BAD_PARAMETERS;

  args = (struct iptz_args *)params[0].memref.buffer;
  results = (struct iptz_results *)params[1].memref.buffer;

  buffer = init_buffer(args);
  if (buffer == NULL)
    return TEE_ERROR_OUT_OF_MEMORY;

  if (args->protocol == IPERFTZ_TCP) {
    socket = TEE_tcpSocket;
    res = tcp_connect(&tcpSetup, &socketCtx, args, TEE_TCP_SET_RECVBUF);
  } else {
    socket = TEE_udpSocket;
    res = udp_connect(&udpSetup, &socketCtx, args);
  }
  if (res != TEE_SUCCESS)
    return res;

  init_results(results);

  /* Send some datagrams first to "synchronize" with the server */
  if (args->protocol == IPERFTZ_UDP) {
    buflen = args->blksize < 1024 ? args->blksize : 1024;
    socket->send(socketCtx, buffer, &buflen, 0);
  }
  
  TEE_GetSystemTime(&ta);
  do {
    uint32_t bytes = 0;

    TEE_GetSystemTime(&ti);
    do {
      buflen = args->blksize - bytes;
      res = socket->recv(socketCtx, buffer + bytes, &buflen, 0);
      bytes += buflen;
    } while ((bytes < args->blksize) && (res == TEE_SUCCESS));
    TEE_GetSystemTime(&to);
    
    diff = to.millis - ti.millis;
    if (diff > to.millis) {
      results->worlds_sec += to.seconds - ti.seconds - 1;
      results->worlds_msec += to.millis + (0xffffffff - diff);
    } else {
      uint32_t seconds;
      seconds = to.seconds - ti.seconds;
      results->worlds_sec += seconds;
      results->worlds_msec += diff;
      if ((seconds == 0) && (diff == 0))
	results->zcycles++;
    }

    while (results->worlds_msec >= 1000) {
      results->worlds_sec++;
      results->worlds_msec -= 1000;
    }
    
    results->cycles++;
    results->bytes_transmitted += bytes;

    diff = to.millis - ta.millis;
    if (diff > to.millis) {
      results->runtime_sec = to.seconds - ta.seconds - 1;
      results->runtime_msec = to.millis + (0xffffffff - diff);
    } else {
      results->runtime_sec = to.seconds - ta.seconds;
      results->runtime_msec = diff;
    }
  } while (((args->transmit_bytes == 0) && (results->runtime_sec < 10)) ||
	   (((args->transmit_bytes > 0) && (results->bytes_transmitted < args->transmit_bytes)) &&
	    (res == TEE_SUCCESS)));

  socket->close(socketCtx);

  if (res != TEE_SUCCESS)
    EMSG("recv() failed for socket. Return code: %#0" PRIX32, res);

  return res;
}

static TEE_Result iperfTZ_send(uint32_t param_types, TEE_Param params[4])
{
  TEE_iSocket *socket = NULL;
  TEE_iSocketHandle socketCtx;
  TEE_Result res;
  TEE_tcpSocket_Setup tcpSetup;
  TEE_udpSocket_Setup udpSetup;
  TEE_Time ta, ti, to;
  char *buffer;
  uint32_t buflen;
  struct iptz_args *args;
  struct iptz_results *results;
  uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
					     TEE_PARAM_TYPE_MEMREF_OUTPUT,
					     TEE_PARAM_TYPE_NONE,
					     TEE_PARAM_TYPE_NONE);
  if (param_types != exp_param_types)
    return TEE_ERROR_BAD_PARAMETERS;

  args = (struct iptz_args *)params[0].memref.buffer;
  results = (struct iptz_results *)params[1].memref.buffer;

  buffer = init_buffer(args);
  if (buffer == NULL)
    return TEE_ERROR_OUT_OF_MEMORY;

  if (args->protocol == IPERFTZ_TCP) {
    socket = TEE_tcpSocket;
    res = tcp_connect(&tcpSetup, &socketCtx, args, TEE_TCP_SET_SENDBUF);
  } else {
    socket = TEE_udpSocket;
    res = udp_connect(&udpSetup, &socketCtx, args);
  }
  if (res != TEE_SUCCESS)
    return res;
  
  init_results(results);

  TEE_GetSystemTime(&ta);
  do {
    uint32_t bytes;
    uint32_t diff;

    if ((args->bitrate == 0U) || (args->bitrate > ((results->bytes_transmitted + args->blksize) * 8 / (results->runtime_sec * 1000 + results->runtime_msec) * 1000))) {
      TEE_GetSystemTime(&ti);
      bytes = 0;
      do {
	buflen = args->blksize - bytes;
	res = socket->send(socketCtx, buffer + bytes, &buflen, TEE_TIMEOUT_INFINITE);
	bytes += buflen;
      } while ((bytes < args->blksize) && (res == TEE_SUCCESS));
      TEE_GetSystemTime(&to);
        
      diff = to.millis - ti.millis;
      if (diff > to.millis) {
	results->worlds_sec += to.seconds - ti.seconds - 1;
	results->worlds_msec += to.millis + (0xffffffff - diff);
      } else {
	uint32_t seconds;
	seconds = to.seconds - ti.seconds;
	results->worlds_sec += seconds;
	results->worlds_msec += diff;
	if ((seconds == 0) && (diff == 0))
	  results->zcycles++;
      }

      while (results->worlds_msec >= 1000) {
	results->worlds_sec++;
	results->worlds_msec -= 1000;
      }
    
      results->cycles++;
      results->bytes_transmitted += bytes;
    } else {
      TEE_GetSystemTime(&to);
    }

    diff = to.millis - ta.millis;
    if (diff > to.millis) {
      results->runtime_sec = to.seconds - ta.seconds - 1;
      results->runtime_msec = to.millis + (0xffffffff - diff);
    } else {
      results->runtime_sec = to.seconds - ta.seconds;
      results->runtime_msec = diff;
    }
  } while (((args->transmit_bytes == 0) && (results->runtime_sec < 10)) ||
	   (((args->transmit_bytes > 0) && (results->bytes_transmitted < args->transmit_bytes)) &&
	    (res == TEE_SUCCESS)));

  socket->close(socketCtx);

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
	case IPERFTZ_TA_RECV:
	  return iperfTZ_recv(param_types, params);
	case IPERFTZ_TA_SEND:
	  return iperfTZ_send(param_types, params);
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}
