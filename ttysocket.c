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

#include "dbg.h"
#include "string_functions.h"
#include "ttysocket.h"

static int sec = 0, usec = 100; // timeout
static FILE *dupfile = NULL; // file for output
static chardevice *device = NULL; // current opened device

// TODO: if unix socket name starts with \0 translate it as \\0 to d->name!

// set Read_tty timeout in milliseconds
void settimeout(int tmout){
    sec = 0;
    if(tmout > 999){
        sec = tmout / 1000;
        tmout -= sec * 1000;
    }
    usec = tmout * 1000L;
}

/**
 * wait for answer from socket
 * @param sock - socket fd
 * @return 0 in case of timeout, 1 in case of socket ready, -1 if error
 */
static int waittoread(int fd){
    fd_set fds;
    struct timeval timeout;
    timeout.tv_sec = sec;
    timeout.tv_usec = usec;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    do{
        int rc = select(fd+1, &fds, NULL, NULL, &timeout);
        if(rc < 0){
            if(errno != EINTR){
                WARN("select()");
                return -1;
            }
            continue;
        }
        break;
    }while(1);
    if(FD_ISSET(fd, &fds)){
        //DBG("FD_ISSET");
        return 1;
    }
    return 0;
}

// get data drom TTY
static uint8_t *getttydata(int *len){
    if(!device || !device->dev) return NULL;
    TTY_descr2 *D = device->dev;
    if(D->comfd < 0) return NULL;
    int L = 0;
    int length = D->bufsz - 1; // -1 for terminating zero
    uint8_t *ptr = D->buf;
    int s = 0;
    do{
        if(!(s = waittoread(D->comfd))) break;
        if(s < 0){
            if(len) *len = 0;
            return NULL;
        }
        int l = read(D->comfd, ptr, length);
        if(l < 1){ // disconnected
            if(len) *len = -1;
            return NULL;
        }
        ptr += l; L += l;
        length -= l;
    }while(length);
    D->buflen = L;
    D->buf[L] = 0; // for text buffers
    if(len) *len = L;
    if(!L) return NULL;
    DBG("buffer len: %zd, content: =%s=", D->buflen, D->buf);
    return D->buf;
}

static uint8_t *getsockdata(int *len){
    if(!device || !device->dev) return NULL;
    TTY_descr2 *D = device->dev;
    if(D->comfd < 0) return NULL;
    uint8_t *ptr = NULL;
    int n = waittoread(D->comfd);
    if(n == 1){
        n = read(D->comfd, D->buf, D->bufsz-1);
        if(n > 0){
            ptr = D->buf;
            ptr[n] = 0;
            D->buflen = n;
            DBG("got %d: ..%s..", n, ptr);
        }else{
            DBG("Got nothing");
            n = -1;
        }
    }
    if(len) *len = n;
    return ptr;
}

/**
 * @brief ReadData - get data from serial device or socket
 * @param d - device
 * @param len (o) - length of data read (-1 if device disconnected)
 * @return NULL or string
 */
uint8_t *ReadData(int *len){
    if(!device || !device->dev) return NULL;
    if(len) *len = -1;
    uint8_t *r = NULL;
    switch(device->type){
        case DEV_TTY:
            r = getttydata(len);
        break;
        case DEV_NETSOCKET:
        case DEV_UNIXSOCKET:
            r = getsockdata(len);
        break;
        default:
        break;
    }
    if(r && dupfile){
        fwrite("< ", 1, 2, dupfile);
        fwrite(r, 1, *len, dupfile);
    }
    return r;
}

/**
 * @brief SendData - send data to tty or socket
 * @param d - device
 * @param data - buffer with data
 * @return 0 if error or empty string, -1 if disconnected
 */
