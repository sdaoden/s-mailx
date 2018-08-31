/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Miscellaneous user commands, like `echo', `pwd', etc.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
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
#undef su_FILE
#define su_FILE cmd_misc

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

/* Expand the shell escape by expanding unescaped !'s into the last issued
 * command where possible */
static char const *a_cmisc_bangexp(char const *cp);

/* c_n?echo(), c_n?echoerr() */
static int a_cmisc_echo(void *vp, FILE *fp, bool_t donl);

/* c_read() */
static bool_t a_cmisc_read_set(char const *cp, char const *value);

/* c_version() */
static int a_cmisc_version_cmp(void const *s1, void const *s2);

static char const *
a_cmisc_bangexp(char const *cp){
   static struct str last_bang;

   struct n_string xbang, *bang;
   char c;
   bool_t changed;
   NYD_IN;

   if(!ok_blook(bang))
      goto jleave;

   changed = FAL0;

   for(bang = n_string_creat(&xbang); (c = *cp++) != '\0';){
      if(c == '!'){
         if(last_bang.l > 0)
            bang = n_string_push_buf(bang, last_bang.s, last_bang.l);
         changed = TRU1;
      }else{
         if(c == '\\' && *cp == '!'){
            ++cp;
            c = '!';
            changed = TRU1;
         }
         bang = n_string_push_c(bang, c);
      }
   }

   if(last_bang.s != NULL)
      n_free(last_bang.s);
   last_bang.s = n_string_cp(bang);
   last_bang.l = bang->s_len;
   bang = n_string_drop_ownership(bang);
   n_string_gut(bang);

   cp = last_bang.s;
   if(changed)
      fprintf(n_stdout, "!%s\n", cp);
jleave:
   NYD_OU;
   return cp;
}

static int
a_cmisc_echo(void *vp, FILE *fp, bool_t donl){
   struct n_string s, *sp;
   int rv;
   bool_t doerr;
   char const **argv, *varname, **ap, *cp;
   NYD2_IN;

   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;
   sp = n_string_reserve(n_string_creat_auto(&s), 121/* XXX */);
#ifdef HAVE_ERRORS
   doerr = (fp == n_stderr &&  (n_psonce & n_PSO_INTERACTIVE));
#else
   doerr = FAL0;
#endif

   for(ap = argv; *ap != NULL; ++ap){
      if(ap != argv)
         sp = n_string_push_c(sp, ' ');
      if((cp = fexpand(*ap, FEXP_NSHORTCUT | FEXP_NVAR)) == NULL)
         cp = *ap;
      sp = n_string_push_cp(sp, cp);
   }
   if(donl)
      sp = n_string_push_c(sp, '\n');
   cp = n_string_cp(sp);

   if(varname == NULL){
      si32_t e;

      e = n_ERR_NONE;
      if(doerr){
         /* xxx Ensure *log-prefix* will be placed by n_err() for next msg */
         if(donl)
            cp = n_string_cp(n_string_trunc(sp, sp->s_len - 1));
         n_err((donl ? "%s\n" : "%s"), cp);
      }else if(fputs(cp, fp) == EOF)
         e = n_err_no;
      if((rv = (fflush(fp) == EOF)))
         e = n_err_no;
      rv |= ferror(fp) ? 1 : 0;
      n_pstate_err_no = e;
   }else if(!n_var_vset(varname, (uintptr_t)cp)){
      n_pstate_err_no = n_ERR_NOTSUP;
      rv = -1;
   }else{
      n_pstate_err_no = n_ERR_NONE;
      rv = (int)sp->s_len;
   }
   NYD2_OU;
   return rv;
}

static bool_t
a_cmisc_read_set(char const *cp, char const *value){
   bool_t rv;
   NYD2_IN;

   if(!n_shexp_is_valid_varname(cp))
      value = N_("not a valid variable name");
   else if(!n_var_is_user_writable(cp))
      value = N_("variable is read-only");
   else if(!n_var_vset(cp, (uintptr_t)value))
      value = N_("failed to update variable value");
   else{
      rv = TRU1;
      goto jleave;
   }
   n_err("`read': %s: %s\n", V_(value), n_shexp_quote_cp(cp, FAL0));
   rv = FAL0;
jleave:
   NYD2_OU;
   return rv;
}

static int
a_cmisc_version_cmp(void const *s1, void const *s2){
   char const * const *cp1, * const *cp2;
   int rv;
   NYD2_IN;

   cp1 = s1;
   cp2 = s2;
   rv = strcmp(&(*cp1)[1], &(*cp2)[1]);
   NYD2_OU;
   return rv;
}

