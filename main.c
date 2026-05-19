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
#include <strings.h>
#include <unistd.h>

#include "cmdlnopts.h"
#include "ncurses_and_readline.h"
#include "ttysocket.h"

#include "dbg.h"

static int should_exit = 0;

void signals(int signo){
    DBG("signo = %d", signo);
    should_exit = 1;
    if(signo > 0){
        DBG("Exit by signal %d", signo);
    }else if(signo < 0){
        should_exit = -1;
        DBG("exit by disconnection");
    }
}

int main(int argc, char **argv){
    chardevice *conndev = MALLOC(chardevice, 1); // should be allocated to FREE later
    pthread_mutex_init(&conndev->mutex, NULL);
    conndev->type = DEV_TTY;
    glob_pars *G = NULL; // default parameters see in cmdlnopts.c
    sl_init();
#ifdef EBUG
    OPENLOG("debug.log", LOGLEVEL_ANY, 1);
#endif
    DBG("Parsing");
    G = parse_args(argc, argv);
    if(G->tmoutms < 0.){
        ERRX("Timeout should be >= 0");
        return 1;
    }
    if(sl_tty_tmout(G->tmoutms) < 0){
        ERRX("Can't set timeout to %g ms", G->tmoutms);
        return 1;
    }
    if(!G->node){
        ERRX("You should point node name");
        return 1;
    }
    const char *EOL = "\n", *seol = "\\n";
    if(strcasecmp(G->eol, "n")){
        if(strcasecmp(G->eol, "r") == 0){ EOL = "\r"; seol = "\\r"; }
        else if(strcasecmp(G->eol, "rn") == 0){ EOL = "\r\n"; seol = "\\r\\n"; }
        else if(strcasecmp(G->eol, "nr") == 0){ EOL = "\n\r"; seol = "\\n\\r"; }
        else{
            ERRX("End of line should be \"r\", \"n\", \"rn\" or \"nr\"");
            return 1;
        }
    }
    strcpy(conndev->eol, EOL);
    strcpy(conndev->seol, seol);
    DBG("eol: %s, seol: %s", conndev->eol, conndev->seol);
    conndev->node = strdup(G->node);
    if(!conndev->node){
        ERR("strdup()");
        return 1;
    }
    DBG("node: %s", conndev->node);
    if(G->socket || G->unixsock){ // INET/UNIX socket
        if(G->unixsock) conndev->type = DEV_UNIXSOCKET;
        else conndev->type = DEV_NETSOCKET;
    }else{ // serial device
        conndev->speed = G->speed;
        conndev->serformat = strdup(G->serformat); // `port` of tty is serial format
        if(!conndev->serformat){
            ERR("strdup()");
            return 1;
        }
        DBG("speed=%d, format=%s", conndev->speed, conndev->serformat);
        conndev->exclusive = G->exclusive;
    }
    if(!opendev(conndev, G->dumpfile)){
        exit(1);
    }
    init_ncurses();
    init_readline();
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, signals);  // hup - quit
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    pthread_t writer;
    if(pthread_create(&writer, NULL, cmdline, (void*)conndev)) ERR("pthread_create()");
    uint8_t buf[BUFSIZ];
    while(conndev && conndev->fd > -1 && !should_exit){
        ssize_t got = ReadData(buf, BUFSIZ);
        if(got > 0){
            AddData(buf, got);
        }else if(got < 0){
            WARNX("Device disconnected -> exit");
            signals(-1);
        }
        usleep(1000);
    }
    DBG("Exit writer");
    exit_writer();
    usleep(1000);
    deinit_ncurses();
    deinit_readline();
    closedev();
    if(should_exit < 0) red("\nExit with error (disconnected?)\n\n");
    else DBG("main(): Normal exit");
    return 0;
}

