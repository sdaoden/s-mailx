/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `(un)?mimetype' and other mime.types(5) related facilities.
 *@ "Keep in sync with" ./mime.types.
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE mime_types
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/file-streams.h"
/* TODO that this does not belong: clear */
#include "mx/filter-html.h"

/* TODO fake */
#include "su/code-in.h"

enum mime_type {
   _MT_APPLICATION,
   _MT_AUDIO,
   _MT_IMAGE,
   _MT_MESSAGE,
   _MT_MULTIPART,
   _MT_TEXT,
   _MT_VIDEO,
   _MT_OTHER,
   __MT_TMIN = 0u,
   __MT_TMAX = _MT_OTHER,
   __MT_TMASK = 0x07u,

   _MT_CMD = 1u<< 8,          /* Via `mimetype' (not struct mtbltin) */
   _MT_USR = 1u<< 9,          /* VAL_MIME_TYPES_USR */
   _MT_SYS = 1u<<10,          /* VAL_MIME_TYPES_SYS */
   _MT_FSPEC = 1u<<11,        /* Loaded via f= *mimetypes-load-control* spec. */

   a_MT_TM_PLAIN = 1u<<16,    /* Without pipe handler display as text */
   a_MT_TM_SOUP_h = 2u<<16,   /* Ditto, but HTML tagsoup parser if possible */
   a_MT_TM_SOUP_H = 3u<<16,   /* HTML tagsoup parser, else NOT plain text */
   a_MT_TM_QUIET = 4u<<16,    /* No "no mime handler available" message */
   a_MT__TM_MARKMASK = 7u<<16
};

enum mime_type_class {
   _MT_C_NONE,
   _MT_C_CLEAN = _MT_C_NONE,  /* Plain RFC 5322 message */
   _MT_C_DEEP_INSPECT = 1u<<0,   /* Always test all the file */
   _MT_C_NCTT = 1u<<1,        /* *contenttype == NULL */
   _MT_C_ISTXT = 1u<<2,       /* *contenttype =~ text\/ */
   _MT_C_ISTXTCOK = 1u<<3,    /* _ISTXT + *mime-allow-text-controls* */
   _MT_C_HIGHBIT = 1u<<4,     /* Not 7bit clean */
   _MT_C_LONGLINES = 1u<<5,   /* MIME_LINELEN_LIMIT exceed. */
   _MT_C_CTRLCHAR = 1u<<6,    /* Control characters seen */
   _MT_C_HASNUL = 1u<<7,      /* Contains \0 characters */
   _MT_C_NOTERMNL = 1u<<8,    /* Lacks a final newline */
   _MT_C_FROM_ = 1u<<9,       /* ^From_ seen */
   _MT_C_FROM_1STLINE = 1u<<10,  /* From_ line seen */
   _MT_C_SUGGEST_DONE = 1u<<16,  /* Inspector suggests to stop further parse */
   _MT_C__1STLINE = 1u<<17    /* .. */
};

struct mtbltin {
   u32         mtb_flags;
   u32         mtb_mtlen;
   char const     *mtb_line;
};

struct mtnode {
   struct mtnode  *mt_next;
   u32         mt_flags;
   u32         mt_mtlen;   /* Length of MIME type string, rest thereafter */
   /* C99 forbids flexible arrays in union, so unfortunately we waste a pointer
    * that could already store character data here */
   char const     *mt_line;
};

struct mtlookup {
   char const           *mtl_name;
   uz               mtl_nlen;
   struct mtnode const  *mtl_node;
   char                 *mtl_result;   /* If requested, salloc()ed MIME type */
};

struct mt_class_arg {
   char const *mtca_buf;
   uz mtca_len;
   sz mtca_curlnlen;
   /*char mtca_lastc;*/
   char mtca_c;
   u8 mtca__dummy[3];
   enum mime_type_class mtca_mtc;
   u64 mtca_all_len;
   u64 mtca_all_highbit; /* TODO not yet interpreted */
   u64 mtca_all_bogus;
};

static struct mtbltin const   _mt_bltin[] = {
#include "gen-mime-types.h" /* */
};

static char const             _mt_typnames[][16] = {
   "application/", "audio/", "image/",
   "message/", "multipart/", "text/",
   "video/"
};
CTAV(_MT_APPLICATION == 0 && _MT_AUDIO == 1 && _MT_IMAGE == 2 &&
   _MT_MESSAGE == 3 && _MT_MULTIPART == 4 && _MT_TEXT == 5 &&
   _MT_VIDEO == 6);

/* */
static boole           _mt_is_init;
static struct mtnode    *_mt_list;

/* Initialize MIME type list in order */
static void             _mt_init(void);
static boole           __mt_load_file(u32 orflags,
                           char const *file, char **line, uz *linesize);

/* Create (prepend) a new MIME type; cmdcalled results in a bit more verbosity
 * for `mimetype' */
static struct mtnode *  _mt_create(boole cmdcalled, u32 orflags,
                           char const *line, uz len);

/* Try to find MIME type by X (after zeroing mtlp), return NULL if not found;
 * if with_result >mtl_result will be created upon success for the former */
static struct mtlookup * _mt_by_filename(struct mtlookup *mtlp,
                           char const *name, boole with_result);
static struct mtlookup * _mt_by_mtname(struct mtlookup *mtlp,
                           char const *mtname);

/* In-depth inspection of raw content: call _round() repeatedly, last time with
 * a 0 length buffer, finally check .mtca_mtc for result.
 * No further call is needed if _round() return includes _MT_C_SUGGEST_DONE,
 * as the resulting classification is unambiguous */
su_SINLINE struct mt_class_arg * _mt_classify_init(struct mt_class_arg *mtcap,
                                 enum mime_type_class initval);
