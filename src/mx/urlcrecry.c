/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ URL parsing, credential handling and crypto hooks.
 *@ .netrc parser quite loosely based upon NetBSD usr.bin/ftp/
 *@   $NetBSD: ruserpass.c,v 1.33 2007/04/17 05:52:04 lukem Exp $
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
#define su_FILE urlcrecry
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>

#ifdef mx_HAVE_NET
# include <su/icodec.h>
#endif

#include "mx/child.h"
#include "mx/file-streams.h"
#include "mx/sigs.h"
#include "mx/tty.h"
#include "mx/ui-str.h"

/* TODO fake */
#include "su/code-in.h"

#ifdef mx_HAVE_NETRC
  /* NetBSD usr.bin/ftp/ruserpass.c uses 100 bytes for that, we need four
   * concurrently (dummy, host, user, pass), so make it a KB */
# define NRC_TOKEN_MAXLEN   (1024 / 4)

enum nrc_token {
   NRC_ERROR      = -1,
   NRC_NONE       = 0,
   NRC_DEFAULT,
   NRC_LOGIN,
   NRC_PASSWORD,
   NRC_ACCOUNT,
   NRC_MACDEF,
   NRC_MACHINE,
   NRC_INPUT
};

struct nrc_node {
   struct nrc_node   *nrc_next;
   struct nrc_node   *nrc_result;   /* In match phase, former possible one */
   u32            nrc_mlen;      /* Length of machine name */
   u32            nrc_ulen;      /* Length of user name */
   u32            nrc_plen;      /* Length of password */
   char              nrc_dat[VFIELD_SIZE(sizeof(u32))];
};
# define NRC_NODE_ERR   ((struct nrc_node*)-1)

static struct nrc_node  *_nrc_list;
#endif /* mx_HAVE_NETRC */

/* Find the last @ before a slash
 * TODO Casts off the const but this is ok here; obsolete function! */
#ifdef mx_HAVE_NET /* temporary (we'll have file://..) */
static char *           _url_last_at_before_slash(char const *cp);
#endif

#ifdef mx_HAVE_NETRC
/* Initialize .netrc cache */
static void             _nrc_init(void);
static enum nrc_token   __nrc_token(FILE *fi, char buffer[NRC_TOKEN_MAXLEN],
                           boole *nl_last);

/* We shall lookup a machine in .netrc says ok_blook(netrc_lookup).
 * only_pass is true then the lookup is for the password only, otherwise we
 * look for a user (and add password only if we have an exact machine match) */
static boole           _nrc_lookup(struct url *urlp, boole only_pass);

/* 0=no match; 1=exact match; -1=wildcard match */
static int              __nrc_host_match(struct nrc_node const *nrc,
                           struct url const *urlp);
static boole           __nrc_find_user(struct url *urlp,
                           struct nrc_node const *nrc);
static boole           __nrc_find_pass(struct url *urlp, boole user_match,
                           struct nrc_node const *nrc);
#endif /* mx_HAVE_NETRC */

#ifdef mx_HAVE_NET
static char *
_url_last_at_before_slash(char const *cp){
   char const *xcp;
   char c;
   NYD2_IN;

   for(xcp = cp; (c = *xcp) != '\0'; ++xcp)
      if(c == '/')
         break;
   while(xcp > cp && *--xcp != '@')
      ;
   if(*xcp != '@')
      xcp = NIL;
   NYD2_OU;
   return UNCONST(char*,xcp);
}
#endif

#ifdef mx_HAVE_NETRC
static void
_nrc_init(void)
{
   char buffer[NRC_TOKEN_MAXLEN], host[NRC_TOKEN_MAXLEN],
      user[NRC_TOKEN_MAXLEN], pass[NRC_TOKEN_MAXLEN], *netrc_load;
   struct stat sb;
   FILE * volatile fi;
   enum nrc_token t;
   boole volatile ispipe;
   boole seen_default, nl_last;
   struct nrc_node * volatile ntail, * volatile nhead, * volatile nrc;
   NYD_IN;

   UNINIT(ntail, NULL);
   nhead = NULL;
   nrc = NRC_NODE_ERR;
   ispipe = FAL0;
   fi = NULL;

   mx_sigs_all_holdx(); /* todo */

   if ((netrc_load = ok_vlook(netrc_pipe)) != NULL) {
      ispipe = TRU1;
      if((fi = mx_fs_pipe_open(netrc_load, "r", ok_vlook(SHELL), NIL,
            mx_CHILD_FD_NULL)) == NIL){
         n_perr(netrc_load, 0);
         goto j_leave;
      }
   } else {
      if ((netrc_load = fexpand(ok_vlook(NETRC), FEXP_LOCAL | FEXP_NOPROTO)
            ) == NULL)
         goto j_leave;

      if((fi = mx_fs_open(netrc_load, "r")) == NIL){
         n_err(_("Cannot open %s\n"), n_shexp_quote_cp(netrc_load, FAL0));
         goto j_leave;
      }

      /* Be simple and apply rigid (permission) check(s) */
      if (fstat(fileno(fi), &sb) == -1 || !S_ISREG(sb.st_mode) ||
            (sb.st_mode & (S_IRWXG | S_IRWXO))) {
         n_err(_("Not a regular file, or accessible by non-user: %s\n"),
            n_shexp_quote_cp(netrc_load, FAL0));
         goto jleave;
      }
   }

   seen_default = FAL0;
   nl_last = TRU1;
jnext:
   switch((t = __nrc_token(fi, buffer, &nl_last))) {
   case NRC_NONE:
      break;
   default: /* Doesn't happen (but on error?), keep CC happy */
   case NRC_DEFAULT:
jdef:
      /* We ignore the default entry (require an exact host match), and we also
       * ignore anything after such an entry (faulty syntax) */
      seen_default = TRU1;
      /* FALLTHRU */
   case NRC_MACHINE:
jm_h:
      /* Normalize HOST to lowercase */
      *host = '\0';
      if (!seen_default && (t = __nrc_token(fi, host, &nl_last)) != NRC_INPUT)
         goto jerr;
      else {
         char *cp;
         for (cp = host; *cp != '\0'; ++cp)
            *cp = su_cs_to_lower(*cp);
      }

      *user = *pass = '\0';
      while ((t = __nrc_token(fi, buffer, &nl_last)) != NRC_NONE &&
            t != NRC_MACHINE && t != NRC_DEFAULT) {
         switch(t) {
         case NRC_LOGIN:
            if ((t = __nrc_token(fi, user, &nl_last)) != NRC_INPUT)
               goto jerr;
            break;
         case NRC_PASSWORD:
            if ((t = __nrc_token(fi, pass, &nl_last)) != NRC_INPUT)
               goto jerr;
            break;
         case NRC_ACCOUNT:
            if ((t = __nrc_token(fi, buffer, &nl_last)) != NRC_INPUT)
               goto jerr;
            break;
         case NRC_MACDEF:
            if ((t = __nrc_token(fi, buffer, &nl_last)) != NRC_INPUT)
               goto jerr;
            else {
               int i = 0, c;
               while ((c = getc(fi)) != EOF)
                  if (c == '\n') { /* xxx */
                     /* Don't care about comments here, since we parse until
                      * we've seen two successive newline characters */
                     if (i)
                        break;
                     i = 1;
                  } else
                     i = 0;
            }
            break;
         default:
         case NRC_ERROR:
            goto jerr;
         }
      }

      if (!seen_default && (*user != '\0' || *pass != '\0')) {
         uz hl = su_cs_len(host), usrl = su_cs_len(user),
            pl = su_cs_len(pass);
         struct nrc_node *nx = n_alloc(VSTRUCT_SIZEOF(struct nrc_node,
               nrc_dat) + hl +1 + usrl +1 + pl +1);

         if (nhead != NULL)
            ntail->nrc_next = nx;
         else
            nhead = nx;
         ntail = nx;
         nx->nrc_next = NULL;
         nx->nrc_mlen = hl;
         nx->nrc_ulen = usrl;
         nx->nrc_plen = pl;
         su_mem_copy(nx->nrc_dat, host, ++hl);
         su_mem_copy(nx->nrc_dat + hl, user, ++usrl);
         su_mem_copy(nx->nrc_dat + hl + usrl, pass, ++pl);
      }
      if (t == NRC_MACHINE)
         goto jm_h;
      if (t == NRC_DEFAULT)
         goto jdef;
      if (t != NRC_NONE)
         goto jnext;
      break;
   case NRC_ERROR:
jerr:
      if(n_poption & n_PO_D_V)
         n_err(_("Errors occurred while parsing %s\n"),
            n_shexp_quote_cp(netrc_load, FAL0));
      ASSERT(nrc == NRC_NODE_ERR);
      goto jleave;
   }

   if (nhead != NULL)
      nrc = nhead;
jleave:
   if(fi != NIL){
      if(ispipe)
         mx_fs_pipe_close(fi, TRU1);
      else
         mx_fs_close(fi);
   }
   if (nrc == NRC_NODE_ERR)
      while (nhead != NULL) {
         ntail = nhead;
         nhead = nhead->nrc_next;
         n_free(ntail);
      }
j_leave:
   _nrc_list = nrc;
   mx_sigs_all_rele();
   NYD_OU;
}

