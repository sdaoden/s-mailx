/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ User commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

static int        _screen;
static sigjmp_buf _cmd1_pipestop;
static sigjmp_buf _cmd1_pipejmp;

static void    _cmd1_onpipe(int signo);
static void    _cmd1_brokpipe(int signo);

/* Prepare and print "[Message: xy]:" intro */
static void    _show_msg_overview(FILE *obuf, struct message *mp, int msg_no);

/* ... And place the extracted date in `date' */
static void    _parse_from_(struct message *mp, char date[FROM_DATEBUF]);

/* Print out the header of a specific message
 * __hprf: handle *headline*
 * __subject: Subject:, but return NULL if threaded and Subject: yet seen
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
static int     _scroll1(char *arg, int onlynew);

/* Shared `headers' implementation */
static int     _headers(int msgspec);

/* Show the requested messages */
static int     _type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
                  bool_t dodecode, char *cmd, off_t *tstats);

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
_cmd1_brokpipe(int signo)
{
   NYD_X; /* Signal handler */
   UNUSED(signo);
   siglongjmp(_cmd1_pipestop, 1);
}

static void
_show_msg_overview(FILE *obuf, struct message *mp, int msg_no)
{
   char const *cpre = "", *csuf = "";
   NYD_ENTER;

#ifdef HAVE_COLOUR
   if (colour_table != NULL) {
      struct str const *sp;

      if ((sp = colour_get(COLOURSPEC_MSGINFO)) != NULL)
         cpre = sp->s;
      csuf = colour_get(COLOURSPEC_RESET)->s;
   }
#endif
   fprintf(obuf, _("%s[-- Message %2d -- %lu lines, %lu bytes --]:%s\n"),
      cpre, msg_no, (ul_it)mp->m_lines, (ul_it)mp->m_size, csuf);
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
   enum {attrlen = 13};
   char attrlist[attrlen +1], *cp;
   char const *fmt;
   NYD_ENTER;

   if ((cp = ok_vlook(attrlist)) != NULL) {
      if (strlen(cp) == attrlen) {
         memcpy(attrlist, cp, attrlen +1);
         goto jattrok;
      }
      fprintf(stderr, _(
         "The value of *attrlist* is not of the correct length\n"));
   }
   if (ok_blook(bsdcompat) || ok_blook(bsdflags) ||
         getenv("SYSV3") != NULL) {
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$";
      memcpy(attrlist, bsdattr, sizeof bsdattr);
   } else {
      char const pattr[attrlen +1] = "NUROSPMFAT+-$";
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
   char datebuf[FROM_DATEBUF], *cp, *subjline;
   char const *datefmt, *date, *name, *fp;
   int i, n, s, wleft, subjlen;
   struct message *mp;
   time_t datet;
   enum {
      _NONE       = 0,
      _ISADDR     = 1<<0,
      _ISTO       = 1<<1,
      _IFMT       = 1<<2,
      _LOOP_MASK  = (1<<3) - 1,
      _SFMT       = 1<<3
   } flags = _NONE;
   NYD_ENTER;

   mp = message + msgno - 1;
   datet = mp->m_time;
   date = NULL;

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
   for (fp = fmt; *fp != '\0'; ++fp) {
      char c;
      if ((c = *fp & 0xFF) != '%')
         putc(c, f);
      else {
         flags &= _LOOP_MASK;
         n = 0;
         s = 1;
         if (*++fp == '-') {
            s = -1;
            ++fp;
         } else if (*fp == '+')
            ++fp;
         if (digitchar(*fp)) {
            do
               n = 10*n + *fp - '0';
            while (++fp, digitchar(*fp));
         }
         if (*fp == '\0')
            break;

         n *= s;
         switch ((c = *fp & 0xFF)) {
         case '%':
            goto jputc;
         case '>':
         case '<':
            if (dot != mp)
               c = ' ';
            goto jputc;
         case '$':
#ifdef HAVE_SPAM
            if (n == 0)
               n = 4;
            if (UICMP(32, ABS(n), >, wleft))
               n = (n < 0) ? -wleft : wleft;
            {  char buf[16];
               snprintf(buf, sizeof buf, "%u.%u",
                  (mp->m_spamscore >> 8), (mp->m_spamscore & 0xFF));
               n = fprintf(f, "%*s", n, buf);
               wleft = (n >= 0) ? wleft - n : 0;
            }
#else
            c = '?';
            goto jputc;
#endif
         case 'a':
            c = _dispc(mp, attrlist);
jputc:
            if (UICMP(32, ABS(n), >, wleft))
               n = (n < 0) ? -wleft : wleft;
            n = fprintf(f, "%*c", n, c);
            wleft = (n >= 0) ? wleft - n : 0;
            break;
         case 'd':
            if (datefmt != NULL) {
               i = strftime(datebuf, sizeof datebuf, datefmt,
                     &time_current.tc_local);
               if (i != 0)
                  date = datebuf;
               else
                  fprintf(stderr, _(
                     "Ignored date format, it excesses the target buffer "
                     "(%lu bytes)\n"), (ul_it)sizeof datebuf);
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
               n = __putindent(f, mp, MIN(wleft, scrnwidth - 60));
               wleft = (n >= 0) ? wleft - n : 0;
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
            n = fprintf(f, "%*lu", n, (ul_it)msgno);
            wleft = (n >= 0) ? wleft - n : 0;
            break;
         case 'o':
            if (n == 0)
               n = -5;
            if (UICMP(32, ABS(n), >, wleft))
               n = (n < 0) ? -wleft : wleft;
            n = fprintf(f, "%*lu", n, (long)mp->m_xsize);
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
               subjline = __subject(mp, (threaded && (flags & _IFMT)),
                     yetprinted);
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
         case 't':
            if (n == 0) {
               n = 3;
               if (threaded)
                  for (i = msgCount; i > 999; i /= 10)
                     ++n;
            }
            if (UICMP(32, ABS(n), >, wleft))
               n = (n < 0) ? -wleft : wleft;
            n = fprintf(f, "%*lu", n,
                  (threaded ? (ul_it)mp->m_threadpos : (ul_it)msgno));
            wleft = (n >= 0) ? wleft - n : 0;
            break;
         case 'U':
#ifdef HAVE_IMAP
            if (n == 0)
               n = 9;
            if (UICMP(32, ABS(n), >, wleft))
               n = (n < 0) ? -wleft : wleft;
            n = fprintf(f, "%*lu", n, mp->m_uid);
            wleft = (n >= 0) ? wleft - n : 0;
            break;
#else
            c = '?';
            goto jputc;
#endif
         }

         if (wleft <= 0)
            break;
      }
   }
   putc('\n', f);

   if (subjline != NULL && subjline != (char*)-1)
      free(subjline);
   NYD_LEAVE;
}

static char *
__subject(struct message *mp, bool_t threaded, size_t yetprinted)
{
   /* XXX NOTE: because of efficiency reasons we simply ignore any encoded
    * XXX parts and use ASCII case-insensitive comparison */
   struct str in, out;
   struct message *xmp;
   char *rv = (char*)-1, *ms, *mso, *os;
   NYD_ENTER;

   if ((ms = hfield1("subject", mp)) == NULL)
      goto jleave;

   if (!threaded || mp->m_level == 0)
      goto jconv;

   /* In a display thread - check wether this message uses the same
    * Subject: as it's parent or elder neighbour, suppress printing it if
    * this is the case.  To extend this a bit, ignore any leading Re: or
    * Fwd: plus follow-up WS.  Ignore invisible messages along the way */
   mso = subject_re_trim(ms);
   for (xmp = mp; (xmp = prev_in_thread(xmp)) != NULL && yetprinted-- > 0;)
      if (visible(xmp) && (os = hfield1("subject", xmp)) != NULL &&
            !asccasecmp(mso, subject_re_trim(os)))
         goto jleave;
jconv:
   in.s = ms;
   in.l = strlen(ms);
   mime_fromhdr(&in, &out, TD_ICONV | TD_ISPR);
   rv = out.s;
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
   indw += (/*putuc(0x261E, fp)*/putc('>', fp) != EOF);

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
_scroll1(char *arg, int onlynew)
{
   int msgspec, size;
   NYD_ENTER;

   msgspec = onlynew ? -1 : 0;
   size = screensize();

   if (arg[0] != '\0' && arg[1] != '\0')
      goto jerr;
   switch (*arg) {
   case '1': case '2': case '3': case '4': case '5':
   case '6': case '7': case '8': case '9': case '0':
      _screen = atoi(arg);
      goto jscroll_forward;
   case '\0':
      ++_screen;
      goto jscroll_forward;
   case '$':
      _screen = msgCount / size;
      goto jscroll_forward;
   case '+':
      if (arg[1] == '\0')
         ++_screen;
      else
         _screen += atoi(arg + 1);
jscroll_forward:
      if (_screen * size > msgCount) {
         _screen = msgCount / size;
         printf(_("On last screenful of messages\n"));
      }
      break;
   case '-':
      if (arg[1] == '\0')
         --_screen;
      else
         _screen -= atoi(arg + 1);
      if (_screen < 0) {
         _screen = 0;
         printf(_("On first screenful of messages\n"));
      }
      if (msgspec == -1)
         msgspec = -2;
      break;
   default:
jerr:
      fprintf(stderr, _("Unrecognized scrolling command \"%s\"\n"), arg);
      size = 1;
      goto jleave;
   }

   size = _headers(msgspec);
jleave:
   NYD_LEAVE;
   return size;
}

static int
_headers(int msgspec) /* FIXME rework v14.8; also: Neitzel mail, 2014-08-21 */
{
   ui32_t flag;
   int g, k, mesg, size, lastg = 1;
   struct message *mp, *mq, *lastmq = NULL;
   enum mflag fl = MNEW | MFLAGGED;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);

   flag = 0;
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
#ifdef HAVE_IMAP
      if (mb.mb_type == MB_IMAP)
         imap_getheaders(mesg + 1, mesg + size);
#endif
      srelax_hold();
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
   }

   if (!flag)
      printf(_("No more mail.\n"));
   NYD_LEAVE;
   return !flag;
}

static int
_type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
   bool_t dodecode, char *cmd, off_t *tstats)
{
   off_t mstats[2];
   int rv, *ip;
   struct message *mp;
   char const *cp;
   FILE * volatile obuf;
   bool_t volatile hadsig = FAL0, isrelax = FAL0;
   NYD_ENTER;
   {/* C89.. */
   enum sendaction const action = ((dopipe && ok_blook(piperaw))
         ? SEND_MBOX : dodecode
         ? SEND_SHOW : doign
         ? SEND_TODISP : SEND_TODISP_ALL);
   bool_t const volatile formfeed = (dopipe && ok_blook(page));
   obuf = stdout;

   if (sigsetjmp(_cmd1_pipestop, 1)) {
      hadsig = TRU1;
      goto jclose_pipe;
   }

   if (dopipe) {
      if ((cp = ok_vlook(SHELL)) == NULL)
         cp = XSHELL;
      if ((obuf = Popen(cmd, "w", cp, NULL, 1)) == NULL) {
         perror(cmd);
         obuf = stdout;
      } else
         safe_signal(SIGPIPE, &_cmd1_brokpipe);
   } else if ((options & OPT_TTYOUT) &&
         (dopage || (cp = ok_vlook(crt)) != NULL)) {
      char const *pager = NULL;
      size_t nlines = 0;

      if (!dopage) {
         for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
            mp = message + *ip - 1;
            if (!(mp->m_have & HAVE_BODY))
               if (get_body(mp) != OKAY) {
                  rv = 1;
                  goto jleave;
               }
            nlines += mp->m_lines + 1; /* Message info XXX and PARTS... */
         }
      }

      /* `>=' not `<': we return to the prompt */
      if (dopage || UICMP(z, nlines, >=,
            (*cp != '\0' ? atoi(cp) : realscreenheight))) {
         char const *env_add[2];
         pager = get_pager(env_add + 0);
         env_add[1] = NULL;
         obuf = Popen(pager, "w", NULL, env_add, 1);
         if (obuf == NULL) {
            perror(pager);
            obuf = stdout;
            pager = NULL;
         } else
            safe_signal(SIGPIPE, &_cmd1_brokpipe);
      }
#ifdef HAVE_COLOUR
      if (action != SEND_MBOX)
         colour_table_create(pager != NULL); /* (salloc()s!) */
#endif
   }
#ifdef HAVE_COLOUR
   else if ((options & OPT_TTYOUT) && action != SEND_MBOX)
      colour_table_create(FAL0); /* (salloc()s!) */
#endif

   /*TODO unless we have our signal manager special care must be taken */
   srelax_hold();
   isrelax = TRU1;
   for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      uncollapse1(mp, 1);
      if (!dopipe) {
         if (ip != msgvec)
            fprintf(obuf, "\n");
         if (action != SEND_MBOX)
            _show_msg_overview(obuf, mp, *ip);
      }
      sendmp(mp, obuf, (doign ? ignore : NULL), NULL, action, mstats);
      srelax();
      if (formfeed) /* TODO a nicer way to separate piped messages! */
         putc('\f', obuf);
      if (tstats) {
         tstats[0] += mstats[0];
         tstats[1] += mstats[1];
      }
   }
   srelax_rele();
   isrelax = FAL0;

