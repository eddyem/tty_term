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

#if 0
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>  // unix socket
#endif

#include <string.h>
#include <usefull_macros.h>

#include "dbg.h"
#include "string_functions.h"
#include "ttysocket.h"

static FILE *dupfile = NULL; // file for output
static chardevice *device = NULL; // current opened device

/**
 * @brief ReadData - get data from serial device or socket
 * @param buf - buffer
 * @param len - buffer length
 * @return amount of read bytes or -1 in case of error / disconnection
 */
ssize_t ReadData(uint8_t *buf, size_t len){
    if(!device || device->fd < 0){
        WARNX("ReadData(): No connection");
        return -1;
    }
    if(!buf || len == 0){
        WARNX("ReadData(): Bad receive buffer");
        return 0;
    }
    ssize_t l = -1;
    static int errctr = 0;
    if(0 == pthread_mutex_lock(&device->mutex)){
        int s = sl_canread(device->fd);
        if(s < 1){
            pthread_mutex_unlock(&device->mutex);
            return (ssize_t)s;
        }
        l = read(device->fd, buf, len);
        if(l > 0){
            DBG("receive: %zd", l);
            if(dupfile){
                fwrite("< ", 1, 2, dupfile);
                fwrite(buf, 1, l, dupfile);
            }
            errctr = 0;
        }else{
            DBG("Disconnected? Got %zd bytes", l);
            if(++errctr > 3){
                l = -1;
                errctr = 0;
            }
        }
        pthread_mutex_unlock(&device->mutex);
    }else WARN("ReadData(): pthread_mutex_lock()");
    return l;
}

/**
 * @brief SendData - send data to tty or socket
 * @param d - device
 * @param data - buffer with data
 * @return 0 if error or empty string, -1 if disconnected or error
 */
ssize_t SendData(const uint8_t *data, size_t len){
    if(!device || device->fd < 0){
        WARNX("SendData(): No connection");
        return -1;
    }
    if(!data || len == 0){
        WARNX("SendData(): Bad data to send");
        return 0;
    }
    ssize_t ret = -1;
    DBG("Send %d bytes", len);
    DBG("lock");
    if(0 == pthread_mutex_lock(&device->mutex)){
        DBG("write");
        ret = write(device->fd, data, len);
        if(data && dupfile){
            fwrite("> ", 1, 2, dupfile);
            fwrite(data, 1, len, dupfile);
        }
        DBG("unlock");
        pthread_mutex_unlock(&device->mutex);
    }else WARN("SendData(): pthread_mutex_lock()");
    DBG("send: %zd", ret);
    return ret;
}

/**
 * @brief opendev - open TTY or socket output device
 * @param d - device type
 * @param path - path to dump file or NULL
 * @return FALSE if failed
 */
int opendev(chardevice *d, char *path){
    if(!d) return FALSE;
    DBG("Try to open device");
    device = d;//MALLOC(chardevice, 1);
    //memcpy(device, d, sizeof(chardevice));
    //device->node = strdup(d->node);
    //if(d->serformat) device->serformat = strdup(d->serformat);
    DBG("devtype=%d", device->type);
    switch(device->type){
        case DEV_TTY:
            DBG("Serial");
            device->fd = sl_tty_fdescr(device->node, device->serformat, device->speed, device->exclusive);
        break;
        case DEV_NETSOCKET:
            DBG("INET socket");
            device->fd = sl_sock_open(SOCKT_NET, device->node, 0, 0);
        break;
        case DEV_UNIXSOCKET:
            DBG("UNIX-socket");
            device->fd = sl_sock_open(SOCKT_UNIX, device->node, 0, 0);
        break;
        default:
            return FALSE;
    }
    if(device->fd < 0){
        WARN("Can't open %s", device->node);
        DBG("CANT OPEN");
        return FALSE;
    }
    if(path){ // open logging file
        dupfile = fopen(path, "a");
        if(!dupfile){
            WARN("Can't open %s", path);
            closedev();
            return FALSE;
        }
    }
    changeeol(device->eol); // allow string functions to know EOL
    //d->fd = device->fd;
    return TRUE;
}

void closedev(){
    FNAME();
    if(!device) return;
    // avoid dead-locks
    pthread_mutex_trylock(&device->mutex);
    int fd = device->fd;
    device->fd = -1;
    usleep(1000); // wait a little for threads
    close(fd);
    DBG("Device closed");
    pthread_mutex_unlock(&device->mutex);
    if(dupfile){
        DBG("Dupfile closed");
        fclose(dupfile);
        dupfile = NULL;
    }
    DBG("free node");
    FREE(device->node);
    DBG("free serformat");
    FREE(device->serformat);
    DBG("free device");
    FREE(device);
    DBG("Device closed");
}
