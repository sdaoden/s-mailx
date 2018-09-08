/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Commands: conditional constructs.
 *
 * Copyright (c) 2014 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#define a_CCND_IF_ISSKIP() \
   (n_go_data->gdc_ifcond != NULL &&\
      (((struct a_ccnd_if_node*)n_go_data->gdc_ifcond)->cin_noop ||\
       !((struct a_ccnd_if_node*)n_go_data->gdc_ifcond)->cin_go))

struct a_ccnd_if_node{
   struct a_ccnd_if_node *cin_outer;
   bool_t cin_error;    /* Bad expression, skip entire if..endif */
   bool_t cin_noop;     /* Outer stack !cin_go, entirely no-op */
   bool_t cin_go;       /* Green light */
   bool_t cin_else;     /* In `else' clause */
   ui8_t cin__dummy[4];
};

struct a_ccnd_if_ctx{
   char const * const *cic_argv_base;
   char const * const *cic_argv_max;   /* BUT: .cic_argv MUST be terminated! */
   char const * const *cic_argv;
};

/* */
static void a_ccnd_oif_error(struct a_ccnd_if_ctx const *cicp,
               char const *msg_or_null, char const *nearby_or_null);

/* noop and (1) don't work for real, only syntax-check and
 * (2) non-error return is ignored */
static si8_t a_ccnd_oif_test(struct a_ccnd_if_ctx *cicp, bool_t noop);
static si8_t a_ccnd_oif_group(struct a_ccnd_if_ctx *cicp, size_t level,
               bool_t noop);

/* Shared `if' / `elif' implementation */
static int a_ccnd_if(void *v, bool_t iselif);

static void
a_ccnd_oif_error(struct a_ccnd_if_ctx const *cicp, char const *msg_or_null,
      char const *nearby_or_null){
   struct str s;
   NYD2_IN;

   if(msg_or_null == NULL)
      msg_or_null = _("invalid expression syntax");

   if(nearby_or_null != NULL)
      n_err(_("`if' conditional: %s -- near: %s\n"),
         msg_or_null, nearby_or_null);
   else
      n_err(_("`if' conditional: %s\n"), msg_or_null);

   if((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_D_V)){
      str_concat_cpa(&s, cicp->cic_argv_base,
         (*cicp->cic_argv_base != NULL ? " " : n_empty));
      n_err(_("   Expression: %s\n"), s.s);

      str_concat_cpa(&s, cicp->cic_argv,
         (*cicp->cic_argv != NULL ? " " : n_empty));
      n_err(_("   Stopped at: %s\n"), s.s);
   }
   NYD2_OU;
}

