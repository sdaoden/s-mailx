/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-filetype.h.
 *@ TODO Simply use su_string data via "LOAD\0SAVE[\0]" values, then drop all
 *@ TODO the toolbox and such stuff in here!
 *@ TODO _FT_ -> _CFT
 *
 * Copyright (c) 2017 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_filetype
#define mx_SOURCE
#define mx_SOURCE_CMD_FILETYPE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/cmd.h"

#include "mx/cmd-filetype.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* ..of a_ft_dp */
#define a_FT_FLAGS (su_CS_DICT_OWNS | su_CS_DICT_CASE |\
      su_CS_DICT_HEAD_RESORT | su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_FT_TRESHOLD_SHIFT 4

struct a_ft_dat{
   struct str ftd_load;
   struct str ftd_save;
};

static struct mx_filetype const a_ft_OBSOLETE_xz = { /* TODO v15 compat */
   "xz", 2,
      "xz -cd", sizeof("xz -cd") -1,
      "xz -cz", sizeof("xz -cz") -1
}, a_ft_OBSOLETE_gz = {
   "gz", 2,
      "gzip -cd", sizeof("gzip -cd") -1,
      "gzip -cz", sizeof("gzip -cz") -1
}, a_ft_OBSOLETE_bz2 = {
   "bz2", 3,
      "bzip2 -cd", sizeof("bzip2 -cd") -1,
      "bzip2 -cz", sizeof("bzip2 -cz") -1
};

/* +toolbox below */

static struct su_cs_dict *a_ft_dp, a_ft__d; /* XXX atexit _gut() (DVL()) */

/* */
static void *a_ft_clone(void const *t, u32 estate);
#if DVLOR(1, 0)
static void a_ft_delete(void *self);
#else
# define a_ft_delete su_mem_free
#endif
static void *a_ft_assign(void *self, void const *t, u32 estate);

static struct su_toolbox const a_ft_tbox = su_TOOLBOX_I9R(
   &a_ft_clone, &a_ft_delete, &a_ft_assign, NIL, NIL
);

/* */
static struct n_strlist *a_ft_dump(char const *cmdname, char const *key,
      void const *dat);

static void *
a_ft_clone(void const *t, u32 estate){
   char *cp;
   uz l;
   struct a_ft_dat *rv;
   struct a_ft_dat const *tp;
   NYD_IN;

   estate &= su_STATE_ERR_MASK;

   /* The public entry ensures this fits U32_MAX! */
   tp = S(struct a_ft_dat const*,t);
   l = sizeof(*rv) + tp->ftd_load.l +1 + tp->ftd_save.l +1;

   if((rv = S(struct a_ft_dat*,su_ALLOCATE(l, 1, estate))) != NIL){
      cp = S(char*,&rv[1]);
      su_mem_copy(rv->ftd_load.s = cp, tp->ftd_load.s,
         (rv->ftd_load.l = tp->ftd_load.l) +1);
      cp += tp->ftd_load.l +1;
      su_mem_copy(rv->ftd_save.s = cp, tp->ftd_save.s,
         (rv->ftd_save.l = tp->ftd_save.l) +1);
   }

   NYD_OU;
   return rv;
}

#if DVLOR(1, 0)
static void
a_ft_delete(void *self){
   NYD_IN;

   su_FREE(self);

   NYD_OU;
}
#endif

static void *
a_ft_assign(void *self, void const *t, u32 estate){
   void *rv;
   NYD_IN;

   if((rv = a_ft_clone(t, estate)) != NIL)
      su_FREE(self);

   NYD_OU;
   return rv;
}

static struct n_strlist *
a_ft_dump(char const *cmdname, char const *key, void const *dat){
   /* XXX real strlist + str_to_fmt() */
   char *cp;
   struct n_strlist *slp;
   uz kl, dloadl, dsavel, cl;
   char const *kp, *dloadp, *dsavep;
   struct a_ft_dat const *ftdp;
   NYD2_IN;

   ftdp = S(struct a_ft_dat const*,dat);
   kp = n_shexp_quote_cp(key, TRU1);
   dloadp = n_shexp_quote_cp(ftdp->ftd_load.s, TRU1);
   dsavep = n_shexp_quote_cp(ftdp->ftd_save.s, TRU1);
   kl = su_cs_len(kp);
   dloadl = su_cs_len(dloadp);
   dsavel = su_cs_len(dsavep);
   cl = su_cs_len(cmdname);

   slp = n_STRLIST_AUTO_ALLOC(cl + 1 + kl + 1 + dloadl + 1 + dsavel +1);
   slp->sl_next = NIL;
   cp = slp->sl_dat;
   su_mem_copy(cp, cmdname, cl);
   cp += cl;
   *cp++ = ' ';
   su_mem_copy(cp, kp, kl);
   cp += kl;
   *cp++ = ' ';
   su_mem_copy(cp, dloadp, dloadl);
   cp += dloadl;
   *cp++ = ' ';
   su_mem_copy(cp, dsavep, dsavel);
   cp += dsavel;
   *cp = '\0';
   slp->sl_len = P2UZ(cp - slp->sl_dat);

   NYD2_OU;
   return slp;
}

