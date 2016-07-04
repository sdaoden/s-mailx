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

/* POSIX says
 *   Environment variable names used by the utilities in the Shell and
 *   Utilities volume of POSIX.1-2008 consist solely of uppercase
 *   letters, digits, and the <underscore> ('_') from the characters
 *   defined in Portable Character Set and do not begin with a digit.
 *   Other characters may be permitted by an implementation;
 *   applications shall tolerate the presence of such names.
 * We do support the hyphen "-" because it is common for mailx. */
#define a_SHEXP_ISVARC(C) (alnumchar(C) || (C) == '_' || (C) == '-')

struct shvar_stack {
   struct shvar_stack *shs_next; /* Outer stack frame */
   char const  *shs_value; /* Remaining value to expand */
   size_t      shs_len;    /* gth of .shs_dat this level */
   char const  *shs_dat;   /* Result data of this level */
   bool_t      *shs_err;   /* Or NULL */
   bool_t      shs_bsesc;  /* Shall backslash escaping be performed */
};

/* Locate the user's mailbox file (where new, unread mail is queued) */
static char * _findmail(char const *user, bool_t force);

/* Perform shell meta character expansion TODO obsolete (INSECURE!) */
static char *     _globname(char const *name, enum fexp_mode fexpm);

/* Perform shell variable expansion */
static char *  _sh_exp_var(struct shvar_stack *shsp);

