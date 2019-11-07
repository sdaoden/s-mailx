/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cred-netrc.h.
 *@ .netrc parser quite loosely based upon NetBSD usr.bin/ftp/
 *@   $NetBSD: ruserpass.c,v 1.33 2007/04/17 05:52:04 lukem Exp $
 *@ TODO With an on_loop_tick_event, trigger cache update once per loop max.
 *@ TODO I.e., unless *netrc-pipe* was set, auto check for updates.
 *
 * Copyright (c) 2014 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#undef su_FILE
#define su_FILE cred_netrc
#define mx_SOURCE
#define mx_SOURCE_CRED_NETRC

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_NETRC
#include <su/cs.h>
#include <su/mem.h>

#include "mx/cmd.h"
#include "mx/child.h"
#include "mx/file-streams.h"
#include "mx/sigs.h"
#include "mx/url.h"

#include "mx/cred-netrc.h"
#include "su/code-in.h"

/* NetBSD usr.bin/ftp/ruserpass.c uses 100 bytes for that, we need four
* concurrently (dummy, host, user, pass), so make it a KB */
#define a_NETRC_TOKEN_MAXLEN (1024u / 4)

enum a_netrc_token{
   a_NETRC_ERROR = -1,
   a_NETRC_NONE = 0,
   a_NETRC_DEFAULT,
   a_NETRC_LOGIN,
   a_NETRC_PASSWORD,
   a_NETRC_ACCOUNT,
   a_NETRC_MACDEF,
   a_NETRC_MACHINE,
   a_NETRC_INPUT
};

struct a_netrc_node{
   struct a_netrc_node *nrc_next;
   struct a_netrc_node *nrc_last_match; /* Match phase, former possible one */
   u32 nrc_mlen; /* Length of machine name */
   u32 nrc_ulen; /* Length of user name */
   u32 nrc_plen; /* Length of password */
   char nrc_dat[VFIELD_SIZE(sizeof(u32))];
};
#define a_NETRC_NODE_ERR R(struct a_netrc_node*,-1)

static struct a_netrc_node *a_netrc_cache;

/* Reverse solidus quote (" and \) v'alue, and return autorec_alloc()ed */
static char *a_netrc_bsd_quote(char const *v);

/* Initialize .netrc cache */
static void a_netrc_init(void);

static enum a_netrc_token a_netrc__token(FILE *fi,
      char buffer[a_NETRC_TOKEN_MAXLEN], boole *nl_last);

/* 0=no match; 1=exact match; -1=wildcard match */
static int a_netrc_match_host(struct a_netrc_node const *nrc,
      struct mx_url const *urlp);

/* */
static boole a_netrc_find_user(struct mx_url *urlp,
      struct a_netrc_node const *nrc);
static boole a_netrc_find_pass(struct mx_url *urlp, boole user_match,
      struct a_netrc_node const *nrc);

static char *
a_netrc_bsd_quote(char const *v){
   char const *cp;
   uz i;
   char c, *rv;
   NYD2_IN;

   for(i = 0, cp = v; (c = *cp) != '\0'; ++i, ++cp)
      if(c == '"' || c == '\\')
         ++i;

   rv = n_autorec_alloc(i +1);

   for(i = 0, cp = v; (c = *cp) != '\0'; rv[i++] = c, ++cp)
      if(c == '"' || c == '\\')
         rv[i++] = '\\';
   rv[i] = '\0';
   NYD2_OU;
   return rv;
}