int
c_filetype(void *vp){ /* TODO support auto chains: .tar.gz -> .gz + .tar */
   struct a_ft_dat ftd;
   struct su_cs_dict_view dv;
   int rv;
   char const **argv, *key;
   NYD_IN;

   if((key = *(argv = vp)) == NIL){
      struct n_strlist *slp;

      slp = NIL;
      rv = !(mx_xy_dump_dict("filetype", a_ft_dp, &slp, NIL, &a_ft_dump) &&
            mx_page_or_print_strlist("filetype", slp, FAL0));
      goto jleave;
   }

   if(argv[1] == NIL){
      if(a_ft_dp != NIL &&
            su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_ft_dp), key)){
         struct n_strlist *slp;

         slp = a_ft_dump("filetype", su_cs_dict_view_key(&dv),
               su_cs_dict_view_data(&dv));
         rv = (fputs(slp->sl_dat, n_stdout) == EOF);
         rv |= (putc('\n', n_stdout) == EOF);
      }else{
         n_err(_("No such filetype: %s\n"), n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }
   }else{
      if(a_ft_dp == NIL)
         a_ft_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_ft__d, a_FT_FLAGS, &a_ft_tbox),
               a_FT_TRESHOLD_SHIFT);

      rv = 0;
      do{ /* while(*(argv += 3) != NIL); */
         uz l;

         if(argv[1] == NIL || argv[2] == NIL){
            mx_cmd_print_synopsis(mx_cmd_firstfit("filetype"), NIL);
            rv = 1;
            break;
         }

         ftd.ftd_load.l = l = su_cs_len(
               ftd.ftd_load.s = UNCONST(char*,argv[1]));
         ftd.ftd_save.l = su_cs_len(ftd.ftd_save.s = UNCONST(char*,argv[2]));
         if(U32_MAX -2 <= l || U32_MAX -2 - l <= ftd.ftd_save.l ||
               U32_MAX -2 - (l += ftd.ftd_save.l) <= sizeof ftd)
            goto jerr;

         if(su_cs_dict_replace(a_ft_dp, key, &ftd) > 0){
jerr:
            n_err(_("Failed to create `filetype' storage: %s\n"),
               n_shexp_quote_cp(key, FAL0));
            rv = 1;
         }
      }while((key = *(argv += 3)) != NIL);
   }

jleave:
   NYD_OU;
   return rv;
}

int
c_unfiletype(void *vp){
   int rv;
   NYD_IN;

   rv = !mx_unxy_dict("filetype", a_ft_dp, vp);

   NYD_OU;
   return rv;
}

boole
mx_filetype_trial(struct mx_filetype *res_or_nil, char const *file){
   struct stat stb;
   struct su_cs_dict_view dv;
   struct n_string s_b, *s;
   u32 l;
   NYD_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_assign_cp(s, file); /* XXX enomem++ */
   s = n_string_push_c(s, '.');
   l = s->s_len;

   if(a_ft_dp != NIL){
      su_CS_DICT_FOREACH(a_ft_dp, &dv){
         s = n_string_trunc(s, l);
         s = n_string_push_buf(s, su_cs_dict_view_key(&dv),
               su_cs_dict_view_key_len(&dv));

         if(!stat(n_string_cp(s), &stb) && S_ISREG(stb.st_mode)){
            if(res_or_nil != NIL){
               struct a_ft_dat *ftdp;

               ftdp = S(struct a_ft_dat*,su_cs_dict_view_data(&dv));
               res_or_nil->ft_ext_dat = su_cs_dict_view_key(&dv);
               res_or_nil->ft_ext_len = su_cs_dict_view_key_len(&dv);
               res_or_nil->ft_load_dat = ftdp->ftd_load.s;
               res_or_nil->ft_load_len = ftdp->ftd_load.l;
               res_or_nil->ft_save_dat = ftdp->ftd_save.s;
               res_or_nil->ft_save_len = ftdp->ftd_save.l;
            }
            goto jleave; /* TODO after v15 legacy drop: break; */
         }
      }
   }

   /* TODO v15 legacy code: automatic file hooks for .{bz2,gz,xz},
    * TODO but NOT supporting *file-hook-{load,save}-EXTENSION* */
   s = n_string_trunc(s, l);
   s = n_string_push_buf(s, a_ft_OBSOLETE_xz.ft_ext_dat,
         a_ft_OBSOLETE_xz.ft_ext_len);
   if(!stat(n_string_cp(s), &stb) && S_ISREG(stb.st_mode)){
      n_OBSOLETE("auto .xz support vanishes, please use `filetype' command");
      if(res_or_nil != NIL)
         *res_or_nil = a_ft_OBSOLETE_xz;
      goto jleave;
   }

   s = n_string_trunc(s, l);
   s = n_string_push_buf(s, a_ft_OBSOLETE_gz.ft_ext_dat,
         a_ft_OBSOLETE_gz.ft_ext_len);
   if(!stat(n_string_cp(s), &stb) && S_ISREG(stb.st_mode)){
      n_OBSOLETE("auto .gz support vanishes, please use `filetype' command");
      if(res_or_nil != NIL)
         *res_or_nil = a_ft_OBSOLETE_gz;
      goto jleave;
   }

   s = n_string_trunc(s, l);
   s = n_string_push_buf(s, a_ft_OBSOLETE_bz2.ft_ext_dat,
         a_ft_OBSOLETE_bz2.ft_ext_len);
   if(!stat(n_string_cp(s), &stb) && S_ISREG(stb.st_mode)){
      n_OBSOLETE("auto .bz2 support vanishes, please use `filetype' command");
      if(res_or_nil != NIL)
         *res_or_nil = a_ft_OBSOLETE_bz2;
      goto jleave;
   }

   file = NIL;
jleave:
   /* n_string_gut(s); */

   NYD_OU;
   return (file != NIL);
}

