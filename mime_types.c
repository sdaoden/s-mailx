/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `(un)?mimetype' and other mime.types(5) related facilities.
 *@ "Keep in sync with" ./mime.types.
 *
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE mime_types

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

enum mime_type {
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

   _MT_LOADED  = 1<< 8,       /* Not struct mtbltin */
   _MT_USR     = 1<< 9,       /* MIME_TYPES_USR */
   _MT_SYS     = 1<<10,       /* MIME_TYPES_SYS */

   _MT_PLAIN   = 1<<16,       /* Without pipe handler display as text */
   _MT_SOUP_h  = 2<<16,       /* Ditto, but HTML tagsoup parser if possible */
   _MT_SOUP_H  = 3<<16,       /* HTML tagsoup parser, else NOT plain text */
   __MT_MARKMASK = _MT_SOUP_H
};

enum mime_type_class {
   _MT_C_CLEAN    = 0,        /* Plain RFC 5322 message */
   _MT_C_NCTT     = 1<<0,     /* *contenttype == NULL */
   _MT_C_ISTXT    = 1<<1,     /* *contenttype =~ text\/ */
   _MT_C_ISTXTCOK = 1<<2,     /* _ISTXT + *mime-allow-text-controls* */
   _MT_C_HIGHBIT  = 1<<3,     /* Not 7bit clean */
   _MT_C_LONGLINES = 1<<4,    /* MIME_LINELEN_LIMIT exceed. */
   _MT_C_CTRLCHAR = 1<<5,     /* Control characters seen */
   _MT_C_HASNUL   = 1<<6,     /* Contains \0 characters */
   _MT_C_NOTERMNL = 1<<7,     /* Lacks a final newline */
   _MT_C_FROM_    = 1<<8,     /* ^From_ seen */
   _MT_C_SUGGEST_DONE = 1<<16 /* Inspector suggests to stop further parse */
};

struct mtbltin {
   ui32_t         mtb_flags;
   ui32_t         mtb_mtlen;
   char const     *mtb_line;
};

struct mtnode {
   struct mtnode  *mt_next;
   ui32_t         mt_flags;
   ui32_t         mt_mtlen;   /* Length of MIME type string, rest thereafter */
   /* C99 forbids flexible arrays in union, so unfortunately we waste a pointer
    * that could already store character data here */
   char const     *mt_line;
};

struct mtlookup {
   char const           *mtl_name;
   size_t               mtl_nlen;
   struct mtnode const  *mtl_node;
   char                 *mtl_result;   /* If requested, salloc()ed MIME type */
};

struct mt_class_arg {
   char const  *mtca_buf;
   size_t      mtca_len;
   ssize_t     mtca_curlen;
   char        mtca_lastc;
   char        mtca_c;
   enum mime_type_class mtca_mtc;
};

static struct mtbltin const   _mt_bltin[] = {
#include "mime_types.h"
};

static char const             _mt_typnames[][16] = {
   "application/", "audio/", "image/",
   "message/", "multipart/", "text/",
   "video/"
};
n_CTAV(_MT_APPLICATION == 0 && _MT_AUDIO == 1 && _MT_IMAGE == 2 &&
   _MT_MESSAGE == 3 && _MT_MULTIPART == 4 && _MT_TEXT == 5 &&
   _MT_VIDEO == 6);

/* */
static bool_t           _mt_is_init;
static struct mtnode    *_mt_list;

/* Initialize MIME type list in order */
static void             _mt_init(void);
static bool_t           __mt_load_file(ui32_t orflags,
                           char const *file, char **line, size_t *linesize);

/* Create (prepend) a new MIME type; cmdcalled results in a bit more verbosity
 * for `mimetype' */
static struct mtnode *  _mt_create(bool_t cmdcalled, ui32_t orflags,
                           char const *line, size_t len);

/* Try to find MIME type by X (after zeroing mtlp), return NULL if not found;
 * if with_result >mtl_result will be created upon success for the former */
static struct mtlookup * _mt_by_filename(struct mtlookup *mtlp,
                           char const *name, bool_t with_result);
static struct mtlookup * _mt_by_mtname(struct mtlookup *mtlp,
                           char const *mtname);

/* In-depth inspection of raw content: call _round() repeatedly, last time with
 * a 0 length buffer, finally check .mtca_mtc for result.
 * No further call is needed if _round() return includes _MT_C_SUGGEST_DONE,
 * as the resulting classification is unambiguous */
SINLINE struct mt_class_arg * _mt_classify_init(struct mt_class_arg *mtcap,
                                 enum mime_type_class initval);
static enum mime_type_class   _mt_classify_round(struct mt_class_arg *mtcap);

/* We need an in-depth inspection of an application/octet-stream part */
static enum mimecontent _mt_classify_os_part(ui32_t mce, struct mimepart *mpp);

/* Check whether a *pipe-XY* handler is applicable, and adjust flags according
 * to the defined trigger characters; upon entry MIME_HDL_NULL is set, and that
 * isn't changed if mhp doesn't apply */
static enum mime_handler_flags _mt_pipe_check(struct mime_handler *mhp);