static void
a_netrc_init(void){
   char buffer[a_NETRC_TOKEN_MAXLEN], host[a_NETRC_TOKEN_MAXLEN],
      user[a_NETRC_TOKEN_MAXLEN], pass[a_NETRC_TOKEN_MAXLEN], *netrc_load;
   struct stat sb;
   FILE * volatile fi;
   enum a_netrc_token t;
   boole volatile ispipe;
   boole seen_default, nl_last;
   struct a_netrc_node * volatile ntail, * volatile nhead, * volatile nrc;
   NYD_IN;

   UNINIT(ntail, NIL);
   nhead = NIL;
   nrc = a_NETRC_NODE_ERR;
   ispipe = FAL0;
   fi = NIL;

   mx_sigs_all_holdx(); /* todo */

   if((netrc_load = ok_vlook(netrc_pipe)) != NIL){
      ispipe = TRU1;
      if((fi = mx_fs_pipe_open(netrc_load, "r", ok_vlook(SHELL), NIL,
            mx_CHILD_FD_NULL)) == NIL){
         n_perr(netrc_load, 0);
         goto j_leave;
      }
   }else{
      if((netrc_load = fexpand(ok_vlook(NETRC), (FEXP_NOPROTO |
            FEXP_LOCAL_FILE | FEXP_NSHELL))) == NIL)
         goto j_leave;

      if((fi = mx_fs_open(netrc_load, "r")) == NIL){
         char const *emsg;

         emsg = su_err_doc(su_err_no());
         n_err(_("Cannot open %s\n"),
            n_shexp_quote_cp(netrc_load, FAL0), emsg);
         goto j_leave;
      }

      /* Be simple and apply rigid (permission) check(s) */
      if(fstat(fileno(fi), &sb) == -1 || !S_ISREG(sb.st_mode) ||
            (sb.st_mode & (S_IRWXG | S_IRWXO))){
         n_err(_("Not a regular file, or accessible by non-user: %s\n"),
            n_shexp_quote_cp(netrc_load, FAL0));
         goto jleave;
      }
   }

   seen_default = FAL0;
   nl_last = TRU1;
   switch((t = a_netrc__token(fi, buffer, &nl_last))){
   case a_NETRC_NONE:
      break;
   default: /* Does not happen (but on error?), keep CC happy */
   case a_NETRC_DEFAULT:
jdef:
      /* We ignore the default entry (require an exact host match), and we
       * also ignore anything after such an entry (faulty syntax) */
      seen_default = TRU1;
      /* FALLTHRU */
   case a_NETRC_MACHINE:
jm_h:
      /* Normalize HOST to lowercase */
      *host = '\0';
      if (!seen_default &&
            (t = a_netrc__token(fi, host, &nl_last)) != a_NETRC_INPUT)
         goto jerr;
      else{
         char *cp;

         for(cp = host; *cp != '\0'; ++cp)
            *cp = su_cs_to_lower(*cp);
      }

      *user = *pass = '\0';
      while((t = a_netrc__token(fi, buffer, &nl_last)) != a_NETRC_NONE &&
            t != a_NETRC_MACHINE && t != a_NETRC_DEFAULT){
         switch(t){
         case a_NETRC_LOGIN:
            if((t = a_netrc__token(fi, user, &nl_last)) != a_NETRC_INPUT)
               goto jerr;
            break;
         case a_NETRC_PASSWORD:
            if((t = a_netrc__token(fi, pass, &nl_last)) != a_NETRC_INPUT)
               goto jerr;
            break;
         case a_NETRC_ACCOUNT:
            if((t = a_netrc__token(fi, buffer, &nl_last)) != a_NETRC_INPUT)
               goto jerr;
            break;
         case a_NETRC_MACDEF:
            if((t = a_netrc__token(fi, buffer, &nl_last)) != a_NETRC_INPUT)
               goto jerr;
            else{
               int i, c;

               for(i = 0; (c = getc(fi)) != EOF;)
                  if(c == '\n'){ /* xxx */
                     /* Do not care about comments here, since we parse until
                      * we have seen two successive newline characters */
                     if(i)
                        break;
                     i = 1;
                  }else
                     i = 0;
            }
            break;
         default:
         case a_NETRC_ERROR:
            goto jerr;
         }
      }

      if(!seen_default && (*user != '\0' || *pass != '\0')){
         struct a_netrc_node *nx;
         uz hl, usrl, pl;

         hl = su_cs_len(host);
         usrl = su_cs_len(user);
         pl = su_cs_len(pass);

         nx = su_ALLOC(VSTRUCT_SIZEOF(struct a_netrc_node, nrc_dat) +
               hl +1 + usrl +1 + pl +1);
         if(nhead != NIL)
            ntail->nrc_next = nx;
         else
            nhead = nx;
         ntail = nx;
         nx->nrc_next = NIL;
         nx->nrc_mlen = hl;
         nx->nrc_ulen = usrl;
         nx->nrc_plen = pl;
         su_mem_copy(nx->nrc_dat, host, ++hl);
         su_mem_copy(&nx->nrc_dat[hl], user, ++usrl);
         su_mem_copy(&nx->nrc_dat[hl + usrl], pass, ++pl);
      }

      if(t == a_NETRC_MACHINE)
         goto jm_h;
      if(t == a_NETRC_DEFAULT)
         goto jdef;
      ASSERT(t == a_NETRC_NONE);
      break;
   case a_NETRC_ERROR:
jerr:
      if(n_poption & n_PO_D_V)
         n_err(_("Errors occurred while parsing %s\n"),
            n_shexp_quote_cp(netrc_load, FAL0));
      ASSERT(nrc == a_NETRC_NODE_ERR);
      goto jleave;
   }

   if(nhead != NIL)
      nrc = nhead;

jleave:
   if(fi != NIL){
      if(ispipe)
         mx_fs_pipe_close(fi, TRU1);
      else
         mx_fs_close(fi);
   }

   if(nrc == a_NETRC_NODE_ERR)
      while(nhead != NIL){
         ntail = nhead;
         nhead = nhead->nrc_next;
         su_FREE(ntail);
      }
j_leave:
   a_netrc_cache = nrc;
   mx_sigs_all_rele();
   NYD_OU;
}

