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


// based on https://stackoverflow.com/a/28709979/1965803 ->
// https://github.com/ulfalizer/readline-and-ncurses
// Copyright (c) 2015-2019, Ulf Magnusson
// SPDX-License-Identifier: ISC

#include <curses.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbg.h"
#include "ttysocket.h"
#include "ncurses_and_readline.h"
#include "popup_msg.h"
#include "string_functions.h"

enum { // using colors
    BKG_NO = 1,
    BKGMARKED_NO = 2,
    NORMAL_NO = 3,
    MARKED_NO = 4
};
#define COLOR(x)  COLOR_PAIR(x ## _NO)

// Keeps track of the terminal mode so we can reset the terminal if needed on errors
static bool visual_mode = false;
// insert commands when true; roll upper screen when false
static bool insert_mode = true;
static bool should_exit = false;

static disptype disp_type = DISP_TEXT;  // type of displaying data
static disptype input_type = DISP_TEXT; // parsing type of input data
const char *dispnames[] = {"TEXT", "RAW", "HEX"};

static chardevice *dtty = NULL;

static void fail_exit(const char *msg){
    // Make sure endwin() is only called in visual mode. As a note, calling it
    // twice does not seem to be supported and messed with the cursor position.
    if(visual_mode) endwin();
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static WINDOW *msg_win; // Message window
static WINDOW *sep_win; // Separator line above the command (readline) window
static WINDOW *cmd_win; // Command (readline) window

// string list
typedef struct _Line{
    int Nline;
    char *contents;
    struct _Line *prev, *next;
} Line;
// head of list, current item and first line on screen
Line *head = NULL, *curr = NULL, *firstline = NULL;
int nr_lines = 0; // total anount of data portions @ input

static unsigned char input; // Input character for readline

// Used to signal "no more input" after feeding a character to readline
static bool input_avail = false;

// Not bothering with 'input_avail' and just returning 0 here seems to do the
// right thing too, but this might be safer across readline versions
static int readline_input_avail(){
    return input_avail;
}

static int readline_getc(__attribute__((__unused__)) FILE *dummy){
    input_avail = false;
    return input;
}

static void forward_to_readline(char c){
    input = c;
    input_avail = true;
    rl_callback_read_char();
}

// functions to modify output data
static char *text_putchar(char *next){
    char c = *next++;
    DBG("put 0x%02X (%c)", c, c);
    if(c < 31 || c > 126){
        wattron(msg_win, COLOR(MARKED));
        //waddch(msg_win, c);
        wprintw(msg_win, "%02X", (uint8_t)c);
        wattroff(msg_win, COLOR(MARKED));
    }else{
        //wprintw(msg_win, "%c" COLOR_GREEN "green" COLOR_RED "red" COLOR_OLD, c);
        waddch(msg_win, c);
    }
    return next;
}
static char *raw_putchar(char *next){
    waddch(msg_win, *next);
    return next+1;
}
static char *hex_putchar(char *next){
    waddch(msg_win, *next);
    return next+1;
}

static void msg_win_redisplay(bool for_resize){
    werase(msg_win);
    Line *l = firstline;
    //static char *buf = NULL;
    int nlines = 0; // total amount of lines @ output
    int linemax = LINES - 2;
    char *(*putfn)(char *);
    switch(disp_type){
        case DISP_RAW:
            putfn = raw_putchar;
        break;
        case DISP_HEX:
            putfn = hex_putchar;
        break;
        default:
            putfn = text_putchar;
    }
    for(; l && (nlines < linemax); l = l->next){
        wmove(msg_win, nlines, 0);
        //size_t contlen = strlen(l->contents) + 128;
        //buf = realloc(buf, contlen);
        char *ptr = l->contents;
        while((ptr = putfn(ptr)) && *ptr && nlines < linemax){
            nlines = msg_win->_cury;
        }
        ++nlines;
    }
    if(for_resize) wnoutrefresh(msg_win);
    else wrefresh(msg_win);
}

static void cmd_win_redisplay(bool for_resize){
    int cursor_col = 3 + strlen(dispnames[input_type]) + rl_point; // " > " width is 3
    werase(cmd_win);
    int x = 0, maxw = COLS-2;
    if(cursor_col > maxw){
        x = cursor_col - maxw;
        cursor_col = maxw;
    }
    char abuf[4096];
    snprintf(abuf, 4096, "%s > %s", dispnames[input_type], rl_line_buffer);
    waddstr(cmd_win, abuf+x);
    wmove(cmd_win, 0, cursor_col);
    if(for_resize) wnoutrefresh(cmd_win);
    else wrefresh(cmd_win);
    keypad(cmd_win, TRUE);
    if(insert_mode) curs_set(2);
    else curs_set(0);
}

static void readline_redisplay(){
    cmd_win_redisplay(false);
}

static void show_mode(bool for_resize){
    static const char *insmodetext = "INSERT (F1 - help)";
    wclear(sep_win);
    char buf[128];
    if(insert_mode){
        if(dtty){
        switch(dtty->type){
            case DEV_NETSOCKET:
                snprintf(buf, 127, "%s HOST: %s, ENDLINE: %s, PORT: %s",
                    insmodetext, dtty->name, dtty->seol, dtty->port);
            break;
            case DEV_UNIXSOCKET:
                if(*dtty->name)
                    snprintf(buf, 127, "%s PATH: %s, ENDLINE: %s",
                        insmodetext, dtty->name, dtty->seol);
                else // name starting from \0
                    snprintf(buf, 127, "%s PATH: \\0%s, ENDLINE: %s",
                        insmodetext, dtty->name+1, dtty->seol);
            break;
            case DEV_TTY:
                snprintf(buf, 127, "%s DEV: %s, ENDLINE: %s, SPEED: %d, FORMAT: %s",
                    insmodetext, dtty->name, dtty->seol, dtty->speed, dtty->port);
            break;
            default:
            break;
        }}else{
            snprintf(buf, 127, "INSERT (TAB to switch, ctrl+D to quit) NOT INITIALIZED");
        }
    }else{
        snprintf(buf, 127, "SCROLL (F1 - help) ENDLINE: %s", dtty?dtty->seol:"n");
    }
    wattron(sep_win, COLOR(BKGMARKED));
    wprintw(sep_win, "%s ", dispnames[disp_type]);
    wattroff(sep_win, COLOR(BKGMARKED));
    wprintw(sep_win, "%s", buf);
    if(for_resize) wnoutrefresh(sep_win);
    else wrefresh(sep_win);
    cmd_win_redisplay(for_resize);
}

/**
 * @brief ShowData - show string on display
 * @param text - text string
 */
void ShowData(const char *text){
    if(!text) return;
    if(!*text) text = " "; // empty string
    Line *lp = malloc(sizeof(Line));
    lp->contents = strdup(text);
    lp->prev = curr;
    lp->next = NULL;
    lp->Nline = nr_lines++;
    if(!curr || !head){
        head = curr = firstline = lp;
    }else
        curr->next = lp;
    curr = lp;
    // roll back to show last input
    if(curr->prev){
        firstline = curr;
        int totalln = (strlen(firstline->contents) - 1)/COLS + 1;
        while(firstline->prev){
            totalln += (strlen(firstline->prev->contents) - 1)/COLS + 1;
            if(totalln > LINES-2) break;
            firstline = firstline->prev;
        }
    }
    msg_win_redisplay(true);
    show_mode(true);
    doupdate();
}

static void resize(){
    if(LINES > 2){
        wresize(msg_win, LINES - 2, COLS);
        wresize(sep_win, 1, COLS);
        wresize(cmd_win, 1, COLS);
        mvwin(sep_win, LINES - 2, 0);
        mvwin(cmd_win, LINES - 1, 0);
    }
    msg_win_redisplay(true);
    show_mode(true);
    doupdate();
}

void init_ncurses(){
    if (!initscr())
        fail_exit("Failed to initialize ncurses");
    visual_mode = true;
    if(has_colors()){
        start_color();
        use_default_colors();
    }
    cbreak();
    noecho();
    nonl();
    intrflush(NULL, FALSE);
    keypad(cmd_win, TRUE);
    curs_set(2);
    if(LINES > 2){
        msg_win = newwin(LINES - 2, COLS, 0, 0);
        sep_win = newwin(1, COLS, LINES - 2, 0);
        cmd_win = newwin(1, COLS, LINES - 1, 0);
    }
    else{
        msg_win = newwin(1, COLS, 0, 0);
        sep_win = newwin(1, COLS, 0, 0);
        cmd_win = newwin(1, COLS, 0, 0);
    }
    if(!msg_win || !sep_win || !cmd_win)
        fail_exit("Failed to allocate windows");
    if(has_colors()){
        init_pair(BKG_NO, COLOR_WHITE, COLOR_BLUE);
        init_pair(BKGMARKED_NO, 1, COLOR_BLUE); // COLOR_RED used in my usefull_macros
        init_pair(NORMAL_NO, COLOR_WHITE, COLOR_BLACK);
        init_pair(MARKED_NO, COLOR_CYAN, COLOR_BLACK);
        wbkgd(sep_win, COLOR(BKG));
    }else{
        wbkgd(sep_win, A_STANDOUT);
    }
    show_mode(false);
    mousemask(BUTTON4_PRESSED|BUTTON5_PRESSED, NULL);
}

void deinit_ncurses(){
    visual_mode = false;
    delwin(msg_win);
    delwin(sep_win);
    delwin(cmd_win);
    endwin();
}

static char *previous_line = NULL; // previous line in readline input
static void got_command(char *line){
    if(!line) // Ctrl-D pressed on empty line
        should_exit = true;
    else{
        if(!*line) return; // zero length
        if(!previous_line || strcmp(previous_line, line)) add_history(line); // omit repeats
        FREE(previous_line);
        if(convert_and_send(input_type, line) == -1){
            ERRX("Device disconnected");
        }
        previous_line = line;
    }
}

void init_readline(){
    rl_catch_signals = 0;
    rl_catch_sigwinch = 0;
    rl_deprep_term_function = NULL;
    rl_prep_term_function = NULL;
    rl_change_environment = 0;
    rl_getc_function = readline_getc;
    rl_input_available_hook = readline_input_avail;
    rl_redisplay_function = readline_redisplay;
    rl_callback_handler_install("", got_command);
}

/**
 * @brief change_disp - change input or output data types (text/raw/hex)
 * @param in, out - types for input and display
  */
static void change_disp(disptype in, disptype out){
    if(in >= DISP_TEXT && in < DISP_UNCHANGED){
        input_type = in;
        DBG("input -> %s", dispnames[in]);
    }
    if(out >= DISP_TEXT && out < DISP_UNCHANGED){
        disp_type = out;
    }
    show_mode(false);
}

void deinit_readline(){
    rl_callback_handler_remove();
}

static void rolldown(){
    if(firstline && firstline->prev){
        firstline = firstline->prev;
        msg_win_redisplay(false);
        show_mode(false);
        doupdate();
    }
}

static void rollup(){
    if(firstline && firstline->next){
        firstline = firstline->next;
        msg_win_redisplay(false);
        show_mode(false);
        doupdate();
    }
}

static const char *help[] = {
    "Common commands:",
    "  F1             - show this help",
    "  F2             - text mode",
    "  F3             - raw mode (all symbols in hex codes)",
    "  F4             - hexdump mode (like hexdump output)",
    "  mouse scroll   - scroll text output",
    "  q,^c,^d        - quit",
    "  TAB            - switch between scroll and edit modes",
    "    to change display/input (text/raw/hex) press Fx when scroll/edit",
    "    in scroll mode keys are almost the same like for this help"
    "  Text mode: in input and output all special symbols are like \\code",
    "  Raw mode: output only in hex, input in dec, 0xhex, 0bbin, 0oct (space separated)",
    "  Hexdump mode: output like hexdump, input only hex (with or without spaces)",
    "",
    "This help:",
    "  ^p,<Up>        - scroll the viewport up by one row",
    "  ^n,<Down>      - scroll the viewport down by one row",
    "  ^l,<Left>      - scroll the viewport left by one column",
    "  ^r,<Right>     - scroll the viewport right by one column",
    "  h,<Home>       - scroll the viewport to top of file",
    "  ^F,<PageDn>    - scroll to the next page",
    "  ^B,<PageUp>    - scroll to the previous page",
    "  e,<End>        - scroll the viewport to end of file",
    0
};

/**
 * @brief cmdline - console reading process; runs as separate thread
 * @param arg - tty/socket device to write strings entered by user
 * @return NULL
 */
void *cmdline(void* arg){
    MEVENT event;
    dtty = (chardevice*)arg;
    show_mode(false);
    do{
        int c = wgetch(cmd_win);
        bool processed = true;
        //DBG("wgetch got %d", c);
        disptype dt = DISP_UNCHANGED;
        switch(c){ // common keys for both modes
            case KEY_F(1): // help
                DBG("\n\nASK for help\n\n");
                popup_msg(msg_win, help);
            break;
            case KEY_F(2): // TEXT mode
                DBG("\n\nIN TEXT mode\n\n");
                dt = DISP_TEXT;
            break;
            case KEY_F(3): // RAW mode
                DBG("\n\nIN RAW mode\n\n");
                dt = DISP_RAW;
            break;
            case KEY_F(4): // HEX mode
                DBG("\n\nIN HEX mode\n\n");
                dt = DISP_HEX;
            break;
            case KEY_MOUSE:
                if(getmouse(&event) == OK){
                    if(event.bstate & (BUTTON4_PRESSED)) rolldown(); // wheel up
                    else if(event.bstate & (BUTTON5_PRESSED)) rollup(); // wheel down
                }
            break;
            case '\t': // tab switch between scroll and edit mode
                insert_mode = !insert_mode;
                show_mode(false);
            break;
            case KEY_RESIZE:
                resize();
            break;
            default:
                processed = false;
        }
        if(dt != DISP_UNCHANGED){
            if(insert_mode) change_disp(dt, DISP_UNCHANGED);
            else change_disp(DISP_UNCHANGED, dt);
        }
        if(processed) continue;
        if(insert_mode){
            DBG("forward_to_readline(%d)", c);
            char *ptr = NULL;
            switch(c){ // check special keys
                case KEY_UP:
                    ptr = "A";
                break;
                case KEY_DOWN:
                    ptr = "B";
                break;
                case KEY_RIGHT:
                    ptr = "C";
                break;
                case KEY_LEFT:
                    ptr = "D";
                break;
                case KEY_BACKSPACE:
                    ptr = "H";
                break;
                case KEY_IC:
                    DBG("key insert");
                    ptr = "2~";
                break;
                case KEY_DC:
                    ptr = "3~";
                break;
                case KEY_HOME:
                    ptr = "H";
                break;
                case KEY_PPAGE:
                    ptr = "5~";
                break;
                case KEY_NPAGE:
                    ptr = "6~";
                break;
                case KEY_END:
                    ptr = "F";
                break;
                default:
                    forward_to_readline(c);
            }
            if(ptr){ // arrows and so on: 27, 91, code
                forward_to_readline(27);
                forward_to_readline(91);
                while(*ptr) forward_to_readline(*ptr++);
            }
        }else{
            switch(c){ // TODO: add home/end
                case KEY_UP: // roll down for one item
                    rolldown();
                break;
                case KEY_DOWN: // roll up for one item
                    rollup();
                break;
                case KEY_PPAGE: // PageUp: roll down for 10 items
                    for(int i = 0; i < 10; ++i){
                        if(firstline && firstline->prev) firstline = firstline->prev;
                        else break;
                    }
                    msg_win_redisplay(false);
                break;
                case KEY_NPAGE: // PageUp: roll up for 10 items
                    for(int i = 0; i < 10; ++i){
                        if(firstline && firstline->next) firstline = firstline->next;
                        else break;
                    }
                    msg_win_redisplay(false);
                break;
                default:
                    if(c == 'q' || c == 'Q') should_exit = true; // quit
            }
        }
    }while(!should_exit);
    signals(0);
    return NULL;
}
