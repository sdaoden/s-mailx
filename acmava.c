/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Account, macro and variable handling.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/*
 * TODO in general it would be nice if it would be possible to define "macros"
 * TODO etc. inside of other "macros"
 */

#define MA_PRIME     HSHSIZE
#define MA_HASH(S)   (strhash(S) % MA_PRIME)

enum ma_flags {
   MA_NONE        = 0,
   MA_ACC         = 1<<0,
   MA_TYPE_MASK   = MA_ACC,
   MA_UNDEF       = 1<<1   /* Unlink after lookup */
};

struct macro {
   struct macro   *ma_next;
   char           *ma_name;
   struct mline   *ma_contents;
   size_t         ma_maxlen;     /* Maximum line length */
   enum ma_flags  ma_flags;
   struct var     *ma_localopts; /* `account' unroll list, for `localopts' */
};

struct mline {
   struct mline *l_next;
   size_t      l_length;
   char        l_line[VFIELD_SIZE(sizeof(size_t))];
};

struct var {
   struct var  *v_link;
   char        *v_name;
   char        *v_value;
};

struct lostack {
   struct lostack *s_up;      /* Outer context */
   struct macro   *s_mac;     /* Context (`account' or `define') */
   struct var     *s_localopts;
   bool_t         s_unroll;   /* Unroll? */
};

static struct macro  *_acc_curr;    /* Currently active account */
static struct lostack *_localopts;  /* Currently executing macro unroll list */

/* TODO once we have a dynamically sized hashtable we could unite _macros and
 * TODO _variables into a single hashtable, stripping down fun interface;
 * TODO also, setting and clearing a variable can be easily joined */
static struct macro  *_macros[MA_PRIME];  /* TODO dynamically spaced */
static struct var    *_vars[MA_PRIME];    /* TODO dynamically spaced */

/* Special cased value string allocation */
static char *        _vcopy(char const *str);
static void          _vfree(char *cp);

/* Check for special housekeeping. */
static bool_t        _check_special_vars(char const *name, bool_t enable,
                        char **val);

/* If a variable name begins with a lowercase-character and contains at
 * least one '@', it is converted to all-lowercase. This is necessary
 * for lookups of names based on email addresses.
 *
 * Following the standard, only the part following the last '@' should
 * be lower-cased, but practice has established otherwise here */
static char const *  _canonify(char const *vn);

/* Locate a variable and return its variable node */
static struct var *  _lookup(char const *name, ui_it h, bool_t hisset);

/* Line *cp* consists solely of WS and a } */
static bool_t        _is_closing_angle(char const *cp);

/* Lookup for macros/accounts */
static struct macro *_malook(char const *name, struct macro *data,
                        enum ma_flags mafl);

/* Walk all lines of a macro and execute() them */
static int           _maexec(struct macro const *mp, struct var **unroll_store);

/* User display helpers */
static int           _list_macros(enum ma_flags mafl);

/*  */
static bool_t        _define1(char const *name, enum ma_flags mafl);
static void          _undef1(char const *name, enum ma_flags mafl);
static void          _freelines(struct mline *lp);

/* qsort(3) helper */
static int           __var_list_all_cmp(void const *s1, void const *s2);

/* Update replay-log */
static void          _localopts_add(struct lostack *losp, char const *name,
                        struct var *ovap);
static void          _localopts_unroll(struct var **vapp);

static char *
_vcopy(char const *str)
{
   char *news;
   size_t len;

   if (*str == '\0')
      news = UNCONST("");
   else {
      len = strlen(str) + 1;
      news = smalloc(len);
      memcpy(news, str, len);
   }
   return news;
}

static void
_vfree(char *cp)
{
   if (*cp != '\0')
      free(cp);
}