static enum a_netrc_token
a_netrc__token(FILE *fi, char buffer[a_NETRC_TOKEN_MAXLEN], boole *nl_last){
   int c;
   char *cp;
   enum a_netrc_token rv;
   NYD2_IN;

   rv = a_NETRC_NONE;
   for(;;){
      boole seen_nl;

      c = EOF;
      if(feof(fi) || ferror(fi))
         goto jleave;

      for(seen_nl = *nl_last; (c = getc(fi)) != EOF && su_cs_is_white(c);)
         seen_nl |= (c == '\n');

      if(c == EOF)
         goto jleave;
      /* fetchmail and derived parsers support comments */
      if((*nl_last = seen_nl) && c == '#'){
         while((c = getc(fi)) != EOF && c != '\n')
            ;
         continue;
      }
      break;
   }

   cp = buffer;
   /* Is it a quoted token?  At least IBM syntax also supports ' quotes */
   if(c == '"' || c == '\''){
      int quotec;

      quotec = c;
      /* Not requiring the closing QM is (Net)BSD syntax */
      while((c = getc(fi)) != EOF && c != quotec){
         /* Reverse solidus escaping the next character is (Net)BSD syntax */
         if(c == '\\')
            if((c = getc(fi)) == EOF)
               break;
         *cp++ = c;
         if(PCMP(cp, ==, &buffer[a_NETRC_TOKEN_MAXLEN])){
            rv = a_NETRC_ERROR;
            goto jleave;
         }
      }
   }else{
      *cp++ = c;
      while((c = getc(fi)) != EOF && !su_cs_is_white(c)){
         /* Rverse solidus escaping the next character is (Net)BSD syntax */
         if(c == '\\' && (c = getc(fi)) == EOF)
               break;
         *cp++ = c;
         if(PCMP(cp, ==, &buffer[a_NETRC_TOKEN_MAXLEN])){
            rv = a_NETRC_ERROR;
            goto jleave;
         }
      }
      *nl_last = (c == '\n');
   }
   *cp = '\0';

   /* XXX Table-based keyword checking */
   if(*buffer == '\0')
      do {/*rv = a_NETRC_NONE*/} while(0);
   else if(!su_cs_cmp(buffer, "default"))
      rv = a_NETRC_DEFAULT;
   else if(!su_cs_cmp(buffer, "login"))
      rv = a_NETRC_LOGIN;
   else if(!su_cs_cmp(buffer, "password") || !su_cs_cmp(buffer, "passwd"))
      rv = a_NETRC_PASSWORD;
   else if(!su_cs_cmp(buffer, "account"))
      rv = a_NETRC_ACCOUNT;
   else if(!su_cs_cmp(buffer, "macdef"))
      rv = a_NETRC_MACDEF;
   else if(!su_cs_cmp(buffer, "machine"))
      rv = a_NETRC_MACHINE;
   else
      rv = a_NETRC_INPUT;
jleave:
   if(c == EOF && !feof(fi))
      rv = a_NETRC_ERROR;
   NYD2_OU;
   return rv;
}