int SendData(const uint8_t *data, size_t len){
    if(!device) return -1;
    if(!data || len == 0) return 0;
    int ret = 0;
    DBG("Send %d bytes", len);
    if(0 == pthread_mutex_lock(&device->mutex)){
        switch(device->type){
            case DEV_TTY:
                if(write_tty(device->dev->comfd, (const char*)data, len)) ret = 0;
                else ret = len;
            break;
            case DEV_NETSOCKET:
            case DEV_UNIXSOCKET:
                if(len != (size_t)send(device->dev->comfd, data, len, MSG_NOSIGNAL)) ret = 0;
                else ret = len;
            break;
            default:
                data = NULL;
            break;
        }
        if(data && dupfile){
            fwrite("> ", 1, 2, dupfile);
            fwrite(data, 1, len, dupfile);
        }
        pthread_mutex_unlock(&device->mutex);
    }else ret = -1;
    DBG("ret=%d", ret);
    return ret;
}

static const int socktypes[] = {SOCK_STREAM, SOCK_RAW, SOCK_RDM, SOCK_SEQPACKET, SOCK_DCCP, SOCK_PACKET, SOCK_DGRAM, 0};

static TTY_descr2* opensocket(){
    if(!device) return FALSE;
    TTY_descr2 *descr = MALLOC(TTY_descr2, 1); // only for `buf` and bufsz/buflen
    descr->buf = MALLOC(uint8_t, BUFSIZ);
    descr->bufsz = BUFSIZ;
    // now try to open a socket
    descr->comfd = -1;
    struct hostent *host;
    struct sockaddr_in addr = {0};
    struct sockaddr_un saddr = {0};
    struct sockaddr *sa = NULL;
    socklen_t addrlen = 0;
    int domain = -1;
    if(device->type == DEV_NETSOCKET){
        DBG("NETSOCK to %s", device->name);
        sa = (struct sockaddr*) &addr;
        addrlen = sizeof(addr);
        if((host = gethostbyname(device->name)) == NULL ){
            WARN("gethostbyname()");
            FREE(descr->buf);
            FREE(descr);
            return NULL;
        }
        struct in_addr *ia = (struct in_addr*)host->h_addr_list[0];
        DBG("addr: %s", inet_ntoa(*ia));
        addr.sin_family = AF_INET;
        int p = atoi(device->port); DBG("PORT: %s - %d", device->port, p);
        addr.sin_port = htons(p);
        //addr.sin_addr.s_addr = *(long*)(host->h_addr);
        addr.sin_addr.s_addr = ia->s_addr;
        domain = AF_INET;
    }else{
        DBG("UNSOCK");
        sa = (struct sockaddr*) &saddr;
        addrlen = sizeof(saddr);
        saddr.sun_family = AF_UNIX;
        if(*(device->name) == 0){ // if sun_path[0] == 0 then don't create a file
            DBG("convert name");
            saddr.sun_path[0] = 0;
            strncpy(saddr.sun_path+1, device->name+1, 105);
        }
        else if(strncmp("\\0", device->name, 2) == 0){
            DBG("convert name");
            saddr.sun_path[0] = 0;
            strncpy(saddr.sun_path+1, device->name+2, 105);
        }else  strncpy(saddr.sun_path, device->name, 106);
        domain = AF_UNIX;
    }
    const int *type = socktypes;
    while(*type){
        DBG("type = %d", *type);
        if((descr->comfd = socket(domain, *type, 0)) > -1){
            if(connect(descr->comfd, sa, addrlen) < 0){
                DBG("CANT connect");
                close(descr->comfd);
            }else break;
        }
        ++type;
    }
    if(descr->comfd < 0){
        DBG("NO types");
        WARNX("No types can be choosen");
        FREE(descr->buf);
        FREE(descr);
        return NULL;
    }
    return descr;
}

static char *parse_format(const char *iformat, tcflag_t *flags){
    tcflag_t f = 0;
    if(!iformat){ // default
        *flags = CS8;
        return strdup("8N1");
    }
    if(strlen(iformat) != 3) goto someerr;
    switch(iformat[0]){
        case '5':
            f |= CS5;
        break;
        case '6':
            f |= CS6;
        break;
        case '7':
            f |= CS7;
        break;
        case '8':
            f |= CS8;
        break;
        default:
            goto someerr;
    }
    switch(iformat[1]){
        case '0': // always 0
            f |= PARENB | CMSPAR;
        break;
        case '1': // always 1
            f |= PARENB | CMSPAR | PARODD;
        break;
        case 'E': // even
            f |= PARENB;
        break;
        case 'N': // none
        break;
        case 'O': // odd
            f |= PARENB | PARODD;
        break;
        default:
            goto someerr;
    }
    switch(iformat[2]){
        case '1':
        break;
        case '2':
            f |= CSTOPB;
        break;
        default:
            goto someerr;
    }
    *flags = f;
    return strdup(iformat);
someerr:
    WARNX(_("Wrong USART format \"%s\"; use NPS, where N: 5..8; P: N/E/O/1/0, S: 1/2"), iformat);
    return NULL;
}

