/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME support functions.
 *@ TODO Complete rewrite.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause
 */
/*
 * Copyright (c) 2000
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
#undef n_FILE
#define n_FILE mime

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/* Don't ask, but it keeps body and soul together */
enum a_mime_structure_hack{
   a_MIME_SH_NONE,
   a_MIME_SH_COMMENT,
   a_MIME_SH_QUOTE
};

static char                   *_cs_iter_base, *_cs_iter;
#ifdef HAVE_ICONV
# define _CS_ITER_GET() \
   ((_cs_iter != NULL) ? _cs_iter : ok_vlook(CHARSET_8BIT_OKEY))
#else
# define _CS_ITER_GET() ((_cs_iter != NULL) ? _cs_iter : ok_vlook(ttycharset))
#endif
#define _CS_ITER_STEP() _cs_iter = n_strsep(&_cs_iter_base, ',', TRU1)

/* Is 7-bit enough? */
#ifdef HAVE_ICONV
static bool_t           _has_highbit(char const *s);
static bool_t           _name_highbit(struct name *np);
#endif

/* fwrite(3) while checking for displayability */
static ssize_t          _fwrite_td(struct str const *input,
                           bool_t failiconv, enum tdflags flags,
                           struct str *outrest, struct quoteflt *qf);

/* Convert header fields to RFC 2047 format and write to the file fo */
static ssize_t          mime_write_tohdr(struct str *in, FILE *fo,
                           size_t *colp, enum a_mime_structure_hack msh);

#ifdef HAVE_ICONV
static ssize_t a_mime__convhdra(struct str *inp, FILE *fp, size_t *colp,
                  enum a_mime_structure_hack msh);
#else
# define a_mime__convhdra(S,F,C,MSH) mime_write_tohdr(S, F, C, MSH)
#endif

/* Write an address to a header field */
static ssize_t          mime_write_tohdr_a(struct str *in, FILE *f,
                           size_t *colp, enum a_mime_structure_hack msh);

/* Append to buf, handling resizing */
static void             _append_str(char **buf, size_t *sz, size_t *pos,
                           char const *str, size_t len);
static void             _append_conv(char **buf, size_t *sz, size_t *pos,
                           char const *str, size_t len);

#ifdef HAVE_ICONV
static bool_t
_has_highbit(char const *s)
{
   bool_t rv = TRU1;
   NYD_IN;

   if (s) {
      do
         if ((ui8_t)*s & 0x80)
            goto jleave;
      while (*s++ != '\0');
   }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

static bool_t
_name_highbit(struct name *np)
{
   bool_t rv = TRU1;
   NYD_IN;

   while (np) {
      if (_has_highbit(np->n_name) || _has_highbit(np->n_fullname))
         goto jleave;
      np = np->n_flink;
   }
   rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}
#endif /* HAVE_ICONV */

static sigjmp_buf       __mimefwtd_actjmp; /* TODO someday.. */
static int              __mimefwtd_sig; /* TODO someday.. */
static sighandler_type  __mimefwtd_opipe;
static void
__mimefwtd_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __mimefwtd_sig = sig;
   siglongjmp(__mimefwtd_actjmp, 1);
}

static ssize_t
_fwrite_td(struct str const *input, bool_t failiconv, enum tdflags flags,
   struct str *outrest, struct quoteflt *qf)
{
   /* TODO note: after send/MIME layer rewrite we will have a string pool
    * TODO so that memory allocation count drops down massively; for now,
    * TODO v14.* that is, we pay a lot & heavily depend on the allocator */
   /* TODO well if we get a broken pipe here, and it happens to
    * TODO happen pretty easy when sleeping in a full pipe buffer,
    * TODO then the current codebase performs longjump away;
    * TODO this leaves memory leaks behind ('think up to 3 per,
    * TODO dep. upon alloca availability).  For this to be fixed
    * TODO we either need to get rid of the longjmp()s (tm) or
    * TODO the storage must come from the outside or be tracked
    * TODO in a carrier struct.  Best both.  But storage reuse
    * TODO would be a bigbig win besides */
   /* *input* _may_ point to non-modifyable buffer; but even then it only
    * needs to be dup'ed away if we have to transform the content */
   struct str in, out;
   ssize_t rv;
   NYD_IN;
   n_UNUSED(failiconv);
   n_UNUSED(outrest);

