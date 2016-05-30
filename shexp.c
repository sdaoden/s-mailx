/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Shell "word", file- and other name expansions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
#define n_FILE shexp

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/wait.h>

#include <pwd.h>

#ifdef HAVE_WORDEXP
# include <wordexp.h>
#endif

struct shvar_stack {
   struct shvar_stack *shs_next; /* Outer stack frame */
   char const  *shs_value; /* Remaining value to expand */
   size_t      shs_len;    /* gth of .shs_dat this level */
   char const  *shs_dat;   /* Result data of this level */
   bool_t      *shs_err;   /* Or NULL */
   bool_t      shs_bsesc;  /* Shall backslash escaping be performed */
};

/* Locate the user's mailbox file (where new, unread mail is queued) */
static void       _findmail(char *buf, size_t bufsize, char const *user,
                     bool_t force);

/* Perform shell meta character expansion TODO obsolete (INSECURE!) */
static char *     _globname(char const *name, enum fexp_mode fexpm);

/* Perform shell variable expansion */
static char *  _sh_exp_var(struct shvar_stack *shsp);

static void
_findmail(char *buf, size_t bufsize, char const *user, bool_t force)
{
   char *cp;
   NYD_ENTER;

   if (!force && !strcmp(user, myname) && (cp = ok_vlook(folder)) != NULL) {
      ;
   }

   if (force || (cp = ok_vlook(MAIL)) == NULL)
      snprintf(buf, bufsize, "%s/%s", MAILSPOOL, user); /* TODO */
   else
      n_strscpy(buf, cp, bufsize);
   NYD_LEAVE;
}

static char *
_globname(char const *name, enum fexp_mode fexpm)
{
#ifdef HAVE_WORDEXP
   wordexp_t we;
   char *cp = NULL;
   sigset_t nset;
   int i;
   NYD_ENTER;

   /* Mac OS X Snow Leopard and Linux don't init fields on error, causing
    * SIGSEGV in wordfree(3); so let's just always zero it ourselfs */
   memset(&we, 0, sizeof we);

   /* Some systems (notably Open UNIX 8.0.0) fork a shell for wordexp()
    * and wait, which will fail if our SIGCHLD handler is active */
   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, NULL);
# ifndef WRDE_NOCMD
#  define WRDE_NOCMD 0
# endif
   i = wordexp(name, &we, WRDE_NOCMD);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);

   switch (i) {
   case 0:
      break;
#ifdef WRDE_CMDSUB
   case WRDE_CMDSUB:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Command substitution not allowed\n"), name);
      goto jleave;
#endif
   case WRDE_NOSPACE:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Expansion buffer overflow\n"), name);
      goto jleave;
   case WRDE_BADCHAR:
   case WRDE_SYNTAX:
   default:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("Syntax error in \"%s\"\n"), name);
      goto jleave;
   }

   switch (we.we_wordc) {
   case 1:
      cp = savestr(we.we_wordv[0]);
      break;
   case 0:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": No match\n"), name);
      break;
   default:
      if (fexpm & FEXP_MULTIOK) {
         size_t j, l;

         for (l = 0, j = 0; j < we.we_wordc; ++j)
            l += strlen(we.we_wordv[j]) + 1;
         ++l;
         cp = salloc(l);
         for (l = 0, j = 0; j < we.we_wordc; ++j) {
            size_t x = strlen(we.we_wordv[j]);
            memcpy(cp + l, we.we_wordv[j], x);
            l += x;
            cp[l++] = ' ';
         }
         cp[l] = '\0';
      } else if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Ambiguous\n"), name);
      break;
   }
jleave:
   wordfree(&we);
   NYD_LEAVE;
   return cp;

#else /* HAVE_WORDEXP */
   struct stat sbuf;
   char xname[PATH_MAX +1], cmdbuf[PATH_MAX +1], /* also used for files */
      cp = NULL;
   int pivec[2], pid, l, waits;
   NYD_ENTER;

   if (pipe(pivec) < 0) {
      n_perr(_("pipe"), 0);
      goto jleave;
   }
   snprintf(cmdbuf, sizeof cmdbuf, "echo %s", name);
   pid = start_command(ok_vlook(SHELL), NULL, COMMAND_FD_NULL, pivec[1],
         "-c", cmdbuf, NULL, NULL);
   if (pid < 0) {
      close(pivec[0]);
      close(pivec[1]);
      goto jleave;
   }
   close(pivec[1]);

