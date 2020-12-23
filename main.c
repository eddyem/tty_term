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

#include <signal.h>
#include <stdio.h>
#include <string.h> // strcmp
#include <usefull_macros.h>
#include "cmdlnopts.h"
#include "ncurses_and_readline.h"
#include "tty.h"

#define BUFLEN 4096

static ttyd dtty = {.dev = NULL, .mutex = PTHREAD_MUTEX_INITIALIZER};

//FILE *fd;

void signals(int signo){
    if(dtty.dev){
        pthread_mutex_lock(&dtty.mutex);
        close_tty(&dtty.dev);
    }
    //fprintf(fd, "stop\n");
    //fflush(fd);
    deinit_ncurses();
    deinit_readline();
    exit(signo);
}

int main(int argc, char **argv){
    glob_pars *G = NULL; // default parameters see in cmdlnopts.c
    initial_setup();
    G = parse_args(argc, argv);
    if(G->tmoutms < 0) ERRX("Timeout should be >= 0");
    dtty.dev = new_tty(G->ttyname, G->speed, BUFLEN);
    if(!dtty.dev || !(dtty.dev = tty_open(dtty.dev, 1))){
        WARN("Can't open device %s", G->ttyname);
        signals(1);
    }
    //fd = fopen("loglog", "w");
    //fprintf(fd, "start\n");
    const char *EOL = "\n", *seol = "\\n";
    if(strcasecmp(G->eol, "n")){
        if(strcasecmp(G->eol, "r") == 0){ EOL = "\r"; seol = "\\r"; }
        else if(strcasecmp(G->eol, "rn") == 0){ EOL = "\r\n"; seol = "\\r\\n"; }
        else if(strcasecmp(G->eol, "nr") == 0){ EOL = "\n\r"; seol = "\\n\\r"; }
        else ERRX("End of line should be \"r\", \"n\" or \"rn\" or \"nr\"");
    }
    strcpy(dtty.eol, EOL);
    strcpy(dtty.seol, seol);
    int eollen = strlen(EOL);
    dtty.eollen = eollen;
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, signals);  // hup - quit
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    init_ncurses();
    init_readline();
    pthread_t writer;
    if(pthread_create(&writer, NULL, cmdline, (void*)&dtty)) ERR("pthread_create()");
    settimeout(G->tmoutms);
    while(1){
        if(0 == pthread_mutex_lock(&dtty.mutex)){
            int l = Read_tty(dtty.dev);
            if(l > 0){
                char *buf = dtty.dev->buf;
                char *eol = NULL, *estr = buf + l;
                do{
                    eol = strchr(buf, '\n');
                    if(eol){
                        *eol = 0;
                        add_ttydata(buf);
                        buf = eol + 1;
                    }else{
                        add_ttydata(buf);
                    }
                    /*eol = strstr(buf, EOL);
                    if(eol){
                        *eol = 0;
                        add_ttydata(buf);
                        buf = eol + eollen;
                    }else{
                        char *ptr = buf;
                        while(*ptr){
                            if(*ptr == '\n' || *ptr == '\r'){ *ptr = 0; break;}
                            ++ptr;
                        }
                        add_ttydata(buf);
                    }*/
                }while(eol && buf < estr);
            }else if(l < 0){
                pthread_mutex_unlock(&dtty.mutex);
                ERRX("Device disconnected");
            }
            pthread_mutex_unlock(&dtty.mutex);
            usleep(1000);
        }
    }
    // never reached
    return 0;
}