   in = *input;
   out.s = NULL;
   out.l = 0;

#ifdef HAVE_ICONV
   if ((flags & TD_ICONV) && iconvd != (iconv_t)-1) {
      int err;
      char *buf;

      buf = NULL;

      if (outrest != NULL && outrest->l > 0) {
         in.l = outrest->l + input->l;
         in.s = buf = n_alloc(in.l +1);
         memcpy(in.s, outrest->s, outrest->l);
         memcpy(&in.s[outrest->l], input->s, input->l);
         outrest->l = 0;
      }

      rv = 0;

      /* TODO Sigh, no problem if we have a filter that has a buffer (or
       * TODO become fed with entire lines, whatever), but for now we need
       * TODO to ensure we pass entire lines from in here to iconv(3), because
       * TODO the Citrus iconv(3) will fail tests with stateful encodings
       * TODO if we do not (only seen on FreeBSD) */
#if 0 /* TODO actually not needed indeed, it was known iswprint() error! */
      if(!(flags & _TD_EOF) && outrest != NULL){
         size_t i, j;
         char const *cp;

         if((cp = memchr(in.s, '\n', j = in.l)) != NULL){
            i = PTR2SIZE(cp - in.s);
            j -= i;
            while(j > 0 && *cp == '\n') /* XXX one iteration too much */
               ++cp, --j, ++i;
            if(j != 0)
               n_str_assign_buf(outrest, cp, j);
            in.l = i;
         }else{
            n_str_assign(outrest, &in);
            goto jleave;
         }
      }
#endif

      if((err = n_iconv_str(iconvd,
            (failiconv ? n_ICONV_NONE : n_ICONV_UNIDEFAULT),
            &out, &in, &in)) != 0){
         if(err != n_ERR_INVAL)
            n_iconv_reset(iconvd);

         if(outrest != NULL && in.l > 0){
            /* Incomplete multibyte at EOF is special xxx _INVAL? */
            if (flags & _TD_EOF) {
               out.s = n_realloc(out.s, out.l + sizeof(n_unirepl));
               if(n_psonce & n_PSO_UNICODE){
                  memcpy(&out.s[out.l], n_unirepl, sizeof(n_unirepl) -1);
                  out.l += sizeof(n_unirepl) -1;
               }else
                  out.s[out.l++] = '?';
            } else
               n_str_add(outrest, &in);
         }else
            rv = -1;
      }
      in = out;
      out.l = 0;
      out.s = NULL;
      flags &= ~_TD_BUFCOPY;

      if(buf != NULL)
         n_free(buf);
      if(rv < 0)
         goto jleave;
   }else
#endif /* HAVE_ICONV */
   /* Else, if we will modify the data bytes and thus introduce the potential
    * of messing up multibyte sequences which become splitted over buffer
    * boundaries TODO and unless we don't have our filter chain which will
    * TODO make these hacks go by, buffer data until we see a NL */
         if((flags & (TD_ISPR | TD_DELCTRL)) && outrest != NULL &&
#ifdef HAVE_ICONV
         iconvd == (iconv_t)-1 &&
#endif
         (!(flags & _TD_EOF) || outrest->l > 0)
   ) {
      size_t i;
      char *cp;

      for (cp = &in.s[in.l]; cp > in.s && cp[-1] != '\n'; --cp)
         ;
      i = PTR2SIZE(cp - in.s);

      if (i != in.l) {
         if (i > 0) {
            n_str_assign_buf(outrest, cp, in.l - i);
            cp = n_alloc(i +1);
            memcpy(cp, in.s, in.l = i);
            (in.s = cp)[in.l = i] = '\0';
            flags &= ~_TD_BUFCOPY;
         } else {
            n_str_add_buf(outrest, input->s, input->l);
            rv = 0;
            goto jleave;
         }
      }
   }

   if (flags & TD_ISPR)
      makeprint(&in, &out);
   else if (flags & _TD_BUFCOPY)
      n_str_dup(&out, &in);
   else
      out = in;
   if (flags & TD_DELCTRL)
      out.l = delctrl(out.s, out.l);

   __mimefwtd_sig = 0;
   __mimefwtd_opipe = safe_signal(SIGPIPE, &__mimefwtd_onsig);
   if (sigsetjmp(__mimefwtd_actjmp, 1)) {
      rv = 0;
      goto j__sig;
   }

   rv = quoteflt_push(qf, out.s, out.l);

j__sig:
   if (out.s != in.s)
      n_free(out.s);
   if (in.s != input->s)
      n_free(in.s);
   safe_signal(SIGPIPE, __mimefwtd_opipe);
   if (__mimefwtd_sig != 0)
      n_raise(__mimefwtd_sig);
jleave:
   NYD_OU;
   return rv;
}

static ssize_t
mime_write_tohdr(struct str *in, FILE *fo, size_t *colp,
   enum a_mime_structure_hack msh)
{
   /* TODO mime_write_tohdr(): we don't know the name of our header->maxcol..
    * TODO  MIME/send layer rewrite: more available state!!
    * TODO   Because of this we cannot make a difference in between structured
    * TODO   and unstructured headers (RFC 2047, 5. (2))
    * TODO   This means, e.g., that this gets called multiple times for a
    * TODO   structured header and always starts thinking it is at column 0.
    * TODO   I.e., it may get called for only the content of a comment etc.,
    * TODO   not knowing anything of its context.
    * TODO   Instead we should have a list of header body content tokens,
    * TODO   convert them, and then dump the converted tokens, breaking lines.
    * TODO   I.e., get rid of convhdra, mime_write_tohdr_a and such...
    * TODO   Somewhen, the following should produce smooth stuff:
    * TODO   '  "Hallo\"," Dr. Backe "Bl\"ö\"d" (Gell) <ha@llöch.en>
    * TODO    "Nochm\"a\"l"<ta@tu.da>(Dümm)'
    * TODO NOT MULTIBYTE SAFE IF AN ENCODED WORD HAS TO BE SPLITTED!
    * TODO  To be better we had to mbtowc_l() (non-std! and no locale!!) and
    * TODO   work char-wise!  ->  S-CText..
    * TODO  The real problem for STD compatibility is however that "in" is
    * TODO   already iconv(3) encoded to the target character set!  We could
    * TODO   also solve it (very expensively!) if we would narrow down to an
    * TODO   encoded word and then iconv(3)+MIME encode in one go, in which
    * TODO   case multibyte errors could be caught! */
   enum {
      /* Maximum line length */
      a_MAXCOL_NENC = MIME_LINELEN,
      a_MAXCOL = MIME_LINELEN_RFC2047
   };

   struct str cout, cin;
   enum {
      _FIRST      = 1<<0,  /* Nothing written yet, start of string */
      _MSH_NOTHING = 1<<1, /* Now, really: nothing at all has been written */
      a_ANYENC = 1<<2,     /* We have RFC 2047 anything at least once */
      _NO_QP      = 1<<3,  /* No quoted-printable allowed */
      _NO_B64     = 1<<4,  /* Ditto, base64 */
      _ENC_LAST   = 1<<5,  /* Last round generated encoded word */
      _SHOULD_BEE = 1<<6,  /* Avoid lines longer than SHOULD via encoding */
      _RND_SHIFT  = 7,
      _RND_MASK   = (1<<_RND_SHIFT) - 1,
      _SPACE      = 1<<(_RND_SHIFT+1),    /* Leading whitespace */
      _8BIT       = 1<<(_RND_SHIFT+2),    /* High bit set */
      _ENCODE     = 1<<(_RND_SHIFT+3),    /* Need encoding */
      _ENC_B64    = 1<<(_RND_SHIFT+4),    /* - let it be base64 */
      _OVERLONG   = 1<<(_RND_SHIFT+5)     /* Temporarily rised limit */
   } flags;
   char const *cset7, *cset8, *wbot, *upper, *wend, *wcur;
   ui32_t cset7_len, cset8_len;
   size_t col, i, j;
   ssize_t sz;

