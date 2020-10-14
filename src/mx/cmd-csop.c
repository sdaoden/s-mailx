/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-csop.h.
 *@ TODO - better commandline parser that can dive into subcommands could
 *@ TODO   get rid of a lot of ERR_SYNOPSIS cruft.
 *@ TODO - _CSOP -> _CCSOP
 *
 * Copyright (c) 2017 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_csop
#define mx_SOURCE
#define mx_SOURCE_CMD_CSOP

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_CMD_CSOP

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/cmd.h"

#include "mx/cmd-csop.h"
#include "su/code-in.h"

enum a_csop_cmd{
   a_CSOP_CMD_LENGTH,
   a_CSOP_CMD_HASH32,
   a_CSOP_CMD_HASH,
   a_CSOP_CMD_FIND,
   a_CSOP_CMD_IFIND, /* v15compat */
   a_CSOP_CMD_SUBSTRING,
   a_CSOP_CMD_TRIM,
   a_CSOP_CMD_TRIM_FRONT,
   a_CSOP_CMD_TRIM_END,
   a_CSOP_CMD__MAX
};

enum a_csop_err{
   a_CSOP_ERR_NONE,
   a_CSOP_ERR_SYNOPSIS,
   a_CSOP_ERR_SUBCMD,
   a_CSOP_ERR_MOD_NOT_ALLOWED,
   a_CSOP_ERR_NUM_RANGE,
   a_CSOP_ERR_STR_OVERFLOW,
   a_CSOP_ERR_STR_NODATA,
   a_CSOP_ERR_STR_GENERIC
};
enum {a_CSOP_ERR__MAX = a_CSOP_ERR_STR_GENERIC};

CTA(S(uz,a_CSOP_CMD__MAX | a_CSOP_ERR__MAX) <= 0x7Fu, "Bit range excess");

enum a_csop_flags{
   a_CSOP_NONE,
   a_CSOP_ERR = 1u<<0, /* There was an error */
   a_CSOP_SOFTOVERFLOW = 1u<<1,
   a_CSOP_ISNUM = 1u<<2,
   a_CSOP_ISDECIMAL = 1u<<3, /* Print only decimal result */
   a_CSOP_MOD_CASE = 1u<<4, /* Case-insensitive / XY */
   a_CSOP_MOD_MASK = a_CSOP_MOD_CASE,

   a_CSOP__FMASK = 0x1FFu,
   a_CSOP__FSHIFT = 9u,
   a_CSOP__FCMDMASK = 0xFE00u,
   a_CSOP__TMP = 1u<<30
};
/* .csc_cmderr=8-bit, and so a_csop_subcmd can store CMD+MOD flags in 16-bit */
CTA(((S(u32,a_CSOP_CMD__MAX | a_CSOP_ERR__MAX) << a_CSOP__FSHIFT) &
   ~a_CSOP__FCMDMASK) == 0, "Bit ranges overlap");

struct a_csop_ctx{
   u32 csc_flags;
   u8 csc_cmderr; /* On input, a_vexpr_cmd, on output (maybe) a_vexpr_err */
   boole csc_cm_local; /* `local' command modifier */
   u8 csc__pad[2];
   char const **csc_argv;
   char const *csc_cmd_name;
   char const *csc_varname; /* `vput' command modifier */
   char const *csc_varres;
   char const *csc_arg; /* The current arg (_ERR: which caused failure) */
   s64 csc_lhv;
   char csc_iencbuf[2+1/* BASE# prefix*/ + su_IENC_BUFFER_SIZE + 1];
};

struct a_csop_subcmd{
   u16 css_mpv;
   char css_name[14];
};

static struct a_csop_subcmd const a_csop_subcmds[] = {
#undef a_X
#define a_X(C,F) (S(u16,C) << a_CSOP__FSHIFT) | F

   {a_X(a_CSOP_CMD_LENGTH, 0), "length"},
   {a_X(a_CSOP_CMD_HASH, a_CSOP_MOD_CASE), "hash"},
   {a_X(a_CSOP_CMD_HASH32, a_CSOP_MOD_CASE), "hash32"},
   {a_X(a_CSOP_CMD_FIND, a_CSOP_MOD_CASE), "find"},
   {a_X(a_CSOP_CMD_IFIND, 0), "ifind"},
   {a_X(a_CSOP_CMD_SUBSTRING, 0), "substring"},
   {a_X(a_CSOP_CMD_TRIM, 0), "trim"},
   {a_X(a_CSOP_CMD_TRIM_FRONT, 0), "trim-front\0"},
   {a_X(a_CSOP_CMD_TRIM_END, 0), "trim-end"}

#undef a_X
};

