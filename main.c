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
#include "tty.h"

// we don't need more than 64 bytes for TTY input buffer - USB CDC can't transmit more in one packet
#define BUFLEN 65

static TTY_descr *dev = NULL;

void signals(int signo){
    restore_console();
    if(dev) close_tty(&dev);
    exit(signo);
}

typedef enum{
    EOL_N = 0,
    EOL_R,
    EOL_RN
} eol_t;

int main(int argc, char **argv){
    glob_pars *G = NULL; // default parameters see in cmdlnopts.c
    initial_setup();
    G = parse_args(argc, argv);
    dev = new_tty(G->ttyname, G->speed, BUFLEN);
    if(!dev || !(dev = tty_open(dev, 1))) return 1; // open exclusively
    eol_t eol = EOL_N;
    if(strcmp(G->eol, "n")){ // change eol
        if(strcmp(G->eol, "r") == 0) eol = EOL_R;
        else if(strcmp(G->eol, "rn") == 0) eol = EOL_RN;
        else ERRX("End of line should be \"r\", \"n\" or \"rn\"");
    }
    signal(SIGTERM, signals); // kill (-15) - quit
    signal(SIGHUP, signals);  // hup - quit
    signal(SIGINT, signals);  // ctrl+C - quit
    signal(SIGQUIT, signals); // ctrl+\ - quit
    signal(SIGTSTP, SIG_IGN); // ignore ctrl+Z
    setup_con();
    const char r = '\r';
    while(1){
        int b;
        int l = read_ttyX(dev, &b);
        if(l < 0) signals(9);
        if(b > -1){
            char c = (char)b;
            if(c == '\n' && eol != EOL_N){ // !\n
                if(eol == EOL_R) c = '\r'; // \r
                else if(write_tty(dev->comfd, &r, 1)) WARN("write_tty()"); // \r\n
            }
            if(write_tty(dev->comfd, &c, 1)) WARN("write_tty()");
        }
        if(l){
            printf("%s", dev->buf);
            fflush(stdout);
           // if(fout) copy_buf_to_file(buff, &oldcmd);
        }
    }
    // never reached
    return 0;
}

