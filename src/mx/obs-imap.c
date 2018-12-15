/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ IMAP v4r1 client following RFC 2060.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 * SPDX-License-Identifier: BSD-4-Clause
 */
/*
 * Copyright (c) 2004
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef su_FILE
#define su_FILE obs_imap
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#ifdef mx_HAVE_IMAP
# include <sys/socket.h>

# include <netdb.h>

# include <netinet/in.h>

# ifdef mx_HAVE_ARPA_INET_H
#  include <arpa/inet.h>
# endif
#endif

EMPTY_FILE()
#ifdef mx_HAVE_IMAP
#define IMAP_ANSWER() \
{\
   if (mp->mb_type != MB_CACHE) {\
      enum okay ok = OKAY;\
      while (mp->mb_active & MB_COMD)\
         ok = imap_answer(mp, 1);\
      if (ok == STOP)\
         return STOP;\
   }\
}

/* TODO IMAP_OUT() simply returns instead of doing "actioN" if imap_finish()
 * TODO fails, which leaves behind leaks in, e.g., imap_append1()!
 * TODO IMAP_XOUT() was added due to this, but (1) needs to be used everywhere
 * TODO and (2) doesn't handle all I/O errors itself, yet, too.
 * TODO I.e., that should be a function, not a macro ... or so.
 * TODO This entire module needs MASSIVE work! */
#define IMAP_OUT(X,Y,ACTION)  IMAP_XOUT(X, Y, ACTION, return STOP)
#define IMAP_XOUT(X,Y,ACTIONERR,ACTIONBAIL) \
{\
   if (mp->mb_type != MB_CACHE) {\
      if (imap_finish(mp) == STOP) {\
         ACTIONBAIL;\
      }\
      if (n_poption & n_PO_D_VV)\
         n_err(">>> %s", X);\
      mp->mb_active |= Y;\
      if (swrite(&mp->mb_sock, X) == STOP) {\
         ACTIONERR;\
      }\
   } else {\
      if (queuefp != NULL)\
         fputs(X, queuefp);\
   }\
}

static struct record {
   struct record  *rec_next;
   unsigned long  rec_count;
   enum rec_type {
      REC_EXISTS,
      REC_EXPUNGE
   }              rec_type;
} *record, *recend;

static enum {
   RESPONSE_TAGGED,
   RESPONSE_DATA,
   RESPONSE_FATAL,
   RESPONSE_CONT,
   RESPONSE_ILLEGAL
} response_type;

static enum {
   RESPONSE_OK,
   RESPONSE_NO,
   RESPONSE_BAD,
   RESPONSE_PREAUTH,
   RESPONSE_BYE,
   RESPONSE_OTHER,
   RESPONSE_UNKNOWN
} response_status;

static char *responded_tag;
static char *responded_text;
static char *responded_other_text;
static long responded_other_number;

static enum {
   MAILBOX_DATA_FLAGS,
   MAILBOX_DATA_LIST,
   MAILBOX_DATA_LSUB,
   MAILBOX_DATA_MAILBOX,
   MAILBOX_DATA_SEARCH,
   MAILBOX_DATA_STATUS,
   MAILBOX_DATA_EXISTS,
   MAILBOX_DATA_RECENT,
   MESSAGE_DATA_EXPUNGE,
   MESSAGE_DATA_FETCH,
   CAPABILITY_DATA,
   RESPONSE_OTHER_UNKNOWN
} response_other;

static enum list_attributes {
   LIST_NONE         = 000,
   LIST_NOINFERIORS  = 001,
   LIST_NOSELECT     = 002,
   LIST_MARKED       = 004,
   LIST_UNMARKED     = 010
} list_attributes;

static int  list_hierarchy_delimiter;
static char *list_name;

struct list_item {
   struct list_item     *l_next;
   char                 *l_name;
   char                 *l_base;
   enum list_attributes l_attr;
   int                  l_delim;
   int                  l_level;
   int                  l_has_children;
};

static char             *imapbuf;   /* TODO not static, use pool */
static size_t           imapbufsize;
static sigjmp_buf       imapjmp;
static sighandler_type  savealrm;
static int              imapkeepalive;
static long             had_exists = -1;
static long             had_expunge = -1;
static long             expunged_messages;
static int volatile     imaplock;
static int              same_imap_account;
static bool_t           _imap_rdonly;

static char *imap_quotestr(char const *s);
static char *imap_unquotestr(char const *s);
static void imap_delim_init(struct mailbox *mp, struct url const *urlp);
static char const *imap_path_normalize(struct mailbox *mp, char const *cp);
/* Returns NULL on error */
static char *imap_path_quote(struct mailbox *mp, char const *cp);
static void       imap_other_get(char *pp);
static void       imap_response_get(const char **cp);
static void       imap_response_parse(void);
static enum okay  imap_answer(struct mailbox *mp, int errprnt);
static enum okay  imap_parse_list(void);
static enum okay  imap_finish(struct mailbox *mp);
static void       imap_timer_off(void);
static void       imapcatch(int s);
static void       _imap_maincatch(int s);
static enum okay  imap_noop1(struct mailbox *mp);
static void       rec_queue(enum rec_type type, unsigned long cnt);
static enum okay  rec_dequeue(void);
static void       rec_rmqueue(void);
static void       imapalarm(int s);
static enum okay  imap_preauth(struct mailbox *mp, struct url *urlp);
static enum okay  imap_capability(struct mailbox *mp);
static enum okay  imap_auth(struct mailbox *mp, struct ccred *ccred);
#ifdef mx_HAVE_MD5
static enum okay  imap_cram_md5(struct mailbox *mp, struct ccred *ccred);
#endif
static enum okay  imap_login(struct mailbox *mp, struct ccred *ccred);
#ifdef mx_HAVE_GSSAPI
static enum okay  _imap_gssapi(struct mailbox *mp, struct ccred *ccred);
#endif
static enum okay  imap_flags(struct mailbox *mp, unsigned X, unsigned Y);
static void       imap_init(struct mailbox *mp, int n);
static void       imap_setptr(struct mailbox *mp, int nmail, int transparent,
                     int *prevcount);
static bool_t     _imap_getcred(struct mailbox *mbp, struct ccred *ccredp,
                     struct url *urlp);
static int _imap_setfile1(char const *who, struct url *urlp,
            enum fedit_mode fm, int transparent);
static int        imap_fetchdata(struct mailbox *mp, struct message *m,
                     size_t expected, int need, const char *head,
                     size_t headsize, long headlines);
static void       imap_putstr(struct mailbox *mp, struct message *m,
                     const char *str, const char *head, size_t headsize,
                     long headlines);
static enum okay  imap_get(struct mailbox *mp, struct message *m,
                     enum needspec need);
static void       commitmsg(struct mailbox *mp, struct message *to,
                     struct message *from, enum content_info content_info);
static enum okay  imap_fetchheaders(struct mailbox *mp, struct message *m,
                     int bot, int top);
static enum okay  imap_exit(struct mailbox *mp);
static enum okay  imap_delete(struct mailbox *mp, int n, struct message *m,
                     int needstat);
static enum okay  imap_close(struct mailbox *mp);
static enum okay  imap_update(struct mailbox *mp);
static enum okay  imap_store(struct mailbox *mp, struct message *m, int n,
                     int c, const char *sp, int needstat);
static enum okay  imap_unstore(struct message *m, int n, const char *flag);
static const char *tag(int new);
static char *     imap_putflags(int f);
static void       imap_getflags(const char *cp, char const **xp, enum mflag *f);
static enum okay  imap_append1(struct mailbox *mp, const char *name, FILE *fp,
                     off_t off1, long xsize, enum mflag flag, time_t t);
static enum okay  imap_append0(struct mailbox *mp, const char *name, FILE *fp,
                     long offset);
static enum okay  imap_list1(struct mailbox *mp, const char *base,
                     struct list_item **list, struct list_item **lend,
                     int level);
static enum okay  imap_list(struct mailbox *mp, const char *base, int strip,
                     FILE *fp);
static enum okay  imap_copy1(struct mailbox *mp, struct message *m, int n,
                     const char *name);
static enum okay  imap_copyuid_parse(const char *cp,
                     ui64_t *uidvalidity, ui64_t *olduid, ui64_t *newuid);
static enum okay  imap_appenduid_parse(const char *cp,
                     ui64_t *uidvalidity, ui64_t *uid);
static enum okay  imap_copyuid(struct mailbox *mp, struct message *m,
                     const char *name);
static enum okay  imap_appenduid(struct mailbox *mp, FILE *fp, time_t t,
                     long off1, long xsize, long size, long lines, int flag,
                     const char *name);
static enum okay  imap_appenduid_cached(struct mailbox *mp, FILE *fp);
#ifdef mx_HAVE_IMAP_SEARCH
static ssize_t    imap_search2(struct mailbox *mp, struct message *m, int cnt,
                     const char *spec, int f);
#endif
static enum okay  imap_remove1(struct mailbox *mp, const char *name);
static enum okay  imap_rename1(struct mailbox *mp, const char *old,
                     const char *new);
static char *     imap_strex(char const *cp, char const **xp);
static enum okay  check_expunged(void);

static char *
imap_quotestr(char const *s)
{
   char *n, *np;
   n_NYD2_IN;

   np = n = n_autorec_alloc(2 * strlen(s) + 3);
   *np++ = '"';
   while (*s) {
      if (*s == '"' || *s == '\\')
         *np++ = '\\';
      *np++ = *s++;
   }
   *np++ = '"';
   *np = '\0';
   n_NYD2_OU;
   return n;
}

static char *
imap_unquotestr(char const *s)
{
   char *n, *np;
   n_NYD2_IN;

   if (*s != '"') {
      n = savestr(s);
      goto jleave;
   }

   np = n = n_autorec_alloc(strlen(s) + 1);
   while (*++s) {
      if (*s == '\\')
         s++;
      else if (*s == '"')
         break;
      *np++ = *s;
   }
   *np = '\0';
jleave:
   n_NYD2_OU;
   return n;
}

static void
imap_delim_init(struct mailbox *mp, struct url const *urlp){
   size_t i;
   char const *cp;
   n_NYD2_IN;

   mp->mb_imap_delim[0] = '\0';

   if((cp = xok_vlook(imap_delim, urlp, OXM_ALL)) != NULL){
      i = strlen(cp);

      if(i == 0){
         cp = n_IMAP_DELIM;
         i = sizeof(n_IMAP_DELIM) -1;
         goto jcopy;
      }

      if(i < n_NELEM(mp->mb_imap_delim))
jcopy:
         memcpy(&mb.mb_imap_delim[0], cp, i +1);
      else
         n_err(_("*imap-delim* for %s is too long: %s\n"),
            urlp->url_input, cp);
   }
   n_NYD2_OU;
}

static char const *
imap_path_normalize(struct mailbox *mp, char const *cp){
   char *rv_base, *rv, dc2, dc, c, lc;
   char const *dcp;
   n_NYD2_IN;

   /* Unless we operate in free fly, honour a non-set *imap-delim* to mean "use
    * exactly what i have specified" */
   if(mp == NULL || mp->mb_imap_delim[0] == '\0')
      dcp = &n_IMAP_DELIM[0];
   else
      dcp = &mp->mb_imap_delim[0];
   dc2 = ((dc = *dcp) != '\0') ? *++dcp : dc;

   /* Plain names don't need path quoting */
   /* C99 */{
      size_t i, j;
      char const *cpx;

      for(cpx = cp;; ++cpx)
         if((c = *cpx) == '\0')
            goto jleave;
         else if(dc == '\0'){
            if(strchr(n_IMAP_DELIM, c)){
               dc = c;
               break;
            }
         }else if(c == dc)
            break;
         else if(dc2 && strchr(dcp, c) != NULL)
            break;

      /* And we don't need to reevaluate what we have seen yet */
      i = PTR2SIZE(cpx - cp);
      rv = rv_base = n_autorec_alloc(i + (j = strlen(cpx) +1));
      if(i > 0)
         memcpy(rv, cp, i);
      memcpy(&rv[i], cpx, j);
      rv += i;
      cp = cpx;
   }

   /* Squeeze adjacent delimiters, convert remain to dc */
   for(lc = '\0'; (c = *cp++) != '\0'; lc = c){
      if(c == dc || (lc != '\0' && dc2 && strchr(dcp, c) != NULL))
         c = dc;
      if(c != dc || lc != dc)
         *rv++ = c;
   }
   *rv = '\0';

   cp = rv_base;
jleave:
   n_NYD2_OU;
   return cp;
}

