/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mta-aliases.h. XXX Support multiple files
 *
 * Copyright (c) 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#include "mx/file-streams.h"
#include "mx/names.h"

#include "mx/mta-aliases.h"
#include "su/code-in.h"

struct a_mtaali_g{
   char *mag_path; /* MTA alias file path, expanded (and init switch) */
   s64 mag_mtime; /* Modification time once last read in */
   s64 mag_size; /* Ditto, file size */
   /* We store n_strlist values which are set to "name + NUL + boole",
    * where the boole indicates whether name is a NAME (needs recursion) */
   struct su_cs_dict mag_dict;
};

struct a_mtaali_stack{
   struct a_mtaali_g *mas_entry;
   char const *mas_path;
   char const *mas_user;
   struct su_cs_dict mas_dict;
   struct stat mas_sb;
};

struct a_mtaali_query{
   struct su_cs_dict *maq_dp;
   struct mx_name *maq_result;
   s32 maq_err;
   u32 maq_type;
};

static struct a_mtaali_g a_mtaali_g; /* XXX debug atexit */

static void a_mtaali_gut_csd(struct su_cs_dict *csdp);

static s32 a_mtaali_cache_check(char const *usrfile);
static s32 a_mtaali_read_file(struct a_mtaali_stack *masp);

static void a_mtaali_expand(uz lvl, char const *name,
      struct a_mtaali_query *maqp);

static void
a_mtaali_gut_csd(struct su_cs_dict *csdp){
   struct n_strlist *slp, *tmp;
   struct su_cs_dict_view csdv;
   NYD2_IN;

   su_cs_dict_view_setup(&csdv, csdp);

   su_CS_DICT_VIEW_FOREACH(&csdv){
      for(slp = S(struct n_strlist*,su_cs_dict_view_data(&csdv));
            slp != NIL;){
         tmp = slp;
         slp = slp->sl_next;
         su_FREE(tmp);
      }
   }

   su_cs_dict_gut(csdp);
   NYD2_OU;
}

static s32
a_mtaali_cache_check(char const *usrfile){
   struct a_mtaali_stack mas;
   s32 rv;
   NYD_IN;

   if((mas.mas_path =
         fexpand(mas.mas_user = usrfile, FEXP_LOCAL | FEXP_NOPROTO)) == NIL){
      rv = su_ERR_NOENT;
      goto jerr;
   }else if(stat(mas.mas_path, &mas.mas_sb) == -1){
      rv = su_err_no();
jerr:
      n_err(_("*mta_aliases*: %s: %s\n"),
         n_shexp_quote_cp(mas.mas_user, FAL0), su_err_doc(rv));
   }else if(a_mtaali_g.mag_path == NIL ||
         su_cs_cmp(mas.mas_path, a_mtaali_g.mag_path) ||
         a_mtaali_g.mag_mtime < mas.mas_sb.st_mtime ||
         a_mtaali_g.mag_size != mas.mas_sb.st_size){
      if((rv = a_mtaali_read_file(&mas)) == su_ERR_NONE){
         if(a_mtaali_g.mag_path == NIL)
            su_cs_dict_create(&a_mtaali_g.mag_dict,
               (su_CS_DICT_POW2_SPACED | su_CS_DICT_CASE), NIL);
         else
            su_FREE(a_mtaali_g.mag_path);

         a_mtaali_g.mag_path = su_cs_dup(mas.mas_path, 0);
         a_mtaali_g.mag_mtime = S(s64,mas.mas_sb.st_mtime);
         a_mtaali_g.mag_size = S(s64,mas.mas_sb.st_size);
         su_cs_dict_swap(&a_mtaali_g.mag_dict, &mas.mas_dict);
         a_mtaali_gut_csd(&mas.mas_dict);
      }
   }else
      rv = su_ERR_NONE;

   NYD_OU;
   return rv;
}