jagain:
   l = read(pivec[0], xname, sizeof xname);
   if (l < 0) {
      if (errno == EINTR)
         goto jagain;
      n_perr(_("read"), 0);
      close(pivec[0]);
      goto jleave;
   }
   close(pivec[0]);
   if (!wait_child(pid, &waits) && WTERMSIG(waits) != SIGPIPE) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Expansion failed\n"), name);
      goto jleave;
   }
   if (l == 0) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": No match\n"), name);
      goto jleave;
   }
   if (l == sizeof xname) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Expansion buffer overflow\n"), name);
      goto jleave;
   }
   xname[l] = 0;
   for (cp = xname + l - 1; *cp == '\n' && cp > xname; --cp)
      ;
   cp[1] = '\0';
   if (!(fexpm & FEXP_MULTIOK) && strchr(xname, ' ') != NULL &&
         stat(xname, &sbuf) < 0) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Ambiguous\n"), name);
      cp = NULL;
      goto jleave;
   }
   cp = savestr(xname);
jleave:
   NYD_LEAVE;
   return cp;
#endif /* !HAVE_WORDEXP */
}

static char *
_sh_exp_var(struct shvar_stack *shsp)
{
   struct shvar_stack next, *np, *tmp;
   char const *vp;
   char lc, c, *cp, *rv;
   size_t i;
   NYD2_ENTER;

   if (*(vp = shsp->shs_value) != '$') {
      bool_t bsesc = shsp->shs_bsesc;
      union {bool_t hadbs; char c;} u = {FAL0};

      shsp->shs_dat = vp;
      for (lc = '\0', i = 0; ((c = *vp) != '\0'); ++i, ++vp) {
         if (c == '$' && lc != '\\')
            break;
         if (!bsesc)
            continue;
         lc = (lc == '\\') ? (u.hadbs = TRU1, '\0') : c;
      }
      shsp->shs_len = i;

      if (u.hadbs) {
         shsp->shs_dat = cp = savestrbuf(shsp->shs_dat, i);

         for (lc = '\0', rv = cp; (u.c = *cp++) != '\0';) {
            if (u.c != '\\' || lc == '\\')
               *rv++ = u.c;
            lc = (lc == '\\') ? '\0' : u.c;
         }
         *rv = '\0';

         shsp->shs_len = PTR2SIZE(rv - shsp->shs_dat);
      }
   } else {
      if ((lc = (*++vp == '{')))
         ++vp;

      /* POSIX says
       *   Environment variable names used by the utilities in the Shell and
       *   Utilities volume of POSIX.1-2008 consist solely of uppercase
       *   letters, digits, and the <underscore> ('_') from the characters
       *   defined in Portable Character Set and do not begin with a digit.
       *   Other characters may be permitted by an implementation;
       *   applications shall tolerate the presence of such names.
       * We do support the hyphen "-" because it is common for mailx. */
      shsp->shs_dat = vp;
      for (i = 0; (c = *vp) != '\0'; ++i, ++vp)
         if (!alnumchar(c) && c != '_' && c != '-')
            break;

      if (lc) {
         if (c != '}') {
            n_err(_("Variable name misses closing \"}\": \"%s\"\n"),
               shsp->shs_value);
            shsp->shs_len = strlen(shsp->shs_value);
            shsp->shs_dat = shsp->shs_value;
            if (shsp->shs_err != NULL)
               *shsp->shs_err = TRU1;
            goto junroll;
         }
         c = *++vp;
      }

      shsp->shs_len = i;
      /* Check getenv(3) shall no internal variable exist! */
      if ((rv = vok_vlook(cp = savestrbuf(shsp->shs_dat, i))) != NULL ||
            (rv = getenv(cp)) != NULL)
         shsp->shs_len = strlen(shsp->shs_dat = rv);
      else
         shsp->shs_len = 0, shsp->shs_dat = UNCONST("");
   }
   if (c != '\0')
      goto jrecurse;

   /* That level made the great and completed encoding.  Build result */
junroll:
   for (i = 0, np = shsp, shsp = NULL; np != NULL;) {
      i += np->shs_len;
      tmp = np->shs_next;
      np->shs_next = shsp;
      shsp = np;
      np = tmp;
   }

   cp = rv = salloc(i +1);
   while (shsp != NULL) {
      np = shsp;
      shsp = shsp->shs_next;
      memcpy(cp, np->shs_dat, np->shs_len);
      cp += np->shs_len;
   }
   *cp = '\0';

jleave:
   NYD2_LEAVE;
   return rv;
jrecurse:
   memset(&next, 0, sizeof next);
   next.shs_next = shsp;
   next.shs_value = vp;
   next.shs_err = shsp->shs_err;
   next.shs_bsesc = shsp->shs_bsesc;
   rv = _sh_exp_var(&next);
   goto jleave;
}