   NYD_IN;

   cout.s = NULL, cout.l = 0;
   cset7 = ok_vlook(charset_7bit);
   cset7_len = (ui32_t)strlen(cset7);
   cset8 = _CS_ITER_GET(); /* TODO MIME/send layer: iter active? iter! else */
   cset8_len = (ui32_t)strlen(cset8);

   flags = _FIRST;
   if(msh != a_MIME_SH_NONE)
      flags |= _MSH_NOTHING;

   /* RFC 1468, "MIME Considerations":
    *     ISO-2022-JP may also be used in MIME Part 2 headers.  The "B"
    *     encoding should be used with ISO-2022-JP text. */
   /* TODO of course, our current implementation won't deal properly with
    * TODO any stateful encoding at all... (the standard says each encoded
    * TODO word must include all necessary reset sequences..., i.e., each
    * TODO encoded word must be a self-contained iconv(3) life cycle) */
   if (!asccasecmp(cset8, "iso-2022-jp") || mime_enc_target() == MIMEE_B64)
      flags |= _NO_QP;

   wbot = in->s;
   upper = wbot + in->l;
   sz = 0;

   if(colp == NULL || (col = *colp) == 0)
      col = sizeof("Mail-Followup-To: ") -1; /* TODO dreadful thing */

   /* The user may specify empy quoted-strings or comments, keep them! */
   if(wbot == upper) {
      if(flags & _MSH_NOTHING){
         flags &= ~_MSH_NOTHING;
         putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
         sz = 1;
         ++col;
      }
   } else for (; wbot < upper; flags &= ~_FIRST, wbot = wend) {
      flags &= _RND_MASK;
      wcur = wbot;
      while (wcur < upper && whitechar(*wcur)) {
         flags |= _SPACE;
         ++wcur;
      }

      /* Any occurrence of whitespace resets prevention of lines >SHOULD via
       * enforced encoding (xxx SHOULD, but.. encoding is expensive!!) */
      if (flags & _SPACE)
         flags &= ~_SHOULD_BEE;

     /* Data ends with WS - dump it and done.
      * Also, if we have seen multiple successive whitespace characters, then
      * if there was no encoded word last, i.e., if we can simply take them
      * over to the output as-is, keep one WS for possible later separation
      * purposes and simply print the others as-is, directly! */
      if (wcur == upper) {
         wend = wcur;
         goto jnoenc_putws;
      }
      if ((flags & (_ENC_LAST | _SPACE)) == _SPACE && wcur - wbot > 1) {
         wend = wcur - 1;
         goto jnoenc_putws;
      }

      /* Skip over a word to next non-whitespace, keep track along the way
       * whether our 7-bit charset suffices to represent the data */
      for (wend = wcur; wend < upper; ++wend) {
         if (whitechar(*wend))
            break;
         if ((uc_i)*wend & 0x80)
            flags |= _8BIT;
      }

      /* Decide whether the range has to become encoded or not */
      i = PTR2SIZE(wend - wcur);
      j = mime_enc_mustquote(wcur, i, MIMEEF_ISHEAD);
      /* If it just cannot fit on a SHOULD line length, force encode */
      if (i > a_MAXCOL_NENC) {
         flags |= _SHOULD_BEE; /* (Sigh: SHOULD only, not MUST..) */
         goto j_beejump;
      }
      if ((flags & _SHOULD_BEE) || j > 0) {
j_beejump:
         flags |= _ENCODE;
         /* Use base64 if requested or more than 50% -37.5-% of the bytes of
          * the string need to be encoded */
         if ((flags & _NO_QP) || j >= i >> 1)/*(i >> 2) + (i >> 3))*/
            flags |= _ENC_B64;
      }
      DBG( if (flags & _8BIT) assert(flags & _ENCODE); )

      if (!(flags & _ENCODE)) {
         /* Encoded word produced, but no linear whitespace for necessary RFC
          * 2047 separation?  Generate artificial data (bad standard!) */
         if ((flags & (_ENC_LAST | _SPACE)) == _ENC_LAST) {
            if (col >= a_MAXCOL) {
               putc('\n', fo);
               ++sz;
               col = 0;
            }
            if(flags & _MSH_NOTHING){
               flags &= ~_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++sz;
               ++col;
            }
            putc(' ', fo);
            ++sz;
            ++col;
         }

jnoenc_putws:
         flags &= ~_ENC_LAST;

         /* todo No effort here: (1) v15.0 has to bring complete rewrite,
          * todo (2) the standard is braindead and (3) usually this is one
          * todo word only, and why be smarter than the standard? */
jnoenc_retry:
         i = PTR2SIZE(wend - wbot);
         if (i + col + ((flags & _MSH_NOTHING) != 0) <=
                  (flags & _OVERLONG ? MIME_LINELEN_MAX
                   : (flags & a_ANYENC ? a_MAXCOL : a_MAXCOL_NENC))) {
            if(flags & _MSH_NOTHING){
               flags &= ~_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++sz;
               ++col;
            }
            i = fwrite(wbot, sizeof *wbot, i, fo);
            sz += i;
            col += i;
            continue;
         }

         /* Doesn't fit, try to break the line first; */
         if (col > 1) {
            putc('\n', fo);
            if (whitechar(*wbot)) {
               putc((uc_i)*wbot, fo);
               ++wbot;
            } else
               putc(' ', fo); /* Bad standard: artificial data! */
            sz += 2;
            col = 1;
            if(flags & _MSH_NOTHING){
               flags &= ~_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++sz;
               ++col;
            }
            flags |= _OVERLONG;
            goto jnoenc_retry;
         }

         /* It is so long that it needs to be broken, effectively causing
          * artificial spaces to be inserted (bad standard), yuck */
         /* todo This is not multibyte safe, as above; and completely stupid
          * todo P.S.: our _SHOULD_BEE prevents these cases in the meanwhile */
/* FIXME n_PSO_UNICODE and parse using UTF-8 sync possibility! */
         wcur = wbot + MIME_LINELEN_MAX - 8;
         while (wend > wcur)
            wend -= 4;
         goto jnoenc_retry;
      } else {
         /* Encoding to encoded word(s); deal with leading whitespace, place
          * a separator first as necessary: encoded words must always be
          * separated from text and other encoded words with linear WS.
          * And if an encoded word was last, intermediate whitespace must
          * also be encoded, otherwise it would get stripped away! */
         wcur = n_UNCONST(n_empty);
         if ((flags & (_ENC_LAST | _SPACE)) != _SPACE) {
            /* Reinclude whitespace */
            flags &= ~_SPACE;
            /* We don't need to place a separator at the very beginning */
            if (!(flags & _FIRST))
               wcur = n_UNCONST(" ");
         } else
            wcur = wbot++;

         flags |= a_ANYENC | _ENC_LAST;
         n_pstate |= n_PS_HEADER_NEEDED_MIME;

         /* RFC 2047:
          *    An 'encoded-word' may not be more than 75 characters long,
          *    including 'charset', 'encoding', 'encoded-text', and
          *    delimiters.  If it is desirable to encode more text than will
          *    fit in an 'encoded-word' of 75 characters, multiple
          *    'encoded-word's (separated by CRLF SPACE) may be used.
          *
          *    While there is no limit to the length of a multiple-line
          *    header field, each line of a header field that contains one
          *    or more 'encoded-word's is limited to 76 characters */
jenc_retry:
         cin.s = n_UNCONST(wbot);
         cin.l = PTR2SIZE(wend - wbot);

         /* C99 */{
            struct str *xout;

            if(flags & _ENC_B64)
               xout = b64_encode(&cout, &cin, B64_ISHEAD | B64_ISENCWORD);
            else
               xout = qp_encode(&cout, &cin, QP_ISHEAD | QP_ISENCWORD);
            if(xout == NULL){
               sz = -1;
               break;
            }
            j = xout->l;
         }
         /* (Avoid trigraphs in the RFC 2047 placeholder..) */
         i = j + (flags & _8BIT ? cset8_len : cset7_len) + sizeof("=!!B!!=") -1;
         if (*wcur != '\0')
            ++i;

jenc_retry_same:
         /* Unfortunately RFC 2047 explicitly disallows encoded words to be
          * longer (just like RFC 5322's "a line SHOULD fit in 78 but MAY be
          * 998 characters long"), so we cannot use the _OVERLONG mechanism,
          * even though all tested mailers seem to support it */
         if (i + col <= (/*flags & _OVERLONG ? MIME_LINELEN_MAX :*/ a_MAXCOL)) {
            if(flags & _MSH_NOTHING){
               flags &= ~_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++sz;
               ++col;
            }
            fprintf(fo, "%.1s=?%s?%c?%.*s?=",
               wcur, (flags & _8BIT ? cset8 : cset7),
               (flags & _ENC_B64 ? 'B' : 'Q'),
               (int)cout.l, cout.s);
            sz += i;
            col += i;
            continue;
         }

         /* Doesn't fit, try to break the line first */
         /* TODO I've commented out the _FIRST test since we (1) cannot do
          * TODO _OVERLONG since (MUAs support but) the standard disallows,
          * TODO and because of our iconv problem i prefer an empty first line
          * TODO in favour of a possibly messed up multibytes character. :-( */
         if (col > 1 /* TODO && !(flags & _FIRST)*/) {
            putc('\n', fo);
            sz += 2;
            col = 1;
            if (!(flags & _SPACE)) {
               putc(' ', fo);
               wcur = n_UNCONST(n_empty);
               /*flags |= _OVERLONG;*/
               goto jenc_retry_same;
            } else {
               putc((uc_i)*wcur, fo);
               if (whitechar(*(wcur = wbot)))
                  ++wbot;
               else {
                  flags &= ~_SPACE;
                  wcur = n_UNCONST(n_empty);
               }
               /*flags &= ~_OVERLONG;*/
               goto jenc_retry;
            }
         }

         /* It is so long that it needs to be broken, effectively causing
          * artificial data to be inserted (bad standard), yuck */
         /* todo This is not multibyte safe, as above */
         /*if (!(flags & _OVERLONG)) { Mechanism explicitly forbidden by 2047
            flags |= _OVERLONG;
            goto jenc_retry;
         }*/

/* FIXME n_PSO_UNICODE and parse using UTF-8 sync possibility! */
         i = PTR2SIZE(wend - wbot) + !!(flags & _SPACE);
         j = 3 + !(flags & _ENC_B64);
         for (;;) {
            wend -= j;
            i -= j;
            /* (Note the problem most likely is the transfer-encoding blow,
             * which is why we test this *after* the decrements.. */
            if (i <= a_MAXCOL)
               break;
         }
         goto jenc_retry;
      }
   }

