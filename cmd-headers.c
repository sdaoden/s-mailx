/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header display, search, etc., related user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef n_FILE
#define n_FILE cmd_headers

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

static int        _screen;

/* ... And place the extracted date in `date' */
static void    _parse_from_(struct message *mp, char date[n_FROM_DATEBUF]);

/* Print out the header of a specific message
 * __hprf: handle *headline*
 * __subject: return -1 if Subject: yet seen, otherwise smalloc()d Subject:
 * __putindent: print out the indenting in threaded display */
static void    _print_head(size_t yetprinted, size_t msgno, FILE *f,
                  bool_t threaded);
static void    __hprf(size_t yetprinted, char const *fmt, size_t msgno,
                  FILE *f, bool_t threaded, char const *attrlist);
static char *  __subject(struct message *mp, bool_t threaded,
                  size_t yetprinted);
static int     __putindent(FILE *fp, struct message *mp, int maxwidth);

static int     _dispc(struct message *mp, char const *a);

/* Shared `z' implementation */
static int a_cmd_scroll(char const *arg, bool_t onlynew);

/* Shared `headers' implementation */
static int     _headers(int msgspec);

static void
_parse_from_(struct message *mp, char date[n_FROM_DATEBUF]) /* TODO line pool */
{
   FILE *ibuf;
   int hlen;
   char *hline = NULL;
   size_t hsize = 0;
   NYD_ENTER;

   if ((ibuf = setinput(&mb, mp, NEED_HEADER)) != NULL &&
         (hlen = readline_restart(ibuf, &hline, &hsize, 0)) > 0)
      extract_date_from_from_(hline, hlen, date);
   if (hline != NULL)
      free(hline);
   NYD_LEAVE;
}

static void
_print_head(size_t yetprinted, size_t msgno, FILE *f, bool_t threaded)
{
   enum {attrlen = 14};
   char attrlist[attrlen +1], *cp;
   char const *fmt;
   NYD_ENTER;

   if ((cp = ok_vlook(attrlist)) != NULL) {
      if (strlen(cp) == attrlen) {
         memcpy(attrlist, cp, attrlen +1);
         goto jattrok;
      }
      n_err(_("*attrlist* is not of the correct length, using built-in\n"));
   }

   if (ok_blook(bsdcompat) || ok_blook(bsdflags)) {
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";
      memcpy(attrlist, bsdattr, sizeof bsdattr);
   } else if (ok_blook(SYSV3)) {
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";
      memcpy(attrlist, bsdattr, sizeof bsdattr);
      n_OBSOLETE(_("*SYSV3*: please use *bsdcompat* or *bsdflags*, "
         "or set *attrlist*"));
   } else {
      char const pattr[attrlen +1]   = "NUROSPMFAT+-$~";
      memcpy(attrlist, pattr, sizeof pattr);
   }

jattrok:
   if ((fmt = ok_vlook(headline)) == NULL) {
      fmt = ((ok_blook(bsdcompat) || ok_blook(bsdheadline))
            ? "%>%a%m %-20f  %16d %4l/%-5o %i%-S"
            : "%>%a%m %-18f %-16d %4l/%-5o %i%-s");
   }

   __hprf(yetprinted, fmt, msgno, f, threaded, attrlist);
   NYD_LEAVE;
}

