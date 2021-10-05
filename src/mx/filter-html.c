/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of filter-html.h.
 *@ TODO Rewrite wchar_t based (requires mx_HAVE_C90AMEND1)
 *@ TODO . Change add_data() to enqueue more data before deciding to go on.
 *@ TODO   Should improve soup detection.  I.e., we could keep the dumb
 *@ TODO   approach all around if just add_data() would encapsulate tags and
 *@ TODO   detect soup better, like quoted strings going on etc.
 *@ TODO . Numeric &#NO; entities should be treated by struct a_flthtml_ent
 *@ TODO . Binary sort/search ENTITY table
 *@ TODO . Yes, we COULD support CSS based quoting when we'd check type="quote"
 *@ TODO   (nonstandard) and watch out for style="gmail_quote" (or so, VERY
 *@ TODO   nonstandard) and tracking a stack of such elements (to be popped
 *@ TODO   once the closing element is seen).  Then, after writing a newline,
 *@ TODO   place sizeof(stack) ">"s first.  But aren't these HTML mails rude?
 *@ TODO Interlocking and non-well-formed data will break us down
 *
 * Copyright (c) 2015 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE filter_html
#define mx_SOURCE
#define mx_SOURCE_FILTER_HTML

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
#ifdef mx_HAVE_C90AMEND1
# include <wctype.h>
# include <wchar.h>
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/utf.h>

#include "mx/sigs.h"
#include "mx/termios.h"

#include "mx/filter-html.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_flthtml_limits{
   a_FLTHTML_MINLEN = 10, /* Minimum line length (cannot really be smaller) */
   a_FLTHTML_BRKSUB = 8 /* Start considering line break MAX - BRKSUB */
};

enum a_flthtml_flags{
   a_FLTHTML_BQUOTE_MASK = 0xFFFFu,
   a_FLTHTML_UTF8 = 1u<<16, /* Data is in UTF-8 */
   a_FLTHTML_ERROR = 1u<<17, /* Hard error, bail as soon as possible */
   a_FLTHTML_NOPUT = 1u<<18, /* (In a tag,) Don't generate output */
   a_FLTHTML_IGN = 1u<<19, /* Ignore mode on */
   a_FLTHTML_ANY = 1u<<20, /* Yet seen just any output */
   a_FLTHTML_PRE = 1u<<21, /* In <pre>formatted mode */
   a_FLTHTML_ENT = 1u<<22, /* Currently parsing an entity */
   a_FLTHTML_BLANK = 1u<<23, /* Whitespace last */
   a_FLTHTML_HREF = 1u<<24, /* External <a href=> was the last href seen */

   a_FLTHTML_NL_1 = 1u<<25, /* One \n seen */
   a_FLTHTML_NL_2 = 2u<<25, /* We have produced an all empty line */
   a_FLTHTML_NL_MASK = a_FLTHTML_NL_1 | a_FLTHTML_NL_2
};

enum a_flthtml_special_actions{
   a_FLTHTML_SA_NEEDSEP = -1, /* Need an empty line (paragraph separator) */
   a_FLTHTML_SA_NEEDNL = -2, /* Need a new line start (table row) */
   a_FLTHTML_SA_IGN = -3, /* Things like <style>..</style>, <script>.. */
   a_FLTHTML_SA_PRE = -4, /* <pre>.. */
   a_FLTHTML_SA_PRE_END = -5,
   a_FLTHTML_SA_IMG = -6, /* <img> */
   a_FLTHTML_SA_HREF = -7, /* <a>.. */
   a_FLTHTML_SA_HREF_END = -8,
   a_FLTHTML_SA_BQUOTE = -9, /* <blockquote>, interpreted as citation! */
   a_FLTHTML_SA_BQUOTE_END = -10
};

enum a_flthtml_entity_flags{
   a_FLTHTML_EF_HAVE_UNI = 1u<<6, /* Have a Unicode replacement character */
   a_FLTHTML_EF_HAVE_CSTR = 1u<<7, /* Have a string replacement */
   /* We store the length of the entity name in the flags, too */
   a_FLTHTML_EF_LENGTH_MASK = (1u<<6) - 1
};

struct mx_flthtml_href{
   struct mx_flthtml_href *fhh_next;
   u32 fhh_no; /* Running sequence */
   u32 fhh_len; /* of .fhh_dat */
   char fhh_dat[VFIELD_SIZE(0)];
};

struct mx_flthtml_tag{
   s32 fht_act; /* char or a_flthtml_special_actions */
   /* Not NUL: character to inject, with high bit set: suffix a space.
    * Note: only recognized with _SA_NEEDSEP or _SA_NEEDNL */
   char fht_injc;
   u8 fht_len; /* Useful bytes in (NUL terminated) .fht_tag */
   char const fht_tag[10]; /* Tag less < and > surroundings (TR, /TR, ..) */
};
CTA(su_FIELD_SIZEOF(struct mx_flthtml_tag,fht_tag) < LINESIZE,
   "Structure field too large a size"); /* .fh_ign_tag */

struct a_flthtml_ent{
   u8 fhe_flags; /* enum a_flthtml_entity_flags plus length of .fhe_ent */
   char fhe_c; /* Plain replacement character */
   u16 fhe_uni; /* Unicode codepoint if a_FLTHTML_EF_HAVE_UNI */
   char fhe_cstr[5]; /* _EF_HAVE_CSTR (e.g., &hellip; -> ...) */
   char const fhe_ent[7]; /* Entity less & and ; surroundings */
};

/* Tag list; todo not binary searched */
static struct mx_flthtml_tag const a_flthtml_tags[] = {
# undef a_X
# undef a_XC
# define a_X(S,A) {A, '\0', sizeof(S) -1, S "\0"}
# define a_XC(S,C,A) {A, C, sizeof(S) -1, S "\0"}

# if 0 /* This is treated very special (to avoid wasting space in .fht_tag) */
   a_X("BLOCKQUOTE", a_FLTHTML_SA_BQUOTE),
         a_X("/BLOCKQUOTE", a_FLTHTML_SA_BQUOTE_END),
# endif

   a_X("P", a_FLTHTML_SA_NEEDSEP), a_X("/P", a_FLTHTML_SA_NEEDNL),
   a_X("DIV", a_FLTHTML_SA_NEEDSEP), a_X("/DIV", a_FLTHTML_SA_NEEDNL),
   a_X("TR", a_FLTHTML_SA_NEEDNL),
      a_X("/TH", '\t'),
      a_X("/TD", '\t'),
   /* Let it stand out; also since we do not support implicit paragraphs after
    * block elements, plain running text after a list (seen in Unicode
    * announcement via Firefox) */
   a_X("UL", a_FLTHTML_SA_NEEDSEP), a_X("/UL", a_FLTHTML_SA_NEEDSEP),
   a_XC("LI", (char)0x80 | '*', a_FLTHTML_SA_NEEDSEP),
   a_X("DL", a_FLTHTML_SA_NEEDSEP),
   a_X("DT", a_FLTHTML_SA_NEEDNL),