FL char const *
imap_path_encode(char const *cp, bool_t *err_or_null){
   /* To a large extend inspired by dovecot(1) */
   struct str out;
   bool_t err_def;
   ui8_t *be16p_base, *be16p;
   char const *emsg;
   char c;
   size_t l, l_plain;
   n_NYD2_IN;

   if(err_or_null == NULL)
      err_or_null = &err_def;
   *err_or_null = FAL0;

   /* Is this a string that works out as "plain US-ASCII"? */
   for(l = 0;; ++l)
      if((c = cp[l]) == '\0')
         goto jleave;
      else if(c <= 0x1F || c >= 0x7F || c == '&')
         break;

   *err_or_null = TRU1;

   /* We need to encode in mUTF-7!  For that, we first have to convert the
    * local charset to UTF-8, then convert all characters which need to be
    * encoded (except plain "&") to UTF-16BE first, then that to mUTF-7.
    * We can skip the UTF-8 conversion occasionally, however */
#if (defined mx_HAVE_DEVEL || !defined mx_HAVE_ALWAYS_UNICODE_LOCALE) &&\
      defined mx_HAVE_ICONV
   if(!(n_psonce & n_PSO_UNICODE)){
      char const *x;

      emsg = N_("iconv(3) from locale charset to UTF-8 failed");
      if((x = n_iconv_onetime_cp(n_ICONV_NONE, "utf-8", ok_vlook(ttycharset),
            cp)) == NULL)
         goto jerr;
      cp = x;

      /* So: Why not start all over again?
       * Is this a string that works out as "plain US-ASCII"? */
      for(l = 0;; ++l)
         if((c = cp[l]) == '\0')
            goto jleave;
         else if(c <= 0x1F || c >= 0x7F || c == '&')
            break;
   }
#endif

   /* We need to encode, save what we have, encode the rest */
   l_plain = l;

   for(cp += l, l = 0; cp[l] != '\0'; ++l)
      ;
   be16p_base = n_autorec_alloc((l << 1) +1); /* XXX use n_string, resize */

   out.s = n_autorec_alloc(l_plain + (l << 2) +1); /* XXX use n_string.. */
   if(l_plain > 0)
      memcpy(out.s, &cp[-l_plain], out.l = l_plain);
   else
      out.l = 0;
   DBG( l_plain += (l << 2); )

   while(l > 0){
      c = *cp++;
      --l;

      if(c == '&'){
         out.s[out.l + 0] = '&';
         out.s[out.l + 1] = '-';
         out.l += 2;
      }else if(c > 0x1F && c < 0x7F)
         out.s[out.l++] = c;
      else{
         static char const mb64ct[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
         ui32_t utf32;

         /* Convert consecutive non-representables */
         emsg = N_("Invalid UTF-8 sequence, cannot convert to UTF-32");

         for(be16p = be16p_base, --cp, ++l;;){
            if((utf32 = n_utf8_to_utf32(&cp, &l)) == UI32_MAX)
               goto jerr;

            /* TODO S-CText: magic utf16 conversions */
            if(utf32 < 0x10000){
               be16p[1] = utf32 & 0xFF;
               be16p[0] = (utf32 >>= 8, utf32 &= 0xFF);
               be16p += 2;
            }else{
               ui16_t s7e;

               utf32 -= 0x10000;
               s7e = 0xD800u | (utf32 >> 10);
               be16p[1] = s7e & 0xFF;
               be16p[0] = (s7e >>= 8, s7e &= 0xFF);
               s7e = 0xDC00u | (utf32 &= 0x03FF);
               be16p[3] = s7e & 0xFF;
               be16p[2] = (s7e >>= 8, s7e &= 0xFF);
               be16p += 4;
            }

            if(l == 0)
               break;
            if((c = *cp) > 0x1F && c < 0x7F)
               break;
         }

         /* And then warp that UTF-16BE to mUTF-7 */
         out.s[out.l++] = '&';
         utf32 = (ui32_t)PTR2SIZE(be16p - be16p_base);
         be16p = be16p_base;

         for(; utf32 >= 3; be16p += 3, utf32 -= 3){
            out.s[out.l+0] = mb64ct[                            be16p[0] >> 2 ];
            out.s[out.l+1] = mb64ct[((be16p[0] & 0x03) << 4) | (be16p[1] >> 4)];
            out.s[out.l+2] = mb64ct[((be16p[1] & 0x0F) << 2) | (be16p[2] >> 6)];
            out.s[out.l+3] = mb64ct[  be16p[2] & 0x3F];
            out.l += 4;
         }
         if(utf32 > 0){
            out.s[out.l + 0] = mb64ct[be16p[0] >> 2];
            if(--utf32 == 0){
               out.s[out.l + 1] = mb64ct[ (be16p[0] & 0x03) << 4];
               out.l += 2;
            }else{
               out.s[out.l + 1] = mb64ct[((be16p[0] & 0x03) << 4) |
                     (be16p[1] >> 4)];
               out.s[out.l + 2] = mb64ct[ (be16p[1] & 0x0F) << 2];
               out.l += 3;
            }
         }
         out.s[out.l++] = '-';
      }
   }
   out.s[out.l] = '\0';
   assert(out.l <= l_plain);
   *err_or_null = FAL0;
   cp = out.s;
jleave:
   n_NYD2_OU;
   return cp;
jerr:
   n_err(_("Cannot encode IMAP path %s\n  %s\n"), cp, V_(emsg));
   goto jleave;
}

FL char *
imap_path_decode(char const *path, bool_t *err_or_null){
   /* To a large extend inspired by dovecot(1) TODO use string */
   bool_t err_def;
   ui8_t *mb64p_base, *mb64p, *mb64xp;
   char const *emsg, *cp;
   char *rv_base, *rv, c;
   size_t l_orig, l, i;
   n_NYD2_IN;

   if(err_or_null == NULL)
      err_or_null = &err_def;
   *err_or_null = FAL0;

   l = l_orig = strlen(path);
   rv = rv_base = n_autorec_alloc(l << 1);
   memcpy(rv, path, l +1);

   /* xxx Don't check for invalid characters from malicious servers */
   if(l == 0 || (cp = memchr(path, '&', l)) == NULL)
      goto jleave;

   *err_or_null = TRU1;

   emsg = N_("Invalid mUTF-7 encoding");
   i = PTR2SIZE(cp - path);
   rv += i;
   l -= i;
   mb64p_base = NULL;

   while(l > 0){
      if((c = *cp) != '&'){
         if(c <= 0x1F || c >= 0x7F){
            emsg = N_("Invalid mUTF-7: unencoded control or 8-bit byte");
            goto jerr;
         }
         *rv++ = c;
         ++cp;
         --l;
      }else if(--l == 0)
         goto jeincpl;
      else if(*++cp == '-'){
         *rv++ = '&';
         ++cp;
         --l;
      }else if(l < 3){
jeincpl:
         emsg = N_("Invalid mUTF-7: incomplete input");
         goto jerr;
      }else{
         /* mUTF-7 -> UTF-16BE -> UTF-8 */
         static ui8_t const mb64dt[256] = {
#undef XX
#define XX 0xFFu
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,62, 63,XX,XX,XX,
            52,53,54,55, 56,57,58,59, 60,61,XX,XX, XX,XX,XX,XX,
            XX, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
            15,16,17,18, 19,20,21,22, 23,24,25,XX, XX,XX,XX,XX,
            XX,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
            41,42,43,44, 45,46,47,48, 49,50,51,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX,
            XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX, XX,XX,XX,XX
         };

         if(mb64p_base == NULL)
            mb64p_base = n_autorec_alloc(l);

         /* Decode the mUTF-7 to what is indeed UTF-16BE */
         for(mb64p = mb64p_base;;){
            assert(l >= 3);
            if((mb64p[0] = mb64dt[(ui8_t)cp[0]]) == XX ||
                  (mb64p[1] = mb64dt[(ui8_t)cp[1]]) == XX)
               goto jerr;
            mb64p += 2;

            c = cp[2];
            cp += 3;
            l -= 3;
            if(c == '-')
               break;
            if((*mb64p++ = mb64dt[(ui8_t)c]) == XX)
               goto jerr;

            if(l == 0)
               goto jerr;
            --l;
            if((c = *cp++) == '-')
               break;
            if((*mb64p++ = mb64dt[(ui8_t)c]) == XX)
               goto jerr;

            if(l < 3){
               if(l > 0 && *cp == '-'){
                  --l;
                  ++cp;
                  break;
               }
               goto jerr;
            }
         }
#undef XX

         if(l >= 2 && cp[0] == '&' && cp[1] != '-'){
            emsg = N_("Invalid mUTF-7, consecutive encoded sequences");
            goto jerr;
         }

         /* Yet halfway decoded mUTF-7, go remaining way to gain UTF-16BE */
         i = PTR2SIZE(mb64p - mb64p_base);
         mb64p = mb64xp = mb64p_base;

         while(i > 0){
            ui8_t ul, u0, u1, u2, u3;

            ul = (i >= 4) ? 4 : i & 0x3;
            i -= ul;
            u0 = mb64xp[0];
            u1 = mb64xp[1];
            u2 = (ul < 3) ? 0 : mb64xp[2];
            u3 = (ul < 4) ? 0 : mb64xp[3];
            mb64xp += ul;
            *mb64p++ = (u0 <<= 2) | (u1 >> 4);
            if(ul < 3)
               break;
            *mb64p++ = (u1 <<= 4) | (u2 >> 2);
            if(ul < 4)
               break;
            *mb64p++ = (u2 <<= 6, u2 &= 0xC0) | u3;
         }

         /* UTF-16BE we convert to UTF-8 */
         i = PTR2SIZE(mb64p - mb64p_base);
         if(i & 1){
            emsg = N_("Odd bytecount for UTF-16BE input");
            goto jerr;
         }

         /* TODO S-CText: magic utf16 conversions */
         emsg = N_("Invalid UTF-16BE encoding");

         for(mb64p = mb64p_base; i > 0;){
            ui32_t utf32;
            ui16_t uhi, ulo;

            uhi = mb64p[0];
            uhi <<= 8;
            uhi |= mb64p[1];

            /* Not a surrogate? */
            if(uhi < 0xD800 || uhi > 0xDFFF){
               utf32 = uhi;
               mb64p += 2;
               i -= 2;
            }else if(uhi > 0xDBFF)
               goto jerr;
            else if(i < 4){
               emsg = N_("Incomplete UTF-16BE surrogate pair");
               goto jerr;
            }else{
               ulo = mb64p[2];
               ulo <<= 8;
               ulo |= mb64p[3];
               if(ulo < 0xDC00 || ulo > 0xDFFF)
                  goto jerr;

               utf32 = (uhi &= 0x03FF);
               utf32 <<= 10;
               utf32 += 0x10000;
               utf32 |= (ulo &= 0x03FF);
               mb64p += 4;
               i -= 4;
            }

            utf32 = n_utf32_to_utf8(utf32, rv);
            rv += utf32;
         }
      }
   }
   *rv = '\0';

   /* We can skip the UTF-8 conversion occasionally */
#if (defined mx_HAVE_DEVEL || !defined mx_HAVE_ALWAYS_UNICODE_LOCALE) &&\
      defined mx_HAVE_ICONV
   if(!(n_psonce & n_PSO_UNICODE)){
      emsg = N_("iconv(3) from UTF-8 to locale charset failed");
      if((rv = n_iconv_onetime_cp(n_ICONV_NONE, NULL, NULL, rv_base)) == NULL)
         goto jerr;
   }
#endif

   *err_or_null = FAL0;
   rv = rv_base;
jleave:
   n_NYD2_OU;
   return rv;
jerr:
   n_err(_("Cannot decode IMAP path %s\n  %s\n"), path, V_(emsg));
   memcpy(rv = rv_base, path, ++l_orig);
   goto jleave;
}

static char *
imap_path_quote(struct mailbox *mp, char const *cp){
   bool_t err;
   char *rv;
   n_NYD2_IN;

   cp = imap_path_normalize(mp, cp);
   cp = imap_path_encode(cp, &err);
   rv = err ? NULL : imap_quotestr(cp);
   n_NYD2_OU;
   return rv;
}

static void
imap_other_get(char *pp)
{
   char *xp;
   n_NYD2_IN;

   if (ascncasecmp(pp, "FLAGS ", 6) == 0) {
      pp += 6;
      response_other = MAILBOX_DATA_FLAGS;
   } else if (ascncasecmp(pp, "LIST ", 5) == 0) {
      pp += 5;
      response_other = MAILBOX_DATA_LIST;
   } else if (ascncasecmp(pp, "LSUB ", 5) == 0) {
      pp += 5;
      response_other = MAILBOX_DATA_LSUB;
   } else if (ascncasecmp(pp, "MAILBOX ", 8) == 0) {
      pp += 8;
      response_other = MAILBOX_DATA_MAILBOX;
   } else if (ascncasecmp(pp, "SEARCH ", 7) == 0) {
      pp += 7;
      response_other = MAILBOX_DATA_SEARCH;
   } else if (ascncasecmp(pp, "STATUS ", 7) == 0) {
      pp += 7;
      response_other = MAILBOX_DATA_STATUS;
   } else if (ascncasecmp(pp, "CAPABILITY ", 11) == 0) {
      pp += 11;
      response_other = CAPABILITY_DATA;
   } else {
      responded_other_number = strtol(pp, &xp, 10);
      while (*xp == ' ')
         ++xp;
      if (ascncasecmp(xp, "EXISTS\r\n", 8) == 0) {
         response_other = MAILBOX_DATA_EXISTS;
      } else if (ascncasecmp(xp, "RECENT\r\n", 8) == 0) {
         response_other = MAILBOX_DATA_RECENT;
      } else if (ascncasecmp(xp, "EXPUNGE\r\n", 9) == 0) {
         response_other = MESSAGE_DATA_EXPUNGE;
      } else if (ascncasecmp(xp, "FETCH ", 6) == 0) {
         pp = &xp[6];
         response_other = MESSAGE_DATA_FETCH;
      } else
         response_other = RESPONSE_OTHER_UNKNOWN;
   }
   responded_other_text = pp;
   n_NYD2_OU;
}

static void
imap_response_get(const char **cp)
{
   n_NYD2_IN;
   if (ascncasecmp(*cp, "OK ", 3) == 0) {
      *cp += 3;
      response_status = RESPONSE_OK;
   } else if (ascncasecmp(*cp, "NO ", 3) == 0) {
      *cp += 3;
      response_status = RESPONSE_NO;
   } else if (ascncasecmp(*cp, "BAD ", 4) == 0) {
      *cp += 4;
      response_status = RESPONSE_BAD;
   } else if (ascncasecmp(*cp, "PREAUTH ", 8) == 0) {
      *cp += 8;
      response_status = RESPONSE_PREAUTH;
   } else if (ascncasecmp(*cp, "BYE ", 4) == 0) {
      *cp += 4;
      response_status = RESPONSE_BYE;
   } else
      response_status = RESPONSE_OTHER;
   n_NYD2_OU;
}

static void
imap_response_parse(void)
{
   static char *parsebuf; /* TODO Use pool */
   static size_t  parsebufsize;

   const char *ip = imapbuf;
   char *pp;
   n_NYD2_IN;

   if (parsebufsize < imapbufsize + 1)
      parsebuf = n_realloc(parsebuf, parsebufsize = imapbufsize);
   memcpy(parsebuf, imapbuf, strlen(imapbuf) + 1);
   pp = parsebuf;
   switch (*ip) {
   case '+':
      response_type = RESPONSE_CONT;
      ip++;
      pp++;
      while (*ip == ' ') {
         ip++;
         pp++;
      }
      break;
   case '*':
      ip++;
      pp++;
      while (*ip == ' ') {
         ip++;
         pp++;
      }
      imap_response_get(&ip);
      pp = &parsebuf[ip - imapbuf];
      switch (response_status) {
      case RESPONSE_BYE:
         response_type = RESPONSE_FATAL;
         break;
      default:
         response_type = RESPONSE_DATA;
      }
      break;
   default:
      responded_tag = parsebuf;
      while (*pp && *pp != ' ')
         pp++;
      if (*pp == '\0') {
         response_type = RESPONSE_ILLEGAL;
         break;
      }
      *pp++ = '\0';
      while (*pp && *pp == ' ')
         pp++;
      if (*pp == '\0') {
         response_type = RESPONSE_ILLEGAL;
         break;
      }
      ip = &imapbuf[pp - parsebuf];
      response_type = RESPONSE_TAGGED;
      imap_response_get(&ip);
      pp = &parsebuf[ip - imapbuf];
   }
   responded_text = pp;
   if (response_type != RESPONSE_CONT && response_type != RESPONSE_ILLEGAL &&
         response_status == RESPONSE_OTHER)
      imap_other_get(pp);
   n_NYD2_OU;
}

static enum okay
imap_answer(struct mailbox *mp, int errprnt)
{
   int i, complete;
   enum okay rv;
   n_NYD2_IN;

   rv = OKAY;
   if (mp->mb_type == MB_CACHE)
      goto jleave;
   rv = STOP;
jagain:
   if (sgetline(&imapbuf, &imapbufsize, NULL, &mp->mb_sock) > 0) {
      if (n_poption & n_PO_D_VV)
         n_err(">>> SERVER: %s", imapbuf);
      imap_response_parse();
      if (response_type == RESPONSE_ILLEGAL)
         goto jagain;
      if (response_type == RESPONSE_CONT) {
         rv = OKAY;
         goto jleave;
      }
      if (response_status == RESPONSE_OTHER) {
         if (response_other == MAILBOX_DATA_EXISTS) {
            had_exists = responded_other_number;
            rec_queue(REC_EXISTS, responded_other_number);
            if (had_expunge > 0)
               had_expunge = 0;
         } else if (response_other == MESSAGE_DATA_EXPUNGE) {
            rec_queue(REC_EXPUNGE, responded_other_number);
            if (had_expunge < 0)
               had_expunge = 0;
            had_expunge++;
            expunged_messages++;
         }
      }
      complete = 0;
      if (response_type == RESPONSE_TAGGED) {
         if (asccasecmp(responded_tag, tag(0)) == 0)
            complete |= 1;
         else
            goto jagain;
      }
      switch (response_status) {
      case RESPONSE_PREAUTH:
         mp->mb_active &= ~MB_PREAUTH;
         /*FALLTHRU*/
      case RESPONSE_OK:
jokay:
         rv = OKAY;
         complete |= 2;
         break;
      case RESPONSE_NO:
      case RESPONSE_BAD:
jstop:
         complete |= 2;
         if (errprnt)
            n_err(_("IMAP error: %s"), responded_text);
         break;
      case RESPONSE_UNKNOWN:  /* does not happen */
      case RESPONSE_BYE:
         i = mp->mb_active;
         mp->mb_active = MB_NONE;
         if (i & MB_BYE)
            goto jokay;
         goto jstop;
      case RESPONSE_OTHER:
         rv = OKAY;
         break;
      }
      if (response_status != RESPONSE_OTHER &&
            ascncasecmp(responded_text, "[ALERT] ", 8) == 0)
         n_err(_("IMAP alert: %s"), &responded_text[8]);
      if (complete == 3)
         mp->mb_active &= ~MB_COMD;
   } else
      mp->mb_active = MB_NONE;
jleave:
   n_NYD2_OU;
   return rv;
}

static enum okay
imap_parse_list(void)
{
   char *cp;
   enum okay rv;
   n_NYD2_IN;

   rv = STOP;

   cp = responded_other_text;
   list_attributes = LIST_NONE;
   if (*cp == '(') {
      while (*cp && *cp != ')') {
         if (*cp == '\\') {
            if (ascncasecmp(&cp[1], "Noinferiors ", 12) == 0) {
               list_attributes |= LIST_NOINFERIORS;
               cp += 12;
            } else if (ascncasecmp(&cp[1], "Noselect ", 9) == 0) {
               list_attributes |= LIST_NOSELECT;
               cp += 9;
            } else if (ascncasecmp(&cp[1], "Marked ", 7) == 0) {
               list_attributes |= LIST_MARKED;
               cp += 7;
            } else if (ascncasecmp(&cp[1], "Unmarked ", 9) == 0) {
               list_attributes |= LIST_UNMARKED;
               cp += 9;
            }
         }
         cp++;
      }
      if (*++cp != ' ')
         goto jleave;
      while (*cp == ' ')
         cp++;
   }

   list_hierarchy_delimiter = EOF;
   if (*cp == '"') {
      if (*++cp == '\\')
         cp++;
      list_hierarchy_delimiter = *cp++ & 0377;
      if (cp[0] != '"' || cp[1] != ' ')
         goto jleave;
      cp++;
   } else if (cp[0] == 'N' && cp[1] == 'I' && cp[2] == 'L' && cp[3] == ' ') {
      list_hierarchy_delimiter = EOF;
      cp += 3;
   }

   while (*cp == ' ')
      cp++;
   list_name = cp;
   while (*cp && *cp != '\r')
      cp++;
   *cp = '\0';
   rv = OKAY;
jleave:
   n_NYD2_OU;
   return rv;
}

static enum okay
imap_finish(struct mailbox *mp)
{
   n_NYD_IN;
   while (mp->mb_sock.s_fd > 0 && mp->mb_active & MB_COMD)
      imap_answer(mp, 1);
   n_NYD_OU;
   return OKAY;
}

static void
imap_timer_off(void)
{
   n_NYD_IN;
   if (imapkeepalive > 0) {
      alarm(0);
      safe_signal(SIGALRM, savealrm);
   }
   n_NYD_OU;
}

static void
imapcatch(int s)
{
   n_NYD_X; /*  Signal handler */
   switch (s) {
   case SIGINT:
      n_err_sighdl(_("Interrupt\n"));
      siglongjmp(imapjmp, 1);
      /*NOTREACHED*/
   case SIGPIPE:
      n_err_sighdl(_("Received SIGPIPE during IMAP operation\n"));
      break;
   }
}

static void
_imap_maincatch(int s)
{
   n_NYD_X; /*  Signal handler */
   n_UNUSED(s);
   if (interrupts++ == 0) {
      n_err_sighdl(_("Interrupt\n"));
      return;
   }
   n_go_onintr_for_imap();
}

static enum okay
imap_noop1(struct mailbox *mp)
{
   char o[LINESIZE];
   FILE *queuefp = NULL;
   n_NYD_X;

   snprintf(o, sizeof o, "%s NOOP\r\n", tag(1));
   IMAP_OUT(o, MB_COMD, return STOP)
   IMAP_ANSWER()
   return OKAY;
}

FL char const *
imap_fileof(char const *xcp)
{
   char const *cp = xcp;
   int state = 0;
   n_NYD_IN;

   while (*cp) {
      if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
         cp += 3;
         state = 1;
      }
      if (cp[0] == '/' && state == 1) {
         ++cp;
         goto jleave;
      }
      if (cp[0] == '/') {
         cp = xcp;
         goto jleave;
      }
      ++cp;
   }
jleave:
   n_NYD_OU;
   return cp;
}