static enum nrc_token
__nrc_token(FILE *fi, char buffer[NRC_TOKEN_MAXLEN], boole *nl_last)
{
   int c;
   char *cp;
   enum nrc_token rv;
   NYD2_IN;

   rv = NRC_NONE;
   for (;;) {
      boole seen_nl;

      c = EOF;
      if (feof(fi) || ferror(fi))
         goto jleave;

      for (seen_nl = *nl_last; (c = getc(fi)) != EOF && su_cs_is_white(c);)
         seen_nl |= (c == '\n');

      if (c == EOF)
         goto jleave;
      /* fetchmail and derived parsers support comments */
      if ((*nl_last = seen_nl) && c == '#') {
         while ((c = getc(fi)) != EOF && c != '\n')
            ;
         continue;
      }
      break;
   }

   cp = buffer;
   /* Is it a quoted token?  At least IBM syntax also supports ' quotes */
   if (c == '"' || c == '\'') {
      int quotec = c;

      /* Not requiring the closing QM is (Net)BSD syntax */
      while ((c = getc(fi)) != EOF && c != quotec) {
         /* Reverse solidus escaping the next character is (Net)BSD syntax */
         if (c == '\\')
            if ((c = getc(fi)) == EOF)
               break;
         *cp++ = c;
         if (PCMP(cp, ==, buffer + NRC_TOKEN_MAXLEN)) {
            rv = NRC_ERROR;
            goto jleave;
         }
      }
   } else {
      *cp++ = c;
      while ((c = getc(fi)) != EOF && !su_cs_is_white(c)) {
         /* Rverse solidus  escaping the next character is (Net)BSD syntax */
         if (c == '\\' && (c = getc(fi)) == EOF)
               break;
         *cp++ = c;
         if (PCMP(cp, ==, buffer + NRC_TOKEN_MAXLEN)) {
            rv = NRC_ERROR;
            goto jleave;
         }
      }
      *nl_last = (c == '\n');
   }
   *cp = '\0';

   if (*buffer == '\0')
      do {/*rv = NRC_NONE*/} while (0);
   else if (!su_cs_cmp(buffer, "default"))
      rv = NRC_DEFAULT;
   else if (!su_cs_cmp(buffer, "login"))
      rv = NRC_LOGIN;
   else if (!su_cs_cmp(buffer, "password") || !su_cs_cmp(buffer, "passwd"))
      rv = NRC_PASSWORD;
   else if (!su_cs_cmp(buffer, "account"))
      rv = NRC_ACCOUNT;
   else if (!su_cs_cmp(buffer, "macdef"))
      rv = NRC_MACDEF;
   else if (!su_cs_cmp(buffer, "machine"))
      rv = NRC_MACHINE;
   else
      rv = NRC_INPUT;
jleave:
   if (c == EOF && !feof(fi))
      rv = NRC_ERROR;
   NYD2_OU;
   return rv;
}

static boole
_nrc_lookup(struct url *urlp, boole only_pass)
{
   struct nrc_node *nrc, *nrc_wild, *nrc_exact;
   boole rv = FAL0;
   NYD_IN;

   ASSERT(!only_pass || urlp->url_user.s != NULL);
   ASSERT(only_pass || urlp->url_user.s == NULL);

   if (_nrc_list == NULL)
      _nrc_init();
   if (_nrc_list == NRC_NODE_ERR)
      goto jleave;

   nrc_wild = nrc_exact = NULL;
   for (nrc = _nrc_list; nrc != NULL; nrc = nrc->nrc_next)
      switch (__nrc_host_match(nrc, urlp)) {
      case 1:
         nrc->nrc_result = nrc_exact;
         nrc_exact = nrc;
         continue;
      case -1:
         nrc->nrc_result = nrc_wild;
         nrc_wild = nrc;
         /* FALLTHRU */
      case 0:
         continue;
      }

   if (!only_pass && urlp->url_user.s == NULL) {
      /* Must be an unambiguous entry of its kind */
      if (nrc_exact != NULL && nrc_exact->nrc_result != NULL)
         goto jleave;
      if (__nrc_find_user(urlp, nrc_exact))
         goto j_user;

      if (nrc_wild != NULL && nrc_wild->nrc_result != NULL)
         goto jleave;
      if (!__nrc_find_user(urlp, nrc_wild))
         goto jleave;
j_user:
      ;
   }

   if (__nrc_find_pass(urlp, TRU1, nrc_exact) ||
         __nrc_find_pass(urlp, TRU1, nrc_wild) ||
         /* Do not try to find a password without exact user match unless we've
          * been called during credential lookup, a.k.a. the second time */
         !only_pass ||
         __nrc_find_pass(urlp, FAL0, nrc_exact) ||
         __nrc_find_pass(urlp, FAL0, nrc_wild))
      rv = TRU1;
jleave:
   NYD_OU;
   return rv;
}

static int
__nrc_host_match(struct nrc_node const *nrc, struct url const *urlp)
{
   char const *d2, *d1;
   uz l2, l1;
   int rv = 0;
   NYD2_IN;

   /* Find a matching machine -- entries are all lowercase normalized */
   if (nrc->nrc_mlen == urlp->url_host.l) {
      if (LIKELY(!su_mem_cmp(nrc->nrc_dat,
            urlp->url_host.s, urlp->url_host.l)))
         rv = 1;
      goto jleave;
   }

   /* Cannot be an exact match, but maybe the .netrc machine starts with
    * a "*." glob, which we recognize as an extension, meaning "skip
    * a single subdomain, then match the rest" */
   d1 = nrc->nrc_dat + 2;
   l1 = nrc->nrc_mlen;
   if (l1 <= 2 || d1[-1] != '.' || d1[-2] != '*')
      goto jleave;
   l1 -= 2;

   /* Brute skipping over one subdomain, no RFC 1035 or RFC 1122 checks;
    * in fact this even succeeds for ".host.com", but - why care, here? */
   d2 = urlp->url_host.s;
   l2 = urlp->url_host.l;
   while (l2 > 0) {
      --l2;
      if (*d2++ == '.')
         break;
   }

   if (l2 == l1 && !su_mem_cmp(d1, d2, l1))
      /* This matches, but we won't use it directly but watch out for an
       * exact match first! */
      rv = -1;
jleave:
   NYD2_OU;
   return rv;
}

