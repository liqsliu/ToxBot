/*  log.c
 *
 *
 *  Copyright (C) 2021 toxbot All Rights Reserved.
 *
 *  This file is part of toxbot.
 *
 *  toxbot is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  toxbot is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with toxbot. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdarg.h>

#include <string.h>

#include "misc.h"

#define TIMESTAMP_SIZE 64
#define MAX_MESSAGE_SIZE 512

static struct tm *get_wall_time(void)
{
    struct tm *timeinfo;
    time_t t = get_time();
    timeinfo = localtime((const time_t *) &t);
    return timeinfo;
}

void log_timestamp(const char *message, ...)
{
    char format[MAX_MESSAGE_SIZE];

    va_list args;
    va_start(args, message);
    vsnprintf(format, sizeof(format), message, args);
    va_end(args);

    char ts[TIMESTAMP_SIZE];
    strftime(ts, TIMESTAMP_SIZE,"[%H:%M:%S]", get_wall_time());

    printf("%s %s\n", ts, format);
}

void log_error_timestamp(int err, const char *message, ...)
{
    char format[MAX_MESSAGE_SIZE];

    va_list args;
    va_start(args, message);
    vsnprintf(format, sizeof(format), message, args);
    va_end(args);

    char ts[TIMESTAMP_SIZE];
    strftime(ts, TIMESTAMP_SIZE,"[%H:%M:%S]", get_wall_time());

    fprintf(stderr, "%s %s (error %d)\n", ts, format, err);
}


uint8_t short_text_length = 64;
/** char *shorten_text(char *text) */
/* void logs(const char *text) */
void logs(const char *message, ...)
{
    char text[short_text_length];

    va_list args;
    va_start(args, message);
    vsnprintf(text, sizeof(text), message, args);
    va_end(args);

    char ts[TIMESTAMP_SIZE];
    strftime(ts, TIMESTAMP_SIZE,"[%H:%M:%S]", get_wall_time());

    size_t len = strlen(text);
    if (len < short_text_length) {
        printf("%s %s\n", ts, text);
    } else {
        /* if (len > TOX_MAX_MESSAGE_LENGTH) { */
        /*     log_timestamp("len is too big: %lu", len); */
        /*     len > TOX_MAX_MESSAGE_LENGTH; */
        /* } */
        char s[short_text_length];
        char s2[short_text_length];
        sprintf(s2, "...%d/%lu", short_text_length, len);
        /* printf("s2: %s\n", s2); */
        size_t len2 =  short_text_length-1 - strlen(s2);
        char *p=s;
        for (int i=0; i<short_text_length; ++i) {
            /** if (strlen(s) < len2) { */
            if (p-s >= len2) {
                break;
            }
            if (s[i] != '\n') {
                *p = text[i];
            } else {
                *p = '\\';
                ++p;
                *p = 'n';
            }
            ++p;
        }
        *p = '\0';
        /* printf("s: %s\n", s); */
        /** log_timestamp("s: %s", s); */
        /** sprintf(text, "%s%s", s, s2); */
        /** log_timestamp("text: %s", text); */
        /** printf(text); */
        /** return text; */
        /* printf("%s%s\n", s, s2); */
        strcat(s, s2);
        printf("%s %s\n", ts, s);
    }

}