static void
__hprf(size_t yetprinted, char const *fmt, size_t msgno, FILE *f,
   bool_t threaded, char const *attrlist)
{
   char buf[16], datebuf[n_FROM_DATEBUF], cbuf[8], *cp, *subjline;
   char const *datefmt, *date, *name, *fp n_COLOUR( COMMA *colo_tag );
   int i, n, s, wleft, subjlen;
   struct message *mp;
   time_t datet;
   n_COLOUR( struct n_colour_pen *cpen_new COMMA *cpen_cur COMMA *cpen_bas; )
   enum {
      _NONE       = 0,
      _ISDOT      = 1<<0,
      _ISADDR     = 1<<1,
      _ISTO       = 1<<2,
      _IFMT       = 1<<3,
      _LOOP_MASK  = (1<<4) - 1,
      _SFMT       = 1<<4,        /* It is 'S' */
      /* For the simple byte-based counts in wleft and n we sometimes need
       * adjustments to compensate for additional bytes of UTF-8 sequences */
      _PUTCB_UTF8_SHIFT = 5,
      _PUTCB_UTF8_MASK = 3<<5
   } flags = _NONE;
   NYD_ENTER;
   n_UNUSED(buf);

   if ((mp = message + msgno - 1) == dot)
      flags = _ISDOT;
   datet = mp->m_time;
   date = NULL;
   n_COLOUR( colo_tag = NULL; )

   datefmt = ok_vlook(datefield);
jredo:
   if (datefmt != NULL) {
      fp = hfield1("date", mp);/* TODO use m_date field! */
      if (fp == NULL) {
         datefmt = NULL;
         goto jredo;
      }
      datet = rfctime(fp);
      date = fakedate(datet);
      fp = ok_vlook(datefield_markout_older);
      i = (*datefmt != '\0');
      if (fp != NULL)
         i |= (*fp != '\0') ? 2 | 4 : 2; /* XXX no magics */

      /* May we strftime(3)? */
      if (i & (1 | 4))
         memcpy(&time_current.tc_local, localtime(&datet),
            sizeof time_current.tc_local);

      if ((i & 2) &&
            (UICMP(64,datet, >, time_current.tc_time + n_DATE_SECSDAY) ||
#define _6M ((n_DATE_DAYSYEAR / 2) * n_DATE_SECSDAY)
            UICMP(64,datet + _6M, <, time_current.tc_time))) {
#undef _6M
         if ((datefmt = (i & 4) ? fp : NULL) == NULL) {
            memset(datebuf, ' ', n_FROM_DATEBUF); /* xxx ur */
            memcpy(datebuf + 4, date + 4, 7);
            datebuf[4 + 7] = ' ';
            memcpy(datebuf + 4 + 7 + 1, date + 20, 4);
            datebuf[4 + 7 + 1 + 4] = '\0';
            date = datebuf;
         }
         n_COLOUR( colo_tag = n_COLOUR_TAG_SUM_OLDER; )
      } else if ((i & 1) == 0)
         datefmt = NULL;
   } else if (datet == (time_t)0 && !(mp->m_flag & MNOFROM)) {
      /* TODO eliminate this path, query the FROM_ date in setptr(),
       * TODO all other codepaths do so by themselves ALREADY ?????
       * TODO assert(mp->m_time != 0);, then
       * TODO ALSO changes behaviour of datefield_markout_older */
      _parse_from_(mp, datebuf);
      date = datebuf;
   } else
      date = fakedate(datet);

   flags |= _ISADDR;
   name = name1(mp, 0);
   if (name != NULL && ok_blook(showto) && n_is_myname(skin(name))) {
      if ((cp = hfield1("to", mp)) != NULL) {
         name = cp;
         flags |= _ISTO;
      }
   }
   if (name == NULL) {
      name = n_empty;
      flags &= ~_ISADDR;
   }
   if (flags & _ISADDR)
      name = ok_blook(showname) ? realname(name) : prstr(skin(name));

   subjline = NULL;

   /* Detect the width of the non-format characters in *headline*;
    * like that we can simply use putc() in the next loop, since we have
    * already calculated their column widths (TODO it's sick) */
   wleft = subjlen = n_scrnwidth;

   for (fp = fmt; *fp != '\0'; ++fp) {
      if (*fp == '%') {
         if (*++fp == '-')
            ++fp;
         else if (*fp == '+')
            ++fp;
         if (digitchar(*fp)) {
            n = 0;
            do
               n = 10*n + *fp - '0';
            while (++fp, digitchar(*fp));
            subjlen -= n;
         }
         if (*fp == 'i')
            flags |= _IFMT;

         if (*fp == '\0')
            break;
      } else {
#ifdef HAVE_WCWIDTH
         if (n_mb_cur_max > 1) {
            wchar_t  wc;
            if ((s = mbtowc(&wc, fp, n_mb_cur_max)) == -1)
               n = s = 1;
            else if ((n = wcwidth(wc)) == -1)
               n = 1;
         } else
#endif
            n = s = 1;
         subjlen -= n;
         wleft -= n;
         while (--s > 0)
            ++fp;
      }
   }

   /* Walk *headline*, producing output TODO not (really) MB safe */
#ifdef HAVE_COLOUR
   if(n_COLOUR_IS_ACTIVE()){
      if(flags & _ISDOT)
         colo_tag = n_COLOUR_TAG_SUM_DOT;
      cpen_bas = n_colour_pen_create(n_COLOUR_ID_SUM_HEADER, colo_tag);
      n_colour_pen_put(cpen_new = cpen_cur = cpen_bas);
   }else
      cpen_new = cpen_bas = cpen_cur = NULL;
#endif

   for (fp = fmt; *fp != '\0'; ++fp) {
      char c;

      if ((c = *fp & 0xFF) != '%') {
         n_COLOUR(
            if(n_COLOUR_IS_ACTIVE() && (cpen_new = cpen_bas) != cpen_cur)
               n_colour_pen_put(cpen_cur = cpen_new);
         );
         putc(c, f);
         continue;
      }

      flags &= _LOOP_MASK;
      n = 0;
      s = 1;
      if ((c = *++fp) == '-') {
         s = -1;
         ++fp;
      } else if (c == '+')
         ++fp;
      if (digitchar(*fp)) {
         do
            n = 10*n + *fp - '0';
         while (++fp, digitchar(*fp));
      }

      if ((c = *fp & 0xFF) == '\0')
         break;
      n *= s;

      cbuf[1] = '\0';
      switch (c) {
      case '%':
         goto jputcb;
      case '>':
      case '<':
         if (flags & _ISDOT) {
            n_COLOUR(
               if(n_COLOUR_IS_ACTIVE())
                  cpen_new = n_colour_pen_create(n_COLOUR_ID_SUM_DOTMARK,
                        colo_tag);
            );
            if (n_psonce & n_PSO_UNICODE) {
               if (c == '>')
                  /* 25B8;BLACK RIGHT-POINTING SMALL TRIANGLE: ▸ */
                  cbuf[1] = (char)0x96, cbuf[2] = (char)0xB8;
               else
                  /* 25C2;BLACK LEFT-POINTING SMALL TRIANGLE: ◂ */
                  cbuf[1] = (char)0x97, cbuf[2] = (char)0x82;
               c = (char)0xE2;
               cbuf[3] = '\0';
               flags |= 2 << _PUTCB_UTF8_SHIFT;
            }
         } else
            c = ' ';
         goto jputcb;
      case '$':
#ifdef HAVE_SPAM
         if (n == 0)
            n = 5;
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         snprintf(buf, sizeof buf, "%u.%02u",
            (mp->m_spamscore >> 8), (mp->m_spamscore & 0xFF));
         n = fprintf(f, "%*s", n, buf);
         wleft = (n >= 0) ? wleft - n : 0;
         break;
#else
         c = '?';
         goto jputcb;
#endif
      case 'a':
         c = _dispc(mp, attrlist);
jputcb:
#ifdef HAVE_COLOUR
         if(n_COLOUR_IS_ACTIVE()){
            if(cpen_new == cpen_cur)
               cpen_new = cpen_bas;
            if(cpen_new != cpen_cur)
               n_colour_pen_put(cpen_cur = cpen_new);
         }
#endif
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         cbuf[0] = c;
         n = fprintf(f, "%*s", n, cbuf);
         if (n >= 0) {
            wleft -= n;
            if ((n = (flags & _PUTCB_UTF8_MASK)) != 0) {
               n >>= _PUTCB_UTF8_SHIFT;
               wleft += n;
            }
         } else {
            wleft = 0; /* TODO I/O error.. ? break? */
         }
#ifdef HAVE_COLOUR
         if(n_COLOUR_IS_ACTIVE() && (cpen_new = cpen_bas) != cpen_cur)
            n_colour_pen_put(cpen_cur = cpen_new);
#endif
         break;
      case 'd':
         if (datefmt != NULL) {
            i = strftime(datebuf, sizeof datebuf, datefmt,
                  &time_current.tc_local);
            if (i != 0)
               date = datebuf;
            else
               n_err(_("Ignored date format, it excesses the target "
                  "buffer (%lu bytes)\n"), (ul_i)sizeof(datebuf));
            datefmt = NULL;
         }
         if (n == 0)
            n = 16;
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*.*s", n, n_ABS(n), date);
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'e':
         if (n == 0)
            n = 2;
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*u", n, (threaded == 1 ? mp->m_level : 0));
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'f':
         if (n == 0) {
            n = 18;
            if (s < 0)
               n = -n;
         }
         i = n_ABS(n);
         if (i > wleft) {
            i = wleft;
            n = (n < 0) ? -wleft : wleft;
         }
         if (flags & _ISTO) /* XXX tr()! */
            i -= 3;
         n = fprintf(f, "%s%s", ((flags & _ISTO) ? "To " : n_empty),
               colalign(name, i, n, &wleft));
         if (n < 0)
            wleft = 0;
         else if (flags & _ISTO)
            wleft -= 3;
         break;
      case 'i':
         if (threaded) {
#ifdef HAVE_COLOUR
            if(n_COLOUR_IS_ACTIVE()){
               cpen_new = n_colour_pen_create(n_COLOUR_ID_SUM_THREAD, colo_tag);
               if(cpen_new != cpen_cur)
                  n_colour_pen_put(cpen_cur = cpen_new);
            }
#endif
            n = __putindent(f, mp, n_MIN(wleft, (int)n_scrnwidth - 60));
            wleft = (n >= 0) ? wleft - n : 0;
#ifdef HAVE_COLOUR
            if(n_COLOUR_IS_ACTIVE() && (cpen_new = cpen_bas) != cpen_cur)
               n_colour_pen_put(cpen_cur = cpen_new);
#endif
         }
         break;
      case 'l':
         if (n == 0)
            n = 4;
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         if (mp->m_xlines) {
            n = fprintf(f, "%*ld", n, mp->m_xlines);
            wleft = (n >= 0) ? wleft - n : 0;
         } else {
            n = n_ABS(n);
            wleft -= n;
            while (n-- != 0)
               putc(' ', f);
         }
         break;
      case 'm':
         if (n == 0) {
            n = 3;
            if (threaded)
               for (i = msgCount; i > 999; i /= 10)
                  ++n;
         }
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*lu", n, (ul_i)msgno);
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'o':
         if (n == 0)
            n = -5;
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*lu", n, (ul_i)mp->m_xsize);
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'S':
         flags |= _SFMT;
         /*FALLTHRU*/
      case 's':
         if (n == 0)
            n = subjlen - 2;
         if (n > 0 && s < 0)
            n = -n;
         if (subjlen > wleft)
            subjlen = wleft;
         if (UICMP(32, n_ABS(n), >, subjlen))
            n = (n < 0) ? -subjlen : subjlen;
         if (flags & _SFMT)
            n -= (n < 0) ? -2 : 2;
         if (n == 0)
            break;
         if (subjline == NULL)
            subjline = __subject(mp, (threaded && (flags & _IFMT)), yetprinted);
         if (subjline == (char*)-1) {
            n = fprintf(f, "%*s", n, n_empty);
            wleft = (n >= 0) ? wleft - n : 0;
         } else {
            n = fprintf(f, ((flags & _SFMT) ? "\"%s\"" : "%s"),
                  colalign(subjline, n_ABS(n), n, &wleft));
            if (n < 0)
               wleft = 0;
         }
         break;
      case 'T': /* Message recipient flags */
         switch(is_mlist_mp(mp, MLIST_OTHER)){
         case MLIST_OTHER: c = ' '; break;
         case MLIST_KNOWN: c = 'l'; break;
         case MLIST_SUBSCRIBED: c = 'L'; break;
         }
         goto jputcb;
      case 't':
         if (n == 0) {
            n = 3;
            if (threaded)
               for (i = msgCount; i > 999; i /= 10)
                  ++n;
         }
         if (UICMP(32, n_ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*lu",
               n, (threaded ? (ul_i)mp->m_threadpos : (ul_i)msgno));
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'U':
#ifdef HAVE_IMAP
            if (n == 0)
               n = 9;
            if (UICMP(32, n_ABS(n), >, wleft))
               n = (n < 0) ? -wleft : wleft;
            n = fprintf(f, "%*lu", n, mp->m_uid);
            wleft = (n >= 0) ? wleft - n : 0;
            break;
#else
            c = '?';
            goto jputcb;
#endif
      default:
         if (n_poption & n_PO_D_V)
            n_err(_("Unkown *headline* format: %%%c\n"), c);
         c = '?';
         goto jputcb;
      }

      if (wleft <= 0)
         break;
   }

   n_COLOUR( n_colour_reset(); )
   putc('\n', f);

   if (subjline != NULL && subjline != (char*)-1)
      free(subjline);
   NYD_LEAVE;
}

static char *
__subject(struct message *mp, bool_t threaded, size_t yetprinted)
{
   struct str in, out;
   char *rv, *ms;
   NYD_ENTER;

   rv = (char*)-1;

   if ((ms = hfield1("subject", mp)) == NULL)
      goto jleave;

   in.l = strlen(in.s = ms);
   mime_fromhdr(&in, &out, TD_ICONV | TD_ISPR);
   rv = ms = out.s;

   if (!threaded || mp->m_level == 0)
      goto jleave;

   /* In a display thread - check whether this message uses the same
    * Subject: as it's parent or elder neighbour, suppress printing it if
    * this is the case.  To extend this a bit, ignore any leading Re: or
    * Fwd: plus follow-up WS.  Ignore invisible messages along the way */
   ms = n_UNCONST(subject_re_trim(n_UNCONST(ms)));

   for (; (mp = prev_in_thread(mp)) != NULL && yetprinted-- > 0;) {
      char *os;

      if (visible(mp) && (os = hfield1("subject", mp)) != NULL) {
         struct str oout;
         int x;

         in.l = strlen(in.s = os);
         mime_fromhdr(&in, &oout, TD_ICONV | TD_ISPR);
         x = asccasecmp(ms, subject_re_trim(oout.s));
         free(oout.s);

         if (!x) {
            free(out.s);
            rv = (char*)-1;
         }
         break;
      }
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static int
__putindent(FILE *fp, struct message *mp, int maxwidth)/* XXX no magic consts */
{
   struct message *mq;
   int *us, indlvl, indw, i, important = MNEW | MFLAGGED;
   char *cs;
   NYD_ENTER;

   if (mp->m_level == 0 || maxwidth == 0) {
      indw = 0;
      goto jleave;
   }

   cs = ac_alloc(mp->m_level);
   us = ac_alloc(mp->m_level * sizeof *us);

   i = mp->m_level - 1;
   if (mp->m_younger && UICMP(32, i + 1, ==, mp->m_younger->m_level)) {
      if (mp->m_parent && mp->m_parent->m_flag & important)
         us[i] = mp->m_flag & important ? 0x2523 : 0x2520;
      else
         us[i] = mp->m_flag & important ? 0x251D : 0x251C;
      cs[i] = '+';
   } else {
      if (mp->m_parent && mp->m_parent->m_flag & important)
         us[i] = mp->m_flag & important ? 0x2517 : 0x2516;
      else
         us[i] = mp->m_flag & important ? 0x2515 : 0x2514;
      cs[i] = '\\';
   }

   mq = mp->m_parent;
   for (i = mp->m_level - 2; i >= 0; --i) {
      if (mq) {
         if (UICMP(32, i, >, mq->m_level - 1)) {
            us[i] = cs[i] = ' ';
            continue;
         }
         if (mq->m_younger) {
            if (mq->m_parent && (mq->m_parent->m_flag & important))
               us[i] = 0x2503;
            else
               us[i] = 0x2502;
            cs[i] = '|';
         } else
            us[i] = cs[i] = ' ';
         mq = mq->m_parent;
      } else
         us[i] = cs[i] = ' ';
   }

   --maxwidth;
   for (indlvl = indw = 0; (ui8_t)indlvl < mp->m_level && indw < maxwidth;
         ++indlvl) {
      if (indw < maxwidth - 1)
         indw += (int)putuc(us[indlvl], cs[indlvl] & 0xFF, fp);
      else
         indw += (int)putuc(0x21B8, '^', fp);
   }
   indw += putuc(0x25B8, '>', fp);

   ac_free(us);
   ac_free(cs);
jleave:
   NYD_LEAVE;
   return indw;
}

static int
_dispc(struct message *mp, char const *a)
{
   int i = ' ';
   NYD_ENTER;

   if ((mp->m_flag & (MREAD | MNEW)) == MREAD)
      i = a[3];
   if ((mp->m_flag & (MREAD | MNEW)) == (MREAD | MNEW))
      i = a[2];
   if (mp->m_flag & MANSWERED)
      i = a[8];
   if (mp->m_flag & MDRAFTED)
      i = a[9];
   if ((mp->m_flag & (MREAD | MNEW)) == MNEW)
      i = a[0];
   if (!(mp->m_flag & (MREAD | MNEW)))
      i = a[1];
   if (mp->m_flag & MSPAM)
      i = a[12];
   if (mp->m_flag & MSPAMUNSURE)
      i = a[13];
   if (mp->m_flag & MSAVED)
      i = a[4];
   if (mp->m_flag & MPRESERVE)
      i = a[5];
   if (mp->m_flag & (MBOX | MBOXED))
      i = a[6];
   if (mp->m_flag & MFLAGGED)
      i = a[7];
   if (mb.mb_threaded == 1 && mp->m_collapsed > 0)
      i = a[11];
   if (mb.mb_threaded == 1 && mp->m_collapsed < 0)
      i = a[10];
   NYD_LEAVE;
   return i;
}

static int
a_cmd_scroll(char const *arg, bool_t onlynew){
   siz_t l;
   bool_t isabs;
   int msgspec, size, maxs;
   NYD2_ENTER;

   /* TODO scroll problem: we do not know whether + and $ have already reached
    * TODO the last screen in threaded mode */
   msgspec = onlynew ? -1 : 0;
   size = (int)/*TODO*/n_screensize();
   if((maxs = msgCount / size) > 0 && msgCount % size == 0)
      --maxs;

   if(arg == NULL)
      arg = n_empty;
   switch(*arg){
   case '\0':
      ++_screen;
      goto jfwd;
   case '^':
      if(arg[1] != '\0')
         goto jerr;
      if(_screen == 0)
         goto jerrbwd;
      _screen = 0;
      break;
   case '$':
      if(arg[1] != '\0')
         goto jerr;
      if(_screen == maxs)
         goto jerrfwd;
      _screen = maxs;
      break;
   case '+':
      if(arg[1] == '\0')
         ++_screen;
      else{
         isabs = FAL0;

         ++arg;
         if(0){
   case '1': case '2': case '3': case '4': case '5':
   case '6': case '7': case '8': case '9': case '0':
            isabs = TRU1;
         }
         if((n_idec_siz_cp(&l, arg, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED)
            goto jerr;
         if(l > maxs - (isabs ? 0 : _screen))
            goto jerrfwd;
         _screen = isabs ? (int)l : _screen + l;
      }
jfwd:
      if(_screen > maxs){
jerrfwd:
         _screen = maxs;
         fprintf(n_stdout, _("On last screenful of messages\n"));
      }
      break;

   case '-':
      if(arg[1] == '\0')
         --_screen;
      else{
         if((n_idec_siz_cp(&l, ++arg, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED)
            goto jerr;
         if(l > _screen)
            goto jerrbwd;
         _screen -= l;
      }
      if(_screen < 0){
jerrbwd:
         _screen = 0;
         fprintf(n_stdout, _("On first screenful of messages\n"));
      }
      if(msgspec == -1)
         msgspec = -2;
      break;
   default:
jerr:
      n_err(_("Unrecognized scrolling command: %s\n"), arg);
      size = 1;
      goto jleave;
   }

   size = _headers(msgspec);
jleave:
   NYD2_LEAVE;
   return size;
}

static int
_headers(int msgspec) /* TODO rework v15 */
{
   ui32_t volatile flag;
   int g, k, mesg, size;
   int volatile lastg = 1;
   struct message *mp, *mq, *lastmq = NULL;
   enum mflag fl = MNEW | MFLAGGED;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);
   flag = 0;

   size = (int)/*TODO*/n_screensize();
   if (_screen < 0)
      _screen = 0;
#if 0 /* FIXME original code path */
      k = _screen * size;
#else
   if (msgspec <= 0)
      k = _screen * size;
   else
      k = msgspec;
#endif
   if (k >= msgCount)
      k = msgCount - size;
   if (k < 0)
      k = 0;

   if (mb.mb_threaded == 0) {
      g = 0;
      mq = message;
      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if (visible(mp)) {
            if (g % size == 0)
               mq = mp;
            if (mp->m_flag & fl) {
               lastg = g;
               lastmq = mq;
            }
            if ((msgspec > 0 && PTRCMP(mp, ==, message + msgspec - 1)) ||
                  (msgspec == 0 && g == k) ||
                  (msgspec == -2 && g == k + size && lastmq) ||
                  (msgspec < 0 && g >= k && (mp->m_flag & fl) != 0))
               break;
            g++;
         }
      if (lastmq && (msgspec == -2 ||
            (msgspec == -1 && PTRCMP(mp, ==, message + msgCount)))) {
         g = lastg;
         mq = lastmq;
      }
      _screen = g / size;

      mp = mq;
      mesg = (int)PTR2SIZE(mp - message);
      if (PTRCMP(dot, !=, message + msgspec - 1)) { /* TODO really?? */
         for (mq = mp; PTRCMP(mq, <, message + msgCount); ++mq)
            if (visible(mq)) {
               setdot(mq);
               break;
            }
      }

#ifdef HAVE_IMAP
      if (mb.mb_type == MB_IMAP)
         imap_getheaders(mesg + 1, mesg + size);
#endif
      n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, n_stdout, FAL0); )
      srelax_hold();
      for (; PTRCMP(mp, <, message + msgCount); ++mp) {
         ++mesg;
         if (!visible(mp))
            continue;
         if (UICMP(32, flag++, >=, size))
            break;
         _print_head(0, mesg, n_stdout, 0);
         srelax();
      }
      srelax_rele();
      n_COLOUR( n_colour_env_gut(); )
   } else { /* threaded */
      g = 0;
      mq = threadroot;
      for (mp = threadroot; mp; mp = next_in_thread(mp))
         if (visible(mp) &&
               (mp->m_collapsed <= 0 ||
                PTRCMP(mp, ==, message + msgspec - 1))) {
            if (g % size == 0)
               mq = mp;
            if (mp->m_flag & fl) {
               lastg = g;
               lastmq = mq;
            }
            if ((msgspec > 0 && PTRCMP(mp, ==, message + msgspec - 1)) ||
                  (msgspec == 0 && g == k) ||
                  (msgspec == -2 && g == k + size && lastmq) ||
                  (msgspec < 0 && g >= k && (mp->m_flag & fl) != 0))
               break;
            g++;
         }
      if (lastmq && (msgspec == -2 ||
            (msgspec == -1 && PTRCMP(mp, ==, message + msgCount)))) {
         g = lastg;
         mq = lastmq;
      }
      _screen = g / size;
      mp = mq;
      if (PTRCMP(dot, !=, message + msgspec - 1)) { /* TODO really?? */
         for (mq = mp; mq; mq = next_in_thread(mq))
            if (visible(mq) && mq->m_collapsed <= 0) {
               setdot(mq);
               break;
            }
      }

      n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, n_stdout, FAL0); )
      srelax_hold();
      while (mp) {
         if (visible(mp) &&
               (mp->m_collapsed <= 0 ||
                PTRCMP(mp, ==, message + msgspec - 1))) {
            if (UICMP(32, flag++, >=, size))
               break;
            _print_head(flag - 1, PTR2SIZE(mp - message + 1), n_stdout,
               mb.mb_threaded);
            srelax();
         }
         mp = next_in_thread(mp);
      }
      srelax_rele();
      n_COLOUR( n_colour_env_gut(); )
   }

   if (!flag) {
      fprintf(n_stdout, _("No more mail.\n"));
      if (n_pstate & (n_PS_ROBOT | n_PS_HOOK_MASK))
         flag = !flag;
   }
   NYD_LEAVE;
   return !flag;
}

FL int
c_headers(void *v)
{
   int rv;
   NYD_ENTER;

   rv = print_header_group((int*)v);
   NYD_LEAVE;
   return rv;
}

FL int
print_header_group(int *vector)
{
   int rv;
   NYD_ENTER;

   assert(vector != NULL && vector != (void*)-1);
   rv = _headers(vector[0]);
   NYD_LEAVE;
   return rv;
}

FL int
c_scroll(void *v)
{
   int rv;
   NYD_ENTER;

   rv = a_cmd_scroll(*(char const**)v, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Scroll(void *v)
{
   int rv;
   NYD_ENTER;

   rv = a_cmd_scroll(*(char const**)v, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_dotmove(void *v)
{
   char const *args;
   int msgvec[2], rv;
   NYD_ENTER;

   if (*(args = v) == '\0' || args[1] != '\0') {
jerr:
      n_err(_("Synopsis: dotmove: up <-> or down <+> by one message\n"));
      rv = 1;
   } else switch (args[0]) {
   case '-':
   case '+':
      if (msgCount == 0) {
         fprintf(n_stdout, _("At EOF\n"));
         rv = 0;
      } else if (getmsglist(n_UNCONST(/*TODO*/ args), msgvec, 0) > 0) {
         setdot(message + msgvec[0] - 1);
         msgvec[1] = 0;
         rv = c_headers(msgvec);
      } else
         rv = 1;
      break;
   default:
      goto jerr;
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_from(void *v)
{
   int *msgvec = v, *ip, n;
   char *cp;
   FILE * volatile obuf;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);

   obuf = n_stdout;

   if (n_psonce & n_PSO_INTERACTIVE) {
      if ((cp = ok_vlook(crt)) != NULL) {
         uiz_t ib;

         for (n = 0, ip = msgvec; *ip != 0; ++ip)
            ++n;

         if(*cp == '\0')
            ib = n_screensize();
         else
            n_idec_uiz_cp(&ib, cp, 0, NULL);
         if (UICMP(z, n, >, ib) && (obuf = n_pager_open()) == NULL)
            obuf = n_stdout;
      }
   }

   /* Update dot before display so that the dotmark etc. are correct */
   for (ip = msgvec; *ip != 0; ++ip)
      ;
   if (--ip >= msgvec)
      setdot(message + *ip - 1);

   n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, obuf, obuf != n_stdout); )
   srelax_hold();
   for (n = 0, ip = msgvec; *ip != 0; ++ip) { /* TODO join into _print_head() */
      _print_head((size_t)n++, (size_t)*ip, obuf, mb.mb_threaded);
      srelax();
   }
   srelax_rele();
   n_COLOUR( n_colour_env_gut(); )

   if (obuf != n_stdout)
      n_pager_close(obuf);
   NYD_LEAVE;
   return 0;
}

FL void
print_headers(size_t bottom, size_t topx, bool_t only_marked)
{
   size_t printed;
   NYD_ENTER;

#ifdef HAVE_IMAP
   if (mb.mb_type == MB_IMAP)
      imap_getheaders(bottom, topx);
#endif
   time_current_update(&time_current, FAL0);

   n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, n_stdout, FAL0); )
   srelax_hold();
   for (printed = 0; bottom <= topx; ++bottom) {
      struct message *mp = message + bottom - 1;
      if (only_marked) {
         if (!(mp->m_flag & MMARK))
            continue;
      } else if (!visible(mp))
         continue;
      _print_head(printed++, bottom, n_stdout, FAL0);
      srelax();
   }
   srelax_rele();
   n_COLOUR( n_colour_env_gut(); )
   NYD_LEAVE;
}

/* s-it-mode */