static si8_t
a_ccnd_oif_test(struct a_ccnd_if_ctx *cicp, bool_t noop){
   char const *emsg, * const *argv, *cp, *lhv, *op, *rhv;
   size_t argc;
   char c;
   si8_t rv;
   NYD2_IN;

   rv = -1;
   emsg = NULL;
   argv = cicp->cic_argv;
   argc = PTR2SIZE(cicp->cic_argv_max - cicp->cic_argv);
   cp = argv[0];

   if(*cp != '$'){
      if(argc > 1)
         goto jesyn;
   }else if(cp[1] == '\0')
      goto jesyn;
   else if(argc > 3){
#ifdef mx_HAVE_REGEX
jesyn_ntr:
#endif
      if(0){
jesyn:
         if(emsg != NULL)
            emsg = V_(emsg);
      }
      a_ccnd_oif_error(cicp, emsg, cp);
      goto jleave;
   }

   switch(*cp){
   default:
      switch((rv = n_boolify(cp, UIZ_MAX, TRUM1))){
      case FAL0:
      case TRU1:
         break;
      default:
         emsg = N_("Expected a boolean");
         goto jesyn;
      }
      break;
   case 'R': case 'r':
      rv = !(n_psonce & n_PSO_SENDMODE);
      break;
   case 'S': case 's':
      rv = ((n_psonce & n_PSO_SENDMODE) != 0);
      break;
   case 'T': case 't':
      if(!asccasecmp(cp, "true")) /* Beware! */
         rv = TRU1;
      else
         rv = ((n_psonce & n_PSO_INTERACTIVE) != 0);
      break;
   case '$':{
      enum{
         a_NONE,
         a_ICASE = 1u<<0
      } flags = a_NONE;

      /* Look up the value in question, we need it anyway */
      if(*++cp == '{'){
         size_t i = strlen(cp);

         if(i > 0 && cp[i - 1] == '}')
            cp = savestrbuf(++cp, i -= 2);
         else
            goto jesyn;
      }
      if(noop)
         lhv = NULL;
      else
         lhv = n_var_vlook(cp, TRU1);

      /* Single argument, "implicit boolean" form? */
      if(argc == 1){
         rv = (lhv != NULL);
         break;
      }
      op = argv[1];

      /* Three argument comparison form required, check syntax */
      emsg = N_("unrecognized condition");
      if(argc == 2 || (c = op[0]) == '\0')
         goto jesyn;

      /* May be modifier */
      if(c == '@'){
         for(;;){
            c = *++op;
            if(c == 'i')
               flags |= a_ICASE;
            else
               break;
         }
         if(flags == a_NONE)
            flags = a_ICASE;
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
      if((rhv = argv[2]) == NULL /* Can't happen */)
         goto jesyn;
      if(*rhv == '$'){
         if(*++rhv == '\0')
            goto jesyn;
         else if(*rhv == '{'){
            size_t i = strlen(rhv);

            if(i > 0 && rhv[i - 1] == '}')
               rhv = savestrbuf(++rhv, i -= 2);
            else{
               cp = --rhv;
               goto jesyn;
            }
         }
         if(noop)
            rhv = NULL;
         else
            rhv = n_var_vlook(cp = rhv, TRU1);
      }

      /* A null value is treated as the empty string */
      emsg = NULL;
      if(lhv == NULL)
         lhv = n_UNCONST(n_empty);
      if(rhv == NULL)
         rhv = n_UNCONST(n_empty);

#ifdef mx_HAVE_REGEX
      if(op[1] == '~'){
         regex_t re;
         int s;

         if((s = regcomp(&re, rhv, REG_EXTENDED | REG_NOSUB |
               (flags & a_ICASE ? REG_ICASE : 0))) != 0){
            emsg = savecat(_("invalid regular expression: "),
                  n_regex_err_to_doc(NULL, s));
            goto jesyn_ntr;
         }
         if(!noop)
            rv = (regexec(&re, lhv, 0,NULL, 0) == REG_NOMATCH) ^ (c == '=');
         regfree(&re);
      }else
#endif
            if(noop)
         break;
      else if(op[1] == '%' || op[1] == '@'){
         if(op[1] == '@')
            n_OBSOLETE("`if'++: \"=@\" and \"!@\" became \"=%\" and \"!%\"");
         rv = ((flags & a_ICASE ? asccasestr(lhv, rhv) : strstr(lhv, rhv)
               ) == NULL) ^ (c == '=');
      }else if(c == '-'){
         si64_t lhvi, rhvi;

         if(*lhv == '\0')
            lhv = n_0;
         if(*rhv == '\0')
            rhv = n_0;
         if((n_idec_si64_cp(&lhvi, lhv, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED || (n_idec_si64_cp(&rhvi, rhv, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED){
            emsg = N_("integer expression expected");
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
         si32_t scmp;

         scmp = (flags & a_ICASE) ? asccasecmp(lhv, rhv) : strcmp(lhv, rhv);
         switch(c){
         default:
         case '=': rv = (scmp == 0); break;
         case '!': rv = (scmp != 0); break;
         case '<': rv = (op[1] == '\0') ? scmp < 0 : scmp <= 0; break;
         case '>': rv = (op[1] == '\0') ? scmp > 0 : scmp >= 0; break;
         }
      }
      }break;
   }

   if(noop && rv < 0)
      rv = TRU1;
jleave:
   NYD2_OU;
   return rv;
}

static si8_t
a_ccnd_oif_group(struct a_ccnd_if_ctx *cicp, size_t level, bool_t noop){
   char const *emsg, *arg0, * const *argv, * const *argv_max_save;
   size_t i;
   char unary, c;
   enum{
      a_FIRST = 1<<0,
      a_END_OK = 1<<1,
      a_NEED_LIST = 1<<2,

      a_CANNOT_UNARY = a_NEED_LIST,
      a_CANNOT_OBRACK = a_NEED_LIST,
      a_CANNOT_CBRACK = a_FIRST,
      a_CANNOT_LIST = a_FIRST,
      a_CANNOT_COND = a_NEED_LIST
   } state;
   si8_t rv, xrv;
   NYD2_IN;

   rv = -1;
   state = a_FIRST;
   unary = '\0';
   emsg = NULL;

   for(;;){
      arg0 = *(argv = cicp->cic_argv);
      if(arg0 == NULL){
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
            if((arg0 = argv[i]) == NULL)
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
   if(emsg == NULL)
      emsg = N_("invalid grouping");
   a_ccnd_oif_error(cicp, V_(emsg), arg0);
   rv = -1;
   goto jleave;
}

static int
a_ccnd_if(void *v, bool_t iselif){
   struct a_ccnd_if_ctx cic;
   char const * const *argv;
   size_t argc;
   si8_t xrv, rv;
   struct a_ccnd_if_node *cinp;
   NYD_IN;

   if(!iselif){
      cinp = n_alloc(sizeof *cinp);
      cinp->cin_outer = n_go_data->gdc_ifcond;
   }else{
      cinp = n_go_data->gdc_ifcond;
      assert(cinp != NULL);
   }
   cinp->cin_error = FAL0;
   cinp->cin_noop = a_CCND_IF_ISSKIP();
   cinp->cin_go = TRU1;
   cinp->cin_else = FAL0;
   if(!iselif)
      n_go_data->gdc_ifcond = cinp;

   if(cinp->cin_noop){
      rv = 0;
      goto jleave;
   }

   /* For heaven's sake, support comments _today_ TODO wyshlist.. */
   for(argc = 0, argv = v; argv[argc] != NULL; ++argc)
      if(argv[argc][0] == '#'){
         char const **nav = n_autorec_alloc(sizeof(char*) * (argc + 1));
         size_t i;

         for(i = 0; i < argc; ++i)
            nav[i] = argv[i];
         nav[i] = NULL;
         argv = nav;
         break;
      }
   cic.cic_argv_base = cic.cic_argv = argv;
   cic.cic_argv_max = &argv[argc];
   xrv = a_ccnd_oif_group(&cic, 0, FAL0);

   if(xrv >= 0){
      cinp->cin_go = (bool_t)xrv;
      rv = 0;
   }else{
      cinp->cin_error = cinp->cin_noop = TRU1;
      rv = 1;
   }
jleave:
   NYD_OU;
   return rv;
}

FL int
c_if(void *v){
   int rv;
   NYD_IN;

   rv = a_ccnd_if(v, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_elif(void *v){
   struct a_ccnd_if_node *cinp;
   int rv;
   NYD_IN;

   if((cinp = n_go_data->gdc_ifcond) == NULL || cinp->cin_else){
      n_err(_("`elif' without a matching `if'\n"));
      rv = 1;
   }else if(!cinp->cin_error){
      cinp->cin_go = !cinp->cin_go; /* Cause right _IF_ISSKIP() result */
      rv = a_ccnd_if(v, TRU1);
   }else
      rv = 0;
   NYD_OU;
   return rv;
}

FL int
c_else(void *v){
   int rv;
   struct a_ccnd_if_node *cinp;
   NYD_IN;
   n_UNUSED(v);

   if((cinp = n_go_data->gdc_ifcond) == NULL || cinp->cin_else){
      n_err(_("`else' without a matching `if'\n"));
      rv = 1;
   }else{
      cinp->cin_else = TRU1;
      cinp->cin_go = !cinp->cin_go;
      rv = 0;
   }
   NYD_OU;
   return rv;
}

FL int
c_endif(void *v){
   int rv;
   struct a_ccnd_if_node *cinp;
   NYD_IN;
   n_UNUSED(v);

   if((cinp = n_go_data->gdc_ifcond) == NULL){
      n_err(_("`endif' without a matching `if'\n"));
      rv = 1;
   }else{
      n_go_data->gdc_ifcond = cinp->cin_outer;
      n_free(cinp);
      rv = 0;
   }
   NYD_OU;
   return rv;
}

FL bool_t
n_cnd_if_isskip(void){
   bool_t rv;
   NYD2_IN;

   rv = a_CCND_IF_ISSKIP();
   NYD2_OU;
   return rv;
}

FL void
n_cnd_if_stack_del(struct n_go_data_ctx *gdcp){
   struct a_ccnd_if_node *vp, *cinp;
   NYD2_IN;

   vp = gdcp->gdc_ifcond;
   gdcp->gdc_ifcond = NULL;

   while((cinp = vp) != NULL){
      vp = cinp->cin_outer;
      n_free(cinp);
   }
   NYD2_OU;
}

/* s-it-mode */