   if(!(flags & _MSH_NOTHING) && msh != a_MIME_SH_NONE){
      putc((msh == a_MIME_SH_COMMENT ? ')' : '"'), fo);
      ++sz;
      ++col;
   }

   if(cout.s != NULL)
      n_free(cout.s);

   if(colp != NULL)
      *colp = col;
   NYD_OU;
   return sz;
}

#ifdef HAVE_ICONV
static ssize_t
a_mime__convhdra(struct str *inp, FILE *fp, size_t *colp,
      enum a_mime_structure_hack msh){
   struct str ciconv;
   ssize_t rv;
   NYD_IN;

   rv = 0;
   ciconv.s = NULL;

   if(inp->l > 0 && iconvd != (iconv_t)-1){
      ciconv.l = 0;
      if(n_iconv_str(iconvd, n_ICONV_NONE, &ciconv, inp, NULL) != 0){
         n_iconv_reset(iconvd);
         goto jleave;
      }
      *inp = ciconv;
   }

   rv = mime_write_tohdr(inp, fp, colp, msh);
jleave:
   if(ciconv.s != NULL)
      n_free(ciconv.s);
   NYD_OU;
   return rv;
}
#endif /* HAVE_ICONV */

static ssize_t
mime_write_tohdr_a(struct str *in, FILE *f, size_t *colp,
   enum a_mime_structure_hack msh)
{
   struct str xin;
   size_t i;
   char const *cp, *lastcp;
   ssize_t sz, x;
   NYD_IN;

