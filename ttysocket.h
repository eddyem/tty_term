/*
 * This file is part of the ttyterm project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <pthread.h>
#include <asm-generic/termbits.h>
#include <stdbool.h>
#include <stdint.h>
#include <usefull_macros.h>

typedef enum{ // device: tty terminal, network socket or UNIX socket
    DEV_TTY,
    DEV_NETSOCKET,
    DEV_UNIXSOCKET,
} devtype;

typedef struct{
    devtype type;               // type
    char *node;                 // filename (dev or UNIX socket) or server name:IP
    char *serformat;            // serial format like "8N1"
    int fd;                     // file descriptor
    int speed;                  // tty speed
    pthread_mutex_t mutex;      // reading/writing mutex
    char eol[3];                // end of line
    char seol[5];               // `eol` with doubled backslash (for print @ screen)
    int exclusive;
} chardevice;

ssize_t ReadData(uint8_t *buf, size_t len);
ssize_t SendData(const uint8_t *data, size_t len);
int opendev(chardevice *d, char *path);
void closedev();