   a_X("A", a_FLTHTML_SA_HREF), a_X("/A", a_FLTHTML_SA_HREF_END),
   a_X("IMG", a_FLTHTML_SA_IMG),
   a_X("BR", '\n'),
   a_X("PRE", a_FLTHTML_SA_PRE), a_X("/PRE", a_FLTHTML_SA_PRE_END),
   a_X("TITLE", a_FLTHTML_SA_NEEDSEP), /*a_X("/TITLE", '\n'),*/
   a_X("H1", a_FLTHTML_SA_NEEDSEP), /*a_X("/H1", '\n'),*/
   a_X("H2", a_FLTHTML_SA_NEEDSEP), /*a_X("/H2", '\n'),*/
   a_X("H3", a_FLTHTML_SA_NEEDSEP), /*a_X("/H3", '\n'),*/
   a_X("H4", a_FLTHTML_SA_NEEDSEP), /*a_X("/H4", '\n'),*/
   a_X("H5", a_FLTHTML_SA_NEEDSEP), /*a_X("/H5", '\n'),*/
   a_X("H6", a_FLTHTML_SA_NEEDSEP), /*a_X("/H6", '\n'),*/

   a_X("SCRIPT", a_FLTHTML_SA_IGN),
   a_X("STYLE", a_FLTHTML_SA_IGN),
   a_X("XML", a_FLTHTML_SA_IGN)

# undef a_X
# undef a_XC
};

/* Entity list, more or less HTML 4.0; TODO not binary searched */
static struct a_flthtml_ent const a_flthtml_ents[] = {
# undef a_X
# undef a_XU
# undef a_XS
# undef a_XSU
# define a_X(E,C) {(sizeof(E) -1), C, 0x0u, "", E "\0"}
# define a_XU(E,C,U) {(sizeof(E) -1) | a_FLTHTML_EF_HAVE_UNI, C, U, "", E "\0"}
# define a_XS(E,S) \
   {(sizeof(E) -1) | a_FLTHTML_EF_HAVE_CSTR, '\0', 0x0u,S "\0",E "\0"}
# define a_XSU(E,S,U) \
   {(sizeof(E) -1) | a_FLTHTML_EF_HAVE_UNI | a_FLTHTML_EF_HAVE_CSTR,\
    '\0', U, S "\0", E "\0"}

   a_X("quot", '"'),
   a_X("amp", '&'),
   a_X("lt", '<'), a_X("gt", '>'),

   a_XU("nbsp", ' ', 0x0020 /* Note: not 0x00A0 seems to be better for us */),
   a_XSU("hellip", "...", 0x2026),
   a_XSU("mdash", "---", 0x2014), a_XSU("ndash", "--", 0x2013),
   a_XSU("lsaquo", "<", 0x2039), a_XSU("rsaquo", ">", 0x203A),
   a_XSU("lsquo", "'", 0x2018), a_XSU("rsquo", "'", 0x2019),
   a_XSU("ldquo", "\"", 0x201C), a_XSU("rdquo", "\"", 0x201D),
   a_XSU("uarr", "^|", 0x2191), a_XSU("darr", "|v", 0x2193),
   a_XU("bull", '.', 0x2022),

   a_XSU("euro", "EUR", 0x20AC),
   a_XSU("infin", "INFY", 0x221E),