FL char *
fexpand(char const *name, enum fexp_mode fexpm)
{
   char cbuf[PATH_MAX +1];
   char const *res;
   struct str s;
   bool_t dyn;
   NYD_ENTER;

   /* The order of evaluation is "%" and "#" expand into constants.
    * "&" can expand into "+".  "+" can expand into shell meta characters.
    * Shell meta characters expand into constants.
    * This way, we make no recursive expansion */
   if ((fexpm & FEXP_NSHORTCUT) || (res = shortcut_expand(name)) == NULL)
      res = UNCONST(name);

   if (fexpm & FEXP_SHELL) {
      dyn = FAL0;
      goto jshell;
   }
jnext:
   dyn = FAL0;
   switch (*res) {
   case '%':
      if (res[1] == ':' && res[2] != '\0') {
         res = &res[2];
         goto jnext;
      }
      _findmail(cbuf, sizeof cbuf, (res[1] != '\0' ? res + 1 : myname),
         (res[1] != '\0'));
      res = cbuf;
      goto jislocal;
   case '#':
      if (res[1] != '\0')
         break;
      if (prevfile[0] == '\0') {
         n_err(_("No previous file\n"));
         res = NULL;
         goto jleave;
      }
      res = prevfile;
      goto jislocal;
   case '&':
      if (res[1] == '\0')
         res = ok_vlook(MBOX);
      break;
   }

   if (res[0] == '+' && getfold(cbuf, sizeof cbuf)) {
      size_t i = strlen(cbuf);

      res = str_concat_csvl(&s, cbuf,
            ((i > 0 && cbuf[i - 1] == '/') ? "" : "/"), res + 1, NULL)->s;
      dyn = TRU1;

      if (res[0] == '%' && res[1] == ':') {
         res += 2;
         goto jnext;
      }
   }

   /* Catch the most common shell meta character */
jshell:
   if (res[0] == '~') {
      res = n_shell_expand_tilde(res, NULL);
      dyn = TRU1;
   }
   if (anyof(res, "|&;<>{}()[]*?$`'\"\\"))
      switch (which_protocol(res)) {
      case PROTO_FILE:
      case PROTO_MAILDIR:
         res = (fexpm & FEXP_NSHELL) ? n_shell_expand_var(res, TRU1, NULL)
               : _globname(res, fexpm);
         dyn = TRU1;
         goto jleave;
      default:
         break;
      }
jislocal:
   if (fexpm & FEXP_LOCAL)
      switch (which_protocol(res)) {
      case PROTO_FILE:
      case PROTO_MAILDIR:
         break;
      default:
         n_err(_("Not a local file or directory: \"%s\"\n"), name);
         res = NULL;
         break;
      }
jleave:
   if (res && !dyn)
      res = savestr(res);
   NYD_LEAVE;
   return UNCONST(res);
}

FL char *
fexpand_nshell_quote(char const *name)
{
   size_t i, j;
   char *rv, c;
   NYD_ENTER;

   for (i = j = 0; (c = name[i]) != '\0'; ++i)
      if (c == '\\')
         ++j;

   if (j == 0)
      rv = savestrbuf(name, i);
   else {
      rv = salloc(i + j +1);
      for (i = j = 0; (c = name[i]) != '\0'; ++i) {
         rv[j++] = c;
         if (c == '\\')
            rv[j++] = c;
      }
      rv[j] = '\0';
   }
   NYD_LEAVE;
   return rv;
}

