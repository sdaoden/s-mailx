/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mta-aliases.h. XXX Support multiple files
 *@ TODO With an on_loop_tick_event, trigger cache update once per loop max.
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
#define su_FILE mta_aliases
#define mx_SOURCE
#define mx_SOURCE_MTA_ALIASES

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_MTA_ALIASES
#include <sys/stat.h> /* TODO su_path_info */

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/mem.h>

#include "mx/cmd.h"
#include "mx/file-streams.h"
#include "mx/names.h"
#include "mx/termios.h"

#include "mx/mta-aliases.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_mtaali_type{
   a_MTAALI_T_NAME,
   a_MTAALI_T_ADDR,
   a_MTAALI_T_FILE,
   a_MTAALI_T_PIPE
};

struct a_mtaali_g{
   char *mag_path; /* MTA alias file path, expanded (and init switch) */
   struct a_mtaali_alias *mag_aliases; /* In parse order */
   /* We store n_strlist values which are set to "name + NUL + ENUM",
    * where ENUM is enum a_mtaali_type (NAME needs recursion).
    * The first entry also has memory to store the mtaali_alias */
   struct su_cs_dict mag_dict;
};
#define a_MTAALI_G_ERR R(char*,-1) /* .mag_path */

struct a_mtaali_alias{
   struct a_mtaali_alias *maa_next;
   char const *maa_key;
   /* Values are set to "name + NUL + ENUM", where ENUM is enum a_mtaali_type
    * (NAME needs recursion).
    * The first entry also provides memory to store the mtaali_alias */
   struct n_strlist *maa_values;
};

struct a_mtaali_stack{
   char const *mas_path_usr; /* Unexpanded version */
   char const *mas_path;
   struct a_mtaali_alias *mas_aliases;
   struct su_cs_dict mas_dict;
};

struct a_mtaali_query{
   struct su_cs_dict *maq_dp;
   struct mx_name *maq_result;
   s32 maq_err;
   u32 maq_type;
};

static struct a_mtaali_g a_mtaali_g; /* XXX debug atexit */

static void a_mtaali_gut_csd(struct su_cs_dict *csdp);

static s32 a_mtaali_cache_init(char const *usrfile);
static s32 a_mtaali__read_file(struct a_mtaali_stack *masp);

static void a_mtaali_expand(uz lvl, char const *name,
      struct a_mtaali_query *maqp);

static void
a_mtaali_gut_csd(struct su_cs_dict *csdp){
   struct su_cs_dict_view csdv;
   NYD2_IN;

   su_CS_DICT_FOREACH(csdp, &csdv){
      union {void *v; struct a_mtaali_alias *maa; struct n_strlist *sl;} p;

      p.v = su_cs_dict_view_data(&csdv);

      for(p.sl = p.maa->maa_values; p.sl != NIL;){
         struct n_strlist *tmp;

         tmp = p.sl;
         p.sl = p.sl->sl_next;
         su_FREE(tmp);
      }
   }

   su_cs_dict_gut(csdp);
   NYD2_OU;
}

static s32
a_mtaali_cache_init(char const *usrfile){
   struct a_mtaali_stack mas;
   s32 rv;
   NYD_IN;

   if((mas.mas_path =
         fexpand(mas.mas_path_usr = usrfile, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL))) == NIL){
      rv = su_ERR_NOENT;
      goto jerr;
   }else if(a_mtaali_g.mag_path == NIL ||
         a_mtaali_g.mag_path == a_MTAALI_G_ERR ||
         su_cs_cmp(mas.mas_path, a_mtaali_g.mag_path)){
      if((rv = a_mtaali__read_file(&mas)) != su_ERR_NONE)
         goto jerr_nolog;

      if(a_mtaali_g.mag_path != NIL && a_mtaali_g.mag_path != a_MTAALI_G_ERR){
         su_FREE(a_mtaali_g.mag_path);
         a_mtaali_gut_csd(&a_mtaali_g.mag_dict);
      }
      a_mtaali_g.mag_path = su_cs_dup(mas.mas_path, 0);
      a_mtaali_g.mag_aliases = mas.mas_aliases;
      a_mtaali_g.mag_dict = mas.mas_dict;
   }else
      rv = su_ERR_NONE;