   /* Latin1 entities */
/*nbsp   "&#160;" no-break space = non-breaking space, U+00A0 ISOnum*/
   a_XSU("iexcl", "!", 0x00A1),
   a_XSU("cent", "CENT", 0x00A2),
   a_XSU("pound", "GBP", 0x00A3),
/*curren "&#164;" currency sign, U+00A4 ISOnum*/
   a_XSU("yen", "JPY", 0x00A5),
/*brvbar "&#166;" broken bar = broken vertical bar, U+00A6 ISOnum*/
   a_XSU("sect", "S:", 0x00A7),
/*uml    "&#168;" diaeresis = spacing diaeresis, U+00A8 ISOdia*/
   a_XSU("copy", "(C)", 0x00A9),
/*ordf   "&#170;" feminine ordinal indicator, U+00AA ISOnum*/
   a_XSU("laquo", "<<", 0x00AB),
/*not    "&#172;" not sign, U+00AC ISOnum*/
   a_XSU("shy", "-", 0x00AD),
   a_XSU("reg", "(R)", 0x00AE),
/*macr   "&#175;" macron = spacing macron, U+00AF ISOdia */
/*deg    "&#176;" degree sign, U+00B0 ISOnum*/
/*plusmn "&#177;" plus-minus sign = plus-or-minus sign, U+00B1 ISOnum*/
/*sup2   "&#178;" superscript two = superscript digit two, U+00B2 ISOnum*/
/*sup3   "&#179;" superscript three = superscript digit three, U+00B3 ISOnum*/
/*acute  "&#180;" acute accent = spacing acute, U+00B4 ISOdia*/
/*micro  "&#181;" micro sign, U+00B5 ISOnum*/
/*para   "&#182;" pilcrow sign = paragraph sign, U+00B6 ISOnum*/
   a_XU("middot", '.', 0x00B7),
/*cedil  "&#184;" cedilla = spacing cedilla, U+00B8 ISOdia*/
/*sup1   "&#185;" superscript one = superscript digit one, U+00B9 ISOnum*/
/*ordm   "&#186;" masculine ordinal indicator, U+00BA ISOnum*/
   a_XSU("raquo", ">>", 0x00BB),
/*frac14 "&#188;" vulgar fraction one quarter, U+00BC ISOnum*/
/*frac12 "&#189;" vulgar fraction one half, U+00BD ISOnum*/
/*frac34 "&#190;" vulgar fraction three quarters, U+00BE ISOnum*/
   a_XU("iquest", '?', 0x00BF),
   a_XU("Agrave", 'A', 0x00C0),
   a_XU("Aacute", 'A', 0x00C1),
   a_XU("Acirc", 'A', 0x00C2),
   a_XU("Atilde", 'A', 0x00C3),
   a_XSU("Auml", "Ae", 0x00C4),
   a_XU("Aring", 'A', 0x00C5),
   a_XSU("AElig", "AE", 0x00C6),
   a_XU("Ccedil", 'C', 0x00C7),
   a_XU("Egrave", 'E', 0x00C8),
   a_XU("Eacute", 'E', 0x00C9),
   a_XU("Ecirc", 'E', 0x00CA),
   a_XU("Euml", 'E', 0x00CB),
   a_XU("Igrave", 'I', 0x00CC),
   a_XU("Iacute", 'I', 0x00CD),
   a_XU("Icirc", 'I', 0x00CE),
   a_XU("Iuml", 'I', 0x00CF),
/*ETH    "&#208;" latin capital letter ETH, U+00D0 ISOlat1*/
   a_XU("Ntilde", 'N', 0x00D1),
   a_XU("Ograve", 'O', 0x00D2),
   a_XU("Oacute", 'O', 0x00D3),
   a_XU("Ocirc", 'O', 0x00D4),
   a_XU("Otilde", 'O', 0x00D5),
   a_XSU("Ouml", "OE", 0x00D6),
/*times  "&#215;" multiplication sign, U+00D7 ISOnum*/
   a_XU("Oslash", 'O', 0x00D8),
   a_XU("Ugrave", 'U', 0x00D9),
   a_XU("Uacute", 'U', 0x00DA),
   a_XU("Ucirc", 'U', 0x00DB),
   a_XSU("Uuml", "UE", 0x00DC),
   a_XU("Yacute", 'Y', 0x00DD),
/*THORN  "&#222;" latin capital letter THORN, U+00DE ISOlat1*/
   a_XSU("szlig", "ss", 0x00DF),
   a_XU("agrave", 'a', 0x00E0),
   a_XU("aacute", 'a', 0x00E1),
   a_XU("acirc", 'a', 0x00E2),
   a_XU("atilde", 'a', 0x00E3),
   a_XSU("auml", "ae", 0x00E4),
   a_XU("aring", 'a', 0x00E5),
   a_XSU("aelig", "ae", 0x00E6),
   a_XU("ccedil", 'c', 0x00E7),
   a_XU("egrave", 'e', 0x00E8),
   a_XU("eacute", 'e', 0x00E9),
   a_XU("ecirc", 'e', 0x00EA),
   a_XU("euml", 'e', 0x00EB),
   a_XU("igrave", 'i', 0x00EC),
   a_XU("iacute", 'i', 0x00ED),
   a_XU("icirc", 'i', 0x00EE),
   a_XU("iuml", 'i', 0x00EF),
/*eth    "&#240;" latin small letter eth, U+00F0 ISOlat1*/
   a_XU("ntilde", 'n', 0x00F1),
   a_XU("ograve", 'o', 0x00F2),
   a_XU("oacute", 'o', 0x00F3),
   a_XU("ocirc", 'o', 0x00F4),
   a_XU("otilde", 'o', 0x00F5),
   a_XSU("ouml", "oe", 0x00F6),
/*divide "&#247;" division sign, U+00F7 ISOnum*/
   a_XU("oslash", 'o', 0x00F8),
   a_XU("ugrave", 'u', 0x00F9),
   a_XU("uacute", 'u', 0x00FA),
   a_XU("ucirc", 'u', 0x00FB),
   a_XSU("uuml", "ue", 0x00FC),
   a_XU("yacute", 'y', 0x00FD),
/*thorn  "&#254;" latin small letter thorn, 0x00FE ISOlat1*/
   a_XU("yuml", 'y', 0x00FF),

   /* Latin Extended-A */
   a_XSU("OElig", "OE", 0x0152),
   a_XSU("oelig", "oe", 0x0153),
   a_XU("Scaron", 'S', 0x0160),
   a_XU("scaron", 's', 0x0161),
   a_XU("Yuml", 'Y', 0x0178),

   /* Spacing Modifier Letters */
/*circ "&#710;" modifier letter circumflex accent, u+02C6 ISOpub*/
   a_XU("tilde", '~', 0x02DC),

   /*- General Punctuation (many of them above) */
   a_XU("ensp", ' ', 0x2002),
   a_XU("emsp", ' ', 0x2003),
   a_XU("thinsp", ' ', 0x2009),
   a_XU("zwnj", '\0', 0x200C),
   a_XU("zwj", '\0', 0x200D),
/*lrm "&#8206;" left-to-right mark, u+200E NEW RFC 2070*/
/*rlm "&#8207;" right-to-left mark, u+200F NEW RFC 2070*/
   a_XU("sbquo", ',',  0x201A),
   a_XSU("bdquo", ",,", 0x201E)
/*dagger "&#8224;" dagger, u+2020 ISOpub*/
/*Dagger "&#8225;" double dagger, u+2021 ISOpub*/
/*permil "&#8240;" per mille sign, u+2030 ISOtech*/

# undef a_X
# undef a_XU
# undef a_XS
# undef a_XSU
};

/* Real output */
static struct mx_flthtml *a_flthtml_dump_hrefs(struct mx_flthtml *self);
static struct mx_flthtml *a_flthtml_dump(struct mx_flthtml *self);
static struct mx_flthtml *a_flthtml_store(struct mx_flthtml *self,
      char c);
# ifdef mx_HAVE_NATCH_CHAR
static struct mx_flthtml *a_flthtml__sync_mbstuff(struct mx_flthtml *self);
# endif

/* Virtual output */
static struct mx_flthtml *a_flthtml_nl(struct mx_flthtml *self);
static struct mx_flthtml *a_flthtml_nl_force(struct mx_flthtml *self);
static struct mx_flthtml *a_flthtml_putc(struct mx_flthtml *self, char c);
static struct mx_flthtml *a_flthtml_putc_premode(struct mx_flthtml *self,
      char c);
static struct mx_flthtml *a_flthtml_puts(struct mx_flthtml *self,
      char const *cp);
static struct mx_flthtml *a_flthtml_putbuf(struct mx_flthtml *self,
      char const *cp, uz len);

/* Try locate a param'eter in >fh_bdat, store it (non-terminated!) or NIL */
static struct mx_flthtml *a_flthtml_param(struct mx_flthtml *self,
      struct str *store, char const *param);

/* Expand all entities in the given parameter */
static struct mx_flthtml *a_flthtml_expand_all_ents(
      struct mx_flthtml *self, struct str const *param);

/* Completely parsed over a tag / an entity, interpret that */
static struct mx_flthtml *a_flthtml_check_tag(struct mx_flthtml *self,
      char const *s);
static struct mx_flthtml *a_flthtml_check_ent(struct mx_flthtml *self,
      char const *s, uz l);

/* Input handler */
static sz a_flthtml_add_data(struct mx_flthtml *self, char const *dat,
      uz len);

