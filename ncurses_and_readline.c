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

//#include <signal.h>

#include "dbg.h"
#include "ttysocket.h"
#include "ncurses_and_readline.h"
#include "popup_msg.h"
#include "string_functions.h"

enum { // using colors
    BKG_NO = 1,   // normal status string
    BKGMARKED_NO, // marked status string
    NORMAL_NO,    // normal output
    MARKED_NO,    // marked output
    ERROR_NO      // error displayed
};
#define COLOR(x)  COLOR_PAIR(x ## _NO)


// Keeps track of the terminal mode so we can reset the terminal if needed on errors
static bool visual_mode = false;
// insert commands when true; roll upper screen when false
static bool insert_mode = true;
static bool should_exit = false;

static disptype disp_type = DISP_TEXT;  // type of displaying data
static disptype input_type = DISP_TEXT; // parsing type of input data
const char *dispnames[DISP_SIZE] = {"TEXT", "RAW", "HEX", "RTU (RAW)", "RTU (HEX)", "Error"};

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

// amount of lines in line_arary
//#define LINEARRSZ   (BUFSIZ/100)
#define LINEARRSZ   3
// formatted buffer initial size
#define FBUFSIZ     30
// raw buffer initial size
#define RBUFSIZ     30
// amount of spaces and delimeters in hexview string (address + 2 lines + 3 additional spaces)
#define HEXDSPACES   (13)
// maximal columns in line
#define MAXCOLS      (512)

typedef struct{
    char *formatted_buffer; // formatted buffer, ptrtobuf(i) returns i'th string
    size_t fbuf_size;       // size of `formatted_buffer` in bytes (zero-terminated lines)
    size_t fbuf_curr;       // current size of data in buffer
    size_t *line_array_idx; // indexes of starting symbols of each line in `formatted_buffer`
    size_t lnarr_size;      // full size of `line_array_idx`
    size_t lnarr_curr;      // current index in `line_array_idx` (last string)
    size_t linelen;         // max length of one line (excluding terminated 0)
    size_t lastlen;         // length of last string
} linebuf_t;

static linebuf_t *linebuffer = NULL; // string buffer for current representation
static uint8_t *raw_buffer = NULL; // raw buffer for incoming data
static size_t rawbufsz = 0, rawbufcur = 0; // full raw buffer size and current bytes amount
static size_t firstdisplineno = 0; // current first displayed line number (when scrolling)

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

/**
 * @brief show_err - show error on status string
 * @param text - text to display
 */
static void show_err(const char *text){
    wclear(sep_win);
    wattron(sep_win, COLOR(ERROR));
    wprintw(sep_win, "%s", text);
    wattroff(sep_win, COLOR(ERROR));
    wrefresh(sep_win);
}

/**
 * @brief ptrtobuf - get n'th string of `formatted_buffer`
 * @param lineno - line number
 * @return pointer to n'th string in formatted buffer
 */
static char *ptrtobuf(size_t lineno){
    if(!linebuffer->line_array_idx){
        show_err("line_array_idx not inited");
        return NULL;
    }
    if(lineno > linebuffer->lnarr_curr) return NULL;
    size_t idx = linebuffer->line_array_idx[lineno];
    if(idx > linebuffer->fbuf_curr) return NULL;
    return (linebuffer->formatted_buffer + idx);
}

#if 0
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
#endif

/**
 * @brief msg_win_redisplay - redisplay message window
 * @param group_refresh - true for grouping refresh (don't call doupdate())
 */
static void msg_win_redisplay(bool group_refresh){
    if(!linebuffer) return;
    werase(msg_win);
    int linemax = LINES - 2;
    if(firstdisplineno >= linebuffer->lnarr_curr){
        size_t l = (linemax > 1) ? linemax / 2 : 1;
        if(linebuffer->lnarr_curr < l) firstdisplineno = 0;
        else firstdisplineno = linebuffer->lnarr_curr - l;
    }
    size_t lastl = firstdisplineno + linemax;
    if(lastl > linebuffer->lnarr_curr+1) lastl = linebuffer->lnarr_curr+1;
    int i = 0;
    for(size_t curline = firstdisplineno; curline < lastl; ++curline, ++i){
        mvwprintw(msg_win, i, 0, "%s", linebuffer->formatted_buffer + linebuffer->line_array_idx[curline]);
    }
    if(group_refresh) wnoutrefresh(msg_win);
    else wrefresh(msg_win);
}