static void
_mt_init(void)
{
   struct mtnode *tail;
   char c, *line; /* TODO line pool (below) */
   size_t linesize;
   ui32_t i, j;
   char const *srcs_arr[10], *ccp, **srcs;
   NYD_ENTER;

   /*if (_mt_is_init)
    *  goto jleave;*/

   /* Always load our builtins */
   for (tail = NULL, i = 0; i < n_NELEM(_mt_bltin); ++i) {
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
      mtnp->mt_line = mtbp->mtb_line;
   }

   /* Decide which files sources have to be loaded */
   if ((ccp = ok_vlook(mimetypes_load_control)) == NULL)
      ccp = "US";
   else if (*ccp == '\0')
      goto jleave;

   srcs = srcs_arr + 2;
   srcs[-1] = srcs[-2] = NULL;

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
               if (PTR2SIZE(srcs - srcs_arr) < n_NELEM(srcs_arr))
                  *srcs++ = ccp;
               else
                  n_err(_("*mimetypes-load-control*: too many sources, "
                        "skipping %s\n"), n_shexp_quote_cp(ccp, FAL0));
               continue;
            }
            /* FALLTHRU */
         default:
            goto jecontent;
         }
      }
   } else for (i = 0; (c = ccp[i]) != '\0'; ++i)
      switch (c) {
      case 'S': case 's': srcs_arr[1] = MIME_TYPES_SYS; break;
      case 'U': case 'u': srcs_arr[0] = MIME_TYPES_USR; break;
      default:
jecontent:
         n_err(_("*mimetypes-load-control*: unsupported content: %s\n"), ccp);
         goto jleave;
      }

   /* Load all file-based sources in the desired order */
   line = NULL;
   linesize = 0;
   for (j = 0, i = (ui32_t)PTR2SIZE(srcs - srcs_arr), srcs = srcs_arr;
         i > 0; ++j, ++srcs, --i)
      if (*srcs == NULL)
         continue;
      else if (!__mt_load_file((j == 0 ? _MT_USR : (j == 1 ? _MT_SYS : 0)),
            *srcs, &line, &linesize)) {
         if ((options & OPT_D_V) || j > 1)
            n_err(_("*mimetypes-load-control*: can't open or load %s\n"),
               n_shexp_quote_cp(*srcs, FAL0));
      }
   if (line != NULL)
      free(line);
jleave:
   _mt_is_init = TRU1;
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
      if ((mtnp = _mt_create(FAL0, orflags, *line, len)) != NULL) {
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
_mt_create(bool_t cmdcalled, ui32_t orflags, char const *line, size_t len)
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

   /* (But wait - is there a type marker?) */
   if (!(orflags & (_MT_USR | _MT_SYS)) && *typ == '@') {
      if (len < 2)
         goto jeinval;
      if (typ[1] == ' ') {
         orflags |= _MT_PLAIN;
         typ += 2;
         len -= 2;
         line += 2;
      } else if (len > 4 && typ[2] == '@' && typ[3] == ' ') {
         switch (typ[1]) {
         case 't':   orflags |= _MT_PLAIN;   goto jexttypmar;
         case 'h':   orflags |= _MT_SOUP_h;  goto jexttypmar;
         case 'H':   orflags |= _MT_SOUP_H;
jexttypmar:
            typ += 4;
            len -= 4;
            line += 4;
            break;
         default:
            goto jeinval;
         }
      } else
         goto jeinval;
   }

   while (len > 0 && !blankchar(*line))
      ++line, --len;
   /* Ignore empty lines and even incomplete specifications (only MIME type)
    * because this is quite common in mime.types(5) files */
   if (len == 0 || (tlen = PTR2SIZE(line - typ)) == 0) {
      if (cmdcalled)
         n_err(_("Empty MIME type or no extensions given: %s\n"),
            (len == 0 ? _("(no value)") : line));
      goto jleave;
   }

   if ((subtyp = memchr(typ, '/', tlen)) == NULL) {
jeinval:
      if (cmdcalled || (options & OPT_D_V))
         n_err(_("%s MIME type: %s\n"),
            (cmdcalled ? _("Invalid") : _("mime.types(5): invalid")), typ);
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

   /* Strip leading whitespace from the list of extensions;
    * trailing WS has already been trimmed away above.
    * Be silent on slots which define a mimetype without any value */
   while (len > 0 && blankchar(*line))
      ++line, --len;
   if (len == 0)
      goto jleave;

   /*  */
   mtnp = smalloc(sizeof(*mtnp) + tlen + len +1);
   mtnp->mt_next = NULL;
   mtnp->mt_flags = (orflags |= _MT_LOADED);
   mtnp->mt_mtlen = (ui32_t)tlen;
   {  char *l = (char*)(mtnp + 1);
      mtnp->mt_line = l;
      memcpy(l, typ, tlen);
      memcpy(l + tlen, line, len);
      tlen += len;
      l[tlen] = '\0';
   }

jleave:
   NYD_LEAVE;
   return mtnp;
}

static struct mtlookup *
_mt_by_filename(struct mtlookup *mtlp, char const *name, bool_t with_result)
{
   struct mtnode *mtnp;
   size_t nlen, i, j;
   char const *ext, *cp;
   NYD2_ENTER;

   memset(mtlp, 0, sizeof *mtlp);

   if ((nlen = strlen(name)) == 0) /* TODO name should be a URI */
      goto jnull_leave;
   /* We need a period TODO we should support names like README etc. */
   for (i = nlen; name[--i] != '.';)
      if (i == 0 || name[i] == '/') /* XXX no magics */
         goto jnull_leave;
   /* While here, basename() it */
   while (i > 0 && name[i - 1] != '/')
      --i;
   name += i;
   nlen -= i;
   mtlp->mtl_name = name;
   mtlp->mtl_nlen = nlen;

   if (!_mt_is_init)
      _mt_init();

   /* ..all the MIME types */
   for (mtnp = _mt_list; mtnp != NULL; mtnp = mtnp->mt_next)
      for (ext = mtnp->mt_line + mtnp->mt_mtlen;; ext = cp) {
         cp = ext;
         while (whitechar(*cp))
            ++cp;
         ext = cp;
         while (!whitechar(*cp) && *cp != '\0')
            ++cp;

         if ((i = PTR2SIZE(cp - ext)) == 0)
            break;
         /* Don't allow neither of ".txt" or "txt" to match "txt" */
         else if (i + 1 >= nlen || name[(j = nlen - i) - 1] != '.' ||
               ascncasecmp(name + j, ext, i))
            continue;

         /* Found it */
         mtlp->mtl_node = mtnp;

         if (!with_result)
            goto jleave;

         if ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            name = n_empty;
            j = 0;
         } else {
            name = _mt_typnames[mtnp->mt_flags & __MT_TMASK];
            j = strlen(name);
         }
         i = mtnp->mt_mtlen;
         mtlp->mtl_result = salloc(i + j +1);
         if (j > 0)
            memcpy(mtlp->mtl_result, name, j);
         memcpy(mtlp->mtl_result + j, mtnp->mt_line, i);
         mtlp->mtl_result[j += i] = '\0';
         goto jleave;
      }
jnull_leave:
   mtlp = NULL;
jleave:
   NYD2_LEAVE;
   return mtlp;
}

