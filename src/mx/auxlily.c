/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that don't fit anywhere else.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE auxlily
#define mx_SOURCE
#define mx_SOURCE_AUXLILY

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

#include <time.h>

#ifdef mx_HAVE_IDNA
# if mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN2
#  include <idn2.h>
# elif mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN
#  include <idna.h>
#  include <idn-free.h>
# elif mx_HAVE_IDNA == n_IDNA_IMPL_IDNKIT
#  include <idn/api.h>
# endif
#endif

#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
#  include <sys/socket.h>
# endif

# include <netdb.h>
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/sort.h>
#include <su/time.h>

#include "mx/child.h"
#include "mx/cmd-filetype.h"
#include "mx/colour.h"
#include "mx/compat.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/termios.h"
#include "mx/tty.h"

#ifdef mx_HAVE_ERRORS
# include "mx/cmd.h"
#endif
#ifdef mx_HAVE_IDNA
# include "mx/iconv.h"
#endif

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* The difference in between mx_HAVE_ERRORS and not, is size of queue only */
struct a_aux_err_node{
   struct a_aux_err_node *ae_next;
   u32 ae_cnt;
   boole ae_done;
   u8 ae_pad[3];
   uz ae_dumped_till;
   struct n_string ae_str;
};

/* Error ring, for `errors' */
static struct a_aux_err_node *a_aux_err_head;
static struct a_aux_err_node *a_aux_err_tail;

/* Get our $PAGER; if env_addon is not NULL it is checked whether we know about
 * some environment variable that supports colour+ and set *env_addon to that,
 * e.g., "LESS=FRSXi" */
static char const *a_aux_pager_get(char const **env_addon);

static char const *
a_aux_pager_get(char const **env_addon){
   char const *rv;
   NYD_IN;

   rv = ok_vlook(PAGER);

   if(env_addon != NIL){
      *env_addon = NIL;
      /* Update the manual upon any changes:
       *    *colour-pager*, $PAGER */
      if(su_cs_find(rv, "less") != NIL){
         if(getenv("LESS") == NIL)
            *env_addon = "LESS=RI";
      }else if(su_cs_find(rv, "lv") != NIL){
         if(getenv("LV") == NIL)
            *env_addon = "LV=-c";
      }
   }
   NYD_OU;
   return rv;
}

FL struct n_string *
mx_version(struct n_string *s){
   NYD2_IN;

   s = n_string_push_cp(s, n_uagent);
   s = n_string_push_c(s, ' ');
   s = n_string_push_cp(s, ok_vlook(version));
   s = n_string_push_c(s, ',');
   s = n_string_push_c(s, ' ');
   s = n_string_push_cp(s, ok_vlook(version_date));
   s = n_string_push_c(s, ' ');
   s = n_string_push_c(s, '(');
   s = n_string_push_cp(s, _("built for "));
   s = n_string_push_cp(s, ok_vlook(build_os));
   s = n_string_push_c(s, ')');
   s = n_string_push_c(s, '\n');

   NYD2_OU;
   return s;
}

FL uz
n_screensize(void){
   char const *cp;
   uz rv;
   NYD2_IN;

   if((cp = ok_vlook(screen)) != NIL){
      su_idec_uz_cp(&rv, cp, 0, NIL);
      if(rv == 0)
         rv = mx_termios_dimen.tiosd_height;
   }else
      rv = mx_termios_dimen.tiosd_height;

   if(rv > 2)
      rv -= 2;
   NYD2_OU;
   return rv;
}

FL FILE *
mx_pager_open(void){
   char const *env_add[2], *pager;
   FILE *rv;
   NYD2_IN;

   ASSERT(n_psonce & n_PSO_INTERACTIVE);

   pager = a_aux_pager_get(env_add + 0);
   env_add[1] = NIL;

   if((rv = mx_fs_pipe_open(pager, mx_FS_PIPE_WRITE_CHILD_PASS, NIL, env_add,
            -1)) == NIL)
      n_perr(pager, 0);

   NYD2_OU;
   return rv;
}

FL boole
mx_pager_close(FILE *fp){
   boole rv;
   NYD2_IN;

   rv = mx_fs_pipe_close(fp, TRU1);

   NYD2_OU;
   return rv;
}

FL void
page_or_print(FILE *fp, uz lines)
{
   int c;
   char const *cp;
   NYD_IN;

   fflush_rewind(fp);

   if(mx_go_may_yield_control() && (cp = ok_vlook(crt)) != NIL){
      uz rows;

      if(*cp == '\0')
         rows = mx_termios_dimen.tiosd_height;
      else
         su_idec_uz_cp(&rows, cp, 0, NULL);
      /* Avoid overflow later on */
      if(rows == UZ_MAX)
         --rows;

      if (rows > 0 && lines == 0) {
         while ((c = getc(fp)) != EOF)
            if (c == '\n' && ++lines >= rows)
               break;
         really_rewind(fp);
      }

      /* Take account for the follow-up prompt */
      if(lines + 1 >= rows){
         struct mx_child_ctx cc;
         char const *env_addon[2];

         mx_child_ctx_setup(&cc);
         cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
         cc.cc_fds[mx_CHILD_FD_IN] = fileno(fp);
         cc.cc_cmd = a_aux_pager_get(&env_addon[0]);
         env_addon[1] = NIL;
         cc.cc_env_addon = env_addon;
         mx_child_run(&cc);
         goto jleave;
      }
   }

   while ((c = getc(fp)) != EOF)
      putc(c, n_stdout);
jleave:
   NYD_OU;
}

