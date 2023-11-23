/*
 * This file is part of the ttyterm project.
 * Copyright 2023 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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

/****************************************************************************
 * Copyright 2018-2020,2021 Thomas E. Dickey                                *
 * Copyright 2017 Free Software Foundation, Inc.                            *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/
/*
 * $Id: popup_msg.c,v 1.11 2021/12/18 21:19:19 tom Exp $
 *
 * Show a multi-line message in a window which may extend beyond the screen.
 *
 * Thomas Dickey - 2017/4/15.
 */

#include <string.h>

#include "dbg.h"
#include "popup_msg.h"

static WINDOW *old_window;

static void begin_popup(void){
    doupdate();
    old_window = dupwin(curscr);
}

static void end_popup(void){
    touchwin(old_window);
    wnoutrefresh(old_window);
    doupdate();
    delwin(old_window);
}

/*
 * Display a temporary window, e.g., to display a help-message.
 */
void popup_msg(WINDOW *parent, const char *const *msg){
    int maxx, maxy, x0, y0, x1 = 0, y1 = 0, y2 = 0;
    getmaxyx(parent, maxy, maxx);
    // borders
    //x0 = (maxx > 12) ? 2 : ((maxx > 9) ? 1 : 0);
    x0 = (maxx > 80) ?  maxx/2-40 : maxx / 32;
    y0 = (maxy > 20) ? 2 : ((maxy > 16) ? 1 : 0);
    int wide = maxx - 2*x0;
    int high = maxy - 2*y0;
    WINDOW *help;
    WINDOW *data;
    int n;
    int width = 0;
    int length;
    int last_y, last_x;
    int ch = ERR;

    for (n = 0; msg[n] != 0; ++n) {
        int check = (int) strlen(msg[n]);
        if (width < check) width = check;
    }
    length = n;
    last_x = width - wide + 4;

    if((help = newwin(high, wide, y0, x0)) == 0) return;
    if((data = newpad(length + 1, width + 1)) == 0){
        delwin(help);
        return;
    }

    begin_popup();
    keypad(data, TRUE);
    for (n = 0; n < length; ++n){
        waddstr(data, msg[n]);
        if ((n + 1) < length) waddch(data, '\n');
    }
    y2 = getcury(data);
    last_y = (y2 - (high - 3));

    do{
        switch (ch){
            case KEY_HOME:
                y1 = 0;
                break;
            case KEY_END:
                y1 = last_y;
                break;
            case KEY_PREVIOUS:
            case KEY_PPAGE:
                if (y1 > 0) {
                y1 -= high / 2;
                if (y1 < 0)
                    y1 = 0;
                } else {
                beep();
                }
                break;
            case KEY_NEXT:
            case KEY_NPAGE:
                if (y1 < last_y) {
                y1 += high / 2;
                if (y1 > last_y)
                    y1 = last_y;
                } else {
                beep();
                }
                break;
            case CTRL('P'):
            case KEY_UP:
                if (y1 > 0)
                --y1;
                else
                beep();
                break;
            case CTRL('N'):
            case KEY_DOWN:
                if (y1 < last_y)
                ++y1;
                else
                beep();
                break;
            case CTRL('L'):
            case KEY_LEFT:
                if(x1 > 0) --x1;
                else beep();
            break;
            case CTRL('R'):
            case KEY_RIGHT:
                if(x1 < last_x) ++x1;
                else beep();
            break;
            default:
                beep();
                break;
            case ERR:
                break;
        }
        werase(help);
        box(help, 0, 0);
        wnoutrefresh(help);
        pnoutrefresh(data, y1, x1, y0+1, x0+1,  y0+high-2, x0+wide-2);
        doupdate();
    } while ((ch = wgetch(data)) != ERR && ch != QUIT && ch != ESCAPE);
    werase(help);
    wrefresh(help);
    delwin(help);
    delwin(data);

    end_popup();
}