FL char *
n_shell_expand_tilde(char const *s, bool_t *err_or_null)
{
   struct passwd *pwp;
   size_t nl, rl;
   char const *rp, *np;
   char *rv;
   bool_t err;
   NYD2_ENTER;

   err = FAL0;

   if (s[0] != '~')
      goto jasis;

   if (*(rp = s + 1) == '/' || *rp == '\0')
      np = homedir;
   else {
      if ((rp = strchr(s + 1, '/')) == NULL)
         rp = (np = UNCONST(s)) + 1;
      else {
         nl = PTR2SIZE(rp - s);
         np = savestrbuf(s, nl);
      }

      if ((pwp = getpwnam(np)) == NULL) {
         err = TRU1;
         goto jasis;
      }
      np = pwp->pw_name;
   }

   nl = strlen(np);
   rl = strlen(rp);
   rv = salloc(nl + 1 + rl +1);
   memcpy(rv, np, nl);
   if (rl > 0) {
      memcpy(rv + nl, rp, rl);
      nl += rl;
   }
   rv[nl] = '\0';
   goto jleave;

jasis:
   rv = savestr(s);
jleave:
   if (err_or_null != NULL)
      *err_or_null = err;
   NYD2_LEAVE;
   return rv;
}

FL char *
n_shell_expand_var(char const *s, bool_t bsescape, bool_t *err_or_null)
{
   struct shvar_stack top;
   char *rv;
   NYD2_ENTER;

   memset(&top, 0, sizeof top);

   top.shs_value = s;
   if ((top.shs_err = err_or_null) != NULL)
      *err_or_null = FAL0;
   top.shs_bsesc = bsescape;
   rv = _sh_exp_var(&top);
   NYD2_LEAVE;
   return rv;
}

FL int
n_shell_expand_escape(char const **s, bool_t use_nail_extensions)
{
   char const *xs;
   int c, n;
   NYD2_ENTER;

   xs = *s;

   if ((c = *xs & 0xFF) == '\0')
      goto jleave;
   ++xs;
   if (c != '\\')
      goto jleave;

   switch ((c = *xs & 0xFF)) {
   case 'a':   c = '\a';         break;
   case 'b':   c = '\b';         break;
   case 'c':   c = PROMPT_STOP;  break;
   case 'f':   c = '\f';         break;
   case 'n':   c = '\n';         break;
   case 'r':   c = '\r';         break;
   case 't':   c = '\t';         break;
   case 'v':   c = '\v';         break;

   /* Hexadecimal TODO uses ASCII */
   case 'X':
   case 'x': {
      static ui8_t const hexatoi[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
#undef a_HEX
#define a_HEX(n) \
   hexatoi[(ui8_t)((n) - ((n) <= '9' ? 48 : ((n) <= 'F' ? 55 : 87)))]

      c = 0;
      ++xs;
      if(hexchar(*xs))
         c = a_HEX(*xs);
      else{
         --xs;
         if(options & OPT_D_V)
            n_err(_("Invalid \"\\xNUMBER\" notation in \"%s\"\n"), xs - 1);
         c = '\\';
         goto jleave;
      }
      ++xs;
      if(hexchar(*xs)){
         c <<= 4;
         c += a_HEX(*xs);
         ++xs;
      }
      goto jleave;
   }
#undef a_HEX

   /* octal, with optional 0 prefix */
   case '0':
      ++xs;
      if(0){
   default:
         if(*xs == '\0'){
            c = '\\';
            break;
         }
      }
      for (c = 0, n = 3; n-- > 0 && octalchar(*xs); ++xs) {
         c <<= 3;
         c |= *xs - '0';
      }
      goto jleave;

   /* S-nail extension for nice (get)prompt(()) support */
   case '&':
   case '?':
   case '$':
   case '@':
      if (use_nail_extensions) {
         switch (c) {
         case '&':   c = ok_blook(bsdcompat)       ? '&' : '?';   break;
         case '?':   c = (pstate & PS_EVAL_ERROR)  ? '1' : '0';   break;
         case '$':   c = PROMPT_DOLLAR;                           break;
         case '@':   c = PROMPT_AT;                               break;
         }
         break;
      }

      /* FALLTHRU */
   case '\0':
      /* A sole <backslash> at EOS is treated as-is! */
      c = '\\';
      /* FALLTHRU */
   case '\\':
      break;
   }

   ++xs;
jleave:
   *s = xs;
   NYD2_LEAVE;
   return c;
}

/* s-it-mode */
