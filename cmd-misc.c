/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Miscellaneous user commands, like `echo', `pwd', etc.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE cmd_misc

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Expand the shell escape by expanding unescaped !'s into the last issued
 * command where possible */
static char const *a_cmisc_bangexp(char const *cp);

/* c_n?echo(), c_n?echoerr() */
static int a_cmisc_echo(void *vp, FILE *fp, bool_t donl);

static char const *
a_cmisc_bangexp(char const *cp){
   static struct str last_bang;

   struct n_string xbang, *bang;
   char c;
   bool_t changed;
   NYD_ENTER;

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
      free(last_bang.s);
   last_bang.s = n_string_cp(bang);
   last_bang.l = bang->s_len;
   bang = n_string_drop_ownership(bang);
   n_string_gut(bang);

   cp = last_bang.s;
   if(changed)
      fprintf(n_stdout, "!%s\n", cp);
jleave:
   NYD_LEAVE;
   return cp;
}

static int
a_cmisc_echo(void *vp, FILE *fp, bool_t donl){
   char const **argv, **ap, *cp;
   NYD2_ENTER;

   for(ap = argv = vp; *ap != NULL; ++ap){
      if(ap != argv)
         putc(' ', fp);
      if((cp = fexpand(*ap, FEXP_NSHORTCUT | FEXP_NVAR)) == NULL)
         cp = *ap;
      fputs(cp, fp);
   }
   if(donl)
      putc('\n', fp);
   fflush(fp);
   NYD2_LEAVE;
   return 0;
}

FL int
c_sleep(void *v){
   uiz_t sec, msec;
   char **argv;
   NYD_ENTER;

   argv = v;

   if((n_idec_uiz_cp(&sec, argv[0], 0, NULL) &
         (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
         ) != n_IDEC_STATE_CONSUMED)
      goto jesyn;

   if(argv[1] == NULL)
      msec = 0;
   else if((n_idec_uiz_cp(&msec, argv[1], 0, NULL) &
         (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
         ) != n_IDEC_STATE_CONSUMED)
      goto jesyn;

   if(UIZ_MAX / n_DATE_MILLISSEC < sec)
      goto jeover;
   sec *= n_DATE_MILLISSEC;

   if(UIZ_MAX - sec < msec)
      goto jeover;
   msec += sec;

   n_pstate_err_no = (n_msleep(msec, (argv[2] == NULL)) > 0)
         ? n_ERR_INTR : n_ERR_NONE;
jleave:
   NYD_LEAVE;
   return (argv == NULL);
jeover:
   n_err(_("`sleep': argument(s) overflow(s) datatype\n"));
   n_pstate_err_no = n_ERR_OVERFLOW;
   argv = NULL;
   goto jleave;
jesyn:
   n_err(_("Synopsis: sleep: <seconds> [<milliseconds>] [uninterruptible]\n"));
   n_pstate_err_no = n_ERR_INVAL;
   argv = NULL;
   goto jleave;
}

FL int
c_shell(void *v)
{
   sigset_t mask;
   FILE *fp;
   char const **argv, *varname, *varres, *cp;
   NYD_ENTER;

   n_pstate_err_no = n_ERR_NONE;
   argv = v;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;

   if(varname != NULL &&
         (fp = Ftmp(NULL, "shell", OF_RDWR | OF_UNLINK | OF_REGISTER)
            ) == NULL){
      n_pstate_err_no = n_ERR_CANCELED;
      varres = n_empty;
   }else{
      cp = a_cmisc_bangexp(*argv);

      sigemptyset(&mask);
      (void)n_child_run(ok_vlook(SHELL), &mask, /* TODO TRUE EXIT STATUS */
            n_CHILD_FD_PASS, (varname != NULL ? fileno(fp) : n_CHILD_FD_PASS),
            "-c", cp, NULL, NULL);

      if(varname != NULL){
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
            if(l != 0)
               n_pstate_err_no = errno;
         }
         Fclose(fp);
      }
   }

   if(varname != NULL){
      if(!n_var_vset(varname, (uintptr_t)varres))
         n_pstate_err_no = n_ERR_NOTSUP;
   }else
      fprintf(n_stdout, "!\n");
      /* Line buffered fflush(n_stdout); */

   NYD_LEAVE;
   return (n_pstate_err_no != n_ERR_NONE);
}

FL int
c_dosh(void *v)
{
   NYD_ENTER;
   n_UNUSED(v);

   n_child_run(ok_vlook(SHELL), 0, n_CHILD_FD_PASS, n_CHILD_FD_PASS, NULL,
      NULL, NULL, NULL);
   putc('\n', n_stdout);
   /* Line buffered fflush(n_stdout); */
   NYD_LEAVE;
   return 0;
}

FL int
c_cwd(void *v){
   struct n_string s_b, *sp;
   size_t l;
   char const *varname;
   NYD_ENTER;

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
   NYD_LEAVE;
   return (v == NULL);
}

FL int
c_chdir(void *v)
{
   char **arglist = v;
   char const *cp;
   NYD_ENTER;

   if (*arglist == NULL)
      cp = ok_vlook(HOME);
   else if ((cp = fexpand(*arglist, FEXP_LOCAL | FEXP_NOPROTO)) == NULL)
      goto jleave;
   if (chdir(cp) == -1) {
      n_perr(cp, 0);
      cp = NULL;
   }
jleave:
   NYD_LEAVE;
   return (cp == NULL);
}

FL int
c_echo(void *v){
   int rv;
   NYD_ENTER;

   rv = a_cmisc_echo(v, n_stdout, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_echoerr(void *v){
   int rv;
   NYD_ENTER;

   rv = a_cmisc_echo(v, n_stderr, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_echon(void *v){
   int rv;
   NYD_ENTER;

   rv = a_cmisc_echo(v, n_stdout, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_echoerrn(void *v){
   int rv;
   NYD_ENTER;

   rv = a_cmisc_echo(v, n_stderr, FAL0);
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
