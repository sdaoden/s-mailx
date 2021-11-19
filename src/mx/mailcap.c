/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mailcap.h.
 *@ TODO - We do not support the additional formats '%n' and '%F' (manual!).
 *@ TODO - We do not support handlers for multipart MIME parts (manual!).
 *@ TODO - We only support viewing/quoting (+ implications on fmt expansion).
 *@ TODO - With an on_loop_tick_event, trigger cache update once per loop max.
 *@ TODO   (or, if we ever get there, use a path_monitor: for all such!)
 *
 * Copyright (c) 2019 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE mailcap
#define mx_SOURCE
#define mx_SOURCE_MAILCAP

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_MAILCAP
#include "su/cs.h"
#include "su/cs-dict.h"
#include "su/mem.h"
#include "su/mem-bag.h"

#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/file-streams.h"
#include "mx/mime-param.h"
#include "mx/mime-type.h"

#include "mx/mailcap.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Whether we should try to place as much on a line as possible (Y).
 * Otherwise (X) we place commands on lines of their own */
#define a_MAILCAP_DUMP_SEP_INJ(X,Y) X /* Y */

/* Dictionary stores a_mailcap_hdl* list, not owned */
#define a_MAILCAP_CSD_FLAGS (su_CS_DICT_CASE | su_CS_DICT_HEAD_RESORT |\
      su_CS_DICT_ERR_PASS)
#define a_MAILCAP_CSD_TRESHOLD_SHIFT 3

/* Must be alphabetical */
enum a_mailcap_sfields{
   a_MAILCAP_SF_CMD,
   a_MAILCAP_SF_COMPOSE,
   a_MAILCAP_SF_COMPOSETYPED,
   a_MAILCAP_SF_DESCRIPTION,
   a_MAILCAP_SF_EDIT,
   a_MAILCAP_SF_NAMETEMPLATE,
   a_MAILCAP_SF_PRINT,
   a_MAILCAP_SF_TEST,
   a_MAILCAP_SF_X11_BITMAP
};
CTAV(a_MAILCAP_SF_CMD == 0);
enum {a_MAILCAP_SF_MAX = a_MAILCAP_SF_X11_BITMAP + 1};

/* sfields we really handle, less test (no format expansion in there) */
#define a_MAILCAP_SFIELD_SUPPORTED(X) \
   ((X) == a_MAILCAP_SF_CMD /*|| (X) == a_MAILCAP_SF_TEST*/)

enum a_mailcap_handler_flags{
   a_MAILCAP_F_TEXTUALNEWLINES = mx_MIME_TYPE_HDL_MAX << 1,
   a_MAILCAP_F_TEST_ONCE = mx_MIME_TYPE_HDL_MAX << 2,
   a_MAILCAP_F_TEST_ONCE_DONE = mx_MIME_TYPE_HDL_MAX << 3,
   a_MAILCAP_F_TEST_ONCE_SUCCESS = mx_MIME_TYPE_HDL_MAX << 4,
   a_MAILCAP_F_HAS_S_FORMAT = mx_MIME_TYPE_HDL_MAX << 5, /* Somewhere a %s */
   a_MAILCAP_F_LAST_RESORT = mx_MIME_TYPE_HDL_MAX << 6,
   a_MAILCAP_F_IGNORE = mx_MIME_TYPE_HDL_MAX << 7
};
enum {a_MAILCAP_F_MAX = a_MAILCAP_F_IGNORE};
CTA(a_MAILCAP_F_MAX <= S32_MAX,
   "a_mailcap_hdl.mch_flags bit range excessed");

struct a_mailcap_hdl{
   struct a_mailcap_hdl *mch_next;
   BITENUM_IS(u32,a_mailcap_handler_flags) mch_flags;
   u8 mch__pad[2];
   /* All strings are placed in successive memory after "self".
    * Since mch_cmd always exists 0 is the invalid offset for the rest.
    * The sum of all strings fits in S32_MAX.
    * sfield_has_format is a bitset */
   u16 mch_sfield_has_format;
   u32 mch_sfields[a_MAILCAP_SF_MAX];
};

struct a_mailcap_load_stack{
   char const *mcls_name;
   char const *mcls_name_quoted; /* Messages somewhat common, just do it. */
   FILE *mcls_fp;
   char const *mcls_type_subtype;
   struct str mcls_dat;
   struct str mcls_conti_dat;
   uz mcls_conti_len;
   /* Preparated handler; during preparation string data is temporarily stored
    * in .mcls_hdl_buf */
   struct a_mailcap_hdl mcls_hdl;
   struct n_string mcls_hdl_buf;
};

struct a_mailcap_flags{
   BITENUM_IS(u32,a_mailcap_handler_flags) mcf_flags;
   char mcf_name[28];
};

static struct a_mailcap_flags const a_mailcap_flags[] = {
   /* In manual order */
   {mx_MIME_TYPE_HDL_COPIOUSOUTPUT, "copiousoutput"},
   {mx_MIME_TYPE_HDL_NEEDSTERM, "needsterminal"},
   {a_MAILCAP_F_TEXTUALNEWLINES, "textualnewlines"},
   {mx_MIME_TYPE_HDL_ASYNC, "x-mailx-async"},
   {mx_MIME_TYPE_HDL_NOQUOTE, "x-mailx-noquote"},
   {a_MAILCAP_F_TEST_ONCE, "x-mailx-test-once"},
   {mx_MIME_TYPE_HDL_TMPF, "x-mailx-tmpfile"},
   {mx_MIME_TYPE_HDL_TMPF_FILL, "x-mailx-tmpfile-fill"},
   {mx_MIME_TYPE_HDL_TMPF_UNLINK, "x-mailx-tmpfile-unlink\0"},
   {a_MAILCAP_F_LAST_RESORT, "x-mailx-last-resort"},
   {a_MAILCAP_F_IGNORE, "x-mailx-ignore"}
};