static boole
__nrc_find_user(struct url *urlp, struct nrc_node const *nrc)
{
   NYD2_IN;

   for (; nrc != NULL; nrc = nrc->nrc_result)
      if (nrc->nrc_ulen > 0) {
         /* Fake it was part of URL otherwise XXX */
         urlp->url_flags |= n_URL_HAD_USER;
         /* That buffer will be duplicated by url_parse() in this case! */
         urlp->url_user.s = n_UNCONST(nrc->nrc_dat + nrc->nrc_mlen +1);
         urlp->url_user.l = nrc->nrc_ulen;
         break;
      }

   NYD2_OU;
   return (nrc != NULL);
}

static boole
__nrc_find_pass(struct url *urlp, boole user_match, struct nrc_node const *nrc)
{
   NYD2_IN;

   for (; nrc != NULL; nrc = nrc->nrc_result) {
      boole um = (nrc->nrc_ulen == urlp->url_user.l &&
            !su_mem_cmp(nrc->nrc_dat + nrc->nrc_mlen +1, urlp->url_user.s,
               urlp->url_user.l));

      if (user_match) {
         if (!um)
            continue;
      } else if (!um && nrc->nrc_ulen > 0)
         continue;
      if (nrc->nrc_plen == 0)
         continue;

      /* We are responsible for duplicating this buffer! */
      urlp->url_pass.s = savestrbuf(nrc->nrc_dat + nrc->nrc_mlen +1 +
            nrc->nrc_ulen + 1, (urlp->url_pass.l = nrc->nrc_plen));
      break;
   }

   NYD2_OU;
   return (nrc != NULL);
}
#endif /* mx_HAVE_NETRC */

FL char *
(urlxenc)(char const *cp, boole ispath  su_DBG_LOC_ARGS_DECL)
{
   char *n, *np, c1;
   NYD2_IN;

   /* C99 */{
      uz i;

      i = su_cs_len(cp);
      if(i >= UZ_MAX / 3){
         n = NULL;
         goto jleave;
      }
      i *= 3;
      ++i;
      np = n = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(i, su_DBG_LOC_ARGS_ORUSE);
   }

   for (; (c1 = *cp) != '\0'; ++cp) {
      /* (RFC 1738) RFC 3986, 2.3 Unreserved Characters:
       *    ALPHA / DIGIT / "-" / "." / "_" / "~"
       * However add a special is[file]path mode for file-system friendliness */
      if (su_cs_is_alnum(c1) || c1 == '_')
         *np++ = c1;
      else if (!ispath) {
         if (c1 != '-' && c1 != '.' && c1 != '~')
            goto jesc;
         *np++ = c1;
      } else if (PCMP(np, >, n) && (*cp == '-' || *cp == '.')) /* XXX imap */
         *np++ = c1;
      else {
jesc:
         np[0] = '%';
         n_c_to_hex_base16(np + 1, c1);
         np += 3;
      }
   }
   *np = '\0';
jleave:
   NYD2_OU;
   return n;
}

FL char *
(urlxdec)(char const *cp  su_DBG_LOC_ARGS_DECL)
{
   char *n, *np;
   s32 c;
   NYD2_IN;

   np = n = su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(su_cs_len(cp) +1,
         su_DBG_LOC_ARGS_ORUSE);

   while ((c = (uc)*cp++) != '\0') {
      if (c == '%' && cp[0] != '\0' && cp[1] != '\0') {
         s32 o = c;
         if (LIKELY((c = n_c_from_hex_base16(cp)) >= '\0'))
            cp += 2;
         else
            c = o;
      }
      *np++ = (char)c;
   }
   *np = '\0';
   NYD2_OU;
   return n;
}

FL int
c_urlcodec(void *vp){
   boole ispath;
   uz alen;
   char const **argv, *varname, *varres, *act, *cp;
   NYD_IN;

   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;

   act = *argv;
   for(cp = act; *cp != '\0' && !su_cs_is_space(*cp); ++cp)
      ;
   if((ispath = (*act == 'p'))){
      if(!su_cs_cmp_case_n(++act, "ath", 3))
         act += 3;
   }
   if(act >= cp)
      goto jesynopsis;
   alen = P2UZ(cp - act);
   if(*cp != '\0')
      ++cp;

   n_pstate_err_no = su_ERR_NONE;

   if(su_cs_starts_with_case_n("encode", act, alen))
      varres = urlxenc(cp, ispath);
   else if(su_cs_starts_with_case_n("decode", act, alen))
      varres = urlxdec(cp);
   else
      goto jesynopsis;

   if(varres == NULL){
      n_pstate_err_no = su_ERR_CANCELED;
      varres = cp;
      vp = NULL;
   }

   if(varname != NULL){
      if(!n_var_vset(varname, (up)varres)){
         n_pstate_err_no = su_ERR_NOTSUP;
         cp = NULL;
      }
   }else{
      struct str in, out;

      in.l = su_cs_len(in.s = n_UNCONST(varres));
      makeprint(&in, &out);
      if(fprintf(n_stdout, "%s\n", out.s) < 0){
         n_pstate_err_no = su_err_no();
         vp = NULL;
      }
      n_free(out.s);
   }

jleave:
   NYD_OU;
   return (vp != NULL ? 0 : 1);
jesynopsis:
   n_err(_("Synopsis: urlcodec: "
      "<[path]e[ncode]|[path]d[ecode]> <rest-of-line>\n"));
   n_pstate_err_no = su_ERR_INVAL;
   vp = NULL;
   goto jleave;
}

FL int
c_urlencode(void *v) /* XXX IDNA?? */
{
   char **ap;
   NYD_IN;

   n_OBSOLETE("`urlencode': please use `urlcodec enc[ode]' instead");

   for (ap = v; *ap != NULL; ++ap) {
      char *in = *ap, *out = urlxenc(in, FAL0);

      if(out == NULL)
         out = n_UNCONST(V_(n_error));
      fprintf(n_stdout,
         " in: <%s> (%" PRIuZ " bytes)\nout: <%s> (%" PRIuZ " bytes)\n",
         in, su_cs_len(in), out, su_cs_len(out));
   }
   NYD_OU;
   return 0;
}

FL int
c_urldecode(void *v) /* XXX IDNA?? */
{
   char **ap;
   NYD_IN;

   n_OBSOLETE("`urldecode': please use `urlcodec dec[ode]' instead");

   for (ap = v; *ap != NULL; ++ap) {
      char *in = *ap, *out = urlxdec(in);

      if(out == NULL)
         out = n_UNCONST(V_(n_error));
      fprintf(n_stdout,
         " in: <%s> (%" PRIuZ " bytes)\nout: <%s> (%" PRIuZ " bytes)\n",
         in, su_cs_len(in), out, su_cs_len(out));
   }
   NYD_OU;
   return 0;
}

