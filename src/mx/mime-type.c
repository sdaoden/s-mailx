/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mime-type.h.
 *@ "Keep in sync with" ../../mime.types.
 *@ TODO With an on_loop_tick_event, trigger cache update once per loop max.
 *
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE mime_type
#define mx_SOURCE
#define mx_SOURCE_MIME_TYPE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/compat.h"
#include "mx/file-streams.h"
#include "mx/mime.h"
#include "mx/mime-enc.h"

#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
   /* TODO that this does not belong: clear */
# include "mx/filter-html.h"
#endif
#ifdef mx_HAVE_MAILCAP
# include "mx/mailcap.h"
#endif

#include "mx/mime-type.h"
#include "su/code-in.h"

enum a_mt_flags{
   a_MT_APPLICATION,
   a_MT_AUDIO,
   a_MT_IMAGE,
   a_MT_MESSAGE,
   a_MT_MULTIPART,
   a_MT_TEXT,
   a_MT_VIDEO,
   a_MT_OTHER,
   a_MT__TMIN = 0u,
   a_MT__TMAX = a_MT_OTHER,
   a_MT__TMASK = 0x07u,

   a_MT_CMD = 1u<<8, /* Via `mimetype' (not struct a_mt_bltin) */
   a_MT_USR = 1u<<9, /* VAL_MIME_TYPES_USR */
   a_MT_SYS = 1u<<10, /* VAL_MIME_TYPES_SYS */
   a_MT_FSPEC = 1u<<11, /* Via f= *mimetypes-load-control* spec. */

   a_MT_TM_PLAIN = 1u<<16, /* Without pipe handler display as text */
   a_MT_TM_SOUP_h = 2u<<16, /* Ditto, but HTML tagsoup parser iff */
   a_MT_TM_SOUP_H = 3u<<16, /* HTML tagsoup, else NOT plain text */
   a_MT_TM_QUIET = 4u<<16, /* No "no mime handler available" message */
   a_MT__TM_MARKMASK = 7u<<16
};

enum a_mt_class{
   a_MT_C_NONE,
   a_MT_C_CLEAN = a_MT_C_NONE, /* Plain RFC 5322 message */
   a_MT_C_DEEP_INSPECT = 1u<<0, /* Always test all the file */
   a_MT_C_NCTT = 1u<<1, /* *content_type == NIL */
   a_MT_C_ISTXT = 1u<<2, /* *content_type =~ text\/ */
   a_MT_C_ISTXTCOK = 1u<<3, /* _ISTXT + *mime-allow-text-controls* */
   a_MT_C_HIGHBIT = 1u<<4, /* Not 7bit clean */
   a_MT_C_LONGLINES = 1u<<5, /* MIME_LINELEN_LIMIT exceed. */
   a_MT_C_CTRLCHAR = 1u<<6, /* Control characters seen */
   a_MT_C_HASNUL = 1u<<7, /* Contains \0 characters */
   a_MT_C_NOTERMNL = 1u<<8, /* Lacks a final newline */
   a_MT_C_FROM_ = 1u<<9, /* ^From_ seen */
   a_MT_C_FROM_1STLINE = 1u<<10, /* From_ line seen */
   a_MT_C_SUGGEST_DONE = 1u<<16, /* Inspector suggests parse stop */
   a_MT_C__1STLINE = 1u<<17 /* .. */
};

enum a_mt_counter_evidence{
   a_MT_CE_NONE,
   a_MT_CE_SET = 1u<<0, /* *mime-counter-evidence* was set */
   a_MT_CE_BIN_OVWR = 1u<<1, /* appli../o.-s.: check, ovw if possible */
   a_MT_CE_ALL_OVWR = 1u<<2, /* all: check, ovw if possible */
   a_MT_CE_BIN_PARSE = 1u<<3 /* appli../o.-s.: classify contents last */
};

struct a_mt_bltin{
   BITENUM_IS(u32,a_mt_flags) mtb_flags;
   u32 mtb_mtlen;
   char const *mtb_line;
};

struct a_mt_node{
   struct a_mt_node *mtn_next;
   BITENUM_IS(u32,a_mt_flags) mtn_flags;
   u32 mtn_len; /* Length of MIME type string, rest thereafter */
   char const *mtn_line;
};

struct a_mt_lookup{
   char const *mtl_name;
   uz mtl_nlen;
   struct a_mt_node const *mtl_node;
   char *mtl_result; /* If requested, AUTO_ALLOC()ed MIME type */
};

struct a_mt_class_arg{
   char const *mtca_buf;
   uz mtca_len;
   sz mtca_curlnlen;
   /*char mtca_lastc;*/
   char mtca_c;
   u8 mtca__dummy[3];
   BITENUM_IS(u32,a_mt_class) mtca_mtc;
   u64 mtca_all_len;
   u64 mtca_all_highbit; /* TODO not yet interpreted */
   u64 mtca_all_bogus;
};

static struct a_mt_bltin const a_mt_bltin[] = {
#include "gen-mime-types.h" /* */
};

static char const a_mt_names[][16] = {
   "application/", "audio/", "image/",
   "message/", "multipart/", "text/",
   "video/"
};
CTAV(a_MT_APPLICATION == 0 && a_MT_AUDIO == 1 &&
   a_MT_IMAGE == 2 && a_MT_MESSAGE == 3 &&
   a_MT_MULTIPART == 4 && a_MT_TEXT == 5 &&
   a_MT_VIDEO == 6);

/* */
static boole a_mt_is_init;
static struct a_mt_node *a_mt_list;

/* Initialize MIME type list in order */
static void a_mt_init(void);
static boole a_mt__load_file(BITENUM_IS(u32,a_mt_flags) orflags,
      char const *file, char **line, uz *linesize);

/* Create (prepend) a new MIME type; cmdcalled results in a bit more verbosity
 * for `mimetype' */
static struct a_mt_node *a_mt_create(boole cmdcalled,
      BITENUM_IS(u32,a_mt_flags) orflags, char const *line, uz len);

/* Try to find MIME type by X (after zeroing mtlp), return NIL if not found;
 * if with_result >mtl_result will be created upon success for the former */
static struct a_mt_lookup *a_mt_by_filename(struct a_mt_lookup *mtlp,
      char const *name, boole with_result);
static struct a_mt_lookup *a_mt_by_name(struct a_mt_lookup *mtlp,
      char const *name);

/* In-depth inspection of raw content: call _round() repeatedly, last time with
 * a 0 length buffer, finally check .mtca_mtc for result.
 * No further call is needed if _round() return includes _C_SUGGEST_DONE,
 * as the resulting classification is unambiguous */
SINLINE struct a_mt_class_arg *a_mt_classify_init(struct a_mt_class_arg *mtcap,
      BITENUM_IS(u32,a_mt_class) initval);
static BITENUM_IS(u32,a_mt_class) a_mt_classify_round(
      struct a_mt_class_arg *mtcap);

/* We need an in-depth inspection of an application/octet-stream part */
static enum mx_mime_type a_mt_classify_o_s_part(
      BITENUM_IS(u32,a_mt_counter_envidence) mce, struct mimepart *mpp,
      boole deep_inspect);

/* Check whether a *pipe-XY* handler is applicable, and adjust flags according
 * to the defined trigger characters; upon entry MIME_TYPE_HDL_NIL is set, and
 * that is not changed if mthp does not apply */
