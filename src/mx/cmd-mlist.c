/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-mlist.h.
 *@ XXX Use a su_cs_set for non-regex stuff?
 *@ XXX use su_list for the regex stuff?
 *@ TODO use su_regex (and if it's a wrapper only)
 *@ TODO _ML -> _CML
 *
 * Copyright (c) 2014 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_mlist
#define mx_SOURCE
#define mx_SOURCE_CMD_MLIST

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#ifdef mx_HAVE_REGEX
# include <regex.h>
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/names.h"

#include "mx/cmd-mlist.h"
#include "su/code-in.h"

/* ..of a_ml_dp.. */
#define a_ML_FLAGS (su_CS_DICT_POW2_SPACED | su_CS_DICT_CASE |\
      su_CS_DICT_HEAD_RESORT | su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_ML_TRESHOLD_SHIFT 2

/* ..of a_ml_re_dp.  Only for un-registration + flag change: use a high t.-s.
 * Usage of NILISVALO is essential for our purpose, since we use
 * view_set_data() to change the subscription state, but which is supposed to
 * only flip the lowermost bit -- see a_ml_dp docu below.
 * I.e., clone() -> template is char const* == regex, assign(): template is
 * a_ml_regex* == self with bit 1 indicating new subscription state, delete():
 * a_ml_regex* == self with bit 1 indicating subscription state.
 * We MUST pass non-NIL to assign()! */
#ifdef mx_HAVE_REGEX
# define a_ML_RE_FLAGS (su_CS_DICT_POW2_SPACED | su_CS_DICT_OWNS |\
      su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS | su_CS_DICT_NILISVALO)
# define a_ML_RE_TRESHOLD_SHIFT 4
#endif

#ifdef mx_HAVE_REGEX
struct a_ml_regex{
   struct a_ml_regex *mlr_last;
   struct a_ml_regex *mlr_next;
   regex_t mlr_regex;
};
#endif

/* The value of our direct match dict is in fact a boole, the regex one
 * uses a_ml_regex* (still with boole as first bit, as above) */
static struct su_cs_dict *a_ml_dp, a_ml__d; /* XXX atexit _gut() (DVL()) */
#ifdef mx_HAVE_REGEX
static struct su_cs_dict *a_ml_re_dp, a_ml__re_d; /* XXX atexit _gut (DVL()) */

/* Regex are searched in order, subscribed first, then "default"; make this
 * easier by using two dedicated lists.
 * We will perform automatic head resorting on these lists in the hope that
 * this will place often used matches nearer to the head over time */
static struct a_ml_regex *a_ml_re_def, *a_ml_re_sub;

/* +toolbox below */
#endif

/* */
static boole a_ml_mux(boole subscribe, char const **argv);

/* */
static void a_ml_unsub_all(struct su_cs_dict_view *dvp, struct su_cs_dict *dp);

/* */
static struct n_strlist *a_ml_dump(char const *cmdname, char const *key,
      void const *dat);

/* su_toolbox for a_ml_re_dp */
#ifdef mx_HAVE_REGEX
static void *a_ml_re_clone(void const *t, u32 estate);
static void a_ml_re_delete(void *self);
static void *a_ml_re_assign(void *self, void const *t, u32 estate);

static struct su_toolbox const a_ml_re_tbox = su_TOOLBOX_I9R(
   &a_ml_re_clone, &a_ml_re_delete, &a_ml_re_assign, NIL, NIL
);
#endif

static boole
a_ml_mux(boole subscribe, char const **argv){
   struct su_cs_dict_view dv;
   boole notrv;
   char const *cmd, *key;
   NYD2_IN;

   cmd = subscribe ? "mlsubscribe" : "mlist";

   if((key = *argv) == NIL){
      struct n_strlist *slp, *stailp;

      stailp = slp = NIL;
      notrv = !(mx_xy_dump_dict(cmd, a_ml_dp, &slp, &stailp, &a_ml_dump) &&
#ifdef mx_HAVE_REGEX
            mx_xy_dump_dict(cmd, a_ml_re_dp, &slp, &stailp, &a_ml_dump) &&
#endif
            mx_page_or_print_strlist(cmd, slp, FAL0));
   }else{
      if(a_ml_dp == NIL){
         a_ml_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_ml__d, a_ML_FLAGS, NIL),
               a_ML_TRESHOLD_SHIFT);
#ifdef mx_HAVE_REGEX
         a_ml_re_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_ml__re_d, a_ML_RE_FLAGS, &a_ml_re_tbox),
               a_ML_RE_TRESHOLD_SHIFT);
#endif
      }

      notrv = FAL0;
      do{ /* while((key = *++argv) != NIL); */
         union {void const *cvp; void *vp; up flags;} u;

         /* Does this already exist, somewhere? */
         if(su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_ml_dp), key)
