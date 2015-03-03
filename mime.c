/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME support functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

enum mime_type {
   /* Note that the _mt_typnames[] order */
   _MT_APPLICATION,
   _MT_AUDIO,
   _MT_IMAGE,
   _MT_MESSAGE,
   _MT_MULTIPART,
   _MT_TEXT,
   _MT_VIDEO,
   _MT_OTHER,
   __MT_TMIN   = 0,
   __MT_TMAX   = _MT_OTHER,
   __MT_TMASK  = 0x07,

   _MT_REGEX   = 1<< 3,       /* Line contains regex */
   _MT_LOADED  = 1<< 4,       /* Not struct mtbltin */
   _MT_USR     = 1<< 5,       /* MIME_TYPES_USR */
   _MT_SYS     = 1<< 6        /* MIME_TYPES_SYS */
};

struct mtbltin {
   ui32_t      mtb_flags;
   ui32_t      mtb_mtlen;
   char const  *mtb_line;
};

struct mtloaded {
   char        mtl_line[VFIELD_SIZE(8)];
};

struct mtnode {
   struct mtnode  *mt_next;
   ui32_t         mt_flags;
   ui32_t         mt_mtlen;   /* Length of MIME type string, rest thereafter */
#ifdef HAVE_REGEX
   regex_t        *mt_regex;
#endif
   union {
      struct mtbltin const *_bltin;
      struct mtloaded      _loaded;
   }              mt;
};

static struct mtbltin const   _mt_bltin[] = {
#include "mime_types.h"
};

static char const             _mt_typnames[][16] = {
   "application/", "audio/", "image/",
   "message/", "multipart/", "text/",
   "video/"
};
CTA(_MT_APPLICATION == 0 && _MT_AUDIO == 1 && _MT_IMAGE == 2 &&
   _MT_MESSAGE == 3 && _MT_MULTIPART == 4 && _MT_TEXT == 5 &&
   _MT_VIDEO == 6);

static struct mtnode          *_mt_list;

static char                   *_cs_iter_base, *_cs_iter;
#ifdef HAVE_ICONV
# define _CS_ITER_GET() ((_cs_iter != NULL) ? _cs_iter : charset_get_8bit())
#else
# define _CS_ITER_GET() ((_cs_iter != NULL) ? _cs_iter : charset_get_lc())
#endif
#define _CS_ITER_STEP() _cs_iter = n_strsep(&_cs_iter_base, ',', TRU1)

/* Initialize MIME type list / (lazy) create regular expression.
 * __mt_load_file will prepend all entries of file onto *inject in order.
 * Note: _mt_regcomp() will destruct mtnp if regex can't be compiled! */
static void             _mt_init(void);
static bool_t           __mt_load_file(ui32_t orflags,
                           char const *file, char **line, size_t *linesize);
static struct mtnode *  __mt_add_line(ui32_t orflags,
                           char const *line, size_t len);
#ifdef HAVE_REGEX
static bool_t           _mt_regcomp(struct mtnode *mtnp);
#endif

/* Is 7-bit enough? */
#ifdef HAVE_ICONV
static bool_t           _has_highbit(char const *s);
static bool_t           _name_highbit(struct name *np);
#endif

/* Get the conversion that matches *encoding* */
static enum conversion  _conversion_by_encoding(void);

/* fwrite(3) while checking for displayability */
static ssize_t          _fwrite_td(struct str const *input, enum tdflags flags,
                           struct str *rest, struct quoteflt *qf);

static size_t           delctrl(char *cp, size_t sz);

static int              is_this_enc(char const *line, char const *encoding);

/* Convert header fields to RFC 1522 format and write to the file fo */
static ssize_t          mime_write_tohdr(struct str *in, FILE *fo);

/* Write len characters of the passed string to the passed file, doing charset
 * and header conversion */
static ssize_t          convhdra(char const *str, size_t len, FILE *fp);

/* Write an address to a header field */
static ssize_t          mime_write_tohdr_a(struct str *in, FILE *f);

/* Append to buf, handling resizing */
static void             _append_str(char **buf, size_t *sz, size_t *pos,
                           char const *str, size_t len);
static void             _append_conv(char **buf, size_t *sz, size_t *pos,
                           char const *str, size_t len);

static void
_mt_init(void)
{
   struct mtnode *tail;
   char c, *line; /* TODO line pool (below) */
   size_t linesize;
   ui32_t i, j;
   char const *srcs_arr[10], *ccp, **srcs;
   NYD_ENTER;

   /* Always load our builtins */
   for (tail = NULL, i = 0; i < NELEM(_mt_bltin); ++i) {
      struct mtbltin const *mtbp = _mt_bltin + i;
      struct mtnode *mtnp = smalloc(sizeof *mtnp);

      if (tail != NULL)
         tail->mt_next = mtnp;
      else
         _mt_list = mtnp;
      tail = mtnp;
      mtnp->mt_next = NULL;
      mtnp->mt_flags = mtbp->mtb_flags;
      mtnp->mt_mtlen = mtbp->mtb_mtlen;
#ifdef HAVE_REGEX
      mtnp->mt_regex = NULL;
#endif
      mtnp->mt._bltin = mtbp;
   }

   /*  */
   if ((ccp = ok_vlook(mimetypes_load_control)) == NULL)
      ccp = "US";
   else if (*ccp == '\0')
      goto jpolish;

   srcs = srcs_arr + 2;
   srcs[-1] = srcs[-2] = NULL;

#ifdef HAVE_REGEX
   if (strchr(ccp, '=') != NULL) {
      line = savestr(ccp);

      while ((ccp = n_strsep(&line, ',', TRU1)) != NULL) {
         switch ((c = *ccp)) {
         case 'S': case 's':
            srcs_arr[1] = MIME_TYPES_SYS;
            if (0) {
               /* FALLTHRU */
         case 'U': case 'u':
               srcs_arr[0] = MIME_TYPES_USR;
            }
            if (ccp[1] != '\0')
               goto jecontent;
            break;
         case 'F': case 'f':
            if (*++ccp == '=' && *++ccp != '\0') {
               if (PTR2SIZE(srcs - srcs_arr) < NELEM(srcs_arr))
                  *srcs++ = ccp;
               else if (options & OPT_D_V)
                  fprintf(stderr,
                     _("*mimetypes-load-control*: too many sources, "
                        "skipping \"%s\"\n"), ccp);
               continue;
            }
            /* FALLTHRU */
         default:
            if (options & OPT_D_V)
jecontent:
               fprintf(stderr,
                  _("*mimetypes-load-control*: unsupported content: \"%s\"\n"),
                  ccp);
            break;
         }
      }
   } else
#endif /* HAVE_REGEX */
   for (i = 0; (c = ccp[i]) != '\0'; ++i)
      switch (c) {
      case 'S': case 's': srcs_arr[1] = MIME_TYPES_SYS; break;
      case 'U': case 'u': srcs_arr[0] = MIME_TYPES_USR; break;
      default:
         if (options & OPT_D_V)
            fprintf(stderr,
               _("*mimetypes-load-control*: unsupported content: \"%s\"\n"),
               ccp);
         break;
      }

   /* Load all desired sources in order */
   line = NULL;
   linesize = 0;
   for (j = 0, i = (ui32_t)PTR2SIZE(srcs - srcs_arr), srcs = srcs_arr; i > 0;
         ++j, ++srcs, --i)
      if (*srcs == NULL)
         continue;
      else if (!__mt_load_file(
            (j == 0 ? _MT_USR : (j == 1 ? _MT_SYS : _MT_REGEX)),
            *srcs, &line, &linesize)) {
         if ((options & OPT_D_V) || j > 1)
            fprintf(stderr,
               _("*mimetypes-load-control*: can't open or load \"%s\"\n"),
               *srcs);
      }
   if (line != NULL)
      free(line);

   /* Finally, in debug or very verbose mode, compile regular expressions right
    * away in order to be able to produce compact block of info messages */
jpolish:
#ifdef HAVE_REGEX
   if (options & OPT_D_VV)
      for (tail = _mt_list; tail != NULL;) {
         struct mtnode *xnp = tail;
         tail = tail->mt_next;
         if (xnp->mt_flags & _MT_REGEX)
            _mt_regcomp(xnp);
      }
#endif
   NYD_LEAVE;
}