static BITENUM_IS(u32,mx_mime_type_handler_flags) a_mt_pipe_check(
      struct mx_mime_type_handler *mthp, enum sendaction action);

static void
a_mt_init(void){
   uz linesize;
   char c, *line;
   char const *srcs_arr[10], *ccp, **srcs;
   u32 i, j;
   struct a_mt_node *tail;
   NYD_IN;

   /*if(a_mt_is_init) Done by callees
    *  goto jleave;*/

   /* Always load our built-ins */
   for(tail = NIL, i = 0; i < NELEM(a_mt_bltin); ++i){
      struct a_mt_bltin const *mtbp;
      struct a_mt_node *mtnp;

      mtnp = su_ALLOC(sizeof *mtnp);
      mtbp = &a_mt_bltin[i];

      if(tail != NIL)
         tail->mtn_next = mtnp;
      else
         a_mt_list = mtnp;
      tail = mtnp;
      mtnp->mtn_next = NIL;
      mtnp->mtn_flags = mtbp->mtb_flags;
      mtnp->mtn_len = mtbp->mtb_mtlen;
      mtnp->mtn_line = mtbp->mtb_line;
   }

   /* Decide which files sources have to be loaded */
   if((ccp = ok_vlook(mimetypes_load_control)) == NIL)
      ccp = "US";
   else if(*ccp == '\0')
      goto jleave;

   srcs = &srcs_arr[2];
   srcs[-1] = srcs[-2] = NIL;

   if(su_cs_find_c(ccp, '=') != NIL){
      line = savestr(ccp);

      while((ccp = su_cs_sep_c(&line, ',', TRU1)) != NIL){
         switch((c = *ccp)){
         case 'S': case 's':
            srcs_arr[1] = VAL_MIME_TYPES_SYS;
            if(0){
               /* FALLTHRU */
         case 'U': case 'u':
               srcs_arr[0] = VAL_MIME_TYPES_USR;
            }
            if (ccp[1] != '\0')
               goto jecontent;
            break;
         case 'F': case 'f':
            if(*++ccp == '=' && *++ccp != '\0'){
               if(P2UZ(srcs - srcs_arr) < NELEM(srcs_arr))
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
   }else for(i = 0; (c = ccp[i]) != '\0'; ++i)
      switch(c){
      case 'S': case 's': srcs_arr[1] = VAL_MIME_TYPES_SYS; break;
      case 'U': case 'u': srcs_arr[0] = VAL_MIME_TYPES_USR; break;
      default:
jecontent:
         n_err(_("*mimetypes-load-control*: unsupported value: %s\n"), ccp);
         goto jleave;
      }

   /* Load all file-based sources in the desired order */
   mx_fs_linepool_aquire(&line, &linesize);
   for(j = 0, i = S(u32,P2UZ(srcs - srcs_arr)), srcs = srcs_arr;
         i > 0; ++j, ++srcs, --i)
      if(*srcs == NIL)
         continue;
      else if(!a_mt__load_file((j == 0 ? a_MT_USR
                  : (j == 1 ? a_MT_SYS : a_MT_FSPEC)),
               *srcs, &line, &linesize)){
         s32 eno;

         if((eno = su_err_no()) != su_ERR_NOENT ||
               (n_poption & n_PO_D_V) || j > 1)
            n_err(_("*mimetypes-load-control*: cannot open or load %s: %s\n"),
               n_shexp_quote_cp(*srcs, FAL0), su_err_doc(eno));
      }
   mx_fs_linepool_release(line, linesize);

jleave:
   a_mt_is_init = TRU1;
   NYD_OU;
}

static boole
a_mt__load_file(u32 orflags, char const *file, char **line, uz *linesize){
   uz len;
   struct a_mt_node *head, *tail, *mtnp;
   FILE *fp;
   char const *cp;
   NYD_IN;

   if((cp = fexpand(file, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
         ) == NIL || (fp = mx_fs_open(cp, "r")) == NIL){
      cp = NIL;
      goto jleave;
   }

   head = tail = NIL;

   while(fgetline(line, linesize, NIL, &len, fp, FAL0) != NIL)
      if((mtnp = a_mt_create(FAL0, orflags, *line, len)) != NIL){
         if(head == NIL)
            head = tail = mtnp;
         else
            tail->mtn_next = mtnp;
         tail = mtnp;
      }

   if(ferror(fp))
      cp = NIL;
   else if(head != NIL){
      tail->mtn_next = a_mt_list;
      a_mt_list = head;
   }

   mx_fs_close(fp);

jleave:
   NYD_OU;
   return (cp != NIL);
}

static struct a_mt_node *
a_mt_create(boole cmdcalled, BITENUM_IS(u32,a_mt_flags) orflags,
      char const *line, uz len){
   uz tlen, i;
   char const *typ, *subtyp;
   struct a_mt_node *mtnp;
   NYD_IN;

   mtnp = NIL;

   /* Drop anything after a comment first TODO v15: only when read from file */
   if((typ = su_mem_find(line, '#', len)) != NIL)
      len = P2UZ(typ - line);

   /* Then trim any trailing whitespace from line (including NL/CR) */
   /* C99 */{
      struct str work;

      work.s = UNCONST(char*,line);
      work.l = len;
      line = n_str_trim(&work, n_STR_TRIM_BOTH)->s;
      len = work.l;
   }
   typ = line;

   /* (But wait - is there a type marker?) */
   tlen = len;
   if(!(orflags & (a_MT_USR | a_MT_SYS)) && (*typ == '?' || *typ == '@')){
      if(*typ == '@') /* v15compat (plus trailing below) */
         n_OBSOLETE2(_("mimetype: type markers (and much more) use ? not @"),
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

   while(len > 0 && !su_cs_is_blank(*line))
      ++line, --len;
   /* Ignore empty lines and even incomplete specifications (only MIME type)
    * because this is quite common in mime.types(5) files */
   if(len == 0 || (tlen = P2UZ(line - typ)) == 0){
      if(cmdcalled || (orflags & a_MT_FSPEC)){
         if(len == 0){
            line = _("(no value)");
            len = su_cs_len(line);
         }
         n_err(_("Empty MIME type or no extensions given: %.*s\n"),
            S(int,len), line);
      }
      goto jleave;
   }

   if((subtyp = su_mem_find(typ, '/', tlen)) == NIL || subtyp[1] == '\0' ||
         su_cs_is_space(subtyp[1])) {
jeinval:
      if(cmdcalled || (orflags & a_MT_FSPEC) || (n_poption & n_PO_D_V))
         n_err(_("%s MIME type: %.*s\n"),
            (cmdcalled ? _("Invalid") : _("mime.types(5): invalid")),
            (int)tlen, typ);
      goto jleave;
   }
   ++subtyp;

   /* Map to mime_type */
   tlen = P2UZ(subtyp - typ);
   for(i = a_MT__TMIN;;){
      if(!su_cs_cmp_case_n(a_mt_names[i], typ, tlen)){
         orflags |= i;
         tlen = P2UZ(line - subtyp);
         typ = subtyp;
         break;
      }
      if(++i == a_MT__TMAX){
         orflags |= a_MT_OTHER;
         tlen = P2UZ(line - typ);
         break;
      }
   }

   /* Strip leading whitespace from the list of extensions;
    * trailing WS has already been trimmed away above.
    * Be silent on slots which define a mimetype without any value */
   while(len > 0 && su_cs_is_blank(*line))
      ++line, --len;
   if(len == 0)
      goto jleave;

   /*  */
   mtnp = su_ALLOC(sizeof(*mtnp) + tlen + len +1);
   mtnp->mtn_next = NIL;
   mtnp->mtn_flags = orflags;
   mtnp->mtn_len = S(u32,tlen);
   /* C99 */{
      char *l;

      l = S(char*,&mtnp[1]);
      mtnp->mtn_line = l;
      su_mem_copy(l, typ, tlen);
      su_mem_copy(&l[tlen], line, len);
      l[tlen += len] = '\0';
   }

jleave:
   NYD_OU;
   return mtnp;
}

static struct a_mt_lookup *
a_mt_by_filename(struct a_mt_lookup *mtlp, char const *name,
      boole with_result){
   char const *ext, *cp;
   struct a_mt_node *mtnp;
   uz nlen, i, j;
   NYD2_IN;

   su_mem_set(mtlp, 0, sizeof *mtlp);

   if((nlen = su_cs_len(name)) == 0) /* TODO name should be a URI */
      goto jnull_leave;
   /* We need a period TODO we should support names like README etc. */
   for(i = nlen; name[--i] != '.';)
      if(i == 0 || name[i] == '/') /* XXX no magics */
         goto jnull_leave;
   /* While here, basename() it */
   while(i > 0 && name[i - 1] != '/')
      --i;
   name += i;
   nlen -= i;
   mtlp->mtl_name = name;
   mtlp->mtl_nlen = nlen;

   if(!a_mt_is_init)
      a_mt_init();

   /* ..all the MIME types */
   for(mtnp = a_mt_list; mtnp != NIL; mtnp = mtnp->mtn_next){
      for(ext = &mtnp->mtn_line[mtnp->mtn_len];; ext = cp){
         cp = ext;
         while(su_cs_is_space(*cp))
            ++cp;
         ext = cp;
         while(!su_cs_is_space(*cp) && *cp != '\0')
            ++cp;

         if((i = P2UZ(cp - ext)) == 0)
            break;
         /* Do not allow neither of ".txt" or "txt" to match "txt" */
         else if(i + 1 >= nlen || name[(j = nlen - i) - 1] != '.' ||
               su_cs_cmp_case_n(name + j, ext, i))
            continue;

         /* Found it */
         mtlp->mtl_node = mtnp;

         if(!with_result)
            goto jleave;

         if((mtnp->mtn_flags & a_MT__TMASK) == a_MT_OTHER){
            name = su_empty;
            j = 0;
         }else{
            name = a_mt_names[mtnp->mtn_flags & a_MT__TMASK];
            j = su_cs_len(name);
         }
         i = mtnp->mtn_len;
         mtlp->mtl_result = n_autorec_alloc(i + j +1);
         if(j > 0)
            su_mem_copy(mtlp->mtl_result, name, j);
         su_mem_copy(&mtlp->mtl_result[j], mtnp->mtn_line, i);
         mtlp->mtl_result[j += i] = '\0';
         goto jleave;
      }
   }

jnull_leave:
   mtlp = NIL;
jleave:
   NYD2_OU;
   return mtlp;
}

static struct a_mt_lookup *
a_mt_by_name(struct a_mt_lookup *mtlp, char const *name){
   uz nlen, i, j;
   char const *cp;
   struct a_mt_node *mtnp;
   NYD2_IN;

   su_mem_set(mtlp, 0, sizeof *mtlp);

   if((mtlp->mtl_nlen = nlen = su_cs_len(mtlp->mtl_name = name)) == 0)
      goto jnil_leave;

   if(!a_mt_is_init)
      a_mt_init();

   /* ..all the MIME types */
   for(mtnp = a_mt_list; mtnp != NIL; mtnp = mtnp->mtn_next){
      if((mtnp->mtn_flags & a_MT__TMASK) == a_MT_OTHER){
         cp = su_empty;
         j = 0;
      }else{
         cp = a_mt_names[mtnp->mtn_flags & a_MT__TMASK];
         j = su_cs_len(cp);
      }
      i = mtnp->mtn_len;

      if(i + j == mtlp->mtl_nlen){
         char *xmt;

         xmt = n_lofi_alloc(i + j +1);
         if(j > 0)
            su_mem_copy(xmt, cp, j);
         su_mem_copy(&xmt[j], mtnp->mtn_line, i);
         xmt[j += i] = '\0';
         i = su_cs_cmp_case(name, xmt);
         n_lofi_free(xmt);

         /* Found it? */
         if(!i){
            mtlp->mtl_node = mtnp;
            goto jleave;
         }
      }
   }

jnil_leave:
   mtlp = NIL;
jleave:
   NYD2_OU;
   return mtlp;
}

SINLINE struct a_mt_class_arg *
a_mt_classify_init(struct a_mt_class_arg * mtcap,
      BITENUM_IS(u32,a_mt_class) initval){
   NYD2_IN;

   su_mem_set(mtcap, 0, sizeof *mtcap);
   /*mtcap->mtca_lastc =*/ mtcap->mtca_c = EOF;
   mtcap->mtca_mtc = initval | a_MT_C__1STLINE;

   NYD2_OU;
   return mtcap;
}

static BITENUM_IS(u32,a_mt_class)
a_mt_classify_round(struct a_mt_class_arg *mtcap){
   /* TODO classify_round: dig UTF-8 for !text/!! */
   /* TODO BTW., after the MIME/send layer rewrite we could use a MIME
    * TODO boundary of "=-=-=" if we would add a B_ in EQ spirit to F_,
    * TODO and report that state (to mime_param_boundary_create()) */
#define a_F_ "From "
#define a_F_SIZEOF (sizeof(a_F_) -1)

   char f_buf[a_F_SIZEOF], *f_p = f_buf;
   BITENUM_IS(u32,a_mt_class) mtc;
   int c, lastc;
   s64 alllen;
   sz curlnlen;
   uz blen;
   char const *buf;
   NYD2_IN;

   buf = mtcap->mtca_buf;
   blen = mtcap->mtca_len;
   curlnlen = mtcap->mtca_curlnlen;
   alllen = mtcap->mtca_all_len;
   c = mtcap->mtca_c;
   /*lastc = mtcap->mtca_lastc;*/
   mtc = mtcap->mtca_mtc;

   for(;; ++curlnlen){
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
         c = S(uc,*buf++);
      }
      --blen;

      if(c == '\0'){
         mtc |= a_MT_C_HASNUL;
         if(!(mtc & a_MT_C_ISTXTCOK)){
            mtc |= a_MT_C_SUGGEST_DONE;
            break;
         }
         continue;
      }
      if(c == '\n' || c == EOF){
         mtc &= ~a_MT_C__1STLINE;
         if(curlnlen >= MIME_LINELEN_LIMIT)
            mtc |= a_MT_C_LONGLINES;
         if(c == EOF)
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
       /*else*/ if(lastc == '\r')
         mtc |= a_MT_C_CTRLCHAR;

      /* Control character? XXX this is all ASCII here */
      if(c < 0x20 || c == 0x7F){
         /* RFC 2045, 6.7, as above ... */
         if(c != '\t' && c != '\r')
            mtc |= a_MT_C_CTRLCHAR;

         /* If there is a escape sequence in reverse solidus notation defined
          * for this in ANSI X3.159-1989 (ANSI C89), do not treat it as
          * a control for real.  I.e., \a=\x07=BEL, \b=\x08=BS, \t=\x09=HT.
          * Do not follow libmagic(1) in respect to \v=\x0B=VT.  \f=\x0C=NP; do
          * ignore \e=\x1B=ESC */
         if((c >= '\x07' && c <= '\x0D') || c == '\x1B')
            continue;

         /* As a special case, if we are going for displaying data to the user
          * or quoting a message then simply continue this, in the end, in case
          * we get there, we will decide upon the all_len/all_bogus ratio
          * whether this is usable plain text or not */
         ++mtcap->mtca_all_bogus;
         if(mtc & a_MT_C_DEEP_INSPECT)
            continue;

         mtc |= a_MT_C_HASNUL; /* Force base64 */
         if(!(mtc & a_MT_C_ISTXTCOK)){
            mtc |= a_MT_C_SUGGEST_DONE;
            break;
         }
      }else if(S(u8,c) & 0x80){
         mtc |= a_MT_C_HIGHBIT;
         ++mtcap->mtca_all_highbit;
         if(!(mtc & (a_MT_C_NCTT | a_MT_C_ISTXT))){/* TODO _NCTT?*/
            mtc |= a_MT_C_HASNUL /*base64*/ | a_MT_C_SUGGEST_DONE;
            break;
         }
      }else if(!(mtc & a_MT_C_FROM_) &&
            UCMP(z, curlnlen, <, a_F_SIZEOF)){
         *f_p++ = S(char,c);
         if(UCMP(z, curlnlen, ==, a_F_SIZEOF - 1) &&
               P2UZ(f_p - f_buf) == a_F_SIZEOF &&
               !su_mem_cmp(f_buf, a_F_, a_F_SIZEOF)){
            mtc |= a_MT_C_FROM_;
            if(mtc & a_MT_C__1STLINE)
               mtc |= a_MT_C_FROM_1STLINE;
         }
      }
   }
   if(c == EOF && lastc != '\n')
      mtc |= a_MT_C_NOTERMNL;

   mtcap->mtca_curlnlen = curlnlen;
   /*mtcap->mtca_lastc = lastc*/;
   mtcap->mtca_c = c;
   mtcap->mtca_mtc = mtc;
   mtcap->mtca_all_len = alllen;

   NYD2_OU;
   return mtc;

#undef a_F_
#undef a_F_SIZEOF
}

static enum mx_mime_type
a_mt_classify_o_s_part(BITENUM_IS(u32,a_mt_counter_evidence) mce,
      struct mimepart *mpp, boole deep_inspect){
   struct str in = {NIL, 0}, outrest, inrest, dec;
   struct a_mt_class_arg mtca;
   int lc, c;
   uz cnt, lsz;
   FILE *ibuf;
   long start_off;
   boole did_inrest;
   enum a_mt_class mtc;
   enum mx_mime_type mt;
   NYD2_IN;

   ASSERT(mpp->m_mime_enc != mx_MIME_ENC_BIN);

   outrest = inrest = dec = in;
   mt = mx_MIME_TYPE_UNKNOWN;
   mtc = a_MT_C_NONE;
   did_inrest = FAL0;

   /* TODO v15-compat Note we actually bypass our usual file handling by
    * TODO directly using fseek() on mb.mb_itf -- the v15 rewrite will change
    * TODO all of this, and until then doing it like this is the only option
    * TODO to integrate nicely into whoever calls us */
   if((start_off = ftell(mb.mb_itf)) == -1)
      goto jleave;
   if((ibuf = setinput(&mb, R(struct message*,mpp), NEED_BODY)) == NIL){
jos_leave:
      (void)fseek(mb.mb_itf, start_off, SEEK_SET);
      goto jleave;
   }
   cnt = mpp->m_size;

   /* Skip part headers */
   for(lc = '\0'; cnt > 0; lc = c, --cnt)
      if((c = getc(ibuf)) == EOF || (c == '\n' && lc == '\n'))
         break;
   if(cnt == 0 || ferror(ibuf))
      goto jos_leave;

   /* So now let's inspect the part content, decoding content-transfer-encoding
    * along the way TODO this should simply be "mime_factory_create(MPP)"!
    * TODO In fact m_mime_classifier_(setup|call|call_part|finalize)() and the
    * TODO state(s) should become reported to the outer
    * TODO world like that (see MIME boundary TODO around here) */
   a_mt_classify_init(&mtca, (a_MT_C_ISTXT |
      (deep_inspect ? a_MT_C_DEEP_INSPECT : a_MT_C_NONE)));

   for(lsz = 0;;){
      boole dobuf;

      c = (--cnt == 0) ? EOF : getc(ibuf);
      if((dobuf = (c == '\n'))){
         /* Ignore empty lines */
         if(lsz == 0)
            continue;
      }else if((dobuf = (c == EOF))){
         if(lsz == 0 && outrest.l == 0)
            break;
      }

      if(in.l + 1 >= lsz)
         in.s = su_REALLOC(in.s, lsz += LINESIZE);
      if(c != EOF)
         in.s[in.l++] = S(char,c);
      if(!dobuf)
         continue;

jdobuf:
      switch(mpp->m_mime_enc){
      case mx_MIME_ENC_B64:
         if(!mx_b64_dec_part(&dec, &in, &outrest,
               (did_inrest ? NIL : &inrest))){
            mtca.mtca_mtc = a_MT_C_HASNUL;
            goto jstopit; /* break;break; */
         }
         break;
      case mx_MIME_ENC_QP:
         /* Drin */
         if(!mx_qp_dec_part(&dec, &in, &outrest, &inrest)){
            mtca.mtca_mtc = a_MT_C_HASNUL;
            goto jstopit; /* break;break; */
         }
         if(dec.l == 0 && c != EOF){
            in.l = 0;
            continue;
         }
         break;
      default:
         /* Temporarily switch those two buffers.. */
         dec = in;
         in.s = NIL;
         in.l = 0;
         break;
      }

      mtca.mtca_buf = dec.s;
      mtca.mtca_len = (sz)dec.l;
      if((mtc = a_mt_classify_round(&mtca)) & a_MT_C_SUGGEST_DONE){
         mtc = a_MT_C_HASNUL;
         break;
      }

      if(c == EOF)
         break;
      /* ..and restore switched */
      if(in.s == NIL){
         in = dec;
         dec.s = NIL;
      }
      in.l = dec.l = 0;
   }

   if((in.l = inrest.l) > 0){
      in.s = inrest.s;
      inrest.s = NIL;
      did_inrest = TRU1;
      goto jdobuf;
   }
   if(outrest.l > 0)
      goto jdobuf;

jstopit:
   if(in.s != NIL)
      su_FREE(in.s);
   if(dec.s != NIL)
      su_FREE(dec.s);
   if(outrest.s != NIL)
      su_FREE(outrest.s);
   if(inrest.s != NIL)
      su_FREE(inrest.s);

   /* Restore file position to what caller expects (sic) */
   fseek(mb.mb_itf, start_off, SEEK_SET);

   if(!(mtc & (a_MT_C_HASNUL /*| a_MT_C_CTRLCHAR XXX really? */))){
      /* In that special relaxed case we may very well wave through
       * octet-streams full of control characters, as they do no harm
       * TODO This should be part of m_mime_classifier_finalize() then! */
      if(deep_inspect &&
            mtca.mtca_all_len - mtca.mtca_all_bogus < mtca.mtca_all_len >> 2)
         goto jleave;

      mt = mx_MIME_TYPE_TEXT_PLAIN;
      if(mce & a_MT_CE_ALL_OVWR)
         mpp->m_ct_type_plain = "text/plain";
      if(mce & (a_MT_CE_BIN_OVWR | a_MT_CE_ALL_OVWR))
         mpp->m_ct_type_usr_ovwr = "text/plain";
   }

jleave:
   NYD2_OU;
   return mt;
}

static BITENUM_IS(u32,mx_mime_type_handler_flags)
a_mt_pipe_check(struct mx_mime_type_handler *mthp,
      enum sendaction action){
   char const *cp;
   BITENUM_IS(u32,mx_mime_type_handler_flags) rv_orig, rv;
   NYD2_IN;

   rv_orig = rv = mthp->mth_flags;
   ASSERT((rv & mx_MIME_TYPE_HDL_TYPE_MASK) == mx_MIME_TYPE_HDL_NIL);

   /* Do we have any handler for this part? */
   if(*(cp = mthp->mth_shell_cmd) == '\0')
      goto jleave;
   else if(*cp++ != '?' && cp[-1] != '@'/* v15compat */){
      rv |= mx_MIME_TYPE_HDL_CMD;
      goto jleave;
   }else{
      if(cp[-1] == '@')/* v15compat */
         n_OBSOLETE2(_("*pipe-TYPE/SUBTYPE*+': type markers (and much more) "
            "use ? not @"), mthp->mth_shell_cmd);
      if(*cp == '\0'){
         rv |= mx_MIME_TYPE_HDL_TEXT | mx_MIME_TYPE_HDL_COPIOUSOUTPUT;
         goto jleave;
      }
   }

jnextc:
   switch(*cp){
   case '*': rv |= mx_MIME_TYPE_HDL_COPIOUSOUTPUT; ++cp; goto jnextc;
   case '#': rv |= mx_MIME_TYPE_HDL_NOQUOTE; ++cp; goto jnextc;
   case '&': rv |= mx_MIME_TYPE_HDL_ASYNC; ++cp; goto jnextc;
   case '!': rv |= mx_MIME_TYPE_HDL_NEEDSTERM; ++cp; goto jnextc;
   case '+':
      if(rv & mx_MIME_TYPE_HDL_TMPF)
         rv |= mx_MIME_TYPE_HDL_TMPF_UNLINK;
      rv |= mx_MIME_TYPE_HDL_TMPF;
      ++cp;
      goto jnextc;
   case '=':
      rv |= mx_MIME_TYPE_HDL_TMPF_FILL;
      ++cp;
      goto jnextc;

   case 't':
      switch(rv & mx_MIME_TYPE_HDL_TYPE_MASK){
      case mx_MIME_TYPE_HDL_NIL: /* FALLTHRU */
      case mx_MIME_TYPE_HDL_TEXT: break;
      default:
         cp = N_("only one type-marker can be used");
         goto jerrlog;
      }
      rv |= mx_MIME_TYPE_HDL_TEXT | mx_MIME_TYPE_HDL_COPIOUSOUTPUT;
      ++cp;
      goto jnextc;
   case 'h':
      switch(rv & mx_MIME_TYPE_HDL_TYPE_MASK){
      case mx_MIME_TYPE_HDL_NIL: /* FALLTHRU */
      case mx_MIME_TYPE_HDL_PTF: break;
      default:
         cp = N_("only one type-marker can be used");
         goto jerrlog;
      }
#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
      mthp->mth_ptf = &mx_flthtml_process_main;
      mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s =
            UNCONST(char*,_("Built-in HTML tagsoup filter")));
      rv |= mx_MIME_TYPE_HDL_PTF | mx_MIME_TYPE_HDL_COPIOUSOUTPUT;
      ++cp;
      goto jnextc;
#else
      cp = N_("?h type-marker unsupported (HTML tagsoup filter not built-in)");
      goto jerrlog;
#endif

   case '@':/* v15compat */
      /* FALLTHRU */
   case '?': /* End of flags */
      ++cp;
      /* FALLTHRU */
   default:
      break;
   }
   mthp->mth_shell_cmd = cp;

   /* Implications */
   if(rv & mx_MIME_TYPE_HDL_TMPF_FILL)
      rv |= mx_MIME_TYPE_HDL_TMPF;

   /* Exceptions */
   if(action == SEND_QUOTE || action == SEND_QUOTE_ALL){
      if(rv & mx_MIME_TYPE_HDL_NOQUOTE)
         goto jerr;
      /* Cannot fetch data back from asynchronous process */
      if(rv & mx_MIME_TYPE_HDL_ASYNC)
         goto jerr;
      if(rv & mx_MIME_TYPE_HDL_NEEDSTERM) /* XXX for now */
         goto jerr;
      /* xxx Need copiousoutput, and nothing else (for now) */
      if(!(rv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT))
         goto jerr;
   }

   if(rv & mx_MIME_TYPE_HDL_NEEDSTERM){
      if(rv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT){
         cp = N_("cannot use needsterminal and copiousoutput");
         goto jerr;
      }
      if(rv & mx_MIME_TYPE_HDL_ASYNC){
         cp = N_("cannot use needsterminal and x-mailx-async");
         goto jerr;
      }
      /* needsterminal needs a terminal */
      if(!(n_psonce & n_PSO_INTERACTIVE))
         goto jerr;
   }

   if(rv & mx_MIME_TYPE_HDL_ASYNC){
      if(rv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT){
         cp = N_("cannot use x-mailx-async and copiousoutput");
         goto jerrlog;
      }
      if(rv & mx_MIME_TYPE_HDL_TMPF_UNLINK){
         cp = N_("cannot use x-mailx-async and x-mailx-tmpfile-unlink");
         goto jerrlog;
      }
   }

   if((rv & mx_MIME_TYPE_HDL_TYPE_MASK) != mx_MIME_TYPE_HDL_NIL){
      if(rv & ~(mx_MIME_TYPE_HDL_TYPE_MASK | mx_MIME_TYPE_HDL_COPIOUSOUTPUT |
            mx_MIME_TYPE_HDL_NOQUOTE)){
         cp = N_("?[th] type-markers only support flags * and #");
         goto jerrlog;
      }
   }else
      rv |= mx_MIME_TYPE_HDL_CMD;

jleave:
   mthp->mth_flags = rv;
   NYD2_OU;
   return rv;

jerrlog:
   n_err(_("MIME type handlers: %s\n"), V_(cp));
jerr:
   rv = rv_orig;
   goto jleave;
}

boole
mx_mime_type_is_valid(char const *name, boole t_a_subt,
      boole subt_wildcard_ok){
   char c;
   NYD2_IN;

   if(t_a_subt)
      t_a_subt = TRU1;

   while((c = *name++) != '\0'){
      /* RFC 4288, section 4.2 */
      if(su_cs_is_alnum(c) || c == '!' ||
            c == '#' || c == '$' || c == '&' || c == '.' ||
            c == '+' || c == '-' || c == '^' || c == '_')
         continue;

      if(c == '/'){
         if(t_a_subt != TRU1)
            break;
         t_a_subt = TRUM1;
         continue;
      }

      if(c == '*' && t_a_subt == TRUM1 && subt_wildcard_ok)
         /* Must be last character, then */
         c = *name;
      break;
   }

   NYD2_OU;
   return (c == '\0');
}

boole
mx_mime_type_is_known(char const *name){
   struct a_mt_lookup mtl;
   boole rv;
   NYD_IN;

   rv = (a_mt_by_name(&mtl, name) != NIL);

   NYD_OU;
   return rv;
}

char *
mx_mime_type_classify_filename(char const *name){
   struct a_mt_lookup mtl;
   NYD_IN;

   a_mt_by_filename(&mtl, name, TRU1);

   NYD_OU;
   return mtl.mtl_result;
}

enum conversion
mx_mime_type_classify_file(FILE *fp, char const **content_type,
      char const **charset, boole *do_iconv, boole no_mboxo){
   /* TODO classify once only PLEASE PLEASE PLEASE */
   /* TODO message/rfc822 is special in that it may only be 7bit, 8bit or
    * TODO binary according to RFC 2046, 5.2.1
    * TODO The handling of which is a hack */
   enum conversion c;
   off_t fpsz;
   enum mx_mime_enc menc;
   boole rfc822;
   enum a_mt_class mtc;
   NYD_IN;

   ASSERT(ftell(fp) == 0x0l);

   *do_iconv = FAL0;

   if(*content_type == NIL){
      mtc = a_MT_C_NCTT;
      rfc822 = FAL0;
   }else if(!su_cs_cmp_case_n(*content_type, "text/", 5)){
      mtc = ok_blook(mime_allow_text_controls)
            ? a_MT_C_ISTXT | a_MT_C_ISTXTCOK : a_MT_C_ISTXT;
      rfc822 = FAL0;
   }else if(!su_cs_cmp_case(*content_type, "message/rfc822")){
      mtc = a_MT_C_ISTXT;
      rfc822 = TRU1;
   }else{
      mtc = a_MT_C_CLEAN;
      rfc822 = FAL0;
   }

   menc = mx_mime_enc_target();

   if((fpsz = fsize(fp)) == 0)
      goto j7bit;
   else{
      struct a_mt_class_arg mtca;
      char *buf;

      a_mt_classify_init(&mtca, mtc);
      buf = su_LOFI_ALLOC(BUFFER_SIZE);
      for(;;){
         mtca.mtca_len = fread(buf, sizeof(buf[0]), BUFFER_SIZE, fp);
         mtca.mtca_buf = buf;
         if((mtc = a_mt_classify_round(&mtca)) & a_MT_C_SUGGEST_DONE)
            break;
         if(mtca.mtca_len == 0)
            break;
      }
      su_LOFI_FREE(buf);

      /* TODO ferror(fp) ! */
      rewind(fp);
   }

   if(mtc & a_MT_C_HASNUL){
      menc = mx_MIME_ENC_B64;
      /* XXX Do not overwrite text content-type to allow UTF-16 and such, but
       * XXX only on request; otherwise enforce what file(1)/libmagic(3) would
       * XXX suggest */
      if(mtc & a_MT_C_ISTXTCOK)
         goto jcharset;
      if(mtc & (a_MT_C_NCTT | a_MT_C_ISTXT))
         *content_type = "application/octet-stream";
      goto jleave;
   }

   if(mtc & (a_MT_C_LONGLINES | a_MT_C_CTRLCHAR | a_MT_C_NOTERMNL |
            a_MT_C_FROM_)){
      if(menc != mx_MIME_ENC_B64 && menc != mx_MIME_ENC_QP){
         /* If the user chooses 8bit, and we do not privacy-sign the message,
          * then if encoding would be enforced only because of a ^From_, no */
         if((mtc & (a_MT_C_LONGLINES | a_MT_C_CTRLCHAR |
                  a_MT_C_NOTERMNL | a_MT_C_FROM_)) != a_MT_C_FROM_ || no_mboxo)
            menc = mx_MIME_ENC_QP;
         else{
            ASSERT(menc != mx_MIME_ENC_7B);
            menc = (mtc & a_MT_C_HIGHBIT) ? mx_MIME_ENC_8B : mx_MIME_ENC_7B;
         }
      }
      *do_iconv = ((mtc & a_MT_C_HIGHBIT) != 0);
   }else if(mtc & a_MT_C_HIGHBIT){
      if(mtc & (a_MT_C_NCTT | a_MT_C_ISTXT))
         *do_iconv = TRU1;
   }else
j7bit:
      menc = mx_MIME_ENC_7B;
   if(mtc & a_MT_C_NCTT)
      *content_type = "text/plain";

   /* Not an attachment with specified charset? */
jcharset:
   if(*charset == NIL) /* TODO MIME/send: iter active? iter! else */
      *charset = (mtc & a_MT_C_HIGHBIT) ? mx_mime_charset_iter_or_fallback()
            : ok_vlook(charset_7bit);
jleave:
   /* TODO mime_type_file_classify() should not return conversion */
   if(rfc822){
      if(mtc & a_MT_C_FROM_1STLINE){
         n_err(_("Pre-v15 %s cannot handle message/rfc822 that "
              "indeed is a RFC 4155 MBOX!\n"
            "  Forcing a content-type of application/mbox!\n"),
            n_uagent);
         *content_type = "application/mbox";
         goto jnorfc822;
      }
      c = (menc == mx_MIME_ENC_7B ? CONV_7BIT
            : (menc == mx_MIME_ENC_8B ? CONV_8BIT
            /* May have only 7-bit, 8-bit and binary.  Try to avoid latter */
            : ((mtc & a_MT_C_HASNUL) ? CONV_NONE
            : ((mtc & a_MT_C_HIGHBIT) ? CONV_8BIT : CONV_7BIT))));
   }else
jnorfc822:
      c = (menc == mx_MIME_ENC_7B ? CONV_7BIT
            : (menc == mx_MIME_ENC_8B ? CONV_8BIT
            : (menc == mx_MIME_ENC_QP ? CONV_TOQP : CONV_TOB64)));
   NYD_OU;
   return c;
}

enum mx_mime_type
mx_mime_type_classify_part(struct mimepart *mpp, boole for_user_context){
   /* TODO n_mimetype_classify_part() <-> m_mime_classifier_ with life cycle */
   struct a_mt_lookup mtl;
   boole is_os;
   union {char const *cp; u32 f;} mce;
   char const *ct;
   enum mx_mime_type mc;
   NYD_IN;

   mc = mx_MIME_TYPE_UNKNOWN;
   if((ct = mpp->m_ct_type_plain) == NIL) /* TODO may not */
      ct = su_empty;

   if((mce.cp = ok_vlook(mime_counter_evidence)) != NIL && *mce.cp != '\0'){
      if((su_idec_u32_cp(&mce.f, mce.cp, 0, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED){
         n_err(_("Invalid *mime-counter-evidence* value content\n"));
         is_os = FAL0;
      }else{
         mce.f |= a_MT_CE_SET;
         is_os = !su_cs_cmp_case(ct, "application/octet-stream");

         if(mpp->m_filename != NIL &&
               (is_os || (mce.f & a_MT_CE_ALL_OVWR))){
            if(a_mt_by_filename(&mtl, mpp->m_filename, TRU1) == NIL){
               if(is_os)
                  goto jos_content_check;
            }else if(is_os || su_cs_cmp_case(ct, mtl.mtl_result)){
               if(mce.f & a_MT_CE_ALL_OVWR)
                  mpp->m_ct_type_plain = ct = mtl.mtl_result;
               if(mce.f & (a_MT_CE_BIN_OVWR | a_MT_CE_ALL_OVWR))
                  mpp->m_ct_type_usr_ovwr = ct = mtl.mtl_result;
            }
         }
      }
   }else
      is_os = FAL0;

   if(*ct == '\0' || su_cs_find_c(ct, '/') == NIL) /* Compat with non-MIME */
      mc = mx_MIME_TYPE_TEXT;
   else if(su_cs_starts_with_case(ct, "text/")){
      ct += sizeof("text/") -1;
      if(!su_cs_cmp_case(ct, "plain"))
         mc = mx_MIME_TYPE_TEXT_PLAIN;
      else if(!su_cs_cmp_case(ct, "html"))
         mc = mx_MIME_TYPE_TEXT_HTML;
      else
         mc = mx_MIME_TYPE_TEXT;
   }else if(su_cs_starts_with_case(ct, "message/")){
      ct += sizeof("message/") -1;
      if(!su_cs_cmp_case(ct, "rfc822"))
         mc = mx_MIME_TYPE_822;
      else
         mc = mx_MIME_TYPE_MESSAGE;
   }else if(su_cs_starts_with_case(ct, "multipart/")){
      struct multi_types{
         char mt_name[12];
         enum mx_mime_type mt_mc;
      } const mta[] = {
         {"alternative\0", mx_MIME_TYPE_ALTERNATIVE},
         {"related", mx_MIME_TYPE_RELATED},
         {"digest", mx_MIME_TYPE_DIGEST},
         {"signed", mx_MIME_TYPE_SIGNED},
         {"encrypted", mx_MIME_TYPE_ENCRYPTED}
      }, *mtap;

      for(ct += sizeof("multipart/") -1, mtap = mta;;)
         if(!su_cs_cmp_case(ct, mtap->mt_name)){
            mc = mtap->mt_mc;
            break;
         }else if(++mtap == &mta[NELEM(mta)]){
            mc = mx_MIME_TYPE_MULTI;
            break;
         }
   }else if(su_cs_starts_with_case(ct, "application/")){
      if(is_os)
         goto jos_content_check;
      ct += sizeof("application/") -1;
      if(!su_cs_cmp_case(ct, "pkcs7-mime") ||
            !su_cs_cmp_case(ct, "x-pkcs7-mime"))
         mc = mx_MIME_TYPE_PKCS7;
   }

jleave:
   NYD_OU;
   return mc;

jos_content_check:
   if((mce.f & a_MT_CE_BIN_PARSE) &&
         mpp->m_mime_enc != mx_MIME_ENC_BIN && mpp->m_charset != NIL)
      mc = a_mt_classify_o_s_part(mce.f, mpp, for_user_context);
   goto jleave;
}

BITENUM_IS(u32,mx_mime_type_handler_flags)
mx_mime_type_handler(struct mx_mime_type_handler *mthp,
      struct mimepart const *mpp, enum sendaction action){
#define a__S "pipe-"
#define a__L (sizeof(a__S) -1)

   struct a_mt_lookup mtl;
   char const *es, *cs, *ccp;
   uz el, cl, l;
   char *buf, *cp;
   BITENUM_IS(u32,mx_mime_type_hander_flags) rv, xrv;
   NYD_IN;

   su_mem_set(mthp, 0, sizeof *mthp);
   buf = NIL;
   xrv = rv = mx_MIME_TYPE_HDL_NIL;

   if(action != SEND_QUOTE && action != SEND_QUOTE_ALL &&
         action != SEND_TODISP && action != SEND_TODISP_ALL &&
         action != SEND_TODISP_PARTS)
      goto jleave;

   el = ((es = mpp->m_filename) != NIL &&
         (es = su_cs_rfind_c(es, '.')) != NIL &&
         *++es != '\0') ? su_cs_len(es) : 0;
   cl = ((cs = mpp->m_ct_type_usr_ovwr) != NIL ||
         (cs = mpp->m_ct_type_plain) != NIL) ? su_cs_len(cs) : 0;
   if((l = MAX(el, cl)) == 0)
      /* TODO this should be done during parse time! */
      goto jleave;

   /* We do not pass the flags around, so ensure carrier is up-to-date */
   mthp->mth_flags = rv;

   buf = su_LOFI_ALLOC(a__L + l +1);
   su_mem_copy(buf, a__S, a__L);

   /* I. *pipe-EXTENSION* handlers take precedence.
    * Yes, we really "fail" here for file extensions which clash MIME types */
   if(el > 0){
      su_mem_copy(buf + a__L, es, el +1);
      for(cp = &buf[a__L]; *cp != '\0'; ++cp)
         *cp = su_cs_to_lower(*cp);

      if((mthp->mth_shell_cmd = ccp = n_var_vlook(buf, FAL0)) != NIL){
         rv = a_mt_pipe_check(mthp, action);
         if((rv & mx_MIME_TYPE_HDL_TYPE_MASK) != mx_MIME_TYPE_HDL_NIL)
            goto jleave;
      }
   }

   /* Only MIME Content-Type: to follow, if any */
   if(cl == 0)
      goto jleave;

   su_mem_copy(cp = &buf[a__L], cs, cl +1);
   cs = cp; /* Ensure normalized variant is henceforth used */
   for(; *cp != '\0'; ++cp)
      *cp = su_cs_to_lower(*cp);

   /* II.: *pipe-TYPE/SUBTYPE* */
   if((mthp->mth_shell_cmd = n_var_vlook(buf, FAL0)) != NIL){
      rv = a_mt_pipe_check(mthp, action);
         if((rv & mx_MIME_TYPE_HDL_TYPE_MASK) != mx_MIME_TYPE_HDL_NIL)
            goto jleave;
   }

   /* III. RFC 1524 / Mailcap lookup */
#ifdef mx_HAVE_MAILCAP
   switch(mx_mailcap_handler(mthp, cs, action, mpp)){
   case TRU1:
      rv = mthp->mth_flags;
      goto jleave;
   case TRUM1:
      xrv = mthp->mth_flags; /* "Use at last-resort" handler */
      break;
   default:
      break;
   }
#endif

   /* IV. and final: `mimetype' type-marker extension induced handler */
   if(a_mt_by_name(&mtl, cs) != NIL){
      switch(mtl.mtl_node->mtn_flags & a_MT__TM_MARKMASK){
#ifndef mx_HAVE_FILTER_HTML_TAGSOUP
      case a_MT_TM_SOUP_H:
         break;
#endif
      case a_MT_TM_SOUP_h:
#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
      case a_MT_TM_SOUP_H:
         mthp->mth_ptf = &mx_flthtml_process_main;
         mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s =
               UNCONST(char*,_("Built-in HTML tagsoup filter")));
         rv ^= mx_MIME_TYPE_HDL_NIL | mx_MIME_TYPE_HDL_PTF;
         goto jleave;
#endif
         /* FALLTHRU */
      case a_MT_TM_PLAIN:
         mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s =
               UNCONST(char*,_("Plain text")));
         rv ^= mx_MIME_TYPE_HDL_NIL | mx_MIME_TYPE_HDL_TEXT;
         goto jleave;
      case a_MT_TM_QUIET:
         mthp->mth_msg.l = 0;
         mthp->mth_msg.s = UNCONST(char*,su_empty);
         goto jleave;
      default:
         break;
      }
   }

   /* Last-resort, anyone? */
   if(xrv != mx_MIME_TYPE_HDL_NIL)
      rv = xrv;

jleave:
   if(buf != NIL)
      su_LOFI_FREE(buf);

   xrv = rv;
   if((rv &= mx_MIME_TYPE_HDL_TYPE_MASK) == mx_MIME_TYPE_HDL_NIL){
      if(mthp->mth_msg.s == NIL)
         mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s = UNCONST(char*,
               A_("[-- No MIME handler installed, or not applicable --]\n")));
   }else if(rv == mx_MIME_TYPE_HDL_CMD &&
         !(xrv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT) &&
         action != SEND_TODISP_PARTS){
      mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s = UNCONST(char*,
            _("[-- Use the command `mimeview' to display this --]\n")));
      xrv &= ~mx_MIME_TYPE_HDL_TYPE_MASK;
      xrv |= (rv = mx_MIME_TYPE_HDL_MSG);
   }
   mthp->mth_flags = xrv;

   NYD_OU;
   return rv;

#undef a__L
#undef a__S
}

int
c_mimetype(void *vp){
   struct n_string s_b, *s;
   struct a_mt_node *mtnp;
   char **argv;
   NYD_IN;

   if(!a_mt_is_init)
      a_mt_init();

   s = n_string_creat_auto(&s_b);

   if(*(argv = vp) == NIL){
      FILE *fp;
      uz l;

      if(a_mt_list == NIL){
         fprintf(n_stdout,
            _("# `mimetype': no mime.type(5) data available\n"));
         goto jleave;
      }

      if((fp = mx_fs_tmp_open("mimetype", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
               mx_FS_O_REGISTER), NIL)) == NIL){
         n_perr(_("tmpfile"), 0);
         fp = n_stdout;
      }

      s = n_string_reserve(s, 63);

      for(l = 0, mtnp = a_mt_list; mtnp != NIL; ++l, mtnp = mtnp->mtn_next){
         char const *cp;

         s = n_string_trunc(s, 0);

         switch(mtnp->mtn_flags & a_MT__TM_MARKMASK){
         case a_MT_TM_PLAIN: cp = "?t "; break;
         case a_MT_TM_SOUP_h: cp = "?h "; break;
         case a_MT_TM_SOUP_H: cp = "?H "; break;
         case a_MT_TM_QUIET: cp = "?q "; break;
         default: cp = NIL; break;
         }
         if(cp != NIL)
            s = n_string_push_cp(s, cp);

         if((mtnp->mtn_flags & a_MT__TMASK) != a_MT_OTHER)
            s = n_string_push_cp(s,
                  a_mt_names[mtnp->mtn_flags &a_MT__TMASK]);

         s = n_string_push_buf(s, mtnp->mtn_line, mtnp->mtn_len);
         s = n_string_push_c(s, ' ');
         s = n_string_push_c(s, ' ');
         s = n_string_push_cp(s, &mtnp->mtn_line[mtnp->mtn_len]);

         fprintf(fp, "mimetype %s%s\n", n_string_cp(s),
            ((n_poption & n_PO_D_V) == 0 ? su_empty
               : (mtnp->mtn_flags & a_MT_USR ? " # user"
               : (mtnp->mtn_flags & a_MT_SYS ? " # system"
               : (mtnp->mtn_flags & a_MT_FSPEC ? " # f= file"
               : (mtnp->mtn_flags & a_MT_CMD ? " # command"
               : " # built-in"))))));
       }

      if(fp != n_stdout){
         page_or_print(fp, l);

         mx_fs_close(fp);
      }else
         clearerr(fp);
   }else{
      for(; *argv != NIL; ++argv){
         if(s->s_len > 0)
            s = n_string_push_c(s, ' ');
         s = n_string_push_cp(s, *argv);
      }

      mtnp = a_mt_create(TRU1, a_MT_CMD, n_string_cp(s), s->s_len);
      if(mtnp != NIL){
         mtnp->mtn_next = a_mt_list;
         a_mt_list = mtnp;
      }else
         vp = NIL;
   }

jleave:
   NYD_OU;
   return (vp == NIL ? n_EXIT_ERR : n_EXIT_OK);
}

int
c_unmimetype(void *vp){
   boole match;
   struct a_mt_node *lnp, *mtnp;
   char **argv;
   NYD_IN;

   argv = vp;

   /* Need to load that first as necessary */
   if(!a_mt_is_init)
      a_mt_init();

   for(; *argv != NIL; ++argv){
      if(!su_cs_cmp_case(*argv, "reset")){
         a_mt_is_init = FAL0;
         goto jdelall;
      }

      if(argv[0][0] == '*' && argv[0][1] == '\0'){
jdelall:
         while((mtnp = a_mt_list) != NIL){
            a_mt_list = mtnp->mtn_next;
            su_FREE(mtnp);
         }
         continue;
      }

      for(match = FAL0, lnp = NIL, mtnp = a_mt_list; mtnp != NIL;){
         char *val;
         uz i;
         char const *typ;

         if((mtnp->mtn_flags & a_MT__TMASK) == a_MT_OTHER){
            typ = su_empty;
            i = 0;
         }else{
            typ = a_mt_names[mtnp->mtn_flags & a_MT__TMASK];
            i = su_cs_len(typ);
         }

         val = n_lofi_alloc(i + mtnp->mtn_len +1);
         su_mem_copy(val, typ, i);
         su_mem_copy(&val[i], mtnp->mtn_line, mtnp->mtn_len);
         val[i += mtnp->mtn_len] = '\0';
         i = su_cs_cmp_case(val, *argv);
         n_lofi_free(val);

         if(!i){
            struct a_mt_node *nnp;

            nnp = mtnp->mtn_next;
            if(lnp == NIL)
               a_mt_list = nnp;
            else
               lnp->mtn_next = nnp;
            su_FREE(mtnp);
            mtnp = nnp;
            match = TRU1;
         }else
            lnp = mtnp, mtnp = mtnp->mtn_next;
      }

      if(!match){
         if(!(n_pstate & n_PS_ROBOT) || (n_poption & n_PO_D_V))
            n_err(_("No such MIME type: %s\n"), n_shexp_quote_cp(*argv, FAL0));
         vp = NIL;
      }
   }

   NYD_OU;
   return (vp == NIL ? n_EXIT_ERR : n_EXIT_OK);
}

#include "su/code-ou.h"
/* s-it-mode */
