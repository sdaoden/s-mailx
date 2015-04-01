/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Account, macro and variable handling.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#undef n_FILE
#define n_FILE accmacvar

#ifndef HAVE_AMALGAMATION
# define _ACCMACVAR_SOURCE /* For _features[] */
# include "nail.h"
#endif

/*
 * HOWTO add a new non-dynamic binary or value option:
 * - add an entry to nail.h:enum okeys
 * - run create-okey-map.pl
 * - update nail.1
 */

/* Note: changing the hash function must be reflected in create-okey-map.pl */
#define MA_PRIME           HSHSIZE
#define MA_NAME2HASH(N)    torek_hash(N)
#define MA_HASH2PRIME(H)   ((H) % MA_PRIME)

enum ma_flags {
   MA_NONE        = 0,
   MA_ACC         = 1<<0,
   MA_TYPE_MASK   = MA_ACC,
   MA_UNDEF       = 1<<1,        /* Unlink after lookup */
   MA_DELETED     = 1<<13        /* Only for _acc_curr: deleted while active */
};

enum var_map_flags {
   VM_NONE     = 0,
   VM_BINARY   = 1<<0,           /* ok_b_* */
   VM_RDONLY   = 1<<1,           /* May not be set by user */
   VM_SPECIAL  = 1<<2,           /* Wants _var_check_specials() evaluation */
   VM_VIRTUAL  = 1<<3            /* "Stateless": no var* -- implies VM_RDONLY */
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
   struct mline   *l_next;
   size_t         l_length;
   char           l_line[VFIELD_SIZE(sizeof(size_t))];
};

struct var {
   struct var     *v_link;
   char           *v_value;
   char           v_name[VFIELD_SIZE(sizeof(size_t))];
};

struct var_virtual {
   ui32_t         vv_okey;
   struct var const *vv_var;
};

struct var_map {
   ui32_t         vm_hash;
   ui16_t         vm_keyoff;
   ui16_t         vm_flags;      /* var_map_flags bits */
};

struct var_carrier {
   char const     *vc_name;
   ui32_t         vc_hash;
   ui32_t         vc_prime;
   struct var     *vc_var;
   struct var_map const *vc_vmap;
   enum okeys     vc_okey;
};

struct lostack {
   struct lostack *s_up;         /* Outer context */
   struct macro   *s_mac;        /* Context (`account' or `define') */
   struct var     *s_localopts;
   bool_t         s_unroll;      /* Unroll? */
};

/* Include the constant create-okey-map.pl output */
#include "version.h"
#include "okeys.h"

static struct macro  *_acc_curr;    /* Currently active account */
static struct lostack *_localopts;  /* Currently executing macro unroll list */
/* TODO We really deserve localopts support for *folder-hook*s, so hack it in
 * TODO today via a static lostack, it should be a field in mailbox, once that
 * TODO is a real multi-instance object */
static struct var    *_folder_hook_localopts;

/* TODO once we have a dynamically sized hashtable we could unite _macros and
 * TODO _variables into a single hashtable, stripping down fun interface;
 * TODO also, setting and clearing a variable can be easily joined */
static struct var    *_vars[MA_PRIME];    /* TODO dynamically spaced */
static struct macro  *_macros[MA_PRIME];  /* TODO dynamically spaced */

/* Special cased value string allocation */
static char *        _var_vcopy(char const *str);
static void          _var_vfree(char *cp);

/* Check for special housekeeping. */
static bool_t        _var_check_specials(enum okeys okey, bool_t enable,
                        char **val);

/* If a variable name begins with a lowercase-character and contains at
 * least one '@', it is converted to all-lowercase. This is necessary
 * for lookups of names based on email addresses.
 * Following the standard, only the part following the last '@' should
 * be lower-cased, but practice has established otherwise here.
 * Return value may have been placed in string dope (salloc()) */
static char const *  _var_canonify(char const *vn);

/* Try to reverse lookup an option name to an enum okeys mapping.
 * Updates vcp.vc_name and vcp.vc_hash; vcp.vc_vmap is NULL if none found */
static bool_t        _var_revlookup(struct var_carrier *vcp, char const *name);

/* Lookup a variable from vcp.vc_(vmap|name|hash), return wether it was found.
 * Sets vcp.vc_prime; vcp.vc_var is NULL if not found */
static bool_t        _var_lookup(struct var_carrier *vcp);

/* Set variable from vcp.vc_(vmap|name|hash), return success */
static bool_t        _var_set(struct var_carrier *vcp, char const *value);

/* Clear variable from vcp.vc_(vmap|name|hash); sets vcp.vc_var to NULL,
 * return success */
static bool_t        _var_clear(struct var_carrier *vcp);

/* List all variables */
static void          _var_list_all(void);

static int           __var_list_all_cmp(void const *s1, void const *s2);

/* Shared c_set() and c_setenv() impl, return success */
static bool_t        _var_set_env(char **ap, bool_t issetenv);

/* Does cp consist solely of WS and a } */
static bool_t        _is_closing_angle(char const *cp);

/* Lookup for macros/accounts */
static struct macro *_ma_look(char const *name, struct macro *data,
                        enum ma_flags mafl);

/* Walk all lines of a macro and execute() them */
static int           _ma_exec(struct macro const *mp, struct var **unroller);

/* User display helpers */
static int           _ma_list(enum ma_flags mafl);

/* */
static bool_t        _ma_define(char const *name, enum ma_flags mafl);
static void          _ma_undefine(char const *name, enum ma_flags mafl);
static void          _ma_freelines(struct mline *lp);

/* Update replay-log */
static void          _localopts_add(struct lostack *losp, char const *name,
                        struct var *ovap);
