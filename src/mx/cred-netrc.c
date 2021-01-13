/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cred-netrc.h.
 *@ .netrc parser quite loosely based upon NetBSD usr.bin/ftp/
 *@   $NetBSD: ruserpass.c,v 1.33 2007/04/17 05:52:04 lukem Exp $
 *@ TODO With an on_loop_tick_event, trigger cache update once per loop max.
 *@ TODO I.e., unless *netrc-pipe* was set, auto check for updates.
 *
 * Copyright (c) 2014 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cred_netrc
#define mx_SOURCE
#define mx_SOURCE_CRED_NETRC

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_NETRC
#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/cmd.h"
#include "mx/child.h"
#include "mx/file-streams.h"
#include "mx/url.h"

#include "mx/cred-netrc.h"
#include "su/code-in.h"

/* NetBSD usr.bin/ftp/ruserpass.c uses 100 bytes for that, we need four
* concurrently (dummy, host, user, pass), so make it a KB */
#define a_NETRC_TOKEN_MAXLEN (1024u / 4)

/* Dictionary stores a_netrc_entry, not owned */
#define a_NETRC_FLAGS (su_CS_DICT_CASE | su_CS_DICT_HEAD_RESORT |\
      su_CS_DICT_ERR_PASS)
#define a_NETRC_TRESHOLD_SHIFT 3

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

struct a_netrc_entry{
   struct a_netrc_entry *nrce_next;
   u32 nrce_password_idx; /* Offset in .nrce_dat, U32_MAX if not set */
   boole nrce_has_login; /* Have login at .nrce_dat[0] */
   char nrce_dat[VFIELD_SIZE(3)];
};

static struct su_cs_dict *a_netrc_dp, a_netrc__d; /* XXX atexit _gut (DVL()) */

/* We stop parsing and _gut(FAL0) on hard errors like NOMEM, OVERFLOW and IO */
static void a_netrc_create(void);
static enum a_netrc_token a_netrc__token(FILE *fi,
      char buffer[a_NETRC_TOKEN_MAXLEN], boole *nl_last);
static void a_netrc_gut(boole gut_dp);

/* */
static struct n_strlist *a_netrc_dump(char const *cmdname, char const *key,
      void const *dat);

/* */
static char *a_netrc_bsd_quote(char const *v);

