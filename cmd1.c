/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ User commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#define n_FILE cmd1

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

static int        _screen;
static sigjmp_buf _cmd1_pipejmp;

static void    _cmd1_onpipe(int signo);

/* Prepare and print "[Message: xy]:" intro */
static void    _show_msg_overview(FILE *obuf, struct message *mp, int msg_no);

/* ... And place the extracted date in `date' */
static void    _parse_from_(struct message *mp, char date[FROM_DATEBUF]);

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

/* Show the requested messages */
static int     _type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
                  bool_t dodecode, char *cmd, ui64_t *tstats);

/* Pipe the requested messages */
static int     _pipe1(char *str, int doign);

static void
_cmd1_onpipe(int signo)
{
   NYD_X; /* Signal handler */
   UNUSED(signo);
   siglongjmp(_cmd1_pipejmp, 1);
}

static void
_show_msg_overview(FILE *obuf, struct message *mp, int msg_no)
{
   char const *cpre = "", *csuf = "";
   NYD_ENTER;

#ifdef HAVE_COLOUR
   if (pstate & PS_COLOUR_ACTIVE) {
      struct n_colour_pen *cpen;

      if ((cpen = n_colour_pen_create(n_COLOUR_ID_VIEW_MSGINFO, NULL)) != NULL){
         struct str const *sp;

         if ((sp = n_colour_pen_to_str(cpen)) != NULL)
            cpre = sp->s;
         if ((sp = n_colour_reset_to_str()) != NULL)
            csuf = sp->s;
      }
   }
#endif
   fprintf(obuf, _("%s[-- Message %2d -- %lu lines, %lu bytes --]:%s\n"),
      cpre, msg_no, (ul_i)mp->m_lines, (ul_i)mp->m_size, csuf);
   NYD_LEAVE;
}

static void
_parse_from_(struct message *mp, char date[FROM_DATEBUF]) /* TODO line pool */
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
      n_err(_("*attrlist* is not of the correct length, using builtin\n"));
   }

   if (ok_blook(bsdcompat) || ok_blook(bsdflags)) {
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";
      memcpy(attrlist, bsdattr, sizeof bsdattr);
   } else if (env_blook("SYSV3", FAL0)) {
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";
      memcpy(attrlist, bsdattr, sizeof bsdattr);
      OBSOLETE(_("*SYSV3*: please use *bsdcompat* or *bsdflags*, "
         "or set *attrlist*"));
   } else {
      char const pattr[attrlen +1]   = "NUROSPMFAT+-$~";
      memcpy(attrlist, pattr, sizeof pattr);
   }

jattrok:
   if ((fmt = ok_vlook(headline)) == NULL) {
      fmt = ((ok_blook(bsdcompat) || ok_blook(bsdheadline))
            ? "%>%a%m %-20f  %16d %3l/%-5o %i%-S"
            : "%>%a%m %-18f %16d %4l/%-5o %i%-s");
   }

   __hprf(yetprinted, fmt, msgno, f, threaded, attrlist);
   NYD_LEAVE;
}