static void          _localopts_unroll(struct var **vapp);

static char *
_var_vcopy(char const *str)
{
   char *news;
   size_t len;
   NYD2_ENTER;

   if (*str == '\0')
      news = UNCONST("");
   else {
      len = strlen(str) +1;
      news = smalloc(len);
      memcpy(news, str, len);
   }
   NYD2_LEAVE;
   return news;
}

static void
_var_vfree(char *cp)
{
   NYD2_ENTER;
   if (*cp != '\0')
      free(cp);
   NYD2_LEAVE;
}

static bool_t
_var_check_specials(enum okeys okey, bool_t enable, char **val)
{
   char *cp = NULL;
   bool_t ok = TRU1;
   int flag = 0;
   NYD2_ENTER;

   switch (okey) {
   case ok_b_debug:
      flag = OPT_DEBUG;
      break;
   case ok_b_header:
      flag = OPT_N_FLAG;
      enable = !enable;
      break;
   case ok_b_memdebug:
      flag = OPT_MEMDEBUG;
      break;
   case ok_b_skipemptybody:
      flag = OPT_E_FLAG;
      break;
   case ok_b_verbose:
      flag = (enable && !(options & OPT_VERB))
            ? OPT_VERB : OPT_VERB | OPT_VERBVERB;
      break;
   case ok_v_folder:
      ok = (val != NULL && var_folder_updated(*val, &cp));
      if (ok && cp != NULL) {
         _var_vfree(*val);
         /* It's smalloc()ed, but ensure we don't leak */
         if (*cp == '\0') {
            *val = UNCONST("");
            free(cp);
         } else
            *val = cp;
      }
      break;
#ifdef HAVE_NCL
   case ok_v_line_editor_cursor_right:
      if ((ok = (val != NULL && *val != NULL))) {
         /* Set with no value? TODO very guly */
         if (*(cp = *val) != '\0') {
            char const *x = cp;
            int c;
            do {
               c = expand_shell_escape(&x, FAL0);
               if (c < 0)
                  break;
               *cp++ = (char)c;
            } while (*x != '\0');
            *cp = '\0';
         }
      }
      break;
#endif
   default:
      break;
   }

   if (flag) {
      if (enable)
         options |= flag;
      else
         options &= ~flag;
   }
   NYD2_LEAVE;
   return ok;
}

static char const *
_var_canonify(char const *vn)
{
   NYD2_ENTER;
   if (!upperchar(*vn)) {
      char const *vp;

      for (vp = vn; *vp != '\0' && *vp != '@'; ++vp)
         ;
      vn = (*vp == '@') ? i_strdup(vn) : vn;
   }
   NYD2_LEAVE;
   return vn;
}

static bool_t
_var_revlookup(struct var_carrier *vcp, char const *name)
{
   ui32_t hash, i, j;
   struct var_map const *vmp;
   NYD2_ENTER;

   vcp->vc_name = name = _var_canonify(name);
   vcp->vc_hash = hash = MA_NAME2HASH(name);

   for (i = hash % _VAR_REV_PRIME, j = 0; j <= _VAR_REV_LONGEST; ++j) {
      ui32_t x = _var_revmap[i];
      if (x == _VAR_REV_ILL)
         break;
      vmp = _var_map + x;
      if (vmp->vm_hash == hash && !strcmp(_var_keydat + vmp->vm_keyoff, name)) {
         vcp->vc_vmap = vmp;
         vcp->vc_okey = (enum okeys)x;
         goto jleave;
      }
      if (++i == _VAR_REV_PRIME) {
#ifdef _VAR_REV_WRAPAROUND
         i = 0;
#else
         break;
#endif
      }
   }
   vcp->vc_vmap = NULL;
   vcp = NULL;
jleave:
   NYD2_LEAVE;
   return (vcp != NULL);
}

static bool_t
_var_lookup(struct var_carrier *vcp)
{
   struct var **vap, *lvp, *vp;
   NYD2_ENTER;

   /* XXX _So_ unlikely that it should be checked if normal lookup fails! */
   if (UNLIKELY(vcp->vc_vmap != NULL &&
         (vcp->vc_vmap->vm_flags & VM_VIRTUAL) != 0)) {
      struct var_virtual const *vvp;

      for (vvp = _var_virtuals;
            PTRCMP(vvp, <, _var_virtuals + NELEM(_var_virtuals)); ++vvp)
         if (vvp->vv_okey == vcp->vc_okey) {
            vp = UNCONST(vvp->vv_var);
            goto jleave;
         }
      assert(0);
   }

   vap = _vars + (vcp->vc_prime = MA_HASH2PRIME(vcp->vc_hash));

   for (lvp = NULL, vp = *vap; vp != NULL; lvp = vp, vp = vp->v_link)
      if (!strcmp(vp->v_name, vcp->vc_name)) {
         /* Relink as head, hope it "sorts on usage" over time.
          * _var_clear() relies on this behaviour */
         if (lvp != NULL) {
            lvp->v_link = vp->v_link;
            vp->v_link = *vap;
            *vap = vp;
         }
         goto jleave;
      }
   vp = NULL;
jleave:
   vcp->vc_var = vp;
   NYD2_LEAVE;
   return (vp != NULL);
}