   in->s[in->l] = '\0';

   if((cp = routeaddr(lastcp = in->s)) != NULL && cp > lastcp) {
      xin.s = n_UNCONST(lastcp);
      xin.l = PTR2SIZE(cp - lastcp);
      if ((sz = a_mime__convhdra(&xin, f, colp, msh)) < 0)
         goto jleave;
      lastcp = cp;
   } else {
      cp = lastcp;
      sz = 0;
   }

   for( ; *cp != '\0'; ++cp){
      switch(*cp){
      case '(':
         i = PTR2SIZE(cp - lastcp);
         if(i > 0){
            if(fwrite(lastcp, 1, i, f) != i)
               goto jerr;
            sz += i;
         }
         lastcp = ++cp;
         cp = skip_comment(cp);
         if(cp > lastcp)
            --cp;
         /* We want to keep empty comments, too! */
         xin.s = n_UNCONST(lastcp);
         xin.l = PTR2SIZE(cp - lastcp);
         if ((x = a_mime__convhdra(&xin, f, colp, a_MIME_SH_COMMENT)) < 0)
            goto jerr;
         sz += x;
         lastcp = &cp[1];
         break;
      case '"':
         i = PTR2SIZE(cp - lastcp);
         if(i > 0){
            if(fwrite(lastcp, 1, i, f) != i)
               goto jerr;
            sz += i;
         }
         for(lastcp = ++cp; *cp != '\0'; ++cp){
            if(*cp == '"')
               break;
            if(*cp == '\\' && cp[1] != '\0')
               ++cp;
         }
         /* We want to keep empty quoted-strings, too! */
         xin.s = n_UNCONST(lastcp);
         xin.l = PTR2SIZE(cp - lastcp);
         if((x = a_mime__convhdra(&xin, f, colp, a_MIME_SH_QUOTE)) < 0)
            goto jerr;
         sz += x;
         ++sz;
         lastcp = &cp[1];
         break;
      }
   }

   i = PTR2SIZE(cp - lastcp);
   if(i > 0){
      if(fwrite(lastcp, 1, i, f) != i)
         goto jerr;
      sz += i;
   }
jleave:
   NYD_OU;
   return sz;
jerr:
   sz = -1;
   goto jleave;
}

static void
_append_str(char **buf, size_t *sz, size_t *pos, char const *str, size_t len)
{
   NYD_IN;
   *buf = n_realloc(*buf, *sz += len);
   memcpy(&(*buf)[*pos], str, len);
   *pos += len;
   NYD_OU;
}

static void
_append_conv(char **buf, size_t *sz, size_t *pos, char const *str, size_t len)
{
   struct str in, out;
   NYD_IN;

   in.s = n_UNCONST(str);
   in.l = len;
   mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);
   _append_str(buf, sz, pos, out.s, out.l);
   n_free(out.s);
   NYD_OU;
}

FL bool_t
charset_iter_reset(char const *a_charset_to_try_first) /* TODO elim. dups! */
{
   char const *sarr[3];
   size_t sarrl[3], len;
   char *cp;
   NYD_IN;
   n_UNUSED(a_charset_to_try_first);

#ifdef HAVE_ICONV
   sarr[2] = ok_vlook(CHARSET_8BIT_OKEY);

   if(a_charset_to_try_first != NULL && strcmp(a_charset_to_try_first, sarr[2]))
      sarr[0] = a_charset_to_try_first;
   else
      sarr[0] = NULL;

   if((sarr[1] = ok_vlook(sendcharsets)) == NULL &&
         ok_blook(sendcharsets_else_ttycharset)){
      cp = n_UNCONST(ok_vlook(ttycharset));
      if(strcmp(cp, sarr[2]) && (sarr[0] == NULL || strcmp(cp, sarr[0])))
         sarr[1] = cp;
   }
#else
   sarr[2] = ok_vlook(ttycharset);
#endif

   sarrl[2] = len = strlen(sarr[2]);
#ifdef HAVE_ICONV
   if ((cp = n_UNCONST(sarr[1])) != NULL)
      len += (sarrl[1] = strlen(cp));
   else
      sarrl[1] = 0;
   if ((cp = n_UNCONST(sarr[0])) != NULL)
      len += (sarrl[0] = strlen(cp));
   else
      sarrl[0] = 0;
#endif

   _cs_iter_base = cp = n_autorec_alloc(len + 1 + 1 +1);

#ifdef HAVE_ICONV
   if ((len = sarrl[0]) != 0) {
      memcpy(cp, sarr[0], len);
      cp[len] = ',';
      cp += ++len;
   }
   if ((len = sarrl[1]) != 0) {
      memcpy(cp, sarr[1], len);
      cp[len] = ',';
      cp += ++len;
   }
#endif
   len = sarrl[2];
   memcpy(cp, sarr[2], len);
   cp[len] = '\0';

   _CS_ITER_STEP();
   NYD_OU;
   return (_cs_iter != NULL);
}

FL bool_t
charset_iter_next(void)
{
   bool_t rv;
   NYD_IN;

   _CS_ITER_STEP();
   rv = (_cs_iter != NULL);
   NYD_OU;
   return rv;
}

FL bool_t
charset_iter_is_valid(void)
{
   bool_t rv;
   NYD_IN;

   rv = (_cs_iter != NULL);
   NYD_OU;
   return rv;
}

FL char const *
charset_iter(void)
{
   char const *rv;
   NYD_IN;

   rv = _cs_iter;
   NYD_OU;
   return rv;
}

FL char const *
charset_iter_or_fallback(void)
{
   char const *rv;
   NYD_IN;

   rv = _CS_ITER_GET();
   NYD_OU;
   return rv;
}