jleave:
   NYD_OU;
   return rv;

jerr:
   n_err(_("*mta_aliases*: %s: %s\n"),
      n_shexp_quote_cp(mas.mas_path_usr, FAL0), su_err_doc(rv));
jerr_nolog:
   if(a_mtaali_g.mag_path != NIL && a_mtaali_g.mag_path != a_MTAALI_G_ERR){
      a_mtaali_gut_csd(&mas.mas_dict);
      su_FREE(a_mtaali_g.mag_path);
   }
   a_mtaali_g.mag_path = a_MTAALI_G_ERR;
   goto jleave;
}

static s32
a_mtaali__read_file(struct a_mtaali_stack *masp){
   struct str line, l;
   struct su_cs_dict_view csdv;
   struct a_mtaali_alias **maapp, *maap;
   struct n_string ns, *nsp;
   struct su_cs_dict *dp;
   s32 rv;
   FILE *afp;
   NYD_IN;

   if((afp = mx_fs_open(masp->mas_path, "r")) == NIL){
      rv = su_err_no();
      n_err(_("*mta-aliases*: cannot open %s: %s\n"),
         n_shexp_quote_cp(masp->mas_path_usr, FAL0), su_err_doc(rv));
      goto jleave;
   }

   dp = su_cs_dict_create(&masp->mas_dict,
         (su_CS_DICT_POW2_SPACED | su_CS_DICT_CASE), NIL);
   su_cs_dict_view_setup(&csdv, dp);
   nsp = n_string_creat_auto(&ns);
   nsp = n_string_book(nsp, 512);

   /* Read in the database */
   mx_fs_linepool_aquire(&line.s, &line.l);

   masp->mas_aliases = NIL;
   maapp = &masp->mas_aliases;

   while((rv = readline_restart(afp, &line.s, &line.l, 0)) >= 0){
      /* :: According to Postfix aliases(5) */
      l.s = line.s;
      l.l = S(uz,rv);
      n_str_trim(&l, n_STR_TRIM_BOTH);

      /* :
       *    Empty lines and whitespace-only lines are ignored, as are lines
       *    whose first non-whitespace character is a `#'. */
      if(l.l == 0 || l.s[0] == '#')
         continue;

      /* :
       *    A logical line starts with non-whitespace text.  A line that starts
       *    with whitespace continues a logical line. */
      if(l.s != line.s || nsp->s_len == 0){
         if(!n_string_can_book(nsp, l.l)){
            su_state_err(su_STATE_ERR_OVERFLOW, (su_STATE_ERR_PASS |
               su_STATE_ERR_NOERRNO), _("*mta-aliases*: line too long"));
            l.s = UNCONST(char*,N_("line too long"));
            rv = su_ERR_OVERFLOW;
            goto jparse_err;
         }
         nsp = n_string_push_buf(nsp, l.s, l.l);
         continue;
      }

      ASSERT(nsp->s_len > 0);
jparse_line:{
         /* :
          *    An alias definition has the form
          *       name: value1, value2, ...
          *    ...
          *    The name is a local address (no domain part).  Use double quotes
          *    when the name contains any special characters such as
          *    whitespace, `#', `:', or `@'. The name is folded to lowercase,
          *    in order to make database lookups case insensitive.
          * XXX Don't support quoted names nor special characters (manual!) */
         struct str l2;
         char c;

         l2.l = nsp->s_len;
         l2.s = n_string_cp(nsp);

         if((l2.s = su_cs_find_c(l2.s, ':')) == NIL ||
               (l2.l = P2UZ(l2.s - nsp->s_dat)) == 0){
            l.s = UNCONST(char*,N_("invalid line"));
            rv = su_ERR_INVAL;
            goto jparse_err;
         }

         /* XXX Manual! "name" may only be a Unix username (useradd(8)):
          *    Usernames must start with a lower case letter or an underscore,
          *    followed by lower case letters, digits, underscores, or dashes.
          *    They can end with a dollar sign. In regular expression terms:
          *       [a-z_][a-z0-9_-]*[$]?
          *    Usernames may only be up to 32 characters long.
          * Test against alpha since the csdict will lowercase names.. */
         *l2.s = '\0';
         c = *(l2.s = nsp->s_dat);
         if(!su_cs_is_alpha(c) && c != '_'){
jename:
            l.s = UNCONST(char*,N_("not a valid name\n"));
            rv = su_ERR_INVAL;
            goto jparse_err;
         }
         while((c = *++l2.s) != '\0')
            /* On change adjust `alias' and impl., too */
            if(!su_cs_is_alnum(c) && c != '_' && c != '-'){
               if(c == '$' && *l2.s == '\0')
                  break;
               goto jename;
            }

         /* Be strict regarding file content */
         if(UNLIKELY(su_cs_dict_has_key(dp, nsp->s_dat))){
            l.s = UNCONST(char*,N_("duplicate name"));
            rv = su_ERR_ADDRINUSE;
            goto jparse_err;
         }else{
            /* Seems to be a usable name.  Parse data off */
            struct n_strlist **tailp;
            struct mx_name *nphead,*np;

            nphead = lextract(l2.s = &nsp->s_dat[l2.l + 1], GTO | GFULL |
                  GQUOTE_ENCLOSED_OK);

            if(UNLIKELY(nphead == NIL)){
jeval:
               n_err(_("*mta_aliases*: %s: ignoring empty/unsupported value: "
                     "%s: %s\n"),
                  n_shexp_quote_cp(masp->mas_path_usr, FAL0),
                  n_shexp_quote_cp(nsp->s_dat, FAL0),
                  n_shexp_quote_cp(l2.s, FAL0));
               continue;
            }

            for(np = nphead; np != NIL; np = np->n_flink)
               /* TODO :include:/file/path directive not yet <> manual! */
               if((np->n_flags & mx_NAME_ADDRSPEC_ISFILE) &&
                     su_cs_starts_with(np->n_name, ":include:"))
                  goto jeval;

            UNINIT(tailp, NIL);
            for(maap = NIL, np = nphead; np != NIL; np = np->n_flink){
               struct n_strlist *slp;
               uz i;

               i = su_cs_len(np->n_fullname) +1;
               slp = n_STRLIST_ALLOC(i + 1 +
                     (maap == NIL ? 2*Z_ALIGN_OVER(ALIGNOF(*maap)) : 0));

               if(maap == NIL){
                  maap = P_ALIGN(struct a_mtaali_alias*, maap,
                        &slp->sl_dat[i +1 + 1]);
                  maap->maa_next = NIL;
                  tailp = &maap->maa_values;
                  *maapp = maap;
                  maapp = &maap->maa_next;
               }

               *tailp = slp;
               slp->sl_next = NIL;
               tailp = &slp->sl_next;
               slp->sl_len = i -1;
               su_mem_copy(slp->sl_dat, np->n_fullname, i);

               switch(np->n_flags & mx_NAME_ADDRSPEC_ISMASK){
               case mx_NAME_ADDRSPEC_ISFILE: c = a_MTAALI_T_FILE; break;
               case mx_NAME_ADDRSPEC_ISPIPE: c = a_MTAALI_T_PIPE; break;
               case mx_NAME_ADDRSPEC_ISNAME: c = a_MTAALI_T_NAME; break;
               default:
               case mx_NAME_ADDRSPEC_ISADDR: c = a_MTAALI_T_ADDR; break;
               }
               slp->sl_dat[i] = c;
            }

            if((rv = su_cs_dict_view_reset_insert(&csdv, nsp->s_dat, maap)
                  ) != su_ERR_NONE){
               n_err(_("*mta_aliases*: failed to create storage: %s\n"),
                  su_err_doc(rv));
               goto jdone;
            }
            maap->maa_key = su_cs_dict_view_key(&csdv);
         }
      }

      /* Worked last line leftover? */
      if(l.l == 0){
         /*sp = n_string_trunc(sp, 0);
          *break;*/
         goto jparse_done;
      }
      nsp = n_string_assign_buf(nsp, l.s, l.l);
   }
   /* Last line leftover to parse? */
   if(nsp->s_len > 0){
      l.l = 0;
      goto jparse_line;
   }

jparse_done:
   rv = su_ERR_NONE;
jdone:
   mx_fs_linepool_release(line.s, line.l);

   if(rv != su_ERR_NONE)
      a_mtaali_gut_csd(dp);

   mx_fs_close(afp);
jleave:
   NYD_OU;
   return rv;

jparse_err:
   n_err("*mta-aliases*: %s: %s: %s\n",
      n_shexp_quote_cp(masp->mas_path_usr, FAL0), V_(l.s),
      n_shexp_quote_cp(nsp->s_dat, FAL0));
   goto jdone;
}