/* Entered with .vc_flags=NONE(|MOD_CASE)? */
static void a_csop_cmd(struct a_csop_ctx *cscp);

static void
a_csop_cmd(struct a_csop_ctx *cscp){
   uz i;
   NYD2_IN;

   switch(cscp->csc_cmderr){
   default:
   case a_CSOP_CMD_LENGTH:
      cscp->csc_flags |= a_CSOP_ISNUM | a_CSOP_ISDECIMAL;
      if(cscp->csc_argv[0] == NIL || cscp->csc_argv[1] != NIL){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_SYNOPSIS;
         break;
      }
      cscp->csc_arg = cscp->csc_argv[0];

      i = su_cs_len(cscp->csc_arg);
      if(UCMP(64, i, >, S64_MAX)){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_STR_OVERFLOW;
         break;
      }
      cscp->csc_lhv = S(s64,i);
      break;

   case a_CSOP_CMD_HASH32:
   case a_CSOP_CMD_HASH:
      cscp->csc_flags |= a_CSOP_ISNUM | a_CSOP_ISDECIMAL;
      if(cscp->csc_argv[0] == NIL || cscp->csc_argv[1] != NIL){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_SYNOPSIS;
         break;
      }
      cscp->csc_arg = cscp->csc_argv[0];

      i = ((cscp->csc_flags & a_CSOP_MOD_CASE)
            ? su_cs_toolbox_case.tb_hash : su_cs_toolbox.tb_hash
            )(cscp->csc_arg);
      if(cscp->csc_cmderr == a_CSOP_CMD_HASH32)
         i = S(u32,i & U32_MAX);
      cscp->csc_lhv = S(s64,i);
      break;

   case a_CSOP_CMD_IFIND:
      n_OBSOLETE(_("csop: ifind: simply use find?[case] instead, please"));
      cscp->csc_flags |= a_CSOP_MOD_CASE;
      /* FALLTHRU */
   case a_CSOP_CMD_FIND:
      cscp->csc_flags |= a_CSOP_ISNUM | a_CSOP_ISDECIMAL;
      if(cscp->csc_argv[0] == NIL || cscp->csc_argv[1] == NIL ||
            cscp->csc_argv[2] != NIL){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_SYNOPSIS;
         break;
      }
      cscp->csc_arg = cscp->csc_argv[1];

      cscp->csc_varres = ((cscp->csc_flags & a_CSOP_MOD_CASE)
            ? su_cs_find_case : su_cs_find)(cscp->csc_argv[0], cscp->csc_arg);
      if(cscp->csc_varres == NIL){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_STR_NODATA;
         break;
      }
      i = P2UZ(cscp->csc_varres - cscp->csc_argv[0]);
      if(UCMP(64, i, >, S64_MAX)){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_STR_OVERFLOW;
         break;
      }
      cscp->csc_lhv = S(s64,i);
      break;

   case a_CSOP_CMD_SUBSTRING:
      if(cscp->csc_argv[0] == NIL || (cscp->csc_argv[1] != NIL &&
            (cscp->csc_argv[2] != NIL && cscp->csc_argv[3] != NIL))){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_SYNOPSIS;
         break;
      }
      cscp->csc_varres =
      cscp->csc_arg = cscp->csc_argv[0];

      i = su_cs_len(cscp->csc_arg);

      /* Offset */
      if(cscp->csc_argv[1] == NIL || cscp->csc_argv[1][0] == '\0')
         cscp->csc_lhv = 0;
      else if((su_idec_s64_cp(&cscp->csc_lhv, cscp->csc_argv[1], 0, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_NUM_RANGE;
         break;
      }else if(cscp->csc_lhv < 0){
         if(UCMP(64, i, <, -cscp->csc_lhv)){
            cscp->csc_flags |= a_CSOP_ERR;
            cscp->csc_cmderr = a_CSOP_ERR_NUM_RANGE;
            goto jesubstring_off;
         }
         cscp->csc_lhv += i;
      }

      if(LIKELY(UCMP(64, i, >=, cscp->csc_lhv))){
         i -= cscp->csc_lhv;
         cscp->csc_varres += cscp->csc_lhv;
      }else{
jesubstring_off:
         if(n_poption & n_PO_D_V)
            n_err(_("vexpr: substring: offset argument too large: %s\n"),
               n_shexp_quote_cp(cscp->csc_arg, FAL0));
         cscp->csc_flags |= a_CSOP_SOFTOVERFLOW;
      }

      /* Length */
      if(cscp->csc_argv[2] != NIL){
         if(cscp->csc_argv[2][0] == '\0')
            cscp->csc_lhv = 0;
         else if((su_idec_s64_cp(&cscp->csc_lhv, cscp->csc_argv[2], 0, NIL
                  ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
               ) != su_IDEC_STATE_CONSUMED){
            cscp->csc_flags |= a_CSOP_ERR;
            cscp->csc_cmderr = a_CSOP_ERR_NUM_RANGE;
            break;
         }else if(cscp->csc_lhv < 0){
            if(UCMP(64, i, <, -cscp->csc_lhv)){
               goto jesubstring_len;
            }
            cscp->csc_lhv += i;
         }

         if(UCMP(64, i, >=, cscp->csc_lhv)){
            if(UCMP(64, i, !=, cscp->csc_lhv))
               cscp->csc_varres =
                     savestrbuf(cscp->csc_varres, S(uz,cscp->csc_lhv));
         }else{
jesubstring_len:
            if(n_poption & n_PO_D_V)
               n_err(_("vexpr: substring: length argument too large: %s\n"),
                  n_shexp_quote_cp(cscp->csc_arg, FAL0));
            cscp->csc_flags |= a_CSOP_SOFTOVERFLOW;
         }
      }
      break;

   case a_CSOP_CMD_TRIM:{
      struct str trim;
      enum n_str_trim_flags stf;

      stf = n_STR_TRIM_BOTH;
      if(0){
   case a_CSOP_CMD_TRIM_FRONT:
         stf = n_STR_TRIM_FRONT;
      }else if(0){
   case a_CSOP_CMD_TRIM_END:
         stf = n_STR_TRIM_END;
      }

      if(cscp->csc_argv[0] == NIL || cscp->csc_argv[1] != NIL){
         cscp->csc_flags |= a_CSOP_ERR;
         cscp->csc_cmderr = a_CSOP_ERR_SYNOPSIS;
         break;
      }
      cscp->csc_arg = cscp->csc_argv[0];

      trim.l = su_cs_len(trim.s = UNCONST(char*,cscp->csc_arg));
      (void)n_str_trim(&trim, stf);
      cscp->csc_varres = savestrbuf(trim.s, trim.l);
      }break;
   }

   NYD2_OU;
}

int
c_csop(void *vp){
   struct a_csop_ctx csc;
   char const *cp;
   u32 f;
   uz i, j;
   NYD_IN;

   DVL( su_mem_set(&csc, 0xAA, sizeof csc); )
   csc.csc_flags = a_CSOP_ERR;
   csc.csc_cmderr = a_CSOP_ERR_SUBCMD;
   csc.csc_cm_local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);
   csc.csc_argv = S(char const**,vp);
   csc.csc_varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *csc.csc_argv++ : NIL;
   csc.csc_arg =
   csc.csc_cmd_name = *csc.csc_argv++;
   csc.csc_varres = su_empty;

   if((cp = su_cs_find_c(csc.csc_cmd_name, '?')) != NIL){
      j = P2UZ(cp - csc.csc_cmd_name);
      if(cp[1] != '\0' && !su_cs_starts_with_case("case", &cp[1])){
         n_err(_("csop: invalid modifier used: %s\n"),
            n_shexp_quote_cp(csc.csc_cmd_name, FAL0));
         f = a_CSOP_ERR;
         goto jleave;
      }
      f = a_CSOP_MOD_CASE;
   }else{
      f = a_CSOP_NONE;
      if(*csc.csc_cmd_name == '@'){ /* v15compat */
         n_OBSOLETE2(_("csop: please use ? modifier suffix, "
               "not @ prefix"), n_shexp_quote_cp(csc.csc_cmd_name, FAL0));
         ++csc.csc_cmd_name;
         f = a_CSOP_MOD_CASE;
      }
      j = su_cs_len(csc.csc_cmd_name);
   }

   for(i = 0; i < NELEM(a_csop_subcmds); ++i){
      if(su_cs_starts_with_case_n(a_csop_subcmds[i].css_name,
            csc.csc_cmd_name, j)){
         csc.csc_cmd_name = a_csop_subcmds[i].css_name;
         i = a_csop_subcmds[i].css_mpv;

         if(UNLIKELY(f & a_CSOP_MOD_MASK)){
            /*u32 f2;

            f2 = f & a_CSOP_MOD_MASK;*/

            if(UNLIKELY(!(i & a_CSOP_MOD_MASK))){
               csc.csc_cmderr = a_CSOP_ERR_MOD_NOT_ALLOWED;
               break;
            /*
            }else if(UNLIKELY(f2 != a_CSOP_MOD_MASK &&
                  f2 != (i & a_CSOP_MOD_MASK))){
               csc.csc_cmderr = a_CSOP_ERR_MOD_NOT_SUPPORTED;
               break;
            */
            }
         }

         csc.csc_arg = csc.csc_cmd_name;
         csc.csc_flags = f;
         i = (i & a_CSOP__FCMDMASK) >> a_CSOP__FSHIFT;
         csc.csc_cmderr = S(u8,i);
         a_csop_cmd(&csc);
         break;
      }
   }
   f = csc.csc_flags;

   if(LIKELY(!(f & a_CSOP_ERR))){
      n_pstate_err_no = (f & a_CSOP_SOFTOVERFLOW)
            ? su_ERR_OVERFLOW : su_ERR_NONE;
   }else switch(csc.csc_cmderr){
   case a_CSOP_ERR_NONE:
      ASSERT(0);
      break;
   case a_CSOP_ERR_SYNOPSIS:
      mx_cmd_print_synopsis(mx_cmd_firstfit("csop"), NIL);
      n_pstate_err_no = su_ERR_INVAL;
      goto jenum;
   case a_CSOP_ERR_SUBCMD:
      n_err(_("csop: invalid subcommand: %s\n"),
         n_shexp_quote_cp(csc.csc_arg, FAL0));
      n_pstate_err_no = su_ERR_INVAL;
      goto jenum;
   case a_CSOP_ERR_MOD_NOT_ALLOWED:
      n_err(_("csop: modifiers not allowed for subcommand: %s\n"),
         n_shexp_quote_cp(csc.csc_arg, FAL0));
      n_pstate_err_no = su_ERR_INVAL;
      goto jenum;
   case a_CSOP_ERR_NUM_RANGE:
      n_err(_("csop: numeric argument invalid or out of range: %s\n"),
         n_shexp_quote_cp(csc.csc_arg, FAL0));
      n_pstate_err_no = su_ERR_RANGE;
      goto jenum;
   default:
jenum:
      f = a_CSOP_ERR | a_CSOP_ISNUM;
      csc.csc_lhv = -1;
      break;
   case a_CSOP_ERR_STR_OVERFLOW:
      n_err(_("csop: string length or offset overflows datatype\n"));
      n_pstate_err_no = su_ERR_OVERFLOW;
      goto jestr;
   case a_CSOP_ERR_STR_NODATA:
      n_pstate_err_no = su_ERR_NODATA;
      /* FALLTHRU*/
   case a_CSOP_ERR_STR_GENERIC:
jestr:
      csc.csc_varres = su_empty;
      f = a_CSOP_ERR;
      break;
   }

   /* Generate the variable value content.
    * Anticipate in our handling below!  (Avoid needless work) */
   if(f & a_CSOP_ISNUM){
      cp = su_ienc(csc.csc_iencbuf, csc.csc_lhv, 10, su_IENC_MODE_SIGNED_TYPE);
      if(cp != NIL)
         csc.csc_varres = cp;
      else{
         f |= a_CSOP_ERR;
         csc.csc_varres = su_empty;
      }
   }

   if(csc.csc_varname == NIL){
      /* If there was no error and we are printing a numeric result, print some
       * more bases for the fun of it */
      if(csc.csc_varres != NIL &&
            fprintf(n_stdout, "%s\n", csc.csc_varres) < 0){
         n_pstate_err_no = su_err_no();
         f |= a_CSOP_ERR;
      }
   }else if(!n_var_vset(csc.csc_varname, S(up,csc.csc_varres),
         csc.csc_cm_local)){
      n_pstate_err_no = su_ERR_NOTSUP;
      f |= a_CSOP_ERR;
   }

jleave:
   NYD_OU;
   return (f & a_CSOP_ERR) ? 1 : 0;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_CMD_CSOP */
/* s-it-mode */
