/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-cnd.h.
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
#define su_FILE cmd_cnd
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/go.h"

#include "mx/cmd-cnd.h"
#include "su/code-in.h"

#define a_CCND_IF_IS_ACTIVE() (mx_go_data->gdc_ifcond != NIL)
#define a_CCND_IF_IS_SKIP() \
   (a_CCND_IF_IS_ACTIVE() &&\
      (R(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond)->cin_noop ||\
       !R(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond)->cin_go))

struct a_ccnd_if_node{
   struct a_ccnd_if_node *cin_outer;
   boole cin_error; /* Bad expression, skip entire if..endif */
   boole cin_noop; /* Outer stack !cin_go, entirely no-op */
   boole cin_go; /* Green light */
   boole cin_else; /* In `else' clause */
   u8 cin__dummy[4];
};

struct a_ccnd_if_ctx{
   char const * const *cic_argv_base;
   char const * const *cic_argv_max; /* BUT: .cic_argv MUST be terminated! */
   char const * const *cic_argv;
};

/* */
static void a_ccnd_oif_error(struct a_ccnd_if_ctx const *cicp,
      char const *msg_or_nil, char const *nearby_or_nil);

/* No-op and (1) do not work for real, only syntax-check and
 * (2) non-error return is ignored */
static s8 a_ccnd_oif_test(struct a_ccnd_if_ctx *cicp, boole noop);
static s8 a_ccnd_oif_group(struct a_ccnd_if_ctx *cicp, uz level, boole noop);

/* Shared `if' / `elif' implementation */
static int a_ccnd_if(void *vp, boole iselif);

static void
a_ccnd_oif_error(struct a_ccnd_if_ctx const *cicp, char const *msg_or_nil,
      char const *nearby_or_nil){
   struct str s;
   NYD2_IN;

   if(msg_or_nil == NIL)
      msg_or_nil = _("invalid expression syntax");

   if(nearby_or_nil != NIL)
      n_err(_("if: %s -- near: %s\n"), msg_or_nil, nearby_or_nil);
   else
      n_err(_("if: %s\n"), msg_or_nil);

   if((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_D_V)){
      str_concat_cpa(&s, cicp->cic_argv_base,
         (*cicp->cic_argv_base != NIL ? " " : su_empty));
      n_err(_("   Expression: %s\n"), s.s);

      str_concat_cpa(&s, cicp->cic_argv,
         (*cicp->cic_argv != NIL ? " " : su_empty));
      n_err(_("   Stopped at: %s\n"), s.s);
   }

   NYD2_OU;
}