static void
a_mtaali_expand(uz lvl, char const *name, struct a_mtaali_query *maqp){
   union {void *v; struct a_mtaali_alias *maa; struct n_strlist *sl;} p;
   NYD2_IN;

   ++lvl;

   if((p.v = su_cs_dict_lookup(maqp->maq_dp, name)) == NIL){
jput_name:
      maqp->maq_err = su_ERR_DESTADDRREQ;
      maqp->maq_result = cat(nalloc(name, maqp->maq_type | GFULL),
            maqp->maq_result);
   }else{
      for(p.sl = p.maa->maa_values; p.sl != NIL; p.sl = p.sl->sl_next){
         /* Is it a name itself? */
         if(p.sl->sl_dat[p.sl->sl_len + 1] == a_MTAALI_T_NAME){
            if(UCMP(z, lvl, <, n_ALIAS_MAXEXP)) /* TODO not a real error! */
               a_mtaali_expand(lvl, p.sl->sl_dat, maqp);
            else{
               n_err(_("*mta_aliases*: stopping recursion at depth %d\n"),
                  n_ALIAS_MAXEXP);
               goto jput_name;
            }
         }else
            maqp->maq_result = cat(maqp->maq_result,
                  nalloc(p.sl->sl_dat, maqp->maq_type | GFULL));
      }
   }

   NYD2_OU;
}

