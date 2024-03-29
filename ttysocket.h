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
#ifndef TTY_H__
#define TTY_H__

#include <pthread.h>
#include <asm-generic/termbits.h>
#include <stdbool.h>
#include <stdint.h>
//#include "dbg.h"

typedef enum{ // device: tty terminal, network socket or UNIX socket
    DEV_TTY,
    DEV_NETSOCKET,
    DEV_UNIXSOCKET,
} devtype;

typedef struct {
    char *portname;         // device filename (should be freed before structure freeing)
    int speed;              // baudrate in human-readable format
    char *format;           // format like 8N1
    struct termios2 oldtty; // TTY flags for previous port settings
    struct termios2 tty;    // TTY flags for current settings
    int comfd;              // TTY file descriptor
    uint8_t *buf;           // buffer for data read
    size_t bufsz;           // size of buf
    size_t buflen;          // length of data read into buf
} TTY_descr2;

typedef struct{
    devtype type;               // type
    char *name;                 // filename (dev or UNIX socket) or server name/IP
    TTY_descr2 *dev;            // tty serial device
    char *port;                 // port to connect
    int speed;                  // tty speed
    pthread_mutex_t mutex;      // reading/writing mutex
    char eol[3];                // end of line
    char seol[5];               // `eol` with doubled backslash (for print @ screen)
} chardevice;

uint8_t *ReadData(int *l);
int SendData(const uint8_t *data, size_t len);
void settimeout(int tms);
int opendev(chardevice *d, char *path);
void closedev();

#endif // TTY_H__