static struct su_cs_dict *a_mailcap_dp, a_mailcap__d; /* XXX atexit _gut() */

/* We stop parsing and _gut(FAL0) on hard errors like NOMEM, OVERFLOW and IO.
 * The __parse*() series return is-error, with TRUM1 being a fatal one */
static void a_mailcap_create(void);

static boole a_mailcap__load_file(struct a_mailcap_load_stack *mclsp);
static boole a_mailcap__parse_line(struct a_mailcap_load_stack *mclsp,
      char *dp, uz dl);
static boole a_mailcap__parse_kv(struct a_mailcap_load_stack *mclsp,
      char *kp, char *vp);
static boole a_mailcap__parse_value(u32 sfield,
      struct a_mailcap_load_stack *mclsp, struct str *s);
static boole a_mailcap__parse_flag(struct a_mailcap_load_stack *mclsp,
      char *flag);
static boole a_mailcap__parse_create_hdl(struct a_mailcap_load_stack *mclsp,
      struct a_mailcap_hdl **ins_or_nil);

static void a_mailcap_gut(boole gut_dp);

/* */
static struct n_strlist *a_mailcap_dump(char const *cmdname, char const *key,
      void const *dat);

static void a_mailcap__dump_kv(u32 sfield, struct n_string *s, uz *llp,
      char const *pre, char const *vp);
static struct n_string *a_mailcap__dump_quote(struct n_string *s,
      char const *cp, boole quotequote);

/* Expand a command string with embedded formats */
static char const *a_mailcap_expand_formats(char const *format,
      struct mimepart const *mpp, char const *ct);

static void
a_mailcap_create(void){
   struct a_mailcap_load_stack mcls;
   char *cp_base, *cp;
   NYD_IN;

   a_mailcap_dp = su_cs_dict_set_treshold_shift(
            su_cs_dict_create(&a_mailcap__d, a_MAILCAP_CSD_FLAGS, NIL),
         a_MAILCAP_CSD_TRESHOLD_SHIFT);

   if(*(cp_base = UNCONST(char*,ok_vlook(MAILCAPS))) == '\0')
      goto jleave;

   su_mem_set(&mcls, 0, sizeof mcls);
   mx_fs_linepool_aquire(&mcls.mcls_dat.s, &mcls.mcls_dat.l);
   n_string_book(n_string_creat(&mcls.mcls_hdl_buf), 248); /* ovflw not yet */

   for(cp_base = savestr(cp_base);
         (cp = su_cs_sep_c(&cp_base, ':', TRU1)) != NIL;){
      if((cp = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
            ) == NIL)
         continue;

      mcls.mcls_name_quoted = n_shexp_quote_cp(cp, FAL0);
      if((mcls.mcls_fp = mx_fs_open(mcls.mcls_name = cp, mx_FS_O_RDONLY)
            ) == NIL){
         s32 eno;

         if((eno = su_err_no()) != su_ERR_NOENT)
            n_err(_("$MAILCAPS: cannot open %s: %s\n"),
               mcls.mcls_name_quoted, su_err_doc(eno));
         continue;
      }

      if(!a_mailcap__load_file(&mcls))
         cp = NIL;

      mx_fs_close(mcls.mcls_fp);

      if(cp == NIL){
         a_mailcap_gut(FAL0);
         break;
      }
   }

   if(mcls.mcls_conti_dat.s != NIL)
      mx_fs_linepool_release(mcls.mcls_conti_dat.s, mcls.mcls_conti_dat.l);
   mx_fs_linepool_release(mcls.mcls_dat.s, mcls.mcls_dat.l);

   n_string_gut(&mcls.mcls_hdl_buf);

jleave:
   NYD_OU;
}