FL enum okay
imap_noop(void)
{
   sighandler_type volatile oldint, oldpipe;
   enum okay volatile rv = STOP;
   n_NYD_IN;

   if (mb.mb_type != MB_IMAP)
      goto jleave;

   imaplock = 1;
   if ((oldint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   oldpipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (oldpipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      rv = imap_noop1(&mb);
   }
   safe_signal(SIGINT, oldint);
   safe_signal(SIGPIPE, oldpipe);
   imaplock = 0;
jleave:
   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}

static void
rec_queue(enum rec_type rt, unsigned long cnt)
{
   struct record *rp;
   n_NYD_IN;

   rp = n_calloc(1, sizeof *rp);
   rp->rec_type = rt;
   rp->rec_count = cnt;
   if (record && recend) {
      recend->rec_next = rp;
      recend = rp;
   } else
      record = recend = rp;
   n_NYD_OU;
}

static enum okay
rec_dequeue(void)
{
   struct message *omessage;
   struct record *rp, *rq;
   uiz_t exists = 0, i;
   enum okay rv = STOP;
   n_NYD_IN;

   if (record == NULL)
      goto jleave;

   omessage = message;
   message = n_alloc((msgCount+1) * sizeof *message);
   if (msgCount)
      memcpy(message, omessage, msgCount * sizeof *message);
   memset(&message[msgCount], 0, sizeof *message);

   rp = record, rq = NULL;
   rv = OKAY;
   while (rp != NULL) {
      switch (rp->rec_type) {
      case REC_EXISTS:
         exists = rp->rec_count;
         break;
      case REC_EXPUNGE:
         if (rp->rec_count == 0) {
            rv = STOP;
            break;
         }
         if (rp->rec_count > (unsigned long)msgCount) {
            if (exists == 0 || rp->rec_count > exists--)
               rv = STOP;
            break;
         }
         if (exists > 0)
            exists--;
         delcache(&mb, &message[rp->rec_count-1]);
         memmove(&message[rp->rec_count-1], &message[rp->rec_count],
            ((msgCount - rp->rec_count + 1) * sizeof *message));
         --msgCount;
         /* If the message was part of a collapsed thread,
          * the m_collapsed field of one of its ancestors
          * should be incremented. It seems hardly possible
          * to do this with the current message structure,
          * though. The result is that a '+' may be shown
          * in the header summary even if no collapsed
          * children exists */
         break;
      }
      if (rq != NULL)
         n_free(rq);
      rq = rp;
      rp = rp->rec_next;
   }
   if (rq != NULL)
      n_free(rq);

   record = recend = NULL;
   if (rv == OKAY && UICMP(z, exists, >, msgCount)) {
      message = n_realloc(message, (exists + 1) * sizeof *message);
      memset(&message[msgCount], 0, (exists - msgCount + 1) * sizeof *message);
      for (i = msgCount; i < exists; ++i)
         imap_init(&mb, i);
      imap_flags(&mb, msgCount+1, exists);
      msgCount = exists;
   }

   if (rv == STOP) {
      n_free(message);
      message = omessage;
   }
jleave:
   n_NYD_OU;
   return rv;
}

static void
rec_rmqueue(void)
{
   struct record *rp;
   n_NYD_IN;

   for (rp = record; rp != NULL;) {
      struct record *tmp = rp;
      rp = rp->rec_next;
      n_free(tmp);
   }
   record = recend = NULL;
   n_NYD_OU;
}

/*ARGSUSED*/
static void
imapalarm(int s)
{
   sighandler_type volatile saveint, savepipe;
   n_NYD_X; /* Signal handler */
   n_UNUSED(s);

   if (imaplock++ == 0) {
      if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &_imap_maincatch);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if (sigsetjmp(imapjmp, 1)) {
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         goto jbrk;
      }
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);
      if (imap_noop1(&mb) != OKAY) {
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         goto jleave;
      }
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
   }
jbrk:
   alarm(imapkeepalive);
jleave:
   --imaplock;
}

static enum okay
imap_preauth(struct mailbox *mp, struct url *urlp)
{
   n_NYD_X;

   mp->mb_active |= MB_PREAUTH;
   imap_answer(mp, 1);

#ifdef mx_HAVE_TLS
   if (!mp->mb_sock.s_use_tls && xok_blook(imap_use_starttls, urlp, OXM_ALL)) {
      FILE *queuefp = NULL;
      char o[LINESIZE];

      snprintf(o, sizeof o, "%s STARTTLS\r\n", tag(1));
      IMAP_OUT(o, MB_COMD, return STOP)
      IMAP_ANSWER()
      if(!n_tls_open(urlp, &mp->mb_sock))
         return STOP;
   }
#else
   if (xok_blook(imap_use_starttls, urlp, OXM_ALL)) {
      n_err(_("No TLS support compiled in\n"));
      return STOP;
   }
#endif

   imap_capability(mp);
   return OKAY;
}

static enum okay
imap_capability(struct mailbox *mp)
{
   char o[LINESIZE];
   FILE *queuefp = NULL;
   enum okay ok = STOP;
   const char *cp;
   n_NYD_X;

   snprintf(o, sizeof o, "%s CAPABILITY\r\n", tag(1));
   IMAP_OUT(o, MB_COMD, return STOP)
   while (mp->mb_active & MB_COMD) {
      ok = imap_answer(mp, 0);
      if (response_status == RESPONSE_OTHER &&
            response_other == CAPABILITY_DATA) {
         cp = responded_other_text;
         while (*cp) {
            while (spacechar(*cp))
               ++cp;
            if (strncmp(cp, "UIDPLUS", 7) == 0 && spacechar(cp[7]))
               /* RFC 2359 */
               mp->mb_flags |= MB_UIDPLUS;
            while (*cp && !spacechar(*cp))
               ++cp;
         }
      }
   }
   return ok;
}

static enum okay
imap_auth(struct mailbox *mp, struct ccred *ccred)
{
   enum okay rv;
   n_NYD_IN;

   if (!(mp->mb_active & MB_PREAUTH)) {
      rv = OKAY;
      goto jleave;
   }

   switch (ccred->cc_authtype) {
   case AUTHTYPE_LOGIN:
      rv = imap_login(mp, ccred);
      break;
#ifdef mx_HAVE_MD5
   case AUTHTYPE_CRAM_MD5:
      rv = imap_cram_md5(mp, ccred);
      break;
#endif
#ifdef mx_HAVE_GSSAPI
   case AUTHTYPE_GSSAPI:
      rv = _imap_gssapi(mp, ccred);
      break;
#endif
   default:
      rv = STOP;
      break;
   }
jleave:
   n_NYD_OU;
   return rv;
}

#ifdef mx_HAVE_MD5
static enum okay
imap_cram_md5(struct mailbox *mp, struct ccred *ccred)
{
   char o[LINESIZE], *cp;
   FILE *queuefp = NULL;
   enum okay rv = STOP;
   n_NYD_IN;

   snprintf(o, sizeof o, "%s AUTHENTICATE CRAM-MD5\r\n", tag(1));
   IMAP_XOUT(o, 0, goto jleave, goto jleave);
   imap_answer(mp, 1);
   if (response_type != RESPONSE_CONT)
      goto jleave;

   cp = cram_md5_string(&ccred->cc_user, &ccred->cc_pass, responded_text);
   if(cp == NULL)
      goto jleave;
   IMAP_XOUT(cp, MB_COMD, goto jleave, goto jleave);
   while (mp->mb_active & MB_COMD)
      rv = imap_answer(mp, 1);
jleave:
   n_NYD_OU;
   return rv;
}
#endif /* mx_HAVE_MD5 */

static enum okay
imap_login(struct mailbox *mp, struct ccred *ccred)
{
   char o[LINESIZE];
   FILE *queuefp = NULL;
   enum okay rv = STOP;
   n_NYD_IN;

   snprintf(o, sizeof o, "%s LOGIN %s %s\r\n",
      tag(1), imap_quotestr(ccred->cc_user.s), imap_quotestr(ccred->cc_pass.s));
   IMAP_XOUT(o, MB_COMD, goto jleave, goto jleave);
   while (mp->mb_active & MB_COMD)
      rv = imap_answer(mp, 1);
jleave:
   n_NYD_OU;
   return rv;
}

#ifdef mx_HAVE_GSSAPI
# include "mx/obs-imap-gssapi.h"
#endif

FL enum okay
imap_select(struct mailbox *mp, off_t *size, int *cnt, const char *mbx,
   enum fedit_mode fm)
{
   char o[LINESIZE];
   char const *qname, *cp;
   FILE *queuefp;
   enum okay ok;
   n_NYD_X;
   n_UNUSED(size);

   ok = STOP;
   queuefp = NULL;

   if((qname = imap_path_quote(mp, mbx)) == NULL)
      goto jleave;

   ok = OKAY;

   mp->mb_uidvalidity = 0;
   snprintf(o, sizeof o, "%s %s %s\r\n", tag(1),
      (fm & FEDIT_RDONLY ? "EXAMINE" : "SELECT"), qname);
   IMAP_OUT(o, MB_COMD, ok = STOP;goto jleave)
   while (mp->mb_active & MB_COMD) {
      ok = imap_answer(mp, 1);
      if (response_status != RESPONSE_OTHER &&
            (cp = asccasestr(responded_text, "[UIDVALIDITY ")) != NULL)
         n_idec_ui64_cp(&mp->mb_uidvalidity, &cp[13], 10, NULL);/* TODO err? */
   }
   *cnt = (had_exists > 0) ? had_exists : 0;
   if (response_status != RESPONSE_OTHER &&
         ascncasecmp(responded_text, "[READ-ONLY] ", 12) == 0)
      mp->mb_perm = 0;
jleave:
   return ok;
}

static enum okay
imap_flags(struct mailbox *mp, unsigned X, unsigned Y)
{
   char o[LINESIZE];
   FILE *queuefp = NULL;
   char const *cp;
   struct message *m;
   unsigned x = X, y = Y, n;
   n_NYD_X;

   snprintf(o, sizeof o, "%s FETCH %u:%u (FLAGS UID)\r\n", tag(1), x, y);
   IMAP_OUT(o, MB_COMD, return STOP)
   while (mp->mb_active & MB_COMD) {
      imap_answer(mp, 1);
      if (response_status == RESPONSE_OTHER &&
            response_other == MESSAGE_DATA_FETCH) {
         n = responded_other_number;
         if (n < x || n > y)
            continue;
         m = &message[n-1];
         m->m_xsize = 0;
      } else
         continue;

      if ((cp = asccasestr(responded_other_text, "FLAGS ")) != NULL) {
         cp += 5;
         while (*cp == ' ')
            cp++;
         if (*cp == '(')
            imap_getflags(cp, &cp, &m->m_flag);
      }

      if ((cp = asccasestr(responded_other_text, "UID ")) != NULL)
         n_idec_ui64_cp(&m->m_uid, &cp[4], 10, NULL);/* TODO errors? */
      getcache1(mp, m, NEED_UNSPEC, 1);
      m->m_flag &= ~MHIDDEN;
   }

   while (x <= y && message[x-1].m_xsize && message[x-1].m_time)
      x++;
   while (y > x && message[y-1].m_xsize && message[y-1].m_time)
      y--;
   if (x <= y) {
      snprintf(o, sizeof o, "%s FETCH %u:%u (RFC822.SIZE INTERNALDATE)\r\n",
         tag(1), x, y);
      IMAP_OUT(o, MB_COMD, return STOP)
      while (mp->mb_active & MB_COMD) {
         imap_answer(mp, 1);
         if (response_status == RESPONSE_OTHER &&
               response_other == MESSAGE_DATA_FETCH) {
            n = responded_other_number;
            if (n < x || n > y)
               continue;
            m = &message[n-1];
         } else
            continue;
         if ((cp = asccasestr(responded_other_text, "RFC822.SIZE ")) != NULL)
            m->m_xsize = strtol(&cp[12], NULL, 10);
         if ((cp = asccasestr(responded_other_text, "INTERNALDATE ")) != NULL)
            m->m_time = imap_read_date_time(&cp[13]);
      }
   }

   srelax_hold();
   for (n = X; n <= Y; ++n) {
      putcache(mp, &message[n-1]);
      srelax();
   }
   srelax_rele();
   return OKAY;
}

static void
imap_init(struct mailbox *mp, int n)
{
   struct message *m;
   n_NYD_IN;
   n_UNUSED(mp);

   m = message + n;
   m->m_flag = MUSED | MNOFROM;
   m->m_block = 0;
   m->m_offset = 0;
   n_NYD_OU;
}

static void
imap_setptr(struct mailbox *mp, int nmail, int transparent, int *prevcount)
{
   struct message *omessage = 0;
   int i, omsgCount = 0;
   enum okay dequeued = STOP;
   n_NYD_IN;

   if (nmail || transparent) {
      omessage = message;
      omsgCount = msgCount;
   }
   if (nmail)
      dequeued = rec_dequeue();

   if (had_exists >= 0) {
      if (dequeued != OKAY)
         msgCount = had_exists;
      had_exists = -1;
   }
   if (had_expunge >= 0) {
      if (dequeued != OKAY)
         msgCount -= had_expunge;
      had_expunge = -1;
   }

   if (nmail && expunged_messages)
      printf("Expunged %ld message%s.\n", expunged_messages,
         (expunged_messages != 1 ? "s" : ""));
   *prevcount = omsgCount - expunged_messages;
   expunged_messages = 0;
   if (msgCount < 0) {
      fputs("IMAP error: Negative message count\n", stderr);
      msgCount = 0;
   }

   if (dequeued != OKAY) {
      message = n_calloc(msgCount + 1, sizeof *message);
      for (i = 0; i < msgCount; i++)
         imap_init(mp, i);
      if (!nmail && mp->mb_type == MB_IMAP)
         initcache(mp);
      if (msgCount > 0)
         imap_flags(mp, 1, msgCount);
      message[msgCount].m_size = 0;
      message[msgCount].m_lines = 0;
      rec_rmqueue();
   }
   if (nmail || transparent)
      transflags(omessage, omsgCount, transparent);
   else
      setdot(message);
   n_NYD_OU;
}

FL int
imap_setfile(char const * volatile who, const char *xserver,
   enum fedit_mode fm)
{
   struct url url;
   int rv;
   n_NYD_IN;

   if (!url_parse(&url, CPROTO_IMAP, xserver)) {
      rv = 1;
      goto jleave;
   }
   if (!ok_blook(v15_compat) &&
         (!(url.url_flags & n_URL_HAD_USER) || url.url_pass.s != NULL))
      n_err(_("New-style URL used without *v15-compat* being set!\n"));

