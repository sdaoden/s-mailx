/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header display, search, etc., related user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
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

/* Print out the header of a specific message.
 * time_current must be up-to-date when this is called.
 * a_cmd__hprf: handle *headline*
 * a_cmd__subject: -1 if Subject: yet seen, otherwise n_alloc()d Subject:
 * a_cmd__putindent: print out the indenting in threaded display
 * a_cmd__putuc: print out a Unicode character or a substitute for it, return
 *    0 on error or wcwidth() (or 1) on success */
static void a_cmd_print_head(size_t yetprinted, size_t msgno, FILE *f,
                  bool_t threaded, bool_t subject_thread_compress);

static void a_cmd__hprf(size_t yetprinted, char const *fmt, size_t msgno,
               FILE *f, bool_t threaded, bool_t subject_thread_compress,
               char const *attrlist);
static char *a_cmd__subject(struct message *mp, bool_t threaded,
               bool_t subject_thread_compress, size_t yetprinted);
static int a_cmd__putindent(FILE *fp, struct message *mp, int maxwidth);
static size_t a_cmd__putuc(int u, int c, FILE *fp);
static int a_cmd__dispc(struct message *mp, char const *a);

/* Shared `z' implementation */
static int a_cmd_scroll(char const *arg, bool_t onlynew);

/* Shared `headers' implementation */
static int     _headers(int msgspec);

static void
a_cmd_print_head(size_t yetprinted, size_t msgno, FILE *f, bool_t threaded,
      bool_t subject_thread_compress){
   enum {attrlen = 14};
   char attrlist[attrlen +1], *cp;
   char const *fmt;
   NYD2_ENTER;

   if((cp = ok_vlook(attrlist)) != NULL){
      if(strlen(cp) == attrlen){
         memcpy(attrlist, cp, attrlen +1);
         goto jattrok;
      }
      n_err(_("*attrlist* is not of the correct length, using built-in\n"));
   }

   if(ok_blook(bsdcompat) || ok_blook(bsdflags)){
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";

      memcpy(attrlist, bsdattr, sizeof bsdattr);
   }else if(ok_blook(SYSV3)){
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";

      memcpy(attrlist, bsdattr, sizeof bsdattr);
      n_OBSOLETE(_("*SYSV3*: please use *bsdcompat* or *bsdflags*, "
         "or set *attrlist*"));
   }else{
      char const pattr[attrlen +1]   = "NUROSPMFAT+-$~";

      memcpy(attrlist, pattr, sizeof pattr);
   }

jattrok:
   if((fmt = ok_vlook(headline)) == NULL){
      fmt = ((ok_blook(bsdcompat) || ok_blook(bsdheadline))
            ? "%>%a%m %-20f  %16d %4l/%-5o %i%-S"
            : "%>%a%m %-18f %-16d %4l/%-5o %i%-s");
   }

   a_cmd__hprf(yetprinted, fmt, msgno, f, threaded, subject_thread_compress,
      attrlist);
   NYD2_LEAVE;
}

static void
a_cmd__hprf(size_t yetprinted, char const *fmt, size_t msgno, FILE *f,
   bool_t threaded, bool_t subject_thread_compress, char const *attrlist)
{
   char buf[16], cbuf[8], *cp, *subjline;
   char const *date, *name, *fp, *color_tag;
   int i, n, s, wleft, subjlen;
   struct message *mp;
   n_COLOUR( struct n_colour_pen *cpen_new COMMA *cpen_cur COMMA *cpen_bas; )
   enum {
      _NONE       = 0,
      _ISDOT      = 1<<0,
      _ISTO       = 1<<1,
      _IFMT       = 1<<2,
      _LOOP_MASK  = (1<<4) - 1,
      _SFMT       = 1<<4,        /* It is 'S' */
      /* For the simple byte-based counts in wleft and n we sometimes need
       * adjustments to compensate for additional bytes of UTF-8 sequences */
      _PUTCB_UTF8_SHIFT = 5,
      _PUTCB_UTF8_MASK = 3<<5
   } flags = _NONE;
   NYD2_ENTER;
   n_UNUSED(buf);

   if ((mp = message + msgno - 1) == dot)
      flags = _ISDOT;

   color_tag = NULL;
   date = n_header_textual_date_info(mp, &color_tag);
   /* C99 */{
      bool_t isto;

      n_header_textual_sender_info(mp, &cp, NULL, NULL, NULL, &isto);
      name = cp;
      if(isto)
         flags |= _ISTO;
   }

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
         color_tag = n_COLOUR_TAG_SUM_DOT;
      cpen_bas = n_colour_pen_create(n_COLOUR_ID_SUM_HEADER, color_tag);
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
                        color_tag);
            );
            if((n_psonce & n_PSO_UNICODE) && !ok_blook(headline_plain)){
               if (c == '>')
                  /* 25B8;BLACK RIGHT-POINTING SMALL TRIANGLE */
                  cbuf[1] = (char)0x96, cbuf[2] = (char)0xB8;
               else
                  /* 25C2;BLACK LEFT-POINTING SMALL TRIANGLE */
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
         c = a_cmd__dispc(mp, attrlist);
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
               cpen_new = n_colour_pen_create(n_COLOUR_ID_SUM_THREAD,
                     color_tag);
               if(cpen_new != cpen_cur)
                  n_colour_pen_put(cpen_cur = cpen_new);
            }