FL char *
url_mailto_to_address(char const *mailtop){ /* TODO hack! RFC 6068; factory? */
   uz i;
   char *rv;
   char const *mailtop_orig;
   NYD_IN;

   if(!su_cs_starts_with(mailtop_orig = mailtop, "mailto:")){
      rv = NULL;
      goto jleave;
   }
   mailtop += sizeof("mailto:") -1;

   /* TODO This is all intermediate, and for now just enough to understand
    * TODO a little bit of a little more advanced List-Post: headers. */
   /* Strip any hfield additions, keep only to addr-spec's */
   if((rv = su_cs_find_c(mailtop, '?')) != NULL)
      rv = savestrbuf(mailtop, i = P2UZ(rv - mailtop));
   else
      rv = savestrbuf(mailtop, i = su_cs_len(mailtop));

   i = su_cs_len(rv);

   /* Simply perform percent-decoding if there is a percent % */
   if(su_mem_find(rv, '%', i) != NULL){
      char *rv_base;
      boole err;

      for(err = FAL0, mailtop = rv_base = rv; i > 0;){
         char c;

         if((c = *mailtop++) == '%'){
            s32 cc;

            if(i < 3 || (cc = n_c_from_hex_base16(mailtop)) < 0){
               if(!err && (err = TRU1, n_poption & n_PO_D_V))
                  n_err(_("Invalid RFC 6068 'mailto' URL: %s\n"),
                     n_shexp_quote_cp(mailtop_orig, FAL0));
               goto jhex_putc;
            }
            *rv++ = (char)cc;
            mailtop += 2;
            i -= 3;
         }else{
jhex_putc:
            *rv++ = c;
            --i;
         }
      }
      *rv = '\0';
      rv = rv_base;
   }
jleave:
   NYD_OU;
   return rv;
}

FL char const *
n_servbyname(char const *proto, u16 *port_or_nil, boole *issnd_or_nil){
   static struct{
      char const name[14];
      char const port[7];
      boole issnd;
      u16 portno;
   } const tbl[] = {
      { "smtp", "25", TRU1, 25},
      { "smtps", "465", TRU1, 465},
      { "submission", "587", TRU1, 587},
      { "submissions", "465", TRU1, 465},
      { "pop3", "110", FAL0, 110},
      { "pop3s", "995", FAL0, 995},
      { "imap", "143", FAL0, 143},
      { "imaps", "993", FAL0, 993},
      { "file", "", TRU1, 0},
      { "test", "", TRU1, U16_MAX}
   };
   char const *rv;
   uz l, i;
   NYD2_IN;

   for(rv = proto; *rv != '\0'; ++rv)
      if(*rv == ':')
         break;
   l = P2UZ(rv - proto);

   for(rv = NIL, i = 0; i < NELEM(tbl); ++i)
      if(!su_cs_cmp_case_n(tbl[i].name, proto, l)){
         rv = tbl[i].port;
         if(port_or_nil != NIL)
            *port_or_nil = tbl[i].portno;
         if(issnd_or_nil != NIL)
            *issnd_or_nil = tbl[i].issnd;
         break;
      }
   NYD2_OU;
   return rv;
}