FL void
charset_iter_recurse(char *outer_storage[2]) /* TODO LEGACY FUN, REMOVE */
{
   NYD_IN;
   outer_storage[0] = _cs_iter_base;
   outer_storage[1] = _cs_iter;
   NYD_OU;
}

FL void
charset_iter_restore(char *outer_storage[2]) /* TODO LEGACY FUN, REMOVE */
{
   NYD_IN;
   _cs_iter_base = outer_storage[0];
   _cs_iter = outer_storage[1];
   NYD_OU;
}

#ifdef HAVE_ICONV
FL char const *
need_hdrconv(struct header *hp) /* TODO once only, then iter */
{
   struct n_header_field *hfp;
   char const *rv;
   NYD_IN;

   rv = NULL;

   /* C99 */{
      struct n_header_field *chlp[3]; /* TODO JOINED AFTER COMPOSE! */
      ui32_t i;

      chlp[0] = n_poption_arg_C;
      chlp[1] = n_customhdr_list;
      chlp[2] = hp->h_user_headers;

      for(i = 0; i < n_NELEM(chlp); ++i)
         if((hfp = chlp[i]) != NULL)
            do if(_has_highbit(hfp->hf_dat + hfp->hf_nl +1))
               goto jneeds;
            while((hfp = hfp->hf_next) != NULL);
   }

   if (hp->h_mft != NULL) {
      if (_name_highbit(hp->h_mft))
         goto jneeds;
   }
   if (hp->h_from != NULL) {
      if (_name_highbit(hp->h_from))
         goto jneeds;
   } else if (_has_highbit(myaddrs(NULL)))
      goto jneeds;
   if (hp->h_reply_to) {
      if (_name_highbit(hp->h_reply_to))
         goto jneeds;
   } else {
      char const *v15compat;

      if((v15compat = ok_vlook(replyto)) != NULL)
         n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
      if(_has_highbit(v15compat))
         goto jneeds;
      if(_has_highbit(ok_vlook(reply_to)))
         goto jneeds;
   }
   if (hp->h_sender) {
      if (_name_highbit(hp->h_sender))
         goto jneeds;
   } else if (_has_highbit(ok_vlook(sender)))
      goto jneeds;

   if (_name_highbit(hp->h_to))
      goto jneeds;
   if (_name_highbit(hp->h_cc))
      goto jneeds;
   if (_name_highbit(hp->h_bcc))
      goto jneeds;
   if (_has_highbit(hp->h_subject))
jneeds:
      rv = _CS_ITER_GET(); /* TODO MIME/send: iter active? iter! else */
   NYD_OU;
   return rv;
}
#endif /* HAVE_ICONV */

FL void
mime_fromhdr(struct str const *in, struct str *out, enum tdflags flags)
{
   /* TODO mime_fromhdr(): is called with strings that contain newlines;
    * TODO this is the usual newline problem all around the codebase;
    * TODO i.e., if we strip it, then the display misses it ;>
    * TODO this is why it is so messy and why S-nail v14.2 plus additional
    * TODO patch for v14.5.2 (and maybe even v14.5.3 subminor) occurred, and
    * TODO why our display reflects what is contained in the message: the 1:1
    * TODO relationship of message content and display!
    * TODO instead a header line should be decoded to what it is (a single
    * TODO line that is) and it should be objective to the backend whether
    * TODO it'll be folded to fit onto the display or not, e.g., for search
    * TODO purposes etc.  then the only condition we have to honour in here
    * TODO is that whitespace in between multiple adjacent MIME encoded words
    * TODO á la RFC 2047 is discarded; i.e.: this function should deal with
    * TODO RFC 2047 and be renamed: mime_fromhdr() -> mime_rfc2047_decode() */
   struct str cin, cout;
   char *p, *op, *upper;
   ui32_t convert, lastenc, lastoutl;
#ifdef HAVE_ICONV
   char const *tcs;
   char *cbeg;
   iconv_t fhicd = (iconv_t)-1;
#endif
   NYD_IN;

   out->l = 0;
   if (in->l == 0) {
      *(out->s = n_alloc(1)) = '\0';
      goto jleave;
   }
   out->s = NULL;

#ifdef HAVE_ICONV
   tcs = ok_vlook(ttycharset);
#endif
   p = in->s;
   upper = p + in->l;
   lastenc = lastoutl = 0;

   while (p < upper) {
      op = p;
      if (*p == '=' && *(p + 1) == '?') {
         p += 2;
#ifdef HAVE_ICONV
         cbeg = p;
#endif
         while (p < upper && *p != '?')
            ++p;  /* strip charset */
         if (p >= upper)
            goto jnotmime;
         ++p;
#ifdef HAVE_ICONV
         if (flags & TD_ICONV) {
            size_t i = PTR2SIZE(p - cbeg);
            char *ltag, *cs = n_lofi_alloc(i);

            memcpy(cs, cbeg, --i);
            cs[i] = '\0';
            /* RFC 2231 extends the RFC 2047 character set definition in
             * encoded words by language tags - silently strip those off */
            if ((ltag = strchr(cs, '*')) != NULL)
               *ltag = '\0';

            if (fhicd != (iconv_t)-1)
               n_iconv_close(fhicd);
            fhicd = asccasecmp(cs, tcs) ? n_iconv_open(tcs, cs) : (iconv_t)-1;
            n_lofi_free(cs);
         }
#endif
         switch (*p) {
         case 'B': case 'b':
            convert = CONV_FROMB64;
            break;
         case 'Q': case 'q':
            convert = CONV_FROMQP;
            break;
         default: /* invalid, ignore */
            goto jnotmime;
         }
         if (*++p != '?')
            goto jnotmime;
         cin.s = ++p;
         cin.l = 1;
         for (;;) {
            if (PTRCMP(p + 1, >=, upper))
               goto jnotmime;
            if (*p++ == '?' && *p == '=')
               break;
            ++cin.l;
         }
         ++p;
         --cin.l;

         cout.s = NULL;
         cout.l = 0;
         if (convert == CONV_FROMB64) {
            if(!b64_decode_header(&cout, &cin))
               n_str_assign_cp(&cout, _("[Invalid Base64 encoding]"));
         }else if(!qp_decode_header(&cout, &cin))
            n_str_assign_cp(&cout, _("[Invalid Quoted-Printable encoding]"));
         /* Normalize all decoded newlines to spaces XXX only \0/\n yet */
         /* C99 */{
            char const *xcp;
            bool_t any;
            uiz_t i, j;

            for(any = FAL0, i = cout.l; i-- != 0;)
               switch(cout.s[i]){
               case '\0':
               case '\n':
                  any = TRU1;
                  cout.s[i] = ' ';
                  /* FALLTHRU */
               default:
                  break;

               }

            if(any){
               /* I18N: must be non-empty, last must be closing bracket/xy */
               xcp = _("[Content normalized: ]");
               i = strlen(xcp);
               j = cout.l;
               n_str_add_buf(&cout, xcp, i);
               memmove(&cout.s[i - 1], cout.s, j);
               memcpy(&cout.s[0], xcp, i - 1);
               cout.s[cout.l - 1] = xcp[i - 1];
            }
         }


         out->l = lastenc;
#ifdef HAVE_ICONV
         /* TODO Does not really work if we have assigned some ASCII or even
          * TODO translated strings because of errors! */
         if ((flags & TD_ICONV) && fhicd != (iconv_t)-1) {
            cin.s = NULL, cin.l = 0; /* XXX string pool ! */
            convert = n_iconv_str(fhicd, n_ICONV_UNIDEFAULT, &cin, &cout, NULL);
            out = n_str_add(out, &cin);
            if (convert) {/* n_ERR_INVAL at EOS */
               n_iconv_reset(fhicd);
               out = n_str_add_buf(out, n_qm, 1); /* TODO unicode replacement */
            }
            n_free(cin.s);
         } else
#endif
            out = n_str_add(out, &cout);
         lastenc = lastoutl = out->l;
         n_free(cout.s);
      } else
jnotmime: {
         bool_t onlyws;

         p = op;
         onlyws = (lastenc > 0);
         for (;;) {
            if (++op == upper)
               break;
            if (op[0] == '=' && (PTRCMP(op + 1, ==, upper) || op[1] == '?'))
               break;
            if (onlyws && !blankchar(*op))
               onlyws = FAL0;
         }

         out = n_str_add_buf(out, p, PTR2SIZE(op - p));
         p = op;
         if (!onlyws || lastoutl != lastenc)
            lastenc = out->l;
         lastoutl = out->l;
      }
   }
   out->s[out->l] = '\0';

   if (flags & TD_ISPR) {
      makeprint(out, &cout);
      n_free(out->s);
      *out = cout;
   }
   if (flags & TD_DELCTRL)
      out->l = delctrl(out->s, out->l);
#ifdef HAVE_ICONV
   if (fhicd != (iconv_t)-1)
      n_iconv_close(fhicd);
#endif
jleave:
   NYD_OU;
   return;
}