static boole
a_mailcap__load_file(struct a_mailcap_load_stack *mclsp){
   enum{a_NONE, a_CONTI = 1u<<0, a_EOF = 1u<<1, a_NEWCONTI = 1u<<2};
   char const *emsg;
   uz len;
   u32 f;
   NYD2_IN;

   emsg = NIL;

   for(f = a_NONE;;){
      if(fgetline(&mclsp->mcls_dat.s, &mclsp->mcls_dat.l, NIL, &len,
            mclsp->mcls_fp, TRU1) == NIL){
         if(ferror(mclsp->mcls_fp)){
            emsg = N_("I/O error");
            goto jerr;
         }
         f |= a_EOF;
         if(f & a_CONTI)
            goto jconti_do;
         break;
      }
      ASSERT(len > 0);
      mclsp->mcls_dat.s[--len] = '\0';

      /* Is it a comment?  Must be in first column, cannot be continued */
      if(!(f & a_CONTI) && len > 0 && mclsp->mcls_dat.s[0] == '#')
         continue;

      /* Is it a continuation line?  It really is for an uneven number of \ */
      f &= ~a_NEWCONTI;
      if(len > 0 && mclsp->mcls_dat.s[len - 1] == '\\'){
         uz i, j;

         if(len == 1)
            continue;
         else for(j = 1, i = len - 1; i-- > 0; ++j)
            if(mclsp->mcls_dat.s[i] != '\\'){
               if(j & 1){
                  f |= a_NEWCONTI;
                  --len;
               }
               break;
            }
      }

      /* Necessary to create/append to continuation line storage? */
      if(f & (a_CONTI | a_NEWCONTI)){
         if(mclsp->mcls_conti_dat.s == NIL){
            mclsp->mcls_conti_dat = mclsp->mcls_dat;
            mclsp->mcls_conti_len = len;
            mx_fs_linepool_aquire(&mclsp->mcls_dat.s, &mclsp->mcls_dat.l);
         }else{
            if(!mx_fs_linepool_book(&mclsp->mcls_conti_dat.s,
                  &mclsp->mcls_conti_dat.l, mclsp->mcls_conti_len,
                  MAX(len, 256)))
               goto jetoolong;
            su_mem_copy(&mclsp->mcls_conti_dat.s[mclsp->mcls_conti_len],
               mclsp->mcls_dat.s, len +1);
            mclsp->mcls_conti_len += len;
         }
         f |= a_CONTI;

         if(f & a_NEWCONTI)
            continue;

jconti_do:
         /* C99 */{
            boole x;

            x = a_mailcap__parse_line(mclsp, mclsp->mcls_conti_dat.s,
                  mclsp->mcls_conti_len);
            /* Release the buffer to the linepool, like that we can swap it in
             * again the next time, shall this be necessary! */
            mx_fs_linepool_release(mclsp->mcls_conti_dat.s,
               mclsp->mcls_conti_dat.l);
            mclsp->mcls_conti_dat.s = NIL;

            switch(x){
            case FAL0: break;
            case TRU1: break;
            case TRUM1: goto jenomem;
            }
         }
         if((f ^= a_CONTI) & a_EOF)
            break;
      }else switch(a_mailcap__parse_line(mclsp, mclsp->mcls_dat.s, len)){
      case FAL0: break;
      case TRU1: break;
      case TRUM1: goto jenomem;
      }
   }

jleave:
   NYD2_OU;
   return (emsg == NIL);

jenomem:
   emsg = N_("out of memory");
   goto jerr;
jetoolong:
   su_state_err(su_STATE_ERR_OVERFLOW, (su_STATE_ERR_PASS |
      su_STATE_ERR_NOERRNO), _("$MAILCAPS: line too long"));
   emsg = N_("line too long");
jerr:
   n_err(_("$MAILCAPS: %s while loading %s\n"),
      V_(emsg), mclsp->mcls_name_quoted);
   goto jleave;
}

static boole
a_mailcap__parse_line(struct a_mailcap_load_stack *mclsp, char *dp, uz dl){
   struct str s;
   union {void *v; struct a_mailcap_hdl *mch; struct a_mailcap_hdl **pmch;} p;
   char *cp, *cp2, *key;
   uz rnd;
   boole rv;
   NYD2_IN;

   su_mem_set(&mclsp->mcls_hdl, 0, sizeof(mclsp->mcls_hdl));
   n_string_trunc(&mclsp->mcls_hdl_buf, 0);

   rv = TRU1;

   if(dl == 0)
      goto jleave;

   s.s = dp;
   s.l = dl;
   if(n_str_trim(&s, n_STR_TRIM_BOTH)->l == 0){
      if(n_poption & n_PO_D_V)
         n_err(_("$MAILCAPS: %s: line empty after whitespace removal "
               "(invalid RFC 1524 syntax)\n"), mclsp->mcls_name_quoted);
      goto jleave;
   }else if(s.l >= S32_MAX){
      /* As stated, the sum must fit in S32_MAX */
      rv = TRUM1;
      goto jleave;
   }
   (dp = s.s)[s.l] = '\0';

   rnd = 0;
   UNINIT(key, NIL);
   UNINIT(p.v, NIL);

   while((cp = su_cs_sep_escable_c(&dp, ';', FAL0)) != NIL){
      /* We do not allow empty fields, but there may be a trailing semicolon */
      if(*cp == '\0' && dp == NIL)
         break;

      /* First: TYPE/SUBTYPE; separate them first */
      if(++rnd == 1){
         if(*cp == '\0'){
            key = UNCONST(char*,N_("no MIME TYPE"));
            goto jerr;
         }else if((cp2 = su_cs_find_c(cp, '/')) == NIL){
jesubtype:
            n_err(_("$MAILCAPS: %s: missing SUBTYPE, assuming /* (any): %s\n"),
               mclsp->mcls_name_quoted, cp);
            cp2 = UNCONST(char*,n_star);
         }else{
            *cp2++ = '\0';
            if(*cp2 == '\0')
               goto jesubtype;
         }

         /* And unite for the real one */
         key = savecatsep(cp, '/', cp2);
         if(!mx_mime_type_is_valid(key, TRU1, TRU1)){
            cp = key;
            key = UNCONST(char*,N_("invalid MIME type"));
            goto jerr;
         }
         mclsp->mcls_type_subtype = key;

         if((p.v = su_cs_dict_lookup(a_mailcap_dp, key)) != NIL){
            while(p.mch->mch_next != NIL)
               p.mch = p.mch->mch_next;
            p.pmch = &p.mch->mch_next;
         }
      }
      /* The mandatory view command */
      else if(rnd == 2){
         s.l = su_cs_len(s.s = cp);
         n_str_trim(&s, n_STR_TRIM_BOTH);
         if((rv = a_mailcap__parse_value(a_MAILCAP_SF_CMD, mclsp, &s)))
            goto jleave;
      }
      /* An optional field */
      else{
         if(*cp == '\0'){
            if(n_poption & n_PO_D_V)
               n_err(_("$MAILCAPS: %s: ignoring empty optional field: %s\n"),
                  mclsp->mcls_name_quoted, key);
         }else if((cp2 = su_cs_find_c(cp, '=')) != NIL){
            *cp2++ = '\0';
            if((rv = a_mailcap__parse_kv(mclsp, cp, cp2)))
               goto jleave;
         }else if(a_mailcap__parse_flag(mclsp, cp))
            goto jleave;
      }
   }

   rv = a_mailcap__parse_create_hdl(mclsp, p.pmch);

jleave:
   NYD2_OU;
   return rv;

jerr:
   n_err(_("$MAILCAPS: %s: skip due to error: %s: %s\n"),
      mclsp->mcls_name_quoted, V_(key), cp);
   rv = TRU1;
   goto jleave;
}