#ifdef mx_HAVE_NET /* Note: not indented for that -- later: file:// etc.! */
FL boole
url_parse(struct url *urlp, enum cproto cproto, char const *data)
{
#if defined mx_HAVE_SMTP && defined mx_HAVE_POP3 && defined mx_HAVE_IMAP
# define a_ALLPROTO
#endif
#if defined mx_HAVE_SMTP || defined mx_HAVE_POP3 || defined mx_HAVE_IMAP || \
      defined mx_HAVE_TLS
# define a_ANYPROTO
   char *cp, *x;
#endif
   boole rv = FAL0;
   NYD_IN;
   UNUSED(data);

   su_mem_set(urlp, 0, sizeof *urlp);
   urlp->url_input = data;
   urlp->url_cproto = cproto;

   /* Network protocol */
#define a_PROTOX(X,Y,Z) \
   urlp->url_portno = Y;\
   su_mem_copy(urlp->url_proto, X "://\0", sizeof(X "://\0"));\
   urlp->url_proto[sizeof(X) -1] = '\0';\
   urlp->url_proto_len = sizeof(X) -1;\
   do{ Z; }while(0)
#define a_PRIVPROTOX(X,Y,Z) \
   do{ a_PROTOX(X, Y, Z); }while(0)
#define a__IF(X,Y,Z)  \
   if(!su_cs_cmp_case_n(data, X "://", sizeof(X "://") -1)){\
      a_PROTOX(X, Y, Z);\
      data += sizeof(X "://") -1;\
      goto juser;\
   }
#define a_IF(X,Y) a__IF(X, Y, (void)0)
#ifdef mx_HAVE_TLS
# define a_IFS(X,Y) a__IF(X, Y, urlp->url_flags |= n_URL_TLS_REQUIRED)
# define a_IFs(X,Y) a__IF(X, Y, urlp->url_flags |= n_URL_TLS_OPTIONAL)
#else
# define a_IFS(X,Y) goto jeproto;
# define a_IFs(X,Y) a_IF(X, Y)
#endif

   switch(cproto){
   case CPROTO_CERTINFO:
      /* The special `tls' certificate info protocol
       * We do allow all protos here, for later getaddrinfo() usage! */
#ifdef mx_HAVE_TLS
      if((cp = su_cs_find(data, "://")) == NULL)
         a_PRIVPROTOX("https", 443, urlp->url_flags |= n_URL_TLS_REQUIRED);
      else{
         uz i;

         if((i = P2UZ(&cp[sizeof("://") -1] - data)) + 2 >=
               sizeof(urlp->url_proto))
            goto jeproto;
         su_mem_copy(urlp->url_proto, data, i);
         data += i;
         i -= sizeof("://") -1;
         urlp->url_proto[i] = '\0';\
         urlp->url_proto_len = i;
         urlp->url_flags |= n_URL_TLS_REQUIRED;
      }
      break;
#else
      goto jeproto;
#endif
   case CPROTO_CCRED:
      /* The special S/MIME etc. credential lookup TODO TLS client cert! */
#ifdef mx_HAVE_TLS
      a_PRIVPROTOX("ccred", 0, (void)0);
      break;
#else
      goto jeproto;
#endif
   case CPROTO_SOCKS:
      a_IF("socks5", 1080);
      a_IF("socks", 1080);
      a_PROTOX("socks", 1080, (void)0);
      break;
   case CPROTO_SMTP:
#ifdef mx_HAVE_SMTP
      a_IFS("smtps", 465)
      a_IFs("smtp", 25)
      a_IFs("submission", 587)
      a_IFS("submissions", 465)
      a_PROTOX("smtp", 25, urlp->url_flags |= n_URL_TLS_OPTIONAL);
      break;
#else
      goto jeproto;
#endif
   case CPROTO_POP3:
#ifdef mx_HAVE_POP3
      a_IFS("pop3s", 995)
      a_IFs("pop3", 110)
      a_PROTOX("pop3", 110, urlp->url_flags |= n_URL_TLS_OPTIONAL);
      break;
#else
      goto jeproto;
#endif
#ifdef mx_HAVE_IMAP
   case CPROTO_IMAP:
      a_IFS("imaps", 993)
      a_IFs("imap", 143)
      a_PROTOX("imap", 143, urlp->url_flags |= n_URL_TLS_OPTIONAL);
      break;
#else
      goto jeproto;
#endif
   }

#undef a_PRIVPROTOX
#undef a_PROTOX
#undef a__IF
#undef a_IF
#undef a_IFS
#undef a_IFs

   if (su_cs_find(data, "://") != NULL) {
jeproto:
      n_err(_("URL proto:// invalid (protocol or TLS support missing?): %s\n"),
         urlp->url_input);
      goto jleave;
   }
#ifdef a_ANYPROTO

   /* User and password, I */
juser:
   if ((cp = _url_last_at_before_slash(data)) != NULL) {
      uz l;
      char const *urlpe, *d;
      char *ub;

      l = P2UZ(cp - data);
      ub = n_lofi_alloc(l +1);
      d = data;
      urlp->url_flags |= n_URL_HAD_USER;
      data = &cp[1];

      /* And also have a password? */
      if((cp = su_mem_find(d, ':', l)) != NULL){
         uz i = P2UZ(cp - d);

         l -= i + 1;
         su_mem_copy(ub, cp + 1, l);
         ub[l] = '\0';

         if((urlp->url_pass.s = urlxdec(ub)) == NULL)
            goto jurlp_err;
         urlp->url_pass.l = su_cs_len(urlp->url_pass.s);
         if((urlpe = urlxenc(urlp->url_pass.s, FAL0)) == NULL)
            goto jurlp_err;
         if(su_cs_cmp(ub, urlpe))
            goto jurlp_err;
         l = i;
      }

      su_mem_copy(ub, d, l);
      ub[l] = '\0';
      if((urlp->url_user.s = urlxdec(ub)) == NULL)
         goto jurlp_err;
      urlp->url_user.l = su_cs_len(urlp->url_user.s);
      if((urlp->url_user_enc.s = urlxenc(urlp->url_user.s, FAL0)) == NULL)
         goto jurlp_err;
      urlp->url_user_enc.l = su_cs_len(urlp->url_user_enc.s);

      if(urlp->url_user_enc.l != l || su_mem_cmp(urlp->url_user_enc.s, ub, l)){
jurlp_err:
         n_err(_("String is not properly URL percent encoded: %s\n"), ub);
         d = NULL;
      }

      n_lofi_free(ub);
      if(d == NULL)
         goto jleave;
   }

   /* Servername and port -- and possible path suffix */
   if ((cp = su_cs_find_c(data, ':')) != NULL) { /* TODO URL: use IPAddress! */
      urlp->url_port = x = savestr(x = &cp[1]);
      if ((x = su_cs_find_c(x, '/')) != NULL) {
         *x = '\0';
         while(*++x == '/')
            ;
      }

      if((su_idec_u16_cp(&urlp->url_portno, urlp->url_port, 10, NULL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || urlp->url_portno == 0){
         n_err(_("URL with invalid port number: %s\n"), urlp->url_input);
         goto jleave;
      }
   } else {
      if ((x = su_cs_find_c(data, '/')) != NULL) {
         data = savestrbuf(data, P2UZ(x - data));
         while(*++x == '/')
            ;
      }
      cp = n_UNCONST(data + su_cs_len(data));
   }

   /* A (non-empty) path may only occur with IMAP */
   if (x != NULL && *x != '\0') {
      /* Take care not to count adjacent solidus for real, on either end */
      char *x2;
      uz i;
      boole trailsol;

      for(trailsol = FAL0, x2 = savestrbuf(x, i = su_cs_len(x)); i > 0;
            trailsol = TRU1, --i)
         if(x2[i - 1] != '/')
            break;
      x2[i] = '\0';

      if (i > 0) {
         if (cproto != CPROTO_IMAP) {
            n_err(_("URL protocol doesn't support paths: \"%s\"\n"),
               urlp->url_input);
            goto jleave;
         }
# ifdef mx_HAVE_IMAP
         if(trailsol){
            urlp->url_path.s = n_autorec_alloc(i + sizeof("/INBOX"));
            su_mem_copy(urlp->url_path.s, x, i);
            su_mem_copy(&urlp->url_path.s[i], "/INBOX", sizeof("/INBOX"));
            urlp->url_path.l = (i += sizeof("/INBOX") -1);
         }else
# endif
            urlp->url_path.l = i, urlp->url_path.s = x2;
      }
   }
# ifdef mx_HAVE_IMAP
   if(cproto == CPROTO_IMAP && urlp->url_path.s == NULL)
      urlp->url_path.s = savestrbuf("INBOX",
            urlp->url_path.l = sizeof("INBOX") -1);
# endif

   urlp->url_host.s = savestrbuf(data, urlp->url_host.l = P2UZ(cp - data));
   {  uz i;
      for (cp = urlp->url_host.s, i = urlp->url_host.l; i != 0; ++cp, --i)
         *cp = su_cs_to_lower(*cp);
   }
# ifdef mx_HAVE_IDNA
   if(!ok_blook(idna_disable)){
      struct n_string idna;

      if(!n_idna_to_ascii(n_string_creat_auto(&idna), urlp->url_host.s,
               urlp->url_host.l)){
         n_err(_("URL host fails IDNA conversion: %s\n"), urlp->url_input);
         goto jleave;
      }
      urlp->url_host.s = n_string_cp(&idna);
      urlp->url_host.l = idna.s_len;
   }
# endif /* mx_HAVE_IDNA */

   /* .url_h_p: HOST:PORT */
   {  uz upl, i;
      struct str *s = &urlp->url_h_p;

      upl = (urlp->url_port == NULL) ? 0 : 1u + su_cs_len(urlp->url_port);
      s->s = n_autorec_alloc(urlp->url_host.l + upl +1);
      su_mem_copy(s->s, urlp->url_host.s, i = urlp->url_host.l);
      if(upl > 0){
         s->s[i++] = ':';
         su_mem_copy(&s->s[i], urlp->url_port, --upl);
         i += upl;
      }
      s->s[s->l = i] = '\0';
   }

   /* User, II
    * If there was no user in the URL, do we have *user-HOST* or *user*? */
   if (!(urlp->url_flags & n_URL_HAD_USER)) {
      if ((urlp->url_user.s = xok_vlook(user, urlp, OXM_PLAIN | OXM_H_P))
            == NULL) {
         /* No, check whether .netrc lookup is desired */
# ifdef mx_HAVE_NETRC
         if (ok_vlook(v15_compat) == su_NIL ||
               !xok_blook(netrc_lookup, urlp, OXM_PLAIN | OXM_H_P) ||
               !_nrc_lookup(urlp, FAL0))
# endif
            urlp->url_user.s = n_UNCONST(ok_vlook(LOGNAME));
      }

      urlp->url_user.l = su_cs_len(urlp->url_user.s);
      urlp->url_user.s = savestrbuf(urlp->url_user.s, urlp->url_user.l);
      if((urlp->url_user_enc.s = urlxenc(urlp->url_user.s, FAL0)) == NULL){
         n_err(_("Cannot URL encode %s\n"), urlp->url_user.s);
         goto jleave;
      }
      urlp->url_user_enc.l = su_cs_len(urlp->url_user_enc.s);
   }

   /* And then there are a lot of prebuild string combinations TODO do lazy */

   /* .url_u_h: .url_user@.url_host
    * For SMTP we apply ridiculously complicated *v15-compat* plus
    * *smtp-hostname* / *hostname* dependent rules */
   {  struct str h, *s;
      uz i;

      if (cproto == CPROTO_SMTP && ok_vlook(v15_compat) != su_NIL &&
            (cp = ok_vlook(smtp_hostname)) != NULL) {
         if (*cp == '\0')
            cp = n_nodename(TRU1);
         h.s = savestrbuf(cp, h.l = su_cs_len(cp));
      } else
         h = urlp->url_host;

      s = &urlp->url_u_h;
      i = urlp->url_user.l;

      s->s = n_autorec_alloc(i + 1 + h.l +1);
      if (i > 0) {
         su_mem_copy(s->s, urlp->url_user.s, i);
         s->s[i++] = '@';
      }
      su_mem_copy(s->s + i, h.s, h.l +1);
      i += h.l;
      s->l = i;
   }

   /* .url_u_h_p: .url_user@.url_host[:.url_port] */
   {  struct str *s = &urlp->url_u_h_p;
      uz i = urlp->url_user.l;

      s->s = n_autorec_alloc(i + 1 + urlp->url_h_p.l +1);
      if (i > 0) {
         su_mem_copy(s->s, urlp->url_user.s, i);
         s->s[i++] = '@';
      }
      su_mem_copy(s->s + i, urlp->url_h_p.s, urlp->url_h_p.l +1);
      i += urlp->url_h_p.l;
      s->l = i;
   }

   /* .url_eu_h_p: .url_user_enc@.url_host[:.url_port] */
   {  struct str *s = &urlp->url_eu_h_p;
      uz i = urlp->url_user_enc.l;

      s->s = n_autorec_alloc(i + 1 + urlp->url_h_p.l +1);
      if (i > 0) {
         su_mem_copy(s->s, urlp->url_user_enc.s, i);
         s->s[i++] = '@';
      }
      su_mem_copy(s->s + i, urlp->url_h_p.s, urlp->url_h_p.l +1);
      i += urlp->url_h_p.l;
      s->l = i;
   }

   /* .url_p_u_h_p: .url_proto://.url_u_h_p */
   {  uz i;
      char *ud;

      ud = n_autorec_alloc((i = urlp->url_proto_len + sizeof("://") -1 +
            urlp->url_u_h_p.l) +1);
      urlp->url_proto[urlp->url_proto_len] = ':';
      su_mem_copy(su_cs_pcopy(ud, urlp->url_proto), urlp->url_u_h_p.s,
         urlp->url_u_h_p.l +1);
      urlp->url_proto[urlp->url_proto_len] = '\0';

      urlp->url_p_u_h_p = ud;
   }

   /* .url_p_eu_h_p, .url_p_eu_h_p_p: .url_proto://.url_eu_h_p[/.url_path] */
   {  uz i;
      char *ud;

      ud = n_autorec_alloc((i = urlp->url_proto_len + sizeof("://") -1 +
            urlp->url_eu_h_p.l) + 1 + urlp->url_path.l +1);
      urlp->url_proto[urlp->url_proto_len] = ':';
      su_mem_copy(su_cs_pcopy(ud, urlp->url_proto), urlp->url_eu_h_p.s,
         urlp->url_eu_h_p.l +1);
      urlp->url_proto[urlp->url_proto_len] = '\0';

      if (urlp->url_path.l == 0)
         urlp->url_p_eu_h_p = urlp->url_p_eu_h_p_p = ud;
      else {
         urlp->url_p_eu_h_p = savestrbuf(ud, i);
         urlp->url_p_eu_h_p_p = ud;
         ud += i;
         *ud++ = '/';
         su_mem_copy(ud, urlp->url_path.s, urlp->url_path.l +1);
      }
   }

   rv = TRU1;
#endif /* a_ANYPROTO */
jleave:
   NYD_OU;
   return rv;
#undef a_ANYPROTO
#undef a_ALLPROTO
}

FL boole
ccred_lookup_old(struct ccred *ccp, enum cproto cproto, char const *addr)
{
   char const *pname, *pxstr, *authdef, *s;
   uz pxlen, addrlen, i;
   char *vbuf;
   u8 authmask;
   enum {NONE=0, WANT_PASS=1<<0, REQ_PASS=1<<1, WANT_USER=1<<2, REQ_USER=1<<3}
      ware = NONE;
   boole addr_is_nuser = FAL0; /* XXX v15.0 legacy! v15_compat */
   NYD_IN;

   n_OBSOLETE(_("Use of old-style credentials, which will vanish in v15!\n"
      "  Please read the manual section "
         "\"On URL syntax and credential lookup\""));

   su_mem_set(ccp, 0, sizeof *ccp);

   switch (cproto) {
   default:
   case CPROTO_SMTP:
      pname = "SMTP";
      pxstr = "smtp-auth";
      pxlen = sizeof("smtp-auth") -1;
      authmask = AUTHTYPE_NONE | AUTHTYPE_PLAIN | AUTHTYPE_LOGIN |
            AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "none";
      addr_is_nuser = TRU1;
      break;
   case CPROTO_POP3:
      pname = "POP3";
      pxstr = "pop3-auth";
      pxlen = sizeof("pop3-auth") -1;
      authmask = AUTHTYPE_PLAIN;
      authdef = "plain";
      break;
#ifdef mx_HAVE_IMAP
   case CPROTO_IMAP:
      pname = "IMAP";
      pxstr = "imap-auth";
      pxlen = sizeof("imap-auth") -1;
      authmask = AUTHTYPE_LOGIN | AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "login";
      break;
#endif
   }

   ccp->cc_cproto = cproto;
   addrlen = su_cs_len(addr);
   vbuf = n_lofi_alloc(pxlen + addrlen + sizeof("-password-")-1 +1);
   su_mem_copy(vbuf, pxstr, pxlen);

   /* Authentication type */
   vbuf[pxlen] = '-';
   su_mem_copy(vbuf + pxlen + 1, addr, addrlen +1);
   if ((s = n_var_vlook(vbuf, FAL0)) == NULL) {
      vbuf[pxlen] = '\0';
      if ((s = n_var_vlook(vbuf, FAL0)) == NULL)
         s = n_UNCONST(authdef);
   }

   if (!su_cs_cmp_case(s, "none")) {
      ccp->cc_auth = "NONE";
      ccp->cc_authtype = AUTHTYPE_NONE;
      /*ware = NONE;*/
   } else if (!su_cs_cmp_case(s, "plain")) {
      ccp->cc_auth = "PLAIN";
      ccp->cc_authtype = AUTHTYPE_PLAIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "login")) {
      ccp->cc_auth = "LOGIN";
      ccp->cc_authtype = AUTHTYPE_LOGIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "cram-md5")) {
      ccp->cc_auth = "CRAM-MD5";
      ccp->cc_authtype = AUTHTYPE_CRAM_MD5;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "gssapi")) {
      ccp->cc_auth = "GSS-API";
      ccp->cc_authtype = AUTHTYPE_GSSAPI;
      ware = REQ_USER;
   } /* no else */

   /* Verify method */
   if (!(ccp->cc_authtype & authmask)) {
      n_err(_("Unsupported %s authentication method: %s\n"), pname, s);
      ccp = NULL;
      goto jleave;
   }