/**
 * @brief cmd_win_redisplay - redisplay command (input) window
 * @param group_refresh - true for grouping refresh (don't call doupdate())
 */
static void cmd_win_redisplay(bool group_refresh){
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
    if(group_refresh) wnoutrefresh(cmd_win);
    else wrefresh(cmd_win);
    keypad(cmd_win, TRUE);
    if(insert_mode) curs_set(2);
    else curs_set(0);
}

static void readline_redisplay(){
    cmd_win_redisplay(false);
}

/**
 * @brief show_mode - redisplay middle string (with work mode and settings) + call cmd_win_redisplay
 * @param group_refresh - true for grouping refresh (don't call doupdate())
 */
static void show_mode(bool group_refresh){
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
    if(group_refresh) wnoutrefresh(sep_win);
    else wrefresh(sep_win);
    cmd_win_redisplay(group_refresh);
}

/**
 * @brief redisplay_addline - redisplay after line adding
 * @param group_refresh - true for grouping refresh (don't call doupdate())
 */
static void redisplay_addline(){
    // redisplay only if previous line was on screen
    size_t lastno = firstdisplineno + LINES - 2; // number of first line out of screen
    if(lastno < linebuffer->lnarr_curr){
        return;
    }
    else if(lastno == linebuffer->lnarr_curr){ // scroll text by one line up
        ++firstdisplineno;
    }
    msg_win_redisplay(true);
    show_mode(true);
    doupdate();
}

/**
 * @brief linebuf_free - clear memory of `linebuffer`
 */
static void linebuf_free(){
    if(!linebuffer) return;
    FREE(linebuffer->formatted_buffer);
    FREE(linebuffer->line_array_idx);
    FREE(linebuffer);
}

/**
 * @brief chksizes - check sizes of buffers and enlarge them if need
 */
static void chksizes(){
    size_t addportion = MAXCOLS*3;
    if(rawbufsz - rawbufcur < addportion){ // raw buffer always should be big enough
        rawbufsz += (addportion > RBUFSIZ) ? addportion : RBUFSIZ;
        DBG("Enlarge raw buffer to %zd", rawbufsz);
        raw_buffer = realloc(raw_buffer, rawbufsz);
    }
    if(linebuffer->fbuf_size - linebuffer->fbuf_curr < addportion){ // realloc buffer if need
        linebuffer->fbuf_size += (addportion > FBUFSIZ) ? addportion : FBUFSIZ;
        DBG("Enlarge formatted buffer to %zd", linebuffer->fbuf_size);
        linebuffer->formatted_buffer = realloc(linebuffer->formatted_buffer, linebuffer->fbuf_size);
    }
    if(linebuffer->lnarr_size - linebuffer->lnarr_curr < 3){
        linebuffer->lnarr_size += LINEARRSZ;
        DBG("Enlarge line array buffer to %zd", linebuffer->lnarr_size);
        linebuffer->line_array_idx = realloc(linebuffer->line_array_idx, linebuffer->lnarr_size * sizeof(size_t));
    }
}

/**
 * @brief linebuf_new - allocate data for new linebuffer
 */
