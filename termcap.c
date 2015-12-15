/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Terminal capability interaction.
 *
 * Copyright (c) 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#undef n_FILE
#define n_FILE termcap

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE()
#ifdef HAVE_TERMCAP
/* If available, curses.h must be included before term.h! */
#ifdef HAVE_TERMCAP_CURSES
# include <curses.h>
#endif
#include <term.h>

static char *a_termcap_buffer, *a_termcap_ti, *a_termcap_te;

static int a_termcap_putc(int c);

static int
a_termcap_putc(int c){
   return putchar(c);
}

FL void
(termcap_init)(void){
   /* For newer ncurses based termcap emulation buf will remain unused, for
    * elder non-emulated ones really weird things will happen if an entry
    * would require more than 1024 bytes, so don't mind.
    * Things are more unserious with cmdbuf, but a single termcap command
    * should really not excess that limit */
   char buf[1024 + 512], cmdbuf[2048], *cpb, *cpti, *cpte, *cp;
   NYD_ENTER;

   assert(options & OPT_INTERACTIVE);

   if(!ok_blook(term_ca_mode))
      goto jleave;
   if((cp = env_vlook("TERM", FAL0)) == NULL)
      goto jleave;

   if(!tgetent(buf, cp))
      goto jleave;
   cpb = cmdbuf;

   cpti = cpb;
   if((cp = tgetstr(UNCONST("ti"), &cpb)) == NULL)
      goto jleave;
   cpte = cpb;
   if((cp = tgetstr(UNCONST("te"), &cpb)) == NULL)
      goto jleave;

   a_termcap_buffer = smalloc(PTR2SIZE(cpb - cmdbuf));
   memcpy(a_termcap_buffer, cmdbuf, PTR2SIZE(cpb - cmdbuf));

   a_termcap_ti = a_termcap_buffer + PTR2SIZE(cpti - cmdbuf);
   a_termcap_te = a_termcap_ti + PTR2SIZE(cpte - cpti);

   tputs(a_termcap_ti, 1, &a_termcap_putc);
   fflush(stdout);
jleave:
   NYD_LEAVE;
}

FL void
(termcap_destroy)(void){
   NYD_ENTER;
   assert(options & OPT_INTERACTIVE);

   if(a_termcap_buffer != NULL){
      tputs(a_termcap_te, 1, &a_termcap_putc);
      fflush(stdout);

      free(a_termcap_buffer);
      DBG( a_termcap_buffer = NULL; )
   }
   NYD_LEAVE;
}

FL void
(termcap_suspend)(void){
   NYD2_ENTER;
   assert(options & OPT_INTERACTIVE);

   if(a_termcap_buffer != NULL){
      tputs(a_termcap_te, 1, &a_termcap_putc);
      fflush(stdout);
   }
   NYD2_LEAVE;
}

FL void
(termcap_resume)(void){
   NYD2_ENTER;
   assert(options & OPT_INTERACTIVE);

   if(a_termcap_buffer != NULL){
      tputs(a_termcap_ti, 1, &a_termcap_putc);
      fflush(stdout);
   }
   NYD2_LEAVE;
}
#endif /* HAVE_TERMCAP */

/* s-it-mode */