static bool_t
__mt_load_file(ui32_t orflags, char const *file, char **line, size_t *linesize)
{
   char const *cp;
   FILE *fp;
   struct mtnode *head, *tail, *mtnp;
   size_t len;
   NYD_ENTER;

   if ((cp = file_expand(file)) == NULL || (fp = Fopen(cp, "r")) == NULL) {
      cp = NULL;
      goto jleave;
   }

   for (head = tail = NULL; fgetline(line, linesize, NULL, &len, fp, 0) != 0;)
      if ((mtnp = __mt_add_line(orflags, *line, len)) != NULL) {
         if (head == NULL)
            head = tail = mtnp;
         else
            tail->mt_next = mtnp;
         tail = mtnp;
      }
   if (head != NULL) {
      tail->mt_next = _mt_list;
      _mt_list = head;
   }

   Fclose(fp);
jleave:
   NYD_LEAVE;
   return (cp != NULL);
}

static struct mtnode *
__mt_add_line(ui32_t orflags, char const *line, size_t len)
{
   struct mtnode *mtnp = NULL;
   char const *typ, *subtyp;
   size_t tlen, i;
   NYD_ENTER;

   /* Drop anything after a comment first */
   if ((typ = memchr(line, '#', len)) != NULL)
      len = PTR2SIZE(typ - line);

   /* Then trim any trailing whitespace from line (including NL/CR) */
   while (len > 0 && spacechar(line[len - 1]))
      --len;

   /* Isolate MIME type, trim any whitespace from it */
   while (len > 0 && blankchar(*line))
      ++line, --len;
   typ = line;
   while (len > 0 && !blankchar(*line))
      ++line, --len;
   if (len == 0)
      goto jleave;
   tlen = PTR2SIZE(line - typ);
   /* Ignore empty lines */
   if (tlen == 0)
      goto jleave;

   if ((subtyp = memchr(typ, '/', tlen)) == NULL) {
      if (options & OPT_D_V)
         fprintf(stderr, "mime.types(5): invalid MIME type: \"%s\"\n", typ);
      goto jleave;
   }
   ++subtyp;

   /* Map to mime_type */
   tlen = PTR2SIZE(subtyp - typ);
   for (i = __MT_TMIN;;) {
      if (!ascncasecmp(_mt_typnames[i], typ, tlen)) {
         orflags |= i;
         tlen = PTR2SIZE(line - subtyp);
         typ = subtyp;
         break;
      }
      if (++i == __MT_TMAX) {
         orflags |= _MT_OTHER;
         tlen = PTR2SIZE(line - typ);
         break;
      }
   }

   /* Strip leading whitespace from the list of extensions / the regex;
    * trailing WS has already been trimmed away above.
    * Be silent on slots which define a mimetype without any value */
   while (len > 0 && blankchar(*line))
      ++line, --len;
   if (len == 0)
      goto jleave;

   /*  */
   mtnp = smalloc(sizeof(*mtnp) -
         VFIELD_SIZEOF(struct mtloaded, mtl_line) + tlen + len +1);
   mtnp->mt_next = NULL;
   mtnp->mt_flags = (orflags |= _MT_LOADED);
   mtnp->mt_mtlen = (ui32_t)tlen;
#ifdef HAVE_REGEX
   mtnp->mt_regex = NULL;
#endif
   memcpy(mtnp->mt._loaded.mtl_line, typ, tlen);
   memcpy(mtnp->mt._loaded.mtl_line + tlen, line, len);
   tlen += len;
   mtnp->mt._loaded.mtl_line[tlen] = '\0';

jleave:
   NYD_LEAVE;
   return mtnp;
}

#ifdef HAVE_REGEX
static bool_t
_mt_regcomp(struct mtnode *mtnp)
{
   regex_t *rep;
   char const *line;
   NYD2_ENTER;

   assert(mtnp->mt_flags & _MT_REGEX);

   /* Lines of the dynamically loaded f=FILE will only be compiled as regular
    * expressions if any "magical" regular expression characters are seen */
   if ((mtnp->mt_flags & (_MT_LOADED | _MT_USR | _MT_SYS)) == _MT_LOADED) {
      line = mtnp->mt._loaded.mtl_line + mtnp->mt_mtlen;

      if (!is_maybe_regex(line)) {
         mtnp->mt_flags &= ~_MT_REGEX;
         goto jleave;
      }
   } else
      line = mtnp->mt._bltin->mtb_line + mtnp->mt_mtlen;

   rep = smalloc(sizeof *rep);

   if (regcomp(rep, line, REG_EXTENDED | REG_ICASE | REG_NOSUB) == 0)
      mtnp->mt_regex = rep;
   else {
      free(rep);
      fprintf(stderr, _("Invalid regular expression: \"%s\"\n"), line);

      if (mtnp == _mt_list)
         _mt_list = mtnp->mt_next;
      else {
         struct mtnode *x = _mt_list;
         while (x->mt_next != mtnp)
            x = x->mt_next;
         x->mt_next = mtnp->mt_next;
      }

      free(mtnp);
      mtnp = NULL;
   }

jleave:
   NYD2_LEAVE;
   return (mtnp != NULL);
}
#endif /* HAVE_REGEX */