static s8
a_ccnd_oif_test(struct a_ccnd_if_ctx *cicp, boole noop){
   char const *emsg, * const *argv, *cp, *lhv, *op, *rhv;
   uz argc;
   char c;
   s8 rv;
   NYD2_IN;

   rv = -1;
   emsg = NIL;
   argv = cicp->cic_argv;
   argc = P2UZ(cicp->cic_argv_max - cicp->cic_argv);
   cp = argv[0];

   if(UNLIKELY(argc != 1 && argc != 3 &&
         (argc != 2 || !(n_pstate & n_PS_ARGMOD_WYSH)))){
jesyn:
      if(emsg != NIL)
         emsg = V_(emsg);
#ifdef mx_HAVE_REGEX
jesyn_ntr:
#endif
      a_ccnd_oif_error(cicp, emsg, cp);
      goto jleave;
   }

   if(argc == 1){
      switch(*cp){
      case '$': /* v15compat */
         if(!(n_pstate & n_PS_ARGMOD_WYSH)){
            /* v15compat (!wysh): $ trigger? */
            if(cp[1] == '\0')
               goto jesyn;

            /* Look up the value in question, we need it anyway */
            if(*++cp == '{'){
               uz i = su_cs_len(cp);

               if(i > 0 && cp[i - 1] == '}')
                  cp = savestrbuf(++cp, i -= 2);
               else
                  goto jesyn;
            }

            lhv = noop ? NIL : n_var_vlook(cp, TRU1);
            rv = (lhv != NIL);
            break;
         }
         /* FALLTHRU */

      default:
         switch((rv = n_boolify(cp, UZ_MAX, TRUM1))){
         case FAL0:
         case TRU1:
            break;
         default:
            emsg = N_("Expected a boolean");
            goto jesyn;
         }
         break;
      case 'R': case 'r':
         rv = ((n_psonce & n_PSO_SENDMODE) == 0);
         break;
      case 'S': case 's':
         rv = ((n_psonce & n_PSO_SENDMODE) != 0);
         break;
      case 'T': case 't':
         if(!su_cs_cmp_case(cp, "true")) /* Beware! */
            rv = TRU1;
         else
            rv = ((n_psonce & n_PSO_INTERACTIVE) != 0);
         break;
      }
   }else if(argc == 2){
      ASSERT(n_pstate & n_PS_ARGMOD_WYSH);
      emsg = N_("unrecognized condition");
      if(cp[0] != '-' || cp[2] != '\0')
         goto jesyn;

      switch((c = cp[1])){
      case 'N':
      case 'Z':
         if(noop)
            rv = TRU1;
         else{
            lhv = n_var_vlook(argv[1], TRU1);
            rv = (c == 'N') ? (lhv != NIL) : (lhv == NIL);
         }
         break;
      case 'n':
      case 'z':
         if(noop)
            rv = TRU1;
         else{
            lhv = argv[1];
            rv = (c == 'n') ? (*lhv != '\0') : (*lhv == '\0');
         }
         break;
      default:
         goto jesyn;
      }
   }else{
      enum{
         a_NONE,
         a_MOD = 1u<<0,
         a_ICASE = 1u<<1,
         a_SATURATED = 1u<<2
      } flags = a_NONE;

      if(n_pstate & n_PS_ARGMOD_WYSH)
         lhv = cp;
      else{
         if(*cp == '$'){ /* v15compat (!wysh): $ trigger? */
            if(cp[1] == '\0')
               goto jesyn;

            /* Look up the value in question, we need it anyway */
            if(*++cp == '{'){
               uz i = su_cs_len(cp);

               if(i > 0 && cp[i - 1] == '}')
                  cp = savestrbuf(++cp, i -= 2);
               else
                  goto jesyn;
            }

            lhv = noop ? NIL : n_var_vlook(cp, TRU1);
         }else
            goto jesyn;
      }

      /* Three argument comparison form required, check syntax */
      emsg = N_("unrecognized condition");
      op = argv[1];
      if((c = op[0]) == '\0')
         goto jesyn;

      /* May be modifier */
      if(c == '@'){ /* v15compat */
         n_OBSOLETE2(_("if/elif: please use ? modifier suffix, "
               "not @ prefix: %s"),
               savecatsep(n_shexp_quote_cp(argv[0], FAL0), ' ',
                  savecatsep(n_shexp_quote_cp(argv[1], FAL0), ' ',
                     n_shexp_quote_cp(argv[2], FAL0))));
         for(;;){
            c = *++op;
            if(c == 'i')
               flags |= a_ICASE;
            else
               break;
         }
         if(flags == a_NONE)
            flags = a_ICASE;
      }else
      if((cp = su_cs_find_c(op, '?')) != NIL){
         if(cp[1] == '\0')
            flags |= a_MOD;
         else if(su_cs_starts_with_case("case", &cp[1]))
            flags |= a_ICASE;
         else if(su_cs_starts_with_case("saturated", &cp[1]))
            flags |= a_SATURATED;
         else{
            emsg = N_("invalid modifier");
            goto jesyn;
         }
         op = savestrbuf(op, P2UZ(cp - op)); /* v15compat */
         cp = argv[0];
      }

      if(op[1] == '\0'){
         if(c != '<' && c != '>')
            goto jesyn;
      }else if(c != '-' && op[2] != '\0')
         goto jesyn;
      else if(c == '<' || c == '>'){
         if(op[1] != '=')
            goto jesyn;
      }else if(c == '=' || c == '!'){
         if(op[1] != '=' && op[1] != '%' && op[1] != '@'
#ifdef mx_HAVE_REGEX
               && op[1] != '~'
#endif
         )
            goto jesyn;
      }else if(c == '-'){
         if(op[1] == '\0' || op[2] == '\0' || op[3] != '\0')
            goto jesyn;
         if(op[1] == 'e'){
            if(op[2] != 'q')
               goto jesyn;
         }else if(op[1] == 'g' || op[1] == 'l'){
            if(op[2] != 'e' && op[2] != 't')
               goto jesyn;
         }else if(op[1] == 'n'){
            if(op[2] != 'e')
               goto jesyn;
         }else
            goto jesyn;
      }else
         goto jesyn;

      /* The right hand side may also be a variable, more syntax checking */
      emsg = N_("invalid right hand side");
      if((rhv = argv[2]) == NIL /* Can't happen */)
         goto jesyn;

      if(!(n_pstate & n_PS_ARGMOD_WYSH)){
         if(*rhv == '$'){/* v15compat */
            if(*++rhv == '\0')
               goto jesyn;

            if(*rhv == '{'){
               uz i;

               i = su_cs_len(rhv);

               if(i > 0 && rhv[i - 1] == '}')
                  rhv = savestrbuf(++rhv, i -= 2);
               else{
                  cp = --rhv;
                  goto jesyn;
               }
            }

            rhv = noop ? NIL : n_var_vlook(cp = rhv, TRU1);
         }
      }

      /* A null value is treated as the empty string */
      emsg = NIL;
      if(lhv == NIL)
         lhv = UNCONST(char*,su_empty);
      if(rhv == NIL)
         rhv = UNCONST(char*,su_empty);

#ifdef mx_HAVE_REGEX
      if(op[1] == '~'){
         regex_t re;
         int s;

         if(flags & a_SATURATED){
            emsg = N_("invalid modifier for operational mode");
            goto jesyn;
         }

         if((s = regcomp(&re, rhv, REG_EXTENDED | REG_NOSUB |
               (flags & (a_MOD | a_ICASE) ? REG_ICASE : 0))) != 0){
            emsg = savecat(_("invalid regular expression: "),
                  n_regex_err_to_doc(NIL, s));
            goto jesyn_ntr;
         }

         if(!noop)
            rv = (regexec(&re, lhv, 0,NIL, 0) == REG_NOMATCH) ^ (c == '=');

         regfree(&re);
      }else
#endif /* mx_HAVE_REGEX */
            if(noop){
         ;
      }else if(op[1] == '%' || op[1] == '@'){
         if(flags & a_SATURATED){
            emsg = N_("invalid modifier for operational mode");
            goto jesyn;
         }

         if(op[1] == '@') /* v15compat */
            n_OBSOLETE("if++: \"=@\" and \"!@\" became \"=%\" and \"!%\"");
         rv = ((flags & (a_MOD | a_ICASE) ? su_cs_find_case(lhv, rhv)
               : su_cs_find(lhv, rhv)) == NIL) ^ (c == '=');
      }else if(c == '-'){
         u32 lhvis, rhvis;
         s64 lhvi, rhvi;

         if(flags & a_ICASE){
            emsg = N_("invalid modifier for operational mode");
            goto jesyn;
         }

         if(*lhv == '\0')
            lhv = n_0;
         if(*rhv == '\0')
            rhv = n_0;
         rhvis = lhvis = (flags & (a_MOD | a_SATURATED)
                  ? su_IDEC_MODE_LIMIT_NOERROR : su_IDEC_MODE_NONE) |
               su_IDEC_MODE_SIGNED_TYPE;
         lhvis = su_idec_cp(&lhvi, lhv, 0, lhvis, NIL);
         rhvis = su_idec_cp(&rhvi, rhv, 0, rhvis, NIL);

         if((lhvis & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
                  ) != su_IDEC_STATE_CONSUMED ||
               (rhvis & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
                  ) != su_IDEC_STATE_CONSUMED){
            /* TODO if/elif: should support $! and set ERR-OVERFLOW!! */
            emsg = N_("invalid integer number");
            goto jesyn;
         }

         lhvi -= rhvi;
         switch(op[1]){
         default:
         case 'e': rv = (lhvi == 0); break;
         case 'n': rv = (lhvi != 0); break;
         case 'l': rv = (op[2] == 't') ? lhvi < 0 : lhvi <= 0; break;
         case 'g': rv = (op[2] == 't') ? lhvi > 0 : lhvi >= 0; break;
         }
         break;
      }else{
         s32 scmp;

         if(flags & a_SATURATED){
            emsg = N_("invalid modifier for operational mode");
            goto jesyn;
         }

         scmp = (flags & (a_MOD | a_ICASE)) ? su_cs_cmp_case(lhv, rhv)
               : su_cs_cmp(lhv, rhv);
         switch(c){
         default:
         case '=': rv = (scmp == 0); break;
         case '!': rv = (scmp != 0); break;
         case '<': rv = (op[1] == '\0') ? scmp < 0 : scmp <= 0; break;
         case '>': rv = (op[1] == '\0') ? scmp > 0 : scmp >= 0; break;
         }
      }
   }

   if(noop && rv < 0)
      rv = TRU1;

jleave:
   NYD2_OU;
   return rv;
}