static bool_t
_check_special_vars(char const *name, bool_t enable, char **val)
{
   /* TODO _check_special_vars --> value cache */
   char *cp = NULL;
   bool_t rv = TRU1;
   int flag = 0;

   if (strcmp(name, "debug") == 0)
      flag = OPT_DEBUG;
   else if (strcmp(name, "header") == 0)
      flag = OPT_N_FLAG, enable = ! enable;
   else if (strcmp(name, "skipemptybody") == 0)
      flag = OPT_E_FLAG;
   else if (strcmp(name, "verbose") == 0)
      flag = OPT_VERBOSE;
   else if (strcmp(name, "prompt") == 0)
      flag = OPT_NOPROMPT, enable = ! enable;
   else if (strcmp(name, "folder") == 0) {
      rv = var_folder_updated(*val, &cp);
      if (rv && cp != NULL) {
         _vfree(*val);
         /* It's smalloc()ed, but ensure we don't leak */
         if (*cp == '\0') {
            *val = UNCONST("");
            free(cp);
         } else
            *val = cp;
      }
   }
#ifdef HAVE_NCL
   else if (strcmp(name, "line-editor-cursor-right") == 0) {
      char const *x = cp = *val;
      int c;
      while (*x != '\0') {
         c = expand_shell_escape(&x, FAL0);
         if (c < 0)
            break;
         *cp++ = (char)c;
      }
      *cp++ = '\0';
   }
#endif

   if (flag) {
      if (enable)
         options |= flag;
      else
         options &= ~flag;
   }
   return rv;
}

static char const *
_canonify(char const *vn)
{
   if (! upperchar(*vn)) {
      char const *vp;

      for (vp = vn; *vp != '\0' && *vp != '@'; ++vp)
         ;
      vn = (*vp == '@') ? i_strdup(vn) : vn;
   }
   return vn;
}

static struct var *
_lookup(char const *name, ui_it h, bool_t hisset)
{
   struct var **vap, *lvp, *vp;

   if (! hisset)
      h = MA_HASH(name);
   vap = _vars + h;

   for (lvp = NULL, vp = *vap; vp != NULL; lvp = vp, vp = vp->v_link)
      if (*vp->v_name == *name && strcmp(vp->v_name, name) == 0) {
         /* Relink as head, hope it "sorts on usage" over time */
         if (lvp != NULL) {
            lvp->v_link = vp->v_link;
            vp->v_link = *vap;
            *vap = vp;
         }
         goto jleave;
      }
   vp = NULL;
jleave:
   return vp;
}

static bool_t
_is_closing_angle(char const *cp)
{
   bool_t rv = FAL0;
   while (spacechar(*cp))
      ++cp;
   if (*cp++ != '}')
      goto jleave;
   while (spacechar(*cp))
      ++cp;
   rv = (*cp == '\0');
jleave:
   return rv;
}

static struct macro *
_malook(char const *name, struct macro *data, enum ma_flags mafl)
{
   enum ma_flags save_mafl;
   ui_it h;
   struct macro *lmp, *mp;

   save_mafl = mafl;
   mafl &= MA_TYPE_MASK;
   h = MA_HASH(name);

   for (lmp = NULL, mp = _macros[h]; mp != NULL; lmp = mp, mp = mp->ma_next) {
      if ((mp->ma_flags & MA_TYPE_MASK) == mafl &&
            strcmp(mp->ma_name, name) == 0) {
         if (save_mafl & MA_UNDEF) {
            if (lmp == NULL)
               _macros[h] = mp->ma_next;
            else
               lmp->ma_next = mp->ma_next;
         }
         goto jleave;
      }
   }

   if (data != NULL) {
      data->ma_next = _macros[h];
      _macros[h] = data;
      mp = NULL;
   }
jleave:
   return mp;
}