#ifdef HAVE_ICONV
static bool_t
_has_highbit(char const *s)
{
   bool_t rv = TRU1;
   NYD_ENTER;

   if (s) {
      do
         if ((ui8_t)*s & 0x80)
            goto jleave;
      while (*s++ != '\0');
   }
   rv = FAL0;
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_name_highbit(struct name *np)
{
   bool_t rv = TRU1;
   NYD_ENTER;

   while (np) {
      if (_has_highbit(np->n_name) || _has_highbit(np->n_fullname))
         goto jleave;
      np = np->n_flink;
   }
   rv = FAL0;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_ICONV */

static enum conversion
_conversion_by_encoding(void)
{
   char const *cp;
   enum conversion ret;
   NYD_ENTER;

   if ((cp = ok_vlook(encoding)) == NULL)
      ret = MIME_DEFAULT_ENCODING;
   else if (!strcmp(cp, "quoted-printable"))
      ret = CONV_TOQP;
   else if (!strcmp(cp, "8bit"))
      ret = CONV_8BIT;
   else if (!strcmp(cp, "base64"))
      ret = CONV_TOB64;
   else {
      fprintf(stderr, _("Warning: invalid encoding %s, using base64\n"),
         cp);
      ret = CONV_TOB64;
   }
   NYD_LEAVE;
   return ret;
}

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
_fwrite_td(struct str const *input, enum tdflags flags, struct str *rest,
   struct quoteflt *qf)
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
   NYD_ENTER;
   UNUSED(rest);

   in = *input;
   out.s = NULL;
   out.l = 0;

#ifdef HAVE_ICONV
   if ((flags & TD_ICONV) && iconvd != (iconv_t)-1) {
      char *buf = NULL;

      if (rest != NULL && rest->l > 0) {
         in.l = rest->l + input->l;
         in.s = buf = smalloc(in.l +1);
         memcpy(in.s, rest->s, rest->l);
         memcpy(in.s + rest->l, input->s, input->l);
         rest->l = 0;
      }

      if (n_iconv_str(iconvd, &out, &in, &in, TRU1) != 0 && rest != NULL &&
            in.l > 0) {
         /* Incomplete multibyte at EOF is special */
         if (flags & _TD_EOF) {
            out.s = srealloc(out.s, out.l + 4);
            /* TODO 0xFFFD out.s[out.l++] = '[';*/
            out.s[out.l++] = '?'; /* TODO 0xFFFD !!! */
            /* TODO 0xFFFD out.s[out.l++] = ']';*/
         } else
            n_str_add(rest, &in);
      }
      in = out;
      out.s = NULL;
      flags &= ~_TD_BUFCOPY;

      if (buf != NULL)
         free(buf);
   }
#endif

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
      free(out.s);
   if (in.s != input->s)
      free(in.s);
   safe_signal(SIGPIPE, __mimefwtd_opipe);
   if (__mimefwtd_sig != 0)
      kill(0, __mimefwtd_sig);
   NYD_LEAVE;
   return rv;
}

static size_t
delctrl(char *cp, size_t sz)
{
   size_t x = 0, y = 0;
   NYD_ENTER;

   while (x < sz) {
      if (!cntrlchar(cp[x]))
         cp[y++] = cp[x];
      ++x;
   }
   NYD_LEAVE;
   return y;
}

static int
is_this_enc(char const *line, char const *encoding)
{
   int rv, c, quoted = 0;
   NYD_ENTER;

   if (*line == '"')
      quoted = 1, ++line;
   rv = 0;
   while (*line != '\0' && *encoding)
      if ((c = *line++, lowerconv(c) != *encoding++))
         goto jleave;
   rv = 1;
   if (quoted && *line == '"')
      goto jleave;
   if (*line == '\0' || whitechar(*line))
      goto jleave;
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static ssize_t
mime_write_tohdr(struct str *in, FILE *fo)
{
   /* TODO mime_write_tohdr(): we don't know the name of our header->maxcol..
    * TODO  MIME/send layer rewrite: more available state!!
    * TODO   Because of this we cannot make a difference in between structured
    * TODO   and unstructured headers (RFC 2047, 5. (2))
    * TODO NOT MULTIBYTE SAFE IF AN ENCODED WORD HAS TO BE SPLITTED!
    * TODO  To be better we had to mbtowc_l() (non-std! and no locale!!) and
    * TODO   work char-wise!  ->  S-CText..
    * TODO  The real problem for STD compatibility is however that "in" is
    * TODO   already iconv(3) encoded to the target character set!  We could
    * TODO   also solve it (very expensively!) if we would narrow down to an
    * TODO   encoded word and then iconv(3)+CTencode in one go, in which case
    * TODO   multibyte errors could be catched!
    * TODO All this doesn't take any care about RFC 2231, but simply and
    * TODO   falsely applies RFC 2047 and normal RFC 822/5322 folding to values
    * TODO   of parameters; part of the problem is that we just don't make a
    * TODO   difference in structured and unstructed headers, as long in TODO!
    * TODO   See also RFC 2047, 5., .." These are the ONLY locations"..
    * TODO   So, for now we require mutt(1)s "rfc2047_parameters=yes" support!!
    * TODO BTW.: the purpose of QP is to allow non MIME-aware ASCII guys to
    * TODO read the field nonetheless... */
   enum {
      /* Maximum line length *//* XXX we are too inflexible and could use
       * XXX MIME_LINELEN unless an RFC 2047 encoding was actually used */
      _MAXCOL  = MIME_LINELEN_RFC2047
   };
   enum {
      _FIRST      = 1<<0,  /* Nothing written yet, start of string */
      _NO_QP      = 1<<1,  /* No quoted-printable allowed */
      _NO_B64     = 1<<2,  /* Ditto, base64 */
      _ENC_LAST   = 1<<3,  /* Last round generated encoded word */
      _SHOULD_BEE = 1<<4,  /* Avoid lines longer than SHOULD via encoding */
      _RND_SHIFT  = 5,
      _RND_MASK   = (1<<_RND_SHIFT) - 1,
      _SPACE      = 1<<(_RND_SHIFT+1),    /* Leading whitespace */
      _8BIT       = 1<<(_RND_SHIFT+2),    /* High bit set */
      _ENCODE     = 1<<(_RND_SHIFT+3),    /* Need encoding */
      _ENC_B64    = 1<<(_RND_SHIFT+4),    /* - let it be base64 */
      _OVERLONG   = 1<<(_RND_SHIFT+5)     /* Temporarily rised limit */
   } flags = _FIRST;

   struct str cout, cin;
   char const *cset7, *cset8, *wbot, *upper, *wend, *wcur;
   ui32_t cset7_len, cset8_len;
   size_t col, i, j;
   ssize_t sz;
   NYD_ENTER;

   cout.s = NULL, cout.l = 0;
   cset7 = charset_get_7bit();
   cset7_len = (ui32_t)strlen(cset7);
   cset8 = _CS_ITER_GET(); /* TODO MIME/send layer: iter active? iter! else */
   cset8_len = (ui32_t)strlen(cset8);

   /* RFC 1468, "MIME Considerations":
    *     ISO-2022-JP may also be used in MIME Part 2 headers.  The "B"
    *     encoding should be used with ISO-2022-JP text. */
   /* TODO of course, our current implementation won't deal properly with
    * TODO any stateful encoding at all... (the standard says each encoded
    * TODO word must include all necessary reset sequences..., i.e., each
    * TODO encoded word must be a self-contained iconv(3) life cycle) */
   if (!asccasecmp(cset8, "iso-2022-jp"))
      flags |= _NO_QP;

   wbot = in->s;
   upper = wbot + in->l;
   col = sizeof("Content-Transfer-Encoding: ") -1; /* dreadful thing */

   for (sz = 0; wbot < upper; flags &= ~_FIRST, wbot = wend) {
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
       * wether our 7-bit charset suffices to represent the data */
      for (wend = wcur; wend < upper; ++wend) {
         if (whitechar(*wend))
            break;
         if ((uc_i)*wend & 0x80)
            flags |= _8BIT;
      }

      /* Decide wether the range has to become encoded or not */
      i = PTR2SIZE(wend - wcur);
      j = mime_cte_mustquote(wcur, i, MIMECTE_ISHEAD);
      /* If it just cannot fit on a SHOULD line length, force encode */
      if (i >= _MAXCOL) {
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
            if (col >= _MAXCOL) {
               putc('\n', fo);
               ++sz;
               col = 0;
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
         if (i + col <= (flags & _OVERLONG ? MIME_LINELEN_MAX : _MAXCOL)) {
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
            flags |= _OVERLONG;
            goto jnoenc_retry;
         }

         /* It is so long that it needs to be broken, effectively causing
          * artificial spaces to be inserted (bad standard), yuck */
         /* todo This is not multibyte safe, as above; and completely stupid
          * todo P.S.: our _SHOULD_BEE prevents these cases in the meanwhile */
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
         wcur = UNCONST("");
         if ((flags & (_ENC_LAST | _SPACE)) != _SPACE) {
            /* Reinclude whitespace */
            flags &= ~_SPACE;
            /* We don't need to place a separator at the very beginning */
            if (!(flags & _FIRST))
               wcur = UNCONST(" ");
         } else
            wcur = wbot++;

         flags |= _ENC_LAST;

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
         cin.s = UNCONST(wbot);
         cin.l = PTR2SIZE(wend - wbot);

         if (flags & _ENC_B64)
            j = b64_encode(&cout, &cin, B64_ISHEAD | B64_ISENCWORD)->l;
         else
            j = qp_encode(&cout, &cin, QP_ISHEAD | QP_ISENCWORD)->l;
         /* (Avoid trigraphs in the RFC 2047 placeholder..) */
         i = j + (flags & _8BIT ? cset8_len : cset7_len) + sizeof("=!!B!!=") -1;
         if (*wcur != '\0')
            ++i;

jenc_retry_same:
         /* Unfortunately RFC 2047 explicitly disallows encoded words to be
          * longer (just like RFC 5322's "a line SHOULD fit in 78 but MAY be
          * 998 characters long"), so we cannot use the _OVERLONG mechanism,
          * even though all tested mailers seem to support it */
         if (i + col <= (/*flags & _OVERLONG ? MIME_LINELEN_MAX :*/ _MAXCOL)) {
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
               wcur = UNCONST("");
               /*flags |= _OVERLONG;*/
               goto jenc_retry_same;
            } else {
               putc((uc_i)*wcur, fo);
               if (whitechar(*(wcur = wbot)))
                  ++wbot;
               else {
                  flags &= ~_SPACE;
                  wcur = UNCONST("");
               }
               /*flags &= ~_OVERLONG;*/
               goto jenc_retry;
            }
         }

         /* It is so long that it needs to be broken, effectively causing
          * artificial data to be inserted (bad standard), yuck */
         /* todo This is not multibyte safe, as above */
         /*if (!(flags & _OVERLONG)) {
            flags |= _OVERLONG;
            goto jenc_retry;
         }*/
         i = PTR2SIZE(wend - wbot) + !!(flags & _SPACE);
         j = 3 + !(flags & _ENC_B64);
         for (;;) {
            wend -= j;
            i -= j;
            /* (Note the problem most likely is the transfer-encoding blow,
             * which is why we test this *after* the decrements.. */
            if (i <= _MAXCOL)
               break;
         }
         goto jenc_retry;
      }
   }

   if (cout.s != NULL)
      free(cout.s);
   NYD_LEAVE;
   return sz;
}

static ssize_t
convhdra(char const *str, size_t len, FILE *fp)
{
#ifdef HAVE_ICONV
   struct str ciconv;
#endif
   struct str cin;
   ssize_t ret = 0;
   NYD_ENTER;

   cin.s = UNCONST(str);
   cin.l = len;
#ifdef HAVE_ICONV
   ciconv.s = NULL;
   if (iconvd != (iconv_t)-1) {
      ciconv.l = 0;
      if (n_iconv_str(iconvd, &ciconv, &cin, NULL, FAL0) != 0)
         goto jleave;
      cin = ciconv;
   }
#endif
   ret = mime_write_tohdr(&cin, fp);
#ifdef HAVE_ICONV
jleave:
   if (ciconv.s != NULL)
      free(ciconv.s);
#endif
   NYD_LEAVE;
   return ret;
}

static ssize_t
mime_write_tohdr_a(struct str *in, FILE *f) /* TODO error handling */
{
   char const *cp, *lastcp;
   ssize_t sz, x;
   NYD_ENTER;

   in->s[in->l] = '\0';
   lastcp = in->s;
   if ((cp = routeaddr(in->s)) != NULL && cp > lastcp) {
      if ((sz = convhdra(lastcp, PTR2SIZE(cp - lastcp), f)) < 0)
         goto jleave;
      lastcp = cp;
   } else {
      cp = in->s;
      sz = 0;
   }

   for ( ; *cp != '\0'; ++cp) {
      switch (*cp) {
      case '(':
         sz += fwrite(lastcp, 1, PTR2SIZE(cp - lastcp + 1), f);
         lastcp = ++cp;
         cp = skip_comment(cp);
         if (--cp > lastcp) {
            if ((x = convhdra(lastcp, PTR2SIZE(cp - lastcp), f)) < 0) {
               sz = x;
               goto jleave;
            }
            sz += x;
         }
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
      sz += fwrite(lastcp, 1, PTR2SIZE(cp - lastcp), f);
jleave:
   NYD_LEAVE;
   return sz;
}

static void
_append_str(char **buf, size_t *sz, size_t *pos, char const *str, size_t len)
{
   NYD_ENTER;
   *buf = srealloc(*buf, *sz += len);
   memcpy(&(*buf)[*pos], str, len);
   *pos += len;
   NYD_LEAVE;
}

static void
_append_conv(char **buf, size_t *sz, size_t *pos, char const *str, size_t len)
{
   struct str in, out;
   NYD_ENTER;

   in.s = UNCONST(str);
   in.l = len;
   mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);
   _append_str(buf, sz, pos, out.s, out.l);
   free(out.s);
   NYD_LEAVE;
}

FL char const *
charset_get_7bit(void)
{
   char const *t;
   NYD_ENTER;

   if ((t = ok_vlook(charset_7bit)) == NULL)
      t = CHARSET_7BIT;
   NYD_LEAVE;
   return t;
}

#ifdef HAVE_ICONV
FL char const *
charset_get_8bit(void)
{
   char const *t;
   NYD_ENTER;

   if ((t = ok_vlook(CHARSET_8BIT_OKEY)) == NULL)
      t = CHARSET_8BIT;
   NYD_LEAVE;
   return t;
}
#endif

FL char const *
charset_get_lc(void)
{
   char const *t;
   NYD_ENTER;

   if ((t = ok_vlook(ttycharset)) == NULL)
      t = CHARSET_8BIT;
   NYD_LEAVE;
   return t;
}

FL bool_t
charset_iter_reset(char const *a_charset_to_try_first)
{
   char const *sarr[3];
   size_t sarrl[3], len;
   char *cp;
   NYD_ENTER;
   UNUSED(a_charset_to_try_first);

#ifdef HAVE_ICONV
   sarr[0] = a_charset_to_try_first;
   if ((sarr[1] = ok_vlook(sendcharsets)) == NULL &&
         ok_blook(sendcharsets_else_ttycharset))
      sarr[1] = charset_get_lc();
   sarr[2] = charset_get_8bit();
#else
   sarr[2] = charset_get_lc();
#endif

   sarrl[2] = len = strlen(sarr[2]);
#ifdef HAVE_ICONV
   if ((cp = UNCONST(sarr[1])) != NULL)
      len += (sarrl[1] = strlen(cp));
   else
      sarrl[1] = 0;
   if ((cp = UNCONST(sarr[0])) != NULL)
      len += (sarrl[0] = strlen(cp));
   else
      sarrl[0] = 0;
#endif

   _cs_iter_base = cp = salloc(len + 1 + 1 +1);

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
   NYD_LEAVE;
   return (_cs_iter != NULL);
}

FL bool_t
charset_iter_next(void)
{
   bool_t rv;
   NYD_ENTER;

   _CS_ITER_STEP();
   rv = (_cs_iter != NULL);
   NYD_LEAVE;
   return rv;
}

FL bool_t
charset_iter_is_valid(void)
{
   bool_t rv;
   NYD_ENTER;

   rv = (_cs_iter != NULL);
   NYD_LEAVE;
   return rv;
}

FL char const *
charset_iter(void)
{
   char const *rv;
   NYD_ENTER;

   rv = _cs_iter;
   NYD_LEAVE;
   return rv;
}

FL void
charset_iter_recurse(char *outer_storage[2]) /* TODO LEGACY FUN, REMOVE */
{
   NYD_ENTER;
   outer_storage[0] = _cs_iter_base;
   outer_storage[1] = _cs_iter;
   NYD_LEAVE;
}

FL void
charset_iter_restore(char *outer_storage[2]) /* TODO LEGACY FUN, REMOVE */
{
   NYD_ENTER;
   _cs_iter_base = outer_storage[0];
   _cs_iter = outer_storage[1];
   NYD_LEAVE;
}

#ifdef HAVE_ICONV
FL char const *
need_hdrconv(struct header *hp, enum gfield w) /* TODO once only, then iter */
{
   char const *ret = NULL;
   NYD_ENTER;

   if (w & GIDENT) {
      if (hp->h_mft != NULL) {
         if (_name_highbit(hp->h_mft))
            goto jneeds;
      }
      if (hp->h_from != NULL) {
         if (_name_highbit(hp->h_from))
            goto jneeds;
      } else if (_has_highbit(myaddrs(NULL)))
         goto jneeds;
      if (hp->h_organization) {
         if (_has_highbit(hp->h_organization))
            goto jneeds;
      } else if (_has_highbit(ok_vlook(ORGANIZATION)))
         goto jneeds;
      if (hp->h_replyto) {
         if (_name_highbit(hp->h_replyto))
            goto jneeds;
      } else if (_has_highbit(ok_vlook(replyto)))
         goto jneeds;
      if (hp->h_sender) {
         if (_name_highbit(hp->h_sender))
            goto jneeds;
      } else if (_has_highbit(ok_vlook(sender)))
         goto jneeds;
   }
   if ((w & GTO) && _name_highbit(hp->h_to))
      goto jneeds;
   if ((w & GCC) && _name_highbit(hp->h_cc))
      goto jneeds;
   if ((w & GBCC) && _name_highbit(hp->h_bcc))
      goto jneeds;
   if ((w & GSUBJECT) && _has_highbit(hp->h_subject))
jneeds:
      ret = _CS_ITER_GET(); /* TODO MIME/send: iter active? iter! else */
   NYD_LEAVE;
   return ret;
}
#endif /* HAVE_ICONV */

FL enum mimeenc
mime_getenc(char *p)
{
   enum mimeenc rv;
   NYD_ENTER;

   if (is_this_enc(p, "7bit"))
      rv = MIME_7B;
   else if (is_this_enc(p, "8bit"))
      rv = MIME_8B;
   else if (is_this_enc(p, "base64"))
      rv = MIME_B64;
   else if (is_this_enc(p, "binary"))
      rv = MIME_BIN;
   else if (is_this_enc(p, "quoted-printable"))
      rv = MIME_QP;
   else
      rv = MIME_NONE;
   NYD_LEAVE;
   return rv;
}

FL char *
mime_getparam(char const *param, char *h)
{
   char *p = h, *q, *rv = NULL;
   int c;
   size_t sz;
   NYD_ENTER;

   sz = strlen(param);
   if (!whitechar(*p)) {
      c = '\0';
      while (*p && (*p != ';' || c == '\\')) {
         c = (c == '\\') ? '\0' : *p;
         ++p;
      }
      if (*p++ == '\0')
         goto jleave;
   }

   for (;;) {
      while (whitechar(*p))
         ++p;
      if (!ascncasecmp(p, param, sz)) {
         p += sz;
         while (whitechar(*p))
            ++p;
         if (*p++ == '=')
            break;
      }
      c = '\0';
      while (*p != '\0' && (*p != ';' || c == '\\')) {
         if (*p == '"' && c != '\\') {
            ++p;
            while (*p != '\0' && (*p != '"' || c == '\\')) {
               c = (c == '\\') ? '\0' : *p;
               ++p;
            }
            ++p;
         } else {
            c = (c == '\\') ? '\0' : *p;
            ++p;
         }
      }
      if (*p++ == '\0')
         goto jleave;
   }
   while (whitechar(*p))
      ++p;

   q = p;
   if (*p == '"') {
      p++;
      if ((q = strchr(p, '"')) == NULL)
         goto jleave;
   } else {
      while (*q != '\0' && !whitechar(*q) && *q != ';')
         ++q;
   }
   sz = PTR2SIZE(q - p);
   rv = salloc(q - p +1);
   memcpy(rv, p, sz);
   rv[sz] = '\0';
jleave:
   NYD_LEAVE;
   return rv;
}

FL char *
mime_get_boundary(char *h, size_t *len)
{
   char *q = NULL, *p;
   size_t sz;
   NYD_ENTER;

   if ((p = mime_getparam("boundary", h)) != NULL) {
      sz = strlen(p);
      if (len != NULL)
         *len = sz + 2;
      q = salloc(sz + 2 +1);
      q[0] = q[1] = '-';
      memcpy(q + 2, p, sz);
      *(q + sz + 2) = '\0';
   }
   NYD_LEAVE;
   return q;
}

FL char *
mime_create_boundary(void)
{
   char *bp;
   NYD_ENTER;

   bp = salloc(48);
   snprintf(bp, 48, "=_%011" PRIu64 "=-%s=_",
      (ui64_t)time_current.tc_time, getrandstring(47 - (11 + 6)));
   NYD_LEAVE;
   return bp;
}

FL int
mime_classify_file(FILE *fp, char const **contenttype, char const **charset,
   int *do_iconv)
{
   /* TODO classify once only PLEASE PLEASE PLEASE */
   /* TODO BTW., after the MIME/send layer rewrite we could use a MIME
    * TODO boundary of "=-=-=" if we would add a B_ in EQ spirit to F_,
    * TODO and report that state to the outer world */
#define F_        "From "
#define F_SIZEOF  (sizeof(F_) -1)

   char f_buf[F_SIZEOF], *f_p = f_buf;
   enum {
      _CLEAN      = 0,     /* Plain RFC 2822 message */
      _NCTT       = 1<<0,  /* *contenttype == NULL */
      _ISTXT      = 1<<1,  /* *contenttype =~ text/ */
      _ISTXTCOK   = 1<<2,  /* _ISTXT + *mime-allow-text-controls* */
      _HIGHBIT    = 1<<3,  /* Not 7bit clean */
      _LONGLINES  = 1<<4,  /* MIME_LINELEN_LIMIT exceed. */
      _CTRLCHAR   = 1<<5,  /* Control characters seen */
      _HASNUL     = 1<<6,  /* Contains \0 characters */
      _NOTERMNL   = 1<<7,  /* Lacks a final newline */
      _TRAILWS    = 1<<8,  /* Blanks before NL */
      _FROM_      = 1<<9   /* ^From_ seen */
   } ctt = _CLEAN;
   enum conversion convert;
   ssize_t curlen;
   int c, lastc;
   NYD_ENTER;

   assert(ftell(fp) == 0x0l);

   *do_iconv = 0;

   if (*contenttype == NULL)
      ctt = _NCTT;
   else if (!ascncasecmp(*contenttype, "text/", 5))
      ctt = ok_blook(mime_allow_text_controls) ? _ISTXT | _ISTXTCOK : _ISTXT;
   convert = _conversion_by_encoding();

   if (fsize(fp) == 0)
      goto j7bit;

   /* We have to inspect the file content */
   for (curlen = 0, c = EOF;; ++curlen) {
      lastc = c;
      c = getc(fp);

      if (c == '\0') {
         ctt |= _HASNUL;
         if (!(ctt & _ISTXTCOK))
            break;
         continue;
      }
      if (c == '\n' || c == EOF) {
         if (curlen >= MIME_LINELEN_LIMIT)
            ctt |= _LONGLINES;
         if (c == EOF)
            break;
         if (blankchar(lastc))
            ctt |= _TRAILWS;
         f_p = f_buf;
         curlen = -1;
         continue;
      }
      /* A bit hairy is handling of \r=\x0D=CR.
       * RFC 2045, 6.7:
       * Control characters other than TAB, or CR and LF as parts of CRLF
       * pairs, must not appear.  \r alone does not force _CTRLCHAR below since
       * we cannot peek the next character.  Thus right here, inspect the last
       * seen character for if its \r and set _CTRLCHAR in a delayed fashion */
       /*else*/ if (lastc == '\r')
         ctt |= _CTRLCHAR;

      /* Control character? XXX this is all ASCII here */
      if (c < 0x20 || c == 0x7F) {
         /* RFC 2045, 6.7, as above ... */
         if (c != '\t' && c != '\r')
            ctt |= _CTRLCHAR;
         /* If there is a escape sequence in backslash notation defined for
          * this in ANSI X3.159-1989 (ANSI C89), don't treat it as a control
          * for real.  I.e., \a=\x07=BEL, \b=\x08=BS, \t=\x09=HT.  Don't follow
          * libmagic(1) in respect to \v=\x0B=VT.  \f=\x0C=NP; do ignore
          * \e=\x1B=ESC */
         if ((c >= '\x07' && c <= '\x0D') || c == '\x1B')
            continue;
         ctt |= _HASNUL; /* Force base64 */
         if (!(ctt & _ISTXTCOK))
            break;
      } else if ((ui8_t)c & 0x80) {
         ctt |= _HIGHBIT;
         /* TODO count chars with HIGHBIT? libmagic?
          * TODO try encode part - base64 if bails? */
         if (!(ctt & (_NCTT | _ISTXT))) { /* TODO _NCTT?? */
            ctt |= _HASNUL; /* Force base64 */
            break;
         }
      } else if (!(ctt & _FROM_) && UICMP(z, curlen, <, F_SIZEOF)) {
         *f_p++ = (char)c;
         if (UICMP(z, curlen, ==, F_SIZEOF - 1) &&
               PTR2SIZE(f_p - f_buf) == F_SIZEOF &&
               !memcmp(f_buf, F_, F_SIZEOF))
            ctt |= _FROM_;
      }
   }
   if (lastc != '\n')
      ctt |= _NOTERMNL;
   rewind(fp);

   if (ctt & _HASNUL) {
      convert = CONV_TOB64;
      /* Don't overwrite a text content-type to allow UTF-16 and such, but only
       * on request; else enforce what file(1)/libmagic(3) would suggest */
      if (ctt & _ISTXTCOK)
         goto jcharset;
      if (ctt & (_NCTT | _ISTXT))
         *contenttype = "application/octet-stream";
      if (*charset == NULL)
         *charset = "binary";
      goto jleave;
   }

   if (ctt & (_LONGLINES | _CTRLCHAR | _NOTERMNL | _TRAILWS | _FROM_)) {
      if (convert != CONV_TOB64)
         convert = CONV_TOQP;
      goto jstepi;
   }
   if (ctt & _HIGHBIT) {
jstepi:
      if (ctt & (_NCTT | _ISTXT))
         *do_iconv = ((ctt & _HIGHBIT) != 0);
   } else
j7bit:
      convert = CONV_7BIT;
   if (ctt & _NCTT)
      *contenttype = "text/plain";

   /* Not an attachment with specified charset? */
jcharset:
   if (*charset == NULL) /* TODO MIME/send: iter active? iter! else */
      *charset = (ctt & _HIGHBIT) ? _CS_ITER_GET() : charset_get_7bit();
jleave:
   NYD_LEAVE;
   return convert;
#undef F_
#undef F_SIZEOF
}

FL enum mimecontent
mime_classify_content_of_part(struct mimepart *mpp)
{
   enum mimecontent mc;
   char const *ct;
   union {char const *cp; long l;} mce;
   NYD_ENTER;

   mc = MIME_UNKNOWN;
   ct = mpp->m_ct_type_plain;

   if (!asccasecmp(ct, "application/octet-stream") && mpp->m_filename != NULL &&
         (mce.cp = ok_vlook(mime_counter_evidence)) != NULL) {
      ct = mime_classify_content_type_by_filename(mpp->m_filename);
      if (ct == NULL)
         /* TODO add bit 1 to possible *mime-counter-evidence* value
          * TODO and let it mean to save the attachment in
          * TODO a temporary file that mime_classify_file() can
          * TODO examine, and using MIME_TEXT if that gives us
          * TODO something that seems to be human readable?! */
         goto jleave;

      mce.l = strtol(mce.cp, NULL, 0);
      if (mce.l & MIMECE_USR_OVWR)
         mpp->m_ct_type_usr_ovwr = UNCONST(ct);
   }

   if (strchr(ct, '/') == NULL) /* For compatibility with non-MIME */
      mc = MIME_TEXT;
   else if (!asccasecmp(ct, "text/plain"))
      mc = MIME_TEXT_PLAIN;
   else if (!asccasecmp(ct, "text/html"))
      mc = MIME_TEXT_HTML;
   else if (!ascncasecmp(ct, "text/", 5))
      mc = MIME_TEXT;
   else if (!asccasecmp(ct, "message/rfc822"))
      mc = MIME_822;
   else if (!ascncasecmp(ct, "message/", 8))
      mc = MIME_MESSAGE;
   else if (!ascncasecmp(ct, "multipart/", 10)) {
      ct += sizeof("multipart/") -1;
      if (!asccasecmp(ct, "alternative"))
         mc = MIME_ALTERNATIVE;
      else if (!asccasecmp(ct, "related"))
         mc = MIME_RELATED;
      else if (!asccasecmp(ct, "digest"))
         mc = MIME_DIGEST;
      else
         mc = MIME_MULTI;
   } else if (!asccasecmp(ct, "application/x-pkcs7-mime") ||
         !asccasecmp(ct, "application/pkcs7-mime"))
      mc = MIME_PKCS7;
jleave:
   NYD_LEAVE;
   return mc;
}

FL char *
mime_classify_content_type_by_filename(char const *name)
{
   char *content = NULL;
   struct mtnode *mtnp;
   char const *n_ext;
   size_t nlen, elen, i;
   NYD_ENTER;

   if (_mt_list == NULL)
      _mt_init();
   if (NELEM(_mt_bltin) == 0 && _mt_list == (struct mtnode*)-1)
      goto jleave;

   if ((nlen = strlen(name)) == 0)
      goto jleave;

   /* Try to isolate an extension */
   for (n_ext = name + nlen, elen = 0;; ++elen)
      if (n_ext[-1] == '.')
         break;
      else if (--n_ext == name) {
         elen = 0;
         DBG( n_ext = name + nlen; )
         break;
      }

   /* And then check all the MIME types.  Though it seems rather odd and not
    * all checked implementations do it, */
   for (mtnp = _mt_list; mtnp != NULL;) {
      char const *line, *m_ext, *cp;
      struct mtnode *xnp = mtnp;

      mtnp = mtnp->mt_next;
      line = (xnp->mt_flags & _MT_LOADED)
            ? xnp->mt._loaded.mtl_line : xnp->mt._bltin->mtb_line;

#ifdef HAVE_REGEX
      if (xnp->mt_flags & _MT_REGEX) {
         if ((xnp->mt_regex == NULL && !_mt_regcomp(xnp)) ||
               regexec(xnp->mt_regex, name, 0,NULL, 0) == REG_NOMATCH)
            continue;
         goto jfound;
      } else
#endif
      for (m_ext = line + xnp->mt_mtlen;; m_ext = cp) {
         cp = m_ext;
         while (whitechar(*cp))
            ++cp;
         m_ext = cp;
         while (!whitechar(*cp) && *cp != '\0')
            ++cp;

         if ((i = PTR2SIZE(cp - m_ext)) == 0)
            break;
         else if (elen != i || ascncasecmp(n_ext, m_ext, elen))
            continue;

         /* Found it */
#ifdef HAVE_REGEX
jfound:
#endif
         i = xnp->mt_mtlen;
         if ((xnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            name = "";
            elen = 0;
         } else {
            name = _mt_typnames[xnp->mt_flags & __MT_TMASK];
            elen = strlen(name);
         }
         content = salloc(i + elen +1);
         if (elen)
            memcpy(content, name, elen);
         memcpy(content + elen, line, i);
         content[elen += i] = '\0';
         goto jleave;
      }
   }
jleave:
   NYD_LEAVE;
   return content;
}

FL char *
mimepart_get_handler(struct mimepart const *mpp)
{
#define __S    "pipe-"
#define __L    (sizeof(__S) -1)
   char const *es, *cs;
   size_t el, cl, l;
   char *buf, *rv;
   NYD_ENTER;

   el = ((es = mpp->m_filename) != NULL && (es = strrchr(es, '.')) != NULL &&
         *++es != '\0') ? strlen(es) : 0;
   cl = ((cs = mpp->m_ct_type_usr_ovwr) != NULL ||
         (cs = mpp->m_ct_type_plain) != NULL) ? strlen(cs) : 0;
   if ((l = MAX(el, cl)) == 0) {

/* FIXME here and below : another mime-counter-evidence bit, content check */

      rv = NULL;
      goto jleave;
   }

   buf = ac_alloc(__L + l +1);
   memcpy(buf, __S, __L);

   /* File-extension handlers take precedence.
    * Yes, we really "fail" here for file extensions which clash MIME types */
   if (el > 0) {
      memcpy(buf + __L, es, el +1);
      for (rv = buf + __L; *rv != '\0'; ++rv)
         *rv = lowerconv(*rv);

      if ((rv = vok_vlook(buf)) != NULL)
         goto jok;
   }

   /* Then MIME Content-Type: */
   if (cl > 0) {
      memcpy(buf + __L, cs, cl +1);
      for (rv = buf + __L; *rv != '\0'; ++rv)
         *rv = lowerconv(*rv);

      if ((rv = vok_vlook(buf)) != NULL)
         goto jok;
   }

   rv = NULL;
jok:
   ac_free(buf);
jleave:
   NYD_LEAVE;
   return rv;
#undef __L
#undef __S
}

FL int
c_mimetypes(void *v)
{
   char **argv = v;
   struct mtnode *mtnp;
   NYD_ENTER;

   if (*argv == NULL)
      goto jlist;
   if (argv[1] != NULL)
      goto jerr;
   if (!asccasecmp(*argv, "show"))
      goto jlist;
   if (!asccasecmp(*argv, "clear"))
      goto jclear;
jerr:
   fprintf(stderr, "Synopsis: mimetypes: %s\n",
      _("Either <show> (default) or <clear> the mime.types cache"));
   v = NULL;
jleave:
   NYD_LEAVE;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */

jlist:   {
   FILE *fp;
   size_t l;

   if (_mt_list == NULL)
      _mt_init();
   if (NELEM(_mt_bltin) == 0 && _mt_list == (struct mtnode*)-1) {
      fprintf(stderr, _("*mimetypes-load-control*: interpolate what file?\n"));
      v = NULL;
      goto jleave;
   }

   if ((fp = Ftmp(NULL, "mimelist", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
         == NULL) {
      perror("tmpfile");
      v = NULL;
      goto jleave;
   }

   for (l = 0, mtnp = _mt_list; mtnp != NULL; ++l, mtnp = mtnp->mt_next) {
      char const *typ, *xtyp, *val;

      typ = ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER)
            ? "" : _mt_typnames[mtnp->mt_flags & __MT_TMASK];
      xtyp = (mtnp->mt_flags & _MT_LOADED)
            ? mtnp->mt._loaded.mtl_line : mtnp->mt._bltin->mtb_line;
      val = xtyp + mtnp->mt_mtlen;

      fprintf(fp, "%c%c %s%.*s <%s>\n",
         (mtnp->mt_flags & _MT_REGEX ? '*' : ' '),
         (mtnp->mt_flags & _MT_USR ? 'U'
            : (mtnp->mt_flags & _MT_SYS ? 'S'
            : (mtnp->mt_flags & _MT_LOADED ? 'F' : '@'))),
         typ, (int)mtnp->mt_mtlen, xtyp, val);
   }

   page_or_print(fp, l);
   Fclose(fp);
   }
   goto jleave;

jclear:
   if (NELEM(_mt_bltin) == 0 && _mt_list == (struct mtnode*)-1)
      _mt_list = NULL;
   while ((mtnp = _mt_list) != NULL) {
      _mt_list = mtnp->mt_next;
#ifdef HAVE_REGEX
      if ((mtnp->mt_flags & _MT_REGEX) && mtnp->mt_regex != NULL) {
         regfree(mtnp->mt_regex);
         free(mtnp->mt_regex);
      }
#endif
      free(mtnp);
   }
   goto jleave;
}

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
    * TODO line that is) and it should be objective to the backend wether
    * TODO it'll be folded to fit onto the display or not, e.g., for search
    * TODO purposes etc.  then the only condition we have to honour in here
    * TODO is that whitespace in between multiple adjacent MIME encoded words
    * TODO á la RFC 2047 is discarded; i.e.: this function should deal with
    * TODO RFC 2047 and be renamed: mime_fromhdr() -> mime_rfc2047_decode() */
   struct str cin, cout;
   char *p, *op, *upper, *cs, *cbeg;
   ui32_t convert, lastenc, lastoutl;
#ifdef HAVE_ICONV
   char const *tcs;
   iconv_t fhicd = (iconv_t)-1;
#endif
   NYD_ENTER;

   out->l = 0;
   if (in->l == 0) {
      *(out->s = smalloc(1)) = '\0';
      goto jleave;
   }
   out->s = NULL;

#ifdef HAVE_ICONV
   tcs = charset_get_lc();
#endif
   p = in->s;
   upper = p + in->l;
   lastenc = lastoutl = 0;

   while (p < upper) {
      op = p;
      if (*p == '=' && *(p + 1) == '?') {
         p += 2;
         cbeg = p;
         while (p < upper && *p != '?')
            ++p;  /* strip charset */
         if (p >= upper)
            goto jnotmime;
         cs = salloc(PTR2SIZE(++p - cbeg));
         memcpy(cs, cbeg, PTR2SIZE(p - cbeg - 1));
         cs[p - cbeg - 1] = '\0';
#ifdef HAVE_ICONV
         if (fhicd != (iconv_t)-1)
            n_iconv_close(fhicd);
         fhicd = asccasecmp(cs, tcs) ? n_iconv_open(tcs, cs) : (iconv_t)-1;
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
            /* XXX Take care for, and strip LF from
             * XXX [Invalid Base64 encoding ignored] */
            if (b64_decode(&cout, &cin, NULL) == STOP &&
                  cout.s[cout.l - 1] == '\n')
               --cout.l;
         } else
            qp_decode(&cout, &cin, NULL);

         out->l = lastenc;
#ifdef HAVE_ICONV
         if ((flags & TD_ICONV) && fhicd != (iconv_t)-1) {
            cin.s = NULL, cin.l = 0; /* XXX string pool ! */
            convert = n_iconv_str(fhicd, &cin, &cout, NULL, TRU1);
            out = n_str_add(out, &cin);
            if (convert) /* EINVAL at EOS */
               out = n_str_add_buf(out, "?", 1);
            free(cin.s);
         } else
#endif
            out = n_str_add(out, &cout);
         lastenc = lastoutl = out->l;
         free(cout.s);
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
      free(out->s);
      *out = cout;
   }
   if (flags & TD_DELCTRL)
      out->l = delctrl(out->s, out->l);
#ifdef HAVE_ICONV
   if (fhicd != (iconv_t)-1)
      n_iconv_close(fhicd);
#endif
jleave:
   NYD_LEAVE;
   return;
}

FL char *
mime_fromaddr(char const *name)
{
   char const *cp, *lastcp;
   char *res = NULL;
   size_t ressz = 1, rescur = 0;
   NYD_ENTER;

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
   /* TODO rescur==0: inserted to silence Coverity ...; check that */
   if (rescur == 0)
      res = UNCONST("");
   else
      res[rescur] = '\0';
   {  char *x = res;
      res = savestr(res);
      free(x);
   }
jleave:
   NYD_LEAVE;
   return res;
}

FL ssize_t
xmime_write(char const *ptr, size_t size, FILE *f, enum conversion convert,
   enum tdflags dflags)
{
   ssize_t rv;
   struct quoteflt *qf;
   NYD_ENTER;

   quoteflt_reset(qf = quoteflt_dummy(), f);
   rv = mime_write(ptr, size, f, convert, dflags, qf, NULL);
   quoteflt_flush(qf);
   NYD_LEAVE;
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
   struct quoteflt *qf, struct str * volatile rest)
{
   /* TODO note: after send/MIME layer rewrite we will have a string pool
    * TODO so that memory allocation count drops down massively; for now,
    * TODO v14.0 that is, we pay a lot & heavily depend on the allocator */
   struct str in, out;
   ssize_t sz;
   int state;
   NYD_ENTER;

   in.s = UNCONST(ptr);
   in.l = size;
   out.s = NULL;
   out.l = 0;

   dflags |= _TD_BUFCOPY;
   if ((sz = size) == 0) {
      if (rest != NULL && rest->l != 0)
         goto jconvert;
      goto jleave;
   }

#ifdef HAVE_ICONV
   if ((dflags & TD_ICONV) && iconvd != (iconv_t)-1 &&
         (convert == CONV_TOQP || convert == CONV_8BIT ||
         convert == CONV_TOB64 || convert == CONV_TOHDR)) {
      if (n_iconv_str(iconvd, &out, &in, NULL, FAL0) != 0) {
         /* XXX report conversion error? */;
         sz = -1;
         goto jleave;
      }
      in = out;
      out.s = NULL;
      dflags &= ~_TD_BUFCOPY;
   }
#endif

jconvert:
   __mimemw_sig = 0;
   __mimemw_opipe = safe_signal(SIGPIPE, &__mimemw_onsig);
   if (sigsetjmp(__mimemw_actjmp, 1))
      goto jleave;

   switch (convert) {
   case CONV_FROMQP:
      state = qp_decode(&out, &in, rest);
      goto jqpb64_dec;
   case CONV_TOQP:
      qp_encode(&out, &in, QP_NONE);
      goto jqpb64_enc;
   case CONV_8BIT:
      sz = quoteflt_push(qf, in.s, in.l);
      break;
   case CONV_FROMB64:
      rest = NULL;
      /* FALLTHRU */
   case CONV_FROMB64_T:
      state = b64_decode(&out, &in, rest);
jqpb64_dec:
      if ((sz = out.l) != 0) {
         ui32_t opl = qf->qf_pfix_len;
         if (state != OKAY)
            qf->qf_pfix_len = 0;
         sz = _fwrite_td(&out, (dflags & ~_TD_BUFCOPY), rest, qf);
         qf->qf_pfix_len = opl;
      }
      if (state != OKAY)
         sz = -1;
      break;
   case CONV_TOB64:
      b64_encode(&out, &in, B64_LF | B64_MULTILINE);
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
      sz = mime_write_tohdr(&in, f);
      break;
   case CONV_TOHDR_A:
      sz = mime_write_tohdr_a(&in, f);
      break;
   default:
      sz = _fwrite_td(&in, dflags, NULL, qf);
      break;
   }
jleave:
   if (out.s != NULL)
      free(out.s);
   if (in.s != ptr)
      free(in.s);
   safe_signal(SIGPIPE, __mimemw_opipe);
   if (__mimemw_sig != 0)
      kill(0, __mimemw_sig);
   NYD_LEAVE;
   return sz;
}

/* s-it-mode */
