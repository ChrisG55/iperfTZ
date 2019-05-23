/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * iperfTZ: trusted application header
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
#ifndef IPERFTZ_TA_H
#define ITZPERF_TA_H

#define IPERFTZ_TA_UUID \
	{ 0xe649d2ad, 0x543f, 0x4220, \
		{ 0xb4, 0x8d, 0xb2, 0x60, 0xaf, 0x5d, 0xb9, 0x12} }

/* Command IDs */
enum cmd_id {
  IPERFTZ_TA_RECV,
  IPERFTZ_TA_SEND
};

#define ISPERF_ADDRSTRLEN 46
#define TCP_WINDOW_DEFAULT (16 * 1024)

struct iptz_args {
  uint32_t blksize;
  uint32_t socket_bufsize;
  uint32_t bitrate;
  uint32_t transmit_bytes;
  char ip[ISPERF_ADDRSTRLEN];
};

struct iptz_results {
  uint32_t worlds_sec;   /* world switch time seconds */
  uint32_t worlds_msec;  /* world switch time milliseconds */
  uint32_t runtime_sec;  /* runtime seconds */
  uint32_t runtime_msec; /* runtime milliseconds */
  uint32_t cycles;
  uint32_t zcycles;
  uint32_t bytes_transmitted;
};

#define BUFFER_SIZE (128 * 1024)

#endif /* IPERFTZ_TA_H */
