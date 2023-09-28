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
#include "ttysocket.h"

static int sec = 0, usec = 100; // timeout
static FILE *dupfile = NULL; // file for output
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
        DBG("FD_ISSET");
        return 1;
    }
    return 0;
}

// substitute all EOL's by '\n'
static size_t rmeols(chardevice *d){
    if(!d){
        DBG("d is NULL");
        return 0;
    }
    TTY_descr2 *D = d->dev;
    if(!D || D->comfd < 0){
        DBG("D bad");
        return 0;
    }
    if(0 == strcmp(d->eol, "\n")){
        DBG("No subs need");
        return D->buflen; // don't need to do this
    }
    int L = strlen(D->buf);
    char *newbuf = MALLOC(char, L), *ptr = D->buf, *eptr = D->buf + L;
    while(ptr < eptr){
        char *eol = strstr(ptr, d->eol);
        if(eol){
            eol[0] = '\n';
            eol[1] = 0;
        }
        strcat(newbuf, ptr);
        if(!eol) break;
        ptr = eol + d->eollen;
    }
    strcpy(D->buf, newbuf);
    FREE(newbuf);
    D->buflen = strlen(D->buf);
    return D->buflen;
}

// get data drom TTY
static char *getttydata(chardevice *d, int *len){
    if(!d || !d->dev) return NULL;
    TTY_descr2 *D = d->dev;
    if(D->comfd < 0) return NULL;
    int L = 0;
    int length = D->bufsz;
    char *ptr = D->buf;
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
        if(L >= d->eollen && 0 == strcmp(&ptr[-(d->eollen)], d->eol)){ // found end of line
            break;
        }
    }while(length);
    D->buflen = L;
    D->buf[L] = 0;
    if(len) *len = L;
    if(!L) return NULL;
    rmeols(d);
    DBG("buffer len: %zd, content: =%s=", D->buflen, D->buf);
    return D->buf;
}

