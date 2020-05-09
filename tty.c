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

/**
 * @brief read_ttyX- read data from TTY & console with 1ms timeout WITH disconnect detection
 * @param buff (o) - buffer for data read
 * @param length   - buffer len
 * @param rb (o)   - byte read from console or -1
 * @return amount of bytes read on tty or -1 if disconnected
 */
int read_ttyX(TTY_descr *d, int *rb){
    if(!d || d->comfd < 0) return -1;
    ssize_t L = 0;
    fd_set rfds;
    struct timeval tv;
    int retval;
    if(rb) *rb = -1;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(d->comfd, &rfds);
    // wait for 1ms
    tv.tv_sec = 0; tv.tv_usec = 1000;
    retval = select(d->comfd + 1, &rfds, NULL, NULL, &tv);
    if(!retval) return 0;
    if(retval < 0){
        WARN("select()");
        return -1;
    }
    if(FD_ISSET(STDIN_FILENO, &rfds) && rb){
        *rb = getchar();
    }
    if(FD_ISSET(d->comfd, &rfds)){
        if((L = read(d->comfd, d->buf, d->bufsz)) < 1){
            WARN("read()");
            return -1; // disconnect or other error - close TTY & die
        }
    }
    d->buflen = (size_t)L;
    d->buf[L] = 0;
    return (int)L;
}


#if 0
/**
 * Copy line by line buffer buff to file removing cmd starting from newline
 * @param buffer - data to put into file
 * @param cmd - symbol to remove from line startint (if found, change *cmd to (-1)
 *          or NULL, (-1) if no command to remove
 */
void copy_buf_to_file(char *buffer, int *cmd){
    char *buff, *line, *ptr;
    if(!cmd || *cmd < 0){
        fprintf(fout, "%s", buffer);
        return;
    }
    buff = strdup(buffer), ptr = buff;
    do{
        if(!*ptr) break;
        if(ptr[0] == (char)*cmd){
            *cmd = -1;
            ptr++;
            if(ptr[0] == '\n') ptr++;
            if(!*ptr) break;
        }
        line = ptr;
        ptr = strchr(buff, '\n');
        if(ptr){
            *ptr++ = 0;
            fprintf(fout, "%s\n", line);
        }else
            fprintf(fout, "%s", line); // no newline found in buffer
//      fprintf(fout, "%s\n", line);
    }while(ptr);
    free(buff);
}

#endif