boole
mx_filetype_exists(struct mx_filetype *res_or_nil, char const *file){
   struct su_cs_dict_view dv, *dvp;
   char const *lext, *ext;
   NYD_IN;

   dvp = (a_ft_dp != NIL) ? su_cs_dict_view_setup(&dv, a_ft_dp) : NIL;
   lext = NIL;

   if((ext = su_cs_rfind_c(file, '/')) != NIL)
      file = ++ext;

   for(; (ext = su_cs_find_c(file, '.')) != NIL; lext = file = ext){
      ++ext;

      if(dvp != NIL && su_cs_dict_view_find(dvp, ext)){
         lext = ext; /* return value */
         if(res_or_nil != NIL){
            struct a_ft_dat *ftdp;

            ftdp = S(struct a_ft_dat*,su_cs_dict_view_data(&dv));
            res_or_nil->ft_ext_dat = su_cs_dict_view_key(&dv);
            res_or_nil->ft_ext_len = su_cs_dict_view_key_len(&dv);
            res_or_nil->ft_load_dat = ftdp->ftd_load.s;
            res_or_nil->ft_load_len = ftdp->ftd_load.l;
            res_or_nil->ft_save_dat = ftdp->ftd_save.s;
            res_or_nil->ft_save_len = ftdp->ftd_save.l;
         }
         goto jleave; /* TODO after v15 legacy drop: break; */
      }
   }

   /* TODO v15 legacy code: automatic file hooks for .{bz2,gz,xz},
    * TODO as well as supporting *file-hook-{load,save}-EXTENSION* */
   if(lext == NIL)
      goto jleave;

   if(!su_cs_cmp_case(lext, "xz")){
      n_OBSOLETE("auto .xz support vanishes, please use `filetype' command");
      if(res_or_nil != NIL)
         *res_or_nil = a_ft_OBSOLETE_xz;
      goto jleave;
   }else if(!su_cs_cmp_case(lext, "gz")){
      n_OBSOLETE("auto .gz support vanishes, please use `filetype' command");
      if(res_or_nil != NIL)
         *res_or_nil = a_ft_OBSOLETE_gz;
      goto jleave;
   }else if(!su_cs_cmp_case(lext, "bz2")){
      n_OBSOLETE("auto .bz2 support vanishes, please use `filetype' command");
      if(res_or_nil != NIL)
         *res_or_nil = a_ft_OBSOLETE_bz2;
      goto jleave;
   }else{
      char const *cload, *csave;
      char *vbuf;
      uz l;

#undef a_X1
#define a_X1 "file-hook-load-"
#undef a_X2
#define a_X2 "file-hook-save-"
      l = su_cs_len(lext);
      vbuf = su_LOFI_ALLOC(l + MAX(sizeof(a_X1), sizeof(a_X2)));

      su_mem_copy(vbuf, a_X1, sizeof(a_X1) -1);
      su_mem_copy(&vbuf[sizeof(a_X1) -1], lext, l);
      vbuf[sizeof(a_X1) -1 + l] = '\0';
      cload = n_var_vlook(vbuf, FAL0);

      su_mem_copy(vbuf, a_X2, sizeof(a_X2) -1);
      su_mem_copy(&vbuf[sizeof(a_X2) -1], lext, l);
      vbuf[sizeof(a_X2) -1 + l] = '\0';
      csave = n_var_vlook(vbuf, FAL0);

      su_LOFI_FREE(vbuf);
#undef a_X2
#undef a_X1

      if((csave != NIL) | (cload != NIL)){
         n_OBSOLETE("*file-hook-{load,save}-EXTENSION* will vanish, "
            "please use the `filetype' command");

         if(((csave != NIL) ^ (cload != NIL)) == 0){
            if(res_or_nil != NIL){
               res_or_nil->ft_ext_dat = lext;
               res_or_nil->ft_ext_len = l;
               res_or_nil->ft_load_dat = cload;
               res_or_nil->ft_load_len = su_cs_len(cload);
               res_or_nil->ft_save_dat = csave;
               res_or_nil->ft_save_len = su_cs_len(csave);
            }
            goto jleave;
         }else
            n_alert(_("Incomplete *file-hook-{load,save}-EXTENSION* for: .%s"),
               lext);
      }
   }

   lext = NIL;
jleave:
   NYD_OU;
   return (lext != NIL);
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_FILETYPE
/* s-it-mode */