#endif
            n = a_cmd__putindent(f, mp, n_MIN(wleft, (int)n_scrnwidth - 60));
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
            subjline = a_cmd__subject(mp, (threaded && (flags & _IFMT)),
                  subject_thread_compress, yetprinted);
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
            n = fprintf(f, "%*" PRIu64 , n, mp->m_uid);
            wleft = (n >= 0) ? wleft - n : 0;
            break;
#else
            c = '0';
            goto jputcb;
#endif
      default:
         if (n_poption & n_PO_D_V)
            n_err(_("Unknown *headline* format: %%%c\n"), c);
         c = '?';
         goto jputcb;
      }

      if (wleft <= 0)
         break;
   }

   n_COLOUR( n_colour_reset(); )
   putc('\n', f);

   if (subjline != NULL && subjline != (char*)-1)
      n_free(subjline);
   NYD2_LEAVE;
}

static char *
a_cmd__subject(struct message *mp, bool_t threaded,
   bool_t subject_thread_compress, size_t yetprinted)
{
   struct str in, out;
   char *rv, *ms;
   NYD2_ENTER;

   rv = (char*)-1;

   if ((ms = hfield1("subject", mp)) == NULL)
      goto jleave;

   in.l = strlen(in.s = ms);
   mime_fromhdr(&in, &out, TD_ICONV | TD_ISPR);
   rv = ms = out.s;

   if (!threaded || !subject_thread_compress || mp->m_level == 0)
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
         n_free(oout.s);

         if (!x) {
            n_free(out.s);
            rv = (char*)-1;
         }
         break;
      }
   }
jleave:
   NYD2_LEAVE;
   return rv;
}

static int
a_cmd__putindent(FILE *fp, struct message *mp, int maxwidth)/* XXX magics */
{
   struct message *mq;
   int *us, indlvl, indw, i, important = MNEW | MFLAGGED;
   char *cs;
   NYD2_ENTER;

   if (mp->m_level == 0 || maxwidth == 0) {
      indw = 0;
      goto jleave;
   }

   cs = n_lofi_alloc(mp->m_level);
   us = n_lofi_alloc(mp->m_level * sizeof *us);

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
         indw += (int)a_cmd__putuc(us[indlvl], cs[indlvl] & 0xFF, fp);
      else
         indw += (int)a_cmd__putuc(0x21B8, '^', fp);
   }
   indw += a_cmd__putuc(0x25B8, '>', fp);

   n_lofi_free(us);
   n_lofi_free(cs);
jleave:
   NYD2_LEAVE;
   return indw;
}

static size_t
a_cmd__putuc(int u, int c, FILE *fp){
   size_t rv;
   NYD2_ENTER;
   n_UNUSED(u);

#ifdef HAVE_NATCH_CHAR
   if((n_psonce & n_PSO_UNICODE) && (u & ~(wchar_t)0177) &&
         !ok_blook(headline_plain)){
      char mbb[MB_LEN_MAX];
      int i, n;

      if((n = wctomb(mbb, u)) > 0){
         rv = wcwidth(u);
         for(i = 0; i < n; ++i)
            if(putc(mbb[i] & 0377, fp) == EOF){
               rv = 0;
               break;
            }
      }else if(n == 0)
         rv = (putc('\0', fp) != EOF);
      else
         rv = 0;
   }else
#endif
      rv = (putc(c, fp) != EOF);
   NYD2_LEAVE;
   return rv;
}

static int
a_cmd__dispc(struct message *mp, char const *a)
{
   int i = ' ';
   NYD2_ENTER;

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
   if (mb.mb_threaded == 1) { /* TODO bad, and m_collapsed is weird */
      /* TODO So this does not work because of weird thread handling and
       * TODO intermixing view and controller except when run via -L from
       * TODO command line; in general these flags should go and we need
       * TODO specific *headline* formats which always work and indicate
       * TODO whether a message is in a thread, the head of a subthread etc. */
      if (mp->m_collapsed > 0)
         i = a[11];
      else if (mp->m_collapsed < 0)
         i = a[10];
   }
   NYD2_LEAVE;
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
   bool_t needdot, showlast;
   int g, k, mesg, size;
   struct message *lastmq, *mp, *mq;
   int volatile lastg;
   ui32_t volatile flag;
   enum mflag fl;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);

   fl = MNEW | MFLAGGED;
   flag = 0;
   lastg = 1;
   lastmq = NULL;

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

   needdot = (msgspec <= 0) ? TRU1 : (dot != &message[msgspec - 1]);
   showlast = ok_blook(showlast);

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
#ifdef HAVE_IMAP
      if (mb.mb_type == MB_IMAP)
         imap_getheaders(mesg + 1, mesg + size);