int
c_mtaaliases(void *vp){
   char const *cp;
   boole load_only;
   char **argv;
   NYD_IN;

   argv = vp;
   load_only = FAL0;

   if((cp = *argv) == NIL)
      goto jlist;
   if(argv[1] != NIL)
      goto jerr;
   if(su_cs_starts_with_case("show", cp))
      goto jlist;
   if(su_cs_starts_with_case("clear", cp))
      goto jclear;
   load_only = TRU1;
   if(su_cs_starts_with_case("load", cp))
      goto jclear;
jerr:
   mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("mtaaliases"), NIL);
   vp = NIL;
jleave:
   NYD_OU;
   return (vp == NIL ? n_EXIT_ERR : n_EXIT_OK);

jclear:
   if(a_mtaali_g.mag_path != NIL && a_mtaali_g.mag_path != a_MTAALI_G_ERR){
      a_mtaali_gut_csd(&a_mtaali_g.mag_dict);
      su_FREE(a_mtaali_g.mag_path);
   }
   a_mtaali_g.mag_path = NIL;
   if(load_only)
      goto jlist;
   goto jleave;

jlist:{
   struct n_string quote; /* TODO quoting does not belong; -> RFC 822++ */
   uz scrwid, l, lw;
   struct n_strlist *slp;
   struct a_mtaali_alias *maap;
   FILE *fp;

   if((cp = ok_vlook(mta_aliases)) == NIL){
      n_err(_("mtaaliases: *mta-aliases* not set\n"));
      vp = NIL;
      goto jleave;
   }else if(a_mtaali_cache_init(cp) != su_ERR_NONE ||
         a_mtaali_g.mag_path == a_MTAALI_G_ERR){
      n_err(_("mtaaliases: *mta-aliases* had no content\n"));
      vp = NIL;
      goto jleave;
   }
   if(load_only)
      goto jleave;

   if((fp = mx_fs_tmp_open(NIL, "mtaaliases", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
         mx_FS_O_REGISTER), NIL)) == NIL)
      fp = n_stdout;

   n_string_creat_auto(&quote);
   scrwid = mx_TERMIOS_WIDTH_OF_LISTS();
   l = 0;
   for(maap = a_mtaali_g.mag_aliases; maap != NIL; maap = maap->maa_next){
      boole any;

      /* Our reader above guarantees the name does not need to be quoted! */
      fputs(cp = maap->maa_key, fp);
      putc(':', fp);
      lw = su_cs_len(cp) + 1;

      any = FAL0;
      for(slp = maap->maa_values; slp != NIL; slp = slp->sl_next){
         uz i;

         if(!any)
            any = TRU1;
         else{
            putc(',', fp);
            ++lw;
         }

         /* TODO Is it a name itself?  Otherwise we may need to apply quoting!
          * TODO quoting does not belong; -> RFC 822++ */
         cp = slp->sl_dat;
         i = slp->sl_len;
         if(cp[i + 1] != a_MTAALI_T_NAME && cp[i + 1] != a_MTAALI_T_ADDR &&
               su_cs_first_of_cbuf_cbuf(cp, i, " \t\"#:@", 6) != UZ_MAX){
            char c;

            n_string_reserve(n_string_trunc(&quote, 0), (slp->sl_len * 2) + 2);
            n_string_push_c(&quote, '"');
            while((c = *cp++) != '\0'){
               if(c == '"' || c == '\\')
                  n_string_push_c(&quote, '\\');
               n_string_push_c(&quote, c);
            }
            n_string_push_c(&quote, '"');
            cp = n_string_cp(&quote);
            i = quote.s_len;
         }

         if(lw + i >= scrwid){
            fputs("\n  ", fp);
            ++l;
            lw = 2;
         }
         lw += i + 1;
         putc(' ', fp);
         fputs(cp, fp);
      }
      putc('\n', fp);
      ++l;
   }
   /* n_string_gut(&quote); */

   if(fp != n_stdout){
      page_or_print(fp, l);

      mx_fs_close(fp);
   }else
      clearerr(fp);
   }
   goto jleave;
}

