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
#include "dbg.h"

typedef enum{
    DEV_TTY,
    DEV_NETSOCKET,
    DEV_UNIXSOCKET,
} devtype;

typedef struct{
    devtype type;               // type
    char *name;                 // filename (dev or UNIX socket) or server name/IP
    TTY_descr *dev;             // tty serial device
    char *port;                 // port to connect
    int speed;                  // tty speed
    pthread_mutex_t mutex;      // reading/writing mutex
    char eol[3];                // end of line
    char seol[5];               // `eol` with doubled backslash (for print @ screen)
    int eollen;                 // length of `eol`
} chardevice;

char *ReadData(chardevice *d, int *l);
int SendData(chardevice *d, char *str);
void settimeout(int tms);
int opendev(chardevice *d, char *path);
void closedev(chardevice *d);

#endif // TTY_H__