static void
a_netrc_create(void){
   enum{
      a_NONE,
      a_IS_PIPE = 1u<<0,
      a_LOGIN = 1u<<1,
      a_PASSWORD = 1u<<2,
      a_SEEN_DEFAULT = 1u<<3,
      a_ERROR = 1u<<4
   };

   char buffer[a_NETRC_TOKEN_MAXLEN], machine[a_NETRC_TOKEN_MAXLEN],
      login[a_NETRC_TOKEN_MAXLEN], password[a_NETRC_TOKEN_MAXLEN], *netrc_load;
   struct stat sb;
   boole nl_last;
   enum a_netrc_token t;
   FILE *fi;
   struct a_netrc_entry *nrcep;
   char const *emsg;
   u32 f;
   NYD_IN;

   a_netrc_dp = su_cs_dict_set_treshold_shift(
         su_cs_dict_create(&a_netrc__d, a_NETRC_FLAGS, NIL),
            a_NETRC_TRESHOLD_SHIFT);

   f = a_NONE;
   UNINIT(emsg, NIL);
   nrcep = NIL;
   fi = NIL;

   if((netrc_load = ok_vlook(netrc_pipe)) != NIL){
      f |= a_IS_PIPE;
      if((fi = mx_fs_pipe_open(netrc_load, "r", ok_vlook(SHELL), NIL,
            mx_CHILD_FD_NULL)) == NIL)
         goto jerrdoc;
   }else{
      if((netrc_load = fexpand(ok_vlook(NETRC), (FEXP_NOPROTO |
            FEXP_LOCAL_FILE | FEXP_NSHELL))) == NIL)
         goto jleave;

      if((fi = mx_fs_open(netrc_load, "r")) == NIL)
         goto jerrdoc;

      /* Be simple and apply rigid (permission) check(s) */
      if(fstat(fileno(fi), &sb) == -1 || !S_ISREG(sb.st_mode) ||
            (sb.st_mode & (S_IRWXG | S_IRWXO))){
         emsg = N_("Not a regular file, or accessible by non-user\n");
         goto jerr;
      }
   }

   nl_last = TRU1;
   switch((t = a_netrc__token(fi, buffer, &nl_last))){
   case a_NETRC_NONE:
      break;
   default: /* Does not happen (but on error?), keep CC happy */
   case a_NETRC_DEFAULT:
jdef:
      /* We ignore the default entry (require an exact host match), and we
       * also ignore anything after such an entry (faulty syntax) */
      f |= a_SEEN_DEFAULT;
      /* FALLTHRU */
   case a_NETRC_MACHINE:
jm_h:
      /* Normalize HOST to lowercase */
      *machine = '\0';
      if(!(f & a_SEEN_DEFAULT) &&
            (t = a_netrc__token(fi, machine, &nl_last)) != a_NETRC_INPUT)
         goto jenotinput;

      *login = *password = '\0';
      f &= ~(a_LOGIN | a_PASSWORD);

      while((t = a_netrc__token(fi, buffer, &nl_last)) != a_NETRC_NONE &&
            t != a_NETRC_MACHINE && t != a_NETRC_DEFAULT){
         switch(t){
         case a_NETRC_LOGIN:
            if((t = a_netrc__token(fi, login, &nl_last)) != a_NETRC_INPUT)
               goto jenotinput;
            f |= a_LOGIN;
            break;
         case a_NETRC_PASSWORD:
            if((t = a_netrc__token(fi, password, &nl_last)) != a_NETRC_INPUT)
               goto jenotinput;
            f |= a_PASSWORD;
            break;
         case a_NETRC_ACCOUNT:
            if((t = a_netrc__token(fi, buffer, &nl_last)) != a_NETRC_INPUT)
               goto jenotinput;
            break;
         case a_NETRC_MACDEF:
            if((t = a_netrc__token(fi, buffer, &nl_last)) != a_NETRC_INPUT){
jenotinput:
               emsg = N_("parse error");
               goto jerr;
            }else{
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
            emsg = N_("parse error (unknown token)");
            goto jerr;
         }
      }

      if(!(f & a_SEEN_DEFAULT) && (f & (a_LOGIN | a_PASSWORD))){
         union {void *v; struct a_netrc_entry *nrce;} p;
         u32 llen, plen;

         llen = (f & a_LOGIN) ? su_cs_len(login) : 0;
         plen = (f & a_PASSWORD) ? su_cs_len(password) : 0;
         nrcep = su_ALLOC(VSTRUCT_SIZEOF(struct a_netrc_entry,nrce_dat) +
               llen +1 + plen +1);
         if(nrcep == NIL)
            goto jerrdoc;
         nrcep->nrce_next = NIL;

         if((nrcep->nrce_has_login = ((f & a_LOGIN) != 0)))
            su_mem_copy(&nrcep->nrce_dat[0], login, ++llen);

         if(f & a_PASSWORD)
            su_mem_copy(&nrcep->nrce_dat[nrcep->nrce_password_idx = llen],
               password, ++plen);
         else
            nrcep->nrce_password_idx = U32_MAX;

         if((p.v = su_cs_dict_lookup(a_netrc_dp, machine)) != NIL){
            while(p.nrce->nrce_next != NIL)
               p.nrce = p.nrce->nrce_next;
            p.nrce->nrce_next = nrcep;
         }else{
            s32 err;

            if((err = su_cs_dict_insert(a_netrc_dp, machine, nrcep)
                  ) != su_ERR_NONE){
               emsg = su_err_doc(err);
               goto jerr;
            }
         }

         nrcep = NIL;
      }

      if(t != a_NETRC_NONE && (f & a_SEEN_DEFAULT) && (n_poption & n_PO_D_V))
         n_err(_(".netrc: \"default\" must be last entry, ignoring: %s\n"),
            n_shexp_quote_cp(netrc_load, FAL0));
      if(t == a_NETRC_MACHINE)
         goto jm_h;
      if(t == a_NETRC_DEFAULT)
         goto jdef;
      ASSERT(t == a_NETRC_NONE);
      break;
   case a_NETRC_ERROR:
      emsg = N_("parse error (unknown top level token)");
      goto jerr;
   }

   nrcep = NIL;
jleave:
   if(nrcep != NIL)
      su_FREE(nrcep);

   if(fi != NIL){
      if(f & a_IS_PIPE)
         mx_fs_pipe_close(fi, TRU1);
      else
         mx_fs_close(fi);
   }

   if(f & a_ERROR)
      a_netrc_gut(FAL0);

   NYD_OU;
   return;

jerrdoc:
   emsg = su_err_doc(su_err_no());
jerr:
   UNUSED(emsg);
   n_err(_(".netrc: %s: %s\n"), n_shexp_quote_cp(netrc_load, FAL0), V_(emsg));
   f |= a_ERROR;
   goto jleave;
}

static enum a_netrc_token
a_netrc__token(FILE *fi, char buffer[a_NETRC_TOKEN_MAXLEN], boole *nl_last){
   static struct token_type{
      s8 tt_token;
      char tt_name[15];
   } const *ttap, tta[] = {
      {a_NETRC_NONE, ""},
      {a_NETRC_DEFAULT, "default"},
      {a_NETRC_LOGIN, "login"},
      {a_NETRC_PASSWORD, "password\0"},
      {a_NETRC_PASSWORD, "passwd"},
      {a_NETRC_ACCOUNT, "account"},
      {a_NETRC_MACDEF, "macdef"},
      {a_NETRC_MACHINE, "machine"}
   };
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

   /* Is it a quoted token?  At least IBM syntax also supports ' quotes */
   cp = buffer;
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
         if(PCMP(cp, ==, &buffer[a_NETRC_TOKEN_MAXLEN -1])){
            rv = a_NETRC_ERROR;
            goto jleave;
         }
      }
   }else{
      *cp++ = c;
      while((c = getc(fi)) != EOF && !su_cs_is_white(c)){
         /* Reverse solidus escaping the next character is (Net)BSD syntax */
         if(c == '\\' && (c = getc(fi)) == EOF)
               break;
         *cp++ = c;
         if(PCMP(cp, ==, &buffer[a_NETRC_TOKEN_MAXLEN -1])){
            rv = a_NETRC_ERROR;
            goto jleave;
         }
      }
      *nl_last = (c == '\n');
   }
   *cp = '\0';

   /* */
   rv = a_NETRC_INPUT;
   for(ttap = &tta[0]; ttap < &tta[NELEM(tta)]; ++ttap)
      if(!su_cs_cmp(buffer, ttap->tt_name)){
         rv = ttap->tt_token;
         break;
      }