static enum mime_type_class   _mt_classify_round(struct mt_class_arg *mtcap);

/* We need an in-depth inspection of an application/octet-stream part */
static enum mimecontent _mt_classify_os_part(u32 mce, struct mimepart *mpp,
                           boole deep_inspect);

/* Check whether a *pipe-XY* handler is applicable, and adjust flags according
 * to the defined trigger characters; upon entry MIME_HDL_NULL is set, and that
 * isn't changed if mhp doesn't apply */
static enum mime_handler_flags a_mt_pipe_check(struct mime_handler *mhp);

static void
_mt_init(void)
{
   struct mtnode *tail;
   char c, *line;
   uz linesize;
   u32 i, j;
   char const *srcs_arr[10], *ccp, **srcs;
   NYD_IN;

   /*if (_mt_is_init)
    *  goto jleave;*/

   /* Always load our built-ins */
   for (tail = NULL, i = 0; i < NELEM(_mt_bltin); ++i) {
      struct mtbltin const *mtbp = _mt_bltin + i;
      struct mtnode *mtnp = n_alloc(sizeof *mtnp);

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

   if (su_cs_find_c(ccp, '=') != NULL) {
      line = savestr(ccp);

      while ((ccp = su_cs_sep_c(&line, ',', TRU1)) != NULL) {
         switch ((c = *ccp)) {
         case 'S': case 's':
            srcs_arr[1] = VAL_MIME_TYPES_SYS;
            if (0) {
               /* FALLTHRU */
         case 'U': case 'u':
               srcs_arr[0] = VAL_MIME_TYPES_USR;
            }
            if (ccp[1] != '\0')
               goto jecontent;
            break;
         case 'F': case 'f':
            if (*++ccp == '=' && *++ccp != '\0') {
               if (P2UZ(srcs - srcs_arr) < NELEM(srcs_arr))
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
      case 'S': case 's': srcs_arr[1] = VAL_MIME_TYPES_SYS; break;
      case 'U': case 'u': srcs_arr[0] = VAL_MIME_TYPES_USR; break;
      default:
jecontent:
         n_err(_("*mimetypes-load-control*: unsupported content: %s\n"), ccp);
         goto jleave;
      }

   /* Load all file-based sources in the desired order */
   mx_fs_linepool_aquire(&line, &linesize);
   for(j = 0, i = S(u32,P2UZ(srcs - srcs_arr)), srcs = srcs_arr;
         i > 0; ++j, ++srcs, --i)
      if(*srcs == NIL)
         continue;
      else if(!__mt_load_file((j == 0 ? _MT_USR
               : (j == 1 ? _MT_SYS : _MT_FSPEC)), *srcs, &line, &linesize)) {
         if((n_poption & n_PO_D_V) || j > 1)
            n_err(_("*mimetypes-load-control*: cannot open or load %s\n"),
               n_shexp_quote_cp(*srcs, FAL0));
      }
   mx_fs_linepool_release(line, linesize);

jleave:
   _mt_is_init = TRU1;
   NYD_OU;
}

static boole
__mt_load_file(u32 orflags, char const *file, char **line, uz *linesize)
{
   char const *cp;
   FILE *fp;
   struct mtnode *head, *tail, *mtnp;
   uz len;
   NYD_IN;

   if((cp = fexpand(file, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
         ) == NIL || (fp = mx_fs_open(cp, "r")) == NIL){
      cp = NIL;
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

   mx_fs_close(fp);
jleave:
   NYD_OU;
   return (cp != NULL);
}

static struct mtnode *
_mt_create(boole cmdcalled, u32 orflags, char const *line, uz len)
{
   struct mtnode *mtnp;
   char const *typ, *subtyp;
   uz tlen, i;
   NYD_IN;

   mtnp = NULL;

   /* Drop anything after a comment first TODO v15: only when read from file */
   if ((typ = su_mem_find(line, '#', len)) != NULL)
      len = P2UZ(typ - line);

   /* Then trim any trailing whitespace from line (including NL/CR) */
   /* C99 */{
      struct str work;

      work.s = n_UNCONST(line);
      work.l = len;
      line = n_str_trim(&work, n_STR_TRIM_BOTH)->s;
      len = work.l;
   }
   typ = line;

   /* (But wait - is there a type marker?) */
   tlen = len;
   if(!(orflags & (_MT_USR | _MT_SYS)) && (*typ == '?' || *typ == '@')){
      if(*typ == '@') /* v15compat (plus trailing below) */
         n_OBSOLETE2(_("`mimetype': type markers (and much more) use ? not @"),
            line);
      if(len < 2)
         goto jeinval;
      if(typ[1] == ' '){
         orflags |= a_MT_TM_PLAIN;
         typ += 2;
         len -= 2;
         line += 2;
      }else if(len > 3){
         if(typ[2] == ' ')
            i = 3;
         else if(len > 4 && (typ[2] == '?' || typ[2] == '@') && typ[3] == ' ')
            i = 4;
         else
            goto jeinval;

         switch(typ[1]){
         default: goto jeinval;
         case 't': orflags |= a_MT_TM_PLAIN; break;
         case 'h': orflags |= a_MT_TM_SOUP_h; break;
         case 'H': orflags |= a_MT_TM_SOUP_H; break;
         case 'q': orflags |= a_MT_TM_QUIET; break;
         }
         typ += i;
         len -= i;
         line += i;
      }else
         goto jeinval;
   }

   while (len > 0 && !su_cs_is_blank(*line))
      ++line, --len;
   /* Ignore empty lines and even incomplete specifications (only MIME type)
    * because this is quite common in mime.types(5) files */
   if (len == 0 || (tlen = P2UZ(line - typ)) == 0) {
      if (cmdcalled || (orflags & _MT_FSPEC)) {
         if(len == 0){
            line = _("(no value)");
            len = su_cs_len(line);
         }
         n_err(_("Empty MIME type or no extensions given: %.*s\n"),
            (int)len, line);
      }
      goto jleave;
   }

   if ((subtyp = su_mem_find(typ, '/', tlen)) == NULL || subtyp[1] == '\0' ||
         su_cs_is_space(subtyp[1])) {
jeinval:
      if(cmdcalled || (orflags & _MT_FSPEC) || (n_poption & n_PO_D_V))
         n_err(_("%s MIME type: %.*s\n"),
            (cmdcalled ? _("Invalid") : _("mime.types(5): invalid")),
            (int)tlen, typ);
      goto jleave;
   }
   ++subtyp;

   /* Map to mime_type */
   tlen = P2UZ(subtyp - typ);
   for (i = __MT_TMIN;;) {
      if (!su_cs_cmp_case_n(_mt_typnames[i], typ, tlen)) {
         orflags |= i;
         tlen = P2UZ(line - subtyp);
         typ = subtyp;
         break;
      }
      if (++i == __MT_TMAX) {
         orflags |= _MT_OTHER;
         tlen = P2UZ(line - typ);
         break;
      }
   }

   /* Strip leading whitespace from the list of extensions;
    * trailing WS has already been trimmed away above.
    * Be silent on slots which define a mimetype without any value */
   while (len > 0 && su_cs_is_blank(*line))
      ++line, --len;
   if (len == 0)
      goto jleave;

   /*  */
   mtnp = n_alloc(sizeof(*mtnp) + tlen + len +1);
   mtnp->mt_next = NULL;
   mtnp->mt_flags = orflags;
   mtnp->mt_mtlen = (u32)tlen;
   {  char *l = (char*)(mtnp + 1);
      mtnp->mt_line = l;
      su_mem_copy(l, typ, tlen);
      su_mem_copy(l + tlen, line, len);
      tlen += len;
      l[tlen] = '\0';
   }

jleave:
   NYD_OU;
   return mtnp;
}

static struct mtlookup *
_mt_by_filename(struct mtlookup *mtlp, char const *name, boole with_result)
{
   struct mtnode *mtnp;
   uz nlen, i, j;
   char const *ext, *cp;
   NYD2_IN;

   su_mem_set(mtlp, 0, sizeof *mtlp);

   if ((nlen = su_cs_len(name)) == 0) /* TODO name should be a URI */
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
         while (su_cs_is_white(*cp))
            ++cp;
         ext = cp;
         while (!su_cs_is_white(*cp) && *cp != '\0')
            ++cp;

         if ((i = P2UZ(cp - ext)) == 0)
            break;
         /* Don't allow neither of ".txt" or "txt" to match "txt" */
         else if (i + 1 >= nlen || name[(j = nlen - i) - 1] != '.' ||
               su_cs_cmp_case_n(name + j, ext, i))
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
            j = su_cs_len(name);
         }
         i = mtnp->mt_mtlen;
         mtlp->mtl_result = n_autorec_alloc(i + j +1);
         if (j > 0)
            su_mem_copy(mtlp->mtl_result, name, j);
         su_mem_copy(mtlp->mtl_result + j, mtnp->mt_line, i);
         mtlp->mtl_result[j += i] = '\0';
         goto jleave;
      }
jnull_leave:
   mtlp = NULL;
jleave:
   NYD2_OU;
   return mtlp;
}

static struct mtlookup *
_mt_by_mtname(struct mtlookup *mtlp, char const *mtname)
{
   struct mtnode *mtnp;
   uz nlen, i, j;
   char const *cp;
   NYD2_IN;

   su_mem_set(mtlp, 0, sizeof *mtlp);

   if ((mtlp->mtl_nlen = nlen = su_cs_len(mtlp->mtl_name = mtname)) == 0)
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
            j = su_cs_len(cp);
         }
         i = mtnp->mt_mtlen;

         if (i + j == mtlp->mtl_nlen) {
            char *xmt = n_lofi_alloc(i + j +1);
            if (j > 0)
               su_mem_copy(xmt, cp, j);
            su_mem_copy(xmt + j, mtnp->mt_line, i);
            xmt[j += i] = '\0';
            i = su_cs_cmp_case(mtname, xmt);
            n_lofi_free(xmt);

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
   NYD2_OU;
   return mtlp;
}

su_SINLINE struct mt_class_arg *
_mt_classify_init(struct mt_class_arg * mtcap, enum mime_type_class initval)
{
   NYD2_IN;
   su_mem_set(mtcap, 0, sizeof *mtcap);
   /*mtcap->mtca_lastc =*/ mtcap->mtca_c = EOF;
   mtcap->mtca_mtc = initval | _MT_C__1STLINE;
   NYD2_OU;
   return mtcap;
}

static enum mime_type_class
_mt_classify_round(struct mt_class_arg *mtcap) /* TODO dig UTF-8 for !text/!! */
{
   /* TODO BTW., after the MIME/send layer rewrite we could use a MIME
    * TODO boundary of "=-=-=" if we would add a B_ in EQ spirit to F_,
    * TODO and report that state to the outer world */
#define F_        "From "
#define F_SIZEOF  (sizeof(F_) -1)
   char f_buf[F_SIZEOF], *f_p = f_buf;
   char const *buf;
   uz blen;
   sz curlnlen;
   s64 alllen;
   int c, lastc;
   enum mime_type_class mtc;
   NYD2_IN;

   buf = mtcap->mtca_buf;
   blen = mtcap->mtca_len;
   curlnlen = mtcap->mtca_curlnlen;
   alllen = mtcap->mtca_all_len;
   c = mtcap->mtca_c;
   /*lastc = mtcap->mtca_lastc;*/
   mtc = mtcap->mtca_mtc;

   for (;; ++curlnlen) {
      if(blen == 0){
         /* Real EOF, or only current buffer end? */
         if(mtcap->mtca_len == 0){
            lastc = c;
            c = EOF;
         }else{
            lastc = EOF;
            break;
         }
      }else{
         ++alllen;
         lastc = c;
         c = (uc)*buf++;
      }
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
         mtc &= ~_MT_C__1STLINE;
         if (curlnlen >= MIME_LINELEN_LIMIT)
            mtc |= _MT_C_LONGLINES;
         if (c == EOF)
            break;
         f_p = f_buf;
         curlnlen = -1;
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

         /* If there is a escape sequence in reverse solidus notation defined
          * for this in ANSI X3.159-1989 (ANSI C89), don't treat it as a control
          * for real.  I.e., \a=\x07=BEL, \b=\x08=BS, \t=\x09=HT.  Don't follow
          * libmagic(1) in respect to \v=\x0B=VT.  \f=\x0C=NP; do ignore
          * \e=\x1B=ESC */
         if ((c >= '\x07' && c <= '\x0D') || c == '\x1B')
            continue;

         /* As a special case, if we are going for displaying data to the user
          * or quoting a message then simply continue this, in the end, in case
          * we get there, we will decide upon the all_len/all_bogus ratio
          * whether this is usable plain text or not */
         ++mtcap->mtca_all_bogus;
         if(mtc & _MT_C_DEEP_INSPECT)
            continue;

         mtc |= _MT_C_HASNUL; /* Force base64 */
         if (!(mtc & _MT_C_ISTXTCOK)) {
            mtc |= _MT_C_SUGGEST_DONE;
            break;
         }
      } else if ((u8)c & 0x80) {
         mtc |= _MT_C_HIGHBIT;
         ++mtcap->mtca_all_highbit;
         if (!(mtc & (_MT_C_NCTT | _MT_C_ISTXT))) { /* TODO _NCTT?? */
            mtc |= _MT_C_HASNUL /* Force base64 */ | _MT_C_SUGGEST_DONE;
            break;
         }
      } else if (!(mtc & _MT_C_FROM_) && UCMP(z, curlnlen, <, F_SIZEOF)) {
         *f_p++ = (char)c;
         if (UCMP(z, curlnlen, ==, F_SIZEOF - 1) &&
               P2UZ(f_p - f_buf) == F_SIZEOF &&
               !su_mem_cmp(f_buf, F_, F_SIZEOF)){
            mtc |= _MT_C_FROM_;
            if (mtc & _MT_C__1STLINE)
               mtc |= _MT_C_FROM_1STLINE;
         }
      }
   }
   if (c == EOF && lastc != '\n')
      mtc |= _MT_C_NOTERMNL;

   mtcap->mtca_curlnlen = curlnlen;
   /*mtcap->mtca_lastc = lastc*/;
   mtcap->mtca_c = c;
   mtcap->mtca_mtc = mtc;
   mtcap->mtca_all_len = alllen;
   NYD2_OU;
   return mtc;
#undef F_
#undef F_SIZEOF
}

static enum mimecontent
_mt_classify_os_part(u32 mce, struct mimepart *mpp, boole deep_inspect)
{
   struct str in = {NULL, 0}, outrest, inrest, dec;
   struct mt_class_arg mtca;
   boole did_inrest;
   enum mime_type_class mtc;
   int lc, c;
   uz cnt, lsz;
   FILE *ibuf;
   off_t start_off;
   enum mimecontent mc;
   NYD2_IN;

   ASSERT(mpp->m_mime_enc != MIMEE_BIN);

   outrest = inrest = dec = in;
   mc = MIME_UNKNOWN;
   mtc = 0;
   did_inrest = FAL0;

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
    * along the way TODO this should simply be "mime_factory_create(MPP)"!
    * TODO In fact m_mime_classifier_(setup|call|call_part|finalize)() and the
    * TODO state(s) (the _MT_C states) should become reported to the outer
    * TODO world like that (see MIME boundary TODO around here) */
   _mt_classify_init(&mtca, (_MT_C_ISTXT |
      (deep_inspect ? _MT_C_DEEP_INSPECT : _MT_C_NONE)));

   for (lsz = 0;;) {
      boole dobuf;

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
         in.s = n_realloc(in.s, lsz += LINESIZE);
      if (c != EOF)
         in.s[in.l++] = (char)c;
      if (!dobuf)
         continue;

jdobuf:
      switch (mpp->m_mime_enc) {
      case MIMEE_B64:
         if (!b64_decode_part(&dec, &in, &outrest,
               (did_inrest ? NULL : &inrest))) {
            mtca.mtca_mtc = _MT_C_HASNUL;
            goto jstopit; /* break;break; */
         }
         break;
      case MIMEE_QP:
         /* Drin */
         if (!qp_decode_part(&dec, &in, &outrest, &inrest)) {
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
      mtca.mtca_len = (sz)dec.l;
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
      inrest.s = NULL;
      did_inrest = TRU1;
      goto jdobuf;
   }
   if (outrest.l > 0)
      goto jdobuf;
jstopit:
   if (in.s != NULL)
      n_free(in.s);
   if (dec.s != NULL)
      n_free(dec.s);
   if (outrest.s != NULL)
      n_free(outrest.s);
   if (inrest.s != NULL)
      n_free(inrest.s);

   fseek(mb.mb_itf, start_off, SEEK_SET);

   if (!(mtc & (_MT_C_HASNUL /*| _MT_C_CTRLCHAR XXX really? */))) {
      /* In that special relaxed case we may very well wave through
       * octet-streams full of control characters, as they do no harm
       * TODO This should be part of m_mime_classifier_finalize() then! */
      if(deep_inspect &&
            mtca.mtca_all_len - mtca.mtca_all_bogus < mtca.mtca_all_len >> 2)
         goto jleave;

      mc = MIME_TEXT_PLAIN;
      if (mce & MIMECE_ALL_OVWR)
         mpp->m_ct_type_plain = "text/plain";
      if (mce & (MIMECE_BIN_OVWR | MIMECE_ALL_OVWR))
         mpp->m_ct_type_usr_ovwr = "text/plain";
   }
jleave:
   NYD2_OU;
   return mc;
}

static enum mime_handler_flags
a_mt_pipe_check(struct mime_handler *mhp){
   enum mime_handler_flags rv_orig, rv;
   char const *cp;
   NYD2_IN;

   rv_orig = rv = mhp->mh_flags;

   /* Do we have any handler for this part? */
   if(*(cp = mhp->mh_shell_cmd) == '\0')
      goto jleave;
   else if(*cp++ != '?' && cp[-1] != '@'/* v15compat */){
      rv |= MIME_HDL_CMD;
      goto jleave;
   }else if(*cp == '\0'){
      if(cp[-1] == '@')
         n_OBSOLETE2(_("*pipe-TYPE/SUBTYPE*+': type markers (and much more) "
            "use ? not @"), mhp->mh_shell_cmd);
      rv |= MIME_HDL_TEXT;
      goto jleave;
   }

jnextc:
   switch(*cp){
   case '*': rv |= MIME_HDL_COPIOUSOUTPUT; ++cp; goto jnextc;
   case '#': rv |= MIME_HDL_NOQUOTE; ++cp; goto jnextc;
   case '&': rv |= MIME_HDL_ASYNC; ++cp; goto jnextc;
   case '!': rv |= MIME_HDL_NEEDSTERM; ++cp; goto jnextc;
   case '+':
      if(rv & MIME_HDL_TMPF)
         rv |= MIME_HDL_TMPF_UNLINK;
      rv |= MIME_HDL_TMPF;
      ++cp;
      goto jnextc;
   case '=':
      rv |= MIME_HDL_TMPF_FILL;
      ++cp;
      goto jnextc;
   case '@':/* v15compat */
      /* FALLTHRU */
   case '?':
      ++cp;
      /* FALLTHRU */
   default:
      break;
   }
   mhp->mh_shell_cmd = cp;

   /* Implications */
   if(rv & MIME_HDL_TMPF_FILL)
      rv |= MIME_HDL_TMPF;

   /* Exceptions */
   if(rv & MIME_HDL_ISQUOTE){
      if(rv & MIME_HDL_NOQUOTE)
         goto jerr;

      /* Cannot fetch data back from asynchronous process */
      if(rv & MIME_HDL_ASYNC)
         goto jerr;

      /* TODO Can't use a "needsterminal" program for quoting */
      if(rv & MIME_HDL_NEEDSTERM)
         goto jerr;
   }

   if(rv & MIME_HDL_NEEDSTERM){
      if(rv & MIME_HDL_COPIOUSOUTPUT){
         n_err(_("MIME type handlers: cannot use needsterminal and "
            "copiousoutput together\n"));
         goto jerr;
      }
      if(rv & MIME_HDL_ASYNC){
         n_err(_("MIME type handlers: cannot use needsterminal and "
            "x-mailx-async together\n"));
         goto jerr;
      }

      /* needsterminal needs a terminal */
      if(!(n_psonce & n_PSO_INTERACTIVE))
         goto jerr;
   }

   if(rv & MIME_HDL_ASYNC){
      if(rv & MIME_HDL_COPIOUSOUTPUT){
         n_err(_("MIME type handlers: cannot use x-mailx-async and "
            "copiousoutput together\n"));
         goto jerr;
      }
      if(rv & MIME_HDL_TMPF_UNLINK){
         n_err(_("MIME type handlers: cannot use x-mailx-async and "
            "x-mailx-tmpfile-unlink together\n"));
         goto jerr;
      }
   }

   /* TODO mailcap-only: TMPF_UNLINK): needs -tmpfile OR -tmpfile-fill */

   rv |= MIME_HDL_CMD;
jleave:
   mhp->mh_flags = rv;
   NYD2_OU;
   return rv;
jerr:
   rv = rv_orig;
   goto jleave;
}

FL int
c_mimetype(void *v){
   struct n_string s_b, *s;
   struct mtnode *mtnp;
   char **argv;
   NYD_IN;

   if(!_mt_is_init)
      _mt_init();

   s = n_string_creat_auto(&s_b);

   if(*(argv = v) == NULL){
      FILE *fp;
      uz l;

      if(_mt_list == NULL){
         fprintf(n_stdout, _("# `mimetype': no mime.types(5) available\n"));
         goto jleave;
      }

      if((fp = mx_fs_tmp_open("mimetype", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
               mx_FS_O_REGISTER), NIL)) == NIL){
         n_perr(_("tmpfile"), 0);
         v = NIL;
         goto jleave;
      }

      s = n_string_reserve(s, 63);

      for(l = 0, mtnp = _mt_list; mtnp != NULL; ++l, mtnp = mtnp->mt_next){
         char const *cp;

         s = n_string_trunc(s, 0);

         switch(mtnp->mt_flags & a_MT__TM_MARKMASK){
         case a_MT_TM_PLAIN: cp = "?t "; break;
         case a_MT_TM_SOUP_h: cp = "?h "; break;
         case a_MT_TM_SOUP_H: cp = "?H "; break;
         case a_MT_TM_QUIET: cp = "?q "; break;
         default: cp = NULL; break;
         }
         if(cp != NULL)
            s = n_string_push_cp(s, cp);

         if((mtnp->mt_flags & __MT_TMASK) != _MT_OTHER)
            s = n_string_push_cp(s, _mt_typnames[mtnp->mt_flags &__MT_TMASK]);

         s = n_string_push_buf(s, mtnp->mt_line, mtnp->mt_mtlen);
         s = n_string_push_c(s, ' ');
         s = n_string_push_c(s, ' ');
         s = n_string_push_cp(s, &mtnp->mt_line[mtnp->mt_mtlen]);

         fprintf(fp, "mimetype %s%s\n", n_string_cp(s),
            ((n_poption & n_PO_D_V) == 0 ? n_empty
               : (mtnp->mt_flags & _MT_USR ? " # user"
               : (mtnp->mt_flags & _MT_SYS ? " # system"
               : (mtnp->mt_flags & _MT_FSPEC ? " # f= file"
               : (mtnp->mt_flags & _MT_CMD ? " # command" : " # built-in"))))));
       }

      page_or_print(fp, l);
      mx_fs_close(fp);
   }else{
      for(; *argv != NULL; ++argv){
         if(s->s_len > 0)
            s = n_string_push_c(s, ' ');
         s = n_string_push_cp(s, *argv);
      }

      mtnp = _mt_create(TRU1, _MT_CMD, n_string_cp(s), s->s_len);
      if(mtnp != NULL){
         mtnp->mt_next = _mt_list;
         _mt_list = mtnp;
      }else
         v = NULL;
   }
jleave:
   NYD_OU;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL int
c_unmimetype(void *v)
{
   char **argv = v;
   struct mtnode *lnp, *mtnp;
   boole match;
   NYD_IN;

   /* Need to load that first as necessary */
   if (!_mt_is_init)
      _mt_init();

   for (; *argv != NULL; ++argv) {
      if (!su_cs_cmp_case(*argv, "reset")) {
         _mt_is_init = FAL0;
         goto jdelall;
      }

      if (argv[0][0] == '*' && argv[0][1] == '\0') {
jdelall:
         while ((mtnp = _mt_list) != NULL) {
            _mt_list = mtnp->mt_next;
            n_free(mtnp);
         }
         continue;
      }

      for (match = FAL0, lnp = NULL, mtnp = _mt_list; mtnp != NULL;) {
         char const *typ;
         char *val;
         uz i;

         if ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            typ = n_empty;
            i = 0;
         } else {
            typ = _mt_typnames[mtnp->mt_flags & __MT_TMASK];
            i = su_cs_len(typ);
         }

         val = n_lofi_alloc(i + mtnp->mt_mtlen +1);
         su_mem_copy(val, typ, i);
         su_mem_copy(val + i, mtnp->mt_line, mtnp->mt_mtlen);
         val[i += mtnp->mt_mtlen] = '\0';
         i = su_cs_cmp_case(val, *argv);
         n_lofi_free(val);

         if (!i) {
            struct mtnode *nnp = mtnp->mt_next;
            if (lnp == NULL)
               _mt_list = nnp;
            else
               lnp->mt_next = nnp;
            n_free(mtnp);
            mtnp = nnp;
            match = TRU1;
         } else
            lnp = mtnp, mtnp = mtnp->mt_next;
      }
      if (!match) {
         if (!(n_pstate & n_PS_ROBOT) || (n_poption & n_PO_D_V))
            n_err(_("No such MIME type: %s\n"), *argv);
         v = NULL;
      }
   }
   NYD_OU;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL boole
n_mimetype_check_mtname(char const *name)
{
   struct mtlookup mtl;
   boole rv;
   NYD_IN;

   rv = (_mt_by_mtname(&mtl, name) != NULL);
   NYD_OU;
   return rv;
}

FL char *
n_mimetype_classify_filename(char const *name)
{
   struct mtlookup mtl;
   NYD_IN;

   _mt_by_filename(&mtl, name, TRU1);
   NYD_OU;
   return mtl.mtl_result;
}

FL enum conversion
n_mimetype_classify_file(FILE *fp, char const **contenttype,
   char const **charset, int *do_iconv, boole no_mboxo)
{
   /* TODO classify once only PLEASE PLEASE PLEASE */
   /* TODO message/rfc822 is special in that it may only be 7bit, 8bit or
    * TODO binary according to RFC 2046, 5.2.1
    * TODO The handling of which is a hack */
   boole rfc822;
   enum mime_type_class mtc;
   enum mime_enc menc;
   off_t fpsz;
   enum conversion c;
   NYD_IN;

   ASSERT(ftell(fp) == 0x0l);

   *do_iconv = 0;

   if (*contenttype == NULL) {
      mtc = _MT_C_NCTT;
      rfc822 = FAL0;
   } else if (!su_cs_cmp_case_n(*contenttype, "text/", 5)) {
      mtc = ok_blook(mime_allow_text_controls)
         ? _MT_C_ISTXT | _MT_C_ISTXTCOK : _MT_C_ISTXT;
      rfc822 = FAL0;
   } else if (!su_cs_cmp_case(*contenttype, "message/rfc822")) {
      mtc = _MT_C_ISTXT;
      rfc822 = TRU1;
   } else {
      mtc = _MT_C_CLEAN;
      rfc822 = FAL0;
   }

   menc = mime_enc_target();

   if ((fpsz = fsize(fp)) == 0)
      goto j7bit;
   else {
      char buf[BUFFER_SIZE];
      struct mt_class_arg mtca;

      _mt_classify_init(&mtca, mtc);
      for (;;) {
         mtca.mtca_len = fread(buf, sizeof(buf[0]), NELEM(buf), fp);
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
      goto jleave;
   }

   if(mtc & (_MT_C_LONGLINES | _MT_C_CTRLCHAR | _MT_C_NOTERMNL | _MT_C_FROM_)){
      if(menc != MIMEE_B64 && menc != MIMEE_QP){
         /* If the user chooses 8bit, and we do not privacy-sign the message,
          * then if encoding would be enforced only because of a ^From_, no */
         if((mtc & (_MT_C_LONGLINES | _MT_C_CTRLCHAR | _MT_C_NOTERMNL |
               _MT_C_FROM_)) != _MT_C_FROM_ || no_mboxo)
            menc = MIMEE_QP;
         else{
            ASSERT(menc != MIMEE_7B);
            menc = (mtc & _MT_C_HIGHBIT) ? MIMEE_8B : MIMEE_7B;
         }
      }
      *do_iconv = ((mtc & _MT_C_HIGHBIT) != 0);
   }else if(mtc & _MT_C_HIGHBIT){
      if(mtc & (_MT_C_NCTT | _MT_C_ISTXT))
         *do_iconv = TRU1;
   }else
j7bit:
      menc = MIMEE_7B;
   if(mtc & _MT_C_NCTT)
      *contenttype = "text/plain";

   /* Not an attachment with specified charset? */
jcharset:
   if (*charset == NULL) /* TODO MIME/send: iter active? iter! else */
      *charset = (mtc & _MT_C_HIGHBIT) ? charset_iter_or_fallback()
            : ok_vlook(charset_7bit);
jleave:
   /* TODO mime_type_file_classify() shouldn't return conversion */
   if (rfc822) {
      if (mtc & _MT_C_FROM_1STLINE) {
         n_err(_("Pre-v15 %s cannot handle message/rfc822 that "
              "indeed is a RFC 4155 MBOX!\n"
            "  Forcing a content-type of application/mbox!\n"),
            n_uagent);
         *contenttype = "application/mbox";
         goto jnorfc822;
      }
      c = (menc == MIMEE_7B ? CONV_7BIT
            : (menc == MIMEE_8B ? CONV_8BIT
            /* May have only 7-bit, 8-bit and binary.  Try to avoid latter */
            : ((mtc & _MT_C_HASNUL) ? CONV_NONE
            : ((mtc & _MT_C_HIGHBIT) ? CONV_8BIT : CONV_7BIT))));
   } else
jnorfc822:
      c = (menc == MIMEE_7B ? CONV_7BIT
            : (menc == MIMEE_8B ? CONV_8BIT
            : (menc == MIMEE_QP ? CONV_TOQP : CONV_TOB64)));
   NYD_OU;
   return c;
}

FL enum mimecontent
n_mimetype_classify_part(struct mimepart *mpp, boole for_user_context){
   /* TODO n_mimetype_classify_part() <-> m_mime_classifier_ with life cycle */
   struct mtlookup mtl;
   enum mimecontent mc;
   char const *ct;
   union {char const *cp; u32 f;} mce;
   boole is_os;
   NYD_IN;

   mc = MIME_UNKNOWN;
   if ((ct = mpp->m_ct_type_plain) == NULL) /* TODO may not */
      ct = n_empty;

   if((mce.cp = ok_vlook(mime_counter_evidence)) != NULL && *mce.cp != '\0'){
      if((su_idec_u32_cp(&mce.f, mce.cp, 0, NULL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED){
         n_err(_("Invalid *mime-counter-evidence* value content\n"));
         is_os = FAL0;
      }else{
         mce.f |= MIMECE_SET;
         is_os = !su_cs_cmp_case(ct, "application/octet-stream");

         if(mpp->m_filename != NULL && (is_os || (mce.f & MIMECE_ALL_OVWR))){
            if(_mt_by_filename(&mtl, mpp->m_filename, TRU1) == NULL){
               if(is_os)
                  goto jos_content_check;
            }else if(is_os || su_cs_cmp_case(ct, mtl.mtl_result)){
               if(mce.f & MIMECE_ALL_OVWR)
                  mpp->m_ct_type_plain = ct = mtl.mtl_result;
               if(mce.f & (MIMECE_BIN_OVWR | MIMECE_ALL_OVWR))
                  mpp->m_ct_type_usr_ovwr = ct = mtl.mtl_result;
            }
         }
      }
   }else
      is_os = FAL0;

   if(*ct == '\0' || su_cs_find_c(ct, '/') == NULL) /* Compat with non-MIME */
      mc = MIME_TEXT;
   else if(su_cs_starts_with_case(ct, "text/")){
      ct += sizeof("text/") -1;
      if(!su_cs_cmp_case(ct, "plain"))
         mc = MIME_TEXT_PLAIN;
      else if(!su_cs_cmp_case(ct, "html"))
         mc = MIME_TEXT_HTML;
      else
         mc = MIME_TEXT;
   }else if(su_cs_starts_with_case(ct, "message/")){
      ct += sizeof("message/") -1;
      if(!su_cs_cmp_case(ct, "rfc822"))
         mc = MIME_822;
      else
         mc = MIME_MESSAGE;
   }else if(su_cs_starts_with_case(ct, "multipart/")){
      struct multi_types{
         char mt_name[12];
         enum mimecontent mt_mc;
      } const mta[] = {
         {"alternative\0", MIME_ALTERNATIVE},
         {"related", MIME_RELATED},
         {"digest", MIME_DIGEST},
         {"signed", MIME_SIGNED},
         {"encrypted", MIME_ENCRYPTED}
      }, *mtap;

      for(ct += sizeof("multipart/") -1, mtap = mta;;)
         if(!su_cs_cmp_case(ct, mtap->mt_name)){
            mc = mtap->mt_mc;
            break;
         }else if(++mtap == mta + NELEM(mta)){
            mc = MIME_MULTI;
            break;
         }
   }else if(su_cs_starts_with_case(ct, "application/")){
      if(is_os)
         goto jos_content_check;
      ct += sizeof("application/") -1;
      if(!su_cs_cmp_case(ct, "pkcs7-mime") ||
            !su_cs_cmp_case(ct, "x-pkcs7-mime"))
         mc = MIME_PKCS7;
   }
jleave:
   NYD_OU;
   return mc;

jos_content_check:
   if((mce.f & MIMECE_BIN_PARSE) && mpp->m_mime_enc != MIMEE_BIN &&
         mpp->m_charset != NULL)
      mc = _mt_classify_os_part(mce.f, mpp, for_user_context);
   goto jleave;
}

FL enum mime_handler_flags
n_mimetype_handler(struct mime_handler *mhp, struct mimepart const *mpp,
   enum sendaction action)
{
#define __S    "pipe-"
#define __L    (sizeof(__S) -1)
   struct mtlookup mtl;
   char *buf, *cp;
   enum mime_handler_flags rv, xrv;
   char const *es, *cs, *ccp;
   uz el, cl, l;
   NYD_IN;

   su_mem_set(mhp, 0, sizeof *mhp);
   buf = NULL;

   rv = MIME_HDL_NULL;
   if (action == SEND_QUOTE || action == SEND_QUOTE_ALL)
      rv |= MIME_HDL_ISQUOTE;
   else if (action != SEND_TODISP && action != SEND_TODISP_ALL &&
         action != SEND_TODISP_PARTS)
      goto jleave;

   el = ((es = mpp->m_filename) != NULL &&
         (es = su_cs_rfind_c(es, '.')) != NULL &&
         *++es != '\0') ? su_cs_len(es) : 0;
   cl = ((cs = mpp->m_ct_type_usr_ovwr) != NULL ||
         (cs = mpp->m_ct_type_plain) != NULL) ? su_cs_len(cs) : 0;
   if ((l = MAX(el, cl)) == 0) {
      /* TODO this should be done during parse time! */
      goto jleave;
   }

   /* We don't pass the flags around, so ensure carrier is up-to-date */
   mhp->mh_flags = rv;

   buf = n_lofi_alloc(__L + l +1);
   su_mem_copy(buf, __S, __L);

   /* File-extension handlers take precedence.
    * Yes, we really "fail" here for file extensions which clash MIME types */
   if (el > 0) {
      su_mem_copy(buf + __L, es, el +1);
      for (cp = buf + __L; *cp != '\0'; ++cp)
         *cp = su_cs_to_lower(*cp);

      if ((mhp->mh_shell_cmd = ccp = n_var_vlook(buf, FAL0)) != NULL) {
         rv = a_mt_pipe_check(mhp);
         goto jleave;
      }
   }

   /* Then MIME Content-Type:, if any */
   if (cl == 0)
      goto jleave;

   su_mem_copy(buf + __L, cs, cl +1);
   for (cp = buf + __L; *cp != '\0'; ++cp)
      *cp = su_cs_to_lower(*cp);

   if ((mhp->mh_shell_cmd = n_var_vlook(buf, FAL0)) != NULL) {
      rv = a_mt_pipe_check(mhp);
      goto jleave;
   }

   if (_mt_by_mtname(&mtl, cs) != NULL)
      switch (mtl.mtl_node->mt_flags & a_MT__TM_MARKMASK) {
#ifndef mx_HAVE_FILTER_HTML_TAGSOUP
      case a_MT_TM_SOUP_H:
         break;
#endif
      case a_MT_TM_SOUP_h:
#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
      case a_MT_TM_SOUP_H:
         mhp->mh_ptf = &mx_flthtml_process_main;
         mhp->mh_msg.l = su_cs_len(mhp->mh_msg.s =
               n_UNCONST(_("Built-in HTML tagsoup filter")));
         rv ^= MIME_HDL_NULL | MIME_HDL_PTF;
         goto jleave;
#endif
         /* FALLTHRU */
      case a_MT_TM_PLAIN:
         mhp->mh_msg.l = su_cs_len(mhp->mh_msg.s = n_UNCONST(_("Plain text")));
         rv ^= MIME_HDL_NULL | MIME_HDL_TEXT;
         goto jleave;
      case a_MT_TM_QUIET:
         mhp->mh_msg.l = 0;
         mhp->mh_msg.s = n_UNCONST(n_empty);
         goto jleave;
      default:
         break;
      }

jleave:
   if(buf != NULL)
      n_lofi_free(buf);

   xrv = rv;
   if((rv &= MIME_HDL_TYPE_MASK) == MIME_HDL_NULL){
      if(mhp->mh_msg.s == NULL)
         mhp->mh_msg.l = su_cs_len(mhp->mh_msg.s = n_UNCONST(
               A_("[-- No MIME handler installed, or not applicable --]\n")));
   }else if(rv == MIME_HDL_CMD && !(xrv & MIME_HDL_COPIOUSOUTPUT) &&
         action != SEND_TODISP_PARTS){
      mhp->mh_msg.l = su_cs_len(mhp->mh_msg.s = n_UNCONST(
            _("[-- Use the command `mimeview' to display this --]\n")));
      xrv &= ~MIME_HDL_TYPE_MASK;
      xrv |= (rv = MIME_HDL_MSG);
   }
   mhp->mh_flags = xrv;

   NYD_OU;
   return rv;
#undef __L
#undef __S
}

#include "su/code-ou.h"
/* s-it-mode */
