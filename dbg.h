/*
 * This file is part of the ttyterm project.
 * Copyright 2022 Edward V. Emelianov <edward.emelianoff@gmail.com>.
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
#ifndef DBG_H__
#define DBG_H__

// dirty trick
#define termios xxtermios
#include <usefull_macros.h>
#undef termios

#ifdef EBUG
#undef DBG
#undef FNAME
#undef ERR
#undef ERRX
//#undef WARN
#undef WARNX
#define FNAME() do{LOGDBG("%s (%s, line %d)", __func__, __FILE__, __LINE__);}while(0)
#define DBG(...) do{LOGDBG("%s (%s, line %d):", __func__, __FILE__, __LINE__); \
                  LOGDBGADD(__VA_ARGS__);} while(0)
#define ERR(...) do{red(__VA_ARGS__); printf("\n"); LOGERR(__VA_ARGS__); signals(9);}while(0)
#define ERRX(...) do{red(__VA_ARGS__); printf("\n"); LOGERR(__VA_ARGS__); signals(9);}while(0)
//#define WARN(...) do{LOGWARN(__VA_ARGS__);}while(0)
#define WARNX(...) do{red(__VA_ARGS__); printf("\n"); LOGWARN(__VA_ARGS__);}while(0)
#endif

#endif // DBG_H__