static void linebuf_new(){
    linebuf_free();
    linebuffer = MALLOC(linebuf_t, 1);
    linebuffer->fbuf_size = FBUFSIZ;
    linebuffer->formatted_buffer = MALLOC(char, linebuffer->fbuf_size);
    linebuffer->lnarr_size = LINEARRSZ;
    linebuffer->line_array_idx = MALLOC(size_t, linebuffer->lnarr_size);
    linebuffer->fbuf_curr = 0;
    linebuffer->lnarr_curr = 0;
    linebuffer->lastlen = 0;
    int maxcols = (COLS > MAXCOLS) ? MAXCOLS : COLS;
    // in hexdump view linelen is amount of symbols in one string, lastlen - amount of already printed symbols
    if(disp_type == DISP_HEX){ // calculate minimal line width and ;
        int n = maxcols - HEXDSPACES; // space for data
        n -= n/8; // spaces after each 8 symbols
        n /= 4; // hex XX + space + symbol view
        // n should be 1..4, 8 or 16x
        if(n < 1) n = 1; // minimal - one symbol per string
        else if(n > 4){
            if(n < 8) n = 4;
            //else if(n < 16) n = 8;
            else n -= n % 8;
        }
        linebuffer->linelen = n;
    }else linebuffer->linelen = maxcols;
    DBG("=====>> COLS=%d, maxcols=%d, linelen=%zd", COLS, maxcols, linebuffer->linelen);
    linebuffer->line_array_idx[0] = 0; // initialize first line
    chksizes();
}

/**
 * @brief finalize_line - finalize last line in linebuffer & increase buffer sizes if nesessary
 */
static void finalize_line(){
    chksizes();
    linebuffer->formatted_buffer[linebuffer->fbuf_curr++] = 0; // finalize line
    linebuffer->formatted_buffer[linebuffer->fbuf_curr] = 0; // and clear new line (`realloc` can generate some trash)
    DBG("Current line is %s, no=%zd, len=%zd", linebuffer->formatted_buffer + linebuffer->line_array_idx[linebuffer->lnarr_curr], linebuffer->lnarr_curr, linebuffer->lastlen);
    ++linebuffer->lnarr_curr;
    linebuffer->lastlen = 0;
    linebuffer->line_array_idx[linebuffer->lnarr_curr] = linebuffer->fbuf_curr;
    redisplay_addline();
}

/**
 * @brief FormatData - get new data portion and format it into displayed buffer
 * @param data - data start pointer in `raw_buffer`
 * @param len  - length of data portion
 */
void FormatData(const uint8_t *data, int len){
    if(COLS > MAXCOLS-1) ERRX("Too wide column");
    if(!data || len < 1) return;
    chksizes();
    DBG("Got %d bytes to process", len);
    while(len){
        // count amount of symbols in `data` to display until line is over
        int Nsymbols = 0, curidx = 0;
        int nrest = linebuffer->linelen - linebuffer->lastlen; // n symbols left in string for text/raw
        switch(disp_type){
            case DISP_TEXT: // 1 or 4 bytes per symbol
                while(nrest > 0 && curidx < len){
                    uint8_t c = data[curidx++];
                    if(c == '\n'){ // finish string
                        ++Nsymbols;
                        break;
                    }
                    if(c < 32 || c > 126) nrest -= 4; // "\xXX"
                    else --nrest;
                    if(nrest > -1) ++Nsymbols;
                }
            break;
            case DISP_RAW: // 3 bytes per symbol
                Nsymbols = nrest / 3;
            break;
            case DISP_HEX:
                Nsymbols = nrest;
            break;
            default:
            break;
        }
        if(Nsymbols > len) Nsymbols = len;
        if(Nsymbols == 0){
            DBG("No more plase in line - finalize");
            finalize_line();
            continue;
        }
        DBG("Process %d symbols", Nsymbols);
        if(disp_type != DISP_HEX){
            char *curptr = linebuffer->formatted_buffer+linebuffer->fbuf_curr;
            for(int i = 0; i < Nsymbols; ++i){
                uint8_t c = data[i];
                int nadd = 0;
                switch(disp_type){
                    case DISP_TEXT:
                        if(c == '\n'){ // finish line
                            DBG("Finish line, nadd=%d, i=%d!", nadd, i);
                            finalize_line();
                            break;
                        }
                        if(c < 32 || c > 126) nadd = sprintf(curptr, "\\x%.2X", c);
                        else{
                            nadd = 1;
                            *curptr = c;
                        }
                    break;
                    case DISP_RAW:
                        nadd = sprintf(curptr, "%-3.2X", c);
                    break;
                    default:
                    break;
                }
                linebuffer->fbuf_curr += nadd;
                linebuffer->lastlen += nadd;
                curptr += nadd;
            }
        }else{ // HEXDUMP: refill full string buffer
            char ascii[MAXCOLS]; // buffer for ASCII printing
            char *ptr = ptrtobuf(linebuffer->lnarr_curr);
            if(!ptr) ERRX("Can't get current line");
            size_t address = linebuffer->linelen * linebuffer->lnarr_curr; // string starting address
            const uint8_t *start = data - linebuffer->lastlen; // starting byte in hexdump string
            linebuffer->lastlen += Nsymbols;
            int nadd = sprintf(ptr, "%-10.8zX", address);
            ptr += nadd;
            size_t i = 0;
            for(; i < linebuffer->lastlen; ++i){
                if(0 == (i % 8)){
                    sprintf(ptr, " "); ++ptr; ++nadd;
                }
                uint8_t c = start[i];
                int x = sprintf(ptr, "%-3.2X", c);
                if(c > 31 && c < 127) ascii[i] = c;
                else ascii[i] = '.';
                ptr += x;
                nadd += x;
            }
            ascii[i] = 0;
            int emptyvals = (int)(linebuffer->linelen - linebuffer->lastlen);
            nadd += sprintf(ptr, "%*s|%*s|", 3*emptyvals+emptyvals/8, "", -((int)linebuffer->linelen), ascii);
            linebuffer->fbuf_curr = linebuffer->line_array_idx[linebuffer->lnarr_curr] + nadd;
            DBG("---- Total added symbols: %zd, fbuf_curr=%zd (%zd + %zd)", nadd, linebuffer->fbuf_curr,
                linebuffer->line_array_idx[linebuffer->lnarr_curr], nadd);
        }
        DBG("last=%d, line=%d", linebuffer->lastlen, linebuffer->linelen);
        if(linebuffer->lastlen == linebuffer->linelen) finalize_line();
        len -= Nsymbols;
        data += Nsymbols;
    }
}

