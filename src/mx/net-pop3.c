/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of net-pop3.h.
 *@ TODO UIDL (as struct message.m_uid, *headline* %U), etc...
 *@ TODO enum okay -> boole
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause
 */
/*
 * Copyright (c) 2002
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#define su_FILE net_pop3
#define mx_SOURCE
#define mx_SOURCE_NET_POP3

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_POP3
#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/compat.h"
#include "mx/cred-auth.h"
#include "mx/cred-md5.h"
#include "mx/cred-oauthbearer.h"
#include "mx/file-streams.h"
#include "mx/mime-enc.h"
#include "mx/net-socket.h"
#include "mx/sigs.h"

#ifdef mx_HAVE_GSSAPI
# include "mx/cred-gssapi.h" /* $(MX_SRCDIR) */
#endif

#include "mx/net-pop3.h"
/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct a_pop3_ctx{
   struct mx_socket *pc_sockp;
   struct mx_cred_ctx pc_cred;
   struct mx_url pc_url;
};

static struct str a_pop3_dat;
static char const *a_pop3_realdat;
static sigjmp_buf a_pop3_jmp;
static n_sighdl_t a_pop3_savealrm;
static s32 a_pop3_keepalive;
static int volatile a_pop3_lock;

/* Perform entire login handshake */
static enum okay a_pop3_login(struct mailbox *mp, struct a_pop3_ctx *pcp);

#ifdef mx_HAVE_MD5
/* APOP: get greeting credential or NIL... */
static char *a_pop3_lookup_apop_timestamp(char const *bp);

/* ...and authenticate */
static enum okay a_pop3_auth_apop(struct mailbox *mp,
      struct a_pop3_ctx const *pcp, char const *ts);
#endif

/* Several (other) authentication methods */
static enum okay a_pop3_auth_plain(struct mailbox *mp,
      struct a_pop3_ctx const *pcp);
static enum okay a_pop3_auth_oauthbearer(struct mailbox *mp,
      struct a_pop3_ctx const *pcp, boole is_xoauth2);
static enum okay a_pop3_auth_external(struct mailbox *mp,
      struct a_pop3_ctx const *pcp);

static void a_pop3_timer_off(void);
static enum okay a_pop3_answer(struct mailbox *mp);
static enum okay a_pop3_finish(struct mailbox *mp);
static void a_pop3_catch(int s);
static void a_pop3_maincatch(int s);
static enum okay a_pop3_noop1(struct mailbox *mp);
static void a_pop3alarm(int s);
static enum okay a_pop3_stat(struct mailbox *mp, off_t *size, int *cnt);
static enum okay a_pop3_list(struct mailbox *mp, int n, uz *size);
static void a_pop3_setptr(struct mailbox *mp, struct a_pop3_ctx const *pcp);
static enum okay a_pop3_get(struct mailbox *mp, struct message *m,
      enum needspec need);
static enum okay a_pop3_exit(struct mailbox *mp);
static enum okay a_pop3_delete(struct mailbox *mp, int n);
static enum okay a_pop3_update(struct mailbox *mp);

#ifdef mx_HAVE_GSSAPI
# include <mx/cred-gssapi.h>
#endif

/* Indirect POP3 I/O */
#define a_POP3_OUT(RV,X,Y,ACTIONSTOP) \
do{\
   if(((RV) = a_pop3_finish(mp)) == STOP){\
      ACTIONSTOP;\
   }\
   if(n_poption & n_PO_D_VV)\
      n_err(">>> %s", X);\
   mp->mb_active |= Y;\
   if(((RV) = mx_socket_write(mp->mb_sock, X)) == STOP){\
      ACTIONSTOP;\
   }\
}while(0)

#define a_POP3_ANSWER(RV,ACTIONSTOP) \
do if(((RV) = a_pop3_answer(mp)) == STOP){\
   ACTIONSTOP;\
}while(0)

