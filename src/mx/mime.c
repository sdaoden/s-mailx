/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME support functions.
 *@ TODO Complete rewrite.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE mime
#define mx_SOURCE
#define mx_SOURCE_MIME

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/utf.h>

/* TODO nonsense (should be filter chain!) */
#include "mx/filter-quote.h"
#include "mx/iconv.h"
#include "mx/mime-enc.h"
#include "mx/names.h"
#include "mx/sigs.h"
#include "mx/ui-str.h"

#include "mx/mime.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Don't ask, but it keeps body and soul together */
enum a_mime_structure_hack{
   a_MIME_SH_NONE,
   a_MIME_SH_COMMENT,
   a_MIME_SH_QUOTE
};

static char *a_mime_cs_iter_base, *a_mime_cs_iter;
#ifdef mx_HAVE_ICONV
# define a_MIME_CS_ITER_GET() \
   ((a_mime_cs_iter != NIL) ? a_mime_cs_iter : n_var_oklook(CHARSET_8BIT_OKEY))
#else
# define a_MIME_CS_ITER_GET() \
   ((a_mime_cs_iter != NIL) ? a_mime_cs_iter : ok_vlook(ttycharset))
#endif
#define a_MIME_CS_ITER_STEP() \
   a_mime_cs_iter = su_cs_sep_c(&a_mime_cs_iter_base, ',', TRU1)

/* fwrite(3) while checking for displayability */
static sz a_mime_fwrite_display(struct str const *input,
      BITENUM(u32,mx_mime_display_flags) flags,
      struct str *outrest, struct quoteflt *qf);

/* Convert header fields to RFC 2047 format and write to the file fo */
static sz a_mime_write_tohdr(struct str *in, FILE *fo, uz *colp,
      enum a_mime_structure_hack msh);

#ifdef mx_HAVE_ICONV
static sz a_mime__convhdra(struct str *inp, FILE *fp, uz *colp,
      enum a_mime_structure_hack msh);
#else
# define a_mime__convhdra(S,F,C,MSH) a_mime_write_tohdr(S, F, C, MSH)
#endif

/* Write an address to a header field */
static sz a_mime_write_tohdr_a(struct str *in, FILE *f, uz *colp,
      enum a_mime_structure_hack msh);

/* Append to buf, handling resizing */
static void a_mime_append_str(char **buf, uz *size, uz *pos, char const *str,
      uz len);
static void a_mime_append_conv(char **buf, uz *size, uz *pos, char const *str,
      uz len);

static sigjmp_buf __mimefwtd_actjmp; /* TODO someday.. */
static int __mimefwtd_sig; /* TODO someday.. */
static n_sighdl_t  __mimefwtd_opipe;
static void
__mimefwtd_onsig(int sig){ /* TODO someday, we won't need it no more */
   NYD; /* Signal handler */
   __mimefwtd_sig = sig;
   siglongjmp(__mimefwtd_actjmp, 1);
}

