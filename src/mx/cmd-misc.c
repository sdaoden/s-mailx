/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Miscellaneous user commands, like `echo', `pwd', etc.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

#include <su/cs.h>
#include <su/mem.h>
#include <su/sort.h>

#include "mx/child.h"
#include "mx/file-streams.h"
#include "mx/sigs.h"

/* TODO fake */
#include "su/code-in.h"

/* Expand the shell escape by expanding unescaped !'s into the last issued
 * command where possible */
static char const *a_cmisc_bangexp(char const *cp);

/* c_n?echo(), c_n?echoerr() */
static int a_cmisc_echo(void *vp, FILE *fp, boole donl);

/* c_read() */
static boole a_cmisc_read_set(char const *cp, char const *value);

/* c_version() */
static su_sz a_cmisc_version_cmp(void const *s1, void const *s2);

static char const *
a_cmisc_bangexp(char const *cp){
   static struct str last_bang;

   struct n_string xbang, *bang;
   char c;
   boole changed;
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
a_cmisc_echo(void *vp, FILE *fp, boole donl){
   struct n_string s_b, *s;
   int rv;
   boole doerr;
   char const **argv, *varname, **ap, *cp;
   NYD2_IN;

   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;
   s = n_string_reserve(n_string_creat_auto(&s_b), 121/* XXX */);
#ifdef mx_HAVE_ERRORS
   doerr = (fp == n_stderr &&  (n_psonce & n_PSO_INTERACTIVE));
#else
   doerr = FAL0;
#endif

   for(ap = argv; *ap != NULL; ++ap){
      if(ap != argv)
         s = n_string_push_c(s, ' ');
      if((cp = fexpand(*ap, FEXP_NSHORTCUT | FEXP_NVAR)) == NULL)
         cp = *ap;
      s = n_string_push_cp(s, cp);
   }
   if(donl)
      s = n_string_push_c(s, '\n');
   cp = n_string_cp(s);

   if(varname == NULL){
      s32 e;

      e = su_ERR_NONE;
      if(doerr){
         /* xxx Ensure *log-prefix* will be placed by n_err() for next msg */
         if(donl)
            cp = n_string_cp(n_string_trunc(s, s->s_len - 1));
         n_errx(TRU1, (donl ? "%s\n" : "%s"), cp);
      }else if(fputs(cp, fp) == EOF)
         e = su_err_no();
      if((rv = (fflush(fp) == EOF)))
         e = su_err_no();
      rv |= ferror(fp) ? 1 : 0;
      n_pstate_err_no = e;
   }else if(!n_var_vset(varname, (up)cp)){
      n_pstate_err_no = su_ERR_NOTSUP;
      rv = -1;
   }else{
      n_pstate_err_no = su_ERR_NONE;
      rv = (int)s->s_len;
   }
   NYD2_OU;
   return rv;
}

static boole
a_cmisc_read_set(char const *cp, char const *value){
   boole rv;
   NYD2_IN;

   if(!n_shexp_is_valid_varname(cp))
      value = N_("not a valid variable name");
   else if(!n_var_is_user_writable(cp))
      value = N_("variable is read-only");
   else if(!n_var_vset(cp, (up)value))
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

static su_sz
a_cmisc_version_cmp(void const *s1, void const *s2){
   su_sz rv;
   char const *cp1, *cp2;
   NYD2_IN;

   cp1 = s1;
   cp2 = s2;
   rv = su_cs_cmp(&cp1[1], &cp2[1]);
   NYD2_OU;
   return rv;
}

FL int
c_shell(void *v){
   struct mx_child_ctx cc;
   sigset_t mask;
   int rv;
   FILE *fp;
   char const **argv, *varname, *varres;
   NYD_IN;

   n_pstate_err_no = su_ERR_NONE;
   argv = v;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NIL;
   varres = n_empty;
   fp = NIL;

   if(varname != NIL &&
         (fp = mx_fs_tmp_open("shell", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
               mx_FS_O_REGISTER), NIL)) == NIL){
      n_pstate_err_no = su_ERR_CANCELED;
      rv = -1;
   }else{
      sigemptyset(&mask);
      mx_child_ctx_setup(&cc);
      cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
      cc.cc_mask = &mask;
      if(fp != NIL)
         cc.cc_fds[mx_CHILD_FD_OUT] = fileno(fp);
      cc.cc_cmd = ok_vlook(SHELL);
      cc.cc_args[0] = "-c";
      cc.cc_args[1] = a_cmisc_bangexp(*argv);

      if(!mx_child_run(&cc) || (rv = cc.cc_exit_status) < 0){
         n_pstate_err_no = cc.cc_error;
         rv = -1;
      }
   }

   if(fp != NIL){
      if(rv != -1){
         int c;
         char *x;
         off_t l;

         fflush_rewind(fp);
         l = fsize(fp);
         if(UCMP(64, l, >=, UZ_MAX -42)){
            n_pstate_err_no = su_ERR_NOMEM;
            varres = n_empty;
         }else if(l > 0){
            varres = x = n_autorec_alloc(l +1);

            for(; l > 0 && (c = getc(fp)) != EOF; --l)
               *x++ = c;
            *x++ = '\0';
            if(l != 0){
               n_pstate_err_no = su_err_no();
               varres = n_empty; /* xxx hmmm */
            }
         }
      }

      mx_fs_close(fp);
   }

   if(varname != NIL){
      if(!n_var_vset(varname, R(up,varres))){
         n_pstate_err_no = su_ERR_NOTSUP;
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
c_dosh(void *v){
   struct mx_child_ctx cc;
   int rv;
   NYD_IN;
   UNUSED(v);

   mx_child_ctx_setup(&cc);
   cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
   cc.cc_cmd = ok_vlook(SHELL);

   if(mx_child_run(&cc) && (rv = cc.cc_exit_status) >= 0){
      putc('\n', n_stdout);
      /* Line buffered fflush(n_stdout); */
      n_pstate_err_no = su_ERR_NONE;
   }else{
      n_pstate_err_no = cc.cc_error;
      rv = -1;
   }

   NYD_OU;
   return rv;
}

FL int
c_cwd(void *v){
   struct n_string s_b, *s;
   uz l;
   char const *varname;
   NYD_IN;

   s = n_string_creat_auto(&s_b);
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *(char const**)v : NULL;
   l = PATH_MAX;

   for(;; l += PATH_MAX){
      s = n_string_resize(n_string_trunc(s, 0), l);

      if(getcwd(s->s_dat, s->s_len) == NULL){
         int e;

         e = su_err_no();
         if(e == su_ERR_RANGE)
            continue;
         n_perr(_("Failed to getcwd(3)"), e);
         v = NULL;
         break;
      }

      if(varname != NULL){
         if(!n_var_vset(varname, (up)s->s_dat))
            v = NULL;
      }else{
         l = su_cs_len(s->s_dat);
         s = n_string_trunc(s, l);
         if(fwrite(s->s_dat, 1, s->s_len, n_stdout) == s->s_len &&
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
   struct n_string s_b, *s;
   char *linebuf;
   uz linesize, i;
   int rv;
   char const *ifs, **argv, *cp;
   NYD2_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_reserve(s, 64 -1);

   ifs = ok_vlook(ifs);
   linesize = 0;
   linebuf = NULL;
   argv = vp;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      n_pstate_err_no = su_ERR_INTR;
      rv = -1;
      goto jleave;
   }

   n_pstate_err_no = su_ERR_NONE;
   rv = n_go_input(((n_pstate & n_PS_COMPOSE_MODE
            ? n_GO_INPUT_CTX_COMPOSE : n_GO_INPUT_CTX_DEFAULT) |
         n_GO_INPUT_FORCE_STDIN | n_GO_INPUT_NL_ESC |
         n_GO_INPUT_PROMPT_NONE /* XXX POSIX: PS2: yes! */),
         NULL, &linebuf, &linesize, NULL, NULL);
   if(rv < 0){
      if(!n_go_input_is_eof())
         n_pstate_err_no = su_ERR_BADF;
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
            s = n_string_assign_buf(s, trim.s, trim.l);
            trim.l = 0;
         }else for(cp = trim.s, i = 1;; ++cp, ++i){
            if(su_cs_find_c(ifs, *cp) != NULL){
               s = n_string_assign_buf(s, trim.s, i - 1);
               trim.s += i;
               trim.l -= i;
               break;
            }
            if(i == trim.l)
               goto jitall;
         }

         if(!a_cmisc_read_set(*argv, n_string_cp(s))){
            n_pstate_err_no = su_ERR_NOTSUP;
            rv = -1;
            break;
         }
      }
   }

   /* Set the remains to the empty string */
   for(; *argv != NULL; ++argv)
      if(!a_cmisc_read_set(*argv, n_empty)){
         n_pstate_err_no = su_ERR_NOTSUP;
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
   struct n_string s_b, *s;
   char *linebuf;
   uz linesize;
   int rv;
   char const **argv;
   NYD2_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_reserve(s, 64 -1);

   linesize = 0;
   linebuf = NULL;
   argv = vp;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      n_pstate_err_no = su_ERR_INTR;
      rv = -1;
      goto jleave;
   }

   n_pstate_err_no = su_ERR_NONE;

   for(;;){
      rv = n_go_input(((n_pstate & n_PS_COMPOSE_MODE
               ? n_GO_INPUT_CTX_COMPOSE : n_GO_INPUT_CTX_DEFAULT) |
            n_GO_INPUT_FORCE_STDIN | /*n_GO_INPUT_NL_ESC |*/
            n_GO_INPUT_PROMPT_NONE),
            NULL, &linebuf, &linesize, NULL, NULL);
      if(rv < 0){
         if(!n_go_input_is_eof()){
            n_pstate_err_no = su_ERR_BADF;
            goto jleave;
         }
         if(s->s_len == 0)
            goto jleave;
         break;
      }

      if(n_pstate & n_PS_READLINE_NL)
         linebuf[rv++] = '\n'; /* Replace NUL with it */

      if(UNLIKELY(rv == 0)){ /* xxx will not get*/
         if(n_go_input_is_eof()){
            if(s->s_len == 0){
               rv = -1;
               goto jleave;
            }
            break;
         }
      }else if(LIKELY(UCMP(32, S32_MAX - s->s_len, >, rv)))
         s = n_string_push_buf(s, linebuf, rv);
      else{
         n_pstate_err_no = su_ERR_OVERFLOW;
         rv = -1;
         goto jleave;
      }
   }

   if(!a_cmisc_read_set(argv[0], n_string_cp(s))){
      n_pstate_err_no = su_ERR_NOTSUP;
      rv = -1;
      goto jleave;
   }
   rv = s->s_len;

   n_sigman_cleanup_ping(&sm);
jleave:
   if(linebuf != NULL)
      n_free(linebuf);
   NYD2_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

FL struct n_string *
n_version(struct n_string *s){
   NYD_IN;
   s = n_string_push_cp(s, n_uagent);
   s = n_string_push_c(s, ' ');
   s = n_string_push_cp(s, ok_vlook(version));
   s = n_string_push_c(s, ',');
   s = n_string_push_c(s, ' ');
   s = n_string_push_cp(s, ok_vlook(version_date));
   s = n_string_push_c(s, ' ');
   s = n_string_push_c(s, '(');
   s = n_string_push_cp(s, _("build for "));
   s = n_string_push_cp(s, ok_vlook(build_os));
   s = n_string_push_c(s, ')');
   s = n_string_push_c(s, '\n');
   NYD_OU;
   return s;
}

FL int
c_version(void *vp){
   struct utsname ut;
   struct n_string s_b, *s;
   int rv;
   char *iop;
   char const *cp, **arr;
   uz i, lnlen, j;
   NYD_IN;

   s = n_string_creat_auto(&s_b);
   s = n_string_book(s, 1024);

   /* First two lines */
   s = n_version(s);
   s = n_string_push_cp(s, _("Features included (+) or not (-):\n"));

   /* Some lines with the features.
    * *features* starts with dummy byte to avoid + -> *folder* expansions */
   i = su_cs_len(cp = &ok_vlook(features)[1]) +1;
   iop = n_autorec_alloc(i);
   su_mem_copy(iop, cp, i);

   arr = n_autorec_alloc(sizeof(cp) * VAL_FEATURES_CNT);
   for(i = 0; (cp = su_cs_sep_c(&iop, ',', TRU1)) != NULL; ++i)
      arr[i] = cp;
   su_sort_shell_vpp(su_S(void const**,arr), i, &a_cmisc_version_cmp);

   for(lnlen = 0; i-- > 0;){
      cp = *(arr++);
      j = su_cs_len(cp);

      if((lnlen += j + 1) > 72){
         s = n_string_push_c(s, '\n');
         lnlen = j + 1;
      }
      s = n_string_push_c(s, ' ');
      s = n_string_push_buf(s, cp, j);
   }
   s = n_string_push_c(s, '\n');

   /* */
   if(n_poption & n_PO_V){
      s = n_string_push_cp(s, "Compile: ");
      s = n_string_push_cp(s, ok_vlook(build_cc));
      s = n_string_push_cp(s, "\nLink: ");
      s = n_string_push_cp(s, ok_vlook(build_ld));
      if(*(cp = ok_vlook(build_rest)) != '\0'){
         s = n_string_push_cp(s, "\nRest: ");
         s = n_string_push_cp(s, cp);
      }
      s = n_string_push_c(s, '\n');

      /* A trailing line with info of the running machine */
      uname(&ut);
      s = n_string_push_c(s, '@');
      s = n_string_push_cp(s, ut.sysname);
      s = n_string_push_c(s, ' ');
      s = n_string_push_cp(s, ut.release);
      s = n_string_push_c(s, ' ');
      s = n_string_push_cp(s, ut.version);
      s = n_string_push_c(s, ' ');
      s = n_string_push_cp(s, ut.machine);
      s = n_string_push_c(s, '\n');
   }

   /* Done */
   cp = n_string_cp(s);

   if(n_pstate & n_PS_ARGMOD_VPUT){
      if(n_var_vset(*(char const**)vp, (up)cp))
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

#include "su/code-ou.h"
/* s-it-mode */