static enum okay
a_pop3_login(struct mailbox *mp, struct a_pop3_ctx *pcp){
#ifdef mx_HAVE_MD5
   char *ts;
#endif
   enum okey_xlook_mode oxm;
   enum okay rv;
   NYD_IN;

   oxm = (ok_vlook(v15_compat) != NIL) ? OXM_ALL : OXM_PLAIN | OXM_U_H_P;

   /* Get the greeting, check whether APOP is advertised */
   a_POP3_ANSWER(rv, goto jleave);
#ifdef mx_HAVE_MD5
   ts = (pcp->pc_cred.cc_authtype == mx_CRED_AUTHTYPE_PLAIN)
         ? a_pop3_lookup_apop_timestamp(a_pop3_realdat) : NIL;
#endif

   /* If not yet secured, can we upgrade to TLS? */
#ifdef mx_HAVE_TLS
   if(!(pcp->pc_url.url_flags & mx_URL_TLS_REQUIRED)){
      if(xok_blook(pop3_use_starttls, &pcp->pc_url, oxm)){
         a_POP3_OUT(rv, "STLS" NETNL, MB_COMD, goto jleave);
         a_POP3_ANSWER(rv, goto jleave);
         if(!n_tls_open(&pcp->pc_url, pcp->pc_sockp)){
            rv = STOP;
            goto jleave;
         }
      }else if(pcp->pc_cred.cc_needs_tls){
         n_err(_("POP3 authentication %s needs TLS "
            "(*pop3-use-starttls* set?)\n"),
            pcp->pc_cred.cc_auth);
         rv = STOP;
         goto jleave;
      }
   }
#else
   if(pcp->pc_cred.cc_needs_tls ||
         xok_blook(pop3_use_starttls, &pcp->pc_url, oxm)){
      n_err(_("No TLS support compiled in\n"));
      rv = STOP;
      goto jleave;
   }
#endif

   /* Use the APOP single roundtrip? */
#ifdef mx_HAVE_MD5
   if(ts != NIL && !xok_blook(pop3_no_apop, &pcp->pc_url, oxm)){
      if((rv = a_pop3_auth_apop(mp, pcp, ts)) != OKAY){
         char const *ccp;

# ifdef mx_HAVE_TLS
         if(pcp->pc_sockp->s_use_tls)
            ccp = _("over a TLS encrypted connection");
         else
# endif
            ccp = _("(unfortunely without TLS!)");
         n_err(_("POP3 APOP authentication failed!\n"
            "  Server announced support - please set *pop3-no-apop*,\n"
            "  it enforces plain authentication %s\n"), ccp);
      }
      goto jleave;
   }
#endif

   switch(pcp->pc_cred.cc_authtype){
   case mx_CRED_AUTHTYPE_PLAIN:
      rv = a_pop3_auth_plain(mp, pcp);
      break;
   case mx_CRED_AUTHTYPE_OAUTHBEARER:
   case mx_CRED_AUTHTYPE_XOAUTH2:
      rv = a_pop3_auth_oauthbearer(mp, pcp,
            (pcp->pc_cred.cc_authtype == mx_CRED_AUTHTYPE_XOAUTH2));
      break;
   case mx_CRED_AUTHTYPE_EXTERNAL:
   case mx_CRED_AUTHTYPE_EXTERNANON:
      rv = a_pop3_auth_external(mp, pcp);
      break;
#ifdef mx_HAVE_GSSAPI
   case mx_CRED_AUTHTYPE_GSSAPI:
      if(n_poption & n_PO_D){
         n_err(_(">>> We would perform GSS-API authentication now\n"));
         rv = OKAY;
      }else
         rv = su_CONCAT(su_FILE,_gss)(mp->mb_sock, &pcp->pc_url, &pcp->pc_cred,
               mp) ? OKAY : STOP;
      break;
#endif
   default:
      rv = STOP;
      break;
   }

jleave:
   NYD_OU;
   return rv;
}

#ifdef mx_HAVE_MD5
static char *
a_pop3_lookup_apop_timestamp(char const *bp){
   /* RFC 1939:
    * A POP3 server which implements the APOP command will include
    * a timestamp in its banner greeting.  The syntax of the timestamp
    * corresponds to the "msg-id" in [RFC822]
    * RFC 822:
    * msg-id   = "<" addr-spec ">"
    * addr-spec   = local-part "@" domain */
   char const *cp, *ep;
   uz tl;
   char *rp;
   boole hadat;
   NYD_IN;

   hadat = FAL0;
   rp = NIL;

   if((cp = su_cs_find_c(bp, '<')) == NIL)
      goto jleave;

   /* xxx What about malformed APOP timestamp (<@>) here? */
   for(ep = cp; *ep != '\0'; ++ep){
      if(su_cs_is_space(*ep))
         goto jleave;
      else if(*ep == '@')
         hadat = TRU1;
      else if(*ep == '>'){
         if(!hadat)
            goto jleave;
         break;
      }
   }
   if(*ep != '>')
      goto jleave;

   tl = P2UZ(++ep - cp);
   rp = n_autorec_alloc(tl +1);
   su_mem_copy(rp, cp, tl);
   rp[tl] = '\0';

jleave:
   NYD_OU;
   return rp;
}