#ifdef mx_HAVE_REGEX
               || su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_ml_re_dp
                     ), key)
#endif
         ){
            u.cvp = su_cs_dict_view_data(&dv);

            if(u.flags & TRU1){
               if(subscribe)
                  goto jelisted;
               u.flags &= ~TRU1;
               goto jset_data;
            }else if(!subscribe){
jelisted:
               n_err(_("%s: already listed: %s\n"),
                  cmd, n_shexp_quote_cp(key, FAL0));
               notrv = TRU1;
            }else{
               u.flags |= TRU1;
jset_data:
               notrv |= (su_cs_dict_view_set_data(&dv, u.vp) > 0);
            }
         }else{
            struct su_cs_dict *dp;

            /* A new entry */
            ASSERT((subscribe & ~TRU1) == 0);
            u.flags = subscribe;
#ifdef mx_HAVE_REGEX
            if(n_is_maybe_regex(key)){
               /* XXX Since the key is char* it could reside on an address
                * XXX with bit 1 set, but since it is user input it came in
                * XXX via shell argument quoting, is thus served by our memory,
                * XXX and should thus be aligned properly */
               ASSERT((R(up,key) & 1) == 0);
               u.flags |= R(up,key);
               dp = a_ml_re_dp;
            }else
#endif
               dp = a_ml_dp;

            if(su_cs_dict_insert(dp, key, u.vp) > 0){
               n_err(_("%s: failed to create storage: %s\n"),
                  n_shexp_quote_cp(key, FAL0));
               notrv = 1;
            }
         }
      }while((key = *++argv) != NIL);
   }

   NYD2_OU;
   return !notrv;
}

static void
a_ml_unsub_all(struct su_cs_dict_view *dvp, struct su_cs_dict *dp){
   union {void *vp; up flags;} u;
   NYD2_IN;

   for(su_cs_dict_view_setup(dvp, dp); su_cs_dict_view_is_valid(dvp);
         su_cs_dict_view_next(dvp)){
      u.vp = su_cs_dict_view_data(dvp);

      if(u.flags & TRU1){
         u.flags ^= TRU1;
         su_cs_dict_view_set_data(dvp, u.vp);
      }
   }
   NYD2_OU;
}

static struct n_strlist *
a_ml_dump(char const *cmdname, char const *key, void const *dat){
   /* XXX real strlist + str_to_fmt() */
   char *cp;
   union {void const *cvp; up flags;} u;
   struct n_strlist *slp;
   uz typel, kl, cl;
   char const *typep, *kp;
   NYD2_IN;

   typep = su_empty;
   typel = 0;

   slp = NIL;
   u.cvp = dat;
   if(u.flags & TRU1){
      u.flags &= ~TRU1;

      if(!su_cs_cmp(cmdname, "mlist"))
         goto jleave;

#ifdef mx_HAVE_REGEX
      if(u.cvp != NIL && (n_poption & n_PO_D_V)){
         typep = " # regex(7)";
         typel = sizeof(" # regex(7)") -1;
      }
#endif
   }else if(!su_cs_cmp(cmdname, "mlsubscribe"))
      goto jleave;

   kp = n_shexp_quote_cp(key, TRU1);
   kl = su_cs_len(kp);
   cl = su_cs_len(cmdname);

   slp = n_STRLIST_AUTO_ALLOC(cl + 1 + kl + 1 + typel +1);
   slp->sl_next = NIL;
   cp = slp->sl_dat;
   su_mem_copy(cp, cmdname, cl);
   cp += cl;
   *cp++ = ' ';
   su_mem_copy(cp, kp, kl);
   cp += kl;
   if(typel > 0){
      *cp++ = ' ';
      su_mem_copy(cp, typep, typel);
      cp += typel;
   }
   *cp = '\0';
   slp->sl_len = P2UZ(cp - slp->sl_dat);

jleave:
   NYD2_OU;
   return slp;
}

#ifdef mx_HAVE_REGEX
static void *
a_ml_re_clone(void const *t, u32 estate){
   struct a_ml_regex **mlrpp;
   int s;
   char const *rep;
   union {void const *cvp; up flags; char const *ccp;} u;
   union {struct a_ml_regex *mlrp; up flags; void *vp;} rv;
   NYD_IN;

   if((rv.mlrp = su_TALLOCF(struct a_ml_regex, 1, estate)) != NIL){
      u.cvp = t;
      u.flags &= ~TRU1;
      rep = u.ccp;
      u.cvp = t;

      if((s = regcomp(&rv.mlrp->mlr_regex, rep,
               REG_EXTENDED | REG_ICASE | REG_NOSUB)) == 0){
         rv.mlrp->mlr_last = NIL;
         mlrpp = (u.flags & TRU1) ? &a_ml_re_sub : &a_ml_re_def;
         if((rv.mlrp->mlr_next = *mlrpp) != NIL)
            rv.mlrp->mlr_next->mlr_last = rv.mlrp;
         *mlrpp = rv.mlrp;

         rv.flags |= (u.flags & TRU1);
      }else{
         n_err(_("%s: invalid regular expression: %s: %s\n"),
            (u.flags & TRU1 ? "mlsubscribe" : "mlist"),
            n_shexp_quote_cp(rep, FAL0), n_regex_err_to_doc(NIL, s));
         su_FREE(rv.mlrp);
         su_err_set_no(su_ERR_INVAL);
         rv.vp = NIL;
      }
   }

   NYD_OU;
   return rv.vp;
}