static int
a_netrc_match_host(struct a_netrc_node const *nrc, struct mx_url const *urlp){
   char const *d2, *d1;
   uz l2, l1;
   int rv = 0;
   NYD2_IN;

   /* Find a matching machine -- entries are all lowercase normalized */
   if(nrc->nrc_mlen == urlp->url_host.l){
      if(LIKELY(!su_mem_cmp(nrc->nrc_dat, urlp->url_host.s, urlp->url_host.l)))
         rv = 1;
      goto jleave;
   }

   /* Cannot be an exact match, but maybe the .netrc machine starts with
    * a "*." glob, which we recognize as an extension, meaning "skip
    * a single subdomain, then match the rest" */
   d1 = &nrc->nrc_dat[2];
   l1 = nrc->nrc_mlen;
   if(l1 <= 2 || d1[-1] != '.' || d1[-2] != '*')
      goto jleave;
   l1 -= 2;

   /* Brute skipping over one subdomain, no RFC 1035 or RFC 1122 checks;
    * in fact this even succeeds for ".host.com", but - why care, here? */
   d2 = urlp->url_host.s;
   l2 = urlp->url_host.l;
   while(l2 > 0){
      --l2;
      if(*d2++ == '.')
         break;
   }

   if(l2 == l1 && !su_mem_cmp(d1, d2, l1))
      /* This matches, but we will not use it directly but watch out for an
       * exact match first! */
      rv = -1;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_netrc_find_user(struct mx_url *urlp, struct a_netrc_node const *nrc){
   NYD2_IN;

   for(; nrc != NIL; nrc = nrc->nrc_last_match)
      if(nrc->nrc_ulen > 0){
         /* Fake it was part of URL otherwise XXX */
         urlp->url_flags |= mx_URL_HAD_USER;
         /* That buffer will be duplicated by url_parse() in this case! */
         urlp->url_user.s = UNCONST(char*,&nrc->nrc_dat[nrc->nrc_mlen +1]);
         urlp->url_user.l = nrc->nrc_ulen;
         break;
      }

   NYD2_OU;
   return (nrc != NIL);
}

static boole
a_netrc_find_pass(struct mx_url *urlp, boole user_match,
      struct a_netrc_node const *nrc){
   NYD2_IN;

   for(; nrc != NIL; nrc = nrc->nrc_last_match){
      boole um;

      um = (nrc->nrc_ulen == urlp->url_user.l &&
            !su_mem_cmp(&nrc->nrc_dat[nrc->nrc_mlen +1], urlp->url_user.s,
               urlp->url_user.l));

      if(user_match){
         if(!um)
            continue;
      }else if(!um && nrc->nrc_ulen > 0)
         continue;
      if(nrc->nrc_plen == 0)
         continue;

      /* We are responsible for duplicating this buffer! */
      urlp->url_pass.s = savestrbuf(&nrc->nrc_dat[nrc->nrc_mlen +1 +
            nrc->nrc_ulen + 1], (urlp->url_pass.l = nrc->nrc_plen));
      break;
   }

   NYD2_OU;
   return (nrc != NIL);
}

int
c_netrc(void *vp){
   struct a_netrc_node *nrc;
   boole load_only;
   char **argv;
   NYD_IN;

   argv = vp;
   load_only = FAL0;

   if(*argv == NIL)
      goto jlist;
   if(argv[1] != NIL)
      goto jerr;
   if(su_cs_starts_with_case("show", *argv))
      goto jlist;
   load_only = TRU1;
   if(su_cs_starts_with_case("load", *argv))
      goto jlist;
   if(su_cs_starts_with_case("clear", *argv))
      goto jclear;
jerr:
   mx_cmd_print_synopsis(mx_cmd_firstfit("netrc"), NIL);
   vp = NIL;
jleave:
   NYD_OU;
   return (vp == NIL ? n_EXIT_ERR : n_EXIT_OK);

jclear:
   if(a_netrc_cache == a_NETRC_NODE_ERR)
      a_netrc_cache = NIL;
   else while((nrc = a_netrc_cache) != NIL){
      a_netrc_cache = nrc->nrc_next;
      su_FREE(nrc);
   }
   goto jleave;

jlist:{
   FILE *fp;
   uz l;

   if(a_netrc_cache == NIL)
      a_netrc_init();
   if(a_netrc_cache == a_NETRC_NODE_ERR){
      n_err(_("Interpolate what file?\n"));
      vp = NIL;
      goto jleave;
   }
   if(load_only)
      goto jleave;

   if((fp = mx_fs_tmp_open("netrc", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
         mx_FS_O_REGISTER), NIL)) == NIL)
      fp = n_stdout;

   for(l = 0, nrc = a_netrc_cache; nrc != NIL; ++l, nrc = nrc->nrc_next){
      fprintf(fp, "machine %s ", nrc->nrc_dat); /* XXX quote? */
      if(nrc->nrc_ulen > 0)
         fprintf(fp, "login \"%s\" ",
            a_netrc_bsd_quote(&nrc->nrc_dat[nrc->nrc_mlen +1]));
      if(nrc->nrc_plen > 0)
         fprintf(fp, "password \"%s\"\n",
            a_netrc_bsd_quote(&nrc->nrc_dat[nrc->nrc_mlen +1 +
               nrc->nrc_ulen +1]));
      else
         putc('\n', fp);
   }

   if(fp != n_stdout){
      page_or_print(fp, l);

      mx_fs_close(fp);
   }else
      clearerr(fp);
   }
   goto jleave;
}