jleave:
   if(c == EOF && !feof(fi))
      rv = a_NETRC_ERROR;

   NYD2_OU;
   return rv;
}

static void
a_netrc_gut(boole gut_dp){
   NYD2_IN;

   if(a_netrc_dp != NIL){
      struct su_cs_dict_view csdv;

      su_CS_DICT_FOREACH(a_netrc_dp, &csdv){
         struct a_netrc_entry *nrcep, *tmp;

         for(nrcep = S(struct a_netrc_entry*,su_cs_dict_view_data(&csdv));
               nrcep != NIL;){
            tmp = nrcep;
            nrcep = nrcep->nrce_next;
            su_FREE(tmp);
         }
      }

      if(gut_dp){
         su_cs_dict_gut(a_netrc_dp);
         a_netrc_dp = NIL;
      }else
         su_cs_dict_clear(a_netrc_dp);
   }

   NYD2_OU;
}

static struct n_strlist *
a_netrc_dump(char const *cmdname, char const *key, void const *dat){
   struct n_string s_b, *s;
   struct n_strlist *slp;
   struct a_netrc_entry const *nrcep;
   NYD2_IN;
   UNUSED(cmdname);

   s = n_string_book(n_string_creat_auto(&s_b), 127);
   s = n_string_resize(s, n_STRLIST_PLAIN_SIZE());

   for(nrcep = S(struct a_netrc_entry const*,dat); nrcep != NIL;
         nrcep = nrcep->nrce_next){
      if(S(void const*,nrcep) != dat)
         s = n_string_push_c(s, '\n');

      s = n_string_push_buf(s, "machine ", sizeof("machine ") -1);
      s = n_string_push_cp(s, a_netrc_bsd_quote(key));

      if(nrcep->nrce_has_login){
         s = n_string_push_buf(s, " login ", sizeof(" login ") -1);
         s = n_string_push_cp(s, a_netrc_bsd_quote(&nrcep->nrce_dat[0]));
      }

      if(nrcep->nrce_password_idx != U32_MAX){
         s = n_string_push_buf(s, " password ", sizeof(" password ") -1);
         s = n_string_push_cp(s,
               a_netrc_bsd_quote(&nrcep->nrce_dat[nrcep->nrce_password_idx]));
      }
   }

   s = n_string_push_c(s, '\n');

   n_string_cp(s);

   slp = R(struct n_strlist*,S(void*,s->s_dat));
   /* xxx Should we assert alignment constraint of slp is satisfied?
    * xxx Should be, heap memory with alignment < sizeof(void*) bitter? */
   slp->sl_next = NIL;
   slp->sl_len = s->s_len;
   n_string_drop_ownership(s);

   NYD2_OU;
   return slp;
}

