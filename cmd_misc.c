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
c_shell(void *v)
{
   sigset_t mask;
   char const *cp;
   NYD_ENTER;

   cp = a_cmisc_bangexp(v);

   sigemptyset(&mask);
   run_command(ok_vlook(SHELL), &mask, COMMAND_FD_PASS, COMMAND_FD_PASS, "-c",
      cp, NULL, NULL);
   fprintf(n_stdout, "!\n");
   NYD_LEAVE;
   return 0;
}

FL int
c_dosh(void *v)
{
   NYD_ENTER;
   n_UNUSED(v);

   run_command(ok_vlook(SHELL), 0, COMMAND_FD_PASS, COMMAND_FD_PASS, NULL,
      NULL, NULL, NULL);
   putc('\n', n_stdout);
   NYD_LEAVE;
   return 0;
}

FL int
c_cwd(void *v)
{
   char buf[PATH_MAX]; /* TODO getcwd(3) may return a larger value */
   NYD_ENTER;

   if (getcwd(buf, sizeof buf) != NULL) {
      fputs(buf, n_stdout);
      putc('\n', n_stdout);
      v = (void*)0x1;
   } else {
      n_perr(_("getcwd"), 0);
      v = NULL;
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
