/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-cnd.h.
 *
 * Copyright (c) 2014 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_cnd
#define mx_SOURCE
#define mx_SOURCE_CMD_CND

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#ifdef mx_HAVE_REGEX
# include <su/re.h>
#endif

#include "mx/go.h"

#include "mx/cmd-cnd.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
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
   char const * const *cic_argv;
};

/* */
static void a_ccnd_oif_error(struct a_ccnd_if_ctx const *cicp,
      char const *msg_or_nil, char const *nearby_or_nil);

/* No-op and (1) do not work for real, only syntax-check and
 * (2) non-error return is ignored.
 * argv is a "proposed argc" until group end or AND-OR or argv NIL */
static boole a_ccnd_oif_test(struct a_ccnd_if_ctx *cicp, boole noop,
      uz argv, uz *unary);
static boole a_ccnd_oif_group(struct a_ccnd_if_ctx *cicp, uz level,
      boole noop);

/* Shared `if' / `elif' implementation */
static int a_ccnd_if(void *vp, boole iselif);

static void
a_ccnd_oif_error(struct a_ccnd_if_ctx const *cicp, char const *msg_or_nil,
      char const *nearby_or_nil){
   struct str s;
   char const *sep, *expr;
   NYD2_IN;

   str_concat_cpa(&s, cicp->cic_argv_base,
      (*cicp->cic_argv_base != NIL ? " " : su_empty));

   if(msg_or_nil == NIL)
      msg_or_nil = _("invalid expression syntax");

   if((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_D_V))
      sep = expr = su_empty;
   else
      sep = ": ", expr = s.s;

   if(nearby_or_nil != NIL)
      n_err(_("*if: %s (near: %s)%s%s\n"),
         msg_or_nil, nearby_or_nil, sep, expr);
   else
      n_err(_("*if: %s%s%s\n"), msg_or_nil, sep, expr);

   if(sep == su_empty){
      ul argc;

      n_err(_("   Expression: %s\n"), s.s);

      str_concat_cpa(&s, cicp->cic_argv,
         (*cicp->cic_argv != NIL ? " # " : su_empty));

      for(argc = 0; cicp->cic_argv[argc] != NIL; ++argc){
      }
      n_err(_("   Stopped, %lu token(s) left: %s\n"), argc, s.s);
   }

   n_pstate_err_no = su_ERR_INVAL;

   NYD2_OU;
}