boole
mx_netrc_lookup(struct mx_url *urlp, boole only_pass){
   struct a_netrc_node *nrc, *wild, *exact;
   boole rv;
   NYD_IN;

   rv = FAL0;

   ASSERT_NYD(!only_pass || urlp->url_user.s != NIL);
   ASSERT_NYD(only_pass || urlp->url_user.s == NIL);

   if(a_netrc_cache == NIL)
      a_netrc_init();
   if(a_netrc_cache == a_NETRC_NODE_ERR)
      goto jleave;

   wild = exact = NIL;
   for(nrc = a_netrc_cache; nrc != NIL; nrc = nrc->nrc_next)
      switch(a_netrc_match_host(nrc, urlp)){
      case 1:
         nrc->nrc_last_match = exact;
         exact = nrc;
         continue;
      case -1:
         nrc->nrc_last_match = wild;
         wild = nrc;
         /* FALLTHRU */
      case 0:
         continue;
      }

   if(!only_pass && urlp->url_user.s == NIL){
      /* Must be an unambiguous entry of its kind */
      if(exact != NIL && exact->nrc_last_match != NIL)
         goto jleave;
      if(a_netrc_find_user(urlp, exact))
         goto j_user;

      if(wild != NIL && wild->nrc_last_match != NIL)
         goto jleave;
      if(!a_netrc_find_user(urlp, wild))
         goto jleave;
j_user:;
   }

   if(a_netrc_find_pass(urlp, TRU1, exact) ||
         a_netrc_find_pass(urlp, TRU1, wild) ||
         /* Do not try to find a password without exact user match unless we
          * have been called during credential lookup, aka the second time */
         !only_pass ||
         a_netrc_find_pass(urlp, FAL0, exact) ||
         a_netrc_find_pass(urlp, FAL0, wild))
      rv = TRU1;

jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_NETRC */
/* s-it-mode */
