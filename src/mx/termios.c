/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of termios.h.
 *@ FIXME everywhere: tcsetattr() generates SIGTTOU when we're not in
 *@ FIXME foreground pgrp, and can fail with EINTR!! also affects
 *@ FIXME termios_state_reset()
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#undef su_FILE
#define su_FILE termios
#define mx_SOURCE
#define mx_SOURCE_TERMIOS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/ioctl.h>

#include <termios.h>

#include <su/mem.h>

#include "mx/tty.h"

#include "mx/termios.h"
#include "su/code-in.h"

struct a_termios_g{
   boole tiosg_init;
   u8 tiosg_state_cmd;
   u8 tiosc__pad[6];
   struct termios tiosg_normal;
   struct termios tiosg_other;
};

struct a_termios_g a_termios_g;

boole
mx_termios_cmd(enum mx_termios_cmd cmd, uz a1){
   /* xxx tcsetattr not correct says manual: would need to requery and check
    * whether all desired changes made it instead! */
   boole rv;
   NYD_IN;

   if(!a_termios_g.tiosg_init){
      ASSERT(cmd == mx_TERMIOS_CMD_QUERY);
      a_termios_g.tiosg_init = TRU1;
      a_termios_g.tiosg_state_cmd = mx_TERMIOS_CMD_NORMAL;
   }

   switch(cmd){
   default:
   case mx_TERMIOS_CMD_QUERY:
      rv = (tcgetattr(fileno(mx_tty_fp), &a_termios_g.tiosg_normal) == 0);
      /* XXX always set ECHO and ICANON in our "normal" canonical state */
      a_termios_g.tiosg_normal.c_lflag |= ECHO | ICANON;
      break;
   case mx_TERMIOS_CMD_NORMAL:
      rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &a_termios_g.tiosg_normal
            ) == 0);
      break;
   case mx_TERMIOS_CMD_PASSWORD:
      su_mem_copy(&a_termios_g.tiosg_other, &a_termios_g.tiosg_normal,
         sizeof(a_termios_g.tiosg_normal));
      a_termios_g.tiosg_other.c_iflag &= ~(ISTRIP);
      a_termios_g.tiosg_other.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
      rv = (tcsetattr(fileno(mx_tty_fp), TCSAFLUSH, &a_termios_g.tiosg_other
            ) == 0);
      break;
   case mx_TERMIOS_CMD_RAW:
   case mx_TERMIOS_CMD_RAW_TIMEOUT:
      su_mem_copy(&a_termios_g.tiosg_other, &a_termios_g.tiosg_normal,
         sizeof(a_termios_g.tiosg_normal));
      a1 = MIN(U8_MAX, a1);
      if(cmd == mx_TERMIOS_CMD_RAW){
         a_termios_g.tiosg_other.c_cc[VMIN] = S(u8,a1);
         a_termios_g.tiosg_other.c_cc[VTIME] = 0;
      }else{
         a_termios_g.tiosg_other.c_cc[VMIN] = 0;
         a_termios_g.tiosg_other.c_cc[VTIME] = S(u8,a1);
      }
      a_termios_g.tiosg_other.c_iflag &= ~(ISTRIP | IGNCR | IXON | IXOFF);
      a_termios_g.tiosg_other.c_lflag &= ~(ECHO /*| ECHOE | ECHONL */|
            ICANON | IEXTEN | ISIG);
      rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &a_termios_g.tiosg_other
            ) == 0);
      break;
   }

   if(rv && cmd != mx_TERMIOS_CMD_QUERY)
      a_termios_g.tiosg_state_cmd = cmd;

   NYD_OU;
   return rv;
}

void
mx_termios_dimension_lookup(struct mx_termios_dimension *tiosdp){
   struct termios tbuf;
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
   struct winsize ws;
#elif defined TIOCGSIZE
   struct ttysize ts;
#else
# error One of TCGETWINSIZE, TIOCGWINSZ and TIOCGSIZE
#endif
   NYD_IN;

#ifdef mx_HAVE_TCGETWINSIZE
   if(tcgetwinsize(fileno(mx_tty_fp), &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGWINSZ
   if(ioctl(fileno(mx_tty_fp), TIOCGWINSZ, &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGSIZE
   if(ioctl(fileno(mx_tty_fp), TIOCGSIZE, &ws) == -1)
      ts.ts_lines = ts.ts_cols = 0;
#endif

#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
   if(ws.ws_row != 0)
      tiosdp->tiosd_height = tiosdp->tiosd_real_height = ws.ws_row;
#elif defined TIOCGSIZE
   if(ts.ts_lines != 0)
      tiosdp->tiosd_height = tiosdp->tiosd_real_height = ts.ts_lines;
#endif
   else{
      speed_t ospeed;

      ospeed = ((tcgetattr(fileno(mx_tty_fp), &tbuf) == -1)
            ? B9600 : cfgetospeed(&tbuf));

      if(ospeed < B1200)
         tiosdp->tiosd_height = 9;
      else if(ospeed == B1200)
         tiosdp->tiosd_height = 14;
      else
         tiosdp->tiosd_height = mx_TERMIOS_DEFAULT_HEIGHT;

      tiosdp->tiosd_real_height = mx_TERMIOS_DEFAULT_HEIGHT;
   }

   if(0 == (
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
       tiosdp->tiosd_width = ws.ws_col
#elif defined TIOCGSIZE
       tiosdp->tiosd_width = ts.ts_cols
#endif
         ))
      tiosdp->tiosd_width = mx_TERMIOS_DEFAULT_WIDTH;

   NYD_OU;
}

#include "su/code-ou.h"
/* s-it-mode */
