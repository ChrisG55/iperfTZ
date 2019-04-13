/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * iperfTZ: trusted application definitions header
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

#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <iperfTZ_ta.h>

#define TA_DATA_SIZE   (256 * 1024) /* heap size */
#define TA_DESCRIPTION "Generic Interface Socket Trusted Application"
#define TA_FLAGS       TA_FLAG_EXEC_DDR
#define TA_STACK_SIZE  (256 * 1024)
#define TA_UUID	       IPERFTZ_TA_UUID
#define TA_VERSION     "0.1"

#endif /* USER_TA_HEADER_DEFINES_H */