static boole
a_mailcap__parse_kv(struct a_mailcap_load_stack *mclsp, char *kp, char *vp){
#undef a_X
#define a_X(X,Y) FIELD_INITI(CONCAT(a_MAILCAP_SF_, X) - 1) STRING(Y)
   static char const sfa[][16] = {
      a_X(COMPOSE, compose),
      a_X(COMPOSETYPED, composetyped),
      a_X(DESCRIPTION, description),
      a_X(EDIT, edit),
      a_X(NAMETEMPLATE, nametemplate),
      a_X(PRINT, print),
      a_X(TEST, test),
      a_X(X11_BITMAP, x11-bitmap)
   };
#undef a_X

   struct str s;
   char **cpp;
   char const *emsg, (*sfapp)[16];
   boole rv;
   NYD2_IN;

   /* Trim key and value */
   rv = TRU1;
   emsg = R(char*,1);
   cpp = &kp;
jredo:
   s.l = su_cs_len(s.s = *cpp);
   if(n_str_trim(&s, n_STR_TRIM_BOTH)->l == 0){
      emsg = (emsg == R(char*,1)) ? N_("ignored: empty key")
            : N_("ignored: empty value");
      goto jerr;
   }
   (*cpp = s.s)[s.l] = '\0';

   if(emsg == R(char*,1)){
      emsg = R(char*,-1);
      cpp = &vp;
      goto jredo;
   }

   emsg = NIL;

   /* Find keyword */
   for(sfapp = &sfa[0]; sfapp < &sfa[NELEM(sfa)]; ++sfapp){
      if(!su_cs_cmp_case(kp, *sfapp)){
         uz i;

         ASSERT(s.s == vp);
         i = P2UZ(sfapp - &sfa[0]) + 1;

         if((n_poption & n_PO_D_V) && mclsp->mcls_hdl.mch_sfields[i] != 0)
            n_err(_("$MAILCAPS: %s: %s: multiple %s fields\n"),
               mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, kp);

         switch(i){
         default:
            break;
         case a_MAILCAP_SF_DESCRIPTION:
            /* This is "optionally quoted" */
            if(*vp == '"'){
               s.s = ++vp;
               --s.l;
               if(s.s[s.l - 1] == '"')
                  s.s[--s.l] = '\0';
               else
                  emsg = N_("unclosed quoted description");
            }
            break;

         case a_MAILCAP_SF_NAMETEMPLATE:{
            char *cp, c;

            if((cp = vp)[0] != '%' || cp[1] != 's'){
jentempl:
               emsg = N_("unsatisfied constraints, ignoring nametemplate");
               goto jerr;
            }
            for(cp += 2; (c = *cp++) != '\0';)
               if(!su_cs_is_alnum(c) && c != '_' && c != '.')
                  goto jentempl;
            }
            break;
         }

         mclsp->mcls_hdl.mch_sfields[i] = mclsp->mcls_hdl_buf.s_len;
         if((rv = a_mailcap__parse_value(i, mclsp, &s)))
            mclsp->mcls_hdl.mch_sfields[i] = 0;

         if(emsg != NIL)
            goto jerr;
         goto jleave;
      }
   }

   if((rv = (kp[0] != 'x' && kp[0] != '\0' && kp[1] != '-')) ||
         (n_poption & n_PO_D_V)){
      emsg = N_("ignored unknown string/command");
      goto jerr;
   }

   rv = FAL0;
jleave:
   NYD2_OU;
   return rv;

jerr:
   /* I18N: FILENAME: TYPE/SUBTYPE: ERROR MSG: key = value */
   n_err(_("$MAILCAPS: %s: %s: %s: %s = %s\n"),
      mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, V_(emsg), kp, vp);
   goto jleave;
}

