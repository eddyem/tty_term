/*
 * This file is part of the ttyterm project.
 * Copyright 2020 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

#include <stdio.h> // getchar
#include <sys/select.h>
#include "tty.h"

static int sec = 0, usec = 100; // timeout

void settimeout(int tmout){
    sec = 0;
    if(tmout > 999){
        sec = tmout / 1000;
        tmout -= sec * 1000;
    }
    usec = tmout * 1000L;
}

//extern FILE *fd;

int Read_tty(TTY_descr *d){
    if(!d || d->comfd < 0) return 0;
    size_t L = 0;
    ssize_t l;
    size_t length = d->bufsz;
    char *ptr = d->buf;
    fd_set rfds;
    struct timeval tv;
    int retval;
    do{
        l = 0;
        FD_ZERO(&rfds);
        FD_SET(d->comfd, &rfds);
        tv.tv_sec = sec; tv.tv_usec = usec;
        retval = select(d->comfd + 1, &rfds, NULL, NULL, &tv);
        if(!retval) break;
        if(retval < 0) return -1;
        if(FD_ISSET(d->comfd, &rfds)){
            l = read(d->comfd, ptr, length);
            if(l < 1) return -1; // disconnected
            ptr += l; L += l;
            length -= l;
        }
    }while(l && length);
    d->buflen = L;
    d->buf[L] = 0;
    return (int)L;
}

