/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Commands: conditional constructs.
 *
 * Copyright (c) 2014 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE cmd_cnd

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct cond_stack {
   struct cond_stack *c_outer;
   bool_t            c_error; /* Bad expression, skip entire if..endif */
   bool_t            c_noop;  /* Outer stack !c_go, entirely no-op */
   bool_t            c_go;    /* Green light */
   bool_t            c_else;  /* In `else' clause */
   ui8_t             __dummy[4];
};

struct if_cmd {
   char const  * const *ic_argv_base;
   char const  * const *ic_argv_max;   /* BUT: .ic_argv MUST be terminated! */
   char const  * const *ic_argv;
};

static struct cond_stack   *_cond_stack;

/* */
static void    _if_error(struct if_cmd const *icp, char const *msg_or_null,
                  char const *nearby_or_null);

/* noop and (1) don't work for real, only syntax-check and
 * (2) non-error return is ignored */
static si8_t   _if_test(struct if_cmd *icp, bool_t noop);
static si8_t   _if_group(struct if_cmd *icp, size_t level, bool_t noop);

static void
_if_error(struct if_cmd const *icp, char const *msg_or_null,
   char const *nearby_or_null)
{
   struct str s;
   NYD2_ENTER;

   if (msg_or_null == NULL)
      msg_or_null = _("invalid expression syntax");

   if (nearby_or_null != NULL)
      n_err(_("`if' conditional: %s -- near: %s\n"),
         msg_or_null, nearby_or_null);
   else
      n_err(_("`if' conditional: %s\n"), msg_or_null);

   if ((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_D_V)) {
      str_concat_cpa(&s, icp->ic_argv_base,
         (*icp->ic_argv_base != NULL ? " " : n_empty));
      n_err(_("   Expression: %s\n"), s.s);

      str_concat_cpa(&s, icp->ic_argv, (*icp->ic_argv != NULL ? " " : n_empty));
      n_err(_("   Stopped at: %s\n"), s.s);
   }

   NYD2_LEAVE;
}