static void
__hprf(size_t yetprinted, char const *fmt, size_t msgno, FILE *f,
   bool_t threaded, char const *attrlist)
{
   char buf[16], datebuf[FROM_DATEBUF], cbuf[8], *cp, *subjline;
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
      _SFMT       = 1<<4
   } flags = _NONE;
   NYD_ENTER;
   UNUSED(buf);

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

      if ((i & 2) && (datet > time_current.tc_time + DATE_SECSDAY ||
#define _6M ((DATE_DAYSYEAR / 2) * DATE_SECSDAY)
            (datet + _6M < time_current.tc_time))) {
#undef _6M
         if ((datefmt = (i & 4) ? fp : NULL) == NULL) {
            memset(datebuf, ' ', FROM_DATEBUF); /* xxx ur */
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
   if (name != NULL && ok_blook(showto) && is_myname(skin(name))) {
      if ((cp = hfield1("to", mp)) != NULL) {
         name = cp;
         flags |= _ISTO;
      }
   }
   if (name == NULL) {
      name = "";
      flags &= ~_ISADDR;
   }
   if (flags & _ISADDR)
      name = ok_blook(showname) ? realname(name) : prstr(skin(name));

   subjline = NULL;

   /* Detect the width of the non-format characters in *headline*;
    * like that we can simply use putc() in the next loop, since we have
    * already calculated their column widths (TODO it's sick) */
   wleft = subjlen = scrnwidth;

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
         if (mb_cur_max > 1) {
            wchar_t  wc;
            if ((s = mbtowc(&wc, fp, mb_cur_max)) == -1)
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
   if (flags & _ISDOT)
      colo_tag = n_COLOUR_TAG_SUM_DOT;
   cpen_bas = n_colour_pen_create(n_COLOUR_ID_SUM_HEADER, colo_tag);
   n_colour_pen_put(cpen_new = cpen_cur = cpen_bas, f);
#endif

   for (fp = fmt; *fp != '\0'; ++fp) {
      char c;

      if ((c = *fp & 0xFF) != '%') {
#ifdef HAVE_COLOUR
         if ((cpen_new = cpen_bas) != cpen_cur)
            n_colour_pen_put(cpen_cur = cpen_new, f);
#endif
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
            n_COLOUR( cpen_new = n_colour_pen_create(n_COLOUR_ID_SUM_DOTMARK,
                  colo_tag); );
            if (options & OPT_UNICODE) {
               if (c == '>')
                  /* 25B8;BLACK RIGHT-POINTING SMALL TRIANGLE: ▸ */
                  cbuf[1] = (char)0x96, cbuf[2] = (char)0xB8;
               else
                  /* 25C2;BLACK LEFT-POINTING SMALL TRIANGLE: ◂ */
                  cbuf[1] = (char)0x97, cbuf[2] = (char)0x82;
               c = (char)0xE2;
               cbuf[3] = '\0';
            }
         } else
            c = ' ';
         goto jputcb;
      case '$':
#ifdef HAVE_SPAM
         if (n == 0)
            n = 5;
         if (UICMP(32, ABS(n), >, wleft))
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
         if (cpen_new == cpen_cur)
            cpen_new = cpen_bas;
         if (cpen_new != cpen_cur)
            n_colour_pen_put(cpen_cur = cpen_new, f);
#endif
         if (UICMP(32, ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         cbuf[0] = c;
         n = fprintf(f, "%*s", n, cbuf);
         wleft = (n >= 0) ? wleft - n : 0;
#ifdef HAVE_COLOUR
         if ((cpen_new = cpen_bas) != cpen_cur)
            n_colour_pen_put(cpen_cur = cpen_new, f);
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
         if (UICMP(32, ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*.*s", n, n, date);
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'e':
         if (n == 0)
            n = 2;
         if (UICMP(32, ABS(n), >, wleft))
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
         i = ABS(n);
         if (i > wleft) {
            i = wleft;
            n = (n < 0) ? -wleft : wleft;
         }
         if (flags & _ISTO) /* XXX tr()! */
            i -= 3;
         n = fprintf(f, "%s%s", ((flags & _ISTO) ? "To " : ""),
               colalign(name, i, n, &wleft));
         if (n < 0)
            wleft = 0;
         else if (flags & _ISTO)
            wleft -= 3;
         break;
      case 'i':
         if (threaded) {
#ifdef HAVE_COLOUR
            cpen_new = n_colour_pen_create(n_COLOUR_ID_SUM_THREAD, colo_tag);
            if (cpen_new != cpen_cur)
               n_colour_pen_put(cpen_cur = cpen_new, f);
#endif
            n = __putindent(f, mp, MIN(wleft, scrnwidth - 60));
            wleft = (n >= 0) ? wleft - n : 0;
#ifdef HAVE_COLOUR
            if ((cpen_new = cpen_bas) != cpen_cur)
               n_colour_pen_put(cpen_cur = cpen_new, f);
#endif
         }
         break;
      case 'l':
         if (n == 0)
            n = 4;
         if (UICMP(32, ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         if (mp->m_xlines) {
            n = fprintf(f, "%*ld", n, mp->m_xlines);
            wleft = (n >= 0) ? wleft - n : 0;
         } else {
            n = ABS(n);
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
         if (UICMP(32, ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*lu", n, (ul_i)msgno);
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'o':
         if (n == 0)
            n = -5;
         if (UICMP(32, ABS(n), >, wleft))
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
         if (UICMP(32, ABS(n), >, subjlen))
            n = (n < 0) ? -subjlen : subjlen;
         if (flags & _SFMT)
            n -= (n < 0) ? -2 : 2;
         if (n == 0)
            break;
         if (subjline == NULL)
            subjline = __subject(mp, (threaded && (flags & _IFMT)), yetprinted);
         if (subjline == (char*)-1) {
            n = fprintf(f, "%*s", n, "");
            wleft = (n >= 0) ? wleft - n : 0;
         } else {
            n = fprintf(f, ((flags & _SFMT) ? "\"%s\"" : "%s"),
                  colalign(subjline, ABS(n), n, &wleft));
            if (n < 0)
               wleft = 0;
         }
         break;
      case 'T': { /* Message recipient flags */
         /* We never can reuse "name" since it's the full name */
         struct name const *np = lextract(hfield1("to", mp), GTO | GSKIN);
         c = ' ';
         i = 0;
j_A_redo:
         for (; np != NULL; np = np->n_flink) {
            switch (is_mlist(np->n_name, FAL0)) {
            case MLIST_SUBSCRIBED:  c = 'S'; goto jputcb;
            case MLIST_KNOWN:       c = 'L'; goto jputcb;
            case MLIST_OTHER:
            default:                break;
            }
         }
         if (i != 0)
            goto jputcb;
         ++i;
         np = lextract(hfield1("cc", mp), GCC | GSKIN);
         goto j_A_redo;
      }
      case 't':
         if (n == 0) {
            n = 3;
            if (threaded)
               for (i = msgCount; i > 999; i /= 10)
                  ++n;
         }
         if (UICMP(32, ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*lu",
               n, (threaded ? (ul_i)mp->m_threadpos : (ul_i)msgno));
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      default:
         if (options & OPT_D_V)
            n_err(_("Unkown *headline* format: \"%%%c\"\n"), c);
         c = '?';
         goto jputcb;
      }

      if (wleft <= 0)
         break;
   }

#ifdef HAVE_COLOUR
   n_colour_reset(f);
#endif
   putc('\n', f);

   if (subjline != NULL && subjline != (char*)-1)
      free(subjline);
   NYD_LEAVE;
}

static char *
__subject(struct message *mp, bool_t threaded, size_t yetprinted)
{
   struct str in, out;
   char *rv = (char*)-1, *ms;
   NYD_ENTER;

   if ((ms = hfield1("subject", mp)) == NULL)
      goto jleave;

   in.l = strlen(in.s = ms);
   mime_fromhdr(&in, &out, TD_ICONV | TD_ISPR);
   rv = ms = out.s;

   if (!threaded || mp->m_level == 0)
      goto jleave;

   /* In a display thread - check wether this message uses the same
    * Subject: as it's parent or elder neighbour, suppress printing it if
    * this is the case.  To extend this a bit, ignore any leading Re: or
    * Fwd: plus follow-up WS.  Ignore invisible messages along the way */
   ms = subject_re_trim(ms);

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
   long l;
   char *eptr;
   bool_t isabs;
   int msgspec, size, maxs;
   NYD2_ENTER;

   msgspec = onlynew ? -1 : 0;
   size = screensize();
   maxs = msgCount / size;

   switch(*arg){
   case '\0':
      ++_screen;
      goto jfwd;
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
         l = strtol(arg, &eptr, 10);
         if(*eptr != '\0')
            goto jerr;
         if(l > maxs - (isabs ? 0 : _screen))
            goto jerrfwd;
         _screen = isabs ? (int)l : _screen + l;
      }
jfwd:
      if(_screen > maxs){
jerrfwd:
         _screen = maxs;
         printf(_("On last screenful of messages\n"));
      }
      break;

   case '-':
      if(arg[1] == '\0')
         --_screen;
      else{
         ++arg;
         l = strtol(arg, &eptr, 10);
         if(*eptr != '\0')
            goto jerr;
         if(l > _screen)
            goto jerrbwd;
         _screen -= l;
      }
      if(_screen < 0){
jerrbwd:
         _screen = 0;
         printf(_("On first screenful of messages\n"));
      }
      if(msgspec == -1)
         msgspec = -2;
      break;
   default:
jerr:
      n_err(_("Unrecognized scrolling command \"%s\"\n"), arg);
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
   bool_t volatile isrelax;
   sighandler_type volatile opipe;
   ui32_t volatile flag;
   int g, k, mesg, size, lastg = 1;
   struct message *mp, *mq, *lastmq = NULL;
   enum mflag fl = MNEW | MFLAGGED;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);

   isrelax = FAL0;
   opipe = NULL;
   flag = 0;
   if (sigsetjmp(_cmd1_pipejmp, 1))
      goto jleave;
   opipe = safe_signal(SIGPIPE, &_cmd1_onpipe);

#ifdef HAVE_COLOUR
   if (options & OPT_INTERACTIVE)
      n_colour_env_create(n_COLOUR_GROUP_SUM, FAL0);
#endif

   size = screensize();
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

      srelax_hold();
      isrelax = TRU1;
      for (; PTRCMP(mp, <, message + msgCount); ++mp) {
         ++mesg;
         if (!visible(mp))
            continue;
         if (UICMP(32, flag++, >=, size))
            break;
         _print_head(0, mesg, stdout, 0);
         srelax();
      }
      srelax_rele();
      isrelax = FAL0;
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

      srelax_hold();
      isrelax = TRU1;
      while (mp) {
         if (visible(mp) &&
               (mp->m_collapsed <= 0 ||
                PTRCMP(mp, ==, message + msgspec - 1))) {
            if (UICMP(32, flag++, >=, size))
               break;
            _print_head(flag - 1, PTR2SIZE(mp - message + 1), stdout,
               mb.mb_threaded);
            srelax();
         }
         mp = next_in_thread(mp);
      }
      srelax_rele();
      isrelax = FAL0;
   }

   if (!flag)
      printf(_("No more mail.\n"));
jleave:
   if (isrelax)
      srelax_rele();
   n_COLOUR( n_colour_env_gut(stdout); )
   if (opipe != NULL)
      safe_signal(SIGPIPE, opipe);
   NYD_LEAVE;
   return !flag;
}

static int
_type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
   bool_t dodecode, char *cmd, ui64_t *tstats)
{
   ui64_t mstats[1];
   int rv = 1, *ip;
   struct message *mp;
   char const *cp;
   FILE * volatile obuf;
   bool_t volatile isrelax = FAL0;
   sighandler_type opipe = NULL;
   NYD_ENTER;
   {/* C89.. */
   enum sendaction const action = ((dopipe && ok_blook(piperaw))
         ? SEND_MBOX : dodecode
         ? SEND_SHOW : doign
         ? SEND_TODISP : SEND_TODISP_ALL);
   bool_t const volatile formfeed = (dopipe && ok_blook(page));
   obuf = stdout;

   if (sigsetjmp(_cmd1_pipejmp, 1))
      goto jleave;
   opipe = safe_signal(SIGPIPE, &_cmd1_onpipe);

   if (dopipe) {
      if ((cp = ok_vlook(SHELL)) == NULL)
         cp = XSHELL;
      if ((obuf = Popen(cmd, "w", cp, NULL, 1)) == NULL) {
         n_perr(cmd, 0);
         obuf = stdout;
      }
   } else if ((options & OPT_TTYOUT) && (dopage ||
         ((options & OPT_INTERACTIVE) && (cp = ok_vlook(crt)) != NULL))) {
      size_t nlines = 0;

      if (!dopage) {
         for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
            mp = message + *ip - 1;
            if (!(mp->m_have & HAVE_BODY))
               if (get_body(mp) != OKAY)
                  goto jleave;
            nlines += mp->m_lines + 1; /* Message info XXX and PARTS... */
         }
      }

      /* >= not <: we return to the prompt */
      if (dopage || UICMP(z, nlines, >=,
            (*cp != '\0' ? atoi(cp) : realscreenheight))) {
         char const *env_add[2], *pager = get_pager(env_add + 0);
         env_add[1] = NULL;
         obuf = Popen(pager, "w", NULL, env_add, 1);
         if (obuf == NULL) {
            n_perr(pager, 0);
            obuf = stdout;
         }
      }
#ifdef HAVE_COLOUR
      if ((options & OPT_INTERACTIVE) &&
            (action == SEND_TODISP || action == SEND_TODISP_ALL ||
             action == SEND_SHOW))
         n_colour_env_create(n_COLOUR_GROUP_VIEW, obuf != stdout);
#endif
   }
#ifdef HAVE_COLOUR
   else if ((options & OPT_INTERACTIVE) &&
         (action == SEND_TODISP || action == SEND_TODISP_ALL))
      n_colour_env_create(n_COLOUR_GROUP_VIEW, FAL0);
#endif

   /*TODO unless we have our signal manager special care must be taken */
   srelax_hold();
   isrelax = TRU1;
   for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      uncollapse1(mp, 1);
      if (!dopipe && ip != msgvec)
         fprintf(obuf, "\n");
      if (action != SEND_MBOX)
         _show_msg_overview(obuf, mp, *ip);
      sendmp(mp, obuf, (doign ? ignore : NULL), NULL, action, mstats);
      srelax();
      if (formfeed) /* TODO a nicer way to separate piped messages! */
         putc('\f', obuf);
      if (tstats != NULL)
         tstats[0] += mstats[0];
   }
   srelax_rele();
   isrelax = FAL0;

   rv = 0;
jleave:
   if (obuf != stdout)
      /* Ignore SIGPIPE so it can't cause a duplicate close */
      safe_signal(SIGPIPE, SIG_IGN);
   if (isrelax)
      srelax_rele();
   n_COLOUR( n_colour_env_gut(obuf); )
   if (obuf != stdout)
      Pclose(obuf, TRU1);
   if (opipe != NULL)
      safe_signal(SIGPIPE, dflpipe);
   }
   NYD_LEAVE;
   return rv;
}

static int
_pipe1(char *str, int doign)
{
   ui64_t stats[1];
   char *cmd;
   int *msgvec, rv = 1;
   bool_t needs_list;
   NYD_ENTER;

   if ((cmd = laststring(str, &needs_list, TRU1)) == NULL) {
      cmd = ok_vlook(cmd);
      if (cmd == NULL || *cmd == '\0') {
         n_err(_("Variable *cmd* not set\n"));
         goto jleave;
      }
   }

   msgvec = salloc((msgCount + 2) * sizeof *msgvec);

   if (!needs_list) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (pstate & PS_HOOK_MASK) {
            rv = 0;
            goto jleave;
         }
         puts(_("No messages to pipe."));
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;
   if (*msgvec == 0) {
      if (pstate & PS_HOOK_MASK) {
         rv = 0;
         goto jleave;
      }
      printf("No applicable messages.\n");
      goto jleave;
   }

   printf(_("Pipe to: \"%s\"\n"), cmd);
   stats[0] = 0;
   if ((rv = _type1(msgvec, doign, FAL0, TRU1, FAL0, cmd, stats)) == 0)
      printf("\"%s\" %" PRIu64 " bytes\n", cmd, stats[0]);
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_cmdnotsupp(void *v) /* TODO -> lex.c */
{
   NYD_ENTER;
   UNUSED(v);
   n_err(_("The requested feature is not compiled in\n"));
   NYD_LEAVE;
   return 1;
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

   rv = a_cmd_scroll(v, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Scroll(void *v)
{
   int rv;
   NYD_ENTER;

   rv = a_cmd_scroll(v, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_from(void *v)
{
   int *msgvec = v, *ip, n;
   char *cp;
   FILE * volatile obuf;
   bool_t volatile isrelax;
   sighandler_type volatile opipe;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);

   obuf = stdout;
   isrelax = FAL0;
   opipe = NULL;
   if (sigsetjmp(_cmd1_pipejmp, 1))
      goto jleave;
   opipe = safe_signal(SIGPIPE, &_cmd1_onpipe);

   if (options & OPT_INTERACTIVE) {
      if ((cp = ok_vlook(crt)) != NULL) {
         for (n = 0, ip = msgvec; *ip != 0; ++ip)
            ++n;
         if (n > (*cp == '\0' ? screensize() : atoi(cp)) + 3) {
            char const *p = get_pager(NULL);
            if ((obuf = Popen(p, "w", NULL, NULL, 1)) == NULL) {
               n_perr(p, 0);
               obuf = stdout;
            }
         }
      }
      n_COLOUR( n_colour_env_create(n_COLOUR_GROUP_SUM, obuf != stdout); )
   }

   /* Update dot before display so that the dotmark etc. are correct */
   for (ip = msgvec; *ip != 0; ++ip)
      ;
   if (--ip >= msgvec)
      setdot(message + *ip - 1);

   srelax_hold();
   isrelax = TRU1;
   for (n = 0, ip = msgvec; *ip != 0; ++ip) { /* TODO join into _print_head() */
      _print_head((size_t)n++, (size_t)*ip, obuf, mb.mb_threaded);
      srelax();
   }
   srelax_rele();
   isrelax = FAL0;

jleave:
   if (obuf != stdout)
      /* Ignore SIGPIPE so it can't cause a duplicate close */
      safe_signal(SIGPIPE, SIG_IGN);
   if (isrelax)
      srelax_rele();
   n_COLOUR( n_colour_env_gut(obuf); )
   if (obuf != stdout)
      Pclose(obuf, TRU1);
   if (opipe != NULL)
      safe_signal(SIGPIPE, opipe);
   NYD_LEAVE;
   return 0;
}

FL void
print_headers(size_t bottom, size_t topx, bool_t only_marked)
{
   bool_t volatile isrelax;
   sighandler_type volatile opipe;
   size_t printed;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);

   isrelax = FAL0;
   opipe = NULL;
   if (sigsetjmp(_cmd1_pipejmp, 1))
      goto jleave;
   opipe = safe_signal(SIGPIPE, &_cmd1_onpipe);

#ifdef HAVE_COLOUR
   if (options & OPT_INTERACTIVE)
      n_colour_env_create(n_COLOUR_GROUP_SUM, FAL0);
#endif

   srelax_hold();
   isrelax = TRU1;
   for (printed = 0; bottom <= topx; ++bottom) {
      struct message *mp = message + bottom - 1;
      if (only_marked) {
         if (!(mp->m_flag & MMARK))
            continue;
      } else if (!visible(mp))
         continue;
      _print_head(printed++, bottom, stdout, FAL0);
      srelax();
   }
   srelax_rele();
   isrelax = FAL0;

jleave:
   if (isrelax)
      srelax_rele();
   n_COLOUR( n_colour_env_gut(stdout); )
   if (opipe != NULL)
      safe_signal(SIGPIPE, opipe);
   NYD_LEAVE;
}

FL int
c_pdot(void *v)
{
   NYD_ENTER;
   UNUSED(v);
   printf("%d\n", (int)PTR2SIZE(dot - message + 1));
   NYD_LEAVE;
   return 0;
}

FL int
c_more(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, TRU1, TRU1, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_More(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, FAL0, TRU1, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_type(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, TRU1, FAL0, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_Type(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, FAL0, FAL0, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_show(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, FAL0, FAL0, FAL0, TRU1, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_pipe(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = _pipe1(str, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Pipe(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = _pipe1(str, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_top(void *v)
{
   int *msgvec = v, *ip, c, topl, lines, empty_last;
   struct message *mp;
   char *cp, *linebuf = NULL;
   size_t linesize = 0;
   FILE *ibuf;
   NYD_ENTER;

   topl = 5;
   cp = ok_vlook(toplines);
   if (cp != NULL) {
      topl = atoi(cp);
      if (topl < 0 || topl > 10000)
         topl = 5;
   }

   /* XXX Colours of `top' only for message and part info lines */
#ifdef HAVE_COLOUR
   if (options & OPT_INTERACTIVE)
      n_colour_env_create(n_COLOUR_GROUP_VIEW, FAL0);
#endif
   empty_last = 1;
   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      pstate |= PS_DID_PRINT_DOT;
      if (!empty_last)
         printf("\n");
      _show_msg_overview(stdout, mp, *ip);
      if (mp->m_flag & MNOFROM)
         /* XXX c_top(): coloured output? */
         printf("From %s %s\n", fakefrom(mp), fakedate(mp->m_time));
      if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL) {  /* XXX could use TOP */
         v = NULL;
         break;
      }
      c = mp->m_lines;
      for (lines = 0; lines < c && UICMP(32, lines, <=, topl); ++lines) {
         if (readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
            break;
         puts(linebuf);

         for (cp = linebuf; *cp != '\0' && blankchar(*cp); ++cp)
            ;
         empty_last = (*cp == '\0');
      }
   }

   n_COLOUR( n_colour_env_gut(stdout); )
   if (linebuf != NULL)
      free(linebuf);
   NYD_LEAVE;
   return (v != NULL);
}

FL int
c_stouch(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag |= MTOUCH;
      dot->m_flag &= ~MPRESERVE;
      pstate |= PS_DID_PRINT_DOT;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_mboxit(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   if (pstate & PS_EDIT) {
      n_err(_("`mbox' can only be used in a system mailbox\n")); /* TODO */
      goto jleave;
   }

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag |= MTOUCH | MBOX;
      dot->m_flag &= ~MPRESERVE;
      pstate |= PS_DID_PRINT_DOT;
   }
jleave:
   NYD_LEAVE;
   return 0;
}

FL int
c_folders(void *v)
{
   char dirname[PATH_MAX], *name, **argv = v;
   char const *cmd;
   int rv = 1;
   NYD_ENTER;

   if (*argv) {
      name = expand(*argv);
      if (name == NULL)
         goto jleave;
   } else if (!getfold(dirname, sizeof dirname)) {
      n_err(_("No value set for \"folder\"\n"));
      goto jleave;
   } else
      name = dirname;

   if ((cmd = ok_vlook(LISTER)) == NULL)
      cmd = XLISTER;
   run_command(cmd, 0, COMMAND_FD_PASS, COMMAND_FD_PASS, name, NULL, NULL,
      NULL);
jleave:
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