static boole
a_mailcap__parse_value(u32 sfield, struct a_mailcap_load_stack *mclsp,
      struct str *s){
   char *cp2, *cp3, c;
   boole rv;
   NYD2_IN;

   if(S(uz,S32_MAX) - mclsp->mcls_hdl_buf.s_len <= s->l +1){
      rv = TRUM1;
      goto jleave;
   }

   rv = FAL0;

   /* Take over unless we see a format, then branch to more expensive code */
   for(cp2 = cp3 = s->s;;){
      c = *cp2++;
      if(c == '\\')
         c = *cp2++;
      else if(c == '%'){
         if(*cp2 == '%')
            ++cp2;
         else{
            --cp2;
            goto jfmt;
         }
      }

      if((*cp3++ = c) == '\0')
         break;
   }

   n_string_push_c(n_string_push_buf(&mclsp->mcls_hdl_buf,
      s->s, P2UZ(cp3 - s->s)), '\0');

jleave:
   NYD2_OU;
   return rv;

jfmt:{
   char *lbuf;

   /* C99 */{
      uz i;

      lbuf = su_LOFI_ALLOC(s->l * 2);
      i = P2UZ(cp3 - s->s);
      su_mem_copy(lbuf, s->s, i);
      cp3 = &lbuf[i];
   }

   for(;;){
      c = *cp2++;
      if(c == '\\')
         c = *cp2++;
      else if(c == '%'){
         switch((c = *cp2++)){
         case '{':
            if(su_cs_find_c(cp2, '}') == NIL){
               n_err(_("$MAILCAPS: %s: %s: unclosed %%{ format: %s\n"),
                  mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, s->s);
               /* We need to skip the entire thing if we are incapable to
                * satisfy the format request when invoking a command */
               if(a_MAILCAP_SFIELD_SUPPORTED(sfield)){
                  rv = TRU1;
                  goto jfmt_leave;
               }
               goto jquote;
            }
            /* FALLTHRU */
            if(0){
         case 's':
               if(sfield == a_MAILCAP_SF_TEST){ /* XXX only view/quote */
                  n_err(_("$MAILCAPS: %s: %s: %%s format cannot be used in "
                        "the \"test\" field\n"),
                     mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, s->s);
                  rv = TRU1;
                  goto jfmt_leave;
               }
               /* xxx Very primitive user-used-false-quotes check */
               if((n_poption & n_PO_D_V) && (*cp2 == '"' || *cp2 == '\''))
                  n_err(_("$MAILCAPS: %s: %s: (maybe!) "
                        "%%s must not be quoted: %s\n"),
                     mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, s->s);

               mclsp->mcls_hdl.mch_flags |= a_MAILCAP_F_HAS_S_FORMAT;
            }
            /* FALLTHRU */
         case 't':
            mclsp->mcls_hdl.mch_sfield_has_format |= 1u << sfield;
            *cp3++ = '\0';
            break;

         case 'n':
            /* FALLTHRU */
         case 'F':
            n_err(_("$MAILCAPS: %s: %s: unsupported format %%%c\n"),
               mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, c);
            /* We need to skip the entire thing if we are incapable to satisfy
             * the format request when invoking a command */
            if(a_MAILCAP_SFIELD_SUPPORTED(sfield)){
               rv = TRU1;
               goto jfmt_leave;
            }
            /* Since it is actually ignored, do not care, do not quote */
            mclsp->mcls_hdl.mch_sfield_has_format |= 1u << sfield;
            *cp3++ = '\0';
            break;

         default:
            n_err(_("$MAILCAPS: %s: %s: invalid format %%%c, "
                  "should be quoted: \\%%%c\n"),
               mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, c, c);
jquote:
            --cp2;
            c = '%';
            break;
         }
      }

      if((*cp3++ = c) == '\0')
         break;
   }

   n_string_push_c(n_string_push_buf(&mclsp->mcls_hdl_buf,
      lbuf, P2UZ(cp3 - lbuf)), '\0');

jfmt_leave:
   su_LOFI_FREE(lbuf);
   }
   goto jleave;
}