static bool_t
_var_set(struct var_carrier *vcp, char const *value)
{
   struct var *vp;
   char *oval;
   bool_t ok = TRU1;
   NYD2_ENTER;

   if (value == NULL) {
      ok = _var_clear(vcp);
      goto jleave;
   }

   _var_lookup(vcp);

   if (vcp->vc_vmap != NULL && (vcp->vc_vmap->vm_flags & VM_RDONLY)) {
      fprintf(stderr, _("Variable readonly: \"%s\"\n"), vcp->vc_name);
      ok = FAL0;
      goto jleave;
   }

   /* Don't care what happens later on, store this in the unroll list */
   if (_localopts != NULL)
      _localopts_add(_localopts, vcp->vc_name, vcp->vc_var);

   if ((vp = vcp->vc_var) == NULL) {
      size_t l = strlen(vcp->vc_name) + 1;

      vcp->vc_var =
      vp = smalloc(sizeof(*vp) - VFIELD_SIZEOF(struct var, v_name) + l);
      vp->v_link = _vars[vcp->vc_prime];
      _vars[vcp->vc_prime] = vp;
      memcpy(vp->v_name, vcp->vc_name, l);
      oval = UNCONST("");
   } else
      oval = vp->v_value;

   if (vcp->vc_vmap == NULL)
      vp->v_value = _var_vcopy(value);
   else {
      /* Via `set' etc. the user may give even binary options non-binary
       * values, ignore that and force binary xxx error log? */
      if (vcp->vc_vmap->vm_flags & VM_BINARY)
         value = UNCONST("");
      vp->v_value = _var_vcopy(value);

      /* Check if update allowed XXX wasteful on error! */
      if ((vcp->vc_vmap->vm_flags & VM_SPECIAL) &&
            !(ok = _var_check_specials(vcp->vc_okey, TRU1, &vp->v_value))) {
         char *cp = vp->v_value;
         vp->v_value = oval;
         oval = cp;
      }
   }

   if (*oval != '\0')
      _var_vfree(oval);
jleave:
   NYD2_LEAVE;
   return ok;
}

static bool_t
_var_clear(struct var_carrier *vcp)
{
   bool_t ok = TRU1;
   NYD2_ENTER;

   if (!_var_lookup(vcp)) {
      if (!(pstate & PS_IN_LOAD) && (options & OPT_D_V))
         fprintf(stderr, _("Variable undefined: \"%s\"\n"), vcp->vc_name);
   } else if (vcp->vc_vmap != NULL && (vcp->vc_vmap->vm_flags & VM_RDONLY)) {
      fprintf(stderr, _("Variable readonly: \"%s\"\n"), vcp->vc_name);
      ok = FAL0;
   } else {
      if (_localopts != NULL)
         _localopts_add(_localopts, vcp->vc_name, vcp->vc_var);

      /* Always listhead after _var_lookup() */
      _vars[vcp->vc_prime] = _vars[vcp->vc_prime]->v_link;
      _var_vfree(vcp->vc_var->v_value);
      free(vcp->vc_var);
      vcp->vc_var = NULL;

      if (vcp->vc_vmap != NULL && (vcp->vc_vmap->vm_flags & VM_SPECIAL))
         ok = _var_check_specials(vcp->vc_okey, FAL0, NULL);
   }
   NYD2_LEAVE;
   return ok;
}