static s8
a_ccnd_oif_group(struct a_ccnd_if_ctx *cicp, uz level, boole noop){
   char const *emsg, *arg0, * const *argv, * const *argv_max_save;
   uz i;
   char unary, c;
   enum{
      a_FIRST = 1u<<0,
      a_END_OK = 1u<<1,
      a_NEED_LIST = 1u<<2,

      a_CANNOT_UNARY = a_NEED_LIST,
      a_CANNOT_OBRACK = a_NEED_LIST,
      a_CANNOT_CBRACK = a_FIRST,
      a_CANNOT_LIST = a_FIRST,
      a_CANNOT_COND = a_NEED_LIST
   } state;
   s8 rv, xrv;
   NYD2_IN;

   rv = -1;
   state = a_FIRST;
   unary = '\0';
   emsg = NIL;

   for(;;){
      arg0 = *(argv = cicp->cic_argv);
      if(arg0 == NIL){
         if(!(state & a_END_OK)){
            emsg = N_("missing expression (premature end)");
            goto jesyn;
         }
         if(noop && rv < 0)
            rv = TRU1;
         break; /* goto jleave; */
      }

      switch((c = *arg0)){
      case '!':
         if(arg0[1] != '\0')
            goto jneed_cond;

         if(state & a_CANNOT_UNARY){
            emsg = N_("cannot use a unary operator here");
            goto jesyn;
         }

         unary = (unary != '\0') ? '\0' : c;
         state &= ~(a_FIRST | a_END_OK);
         cicp->cic_argv = ++argv;
         continue;

      case '[':
      case ']':
         if(arg0[1] != '\0')
            goto jneed_cond;

         if(c == '['){
            if(state & a_CANNOT_OBRACK){
               emsg = N_("cannot open a group here");
               goto jesyn;
            }

            cicp->cic_argv = ++argv;
            if((xrv = a_ccnd_oif_group(cicp, level + 1, noop)) < 0){
               rv = xrv;
               goto jleave;
            }else if(!noop)
               rv = (unary != '\0') ? !xrv : xrv;

            unary = '\0';
            state &= ~(a_FIRST | a_END_OK);
            state |= (level == 0 ? a_END_OK : 0) | a_NEED_LIST;
            continue;
         }else{
            if(state & a_CANNOT_CBRACK){
               emsg = N_("cannot use closing bracket here");
               goto jesyn;
            }

            if(level == 0){
               emsg = N_("no open groups to be closed here");
               goto jesyn;
            }

            cicp->cic_argv = ++argv;
            if(noop && rv < 0)
               rv = TRU1;
            goto jleave;/* break;break; */
         }

      case '|':
      case '&':
         if(c != arg0[1] || arg0[2] != '\0')
            goto jneed_cond;

         if(state & a_CANNOT_LIST){
            emsg = N_("cannot use a AND-OR list here");
            goto jesyn;
         }

         noop = ((c == '&') ^ (rv == TRU1));
         state &= ~(a_FIRST | a_END_OK | a_NEED_LIST);
         cicp->cic_argv = ++argv;
         continue;

      default:
jneed_cond:
         if(state & a_CANNOT_COND){
            emsg = N_("cannot use a `if' condition here");
            goto jesyn;
         }

         for(i = 0;; ++i){
            if((arg0 = argv[i]) == NIL)
               break;
            c = *arg0;
            if(c == '!' && arg0[1] == '\0')
               break;
            if((c == '[' || c == ']') && arg0[1] == '\0')
               break;
            if((c == '&' || c == '|') && c == arg0[1] && arg0[2] == '\0')
               break;
         }
         if(i == 0){
            emsg = N_("empty conditional group");
            goto jesyn;
         }

         argv_max_save = cicp->cic_argv_max;
         cicp->cic_argv_max = argv + i;
         if((xrv = a_ccnd_oif_test(cicp, noop)) < 0){
            rv = xrv;
            goto jleave;
         }else if(!noop)
            rv = (unary != '\0') ? !xrv : xrv;
         cicp->cic_argv_max = argv_max_save;

         cicp->cic_argv = (argv += i);
         unary = '\0';
         state &= ~a_FIRST;
         state |= a_END_OK | a_NEED_LIST;
         break;
      }
   }

jleave:
   NYD2_OU;
   return rv;
jesyn:
   if(emsg == NIL)
      emsg = N_("invalid grouping");
   a_ccnd_oif_error(cicp, V_(emsg), arg0);
   rv = -1;
   goto jleave;
}