static int
_maexec(struct macro const *mp, struct var **unroll_store)
{
   struct lostack los;
   int rv = 0;
   struct mline const *lp;
   char *buf = ac_alloc(mp->ma_maxlen + 1);

   los.s_up = _localopts;
   los.s_mac = UNCONST(mp);
   los.s_localopts = NULL;
   los.s_unroll = FAL0;
   _localopts = &los;

   for (lp = mp->ma_contents; lp; lp = lp->l_next) {
      unset_allow_undefined = TRU1;
      memcpy(buf, lp->l_line, lp->l_length + 1);
      rv |= execute(buf, 0, lp->l_length); /* XXX break if != 0 ? */
      unset_allow_undefined = FAL0;
   }

   _localopts = los.s_up;
   if (unroll_store == NULL)
      _localopts_unroll(&los.s_localopts);
   else
      *unroll_store = los.s_localopts;

   ac_free(buf);
   return rv;
}

static int
_list_macros(enum ma_flags mafl)
{
   FILE *fp;
   char *cp;
   char const *typestr;
   struct macro *mq;
   ui_it ti, mc;
   struct mline *lp;

   if ((fp = Ftemp(&cp, "Ra", "w+", 0600, 1)) == NULL) {
      perror("tmpfile");
      return 1;
   }
   rm(cp);
   Ftfree(&cp);

   mafl &= MA_TYPE_MASK;
   typestr = (mafl & MA_ACC) ? "account" : "define";

   for (ti = mc = 0; ti < MA_PRIME; ++ti)
      for (mq = _macros[ti]; mq; mq = mq->ma_next)
         if ((mq->ma_flags & MA_TYPE_MASK) == mafl) {
            if (++mc > 1)
               fputc('\n', fp);
            fprintf(fp, "%s %s {\n", typestr, mq->ma_name);
            for (lp = mq->ma_contents; lp; lp = lp->l_next)
               fprintf(fp, "  %s\n", lp->l_line);
            fputs("}\n", fp);
         }
   if (mc)
      page_or_print(fp, 0);

   mc = (ui_it)ferror(fp);
   Fclose(fp);
   return (int)mc;
}

static bool_t
_define1(char const *name, enum ma_flags mafl)
{
   bool_t rv = FAL0;
   struct macro *mp;
   struct mline *lp, *lst = NULL, *lnd = NULL;
   char *linebuf = NULL, *cp;
   size_t linesize = 0, maxlen = 0;
   int n, i;

   mp = scalloc(1, sizeof *mp);
   mp->ma_name = sstrdup(name);
   mp->ma_flags = mafl;

   for (;;) {
      n = readline_input(LNED_LF_ESC, "", &linebuf, &linesize);
      if (n <= 0) {
         fprintf(stderr, tr(75, "Unterminated %s definition: \"%s\".\n"),
            (mafl & MA_ACC ? "account" : "macro"), mp->ma_name);
         if (sourcing)
            unstack();
         goto jerr;
      }
      if (_is_closing_angle(linebuf))
         break;

      /* Trim WS */
      for (cp = linebuf, i = 0; i < n; ++cp, ++i)
         if (! whitechar(*cp))
            break;
      if (i == n)
         continue;
      n -= i;
      while (whitechar(cp[n - 1]))
         if (--n == 0)
            break;
      if (n == 0)
         continue;

      maxlen = MAX(maxlen, (size_t)n);
      cp[n++] = '\0';

      lp = scalloc(1, sizeof(*lp) - VFIELD_SIZEOF(struct mline, l_line) + n);
      memcpy(lp->l_line, cp, n);
      lp->l_length = (size_t)--n;
      if (lst != NULL) {
         lnd->l_next = lp;
         lnd = lp;
      } else
         lst = lnd = lp;
   }
   mp->ma_contents = lst;
   mp->ma_maxlen = maxlen;

   if (_malook(mp->ma_name, mp, mafl) != NULL) {
      if (! (mafl & MA_ACC)) {
         fprintf(stderr, tr(76, "A macro named \"%s\" already exists.\n"),
            mp->ma_name);
         lst = mp->ma_contents;
         goto jerr;
      }
      _undef1(mp->ma_name, MA_ACC);
      _malook(mp->ma_name, mp, MA_ACC);
   }

   rv = TRU1;
jleave:
   if (linebuf != NULL)
      free(linebuf);
   return rv;
jerr:
   if (lst != NULL)
      _freelines(lst);
   free(mp->ma_name);
   free(mp);
   goto jleave;
}