jclose_pipe:
   if (obuf != stdout) {
      /* Ignore SIGPIPE so it can't cause a duplicate close */
      safe_signal(SIGPIPE, SIG_IGN);
      if (hadsig && isrelax)
         srelax_rele();
      colour_reset(obuf); /* XXX hacky; only here because we still jump */
      Pclose(obuf, TRU1);
      safe_signal(SIGPIPE, dflpipe);
   }
   rv = 0;
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static int
_pipe1(char *str, int doign)
{
   off_t stats[2];
   char *cmd;
   int *msgvec, rv = 1;
   bool_t needs_list;
   NYD_ENTER;

   if ((cmd = laststring(str, &needs_list, TRU1)) == NULL) {
      cmd = ok_vlook(cmd);
      if (cmd == NULL || *cmd == '\0') {
         fputs(_("variable cmd not set\n"), stderr);
         goto jleave;
      }
   }

   msgvec = salloc((msgCount + 2) * sizeof *msgvec);

   if (!needs_list) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (inhook) {
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
      if (inhook) {
         rv = 0;
         goto jleave;
      }
      printf("No applicable messages.\n");
      goto jleave;
   }

   printf(_("Pipe to: \"%s\"\n"), cmd);
   stats[0] = stats[1] = 0;
   if ((rv = _type1(msgvec, doign, FAL0, TRU1, FAL0, cmd, stats)) == 0) {
      printf("\"%s\" ", cmd);
      if (stats[0] >= 0)
         printf("%lu", (long)stats[0]);
      else
         printf(_("binary"));
      printf("/%lu\n", (long)stats[1]);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_cmdnotsupp(void *v) /* TODO -> lex.c */
{
   NYD_ENTER;
   UNUSED(v);
   fprintf(stderr, _("The requested feature is not compiled in\n"));
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

   rv = _scroll1(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Scroll(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _scroll1(v, 1);
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
   obuf = stdout;

   /* TODO unfixable memory leaks still */
   if (IS_TTY_SESSION() && (cp = ok_vlook(crt)) != NULL) {
      for (n = 0, ip = msgvec; *ip != 0; ++ip)
         n++;
      if (n > (*cp == '\0' ? screensize() : atoi(cp)) + 3) {
         char const *p;
         if (sigsetjmp(_cmd1_pipejmp, 1))
            goto jendpipe;
         p = get_pager(NULL);
         if ((obuf = Popen(p, "w", NULL, NULL, 1)) == NULL) {
            perror(p);
            obuf = stdout;
            cp = NULL;
         } else
            safe_signal(SIGPIPE, &_cmd1_onpipe);
      }
   }

   for (n = 0, ip = msgvec; *ip != 0; ++ip) /* TODO join into _print_head() */
      _print_head((size_t)n++, (size_t)*ip, obuf, mb.mb_threaded);
   if (--ip >= msgvec)
      setdot(message + *ip - 1);

jendpipe:
   if (obuf != stdout) {
      safe_signal(SIGPIPE, SIG_IGN);
      Pclose(obuf, TRU1);
      safe_signal(SIGPIPE, dflpipe);
   }
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

   for (printed = 0; bottom <= topx; ++bottom) {
      struct message *mp = message + bottom - 1;
      if (only_marked) {
         if (!(mp->m_flag & MMARK))
            continue;
      } else if (!visible(mp))
         continue;
      _print_head(printed++, bottom, stdout, FAL0);
   }
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

#ifdef HAVE_COLOUR
   if (options & OPT_TTYOUT)
      colour_table_create(FAL0); /* (salloc()s) */
#endif
   empty_last = 1;
   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      did_print_dot = TRU1;
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
      did_print_dot = TRU1;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_mboxit(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag |= MTOUCH | MBOX;
      dot->m_flag &= ~MPRESERVE;
      did_print_dot = TRU1;
   }
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
      fprintf(stderr, _("No value set for \"folder\"\n"));
      goto jleave;
   } else
      name = dirname;

   if (which_protocol(name) == PROTO_IMAP) {
#ifdef HAVE_IMAP
      imap_folders(name, *argv == NULL);
#else
      rv = c_cmdnotsupp(NULL);
#endif
   } else {
      if ((cmd = ok_vlook(LISTER)) == NULL)
         cmd = XLISTER;
      run_command(cmd, 0, -1, -1, name, NULL, NULL);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