static enum okay
a_pop3_auth_apop(struct mailbox *mp, struct a_pop3_ctx const *pcp,
      char const *ts){
   unsigned char digest[mx_MD5_DIGEST_SIZE];
   char hex[mx_MD5_TOHEX_SIZE], *cp;
   mx_md5_t ctx;
   uz i;
   enum okay rv;
   NYD_IN;

   mx_md5_init(&ctx);
   mx_md5_update(&ctx, S(uc*,UNCONST(char*,ts)), su_cs_len(ts));
   mx_md5_update(&ctx, S(uc*,pcp->pc_cred.cc_pass.s), pcp->pc_cred.cc_pass.l);
   mx_md5_final(digest, &ctx);
   mx_md5_tohex(hex, digest);

   rv = STOP;

   i = pcp->pc_cred.cc_user.l;
   cp = n_lofi_alloc(5 + i + 1 + mx_MD5_TOHEX_SIZE + sizeof(NETNL)-1 +1);

   su_mem_copy(cp, "APOP ", 5);
   su_mem_copy(&cp[5], pcp->pc_cred.cc_user.s, i);
   i += 5;
   cp[i++] = ' ';
   su_mem_copy(&cp[i], hex, mx_MD5_TOHEX_SIZE);
   i += mx_MD5_TOHEX_SIZE;
   su_mem_copy(&cp[i], NETNL, sizeof(NETNL));
   a_POP3_OUT(rv, cp, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

   rv = OKAY;
jleave:
   n_lofi_free(cp);
   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_MD5 */

static enum okay
a_pop3_auth_plain(struct mailbox *mp, struct a_pop3_ctx const *pcp){
   char *cp;
   enum okay rv;
   NYD_IN;

   cp = n_lofi_alloc(MAX(pcp->pc_cred.cc_user.l, pcp->pc_cred.cc_pass.l) +
         5 + sizeof(NETNL)-1 +1);

   rv = STOP;

   su_mem_copy(cp, "USER ", 5);
   su_mem_copy(&cp[5], pcp->pc_cred.cc_user.s, pcp->pc_cred.cc_user.l);
   su_mem_copy(&cp[5 + pcp->pc_cred.cc_user.l], NETNL, sizeof(NETNL));
   a_POP3_OUT(rv, cp, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

   su_mem_copy(cp, "PASS ", 5);
   su_mem_copy(&cp[5], pcp->pc_cred.cc_pass.s, pcp->pc_cred.cc_pass.l);
   su_mem_copy(&cp[5 + pcp->pc_cred.cc_pass.l], NETNL, sizeof(NETNL));
   a_POP3_OUT(rv, cp, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

   rv = OKAY;
jleave:
   n_lofi_free(cp);
   NYD_OU;
   return rv;
}

static enum okay
a_pop3_auth_oauthbearer(struct mailbox *mp, struct a_pop3_ctx const *pcp,
      boole is_xoauth2){
   struct str s;
   uz i;
   char const *mech, oa[] = "AUTH OAUTHBEARER ", xoa[] = "AUTH XOAUTH2 ";
   enum okay rv;
   NYD_IN;

   rv = STOP;

   if(!is_xoauth2)
      mech = oa, i = sizeof(oa) -1;
   else
      mech = xoa, i = sizeof(xoa) -1;

   if(!mx_oauthbearer_create_icr(&s, i, &pcp->pc_url, &pcp->pc_cred,
         is_xoauth2))
      goto jleave;

   su_mem_copy(&s.s[0], mech, i);

   a_POP3_OUT(rv, s.s, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

jleave:
   if(s.s != NIL)
      n_lofi_free(s.s);

   NYD_OU;
   return rv;
}

static enum okay
a_pop3_auth_external(struct mailbox *mp, struct a_pop3_ctx const *pcp){
   struct str s;
   char *cp;
   uz cnt;
   enum okay rv;
   NYD_IN;

   rv = STOP;

   /* Calculate required storage */
#define a_MAX \
   (sizeof("AUTH EXTERNAL ") -1 + sizeof(NETNL) -1 +1)

   cnt = 0;
   if(pcp->pc_cred.cc_authtype != mx_CRED_AUTHTYPE_EXTERNANON){
      cnt = pcp->pc_cred.cc_user.l;
      cnt = mx_b64_enc_calc_size(cnt);
   }
   if(cnt >= UZ_MAX - a_MAX){
      n_err(_("Credentials overflow buffer sizes\n"));
      goto j_leave;
   }
   cnt += a_MAX;
#undef a_MAX

   cp = n_lofi_alloc(cnt +1);

   su_mem_copy(cp, NETLINE("AUTH EXTERNAL"),
      sizeof(NETLINE("AUTH EXTERNAL")));
   a_POP3_OUT(rv, cp, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

   cnt = 0;
   if(pcp->pc_cred.cc_authtype != mx_CRED_AUTHTYPE_EXTERNANON){
      s.s = cp;
      mx_b64_enc_buf(&s, pcp->pc_cred.cc_user.s, pcp->pc_cred.cc_user.l,
         mx_B64_BUF);
      cnt = s.l;
   }
   su_mem_copy(&cp[cnt], NETNL, sizeof(NETNL));
   a_POP3_OUT(rv, cp, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

   rv = OKAY;
jleave:
   n_lofi_free(cp);
j_leave:
   NYD_OU;
   return rv;
}

static void
a_pop3_timer_off(void){
   NYD_IN;
   if(a_pop3_keepalive > 0){
      n_pstate &= ~n_PS_SIGALARM;
      alarm(0);
      safe_signal(SIGALRM, a_pop3_savealrm);
   }
   NYD_OU;
}

static enum okay
a_pop3_answer(struct mailbox *mp){
   int i;
   uz blen;
   enum okay rv;
   NYD_IN;

   rv = STOP;
jretry:
   if((i = mx_socket_getline(&a_pop3_dat.s, &a_pop3_dat.l, &blen, mp->mb_sock)
         ) > 0){
      if((mp->mb_active & (MB_COMD | MB_MULT)) == MB_MULT)
         goto jmultiline;

      while(blen > 0 && (a_pop3_dat.s[blen - 1] == NETNL[0] ||
            a_pop3_dat.s[blen - 1] == NETNL[1]))
         a_pop3_dat.s[--blen] = '\0';

      if(n_poption & n_PO_D_VV)
         n_err(">>> SERVER: %s\n", a_pop3_dat.s);

      switch(*a_pop3_dat.s){
      case '+':
         if(blen == 1)
            a_pop3_realdat = su_empty;
         else{
            for(a_pop3_realdat = a_pop3_dat.s;
                  *a_pop3_realdat != '\0' && !su_cs_is_space(*a_pop3_realdat);
                  ++a_pop3_realdat)
               ;
            while(*a_pop3_realdat != '\0' && su_cs_is_space(*a_pop3_realdat))
               ++a_pop3_realdat;
         }
         rv = OKAY;
         mp->mb_active &= ~MB_COMD;
         break;
      case '-':
         rv = STOP;
         mp->mb_active = MB_NONE;
         n_err(_("POP3 error: %s\n"), a_pop3_dat.s);
         break;
      default:
         /* If the answer starts neither with '+' nor with '-', it must be part
          * of a multiline response.  Get lines until a single dot appears */
jmultiline:
         while(a_pop3_dat.s[0] != '.' || a_pop3_dat.s[1] != NETNL[0] ||
               a_pop3_dat.s[2] != NETNL[1] || a_pop3_dat.s[3] != '\0'){
            i = mx_socket_getline(&a_pop3_dat.s, &a_pop3_dat.l, NIL,
                  mp->mb_sock);
            if(i <= 0)
               goto jeof;
         }
         mp->mb_active &= ~MB_MULT;
         if(mp->mb_active != MB_NONE)
            goto jretry;
      }
   }else{
jeof:
      rv = STOP;
      mp->mb_active = MB_NONE;
   }

   NYD_OU;
   return rv;
}

static enum okay
a_pop3_finish(struct mailbox *mp){
   NYD_IN;
   while(mp->mb_sock->s_fd > 0 && mp->mb_active != MB_NONE)
      a_pop3_answer(mp);
   NYD_OU;
   return OKAY; /* XXX ? */
}

static void
a_pop3_catch(int s){
   switch(s){
   case SIGINT:
      /*n_err_sighdl(_("Interrupt during POP3 operation\n"));*/
      interrupts = 2; /* Force "Interrupt" message shall we onintr(0) */
      siglongjmp(a_pop3_jmp, 1);
   case SIGPIPE:
      n_err_sighdl(_("Received SIGPIPE during POP3 operation\n"));
      break;
   }
}

static void
a_pop3_maincatch(int s){
   UNUSED(s);
   if(interrupts == 0)
      n_err_sighdl(_("\n(Interrupt -- one more to abort operation)\n"));
   else{
      interrupts = 1;
      siglongjmp(a_pop3_jmp, 1);
   }
}

static enum okay
a_pop3_noop1(struct mailbox *mp){
   enum okay rv;
   NYD_IN;

   a_POP3_OUT(rv, "NOOP" NETNL, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_OU;
   return rv;
}

static void
a_pop3alarm(int s){
   n_sighdl_t volatile saveint, savepipe;
   UNUSED(s);

   if(a_pop3_lock++ == 0){
      mx_sigs_all_holdx();
      if((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &a_pop3_maincatch);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if(sigsetjmp(a_pop3_jmp, 1)){
         interrupts = 0;
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         goto jbrk;
      }
      if(savepipe != SIG_IGN)
         safe_signal(SIGPIPE, &a_pop3_catch);
      mx_sigs_all_rele();

      if(a_pop3_noop1(&mb) != OKAY){
         safe_signal(SIGINT, saveint);
         safe_signal(SIGPIPE, savepipe);
         goto jleave;
      }
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
   }
jbrk:
   n_pstate |= n_PS_SIGALARM;
   alarm(a_pop3_keepalive);
jleave:
   --a_pop3_lock;
}

static enum okay
a_pop3_stat(struct mailbox *mp, off_t *size, int *cnt){
   char const *cp;
   enum okay rv;
   NYD_IN;

   a_POP3_OUT(rv, "STAT" NETNL, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

   rv = STOP;
   cp = a_pop3_realdat;

   if(*cp != '\0'){
      uz i;

      if(su_idec_uz_cp(&i, cp, 10, &cp) & su_IDEC_STATE_EMASK)
         goto jerr;
      if(i > INT_MAX)
         goto jerr;
      *cnt = S(int,i);

      while(*cp != '\0' && !su_cs_is_space(*cp))
         ++cp;
      while(*cp != '\0' && su_cs_is_space(*cp))
         ++cp;

      if(*cp == '\0')
         goto jerr;
      if(su_idec_uz_cp(&i, cp, 10, NIL) & su_IDEC_STATE_EMASK)
         goto jerr;
      *size = S(off_t,i);
      rv = OKAY;
   }

   if(rv == STOP)
jerr:
      n_err(_("Invalid POP3 STAT response: %s\n"), a_pop3_dat.s);
jleave:
   NYD_OU;
   return rv;
}

static enum okay
a_pop3_list(struct mailbox *mp, int n, uz *size){
   char o[mx_LINESIZE];
   char const *cp;
   enum okay rv;
   NYD_IN;

   snprintf(o, sizeof o, "LIST %d" NETNL, n);
   a_POP3_OUT(rv, o, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);

   cp = a_pop3_realdat;
   while(*cp != '\0' && !su_cs_is_space(*cp))
      ++cp;
   while(*cp != '\0' && su_cs_is_space(*cp))
      ++cp;
   if(*cp != '\0')
      su_idec_uz_cp(size, cp, 10, NIL);

jleave:
   NYD_OU;
   return rv;
}

static void
a_pop3_setptr(struct mailbox *mp, struct a_pop3_ctx const *pcp){
   uz i;
   enum needspec ns;
   NYD_IN;

   message = n_calloc(msgCount + 1, sizeof *message);
   message[msgCount].m_size = 0;
   message[msgCount].m_lines = 0;
   dot = message; /* (Just do it: avoid crash -- shall i now do ointr(0).. */

   for(i = 0; UCMP(z, i, <, msgCount); ++i){
      struct message *m;

      m = &message[i];
      m->m_flag = MVALID | MNEW | MNOFROM | MNEWEST;
      m->m_block = 0;
      m->m_offset = 0;
      m->m_size = m->m_xsize = 0;
   }

   for(i = 0; UCMP(z, i, <, msgCount); ++i)
      if(!a_pop3_list(mp, i + 1, &message[i].m_xsize))
         goto jleave;

   /* Force the load of all messages right now */
   ns = xok_blook(pop3_bulk_load, &pcp->pc_url, OXM_ALL)
         ? NEED_BODY : NEED_HEADER;
   for(i = 0; UCMP(z, i, <, msgCount); ++i)
      if(!a_pop3_get(mp, message + i, ns))
         goto jleave;

   n_autorec_relax_create();
   for(i = 0; UCMP(z, i, <, msgCount); ++i){
      char const *cp;
      struct message *m;

      m = &message[i];

      if((cp = hfield1("status", m)) != NIL)
         while(*cp != '\0'){
            if(*cp == 'R')
               m->m_flag |= MREAD;
            else if(*cp == 'O')
               m->m_flag &= ~MNEW;
            ++cp;
         }

      substdate(m);
      n_autorec_relax_unroll();
   }
   n_autorec_relax_gut();

   setdot(message, FAL0);
jleave:
   NYD_OU;
}

static enum okay
a_pop3_get(struct mailbox *mp, struct message *m, enum needspec volatile need){
   char o[mx_LINESIZE], *line, *lp;
   n_sighdl_t volatile saveint, savepipe;
   uz linesize, linelen, size;
   int number, lines;
   int volatile emptyline;
   off_t offset;
   enum okay volatile rv;
   NYD_IN;

   mx_fs_linepool_aquire(&line, &linesize);
   saveint = savepipe = SIG_IGN;
   number = S(int,P2UZ(m - message + 1));
   emptyline = 0;
   rv = STOP;

   if(mp->mb_sock == NIL || mp->mb_sock->s_fd < 0){
      n_err(_("POP3 connection already closed\n"));
      ++a_pop3_lock;
      goto jleave;
   }

   if(a_pop3_lock++ == 0){
      mx_sigs_all_holdx();
      if((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
         safe_signal(SIGINT, &a_pop3_maincatch);
      savepipe = safe_signal(SIGPIPE, SIG_IGN);
      if(sigsetjmp(a_pop3_jmp, 1))
         goto jleave;
      if(savepipe != SIG_IGN)
         safe_signal(SIGPIPE, &a_pop3_catch);
      mx_sigs_all_rele();
   }

   fseek(mp->mb_otf, 0L, SEEK_END);
   offset = ftell(mp->mb_otf);
jretry:
   switch(need){
   case NEED_HEADER:
      snprintf(o, sizeof o, "TOP %d 0" NETNL, number);
      break;
   case NEED_BODY:
      snprintf(o, sizeof o, "RETR %d" NETNL, number);
      break;
   case NEED_UNSPEC:
      n_panic("net-pop3.c bug\n");
   }
   a_POP3_OUT(rv, o, MB_COMD | MB_MULT, goto jleave);

   if(a_pop3_answer(mp) == STOP){
      if(need == NEED_HEADER){
         /* The TOP POP3 command is optional, so retry with entire message */
         need = NEED_BODY;
         goto jretry;
      }
      goto jleave;
   }

   size = 0;
   lines = 0;
   while(mx_socket_getline(&line, &linesize, &linelen, mp->mb_sock) > 0){
      while(linelen > 0 && (line[linelen - 1] == NETNL[0] ||
            line[linelen - 1] == NETNL[1]))
         line[--linelen] = '\0';

      if(n_poption & n_PO_D_VVV)
         n_err(">>> SERVER: %s\n", line);

      if(line[0] == '.'){
         if(*(lp = &line[1]) == '\0'){
            mp->mb_active &= ~MB_MULT;
            break;
         }
         --linelen;
      }else
         lp = line;

      /* TODO >>
       * Need to mask 'From ' lines. This cannot be done properly
       * since some servers pass them as 'From ' and others as
       * '>From '. Although one could identify the first kind of
       * server in principle, it is not possible to identify the
       * second as '>From ' may also come from a server of the
       * first type as actual data. So do what is absolutely
       * necessary only - mask 'From '.
       *
       * If the line is the first line of the message header, it
       * is likely a real 'From ' line. In this case, it is just
       * ignored since it violates all standards.
       * TODO i have *never* seen the latter?!?!?
       * TODO <<
       */
      /* TODO Since we simply copy over data without doing any transfer
       * TODO encoding reclassification/adjustment we *have* to perform
       * TODO RFC 4155 compliant From_ quoting here TODO REALLY NOT! */
      if(emptyline && is_head(lp, linelen, FAL0)){
         putc('>', mp->mb_otf);
         ++size;
      }
      if(!(emptyline = (linelen == 0)))
         fwrite(lp, 1, linelen, mp->mb_otf);
      putc('\n', mp->mb_otf);
      size += ++linelen;
      ++lines;
   }

   if(!emptyline){
      /* TODO This is very ugly; but some POP3 daemons don't end a
       * TODO message with NETNL NETNL, and we need \n\n for mbox format.
       * TODO That is to say we do it wrong here in order to get it right
       * TODO when send.c stuff or with MBOX handling, even though THIS
       * TODO line is solely a property of the MBOX database format! */
      putc('\n', mp->mb_otf);
      ++size;
      ++lines;
   }

   fflush(mp->mb_otf);

   m->m_size = size;
   m->m_lines = lines;
   m->m_block = mailx_blockof(offset);
   m->m_offset = mailx_offsetof(offset);

   switch(need){
   case NEED_HEADER:
      m->m_content_info |= CI_HAVE_HEADER;
      break;
   case NEED_BODY:
      m->m_content_info |= CI_HAVE_HEADER | CI_HAVE_BODY;
      m->m_xlines = m->m_lines;
      m->m_xsize = m->m_size;
      break;
   case NEED_UNSPEC:
      break;
   }

   rv = OKAY;
jleave:
   mx_fs_linepool_release(line, linesize);
   if(saveint != SIG_IGN)
      safe_signal(SIGINT, saveint);
   if(savepipe != SIG_IGN)
      safe_signal(SIGPIPE, savepipe);
   --a_pop3_lock;
   NYD_OU;
   if(interrupts)
      n_raise(SIGINT);
   return rv;
}

static enum okay
a_pop3_exit(struct mailbox *mp){
   enum okay rv;
   NYD_IN;

   a_POP3_OUT(rv, "QUIT" NETNL, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_OU;
   return rv;
}

static enum okay
a_pop3_delete(struct mailbox *mp, int n){
   char o[mx_LINESIZE];
   enum okay rv;
   NYD_IN;

   snprintf(o, sizeof o, "DELE %d" NETNL, n);
   a_POP3_OUT(rv, o, MB_COMD, goto jleave);
   a_POP3_ANSWER(rv, goto jleave);
jleave:
   NYD_OU;
   return rv;
}

static enum okay
a_pop3_update(struct mailbox *mp){
   struct message *m;
   int dodel, c, gotcha, held;
   NYD_IN;

   if(!(n_pstate & n_PS_EDIT)){
      holdbits();
      c = 0;
      for(m = message; PCMP(m, <, &message[msgCount]); ++m)
         if(m->m_flag & MBOX)
            ++c;
      if(c > 0)
         makembox();
   }

   gotcha = held = 0;
   for(m = message; PCMP(m, <, message + msgCount); ++m){
      if(n_pstate & n_PS_EDIT)
         dodel = m->m_flag & MDELETED;
      else
         dodel = !((m->m_flag & MPRESERVE) || !(m->m_flag & MTOUCH));
      if(dodel){
         a_pop3_delete(mp, P2UZ(m - message + 1));
         ++gotcha;
      }else
         ++held;
   }

   /* C99 */{
      char const *dnq;

      dnq = n_shexp_quote_cp(displayname, FAL0);

      if(gotcha && (n_pstate & n_PS_EDIT)){
         fprintf(n_stdout, _("%s "), dnq);
         fprintf(n_stdout, (ok_blook(bsdcompat) || ok_blook(bsdmsgs))
            ? _("complete\n") : _("updated\n"));
      }else if(held && !(n_pstate & n_PS_EDIT)){
         if(held == 1)
            fprintf(n_stdout, _("Held 1 message in %s\n"), dnq);
         else
            fprintf(n_stdout, _("Held %d messages in %s\n"), held, dnq);
      }
   }
   fflush(n_stdout);

   NYD_OU;
   return OKAY;
}

#ifdef mx_HAVE_GSSAPI
# include <mx/cred-gssapi.h>
#endif

#undef a_POP3_OUT
#undef a_POP3_ANSWER

enum okay
mx_pop3_noop(void){
   n_sighdl_t volatile saveint, savepipe;
   enum okay volatile rv;
   NYD_IN;

   rv = STOP;
   a_pop3_lock = 1;

   mx_sigs_all_holdx();
   if((saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &a_pop3_maincatch);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if(sigsetjmp(a_pop3_jmp, 1) == 0){
      if(savepipe != SIG_IGN)
         safe_signal(SIGPIPE, &a_pop3_catch);
      mx_sigs_all_rele();
      rv = a_pop3_noop1(&mb);
   }
   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);

   a_pop3_lock = 0;
   NYD_OU;
   return rv;
}

int
mx_pop3_setfile(char const *who, char const *server, enum fedit_mode fm){
   struct a_pop3_ctx pc;
   n_sighdl_t saveint, savepipe;
   char const *cp;
   int volatile rv;
   NYD_IN;

   rv = 1;
   if(fm & FEDIT_NEWMAIL)
      goto jleave;
   rv = -1;

   if(!mx_url_parse(&pc.pc_url, CPROTO_POP3, server))
      goto jleave;

   if(!mx_cred_auth_lookup(&pc.pc_cred, &pc.pc_url))
      goto jleave;

   if(!quit(FAL0))
      goto jleave;

   pc.pc_sockp = su_TALLOC(struct mx_socket, 1);
   if(!mx_socket_open(pc.pc_sockp, &pc.pc_url)){
      su_FREE(pc.pc_sockp);
      goto jleave;
   }

   rv = 1;

   if(fm & FEDIT_SYSBOX)
      n_pstate &= ~n_PS_EDIT;
   else
      n_pstate |= n_PS_EDIT;

   if(mb.mb_sock != NIL){
      if(mb.mb_sock->s_fd >= 0)
         mx_socket_close(mb.mb_sock);
      su_FREE(mb.mb_sock);
      mb.mb_sock = NIL;
   }

   if(mb.mb_itf != NIL){
      fclose(mb.mb_itf);
      mb.mb_itf = NIL;
   }
   if(mb.mb_otf != NIL){
      fclose(mb.mb_otf);
      mb.mb_otf = NIL;
   }

   initbox(pc.pc_url.url_p_u_h_p);
   mb.mb_type = MB_VOID;
   a_pop3_lock = 1;

   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if(sigsetjmp(a_pop3_jmp, 1)){
      mb.mb_sock = NIL;
      mx_socket_close(pc.pc_sockp);
      su_FREE(pc.pc_sockp);
      n_err(_("POP3 connection closed\n"));
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);

      a_pop3_lock = 0;
      rv = -1;
      if(interrupts > 0)
         n_raise(SIGINT);
      goto jleave;
   }
   if(saveint != SIG_IGN)
      safe_signal(SIGINT, &a_pop3_catch);
   if(savepipe != SIG_IGN)
      safe_signal(SIGPIPE, &a_pop3_catch);

   if((cp = xok_vlook(pop3_keepalive, &pc.pc_url, OXM_ALL)) != NIL){
      su_idec_s32_cp(&a_pop3_keepalive, cp, 10, NIL);
      if(a_pop3_keepalive > 0){ /* Is a "positive number" */
         n_pstate |= n_PS_SIGALARM;
         a_pop3_savealrm = safe_signal(SIGALRM, a_pop3alarm);
         alarm(a_pop3_keepalive);
      }
   }

   pc.pc_sockp->s_desc = (pc.pc_url.url_flags & mx_URL_TLS_REQUIRED)
         ? "POP3S" : "POP3";
   pc.pc_sockp->s_onclose = &a_pop3_timer_off;
   mb.mb_sock = pc.pc_sockp;

   if(a_pop3_login(&mb, &pc) != OKAY ||
         a_pop3_stat(&mb, &mailsize, &msgCount) != OKAY){
      mb.mb_sock = NIL;
      mx_socket_close(pc.pc_sockp);
      su_FREE(pc.pc_sockp);
      a_pop3_timer_off();

      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      a_pop3_lock = 0;
      goto jleave;
   }

   setmsize(msgCount);
   mb.mb_type = MB_POP3;
   mb.mb_perm = (fm & FEDIT_RDONLY) ? 0 : MB_DELE;
   a_pop3_setptr(&mb, &pc);

   /*if (!(fm & FEDIT_NEWMAIL)) */{
      n_pstate &= ~n_PS_SAW_COMMAND;
      n_pstate |= n_PS_SETFILE_OPENED;
   }

   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   a_pop3_lock = 0;

   if((n_poption & (n_PO_EXISTONLY | n_PO_HEADERLIST)) == n_PO_EXISTONLY){
      rv = (msgCount == 0);
      goto jleave;
   }

   if(!(n_pstate & n_PS_EDIT) && msgCount == 0){
      if(!ok_blook(emptystart))
         n_err(_("No mail for %s at %s\n"), who, pc.pc_url.url_p_eu_h_p);
      goto jleave;
   }

   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

enum okay
mx_pop3_header(struct message *m){
   enum okay rv;
   NYD_IN;

   /* TODO no URL here, no OXM possible; (however it is used in setfile()..) */
   rv = a_pop3_get(&mb, m,
         (ok_blook(pop3_bulk_load) ? NEED_BODY : NEED_HEADER));
   NYD_OU;
   return rv;
}

enum okay
mx_pop3_body(struct message *m){
   enum okay rv;
   NYD_IN;

   rv = a_pop3_get(&mb, m, NEED_BODY);
   NYD_OU;
   return rv;
}

boole
mx_pop3_quit(boole hold_sigs_on){
   n_sighdl_t volatile saveint, savepipe;
   boole rv;
   NYD_IN;

   if(hold_sigs_on)
      rele_sigs();

   rv = FAL0;

   if(mb.mb_sock == NIL || mb.mb_sock->s_fd < 0){
      n_err(_("POP3 connection already closed\n"));
      rv = TRU1;
      goto jleave;
   }

   a_pop3_lock = 1;
   saveint = safe_signal(SIGINT, SIG_IGN);
   savepipe = safe_signal(SIGPIPE, SIG_IGN);
   if(sigsetjmp(a_pop3_jmp, 1)){
      safe_signal(SIGINT, saveint);
      safe_signal(SIGPIPE, savepipe);
      a_pop3_lock = 0;
      interrupts = 0;
      goto jleave;
   }
   if(saveint != SIG_IGN)
      safe_signal(SIGINT, &a_pop3_catch);
   if(savepipe != SIG_IGN)
      safe_signal(SIGPIPE, &a_pop3_catch);

   a_pop3_update(&mb);
   a_pop3_exit(&mb);
   mx_socket_close(mb.mb_sock);
   su_FREE(mb.mb_sock);
   mb.mb_sock = NIL;

   safe_signal(SIGINT, saveint);
   safe_signal(SIGPIPE, savepipe);
   a_pop3_lock = 0;

   rv = TRU1;
jleave:
   if(hold_sigs_on)
      hold_sigs();
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_POP3 */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_NET_POP3
/* s-it-mode */