FL int
c_shell(void *v)
{
   sigset_t mask;
   int rv;
   FILE *fp;
   char const **argv, *varname, *varres, *cp;
   NYD_IN;

   n_pstate_err_no = n_ERR_NONE;
   argv = v;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;
   varres = n_empty;
   fp = NULL;

   if(varname != NULL &&
         (fp = Ftmp(NULL, "shell", OF_RDWR | OF_UNLINK | OF_REGISTER)
            ) == NULL){
      n_pstate_err_no = n_ERR_CANCELED;
      rv = -1;
   }else{
      cp = a_cmisc_bangexp(*argv);

      sigemptyset(&mask);
      if(n_child_run(ok_vlook(SHELL), &mask,
            n_CHILD_FD_PASS, (fp != NULL ? fileno(fp) : n_CHILD_FD_PASS),
            "-c", cp, NULL, NULL, &rv) < 0){
         n_pstate_err_no = n_err_no;
         rv = -1;
      }else{
         rv = WEXITSTATUS(rv);

         if(fp != NULL){
            int c;
            char *x;
            off_t l;

            fflush_rewind(fp);
            l = fsize(fp);
            if(UICMP(64, l, >=, UIZ_MAX -42)){
               n_pstate_err_no = n_ERR_NOMEM;
               varres = n_empty;
            }else{
               varres = x = n_autorec_alloc(l +1);

               for(; l > 0 && (c = getc(fp)) != EOF; --l)
                  *x++ = c;
               *x++ = '\0';
               if(l != 0){
                  n_pstate_err_no = n_err_no;
                  varres = n_empty; /* xxx hmmm */
               }
            }
         }
      }
   }

   if(fp != NULL)
      Fclose(fp);

   if(varname != NULL){
      if(!n_var_vset(varname, (uintptr_t)varres)){
         n_pstate_err_no = n_ERR_NOTSUP;
         rv = -1;
      }
   }else if(rv >= 0 && (n_psonce & n_PSO_INTERACTIVE)){
      fprintf(n_stdout, "!\n");
      /* Line buffered fflush(n_stdout); */
   }
   NYD_OU;
   return rv;
}

FL int
c_dosh(void *v)
{
   int rv;
   NYD_IN;
   n_UNUSED(v);

   if(n_child_run(ok_vlook(SHELL), 0, n_CHILD_FD_PASS, n_CHILD_FD_PASS, NULL,
         NULL, NULL, NULL, &rv) < 0){
      n_pstate_err_no = n_err_no;
      rv = -1;
   }else{
      putc('\n', n_stdout);
      /* Line buffered fflush(n_stdout); */
      n_pstate_err_no = n_ERR_NONE;
      rv = WEXITSTATUS(rv);
   }
   NYD_OU;
   return rv;
}

FL int
c_cwd(void *v){
   struct n_string s_b, *sp;
   size_t l;
   char const *varname;
   NYD_IN;

   sp = n_string_creat_auto(&s_b);
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *(char const**)v : NULL;
   l = PATH_MAX;

   for(;; l += PATH_MAX){
      sp = n_string_resize(n_string_trunc(sp, 0), l);

      if(getcwd(sp->s_dat, sp->s_len) == NULL){
         int e;

         e = n_err_no;
         if(e == n_ERR_RANGE)
            continue;
         n_perr(_("Failed to getcwd(3)"), e);
         v = NULL;
         break;
      }

      if(varname != NULL){
         if(!n_var_vset(varname, (uintptr_t)sp->s_dat))
            v = NULL;
      }else{
         l = strlen(sp->s_dat);
         sp = n_string_trunc(sp, l);
         if(fwrite(sp->s_dat, 1, sp->s_len, n_stdout) == sp->s_len &&
               putc('\n', n_stdout) == EOF)
            v = NULL;
      }
      break;
   }
   NYD_OU;
   return (v == NULL);
}

FL int
c_chdir(void *v)
{
   char **arglist = v;
   char const *cp;
   NYD_IN;

   if (*arglist == NULL)
      cp = ok_vlook(HOME);
   else if ((cp = fexpand(*arglist, FEXP_LOCAL | FEXP_NOPROTO)) == NULL)
      goto jleave;
   if (chdir(cp) == -1) {
      n_perr(cp, 0);
      cp = NULL;
   }
jleave:
   NYD_OU;
   return (cp == NULL);
}