static int
a_ccnd_if(void *v, boole iselif){
   struct a_ccnd_if_ctx cic;
   char const * const *argv;
   uz argc;
   s8 xrv, rv;
   struct a_ccnd_if_node *cinp;
   NYD_IN;

   if(!iselif){
      cinp = su_ALLOC(sizeof *cinp);
      cinp->cin_outer = mx_go_data->gdc_ifcond;
   }else{
      cinp = mx_go_data->gdc_ifcond;
      ASSERT(cinp != NIL);
   }
   cinp->cin_error = FAL0;
   cinp->cin_noop = a_CCND_IF_IS_SKIP();
   cinp->cin_go = TRU1;
   cinp->cin_else = FAL0;
   if(!iselif)
      mx_go_data->gdc_ifcond = cinp;

   if(cinp->cin_noop){
      rv = 0;
      goto jleave;
   }

   /* For heaven's sake, support comments _today_ TODO wyshlist.. */
   for(argc = 0, argv = v; argv[argc] != NIL; ++argc)
      if(argv[argc][0] == '#'){
         uz i;
         char const **nav;

         nav = su_AUTO_ALLOC(sizeof(char*) * (argc + 1));
         for(i = 0; i < argc; ++i)
            nav[i] = argv[i];
         nav[i] = NIL;
         argv = nav;
         break;
      }
   cic.cic_argv_base = cic.cic_argv = argv;
   cic.cic_argv_max = &argv[argc];
   xrv = a_ccnd_oif_group(&cic, 0, FAL0);

   if(xrv >= 0){
      cinp->cin_go = S(boole,xrv);
      rv = 0;
   }else{
      cinp->cin_error = cinp->cin_noop = TRU1;
      rv = 1;
   }

jleave:
   NYD_OU;
   return rv;
}

