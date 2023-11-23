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
#ifndef NCURSES_AND_READLINE_H__
#define NCURSES_AND_READLINE_H__

#include "dbg.h"
#include "ttysocket.h"

typedef enum{ // display/input data as
    DISP_TEXT,      // text (non-ASCII input and output as \xxx)
    DISP_RAW,       // hex output as xx xx xx, input in as numbers in bin (0bxx), oct(0xx), hex (0xxx||0Xxx) or dec and letters
    DISP_HEX,       // hexdump output, input in hex only (with or without spaces)
    DISP_UNCHANGED  // old
} disptype;

void init_readline();
void deinit_readline();
void init_ncurses();
void deinit_ncurses();
void *cmdline(void* arg);
void AddData(const uint8_t *data, int len);

#endif // NCURSES_AND_READLINE_H__