static struct mtlookup *
_mt_by_mtname(struct mtlookup *mtlp, char const *mtname)
{
   struct mtnode *mtnp;
   size_t nlen, i, j;
   char const *cp;
   NYD2_ENTER;

   memset(mtlp, 0, sizeof *mtlp);

   if ((mtlp->mtl_nlen = nlen = strlen(mtlp->mtl_name = mtname)) == 0)
      goto jnull_leave;

   if (!_mt_is_init)
      _mt_init();

   /* ..all the MIME types */
   for (mtnp = _mt_list; mtnp != NULL; mtnp = mtnp->mt_next) {
         if ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            cp = n_empty;
            j = 0;
         } else {
            cp = _mt_typnames[mtnp->mt_flags & __MT_TMASK];
            j = strlen(cp);
         }
         i = mtnp->mt_mtlen;

         if (i + j == mtlp->mtl_nlen) {
            char *xmt = ac_alloc(i + j +1);
            if (j > 0)
               memcpy(xmt, cp, j);
            memcpy(xmt + j, mtnp->mt_line, i);
            xmt[j += i] = '\0';
            i = asccasecmp(mtname, xmt);
            ac_free(xmt);

            if (!i) {
               /* Found it */
               mtlp->mtl_node = mtnp;
               goto jleave;
            }
         }
      }
jnull_leave:
   mtlp = NULL;
jleave:
   NYD2_LEAVE;
   return mtlp;
}

SINLINE struct mt_class_arg *
_mt_classify_init(struct mt_class_arg * mtcap, enum mime_type_class initval)
{
   NYD2_ENTER;
   memset(mtcap, 0, sizeof *mtcap);
   mtcap->mtca_lastc = mtcap->mtca_c = EOF;
   mtcap->mtca_mtc = initval;
   NYD2_LEAVE;
   return mtcap;
}