FL char *
mime_fromaddr(char const *name)
{
   char const *cp, *lastcp;
   char *res = NULL;
   size_t ressz = 1, rescur = 0;
   NYD_IN;

   if (name == NULL)
      goto jleave;
   if (*name == '\0') {
      res = savestr(name);
      goto jleave;
   }

   if ((cp = routeaddr(name)) != NULL && cp > name) {
      _append_conv(&res, &ressz, &rescur, name, PTR2SIZE(cp - name));
      lastcp = cp;
   } else
      cp = lastcp = name;

   for ( ; *cp; ++cp) {
      switch (*cp) {
      case '(':
         _append_str(&res, &ressz, &rescur, lastcp, PTR2SIZE(cp - lastcp + 1));
         lastcp = ++cp;
         cp = skip_comment(cp);
         if (--cp > lastcp)
            _append_conv(&res, &ressz, &rescur, lastcp, PTR2SIZE(cp - lastcp));
         lastcp = cp;
         break;
      case '"':
         while (*cp) {
            if (*++cp == '"')
               break;
            if (*cp == '\\' && cp[1] != '\0')
               ++cp;
         }
         break;
      }
   }
   if (cp > lastcp)
      _append_str(&res, &ressz, &rescur, lastcp, PTR2SIZE(cp - lastcp));
   /* C99 */{
      char *x;

      x = res;
      res = savestrbuf(res, rescur);
      if(x != NULL)
         n_free(x);
   }
jleave:
   NYD_OU;
   return res;
}

FL ssize_t
xmime_write(char const *ptr, size_t size, FILE *f, enum conversion convert,
   enum tdflags dflags, struct str * volatile outrest,
   struct str * volatile inrest)
{
   ssize_t rv;
   struct quoteflt *qf;
   NYD_IN;

   quoteflt_reset(qf = quoteflt_dummy(), f);
   rv = mime_write(ptr, size, f, convert, dflags, qf, outrest, inrest);
   quoteflt_flush(qf);
   NYD_OU;
   return rv;
}

static sigjmp_buf       __mimemw_actjmp; /* TODO someday.. */
static int              __mimemw_sig; /* TODO someday.. */
static sighandler_type  __mimemw_opipe;
static void
__mimemw_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __mimemw_sig = sig;
   siglongjmp(__mimemw_actjmp, 1);
}

FL ssize_t
mime_write(char const *ptr, size_t size, FILE *f,
   enum conversion convert, enum tdflags volatile dflags,
   struct quoteflt *qf, struct str * volatile outrest,
   struct str * volatile inrest)
{
   /* TODO note: after send/MIME layer rewrite we will have a string pool
    * TODO so that memory allocation count drops down massively; for now,
    * TODO v14.0 that is, we pay a lot & heavily depend on the allocator.
    * TODO P.S.: furthermore all this encapsulated in filter objects instead */
   struct str in, out;
   ssize_t volatile sz;
   NYD_IN;

   dflags |= _TD_BUFCOPY;
   in.s = n_UNCONST(ptr);
   in.l = size;
   out.s = NULL;
   out.l = 0;

