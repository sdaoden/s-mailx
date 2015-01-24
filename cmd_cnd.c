/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Commands: conditional constructs.
 *
 * Copyright (c) 2014 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct cond_stack {
   struct cond_stack *c_outer;
   bool_t            c_noop;  /* Outer stack !c_go, entirely no-op */
   bool_t            c_go;    /* Green light */
   bool_t            c_else;  /* In `else' clause */
   ui8_t             __dummy[5];
};

struct if_cmd {
   char const  * const *ic_argv_base;
   size_t      ic_argc_base;           /* Redundant: NULL terminated */
   char const  * const *ic_argv;
   size_t      ic_argc;
};

static struct cond_stack   *_cond_stack;

static void    _if_error(struct if_cmd const *icp);
static si8_t   _if_test(struct if_cmd *icp, bool_t noop);

static void
_if_error(struct if_cmd const *icp)
{
   struct str s;
   NYD2_ENTER;

   str_concat_cpa(&s, icp->ic_argv_base, (icp->ic_argc_base > 0 ? " " : ""));
   fprintf(stderr, _("Invalid conditional expression \"%s\"\n"), s.s);
   NYD2_LEAVE;
}

static si8_t
_if_test(struct if_cmd *icp, bool_t noop)
{
   char const * const *argv, *cp, *v, *op;
   size_t argc;
   char c;
   si8_t rv = -1;
   NYD2_ENTER;

   argv = icp->ic_argv;
   argc = icp->ic_argc;
   cp = argv[0];

   if (*cp != '$') {
      if (argc > 1)
         goto jesyn;
   } else if (cp[1] == '\0')
      goto jesyn;
   else if (argc > 3) {
jesyn:
      _if_error(icp);
      goto jleave;
   }

   switch (*cp) {
   default:
      switch (boolify(cp, UIZ_MAX, -1)) {
      case 0: rv = FAL0; break;
      case 1: rv = TRU1; break;
      default:
         fprintf(stderr, _("Unrecognized if-keyword: \"%s\"\n"), cp);
         break;
      }
      break;
   case 'R': case 'r':
      rv = !(options & OPT_SENDMODE);
      break;
   case 'S': case 's':
      rv = ((options & OPT_SENDMODE) != 0);
      break;
   case 'T': case 't':
      if (!asccasecmp(cp, "true")) /* Beware! */
         rv = TRU1;
      else
         rv = ((options & OPT_TTYIN) != 0);
      break;
   case '$':
      /* Look up the value in question, we need it anyway */
      v = vok_vlook(++cp);

      /* Single argument, "implicit boolean" form? */
      if (argc == 1) {
         rv = (v != NULL);
         break;
      }
      op = argv[1];

      /* Three argument comparison form required, check syntax */
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
         if (op[1] != '='
#ifdef HAVE_REGEX
               && op[1] != '~'
#endif
         )
            goto jesyn;
      }

      /* A null value is treated as the empty string */
      if (v == NULL)
         v = UNCONST("");
#ifdef HAVE_REGEX
      if (op[1] == '~') {
         regex_t re;

         if (regcomp(&re, argv[2], REG_EXTENDED | REG_ICASE | REG_NOSUB))
            goto jesyn;
         rv = (regexec(&re, v, 0,NULL, 0) == REG_NOMATCH) ^ (c == '=');
         regfree(&re);
      } else
#endif
      {
         /* Try to interpret as integers, prefer those, then */
         char const *argv2 = argv[2];
         char *eptr;
         sl_i sli2, sli1;

         sli2 = strtol(argv2, &eptr, 0);
         if (*argv2 != '\0' && *eptr == '\0') {
            sli1 = strtol((cp = v), &eptr, 0);
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
         sli1 = asccasecmp(v, argv2);
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
jleave:
   NYD2_LEAVE;
   return rv;
}

FL int
c_if(void *v)
{
   struct if_cmd ic;
   struct cond_stack *csp;
   size_t argc;
   si8_t xrv, rv = 1;
   char const * const *argv = v;
   NYD_ENTER;

   csp = smalloc(sizeof *csp);
   csp->c_outer = _cond_stack;
   csp->c_noop = condstack_isskip();
   csp->c_go = TRU1;
   csp->c_else = FAL0;
   _cond_stack = csp;

   if (csp->c_noop) {
      rv = 0;
      goto jleave;
   }

   for (argc = 0; argv[argc] != NULL; ++argc)
      ;
   ic.ic_argv_base = ic.ic_argv = argv;
   ic.ic_argc_base = ic.ic_argc = argc;
   xrv = _if_test(&ic, FAL0);
   if (xrv >= 0) {
      csp->c_go = (bool_t)xrv;
      rv = 0;
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
      fprintf(stderr, _("`elif' without matching `if'\n"));
      rv = 1;
   } else {
      csp->c_go = !csp->c_go;
      rv = c_if(v);
      _cond_stack->c_outer = csp->c_outer;
      free(csp);
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_else(void *v)
{
   int rv;
   NYD_ENTER;
   UNUSED(v);

   if (_cond_stack == NULL || _cond_stack->c_else) {
      fprintf(stderr, _("`else' without matching `if'\n"));
      rv = 1;
   } else {
      _cond_stack->c_go = !_cond_stack->c_go;
      _cond_stack->c_else = TRU1;
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
   UNUSED(v);

   if ((csp = _cond_stack) == NULL) {
      fprintf(stderr, _("`endif' without matching `if'\n"));
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