/**
 * @brief AddData - add new data buffer to global buffer and last displayed string
 * @param data - data
 * @param len  - length of `data`
 */
void AddData(const uint8_t *data, int len){
    // now print all symbols into buff
    chksizes();
    memcpy(raw_buffer + rawbufcur, data, len);
    DBG("Got %d bytes, now buffer have %d", len, rawbufcur+len);
    FormatData(raw_buffer + rawbufcur, len);
    rawbufcur += len;
    redisplay_addline(); // display last symbols if can
}

static void resize(){
    DBG("RESIZE WINDOW");
    if(LINES > 2){
        wresize(msg_win, LINES - 2, COLS);
        wresize(sep_win, 1, COLS);
        wresize(cmd_win, 1, COLS);
        mvwin(sep_win, LINES - 2, 0);
        mvwin(cmd_win, LINES - 1, 0);
    }
    pthread_mutex_lock(&dtty->mutex);
    linebuf_new(); // free old and alloc new
    FormatData(raw_buffer, rawbufcur); // reformat all data
    pthread_mutex_unlock(&dtty->mutex);
    msg_win_redisplay(true);
    show_mode(true);
    doupdate();
}
/*
void swinch(_U_ int sig){
    //signal(SIGWINCH, swinch);
    DBG("got resize");
}*/

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
    wtimeout(cmd_win, 5);
    keypad(cmd_win, TRUE);
    if(has_colors()){
        init_pair(BKG_NO, COLOR_WHITE, COLOR_BLUE);
        init_pair(BKGMARKED_NO, 1, COLOR_BLUE); // COLOR_RED used in my usefull_macros
        init_pair(ERROR_NO, COLOR_BLACK, 1);
        init_pair(NORMAL_NO, COLOR_WHITE, COLOR_BLACK);
        init_pair(MARKED_NO, COLOR_CYAN, COLOR_BLACK);
        wbkgd(sep_win, COLOR(BKG));
    }else{
        wbkgd(sep_win, A_STANDOUT);
    }
    show_mode(false);
    mousemask(BUTTON4_PRESSED|BUTTON5_PRESSED, NULL);
    DBG("INIT raw buffer");
    rawbufsz = RBUFSIZ;
    raw_buffer = MALLOC(uint8_t, rawbufsz);
    linebuf_new();
    //signal(SIGWINCH, swinch);
}

