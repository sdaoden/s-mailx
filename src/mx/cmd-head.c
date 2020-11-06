/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Header display, search, etc., related user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE cmd_head
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/cmd.h"
#include "mx/cmd-mlist.h"
#include "mx/colour.h"
#include "mx/compat.h"
#include "mx/mime.h"
#include "mx/termios.h"
#include "mx/ui-str.h"

/* TODO fake */
#include "su/code-in.h"

static int        _screen;

/* Print out the header of a specific message.
 * time_current must be up-to-date when this is called.
 * a_chead__hprf: handle *headline*
 * a_chead__subject: -1 if Subject: yet seen, otherwise ALLOC()d Subject:
 * a_chead__putindent: print out the indenting in threaded display
 * a_chead__putuc: print out a Unicode character or a substitute for it, return
 *    0 on error or wcwidth() (or 1) on success */
static void a_chead_print_head(uz yetprinted, uz msgno, FILE *f,
               boole threaded, boole subject_thread_compress);

static void a_chead__hprf(uz yetprinted, char const *fmt, uz msgno,
               FILE *f, boole threaded, boole subject_thread_compress,
               char const *attrlist);
static char *a_chead__subject(struct message *mp, boole threaded,
               boole subject_thread_compress, uz yetprinted);
static int a_chead__putindent(FILE *fp, struct message *mp, int maxwidth);
static uz a_chead__putuc(int u, int c, FILE *fp);
static int a_chead__dispc(struct message *mp, char const *a);

/* Shared `z' implementation */
static int a_chead_scroll(char const *arg, boole onlynew);

/* Shared `headers' implementation */
static int     _headers(int msgspec);

static void
a_chead_print_head(uz yetprinted, uz msgno, FILE *f, boole threaded,
      boole subject_thread_compress){
   enum {attrlen = 14};
   char attrlist[attrlen +1], *cp;
   char const *fmt;
   NYD2_IN;

   if((cp = ok_vlook(attrlist)) != NULL){
      if(su_cs_len(cp) == attrlen){
         su_mem_copy(attrlist, cp, attrlen +1);
         goto jattrok;
      }
      n_err(_("*attrlist* is not of the correct length, using built-in\n"));
   }

   if(ok_blook(bsdcompat) || ok_blook(bsdflags)){
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";

      su_mem_copy(attrlist, bsdattr, sizeof bsdattr);
   }else if(ok_blook(SYSV3)){
      char const bsdattr[attrlen +1] = "NU  *HMFAT+-$~";

      su_mem_copy(attrlist, bsdattr, sizeof bsdattr);
      n_OBSOLETE(_("*SYSV3*: please use *bsdcompat* or *bsdflags*, "
         "or set *attrlist*"));
   }else{
      char const pattr[attrlen +1]   = "NUROSPMFAT+-$~";

      su_mem_copy(attrlist, pattr, sizeof pattr);
   }

jattrok:
   if((fmt = ok_vlook(headline)) == NULL){
      fmt = ((ok_blook(bsdcompat) || ok_blook(bsdheadline))
            ? "%>%a%m %-20f  %16d %4l/%-5o %i%-S"
            : "%>%a%m %-18f %-16d %4l/%-5o %i%-s");
   }

   a_chead__hprf(yetprinted, fmt, msgno, f, threaded, subject_thread_compress,
      attrlist);
   NYD2_OU;
}