static char *
a_netrc_bsd_quote(char const *v){
   char const *cp;
   uz i;
   char c, *rv;
   boole quotes;
   NYD2_IN;

   quotes = FAL0;

   for(i = 0, cp = v; (c = *cp) != '\0'; ++i, ++cp)
      /* \n etc. weird, granted */
      if(su_cs_find_c("\"\\ \n\r\t\v", c) != NIL){
         ++i;
         if(!quotes && c != '"' && c != '\\')
            quotes = TRU1;
      }
   if(quotes)
      i += 2;

   rv = su_AUTO_ALLOC(i +1);

   i = 0;
   if(quotes)
      rv[i++] = '"';
   for(cp = v; (c = *cp) != '\0'; rv[i++] = c, ++cp)
      if(c == '"' || c == '\\')
         rv[i++] = '\\';
   if(quotes)
      rv[i++] = '"';
   rv[i] = '\0';

   NYD2_OU;
   return rv;
}

int
c_netrc(void *vp){
   boole load_only;
   char **argv;
   NYD_IN;

   argv = vp;

   load_only = FAL0;
   if(*argv == NIL)
      goto jlist;
   if(su_cs_starts_with_case("lookup", *argv)){
      if(argv[1] == NIL)
         goto jerr;
      goto jlookup;
   }
   if(argv[1] != NIL)
      goto jerr;
   if(su_cs_starts_with_case("show", *argv))
      goto jlist;
   if(su_cs_starts_with_case("clear", *argv))
      goto jclear;

   load_only = TRU1;
   if(su_cs_starts_with_case("load", *argv))
      goto jclear;
jerr:
   mx_cmd_print_synopsis(mx_cmd_firstfit("netrc"), NIL);
   vp = NIL;
jleave:
   NYD_OU;
   return (vp == NIL ? n_EXIT_ERR : n_EXIT_OK);

jlookup:{
   struct mx_netrc_entry nrce;
   struct mx_url url;

   if(!mx_url_parse(&url, CPROTO_NONE, argv[1])){
      n_err(_("netrc: lookup: invalid URL: %s\n"),
         n_shexp_quote_cp(argv[1], FAL0));
      vp = NIL;
   }else if(mx_netrc_lookup(&nrce, &url)){
      fprintf(n_stdout, "netrc: lookup: %s: machine %s",
         url.url_u_h.s, a_netrc_bsd_quote(nrce.nrce_machine));
      if(nrce.nrce_login != NIL)
         fprintf(n_stdout, " login %s", a_netrc_bsd_quote(nrce.nrce_login));
      if(nrce.nrce_password != NIL)
         fprintf(n_stdout, " password %s",
            a_netrc_bsd_quote(nrce.nrce_password));
      putc('\n', n_stdout);
   }else{
      fprintf(n_stdout, _("netrc: lookup: no entry for: %s\n"), url.url_u_h.s);
      vp = NIL;
   }
   goto jleave;
   }

jclear:
   a_netrc_gut(TRU1);
   if(load_only)
      goto jlist;
   goto jleave;

jlist:
   if(a_netrc_dp == NIL)
      a_netrc_create();

   if(!load_only){
      struct n_strlist *slp;

      slp = NIL;
      if(!(mx_xy_dump_dict("netrc", a_netrc_dp, &slp, NIL,
               &a_netrc_dump) &&
            mx_page_or_print_strlist("netrc", slp, TRU1)))
         vp = NIL;
   }
   goto jleave;
}