static void
a_ml_re_delete(void *self){
   struct a_ml_regex **lpp, *lstnp, *nxtnp;
   union {void *vp; up flags; struct a_ml_regex *mlrp;} u;
   NYD_IN;

   u.vp = self;

   if(u.flags & TRU1){
      u.flags &= ~TRU1;
      lpp = &a_ml_re_sub;
   }else
      lpp = &a_ml_re_def;

   lstnp = u.mlrp->mlr_last;
   nxtnp = u.mlrp->mlr_next;
   if(u.mlrp == *lpp)
      *lpp = nxtnp;
   else
      lstnp->mlr_next = nxtnp;
   if(nxtnp != NIL)
      nxtnp->mlr_last = lstnp;

   regfree(&u.mlrp->mlr_regex);
   su_FREE(u.mlrp);
   NYD_OU;
}

static void *
a_ml_re_assign(void *self, void const *t, u32 estate){
   /* Thanks to NILISVALO we can (mis)use assignment for the sole purpose of
    * flipping the subscription bit and performing list relinking! */
   struct a_ml_regex **lpp, *lstnp, *nxtnp;
   union {void *vp; void const *cvp; up flags; struct a_ml_regex *mlrp;} u;
   NYD_IN;
   UNUSED(t);
   UNUSED(estate);

   /* Out old */
   u.vp = self;

   if(u.flags & TRU1){
      u.flags &= ~TRU1;
      self = u.vp;
      lpp = &a_ml_re_sub;
   }else
      lpp = &a_ml_re_def;

   lstnp = u.mlrp->mlr_last;
   nxtnp = u.mlrp->mlr_next;
   if(u.mlrp == *lpp)
      *lpp = nxtnp;
   else
      lstnp->mlr_next = nxtnp;
   if(nxtnp != NIL)
      nxtnp->mlr_last = lstnp;

   /* In new */
   u.cvp = t;

   if(u.flags & TRU1){
      u.vp = self;
      u.flags |= TRU1;
      self = u.vp;
      u.flags ^= TRU1;
      lpp = &a_ml_re_sub;
   }else{
      u.vp = self;
      ASSERT((u.flags & TRU1) == 0);
      lpp = &a_ml_re_def;
   }

   u.mlrp->mlr_last = NIL;
   if((u.mlrp->mlr_next = nxtnp = *lpp) != NIL)
      nxtnp->mlr_last = u.mlrp;
   *lpp = u.mlrp;

   NYD_OU;
   return self;
}
#endif /* mx_HAVE_REGEX */

int
c_mlist(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ml_mux(FAL0, vp);
   NYD_OU;
   return rv;
}

int
c_unmlist(void *vp){
   char const **argv, *key;
   int rv;
   NYD_IN;

   rv = 0;

   for(argv = S(char const**,vp); (key = *argv) != NIL; ++argv){
      if(key[1] == '\0' && key[0] == '*'){
         if(a_ml_dp != NIL)
            su_cs_dict_clear(a_ml_dp);
#ifdef mx_HAVE_REGEX
         if(a_ml_re_dp != NIL)
            su_cs_dict_clear(a_ml_re_dp);
#endif
      }else if(a_ml_dp != NIL && su_cs_dict_remove(a_ml_dp, key))
         ;
#ifdef mx_HAVE_REGEX
      else if(a_ml_re_dp != NIL && su_cs_dict_remove(a_ml_re_dp, key))
         ;
#endif
      else{
         n_err(_("No such `mlist': %s\n"), n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }
   }

   NYD_OU;
   return rv;
}

int
c_mlsubscribe(void *vp){
   int rv;
   NYD_IN;

   rv = !a_ml_mux(TRU1, vp);
   NYD_OU;
   return rv;
}

int
c_unmlsubscribe(void *vp){
   struct su_cs_dict_view dv;
   union {void *vp; up flags;} u;
   char const **argv, *key;
   int rv;
   NYD_IN;

   rv = 0;

   for(argv = S(char const**,vp); (key = *argv) != NIL; ++argv){
      if(key[1] == '\0' && key[0] == '*'){
         if(a_ml_dp != NIL)
            a_ml_unsub_all(&dv, a_ml_dp);
#ifdef mx_HAVE_REGEX
         if(a_ml_re_dp != NIL)
            a_ml_unsub_all(&dv, a_ml_re_dp);
#endif
      }else if(a_ml_dp != NIL &&
            su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_ml_dp), key)){
         goto jtest;
jtest:
         u.vp = su_cs_dict_view_data(&dv);
         if(u.flags & TRU1){
            u.flags ^= TRU1;
            su_cs_dict_view_set_data(&dv, u.vp);
         }else
            goto jenot;
#ifdef mx_HAVE_REGEX
      }else if(a_ml_re_dp != NIL &&
            su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_ml_re_dp), key)){
         goto jtest;
#endif
      }else{
jenot:
         n_err(_("No such `mlsubscribe': %s\n"), n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }
   }

   NYD_OU;
   return rv;
}