   _imap_rdonly = ((fm & FEDIT_RDONLY) != 0);
   rv = _imap_setfile1(who, &url, fm, 0);
jleave:
   n_NYD_OU;
   return rv;
}

static bool_t
_imap_getcred(struct mailbox *mbp, struct ccred *ccredp, struct url *urlp)
{
   bool_t rv = FAL0;
   n_NYD_IN;

   if (ok_blook(v15_compat))
      rv = ccred_lookup(ccredp, urlp);
   else {
      char *var, *old,
         *xuhp = ((urlp->url_flags & n_URL_HAD_USER) ? urlp->url_eu_h_p.s
               : urlp->url_u_h_p.s);

      if ((var = mbp->mb_imap_pass) != NULL) {
         var = savecat("password-", xuhp);
         if ((old = n_UNCONST(n_var_vlook(var, FAL0))) != NULL)
            old = sstrdup(old);
         n_var_vset(var, (uintptr_t)mbp->mb_imap_pass);
      }
      rv = ccred_lookup_old(ccredp, CPROTO_IMAP, xuhp);
      if (var != NULL) {
         if (old != NULL) {
            n_var_vset(var, (uintptr_t)old);
            n_free(old);
         } else
            n_var_vclear(var);
      }
   }

   n_NYD_OU;
   return rv;
}

static int
_imap_setfile1(char const * volatile who, struct url *urlp,
   enum fedit_mode volatile fm, int volatile transparent)
{
   struct sock so;
   struct ccred ccred;
   sighandler_type volatile saveint, savepipe;
   char const *cp;
   int rv;
   int volatile prevcount = 0;
   enum mbflags same_flags;
   n_NYD_IN;

   if (fm & FEDIT_NEWMAIL) {
      saveint = safe_signal(SIGINT, SIG_IGN);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if (saveint != SIG_IGN)
         safe_signal(SIGINT, imapcatch);
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);
      imaplock = 1;
      goto jnmail;
   }

   same_flags = mb.mb_flags;
   same_imap_account = 0;
   if (mb.mb_imap_account != NULL &&
         (mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE)) {
      if (mb.mb_sock.s_fd > 0 && mb.mb_sock.s_rsz >= 0 &&
            !strcmp(mb.mb_imap_account, urlp->url_p_eu_h_p) &&
            disconnected(mb.mb_imap_account) == 0) {
         same_imap_account = 1;
         if (urlp->url_pass.s == NULL && mb.mb_imap_pass != NULL)
/*
            goto jduppass;
      } else if ((transparent || mb.mb_type == MB_CACHE) &&
            !strcmp(mb.mb_imap_account, urlp->url_p_eu_h_p) &&
            urlp->url_pass.s == NULL && mb.mb_imap_pass != NULL)
jduppass:
*/
         urlp->url_pass.l = strlen(urlp->url_pass.s = savestr(mb.mb_imap_pass));
      }
   }

   if (!same_imap_account && mb.mb_imap_pass != NULL) {
      n_free(mb.mb_imap_pass);
      mb.mb_imap_pass = NULL;
   }
   if (!_imap_getcred(&mb, &ccred, urlp)) {
      rv = -1;
      goto jleave;
   }

   memset(&so, 0, sizeof so);
   so.s_fd = -1;
   if (!same_imap_account) {
      if (!disconnected(urlp->url_p_eu_h_p) && !sopen(&so, urlp)) {
         rv = -1;
         goto jleave;
      }
   } else
      so = mb.mb_sock;
   if (!transparent) {
      if(!quit(FAL0)){
         rv = -1;
         goto jleave;
      }
   }

   if (fm & FEDIT_SYSBOX)
      n_pstate &= ~n_PS_EDIT;
   else
      n_pstate |= n_PS_EDIT;
   if (mb.mb_imap_account != NULL)
      n_free(mb.mb_imap_account);
   if (mb.mb_imap_pass != NULL)
      n_free(mb.mb_imap_pass);
   mb.mb_imap_account = sstrdup(urlp->url_p_eu_h_p);
   /* TODO This is a hack to allow '@boxname'; in the end everything will be an
    * TODO object, and mailbox will naturally have an URL and credentials */
   mb.mb_imap_pass = sbufdup(ccred.cc_pass.s, ccred.cc_pass.l);

   if (!same_imap_account) {
      if (mb.mb_sock.s_fd >= 0)
         sclose(&mb.mb_sock);
   }
   same_imap_account = 0;

   if (!transparent) {
      if (mb.mb_itf) {
         fclose(mb.mb_itf);
         mb.mb_itf = NULL;
      }
      if (mb.mb_otf) {
         fclose(mb.mb_otf);
         mb.mb_otf = NULL;
      }
      if (mb.mb_imap_mailbox != NULL)
         n_free(mb.mb_imap_mailbox);
      assert(urlp->url_path.s != NULL);
      imap_delim_init(&mb, urlp);
      mb.mb_imap_mailbox = sstrdup(imap_path_normalize(&mb, urlp->url_path.s));
      initbox(savecatsep(urlp->url_p_eu_h_p,
         (mb.mb_imap_delim[0] != '\0' ? mb.mb_imap_delim[0] : n_IMAP_DELIM[0]),
         mb.mb_imap_mailbox));
   }
   mb.mb_type = MB_VOID;
   mb.mb_active = MB_NONE;

   imaplock = 1;
   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1)) {
      /* Not safe to use &so; save to use mb.mb_sock?? :-( TODO */
      sclose(&mb.mb_sock);
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      imaplock = 0;

      mb.mb_type = MB_VOID;
      mb.mb_active = MB_NONE;
      rv = (fm & (FEDIT_SYSBOX | FEDIT_NEWMAIL)) ? 1 : -1;
      goto jleave;
   }
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, imapcatch);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, imapcatch);

   if (mb.mb_sock.s_fd < 0) {
      if (disconnected(mb.mb_imap_account)) {
         if (cache_setptr(fm, transparent) == STOP)
            n_err(_("Mailbox \"%s\" is not cached\n"), urlp->url_p_eu_h_p_p);
         goto jdone;
      }
      if ((cp = xok_vlook(imap_keepalive, urlp, OXM_ALL)) != NULL) {
         if ((imapkeepalive = strtol(cp, NULL, 10)) > 0) {
            savealrm = safe_signal(SIGALRM, imapalarm);
            alarm(imapkeepalive);
         }
      }

      mb.mb_sock = so;
      mb.mb_sock.s_desc = "IMAP";
      mb.mb_sock.s_onclose = imap_timer_off;
      if (imap_preauth(&mb, urlp) != OKAY || imap_auth(&mb, &ccred) != OKAY) {
         sclose(&mb.mb_sock);
         imap_timer_off();
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         imaplock = 0;
         rv = (fm & (FEDIT_SYSBOX | FEDIT_NEWMAIL)) ? 1 : -1;
         goto jleave;
      }
   } else   /* same account */
      mb.mb_flags |= same_flags;

   if (n_poption & n_PO_R_FLAG)
      fm |= FEDIT_RDONLY;
   mb.mb_perm = (fm & FEDIT_RDONLY) ? 0 : MB_DELE;
   mb.mb_type = MB_IMAP;
   cache_dequeue(&mb);
   assert(urlp->url_path.s != NULL);
   if (imap_select(&mb, &mailsize, &msgCount, urlp->url_path.s, fm) != OKAY) {
      /*sclose(&mb.mb_sock);
      imap_timer_off();*/
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      imaplock = 0;
      mb.mb_type = MB_VOID;
      rv = (fm & (FEDIT_SYSBOX | FEDIT_NEWMAIL)) ? 1 : -1;
      goto jleave;
   }

jnmail:
   imap_setptr(&mb, ((fm & FEDIT_NEWMAIL) != 0), transparent,
      n_UNVOLATILE(&prevcount));
jdone:
   setmsize(msgCount);
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;

   if (!(fm & FEDIT_NEWMAIL) && mb.mb_type == MB_IMAP)
      purgecache(&mb, message, msgCount);
   if (((fm & FEDIT_NEWMAIL) || transparent) && mb.mb_sorted) {
      mb.mb_threaded = 0;
      c_sort((void*)-1);
   }

   if (!(fm & FEDIT_NEWMAIL) && !transparent) {
      n_pstate &= ~n_PS_SAW_COMMAND;
      n_pstate |= n_PS_SETFILE_OPENED;
   }

   if ((n_poption & n_PO_EXISTONLY) && (mb.mb_type == MB_IMAP ||
         mb.mb_type == MB_CACHE)) {
      rv = (msgCount == 0);
      goto jleave;
   }

   if (!(fm & FEDIT_NEWMAIL) && !(n_pstate & n_PS_EDIT) && msgCount == 0) {
      if ((mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE) &&
            !ok_blook(emptystart)){
         char const *intro;

         if(who == NULL)
            intro = who = n_empty;
         else
            intro = _(" for ");
         n_err(_("No mail%s%s at %s\n"), intro, who, urlp->url_p_eu_h_p_p);
      }
      rv = 1;
      goto jleave;
   }

   if (fm & FEDIT_NEWMAIL)
      newmailinfo(prevcount);
   rv = 0;
jleave:
   n_NYD_OU;
   return rv;
}

static int
imap_fetchdata(struct mailbox *mp, struct message *m, size_t expected,
   int need, const char *head, size_t headsize, long headlines)
{
   char *line = NULL, *lp;
   size_t linesize = 0, linelen, size = 0;
   int emptyline = 0, lines = 0, excess = 0;
   off_t offset;
   n_NYD_IN;

   fseek(mp->mb_otf, 0L, SEEK_END);
   offset = ftell(mp->mb_otf);

   if (head)
      fwrite(head, 1, headsize, mp->mb_otf);

   while (sgetline(&line, &linesize, &linelen, &mp->mb_sock) > 0) {
      lp = line;
      if (linelen > expected) {
         excess = linelen - expected;
         linelen = expected;
      }
      /* TODO >>
       * Need to mask 'From ' lines. This cannot be done properly
       * since some servers pass them as 'From ' and others as
       * '>From '. Although one could identify the first kind of
       * server in principle, it is not possible to identify the
       * second as '>From ' may also come from a server of the
       * first type as actual data. So do what is absolutely
       * necessary only - mask 'From '.
       *
       * If the line is the first line of the message header, it
       * is likely a real 'From ' line. In this case, it is just
       * ignored since it violates all standards.
       * TODO can the latter *really* happen??
       * TODO <<
       */
      /* Since we simply copy over data without doing any transfer
       * encoding reclassification/adjustment we *have* to perform
       * RFC 4155 compliant From_ quoting here */
      if (emptyline && is_head(lp, linelen, FAL0)) {
         fputc('>', mp->mb_otf);
         ++size;
      }
      emptyline = 0;
      if (lp[linelen-1] == '\n' && (linelen == 1 || lp[linelen-2] == '\r')) {
         if (linelen > 2) {
            fwrite(lp, 1, linelen - 2, mp->mb_otf);
            size += linelen - 1;
         } else {
            emptyline = 1;
            ++size;
         }
         fputc('\n', mp->mb_otf);
      } else {
         fwrite(lp, 1, linelen, mp->mb_otf);
         size += linelen;
      }
      ++lines;
      if ((expected -= linelen) <= 0)
         break;
   }
   if (!emptyline) {
      /* TODO This is very ugly; but some IMAP daemons don't end a
       * TODO message with \r\n\r\n, and we need \n\n for mbox format.
       * TODO That is to say we do it wrong here in order to get it right
       * TODO when send.c stuff or with MBOX handling, even though THIS
       * TODO line is solely a property of the MBOX database format! */
      fputc('\n', mp->mb_otf);
      ++lines;
      ++size;
   }
   fflush(mp->mb_otf);

   if (m != NULL) {
      m->m_size = size + headsize;
      m->m_lines = lines + headlines;
      m->m_block = mailx_blockof(offset);
      m->m_offset = mailx_offsetof(offset);
      switch (need) {
      case NEED_HEADER:
         m->m_content_info = CI_HAVE_HEADER;
         break;
      case NEED_BODY:
         m->m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
         m->m_xlines = m->m_lines;
         m->m_xsize = m->m_size;
         break;
      }
   }
   n_free(line);
   n_NYD_OU;
   return excess;
}

static void
imap_putstr(struct mailbox *mp, struct message *m, const char *str,
   const char *head, size_t headsize, long headlines)
{
   off_t offset;
   size_t len;
   n_NYD_IN;

   len = strlen(str);
   fseek(mp->mb_otf, 0L, SEEK_END);
   offset = ftell(mp->mb_otf);
   if (head)
      fwrite(head, 1, headsize, mp->mb_otf);
   if (len > 0) {
      fwrite(str, 1, len, mp->mb_otf);
      fputc('\n', mp->mb_otf);
      ++len;
   }
   fflush(mp->mb_otf);

   if (m != NULL) {
      m->m_size = headsize + len;
      m->m_lines = headlines + 1;
      m->m_block = mailx_blockof(offset);
      m->m_offset = mailx_offsetof(offset);
      m->m_content_info |= CI_HAVE_HEADER | CI_HAVE_BODY;
      m->m_xlines = m->m_lines;
      m->m_xsize = m->m_size;
   }
   n_NYD_OU;
}

static enum okay
imap_get(struct mailbox *mp, struct message *m, enum needspec need)
{
   char o[LINESIZE];
   struct message mt;
   sighandler_type volatile saveint, savepipe;
   char * volatile head;
   char const *cp, *loc, * volatile item, * volatile resp;
   size_t expected;
   size_t volatile headsize;
   int number;
   FILE *queuefp;
   long volatile headlines;
   long n;
   enum okay ok;
   n_NYD_X;

   saveint = savepipe = SIG_IGN;
   head = NULL;
   cp = loc = item = resp = NULL;
   headsize = 0;
   number = (int)PTR2SIZE(m - message + 1);
   queuefp = NULL;
   headlines = 0;
   ok = STOP;

   if (getcache(mp, m, need) == OKAY)
      return OKAY;
   if (mp->mb_type == MB_CACHE) {
      n_err(_("Message %lu not available\n"), (ul_i)number);
      return STOP;
   }

   if (mp->mb_sock.s_fd < 0) {
      n_err(_("IMAP connection closed\n"));
      return STOP;
   }

   switch (need) {
   case NEED_HEADER:
      resp = item = "RFC822.HEADER";
      break;
   case NEED_BODY:
      item = "BODY.PEEK[]";
      resp = "BODY[]";
      if ((m->m_content_info & CI_HAVE_HEADER) && m->m_size) {
         char *hdr = n_alloc(m->m_size);
         fflush(mp->mb_otf);
         if (fseek(mp->mb_itf, (long)mailx_positionof(m->m_block, m->m_offset),
               SEEK_SET) < 0 ||
               fread(hdr, 1, m->m_size, mp->mb_itf) != m->m_size) {
            n_free(hdr);
            break;
         }
         head = hdr;
         headsize = m->m_size;
         headlines = m->m_lines;
         item = "BODY.PEEK[TEXT]";
         resp = "BODY[TEXT]";
      }
      break;
   case NEED_UNSPEC:
      return STOP;
   }

   imaplock = 1;
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1)) {
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      imaplock = 0;
      return STOP;
   }
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, imapcatch);

   if (m->m_uid)
      snprintf(o, sizeof o, "%s UID FETCH %" PRIu64 " (%s)\r\n",
         tag(1), m->m_uid, item);
   else {
      if (check_expunged() == STOP)
         goto out;
      snprintf(o, sizeof o, "%s FETCH %u (%s)\r\n", tag(1), number, item);
   }
   IMAP_OUT(o, MB_COMD, goto out)
   for (;;) {
      ui64_t uid;

      ok = imap_answer(mp, 1);
      if (ok == STOP)
         break;
      if (response_status != RESPONSE_OTHER ||
            response_other != MESSAGE_DATA_FETCH)
         continue;
      if ((loc = asccasestr(responded_other_text, resp)) == NULL)
         continue;
      uid = 0;
      if (m->m_uid) {
         if ((cp = asccasestr(responded_other_text, "UID "))) {
            n_idec_ui64_cp(&uid, &cp[4], 10, NULL);/* TODO errors? */
            n = 0;
         } else
            n = -1;
      } else
         n = responded_other_number;
      if ((cp = strrchr(responded_other_text, '{')) == NULL) {
         if (m->m_uid ? m->m_uid != uid : n != number)
            continue;
         if ((cp = strchr(loc, '"')) != NULL) {
            cp = imap_unquotestr(cp);
            imap_putstr(mp, m, cp, head, headsize, headlines);
         } else {
            m->m_content_info |= CI_HAVE_HEADER | CI_HAVE_BODY;
            m->m_xlines = m->m_lines;
            m->m_xsize = m->m_size;
         }
         goto out;
      }
      expected = atol(&cp[1]);
      if (m->m_uid ? n == 0 && m->m_uid != uid : n != number) {
         imap_fetchdata(mp, NULL, expected, need, NULL, 0, 0);
         continue;
      }
      mt = *m;
      imap_fetchdata(mp, &mt, expected, need, head, headsize, headlines);
      if (n >= 0) {
         commitmsg(mp, m, &mt, mt.m_content_info);
         break;
      }
      if (n == -1 && sgetline(&imapbuf, &imapbufsize, NULL, &mp->mb_sock) > 0) {
         if (n_poption & n_PO_VERBVERB)
            fputs(imapbuf, stderr);
         if ((cp = asccasestr(imapbuf, "UID ")) != NULL) {
            n_idec_ui64_cp(&uid, &cp[4], 10, NULL);/* TODO errors? */
            if (uid == m->m_uid) {
               commitmsg(mp, m, &mt, mt.m_content_info);
               break;
            }
         }
      }
   }