static boole
a_ccnd_oif_test(struct a_ccnd_if_ctx *cicp, boole noop, uz argc, uz *unary){
#undef a_REDO
#define a_REDO() \
   if(argc < 3 && *unary > 0){\
      --*unary;\
      ++argc;\
      argv = --cicp->cic_argv;\
      goto jredo;\
   }else if(cicp->cic_argv[argc] != NIL){\
      ++argc;\
      goto jredo;\
   }

   char c, opbuf[4];
   char const *emsg, * const *argv, *cp, *lhv, *op, *rhv;
   boole rv;
   NYD2_IN;
   ASSERT(argc >= 1);

   rv = TRUM1;
   emsg = NIL;
   argv = cicp->cic_argv;

   /* Maximally unary ! was seen here */
   if(UNLIKELY(argc != 1 && argc != 3 &&
         (argc != 2 || !(n_pstate & n_PS_ARGMOD_WYSH)))){
      cp = argv[0];
      goto jesyn;
   }

jredo:
   if(argc == 3)
      goto jargc3;
   if(argc == 2)
      goto jargc2;
   if(argc == 1)
      goto jargc1;

   cp = argv[0];
   goto jesyn;

jresult:
   /* Easier to handle in here */
   if(noop && rv /*== TRUM1*/)
      rv = TRU1;

jleave:
   cicp->cic_argv += argc;

   NYD2_OU;
   return rv;

jargc1:/* C99 */{
   switch(*(cp = argv[0])){
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
         a_REDO();
         emsg = N_("Expected a boolean");
         rv = TRUM1;
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

   goto jresult;
   }

jargc2:/* C99 */{
   emsg = N_("unrecognized condition");

   cp = argv[0];
   if(cp[0] != '-' || cp[2] != '\0'){
      a_REDO();
      goto jesyn;
   }

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
      a_REDO();
      goto jesyn;
   }

   goto jresult;
   }

jargc3:/* C99 */{
   enum a_flags{
      a_NONE,
      a_MOD = 1u<<0,
      a_ICASE = 1u<<1,
      a_SATURATED = 1u<<2
   };

   BITENUM_IS(u32,a_flags) flags;

   flags = a_NONE;

   emsg = N_("unrecognized condition");
   op = argv[1];
   if((c = op[0]) == '\0'){
      cp = op;
      goto jesyn;
   }

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
         cp = op;
         emsg = N_("invalid modifier");
         goto jesyn;
      }

      /* C99 */{
         uz i;

         i = P2UZ(cp - op);
         if(i > 3){
            cp = op;
            goto jesyn;
         }
         opbuf[i] = '\0';
         su_mem_copy(opbuf, op, i);
         op = opbuf;
      }
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

   /* */
   lhv = argv[0];
   if(!(n_pstate & n_PS_ARGMOD_WYSH)){ /* v15compat (!wysh): $ trigger? */
      cp = lhv;
      if(*cp == '$'){
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

   /* The right hand side may also be a variable, more syntax checking */
   emsg = N_("invalid right hand side");
   if((rhv = argv[2]) == NIL /* Cannot happen */)
      goto jesyn;

   if(!(n_pstate & n_PS_ARGMOD_WYSH)){/* v15compat */
      if(*rhv == '$'){
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

   /* String */
#ifdef mx_HAVE_REGEX
   if(op[1] == '~'){
      struct su_re re;

      if(flags & a_SATURATED){
         emsg = N_("invalid modifier for operational mode (regex)");
         goto jesyn;
      }

      su_re_create(&re);

      if(su_re_setup_cp(&re, rhv, (su_RE_SETUP_EXT |
            su_RE_SETUP_TEST_ONLY | ((flags & (a_MOD | a_ICASE))
               ? su_RE_SETUP_ICASE : su_RE_SETUP_NONE))
            ) != su_RE_ERROR_NONE){
         emsg = savecat(_("invalid regular expression: "),
               su_re_error_doc(&re));
         su_re_gut(&re);
         goto jesyn_ntr;
      }

      if(!noop)
         rv = (!su_re_eval_cp(&re, lhv, su_RE_EVAL_NONE) ^ (c == '='));

      su_re_gut(&re);
   }else
#endif /* mx_HAVE_REGEX */
         if(noop){
      ;
   }
   /* Byte find */
   else if(op[1] == '%' || op[1] == '@'){
      if(flags & a_SATURATED){
         emsg = N_("invalid modifier for operational mode");
         goto jesyn;
      }

      if(op[1] == '@') /* v15compat */
         n_OBSOLETE("if++: \"=@\" and \"!@\" became \"=%\" and \"!%\"");
      rv = ((flags & (a_MOD | a_ICASE) ? su_cs_find_case(lhv, rhv)
            : su_cs_find(lhv, rhv)) == NIL) ^ (c == '=');
   }
   /* Integer */
   else if(c == '-'){
      u32 lhvis, rhvis; /* TODO no 32-bit; simply use 64-bit for -gt etc */
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
   }
   /* Byte compare */
   else{
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

   goto jresult;
   }

jesyn:
   if(emsg != NIL)
      emsg = V_(emsg);
#ifdef mx_HAVE_REGEX
jesyn_ntr:
#endif
   a_ccnd_oif_error(cicp, emsg, cp);
   ASSERT(rv == TRUM1);
   goto jleave;

#undef a_REDO
}

static boole
a_ccnd_oif_group(struct a_ccnd_if_ctx *cicp, uz level, boole noop){
   enum a_state{
      a_FIRST = 1u<<0,
      a_END_OK = 1u<<1,
      a_NEED_LIST = 1u<<2,

      a_CANNOT_UNARY = a_NEED_LIST,
      a_CANNOT_OBRACK = a_NEED_LIST,
      a_CANNOT_CBRACK = a_FIRST,
      a_CANNOT_LIST = a_FIRST,
      a_CANNOT_COND = a_NEED_LIST
   };

   char c;
   char const *emsg, *arg0, * const *argv;
   uz unary, i;
   BITENUM_IS(u32,a_state) state;
   boole rv, xrv;
   NYD2_IN;

   rv = TRUM1;
   state = a_FIRST;
   unary = 0;
   emsg = NIL;

   for(;;){
      arg0 = *(argv = cicp->cic_argv);
      if(arg0 == NIL){
         if(!(state & a_END_OK)){
            emsg = N_("missing expression (premature end)");
            goto jesyn;
         }
         if(noop && rv /*== TRUM1*/)
            rv = TRU1;
         break;
      }

      switch((c = *arg0)){
      case '!':
         if(arg0[1] != '\0')
            goto jneed_cond;

         /* This might be regular string argument, but we cannot decide here */
         if(state & a_CANNOT_UNARY){
            emsg = N_("cannot use unary operator here");
            goto jesyn;
         }

         state &= ~(a_FIRST | a_END_OK);
         state |= a_CANNOT_LIST;
         ++unary;
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
            if((xrv = a_ccnd_oif_group(cicp, level + 1, noop)
                  ) == TRUM1){
               rv = xrv;
               goto jleave;
            }else if(!noop)
               rv = (unary & 1) ? !xrv : xrv;

            state &= ~(a_FIRST | a_END_OK);
            state |= (level == 0 ? a_END_OK : 0) | a_NEED_LIST;
            unary = 0;
            continue;
         }else{
            if(state & a_CANNOT_CBRACK){
               emsg = N_("cannot use closing bracket here");
               goto jesyn;
               /* However, maybe user _wants_ this as an argument!
                * emsg = N_("cannot use closing bracket here");
                * goto jesyn; */
               goto jneed_cond;
            }

            if(level == 0){
               /* However, maybe user _wants_ this as an argument!
                * emsg = N_("no open groups to be closed here");
                * goto jesyn; */
               goto jneed_cond;
            }

            cicp->cic_argv = ++argv;
            if(noop && rv /*== TRUM1*/)
               rv = TRU1;
            goto jleave; /* break;"break;" */
         }
         /* not reached */
         break;

      case '|':
      case '&':
         if(c != arg0[1] || arg0[2] != '\0')
            goto jneed_cond;

         if(state & a_CANNOT_LIST){
            /* However, maybe user _wants_ this as an argument!
             * emsg = N_("cannot use AND-OR list here");
             * goto jesyn; */
            goto jneed_cond;
         }

         noop = ((c == '&') ^ (rv == TRU1));
         state &= ~(a_FIRST | a_END_OK | a_NEED_LIST);
         state |= a_CANNOT_LIST;
         ASSERT(unary == 0);

         cicp->cic_argv = ++argv;
         continue;

      default:
jneed_cond:
         if(state & a_CANNOT_COND){
            emsg = N_("cannot use `if' condition here");
            goto jesyn;
         }

         /* Forward scan for something that can end a condition (regulary) */
         for(i = 0;; ++i){
            if((arg0 = argv[i]) == NIL)
               break;
            /* So the first cannot be it but for syntax error.
             * We cannot always gracefully decide in between syntax error and
             * not, anyway, therefore just skip the first */
            if(i == 0)
               continue;
            c = *arg0;
            if((c == '[' || c == ']') && arg0[1] == '\0')
               break;
            if((c == '&' || c == '|') && c == arg0[1] && arg0[2] == '\0')
               break;
         }
         if(i == 0){
            emsg = N_("empty conditional group");
            goto jesyn;
         }

         if((xrv = a_ccnd_oif_test(cicp, noop, i, &unary)) == TRUM1){
            rv = xrv;
            goto jleave;
         }else if(!noop)
            rv = (unary & 1) ? !xrv : xrv;

         state &= ~a_FIRST;
         state |= a_END_OK | a_NEED_LIST;
         unary = 0;
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
   rv = TRUM1;
   goto jleave;
}

static int
a_ccnd_if(void *vp, boole iselif){
   struct a_ccnd_if_ctx cic;
   int rv;
   char const * const *argv;
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
      rv = su_EX_OK;
      goto jleave;
   }

   cic.cic_argv_base = cic.cic_argv = argv = vp;
   while(*argv != NIL)
      ++argv;

   /* C99 */{
      boole xrv;

      if((xrv = a_ccnd_oif_group(&cic, 0, FAL0)) >= FAL0){
         cinp->cin_go = S(boole,xrv);
         rv = su_EX_OK;
      }else{
         cinp->cin_error = cinp->cin_noop = TRU1;
         rv = su_EX_ERR;
      }
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
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_CND
/* s-it-mode */
