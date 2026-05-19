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

#pragma once

/*
 * here are some typedef's for global data
 */
typedef struct{
    int speed;          // baudrate
    int socket;         // open socket
    int unixsock;       // UNIX-socket instead of INET
    int exclusive;      // open serial in exclusive mode
    double tmoutms;     // timeout for select() in ms
    char *dumpfile;     // file to save dump
    char *node;         // node name / device path
    char *eol;          // end of line: \r (CR), \rn (CR+LF) or \n (LF): "r", "rn", "n"
    char *serformat;    // format of serial line
} glob_pars;


glob_pars *parse_args(int argc, char **argv);