static sz
a_mime_fwrite_display(struct str const *input,
      BITENUM(u32,mx_mime_display_flags) flags,
      struct str *outrest, struct quoteflt *qf){
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
   sz rv;
   NYD_IN;
   UNUSED(outrest);

   in = *input;
   out.s = NIL;
   out.l = 0;

#ifdef mx_HAVE_ICONV
   if((flags & mx_MIME_DISPLAY_ICONV) && iconvd != R(iconv_t,-1)){
      int err;
      char *buf;

      ASSERT(!ok_blook(iconv_disable));
      buf = NIL;

      if(outrest != NIL && outrest->l > 0){
         in.l = outrest->l + input->l;
         in.s = buf = su_ALLOC(in.l +1);
         su_mem_copy(in.s, outrest->s, outrest->l);
         su_mem_copy(&in.s[outrest->l], input->s, input->l);
         outrest->l = 0;
      }

      rv = 0;

      /* TODO Sigh, no problem if we have a filter that has a buffer (or
       * TODO become fed with entire lines, whatever), but for now we need
       * TODO to ensure we pass entire lines from in here to iconv(3), because
       * TODO the Citrus iconv(3) will fail tests with stateful encodings
       * TODO if we do not (only seen on FreeBSD) */
#if 0 /* TODO actually not needed indeed, it was known iswprint() error! */
      if(!(flags & mx__MIME_DISPLAY_EOF) && outrest != NIL){
         uz i, j;
         char const *cp;

         if((cp = su_mem_find(in.s, '\n', j = in.l)) != NIL){
            i = P2UZ(cp - in.s);
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

      if((err = n_iconv_str(iconvd, n_ICONV_UNIDEFAULT, &out, &in, &in)) != 0){
         if(err != su_ERR_INVAL)
            n_iconv_reset(iconvd);

         if(outrest != NIL && in.l > 0){
            /* Incomplete multibyte at EOF is special xxx _INVAL? */
            if(flags & mx__MIME_DISPLAY_EOF){
               out.s = su_REALLOC(out.s, out.l + sizeof(su_utf8_replacer));
               if(n_psonce & n_PSO_UNICODE){
                  su_mem_copy(&out.s[out.l], su_utf8_replacer,
                     sizeof(su_utf8_replacer) -1);
                  out.l += sizeof(su_utf8_replacer) -1;
               }else
                  out.s[out.l++] = '?';
            } else
               n_str_add(outrest, &in);
         }else
            rv = -1;
      }
      in = out;
      out.l = 0;
      out.s = NIL;
      flags &= ~mx__MIME_DISPLAY_BUF_CONST;

      if(buf != NIL)
         su_FREE(buf);
      if(rv < 0)
         goto jleave;
   }else
#endif /* mx_HAVE_ICONV */
   /* Else, if we will modify the data bytes and thus introduce the potential
    * of messing up multibyte sequences which become split over buffer
    * boundaries TODO and unless we do not have our filter chain which will
    * TODO make these hacks go by, buffer data until we see a NL */
   if((flags & (mx_MIME_DISPLAY_ISPRINT | mx_MIME_DISPLAY_DEL_CNTRL)) &&
         outrest != NIL &&
#ifdef mx_HAVE_ICONV
         iconvd == R(iconv_t,-1) &&
#endif
         (!(flags & mx__MIME_DISPLAY_EOF) || outrest->l > 0)){
      uz i;
      char *cp;

      for(cp = &in.s[in.l]; cp > in.s && cp[-1] != '\n'; --cp)
         ;
      i = P2UZ(cp - in.s);

      if(i != in.l){
         if(i > 0){
            n_str_assign_buf(outrest, cp, in.l - i);
            cp = su_ALLOC(i +1);
            su_mem_copy(cp, in.s, in.l = i);
            (in.s = cp)[in.l = i] = '\0';
            flags &= ~mx__MIME_DISPLAY_BUF_CONST;
         } else {
            n_str_add_buf(outrest, input->s, input->l);
            rv = 0;
            goto jleave;
         }
      }
   }

   if(flags & mx_MIME_DISPLAY_ISPRINT)
      mx_makeprint(&in, &out);
   else if(flags & mx__MIME_DISPLAY_BUF_CONST)
      n_str_dup(&out, &in);
   else
      out = in;
   if(flags & mx_MIME_DISPLAY_DEL_CNTRL)
      out.l = mx_del_cntrl(out.s, out.l);

   __mimefwtd_sig = 0;
   __mimefwtd_opipe = safe_signal(SIGPIPE, &__mimefwtd_onsig);
   if(sigsetjmp(__mimefwtd_actjmp, 1)){
      rv = 0;
      goto j__sig;
   }

   rv = quoteflt_push(qf, out.s, out.l);

j__sig:
   if(out.s != in.s)
      su_FREE(out.s);
   if(in.s != input->s)
      su_FREE(in.s);

   safe_signal(SIGPIPE, __mimefwtd_opipe);
   if(__mimefwtd_sig != 0)
      n_raise(__mimefwtd_sig);

jleave:
   NYD_OU;
   return rv;
}

static sz
a_mime_write_tohdr(struct str *in, FILE *fo, uz *colp,
      enum a_mime_structure_hack msh){
   /* TODO a_mime_write_tohdr(): we don't know the name of our header->maxcol..
    * TODO  MIME/send layer rewrite: more available state!!
    * TODO   Because of this we cannot make a difference in between structured
    * TODO   and unstructured headers (RFC 2047, 5. (2))
    * TODO   This means, e.g., that this gets called multiple times for a
    * TODO   structured header and always starts thinking it is at column 0.
    * TODO   I.e., it may get called for only the content of a comment etc.,
    * TODO   not knowing anything of its context.
    * TODO   Instead we should have a list of header body content tokens,
    * TODO   convert them, and then dump the converted tokens, breaking lines.
    * TODO   I.e., get rid of convhdra, a_mime_write_tohdr_a and such...
    * TODO   Somewhen, the following should produce smooth stuff:
    * TODO   '  "Hallo\"," Dr. Backe "Bl\"ö\"d" (Gell) <ha@llöch.en>
    * TODO    "Nochm\"a\"l"<ta@tu.da>(Dümm)'
    * TODO NOT MULTIBYTE SAFE IF AN ENCODED WORD HAS TO BE SPLIT!
    * TODO  To be better we had to mbtowc_l() (non-std! and no locale!!) and
    * TODO   work char-wise!  ->  S-CText..
    * TODO  The real problem for STD compatibility is however that "in" is
    * TODO   already iconv(3) encoded to the target character set!  We could
    * TODO   also solve it (very expensively!) if we would narrow down to an
    * TODO   encoded word and then iconv(3)+MIME encode in one go, in which
    * TODO   case multibyte errors could be caught! */
   enum{
      /* Maximum line length */
      a_MAXCOL_NENC = MIME_LINELEN,
      a_MAXCOL = MIME_LINELEN_RFC2047
   };

   enum a_flags{
      a_FIRST = 1u<<0, /* Nothing written yet, start of string */
      a_MSH_NOTHING = 1u<<1, /* Now, really: nothing at all has been written */
      a_ANYENC = 1u<<2, /* We have RFC 2047 anything at least once */
      a_NO_QP = 1u<<3, /* No quoted-printable allowed */
      a_NO_B64 = 1u<<4,  /* Ditto, base64 */
      a_ENC_LAST = 1u<<5, /* Last round generated encoded word */
      a_SHOULD_BEE = 1u<<6, /* Avoid lines longer than SHOULD via encoding */
      a_RND_SHIFT = 7,
      a_RND_MASK = (1u<<a_RND_SHIFT) - 1,
      a_SPACE = 1u<<(a_RND_SHIFT+1), /* Leading whitespace */
      a_8BIT = 1u<<(a_RND_SHIFT+2), /* High bit set */
      a_ENCODE = 1u<<(a_RND_SHIFT+3), /* Need encoding */
      a_ENC_B64 = 1u<<(a_RND_SHIFT+4), /* - let it be base64 */
      a_IF_ENC_NO_B64 = 1<<(a_RND_SHIFT+5), /* - NO! MUST NOT be base64 */
      a_OVERLONG = 1u<<(a_RND_SHIFT+6) /* Temporarily raised limit */
   };

   struct str cout, cin;
   char const *cset7, *cset8, *wbot, *upper, *wend, *wcur;
   u32 cset7_len, cset8_len;
   uz col, i, j;
   sz size;
   BITENUM(u32,a_flags) flags;
   NYD_IN;

   cout.s = NIL, cout.l = 0;
   cset7 = ok_vlook(charset_7bit);
   cset7_len = S(u32,su_cs_len(cset7));
   cset8 = a_MIME_CS_ITER_GET(); /* TODO MIME/send layer: iter? iter! else */
   cset8_len = S(u32,su_cs_len(cset8));

   flags = a_FIRST;
   if(msh != a_MIME_SH_NONE)
      flags |= a_MSH_NOTHING;

   /* RFC 1468, "MIME Considerations":
    *     ISO-2022-JP may also be used in MIME Part 2 headers.  The "B"
    *     encoding should be used with ISO-2022-JP text. */
   /* TODO of course, our current implementation won't deal properly with
    * TODO any stateful encoding at all... (the standard says each encoded
    * TODO word must include all necessary reset sequences..., i.e., each
    * TODO encoded word must be a self-contained iconv(3) life cycle) */
   if(!su_cs_cmp_case(cset8, "iso-2022-jp") ||
         mx_mime_enc_target() == mx_MIME_ENC_B64)
      flags |= a_NO_QP;

   wbot = in->s;
   upper = &wbot[in->l];
   size = 0;

   if(colp == NIL || (col = *colp) == 0)
      col = sizeof("Mail-Followup-To: ") -1; /* TODO dreadful thing */

   /* The user may specify empty quoted-strings or comments, keep them! */
   if(wbot == upper){
      if(flags & a_MSH_NOTHING){
         flags &= ~a_MSH_NOTHING;
         putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
         size = 1;
         ++col;
      }
   }else for(; wbot < upper; flags &= ~a_FIRST, wbot = wend){
      flags &= a_RND_MASK;
      wcur = wbot;
      while(wcur < upper && su_cs_is_white(*wcur)){
         flags |= a_SPACE;
         ++wcur;
      }

      /* Any occurrence of whitespace resets prevention of lines >SHOULD via
       * enforced encoding (xxx SHOULD, but.. encoding is expensive!!) */
      if(flags & a_SPACE)
         flags &= ~a_SHOULD_BEE;

     /* Data ends with WS - dump it and done.
      * Also, if we have seen multiple successive whitespace characters, then
      * if there was no encoded word last, i.e., if we can simply take them
      * over to the output as-is, keep one WS for possible later separation
      * purposes and simply print the others as-is, directly! */
      if(wcur == upper){
         wend = wcur;
         goto jnoenc_putws;
      }
      if((flags & (a_ENC_LAST | a_SPACE)) == a_SPACE && wcur - wbot > 1){
         wend = &wcur[-1];
         goto jnoenc_putws;
      }

      /* Skip over a word to next non-whitespace, keep track along the way
       * whether our 7-bit charset suffices to represent the data */
      for(wend = wcur; wend < upper; ++wend){
         if(su_cs_is_white(*wend))
            break;
         if(S(uc,*wend) & 0x80)
            flags |= a_8BIT;
         /* pure RFC 5322 need to parse these plain */
         else if(*wend == '"' || *wend == '(' || *wend == ')')
            flags |= a_IF_ENC_NO_B64;
      }

      /* Decide whether the range has to become encoded or not */
      i = P2UZ(wend - wcur);
      j = mx_mime_enc_mustquote(wcur, i, TRU1);
      /* If it just cannot fit on a SHOULD line length, force encode */
      if(i > a_MAXCOL_NENC){
         flags |= a_SHOULD_BEE; /* (Sigh: SHOULD only, not MUST..) */
         goto j_beejump;
      }
      if((flags & a_SHOULD_BEE) || j > 0){
j_beejump:
         flags |= a_ENCODE;
         /* Use base64 if requested or more than 50% -37.5-% of the bytes of
          * the string need to be encoded */
         if(flags & a_IF_ENC_NO_B64)
            flags &= ~a_ENC_B64;
         else if((flags & a_NO_QP) || j >= i >> 1)/*(i >> 2) + (i >> 3))*/
            flags |= a_ENC_B64;
      }
      DVLDBG( if(flags & a_8BIT) ASSERT(flags & a_ENCODE); )

      if(!(flags & a_ENCODE)){
         /* Encoded word produced, but no linear whitespace for necessary RFC
          * 2047 separation?  Generate artificial data (bad standard!) */
         if((flags & (a_ENC_LAST | a_SPACE)) == a_ENC_LAST){
            if(col >= a_MAXCOL){
               putc('\n', fo);
               ++size;
               col = 0;
            }
            if(flags & a_MSH_NOTHING){
               flags &= ~a_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++size;
               ++col;
            }
            putc(' ', fo);
            ++size;
            ++col;
         }

jnoenc_putws:
         flags &= ~a_ENC_LAST;

         /* todo No effort here: (1) v15.0 has to bring complete rewrite,
          * todo (2) the standard is "braindead" and (3) usually this is one
          * todo word only, and why be smarter than the standard? */
jnoenc_retry:
         i = P2UZ(wend - wbot);
         if(i + col + ((flags & a_MSH_NOTHING) != 0) <=
                  (flags & a_OVERLONG ? MIME_LINELEN_MAX
                   : (flags & a_ANYENC ? a_MAXCOL : a_MAXCOL_NENC))) {
            if(flags & a_MSH_NOTHING){
               flags &= ~a_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++size;
               ++col;
            }
            i = fwrite(wbot, sizeof *wbot, i, fo);
            size += i;
            col += i;
            continue;
         }

         /* Does not fit, try to break the line first; */
         if(col > 1){
            putc('\n', fo);
            if(su_cs_is_white(*wbot)){
               putc(S(uc,*wbot), fo);
               ++wbot;
            }else
               putc(' ', fo); /* Bad standard: artificial data! */
            size += 2;
            col = 1;
            if(flags & a_MSH_NOTHING){
               flags &= ~a_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++size;
               ++col;
            }
            flags |= a_OVERLONG;
            goto jnoenc_retry;
         }

         /* It is so long that it needs to be broken, effectively causing
          * artificial spaces to be inserted (bad standard), yuck */
         /* todo This is not multibyte safe, as above; and completely stupid
          * todo P.S.: our _SHOULD_BEE prevents these cases in the meanwhile */
/* FIXME n_PSO_UNICODE and parse using UTF-8 sync possibility! */
         wcur = &wbot[MIME_LINELEN_MAX - 8];
         while(wend > wcur)
            wend -= 4;
         goto jnoenc_retry;
      }else{
         /* Encoding to encoded word(s); deal with leading whitespace, place
          * a separator first as necessary: encoded words must always be
          * separated from text and other encoded words with linear WS.
          * And if an encoded word was last, intermediate whitespace must
          * also be encoded, otherwise it would get stripped away! */
         wcur = n_UNCONST(n_empty);
         if((flags & (a_ENC_LAST | a_SPACE)) != a_SPACE){
            /* Reinclude whitespace */
            flags &= ~a_SPACE;
            /* We do not need to place a separator at the very beginning */
            if(!(flags & a_FIRST))
               wcur = UNCONST(char*," ");
         }else
            wcur = wbot++;

         flags |= a_ANYENC | a_ENC_LAST;
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
         cin.s = UNCONST(char*,wbot);
         cin.l = P2UZ(wend - wbot);

         /* C99 */{
            struct str *xout;

            if(flags & a_ENC_B64)
               xout = mx_b64_enc(&cout, &cin,
                     mx_B64_ISHEAD | mx_B64_ISENCWORD);
            else
               xout = mx_qp_enc(&cout, &cin, mx_QP_ISHEAD | mx_QP_ISENCWORD);
            if(xout == NIL){
               size = -1;
               break;
            }
            j = xout->l;
         }
         /* (Avoid ISO C trigraphs in the RFC 2047 placeholder..) */
         i = j + (flags & a_8BIT ? cset8_len : cset7_len) +
               sizeof("=!!B!!=") -1;
         if(*wcur != '\0')
            ++i;

jenc_retry_same:
         /* Unfortunately RFC 2047 explicitly disallows encoded words to be
          * longer (just like RFC 5322's "a line SHOULD fit in 78 but MAY be
          * 998 characters long"), so we cannot use the _OVERLONG mechanism,
          * even though all tested mailers seem to support it */
         if(i + col <= (/*flags & a_OVERLONG ? MIME_LINELEN_MAX :*/ a_MAXCOL)){
            if(flags & a_MSH_NOTHING){
               flags &= ~a_MSH_NOTHING;
               putc((msh == a_MIME_SH_COMMENT ? '(' : '"'), fo);
               ++size;
               ++col;
            }
            fprintf(fo, "%.1s=?%s?%c?%.*s?=",
               wcur, (flags & a_8BIT ? cset8 : cset7),
               (flags & a_ENC_B64 ? 'B' : 'Q'),
               S(int,cout.l), cout.s);
            size += i;
            col += i;
            continue;
         }

         /* Does not fit, try to break the line first */
         /* TODO I have commented out the _FIRST test since we (1) cannot do
          * TODO _OVERLONG since (MUAs support but) the standard disallows,
          * TODO and because of our iconv problem i prefer an empty first line
          * TODO in favour of a possibly messed up multibytes character. :-( */
         if(col > 1 /* TODO && !(flags & _FIRST)*/){
            putc('\n', fo);
            size += 2;
            col = 1;
            if(!(flags & a_SPACE)){
               putc(' ', fo);
               wcur = UNCONST(char*,su_empty);
               /*flags |= a_OVERLONG;*/
               goto jenc_retry_same;
            }else{
               putc(S(uc,*wcur), fo);
               if(su_cs_is_white(*(wcur = wbot)))
                  ++wbot;
               else{
                  flags &= ~a_SPACE;
                  wcur = UNCONST(char*,su_empty);
               }
               /*flags &= ~a_OVERLONG;*/
               goto jenc_retry;
            }
         }

         /* It is so long that it needs to be broken, effectively causing
          * artificial data to be inserted (bad standard), yuck */
         /* todo This is not multibyte safe, as above */
         /*if(!(flags & a_OVERLONG)) { Mechanism explicitly forbidden by 2047
            flags |= a_OVERLONG;
            goto jenc_retry;
         }*/

/* FIXME n_PSO_UNICODE and parse using UTF-8 sync possibility! */
         i = P2UZ(wend - wbot) + !!(flags & a_SPACE);
         j = 3 + !(flags & a_ENC_B64);
         for(;;){
            wend -= j;
            i -= j;
            /* (Note the problem most likely is the transfer-encoding blow,
             * which is why we test this *after* the decrements.. */
            if(i <= a_MAXCOL)
               break;
         }
         goto jenc_retry;
      }
   }

   if(!(flags & a_MSH_NOTHING) && msh != a_MIME_SH_NONE){
      putc((msh == a_MIME_SH_COMMENT ? ')' : '"'), fo);
      ++size;
      ++col;
   }

   if(cout.s != NIL)
      su_FREE(cout.s);

   if(colp != NIL)
      *colp = col;

   NYD_OU;
   return size;
}

#ifdef mx_HAVE_ICONV
static sz
a_mime__convhdra(struct str *inp, FILE *fp, uz *colp,
      enum a_mime_structure_hack msh){
   struct str ciconv;
   sz rv;
   NYD_IN;

   rv = 0;
   ciconv.s = NIL;

   if(inp->l > 0 && iconvd != R(iconv_t,-1)){
      ASSERT(!ok_blook(iconv_disable));
      ciconv.l = 0;
      if(n_iconv_str(iconvd, n_ICONV_NONE, &ciconv, inp, NIL) != 0){
         n_iconv_reset(iconvd);
         rv = -1;
         goto jleave;
      }
      *inp = ciconv;
   }

   rv = a_mime_write_tohdr(inp, fp, colp, msh);

jleave:
   if(ciconv.s != NIL)
      su_FREE(ciconv.s);

   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_ICONV */

static sz
a_mime_write_tohdr_a(struct str *in, FILE *f, uz *colp,
      enum a_mime_structure_hack msh){
   struct str xin;
   uz i;
   char const *cp, *lastcp;
   sz size, x;
   NYD_IN;

   in->s[in->l] = '\0';

   if((cp = routeaddr(lastcp = in->s)) != NIL && cp > lastcp){
      xin.s = UNCONST(char*,lastcp);
      xin.l = P2UZ(cp - lastcp);
      if((size = a_mime__convhdra(&xin, f, colp, msh)) < 0)
         goto jleave;
      lastcp = cp;
   }else{
      cp = lastcp;
      size = 0;
   }

   for( ; *cp != '\0'; ++cp){
      switch(*cp){
      case '(':
         i = P2UZ(cp - lastcp);
         if(i > 0){
            if(fwrite(lastcp, 1, i, f) != i)
               goto jerr;
            size += i;
         }
         lastcp = ++cp;
         cp = skip_comment(cp);
         if(cp > lastcp)
            --cp;
         /* We want to keep empty comments, too! */
         xin.s = UNCONST(char*,lastcp);
         xin.l = P2UZ(cp - lastcp);
         if((x = a_mime__convhdra(&xin, f, colp, a_MIME_SH_COMMENT)) < 0)
            goto jerr;
         size += x;
         lastcp = &cp[1];
         break;
      case '"':
         i = P2UZ(cp - lastcp);
         if(i > 0){
            if(fwrite(lastcp, 1, i, f) != i)
               goto jerr;
            size += i;
         }
         for(lastcp = ++cp; *cp != '\0'; ++cp){
            if(*cp == '"')
               break;
            if(*cp == '\\' && cp[1] != '\0')
               ++cp;
         }
         /* We want to keep empty quoted-strings, too! */
         xin.s = UNCONST(char*,lastcp);
         xin.l = P2UZ(cp - lastcp);
         if((x = a_mime__convhdra(&xin, f, colp, a_MIME_SH_QUOTE)) < 0)
            goto jerr;
         size += x;
         ++size;
         lastcp = &cp[1];
         break;
      }
   }

   i = P2UZ(cp - lastcp);
   if(i > 0){
      if(fwrite(lastcp, 1, i, f) != i)
         goto jerr;
      size += i;
   }

jleave:
   NYD_OU;
   return size;
jerr:
   size = -1;
   goto jleave;
}

static void
a_mime_append_str(char **buf, uz *size, uz *pos, char const *str, uz len){
   NYD_IN;

   *buf = su_REALLOC(*buf, *size += len);
   su_mem_copy(&(*buf)[*pos], str, len);
   *pos += len;

   NYD_OU;
}

static void
a_mime_append_conv(char **buf, uz *size, uz *pos, char const *str, uz len){
   struct str in, out;
   NYD2_IN;

   in.s = UNCONST(char*,str);
   in.l = len;

   if(mx_mime_display_from_header(&in, &out,
         mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT)){
      a_mime_append_str(buf, size, pos, out.s, out.l);
      su_FREE(out.s);
   }

   NYD2_OU;
}

boole
mx_mime_charset_iter_reset(char const *a_charset_to_try_first){ /* TODO dups */
   char const *sarr[3];
   uz sarrl[3], len;
   char *cp;
   NYD_IN;
   UNUSED(a_charset_to_try_first);

#ifdef mx_HAVE_ICONV
   if(!ok_blook(iconv_disable)){
      sarr[2] = n_var_oklook(CHARSET_8BIT_OKEY);

      if(a_charset_to_try_first != NIL &&
            su_cs_cmp(a_charset_to_try_first, sarr[2]))
         sarr[0] = a_charset_to_try_first;
      else
         sarr[0] = NIL;

      if((sarr[1] = ok_vlook(sendcharsets)) == NIL &&
            ok_blook(sendcharsets_else_ttycharset)){
         cp = UNCONST(char*,ok_vlook(ttycharset));
         if(su_cs_cmp(cp, sarr[2]) && (sarr[0] == NIL || su_cs_cmp(cp, sarr[0])))
            sarr[1] = cp;
      }
   }else
      sarr[0] = sarr[1] = NIL,
#endif
      sarr[2] = ok_vlook(ttycharset);

   sarrl[2] = len = su_cs_len(sarr[2]);
#ifdef mx_HAVE_ICONV
   if((cp = UNCONST(char*,sarr[1])) != NIL)
      len += (sarrl[1] = su_cs_len(cp));
   else
      sarrl[1] = 0;
   if((cp = UNCONST(char*,sarr[0])) != NIL)
      len += (sarrl[0] = su_cs_len(cp));
   else
      sarrl[0] = 0;
#endif

   a_mime_cs_iter_base = cp = su_AUTO_ALLOC(len + 1 + 1 +1);

#ifdef mx_HAVE_ICONV
   if((len = sarrl[0]) != 0){
      su_mem_copy(cp, sarr[0], len);
      cp[len] = ',';
      cp += ++len;
   }
   if((len = sarrl[1]) != 0){
      su_mem_copy(cp, sarr[1], len);
      cp[len] = ',';
      cp += ++len;
   }
#endif
   len = sarrl[2];
   su_mem_copy(cp, sarr[2], len);
   cp[len] = '\0';

   a_MIME_CS_ITER_STEP();

   NYD_OU;
   return (a_mime_cs_iter != NIL);
}

boole
mx_mime_charset_iter_next(void){
   boole rv;
   NYD2_IN;

   a_MIME_CS_ITER_STEP();
   rv = (a_mime_cs_iter != NIL);

   NYD2_OU;
   return rv;
}

boole
mx_mime_charset_iter_is_valid(void){
   boole rv;
   NYD2_IN;

   rv = (a_mime_cs_iter != NIL);

   NYD2_OU;
   return rv;
}

char const *
mx_mime_charset_iter(void){
   char const *rv;
   NYD2_IN;

   rv = a_mime_cs_iter;

   NYD2_OU;
   return rv;
}

char const *
mx_mime_charset_iter_or_fallback(void){
   char const *rv;
   NYD2_IN;

   rv = a_MIME_CS_ITER_GET();

   NYD2_OU;
   return rv;
}

void
mx_mime_charset_iter_recurse(char *outer_storage[2]){ /* TODO LEGACY REMOVE */
   NYD2_IN;

   outer_storage[0] = a_mime_cs_iter_base;
   outer_storage[1] = a_mime_cs_iter;

   NYD2_OU;
}

void
mx_mime_charset_iter_restore(char *outer_storage[2]){ /* TODO LEGACY REMOVE */
   NYD2_IN;

   a_mime_cs_iter_base = outer_storage[0];
   a_mime_cs_iter = outer_storage[1];

   NYD2_OU;
}

#ifdef mx_HAVE_ICONV
boole
mx_mime_header_needs_mime(char const *body, char const **charset_or_nil){
   char const *ttyc5t;
   boole rv;
   NYD2_IN;
   ASSERT(body != NIL);

   rv = FAL0;

   for(;;){
      char c;

      c = *body++;
      if(c == '\0')
         break;
      if(S(u8,c) & 0x80){
         rv = TRU1;
         if(charset_or_nil != NIL &&
               (ttyc5t = ok_vlook(ttycharset_detect)) != NIL)
            goto jslow;
         break;
      }
   }

jleave:
   NYD2_OU;
   return rv;

jslow:/* C99 */{
   uz l;

   l = su_cs_len(--body);
   for(;;){
      u32 utf;

      if((utf = su_utf8_to_32(&body, &l)) == U32_MAX){
         *charset_or_nil = (*ttyc5t != '\0') ? savestr/*xxx*/(ttyc5t) : NIL;
         break;
      }else if(utf == '\0'/* XXX ?? */ || l == 0){
         *charset_or_nil = su_utf8_name;
         break;
      }
   }
   }goto jleave;
}
#endif

boole
mx_mime_display_from_header(struct str const *in, struct str *out,
      BITENUM(u32,mx_mime_display_flags) flags){
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
    * TODO RFC 2047 and be renamed: mime_fromhdr() -> mime_rfc2047_decode()
    * TODO Does currently not really fail. */
   struct str cin, cout;
   u32 convert, lastenc, lastoutl;
   char *p, *upper, *op;
#ifdef mx_HAVE_ICONV
   char *cbeg;
   char const *tcs;
   iconv_t fhicd;
#endif
   boole rv;
   NYD_IN;

   rv = TRU1;
   out->l = 0;
   if(in->l == 0){
      *(out->s = su_ALLOC(1)) = '\0';
      goto jleave;
   }
   out->s = NIL;

   rv = FAL0;

#ifdef mx_HAVE_ICONV
   fhicd = R(iconv_t,-1);
   tcs = ok_vlook(ttycharset);
#endif
   p = in->s;
   upper = p + in->l;
   lastenc = lastoutl = 0;

   while(p < upper){
      op = p;
      if(*p == '=' && p[1] == '?'){
         p += 2;
#ifdef mx_HAVE_ICONV
         cbeg = p;
#endif
         while(p < upper && *p != '?')
            ++p;  /* strip charset */
         /* ?[bq]?..?= */
         if(&p[4] >= upper)
            goto Jnotmime;
         ++p;

#ifdef mx_HAVE_ICONV
         if((flags & mx_MIME_DISPLAY_ICONV) && !ok_blook(iconv_disable)){
            uz i;

            if(fhicd != R(iconv_t,-1)){
               n_iconv_close(fhicd);
               fhicd = R(iconv_t,-1);
            }

            i = P2UZ(p - cbeg) - 1;

            if(i > 0){
               char *cs, *ltag;

               cs = su_LOFI_ALLOC(i + 1);
               su_mem_copy(cs, cbeg, i);
               cs[i] = '\0';

               /* RFC 2231 extends the RFC 2047 character set definition in
                * encoded words by language tags - silently strip those off */
               if((ltag = su_cs_find_c(cs, '*')) != NIL)
                  *ltag = '\0';

               fhicd = su_cs_cmp_case(cs, tcs)
                     ? n_iconv_open(tcs, cs) : R(iconv_t,-1);

               su_LOFI_FREE(cs);
            }
         }
#endif /* mx_HAVE_ICONV */

         switch(*p){
         case 'B': case 'b':
            convert = CONV_FROMB64;
            break;
         case 'Q': case 'q':
            convert = CONV_FROMQP;
            break;
         default: /* invalid, ignore */
            goto Jnotmime;
         }
         if(*++p != '?')
            goto Jnotmime;

         cin.s = ++p;
         cin.l = 1;
         for(;;){
            if(PCMP(&p[1], >=, upper))
               goto Jnotmime;
            if(*p++ == '?' && *p == '=')
               break;
            ++cin.l;
         }
         ++p;
         /* Shortcut on empty POI; _ensure_ buffer */
         if(--cin.l == 0){
            out = n_str_add_buf(out, n_qm, 1);
            lastenc = lastoutl = --out->l;
            continue;
         }

         cout.s = NIL;
         cout.l = 0;

         if(convert == CONV_FROMB64){
            if(!mx_b64_dec_header(&cout, &cin))
               goto jdec_qm;
         }else if(!mx_qp_dec_header(&cout, &cin))
jdec_qm:
            n_str_assign_cp(&cout, n_qm);

         /* Normalize all decoded newlines to spaces XXX only \0/\n yet */
         /* C99 */{
            uz i;

            for(i = cout.l; i-- != 0;)
               switch(cout.s[i]){
               case '\0':
               case '\n':
                  cout.s[i] = ' ';
                  /* FALLTHRU */
               default:
                  break;
               }
         }

         out->l = lastenc;
#ifdef mx_HAVE_ICONV
         /* TODO Does not really work if we have assigned some ASCII or even
          * TODO translated strings because of errors! */
         if((flags & mx_MIME_DISPLAY_ICONV) && fhicd != R(iconv_t,-1)){
            ASSERT(!ok_blook(iconv_disable));
            cin.s = NIL, cin.l = 0; /* XXX string pool ! */
            convert = n_iconv_str(fhicd, n_ICONV_UNIDEFAULT, &cin, &cout, NIL);
            out = n_str_add(out, &cin);
            if(convert){/* su_ERR_INVAL at EOS */
               n_iconv_reset(fhicd);
               out = n_str_add_buf(out, n_qm, 1);/* TODO unicode replacement */
            }
            su_FREE(cin.s);
         }else
#endif
            out = n_str_add(out, &cout);
         lastenc = lastoutl = out->l;

         su_FREE(cout.s);
      }else Jnotmime:{
         boole onlyws;

         p = op;
         onlyws = (lastenc > 0);
         for(;;){
            if(++op == upper)
               break;
            if(op[0] == '=' && (op[1] == '?' || &op[1] == upper))
               break;
            if(onlyws && !su_cs_is_blank(*op))
               onlyws = FAL0;
         }

         out = n_str_add_buf(out, p, P2UZ(op - p));
         p = op;
         if(!onlyws || lastoutl != lastenc)
            lastenc = out->l;
         lastoutl = out->l;
      }
   }

   ASSERT(out->s != NIL);
   out->s[out->l] = '\0';

   if(flags & mx_MIME_DISPLAY_ISPRINT){
      mx_makeprint(out, &cout);
      su_FREE(out->s);
      *out = cout;
   }

   if(flags & mx_MIME_DISPLAY_DEL_CNTRL)
      out->l = mx_del_cntrl(out->s, out->l);

#ifdef mx_HAVE_ICONV
   if(fhicd != R(iconv_t,-1)){
      ASSERT(!ok_blook(iconv_disable));
      n_iconv_close(fhicd);
   }
#endif

   rv = TRU1;
jleave:
   if(!rv && out->s != NIL){
      su_FREE(out->s);
      out->s = NIL;
      out->l = 0;
   }

   NYD_OU;
   return rv;
}

char *
mx_mime_fromaddr(char const *name){
   char const *cp, *lastcp;
   uz ressz, rescur;
   char *res, c;
   NYD_IN;

   res = NIL;
   ressz = 1;
   rescur = 0;

   if(name == NIL)
      goto jleave;
   if(*name == '\0'){
      res = savestr(name);
      goto jleave;
   }

   if((cp = routeaddr(name)) != NIL && cp > name){
      a_mime_append_conv(&res, &ressz, &rescur, name, P2UZ(cp - name));
      lastcp = cp;
   }else
      cp = lastcp = name;

   for(; (c = *cp) != '\0'; ++cp){
      switch(c){
      case '(':
         a_mime_append_str(&res, &ressz, &rescur, lastcp,
            P2UZ(cp - lastcp + 1));
         lastcp = ++cp;
         cp = skip_comment(cp);
         if(--cp > lastcp)
            a_mime_append_conv(&res, &ressz, &rescur, lastcp,
               P2UZ(cp - lastcp));
         lastcp = cp;
         break;
      case '"':
         while(*cp != '\0'){
            if((c = *++cp) == '"')
               break;
            if(c == '\\' && cp[1] != '\0')
               ++cp;
         }
         break;
      }
   }

   if(cp > lastcp)
      a_mime_append_str(&res, &ressz, &rescur, lastcp, P2UZ(cp - lastcp));
   /* C99 */{
      char *x;

      x = res;
      res = savestrbuf(res, rescur);
      if(x != NIL)
         su_FREE(x);
   }

jleave:
   NYD_OU;
   return res;
}

sz
mx_xmime_write(char const *ptr, uz size, FILE *f, enum conversion convert,
      BITENUM(u32,mx_mime_display_flags) dflags,
      struct str * volatile outrest, struct str * volatile inrest){
   sz rv;
   struct quoteflt *qf;
   NYD_IN;

   quoteflt_reset(qf = quoteflt_dummy(), f);
   rv = mx_mime_write(ptr, size, f, convert, dflags, qf, outrest, inrest);
   quoteflt_flush(qf);

   NYD_OU;
   return rv;
}

static sigjmp_buf __mimemw_actjmp; /* TODO someday.. */
static int __mimemw_sig; /* TODO someday.. */
static n_sighdl_t __mimemw_opipe;
static void
__mimemw_onsig(int sig){ /* TODO someday, we won't need it no more */
   NYD; /* Signal handler */
   __mimemw_sig = sig;
   siglongjmp(__mimemw_actjmp, 1);
}

sz
mx_mime_write(char const *ptr, uz size, FILE *f, enum conversion convert,
      BITENUM(u32,mx_mime_display_flags) volatile dflags,
      struct quoteflt *qf, struct str * volatile outrest,
      struct str * volatile inrest){
   /* TODO note: after send/MIME layer rewrite we will have a string pool
    * TODO so that memory allocation count drops down massively; for now,
    * TODO v14.0 that is, we pay a lot & heavily depend on the allocator.
    * TODO P.S.: furthermore all this encapsulated in filter objects instead */
   struct str in, out;
   sz volatile xsize;
   NYD_IN;

   dflags |= mx__MIME_DISPLAY_BUF_CONST;
   in.s = UNCONST(char*,ptr);
   in.l = size;
   out.s = NIL;
   out.l = 0;

   if((xsize = size) == 0){
      if(inrest != NIL && inrest->l != 0)
         goto jinrest;
      if(outrest != NIL && outrest->l != 0)
         goto jconvert;
      goto jleave;
   }

   /* TODO This crap requires linewise input, then.  We need a filter chain
    * TODO as in input->iconv->base64 where each filter can have its own
    * TODO buffer, with a filter->fflush() call to get rid of those! */
#ifdef mx_HAVE_ICONV
   if((dflags & mx_MIME_DISPLAY_ICONV) && iconvd != R(iconv_t,-1) &&
         (convert == CONV_TOQP || convert == CONV_8BIT ||
         convert == CONV_TOB64 || convert == CONV_TOHDR)) {
      ASSERT(!ok_blook(iconv_disable));
      if(n_iconv_str(iconvd, n_ICONV_NONE, &out, &in, NIL) != 0){
         n_iconv_reset(iconvd);
         /* TODO This causes hard-failure.  We would need to have an action
          * TODO policy FAIL|IGNORE|SETERROR(but continue) */
         xsize = -1;
         goto jleave;
      }
      in = out;
      out.s = NIL;
      dflags &= ~mx__MIME_DISPLAY_BUF_CONST;
   }
#endif

jinrest:
   if(inrest != NIL && inrest->l > 0){
      if(size == 0){
         in = *inrest;
         inrest->s = NIL;
         inrest->l = 0;
      }else{
         out.s = su_ALLOC(in.l + inrest->l + 1);
         su_mem_copy(out.s, inrest->s, inrest->l);
         if(in.l > 0)
            su_mem_copy(&out.s[inrest->l], in.s, in.l);
         if(in.s != ptr)
            su_FREE(in.s);
         (in.s = out.s)[in.l += inrest->l] = '\0';
         inrest->l = 0;
         out.s = NIL;
      }
      dflags &= ~mx__MIME_DISPLAY_BUF_CONST;
   }

jconvert:
   __mimemw_sig = 0;
   __mimemw_opipe = safe_signal(SIGPIPE, &__mimemw_onsig);
   if(sigsetjmp(__mimemw_actjmp, 1))
      goto jleave;

   switch(convert){
   case CONV_FROMQP:
      if(!mx_qp_dec_part(&out, &in, outrest, inrest)){
         n_err(_("Invalid Quoted-Printable encoding ignored\n"));
         xsize = 0; /* TODO size = -1 stops outer levels! */
         break;
      }
      goto jqpb64_dec;
   case CONV_TOQP:
      if(mx_qp_enc(&out, &in, mx_QP_NONE) == NIL){
         xsize = 0; /* TODO size = -1 stops outer levels! */
         break;
      }
      goto jqpb64_enc;
   case CONV_8BIT:
      xsize = quoteflt_push(qf, in.s, in.l);
      break;
   case CONV_FROMB64:
      if(!mx_b64_dec_part(&out, &in, outrest, inrest))
         goto jeb64;
      outrest = NIL;
      if(0){
      /* FALLTHRU */
   case CONV_FROMB64_T:
         if(!mx_b64_dec_part(&out, &in, outrest, inrest)){
jeb64:
            n_err(_("Invalid Base64 encoding ignored\n"));
            xsize = 0; /* TODO size = -1 stops outer levels! */
            break;
         }
      }
jqpb64_dec:
      if((xsize = out.l) != 0)
         xsize = a_mime_fwrite_display(&out,
               (dflags & ~mx__MIME_DISPLAY_BUF_CONST), outrest, qf);
      break;
   case CONV_TOB64:
      /* TODO hack which is necessary unless this is a filter based approach
       * TODO and each filter has its own buffer (as necessary): we must not
       * TODO pass through a number of bytes which causes padding, otherwise we
       * TODO produce multiple adjacent base64 streams, and that is not treated
       * TODO in the same relaxed fashion like completely bogus bytes by at
       * TODO least mutt and OpenSSL.  So we need an expensive workaround
       * TODO unless we have input->iconv->base64 filter chain as such!! :( */
      if(size != 0 && /* for Coverity, else ASSERT() */ inrest != NIL){
         if(in.l > mx_B64_ENC_INPUT_PER_LINE){
            uz i;

            i = in.l % mx_B64_ENC_INPUT_PER_LINE;
            in.l -= i;

            if(i != 0){
               ASSERT(inrest->l == 0);
               inrest->s = su_REALLOC(inrest->s, i +1);
               su_mem_copy(inrest->s, &in.s[in.l], i);
               inrest->s[inrest->l = i] = '\0';
            }
         }else if(in.l < mx_B64_ENC_INPUT_PER_LINE){
            inrest->s = su_REALLOC(inrest->s, in.l +1);
            su_mem_copy(inrest->s, in.s, in.l);
            inrest->s[inrest->l = in.l] = '\0';
            in.l = 0;
            xsize = 0;
            break;
         }
      }
      if(mx_b64_enc(&out, &in, mx_B64_LF | mx_B64_MULTILINE) == NIL){
         xsize = -1;
         break;
      }
jqpb64_enc:
      xsize = fwrite(out.s, sizeof *out.s, out.l, f);
      if(xsize != S(sz,out.l))
         xsize = -1;
      break;
   case CONV_FROMHDR:
      if(mx_mime_display_from_header(&in, &out,
            (mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT |
             (dflags & mx_MIME_DISPLAY_DEL_CNTRL))))
         xsize = quoteflt_push(qf, out.s, out.l);
      break;
   case CONV_TOHDR:
      xsize = a_mime_write_tohdr(&in, f, NIL, a_MIME_SH_NONE);
      break;
   case CONV_TOHDR_A:{
      uz col;

      if(dflags & mx__MIME_DISPLAY_BUF_CONST){
         n_str_dup(&out, &in);
         in = out;
         out.s = NIL;
         dflags &= ~mx__MIME_DISPLAY_BUF_CONST;
      }
      col = 0;
      xsize = a_mime_write_tohdr_a(&in, f, &col, a_MIME_SH_NONE);
      }break;
   default:
      xsize = a_mime_fwrite_display(&in, dflags, NIL, qf);
      break;
   }

jleave:
   if(out.s != NIL)
      su_FREE(out.s);
   if(in.s != ptr)
      su_FREE(in.s);

   safe_signal(SIGPIPE, __mimemw_opipe);
   if(__mimemw_sig != 0)
      n_raise(__mimemw_sig);

   NYD_OU;
   return xsize;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MIME
/* s-it-mode */