static void
_undef1(char const *name, enum ma_flags mafl)
{
   struct macro *mp;

   if ((mp = _malook(name, NULL, mafl | MA_UNDEF)) != NULL) {
      _freelines(mp->ma_contents);
      free(mp->ma_name);
      free(mp);
   }
}

static void
_freelines(struct mline *lp)
{
   struct mline *lq;

   for (lq = NULL; lp != NULL; ) {
      if (lq != NULL)
         free(lq);
      lq = lp;
      lp = lp->l_next;
   }
   if (lq)
      free(lq);
}

static int
__var_list_all_cmp(void const *s1, void const *s2)
{
   return strcmp(*(char**)UNCONST(s1), *(char**)UNCONST(s2));
}

static void
_localopts_add(struct lostack *losp, char const *name, struct var *ovap)
{
   struct var *vap;
   size_t nl, vl;

   /* Propagate unrolling up the stack, as necessary */
   while (! losp->s_unroll && (losp = losp->s_up) != NULL)
      ;
   if (losp == NULL)
      goto jleave;

   /* We have found a level that wants to unroll; check wether it does it yet */
   for (vap = losp->s_localopts; vap != NULL; vap = vap->v_link)
      if (strcmp(vap->v_name, name) == 0)
         goto jleave;

   nl = strlen(name) + 1;
   vl = (ovap != NULL) ? strlen(ovap->v_value) + 1 : 0;
   vap = smalloc(sizeof(*vap) + nl + vl);
   vap->v_link = losp->s_localopts;
   losp->s_localopts = vap;
   vap->v_name = (char*)(vap + 1);
   memcpy(vap->v_name, name, nl);
   if (vl == 0)
      vap->v_value = NULL;
   else {
      vap->v_value = (char*)(vap + 1) + nl;
      memcpy(vap->v_value, ovap->v_value, vl);
   }
jleave:
   ;
}

static void
_localopts_unroll(struct var **vapp)
{
   struct lostack *save_los;
   struct var *x, *vap;

   vap = *vapp;
   *vapp = NULL;

   save_los = _localopts;
   _localopts = NULL;
   while (vap != NULL) {
      x = vap;
      vap = vap->v_link;
      var_assign(x->v_name, x->v_value);
      free(x);
   }
   _localopts = save_los;
}

FL void
var_assign(char const *name, char const *val)
{
   struct var *vp;
   ui_it h;
   char *oval;

   if (val == NULL) {
      bool_t tmp = unset_allow_undefined;
      unset_allow_undefined = TRU1;
      var_unset(name);
      unset_allow_undefined = tmp;
      goto jleave;
   }

   name = _canonify(name);
   h = MA_HASH(name);
   vp = _lookup(name, h, TRU1);

   /* Don't care what happens later on, store this in the unroll list */
   if (_localopts != NULL)
      _localopts_add(_localopts, name, vp);

   if (vp == NULL) {
      vp = (struct var*)scalloc(1, sizeof *vp);
      vp->v_name = _vcopy(name);
      vp->v_link = _vars[h];
      _vars[h] = vp;
      oval = UNCONST("");
   } else
      oval = vp->v_value;
   vp->v_value = _vcopy(val);

   /* Check if update allowed XXX wasteful on error! */
   if (! _check_special_vars(name, TRU1, &vp->v_value)) {
      char *cp = vp->v_value;
      vp->v_value = oval;
      oval = cp;
   }
   if (*oval != '\0')
      _vfree(oval);
jleave:
   ;
}