static void
a_chead__hprf(uz yetprinted, char const *fmt, uz msgno, FILE *f,
   boole threaded, boole subject_thread_compress, char const *attrlist)
{
   char buf[16], cbuf[8], *cp, *subjline;
   char const *date, *name, *fp, *color_tag;
   int i, n, s, wleft, subjlen;
   struct message *mp;
   mx_COLOUR( struct mx_colour_pen *cpen_new su_COMMA
      *cpen_cur su_COMMA *cpen_bas; )
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
   NYD2_IN;
   UNUSED(buf);

   if ((mp = message + msgno - 1) == dot)
      flags = _ISDOT;

   color_tag = NULL;
   date = n_header_textual_date_info(mp, &color_tag);
   /* C99 */{
      boole isto;

      n_header_textual_sender_info(mp, NIL, &cp, NULL, NULL, NULL, &isto);
      name = cp;
      if(isto)
         flags |= _ISTO;
   }

   subjline = NULL;

   /* Detect the width of the non-format characters in *headline*;
    * like that we can simply use putc() in the next loop, since we have
    * already calculated their column widths (TODO it's sick) */
   wleft = subjlen = mx_termios_dimen.tiosd_width;

   for (fp = fmt; *fp != '\0'; ++fp) {
      if (*fp == '%') {
         if (*++fp == '-')
            ++fp;
         else if (*fp == '+')
            ++fp;
         if (su_cs_is_digit(*fp)) {
            n = 0;
            do
               n = 10*n + *fp - '0';
            while (++fp, su_cs_is_digit(*fp));
            subjlen -= n;
         }
         if (*fp == 'i')
            flags |= _IFMT;

         if (*fp == '\0')
            break;
      } else {
#ifdef mx_HAVE_WCWIDTH
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
#ifdef mx_HAVE_COLOUR
   if(mx_COLOUR_IS_ACTIVE()){
      if(flags & _ISDOT)
         color_tag = mx_COLOUR_TAG_SUM_DOT;
      cpen_bas = mx_colour_pen_create(mx_COLOUR_ID_SUM_HEADER, color_tag);
      mx_colour_pen_put(cpen_new = cpen_cur = cpen_bas);
   }else
      cpen_new = cpen_bas = cpen_cur = NULL;
#endif

   for (fp = fmt; *fp != '\0'; ++fp) {
      char c;

      if ((c = *fp & 0xFF) != '%') {
         mx_COLOUR(
            if(mx_COLOUR_IS_ACTIVE() && (cpen_new = cpen_bas) != cpen_cur)
               mx_colour_pen_put(cpen_cur = cpen_new);
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
      if (su_cs_is_digit(*fp)) {
         do
            n = 10*n + *fp - '0';
         while (++fp, su_cs_is_digit(*fp));
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
            mx_COLOUR(
               if(mx_COLOUR_IS_ACTIVE())
                  cpen_new = mx_colour_pen_create(mx_COLOUR_ID_SUM_DOTMARK,
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
#ifdef mx_HAVE_SPAM
         if (n == 0)
            n = 5;
         if (UCMP(32, ABS(n), >, wleft))
            wleft = 0;
         else{
            snprintf(buf, sizeof buf, "%u.%02u",
               (mp->m_spamscore >> 8), (mp->m_spamscore & 0xFF));
            n = fprintf(f, "%*s", n, buf);
            wleft = (n >= 0) ? wleft - n : 0;
         }
         break;
#else
         c = '?';
         goto jputcb;
#endif
      case 'a':
         c = a_chead__dispc(mp, attrlist);
jputcb:
#ifdef mx_HAVE_COLOUR
         if(mx_COLOUR_IS_ACTIVE()){
            if(cpen_new == cpen_cur)
               cpen_new = cpen_bas;
            if(cpen_new != cpen_cur)
               mx_colour_pen_put(cpen_cur = cpen_new);
         }
#endif
         if (UCMP(32, ABS(n), >, wleft))
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
#ifdef mx_HAVE_COLOUR
         if(mx_COLOUR_IS_ACTIVE() && (cpen_new = cpen_bas) != cpen_cur)
            mx_colour_pen_put(cpen_cur = cpen_new);
#endif
         break;
      case 'd':
         if (n == 0)
            n = 16;
         if (UCMP(32, ABS(n), >, wleft))
            n = (n < 0) ? -wleft : wleft;
         n = fprintf(f, "%*.*s", n, ABS(n), date);
         wleft = (n >= 0) ? wleft - n : 0;
         break;
      case 'e':
         if (n == 0)
            n = 2;
         if (UCMP(32, ABS(n), >, wleft))
            wleft = 0;
         else{
            n = fprintf(f, "%*u", n, (threaded == 1 ? mp->m_level : 0));
            wleft = (n >= 0) ? wleft - n : 0;
         }
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
         if (flags & _ISTO) {/* XXX tr()! */
            if(wleft <= 3){
               wleft = 0;
               break;
            }
            i -= 3;
         }
         n = fprintf(f, "%s%s", ((flags & _ISTO) ? "To " : n_empty),
               mx_colalign(name, i, n, &wleft));
         if (n < 0)
            wleft = 0;
         else if (flags & _ISTO)
            wleft -= 3;
         break;
      case 'i':
         if (threaded) {
#ifdef mx_HAVE_COLOUR
            if(mx_COLOUR_IS_ACTIVE()){
               cpen_new = mx_colour_pen_create(mx_COLOUR_ID_SUM_THREAD,
                     color_tag);
               if(cpen_new != cpen_cur)
                  mx_colour_pen_put(cpen_cur = cpen_new);
            }
#endif
            n = a_chead__putindent(f, mp,
                  MIN(wleft, S(int,mx_termios_dimen.tiosd_width) - 60));
            wleft = (n >= 0) ? wleft - n : 0;
#ifdef mx_HAVE_COLOUR
            if(mx_COLOUR_IS_ACTIVE() && (cpen_new = cpen_bas) != cpen_cur)
               mx_colour_pen_put(cpen_cur = cpen_new);
#endif
         }
         break;
      case 'L': /* ML status */
jmlist: /* v15compat */
         switch(mx_mlist_query_mp(mp, mx_MLIST_OTHER)){
         case mx_MLIST_OTHER: c = ' '; break;
         case mx_MLIST_KNOWN: c = 'l'; break;
         case mx_MLIST_SUBSCRIBED: c = 'L'; break;
         case mx_MLIST_POSSIBLY: c = 'P'; break;
         }
         goto jputcb;
      case 'l':
         if (n == 0)
            n = 4;
         if (UCMP(32, ABS(n), >, wleft))
            wleft = 0;
         else if (mp->m_xlines) {
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
         if (UCMP(32, ABS(n), >, wleft))
            wleft = 0;
         else{
            n = fprintf(f, "%*lu", n, (ul)msgno);
            wleft = (n >= 0) ? wleft - n : 0;
         }
         break;
      case 'o':
         if (n == 0)
            n = -5;
         if (UCMP(32, ABS(n), >, wleft))
            wleft = 0;
         else{
            n = fprintf(f, "%*lu", n, (ul)mp->m_xsize);
            wleft = (n >= 0) ? wleft - n : 0;
         }
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
         if (UCMP(32, ABS(n), >, subjlen))
            n = (n < 0) ? -subjlen : subjlen;
         if (flags & _SFMT)
            n -= (n < 0) ? -2 : 2;
         if (n == 0)
            break;
         if (subjline == NULL)
            subjline = a_chead__subject(mp, (threaded && (flags & _IFMT)),
                  subject_thread_compress, yetprinted);
         if (subjline == (char*)-1) {
            n = fprintf(f, "%*s", n, n_empty);
            wleft = (n >= 0) ? wleft - n : 0;
         } else {
            n = fprintf(f, ((flags & _SFMT) ? "\"%s\"" : "%s"),
                  mx_colalign(subjline, ABS(n), n, &wleft));
            if (n < 0)
               wleft = 0;
         }
         break;
      case 'T':
         n_OBSOLETE("*headline*: please use %L not %T for mailing-list "
            "status");
         goto jmlist;
      case 't':
         if (n == 0) {
            n = 3;
            if (threaded)
               for (i = msgCount; i > 999; i /= 10)
                  ++n;
         }
         if (UCMP(32, ABS(n), >, wleft))
            wleft = 0;
         else{
            n = fprintf(f, "%*lu",
                  n, (threaded ? (ul)mp->m_threadpos : (ul)msgno));
            wleft = (n >= 0) ? wleft - n : 0;
         }
         break;
      case 'U':
#ifdef mx_HAVE_IMAP
            if (n == 0)
               n = 9;
            if (UCMP(32, ABS(n), >, wleft))
               wleft = 0;
            else{
               n = fprintf(f, "%*" PRIu64 , n, mp->m_uid);
               wleft = (n >= 0) ? wleft - n : 0;
            }
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

   mx_COLOUR( mx_colour_reset(); )
   putc('\n', f);

   if(subjline != NIL && subjline != R(char*,-1))
      su_FREE(subjline);

   NYD2_OU;
}

static char *
a_chead__subject(struct message *mp, boole threaded,
      boole subject_thread_compress, uz yetprinted){
   struct str in, out;
   char *rv, *ms;
   NYD2_IN;

   rv = R(char*,-1);

   if((ms = hfield1("subject", mp)) == NIL)
      goto jleave;

   in.l = su_cs_len(in.s = ms);
   mx_mime_display_from_header(&in, &out,
      mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT);
   rv = ms = out.s;

   if(!threaded || !subject_thread_compress || mp->m_level == 0)
      goto jleave;

   /* In a display thread - check whether this message uses the same
    * Subject: as its parent or elder neighbour, suppress printing it if
    * this is the case.  To extend this a bit, ignore any leading Re: or
    * Fwd: plus follow-up WS.  Ignore invisible messages along the way */
   ms = mx_header_subject_edit(ms, mx_HEADER_SUBJECT_EDIT_TRIM_ALL);

   for(; (mp = prev_in_thread(mp)) != NIL && yetprinted-- > 0;){
      char *os;

      if(visible(mp) && (os = hfield1("subject", mp)) != NIL){
         if(!su_cs_cmp_case(ms, mx_header_subject_edit(os,
               mx_HEADER_SUBJECT_EDIT_DECODE_MIME |
               mx_HEADER_SUBJECT_EDIT_TRIM_ALL))){
            su_FREE(out.s);
            rv = R(char*,-1);
         }
         break;
      }
   }

jleave:
   NYD2_OU;
   return rv;
}

static int
a_chead__putindent(FILE *fp, struct message *mp, int maxwidth)/* XXX magics */
{
   struct message *mq;
   int *unis, indlvl, indw, i, important = MNEW | MFLAGGED;
   char *cs;
   NYD2_IN;

   if (mp->m_level == 0 || maxwidth == 0) {
      indw = 0;
      goto jleave;
   }

   cs = n_lofi_alloc(mp->m_level);
   unis = n_lofi_alloc(mp->m_level * sizeof *unis);

   i = mp->m_level - 1;
   if (mp->m_younger && UCMP(32, i + 1, ==, mp->m_younger->m_level)) {
      if (mp->m_parent && mp->m_parent->m_flag & important)
         unis[i] = mp->m_flag & important ? 0x2523 : 0x2520;
      else
         unis[i] = mp->m_flag & important ? 0x251D : 0x251C;
      cs[i] = '+';
   } else {
      if (mp->m_parent && mp->m_parent->m_flag & important)
         unis[i] = mp->m_flag & important ? 0x2517 : 0x2516;
      else
         unis[i] = mp->m_flag & important ? 0x2515 : 0x2514;
      cs[i] = '\\';
   }

   mq = mp->m_parent;
   for (i = mp->m_level - 2; i >= 0; --i) {
      if (mq) {
         if (UCMP(32, i, >, mq->m_level - 1)) {
            unis[i] = cs[i] = ' ';
            continue;
         }
         if (mq->m_younger) {
            if (mq->m_parent && (mq->m_parent->m_flag & important))
               unis[i] = 0x2503;
            else
               unis[i] = 0x2502;
            cs[i] = '|';
         } else
            unis[i] = cs[i] = ' ';
         mq = mq->m_parent;
      } else
         unis[i] = cs[i] = ' ';
   }

   --maxwidth;
   for (indlvl = indw = 0; (u8)indlvl < mp->m_level && indw < maxwidth;
         ++indlvl) {
      if (indw < maxwidth - 1)
         indw += (int)a_chead__putuc(unis[indlvl], cs[indlvl] & 0xFF, fp);
      else
         indw += (int)a_chead__putuc(0x21B8, '^', fp);
   }
   indw += a_chead__putuc(0x25B8, '>', fp);

   n_lofi_free(unis);
   n_lofi_free(cs);
jleave:
   NYD2_OU;
   return indw;
}

static uz
a_chead__putuc(int u, int c, FILE *fp){
   uz rv;
   NYD2_IN;
   UNUSED(u);

#ifdef mx_HAVE_NATCH_CHAR
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
   NYD2_OU;
   return rv;
}

static int
a_chead__dispc(struct message *mp, char const *a)
{
   int i = ' ';
   NYD2_IN;

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
   NYD2_OU;
   return i;
}

static int
a_chead_scroll(char const *arg, boole onlynew){
   sz l;
   boole isabs;
   int msgspec, size, maxs;
   NYD2_IN;

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
         if((su_idec_sz_cp(&l, arg, 0, NULL
                  ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
               ) != su_IDEC_STATE_CONSUMED)
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
         if((su_idec_sz_cp(&l, ++arg, 0, NULL
                  ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
               ) != su_IDEC_STATE_CONSUMED)
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
   NYD2_OU;
   return size;
}

static int
_headers(int msgspec) /* TODO rework v15 */
{
   boole needdot, showlast;
   int g, k, mesg, size;
   struct message *lastmq, *mp, *mq;
   int volatile lastg;
   u32 volatile flag;
   enum mflag fl;
   NYD_IN;

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
      for (mp = message; PCMP(mp, <, message + msgCount); ++mp)
         if (visible(mp)) {
            if (g % size == 0)
               mq = mp;
            if (mp->m_flag & fl) {
               lastg = g;
               lastmq = mq;
            }
            if ((msgspec > 0 && PCMP(mp, ==, message + msgspec - 1)) ||
                  (msgspec == 0 && g == k) ||
                  (msgspec == -2 && g == k + size && lastmq) ||
                  (msgspec < 0 && g >= k && (mp->m_flag & fl) != 0))
               break;
            g++;
         }
      if (lastmq && (msgspec == -2 ||
            (msgspec == -1 && PCMP(mp, ==, message + msgCount)))) {
         g = lastg;
         mq = lastmq;
      }
      _screen = g / size;
      mp = mq;

      mesg = (int)P2UZ(mp - message);
#ifdef mx_HAVE_IMAP
      if (mb.mb_type == MB_IMAP)
         imap_getheaders(mesg + 1, mesg + size);
#endif
      mx_COLOUR( mx_colour_env_create(mx_COLOUR_CTX_SUM, n_stdout, FAL0); )
      n_autorec_relax_create();
      for(lastmq = NULL, mq = &message[msgCount]; mp < mq; lastmq = mp, ++mp){
         ++mesg;
         if (!visible(mp))
            continue;
         if (UCMP(32, flag, >=, size))
            break;
         if(needdot){
            if(showlast){
               if(UCMP(32, flag, ==, size - 1) || &mp[1] == mq)
                  goto jdot_unsort;
            }else if(flag == 0){
jdot_unsort:
               needdot = FAL0;
               setdot(mp, FAL0);
            }
         }
         ++flag;
         a_chead_print_head(0, mesg, n_stdout, FAL0, FAL0);
         n_autorec_relax_unroll();
      }
      if(needdot && ok_blook(showlast)) /* xxx will not show */
         setdot(lastmq, FAL0);
      n_autorec_relax_gut();
      mx_COLOUR( mx_colour_env_gut(); )
   } else { /* threaded */
      g = 0;
      mq = threadroot;
      for (mp = threadroot; mp; mp = next_in_thread(mp)){
         /* TODO thread handling needs rewrite, m_collapsed must go */
         if (visible(mp) &&
               (mp->m_collapsed <= 0 ||
                PCMP(mp, ==, message + msgspec - 1))) {
            if (g % size == 0)
               mq = mp;
            if (mp->m_flag & fl) {
               lastg = g;
               lastmq = mq;
            }
            if ((msgspec > 0 && PCMP(mp, ==, message + msgspec - 1)) ||
                  (msgspec == 0 && g == k) ||
                  (msgspec == -2 && g == k + size && lastmq) ||
                  (msgspec < 0 && g >= k && (mp->m_flag & fl) != 0))
               break;
            g++;
         }
      }
      if (lastmq && (msgspec == -2 ||
            (msgspec == -1 && PCMP(mp, ==, message + msgCount)))) {
         g = lastg;
         mq = lastmq;
      }
      _screen = g / size;
      mp = mq;

      mx_COLOUR( mx_colour_env_create(mx_COLOUR_CTX_SUM, n_stdout, FAL0); )
      n_autorec_relax_create();
      for(lastmq = NULL; mp != NULL; lastmq = mp, mp = mq){
         mq = next_in_thread(mp);
         if (visible(mp) &&
               (mp->m_collapsed <= 0 ||
                PCMP(mp, ==, message + msgspec - 1))) {
            if (UCMP(32, flag, >=, size))
               break;
            if(needdot){
               if(showlast){
                  if(UCMP(32, flag, ==, size - 1) || mq == NULL)
                     goto jdot_sort;
               }else if(flag == 0){
jdot_sort:
                  setdot(mp, needdot = FAL0);
               }
            }
            a_chead_print_head(flag, P2UZ(mp - message + 1), n_stdout,
               mb.mb_threaded, TRU1);
            ++flag;
            n_autorec_relax_unroll();
         }
      }
      if(needdot && ok_blook(showlast)) /* xxx will not show */
         setdot(lastmq, FAL0);
      n_autorec_relax_gut();
      mx_COLOUR( mx_colour_env_gut(); )
   }

   if (flag == 0) {
      fprintf(n_stdout, _("No more mail.\n"));
      if (n_pstate & (n_PS_ROBOT | n_PS_HOOK_MASK))
         flag = !flag;
   }
   NYD_OU;
   return !flag;
}

FL int
c_headers(void *v)
{
   int rv;
   NYD_IN;

   rv = print_header_group((int*)v);
   NYD_OU;
   return rv;
}

FL int
print_header_group(int *vector)
{
   int rv;
   NYD_IN;

   ASSERT(vector != NULL && vector != (void*)-1);
   rv = _headers(vector[0]);
   NYD_OU;
   return rv;
}

FL int
c_scroll(void *v)
{
   int rv;
   NYD_IN;

   rv = a_chead_scroll(*(char const**)v, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_Scroll(void *v)
{
   int rv;
   NYD_IN;

   rv = a_chead_scroll(*(char const**)v, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_dotmove(void *vp){
   int rv, msgvec[2];
   char const *args;
   NYD_IN;

   if(*(args = vp) == '\0' || args[1] != '\0'){
jerr:
      mx_cmd_print_synopsis(mx_cmd_firstfit("dotmove"), NIL);
      rv = n_EXIT_ERR;
   }else switch(args[0]){
   case '-':
   case '+':
      if(msgCount == 0){
         fprintf(n_stdout, _("At EOF\n"));
         rv = 0;
      }else if(n_getmsglist(UNCONST(char*,/*TODO*/args), msgvec, 0, NIL) > 0){
         setdot(&message[msgvec[0] - 1], FAL0);
         msgvec[1] = 0;
         rv = c_headers(msgvec);
      }else
         rv = n_EXIT_ERR;
      break;
   default:
      goto jerr;
   }

   NYD_OU;
   return rv;
}

FL int
c_from(void *vp)
{
   int *msgvec, *ip, n;
   char *cp;
   FILE * volatile obuf;
   NYD_IN;

   if(*(msgvec = vp) == 0)
      goto jleave;

   time_current_update(&time_current, FAL0);

   obuf = n_stdout;

   if (n_psonce & n_PSO_INTERACTIVE) {
      if ((cp = ok_vlook(crt)) != NULL) {
         uz ib;

         for (n = 0, ip = msgvec; *ip != 0; ++ip)
            ++n;

         if(*cp == '\0')
            ib = n_screensize();
         else
            su_idec_uz_cp(&ib, cp, 0, NULL);
         if (UCMP(z, n, >, ib) && (obuf = mx_pager_open()) == NULL)
            obuf = n_stdout;
      }
   }

   /* Update dot before display so that the dotmark etc. are correct */
   for (ip = msgvec; ip[1] != 0; ++ip)
      ;
   setdot(&message[(ok_blook(showlast) ? *ip : *msgvec) - 1], FAL0);

   mx_COLOUR( mx_colour_env_create(mx_COLOUR_CTX_SUM, obuf,
      (obuf != n_stdout)); )
   n_autorec_relax_create();
   for(n = 0, ip = msgvec; *ip != 0; ++ip){ /* TODO join into _print_head() */
      a_chead_print_head((uz)n++, S(uz,*ip), obuf, mb.mb_threaded, FAL0);
      n_autorec_relax_unroll();
   }
   n_autorec_relax_gut();
   mx_COLOUR( mx_colour_env_gut(); )

   if(obuf != n_stdout)
      mx_pager_close(obuf);
   else
      clearerr(obuf);

jleave:
   NYD_OU;
   return 0;
}

FL void
print_headers(int const *msgvec, boole only_marked,
   boole subject_thread_compress)
{
   uz printed;
   NYD_IN;

   time_current_update(&time_current, FAL0);

   mx_COLOUR( mx_colour_env_create(mx_COLOUR_CTX_SUM, n_stdout, FAL0); )
   n_autorec_relax_create();
   for(printed = 0; *msgvec != 0; ++msgvec) {
      struct message *mp = message + *msgvec - 1;
      if (only_marked) {
         if (!(mp->m_flag & MMARK))
            continue;
      } else if (!visible(mp))
         continue;
      a_chead_print_head(printed++, *msgvec, n_stdout, mb.mb_threaded,
         subject_thread_compress);
      n_autorec_relax_unroll();
   }
   n_autorec_relax_gut();
   mx_COLOUR( mx_colour_env_gut(); )
   NYD_OU;
}

#include "su/code-ou.h"
/* s-it-mode */