static enum mime_type_class
_mt_classify_round(struct mt_class_arg *mtcap)
{
   /* TODO BTW., after the MIME/send layer rewrite we could use a MIME
    * TODO boundary of "=-=-=" if we would add a B_ in EQ spirit to F_,
    * TODO and report that state to the outer world */
#define F_        "From "
#define F_SIZEOF  (sizeof(F_) -1)
   char f_buf[F_SIZEOF], *f_p = f_buf;
   char const *buf;
   size_t blen;
   ssize_t curlen;
   int c, lastc;
   enum mime_type_class mtc;
   NYD2_ENTER;

   buf = mtcap->mtca_buf;
   blen = mtcap->mtca_len;
   curlen = mtcap->mtca_curlen;
   c = mtcap->mtca_c;
   lastc = mtcap->mtca_lastc;
   mtc = mtcap->mtca_mtc;

   for (;; ++curlen) {
      lastc = c;
      if (blen == 0) {
         /* Real EOF, or only current buffer end? */
         if (mtcap->mtca_len == 0)
            c = EOF;
         else
            break;
      } else
         c = (uc_i)*buf++;
      --blen;

      if (c == '\0') {
         mtc |= _MT_C_HASNUL;
         if (!(mtc & _MT_C_ISTXTCOK)) {
            mtc |= _MT_C_SUGGEST_DONE;
            break;
         }
         continue;
      }
      if (c == '\n' || c == EOF) {
         if (curlen >= MIME_LINELEN_LIMIT)
            mtc |= _MT_C_LONGLINES;
         if (c == EOF) {
            break;
         }
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
         mtc |= _MT_C_CTRLCHAR;

      /* Control character? XXX this is all ASCII here */
      if (c < 0x20 || c == 0x7F) {
         /* RFC 2045, 6.7, as above ... */
         if (c != '\t' && c != '\r')
            mtc |= _MT_C_CTRLCHAR;
         /* If there is a escape sequence in backslash notation defined for
          * this in ANSI X3.159-1989 (ANSI C89), don't treat it as a control
          * for real.  I.e., \a=\x07=BEL, \b=\x08=BS, \t=\x09=HT.  Don't follow
          * libmagic(1) in respect to \v=\x0B=VT.  \f=\x0C=NP; do ignore
          * \e=\x1B=ESC */
         if ((c >= '\x07' && c <= '\x0D') || c == '\x1B')
            continue;
         mtc |= _MT_C_HASNUL; /* Force base64 */
         if (!(mtc & _MT_C_ISTXTCOK)) {
            mtc |= _MT_C_SUGGEST_DONE;
            break;
         }
      } else if ((ui8_t)c & 0x80) {
         mtc |= _MT_C_HIGHBIT;
         /* TODO count chars with HIGHBIT? libmagic?
          * TODO try encode part - base64 if bails? */
         if (!(mtc & (_MT_C_NCTT | _MT_C_ISTXT))) { /* TODO _NCTT?? */
            mtc |= _MT_C_HASNUL /* Force base64 */ | _MT_C_SUGGEST_DONE;
            break;
         }
      } else if (!(mtc & _MT_C_FROM_) && UICMP(z, curlen, <, F_SIZEOF)) {
         *f_p++ = (char)c;
         if (UICMP(z, curlen, ==, F_SIZEOF - 1) &&
               PTR2SIZE(f_p - f_buf) == F_SIZEOF &&
               !memcmp(f_buf, F_, F_SIZEOF))
            mtc |= _MT_C_FROM_;
      }
   }
   if (c == EOF && lastc != '\n')
      mtc |= _MT_C_NOTERMNL;

   mtcap->mtca_curlen = curlen;
   mtcap->mtca_lastc = lastc;
   mtcap->mtca_c = c;
   mtcap->mtca_mtc = mtc;
   NYD2_LEAVE;
   return mtc;
#undef F_
#undef F_SIZEOF
}

static enum mimecontent
_mt_classify_os_part(ui32_t mce, struct mimepart *mpp)
{
   struct str in = {NULL, 0}, outrest, inrest, dec;
   struct mt_class_arg mtca;
   bool_t did_inrest;
   enum mime_type_class mtc;
   int lc, c;
   size_t cnt, lsz;
   FILE *ibuf;
   off_t start_off;
   enum mimecontent mc;
   NYD2_ENTER;

   assert(mpp->m_mime_enc != MIMEE_BIN);

   outrest = inrest = dec = in;
   mc = MIME_UNKNOWN;
   n_UNINIT(mtc, 0);

   /* TODO v15-compat Note we actually bypass our usual file handling by
    * TODO directly using fseek() on mb.mb_itf -- the v15 rewrite will change
    * TODO all of this, and until then doing it like this is the only option
    * TODO to integrate nicely into whoever calls us */
   start_off = ftell(mb.mb_itf);
   if ((ibuf = setinput(&mb, (struct message*)mpp, NEED_BODY)) == NULL) {
jos_leave:
      fseek(mb.mb_itf, start_off, SEEK_SET);
      goto jleave;
   }
   cnt = mpp->m_size;

   /* Skip part headers */
   for (lc = '\0'; cnt > 0; lc = c, --cnt)
      if ((c = getc(ibuf)) == EOF || (c == '\n' && lc == '\n'))
         break;
   if (cnt == 0 || ferror(ibuf))
      goto jos_leave;

   /* So now let's inspect the part content, decoding content-transfer-encoding
    * along the way TODO this should simply be "mime_factory_create(MPP)"! */
   _mt_classify_init(&mtca, _MT_C_ISTXT);

   for (lsz = 0;;) {
      bool_t dobuf;

      c = (--cnt == 0) ? EOF : getc(ibuf);
      if ((dobuf = (c == '\n'))) {
         /* Ignore empty lines */
         if (lsz == 0)
            continue;
      } else if ((dobuf = (c == EOF))) {
         if (lsz == 0 && outrest.l == 0)
            break;
      }

      if (in.l + 1 >= lsz)
         in.s = srealloc(in.s, lsz += LINESIZE);
      if (c != EOF)
         in.s[in.l++] = (char)c;
      if (!dobuf)
         continue;

jdobuf:
      switch (mpp->m_mime_enc) {
      case MIMEE_B64:
         if (!b64_decode_text(&dec, &in, &outrest,
               (did_inrest ? NULL : &inrest))) {
            mtca.mtca_mtc = _MT_C_HASNUL;
            goto jstopit; /* break;break; */
         }
         break;
      case MIMEE_QP:
         /* Drin */
         if (!qp_decode_text(&dec, &in, &outrest, &inrest)) {
            mtca.mtca_mtc = _MT_C_HASNUL;
            goto jstopit; /* break;break; */
         }
         if (dec.l == 0 && c != EOF) {
            in.l = 0;
            continue;
         }
         break;
      default:
         /* Temporarily switch those two buffers.. */
         dec = in;
         in.s = NULL;
         in.l = 0;
         break;
      }

      mtca.mtca_buf = dec.s;
      mtca.mtca_len = (ssize_t)dec.l;
      if ((mtc = _mt_classify_round(&mtca)) & _MT_C_SUGGEST_DONE) {
         mtc = _MT_C_HASNUL;
         break;
      }

      if (c == EOF)
         break;
      /* ..and restore switched */
      if (in.s == NULL) {
         in = dec;
         dec.s = NULL;
      }
      in.l = dec.l = 0;
   }

   if ((in.l = inrest.l) > 0) {
      in.s = inrest.s;
      did_inrest = TRU1;
      goto jdobuf;
   }
   if (outrest.l > 0)
      goto jdobuf;
jstopit:
   if (in.s != NULL)
      free(in.s);
   if (dec.s != NULL)
      free(dec.s);
   if (outrest.s != NULL)
      free(outrest.s);
   if (inrest.s != NULL)
      free(inrest.s);

   fseek(mb.mb_itf, start_off, SEEK_SET);

   if (!(mtc & (_MT_C_HASNUL | _MT_C_CTRLCHAR))) {
      mc = MIME_TEXT_PLAIN;
      if (mce & MIMECE_ALL_OVWR)
         mpp->m_ct_type_plain = "text/plain";
      if (mce & (MIMECE_BIN_OVWR | MIMECE_ALL_OVWR))
         mpp->m_ct_type_usr_ovwr = "text/plain";
   }
jleave:
   NYD2_LEAVE;
   return mc;
}

static enum mime_handler_flags
_mt_pipe_check(struct mime_handler *mhp)
{
   enum mime_handler_flags rv_orig, rv;
   char const *cp;
   NYD2_ENTER;

   rv_orig = rv = mhp->mh_flags;

   /* Do we have any handler for this part? */
   if (*(cp = mhp->mh_shell_cmd) == '\0')
      goto jleave;
   else if (*cp++ != '@') {
      rv |= MIME_HDL_CMD;
      goto jleave;
   } else if (*cp == '\0') {
      rv |= MIME_HDL_TEXT;
      goto jleave;
   }

jnextc:
   switch (*cp) {
   case '*':   rv |= MIME_HDL_ALWAYS;     ++cp; goto jnextc;
   case '#':   rv |= MIME_HDL_NOQUOTE;    ++cp; goto jnextc;
   case '&':   rv |= MIME_HDL_ASYNC;      ++cp; goto jnextc;
   case '!':   rv |= MIME_HDL_NEEDSTERM;  ++cp; goto jnextc;
   case '+':
      if (rv & MIME_HDL_TMPF)
         rv |= MIME_HDL_TMPF_UNLINK;
      rv |= MIME_HDL_TMPF;
      ++cp;
      goto jnextc;
   case '=':
      rv |= MIME_HDL_TMPF_FILL;
      ++cp;
      goto jnextc;
   case '@':
      ++cp;
      /* FALLTHRU */
   default:
      break;
   }
   mhp->mh_shell_cmd = cp;

   /* Implications */
   if (rv & MIME_HDL_TMPF_FILL)
      rv |= MIME_HDL_TMPF;

   /* Exceptions */
   if (rv & MIME_HDL_ISQUOTE) {
      if (rv & MIME_HDL_NOQUOTE)
         goto jerr;

      /* Cannot fetch data back from asynchronous process */
      if (rv & MIME_HDL_ASYNC)
         goto jerr;

      /* TODO Can't use a "needsterminal" program for quoting */
      if (rv & MIME_HDL_NEEDSTERM)
         goto jerr;
   }

   if (rv & MIME_HDL_NEEDSTERM) {
      if (rv & MIME_HDL_ASYNC) {
         n_err(_("MIME type handlers: can't use needsterminal and "
            "x-nail-async together\n"));
         goto jerr;
      }

      /* needsterminal needs a terminal */
      if (!(options & OPT_INTERACTIVE))
         goto jerr;
   }

   if (!(rv & MIME_HDL_ALWAYS) && !(pstate & PS_MSGLIST_DIRECT)) {
      /* Viewing multiple messages in one go, don't block system */
      mhp->mh_msg.l = strlen(mhp->mh_msg.s = n_UNCONST(
            _("[-- Directly address message only for display --]\n")));
      rv |= MIME_HDL_MSG;
      goto jleave;
   }

   rv |= MIME_HDL_CMD;
jleave:
   mhp->mh_flags = rv;
   NYD2_LEAVE;
   return rv;
jerr:
   rv = rv_orig;
   goto jleave;
}

FL int
c_mimetype(void *v)
{
   char **argv = v;
   struct mtnode *mtnp;
   NYD_ENTER;

   if (!_mt_is_init)
      _mt_init();

   if (*argv == NULL) {
      FILE *fp;
      size_t l;

      if (_mt_list == NULL) {
         printf(_("*mimetypes-load-control*: no mime.types(5) available\n"));
         goto jleave;
      }

      if ((fp = Ftmp(NULL, "mimelist", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
            NULL) {
         n_perr(_("tmpfile"), 0);
         v = NULL;
         goto jleave;
      }

      for (l = 0, mtnp = _mt_list; mtnp != NULL; ++l, mtnp = mtnp->mt_next) {
         char const *tmark, *typ;

         switch (mtnp->mt_flags & __MT_MARKMASK) {
         case _MT_PLAIN:   tmark = "/t"; break;
         case _MT_SOUP_h:  tmark = "/h"; break;
         case _MT_SOUP_H:  tmark = "/H"; break;
         default:          tmark = "  "; break;
         }
         typ = ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER)
               ? n_empty : _mt_typnames[mtnp->mt_flags & __MT_TMASK];

         fprintf(fp, "%c%s %s%.*s  %s\n",
            (mtnp->mt_flags & _MT_USR ? 'U'
               : (mtnp->mt_flags & _MT_SYS ? 'S'
               : (mtnp->mt_flags & _MT_LOADED ? 'F' : 'B'))),
            tmark, typ, (int)mtnp->mt_mtlen, mtnp->mt_line,
            mtnp->mt_line + mtnp->mt_mtlen);
      }

      page_or_print(fp, l);
      Fclose(fp);
   } else {
      for (; *argv != NULL; ++argv) {
         mtnp = _mt_create(TRU1, _MT_LOADED, *argv, strlen(*argv));
         if (mtnp != NULL) {
            mtnp->mt_next = _mt_list;
            _mt_list = mtnp;
         } else
            v = NULL;
      }
   }
jleave:
   NYD_LEAVE;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL int
c_unmimetype(void *v)
{
   char **argv = v;
   struct mtnode *lnp, *mtnp;
   bool_t match;
   NYD_ENTER;

   /* Need to load that first as necessary */
   if (!_mt_is_init)
      _mt_init();

   for (; *argv != NULL; ++argv) {
      if (!asccasecmp(*argv, "reset")) {
         _mt_is_init = FAL0;
         goto jdelall;
      }

      if (argv[0][0] == '*' && argv[0][1] == '\0') {
jdelall:
         while ((mtnp = _mt_list) != NULL) {
            _mt_list = mtnp->mt_next;
            free(mtnp);
         }
         continue;
      }

      for (match = FAL0, lnp = NULL, mtnp = _mt_list; mtnp != NULL;) {
         char const *typ;
         char *val;
         size_t i;

         if ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            typ = n_empty;
            i = 0;
         } else {
            typ = _mt_typnames[mtnp->mt_flags & __MT_TMASK];
            i = strlen(typ);
         }

         val = ac_alloc(i + mtnp->mt_mtlen +1);
         memcpy(val, typ, i);
         memcpy(val + i, mtnp->mt_line, mtnp->mt_mtlen);
         val[i += mtnp->mt_mtlen] = '\0';
         i = asccasecmp(val, *argv);
         ac_free(val);

         if (!i) {
            struct mtnode *nnp = mtnp->mt_next;
            if (lnp == NULL)
               _mt_list = nnp;
            else
               lnp->mt_next = nnp;
            free(mtnp);
            mtnp = nnp;
            match = TRU1;
         } else
            lnp = mtnp, mtnp = mtnp->mt_next;
      }
      if (!match) {
         if (!(pstate & PS_ROBOT) || (options & OPT_D_V))
            n_err(_("No such MIME type: %s\n"), *argv);
         v = NULL;
      }
   }
   NYD_LEAVE;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL bool_t
mime_type_check_mtname(char const *name)
{
   struct mtlookup mtl;
   bool_t rv;
   NYD_ENTER;

   rv = (_mt_by_mtname(&mtl, name) != NULL);
   NYD_LEAVE;
   return rv;
}

FL char *
mime_type_classify_filename(char const *name)
{
   struct mtlookup mtl;
   NYD_ENTER;

   _mt_by_filename(&mtl, name, TRU1);
   NYD_LEAVE;
   return mtl.mtl_result;
}

FL enum conversion
mime_type_classify_file(FILE *fp, char const **contenttype,
   char const **charset, int *do_iconv)
{
   /* TODO classify once only PLEASE PLEASE PLEASE */
   enum mime_type_class mtc;
   enum mime_enc menc;
   off_t fpsz;
   NYD_ENTER;

   assert(ftell(fp) == 0x0l);

   *do_iconv = 0;

   if (*contenttype == NULL)
      mtc = _MT_C_NCTT;
   else if (!ascncasecmp(*contenttype, "text/", 5))
      mtc = ok_blook(mime_allow_text_controls)
         ? _MT_C_ISTXT | _MT_C_ISTXTCOK : _MT_C_ISTXT;
   else
      mtc = _MT_C_CLEAN;

   menc = mime_enc_target();

   if ((fpsz = fsize(fp)) == 0)
      goto j7bit;
   else {
      char buf[BUFFER_SIZE];
      struct mt_class_arg mtca;

      _mt_classify_init(&mtca, mtc);
      for (;;) {
         mtca.mtca_len = fread(buf, sizeof(buf[0]), n_NELEM(buf), fp);
         mtca.mtca_buf = buf;
         if ((mtc = _mt_classify_round(&mtca)) & _MT_C_SUGGEST_DONE)
            break;
         if (mtca.mtca_len == 0)
            break;
      }
      /* TODO ferror(fp) ! */
      rewind(fp);
   }

   if (mtc & _MT_C_HASNUL) {
      menc = MIMEE_B64;
      /* Don't overwrite a text content-type to allow UTF-16 and such, but only
       * on request; else enforce what file(1)/libmagic(3) would suggest */
      if (mtc & _MT_C_ISTXTCOK)
         goto jcharset;
      if (mtc & (_MT_C_NCTT | _MT_C_ISTXT))
         *contenttype = "application/octet-stream";
      if (*charset == NULL)
         *charset = "binary";
      goto jleave;
   }

   if (mtc &
         (_MT_C_LONGLINES | _MT_C_CTRLCHAR | _MT_C_NOTERMNL | _MT_C_FROM_)) {
      if (menc != MIMEE_B64)
         menc = MIMEE_QP;
      goto jstepi;
   }
   if (mtc & _MT_C_HIGHBIT) {
jstepi:
      if (mtc & (_MT_C_NCTT | _MT_C_ISTXT))
         *do_iconv = ((mtc & _MT_C_HIGHBIT) != 0);
   } else
j7bit:
      menc = MIMEE_7B;
   if (mtc & _MT_C_NCTT)
      *contenttype = "text/plain";

   /* Not an attachment with specified charset? */
jcharset:
   if (*charset == NULL) /* TODO MIME/send: iter active? iter! else */
      *charset = (mtc & _MT_C_HIGHBIT) ? charset_iter_or_fallback()
            : charset_get_7bit();
jleave:
   NYD_LEAVE;
   /* TODO mime_type_file_classify() shouldn't return conversion */
   return (menc == MIMEE_7B ? CONV_7BIT :
      (menc == MIMEE_8B ? CONV_8BIT :
      (menc == MIMEE_QP ? CONV_TOQP : CONV_TOB64)));
}

FL enum mimecontent
mime_type_classify_part(struct mimepart *mpp) /* FIXME charset=binary ??? */
{
   struct mtlookup mtl;
   enum mimecontent mc;
   char const *ct;
   union {char const *cp; ui32_t f;} mce;
   bool_t is_os;
   NYD_ENTER;

   mc = MIME_UNKNOWN;
   if ((ct = mpp->m_ct_type_plain) == NULL) /* TODO may not */
      ct = n_empty;

   if ((mce.cp = ok_vlook(mime_counter_evidence)) != NULL) {
      char *eptr;
      ul_i ul;

      ul = strtoul(mce.cp, &eptr, 0); /* XXX strtol */
      if (*mce.cp == '\0')
         is_os = FAL0;
      else if (*eptr != '\0' || (ui64_t)ul >= UI32_MAX) {
         n_err(_("Can't parse *mime-counter-evidence* value: %s\n"), mce.cp);
         is_os = FAL0;
      } else {
         mce.f = (ui32_t)ul | MIMECE_SET;
         is_os = !asccasecmp(ct, "application/octet-stream");

         if (mpp->m_filename != NULL && (is_os || (mce.f & MIMECE_ALL_OVWR))) {
            if (_mt_by_filename(&mtl, mpp->m_filename, TRU1) == NULL) {
               if (is_os)
                  goto jos_content_check;
            } else if (is_os || asccasecmp(ct, mtl.mtl_result)) {
               if (mce.f & MIMECE_ALL_OVWR)
                  mpp->m_ct_type_plain = ct = mtl.mtl_result;
               if (mce.f & (MIMECE_BIN_OVWR | MIMECE_ALL_OVWR))
                  mpp->m_ct_type_usr_ovwr = ct = mtl.mtl_result;
            }
         }
      }
   } else
      is_os = FAL0;

   if (strchr(ct, '/') == NULL) /* For compatibility with non-MIME */
      mc = MIME_TEXT;
   else if (is_asccaseprefix(ct, "text/")) {
      ct += sizeof("text/") -1;
      if (!asccasecmp(ct, "plain"))
         mc = MIME_TEXT_PLAIN;
      else if (!asccasecmp(ct, "html"))
         mc = MIME_TEXT_HTML;
      else
         mc = MIME_TEXT;
   } else if (is_asccaseprefix(ct, "message/")) {
      ct += sizeof("message/") -1;
      if (!asccasecmp(ct, "rfc822"))
         mc = MIME_822;
      else
         mc = MIME_MESSAGE;
   } else if (!ascncasecmp(ct, "multipart/", 10)) {
      ct += sizeof("multipart/") -1;
      if (!asccasecmp(ct, "alternative"))
         mc = MIME_ALTERNATIVE;
      else if (!asccasecmp(ct, "related"))
         mc = MIME_RELATED;
      else if (!asccasecmp(ct, "digest"))
         mc = MIME_DIGEST;
      else
         mc = MIME_MULTI;
   } else if (is_asccaseprefix(ct, "application/")) {
      if (is_os)
         goto jos_content_check;
      ct += sizeof("application/") -1;
      if (!asccasecmp(ct, "pkcs7-mime") || !asccasecmp(ct, "x-pkcs7-mime"))
         mc = MIME_PKCS7;
   }
jleave:
   NYD_LEAVE;
   return mc;

jos_content_check:
   if ((mce.f & MIMECE_BIN_PARSE) && mpp->m_mime_enc != MIMEE_BIN &&
         mpp->m_charset != NULL && asccasecmp(mpp->m_charset, "binary"))
      mc = _mt_classify_os_part(mce.f, mpp);
   goto jleave;
}

FL enum mime_handler_flags
mime_type_handler(struct mime_handler *mhp, struct mimepart const *mpp,
   enum sendaction action)
{
#define __S    "pipe-"
#define __L    (sizeof(__S) -1)
   struct mtlookup mtl;
   char *buf, *cp;
   enum mime_handler_flags rv;
   char const *es, *cs, *ccp;
   size_t el, cl, l;
   NYD_ENTER;

   memset(mhp, 0, sizeof *mhp);
   buf = NULL;

   rv = MIME_HDL_NULL;
   if (action == SEND_QUOTE || action == SEND_QUOTE_ALL)
      rv |= MIME_HDL_ISQUOTE;
   else if (action != SEND_TODISP && action != SEND_TODISP_ALL)
      goto jleave;

   el = ((es = mpp->m_filename) != NULL && (es = strrchr(es, '.')) != NULL &&
         *++es != '\0') ? strlen(es) : 0;
   cl = ((cs = mpp->m_ct_type_usr_ovwr) != NULL ||
         (cs = mpp->m_ct_type_plain) != NULL) ? strlen(cs) : 0;
   if ((l = n_MAX(el, cl)) == 0) {
      /* TODO this should be done during parse time! */
      goto jleave;
   }

   /* We don't pass the flags around, so ensure carrier is up-to-date */
   mhp->mh_flags = rv;

   buf = ac_alloc(__L + l +1);
   memcpy(buf, __S, __L);

   /* File-extension handlers take precedence.
    * Yes, we really "fail" here for file extensions which clash MIME types */
   if (el > 0) {
      memcpy(buf + __L, es, el +1);
      for (cp = buf + __L; *cp != '\0'; ++cp)
         *cp = lowerconv(*cp);

      if ((mhp->mh_shell_cmd = ccp = vok_vlook(buf)) != NULL) {
         rv = _mt_pipe_check(mhp);
         goto jleave;
      }
   }

   /* Then MIME Content-Type:, if any */
   if (cl == 0)
      goto jleave;

   memcpy(buf + __L, cs, cl +1);
   for (cp = buf + __L; *cp != '\0'; ++cp)
      *cp = lowerconv(*cp);

   if ((mhp->mh_shell_cmd = vok_vlook(buf)) != NULL) {
      rv = _mt_pipe_check(mhp);
      goto jleave;
   }

   if (_mt_by_mtname(&mtl, cs) != NULL)
      switch (mtl.mtl_node->mt_flags & __MT_MARKMASK) {
#ifndef HAVE_FILTER_HTML_TAGSOUP
      case _MT_SOUP_H:
         break;
#endif
      case _MT_SOUP_h:
#ifdef HAVE_FILTER_HTML_TAGSOUP
      case _MT_SOUP_H:
         mhp->mh_ptf = &htmlflt_process_main;
         mhp->mh_msg.l = strlen(mhp->mh_msg.s =
               n_UNCONST(_("Builtin HTML tagsoup filter")));
         rv ^= MIME_HDL_NULL | MIME_HDL_PTF;
         goto jleave;
#endif
         /* FALLTHRU */
      case _MT_PLAIN:
         mhp->mh_msg.l = strlen(mhp->mh_msg.s = n_UNCONST(_("Plain text")));
         rv ^= MIME_HDL_NULL | MIME_HDL_TEXT;
         goto jleave;
      default:
         break;
      }

jleave:
   if (buf != NULL)
      ac_free(buf);

   mhp->mh_flags = rv;
   if ((rv &= MIME_HDL_TYPE_MASK) == MIME_HDL_NULL)
      mhp->mh_msg.l = strlen(mhp->mh_msg.s = n_UNCONST(
            _("[-- No MIME handler installed or none applicable --]")));
   NYD_LEAVE;
   return rv;
#undef __L
#undef __S
}

/* s-it-mode */