static si8_t
_if_test(struct if_cmd *icp, bool_t noop)
{
   char const *emsg, * const *argv, *cp, *lhv, *op, *rhv;
   size_t argc;
   char c;
   si8_t rv;
   NYD2_ENTER;

   rv = -1;
   emsg = NULL;
   argv = icp->ic_argv;
   argc = PTR2SIZE(icp->ic_argv_max - icp->ic_argv);
   cp = argv[0];

   if (*cp != '$') {
      if (argc > 1)
         goto jesyn;
   } else if (cp[1] == '\0')
      goto jesyn;
   else if (argc > 3) {
#ifdef HAVE_REGEX
jesyn_ntr:
#endif
      if(0){
jesyn:
         if(emsg != NULL)
            emsg = V_(emsg);
      }
      _if_error(icp, emsg, cp);
      goto jleave;
   }

   switch (*cp) {
   default:
      switch (boolify(cp, UIZ_MAX, -1)) {
      case 0: rv = FAL0; break;
      case 1: rv = TRU1; break;
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
      if (!asccasecmp(cp, "true")) /* Beware! */
         rv = TRU1;
      else
         rv = ((n_psonce & n_PSO_TTYIN) != 0);
      break;
   case '$':
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
      if (argc == 1) {
         rv = (lhv != NULL);
         break;
      }
      op = argv[1];

      /* Three argument comparison form required, check syntax */
      emsg = N_("unrecognized condition");
      if (argc == 2 || (c = op[0]) == '\0')
         goto jesyn;
      if (op[1] == '\0') {
         if (c != '<' && c != '>')
            goto jesyn;
      } else if (op[2] != '\0')
         goto jesyn;
      else if (c == '<' || c == '>') {
         if (op[1] != '=')
            goto jesyn;
      } else if (c == '=' || c == '!') {
         if (op[1] != '=' && op[1] != '@'
#ifdef HAVE_REGEX
               && op[1] != '~'
#endif
         )
            goto jesyn;
      } else
         goto jesyn;

      /* The right hand side may also be a variable, more syntax checking */
      emsg = N_("invalid right hand side");
      if ((rhv = argv[2]) == NULL /* Can't happen */)
         goto jesyn;
      if (*rhv == '$') {
         if (*++rhv == '\0')
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
      if (lhv == NULL)
         lhv = n_UNCONST(n_empty);
      if (rhv == NULL)
         rhv = n_UNCONST(n_empty);

#ifdef HAVE_REGEX
      if (op[1] == '~') {
         regex_t re;
         int s;

         if((s = regcomp(&re, rhv, REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0){
            emsg = savecat(_("invalid regular expression: "),
                  n_regex_err_to_str(&re, s));
            goto jesyn_ntr;
         }
         if (!noop)
            rv = (regexec(&re, lhv, 0,NULL, 0) == REG_NOMATCH) ^ (c == '=');
         regfree(&re);
      } else
#endif
      if (noop)
         break;
      else if (op[1] == '@')
         rv = (asccasestr(lhv, rhv) == NULL) ^ (c == '=');
      else {
         /* Try to interpret as integers, prefer those, then */
         char *eptr;
         sl_i sli2, sli1;

         sli2 = strtol(rhv, &eptr, 0);
         if (*rhv != '\0' && *eptr == '\0') {
            sli1 = strtol((cp = lhv), &eptr, 0);
            if (*cp != '\0' && *eptr == '\0') {
               sli1 -= sli2;
               switch (c) {
               default:
               case '=': rv = (sli1 == 0); break;
               case '!': rv = (sli1 != 0); break;
               case '<': rv = (op[1] == '\0') ? sli1 < 0 : sli1 <= 0; break;
               case '>': rv = (op[1] == '\0') ? sli1 > 0 : sli1 >= 0; break;
               }
               break;
            }
         }

         /* It is not an integer, perform string comparison */
         sli1 = asccasecmp(lhv, rhv);
         switch (c) {
         default:
         case '=': rv = (sli1 == 0); break;
         case '!': rv = (sli1 != 0); break;
         case '<': rv = (op[1] == '\0') ? sli1 < 0 : sli1 <= 0; break;
         case '>': rv = (op[1] == '\0') ? sli1 > 0 : sli1 >= 0; break;
         }
      }
      break;
   }

   if (noop && rv < 0)
      rv = TRU1;
jleave:
   NYD2_LEAVE;
   return rv;
}

static si8_t
_if_group(struct if_cmd *icp, size_t level, bool_t noop)
{
   char const *emsg = NULL, *arg0, * const *argv, * const *argv_max_save;
   size_t i;
   char unary = '\0', c;
   enum {
      _FIRST         = 1<<0,
      _END_OK        = 1<<1,
      _NEED_LIST     = 1<<2,

      _CANNOT_UNARY  = _NEED_LIST,
      _CANNOT_OBRACK = _NEED_LIST,
      _CANNOT_CBRACK = _FIRST,
      _CANNOT_LIST   = _FIRST,
      _CANNOT_COND   = _NEED_LIST
   } state = _FIRST;
   si8_t rv = -1, xrv;
   NYD2_ENTER;

   for (;;) {
      arg0 = *(argv = icp->ic_argv);
      if (arg0 == NULL) {
         if (!(state & _END_OK)) {
            emsg = N_("missing expression (premature end)");
            goto jesyn;
         }
         if (noop && rv < 0)
            rv = TRU1;
         break; /* goto jleave; */
      }

      switch ((c = *arg0)) {
      case '!':
         if (arg0[1] != '\0')
            goto jneed_cond;

         if (state & _CANNOT_UNARY) {
            emsg = N_("cannot use a unary operator here");
            goto jesyn;
         }

         unary = (unary != '\0') ? '\0' : c;
         state &= ~(_FIRST | _END_OK);
         icp->ic_argv = ++argv;
         continue;

      case '[':
      case ']':
         if (arg0[1] != '\0')
            goto jneed_cond;

         if (c == '[') {
            if (state & _CANNOT_OBRACK) {
               emsg = N_("cannot open a group here");
               goto jesyn;
            }

            icp->ic_argv = ++argv;
            if ((xrv = _if_group(icp, level + 1, noop)) < 0) {
               rv = xrv;
               goto jleave;
            } else if (!noop)
               rv = (unary != '\0') ? !xrv : xrv;

            unary = '\0';
            state &= ~(_FIRST | _END_OK);
            state |= (level == 0 ? _END_OK : 0) | _NEED_LIST;
            continue;
         } else {
            if (state & _CANNOT_CBRACK) {
               emsg = N_("cannot use closing bracket here");
               goto jesyn;
            }

            if (level == 0) {
               emsg = N_("no open groups to be closed here");
               goto jesyn;
            }

            icp->ic_argv = ++argv;
            if (noop && rv < 0)
               rv = TRU1;
            goto jleave;/* break;break; */
         }

      case '|':
      case '&':
         if (c != arg0[1] || arg0[2] != '\0')
            goto jneed_cond;

         if (state & _CANNOT_LIST) {
            emsg = N_("cannot use a AND-OR list here");
            goto jesyn;
         }

         noop = ((c == '&') ^ (rv == TRU1));
         state &= ~(_FIRST | _END_OK | _NEED_LIST);
         icp->ic_argv = ++argv;
         continue;

      default:
jneed_cond:
         if (state & _CANNOT_COND) {
            emsg = N_("cannot use a `if' condition here");
            goto jesyn;
         }

         for (i = 0;; ++i) {
            if ((arg0 = argv[i]) == NULL)
               break;
            c = *arg0;
            if (c == '!' && arg0[1] == '\0')
               break;
            if ((c == '[' || c == ']') && arg0[1] == '\0')
               break;
            if ((c == '&' || c == '|') && c == arg0[1] && arg0[2] == '\0')
               break;
         }
         if (i == 0) {
            emsg = N_("empty conditional group");
            goto jesyn;
         }

         argv_max_save = icp->ic_argv_max;
         icp->ic_argv_max = argv + i;
         if ((xrv = _if_test(icp, noop)) < 0) {
            rv = xrv;
            goto jleave;
         } else if (!noop)
            rv = (unary != '\0') ? !xrv : xrv;
         icp->ic_argv_max = argv_max_save;

         icp->ic_argv = (argv += i);
         unary = '\0';
         state &= ~_FIRST;
         state |= _END_OK | _NEED_LIST;
         break;
      }
   }

jleave:
   NYD2_LEAVE;
   return rv;
jesyn:
   if (emsg == NULL)
      emsg = N_("invalid grouping");
   _if_error(icp, V_(emsg), arg0);
   rv = -1;
   goto jleave;
}

FL int
c_if(void *v)
{
   struct if_cmd ic;
   char const * const *argv;
   struct cond_stack *csp;
   size_t argc;
   si8_t xrv, rv;
   NYD_ENTER;

   csp = smalloc(sizeof *csp);
   csp->c_outer = _cond_stack;
   csp->c_error = FAL0;
   csp->c_noop = condstack_isskip();
   csp->c_go = TRU1;
   csp->c_else = FAL0;
   _cond_stack = csp;

   if (csp->c_noop) {
      rv = 0;
      goto jleave;
   }

   /* For heaven's sake, support comments _today_ TODO wyshlist.. */
   for (argc = 0, argv = v; argv[argc] != NULL; ++argc)
      if(argv[argc][0] == '#'){
         char const **nav = salloc(sizeof(char*) * (argc + 1));
         size_t i;

         for(i = 0; i < argc; ++i)
            nav[i] = argv[i];
         nav[i] = NULL;
         argv = nav;
         break;
      }
   ic.ic_argv_base = ic.ic_argv = argv;
   ic.ic_argv_max = &argv[argc];
   xrv = _if_group(&ic, 0, FAL0);

   if (xrv >= 0) {
      csp->c_go = (bool_t)xrv;
      rv = 0;
   } else {
      csp->c_error = csp->c_noop = TRU1;
      rv = 1;
   }
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_elif(void *v)
{
   struct cond_stack *csp;
   int rv;
   NYD_ENTER;

   if ((csp = _cond_stack) == NULL || csp->c_else) {
      n_err(_("`elif' without matching `if'\n"));
      rv = 1;
   } else if (!csp->c_error) {
      csp->c_go = !csp->c_go;
      rv = c_if(v);
      _cond_stack->c_outer = csp->c_outer;
      free(csp);
   } else
      rv = 0;
   NYD_LEAVE;
   return rv;
}

FL int
c_else(void *v)
{
   int rv;
   NYD_ENTER;
   n_UNUSED(v);

   if (_cond_stack == NULL || _cond_stack->c_else) {
      n_err(_("`else' without matching `if'\n"));
      rv = 1;
   } else {
      _cond_stack->c_else = TRU1;
      _cond_stack->c_go = !_cond_stack->c_go;
      rv = 0;
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_endif(void *v)
{
   struct cond_stack *csp;
   int rv;
   NYD_ENTER;
   n_UNUSED(v);

   if ((csp = _cond_stack) == NULL) {
      n_err(_("`endif' without matching `if'\n"));
      rv = 1;
   } else {
      _cond_stack = csp->c_outer;
      free(csp);
      rv = 0;
   }
   NYD_LEAVE;
   return rv;
}

FL bool_t
condstack_isskip(void)
{
   bool_t rv;
   NYD_ENTER;

   rv = (_cond_stack != NULL && (_cond_stack->c_noop || !_cond_stack->c_go));
   NYD_LEAVE;
   return rv;
}

FL void *
condstack_release(void)
{
   void *rv;
   NYD_ENTER;

   rv = _cond_stack;
   _cond_stack = NULL;
   NYD_LEAVE;
   return rv;
}

FL bool_t
condstack_take(void *self)
{
   struct cond_stack *csp;
   bool_t rv;
   NYD_ENTER;

   if (!(rv = ((csp = _cond_stack) == NULL)))
      do {
         _cond_stack = csp->c_outer;
         free(csp);
      } while ((csp = _cond_stack) != NULL);

   _cond_stack = self;
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