FL int
var_unset(char const *name)
{
   int ret = 1;
   ui_it h;
   struct var *vp;

   name = _canonify(name);
   h = MA_HASH(name);
   vp = _lookup(name, h, TRU1);

   if (vp == NULL) {
      if (! sourcing && ! unset_allow_undefined) {
         fprintf(stderr, tr(203, "\"%s\": undefined variable\n"), name);
         goto jleave;
      }
   } else {
      if (_localopts != NULL)
         _localopts_add(_localopts, name, vp);

      /* Always listhead after _lookup() */
      _vars[h] = _vars[h]->v_link;
      _vfree(vp->v_name);
      _vfree(vp->v_value);
      free(vp);

      _check_special_vars(name, FAL0, NULL);
   }
   ret = 0;
jleave:
   return ret;
}

FL char *
var_lookup(char const *name, bool_t look_environ)
{
   struct var *vp;
   char *rv;

   name = _canonify(name);
   if ((vp = _lookup(name, 0, FAL0)) != NULL)
      rv = vp->v_value;
   else if (! look_environ)
      rv = NULL;
   else if ((rv = getenv(name)) != NULL && *rv != '\0')
      rv = savestr(rv);
   return rv;
}

FL void
var_list_all(void)
{
   FILE *fp;
   char *cp, **vacp, **cap;
   struct var *vp;
   size_t no, i;
   char const *fmt;

   if ((fp = Ftemp(&cp, "Ra", "w+", 0600, 1)) == NULL) {
      perror("tmpfile");
      goto jleave;
   }
   rm(cp);
   Ftfree(&cp);

   for (no = i = 0; i < MA_PRIME; ++i)
      for (vp = _vars[i]; vp != NULL; vp = vp->v_link)
         ++no;
   vacp = salloc(no * sizeof(*vacp));
   for (cap = vacp, i = 0; i < MA_PRIME; ++i)
      for (vp = _vars[i]; vp != NULL; vp = vp->v_link)
         *cap++ = vp->v_name;

   if (no > 1)
      qsort(vacp, no, sizeof *vacp, &__var_list_all_cmp);

   i = (boption("bsdcompat") || boption("bsdset"));
   fmt = (i != 0) ? "%s\t%s\n" : "%s=\"%s\"\n";

   for (cap = vacp; no != 0; ++cap, --no) {
      cp = value(*cap); /* TODO internal lookup; binary? value? */
      if (cp == NULL)
         cp = UNCONST("");
      if (i || *cp != '\0')
         fprintf(fp, fmt, *cap, cp);
      else
         fprintf(fp, "%s\n", *cap);
   }

   page_or_print(fp, (size_t)(cap - vacp));
   Fclose(fp);
jleave:
   ;
}

FL int
cdefine(void *v)
{
   int rv = 1;
   char **args = v;
   char const *errs;

   if (args[0] == NULL) {
      errs = tr(504, "Missing macro name to `define'");
      goto jerr;
   }
   if (args[1] == NULL || strcmp(args[1], "{") || args[2] != NULL) {
      errs = tr(505, "Syntax is: define <name> {");
      goto jerr;
   }
   rv = ! _define1(args[0], MA_NONE);
jleave:
   return rv;
jerr:
   fprintf(stderr, "%s\n", errs);
   goto jleave;
}

FL int
cundef(void *v)
{
   int rv = 1;
   char **args = v;

   if (*args == NULL) {
      fprintf(stderr, tr(506, "Missing macro name to `undef'\n"));
      goto jleave;
   }
   do
      _undef1(*args, MA_NONE);
   while (*++args);
   rv = 0;
jleave:
   return rv;
}

FL int
ccall(void *v)
{
   int rv = 1;
   char **args = v;
   char const *errs, *name;
   struct macro *mp;

   if (args[0] == NULL || (args[1] != NULL && args[2] != NULL)) {
      errs = tr(507, "Syntax is: call <%s>\n");
      name = "name";
      goto jerr;
   }

   if ((mp = _malook(*args, NULL, MA_NONE)) == NULL) {
      errs = tr(508, "Undefined macro called: \"%s\"\n");
      name = *args;
      goto jerr;
   }

   rv = _maexec(mp, NULL);
jleave:
   return rv;
jerr:
   fprintf(stderr, errs, name);
   goto jleave;
}

