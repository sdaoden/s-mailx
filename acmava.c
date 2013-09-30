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

#include "nail.h"

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
   struct line    *ma_contents;
   enum ma_flags  ma_flags;
};

struct acc {
   struct macro   ac_super;
   struct accvar  *ac_list;
};

struct line {
   struct line *l_next;
   size_t      l_linesize;
   char        l_line[VFIELD_SIZE(sizeof(size_t))];
};

struct var {
   struct var  *v_link;
   char        *v_name;
   char        *v_value;
};

struct accvar {
   struct accvar  *av_link;
   char           *av_name;   /* Canonicalized */
   char           *av_oval;   /* Value before `account' switch */
   char           *av_val;
};

static struct acc    *_acc_curr;
static bool_t        _acc_in_switch;

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
static int           _maexec(struct macro *mp);

/* User display helpers */
static int           _list_macros(enum ma_flags mafl);
static void          __list_line(FILE *fp, struct line *lp);

/*  */
static bool_t        _define1(char const *name, enum ma_flags mafl);
static void          _undef1(char const *name, enum ma_flags mafl);
static void          _freelines(struct line *lp);

/* qsort(3) helper */
static int           __var_list_all_cmp(void const *s1, void const *s2);

/* Update account replay-log */
static void          _acc_value_update(char const *name, char const *oval,
                        char const *val);
static void          _acc_value_restore(void);

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
   if (*cp)
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
#if ! defined HAVE_READLINE && ! defined HAVE_EDITLINE &&\
      defined HAVE_LINE_EDITOR
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
_maexec(struct macro *mp)
{
   int rv = 0;
   struct line *lp;
   char const *sp, *smax;
   char *copy, *cp;

   unset_allow_undefined = TRU1;
   for (lp = mp->ma_contents; lp; lp = lp->l_next) {
      sp = lp->l_line;
      smax = sp + lp->l_linesize;
      while (sp < smax && (blankchar(*sp) || *sp == '\n' || *sp == '\0'))
         ++sp;
      if (sp == smax)
         continue;
      cp = copy = ac_alloc(lp->l_linesize + (lp->l_line - sp));
      do
         *cp++ = (*sp != '\n') ? *sp : ' ';
      while (++sp < smax);
      rv = execute(copy, 0, (size_t)(cp - copy));
      ac_free(copy);
   }
   unset_allow_undefined = FAL0;
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
   struct line *lp;

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
               __list_line(fp, lp);
            fputs("}\n", fp);
         }
   if (mc)
      page_or_print(fp, 0);

   mc = (ui_it)ferror(fp);
   Fclose(fp);
   return (int)mc;
}

static void
__list_line(FILE *fp, struct line *lp)
{
   char const *sp = lp->l_line, *spmax = sp + lp->l_linesize;
   int c;

   for (; sp < spmax; ++sp) {
      if ((c = *sp & 0xFF) != '\0') {
         if (c == '\n')
            putc('\\', fp);
         putc(c, fp);
      }
   }
   putc('\n', fp);
}