# ifndef mx_HAVE_MD5
   if (ccp->cc_authtype == AUTHTYPE_CRAM_MD5) {
      n_err(_("No CRAM-MD5 support compiled in\n"));
      ccp = NULL;
      goto jleave;
   }
# endif
# ifndef mx_HAVE_GSSAPI
   if (ccp->cc_authtype == AUTHTYPE_GSSAPI) {
      n_err(_("No GSS-API support compiled in\n"));
      ccp = NULL;
      goto jleave;
   }
# endif

   /* User name */
   if (!(ware & (WANT_USER | REQ_USER)))
      goto jpass;

   if (!addr_is_nuser) {
      if ((s = _url_last_at_before_slash(addr)) != NULL) {
         char *cp;

         cp = savestrbuf(addr, P2UZ(s - addr));

         if((ccp->cc_user.s = urlxdec(cp)) == NULL){
            n_err(_("String is not properly URL percent encoded: %s\n"), cp);
            ccp = NULL;
            goto jleave;
         }
         ccp->cc_user.l = su_cs_len(ccp->cc_user.s);
      } else if (ware & REQ_USER)
         goto jgetuser;
      goto jpass;
   }

   su_mem_copy(vbuf + pxlen, "-user-", i = sizeof("-user-") -1);
   i += pxlen;
   su_mem_copy(vbuf + i, addr, addrlen +1);
   if ((s = n_var_vlook(vbuf, FAL0)) == NULL) {
      vbuf[--i] = '\0';
      if ((s = n_var_vlook(vbuf, FAL0)) == NULL && (ware & REQ_USER)) {
         if((s = mx_tty_getuser(NIL)) == NIL){
jgetuser:   /* TODO v15.0: today we simply bail, but we should call getuser().
             * TODO even better: introduce "PROTO-user" and "PROTO-pass" and
             * TODO check that first, then! change control flow, grow vbuf */
            n_err(_("A user is necessary for %s authentication\n"), pname);
            ccp = NULL;
            goto jleave;
         }
      }
   }
   ccp->cc_user.l = su_cs_len(ccp->cc_user.s = savestr(s));

   /* Password */