static void
_var_list_all(void)
{
   FILE *fp;
   size_t no, i;
   struct var *vp;
   char const **vacp, **cap, *fmt;
   NYD2_ENTER;

   if ((fp = Ftmp(NULL, "listvars", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tmpfile");
      goto jleave;
   }

   for (no = i = 0; i < MA_PRIME; ++i)
      for (vp = _vars[i]; vp != NULL; vp = vp->v_link)
         ++no;
   no += NELEM(_var_virtuals);

   vacp = salloc(no * sizeof(*vacp));

   for (cap = vacp, i = 0; i < MA_PRIME; ++i)
      for (vp = _vars[i]; vp != NULL; vp = vp->v_link)
         *cap++ = vp->v_name;
   for (i = 0; i < NELEM(_var_virtuals); ++i)
      *cap++ = _var_virtuals[i].vv_var->v_name;

   if (no > 1)
      qsort(vacp, no, sizeof *vacp, &__var_list_all_cmp);

   i = (ok_blook(bsdcompat) || ok_blook(bsdset));
   fmt = (i != 0) ? "%s\t%s\n" : "%s=\"%s\"\n";

   /* TODO rework _var_list_all() output a la mimetypes (in i==0 mode) */
   for (cap = vacp; no != 0; ++cap, --no) {
      char const *cp = vok_vlook(*cap); /* XXX when lookup checks val/bin... */
      if (cp == NULL)
         cp = "";
      if (i || *cp != '\0')
         fprintf(fp, fmt, *cap, cp);
      else
         fprintf(fp, "%s\n", *cap);
   }

   page_or_print(fp, PTR2SIZE(cap - vacp));
   Fclose(fp);
jleave:
   NYD2_LEAVE;
}

static int
__var_list_all_cmp(void const *s1, void const *s2)
{
   int rv;
   NYD2_ENTER;

   rv = strcmp(*(char**)UNCONST(s1), *(char**)UNCONST(s2));
   NYD2_LEAVE;
   return rv;
}

static bool_t
_var_set_env(char **ap, bool_t issetenv)
{
   char *cp, *cp2, *varbuf, c;
   size_t errs = 0;
   NYD2_ENTER;

   for (; *ap != NULL; ++ap) {
      /* Isolate key */
      cp = *ap;
      cp2 = varbuf = ac_alloc(strlen(cp) +1);
      for (; (c = *cp) != '=' && c != '\0'; ++cp)
         *cp2++ = c;
      *cp2 = '\0';
      if (c == '\0')
         cp = UNCONST("");
      else
         ++cp;
      if (varbuf == cp2) {
         fprintf(stderr, _("Non-null variable name required\n"));
         ++errs;
         goto jnext;
      }

      if (varbuf[0] == 'n' && varbuf[1] == 'o') {
         char const *k = varbuf + 2;

         if (issetenv && (!strcmp(k, "HOME") || /* TODO generic */
               !strcmp(k, "USER") || !strcmp(k, "TMPDIR"))) {/* TODO approach */
            if (options & OPT_D_V)
               fprintf(stderr, _("Cannot `unsetenv' \"%s\"\n"), k);
            ++errs;
            goto jnext;
         }

         errs += !_var_vokclear(k);
         if (issetenv) {
#ifdef HAVE_SETENV
            errs += (unsetenv(k) != 0);
#else
            ++errs;
#endif
         }
      } else {
         errs += !_var_vokset(varbuf, (uintptr_t)cp);
         if (issetenv) {
            do {
            static char const *cp_buf[3];
            char const **pl, **pe;

            if (!strcmp(varbuf, "HOME")) /* TODO generic */
               pl = cp_buf + 0, pe = &homedir;
            else if (!strcmp(varbuf, "USER")) /* TODO approach */
               pl = cp_buf + 1, pe = &myname;
            else if (!strcmp(varbuf, "TMPDIR")) /* TODO also here */
               pl = cp_buf + 2, pe = &tempdir;
            else
               break;

            if (*pl != NULL)
               free(UNCONST(*pl));
            *pe = *pl = sstrdup(cp);
            } while (0);

#ifdef HAVE_SETENV
            errs += (setenv(varbuf, cp, 1) != 0);
#else
            ++errs;
#endif
         }
      }
jnext:
      ac_free(varbuf);
   }

   NYD2_LEAVE;
   return (errs == 0);
}

static bool_t
_is_closing_angle(char const *cp)
{
   bool_t rv = FAL0;
   NYD2_ENTER;

   while (spacechar(*cp))
      ++cp;
   if (*cp++ != '}')
      goto jleave;
   while (spacechar(*cp))
      ++cp;
   rv = (*cp == '\0');
jleave:
   NYD2_LEAVE;
   return rv;
}

static struct macro *
_ma_look(char const *name, struct macro *data, enum ma_flags mafl)
{
   enum ma_flags save_mafl;
   ui32_t h;
   struct macro *lmp, *mp;
   NYD2_ENTER;

   save_mafl = mafl;
   mafl &= MA_TYPE_MASK;
   h = MA_NAME2HASH(name);
   h = MA_HASH2PRIME(h);

   for (lmp = NULL, mp = _macros[h]; mp != NULL; lmp = mp, mp = mp->ma_next) {
      if ((mp->ma_flags & MA_TYPE_MASK) == mafl && !strcmp(mp->ma_name, name)) {
         if (save_mafl & MA_UNDEF) {
            if (lmp == NULL)
               _macros[h] = mp->ma_next;
            else
               lmp->ma_next = mp->ma_next;

            /* TODO it should also be possible to delete running macros */
            if ((mafl & MA_ACC) &&
                  account_name != NULL && !strcmp(account_name, name)) {
               mp->ma_flags |= MA_DELETED;
               fprintf(stderr, _("Delayed deletion of active account \"%s\"\n"),
                  name);
            } else {
               _ma_freelines(mp->ma_contents);
               free(mp->ma_name);
               free(mp);
            }
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
   NYD2_LEAVE;
   return mp;
}

static int
_ma_exec(struct macro const *mp, struct var **unroller)
{
   struct lostack los;
   char *buf;
   struct n2 {struct n2 *up; struct lostack *lo;} *x; /* FIXME hack (sigman+) */
   struct mline const *lp;
   int rv = 0;
   NYD2_ENTER;

   los.s_up = _localopts;
   los.s_mac = UNCONST(mp);
   los.s_localopts = NULL;
   los.s_unroll = FAL0;
   _localopts = &los;

   x = salloc(sizeof *x); /* FIXME intermediate hack (signal man+) */
   x->up = temporary_localopts_store;
   x->lo = _localopts;
   temporary_localopts_store = x;

   buf = ac_alloc(mp->ma_maxlen +1);
   for (lp = mp->ma_contents; lp; lp = lp->l_next) {
      memcpy(buf, lp->l_line, lp->l_length +1);
      rv |= execute(buf, 0, lp->l_length); /* XXX break if != 0 ? */
   }
   ac_free(buf);

   temporary_localopts_store = x->up;  /* FIXME intermediate hack */

   _localopts = los.s_up;
   if (unroller == NULL) {
      if (los.s_localopts != NULL)
         _localopts_unroll(&los.s_localopts);
   } else
      *unroller = los.s_localopts;
   NYD2_LEAVE;
   return rv;
}

static int
_ma_list(enum ma_flags mafl)
{
   FILE *fp;
   char const *typestr;
   struct macro *mq;
   ui32_t ti, mc;
   struct mline *lp;
   NYD2_ENTER;

   if ((fp = Ftmp(NULL, "listmacs", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tmpfile");
      mc = 1;
      goto jleave;
   }

   mafl &= MA_TYPE_MASK;
   typestr = (mafl & MA_ACC) ? "account" : "define";

   for (ti = mc = 0; ti < MA_PRIME; ++ti)
      for (mq = _macros[ti]; mq; mq = mq->ma_next)
         if ((mq->ma_flags & MA_TYPE_MASK) == mafl) {
            if (++mc > 1)
               putc('\n', fp);
            fprintf(fp, "%s %s {\n", typestr, mq->ma_name);
            for (lp = mq->ma_contents; lp != NULL; lp = lp->l_next)
               fprintf(fp, "  %s\n", lp->l_line);
            fputs("}\n", fp);
         }
   if (mc)
      page_or_print(fp, 0);

   mc = (ui32_t)ferror(fp);
   Fclose(fp);
jleave:
   NYD2_LEAVE;
   return (int)mc;
}

static bool_t
_ma_define(char const *name, enum ma_flags mafl)
{
   bool_t rv = FAL0;
   struct macro *mp;
   struct mline *lp, *lst = NULL, *lnd = NULL;
   char *linebuf = NULL, *cp;
   size_t linesize = 0, maxlen = 0;
   int n, i;
   NYD2_ENTER;

   mp = scalloc(1, sizeof *mp);
   mp->ma_name = sstrdup(name);
   mp->ma_flags = mafl;

   for (;;) {
      n = readline_input("", TRU1, &linebuf, &linesize, NULL);
      if (n == 0)
         continue;
      if (n < 0) {
         fprintf(stderr, _("Unterminated %s definition: \"%s\".\n"),
            (mafl & MA_ACC ? "account" : "macro"), mp->ma_name);
         if ((pstate & PS_IN_LOAD) == PS_SOURCING)
            unstack();
         goto jerr;
      }
      if (_is_closing_angle(linebuf))
         break;

      /* Trim WS */
      for (cp = linebuf, i = 0; i < n; ++cp, ++i)
         if (!whitechar(*cp))
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

   if (_ma_look(mp->ma_name, mp, mafl) != NULL) {
      fprintf(stderr, _("A %s named \"%s\" already exists.\n"),
         (mafl & MA_ACC ? "account" : "macro"), mp->ma_name);
      lst = mp->ma_contents;
      goto jerr;
   }

   rv = TRU1;
jleave:
   if (linebuf != NULL)
      free(linebuf);
   NYD2_LEAVE;
   return rv;

jerr:
   if (lst != NULL)
      _ma_freelines(lst);
   free(mp->ma_name);
   free(mp);
   goto jleave;
}

static void
_ma_undefine(char const *name, enum ma_flags mafl)
{
   struct macro *mp;
   NYD2_ENTER;

   if (LIKELY(name[0] != '*' || name[1] != '\0')) {
      if ((mp = _ma_look(name, NULL, mafl | MA_UNDEF)) == NULL)
         fprintf(stderr, _("%s \"%s\" is not defined\n"),
            (mafl & MA_ACC ? "Account" : "Macro"), name);
   } else {
      struct macro **mpp, *lmp;

      for (mpp = _macros; PTRCMP(mpp, <, _macros + NELEM(_macros)); ++mpp)
         for (lmp = NULL, mp = *mpp; mp != NULL;) {
            if ((mp->ma_flags & MA_TYPE_MASK) == mafl) {
               /* xxx Expensive but rare: be simple */
               _ma_look(mp->ma_name, NULL, mafl | MA_UNDEF);
               mp = (lmp == NULL) ? *mpp : lmp->ma_next;
            } else {
               lmp = mp;
               mp = mp->ma_next;
            }
         }
   }
   NYD2_LEAVE;
}

static void
_ma_freelines(struct mline *lp)
{
   struct mline *lq;
   NYD2_ENTER;

   for (lq = NULL; lp != NULL; ) {
      if (lq != NULL)
         free(lq);
      lq = lp;
      lp = lp->l_next;
   }
   if (lq)
      free(lq);
   NYD2_LEAVE;
}

static void
_localopts_add(struct lostack *losp, char const *name, struct var *ovap)
{
   struct var *vap;
   size_t nl, vl;
   NYD2_ENTER;

   /* Propagate unrolling up the stack, as necessary */
   while (!losp->s_unroll && (losp = losp->s_up) != NULL)
      ;
   if (losp == NULL)
      goto jleave;

   /* We've found a level that wants to unroll; check wether it does it yet */
   for (vap = losp->s_localopts; vap != NULL; vap = vap->v_link)
      if (!strcmp(vap->v_name, name))
         goto jleave;

   nl = strlen(name) + 1;
   vl = (ovap != NULL) ? strlen(ovap->v_value) + 1 : 0;
   vap = smalloc(sizeof(*vap) - VFIELD_SIZEOF(struct var, v_name) + nl + vl);
   vap->v_link = losp->s_localopts;
   losp->s_localopts = vap;
   memcpy(vap->v_name, name, nl);
   if (vl == 0)
      vap->v_value = NULL;
   else {
      vap->v_value = vap->v_name + nl;
      memcpy(vap->v_value, ovap->v_value, vl);
   }
jleave:
   NYD2_LEAVE;
}

static void
_localopts_unroll(struct var **vapp)
{
   struct lostack *save_los;
   struct var *x, *vap;
   NYD2_ENTER;

   vap = *vapp;
   *vapp = NULL;

   save_los = _localopts;
   _localopts = NULL;
   while (vap != NULL) {
      x = vap;
      vap = vap->v_link;
      vok_vset(x->v_name, x->v_value);
      free(x);
   }
   _localopts = save_los;
   NYD2_LEAVE;
}

FL char *
_var_oklook(enum okeys okey)
{
   struct var_carrier vc;
   char *rv;
   NYD_ENTER;

   vc.vc_vmap = _var_map + okey;
   vc.vc_name = _var_keydat + _var_map[okey].vm_keyoff;
   vc.vc_hash = _var_map[okey].vm_hash;
   vc.vc_okey = okey;

   if (!_var_lookup(&vc)) {
      if ((rv = getenv(vc.vc_name)) != NULL) {
         _var_set(&vc, rv);
         assert(vc.vc_var != NULL);
         goto jvar;
      }
   } else
jvar:
      rv = vc.vc_var->v_value;
   NYD_LEAVE;
   return rv;
}

FL bool_t
_var_okset(enum okeys okey, uintptr_t val)
{
   struct var_carrier vc;
   bool_t ok;
   NYD_ENTER;

   vc.vc_vmap = _var_map + okey;
   vc.vc_name = _var_keydat + _var_map[okey].vm_keyoff;
   vc.vc_hash = _var_map[okey].vm_hash;
   vc.vc_okey = okey;

   ok = _var_set(&vc, (val == 0x1 ? "" : (char const*)val));
   NYD_LEAVE;
   return ok;
}

FL bool_t
_var_okclear(enum okeys okey)
{
   struct var_carrier vc;
   bool_t rv;
   NYD_ENTER;

   vc.vc_vmap = _var_map + okey;
   vc.vc_name = _var_keydat + _var_map[okey].vm_keyoff;
   vc.vc_hash = _var_map[okey].vm_hash;
   vc.vc_okey = okey;

   rv = _var_clear(&vc);
   NYD_LEAVE;
   return rv;
}

FL char *
_var_voklook(char const *vokey)
{
   struct var_carrier vc;
   char *rv;
   NYD_ENTER;

   _var_revlookup(&vc, vokey);

   if (!_var_lookup(&vc)) {
      if ((rv = getenv(vc.vc_name)) != NULL) {
         _var_set(&vc, rv);
         assert(vc.vc_var != NULL);
         goto jvar;
      }
   } else
jvar:
      rv = vc.vc_var->v_value;
   NYD_LEAVE;
   return rv;
}

FL bool_t
_var_vokset(char const *vokey, uintptr_t val)
{
   struct var_carrier vc;
   bool_t ok;
   NYD_ENTER;

   _var_revlookup(&vc, vokey);

   ok = _var_set(&vc, (val == 0x1 ? "" : (char const*)val));
   NYD_LEAVE;
   return ok;
}

FL bool_t
_var_vokclear(char const *vokey)
{
   struct var_carrier vc;
   bool_t err;
   NYD_ENTER;

   _var_revlookup(&vc, vokey);

   err = !_var_clear(&vc);
   NYD_LEAVE;
   return !err;
}

FL char *
_env_look(char const *envkey, bool_t envonly) /* TODO rather dummy yet!! */
{
   char *rv;
   NYD_ENTER;

   if (envonly)
      rv = getenv(envkey); /* TODO rework vars: cache, output a la mimetypes */
   else
      rv = _var_voklook(envkey);
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_SOCKETS
FL char *
_var_xoklook(enum okeys okey, struct url const *urlp, enum okey_xlook_mode oxm)
{
   struct var_carrier vc;
   struct str const *us;
   size_t nlen;
   char *nbuf = NULL /* CC happiness */, *rv;
   NYD_ENTER;

   assert(oxm & (OXM_PLAIN | OXM_H_P | OXM_U_H_P));

   /* For simplicity: allow this case too */
   if (!(oxm & (OXM_H_P | OXM_U_H_P)))
      goto jplain;

   vc.vc_vmap = _var_map + okey;
   vc.vc_name = _var_keydat + _var_map[okey].vm_keyoff;
   vc.vc_okey = okey;

   us = (oxm & OXM_U_H_P) ? &urlp->url_u_h_p : &urlp->url_h_p;
   nlen = strlen(vc.vc_name);
   nbuf = ac_alloc(nlen + 1 + us->l +1);
   memcpy(nbuf, vc.vc_name, nlen);
   nbuf[nlen++] = '-';

   /* One of .url_u_h_p and .url_h_p we test in here */
   memcpy(nbuf + nlen, us->s, us->l +1);
   vc.vc_name = _var_canonify(nbuf);
   vc.vc_hash = MA_NAME2HASH(vc.vc_name);
   if (_var_lookup(&vc))
      goto jvar;

   /* The second */
   if (oxm & OXM_H_P) {
      us = &urlp->url_h_p;
      memcpy(nbuf + nlen, us->s, us->l +1);
      vc.vc_name = _var_canonify(nbuf);
      vc.vc_hash = MA_NAME2HASH(vc.vc_name);
      if (_var_lookup(&vc)) {
jvar:
         rv = vc.vc_var->v_value;
         goto jleave;
      }
   }

jplain:
   rv = (oxm & OXM_PLAIN) ? _var_oklook(okey) : NULL;
jleave:
   if (oxm & (OXM_H_P | OXM_U_H_P))
      ac_free(nbuf);
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_SOCKETS */

FL int
c_varshow(void *v)
{
   struct var_carrier vc;
   char const **argv = v, *val;
   bool_t isenv, isset;
   NYD_ENTER;

   if (*argv == NULL) {
      v = NULL;
      goto jleave;
   }

   for (; *argv != NULL; ++argv) {
      memset(&vc, 0, sizeof vc);
      _var_revlookup(&vc, *argv);
      if (_var_lookup(&vc)) {
         val = vc.vc_var->v_value;
         isenv = FAL0;
      } else
         isenv = ((val = getenv(vc.vc_name)) != NULL);/* TODO should be flag! */
      if (val == NULL)
         val = UNCONST("NULL");
      isset = (vc.vc_var != NULL);

      if (vc.vc_vmap != NULL) {
         ui16_t f = vc.vc_vmap->vm_flags;

         if (f & VM_BINARY)
            printf(_("\"%s\": (%d) binary%s%s: set=%d (ENVIRON=%d)\n"),
               vc.vc_name, vc.vc_okey, (f & VM_RDONLY ? ", read-only" : ""),
               (f & VM_VIRTUAL ? ", virtual" : ""), isset, isenv);
         else
            printf(_("\"%s\": (%d) value%s%s: set=%d (ENVIRON=%d) value<%s>\n"),
               vc.vc_name, vc.vc_okey, (f & VM_RDONLY ? ", read-only" : ""),
               (f & VM_VIRTUAL ? ", virtual" : ""), isset, isenv, val);
      } else
         printf("\"%s\": (assembled): set=%d (ENVIRON=%d) value<%s>\n",
            vc.vc_name, isset, isenv, val);
   }
jleave:
   NYD_LEAVE;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL int
c_set(void *v)
{
   char **ap = v;
   int err;
   NYD_ENTER;

   if (*ap == NULL) {
      _var_list_all();
      err = 0;
   } else
      err = !_var_set_env(ap, FAL0);
   NYD_LEAVE;
   return err;
}

FL int
c_setenv(void *v)
{
   char **ap = v;
   int err;
   NYD_ENTER;

   if (!(err = !(pstate & PS_STARTED)))
      err = !_var_set_env(ap, TRU1);
   NYD_LEAVE;
   return err;
}

FL int
c_unset(void *v)
{
   char **ap = v;
   int err = 0;
   NYD_ENTER;

   while (*ap != NULL)
      err |= !_var_vokclear(*ap++);
   NYD_LEAVE;
   return err;
}

FL int
c_unsetenv(void *v)
{
   int err;
   NYD_ENTER;

   if (!(err = !(pstate & PS_STARTED))) {
      char **ap;

      for (ap = v; *ap != NULL; ++ap) {
         bool_t bad;

         if (!strcmp(*ap, "HOME") || !strcmp(*ap, "USER") || /* TODO generic */
               !strcmp(*ap, "TMPDIR")) { /* TODO approach */
            if (options & OPT_D_V)
               fprintf(stderr, _("Cannot `unsetenv' \"%s\"\n"), *ap);
            err = 1;
            continue;
         }

         bad = !_var_vokclear(*ap);
         if (
#ifdef HAVE_SETENV
               unsetenv(*ap) != 0 ||
#endif
               bad
         )
            err = 1;
      }
   }
   NYD_LEAVE;
   return err;
}

FL int
c_varedit(void *v)
{
   struct var_carrier vc;
   sighandler_type sigint;
   FILE *of, *nf;
   char *val, **argv = v;
   int err = 0;
   NYD_ENTER;

   sigint = safe_signal(SIGINT, SIG_IGN);

   while (*argv != NULL) {
      memset(&vc, 0, sizeof vc);

      _var_revlookup(&vc, *argv++);

      if (vc.vc_vmap != NULL) {
         if (vc.vc_vmap->vm_flags & VM_BINARY) {
            fprintf(stderr, _("`varedit': can't edit binary variable \"%s\"\n"),
               vc.vc_name);
            continue;
         }
         if (vc.vc_vmap->vm_flags & VM_RDONLY) {
            fprintf(stderr,
               _("`varedit': can't edit readonly variable \"%s\"\n"),
               vc.vc_name);
            continue;
         }
      }

      _var_lookup(&vc);

      if ((of = Ftmp(NULL, "vared", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
            NULL) {
         perror(_("`varedit': cannot create temporary file"));
         err = 1;
         break;
      } else if (vc.vc_var != NULL && *(val = vc.vc_var->v_value) != '\0' &&
            sizeof *val != fwrite(val, strlen(val), sizeof *val, of)) {
         perror(_("`varedit' failed to write an old value to temporary file"));
         Fclose(of);
         err = 1;
         continue;
      }

      fflush_rewind(of);
      nf = run_editor(of, (off_t)-1, 'e', FAL0, NULL, NULL, SEND_MBOX, sigint);
      Fclose(of);

      if (nf != NULL) {
         int c;
         char *base;
         off_t l = fsize(nf);

         assert(l >= 0);
         base = smalloc((size_t)l + 1);

         for (l = 0, val = base; (c = getc(nf)) != EOF; ++val)
            if (c == '\n' || c == '\r') {
               *val = ' ';
               ++l;
            } else {
               *val = (char)(uc_i)c;
               l = 0;
            }
         val -= l;
         *val = '\0';

         if (!vok_vset(vc.vc_name, base))
            err = 1;

         free(base);
      }
      Fclose(nf);
   }

   safe_signal(SIGINT, sigint);
   NYD_LEAVE;
   return err;
}

FL int
c_define(void *v)
{
   int rv = 1;
   char **args = v;
   NYD_ENTER;

   if (args[0] == NULL) {
      rv = _ma_list(MA_NONE);
      goto jleave;
   }

   if (args[1] == NULL || args[1][0] != '{' || args[1][1] != '\0' ||
         args[2] != NULL) {
      fprintf(stderr, _("Syntax is: define <name> {"));
      goto jleave;
   }

   rv = !_ma_define(args[0], MA_NONE);
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_undefine(void *v)
{
   int rv = 1;
   char **args = v;
   NYD_ENTER;

   if (*args == NULL) {
      fprintf(stderr, _("`undefine': required arguments are missing\n"));
      goto jleave;
   }
   do
      _ma_undefine(*args, MA_NONE);
   while (*++args != NULL);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_call(void *v)
{
   int rv = 1;
   char **args = v;
   char const *errs, *name;
   struct macro *mp;
   NYD_ENTER;

   if (args[0] == NULL || (args[1] != NULL && args[2] != NULL)) {
      errs = _("Syntax is: call <%s>\n");
      name = "name";
      goto jerr;
   }

   if ((mp = _ma_look(*args, NULL, MA_NONE)) == NULL) {
      errs = _("Undefined macro called: \"%s\"\n");
      name = *args;
      goto jerr;
   }

   rv = _ma_exec(mp, NULL);
jleave:
   NYD_LEAVE;
   return rv;
jerr:
   fprintf(stderr, errs, name);
   goto jleave;
}

FL bool_t
check_folder_hook(bool_t nmail)
{
   size_t len;
   char *var, *cp;
   struct macro *mp;
   struct var **unroller;
   bool_t rv = TRU1;
   NYD_ENTER;

   var = ac_alloc(len = strlen(mailname) + sizeof("folder-hook-") -1  +1);

   /* First try the fully resolved path */
   snprintf(var, len, "folder-hook-%s", mailname);
   if ((cp = vok_vlook(var)) != NULL)
      goto jmac;

   /* If we are under *folder*, try the usual +NAME syntax, too */
   if (displayname[0] == '+') {
      char *x = mailname + len;

      for (; x > mailname; --x)
         if (x[-1] == '/') {
            snprintf(var, len, "folder-hook-+%s", x);
            if ((cp = vok_vlook(var)) != NULL)
               goto jmac;
            break;
         }
   }

   /* Plain *folder-hook* is our last try */
   if ((cp = ok_vlook(folder_hook)) == NULL)
      goto jleave;

jmac:
   if ((mp = _ma_look(cp, NULL, MA_NONE)) == NULL) {
      fprintf(stderr, _("Cannot call *folder-hook* for \"%s\": "
         "macro \"%s\" does not exist.\n"), displayname, cp);
      rv = FAL0;
      goto jleave;
   }

   pstate &= ~PS_IN_HOOK;
   if (nmail) {
      pstate |= PS_HOOK_NEWMAIL;
      unroller = NULL;
   } else {
      pstate |= PS_HOOK;
      unroller = &_folder_hook_localopts;
   }
   rv = (_ma_exec(mp, unroller) == 0);
   pstate &= ~PS_IN_HOOK;

jleave:
   ac_free(var);
   NYD_LEAVE;
   return rv;
}

FL int
c_account(void *v)
{
   char **args = v;
   struct macro *mp;
   int rv = 1, i, oqf, nqf;
   NYD_ENTER;

   if (args[0] == NULL) {
      rv = _ma_list(MA_ACC);
      goto jleave;
   }

   if (args[1] && args[1][0] == '{' && args[1][1] == '\0') {
      if (args[2] != NULL) {
         fprintf(stderr, _("Syntax is: account <name> {\n"));
         goto jleave;
      }
      if (!asccasecmp(args[0], ACCOUNT_NULL)) {
         fprintf(stderr, _("Error: \"%s\" is a reserved name.\n"),
            ACCOUNT_NULL);
         goto jleave;
      }
      rv = !_ma_define(args[0], MA_ACC);
      goto jleave;
   }

   if (pstate & PS_IN_HOOK) {
      fprintf(stderr, _("Cannot change account from within a hook.\n"));
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   mp = NULL;
   if (asccasecmp(args[0], ACCOUNT_NULL) != 0 &&
         (mp = _ma_look(args[0], NULL, MA_ACC)) == NULL) {
      fprintf(stderr, _("Account \"%s\" does not exist.\n"), args[0]);
      goto jleave;
   }

   oqf = savequitflags();

   if (_acc_curr != NULL) {
      if (_acc_curr->ma_localopts != NULL)
         _localopts_unroll(&_acc_curr->ma_localopts);
      if (_acc_curr->ma_flags & MA_DELETED) { /* xxx can be made generic? */
         _ma_freelines(_acc_curr->ma_contents);
         free(_acc_curr->ma_name);
         free(_acc_curr);
      }
   }

   account_name = (mp != NULL) ? mp->ma_name : NULL;
   _acc_curr = mp;

   if (mp != NULL && _ma_exec(mp, &mp->ma_localopts) == CBAD) {
      /* XXX account switch incomplete, unroll? */
      fprintf(stderr, _("Switching to account \"%s\" failed.\n"), args[0]);
      goto jleave;
   }

   if ((pstate & (PS_STARTED | PS_IN_HOOK)) == PS_STARTED) {
      nqf = savequitflags(); /* TODO obsolete (leave -> void -> new box!) */
      restorequitflags(oqf);
      if ((i = setfile("%", 0)) < 0)
         goto jleave;
      check_folder_hook(FAL0);
      if (i > 0 && !ok_blook(emptystart))
         goto jleave;
      announce(ok_blook(bsdcompat) || ok_blook(bsdannounce));
      restorequitflags(nqf);
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_unaccount(void *v)
{
   int rv = 1;
   char **args = v;
   NYD_ENTER;

   if (*args == NULL) {
      fprintf(stderr, _("`unaccount': required arguments are missing\n"));
      goto jleave;
   }
   do
      _ma_undefine(*args, MA_ACC);
   while (*++args != NULL);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_localopts(void *v)
{
   int rv = 1;
   char **c = v;
   NYD_ENTER;

   if (_localopts == NULL) {
      fprintf(stderr,
         _("Cannot use `localopts' but from within a `define' or `account'\n"));
      goto jleave;
   }

   _localopts->s_unroll = (boolify(*c, UIZ_MAX, FAL0) > 0);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL void
temporary_localopts_free(void) /* XXX intermediate hack */
{
   struct n2 {struct n2 *up; struct lostack *lo;} *x;
   NYD_ENTER;

   x = temporary_localopts_store;
   temporary_localopts_store = NULL;
   _localopts = NULL;

   while (x != NULL) {
      struct lostack *losp = x->lo;
      x = x->up;
      if (losp->s_localopts != NULL)
         _localopts_unroll(&losp->s_localopts);
   }
   NYD_LEAVE;
}

FL void
temporary_localopts_folder_hook_unroll(void) /* XXX intermediate hack */
{
   NYD_ENTER;
   if (_folder_hook_localopts != NULL) {
      void *save = _localopts;
      _localopts = NULL;
      _localopts_unroll(&_folder_hook_localopts);
      _localopts = save;
   }
   NYD_LEAVE;
}

/* s-it-mode */