static s32
a_mtaali_read_file(struct a_mtaali_stack *masp){
   struct str line, l;
   struct n_string s, *sp;
   struct su_cs_dict *dp;
   s32 rv;
   FILE *afp;
   NYD_IN;

   if((afp = mx_fs_open(masp->mas_path, "r")) == NIL){
      rv = su_err_no();
      n_err(_("*mta-aliases*: cannot open %s: %s\n"),
         n_shexp_quote_cp(masp->mas_user, FAL0), su_err_doc(rv));
      goto jleave;
   }

   dp = su_cs_dict_create(&masp->mas_dict,
         (su_CS_DICT_POW2_SPACED | su_CS_DICT_CASE), NIL);
   sp = n_string_creat_auto(&s);
   sp = n_string_book(sp, 512);

   /* Read in the database */
   su_mem_set(&line, 0, sizeof line);

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
      if(l.s != line.s || sp->s_len == 0){
         sp = n_string_push_buf(sp, l.s, l.l);
         continue;
      }

      ASSERT(sp->s_len > 0);
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

         l2.l = sp->s_len;
         l2.s = n_string_cp(sp);

         if((l2.s = su_cs_find_c(l2.s, ':')) == NIL ||
               (l2.l = P2UZ(l2.s - sp->s_dat)) == 0){
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
         c = *(l2.s = sp->s_dat);
         if(!su_cs_is_alpha(c) && c != '_'){
jename:
            l.s = UNCONST(char*,N_("not a valid name\n"));
            rv = su_ERR_INVAL;
            goto jparse_err;
         }
         while((c = *++l2.s) != '\0')
            if(!su_cs_is_alnum(c) && c != '_' && c != '-'){
               if(c == '$' && *l2.s == '\0')
                  break;
               goto jename;
            }

         /* Be strict regarding file content */
         if(UNLIKELY(su_cs_dict_has_key(dp, sp->s_dat))){
            l.s = UNCONST(char*,N_("duplicate name"));
            rv = su_ERR_ADDRINUSE;
            goto jparse_err;
         }else{
            /* Seems to be a usable name.  Parse data off */
            struct n_strlist *head, **tailp;
            struct mx_name *nphead,*np;

            nphead = lextract(l2.s = &sp->s_dat[l2.l + 1], GTO | GFULL |
                  GQUOTE_ENCLOSED_OK);

            if(UNLIKELY(nphead == NIL)){
jeval:
               n_err(_("*mta_aliases*: %s: ignoring empty/unsupported value: "
                     "%s: %s\n"),
                  n_shexp_quote_cp(masp->mas_user, FAL0),
                  n_shexp_quote_cp(sp->s_dat, FAL0),
                  n_shexp_quote_cp(l2.s, FAL0));
               continue;
            }

            for(np = nphead; np != NIL; np = np->n_flink)
               /* TODO :include:/file/path directive not yet <> manual! */
               if((np->n_flags & mx_NAME_ADDRSPEC_ISFILE) &&
                     su_cs_starts_with(np->n_name, ":include:"))
                  goto jeval;

            for(head = NIL, tailp = &head, np = nphead;
                  np != NIL; np = np->n_flink){
               struct n_strlist *slp;

               l2.l = su_cs_len(np->n_fullname) +1;
               slp = n_STRLIST_ALLOC(l2.l + 1);
               *tailp = slp;
               slp->sl_next = NIL;
               tailp = &slp->sl_next;
               slp->sl_len = l2.l -1;
               su_mem_copy(slp->sl_dat, np->n_fullname, l2.l);
               slp->sl_dat[l2.l] = ((np->n_flags & mx_NAME_ADDRSPEC_ISNAME
                     ) != 0);
            }

            if((rv = su_cs_dict_insert(dp, sp->s_dat, head)) != su_ERR_NONE){
               n_err(_("*mta_aliases*: failed to create storage: %s\n"),
                  su_err_doc(rv));
               goto jdone;
            }
         }
      }

      /* Worked last line leftover? */
      if(l.l == 0){
         /*sp = n_string_trunc(sp, 0);
          *break;*/
         goto jparse_done;
      }
      sp = n_string_assign_buf(sp, l.s, l.l);
   }
   /* Last line leftover to parse? */
   if(sp->s_len > 0){
      l.l = 0;
      goto jparse_line;
   }

jparse_done:
   rv = su_ERR_NONE;
jdone:
   if(line.s != NIL)
      n_free(line.s);

   if(rv != su_ERR_NONE)
      a_mtaali_gut_csd(dp);

   mx_fs_close(afp);
jleave:
   NYD_OU;
   return rv;

jparse_err:
   n_err("*mta-aliases*: %s: %s: %s\n",
      n_shexp_quote_cp(masp->mas_user, FAL0), V_(l.s),
      n_shexp_quote_cp(sp->s_dat, FAL0));
   goto jdone;
}

static void
a_mtaali_expand(uz lvl, char const *name, struct a_mtaali_query *maqp){
   struct n_strlist *slp;
   NYD2_IN;

   ++lvl;

   if((slp = S(struct n_strlist*,su_cs_dict_lookup(maqp->maq_dp, name))
         ) == NIL){
jput_name:
      maqp->maq_err = su_ERR_DESTADDRREQ;
      maqp->maq_result = cat(nalloc(name, maqp->maq_type | GFULL),
            maqp->maq_result);
   }else do{
      /* Is it a name itself? */
      if(slp->sl_dat[slp->sl_len + 1] != FAL0){
         if(UCMP(z, lvl, <, n_ALIAS_MAXEXP)) /* TODO not a real error! */
            a_mtaali_expand(lvl, slp->sl_dat, maqp);
         else{
            n_err(_("*mta_aliases*: stopping recursion at depth %d\n"),
               n_ALIAS_MAXEXP);
            goto jput_name;
         }
      }else
         maqp->maq_result = cat(maqp->maq_result,
               nalloc(slp->sl_dat, maqp->maq_type | GFULL));
   }while((slp = slp->sl_next) != NIL);

   NYD2_OU;
}

s32
mx_mta_aliases_expand(struct mx_name **npp){
   struct a_mtaali_query maq;
   struct mx_name *np, *nphead;
   s32 rv;
   char const *file;
   NYD_IN;

   rv = su_ERR_NONE;

   /* Is there the possibility we have to do anything? */
   if((file = ok_vlook(mta_aliases)) == NIL)
      goto jleave;

   for(np = *npp; np != NIL; np = np->n_flink)
      if(!(np->n_type & GDEL) && (np->n_flags & mx_NAME_ADDRSPEC_ISNAME))
         break;
   if(np == NIL)
      goto jleave;

   /* Then lookup the cache, creating/updating it first as necessary */
   if((rv = a_mtaali_cache_check(file)) != su_ERR_NONE)
      goto jleave;

   if(su_cs_dict_count(maq.maq_dp = &a_mtaali_g.mag_dict) == 0){
      rv = su_ERR_DESTADDRREQ;
      goto jleave;
   }

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
/* s-it-mode */