static struct mx_flthtml *
a_flthtml_dump_hrefs(struct mx_flthtml *self){
   struct mx_flthtml_href *hhp;
   NYD2_IN;

   if(!(self->fh_flags & a_FLTHTML_NL_2) && putc('\n', self->fh_os) == EOF){
      self->fh_flags |= a_FLTHTML_ERROR;
      goto jleave;
   }

   /* Reverse the list */
   for(hhp = self->fh_hrefs, self->fh_hrefs = NIL; hhp != NIL;){
      struct mx_flthtml_href *tmp;

      tmp = hhp->fhh_next;
      hhp->fhh_next = self->fh_hrefs;
      self->fh_hrefs = hhp;
      hhp = tmp;
   }

   /* Then dump it */
   while((hhp = self->fh_hrefs) != NIL){
      self->fh_hrefs = hhp->fhh_next;

      if(!(self->fh_flags & a_FLTHTML_ERROR)){
         int w;

         w = fprintf(self->fh_os, "  [%u] %.*s\n",
               hhp->fhh_no, (int)hhp->fhh_len, hhp->fhh_dat);
         if(w < 0)
            self->fh_flags |= a_FLTHTML_ERROR;
      }
      su_FREE(hhp);
   }

   self->fh_flags |= (putc('\n', self->fh_os) == EOF)
         ?  a_FLTHTML_ERROR : a_FLTHTML_NL_1 | a_FLTHTML_NL_2;
   self->fh_href_dist = mx_termios_dimen.tiosd_real_height >> 1;

jleave:
   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_dump(struct mx_flthtml *self){
   u32 f, l;
   char c, *cp;
   NYD2_IN;

   f = (self->fh_flags & ~a_FLTHTML_BLANK);
   l = self->fh_len;
   cp = self->fh_line;
   self->fh_mbwidth = self->fh_mboff = self->fh_last_ws = self->fh_len = 0;

   for(c = '\0'; l > 0; --l){
      c = *cp++;
jput:
      if(putc(c, self->fh_os) == EOF){
         self->fh_flags = (f |= a_FLTHTML_ERROR);
         goto jleave;
      }
   }

   if(c != '\n'){
      f |= (f & a_FLTHTML_NL_1) ? a_FLTHTML_NL_2 : a_FLTHTML_NL_1;
      l = 1;
      c = '\n';
      goto jput;
   }
   self->fh_flags = f;

   /* Check whether there are HREFs to dump; there is so much messy tagsoup out
    * there that it seems best not to simply dump HREFs in each _dump(), but
    * only with some gap, let's say half the real screen height */
   if(--self->fh_href_dist < 0 && (f & a_FLTHTML_NL_2) &&
         self->fh_hrefs != NIL)
      self = a_flthtml_dump_hrefs(self);

jleave:
   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_store(struct mx_flthtml *self, char c){
   u32 l, i;
   NYD2_IN;

   ASSERT(c != '\n');

   l = self->fh_len;
   if(UNLIKELY(l == 0) && (i = (self->fh_flags & a_FLTHTML_BQUOTE_MASK)
         ) != 0 && self->fh_lmax > a_FLTHTML_MINLEN){
      char xc;
      u32 len, j;
      char const *ip;

      ip = ok_vlook(indentprefix);
      len = su_cs_len(ip);
      if(len == 0 || len >= a_FLTHTML_MINLEN){
         ip = "   |"; /* XXX something from *quote-chars* */
         len = sizeof("   |") -1;
      }
      self->fh_len = len;

      xc = '\0';
      for(j = len; j-- != 0;){
         char x;

         if((x = ip[j]) == '\t')
            x = ' ';
         else if(xc == '\0' && su_cs_is_print(x) && !su_cs_is_space(x))
            xc = x;
         self->fh_line[j] = x;
      }
      if(xc == '\0')
         xc = '|';

      while(--i > 0 && self->fh_len < self->fh_lmax - a_FLTHTML_BRKSUB)
         self = a_flthtml_store(self, xc);

      l = self->fh_len;
   }

   self->fh_line[l] = (c == '\t' ? ' ' : c);
   self->fh_len = ++l;

   if(su_cs_is_space(c)){
      if(c == '\t'){
         i = 8 - ((l - 1) & 7); /* xxx magic tab width of 8 */
         if(i > 0){
            do
               self = a_flthtml_store(self, ' ');
            while(--i > 0);
            goto jleave;
         }
      }
      self->fh_last_ws = l;
   }else if(/*c == '.' ||*/ c == ',' || c == ';' || c == '-')
      self->fh_last_ws = l;

   i = l;
# ifdef mx_HAVE_NATCH_CHAR /* XXX This code is really ridiculous! */
   if(n_mb_cur_max > 1){ /* XXX should mbrtowc() and THEN store, at least */
      wchar_t wc;
      int w, x;

      if((x = mbtowc(&wc, self->fh_line + self->fh_mboff,
            l - self->fh_mboff)) > 0){
         if((w = wcwidth(wc)) == -1 ||
               /* Actively filter out L-TO-R and R-TO-R marks TODO ctext */
               (wc == 0x200E || wc == 0x200F ||
                  (wc >= 0x202A && wc <= 0x202E)) ||
               /* And some zero-width messes */
               wc == 0x00AD || (wc >= 0x200B && wc <= 0x200D) ||
               /* Oh about the ISO C wide character interfaces, baby! */
               (wc == 0xFEFF)){
            self->fh_len -= x;
            goto jleave;
         }else if(iswspace(wc))
            self->fh_last_ws = l;
         self->fh_mboff += x;
         i = (self->fh_mbwidth += w);
      }else{
         if(x < 0){
            (void)mbtowc(&wc, NIL, n_mb_cur_max);
            if(UCMP(32, l - self->fh_mboff, >=, n_mb_cur_max)){ /* XXX */
               ++self->fh_mboff;
               ++self->fh_mbwidth;
            }
         }
         i = self->fh_mbwidth;
      }
   }
# endif

   /* Do we need to break the line? */
   if(i >= self->fh_lmax - a_FLTHTML_BRKSUB){
      u32 f, lim;

      /* Let's hope we saw a sane place to break this line! */
      if(self->fh_last_ws >= (lim = self->fh_lmax >> 1)){
jput:
         i = self->fh_len = self->fh_last_ws;
         self = a_flthtml_dump(self);
         if((self->fh_len = (l -= i)) > 0){
            self->fh_flags &= ~a_FLTHTML_NL_MASK;
            su_mem_move(self->fh_line, self->fh_line + i, l);
# ifdef mx_HAVE_NATCH_CHAR
            a_flthtml__sync_mbstuff(self);
# endif
         }
         goto jleave;
      }

      /* Any 7-bit characters? */
      f = self->fh_flags;
      for(i = l; i-- >= lim;)
         if(su_cs_is_ascii((c = self->fh_line[i]))){
            self->fh_last_ws = ++i;
            goto jput;
         }else if((f & a_FLTHTML_UTF8) && (S(u8,c) & 0xC0) != 0x80){
            self->fh_last_ws = i;
            goto jput;
         }

      /* Hard break necessary!  xxx really badly done */
      if(l >= self->fh_lmax - 1)
         self = a_flthtml_dump(self);
   }

jleave:
   NYD2_OU;
   return self;
}

# ifdef mx_HAVE_NATCH_CHAR
static struct mx_flthtml *
a_flthtml__sync_mbstuff(struct mx_flthtml *self){
   wchar_t wc;
   int x;
   char const *b;
   u32 o, w, l;
   NYD2_IN;

   b = self->fh_line;
   o = w = 0;
   l = self->fh_len;

   goto jumpin;
   while(l > 0){
      if((x = mbtowc(&wc, b, l)) == 0)
         break;

      if(x > 0){
         b += x;
         l -= x;
         o += x;
         if((x = wcwidth(wc)) == -1)
            x = 1;
         w += x;
         continue;
      }

      /* Bad, skip over a single character.. XXX very bad indeed */
      ++b;
      ++o;
      ++w;
      --l;
jumpin:
      (void)mbtowc(&wc, NIL, n_mb_cur_max);
   }

   self->fh_mboff = o;
   self->fh_mbwidth = w;

   NYD2_OU;
   return self;
}
# endif /* mx_HAVE_NATCH_CHAR */

static struct mx_flthtml *
a_flthtml_nl(struct mx_flthtml *self){
   u32 f;
   NYD2_IN;

   if(!((f = self->fh_flags) & a_FLTHTML_ERROR)){
      if(f & a_FLTHTML_ANY){
         if((f & a_FLTHTML_NL_MASK) != a_FLTHTML_NL_MASK)
            self = a_flthtml_dump(self);
      }else
         self->fh_flags = (f |= a_FLTHTML_NL_MASK);
   }

   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_nl_force(struct mx_flthtml *self){
   NYD2_IN;

   if(!(self->fh_flags & a_FLTHTML_ERROR))
      self = a_flthtml_dump(self);

   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_putc(struct mx_flthtml *self, char c){
   u32 f;
   NYD2_IN;

   if((f = self->fh_flags) & a_FLTHTML_ERROR)
      goto jleave;

   if(c == '\n'){
      self = a_flthtml_nl(self);
      goto jleave;
   }

   if(c == ' ' || c == '\t'){
      if((f & a_FLTHTML_BLANK) || self->fh_len == 0)
         goto jleave;
      f |= a_FLTHTML_BLANK;
   }else
      f &= ~a_FLTHTML_BLANK;
   f &= ~a_FLTHTML_NL_MASK;
   self->fh_flags = (f |= a_FLTHTML_ANY);
   self = a_flthtml_store(self, c);

jleave:
   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_putc_premode(struct mx_flthtml *self, char c){
   u32 f;
   NYD2_IN;

   if((f = self->fh_flags) & a_FLTHTML_ERROR){
      ;
   }else if(c == '\n')
      self = a_flthtml_nl_force(self);
   else{
      f &= ~a_FLTHTML_NL_MASK;
      self->fh_flags = (f |= a_FLTHTML_ANY);
      self = a_flthtml_store(self, c);
   }

   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_puts(struct mx_flthtml *self, char const *cp){
   char c;
   NYD2_IN;

   while((c = *cp++) != '\0')
      self = a_flthtml_putc(self, c);

   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_putbuf(struct mx_flthtml *self, char const *cp, uz len){
   NYD2_IN;

   while(len-- > 0)
      self = a_flthtml_putc(self, *cp++);

   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_param(struct mx_flthtml *self, struct str *store, char const *param){
   char const *cp;
   char c, x, quote;
   uz i;
   boole hot;
   NYD2_IN;

   store->s = NIL;
   store->l = 0;
   cp = self->fh_bdat;

   /* Skip over any non-WS first; be aware of soup, if it slipped through */
   for(;;){
      if((c = *cp++) == '\0' || c == '>')
         goto jleave;
      if(su_cs_is_blank(c) || c == '\n')
         break;
   }

   /* Search for the parameter, take care of other quoting along the way */
   x = *param++;
   x = su_cs_to_upper(x);
   i = su_cs_len(param);

   for(hot = TRU1;;){
      if((c = *cp++) == '\0' || c == '>')
         goto jleave;
      if(su_cs_is_white(c)){
         hot = TRU1;
         continue;
      }

      /* Could it be a parameter? */
      if(hot){
         hot = FAL0;

         /* Is it the desired one? */
         if((c = su_cs_to_upper(c)) == x && !su_cs_cmp_case_n(param, cp, i)){
            char const *cp2;

            cp2 = &cp[i];

            if((quote = *cp2++) != '='){
               if(quote == '\0' || quote == '>')
                  goto jleave;
               while(su_cs_is_white(quote))
                  quote = *cp2++;
            }
            if(quote == '='){
               cp = cp2;
               break;
            }
            continue; /* XXX Optimize: i bytes or even cp2 can't be it! */
         }
      }

      /* Not the desired one; but a parameter? */
      if(c != '=')
         continue;
      /* If so, properly skip over the value */
      if((c = *cp++) == '"' || c == '\''){
         /* TODO i have forgotten whether reverse solidus quoting is allowed
          * TODO quoted HTML param values?  not supporting that for now.. */
         for(quote = c; (c = *cp++) != '\0' && c != quote;)
            ;
      }else
         while(c != '\0' && !su_cs_is_white(c) && c != '>')
            c = *++cp;
      if(c == '\0')
         goto jleave;
   }

   /* Skip further whitespace */
   for(;;){
      if((c = *cp++) == '\0' || c == '>')
         goto jleave;
      if(!su_cs_is_white(c))
         break;
   }

   if(c == '"' || c == '\''){
      store->s = UNCONST(char*,cp);
      for(quote = c; (c = *cp) != '\0' && c != quote; ++cp)
         ;
      /* XXX ... and we simply ignore a missing trailing " :> */
   }else{
      store->s = UNCONST(char*,&cp[-1]);
      if(!su_cs_is_white(c))
         while((c = *cp) != '\0' && !su_cs_is_white(c) && c != '>')
            ++cp;
   }
   i = P2UZ(cp - store->s);

   /* Terrible tagsoup out there, e.g., groups.google.com produces href=""
    * parameter values prefixed and suffixed by newlines!  Therefore trim the
    * value content TODO join into the parse step above! */
   for(cp = store->s; i > 0 && su_cs_is_space(*cp); ++cp, --i)
      ;
   store->s = UNCONST(char*,cp);
   for(cp += i - 1; i > 0 && su_cs_is_space(*cp); --cp, --i)
      ;
   if((store->l = i) == 0)
      store->s = NIL;

jleave:
   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_expand_all_ents(struct mx_flthtml *self, struct str const *param){
   char const *cp, *maxcp, *ep;
   char c;
   uz i;
   NYD2_IN;

   for(cp = param->s, maxcp = cp + param->l; cp < maxcp;){
      if((c = *cp++) != '&')
jputc:
         self = a_flthtml_putc(self, c);
      else{
         for(ep = cp--;;){
            if(ep == maxcp || (c = *ep++) == '\0'){
               for(; cp < ep; ++cp)
                  self = a_flthtml_putc(self, *cp);
               goto jleave;
            }else if (c == ';'){
               if((i = P2UZ(ep - cp)) > 1){
                  self = a_flthtml_check_ent(self, cp, i);
                  break;
               }else{
                  c = *cp++;
                  goto jputc;
               }
            }
         }
         cp = ep;
      }
   }

jleave:
   NYD2_OU;
   return self;
}

static struct mx_flthtml *
a_flthtml_check_tag(struct mx_flthtml *self, char const *s){
   char nobuf[32], c;
   struct str param;
   uz i;
   struct mx_flthtml_tag const *hftp;
   u32 f;
   NYD2_IN;

   /* Extra check only */
   ASSERT(s != NIL);
   if(*s != '<'){
      DBG( n_alert("HTML tagsoup filter: check_tag() called on soup!"); )
jput_as_is:
      self = a_flthtml_puts(self, self->fh_bdat);
      goto jleave;
   }

   for(++s, i = 0; (c = s[i]) != '\0' && c != '>' && !su_cs_is_white(c); ++i){
      /* Special massage for things like <br/>: after the slash only whitespace
       * may separate us from the closing right angle! */
      if(c == '/'){
         uz j;

         for(j = i + 1; (c = s[j]) != '\0' && c != '>' && su_cs_is_white(c);)
            ++j;
         if(c == '>')
            break;
      }
   }

   for(hftp = a_flthtml_tags;;){
      if(i == hftp->fht_len && !su_cs_cmp_case_n(s, hftp->fht_tag, i)){
         c = s[hftp->fht_len];
         if(c == '>' || c == '/' || su_cs_is_white(c))
            break;
      }

      if(UNLIKELY(PCMP(++hftp, >=, &a_flthtml_tags[NELEM(a_flthtml_tags)]))){
         /* xxx A <blockquote> is very special as it is used for quoting */
         boole isct;

         if((isct = (i > 1 && *s == '/'))){
            ++s;
            --i;
         }

         if(i != sizeof("blockquote") -1 ||
               su_cs_cmp_case_n(s, "blockquote", i) ||
               ((c = s[sizeof("blockquote") -1]) != '>' &&
               !su_cs_is_white(c))){
            s -= isct;
            i += isct;
            goto junknown;
         }

         if(!isct && !(self->fh_flags & a_FLTHTML_NL_2))
            self = a_flthtml_nl(self);
         if(!(self->fh_flags & a_FLTHTML_NL_1))
            self = a_flthtml_nl(self);
         f = self->fh_flags;
         f &= a_FLTHTML_BQUOTE_MASK;
         if(!isct){
            if(f != a_FLTHTML_BQUOTE_MASK)
               ++f;
         }else if(f > 0)
            --f;
         f |= (self->fh_flags & ~a_FLTHTML_BQUOTE_MASK);
         self->fh_flags = f;
         goto jleave;
      }
   }

   f = self->fh_flags;
   switch(hftp->fht_act){
   case a_FLTHTML_SA_PRE_END:
      f &= ~a_FLTHTML_PRE;
      if(0){
         /* FALLTHRU */
   case a_FLTHTML_SA_PRE:
         f |= a_FLTHTML_PRE;
      }
      self->fh_flags = f;
      /* FALLTHRU */

   case a_FLTHTML_SA_NEEDSEP:
      if(!(self->fh_flags & a_FLTHTML_NL_2))
         self = a_flthtml_nl(self);
      /* FALLTHRU */
   case a_FLTHTML_SA_NEEDNL:
      if(!(f & a_FLTHTML_NL_1))
         self = a_flthtml_nl(self);
      if(hftp->fht_injc != '\0') {
         self = a_flthtml_putc(self, hftp->fht_injc & 0x7F);
         if(S(uc,hftp->fht_injc) & 0x80)
            self = a_flthtml_putc(self, ' ');
      }
      break;

   case a_FLTHTML_SA_IGN:
      self->fh_ign_tag = hftp;
      self->fh_flags = (f |= a_FLTHTML_IGN | a_FLTHTML_NOPUT);
      break;

   case a_FLTHTML_SA_IMG:
      self = a_flthtml_param(self, &param, "alt");
      self = a_flthtml_putc(self, '[');
      if(param.s == NIL){
         param.s = UNCONST(char*,"IMG");
         param.l = 3;
         goto jimg_put;
      }/* else */ if(su_mem_find(param.s, '&', param.l) != NIL)
         self = a_flthtml_expand_all_ents(self, &param);
      else
jimg_put:
         self = a_flthtml_putbuf(self, param.s, param.l);
      self = a_flthtml_putc(self, ']');
      break;

   case a_FLTHTML_SA_HREF:
      self = a_flthtml_param(self, &param, "href");
      /* Ignore non-external links */
      if(param.s != NIL && *param.s != '#'){
         struct mx_flthtml_href *hhp;

         hhp = su_ALLOC(VSTRUCT_SIZEOF(struct mx_flthtml_href,fhh_dat) +
               param.l +1);
         hhp->fhh_next = self->fh_hrefs;
         hhp->fhh_no = ++self->fh_href_no;
         hhp->fhh_len = S(u32,param.l);
         su_mem_copy(hhp->fhh_dat, param.s, param.l);

         snprintf(nobuf, sizeof nobuf, "[%u]", hhp->fhh_no);
         self->fh_flags = (f |= a_FLTHTML_HREF);
         self->fh_hrefs = hhp;
         self = a_flthtml_puts(self, nobuf);
      }else
         self->fh_flags = (f &= ~a_FLTHTML_HREF);
      break;
   case a_FLTHTML_SA_HREF_END:
      if(f & a_FLTHTML_HREF){
         snprintf(nobuf, sizeof nobuf, "[/%u]", self->fh_href_no);
         self = a_flthtml_puts(self, nobuf);
      }
      break;

   default:
      c = S(char,hftp->fht_act & 0xFF);
      self = a_flthtml_putc(self, c);
      break;
   case '\0':
      break;
   }

jleave:
   NYD2_OU;
   return self;

   /* The problem is that even invalid tagsoup is widely used, without real
    * searching i have seen e-mail address in <N@H.D> notation, and more.
    * Protect us a bit: look around and possibly write the content as such */
junknown:
   switch(*s){
   case '!':
   case '?':
      /* Ignore <!DOCTYPE, <!-- comments, <? PIs.. */
      goto jleave;
   case '>':
      /* Print out an empty tag as such */
      if(s[1] == '\0'){
         --s;
         goto jput_as_is;
      }
      break;
   case '/':
      ++s;
      break;
   default:
      break;
   }

   /* Also skip over : in order to suppress v:roundrect, w:anchorlock.. */
   while((c = *s++) != '\0' && c != '>' && !su_cs_is_white(c) && c != ':')
      if(!su_cs_is_ascii(c) || su_cs_is_punct(c)){
         self = a_flthtml_puts(self, self->fh_bdat);
         break;
      }
   goto jleave;
}

static struct mx_flthtml *
a_flthtml_check_ent(struct mx_flthtml *self, char const *s, uz l){
   char nobuf[32];
   char const *s_save;
   uz l_save;
   struct a_flthtml_ent const *hfep;
   uz i;
   NYD2_IN;

   s_save = s;
   l_save = l;
   ASSERT(*s == '&');
   ASSERT(l > 0);
   /* False entities seen in the wild ASSERT(s[l - 1] == ';'); */
   ++s;
   l -= 2;

   /* Numeric entity, or try named search */
   if(*s == '#'){
      i = (*++s == 'x') ? 16 : 10;

      if((i != 16 || (++s, --l) > 0) && l < sizeof(nobuf)){
         su_mem_copy(nobuf, s, l);
         nobuf[l] = '\0';
         su_idec_uz_cp(&i, nobuf, i, NIL);
         if(i <= 0x7F)
            self = a_flthtml_putc(self, (char)i);
         else if(self->fh_flags & a_FLTHTML_UTF8){
jputuni:
            l = su_utf32_to_8(S(u32,i), nobuf);
            self = a_flthtml_putbuf(self, nobuf, l);
         }else
            goto jeent;
      }else
         goto jeent;
   }else{
      u32 f, hf;

      f = self->fh_flags;

      for(hfep = a_flthtml_ents;
            PCMP(hfep, <, a_flthtml_ents + NELEM(a_flthtml_ents)); ++hfep)
         if(l == ((hf = hfep->fhe_flags) & a_FLTHTML_EF_LENGTH_MASK) &&
               !strncmp(s, hfep->fhe_ent, l)) {
            if((hf & a_FLTHTML_EF_HAVE_UNI) && (f & a_FLTHTML_UTF8)){
               i = hfep->fhe_uni;
               goto jputuni;
            }else if(hf & a_FLTHTML_EF_HAVE_CSTR)
               self = a_flthtml_puts(self, hfep->fhe_cstr);
            else if(hfep->fhe_c != '\0')
               self = a_flthtml_putc(self, hfep->fhe_c);
            goto jleave;
         }
jeent:
      self = a_flthtml_putbuf(self, s_save, l_save);
   }

jleave:
   NYD2_OU;
   return self;
}

static sz
a_flthtml_add_data(struct mx_flthtml *self, char const *dat, uz len){
   char c, *cp, *cp_max;
   boole hot;
   sz rv = 0;
   NYD_IN;

   /* Final put request? */
   if(dat == NIL){
      if(self->fh_len > 0 || self->fh_hrefs != NIL){
         self = a_flthtml_dump(self);
         if(self->fh_hrefs != NIL)
            self = a_flthtml_dump_hrefs(self);
         rv = 1;
      }
      goto jleave;
   }

   /* Always ensure some initial buffer */
   if((cp = self->fh_curr) != NIL)
      cp_max = self->fh_bmax;
   else{
      cp = self->fh_curr = self->fh_bdat = su_ALLOC(LINESIZE);
      cp_max = self->fh_bmax = cp + LINESIZE -1; /* (Always room for NUL!) */
   }
   hot = (cp != self->fh_bdat);

   for(rv = S(sz,len); len > 0; --len){
      u32 f;

      f = self->fh_flags;
      if(f & a_FLTHTML_ERROR)
         break;
      c = *dat++;

      /* Soup is really weird, and scripts may contain almost anything (and
       * newer CSS standards also cryptic): therefore prefix the a_FLTHTML_IGN
       * test and walk until we see the required end tag */
      /* TODO For real safety FLTHTML_IGN soup condome would also need to know
       * TODO about quoted strings so that 'var i = "</script>";' couldn't
       * TODO fool it!   We really want this mode also for FLTHTML_NOPUT to be
       * TODO able to *gracefully* detect the tag-closing '>', but then if
       * TODO that is a single mechanism we should have made it! */
      if(f & a_FLTHTML_IGN){
         uz i;
         struct mx_flthtml_tag const *hftp;

         hftp = self->fh_ign_tag;

         if(c == '<'){
            hot = TRU1;
jcp_reset:
            cp = self->fh_bdat;
         }else if(c == '>'){
            if(hot){
               if((i = P2UZ(cp - self->fh_bdat)) > 1 &&
                     --i == hftp->fht_len &&
                     !su_cs_cmp_case_n(self->fh_bdat + 1, hftp->fht_tag, i))
                  self->fh_flags = (f &= ~(a_FLTHTML_IGN | a_FLTHTML_NOPUT));
               hot = FAL0;
               goto jcp_reset;
            }
         }else if(hot){
            *cp++ = c;
            i = P2UZ(cp - self->fh_bdat);
            if((i == 1 && c != '/') || --i > hftp->fht_len){
               hot = FAL0;
               goto jcp_reset;
            }
         }
      }else switch(c){
      case '<':
         /* People are using & without &amp;ing it, ditto <; be aware */
         if(f & (a_FLTHTML_NOPUT | a_FLTHTML_ENT)){
            f &= ~a_FLTHTML_ENT;
            /* Special case "<!--" buffer content to deal with really weird
             * things that can be done with "<!--[if gte mso 9]>" syntax */
            if(P2UZ(cp - self->fh_bdat) != 4 ||
                  su_mem_cmp(self->fh_bdat, "<!--", 4)){
               self->fh_flags = f;
               *cp = '\0';
               self = a_flthtml_puts(self, self->fh_bdat);
               f = self->fh_flags;
            }
            break;
         }
         cp = self->fh_bdat;
         *cp++ = c;
         self->fh_flags = (f |= a_FLTHTML_NOPUT);
         break;

      case '>':
         /* Weird tagsoup around, do we actually parse a tag? */
         if(!(f & a_FLTHTML_NOPUT))
            goto jdo_c;
         cp[0] = c;
         cp[1] = '\0';
         f &= ~(a_FLTHTML_NOPUT | a_FLTHTML_ENT);
         self->fh_flags = f;
         self = a_flthtml_check_tag(self, self->fh_bdat);
         *(cp = self->fh_bdat) = '\0'; /* xxx extra safety */
         /* Quick hack to get rid of redundant newline after <pre> XXX */
         if(!(f & a_FLTHTML_PRE) && (self->fh_flags & a_FLTHTML_PRE) &&
               len > 1 && *dat == '\n')
            ++dat, --len;
         break;

      case '\r': /* TODO CR should be stripped in lower level! (Only B64!?!) */
         break;
      case '\n':
         /* End of line is not considered unless we are in PRE section.
          * However, in FLTHTML_NOPUT mode we must be aware of tagsoup which
          * uses newlines for separating parameters */
         if(f & a_FLTHTML_NOPUT)
            goto jdo_c;
         self = (f & a_FLTHTML_PRE) ? a_flthtml_nl_force(self)
               : a_flthtml_putc(self, ' ');
         break;

      case '\t':
         if(!(f & a_FLTHTML_PRE))
            c = ' ';
         /* FALLTHRU */
      default:
jdo_c:
         /* If not currently parsing a tag and bypassing normal output.. */
         if(!(f & a_FLTHTML_NOPUT)){
            if(su_cs_is_cntrl(c))
               break;
            if(c == '&'){
               cp = self->fh_bdat;
               *cp++ = c;
               self->fh_flags = (f |= a_FLTHTML_NOPUT | a_FLTHTML_ENT);
            }else if(f & a_FLTHTML_PRE){
               self = a_flthtml_putc_premode(self, c);
               self->fh_flags &= ~a_FLTHTML_BLANK;
            }else
              self = a_flthtml_putc(self, c);
         }
         /* People are using & without &amp;ing it, so terminate entity
          * processing if it cannot be an entity XXX is_space() not enough */
         else if((f & a_FLTHTML_ENT) && (c == ';' || su_cs_is_space(c))){
            cp[0] = c;
            cp[1] = '\0';
            f &= ~(a_FLTHTML_NOPUT | a_FLTHTML_ENT);
            self->fh_flags = f;
            if(c == ';')
               self = a_flthtml_check_ent(self, self->fh_bdat,
                     P2UZ(cp + 1 - self->fh_bdat));
            else{
               self = a_flthtml_puts(self, self->fh_bdat);
               f = self->fh_flags;
            }
         }else{
            /* We may need to grow the buffer */
            if(PCMP(cp + 42/2, >=, cp_max)){
               uz i, m;

               i = P2UZ(cp - self->fh_bdat);
               m = P2UZ(self->fh_bmax - self->fh_bdat) + LINESIZE;

               cp = self->fh_bdat = su_REALLOC(self->fh_bdat, m);
               self->fh_bmax = cp_max = &cp[m -1];
               self->fh_curr = (cp += i);
            }
            *cp++ = c;
         }
      }
   }
   self->fh_curr = cp;

jleave:
  NYD_OU;
  return (self->fh_flags & a_FLTHTML_ERROR) ? -1 : rv;
}

/*
 * TODO Because we do not support filter chains yet this filter will be run
 * TODO in a dedicated subprocess, driven via a special fs_pipe_open() mode
 */
static boole a_flthtml__hadpipesig;
static void
a_flthtml__onpipe(int signo){
   NYD; /* Signal handler */
   UNUSED(signo);
   a_flthtml__hadpipesig = TRU1;
}

int
mx_flthtml_process_main(void){
   char buf[mx_BUFFER_SIZE];
   struct mx_flthtml hf;
   uz i;
   int rv;
   NYD_IN;

   a_flthtml__hadpipesig = FAL0;
   safe_signal(SIGPIPE, &a_flthtml__onpipe);

   mx_flthtml_init(&hf);
   mx_flthtml_reset(&hf, n_stdout);

   for(;;){
      if((i = fread(buf, sizeof(buf[0]), NELEM(buf), n_stdin)) == 0){
         rv = !feof(n_stdin);
         break;
      }

      if((rv = a_flthtml__hadpipesig))
         break;
      /* Just use this directly.. */
      if(mx_flthtml_push(&hf, buf, i) < 0){
         rv = 1;
         break;
      }
   }
   if(rv == 0 && mx_flthtml_flush(&hf) < 0)
      rv = 1;

   mx_flthtml_destroy(&hf);

   rv |= a_flthtml__hadpipesig;

   NYD_OU;
   return rv;
}

void
mx_flthtml_init(struct mx_flthtml *self){
   NYD_IN;

   /* (Rather redundant though) */
   su_mem_set(self, 0, sizeof *self);

   NYD_OU;
}

void
mx_flthtml_destroy(struct mx_flthtml *self){
   NYD_IN;

   mx_flthtml_reset(self, NIL);

   NYD_OU;
}

void
mx_flthtml_reset(struct mx_flthtml *self, FILE *f){
   struct mx_flthtml_href *hfhp;
   NYD_IN;

   while((hfhp = self->fh_hrefs) != NIL){
      self->fh_hrefs = hfhp->fhh_next;
      su_FREE(hfhp);
   }

   if(self->fh_bdat != NIL)
      su_FREE(self->fh_bdat);
   if(self->fh_line != NIL)
      su_FREE(self->fh_line);

   su_mem_set(self, 0, sizeof *self);

   if(f != NIL){
      u32 sw;

      sw = MAX(a_FLTHTML_MINLEN, mx_termios_dimen.tiosd_width);
      self->fh_line = su_ALLOC(S(uz,sw) * n_mb_cur_max +1);
      self->fh_lmax = sw;

      if(n_psonce & n_PSO_UNICODE) /* TODO not truly generic */
         self->fh_flags = a_FLTHTML_UTF8;
      self->fh_os = f;
   }

   NYD_OU;
}

sz
mx_flthtml_push(struct mx_flthtml *self, char const *dat, uz len){
   sz rv;
   NYD_IN;

   rv = a_flthtml_add_data(self, dat, len);

   NYD_OU;
   return rv;
}

sz
mx_flthtml_flush(struct mx_flthtml *self){
   sz rv;
   NYD_IN;

   rv = a_flthtml_add_data(self, NIL, 0);
   rv |= !fflush(self->fh_os) ? 0 : -1;

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_FILTER_HTML_TAGSOUP */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_FILTER_HTML
/* s-it-mode */