static boole
a_mailcap__parse_flag(struct a_mailcap_load_stack *mclsp, char *flag){
   struct a_mailcap_flags const *fap;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   for(fap = &a_mailcap_flags[0];
         fap < &a_mailcap_flags[NELEM(a_mailcap_flags)]; ++fap)
      if(!su_cs_cmp_case(flag, fap->mcf_name)){
         if((n_poption & n_PO_D_V) &&
               (mclsp->mcls_hdl.mch_flags & fap->mcf_flags))
            n_err(_("$MAILCAPS: %s: %s: multiple %s flags\n"),
               mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, flag);
         mclsp->mcls_hdl.mch_flags |= fap->mcf_flags;
         goto jleave;
      }

   if((rv = (flag[0] != 'x' && flag[0] != '\0' && flag[1] != '-')) ||
         (n_poption & n_PO_D_V))
      n_err(_("$MAILCAPS: %s: %s: ignored unknown flag: %s\n"),
         mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, flag);

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_mailcap__parse_create_hdl(struct a_mailcap_load_stack *mclsp,
      struct a_mailcap_hdl **ins_or_nil){
   struct a_mailcap_hdl *mchp;
   char const *emsg;
   BITENUM_IS(u32,a_mailcap_handler_flags) f;
   boole rv;
   NYD2_IN;

   rv = TRU1;

   /* Flag implications */

   f = mclsp->mcls_hdl.mch_flags;

   if(f & (mx_MIME_TYPE_HDL_TMPF_FILL | mx_MIME_TYPE_HDL_TMPF_UNLINK))
      f |= mx_MIME_TYPE_HDL_TMPF;

   if(f & mx_MIME_TYPE_HDL_ASYNC){
      if(f & mx_MIME_TYPE_HDL_COPIOUSOUTPUT){
         emsg = N_("cannot use x-mailx-async and copiousoutput");
         goto jerr;
      }
      if(f & mx_MIME_TYPE_HDL_TMPF_UNLINK){
         emsg = N_("cannot use x-mailx-async and x-mailx-tmpfile-unlink");
         goto jerr;
      }
   }

   if(f & mx_MIME_TYPE_HDL_NEEDSTERM){
      if(f & mx_MIME_TYPE_HDL_COPIOUSOUTPUT){
         emsg = N_("cannot use needsterminal and copiousoutput");
         goto jerr;
      }
      if(f & mx_MIME_TYPE_HDL_ASYNC){
         emsg = N_("cannot use needsterminal and x-mailx-async");
         goto jerr;
      }
   }

   /* Mailcap implications */

   if(mclsp->mcls_hdl.mch_sfields[a_MAILCAP_SF_NAMETEMPLATE] != 0){
      if(f & a_MAILCAP_F_HAS_S_FORMAT)
         f |= mx_MIME_TYPE_HDL_TMPF_NAMETMPL |
               mx_MIME_TYPE_HDL_TMPF_NAMETMPL_SUFFIX;
      else
         n_err(_("$MAILCAPS: %s: %s: no %%s format, ignoring nametemplate\n"),
            mclsp->mcls_name_quoted, mclsp->mcls_type_subtype);
   }

   if(f & mx_MIME_TYPE_HDL_TMPF){
      /* Not with any %s */
      if(f & a_MAILCAP_F_HAS_S_FORMAT){
         emsg = N_("cannot use x-mailx-tmpfile if formats use %s");
         goto jerr;
      }
   }

   mclsp->mcls_hdl.mch_flags = f;

   /* Since all strings altogether fit in S32_MAX, allocate one big chunk */
   rv = TRUM1;

   mchp = S(struct a_mailcap_hdl*,su_CALLOC(sizeof(*mchp) +
            mclsp->mcls_hdl_buf.s_len +1));
   if(mchp == NIL)
      goto jleave;

   su_mem_copy(mchp, &mclsp->mcls_hdl, sizeof(mclsp->mcls_hdl));
   su_mem_copy(&mchp[1], n_string_cp(&mclsp->mcls_hdl_buf),
      mclsp->mcls_hdl_buf.s_len +1);

   rv = FAL0;

   if(ins_or_nil != NIL)
      *ins_or_nil = mchp;
   else if(su_cs_dict_insert(a_mailcap_dp, mclsp->mcls_type_subtype, mchp) > 0)
      rv = TRUM1;

jleave:
   NYD2_OU;
   return rv;

jerr:
   UNUSED(emsg);
   n_err(_("$MAILCAPS: %s: %s: %s\n"),
      mclsp->mcls_name_quoted, mclsp->mcls_type_subtype, V_(emsg));
   goto jleave;
}

static void
a_mailcap_gut(boole gut_dp){
   NYD2_IN;

   if(a_mailcap_dp != NIL){
      struct su_cs_dict_view csdv;

      su_CS_DICT_FOREACH(a_mailcap_dp, &csdv){
         struct a_mailcap_hdl *mchp, *tmp;

         for(mchp = S(struct a_mailcap_hdl*,su_cs_dict_view_data(&csdv));
               mchp != NIL;){
            tmp = mchp;
            mchp = mchp->mch_next;
            su_FREE(tmp);
         }
      }

      if(gut_dp){
         su_cs_dict_gut(a_mailcap_dp);
         a_mailcap_dp = NIL;
      }else
         su_cs_dict_clear(a_mailcap_dp);
   }

   NYD2_OU;
}

static struct n_strlist *
a_mailcap_dump(char const *cmdname, char const *key, void const *dat){
   /* XXX real strlist + str_to_fmt() */
#undef a_X
#define a_X(X,Y) FIELD_INITI(CONCAT(a_MAILCAP_SF_, X)) Y
   static char const sfa[][20] = {
      a_X(CMD, " "),
      a_X(COMPOSE, " compose = "),
      a_X(COMPOSETYPED, " composetyped = \0"),
      a_X(DESCRIPTION, " description = "),
      a_X(EDIT, " edit = "),
      a_X(NAMETEMPLATE, " nametemplate = "),
      a_X(PRINT, " print = "),
      a_X(TEST, " test = "),
      a_X(X11_BITMAP, " x11-bitmap = ")
   };
#undef a_X

   struct n_string s_b, *s;
   struct a_mailcap_hdl const *mchp;
   struct n_strlist *slp;
   NYD2_IN;
   UNUSED(cmdname);

   s = n_string_book(n_string_creat_auto(&s_b), 127);
   s = n_string_resize(s, n_STRLIST_PLAIN_SIZE());

   for(mchp = S(struct a_mailcap_hdl const*,dat); mchp != NIL;
         mchp = mchp->mch_next){
      uz i, lo, lx;
      char const *buf;

      if(S(void const*,mchp) != dat)
         s = n_string_push_c(s, '\n');

      lo = i = su_cs_len(key);
      s = n_string_push_buf(s, key, i);

      buf = S(char const*,&mchp[1]);
      for(i = a_MAILCAP_SF_CMD; i < NELEM(sfa); ++i)
         if(i == a_MAILCAP_SF_CMD || mchp->mch_sfields[i] > 0)
            a_mailcap__dump_kv(i, s, &lo, sfa[i], &buf[mchp->mch_sfields[i]]);

      if(mchp->mch_flags != 0){
         a_MAILCAP_DUMP_SEP_INJ(boole any = FAL0, (void)0);

         for(i = 0; i < NELEM(a_mailcap_flags); ++i){
            if(mchp->mch_flags & a_mailcap_flags[i].mcf_flags){
               uz j;

               s = n_string_push_c(s, ';');
               ++lo;
               j = su_cs_len(a_mailcap_flags[i].mcf_name);
               if(a_MAILCAP_DUMP_SEP_INJ(!any, FAL0) || lo + j >= 76){
                  s = n_string_push_buf(s, "\\\n ", sizeof("\\\n ") -1);
                  lo = 1;
               }
               lx = s->s_len;
               s = n_string_push_c(s, ' ');
               s = n_string_push_buf(s, a_mailcap_flags[i].mcf_name, j);
               lo += s->s_len - lx;
               a_MAILCAP_DUMP_SEP_INJ(any = TRU1, (void)0);
            }
         }
      }
   }

   s = n_string_push_c(s, '\n');

   n_string_cp(s);

   slp = R(struct n_strlist*,S(void*,s->s_dat));
   /* xxx Should we assert alignment constraint of slp is satisfied?
    * xxx Should be, heap memory with alignment < sizeof(void*) bitter? */
   slp->sl_next = NIL;
   slp->sl_len = s->s_len;
   n_string_drop_ownership(s);

   NYD2_OU;
   return slp;
}

static void
a_mailcap__dump_kv(u32 sfield, struct n_string *s, uz *llp, char const *pre,
      char const *vp){
   boole quote;
   uz lo, prel, i, lx;
   NYD2_IN;

   lo = *llp;
   prel = su_cs_len(pre);

   s = n_string_push_c(s, ';');
   ++lo;

   for(i = 0;; ++i)
      if(vp[i] == '\0' && vp[i + 1] == '\0')
         break;
   /* An empty command is an error if no other field follows, a condition we do
    * not know here, so put something; instead of a dummy field, put success */
   if(i == 0 && sfield == a_MAILCAP_SF_CMD){
      vp = ":\0\0";
      i = 1;
   }

   if(a_MAILCAP_DUMP_SEP_INJ(sfield != a_MAILCAP_SF_CMD || lo + prel + i >= 75,
         lo + prel + i >= 72)){
      s = n_string_push_buf(s, "\\\n ", sizeof("\\\n ") -1);
      lo = 1;
   }

   quote = (sfield == a_MAILCAP_SF_DESCRIPTION);

   lx = s->s_len;
   s = n_string_push_buf(s, pre, prel);

   if(quote && i > 0 && (su_cs_is_space(vp[0]) || su_cs_is_space(vp[i - 1])))
      s = n_string_push_c(s, '"');
   else
      quote = FAL0;
   s = a_mailcap__dump_quote(s, vp, quote);
   if(quote)
      s = n_string_push_c(s, '"');

   lo += s->s_len - lx;
   *llp = lo;
   NYD2_OU;
}

static struct n_string *
a_mailcap__dump_quote(struct n_string *s, char const *cp, boole quotequote){
   char c;
   NYD2_IN;

   for(;;){
      if((c = *cp++) == '\0'){
         if((c = *cp++) == '\0')
            break;
         s = n_string_push_c(s, '%');
      }else if(c == ';' || c == '\\' || c == '%' || (c == '"' && quotequote))
         s = n_string_push_c(s, '\\');
      s = n_string_push_c(s, c);
   }

   NYD2_OU;
   return s;
}

static char const *
a_mailcap_expand_formats(char const *format, struct mimepart const *mpp,
      char const *ct){
   struct n_string s_b, *s = &s_b;
   char const *cp, *xp;
   NYD2_IN;

   s = n_string_creat_auto(s);
   s = n_string_book(s, 128);

   for(cp = format;;){
      char c;

      if((c = *cp++) == '\0'){
         if((c = *cp++) == '\0')
            break;

         switch(c){
         case '{':
            s = n_string_push_c(s, '\'');
            xp = su_cs_find_c(cp, '}');
            ASSERT(xp != NIL); /* (parser) */
            xp = savestrbuf(cp, P2UZ(xp - cp));
            if((xp = mx_mime_param_get(xp, mpp->m_ct_type)) != NIL){
               /* XXX Maybe we should simply shell quote that thing? */
               while((c = *xp++) != '\0'){
                  if(c != '\'')
                     s = n_string_push_c(s, c);
                  else
                     s = n_string_push_cp(s, "'\"'\"'");
               }
            }
            s = n_string_push_c(s, '\'');
            break;
         case 's':
            /* For that we leave the actual expansion up to $SHELL.
             * See XXX comment in mx_mailcap_handler(), however */
            s = n_string_push_cp(s,
                  "\"${" n_PIPEENV_FILENAME_TEMPORARY "}\"");
            break;
         case 't':
            s = n_string_push_cp(s, ct);
            break;
         }
      }else
         s = n_string_push_c(s, c);
   }

   cp = n_string_cp(s);
   n_string_drop_ownership(s);

   NYD2_OU;
   return cp;
}

int
c_mailcap(void *vp){
   boole load_only;
   char **argv;
   NYD_IN;

   argv = vp;

   load_only = FAL0;
   if(*argv == NIL)
      goto jlist;
   if(argv[1] != NIL)
      goto jerr;
   if(su_cs_starts_with_case("show", *argv))
      goto jlist;

   load_only = TRU1;
   if(su_cs_starts_with_case("load", *argv))
      goto jlist;
   if(su_cs_starts_with_case("clear", *argv)){
      a_mailcap_gut(TRU1);
      goto jleave;
   }

jerr:
   mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("mailcap"), NIL);
   vp = NIL;
jleave:
   NYD_OU;
   return (vp == NIL ? n_EXIT_ERR : n_EXIT_OK);

jlist:
   if(a_mailcap_dp == NIL)
      a_mailcap_create();

   if(!load_only){
      struct n_strlist *slp;

      slp = NIL;
      if(!(mx_xy_dump_dict("mailcap", a_mailcap_dp, &slp, NIL,
               &a_mailcap_dump) &&
            mx_page_or_print_strlist("mailcap", slp, TRU1)))
         vp = NIL;
   }
   goto jleave;
}