   if((sz = size) == 0){
      if(inrest != NULL && inrest->l != 0)
         goto jinrest;
      if(outrest != NULL && outrest->l != 0)
         goto jconvert;
      goto jleave;
   }

   /* TODO This crap requires linewise input, then.  We need a filter chain
    * TODO as in input->iconv->base64 where each filter can have its own
    * TODO buffer, with a filter->fflush() call to get rid of those! */
#ifdef HAVE_ICONV
   if ((dflags & TD_ICONV) && iconvd != (iconv_t)-1 &&
         (convert == CONV_TOQP || convert == CONV_8BIT ||
         convert == CONV_TOB64 || convert == CONV_TOHDR)) {
      if (n_iconv_str(iconvd, n_ICONV_NONE, &out, &in, NULL) != 0) {
         n_iconv_reset(iconvd);
         /* TODO This causes hard-failure.  We would need to have an action
          * TODO policy FAIL|IGNORE|SETERROR(but continue) */
         sz = -1;
         goto jleave;
      }
      in = out;
      out.s = NULL;
      dflags &= ~_TD_BUFCOPY;
   }
#endif

jinrest:
   if(inrest != NULL && inrest->l > 0){
      if(size == 0){
         in = *inrest;
         inrest->s = NULL;
         inrest->l = 0;
      }else{
         out.s = n_alloc(in.l + inrest->l + 1);
         memcpy(out.s, inrest->s, inrest->l);
         if(in.l > 0)
            memcpy(&out.s[inrest->l], in.s, in.l);
         if(in.s != ptr)
            n_free(in.s);
         (in.s = out.s)[in.l += inrest->l] = '\0';
         inrest->l = 0;
         out.s = NULL;
      }
      dflags &= ~_TD_BUFCOPY;
   }

jconvert:
   __mimemw_sig = 0;
   __mimemw_opipe = safe_signal(SIGPIPE, &__mimemw_onsig);
   if (sigsetjmp(__mimemw_actjmp, 1))
      goto jleave;

   switch (convert) {
   case CONV_FROMQP:
      if(!qp_decode_part(&out, &in, outrest, inrest)){
         n_err(_("Invalid Quoted-Printable encoding ignored\n"));
         sz = 0; /* TODO sz = -1 stops outer levels! */
         break;
      }
      goto jqpb64_dec;
   case CONV_TOQP:
      if(qp_encode(&out, &in, QP_NONE) == NULL){
         sz = 0; /* TODO sz = -1 stops outer levels! */
         break;
      }
      goto jqpb64_enc;
   case CONV_8BIT:
      sz = quoteflt_push(qf, in.s, in.l);
      break;
   case CONV_FROMB64:
      if(!b64_decode_part(&out, &in, outrest, inrest))
         goto jeb64;
      outrest = NULL;
      if(0){
      /* FALLTHRU */
   case CONV_FROMB64_T:
         if(!b64_decode_part(&out, &in, outrest, inrest)){
jeb64:
            n_err(_("Invalid Base64 encoding ignored\n"));
            sz = 0; /* TODO sz = -1 stops outer levels! */
            break;
         }
      }
jqpb64_dec:
      if ((sz = out.l) != 0)
         sz = _fwrite_td(&out, FAL0, (dflags & ~_TD_BUFCOPY), outrest, qf);
      break;
   case CONV_TOB64:
      /* TODO hack which is necessary unless this is a filter based approach
       * TODO and each filter has its own buffer (as necessary): we must not
       * TODO pass through a number of bytes which causes padding, otherwise we
       * TODO produce multiple adjacent base64 streams, and that is not treated
       * TODO in the same relaxed fashion like completely bogus bytes by at
       * TODO least mutt and OpenSSL.  So we need an expensive workaround
       * TODO unless we have input->iconv->base64 filter chain as such!! :( */
      if(size != 0 && /* for Coverity, else assert() */ inrest != NULL){
         if(in.l > B64_ENCODE_INPUT_PER_LINE){
            size_t i;

            i = in.l % B64_ENCODE_INPUT_PER_LINE;
            in.l -= i;

            if(i != 0){
               assert(inrest->l == 0);
               inrest->s = n_realloc(inrest->s, i +1);
               memcpy(inrest->s, &in.s[in.l], i);
               inrest->s[inrest->l = i] = '\0';
            }
         }else if(in.l < B64_ENCODE_INPUT_PER_LINE){
            inrest->s = n_realloc(inrest->s, in.l +1);
            memcpy(inrest->s, in.s, in.l);
            inrest->s[inrest->l = in.l] = '\0';
            in.l = 0;
            sz = 0;
            break;
         }
      }
      if(b64_encode(&out, &in, B64_LF | B64_MULTILINE) == NULL){
         sz = -1;
         break;
      }
jqpb64_enc:
      sz = fwrite(out.s, sizeof *out.s, out.l, f);
      if (sz != (ssize_t)out.l)
         sz = -1;
      break;
   case CONV_FROMHDR:
      mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV | (dflags & TD_DELCTRL));
      sz = quoteflt_push(qf, out.s, out.l);
      break;
   case CONV_TOHDR:
      sz = mime_write_tohdr(&in, f, NULL, a_MIME_SH_NONE);
      break;
   case CONV_TOHDR_A:{
      size_t col;

      if(dflags & _TD_BUFCOPY){
         n_str_dup(&out, &in);
         in = out;
         out.s = NULL;
         dflags &= ~_TD_BUFCOPY;
      }
      col = 0;
      sz = mime_write_tohdr_a(&in, f, &col, a_MIME_SH_NONE);
      }break;
   default:
      sz = _fwrite_td(&in, TRU1, dflags, NULL, qf);
      break;
   }

jleave:
   if (out.s != NULL)
      n_free(out.s);
   if (in.s != ptr)
      n_free(in.s);
   safe_signal(SIGPIPE, __mimemw_opipe);
   if (__mimemw_sig != 0)
      n_raise(__mimemw_sig);
   NYD_OU;
   return sz;
}

/* s-it-mode */