static TTY_descr2* opentty(){
    if(!device->name){
        /// Отсутствует имя порта
        WARNX(_("Port name is missing"));
        return NULL;
    }
    TTY_descr2 *descr = MALLOC(TTY_descr2, 1);
    descr->portname = strdup(device->name);
    descr->speed = device->speed;
    tcflag_t flags;
    descr->format = parse_format(device->port, &flags);
    if(!descr->format) goto someerr;
    descr->buf = MALLOC(uint8_t, 512);
    descr->bufsz = 511;
    if((descr->comfd = open(descr->portname, O_RDWR|O_NOCTTY)) < 0){
        WARN(_("Can't use port %s"), descr->portname);
        goto someerr;
    }
    if(ioctl(descr->comfd, TCGETS2, &descr->oldtty)){
        WARN(_("Can't get port config"));
        goto someerr;
    }
    descr->tty = descr->oldtty;
    descr->tty.c_lflag = 0; // ~(ICANON | ECHO | ECHOE | ISIG)
    descr->tty.c_iflag = 0; // don't do any changes in input stream
    descr->tty.c_oflag = 0; // don't do any changes in output stream
    descr->tty.c_cflag = BOTHER | flags |CREAD|CLOCAL;
    descr->tty.c_ispeed = device->speed;
    descr->tty.c_ospeed = device->speed;
    if(ioctl(descr->comfd, TCSETS2, &descr->tty)){
        WARN(_("Can't set new port config"));
        goto someerr;
    }
    ioctl(descr->comfd, TCGETS2, &descr->tty);
    device->speed = descr->tty.c_ispeed;
    return descr;
someerr:
    FREE(descr->format);
    FREE(descr->buf);
    FREE(descr);
    return NULL;
}

/**
 * @brief opendev - open TTY or socket output device
 * @param d - device type
 * @return FALSE if failed
 */
int opendev(chardevice *d, char *path){
    if(!d) return FALSE;
    DBG("Try to open device");
    device = MALLOC(chardevice, 1);
    memcpy(device, d, sizeof(chardevice));
    device->name = strdup(d->name);
    device->port = strdup(d->port);
    switch(device->type){
        case DEV_TTY:
            DBG("Serial");
            device->dev = opentty();
            if(!device->dev){
                WARN("Can't open device %s", device->name);
                DBG("CANT OPEN");
                return FALSE;
            }
        break;
        case DEV_NETSOCKET:
        case DEV_UNIXSOCKET:
            device->dev = opensocket();
            if(!device->dev){
                WARNX("Can't open socket");
                DBG("CANT OPEN");
                return FALSE;
            }
        break;
        default:
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
    return TRUE;
}

void closedev(){
    if(!device) return;
    pthread_mutex_unlock(&device->mutex);
    pthread_mutex_trylock(&device->mutex);
    if(dupfile){
        fclose(dupfile);
        dupfile = NULL;
    }
    switch(device->type){
        case DEV_TTY:
            if(device->dev){
                TTY_descr2 *t = device->dev;
                ioctl(t->comfd, TCSETS2, &t->oldtty); // return TTY to previous state
                close(t->comfd);
            }
        break;
        case DEV_NETSOCKET:
            if(device->dev){
                close(device->dev->comfd);
                FREE(device->dev);
            }
        break;
        default:
            return;
    }
    if(device->dev){
        FREE(device->dev->format);
        FREE(device->dev->portname);
        FREE(device->dev->buf);
        FREE(device->dev);
    }
    FREE(device->name);
    FREE(device);
    DBG("Device closed");
}