boole
mx_mailcap_handler(struct mx_mime_type_handler *mthp, char const *ct,
      enum sendaction action, struct mimepart const *mpp){
   union {void *v; char const *c; struct a_mailcap_hdl *mch;} p;
   boole wildcard;
   struct a_mailcap_hdl *mchp;
   NYD_IN;
   UNUSED(mpp);

   mchp = NIL;

   /* For now we support that only, too */
   ASSERT_NYD(action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
      action == SEND_TODISP || action == SEND_TODISP_ALL ||
      action == SEND_TODISP_PARTS);

   if(ok_blook(mailcap_disable))
      goto jleave;

   if(a_mailcap_dp == NIL)
      a_mailcap_create();
   if(su_cs_dict_count(a_mailcap_dp) == 0)
      goto jleave;

   /* Walk over the list of handlers and check whether one fits */
   wildcard = FAL0;
   p.v = su_cs_dict_lookup(a_mailcap_dp, ct);
jagain:
   for(; p.mch != NIL; p.mch = p.mch->mch_next){
      u32 f;

      f = p.mch->mch_flags;

      if(f & a_MAILCAP_F_IGNORE)
         continue;

      if(action == SEND_TODISP || action == SEND_TODISP_ALL){
         /*if(f & mx_MIME_TYPE_HDL_ASYNC)
          *   continue;*/
         if(!(f & mx_MIME_TYPE_HDL_COPIOUSOUTPUT))
            continue;
      }else if(action == SEND_QUOTE || action == SEND_QUOTE_ALL){
         if(f & mx_MIME_TYPE_HDL_NOQUOTE)
            continue;
         /*if(f & mx_MIME_TYPE_HDL_ASYNC)
          *   continue;*/
         if(f & mx_MIME_TYPE_HDL_NEEDSTERM) /* XXX for now */
            continue;
         if(!(f & mx_MIME_TYPE_HDL_COPIOUSOUTPUT)) /* xxx for now */
            continue;
      }else{
         /* `mimeview' */
      }

      /* Flags seem to fit, do we need to test? */
      if(p.mch->mch_sfields[a_MAILCAP_SF_TEST] != 0){
         if(!(f & a_MAILCAP_F_TEST_ONCE) || !(f & a_MAILCAP_F_TEST_ONCE_DONE)){
            struct mx_child_ctx cc;
            char const *cmdp;

            cmdp = &R(char const*,&p.mch[1]
                  )[p.mch->mch_sfields[a_MAILCAP_SF_TEST]];
            if(p.mch->mch_sfield_has_format & (1u << a_MAILCAP_SF_TEST))
               cmdp = a_mailcap_expand_formats(cmdp, mpp, ct);

            mx_child_ctx_setup(&cc);
            cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
            cc.cc_fds[0] = cc.cc_fds[1] = mx_CHILD_FD_NULL;
            mx_child_ctx_set_args_for_sh(&cc, NIL, cmdp);
            if(mx_child_run(&cc) && cc.cc_exit_status == n_EXIT_OK)
               f |= a_MAILCAP_F_TEST_ONCE_SUCCESS;

            if(f & a_MAILCAP_F_TEST_ONCE){
               f |= a_MAILCAP_F_TEST_ONCE_DONE;
               p.mch->mch_flags = f;
            }
         }

         if(!(f & a_MAILCAP_F_TEST_ONCE_SUCCESS))
            continue;
      }

      /* That one shall be it */
      if(f & a_MAILCAP_F_HAS_S_FORMAT){
         f |= mx_MIME_TYPE_HDL_TMPF | mx_MIME_TYPE_HDL_TMPF_FILL;
         if(!(f & mx_MIME_TYPE_HDL_ASYNC))
            f |= mx_MIME_TYPE_HDL_TMPF_UNLINK;
      }
      f |= mx_MIME_TYPE_HDL_CMD;
      mthp->mth_flags = f;

      /* XXX We could use a different policy where the handler has a callback
       * XXX mechanism that is called when the handler's environment is fully
       * XXX setup; mailcap could use that to expand mth_shell_cmd.
       * XXX For now simply embed MAILX_FILENAME_TEMPORARY, and leave expansion
       * XXX up to the shell */
      mthp->mth_shell_cmd = &R(char const*,&p.mch[1]
            )[p.mch->mch_sfields[a_MAILCAP_SF_CMD]];
      if(p.mch->mch_sfield_has_format & (1u << a_MAILCAP_SF_CMD))
         mthp->mth_shell_cmd = a_mailcap_expand_formats(mthp->mth_shell_cmd,
               mpp, ct);

      if(p.mch->mch_sfields[a_MAILCAP_SF_NAMETEMPLATE] != 0){
         mthp->mth_tmpf_nametmpl = &R(char const*,&p.mch[1]
               )[p.mch->mch_sfields[a_MAILCAP_SF_NAMETEMPLATE]];
         ASSERT(mthp->mth_tmpf_nametmpl[0] == '\0' &&
            mthp->mth_tmpf_nametmpl[1] == 's');
         mthp->mth_tmpf_nametmpl += 2;
      }

      mchp = p.mch;
      goto jleave;
   }

   /* Direct match, otherwise try wildcard match? */
   if(!wildcard && (p.c = su_cs_find_c(ct, '/')) != NIL){
      char *cp;
      uz i;

      wildcard = TRU1;
      cp = su_LOFI_ALLOC((i = P2UZ(p.c - ct)) + 2 +1);

      su_mem_copy(cp, ct, i);
      cp[i++] = '/';
      cp[i++] = '*';
      cp[i++] = '\0';
      p.v = su_cs_dict_lookup(a_mailcap_dp, cp);

      su_LOFI_FREE(cp);

      if(p.v != NIL)
         goto jagain;
   }

jleave:
   NYD_OU;
   return (mchp == NIL ? FAL0
      : (mchp->mch_flags & a_MAILCAP_F_LAST_RESORT ? TRUM1 : TRU1));
}

#include "su/code-ou.h"
#endif /* mx_HAVE_MAILCAP */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MAILCAP
/* s-it-mode */