s32
mx_mta_aliases_expand(struct mx_name **npp){
   struct a_mtaali_query maq;
   struct mx_name *np, *nphead;
   s32 rv;
   char const *file;
   NYD_IN;

   rv = su_ERR_NONE;

   /* Is there a possibility that we have to do anything? */
   if((file = ok_vlook(mta_aliases)) == NIL)
      goto jleave;

   for(np = *npp; np != NIL; np = np->n_flink)
      if(!(np->n_type & GDEL) && (np->n_flags & mx_NAME_ADDRSPEC_ISNAME))
         break;
   if(np == NIL)
      goto jleave;

   /* Then lookup the cache, creating/updating it first as necessary */
   if((rv = a_mtaali_cache_init(file)) != su_ERR_NONE ||
         a_mtaali_g.mag_path == a_MTAALI_G_ERR){
      rv = su_ERR_DESTADDRREQ;
      goto jleave;
   }

   maq.maq_dp = &a_mtaali_g.mag_dict;
   nphead = *npp;
   maq.maq_result = *npp = NIL;
   maq.maq_err = su_ERR_NONE;

   for(np = nphead; np != NIL; np = np->n_flink){
      if(np->n_flags & mx_NAME_ADDRSPEC_ISNAME){
         maq.maq_type = np->n_type;
         a_mtaali_expand(0, np->n_name, &maq);
      }else
         maq.maq_result = cat(maq.maq_result, ndup(np, np->n_type));
   }

   *npp = maq.maq_result;
   rv = maq.maq_err;
jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_MTA_ALIASES */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MTA_ALIASES
/* s-it-mode */