int
c_if(void *vp){
   int rv;
   NYD_IN;

   rv = a_ccnd_if(vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_elif(void *vp){
   struct a_ccnd_if_node *cinp;
   int rv;
   NYD_IN;

   if((cinp = mx_go_data->gdc_ifcond) == NIL || cinp->cin_else){
      n_err(_("elif: no matching `if'\n"));
      rv = 1;
   }else if(!cinp->cin_error){
      cinp->cin_go = !cinp->cin_go; /* Cause right _IF_IS_SKIP() result */
      rv = a_ccnd_if(vp, TRU1);
   }else
      rv = 0;

   NYD_OU;
   return rv;
}

int
c_else(void *vp){
   int rv;
   struct a_ccnd_if_node *cinp;
   NYD_IN;
   UNUSED(vp);

   if((cinp = mx_go_data->gdc_ifcond) == NIL || cinp->cin_else){
      n_err(_("else: no matching `if'\n"));
      rv = 1;
   }else{
      cinp->cin_else = TRU1;
      cinp->cin_go = !cinp->cin_go;
      rv = 0;
   }

   NYD_OU;
   return rv;
}

int
c_endif(void *vp){
   int rv;
   struct a_ccnd_if_node *cinp;
   NYD_IN;
   UNUSED(vp);

   if((cinp = mx_go_data->gdc_ifcond) == NIL){
      n_err(_("endif: no matching `if'\n"));
      rv = 1;
   }else{
      mx_go_data->gdc_ifcond = cinp->cin_outer;
      su_FREE(cinp);
      rv = 0;
   }

   NYD_OU;
   return rv;
}

boole
mx_cnd_if_exists(void){
   boole rv;
   NYD2_IN;

   if((rv = a_CCND_IF_IS_ACTIVE()) && a_CCND_IF_IS_SKIP())
      rv = TRUM1;

   NYD2_OU;
   return rv;
}

void
mx_cnd_if_stack_del(struct mx_go_data_ctx *gdcp){
   struct a_ccnd_if_node *vp, *cinp;
   NYD2_IN;

   vp = gdcp->gdc_ifcond;
   gdcp->gdc_ifcond = NIL;

   while((cinp = vp) != NIL){
      vp = cinp->cin_outer;
      su_FREE(cinp);
   }

   NYD2_OU;
}

#include "su/code-ou.h"
/* s-it-mode */