static char *
_findmail(char const *user, bool_t force)
{
   char *rv;
   char const *cp;
   NYD_ENTER;

   if (force || (cp = ok_vlook(MAIL)) == NULL) {
      size_t ul = strlen(user), i = sizeof(MAILSPOOL) -1 + 1 + ul +1;

      rv = salloc(i);
      memcpy(rv, MAILSPOOL, i = sizeof(MAILSPOOL));
      rv[i] = '/';
      memcpy(&rv[++i], user, ul +1);
   } else if ((rv = fexpand(cp, FEXP_NSHELL)) == NULL)
      rv = savestr(cp);
   NYD_LEAVE;
   return rv;
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
   UNUSED(fexpm);

   if(options & OPT_D_V)
      n_err(_("wordexp(3) not available, cannot perform expansion\n"));
   return savestr(name);
#endif
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

      shsp->shs_dat = vp;
      for (i = 0; (c = *vp) != '\0'; ++i, ++vp)
         if (!a_SHEXP_ISVARC(c))
            break;

      if (lc) {
         if (c != '}') {
            n_err(_("Variable name misses closing \"}\": %s\n"),
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
   struct str s;
   char const *cp, *res;
   bool_t dyn;
   NYD_ENTER;

   /* The order of evaluation is "%" and "#" expand into constants.
    * "&" can expand into "+".  "+" can expand into shell meta characters.
    * Shell meta characters expand into constants.
    * This way, we make no recursive expansion */
   if ((fexpm & FEXP_NSHORTCUT) || (res = shortcut_expand(name)) == NULL)
      res = UNCONST(name);

jnext:
   dyn = FAL0;
   switch (*res) {
   case '%':
      if (res[1] == ':' && res[2] != '\0') {
         res = &res[2];
         goto jnext;
      }
      res = _findmail((res[1] != '\0' ? res + 1 : myname), (res[1] != '\0'));
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

   /* POSIX: if *folder* unset or null, "+" shall be retained */
   if (*res == '+' && *(cp = folder_query()) != '\0') {
      size_t i = strlen(cp);

      res = str_concat_csvl(&s, cp,
            ((i == 0 || cp[i -1] == '/') ? "" : "/"), res + 1, NULL)->s;
      dyn = TRU1;

      /* TODO *folder* can't start with %[:], can it!?! */
      if (res[0] == '%' && res[1] == ':') {
         res += 2;
         goto jnext;
      }
   }

   /* Catch the most common shell meta character */
   if (res[0] == '~') {
      res = n_shell_expand_tilde(res, NULL);
      dyn = TRU1;
   }

   if ((fexpm & (FEXP_NSHELL | FEXP_NVAR)) != FEXP_NVAR &&
         ((fexpm & FEXP_NSHELL) ? (strchr(res, '$') != NULL)
          : anyof(res, "|&;<>{}()[]*?$`'\"\\"))) {
      bool_t doexp;

      if(fexpm & FEXP_NOPROTO)
         doexp = TRU1;
      else switch(which_protocol(res)){
      case PROTO_FILE:
      case PROTO_MAILDIR:
         doexp = TRU1;
         break;
      default:
         doexp = FAL0;
         break;
      }

      if(doexp){
         res = (fexpm & FEXP_NSHELL) ? n_shell_expand_var(res, TRU1, NULL)
               : _globname(res, fexpm);
         dyn = TRU1;
      }
   }

jislocal:
   if (fexpm & FEXP_LOCAL)
      switch (which_protocol(res)) {
      case PROTO_FILE:
      case PROTO_MAILDIR:
         break;
      default:
         n_err(_("Not a local file or directory: %s\n"),
            n_shell_quote_cp(name));
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
      np = ok_vlook(HOME);
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
n_shell_expand_escape(char const **s, bool_t use_nail_extensions)/* TODO DROP!*/
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

   /* ESCape */
   case 'E':
   case 'e':
      c = '\033';
      break;

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

FL enum n_shexp_state
n_shell_parse_token(struct n_string *store, struct str *input,
      enum n_shexp_parse_flags flags){
#if defined HAVE_NATCH_CHAR || defined HAVE_ICONV
   char utf[8];
#endif
   char c2, c, quotec;
   bool_t skipq, surplus;
   enum n_shexp_state rv;
   size_t i, il;
   char const *ib_save, *ib;
   NYD2_ENTER;
   UNINIT(c, '\0');

   assert((flags & n_SHEXP_PARSE_DRYRUN) || store != NULL);
   assert(input != NULL);
   assert(input->l == 0 || input->s != NULL);
   assert(!(flags & n_SHEXP_PARSE_LOG) || !(flags & n_SHEXP_PARSE_LOG_D_V));
   assert(!(flags & n_SHEXP_PARSE_IFS_ADD_COMMA) ||
      !(flags & n_SHEXP_PARSE_IFS_IS_COMMA));

   if((flags & n_SHEXP_PARSE_LOG_D_V) && (options & OPT_D_V))
      flags |= n_SHEXP_PARSE_LOG;

   if((flags & n_SHEXP_PARSE_TRUNC) && store != NULL)
      store = n_string_trunc(store, 0);

   ib = input->s;
   if((il = input->l) == UIZ_MAX)
      il = strlen(ib);

jrestart_empty:
   if(flags & n_SHEXP_PARSE_TRIMSPACE){
      for(; il > 0; ++ib, --il)
         if(!blankspacechar(*ib))
            break;
   }
   input->s = UNCONST(ib);
   input->l = il;

   if(il == 0){
      rv = n_SHEXP_STATE_STOP;
      goto jleave;
   }

   if(store != NULL)
      store = n_string_reserve(store, MIN(il, 32)); /* XXX */

   for(rv = n_SHEXP_STATE_NONE, skipq = surplus = FAL0, quotec = '\0'; il > 0;){
      --il, c = *ib++;

      /* If no quote-mode active.. */
      if(quotec == '\0'){
         if(c == '"' || c == '\''){
            quotec = c;
            surplus = (c == '"');
            continue;
         }else if(c == '$'){
            if(il > 0){
               if(*ib == '\''){
                  --il, ++ib;
                  quotec = '\'';
                  surplus = TRU1;
                  continue;
               }else
                  goto J_var_expand;
            }
         }else if(c == '\\'){
            /* Outside of quotes this just escapes any next character, but a sole
             * <backslash> at EOS is left unchanged */
             if(il > 0)
               --il, c = *ib++;
         }else if(c == '#'){
            rv |= n_SHEXP_STATE_STOP;
            goto jleave;
         }else if((flags &
                (n_SHEXP_PARSE_IFS_ADD_COMMA | n_SHEXP_PARSE_IFS_IS_COMMA)) &&
               c == ',')
            break;
         else if(!(flags & n_SHEXP_PARSE_IFS_IS_COMMA) && blankchar(c)){
            ++il, --ib;
            break;
         }
      }else{
         /* Quote-mode */
         if(c == quotec){
            skipq = surplus = FAL0;
            quotec = '\0';
            /* Users may need to recognize the presence of empty quotes */
            rv |= n_SHEXP_STATE_OUTPUT;
            continue;
         }else if(c == '\\' && surplus){
            ib_save = ib - 1;

            /* A sole <backslash> at EOS is treated as-is! */
            if(il == 0)
               break;
            else if((c2 = *ib) == quotec){
               --il, ++ib;
               c = quotec;
            }else if(quotec == '"'){
               /* Double quotes:
                *    The <backslash> shall retain its special meaning as an
                *    escape character (see Section 2.2.1) only when followed
                *    by one of the following characters when considered
                *    special: $ ` " \ <newline> */
               switch(c2){
               case '$':
               case '`':
               /* case '"': already handled via c2 == quotec */
               case '\\':
                  --il, ++ib;
                  c = c2;
                  /* FALLTHRU */
               default:
                  break;
               }
            }else{
               /* Dollar-single-quote */
               --il, ++ib;
               switch(c2){
               case '"':
               /* case '\'': already handled via c2 == quotec */
               case '\\':
                  c = c2;
                  break;

               case 'b': c = '\b'; break;
               case 'f': c = '\f'; break;
               case 'n': c = '\n'; break;
               case 'r': c = '\r'; break;
               case 't': c = '\t'; break;
               case 'v': c = '\v'; break;

               case 'E':
               case 'e': c = '\033'; break;

               /* Control character */
               case 'c':
                  if(il == 0)
                     goto j_dollar_ungetc;
                  --il, c2 = *ib++;
                  if(skipq)
                     continue;
                  c = upperconv(c2) ^ 0x40;
                  if((ui8_t)c > 0x1F && c != 0x7F){ /* ASCII C0: 0..1F, 7F */
                     if(flags & n_SHEXP_PARSE_LOG)
                        n_err(_("Invalid \"\\c\" notation: %.*s\n"),
                           (int)input->l, input->s);
                     rv |= n_SHEXP_STATE_ERR_CONTROL;
                  }
                  /* As an implementation-defined extension, support \c@
                   * EQ printf(1) alike \c */
                  if(c == '\0'){
                     rv |= n_SHEXP_STATE_STOP;
                     goto jleave;
                  }
                  break;

               /* Octal sequence: 1 to 3 octal bytes */
               case '0':
                  /* As an extension (dependent on where you look, echo(1), or
                   * awk(1)/tr(1) etc.), allow leading "0" octal indicator */
                  if(il > 0 && (c = *ib) >= '0' && c <= '7'){
                     c2 = c;
                     --il, ++ib;
                  }
                  /* FALLTHRU */
               case '1': case '2': case '3':
               case '4': case '5': case '6': case '7':
                  c2 -= '0';
                  if(il > 0 && (c = *ib) >= '0' && c <= '7'){
                     c2 = (c2 << 3) | (c - '0');
                     --il, ++ib;
                  }
                  if(il > 0 && (c = *ib) >= '0' && c <= '7'){
                     if((ui8_t)c2 > 0x1F){
                        if(flags & n_SHEXP_PARSE_LOG)
                           n_err(_("\"\\0\" argument exceeds a byte: "
                              "%.*s\n"), (int)input->l, input->s);
                        rv |= n_SHEXP_STATE_ERR_NUMBER;
                        --il, ++ib;
                        /* Write unchanged */
je_ib_save:
                        rv |= n_SHEXP_STATE_OUTPUT;
                        if(!(flags & n_SHEXP_PARSE_DRYRUN))
                           store = n_string_push_buf(store, ib_save,
                                 PTR2SIZE(ib - ib_save));
                        continue;
                     }
                     c2 = (c2 << 3) | (c -= '0');
                     --il, ++ib;
                  }
                  if((c = c2) == '\0')
                     skipq = TRU1;
                  if(skipq)
                     continue;
                  break;

               /* ISO 10646 / Unicode sequence, 8 or 4 hexadecimal bytes */
               case 'U':
                  i = 8;
                  if(0){
                  /* FALLTHRU */
               case 'u':
                     i = 4;
                  }
                  if(il == 0)
                     goto j_dollar_ungetc;
                  if(0){
                     /* FALLTHRU */

               /* Hexadecimal sequence, 1 or 2 hexadecimal bytes */
               case 'X':
               case 'x':
                     if(il == 0)
                        goto j_dollar_ungetc;
                     i = 2;
                  }
                  /* C99 */{
                     static ui8_t const hexatoi[] = { /* XXX uses ASCII */
                        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
                     };
                     size_t no, j;

                     i = MIN(il, i);
                     for(no = j = 0; i-- > 0; --il, ++ib, ++j){
                        c = *ib;
                        if(hexchar(c)){
                           no <<= 4;
                           no += hexatoi[(ui8_t)((c) - ((c) <= '9' ? 48
                                 : ((c) <= 'F' ? 55 : 87)))];
                        }else if(j == 0){
                           if(skipq)
                              break;
                           c2 = (c2 == 'U' || c2 == 'u') ? 'u' : 'x';
                           if(flags & n_SHEXP_PARSE_LOG)
                              n_err(_("Invalid \"\\%c\" notation: %.*s\n"),
                                 c2, (int)input->l, input->s);
                           rv |= n_SHEXP_STATE_ERR_NUMBER;
                           goto je_ib_save;
                        }else
                           break;
                     }

                     /* Unicode massage */
                     if((c2 != 'U' && c2 != 'u') || n_uasciichar(no)){
                        if((c = (char)no) == '\0')
                           skipq = TRU1;
                     }else if(no == 0)
                        skipq = TRU1;
                     else if(!skipq){
                        if(!(flags & n_SHEXP_PARSE_DRYRUN))
                           store = n_string_reserve(store, MAX(j, 4));

                        c2 = FAL0;
                        if(no > 0x10FFFF){ /* XXX magic; CText */
                           if(flags & n_SHEXP_PARSE_LOG)
                              n_err(_("\"\\U\" argument exceeds 0x10FFFF: "
                                 "%.*s\n"), (int)input->l, input->s);
                           rv |= n_SHEXP_STATE_ERR_NUMBER;
                           /* But normalize the output anyway */
                           goto Je_uni_norm;
                        }

#if defined HAVE_NATCH_CHAR || defined HAVE_ICONV
                        j = n_utf32_to_utf8(no, utf);
#endif
#ifdef HAVE_NATCH_CHAR
                        if(options & OPT_UNICODE){
                           rv |= n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_UNICODE;
                           if(!(flags & n_SHEXP_PARSE_DRYRUN))
                              store = n_string_push_buf(store, utf, j);
                           continue;
                        }
#endif
#ifdef HAVE_ICONV
                        /* C99 */{
                           char *icp;

                           icp = n_iconv_onetime_cp(NULL, NULL, utf, FAL0);
                           if(icp != NULL){
                              rv |= n_SHEXP_STATE_OUTPUT;
                              if(!(flags & n_SHEXP_PARSE_DRYRUN))
                                 store = n_string_push_cp(store, icp);
                              continue;
                           }
                        }
#endif
                        if(!(flags & n_SHEXP_PARSE_DRYRUN)) Je_uni_norm:{
                           char itoa[32];

                           rv |= n_SHEXP_STATE_OUTPUT |
                                 n_SHEXP_STATE_ERR_UNICODE;
                           i = snprintf(itoa, sizeof itoa, "\\%c%0*X",
                                 (no > 0xFFFFu ? 'U' : 'u'),
                                 (int)(no > 0xFFFFu ? 8 : 4), (ui32_t)no);
                           store = n_string_push_buf(store, itoa, i);
                        }
                        continue;
                     }
                     if(skipq)
                        continue;
                  }
                  break;

               /* Extension: \$ can be used to expand a variable.
                * Bug|ad effect: if conversion fails, not written "as-is" */
               case '$':
                  if(il == 0)
                     goto j_dollar_ungetc;
                  goto J_var_expand;

               default:
j_dollar_ungetc:
                  /* Follow bash behaviour, print sequence unchanged */
                  ++il, --ib;
                  break;
               }
            }
         }else if(c == '$' && quotec == '"' && il > 0) J_var_expand:{
            bool_t brace;

            if(!(brace = (*ib == '{')) || il > 1){
               char const *cp, *vp;

               ib_save = ib - 1;
               il -= brace;
               vp = (ib += brace);

               for(i = 0; il > 0 && (c = *ib, a_SHEXP_ISVARC(c)); ++i)
                  --il, ++ib;

               if(brace){
                  if(il == 0 || *ib != '}'){
                     if(skipq){
                        assert(surplus && quotec == '\'');
                        continue;
                     }
                     if(flags & n_SHEXP_PARSE_LOG)
                        n_err(_("Closing brace missing for ${VAR}: %.*s\n"),
                           (int)input->l, input->s);
                     rv |= n_SHEXP_STATE_ERR_QUOTEOPEN |
                           n_SHEXP_STATE_ERR_BRACE;
                     goto je_ib_save;
                  }
                  --il, ++ib;
               }

               if(skipq)
                  continue;

               if(i == 0){
                  if(brace){
                     if(flags & n_SHEXP_PARSE_LOG)
                        n_err(_("Bad substitution (${}): %.*s\n"),
                           (int)input->l, input->s);
                     rv |= n_SHEXP_STATE_ERR_BADSUB;
                     goto je_ib_save;
                  }
                  c = '$';
               }else if(flags & n_SHEXP_PARSE_DRYRUN)
                  continue;
               else{
                  vp = savestrbuf(vp, i);
                  /* Check getenv(3) shall no internal variable exist! */
                  if((cp = vok_vlook(vp)) != NULL || (cp = getenv(vp)) != NULL){
                     rv |= n_SHEXP_STATE_OUTPUT;
                     store = n_string_push_cp(store, cp);
                     for(; (c = *cp) != '\0'; ++cp)
                        if(cntrlchar(c)){
                           rv |= n_SHEXP_STATE_CONTROL;
                           break;
                        }
                  }
                  continue;
               }
            }
         }else if(c == '`' && quotec == '"' && il > 0){ /* TODO shell command */
            continue;
         }
      }

      if(!skipq){
         rv |= n_SHEXP_STATE_OUTPUT;
         if(cntrlchar(c))
            rv |= n_SHEXP_STATE_CONTROL;
         if(!(flags & n_SHEXP_PARSE_DRYRUN))
            store = n_string_push_c(store, c);
      }
   }

   if(quotec != '\0'){
      if(flags & n_SHEXP_PARSE_LOG)
         n_err(_("Missing closing quote in: %.*s\n"),
            (int)input->l, input->s);
      rv |= n_SHEXP_STATE_ERR_QUOTEOPEN;
   }

jleave:
   if((flags & n_SHEXP_PARSE_DRYRUN) && store != NULL){
      store = n_string_push_buf(store, input->s, PTR2SIZE(ib - input->s));
      rv |= n_SHEXP_STATE_OUTPUT;
   }

   if(flags & n_SHEXP_PARSE_TRIMSPACE){
      for(; il > 0; ++ib, --il)
         if(!blankchar(*ib))
            break;
   }
   input->l = il;
   input->s = UNCONST(ib);

   if(!(rv & n_SHEXP_STATE_STOP)){
      if(il > 0 && !(rv & n_SHEXP_STATE_OUTPUT) &&
            (flags & n_SHEXP_PARSE_IGNORE_EMPTY))
         goto jrestart_empty;
      if(!(rv & n_SHEXP_STATE_OUTPUT) && il == 0)
         rv |= n_SHEXP_STATE_STOP;
   }
   assert((rv & n_SHEXP_STATE_OUTPUT) || !(rv & n_SHEXP_STATE_UNICODE));
   assert((rv & n_SHEXP_STATE_OUTPUT) || !(rv & n_SHEXP_STATE_CONTROL));
   NYD2_LEAVE;
   return rv;
}

FL struct n_string *
n_shell_quote(struct n_string *store, struct str const *input){
   /* TODO In v15 we need to save (possibly normalize) away user input,
    * TODO so that the ORIGINAL (normalized) input can be used directly.
    * Because we're the last, stay primitive */
   bool_t qflag;
   size_t j, i, il;
   char const *ib;
   NYD2_ENTER;

   assert(store != NULL);
   assert(input != NULL);
   assert(input->l == 0 || input->s != NULL);

   ib = input->s;
   if((il = input->l) == UIZ_MAX)
      il = strlen(ib);

   /* Calculate necessary buffer space */
   if(il == 0)
      qflag = TRU1, j = 0;
   else for(qflag = FAL0, j = sizeof("''") -1, i = 0; i < il; ++i){
      char c = ib[i];

      if(c == '\'' || !asciichar(c) || cntrlchar(c)){
         qflag |= TRUM1;
         j += sizeof("\\0377") -1;
      }else if(c == '\\' || c == '$' || blankchar(c)){
         qflag |= TRU1;
         j += sizeof("\\ ") -1;
      }else
         ++j;
   }
   store = n_string_reserve(store, j + 3);

   if(!qflag)
      store = n_string_push_buf(store, ib, il);
   else if(qflag == TRU1){
      store = n_string_push_c(store, '\'');
      store = n_string_push_buf(store, ib, il);
      store = n_string_push_c(store, '\'');
   }else{
      store = n_string_push_buf(store, "$'", sizeof("$'") -1);

      for(qflag = FAL0, j = 0, i = 0; i < il; ++i){
         char c = ib[i];

         if(c == '\'' || !asciichar(c) || cntrlchar(c)){
            store = n_string_push_c(store, '\\');
            if(cntrlchar(c)){
               char c2 = c;

               switch(c){
               case 0x07: c = 'a'; break;
               case 0x08: c = 'b'; break;
               case 0x09: c = 't'; break;
               case 0x0A: c = 'n'; break;
               case 0x0B: c = 'v'; break;
               case 0x0C: c = 'f'; break;
               case 0x0D: c = 'r'; break;
               default: break;
               }
               if(c == c2){
                  store = n_string_push_c(store, 'c');
                  c ^= 0x40;
               }
               store = n_string_push_c(store, c);
               continue;
            }else if(c != '\''){
#ifdef HAVE_NATCH_CHAR
               if(options & OPT_UNICODE){
                  ui32_t u;
                  char const *ib2 = &ib[i];
                  size_t il2 = il - i, il3 = il2;

                  if((u = n_utf8_to_utf32(&ib2, &il2)) != UI32_MAX){
                     char itoa[32];

                     il2 = -((siz_t)il2 - (siz_t)il3);
                     i += --il2;
                     il3 = snprintf(itoa, sizeof itoa, "%c%0*X",
                           (u > 0xFFFFu ? 'U' : 'u'),
                           (int)(u > 0xFFFFu ? 8 : 4), u);
                     store = n_string_push_buf(store, itoa, il3);
                     goto juseq;
                  }
               }
#endif
               store = n_string_push_buf(store, "xFF", sizeof("xFF") -1);
               n_c_to_hex_base16(&store->s_dat[store->s_len - 2], c);
#ifdef HAVE_NATCH_CHAR
juseq:
#endif
               if(i + 1 < il && hexchar(ib[i + 1]))
                  store = n_string_push_buf(store, "'$'", sizeof("'$'") -1);
               continue;
            }
         }
         store = n_string_push_c(store, c);
      }
      store = n_string_push_c(store, '\'');
   }
   NYD2_LEAVE;
   return store;
}

FL char *
n_shell_quote_cp(char const *cp){
   struct n_string store;
   struct str input;
   char *rv;
   NYD2_ENTER;

   assert(cp != NULL);

   input.s = UNCONST(cp);
   input.l = UIZ_MAX;
   rv = n_string_cp(n_shell_quote(n_string_creat_auto(&store), &input));
   n_string_gut(n_string_drop_ownership(&store));
   NYD2_LEAVE;
   return rv;
}

/* s-it-mode */