boole
mx_netrc_lookup(struct mx_netrc_entry *result, struct mx_url const *urlp){
   struct n_string s_b, *s;
   union {void *v; uz i; struct a_netrc_entry *nrce;} p;
   char const *host;
   boole rv;
   NYD_IN;

   s = n_string_creat_auto(&s_b);

   rv = FAL0;

   if(a_netrc_dp == NIL)
      a_netrc_create();
   if(su_cs_dict_count(a_netrc_dp) == 0)
      goto jleave;

   s = n_string_book(s, urlp->url_host.l + 2);

   if((p.v = su_cs_dict_lookup(a_netrc_dp, host = urlp->url_host.s)) == NIL){
      /* Cannot be an exact match, but maybe .netrc provides a matching
       * "*." wildcard entry, which we recognize as an extension, meaning
       * "skip a single subdomain, then match the rest" */
      for(host = urlp->url_host.s, p.i = urlp->url_host.l;;){
         if(--p.i <= 1)
            goto jleave;
         if(*host++ == '.')
            break;
      }

      s = n_string_push_buf(s, "*.", sizeof("*.") -1);
      s = n_string_push_buf(s, host, p.i);
      if((p.v = su_cs_dict_lookup(a_netrc_dp, host = n_string_cp(s))) == NIL)
         goto jleave;
   }

   /* Without user we will only return a result if unambiguous */
   rv = TRUM1;
   if(urlp->url_user.s != NIL){
      /* (No do{}while() because of gcc 3.4.3 union bug (Solaris 5.10)) */
      for(; p.nrce != NIL; p.nrce = p.nrce->nrce_next){
         if(p.nrce->nrce_has_login){
            if(!su_cs_cmp(&p.nrce->nrce_dat[0], urlp->url_user.s)){
               rv = TRUM1;
               break;
            }
         }else if(rv == TRUM1 && p.nrce->nrce_next == NIL)
            break;
         rv = TRU1;
      }
   }else if(p.nrce->nrce_next != NIL)
      p.nrce = NIL;

   if(p.nrce != NIL){
      su_mem_set(result, 0, sizeof(*result));
      result->nrce_machine = host;
      if(p.nrce->nrce_has_login)
         result->nrce_login = &p.nrce->nrce_dat[0];
      if(p.nrce->nrce_password_idx != U32_MAX)
         result->nrce_password = &p.nrce->nrce_dat[p.nrce->nrce_password_idx];
   }else
      rv = FAL0;

jleave:
   /* su_string_gut(s); */
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_NETRC */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CRED_NETRC
/* s-it-mode */