out:
   while (mp->mb_active & MB_COMD)
      ok = imap_answer(mp, 1);

   if (saveint != SIG_IGN)
      safe_signal(SIGINT, saveint);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, savepipe);
   imaplock--;

   if (ok == OKAY)
      putcache(mp, m);
   if (head != NULL)
      n_free(head);
   if (interrupts)
      n_go_onintr_for_imap();
   return ok;
}

FL enum okay
imap_header(struct message *m)
{
   enum okay rv;
   n_NYD_IN;

   rv = imap_get(&mb, m, NEED_HEADER);
   n_NYD_OU;
   return rv;
}


FL enum okay
imap_body(struct message *m)
{
   enum okay rv;
   n_NYD_IN;

   rv = imap_get(&mb, m, NEED_BODY);
   n_NYD_OU;
   return rv;
}

static void
commitmsg(struct mailbox *mp, struct message *tomp, struct message *frommp,
   enum content_info content_info)
{
   n_NYD_IN;
   tomp->m_size = frommp->m_size;
   tomp->m_lines = frommp->m_lines;
   tomp->m_block = frommp->m_block;
   tomp->m_offset = frommp->m_offset;
   tomp->m_content_info = content_info & CI_HAVE_MASK;
   if (content_info & CI_HAVE_BODY) {
      tomp->m_xlines = frommp->m_lines;
      tomp->m_xsize = frommp->m_size;
   }
   putcache(mp, tomp);
   n_NYD_OU;
}

static enum okay
imap_fetchheaders(struct mailbox *mp, struct message *m, int bot, int topp)
{
   /* bot > topp */
   char o[LINESIZE];
   char const *cp;
   struct message mt;
   size_t expected;
   int n = 0;
   FILE *queuefp = NULL;
   enum okay ok;
   n_NYD_X;

   if (m[bot].m_uid)
      snprintf(o, sizeof o,
         "%s UID FETCH %" PRIu64 ":%" PRIu64 " (RFC822.HEADER)\r\n",
         tag(1), m[bot-1].m_uid, m[topp-1].m_uid);
   else {
      if (check_expunged() == STOP)
         return STOP;
      snprintf(o, sizeof o, "%s FETCH %u:%u (RFC822.HEADER)\r\n",
         tag(1), bot, topp);
   }
   IMAP_OUT(o, MB_COMD, return STOP)

   srelax_hold();
   for (;;) {
      ok = imap_answer(mp, 1);
      if (response_status != RESPONSE_OTHER)
         break;
      if (response_other != MESSAGE_DATA_FETCH)
         continue;
      if (ok == STOP || (cp=strrchr(responded_other_text, '{')) == 0) {
         srelax_rele();
         return STOP;
      }
      if (asccasestr(responded_other_text, "RFC822.HEADER") == NULL)
         continue;
      expected = atol(&cp[1]);
      if (m[bot-1].m_uid) {
         if ((cp = asccasestr(responded_other_text, "UID ")) != NULL) {
            ui64_t uid;

            n_idec_ui64_cp(&uid, &cp[4], 10, NULL);/* TODO errors? */
            for (n = bot; n <= topp; n++)
               if (uid == m[n-1].m_uid)
                  break;
            if (n > topp) {
               imap_fetchdata(mp, NULL, expected, NEED_HEADER, NULL, 0, 0);
               continue;
            }
         } else
            n = -1;
      } else {
         n = responded_other_number;
         if (n <= 0 || n > msgCount) {
            imap_fetchdata(mp, NULL, expected, NEED_HEADER, NULL, 0, 0);
            continue;
         }
      }
      imap_fetchdata(mp, &mt, expected, NEED_HEADER, NULL, 0, 0);
      if (n >= 0 && !(m[n-1].m_content_info & CI_HAVE_HEADER))
         commitmsg(mp, &m[n-1], &mt, CI_HAVE_HEADER);
      if (n == -1 && sgetline(&imapbuf, &imapbufsize, NULL, &mp->mb_sock) > 0) {
         if (n_poption & n_PO_VERBVERB)
            fputs(imapbuf, stderr);
         if ((cp = asccasestr(imapbuf, "UID ")) != NULL) {
            ui64_t uid;

            n_idec_ui64_cp(&uid, &cp[4], 10, NULL);/* TODO errors? */
            for (n = bot; n <= topp; n++)
               if (uid == m[n-1].m_uid)
                  break;
            if (n <= topp && !(m[n-1].m_content_info & CI_HAVE_HEADER))
               commitmsg(mp, &m[n-1], &mt, CI_HAVE_HEADER);
         }
      }
      srelax();
   }
   srelax_rele();

   while (mp->mb_active & MB_COMD)
      ok = imap_answer(mp, 1);
   return ok;
}

FL void
imap_getheaders(int volatile bot, int volatile topp) /* TODO iterator!! */
{
   sighandler_type saveint, savepipe;
   /*enum okay ok = STOP;*/
   int i, chunk = 256;
   n_NYD_X;

   if (mb.mb_type == MB_CACHE)
      return;
   if (bot < 1)
      bot = 1;
   if (topp > msgCount)
      topp = msgCount;
   for (i = bot; i < topp; i++) {
      if ((message[i-1].m_content_info & CI_HAVE_HEADER) ||
            getcache(&mb, &message[i-1], NEED_HEADER) == OKAY)
         bot = i+1;
      else
         break;
   }
   for (i = topp; i > bot; i--) {
      if ((message[i-1].m_content_info & CI_HAVE_HEADER) ||
            getcache(&mb, &message[i-1], NEED_HEADER) == OKAY)
         topp = i-1;
      else
         break;
   }
   if (bot >= topp)
      return;

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      for (i = bot; i <= topp; i += chunk) {
         int j = i + chunk - 1;
         j = n_MIN(j, topp);
         if (visible(message + j))
            /*ok = */imap_fetchheaders(&mb, message, i, j);
         if (interrupts)
            n_go_onintr_for_imap(); /* XXX imaplock? */
      }
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;
}

static enum okay
__imap_exit(struct mailbox *mp)
{
   char o[LINESIZE];
   FILE *queuefp = NULL;
   n_NYD_X;

   mp->mb_active |= MB_BYE;
   snprintf(o, sizeof o, "%s LOGOUT\r\n", tag(1));
   IMAP_OUT(o, MB_COMD, return STOP)
   IMAP_ANSWER()
   return OKAY;
}

static enum okay
imap_exit(struct mailbox *mp)
{
   enum okay rv;
   n_NYD_IN;

   rv = __imap_exit(mp);
#if 0 /* TODO the option today: memory leak(s) and halfway reuse or nottin */
   n_free(mp->mb_imap_pass);
   n_free(mp->mb_imap_account);
   n_free(mp->mb_imap_mailbox);
   if (mp->mb_cache_directory != NULL)
      n_free(mp->mb_cache_directory);
#ifndef mx_HAVE_DEBUG /* TODO ASSERT LEGACY */
   mp->mb_imap_account =
   mp->mb_imap_mailbox =
   mp->mb_cache_directory = "";
#else
   mp->mb_imap_account = NULL; /* for assert legacy time.. */
   mp->mb_imap_mailbox = NULL;
   mp->mb_cache_directory = NULL;
#endif
#endif
   sclose(&mp->mb_sock);
   n_NYD_OU;
   return rv;
}

static enum okay
imap_delete(struct mailbox *mp, int n, struct message *m, int needstat)
{
   n_NYD_IN;
   imap_store(mp, m, n, '+', "\\Deleted", needstat);
   if (mp->mb_type == MB_IMAP)
      delcache(mp, m);
   n_NYD_OU;
   return OKAY;
}

static enum okay
imap_close(struct mailbox *mp)
{
   char o[LINESIZE];
   FILE *queuefp = NULL;
   n_NYD_X;

   snprintf(o, sizeof o, "%s CLOSE\r\n", tag(1));
   IMAP_OUT(o, MB_COMD, return STOP)
   IMAP_ANSWER()
   return OKAY;
}

static enum okay
imap_update(struct mailbox *mp)
{
   struct message *m;
   int dodel, c, gotcha = 0, held = 0, modflags = 0, needstat, stored = 0;
   n_NYD_IN;

   if (!(n_pstate & n_PS_EDIT) && mp->mb_perm != 0) {
      holdbits();
      c = 0;
      for (m = message; PTRCMP(m, <, message + msgCount); ++m)
         if (m->m_flag & MBOX)
            ++c;
      if (c > 0)
         if (makembox() == STOP)
            goto jbypass;
   }

   gotcha = held = 0;
   for (m = message; PTRCMP(m, <, message + msgCount); ++m) {
      if (mp->mb_perm == 0)
         dodel = 0;
      else if (n_pstate & n_PS_EDIT)
         dodel = ((m->m_flag & MDELETED) != 0);
      else
         dodel = !((m->m_flag & MPRESERVE) || !(m->m_flag & MTOUCH));

      /* Fetch the result after around each 800 STORE commands
       * sent (approx. 32k data sent). Otherwise, servers will
       * try to flush the return queue at some point, leading
       * to a deadlock if we are still writing commands but not
       * reading their results */
      needstat = stored > 0 && stored % 800 == 0;
      /* Even if this message has been deleted, continue
       * to set further flags. This is necessary to support
       * Gmail semantics, where "delete" actually means
       * "archive", and the flags are applied to the copy
       * in "All Mail" */
      if ((m->m_flag & (MREAD | MSTATUS)) == (MREAD | MSTATUS)) {
         imap_store(mp, m, m-message+1, '+', "\\Seen", needstat);
         stored++;
      }
      if (m->m_flag & MFLAG) {
         imap_store(mp, m, m-message+1, '+', "\\Flagged", needstat);
         stored++;
      }
      if (m->m_flag & MUNFLAG) {
         imap_store(mp, m, m-message+1, '-', "\\Flagged", needstat);
         stored++;
      }
      if (m->m_flag & MANSWER) {
         imap_store(mp, m, m-message+1, '+', "\\Answered", needstat);
         stored++;
      }
      if (m->m_flag & MUNANSWER) {
         imap_store(mp, m, m-message+1, '-', "\\Answered", needstat);
         stored++;
      }
      if (m->m_flag & MDRAFT) {
         imap_store(mp, m, m-message+1, '+', "\\Draft", needstat);
         stored++;
      }
      if (m->m_flag & MUNDRAFT) {
         imap_store(mp, m, m-message+1, '-', "\\Draft", needstat);
         stored++;
      }

      if (dodel) {
         imap_delete(mp, m-message+1, m, needstat);
         stored++;
         gotcha++;
      } else if (mp->mb_type != MB_CACHE ||
            (!(n_pstate & n_PS_EDIT) &&
             !(m->m_flag & (MBOXED | MSAVED | MDELETED))) ||
            (m->m_flag & (MBOXED | MPRESERVE | MTOUCH)) ==
               (MPRESERVE | MTOUCH) ||
               ((n_pstate & n_PS_EDIT) && !(m->m_flag & MDELETED)))
         held++;
      if (m->m_flag & MNEW) {
         m->m_flag &= ~MNEW;
         m->m_flag |= MSTATUS;
      }
   }
jbypass:
   if (gotcha)
      imap_close(mp);

   for (m = &message[0]; PTRCMP(m, <, message + msgCount); ++m)
      if (!(m->m_flag & MUNLINKED) &&
            m->m_flag & (MBOXED | MDELETED | MSAVED | MSTATUS | MFLAG |
               MUNFLAG | MANSWER | MUNANSWER | MDRAFT | MUNDRAFT)) {
         putcache(mp, m);
         modflags++;
      }

   /* XXX should be readonly (but our IMAP code is weird...) */
   if (!(n_poption & (n_PO_EXISTONLY | n_PO_HEADERSONLY | n_PO_HEADERLIST)) &&
         mb.mb_perm != 0) {
      if ((gotcha || modflags) && (n_pstate & n_PS_EDIT)) {
         printf(_("\"%s\" "), displayname);
         printf((ok_blook(bsdcompat) || ok_blook(bsdmsgs))
            ? _("complete\n") : _("updated.\n"));
      } else if (held && !(n_pstate & n_PS_EDIT)) {
         if (held == 1)
            printf(_("Held 1 message in %s\n"), displayname);
         else
            printf(_("Held %d messages in %s\n"), held, displayname);
      }
      fflush(stdout);
   }
   n_NYD_OU;
   return OKAY;
}

FL bool_t
imap_quit(bool_t hold_sigs_on)
{
   sighandler_type volatile saveint, savepipe;
   bool_t rv;
   n_NYD_IN;

   if(hold_sigs_on)
      rele_sigs();

   if (mb.mb_type == MB_CACHE) {
      rv = (imap_update(&mb) == OKAY);
      goto jleave;
   }

   rv = FAL0;

   if (mb.mb_sock.s_fd < 0) {
      n_err(_("IMAP connection closed\n"));
      goto jleave;
   }

   imaplock = 1;
   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1)) {
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, saveint);
      imaplock = 0;
      goto jleave;
   }
   if (saveint != SIG_IGN)
      safe_signal(SIGINT, imapcatch);
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, imapcatch);

   rv = (imap_update(&mb) == OKAY);
   if(!same_imap_account && imap_exit(&mb) != OKAY)
      rv = FAL0;

   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;
jleave:
   if(hold_sigs_on)
      hold_sigs();
   n_NYD_OU;
   return rv;
}

static enum okay
imap_store(struct mailbox *mp, struct message *m, int n, int c, const char *sp,
   int needstat)
{
   char o[LINESIZE];
   FILE *queuefp = NULL;
   n_NYD_X;

   if (mp->mb_type == MB_CACHE && (queuefp = cache_queue(mp)) == NULL)
      return STOP;
   if (m->m_uid)
      snprintf(o, sizeof o, "%s UID STORE %" PRIu64 " %cFLAGS (%s)\r\n",
         tag(1), m->m_uid, c, sp);
   else {
      if (check_expunged() == STOP)
         return STOP;
      snprintf(o, sizeof o, "%s STORE %u %cFLAGS (%s)\r\n", tag(1), n, c, sp);
   }
   IMAP_OUT(o, MB_COMD, return STOP)
   if (needstat)
      IMAP_ANSWER()
   else
      mb.mb_active &= ~MB_COMD;
   if (queuefp != NULL)
      Fclose(queuefp);
   return OKAY;
}

FL enum okay
imap_undelete(struct message *m, int n)
{
   enum okay rv;
   n_NYD_IN;

   rv = imap_unstore(m, n, "\\Deleted");
   n_NYD_OU;
   return rv;
}

FL enum okay
imap_unread(struct message *m, int n)
{
   enum okay rv;
   n_NYD_IN;

   rv = imap_unstore(m, n, "\\Seen");
   n_NYD_OU;
   return rv;
}

static enum okay
imap_unstore(struct message *m, int n, const char *flag)
{
   sighandler_type saveint, savepipe;
   enum okay volatile rv = STOP;
   n_NYD_IN;

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      rv = imap_store(&mb, m, n, '-', flag, 1);
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;

   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}

static const char *
tag(int new)
{
   static char ts[20];
   static long n;
   n_NYD2_IN;

   if (new)
      ++n;
   snprintf(ts, sizeof ts, "T%lu", n);
   n_NYD2_OU;
   return ts;
}