static bool_t
_define1(char const *name, enum ma_flags mafl)
{
   bool_t rv = FAL0;
   struct macro *mp;
   struct line *lp, *lst = NULL, *lnd = NULL;
   char *linebuf = NULL;
   size_t linesize = 0;
   int n;

   mp = scalloc(1, (mafl & MA_ACC) ? sizeof(struct acc) : sizeof *mp);
   mp->ma_name = sstrdup(name);
   mp->ma_flags = mafl;

   for (;;) {
      n = readline_input(LNED_LF_ESC, "", &linebuf, &linesize);
      if (n < 0) {
         fprintf(stderr,
            tr(75, "Unterminated %s definition: \"%s\".\n"),
            (mafl & MA_ACC ? "account" : "macro"), mp->ma_name);
         if (sourcing)
            unstack();
         goto jerr;
      }
      if (_is_closing_angle(linebuf))
         break;

      ++n;
      lp = scalloc(1, sizeof(*lp) - VFIELD_SIZEOF(struct line, l_line) + n);
      lp->l_linesize = (size_t)n;
      memcpy(lp->l_line, linebuf, n);
      assert(lp->l_line[n - 1] == '\0');
      if (lst != NULL) {
         lnd->l_next = lp;
         lnd = lp;
      } else
         lst = lnd = lp;
   }
   mp->ma_contents = lst;

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
_freelines(struct line *lp)
{
   struct line *lq;

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
_acc_value_update(char const *name, char const *oval, char const *val)
{
   struct accvar *avp;

   if (_acc_curr == NULL)
      goto jleave;

   for (avp = _acc_curr->ac_list; avp != NULL; avp = avp->av_link)
      if (strcmp(avp->av_name, name) == 0) {
         if (avp->av_oval != NULL)
            _vfree(avp->av_oval);
         if (avp->av_val != NULL)
            _vfree(avp->av_val);
         goto jup;
      }

   avp = smalloc(sizeof(struct accvar));
   avp->av_link = _acc_curr->ac_list;
   _acc_curr->ac_list = avp;
   avp->av_name = sstrdup(name);
jup:
   avp->av_oval = (oval != NULL) ? _vcopy(oval) : NULL;
   avp->av_val = (val != NULL) ? _vcopy(val) : NULL;
jleave:
   ;
}

static void
_acc_value_restore(void) /* TODO optimize (restore log in general!) */
{
   struct accvar *x, *avp;

   if (_acc_curr == NULL)
      goto jleave;

   for (avp = _acc_curr->ac_list; avp != NULL;) {
      char *cv = var_lookup(avp->av_name, FAL0);
      if (cv == NULL) {
         if (avp->av_oval == NULL)
            goto jcont;
      } else if (avp->av_val != NULL && strcmp(avp->av_val, cv) != 0)
         goto jcont;
      var_assign(avp->av_name, avp->av_oval);
jcont:
      x = avp;
      avp = avp->av_link;
      free(x->av_name);
      if (x->av_oval != NULL)
         _vfree(x->av_oval);
      if (x->av_val != NULL)
         _vfree(x->av_val);
      free(x);
   }
   _acc_curr->ac_list = NULL;
jleave:
   ;
}

void
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

   if (_acc_in_switch)
      _acc_value_update(name, (vp != NULL ? vp->v_value : NULL), val);

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

int
var_unset(char const *name)
{
   int ret = 1;
   ui_it h;
   struct var *vp;

   name = _canonify(name);
   h = MA_HASH(name);
   vp = _lookup(name, h, TRU1);

   if (_acc_in_switch)
      _acc_value_update(name, (vp != NULL ? vp->v_value : NULL), NULL);

   if (vp == NULL) {
      if (! sourcing && ! unset_allow_undefined) {
         fprintf(stderr,
            tr(203, "\"%s\": undefined variable\n"), name);
         goto jleave;
      }
   } else {
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

char *
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

void
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

int
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

int
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

int
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
   rv = _maexec(mp);
jleave:
   return rv;
jerr:
   fprintf(stderr, errs, name);
   goto jleave;
}

int
callhook(char const *name, int nmail)
{
   int len, r;
   struct macro *mp;
   char *var, *cp;

   var = ac_alloc(len = strlen(name) + 13);
   snprintf(var, len, "folder-hook-%s", name);
   if ((cp = value(var)) == NULL && (cp = value("folder-hook")) == NULL) {
      r = 0;
      goto jleave;
   }
   if ((mp = _malook(cp, NULL, MA_NONE)) == NULL) {
      fprintf(stderr, tr(49, "Cannot call hook for folder \"%s\": "
         "Macro \"%s\" does not exist.\n"), name, cp);
      r = 1;
      goto jleave;
   }
   inhook = nmail ? 3 : 1;
   r = _maexec(mp);
   inhook = 0;
jleave:
   ac_free(var);
   return r;
}

int
cdefines(void *v)
{
   (void)v;
   return _list_macros(MA_NONE);
}

int
c_account(void *v)
{
   char **args = v, *cp;
   int rv = 1, i, oqf, nqf;
   struct macro *mp;

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

   if ((cp = expand("&")) == NULL)
      goto jleave;
   n_strlcpy(mboxname, cp, sizeof mboxname);

   mp = NULL;
   if (asccasecmp(args[0], ACCOUNT_NULL) != 0 &&
         (mp = _malook(args[0], NULL, MA_ACC)) == NULL) {
      fprintf(stderr, tr(519, "Account `%s' does not exist.\n"), args[0]);
      goto jleave;
   }

   oqf = savequitflags();
   account_name = (mp != NULL) ? mp->ma_name : NULL;
   _acc_value_restore();
   _acc_curr = (struct acc*)mp;

   _acc_in_switch = TRU1;
   if (mp != NULL && _maexec(mp) == CBAD) {
      _acc_in_switch = FAL0;
      fprintf(stderr, tr(520, "Switching to account `%s' failed.\n"), args[0]);
      goto jleave;
   }
   _acc_in_switch = FAL0;

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

/* vim:set fenc=utf-8:s-it-mode */
