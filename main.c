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
#include "cmdlnopts.h"
#include "ncurses_and_readline.h"
#include "ttysocket.h"

#include "dbg.h"

static chardevice conndev = {.dev = NULL, .mutex = PTHREAD_MUTEX_INITIALIZER, .name = NULL, .type = DEV_TTY};

void signals(int signo){
    signal(signo, SIG_IGN);
    closedev();
    deinit_ncurses();
    deinit_readline();
    DBG("Exit by signal %d", signo);
    exit(signo);
}

int main(int argc, char **argv){
    glob_pars *G = NULL; // default parameters see in cmdlnopts.c
    initial_setup();
#ifdef EBUG
    OPENLOG("debug.log", LOGLEVEL_ANY, 1);
#endif
    G = parse_args(argc, argv);
    if(G->tmoutms < 0) ERRX("Timeout should be >= 0");
    const char *EOL = "\n", *seol = "\\n";
    if(strcasecmp(G->eol, "n")){
        if(strcasecmp(G->eol, "r") == 0){ EOL = "\r"; seol = "\\r"; }
        else if(strcasecmp(G->eol, "rn") == 0){ EOL = "\r\n"; seol = "\\r\\n"; }
        else if(strcasecmp(G->eol, "nr") == 0){ EOL = "\n\r"; seol = "\\n\\r"; }
        else ERRX("End of line should be \"r\", \"n\" or \"rn\" or \"nr\"");
    }
    strcpy(conndev.eol, EOL);
    strcpy(conndev.seol, seol);
    DBG("eol: %s, seol: %s", conndev.eol, conndev.seol);
    if(!G->ttyname){
        WARNX("You should point name");
        signals(0);
    }
    conndev.name = strdup(G->ttyname);
    conndev.port = strdup(G->port);
    if(G->socket){
        if(!G->port) conndev.type = DEV_UNIXSOCKET;
        else conndev.type = DEV_NETSOCKET;
    }else{
        conndev.speed = G->speed;
    }
    if(!opendev(&conndev, G->dumpfile)){
        signals(0);
    }
    init_ncurses();
    init_readline();
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, signals);  // hup - quit
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    pthread_t writer;
    if(pthread_create(&writer, NULL, cmdline, (void*)&conndev)) ERR("pthread_create()");
    settimeout(G->tmoutms);
    while(1){
        if(0 == pthread_mutex_lock(&conndev.mutex)){
            int l;
            uint8_t *buf = ReadData(&l);
            if(buf && l > 0){
                AddData(buf, l);
            }else if(l < 0){
                pthread_mutex_unlock(&conndev.mutex);
                ERRX("Device disconnected");
            }
            pthread_mutex_unlock(&conndev.mutex);
        }
        usleep(1000);
    }
    // never reached
    return 0;
}