FL enum protocol
which_protocol(char const *name, boole check_stat, boole try_hooks,
   char const **adjusted_or_nil)
{
   /* TODO This which_protocol() sickness should be URL::new()->protocol() */
   char const *cp, *orig_name;
   enum protocol rv, fixrv;
   NYD2_IN;

   rv = fixrv = PROTO_UNKNOWN;

   if(name[0] == '%' && name[1] == ':')
      name += 2;
   orig_name = name;

   for(cp = name; *cp && *cp != ':'; cp++)
      if(!su_cs_is_alnum(*cp))
         goto jfile;

   if(cp[0] == ':' && cp[1] == '/' && cp[2] == '/'){ /* TODO lookup table */
      boole yeshooks;

      yeshooks = FAL0;

      if(!su_cs_cmp_case_n(name, "file", sizeof("file") -1) ||
            !su_cs_cmp_case_n(name, "mbox", sizeof("mbox") -1))
         yeshooks = TRU1, rv = PROTO_FILE;
      else if(!su_cs_cmp_case_n(name, "eml", sizeof("eml") -1))
         yeshooks = TRU1, rv = n_PROTO_EML;
      else if(!su_cs_cmp_case_n(name, "maildir", sizeof("maildir") -1)){
#ifdef mx_HAVE_MAILDIR
         rv = PROTO_MAILDIR;
#else
         n_err(_("No Maildir directory support compiled in\n"));
#endif
      }else if(!su_cs_cmp_case_n(name, "pop3", sizeof("pop3") -1)){
#ifdef mx_HAVE_POP3
         rv = PROTO_POP3;
#else
         n_err(_("No POP3 support compiled in\n"));
#endif
      }else if(!su_cs_cmp_case_n(name, "pop3s", sizeof("pop3s") -1)){
#if defined mx_HAVE_POP3 && defined mx_HAVE_TLS
         rv = PROTO_POP3;
#else
         n_err(_("No POP3S support compiled in\n"));
#endif
      }else if(!su_cs_cmp_case_n(name, "imap", sizeof("imap") -1)){
#ifdef mx_HAVE_IMAP
         rv = PROTO_IMAP;
#else
         n_err(_("No IMAP support compiled in\n"));
#endif
      }else if(!su_cs_cmp_case_n(name, "imaps", sizeof("imaps") -1)){
#if defined mx_HAVE_IMAP && defined mx_HAVE_TLS
         rv = PROTO_IMAP;
#else
         n_err(_("No IMAPS support compiled in\n"));
#endif
      }

      orig_name = name = &cp[3];

      if(yeshooks){
         fixrv = rv;
         goto jcheck;
      }
   }else{
jfile:
      rv = PROTO_FILE;
jcheck:
      if(check_stat || try_hooks){
         struct mx_filetype ft;
         struct stat stb;
         char *np;
         uz i;

         np = n_lofi_alloc((i = su_cs_len(name)) + 4 +1);
         su_mem_copy(np, name, i +1);

         if(!stat(name, &stb)){
            if(S_ISDIR(stb.st_mode)
#ifdef mx_HAVE_MAILDIR
                  && (su_mem_copy(&np[i], "/tmp", 5),
                     !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
                  (su_mem_copy(&np[i], "/new", 5),
                     !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
                  (su_mem_copy(&np[i], "/cur", 5),
                     !stat(np, &stb) && S_ISDIR(stb.st_mode))
#endif
            ){
               rv =
#ifdef mx_HAVE_MAILDIR
                     PROTO_MAILDIR
#else
                     PROTO_UNKNOWN
#endif
               ;
            }
         }else if(try_hooks && mx_filetype_trial(&ft, name)){
            orig_name = savecatsep(name, '.', ft.ft_ext_dat);
            if(fixrv != PROTO_UNKNOWN)
               rv = fixrv;
         }else if(fixrv == PROTO_UNKNOWN &&
               (cp = ok_vlook(newfolders)) != NIL &&
               !su_cs_cmp_case(cp, "maildir")){
            rv =
#ifdef mx_HAVE_MAILDIR
                  PROTO_MAILDIR
#else
                  PROTO_UNKNOWN
#endif
            ;
#ifndef mx_HAVE_MAILDIR
            n_err(_("*newfolders*: no Maildir support compiled in\n"));
#endif
         }

         n_lofi_free(np);

         if(fixrv != PROTO_UNKNOWN && fixrv != rv)
            rv = PROTO_UNKNOWN;
      }
   }

   if(adjusted_or_nil != NIL)
      *adjusted_or_nil = orig_name;

   NYD2_OU;
   return rv;
}

FL char *
n_c_to_hex_base16(char store[3], char c){
   static char const itoa16[] = "0123456789ABCDEF";
   NYD2_IN;

   store[2] = '\0';
   store[1] = itoa16[(u8)c & 0x0F];
   c = ((u8)c >> 4) & 0x0F;
   store[0] = itoa16[(u8)c];
   NYD2_OU;
   return store;
}

FL s32
n_c_from_hex_base16(char const hex[2]){
   static u8 const atoi16[] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x30-0x37 */
      0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x38-0x3F */
      0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, /* 0x40-0x47 */
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x48-0x4f */
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x50-0x57 */
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x58-0x5f */
      0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF  /* 0x60-0x67 */
   };
   u8 i1, i2;
   s32 rv;
   NYD2_IN;

   if ((i1 = (u8)hex[0] - '0') >= NELEM(atoi16) ||
         (i2 = (u8)hex[1] - '0') >= NELEM(atoi16))
      goto jerr;
   i1 = atoi16[i1];
   i2 = atoi16[i2];
   if ((i1 | i2) & 0xF0u)
      goto jerr;
   rv = i1;
   rv <<= 4;
   rv += i2;
jleave:
   NYD2_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

FL char const *
n_getdeadletter(void){
   char const *cp;
   boole bla;
   NYD_IN;

   bla = FAL0;
jredo:
   cp = fexpand(ok_vlook(DEAD), FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL);
   if(cp == NULL || su_cs_len(cp) >= PATH_MAX){
      if(!bla){
         n_err(_("Failed to expand *DEAD*, setting default (%s): %s\n"),
            VAL_DEAD, n_shexp_quote_cp((cp == NULL ? n_empty : cp), FAL0));
         ok_vclear(DEAD);
         bla = TRU1;
         goto jredo;
      }else{
         cp = savecatsep(ok_vlook(TMPDIR), '/', VAL_DEAD_BASENAME);
         n_err(_("Cannot expand *DEAD*, using: %s\n"), cp);
      }
   }
   NYD_OU;
   return cp;
}

FL char *
n_nodename(boole mayoverride){
   static char *sys_hostname, *hostname; /* XXX free-at-exit */

   struct utsname ut;
   char *hn;
#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
   struct addrinfo hints, *res;
# else
   struct hostent *hent;
# endif
#endif
   NYD2_IN;

   if(mayoverride && (hn = ok_vlook(hostname)) != NULL && *hn != '\0'){
      ;
   }else if(su_state_has(su_STATE_REPRODUCIBLE)){
      hn = n_UNCONST(su_reproducible_build);
   }else if((hn = sys_hostname) == NULL){
      boole lofi;

      lofi = FAL0;
      uname(&ut);
      hn = ut.nodename;

#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
      su_mem_set(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_CANONNAME;
      if(getaddrinfo(hn, NULL, &hints, &res) == 0){
         if(res->ai_canonname != NULL){
            uz l;

            l = su_cs_len(res->ai_canonname) +1;
            hn = n_lofi_alloc(l);
            lofi = TRU1;
            su_mem_copy(hn, res->ai_canonname, l);
         }
         freeaddrinfo(res);
      }
# else
      hent = gethostbyname(hn);
      if(hent != NULL)
         hn = hent->h_name;
# endif
#endif /* mx_HAVE_NET */

      /* Ensure it is non-empty! */
      if(hn[0] == '\0')
         hn = n_UNCONST(n_LOCALHOST_DEFAULT_NAME);

#ifdef mx_HAVE_IDNA
      /* C99 */{
         struct n_string cnv;

         n_string_creat(&cnv);
         if(!n_idna_to_ascii(&cnv, hn, UZ_MAX))
            n_panic(_("The system hostname is invalid, "
                  "IDNA conversion failed: %s\n"),
               n_shexp_quote_cp(hn, FAL0));
         sys_hostname = n_string_cp(&cnv);
         n_string_drop_ownership(&cnv);
         /*n_string_gut(&cnv);*/
      }
#else
      sys_hostname = su_cs_dup(hn, 0);
#endif

      if(lofi)
         n_lofi_free(hn);
      hn = sys_hostname;
   }

   if(hostname != NULL && hostname != sys_hostname)
      n_free(hostname);
   hostname = su_cs_dup(hn, 0);
   NYD2_OU;
   return hostname;
}

#ifdef mx_HAVE_IDNA
FL boole
n_idna_to_ascii(struct n_string *out, char const *ibuf, uz ilen){
   char *idna_utf8;
   boole lofi, rv;
   NYD_IN;

   if(ilen == UZ_MAX)
      ilen = su_cs_len(ibuf);

   lofi = FAL0;

   if((rv = (ilen == 0)))
      goto jleave;
   if(ibuf[ilen] != '\0'){
      lofi = TRU1;
      idna_utf8 = n_lofi_alloc(ilen +1);
      su_mem_copy(idna_utf8, ibuf, ilen);
      idna_utf8[ilen] = '\0';
      ibuf = idna_utf8;
   }
   ilen = 0;

# ifndef mx_HAVE_ALWAYS_UNICODE_LOCALE
   if(n_psonce & n_PSO_UNICODE)
# endif
      idna_utf8 = n_UNCONST(ibuf);
# ifndef mx_HAVE_ALWAYS_UNICODE_LOCALE
   else if((idna_utf8 = n_iconv_onetime_cp(n_ICONV_NONE, "utf-8",
         ok_vlook(ttycharset), ibuf)) == NULL)
      goto jleave;
# endif

# if mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN2
   /* C99 */{
      char *idna_ascii;
      int f, rc;

      f = IDN2_NONTRANSITIONAL;
jidn2_redo:
      if((rc = idn2_to_ascii_8z(idna_utf8, &idna_ascii, f)) == IDN2_OK){
         out = n_string_assign_cp(out, idna_ascii);
         idn2_free(idna_ascii);
         rv = TRU1;
         ilen = out->s_len;
      }else if(rc == IDN2_DISALLOWED && f != IDN2_TRANSITIONAL){
         f = IDN2_TRANSITIONAL;
         goto jidn2_redo;
      }
   }

# elif mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN
   /* C99 */{
      char *idna_ascii;

      if(idna_to_ascii_8z(idna_utf8, &idna_ascii, 0) == IDNA_SUCCESS){
         out = n_string_assign_cp(out, idna_ascii);
         idn_free(idna_ascii);
         rv = TRU1;
         ilen = out->s_len;
      }
   }

# elif mx_HAVE_IDNA == n_IDNA_IMPL_IDNKIT
   ilen = su_cs_len(idna_utf8);
jredo:
   switch(idn_encodename(
      /* LOCALCONV changed meaning in v2 and is no longer available for
       * encoding.  This makes sense, bu */
         (
#  ifdef IDN_UNICODECONV /* v2 */
         IDN_ENCODE_APP & ~IDN_UNICODECONV
#  else
         IDN_DELIMMAP | IDN_LOCALMAP | IDN_NAMEPREP | IDN_IDNCONV |
         IDN_LENCHECK | IDN_ASCCHECK
#  endif
         ), idna_utf8,
         n_string_resize(n_string_trunc(out, 0), ilen)->s_dat, ilen)){
   case idn_buffer_overflow:
      ilen += HOST_NAME_MAX +1;
      goto jredo;
   case idn_success:
      rv = TRU1;
      ilen = su_cs_len(out->s_dat);
      break;
   default:
      ilen = 0;
      break;
   }

# else
#  error Unknown mx_HAVE_IDNA
# endif
jleave:
   if(lofi)
      n_lofi_free(n_UNCONST(ibuf));
   out = n_string_trunc(out, ilen);
   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_IDNA */

FL boole
n_boolify(char const *inbuf, uz inlen, boole emptyrv){
   boole rv;
   NYD2_IN;
   ASSERT(inlen == 0 || inbuf != NULL);

   if(inlen == UZ_MAX)
      inlen = su_cs_len(inbuf);

   if(inlen == 0)
      rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
   else{
      if((inlen == 1 && (*inbuf == '1' || *inbuf == 'y' || *inbuf == 'Y')) ||
            !su_cs_cmp_case_n(inbuf, "true", inlen) ||
            !su_cs_cmp_case_n(inbuf, "yes", inlen) ||
            !su_cs_cmp_case_n(inbuf, "on", inlen))
         rv = TRU1;
      else if((inlen == 1 &&
               (*inbuf == '0' || *inbuf == 'n' || *inbuf == 'N')) ||
            !su_cs_cmp_case_n(inbuf, "false", inlen) ||
            !su_cs_cmp_case_n(inbuf, "no", inlen) ||
            !su_cs_cmp_case_n(inbuf, "off", inlen))
         rv = FAL0;
      else{
         u64 ib;

         if((su_idec(&ib, inbuf, inlen, 0, 0, NULL) & (su_IDEC_STATE_EMASK |
               su_IDEC_STATE_CONSUMED)) != su_IDEC_STATE_CONSUMED)
            rv = TRUM1;
         else
            rv = (ib != 0);
      }
   }
   NYD2_OU;
   return rv;
}

FL boole
n_quadify(char const *inbuf, uz inlen, char const *prompt, boole emptyrv){
   boole rv;
   NYD2_IN;
   ASSERT(inlen == 0 || inbuf != NULL);

   if(inlen == UZ_MAX)
      inlen = su_cs_len(inbuf);

   if(inlen == 0)
      rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
   else if((rv = n_boolify(inbuf, inlen, emptyrv)) < FAL0 &&
         !su_cs_cmp_case_n(inbuf, "ask-", 4) &&
         (rv = n_boolify(&inbuf[4], inlen - 4, emptyrv)) >= FAL0 &&
         (n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT))
      rv = mx_tty_yesorno(prompt, rv);
   NYD2_OU;
   return rv;
}

FL boole
n_is_all_or_aster(char const *name){
   boole rv;
   NYD2_IN;

   rv = ((name[0] == '*' && name[1] == '\0') || !su_cs_cmp_case(name, "all"));
   NYD2_OU;
   return rv;
}

FL struct su_timespec const *
n_time_now(boole force_update){ /* TODO event loop update IF cmd requests! */
   static struct su_timespec ts_now;
   NYD2_IN;

   if(UNLIKELY(su_state_has(su_STATE_REPRODUCIBLE))){
      /* Guaranteed 32-bit posnum TODO SOURCE_DATE_EPOCH should be 64-bit! */
      (void)su_idec_s64_cp(&ts_now.ts_sec, ok_vlook(SOURCE_DATE_EPOCH), 0,NIL);
      ts_now.ts_nano = 0;
   }else if(force_update || ts_now.ts_sec == 0)
      su_timespec_current(&ts_now);

   /* Just in case.. */
   if(UNLIKELY(ts_now.ts_sec < 0))
      ts_now.ts_sec = 0;

   NYD2_OU;
   return &ts_now;
}

FL void
time_current_update(struct time_current *tc, boole full_update){
   NYD2_IN;

   tc->tc_time = S(time_t,n_time_now(TRU1)->ts_sec);

   if(full_update){
      char *cp;
      struct tm *tmp;
      time_t t;

      t = tc->tc_time;
jredo:
      if((tmp = gmtime(&t)) == NIL){
         t = 0;
         goto jredo;
      }
      su_mem_copy(&tc->tc_gm, tmp, sizeof tc->tc_gm);

      if((tmp = localtime(&t)) == NIL){
         t = 0;
         goto jredo;
      }
      su_mem_copy(&tc->tc_local, tmp, sizeof tc->tc_local);

      cp = su_cs_pcopy(tc->tc_ctime, n_time_ctime((s64)tc->tc_time, tmp));
      *cp++ = '\n';
      *cp = '\0';
      ASSERT(P2UZ(++cp - tc->tc_ctime) < sizeof(tc->tc_ctime));
   }

   NYD2_OU;
}

FL s32
n_time_tzdiff(s64 secsepoch, struct tm const *utcp_or_nil,
      struct tm const *localp_or_nil){
   struct tm tmbuf[2], *tmx;
   time_t t;
   s32 rv;
   NYD2_IN;
   UNUSED(utcp_or_nil);

   rv = 0;

   if(localp_or_nil == NIL){
      t = S(time_t,secsepoch);
      if((tmx = localtime(&t)) == NIL)
         goto jleave;
      tmbuf[0] = *tmx;
      localp_or_nil = &tmbuf[0];
   }

#ifdef mx_HAVE_TM_GMTOFF
   rv = localp_or_nil->tm_gmtoff;

#else
   if(utcp_or_nil == NIL){
      t = S(time_t,secsepoch);
      if((tmx = gmtime(&t)) == NIL)
         goto jleave;
      tmbuf[1] = *tmx;
      utcp_or_nil = &tmbuf[1];
   }

   rv = ((((localp_or_nil->tm_hour - utcp_or_nil->tm_hour) * 60) +
         (localp_or_nil->tm_min - utcp_or_nil->tm_min)) * 60) +
         (localp_or_nil->tm_sec - utcp_or_nil->tm_sec);

   if((t = (localp_or_nil->tm_yday - utcp_or_nil->tm_yday)) != 0)
      rv += (t == 1) ? su_TIME_DAY_SECS : -S(s32,su_TIME_DAY_SECS);
#endif

jleave:
   NYD2_OU;
   return rv;
}

FL char *
n_time_ctime(s64 secsepoch, struct tm const *localtime_or_nil){/* TODO err*/
   /* Problem is that secsepoch may be invalid for representation of ctime(3),
    * which indeed is asctime(localtime(t)); musl libc says for asctime(3):
    *    ISO C requires us to use the above format string,
    *    even if it will not fit in the buffer. Thus asctime_r
    *    is _supposed_ to crash if the fields in tm are too large.
    *    We follow this behavior and crash "gracefully" to warn
    *    application developers that they may not be so lucky
    *    on other implementations (e.g. stack smashing..).
    * So we need to do it on our own or the libc may kill us */
   static char buf[32]; /* TODO static buffer (-> datetime_to_format()) */

   s32 y, md, th, tm, ts;
   char const *wdn, *mn;
   struct tm const *tmp;
   NYD_IN;
   LCTA(FIELD_SIZEOF(struct time_current,tc_ctime) == sizeof(buf),
      "Buffers should have equal size");

   if((tmp = localtime_or_nil) == NIL){
      time_t t;

      t = (time_t)secsepoch;
jredo:
      if((tmp = localtime(&t)) == NIL){
         /* TODO error log */
         t = 0;
         goto jredo;
      }
   }

   if(UNLIKELY((y = tmp->tm_year) < 0 || y >= 9999/*S32_MAX*/ - 1900)){
      y = 1970;
      wdn = su_time_weekday_names_abbrev[su_TIME_WEEKDAY_THURSDAY];
      mn = su_time_month_names_abbrev[su_TIME_MONTH_JANUARY];
      md = 1;
      th = tm = ts = 0;
   }else{
      y += 1900;
      wdn = su_TIME_WEEKDAY_IS_VALID(tmp->tm_wday)
            ? su_time_weekday_names_abbrev[tmp->tm_wday] : n_qm;
      mn = su_TIME_MONTH_IS_VALID(tmp->tm_mon)
            ? su_time_month_names_abbrev[tmp->tm_mon] : n_qm;

      if((md = tmp->tm_mday) < 1 || md > 31)
         md = 1;

      if((th = tmp->tm_hour) < 0 || th > 23)
         th = 0;
      if((tm = tmp->tm_min) < 0 || tm > 59)
         tm = 0;
      if((ts = tmp->tm_sec) < 0 || ts > 60)
         ts = 0;
   }

   (void)snprintf(buf, sizeof buf, "%3s %3s%3d %.2d:%.2d:%.2d %d",
         wdn, mn, md, th, tm, ts, y);

   NYD_OU;
   return buf;
}

FL void
n_err(char const *format, ...){
   va_list ap;
   NYD2_IN;

   va_start(ap, format);
   n_verrx(FAL0, format, ap);
   va_end(ap);
   NYD2_OU;
}

FL void
n_errx(boole allow_multiple, char const *format, ...){
   va_list ap;
   NYD2_IN;

   va_start(ap, format);
   n_verrx(allow_multiple, format, ap);
   va_end(ap);
   NYD2_OU;
}

FL void
n_verr(char const *format, va_list ap){
   NYD2_IN;
   n_verrx(FAL0, format, ap);
   NYD2_OU;
}

FL void
n_verrx(boole allow_multiple, char const *format, va_list ap){/*XXX sigcondom*/
   /* Unhappy: too complicated, too slow; should possibly print repitition
    * count more often, but be aware of n_PS_ERRORS_NEED_PRINT_ONCE docu */
   mx_COLOUR( static uz c5recur; ) /* *termcap* recursion */
#ifdef mx_HAVE_ERRORS
   u32 errlim;
#endif
   struct str s_b, s;
   struct a_aux_err_node *lenp, *enp;
   sz i;
   char const *lpref, *c5pref, *c5suff;
   NYD2_IN;

   mx_COLOUR( ++c5recur; )
   lpref = NIL;
   c5pref = c5suff = su_empty;

   /* Fully expand the buffer (TODO use fmtenc) */
#undef a_X
#ifdef mx_HAVE_N_VA_COPY
# define a_X 128
#else
# define a_X MIN(LINESIZE, 1024)
#endif
   mx_fs_linepool_aquire(&s_b.s, &s_b.l);
   i = 0;
   if(s_b.l < a_X)
      i = s_b.l = a_X;
#undef a_X

   for(;; s_b.l = ++i /* xxx could wrap, maybe */){
#ifdef mx_HAVE_N_VA_COPY
      va_list vac;

      n_va_copy(vac, ap);
#else
# define vac ap
#endif

      if(i != 0)
         s_b.s = su_MEM_REALLOC(s_b.s, s_b.l);

      i = vsnprintf(s_b.s, s_b.l, format, vac);

#ifdef mx_HAVE_N_VA_COPY
      va_end(vac);
#else
# undef vac
#endif

      if(i <= 0)
         goto jleave;
      if(UCMP(z, i, >=, s_b.l)){
#ifdef mx_HAVE_N_VA_COPY
         continue;
#else
         i = S(int,su_cs_len(s_b.s));
#endif
      }
      break;
   }
   s = s_b;
   s.l = S(uz,i);

   /* Remove control characters but \n as we do not makeprint() XXX config */
   /* C99 */{
      char *ins, *curr, *max, c;

      for(ins = curr = s.s, max = &ins[s.l]; curr < max; ++curr)
         if(!su_cs_is_cntrl(c = *curr) || c == '\n')
            *ins++ = c;
      *ins = '\0';
      s.l = P2UZ(ins - s.s);
   }

   /* We have the prepared error message, take it over line-by-line, possibly
    * completing partly prepared one first */
   if(n_pstate & n_PS_ERRORS_NEED_PRINT_ONCE){
      n_pstate ^= n_PS_ERRORS_NEED_PRINT_ONCE;
      allow_multiple = TRU1;
   }

   /* C99 */{
      u32 poption_save;

      poption_save = n_poption; /* XXX sigh */
      n_poption &= ~n_PO_D_V;

      lpref = ok_vlook(log_prefix);

#ifdef mx_HAVE_ERRORS
      su_idec_u32_cp(&errlim, ok_vlook(errors_limit), 0, NIL);
#endif

#ifdef mx_HAVE_COLOUR
      if(c5recur == 1 && (n_psonce & n_PSO_TTYANY)){
         struct str const *pref, *suff;
         struct mx_colour_pen *cp;

         if((cp = mx_colour_get_pen(mx_COLOUR_GET_FORCED,
                  mx_COLOUR_CTX_MLE, mx_COLOUR_ID_MLE_ERROR, NIL)
                  ) != NIL && (pref = mx_colour_pen_get_cseq(cp)) != NIL &&
               (suff = mx_colour_get_reset_cseq(mx_COLOUR_GET_FORCED)
                     ) != NIL){
            c5pref = pref->s;
            c5suff = suff->s;
         }
      }
#endif

      n_poption = poption_save;
   }

   for(i = 0; UCMP(z, i, <, s.l);){
      char *cp;
      boole isdup;

      lenp = enp = a_aux_err_tail;
      if(enp == NIL || enp->ae_done){
         enp = su_TCALLOC(struct a_aux_err_node, 1);
         enp->ae_cnt = 1;
         n_string_creat(&enp->ae_str);

         if(a_aux_err_tail != NIL)
            a_aux_err_tail->ae_next = enp;
         else
            a_aux_err_head = enp;
         a_aux_err_tail = enp;
      }

      /* xxx if(!n_string_book(&enp->ae_str, s.l - i))
       * xxx    goto jleave;*/

      /* We have completed a line? */
      /* C99 */{
         uz oi, j, k;

         oi = S(uz,i);
         j = s.l - oi;
         k = enp->ae_str.s_len;
         cp = S(char*,su_mem_find(&s.s[oi], '\n', j));

         if(cp == NIL){
            n_string_push_buf(&enp->ae_str, &s.s[oi], j);
            i = s.l;
         }else{
            j = P2UZ(cp - &s.s[oi]);
            i += j + 1;
            n_string_push_buf(&enp->ae_str, &s.s[oi], j);
         }

         /* We need to write it out regardless of whether it is a complete line
          * or not, say (for at least `echoerrn') TODO IO errors not handled */
         if(cp == NIL || allow_multiple || !(n_psonce & n_PSO_INTERACTIVE)){
            fprintf(n_stderr, "%s%s%s%s%s",
               c5pref, (enp->ae_dumped_till == 0 ? lpref : su_empty),
               &n_string_cp(&enp->ae_str)[k], c5suff,
               (cp != NIL ? "\n" : su_empty));
            fflush(n_stderr);
            enp->ae_dumped_till = enp->ae_str.s_len;
         }
      }

      if(cp == NIL)
         continue;
      enp->ae_done = TRU1;

      /* Check whether it is identical to the last one dumped, in which case
       * we throw it away and only increment the counter, as syslog would.
       * If not, dump it out, if not already */
      isdup = FAL0;
      if(lenp != NIL){
         if(lenp != enp &&
               lenp->ae_str.s_len == enp->ae_str.s_len &&
               !su_mem_cmp(lenp->ae_str.s_dat, enp->ae_str.s_dat,
                  enp->ae_str.s_len)){
            ++lenp->ae_cnt;
            isdup = TRU1;
         }
         /* Otherwise, if the last error has a count, say so, unless it would
          * soil and intermix display */
         else if(lenp->ae_cnt > 1 && !allow_multiple &&
               (n_psonce & n_PSO_INTERACTIVE)){
            fprintf(n_stderr,
               _("%s%s-- Last message repeated %u times --%s\n"),
               c5pref, lpref, lenp->ae_cnt, c5suff);
            fflush(n_stderr);
         }
      }

      /* When we come here we need to write at least the/a \n! */
      if(!isdup && !allow_multiple && (n_psonce & n_PSO_INTERACTIVE)){
         fprintf(n_stderr, "%s%s%s%s\n",
            c5pref, (enp->ae_dumped_till == 0 ? lpref : su_empty),
            &n_string_cp(&enp->ae_str)[enp->ae_dumped_till], c5suff);
         fflush(n_stderr);
      }

      if(isdup){
         lenp->ae_next = NIL;
         a_aux_err_tail = lenp;
         n_string_gut(&enp->ae_str);
         su_FREE(enp);
         continue;
      }

#ifdef mx_HAVE_ERRORS
      if(n_pstate_err_cnt < errlim){
         ++n_pstate_err_cnt;
         continue;
      }

      ASSERT(a_aux_err_head != NIL);
      lenp = a_aux_err_head;
      if((a_aux_err_head = lenp->ae_next) == NIL)
         a_aux_err_tail = NIL;
#else
      a_aux_err_head = a_aux_err_tail = enp;
#endif
      if(lenp != NIL){
         n_string_gut(&lenp->ae_str);
         su_FREE(lenp);
      }
   }

jleave:
   mx_fs_linepool_release(s_b.s, s_b.l);
   mx_COLOUR( --c5recur; )
   NYD2_OU;
}

FL void
n_err_sighdl(char const *format, ...){ /* TODO sigsafe; obsolete! */
   va_list ap;
   NYD;

   va_start(ap, format);
   vfprintf(n_stderr, format, ap);
   va_end(ap);
   fflush(n_stderr);
}

FL void
n_perr(char const *msg, int errval){
   int e;
   char const *fmt;
   NYD2_IN;

   if(msg == NULL){
      fmt = "%s%s\n";
      msg = n_empty;
   }else
      fmt = "%s: %s\n";

   e = (errval == 0) ? su_err_no() : errval;
   n_errx(FAL0, fmt, msg, su_err_doc(e));
   if(errval == 0)
      su_err_set_no(e);
   NYD2_OU;
}

FL void
n_alert(char const *format, ...){
   va_list ap;
   NYD2_IN;


   n_err((a_aux_err_tail != NIL && !a_aux_err_tail->ae_done)
      ? _("\nAlert: ") : _("Alert: "));

   va_start(ap, format);
   n_verrx(TRU1, format, ap);
   va_end(ap);

   n_errx(TRU1, "\n");
   NYD2_OU;
}

FL void
n_panic(char const *format, ...){
   va_list ap;
   NYD2_IN;

   if(a_aux_err_tail != NIL && !a_aux_err_tail->ae_done){
      a_aux_err_tail->ae_done = TRU1;
      putc('\n', n_stderr);
   }
   fprintf(n_stderr, "%sPanic: ", ok_vlook(log_prefix));

   va_start(ap, format);
   vfprintf(n_stderr, format, ap);
   va_end(ap);

   putc('\n', n_stderr);
   fflush(n_stderr);
   NYD2_OU;
   abort(); /* Was exit(n_EXIT_ERR); for a while, but no */
}

#ifdef mx_HAVE_ERRORS
FL int
c_errors(void *v){
   char **argv = v;
   struct a_aux_err_node *enp;
   NYD_IN;

   if(*argv == NULL)
      goto jlist;
   if(argv[1] != NULL)
      goto jerr;
   if(!su_cs_cmp_case(*argv, "show"))
      goto jlist;
   if(!su_cs_cmp_case(*argv, "clear"))
      goto jclear;
jerr:
   mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("errors"), NIL);
   v = NIL;
jleave:
   NYD_OU;
   return (v == NULL) ? !STOP : !OKAY; /* xxx 1:bad 0:good -- do some */

jlist:{
      FILE *fp;
      uz i;

      if(a_aux_err_head == NIL){
         fprintf(n_stderr, _("The error ring is empty\n"));
         goto jleave;
      }

      if((fp = mx_fs_tmp_open(NIL, "errors", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL)
         fp = n_stdout;

      for(i = 0, enp = a_aux_err_head; enp != NIL; enp = enp->ae_next)
         fprintf(fp, "%4" PRIuZ "/%-3u %s\n",
            ++i, enp->ae_cnt, n_string_cp(&enp->ae_str));

      if(fp != n_stdout){
         page_or_print(fp, 0);

         mx_fs_close(fp);
      }else
         clearerr(fp);
   }
   /* FALLTHRU */

jclear:
   a_aux_err_tail = NIL;
   n_pstate_err_cnt = 0;
   while((enp = a_aux_err_head) != NIL){
      a_aux_err_head = enp->ae_next;
      n_string_gut(&enp->ae_str);
      su_FREE(enp);
   }
   goto jleave;
}
#endif /* mx_HAVE_ERRORS */

FL boole
mx_unxy_dict(char const *cmdname, struct su_cs_dict *dp, void *vp){
   char const **argv, *key;
   boole rv;
   NYD_IN;

   rv = TRU1;
   key = (argv = vp)[0];

   do{
      if(key[1] == '\0' && key[0] == '*'){
         if(dp != NIL)
            su_cs_dict_clear(dp);
      }else if(dp == NIL || !su_cs_dict_remove(dp, key)){
         n_err(_("No such `%s': %s\n"), cmdname, n_shexp_quote_cp(key, FAL0));
         rv = FAL0;
      }
   }while((key = *++argv) != NIL);

   NYD_OU;
   return rv;
}

FL boole
mx_xy_dump_dict(char const *cmdname, struct su_cs_dict *dp,
      struct n_strlist **result, struct n_strlist **tailpp_or_nil,
      struct n_strlist *(*ptf)(char const *cmdname, char const *key,
         void const *dat)){
   struct su_cs_dict_view dv;
   char const **cpp, **xcpp;
   u32 cnt;
   struct n_strlist *resp, *tailp;
   boole rv;
   NYD_IN;

   rv = TRU1;

   resp = *result;
   if(tailpp_or_nil != NIL)
      tailp = *tailpp_or_nil;
   else if((tailp = resp) != NIL)
      for(;; tailp = tailp->sl_next)
         if(tailp->sl_next == NIL)
            break;

   if(dp == NIL || (cnt = su_cs_dict_count(dp)) == 0)
      goto jleave;

   if(n_poption & n_PO_D_V)
      su_cs_dict_statistics(dp);

   /* TODO we need LOFI/AUTOREC TALLOC version which check overflow!!
    * TODO these then could _really_ return NIL... */
   if(U32_MAX / sizeof(*cpp) <= cnt ||
         (cpp = S(char const**,n_autorec_alloc(sizeof(*cpp) * cnt))) == NIL)
      goto jleave;

   xcpp = cpp;
   su_CS_DICT_FOREACH(dp, &dv)
      *xcpp++ = su_cs_dict_view_key(&dv);
   if(cnt > 1)
      /* This works even for case-insensitive keys because cs_dict will store
       * keys in lowercase-normalized versions, then */
      su_sort_shell_vpp(su_S(void const**,cpp), cnt, su_cs_toolbox.tb_compare);

   for(xcpp = cpp; cnt > 0; ++xcpp, --cnt){
      struct n_strlist *slp;

      if((slp = (*ptf)(cmdname, *xcpp, su_cs_dict_lookup(dp, *xcpp))) == NIL)
         continue;
      if(resp == NIL)
         resp = slp;
      else
         tailp->sl_next = slp;
      tailp = slp;
   }

jleave:
   *result = resp;
   if(tailpp_or_nil != NIL)
      *tailpp_or_nil = tailp;

   NYD_OU;
   return rv;
}

FL struct n_strlist *
mx_xy_dump_dict_gen_ptf(char const *cmdname, char const *key, void const *dat){
   /* XXX real strlist + str_to_fmt() */
   char *cp;
   struct n_strlist *slp;
   uz kl, dl, cl;
   char const *kp, *dp;
   NYD2_IN;

   kp = n_shexp_quote_cp(key, TRU1);
   dp = n_shexp_quote_cp(su_S(char const*,dat), TRU1);
   kl = su_cs_len(kp);
   dl = su_cs_len(dp);
   cl = su_cs_len(cmdname);

   slp = n_STRLIST_AUTO_ALLOC(cl + 1 + kl + 1 + dl +1);
   slp->sl_next = NIL;
   cp = slp->sl_dat;
   su_mem_copy(cp, cmdname, cl);
   cp += cl;
   *cp++ = ' ';
   su_mem_copy(cp, kp, kl);
   cp += kl;
   *cp++ = ' ';
   su_mem_copy(cp, dp, dl);
   cp += dl;
   *cp = '\0';
   slp->sl_len = P2UZ(cp - slp->sl_dat);

   NYD2_OU;
   return slp;
}

FL boole
mx_page_or_print_strlist(char const *cmdname, struct n_strlist *slp,
      boole cnt_lines){
   uz lines;
   FILE *fp;
   boole rv;
   NYD_IN;

   rv = TRU1;

   if((fp = mx_fs_tmp_open(NIL, cmdname, (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)
         ) == NIL)
      fp = n_stdout;

   /* Create visual result */
   for(lines = 0; slp != NIL; slp = slp->sl_next){
      if(fputs(slp->sl_dat, fp) == EOF){
         rv = FAL0;
         break;
      }

      if(!cnt_lines){
jputnl:
         if(putc('\n', fp) == EOF){
            rv = FAL0;
            break;
         }
         ++lines;
      }else{
         char *cp;
         boole lastnl;

         for(lastnl = FAL0, cp = slp->sl_dat; *cp != '\0'; ++cp)
            if((lastnl = (*cp == '\n')))
               ++lines;
         if(!lastnl)
            goto jputnl;
      }
   }

   if(rv && lines == 0){
      if(fprintf(fp, _("# `%s': no data available\n"), cmdname) < 0)
         rv = FAL0;
      else
         lines = 1;
   }

   if(fp != n_stdout){
      page_or_print(fp, lines);

      mx_fs_close(fp);
   }else
      clearerr(fp);

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_AUXLILY
/* s-it-mode */