FL int
c_imapcodec(void *vp){
   bool_t err;
   size_t alen;
   char const **argv, *varname, *varres, *act, *cp;
   n_NYD_IN;

   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;

   act = *argv;
   for(cp = act; *cp != '\0' && !blankspacechar(*cp); ++cp)
      ;
   if(act == cp)
      goto jesynopsis;
   alen = PTR2SIZE(cp - act);
   if(*cp != '\0')
      ++cp;

   n_pstate_err_no = su_ERR_NONE;
   varres = imap_path_normalize(NULL, cp);

   if(is_ascncaseprefix(act, "encode", alen))
      varres = imap_path_encode(varres, &err);
   else if(is_ascncaseprefix(act, "decode", alen))
      varres = imap_path_decode(varres, &err);
   else
      goto jesynopsis;

   if(err){
      n_pstate_err_no = su_ERR_CANCELED;
      varres = cp;
      vp = NULL;
   }

   if(varname != NULL){
      if(!n_var_vset(varname, (uintptr_t)varres)){
         n_pstate_err_no = su_ERR_NOTSUP;
         vp = NULL;
      }
   }else{
      struct str in, out;

      in.l = strlen(in.s = n_UNCONST(varres));
      makeprint(&in, &out);
      if(fprintf(n_stdout, "%s\n", out.s) < 0){
         n_pstate_err_no = su_err_no();
         vp = NULL;
      }
      n_free(out.s);
   }

jleave:
   n_NYD_OU;
   return (vp != NULL ? 0 : 1);
jesynopsis:
   n_err(_("Synopsis: imapcodec: <e[ncode]|d[ecode]> <rest-of-line>\n"));
   n_pstate_err_no = su_ERR_INVAL;
   vp = NULL;
   goto jleave;
}

FL int
c_imap_imap(void *vp)
{
   char o[LINESIZE];
   sighandler_type saveint, savepipe;
   struct mailbox *mp = &mb;
   FILE *queuefp = NULL;
   enum okay volatile ok = STOP;
   n_NYD_X;

   if (mp->mb_type != MB_IMAP) {
      printf("Not operating on an IMAP mailbox.\n");
      return 1;
   }
   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      snprintf(o, sizeof o, "%s %s\r\n", tag(1), (char *)vp);
      IMAP_OUT(o, MB_COMD, goto out)
      while (mp->mb_active & MB_COMD) {
         ok = imap_answer(mp, 0);
         fputs(responded_text, stdout);
      }
   }
out:
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;

   if (interrupts)
      n_go_onintr_for_imap();
   return ok != OKAY;
}

FL int
imap_newmail(int nmail)
{
   n_NYD_IN;

   if (nmail && had_exists < 0 && had_expunge < 0) {
      imaplock = 1;
      imap_noop();
      imaplock = 0;
   }

   if (had_exists == msgCount && had_expunge < 0)
      /* Some servers always respond with EXISTS to NOOP. If
       * the mailbox has been changed but the number of messages
       * has not, an EXPUNGE must also had been sent; otherwise,
       * nothing has changed */
      had_exists = -1;
   n_NYD_OU;
   return (had_expunge >= 0 ? 2 : (had_exists >= 0 ? 1 : 0));
}

static char *
imap_putflags(int f)
{
   const char *cp;
   char *buf, *bp;
   n_NYD2_IN;

   bp = buf = n_autorec_alloc(100);
   if (f & (MREAD | MFLAGGED | MANSWERED | MDRAFTED)) {
      *bp++ = '(';
      if (f & MREAD) {
         if (bp[-1] != '(')
            *bp++ = ' ';
         for (cp = "\\Seen"; *cp; cp++)
            *bp++ = *cp;
      }
      if (f & MFLAGGED) {
         if (bp[-1] != '(')
            *bp++ = ' ';
         for (cp = "\\Flagged"; *cp; cp++)
            *bp++ = *cp;
      }
      if (f & MANSWERED) {
         if (bp[-1] != '(')
            *bp++ = ' ';
         for (cp = "\\Answered"; *cp; cp++)
            *bp++ = *cp;
      }
      if (f & MDRAFT) {
         if (bp[-1] != '(')
            *bp++ = ' ';
         for (cp = "\\Draft"; *cp; cp++)
            *bp++ = *cp;
      }
      *bp++ = ')';
      *bp++ = ' ';
   }
   *bp = '\0';
   n_NYD2_OU;
   return buf;
}

static void
imap_getflags(const char *cp, char const **xp, enum mflag *f)
{
   n_NYD2_IN;
   while (*cp != ')') {
      if (*cp == '\\') {
         if (ascncasecmp(cp, "\\Seen", 5) == 0)
            *f |= MREAD;
         else if (ascncasecmp(cp, "\\Recent", 7) == 0)
            *f |= MNEW;
         else if (ascncasecmp(cp, "\\Deleted", 8) == 0)
            *f |= MDELETED;
         else if (ascncasecmp(cp, "\\Flagged", 8) == 0)
            *f |= MFLAGGED;
         else if (ascncasecmp(cp, "\\Answered", 9) == 0)
            *f |= MANSWERED;
         else if (ascncasecmp(cp, "\\Draft", 6) == 0)
            *f |= MDRAFTED;
      }
      cp++;
   }

   if (xp != NULL)
      *xp = cp;
   n_NYD2_OU;
}

static enum okay
imap_append1(struct mailbox *mp, const char *name, FILE *fp, off_t off1,
   long xsize, enum mflag flag, time_t t)
{
   char o[LINESIZE], *buf;
   size_t bufsize, buflen, cnt;
   long size, lines, ysize;
   char const *qname;
   bool_t twice;
   FILE *queuefp;
   enum okay rv;
   n_NYD_IN;

   rv = STOP;
   queuefp = NULL;
   twice = FAL0;
   buf = NULL;

   if((qname = imap_path_quote(mp, name)) == NULL)
      goto jleave;

   if (mp->mb_type == MB_CACHE) {
      queuefp = cache_queue(mp);
      if (queuefp == NULL) {
         buf = NULL;
         goto jleave;
      }
      rv = OKAY;
   }

   buf = n_alloc(bufsize = LINESIZE);
   buflen = 0;
jagain:
   size = xsize;
   cnt = fsize(fp);
   if (fseek(fp, off1, SEEK_SET) < 0) {
      rv = STOP;
      goto jleave;
   }

   snprintf(o, sizeof o, "%s APPEND %s %s%s {%ld}\r\n",
         tag(1), qname, imap_putflags(flag), imap_make_date_time(t), size);
   IMAP_XOUT(o, MB_COMD, goto jleave, rv=STOP;goto jleave)
   while (mp->mb_active & MB_COMD) {
      rv = imap_answer(mp, twice);
      if (response_type == RESPONSE_CONT)
         break;
   }

   if (mp->mb_type != MB_CACHE && rv == STOP) {
      if (!twice)
         goto jtrycreate;
      else
         goto jleave;
   }

   lines = ysize = 0;
   while (size > 0) {
      fgetline(&buf, &bufsize, &cnt, &buflen, fp, 1);
      lines++;
      ysize += buflen;
      buf[buflen - 1] = '\r';
      buf[buflen] = '\n';
      if (mp->mb_type != MB_CACHE)
         swrite1(&mp->mb_sock, buf, buflen+1, 1);
      else if (queuefp)
         fwrite(buf, 1, buflen+1, queuefp);
      size -= buflen + 1;
   }
   if (mp->mb_type != MB_CACHE)
      swrite(&mp->mb_sock, "\r\n");
   else if (queuefp)
      fputs("\r\n", queuefp);
   while (mp->mb_active & MB_COMD) {
      rv = imap_answer(mp, 0);
      if (response_status == RESPONSE_NO /*&&
            ascncasecmp(responded_text,
               "[TRYCREATE] ", 12) == 0*/) {
jtrycreate:
         if (twice) {
            rv = STOP;
            goto jleave;
         }
         twice = TRU1;
         snprintf(o, sizeof o, "%s CREATE %s\r\n", tag(1), qname);
         IMAP_XOUT(o, MB_COMD, goto jleave, rv=STOP;goto jleave)
         while (mp->mb_active & MB_COMD)
            rv = imap_answer(mp, 1);
         if (rv == STOP)
            goto jleave;
         imap_created_mailbox++;
         goto jagain;
      } else if (rv != OKAY)
         n_err(_("IMAP error: %s"), responded_text);
      else if (response_status == RESPONSE_OK && (mp->mb_flags & MB_UIDPLUS))
         imap_appenduid(mp, fp, t, off1, xsize, ysize, lines, flag, name);
   }
jleave:
   if (queuefp != NULL)
      Fclose(queuefp);
   if (buf != NULL)
      n_free(buf);
   n_NYD_OU;
   return rv;
}

static enum okay
imap_append0(struct mailbox *mp, const char *name, FILE *fp, long offset)
{
   char *buf, *bp, *lp;
   size_t bufsize, buflen, cnt;
   off_t off1 = -1, offs;
   int flag;
   enum {_NONE = 0, _INHEAD = 1<<0, _NLSEP = 1<<1} state;
   time_t tim;
   long size;
   enum okay rv;
   n_NYD_IN;

   buf = n_alloc(bufsize = LINESIZE);
   buflen = 0;
   cnt = fsize(fp);
   offs = offset /* BSD will move due to O_APPEND! ftell(fp) */;
   time(&tim);
   size = 0;

   for (flag = MNEW, state = _NLSEP;;) {
      bp = fgetline(&buf, &bufsize, &cnt, &buflen, fp, 1);

      if (bp == NULL ||
            ((state & (_INHEAD | _NLSEP)) == _NLSEP &&
             is_head(buf, buflen, FAL0))) {
         if (off1 != (off_t)-1) {
            rv = imap_append1(mp, name, fp, off1, size, flag, tim);
            if (rv == STOP)
               goto jleave;
            fseek(fp, offs+buflen, SEEK_SET);
         }
         off1 = offs + buflen;
         size = 0;
         flag = MNEW;
         state = _INHEAD;
         if (bp == NULL)
            break;
         tim = unixtime(buf);
      } else
         size += buflen+1;
      offs += buflen;

      state &= ~_NLSEP;
      if (buf[0] == '\n') {
         state &= ~_INHEAD;
         state |= _NLSEP;
      } else if (state & _INHEAD) {
         if (ascncasecmp(buf, "status", 6) == 0) {
            lp = &buf[6];
            while (whitechar(*lp))
               lp++;
            if (*lp == ':')
               while (*++lp != '\0')
                  switch (*lp) {
                  case 'R':
                     flag |= MREAD;
                     break;
                  case 'O':
                     flag &= ~MNEW;
                     break;
                  }
         } else if (ascncasecmp(buf, "x-status", 8) == 0) {
            lp = &buf[8];
            while (whitechar(*lp))
               lp++;
            if (*lp == ':')
               while (*++lp != '\0')
                  switch (*lp) {
                  case 'F':
                     flag |= MFLAGGED;
                     break;
                  case 'A':
                     flag |= MANSWERED;
                     break;
                  case 'T':
                     flag |= MDRAFTED;
                     break;
                  }
         }
      }
   }
   rv = OKAY;
jleave:
   n_free(buf);
   n_NYD_OU;
   return rv;
}

FL enum okay
imap_append(const char *xserver, FILE *fp, long offset)
{
   sighandler_type volatile saveint, savepipe;
   struct url url;
   struct ccred ccred;
   enum okay rv = STOP;
   n_NYD_IN;

   if (!url_parse(&url, CPROTO_IMAP, xserver))
      goto j_leave;
   if (!ok_blook(v15_compat) &&
         (!(url.url_flags & n_URL_HAD_USER) || url.url_pass.s != NULL))
      n_err(_("New-style URL used without *v15-compat* being set!\n"));
   assert(url.url_path.s != NULL);

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1))
      goto jleave;
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, imapcatch);

   if ((mb.mb_type == MB_CACHE || mb.mb_sock.s_fd > 0) && mb.mb_imap_account &&
         !strcmp(url.url_p_eu_h_p, mb.mb_imap_account)) {
      rv = imap_append0(&mb, url.url_path.s, fp, offset);
   } else {
      struct mailbox mx;

      memset(&mx, 0, sizeof mx);

      if (!_imap_getcred(&mx, &ccred, &url))
         goto jleave;

      imap_delim_init(&mx, &url);
      mx.mb_imap_mailbox = sstrdup(imap_path_normalize(&mx, url.url_path.s));

      if (disconnected(url.url_p_eu_h_p) == 0) {
         if (!sopen(&mx.mb_sock, &url))
            goto jfail;
         mx.mb_sock.s_desc = "IMAP";
         mx.mb_type = MB_IMAP;
         mx.mb_imap_account = n_UNCONST(url.url_p_eu_h_p);
         /* TODO the code now did
          * TODO mx.mb_imap_mailbox = mbx->url.url_patth.s;
          * TODO though imap_mailbox is sfree()d and mbx
          * TODO is possibly even a constant
          * TODO i changed this to sstrdup() sofar, as is used
          * TODO somewhere else in this file for this! */
         if (imap_preauth(&mx, &url) != OKAY ||
               imap_auth(&mx, &ccred) != OKAY) {
            sclose(&mx.mb_sock);
            goto jfail;
         }
         rv = imap_append0(&mx, url.url_path.s, fp, offset);
         imap_exit(&mx);
      } else {
         mx.mb_imap_account = n_UNCONST(url.url_p_eu_h_p);
         mx.mb_type = MB_CACHE;
         rv = imap_append0(&mx, url.url_path.s, fp, offset);
      }
jfail:
      ;
   }

jleave:
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;
j_leave:
   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}

static enum okay
imap_list1(struct mailbox *mp, const char *base, struct list_item **list,
   struct list_item **lend, int level)
{
   char o[LINESIZE], *cp;
   struct list_item *lp;
   const char *qname, *bp;
   FILE *queuefp;
   enum okay ok;
   n_NYD_X;

   ok = STOP;
   queuefp = NULL;

   if((qname = imap_path_quote(mp, base)) == NULL)
      goto jleave;

   *list = *lend = NULL;
   snprintf(o, sizeof o, "%s LIST %s %%\r\n", tag(1), qname);
   IMAP_OUT(o, MB_COMD, goto jleave)
   while (mp->mb_active & MB_COMD) {
      ok = imap_answer(mp, 1);
      if (response_status == RESPONSE_OTHER &&
            response_other == MAILBOX_DATA_LIST && imap_parse_list() == OKAY) {
         cp = imap_path_decode(imap_unquotestr(list_name), NULL);
         lp = n_autorec_calloc(1, sizeof *lp);
         lp->l_name = cp;
         for (bp = base; *bp != '\0' && *bp == *cp; ++bp)
            ++cp;
         lp->l_base = *cp ? cp : savestr(base);
         lp->l_attr = list_attributes;
         lp->l_level = level+1;
         lp->l_delim = list_hierarchy_delimiter;
         if (*list && *lend) {
            (*lend)->l_next = lp;
            *lend = lp;
         } else
            *list = *lend = lp;
      }
   }
jleave:
   return ok;
}

static enum okay
imap_list(struct mailbox *mp, const char *base, int strip, FILE *fp)
{
   struct list_item *list, *lend, *lp, *lx, *ly;
   int n, depth;
   const char *bp;
   char *cp;
   enum okay rv;
   n_NYD_IN;

   depth = (cp = ok_vlook(imap_list_depth)) != NULL ? atoi(cp) : 2;
   if ((rv = imap_list1(mp, base, &list, &lend, 0)) == STOP)
      goto jleave;
   rv = OKAY;
   if (list == NULL || lend == NULL)
      goto jleave;

   for (lp = list; lp; lp = lp->l_next)
      if (lp->l_delim != '/' && lp->l_delim != EOF && lp->l_level < depth &&
            !(lp->l_attr & LIST_NOINFERIORS)) {
         cp = n_autorec_alloc((n = strlen(lp->l_name)) + 2);
         memcpy(cp, lp->l_name, n);
         cp[n] = lp->l_delim;
         cp[n+1] = '\0';
         if (imap_list1(mp, cp, &lx, &ly, lp->l_level) == OKAY && lx && ly) {
            lp->l_has_children = 1;
            if (strcmp(cp, lx->l_name) == 0)
               lx = lx->l_next;
            if (lx) {
               lend->l_next = lx;
               lend = ly;
            }
         }
      }

   for (lp = list; lp; lp = lp->l_next) {
      if (strip) {
         cp = lp->l_name;
         for (bp = base; *bp && *bp == *cp; bp++)
            cp++;
      } else
         cp = lp->l_name;
      if (!(lp->l_attr & LIST_NOSELECT))
         fprintf(fp, "%s\n", *cp ? cp : base);
      else if (lp->l_has_children == 0)
         fprintf(fp, "%s%c\n", *cp ? cp : base,
            (lp->l_delim != EOF ? lp->l_delim : '\n'));
   }
jleave:
   n_NYD_OU;
   return rv;
}

