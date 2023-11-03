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

#include <stdio.h>
#include <string.h>

#include "string_functions.h"

// end of line - for text mode
static char *eol = "\n";
static int eollen = 1;

// read text string and throw out all < 31 and > 126
static inline const char *omit_nonletters(disptype input_type, const char *line){
    int start = (input_type == DISP_TEXT) ? 31 : 32; // remove spaces for RAW and HEX modes
    while(*line){
        char c = *line;
        if(c > start && c < 127) break;
        ++line;
    }
    return line;
}

// return hex of given sybol (0..9, a..f or A..F) or -1
static inline int hex2i(char c){
    if(c >= '0' && c <= '9') return c - '0';
    else if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    else if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static inline const char *getbin(const char *line, int *ch){
    char c = *line;
    if(c < '0' || c > '1'){
        *ch = -1;
        return line+1;
    }
    int num = 0, ctr = 8;
    do{
        if(--ctr < 0) break; // nineth symbol
        if(c < '0' || c > '1'){
            break;
        }
        num <<= 1;
        if(c == '1') num |= 1;
        ++line;
    }while((c = *line));
    *ch = num < 256 ? num : -1;
    return line;

}

/**
 * @brief getoct read octal number without first '0' (up to three symbols)
 * @param line - pointer to string
 * @param ch - data read (or -1 if error)
 * @return pointer to next symbol after this (or first incorrect symbol)
 */
static inline const char *getoct(const char *line, int *ch){
    char c = *line;
    if(c < '0' || c > '7'){
        *ch = -1;
        return line+1;
    }
    int num = 0, ctr = 3;
    do{
        if(--ctr < 0) break; // fourth symbol
        if(c < '0' || c > '7'){ // other symbol
            break;
        }
        num <<= 3;
        num += c - '0';
        ++line;
    }while((c = *line));
    *ch = num < 256 ? num : -1;
    return line;

}

static inline const char *getdec(const char *line, int *ch){
    char c = *line;
    if(c < '0' || c > '9'){
        *ch = -1;
        return line+1;
    }
    int num = 0, ctr = 3;
    do{
        if(--ctr < 0) break; // fourth symbol
        if(c < '0' || c > '9'){ // other symbol
            break;
        }
        num *= 10;
        num += c - '0';
        ++line;
    }while((c = *line));
    *ch = num < 256 ? num : -1;
    return line;
}

/**
 * @brief gethex - read hex number without first '\x'
 * @param line - pointer to string
 * @param ch - data read (or -1 if error)
 * @return pointer to next symbol after hex number (x or xx)
 */
static inline const char *gethex(const char *line, int *ch){
    int i = hex2i(*line++);
    if(i > -1){
        int j = hex2i(*line);
        if(j > -1){
            i = (i<<4) | j;
            ++line;
        }
    }
    *ch = i;
    return line;
}

/**
 * @brief getspec - get special symbol (after '\') (without unicode support!)
 * @param line - pointer to string
 * @param ch - data read (or -1 if error)
 * @return pointer to next symbol after this
 */
static inline const char *getspec(const char *line, int *ch){
    if(!*line){ *ch = -1; return line; }
    int got = -1, s = *line++; // next symbol after '\'
    if(s >= '0' && s <= '7'){ // octal symbol
        line = getoct(line-1, &got);
    }else switch(s){
        case 'a': got = '\a'; break;
        case 'b': got = '\b'; break;
        case 'e': got = '\e'; break;
        case 'f': got = '\f'; break;
        case 'n': got = '\n'; break;
        case 'r': got = '\r'; break;
        case 't': got = '\t'; break;
        case 'v': got = '\v'; break;
        case 'x': line = gethex(line, &got); break; // hex symbol
        default:// return "as is" all other printable symbols
            if(s > 31 && s < 127) got = s;
        break;
    }
    *ch = got;
    return line;
}

/**
 * @brief convert_and_send - convert input line and send it (in text mode add `eol`)
 * @param line - line with data
 * @return amount of bytes sent, 0 if error or -1 if disconnect
 */
int convert_and_send(disptype input_type, const char *line){
    static char *buf = NULL;
    static size_t bufsiz = 0;
    size_t curpos = 0; // position in `buf`
    line = omit_nonletters(input_type, line);
    while(*line){
        if(curpos >= bufsiz){ // out ouptut buffer can't be larger than input
            bufsiz += BUFSIZ;
            buf = realloc(buf, bufsiz);
        }
        int ch = -1;
        switch(input_type){
            case DISP_TEXT: // only check for '\'
                ch = *line++;
                if(ch == '\\') line = getspec(line, &ch);
            break;
            case DISP_RAW: // read next uint8_t and put into buffer
                ch = *line++;
                if(ch == '0'){ // number: 0, 0xHH, 0OOO, 0bBBBBBBBB
                    ch = *line;
                    switch(ch){
                        case 'x': // hexadecimal
                        case 'X':
                            line = gethex(line + 1, &ch);
                        break;
                        case 'b':
                        case 'B':
                            line = getbin(line + 1, &ch);
                        break;
                        default: // zero or octal
                            if(ch >= '0' && ch <= '7') line = getoct(line, &ch);
                            else ch = 0;
                        break;
                    }
                }else if(ch > '0' && ch <= '9'){ // decimal number
                    line = getdec(line-1, &ch);
                } // else - letter (without escape-symbols!)
            break;
            case DISP_HEX: // read next 2 hex bytes and put into buffer
                line = gethex(line, &ch);
            break;
            default:
                return 0; // unknown display type
        }
        if(ch > -1) buf[curpos++] = ch;
        line = omit_nonletters(input_type, line);
    }
    // now insert EOL in text mode
    if(input_type == DISP_TEXT){
        if(curpos+eollen >= bufsiz){
            bufsiz += BUFSIZ;
            buf = realloc(buf, bufsiz);
        }
        snprintf(buf+curpos, eollen+1, "%s", eol);
        curpos += eollen;
        /*snprintf(buf+curpos, 7, "_TEST_");
        curpos += 6;*/
    }
    return SendData(buf, curpos);
}

/**
 * @brief changeeol - set EOL to given
 * @param e - new end of line for text mode
 * WARNING! This function doesn't call free(), so don't call it more than once
 */
void changeeol(const char *e){
    eollen = strlen(e);
    eol = strdup(e);
}