FL int
callhook(char const *name, int nmail)
{
   int len, rv;
   struct macro *mp;
   char *var, *cp;

   var = ac_alloc(len = strlen(name) + 13);
   snprintf(var, len, "folder-hook-%s", name);
   if ((cp = value(var)) == NULL && (cp = value("folder-hook")) == NULL) {
      rv = 0;
      goto jleave;
   }
   if ((mp = _malook(cp, NULL, MA_NONE)) == NULL) {
      fprintf(stderr, tr(49, "Cannot call hook for folder \"%s\": "
         "Macro \"%s\" does not exist.\n"), name, cp);
      rv = 1;
      goto jleave;
   }

   inhook = nmail ? 3 : 1;
   rv = _maexec(mp, NULL);
   inhook = 0;
jleave:
   ac_free(var);
   return rv;
}

FL int
cdefines(void *v)
{
   (void)v;
   return _list_macros(MA_NONE);
}

FL int
c_account(void *v)
{
   char **args = v;
   struct macro *mp;
   int rv = 1, i, oqf, nqf;

   if (args[0] == NULL) {
      rv = _list_macros(MA_ACC);
      goto jleave;
   }

   if (args[1] && args[1][0] == '{' && args[1][1] == '\0') {
      if (args[2] != NULL) {
         fprintf(stderr, tr(517, "Syntax is: account <name> {\n"));
         goto jleave;
      }
      if (asccasecmp(args[0], ACCOUNT_NULL) == 0) {
         fprintf(stderr, tr(521, "Error: `%s' is a reserved name.\n"),
            ACCOUNT_NULL);
         goto jleave;
      }
      rv = ! _define1(args[0], MA_ACC);
      goto jleave;
   }

   if (inhook) {
      fprintf(stderr, tr(518, "Cannot change account from within a hook.\n"));
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   mp = NULL;
   if (asccasecmp(args[0], ACCOUNT_NULL) != 0 &&
         (mp = _malook(args[0], NULL, MA_ACC)) == NULL) {
      fprintf(stderr, tr(519, "Account `%s' does not exist.\n"), args[0]);
      goto jleave;
   }

   oqf = savequitflags();
   if (_acc_curr != NULL)
      _localopts_unroll(&_acc_curr->ma_localopts);
   account_name = (mp != NULL) ? mp->ma_name : NULL;
   _acc_curr = mp;

   if (mp != NULL && _maexec(mp, &mp->ma_localopts) == CBAD) {
      /* XXX account switch incomplete, unroll? */
      fprintf(stderr, tr(520, "Switching to account `%s' failed.\n"), args[0]);
      goto jleave;
   }

   if (! starting && ! inhook) {
      nqf = savequitflags(); /* TODO obsolete (leave -> void -> new box!) */
      restorequitflags(oqf);
      if ((i = setfile("%", 0)) < 0)
         goto jleave;
      callhook(mailname, 0);
      if (i > 0 && ! boption("emptystart"))
         goto jleave;
      announce(boption("bsdcompat") || boption("bsdannounce"));
      restorequitflags(nqf);
   }
   rv = 0;
jleave:
   return rv;
}

FL int
c_localopts(void *v)
{
   int rv = 1;
   char **c = v;

   if (_localopts == NULL) {
      fprintf(stderr, tr(522,
         "Cannot use `localopts' but from within a `define' or `account'\n"));
      goto jleave;
   }

   _localopts->s_unroll = (**c == '0') ? FAL0 : TRU1;
   rv = 0;
jleave:
   return rv;
}

/* vim:set fenc=utf-8:s-it-mode */