FL int
imap_folders(const char * volatile name, int strip)
{
   sighandler_type saveint, savepipe;
   const char * volatile fold, *cp, *sp;
   FILE * volatile fp;
   int rv = 1;
   n_NYD_IN;

   cp = protbase(name);
   sp = mb.mb_imap_account;
   if (sp == NULL || strcmp(cp, sp)) {
      n_err(
         _("Cannot perform `folders' but when on the very IMAP "
         "account; the current one is\n  `%s' -- "
         "try `folders @'\n"),
         (sp != NULL ? sp : _("[NONE]")));
      goto jleave;
   }

   fold = imap_fileof(name);
   if (n_psonce & n_PSO_TTYOUT) {
      if ((fp = Ftmp(NULL, "imapfold", OF_RDWR | OF_UNLINK | OF_REGISTER))
            == NULL) {
         n_perr(_("tmpfile"), 0);
         goto jleave;
      }
   } else
      fp = stdout;

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1)) /* TODO imaplock? */
      goto junroll;
   if (savepipe != SIG_IGN)
      safe_signal(SIGPIPE, imapcatch);

   if (mb.mb_type == MB_CACHE)
      cache_list(&mb, fold, strip, fp);
   else
      imap_list(&mb, fold, strip, fp);

   imaplock = 0;
   if (interrupts) {
      if (n_psonce & n_PSO_TTYOUT)
         Fclose(fp);
      rv = 0;
      goto jleave;
   }
   fflush(fp);

   if (n_psonce & n_PSO_TTYOUT) {
      rewind(fp);
      if (fsize(fp) > 0){
         page_or_print(fp, 0);
         rv = 0;
      }else
         n_err(_("Folder not found\n"));
   }else
      rv = 0;
junroll:
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   if (n_psonce & n_PSO_TTYOUT)
      Fclose(fp);
jleave:
   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}

static enum okay
imap_copy1(struct mailbox *mp, struct message *m, int n, const char *name)
{
   char o[LINESIZE];
   const char *qname;
   bool_t twice, stored;
   FILE *queuefp;
   enum okay ok;
   n_NYD_X;

   ok = STOP;
   queuefp = NULL;
   twice = stored = FAL0;

   /* C99 */{
      size_t i;

      i = strlen(name = imap_fileof(name));
      if(i == 0 || (i > 0 && name[i - 1] == '/'))
         name = savecat(name, "INBOX");
      if((qname = imap_path_quote(mp, name)) == NULL)
         goto jleave;
   }

   if (mp->mb_type == MB_CACHE) {
      if ((queuefp = cache_queue(mp)) == NULL)
         goto jleave;
      ok = OKAY;
   }

   /* Since it is not possible to set flags on the copy, recently
    * set flags must be set on the original to include it in the copy */
   if ((m->m_flag & (MREAD | MSTATUS)) == (MREAD | MSTATUS))
      imap_store(mp, m, n, '+', "\\Seen", 0);
   if (m->m_flag&MFLAG)
      imap_store(mp, m, n, '+', "\\Flagged", 0);
   if (m->m_flag&MUNFLAG)
      imap_store(mp, m, n, '-', "\\Flagged", 0);
   if (m->m_flag&MANSWER)
      imap_store(mp, m, n, '+', "\\Answered", 0);
   if (m->m_flag&MUNANSWER)
      imap_store(mp, m, n, '-', "\\Flagged", 0);
   if (m->m_flag&MDRAFT)
      imap_store(mp, m, n, '+', "\\Draft", 0);
   if (m->m_flag&MUNDRAFT)
      imap_store(mp, m, n, '-', "\\Draft", 0);
again:
   if (m->m_uid)
      snprintf(o, sizeof o, "%s UID COPY %" PRIu64 " %s\r\n",
         tag(1), m->m_uid, qname);
   else {
      if (check_expunged() == STOP)
         goto out;
      snprintf(o, sizeof o, "%s COPY %u %s\r\n", tag(1), n, qname);
   }
   IMAP_OUT(o, MB_COMD, goto out)
   while (mp->mb_active & MB_COMD)
      ok = imap_answer(mp, twice);

   if (mp->mb_type == MB_IMAP && mp->mb_flags & MB_UIDPLUS &&
         response_status == RESPONSE_OK)
      imap_copyuid(mp, m, name);

   if (response_status == RESPONSE_NO && !twice) {
      snprintf(o, sizeof o, "%s CREATE %s\r\n", tag(1), qname);
      IMAP_OUT(o, MB_COMD, goto out)
      while (mp->mb_active & MB_COMD)
         ok = imap_answer(mp, 1);
      if (ok == OKAY) {
         imap_created_mailbox++;
         goto again;
      }
   }

   if (queuefp != NULL)
      Fclose(queuefp);

   /* ... and reset the flag to its initial value so that the 'exit'
    * command still leaves the message unread */
out:
   if ((m->m_flag & (MREAD | MSTATUS)) == (MREAD | MSTATUS)) {
      imap_store(mp, m, n, '-', "\\Seen", 0);
      stored = TRU1;
   }
   if (m->m_flag & MFLAG) {
      imap_store(mp, m, n, '-', "\\Flagged", 0);
      stored = TRU1;
   }
   if (m->m_flag & MUNFLAG) {
      imap_store(mp, m, n, '+', "\\Flagged", 0);
      stored = TRU1;
   }
   if (m->m_flag & MANSWER) {
      imap_store(mp, m, n, '-', "\\Answered", 0);
      stored = TRU1;
   }
   if (m->m_flag & MUNANSWER) {
      imap_store(mp, m, n, '+', "\\Answered", 0);
      stored = TRU1;
   }
   if (m->m_flag & MDRAFT) {
      imap_store(mp, m, n, '-', "\\Draft", 0);
      stored = TRU1;
   }
   if (m->m_flag & MUNDRAFT) {
      imap_store(mp, m, n, '+', "\\Draft", 0);
      stored = TRU1;
   }
   if (stored) {
      mp->mb_active |= MB_COMD;
      (void)imap_finish(mp);
   }
jleave:
   return ok;
}

FL enum okay
imap_copy(struct message *m, int n, const char *name)
{
   sighandler_type saveint, savepipe;
   enum okay volatile rv = STOP;
   n_NYD_IN;

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      rv = imap_copy1(&mb, m, n, name);
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;

   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}

static enum okay
imap_copyuid_parse(const char *cp, ui64_t *uidvalidity, ui64_t *olduid,
   ui64_t *newuid)
{
   char const *xp, *yp, *zp;
   enum okay rv;
   n_NYD_IN;

   n_idec_ui64_cp(uidvalidity, cp, 10, &xp); /* TODO errors */
   n_idec_ui64_cp(olduid, xp, 10, &yp); /* TODO errors */
   n_idec_ui64_cp(newuid, yp, 10, &zp); /* TODO errors */
   rv = (*uidvalidity && *olduid && *newuid && xp > cp && *xp == ' ' &&
      yp > xp && *yp == ' ' && zp > yp && *zp == ']');
   n_NYD_OU;
   return rv;
}

static enum okay
imap_appenduid_parse(const char *cp, ui64_t *uidvalidity, ui64_t *uid)
{
   char const *xp, *yp;
   enum okay rv;
   n_NYD_IN;

   n_idec_ui64_cp(uidvalidity, cp, 10, &xp); /* TODO errors */
   n_idec_ui64_cp(uid, xp, 10, &yp); /* TODO errors */
   rv = (*uidvalidity && *uid && xp > cp && *xp == ' ' && yp > xp &&
      *yp == ']');
   n_NYD_OU;
   return rv;
}

static enum okay
imap_copyuid(struct mailbox *mp, struct message *m, const char *name)
{
   struct mailbox xmb;
   struct message xm;
   const char *cp;
   ui64_t uidvalidity, olduid, newuid;
   enum okay rv;
   n_NYD_IN;

   rv = STOP;

   memset(&xmb, 0, sizeof xmb);

   if ((cp = asccasestr(responded_text, "[COPYUID ")) == NULL ||
         imap_copyuid_parse(&cp[9], &uidvalidity, &olduid, &newuid) == STOP)
      goto jleave;

   rv = OKAY;

   xmb = *mp;
   xmb.mb_cache_directory = NULL;
   xmb.mb_imap_account = sstrdup(mp->mb_imap_account);
   xmb.mb_imap_pass = sstrdup(mp->mb_imap_pass);
   memcpy(&xmb.mb_imap_delim[0], &mp->mb_imap_delim[0],
      sizeof(xmb.mb_imap_delim));
   xmb.mb_imap_mailbox = sstrdup(imap_path_normalize(&xmb, name));
   if (mp->mb_cache_directory != NULL)
      xmb.mb_cache_directory = sstrdup(mp->mb_cache_directory);
   xmb.mb_uidvalidity = uidvalidity;
   initcache(&xmb);

   if (m == NULL) {
      memset(&xm, 0, sizeof xm);
      xm.m_uid = olduid;
      if ((rv = getcache1(mp, &xm, NEED_UNSPEC, 3)) != OKAY)
         goto jleave;
      getcache(mp, &xm, NEED_HEADER);
      getcache(mp, &xm, NEED_BODY);
   } else {
      if ((m->m_content_info & CI_HAVE_HEADER) == 0)
         getcache(mp, m, NEED_HEADER);
      if ((m->m_content_info & CI_HAVE_BODY) == 0)
         getcache(mp, m, NEED_BODY);
      xm = *m;
   }
   xm.m_uid = newuid;
   xm.m_flag &= ~MFULLYCACHED;
   putcache(&xmb, &xm);
jleave:
   if (xmb.mb_cache_directory != NULL)
      n_free(xmb.mb_cache_directory);
   if (xmb.mb_imap_mailbox != NULL)
      n_free(xmb.mb_imap_mailbox);
   if (xmb.mb_imap_pass != NULL)
      n_free(xmb.mb_imap_pass);
   if (xmb.mb_imap_account != NULL)
      n_free(xmb.mb_imap_account);
   n_NYD_OU;
   return rv;
}

static enum okay
imap_appenduid(struct mailbox *mp, FILE *fp, time_t t, long off1, long xsize,
   long size, long lines, int flag, const char *name)
{
   struct mailbox xmb;
   struct message xm;
   const char *cp;
   ui64_t uidvalidity, uid;
   enum okay rv;
   n_NYD_IN;

   rv = STOP;

   if ((cp = asccasestr(responded_text, "[APPENDUID ")) == NULL ||
         imap_appenduid_parse(&cp[11], &uidvalidity, &uid) == STOP)
      goto jleave;

   rv = OKAY;

   xmb = *mp;
   xmb.mb_cache_directory = NULL;
   /* XXX mb_imap_delim reused */
   xmb.mb_imap_mailbox = sstrdup(imap_path_normalize(&xmb, name));
   xmb.mb_uidvalidity = uidvalidity;
   xmb.mb_otf = xmb.mb_itf = fp;
   initcache(&xmb);
   memset(&xm, 0, sizeof xm);
   xm.m_flag = (flag & MREAD) | MNEW;
   xm.m_time = t;
   xm.m_block = mailx_blockof(off1);
   xm.m_offset = mailx_offsetof(off1);
   xm.m_size = size;
   xm.m_xsize = xsize;
   xm.m_lines = xm.m_xlines = lines;
   xm.m_uid = uid;
   xm.m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
   putcache(&xmb, &xm);

   n_free(xmb.mb_imap_mailbox);
jleave:
   n_NYD_OU;
   return rv;
}

static enum okay
imap_appenduid_cached(struct mailbox *mp, FILE *fp)
{
   FILE *tp = NULL;
   time_t t;
   long size, xsize, ysize, lines;
   enum mflag flag = MNEW;
   char *name, *buf, *bp;
   char const *cp;
   size_t bufsize, buflen, cnt;
   enum okay rv = STOP;
   n_NYD_IN;

   buf = n_alloc(bufsize = LINESIZE);
   buflen = 0;
   cnt = fsize(fp);
   if (fgetline(&buf, &bufsize, &cnt, &buflen, fp, 0) == NULL)
      goto jstop;

   for (bp = buf; *bp != ' '; ++bp) /* strip old tag */
      ;
   while (*bp == ' ')
      ++bp;

   if ((cp = strrchr(bp, '{')) == NULL)
      goto jstop;

   xsize = atol(&cp[1]) + 2;
   if ((name = imap_strex(&bp[7], &cp)) == NULL)
      goto jstop;
   while (*cp == ' ')
      cp++;

   if (*cp == '(') {
      imap_getflags(cp, &cp, &flag);
      while (*++cp == ' ')
         ;
   }
   t = imap_read_date_time(cp);

   if ((tp = Ftmp(NULL, "imapapui", OF_RDWR | OF_UNLINK | OF_REGISTER))
         == NULL)
      goto jstop;

   size = xsize;
   ysize = lines = 0;
   while (size > 0) {
      if (fgetline(&buf, &bufsize, &cnt, &buflen, fp, 0) == NULL)
         goto jstop;
      size -= buflen;
      buf[--buflen] = '\0';
      buf[buflen-1] = '\n';
      fwrite(buf, 1, buflen, tp);
      ysize += buflen;
      ++lines;
   }
   fflush(tp);
   rewind(tp);

   imap_appenduid(mp, tp, t, 0, xsize-2, ysize-1, lines-1, flag,
      imap_unquotestr(name));
   rv = OKAY;
jstop:
   n_free(buf);
   if (tp)
      Fclose(tp);
   n_NYD_OU;
   return rv;
}

#ifdef mx_HAVE_IMAP_SEARCH
static ssize_t
imap_search2(struct mailbox *mp, struct message *m, int cnt, const char *spec,
   int f)
{
   char *o, *cs, c;
   size_t n;
   FILE *queuefp = NULL;
   int i;
   const char *cp, *xp;
   ssize_t rv = -1;
   n_NYD_X;

   c = 0;
   for (cp = spec; *cp; cp++)
      c |= *cp;
   if (c & 0200) {
      cp = ok_vlook(ttycharset);
# ifdef mx_HAVE_ICONV
      if(asccasecmp(cp, "utf-8") && asccasecmp(cp, "utf8")){ /* XXX */
         char const *nspec;

         if((nspec = n_iconv_onetime_cp(n_ICONV_DEFAULT, "utf-8", cp, spec)
               ) != NULL){
            spec = nspec;
            cp = "utf-8";
         }
      }
# endif
      cp = imap_quotestr(cp);
      cs = n_lofi_alloc(n = strlen(cp) + 10);
      snprintf(cs, n, "CHARSET %s ", cp);
   } else
      cs = n_UNCONST(n_empty);

   o = n_lofi_alloc(n = strlen(spec) + 60);
   snprintf(o, n, "%s UID SEARCH %s%s\r\n", tag(1), cs, spec);
   IMAP_OUT(o, MB_COMD, goto out)
   /* C99 */{
      enum okay ok;

      for (rv = 0, ok = OKAY; (mp->mb_active & MB_COMD);) {
         if (imap_answer(mp, 0) != OKAY) {
            rv = -1;
            ok = STOP;
         }
         if (response_status == RESPONSE_OTHER &&
               response_other == MAILBOX_DATA_SEARCH) {
            xp = responded_other_text;
            while (*xp && *xp != '\r') {
               ui64_t uid;

               n_idec_ui64_cp(&uid, xp, 10, &xp);/* TODO errors? */
               for (i = 0; i < cnt; i++)
                  if (m[i].m_uid == uid && !(m[i].m_flag & MHIDDEN) &&
                        (f == MDELETED || !(m[i].m_flag & MDELETED))){
                     if(ok == OKAY){
                        mark(i+1, f);
                        ++rv;
                     }
                  }
            }
         }
      }
   }
out:
   n_lofi_free(o);
   if(cs != n_empty)
      n_lofi_free(cs);
   return rv;
}

FL ssize_t
imap_search1(const char * volatile spec, int f)
{
   sighandler_type saveint, savepipe;
   ssize_t volatile rv = -1;
   n_NYD_IN;

   if (mb.mb_type != MB_IMAP)
      goto jleave;

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      rv = imap_search2(&mb, message, msgCount, spec, f);
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;
jleave:
   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}
#endif /* mx_HAVE_IMAP_SEARCH */

FL int
imap_thisaccount(const char *cp)
{
   int rv;
   n_NYD_IN;

   if (mb.mb_type != MB_CACHE && mb.mb_type != MB_IMAP)
      rv = 0;
   else if ((mb.mb_type != MB_CACHE && mb.mb_sock.s_fd < 0) ||
         mb.mb_imap_account == NULL)
      rv = 0;
   else
      rv = !strcmp(protbase(cp), mb.mb_imap_account);
   n_NYD_OU;
   return rv;
}

FL enum okay
imap_remove(const char * volatile name)
{
   sighandler_type volatile saveint, savepipe;
   enum okay volatile rv = STOP;
   n_NYD_IN;

   if (mb.mb_type != MB_IMAP) {
      n_err(_("Refusing to remove \"%s\" in disconnected mode\n"), name);
      goto jleave;
   }

   if (!imap_thisaccount(name)) {
      n_err(_("Can only remove mailboxes on current IMAP server: "
         "\"%s\" not removed\n"), name);
      goto jleave;
   }

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      rv = imap_remove1(&mb, imap_fileof(name));
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;

   if (rv == OKAY)
      rv = cache_remove(name);
jleave:
   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}