#endif
      n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, n_stdout, FAL0); )
      srelax_hold();
      for(lastmq = NULL, mq = &message[msgCount]; mp < mq; lastmq = mp, ++mp){
         ++mesg;
         if (!visible(mp))
            continue;
         if (UICMP(32, flag, >=, size))
            break;
         if(needdot){
            if(showlast){
               if(UICMP(32, flag, ==, size - 1) || &mp[1] == mq)
                  goto jdot_unsort;
            }else if(flag == 0){
jdot_unsort:
               needdot = FAL0;
               setdot(mp);
            }
         }
         ++flag;
         a_cmd_print_head(0, mesg, n_stdout, FAL0, FAL0);
         srelax();
      }
      if(needdot && ok_blook(showlast)) /* xxx will not show */
         setdot(lastmq);
      srelax_rele();
      n_COLOUR( n_colour_env_gut(); )
   } else { /* threaded */
      g = 0;
      mq = threadroot;
      for (mp = threadroot; mp; mp = next_in_thread(mp)){
         /* TODO thread handling needs rewrite, m_collapsed must go */
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
      }
      if (lastmq && (msgspec == -2 ||
            (msgspec == -1 && PTRCMP(mp, ==, message + msgCount)))) {
         g = lastg;
         mq = lastmq;
      }
      _screen = g / size;
      mp = mq;

      n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, n_stdout, FAL0); )
      srelax_hold();
      for(lastmq = NULL; mp != NULL; lastmq = mp, mp = mq){
         mq = next_in_thread(mp);
         if (visible(mp) &&
               (mp->m_collapsed <= 0 ||
                PTRCMP(mp, ==, message + msgspec - 1))) {
            if (UICMP(32, flag, >=, size))
               break;
            if(needdot){
               if(showlast){
                  if(UICMP(32, flag, ==, size - 1) || mq == NULL)
                     goto jdot_sort;
               }else if(flag == 0){
jdot_sort:
                  needdot = FAL0;
                  setdot(mp);
               }
            }
            a_cmd_print_head(flag, PTR2SIZE(mp - message + 1), n_stdout,
               mb.mb_threaded, TRU1);
            ++flag;
            srelax();
         }
      }
      if(needdot && ok_blook(showlast)) /* xxx will not show */
         setdot(lastmq);
      srelax_rele();
      n_COLOUR( n_colour_env_gut(); )
   }

   if (flag == 0) {
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
      } else if (n_getmsglist(n_UNCONST(/*TODO*/args), msgvec, 0, NULL) > 0) {
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
c_from(void *vp)
{
   int *msgvec, *ip, n;
   char *cp;
   FILE * volatile obuf;
   NYD_ENTER;

   if(*(msgvec = vp) == 0)
      goto jleave;

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
   for (ip = msgvec; ip[1] != 0; ++ip)
      ;
   setdot(&message[(ok_blook(showlast) ? *ip : *msgvec) - 1]);

   n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, obuf, obuf != n_stdout); )
   srelax_hold();
   for (n = 0, ip = msgvec; *ip != 0; ++ip) { /* TODO join into _print_head() */
      a_cmd_print_head((size_t)n++, (size_t)*ip, obuf, mb.mb_threaded, FAL0);
      srelax();
   }
   srelax_rele();
   n_COLOUR( n_colour_env_gut(); )

   if (obuf != n_stdout)
      n_pager_close(obuf);
jleave:
   NYD_LEAVE;
   return 0;
}

FL void
print_headers(int const *msgvec, bool_t only_marked,
   bool_t subject_thread_compress)
{
   size_t printed;
   NYD_ENTER;

   time_current_update(&time_current, FAL0);

   n_COLOUR( n_colour_env_create(n_COLOUR_CTX_SUM, n_stdout, FAL0); )
   srelax_hold();
   for(printed = 0; *msgvec != 0; ++msgvec) {
      struct message *mp = message + *msgvec - 1;
      if (only_marked) {
         if (!(mp->m_flag & MMARK))
            continue;
      } else if (!visible(mp))
         continue;
      a_cmd_print_head(printed++, *msgvec, n_stdout, mb.mb_threaded,
         subject_thread_compress);
      srelax();
   }
   srelax_rele();
   n_COLOUR( n_colour_env_gut(); )
   NYD_LEAVE;
}

/* s-it-mode */