FL int
c_echo(void *v){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(v, n_stdout, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_echoerr(void *v){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(v, n_stderr, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_echon(void *v){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(v, n_stdout, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_echoerrn(void *v){
   int rv;
   NYD_IN;

   rv = a_cmisc_echo(v, n_stderr, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_read(void * volatile vp){
   struct n_sigman sm;
   struct str trim;
   struct n_string s, *sp;
   char *linebuf;
   size_t linesize, i;
   int rv;
   char const *ifs, **argv, *cp;
   NYD2_IN;

   sp = n_string_creat_auto(&s);
   sp = n_string_reserve(sp, 64 -1);

   ifs = ok_vlook(ifs);
   linesize = 0;
   linebuf = NULL;
   argv = vp;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      n_pstate_err_no = n_ERR_INTR;
      rv = -1;
      goto jleave;
   }

   n_pstate_err_no = n_ERR_NONE;
   rv = n_go_input(((n_pstate & n_PS_COMPOSE_MODE
            ? n_GO_INPUT_CTX_COMPOSE : n_GO_INPUT_CTX_DEFAULT) |
         n_GO_INPUT_FORCE_STDIN | n_GO_INPUT_NL_ESC |
         n_GO_INPUT_PROMPT_NONE /* XXX POSIX: PS2: yes! */),
         NULL, &linebuf, &linesize, NULL, NULL);
   if(rv < 0){
      if(!n_go_input_is_eof())
         n_pstate_err_no = n_ERR_BADF;
      goto jleave;
   }else if(rv == 0){
      if(n_go_input_is_eof()){
         rv = -1;
         goto jleave;
      }
   }else{
      trim.s = linebuf;
      trim.l = rv;

      for(; *argv != NULL; ++argv){
         if(trim.l == 0 || n_str_trim_ifs(&trim, FAL0)->l == 0)
            break;

         /* The last variable gets the remaining line less trailing IFS-WS */
         if(argv[1] == NULL){
jitall:
            sp = n_string_assign_buf(sp, trim.s, trim.l);
            trim.l = 0;
         }else for(cp = trim.s, i = 1;; ++cp, ++i){
            if(strchr(ifs, *cp) != NULL){
               sp = n_string_assign_buf(sp, trim.s, i - 1);
               trim.s += i;
               trim.l -= i;
               break;
            }
            if(i == trim.l)
               goto jitall;
         }

         if(!a_cmisc_read_set(*argv, n_string_cp(sp))){
            n_pstate_err_no = n_ERR_NOTSUP;
            rv = -1;
            break;
         }
      }
   }

   /* Set the remains to the empty string */
   for(; *argv != NULL; ++argv)
      if(!a_cmisc_read_set(*argv, n_empty)){
         n_pstate_err_no = n_ERR_NOTSUP;
         rv = -1;
         break;
      }

   n_sigman_cleanup_ping(&sm);
jleave:
   if(linebuf != NULL)
      n_free(linebuf);
   NYD2_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

FL int
c_readall(void * vp){ /* TODO 64-bit retval */
   struct n_sigman sm;
   struct n_string s, *sp;
   char *linebuf;
   size_t linesize;
   int rv;
   char const **argv;
   NYD2_IN;

   sp = n_string_creat_auto(&s);
   sp = n_string_reserve(sp, 64 -1);

   linesize = 0;
   linebuf = NULL;
   argv = vp;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      n_pstate_err_no = n_ERR_INTR;
      rv = -1;
      goto jleave;
   }

   n_pstate_err_no = n_ERR_NONE;

   for(;;){
      rv = n_go_input(((n_pstate & n_PS_COMPOSE_MODE
               ? n_GO_INPUT_CTX_COMPOSE : n_GO_INPUT_CTX_DEFAULT) |
            n_GO_INPUT_FORCE_STDIN | /*n_GO_INPUT_NL_ESC |*/
            n_GO_INPUT_PROMPT_NONE),
            NULL, &linebuf, &linesize, NULL, NULL);
      if(rv < 0){
         if(!n_go_input_is_eof()){
            n_pstate_err_no = n_ERR_BADF;
            goto jleave;
         }
         if(sp->s_len == 0)
            goto jleave;
         break;
      }

      if(n_pstate & n_PS_READLINE_NL)
         linebuf[rv++] = '\n'; /* Replace NUL with it */

      if(n_UNLIKELY(rv == 0)){ /* xxx will not get*/
         if(n_go_input_is_eof()){
            if(sp->s_len == 0){
               rv = -1;
               goto jleave;
            }
            break;
         }
      }else if(n_LIKELY(UICMP(32, SI32_MAX - sp->s_len, >, rv)))
         sp = n_string_push_buf(sp, linebuf, rv);
      else{
         n_pstate_err_no = n_ERR_OVERFLOW;
         rv = -1;
         goto jleave;
      }
   }

   if(!a_cmisc_read_set(argv[0], n_string_cp(sp))){
      n_pstate_err_no = n_ERR_NOTSUP;
      rv = -1;
      goto jleave;
   }
   rv = sp->s_len;

   n_sigman_cleanup_ping(&sm);
jleave:
   if(linebuf != NULL)
      n_free(linebuf);
   NYD2_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

FL int
c_version(void *vp){
   struct utsname ut;
   struct n_string s, *sp = &s;
   int rv;
   char *iop;
   char const *cp, **arr;
   size_t i, lnlen, j;
   NYD_IN;

   sp = n_string_creat_auto(sp);
   sp = n_string_book(sp, 1024);

   /* First two lines */
   sp = n_string_push_cp(sp, n_uagent);
   sp = n_string_push_c(sp, ' ');
   sp = n_string_push_cp(sp, ok_vlook(version));
   sp = n_string_push_c(sp, ',');
   sp = n_string_push_c(sp, ' ');
   sp = n_string_push_cp(sp, ok_vlook(version_date));
   sp = n_string_push_c(sp, ' ');
   sp = n_string_push_c(sp, '(');
   sp = n_string_push_cp(sp, _("build for "));
   sp = n_string_push_cp(sp, ok_vlook(build_os));
   sp = n_string_push_c(sp, ')');
   sp = n_string_push_cp(sp, _("\nFeatures included (+) or not (-):\n"));

   /* Some lines with the features.
    * *features* starts with dummy byte to avoid + -> *folder* expansions */
   i = strlen(cp = &ok_vlook(features)[1]) +1;
   iop = n_autorec_alloc(i);
   memcpy(iop, cp, i);

   arr = n_autorec_alloc(sizeof(cp) * VAL_FEATURES_CNT);
   for(i = 0; (cp = n_strsep(&iop, ',', TRU1)) != NULL; ++i)
      arr[i] = cp;
   qsort(arr, i, sizeof(cp), &a_cmisc_version_cmp);

   for(lnlen = 0; i-- > 0;){
      cp = *(arr++);
      j = strlen(cp);

      if((lnlen += j + 1) > 72){
         sp = n_string_push_c(sp, '\n');
         lnlen = j + 1;
      }
      sp = n_string_push_c(sp, ' ');
      sp = n_string_push_buf(sp, cp, j);
   }
   sp = n_string_push_c(sp, '\n');

   /* */
   if(n_poption & n_PO_VERB){
      sp = n_string_push_cp(sp, "Compile: ");
      sp = n_string_push_cp(sp, ok_vlook(build_cc));
      sp = n_string_push_cp(sp, "\nLink: ");
      sp = n_string_push_cp(sp, ok_vlook(build_ld));
      if(*(cp = ok_vlook(build_rest)) != '\0'){
         sp = n_string_push_cp(sp, "\nRest: ");
         sp = n_string_push_cp(sp, cp);
      }
      sp = n_string_push_c(sp, '\n');

      /* A trailing line with info of the running machine */
      uname(&ut);
      sp = n_string_push_c(sp, '@');
      sp = n_string_push_cp(sp, ut.sysname);
      sp = n_string_push_c(sp, ' ');
      sp = n_string_push_cp(sp, ut.release);
      sp = n_string_push_c(sp, ' ');
      sp = n_string_push_cp(sp, ut.version);
      sp = n_string_push_c(sp, ' ');
      sp = n_string_push_cp(sp, ut.machine);
      sp = n_string_push_c(sp, '\n');
   }

   /* Done */
   cp = n_string_cp(sp);

   if(n_pstate & n_PS_ARGMOD_VPUT){
      if(n_var_vset(*(char const**)vp, (uintptr_t)cp))
         rv = 0;
      else
         rv = -1;
   }else{
      if(fputs(cp, n_stdout) != EOF)
         rv = 0;
      else{
         clearerr(n_stdout);
         rv = 1;
      }
   }
   NYD_OU;
   return rv;
}

/* s-it-mode */