static enum okay
imap_remove1(struct mailbox *mp, const char *name)
{
   char *o;
   int os;
   char const *qname;
   FILE *queuefp;
   enum okay ok;
   n_NYD_X;

   ok = STOP;
   queuefp = NULL;

   if((qname = imap_path_quote(mp, name)) != NULL){
      o = n_lofi_alloc(os = strlen(qname) + 100);
      snprintf(o, os, "%s DELETE %s\r\n", tag(1), qname);
      IMAP_OUT(o, MB_COMD, goto out)
      while (mp->mb_active & MB_COMD)
         ok = imap_answer(mp, 1);
out:
      n_lofi_free(o);
   }
   return ok;
}

FL enum okay
imap_rename(const char *old, const char *new)
{
   sighandler_type saveint, savepipe;
   enum okay volatile rv = STOP;
   n_NYD_IN;

   if (mb.mb_type != MB_IMAP) {
      n_err(_("Refusing to rename mailboxes in disconnected mode\n"));
      goto jleave;
   }

   if (!imap_thisaccount(old) || !imap_thisaccount(new)) {
      n_err(_("Can only rename mailboxes on current IMAP "
            "server: \"%s\" not renamed to \"%s\"\n"), old, new);
      goto jleave;
   }

   imaplock = 1;
   if ((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_imap_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if (sigsetjmp(imapjmp, 1) == 0) {
      if (savepipe != SIG_IGN)
         safe_signal(SIGPIPE, imapcatch);

      rv = imap_rename1(&mb, imap_fileof(old), imap_fileof(new));
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   imaplock = 0;

   if (rv == OKAY)
      rv = cache_rename(old, new);
jleave:
   n_NYD_OU;
   if (interrupts)
      n_go_onintr_for_imap();
   return rv;
}

static enum okay
imap_rename1(struct mailbox *mp, const char *old, const char *new)
{
   char *o;
   int os;
   char const *qoname, *qnname;
   FILE *queuefp;
   enum okay ok;
   n_NYD_X;

   ok = STOP;
   queuefp = NULL;

   if((qoname = imap_path_quote(mp, old)) != NULL &&
         (qnname = imap_path_quote(mp, new)) != NULL){
      o = n_lofi_alloc(os = strlen(qoname) + strlen(qnname) + 100);
      snprintf(o, os, "%s RENAME %s %s\r\n", tag(1), qoname, qnname);
      IMAP_OUT(o, MB_COMD, goto out)
      while (mp->mb_active & MB_COMD)
         ok = imap_answer(mp, 1);
out:
      n_lofi_free(o);
   }
   return ok;
}

FL enum okay
imap_dequeue(struct mailbox *mp, FILE *fp)
{
   char o[LINESIZE], *newname, *buf, *bp, *cp, iob[4096];
   size_t bufsize, buflen, cnt;
   long offs, offs1, offs2, octets;
   int twice, gotcha = 0;
   FILE *queuefp = NULL;
   enum okay ok = OKAY, rok = OKAY;
   n_NYD_X;

   buf = n_alloc(bufsize = LINESIZE);
   buflen = 0;
   cnt = fsize(fp);
   while ((offs1 = ftell(fp)) >= 0 &&
         fgetline(&buf, &bufsize, &cnt, &buflen, fp, 0) != NULL) {
      for (bp = buf; *bp != ' '; ++bp) /* strip old tag */
         ;
      while (*bp == ' ')
         ++bp;
      twice = 0;
      if ((offs = ftell(fp)) < 0)
         goto fail;
again:
      snprintf(o, sizeof o, "%s %s", tag(1), bp);
      if (ascncasecmp(bp, "UID COPY ", 9) == 0) {
         cp = &bp[9];
         while (digitchar(*cp))
            cp++;
         if (*cp != ' ')
            goto fail;
         while (*cp == ' ')
            cp++;
         if ((newname = imap_strex(cp, NULL)) == NULL)
            goto fail;
         IMAP_OUT(o, MB_COMD, continue)
         while (mp->mb_active & MB_COMD)
            ok = imap_answer(mp, twice);
         if (response_status == RESPONSE_NO && twice++ == 0)
            goto trycreate;
         if (response_status == RESPONSE_OK && mp->mb_flags & MB_UIDPLUS) {
            imap_copyuid(mp, NULL, imap_unquotestr(newname));
         }
      } else if (ascncasecmp(bp, "UID STORE ", 10) == 0) {
         IMAP_OUT(o, MB_COMD, continue)
         while (mp->mb_active & MB_COMD)
            ok = imap_answer(mp, 1);
         if (ok == OKAY)
            gotcha++;
      } else if (ascncasecmp(bp, "APPEND ", 7) == 0) {
         if ((cp = strrchr(bp, '{')) == NULL)
            goto fail;
         octets = atol(&cp[1]) + 2;
         if ((newname = imap_strex(&bp[7], NULL)) == NULL)
            goto fail;
         IMAP_OUT(o, MB_COMD, continue)
         while (mp->mb_active & MB_COMD) {
            ok = imap_answer(mp, twice);
            if (response_type == RESPONSE_CONT)
               break;
         }
         if (ok == STOP) {
            if (twice++ == 0 && fseek(fp, offs, SEEK_SET) >= 0)
               goto trycreate;
            goto fail;
         }
         while (octets > 0) {
            size_t n = (UICMP(z, octets, >, sizeof iob)
                  ? sizeof iob : (size_t)octets);
            octets -= n;
            if (n != fread(iob, 1, n, fp))
               goto fail;
            swrite1(&mp->mb_sock, iob, n, 1);
         }
         swrite(&mp->mb_sock, "");
         while (mp->mb_active & MB_COMD) {
            ok = imap_answer(mp, 0);
            if (response_status == RESPONSE_NO && twice++ == 0) {
               if (fseek(fp, offs, SEEK_SET) < 0)
                  goto fail;
               goto trycreate;
            }
         }
         if (response_status == RESPONSE_OK && mp->mb_flags & MB_UIDPLUS) {
            if ((offs2 = ftell(fp)) < 0)
               goto fail;
            fseek(fp, offs1, SEEK_SET);
            if (imap_appenduid_cached(mp, fp) == STOP) {
               (void)fseek(fp, offs2, SEEK_SET);
               goto fail;
            }
         }
      } else {
fail:
         n_err(_("Invalid command in IMAP cache queue: \"%s\"\n"), bp);
         rok = STOP;
      }
      continue;
trycreate:
      snprintf(o, sizeof o, "%s CREATE %s\r\n", tag(1), newname);
      IMAP_OUT(o, MB_COMD, continue)
      while (mp->mb_active & MB_COMD)
         ok = imap_answer(mp, 1);
      if (ok == OKAY)
         goto again;
   }
   fflush(fp);
   rewind(fp);
   ftruncate(fileno(fp), 0);
   if (gotcha)
      imap_close(mp);
   n_free(buf);
   return rok;
}

static char *
imap_strex(char const *cp, char const **xp)
{
   char const *cq;
   char *n = NULL;
   n_NYD_IN;

   if (*cp != '"')
      goto jleave;

   for (cq = cp + 1; *cq != '\0'; ++cq) {
      if (*cq == '\\')
         cq++;
      else if (*cq == '"')
         break;
   }
   if (*cq != '"')
      goto jleave;

   n = n_autorec_alloc(cq - cp + 2);
   memcpy(n, cp, cq - cp +1);
   n[cq - cp + 1] = '\0';
   if (xp != NULL)
      *xp = cq + 1;
jleave:
   n_NYD_OU;
   return n;
}

static enum okay
check_expunged(void)
{
   enum okay rv;
   n_NYD_IN;

   if (expunged_messages > 0) {
      n_err(_("Command not executed - messages have been expunged\n"));
      rv = STOP;
   } else
      rv = OKAY;
   n_NYD_OU;
   return rv;
}

FL int
c_connect(void *vp) /* TODO v15-compat mailname<->URL (with password) */
{
   struct url url;
   int rv, omsgCount = msgCount;
   n_NYD_IN;
   n_UNUSED(vp);

   if (mb.mb_type == MB_IMAP && mb.mb_sock.s_fd > 0) {
      n_err(_("Already connected\n"));
      rv = 1;
      goto jleave;
   }

   if (!url_parse(&url, CPROTO_IMAP, mailname)) {
      rv = 1;
      goto jleave;
   }
   ok_bclear(disconnected);
   n_var_vclear(savecat("disconnected-", url.url_u_h_p.s));

   if (mb.mb_type == MB_CACHE) {
      enum fedit_mode fm = FEDIT_NONE;
      if (_imap_rdonly)
         fm |= FEDIT_RDONLY;
      if (!(n_pstate & n_PS_EDIT))
         fm |= FEDIT_SYSBOX;
      _imap_setfile1(NULL, &url, fm, 1);
      if (msgCount > omsgCount)
         newmailinfo(omsgCount);
   }
   rv = 0;
jleave:
   n_NYD_OU;
   return rv;
}

FL int
c_disconnect(void *vp) /* TODO v15-compat mailname<->URL (with password) */
{
   struct url url;
   int rv = 1, *msgvec = vp;
   n_NYD_IN;

   if (mb.mb_type == MB_CACHE) {
      n_err(_("Not connected\n"));
      goto jleave;
   }
   if (mb.mb_type != MB_IMAP || cached_uidvalidity(&mb) == 0) {
      n_err(_("The current mailbox is not cached\n"));
      goto jleave;
   }

   if (!url_parse(&url, CPROTO_IMAP, mailname))
      goto jleave;

   if (*msgvec)
      c_cache(vp);
   ok_bset(disconnected);
   if (mb.mb_type == MB_IMAP) {
      enum fedit_mode fm = FEDIT_NONE;
      if (_imap_rdonly)
         fm |= FEDIT_RDONLY;
      if (!(n_pstate & n_PS_EDIT))
         fm |= FEDIT_SYSBOX;
      sclose(&mb.mb_sock);
      _imap_setfile1(NULL, &url, fm, 1);
   }
   rv = 0;
jleave:
   n_NYD_OU;
   return rv;
}

FL int
c_cache(void *vp)
{
   int rv = 1, *msgvec = vp, *ip;
   struct message *mp;
   n_NYD_IN;

   if (mb.mb_type != MB_IMAP) {
      n_err(_("Not connected to an IMAP server\n"));
      goto jleave;
   }
   if (cached_uidvalidity(&mb) == 0) {
      n_err(_("The current mailbox is not cached\n"));
      goto jleave;
   }

   srelax_hold();
   for (ip = msgvec; *ip; ++ip) {
      mp = &message[*ip - 1];
      if (!(mp->m_content_info & CI_HAVE_BODY)) {
         get_body(mp);
         srelax();
      }
   }
   srelax_rele();
   rv = 0;
jleave:
   n_NYD_OU;
   return rv;
}

FL int
disconnected(const char *file)
{
   struct url url;
   int rv = 1;
   n_NYD_IN;

   if (ok_blook(disconnected)) {
      rv = 1;
      goto jleave;
   }

   if (!url_parse(&url, CPROTO_IMAP, file)) {
      rv = 0;
      goto jleave;
   }
   rv = (n_var_vlook(savecat("disconnected-", url.url_u_h_p.s), FAL0) != NULL);

jleave:
   n_NYD_OU;
   return rv;
}

FL void
transflags(struct message *omessage, long omsgCount, int transparent)
{
   struct message *omp, *nmp, *newdot, *newprevdot;
   int hf;
   n_NYD_IN;

   omp = omessage;
   nmp = message;
   newdot = message;
   newprevdot = NULL;
   while (PTRCMP(omp, <, omessage + omsgCount) &&
         PTRCMP(nmp, <, message + msgCount)) {
      if (dot && nmp->m_uid == dot->m_uid)
         newdot = nmp;
      if (prevdot && nmp->m_uid == prevdot->m_uid)
         newprevdot = nmp;
      if (omp->m_uid == nmp->m_uid) {
         hf = nmp->m_flag & MHIDDEN;
         if (transparent && mb.mb_type == MB_IMAP)
            omp->m_flag &= ~MHIDDEN;
         *nmp++ = *omp++;
         if (transparent && mb.mb_type == MB_CACHE)
            nmp[-1].m_flag |= hf;
      } else if (omp->m_uid < nmp->m_uid)
         ++omp;
      else
         ++nmp;
   }
   dot = newdot;
   setdot(newdot);
   prevdot = newprevdot;
   n_free(omessage);
   n_NYD_OU;
}

FL time_t
imap_read_date_time(const char *cp)
{
   char buf[3];
   time_t t;
   int i, year, month, day, hour, minute, second, sign = -1;
   n_NYD2_IN;

   /* "25-Jul-2004 15:33:44 +0200"
    * |    |    |    |    |    |
    * 0    5   10   15   20   25 */
   if (cp[0] != '"' || strlen(cp) < 28 || cp[27] != '"')
      goto jinvalid;
   day = strtol(&cp[1], NULL, 10);
   for (i = 0;;) {
      if (ascncasecmp(&cp[4], n_month_names[i], 3) == 0)
         break;
      if (n_month_names[++i][0] == '\0')
         goto jinvalid;
   }
   month = i + 1;
   year = strtol(&cp[8], NULL, 10);
   hour = strtol(&cp[13], NULL, 10);
   minute = strtol(&cp[16], NULL, 10);
   second = strtol(&cp[19], NULL, 10);
   if ((t = combinetime(year, month, day, hour, minute, second)) == (time_t)-1)
      goto jinvalid;
   switch (cp[22]) {
   case '-':
      sign = 1;
      break;
   case '+':
      break;
   default:
      goto jinvalid;
   }
   buf[2] = '\0';
   buf[0] = cp[23];
   buf[1] = cp[24];
   t += strtol(buf, NULL, 10) * sign * 3600;
   buf[0] = cp[25];
   buf[1] = cp[26];
   t += strtol(buf, NULL, 10) * sign * 60;
jleave:
   n_NYD2_OU;
   return t;
jinvalid:
   time(&t);
   goto jleave;
}

FL const char *
imap_make_date_time(time_t t)
{
   static char s[40];
   char const *mn;
   si32_t y, md, th, tm, ts;
   struct tm *tmp;
   int tzdiff, tzdiff_hour, tzdiff_min;
   time_t t2;
   n_NYD2_IN;

jredo:
   if((t2 = mktime(gmtime(&t))) == (time_t)-1){
      t = 0;
      goto jredo;
   }
   tzdiff = t - t2;
   if((tmp = localtime(&t)) == NULL){
      t = 0;
      goto jredo;
   }

   tzdiff_hour = (int)(tzdiff / 60);
   tzdiff_min = tzdiff_hour % 60;
   tzdiff_hour /= 60;
   if (tmp->tm_isdst > 0)
      tzdiff_hour++;

   if(n_UNLIKELY((y = tmp->tm_year) < 0 || y >= 9999/*SI32_MAX*/ - 1900)){
      y = 1970;
      mn = n_month_names[0];
      md = 1;
      th = tm = ts = 0;
   }else{
      y += 1900;
      mn = (tmp->tm_mon >= 0 && tmp->tm_mon <= 11)
            ? n_month_names[tmp->tm_mon] : n_qm;

      if((md = tmp->tm_mday) < 1 || md > 31)
         md = 1;

      if((th = tmp->tm_hour) < 0 || th > 23)
         th = 0;
      if((tm = tmp->tm_min) < 0 || tm > 59)
         tm = 0;
      if((ts = tmp->tm_sec) < 0 || ts > 60)
         ts = 0;
   }

   snprintf(s, sizeof s, "\"%02d-%s-%04d %02d:%02d:%02d %+03d%02d\"",
         md, mn, y, th, tm, ts, tzdiff_hour, tzdiff_min);
   n_NYD2_OU;
   return s;
}

FL char *
(protbase)(char const *cp  su_DBG_LOC_ARGS_DECL)
{
   char *n, *np;
   n_NYD2_IN;

   np = n = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(strlen(cp) +1,
         su_DBG_LOC_ARGS_ORUSE);

   /* Just ignore the `is-system-mailbox' prefix XXX */
   if (cp[0] == '%' && cp[1] == ':')
      cp += 2;

   while (*cp != '\0') {
      if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
         *np++ = *cp++;
         *np++ = *cp++;
         *np++ = *cp++;
      } else if (cp[0] == '/')
         break;
      else
         *np++ = *cp++;
   }
   *np = '\0';
   n_NYD2_OU;
   return n;
}
#endif /* mx_HAVE_IMAP */

/* s-it-mode */