static char *getsockdata(chardevice *d, int *len){
    if(!d || !d->dev) return NULL;
    TTY_descr2 *D = d->dev;
    if(D->comfd < 0) return NULL;
    char *ptr = NULL;
    int n = waittoread(D->comfd);
    if(n == 1){
        n = read(D->comfd, D->buf, D->bufsz-1);
        if(n > 0){
            ptr = D->buf;
            ptr[n] = 0;
            D->buflen = n;
            n = rmeols(d);
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
char *ReadData(chardevice *d, int *len){
    if(!d || !d->dev) return NULL;
    if(len) *len = -1;
    char *r = NULL;
    switch(d->type){
        case DEV_TTY:
            r = getttydata(d, len);
        break;
        case DEV_NETSOCKET:
        case DEV_UNIXSOCKET:
            r = getsockdata(d, len);
        break;
        default:
        break;
    }
    if(r && dupfile){
        fprintf(dupfile, "< %s", r);
    }
    return r;
}

/**
 * @brief SendData - send data to tty or socket
 * @param d - device
 * @param str - text string
 * @return 0 if error, -1 if disconnected
 */
int SendData(chardevice *d, char *str){
    char buf[BUFSIZ];
    if(!d) return -1;
    DBG("send %s", str);
    if(!str) return 0;
    int ret = 0;
    if(0 == pthread_mutex_lock(&d->mutex)){
        int l = strlen(str), lplus = l + d->eollen;
        if(l < 1) return 0;
        if(lplus > BUFSIZ-1) lplus = BUFSIZ-1;
        snprintf(buf, lplus+1, "%s%s", str, d->eol);
        DBG("SENDBUF (%d): _%s_", lplus, buf);
        switch(d->type){
            case DEV_TTY:
                if(write_tty(d->dev->comfd, buf, lplus)) ret = 0;
                else ret = l;
            break;
            case DEV_NETSOCKET:
            case DEV_UNIXSOCKET:
                if(lplus != send(d->dev->comfd, buf, lplus, MSG_NOSIGNAL)) ret = 0;
                else ret = l;
                pthread_mutex_unlock(&d->mutex);
            break;
            default:
                str = NULL;
            break;
        }
        if(str && dupfile){
            fprintf(dupfile, "> %s", buf);
        }
        pthread_mutex_unlock(&d->mutex);
    }else ret = -1;
    DBG("ret=%d", ret);
    return ret;
}

static const int socktypes[] = {SOCK_STREAM, SOCK_RAW, SOCK_RDM, SOCK_SEQPACKET, SOCK_DCCP, SOCK_PACKET, SOCK_DGRAM, 0};

static TTY_descr2* opensocket(chardevice *d){
    if(!d) return FALSE;
    TTY_descr2 *descr = MALLOC(TTY_descr2, 1); // only for `buf` and bufsz/buflen
    descr->buf = MALLOC(char, BUFSIZ);
    descr->bufsz = BUFSIZ;
    // now try to open a socket
    descr->comfd = -1;
    struct hostent *host;
    struct sockaddr_in addr = {0};
    struct sockaddr_un saddr = {0};
    struct sockaddr *sa = NULL;
    socklen_t addrlen = 0;
    int domain = -1;
    if(d->type == DEV_NETSOCKET){
        DBG("NETSOCK to %s", d->name);
        sa = (struct sockaddr*) &addr;
        addrlen = sizeof(addr);
        if((host = gethostbyname(d->name)) == NULL ){
            WARN("gethostbyname()");
            FREE(descr->buf);
            FREE(descr);
            return NULL;
        }
        struct in_addr *ia = (struct in_addr*)host->h_addr_list[0];
        DBG("addr: %s", inet_ntoa(*ia));
        addr.sin_family = AF_INET;
        int p = atoi(d->port); DBG("PORT: %s - %d", d->port, p);
        addr.sin_port = htons(p);
        //addr.sin_addr.s_addr = *(long*)(host->h_addr);
        addr.sin_addr.s_addr = ia->s_addr;
        domain = AF_INET;
    }else{
        DBG("UNSOCK");
        sa = (struct sockaddr*) &saddr;
        addrlen = sizeof(saddr);
        saddr.sun_family = AF_UNIX;
        if(*(d->name) == 0){ // if sun_path[0] == 0 then don't create a file
            DBG("convert name");
            saddr.sun_path[0] = 0;
            strncpy(saddr.sun_path+1, d->name+1, 105);
        }
        else if(strncmp("\\0", d->name, 2) == 0){
            DBG("convert name");
            saddr.sun_path[0] = 0;
            strncpy(saddr.sun_path+1, d->name+2, 105);
        }else  strncpy(saddr.sun_path, d->name, 106);
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
            f |= PARENB | PARODD;
        break;
        case 'O': // odd
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

static TTY_descr2* opentty(chardevice *d){
    if(!d->name){
        /// ����������� ��� �����
        WARNX(_("Port name is missing"));
        return NULL;
    }
    TTY_descr2 *descr = MALLOC(TTY_descr2, 1);
    descr->portname = strdup(d->name);
    descr->speed = d->speed;
    tcflag_t flags;
    descr->format = parse_format(d->port, &flags);
    if(!descr->format) goto someerr;
    descr->buf = MALLOC(char, BUFSIZ);
    descr->bufsz = BUFSIZ-1;
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
    descr->tty.c_iflag = 0;
    descr->tty.c_cflag = BOTHER | flags |CREAD|CLOCAL;
    descr->tty.c_ispeed = d->speed;
    descr->tty.c_ospeed = d->speed;
    if(ioctl(descr->comfd, TCSETS2, &descr->tty)){
        WARN(_("Can't set new port config"));
        goto someerr;
    }
    ioctl(descr->comfd, TCGETS2, &descr->tty);
    d->speed = descr->tty.c_ispeed;
#ifdef EBUG
    printf("ispeed: %d, ospeed: %d, cflag=%d (BOTHER=%d)\n", descr->tty.c_ispeed, descr->tty.c_ospeed, descr->tty.c_cflag&CBAUD, BOTHER);
    if(system("stty -F /dev/ttyUSB0")) WARN("system()");
#endif
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
    switch(d->type){
        case DEV_TTY:
            DBG("Serial");
            d->dev = opentty(d);
            if(!d->dev){
                WARN("Can't open device %s", d->name);
                DBG("CANT OPEN");
                return FALSE;
            }
        break;
        case DEV_NETSOCKET:
        case DEV_UNIXSOCKET:
            d->dev = opensocket(d);
            if(!d->dev){
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
            closedev(d);
            return FALSE;
        }
    }
    return TRUE;
}

void closedev(chardevice *d){
    if(!d) return;
    pthread_mutex_unlock(&d->mutex);
    pthread_mutex_trylock(&d->mutex);
    if(dupfile){
        fclose(dupfile);
        dupfile = NULL;
    }
    switch(d->type){
        case DEV_TTY:
            if(d->dev){
                TTY_descr2 *t = d->dev;
                ioctl(t->comfd, TCSETS2, &t->oldtty); // return TTY to previous state
                close(t->comfd);
            }
        break;
        case DEV_NETSOCKET:
            if(d->dev){
                close(d->dev->comfd);
                FREE(d->dev);
            }
        break;
        default:
            return;
    }
    if(d->dev){
        FREE(d->dev->format);
        FREE(d->dev->portname);
        FREE(d->dev->buf);
        FREE(d->dev);
    }
    FREE(d->name);
    DBG("Device closed");
}