void deinit_ncurses(){
    visual_mode = false;
    linebuf_free();
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
        int res = convert_and_send(input_type, line);
        if(res == 0) show_err("Wrong data format");
        else if(res == -1) ERRX("Device disconnected");
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
    if(in >= DISP_TEXT && in < DISP_UNCHANGED && in != input_type){
        input_type = in;
        DBG("input -> %s", dispnames[in]);
    }
    if(out >= DISP_TEXT && out <= DISP_HEX && out != disp_type){
        disp_type = out;
        DBG("output -> %s", dispnames[out]);
        resize(); // reformat everything
    }
    show_mode(false);
}

void deinit_readline(){
    rl_callback_handler_remove();
}

/**
 * @brief rolldown/rollup - roll text by `N` strings
 * @param N - amount of strings
 */
static void rolldown(size_t N){ // if N==0 goto first line
    DBG("rolldown for %zd, first was %zd", N, firstdisplineno);
    size_t old = firstdisplineno;
    if(firstdisplineno < N || N == 0) firstdisplineno = 0;
    else firstdisplineno -= N;
    DBG("old was %zd, become %zd", old, firstdisplineno);
    if(old == firstdisplineno) return;
    msg_win_redisplay(false);
    //msg_win_redisplay(true); show_mode(true); doupdate();
}
static void rollup(size_t N){ // if N==0 goto last line
    DBG("scroll up for %d", N);
    size_t half = (LINES+1)/2;
    if(firstdisplineno + half >= linebuffer->lnarr_curr){
        DBG("Don't need: %zd+%zd >= %zd", firstdisplineno, half, linebuffer->lnarr_curr);
        return; // don't scroll over a half of viewed area
    }
    size_t old = firstdisplineno;
    firstdisplineno += N;
    if(firstdisplineno + half > linebuffer->lnarr_curr || N == 0) firstdisplineno = linebuffer->lnarr_curr - half;
    DBG("old was %zd, become %zd", old, firstdisplineno);
    if(old == firstdisplineno) return;
    msg_win_redisplay(false);
    //msg_win_redisplay(true); show_mode(true); doupdate();
}

static const char *help[] = {
    "Common commands:",
    "  F1             - show this help",
    "  F2             - text mode",
    "  F3             - raw mode (all symbols in hex codes)",
    "  F4             - hexdump mode (like hexdump output)",
    "  F5             - modbus RTU mode (only for sending), input like RAW: ID data",
    "  F6             - modbus RTU mode (only for sending), input like HEX: ID data",
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
        if(c < 0) continue;
        bool processed = true;
        DBG("wgetch got %d", c);
        disptype dt = DISP_UNCHANGED;
        switch(c){ // common keys for both modes
            case KEY_F(1): // help
                DBG("\n\nASK for help\n\n");
                popup_msg(msg_win, help);
                resize(); // call `resize` to enshure that no problems would be later
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
            case KEY_F(5): // RTU mode
                DBG("\n\nIN RTU RAW mode\n\n");
                dt = DISP_RTURAW;
            break;
            case KEY_F(6): // RTU mode
                DBG("\n\nIN RTU HEX mode\n\n");
                dt = DISP_RTUHEX;
            break;
            case KEY_MOUSE:
                if(getmouse(&event) == OK){
                    if(event.bstate & (BUTTON4_PRESSED)) rolldown(1); // wheel up
                    else if(event.bstate & (BUTTON5_PRESSED)) rollup(1); // wheel down
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
                case KEY_HOME:
                    rolldown(0);
                break;
                case KEY_END:
                    rollup(0);
                break;
                case KEY_UP: // roll down for one item
                    rolldown(1);
                break;
                case KEY_DOWN: // roll up for one item
                    rollup(1);
                break;
                case KEY_PPAGE: // PageUp: roll down for 2/3 of screen
                    rolldown((2*LINES)/3);
                break;
                case KEY_NPAGE: // PageUp: roll up for 2/3 of screen
                    rollup((2*LINES)/3);
                break;
                default:
                    if(c == 'q' || c == 'Q') should_exit = true; // quit
            }
        }
    }while(!should_exit);
    signals(0);
    return NULL;
}