jpass:
   if (!(ware & (WANT_PASS | REQ_PASS)))
      goto jleave;

   if (!addr_is_nuser) {
      su_mem_copy(vbuf, "password-", i = sizeof("password-") -1);
   } else {
      su_mem_copy(vbuf + pxlen, "-password-", i = sizeof("-password-") -1);
      i += pxlen;
   }
   su_mem_copy(vbuf + i, addr, addrlen +1);
   if ((s = n_var_vlook(vbuf, FAL0)) == NULL) {
      vbuf[--i] = '\0';
      if ((!addr_is_nuser || (s = n_var_vlook(vbuf, FAL0)) == NULL) &&
            (ware & REQ_PASS)) {
         if((s = mx_tty_getpass(savecat(_("Password for "), pname))) != NIL){
         }else{
            n_err(_("A password is necessary for %s authentication\n"),
               pname);
            ccp = NIL;
            goto jleave;
         }
      }
   }
   if (s != NULL)
      ccp->cc_pass.l = su_cs_len(ccp->cc_pass.s = savestr(s));

jleave:
   n_lofi_free(vbuf);
   if (ccp != NULL && (n_poption & n_PO_D_VV))
      n_err(_("Credentials: host %s, user %s, pass %s\n"),
         addr, (ccp->cc_user.s != NULL ? ccp->cc_user.s : n_empty),
         (ccp->cc_pass.s != NULL ? ccp->cc_pass.s : n_empty));
   NYD_OU;
   return (ccp != NULL);
}

FL boole
ccred_lookup(struct ccred *ccp, struct url *urlp)
{
   char *s;
   char const *pstr, *authdef;
   u8 authmask;
   enum okeys authokey;
   enum {NONE=0, WANT_PASS=1<<0, REQ_PASS=1<<1, WANT_USER=1<<2, REQ_USER=1<<3}
      ware;
   NYD_IN;

   su_mem_set(ccp, 0, sizeof *ccp);
   ccp->cc_user = urlp->url_user;

   ware = NONE;

   switch ((ccp->cc_cproto = urlp->url_cproto)) {
   case CPROTO_CCRED:
      authokey = (enum okeys)-1;
      authmask = AUTHTYPE_PLAIN;
      authdef = "plain";
      pstr = "ccred";
      break;
   default:
   case CPROTO_SMTP:
      authokey = ok_v_smtp_auth;
      authmask = AUTHTYPE_NONE | AUTHTYPE_PLAIN | AUTHTYPE_LOGIN |
            AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "plain";
      pstr = "smtp";
      break;
   case CPROTO_POP3:
      authokey = ok_v_pop3_auth;
      authmask = AUTHTYPE_PLAIN;
      authdef = "plain";
      pstr = "pop3";
      break;
#ifdef mx_HAVE_IMAP
   case CPROTO_IMAP:
      pstr = "imap";
      authokey = ok_v_imap_auth;
      authmask = AUTHTYPE_LOGIN | AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "login";
      break;
#endif
   }

   /* Authentication type */
   if (authokey == (enum okeys)-1 ||
         (s = xok_VLOOK(authokey, urlp, OXM_ALL)) == NULL)
      s = n_UNCONST(authdef);

   if (!su_cs_cmp_case(s, "none")) {
      ccp->cc_auth = "NONE";
      ccp->cc_authtype = AUTHTYPE_NONE;
      /*ware = NONE;*/
   } else if (!su_cs_cmp_case(s, "plain")) {
      ccp->cc_auth = "PLAIN";
      ccp->cc_authtype = AUTHTYPE_PLAIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "login")) {
      ccp->cc_auth = "LOGIN";
      ccp->cc_authtype = AUTHTYPE_LOGIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "cram-md5")) {
      ccp->cc_auth = "CRAM-MD5";
      ccp->cc_authtype = AUTHTYPE_CRAM_MD5;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "gssapi")) {
      ccp->cc_auth = "GSS-API";
      ccp->cc_authtype = AUTHTYPE_GSSAPI;
      ware = REQ_USER;
   } /* no else */

   /* Verify method */
   if (!(ccp->cc_authtype & authmask)) {
      n_err(_("Unsupported %s authentication method: %s\n"), pstr, s);
      ccp = NULL;
      goto jleave;
   }
# ifndef mx_HAVE_MD5
   if (ccp->cc_authtype == AUTHTYPE_CRAM_MD5) {
      n_err(_("No CRAM-MD5 support compiled in\n"));
      ccp = NULL;
      goto jleave;
   }
# endif
# ifndef mx_HAVE_GSSAPI
   if (ccp->cc_authtype == AUTHTYPE_GSSAPI) {
      n_err(_("No GSS-API support compiled in\n"));
      ccp = NULL;
      goto jleave;
   }
# endif

   /* Password */
   ccp->cc_pass = urlp->url_pass;
   if (ccp->cc_pass.s != NULL)
      goto jleave;

   if ((s = xok_vlook(password, urlp, OXM_ALL)) != NULL)
      goto js2pass;

# ifdef mx_HAVE_NETRC
   if (xok_blook(netrc_lookup, urlp, OXM_ALL) && _nrc_lookup(urlp, TRU1)) {
      ccp->cc_pass = urlp->url_pass;
      goto jleave;
   }
# endif

   if (ware & REQ_PASS) {
      if((s = mx_tty_getpass(savecat(urlp->url_u_h.s,
            _(" requires a password: ")))) != NIL)
js2pass:
         ccp->cc_pass.l = su_cs_len(ccp->cc_pass.s = savestr(s));
      else {
         n_err(_("A password is necessary for %s authentication\n"), pstr);
         ccp = NULL;
      }
   }