enum mx_mlist_type
mx_mlist_query(char const *name, boole subscribed_only){
   struct su_cs_dict_view dv;
   union {void *vp; void const *cvp; up flags;} u;
   enum mx_mlist_type rv;
   NYD_IN;

   rv = mx_MLIST_OTHER;

   /* Direct address match? */
   if(a_ml_dp != NIL &&
         su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_ml_dp), name)){
      u.cvp = su_cs_dict_view_data(&dv);
      if(u.flags & TRU1)
         rv = mx_MLIST_SUBSCRIBED;
      else if(!subscribed_only)
         rv = mx_MLIST_KNOWN;
   }
   /* With regex support, walk subscribed and then normal list thereafter */
#ifdef mx_HAVE_REGEX
   else{
      struct a_ml_regex **lpp, *mlrp, *lstnp, *nxtnp;

      lpp = &a_ml_re_sub;
      rv = mx_MLIST_SUBSCRIBED;
jregex_redo:
      if((mlrp = *lpp) != NIL){
         do if(regexec(&mlrp->mlr_regex, name, 0,NIL, 0) != REG_NOMATCH){
            /* Relink head */
            if(mlrp != *lpp){
               lstnp = mlrp->mlr_last;
               nxtnp = mlrp->mlr_next;
               if((lstnp->mlr_next = nxtnp) != NIL)
                  nxtnp->mlr_last = lstnp;

               mlrp->mlr_last = NIL;
               (mlrp->mlr_next = *lpp)->mlr_last = mlrp;
               *lpp = mlrp;
            }
            goto jregex_leave;
         }while((mlrp = mlrp->mlr_next) != NIL);
      }

      if(rv == mx_MLIST_SUBSCRIBED && !subscribed_only){
         rv = mx_MLIST_KNOWN;
         lpp = &a_ml_re_def;
         goto jregex_redo;
      }

      rv = mx_MLIST_OTHER;
jregex_leave:;
   }
#endif /* mx_HAVE_REGEX */

   NYD_OU;
   return rv;
}

enum mx_mlist_type
mx_mlist_query_mp(struct message *mp, enum mx_mlist_type what){
   /* XXX mlist_query_mp() possibly belongs to message or header instead */
   struct mx_name *np;
   boole cc;
   enum mx_mlist_type rv;
   NYD_IN;
   ASSERT(what != mx_MLIST_POSSIBLY);

   rv = mx_MLIST_OTHER;

   cc = FAL0;
   np = lextract(hfield1("to", mp), GTO | GSKIN);
jredo:
   for(; np != NIL; np = np->n_flink){
      switch(mx_mlist_query(np->n_name, FAL0)){
      case mx_MLIST_OTHER:
      case mx_MLIST_POSSIBLY: /* (appease $CC) */
         break;
      case mx_MLIST_KNOWN:
         if(what == mx_MLIST_KNOWN || what == mx_MLIST_OTHER){
            if(rv == mx_MLIST_OTHER)
               rv = mx_MLIST_KNOWN;
            if(what == mx_MLIST_KNOWN)
               goto jleave;
         }
         break;
      case mx_MLIST_SUBSCRIBED:
         if(what == mx_MLIST_SUBSCRIBED || what == mx_MLIST_OTHER){
            if(rv != mx_MLIST_SUBSCRIBED)
               rv = mx_MLIST_SUBSCRIBED;
            goto jleave;
         }
         break;
      }
   }

   if(!cc){
      cc = TRU1;
      np = lextract(hfield1("cc", mp), GCC | GSKIN);
      goto jredo;
   }

   if(what == mx_MLIST_OTHER && mx_header_list_post_of(mp) != NIL)
      rv = mx_MLIST_POSSIBLY;

jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