jleave:
   if(ccp != NULL && (n_poption & n_PO_D_VV))
      n_err(_("Credentials: host %s, user %s, pass %s\n"),
         urlp->url_h_p.s, (ccp->cc_user.s != NULL ? ccp->cc_user.s : n_empty),
         (ccp->cc_pass.s != NULL ? ccp->cc_pass.s : n_empty));
   NYD_OU;
   return (ccp != NULL);
}
#endif /* mx_HAVE_NET */

#ifdef mx_HAVE_NETRC
FL int
c_netrc(void *v)
{
   char **argv = v;
   struct nrc_node *nrc;
   boole load_only;
   NYD_IN;

   load_only = FAL0;
   if (*argv == NULL)
      goto jlist;
   if (argv[1] != NULL)
      goto jerr;
   if (!su_cs_cmp_case(*argv, "show"))
      goto jlist;
   load_only = TRU1;
   if (!su_cs_cmp_case(*argv, "load"))
      goto jlist;
   if (!su_cs_cmp_case(*argv, "clear"))
      goto jclear;
jerr:
   n_err(_("Synopsis: netrc: (<show> or) <clear> the .netrc cache\n"));
   v = NULL;
jleave:
   NYD_OU;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */

jlist:   {
   FILE *fp;
   uz l;

   if (_nrc_list == NULL)
      _nrc_init();
   if (_nrc_list == NRC_NODE_ERR) {
      n_err(_("Interpolate what file?\n"));
      v = NULL;
      goto jleave;
   }
   if (load_only)
      goto jleave;

   if((fp = mx_fs_tmp_open("netrc", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL){
      n_perr(_("tmpfile"), 0);
      v = NIL;
      goto jleave;
   }

   for (l = 0, nrc = _nrc_list; nrc != NULL; ++l, nrc = nrc->nrc_next) {
      fprintf(fp, _("machine %s "), nrc->nrc_dat); /* XXX quote? */
      if (nrc->nrc_ulen > 0)
         fprintf(fp, _("login \"%s\" "),
            string_quote(nrc->nrc_dat + nrc->nrc_mlen +1));
      if (nrc->nrc_plen > 0)
         fprintf(fp, _("password \"%s\"\n"),
            string_quote(nrc->nrc_dat + nrc->nrc_mlen +1 + nrc->nrc_ulen +1));
      else
         putc('\n', fp);
   }

   page_or_print(fp, l);
   mx_fs_close(fp);
   }
   goto jleave;

jclear:
   if (_nrc_list == NRC_NODE_ERR)
      _nrc_list = NULL;
   while ((nrc = _nrc_list) != NULL) {
      _nrc_list = nrc->nrc_next;
      n_free(nrc);
   }
   goto jleave;
}
#endif /* mx_HAVE_NETRC */

#ifdef mx_HAVE_MD5
FL char *
md5tohex(char hex[MD5TOHEX_SIZE], void const *vp)
{
   char const *cp = vp;
   uz i, j;
   NYD_IN;

   for (i = 0; i < MD5TOHEX_SIZE / 2; ++i) {
      j = i << 1;
# define __hex(n) ((n) > 9 ? (n) - 10 + 'a' : (n) + '0')
      hex[j] = __hex((cp[i] & 0xF0) >> 4);
      hex[++j] = __hex(cp[i] & 0x0F);
# undef __hex
   }
   NYD_OU;
   return hex;
}

FL char *
cram_md5_string(struct str const *user, struct str const *pass,
   char const *b64)
{
   struct str in, out;
   char digest[16], *cp;
   NYD_IN;

   out.s = NULL;
   if(user->l >= UZ_MAX - 1 - MD5TOHEX_SIZE - 1)
      goto jleave;
   if(pass->l >= INT_MAX)
      goto jleave;

   in.s = n_UNCONST(b64);
   in.l = su_cs_len(in.s);
   if(!b64_decode(&out, &in))
      goto jleave;
   if(out.l >= INT_MAX){
      n_free(out.s);
      out.s = NULL;
      goto jleave;
   }

   hmac_md5((uc*)out.s, out.l, (uc*)pass->s, pass->l, digest);
   n_free(out.s);
   cp = md5tohex(n_autorec_alloc(MD5TOHEX_SIZE +1), digest);

   in.l = user->l + MD5TOHEX_SIZE +1;
   in.s = n_lofi_alloc(user->l + 1 + MD5TOHEX_SIZE +1);
   su_mem_copy(in.s, user->s, user->l);
   in.s[user->l] = ' ';
   su_mem_copy(&in.s[user->l + 1], cp, MD5TOHEX_SIZE);
   if(b64_encode(&out, &in, B64_SALLOC | B64_CRLF) == NULL)
      out.s = NULL;
   n_lofi_free(in.s);
jleave:
   NYD_OU;
   return out.s;
}

FL void
hmac_md5(unsigned char *text, int text_len, unsigned char *key, int key_len,
   void *digest)
{
   /*
    * This code is taken from
    *
    * Network Working Group                                       H. Krawczyk
    * Request for Comments: 2104                                          IBM
    * Category: Informational                                      M. Bellare
    *                                                                    UCSD
    *                                                              R. Canetti
    *                                                                     IBM
    *                                                           February 1997
    *
    *
    *             HMAC: Keyed-Hashing for Message Authentication
    */
   md5_ctx context;
   unsigned char k_ipad[65]; /* inner padding - key XORd with ipad */
   unsigned char k_opad[65]; /* outer padding - key XORd with opad */
   unsigned char tk[16];
   int i;
   NYD_IN;

   /* if key is longer than 64 bytes reset it to key=MD5(key) */
   if (key_len > 64) {
      md5_ctx tctx;

      md5_init(&tctx);
      md5_update(&tctx, key, key_len);
      md5_final(tk, &tctx);

      key = tk;
      key_len = 16;
   }

   /* the HMAC_MD5 transform looks like:
    *
    * MD5(K XOR opad, MD5(K XOR ipad, text))
    *
    * where K is an n byte key
    * ipad is the byte 0x36 repeated 64 times
    * opad is the byte 0x5c repeated 64 times
    * and text is the data being protected */

   /* start out by storing key in pads */
   su_mem_set(k_ipad, 0, sizeof k_ipad);
   su_mem_set(k_opad, 0, sizeof k_opad);
   su_mem_copy(k_ipad, key, key_len);
   su_mem_copy(k_opad, key, key_len);

   /* XOR key with ipad and opad values */
   for (i=0; i<64; i++) {
      k_ipad[i] ^= 0x36;
      k_opad[i] ^= 0x5c;
   }

   /* perform inner MD5 */
   md5_init(&context);                    /* init context for 1st pass */
   md5_update(&context, k_ipad, 64);      /* start with inner pad */
   md5_update(&context, text, text_len);  /* then text of datagram */
   md5_final(digest, &context);           /* finish up 1st pass */

   /* perform outer MD5 */
   md5_init(&context);                 /* init context for 2nd pass */
   md5_update(&context, k_opad, 64);   /* start with outer pad */
   md5_update(&context, digest, 16);   /* then results of 1st hash */
   md5_final(digest, &context);        /* finish up 2nd pass */
   NYD_OU;
}
#endif /* mx_HAVE_MD5 */

#include "su/code-ou.h"
/* s-it-mode */
