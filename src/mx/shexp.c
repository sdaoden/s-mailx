/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Shell "word", file- and other name expansions, incl. file globbing.
 *@ TODO v15: peek signal states while opendir/readdir/etc.
 *@ TODO "Magic solidus" used as path separator.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
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
#define su_FILE shexp
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <pwd.h>

#ifdef mx_HAVE_FNMATCH
# include <dirent.h>
# include <fnmatch.h>
#endif

#include <su/cs.h>
#include <su/utf.h>

#include "mx/iconv.h"
#include "mx/ui-str.h"

/* POSIX says
 *   Environment variable names used by the utilities in the Shell and
 *   Utilities volume of POSIX.1-2008 consist solely of uppercase
 *   letters, digits, and the <underscore> ('_') from the characters
 *   defined in Portable Character Set and do not begin with a digit.
 *   Other characters may be permitted by an implementation;
 *   applications shall tolerate the presence of such names.
 * We do support the hyphen-minus "-" (except in last position for ${x[:]-y}).
 * We support some special parameter names for one-letter(++) variable names;
 * these have counterparts in the code that manages internal variables,
 * and some more special treatment below! */
#define a_SHEXP_ISVARC(C) (su_cs_is_alnum(C) || (C) == '_' || (C) == '-')
#define a_SHEXP_ISVARC_BAD1ST(C) (su_cs_is_digit(C)) /* (Assumed below!) */
#define a_SHEXP_ISVARC_BADNST(C) ((C) == '-')

enum a_shexp_quote_flags{
   a_SHEXP_QUOTE_NONE,
   a_SHEXP_QUOTE_ROUNDTRIP = 1u<<0, /* Result won't be consumed immediately */

   a_SHEXP_QUOTE_T_REVSOL = 1u<<8,  /* Type: by reverse solidus */
   a_SHEXP_QUOTE_T_SINGLE = 1u<<9,  /* Type: single-quotes */
   a_SHEXP_QUOTE_T_DOUBLE = 1u<<10, /* Type: double-quotes */
   a_SHEXP_QUOTE_T_DOLLAR = 1u<<11, /* Type: dollar-single-quotes */
   a_SHEXP_QUOTE_T_MASK = a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE |
         a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR,

   a_SHEXP_QUOTE__FREESHIFT = 16u
};

#ifdef mx_HAVE_FNMATCH
struct a_shexp_glob_ctx{
   char const *sgc_patdat;       /* Remaining pattern (at and below level) */
   size_t sgc_patlen;
   struct n_string *sgc_outer;   /* Resolved path up to this level */
   ui32_t sgc_flags;
   ui8_t sgc__dummy[4];
};
#endif

struct a_shexp_quote_ctx{
   struct n_string *sqc_store;   /* Result storage */
   struct str sqc_input;         /* Input data, topmost level */
   ui32_t sqc_cnt_revso;
   ui32_t sqc_cnt_single;
   ui32_t sqc_cnt_double;
   ui32_t sqc_cnt_dollar;
   enum a_shexp_quote_flags sqc_flags;
   ui8_t sqc__dummy[4];
};

struct a_shexp_quote_lvl{
   struct a_shexp_quote_lvl *sql_link; /* Outer level */
   struct str sql_dat;                 /* This level (has to) handle(d) */
   enum a_shexp_quote_flags sql_flags;
   ui8_t sql__dummy[4];
};

/* Locate the user's mailbox file (where new, unread mail is queued) */
static char *a_shexp_findmail(char const *user, bool_t force);

/* Expand ^~/? and ^~USER/? constructs.
 * Returns the completely resolved (maybe empty or identical to input)
 * n_autorec_alloc()ed string */
static char *a_shexp_tilde(char const *s);

/* Perform fnmatch(3).  May return NULL on error */
static char *a_shexp_globname(char const *name, enum fexp_mode fexpm);
#ifdef mx_HAVE_FNMATCH
static bool_t a_shexp__glob(struct a_shexp_glob_ctx *sgcp,
               struct n_strlist **slpp);
static int a_shexp__globsort(void const *cvpa, void const *cvpb);
#endif

/* Parse an input string and create a sh(1)ell-quoted result */
static void a_shexp__quote(struct a_shexp_quote_ctx *sqcp,
               struct a_shexp_quote_lvl *sqlp);

static char *
a_shexp_findmail(char const *user, bool_t force){
   char *rv;
   char const *cp;
   n_NYD2_IN;

   if(!force){
      if((cp = ok_vlook(inbox)) != NULL && *cp != '\0'){
         /* _NFOLDER extra introduced to avoid % recursion loops */
         if((rv = fexpand(cp, FEXP_NSPECIAL | FEXP_NFOLDER | FEXP_NSHELL)
               ) != NULL)
            goto jleave;
         n_err(_("*inbox* expansion failed, using $MAIL/built-in: %s\n"), cp);
      }
      /* Heirloom compatibility: an IMAP *folder* becomes "%" */
#ifdef mx_HAVE_IMAP
      else if(cp == NULL && !su_cs_cmp(user, ok_vlook(LOGNAME)) &&
            which_protocol(cp = n_folder_query(), FAL0, FAL0, NULL)
               == PROTO_IMAP){
         /* TODO Compat handling of *folder* with IMAP! */
         n_OBSOLETE("no more expansion of *folder* in \"%\": "
            "please set *inbox*");
         rv = savestr(cp);
         goto jleave;
      }
#endif

      if((cp = ok_vlook(MAIL)) != NULL){
         rv = savestr(cp);
         goto jleave;
      }
   }

   /* C99 */{
      size_t ul, i;

      ul = su_cs_len(user) +1;
      i = sizeof(VAL_MAIL) -1 + 1 + ul;

      rv = n_autorec_alloc(i);
      memcpy(rv, VAL_MAIL, (i = sizeof(VAL_MAIL) -1));
      rv[i] = '/';
      memcpy(&rv[++i], user, ul);
   }
jleave:
   n_NYD2_OU;
   return rv;
}

static char *
a_shexp_tilde(char const *s){
   struct passwd *pwp;
   size_t nl, rl;
   char const *rp, *np;
   char *rv;
   n_NYD2_IN;

   if(*(rp = &s[1]) == '/' || *rp == '\0'){
      np = ok_vlook(HOME);
      rl = su_cs_len(rp);
   }else{
      if((rp = su_cs_find_c(np = rp, '/')) != NULL){
         nl = PTR2SIZE(rp - np);
         np = savestrbuf(np, nl);
         rl = su_cs_len(rp);
      }else
         rl = 0;

      if((pwp = getpwnam(np)) == NULL){
         rv = savestr(s);
         goto jleave;
      }
      np = pwp->pw_dir;
   }

   nl = su_cs_len(np);
   rv = n_autorec_alloc(nl + 1 + rl +1);
   memcpy(rv, np, nl);
   if(rl > 0){
      memcpy(rv + nl, rp, rl);
      nl += rl;
   }
   rv[nl] = '\0';
jleave:
   n_NYD2_OU;
   return rv;
}

static char *
a_shexp_globname(char const *name, enum fexp_mode fexpm){
#ifdef mx_HAVE_FNMATCH
   struct a_shexp_glob_ctx sgc;
   struct n_string outer;
   struct n_strlist *slp;
   char *cp;
   n_NYD_IN;

   memset(&sgc, 0, sizeof sgc);
   sgc.sgc_patlen = su_cs_len(name);
   sgc.sgc_patdat = savestrbuf(name, sgc.sgc_patlen);
   sgc.sgc_outer = n_string_reserve(n_string_creat(&outer), sgc.sgc_patlen);
   sgc.sgc_flags = ((fexpm & FEXP_SILENT) != 0);
   slp = NULL;
   if(a_shexp__glob(&sgc, &slp))
      cp = (char*)1;
   else
      cp = NULL;
   n_string_gut(&outer);

   if(cp == NULL)
      goto jleave;

   if(slp == NULL){
      cp = n_UNCONST(N_("File pattern does not match"));
      goto jerr;
   }else if(slp->sl_next == NULL)
      cp = savestrbuf(slp->sl_dat, slp->sl_len);
   else if(fexpm & FEXP_MULTIOK){
      struct n_strlist **sorta, *xslp;
      size_t i, no, l;

      no = l = 0;
      for(xslp = slp; xslp != NULL; xslp = xslp->sl_next){
         ++no;
         l += xslp->sl_len + 1;
      }

      sorta = n_alloc(sizeof(*sorta) * no);
      no = 0;
      for(xslp = slp; xslp != NULL; xslp = xslp->sl_next)
         sorta[no++] = xslp;
      qsort(sorta, no, sizeof *sorta, &a_shexp__globsort);

      cp = n_autorec_alloc(++l);
      l = 0;
      for(i = 0; i < no; ++i){
         xslp = sorta[i];
         memcpy(&cp[l], xslp->sl_dat, xslp->sl_len);
         l += xslp->sl_len;
         cp[l++] = '\0';
      }
      cp[l] = '\0';

      n_free(sorta);
      n_pstate |= n_PS_EXPAND_MULTIRESULT;
   }else{
      cp = n_UNCONST(N_("File pattern matches multiple results"));
      goto jerr;
   }

jleave:
   while(slp != NULL){
      struct n_strlist *tmp = slp;

      slp = slp->sl_next;
      n_free(tmp);
   }
   n_NYD_OU;
   return cp;

jerr:
   if(!(fexpm & FEXP_SILENT)){
      name = n_shexp_quote_cp(name, FAL0);
      n_err("%s: %s\n", V_(cp), name);
   }
   cp = NULL;
   goto jleave;

#else /* mx_HAVE_FNMATCH */
   n_UNUSED(fexpm);

   if(!(fexpm & FEXP_SILENT))
      n_err(_("No filename pattern (fnmatch(3)) support compiled in\n"));
   return savestr(name);
#endif
}

#ifdef mx_HAVE_FNMATCH
static bool_t
a_shexp__glob(struct a_shexp_glob_ctx *sgcp, struct n_strlist **slpp){
   enum{a_SILENT = 1<<0, a_DEEP=1<<1, a_SALLOC=1<<2};

   struct a_shexp_glob_ctx nsgc;
   struct dirent *dep;
   DIR *dp;
   size_t old_outerlen;
   char const *ccp, *myp;
   n_NYD2_IN;

   /* We need some special treatment for the outermost level.
    * All along our way, normalize path separators */
   if(!(sgcp->sgc_flags & a_DEEP)){
      if(sgcp->sgc_patlen > 0 && sgcp->sgc_patdat[0] == '/'){
         myp = n_string_cp(n_string_push_c(sgcp->sgc_outer, '/'));
         do
            ++sgcp->sgc_patdat;
         while(--sgcp->sgc_patlen > 0 && sgcp->sgc_patdat[0] == '/');
      }else
         myp = "./";
   }else
      myp = n_string_cp(sgcp->sgc_outer);
   old_outerlen = sgcp->sgc_outer->s_len;

   /* Separate current directory/pattern level from any possible remaining
    * pattern in order to be able to use it for fnmatch(3) */
   if((ccp = memchr(sgcp->sgc_patdat, '/', sgcp->sgc_patlen)) == NULL)
      nsgc.sgc_patlen = 0;
   else{
      nsgc = *sgcp;
      nsgc.sgc_flags |= a_DEEP;
      sgcp->sgc_patlen = PTR2SIZE((nsgc.sgc_patdat = &ccp[1]) -
            &sgcp->sgc_patdat[0]);
      nsgc.sgc_patlen -= sgcp->sgc_patlen;

      /* Trim solidus, everywhere */
      if(sgcp->sgc_patlen > 0){
         assert(sgcp->sgc_patdat[sgcp->sgc_patlen -1] == '/');
         ((char*)n_UNCONST(sgcp->sgc_patdat))[--sgcp->sgc_patlen] = '\0';
      }
      while(nsgc.sgc_patlen > 0 && nsgc.sgc_patdat[0] == '/'){
         --nsgc.sgc_patlen;
         ++nsgc.sgc_patdat;
      }
   }

   /* Our current directory level */
   /* xxx Plenty of room for optimizations, like quickshot lstat(2) which may
    * xxx be the (sole) result depending on pattern surroundings, etc. */
   if((dp = opendir(myp)) == NULL){
      int err;

      switch((err = su_err_no())){
      case su_ERR_NOTDIR:
         ccp = N_("cannot access paths under non-directory");
         goto jerr;
      case su_ERR_NOENT:
         ccp = N_("path component of (sub)pattern non-existent");
         goto jerr;
      case su_ERR_ACCES:
         ccp = N_("file permission for file (sub)pattern denied");
         goto jerr;
      case su_ERR_NFILE:
      case su_ERR_MFILE:
         ccp = N_("file descriptor limit reached, cannot open directory");
         goto jerr;
      default:
         ccp = N_("cannot open path component as directory");
         goto jerr;
      }
   }

   /* As necessary, quote bytes in the current pattern TODO This will not
    * TODO truly work out in case the user would try to quote a character
    * TODO class, for example: in "\[a-z]" the "\" would be doubled!  For that
    * TODO to work out, we need the original user input or the shell-expression
    * TODO parse tree, otherwise we do not know what is desired! */
   /* C99 */{
      char *ncp;
      size_t i;
      bool_t need;

      for(need = FAL0, i = 0, myp = sgcp->sgc_patdat; *myp != '\0'; ++myp)
         switch(*myp){
         case '\'': case '"': case '\\': case '$':
         case ' ': case '\t':
            need = TRU1;
            ++i;
            /* FALLTHRU */
         default:
            ++i;
            break;
         }

      if(need){
         ncp = n_autorec_alloc(i +1);
         for(i = 0, myp = sgcp->sgc_patdat; *myp != '\0'; ++myp)
            switch(*myp){
            case '\'': case '"': case '\\': case '$':
            case ' ': case '\t':
               ncp[i++] = '\\';
               /* FALLTHRU */
            default:
               ncp[i++] = *myp;
               break;
            }
         ncp[i] = '\0';
         myp = ncp;
      }else
         myp = sgcp->sgc_patdat;
   }

   while((dep = readdir(dp)) != NULL){
      switch(fnmatch(myp, dep->d_name, FNM_PATHNAME | FNM_PERIOD)){
      case 0:{
         /* A match expresses the desire to recurse if there is more pattern */
         if(nsgc.sgc_patlen > 0){
            bool_t isdir;

            n_string_push_cp((sgcp->sgc_outer->s_len > 0
                  ? n_string_push_c(sgcp->sgc_outer, '/') : sgcp->sgc_outer),
               dep->d_name);

            isdir = FAL0;
#ifdef mx_HAVE_DIRENT_TYPE
            if(dep->d_type == DT_DIR)
               isdir = TRU1;
            else if(dep->d_type == DT_LNK || dep->d_type == DT_UNKNOWN)
#endif
            {
               struct stat sb;

               if(stat(n_string_cp(sgcp->sgc_outer), &sb)){
                  ccp = N_("I/O error when querying file status");
                  goto jerr;
               }else if(S_ISDIR(sb.st_mode))
                  isdir = TRU1;
            }

            /* TODO Recurse with current dir FD open, which could E[MN]FILE!
             * TODO Instead save away a list of such n_string's for later */
            if(isdir && !a_shexp__glob(&nsgc, slpp)){
               ccp = (char*)1;
               goto jleave;
            }

            n_string_trunc(sgcp->sgc_outer, old_outerlen);
         }else{
            struct n_strlist *slp;
            size_t i, j;

            i = su_cs_len(dep->d_name);
            j = (old_outerlen > 0) ? old_outerlen + 1 + i : i;
            slp = n_STRLIST_ALLOC(j);
            *slpp = slp;
            slpp = &slp->sl_next;
            slp->sl_next = NULL;
            if((j = old_outerlen) > 0){
               memcpy(&slp->sl_dat[0], sgcp->sgc_outer->s_dat, j);
               if(slp->sl_dat[j -1] != '/')
                  slp->sl_dat[j++] = '/';
            }
            memcpy(&slp->sl_dat[j], dep->d_name, i);
            slp->sl_dat[j += i] = '\0';
            slp->sl_len = j;
         }
         }break;
      case FNM_NOMATCH:
         break;
      default:
         ccp = N_("fnmatch(3) cannot handle file (sub)pattern");
         goto jerr;
      }
   }

   ccp = NULL;
jleave:
   if(dp != NULL)
      closedir(dp);
   n_NYD2_OU;
   return (ccp == NULL);

jerr:
   if(!(sgcp->sgc_flags & a_SILENT)){
      char const *s2, *s3;

      if(sgcp->sgc_outer->s_len > 0){
         s2 = n_shexp_quote_cp(n_string_cp(sgcp->sgc_outer), FAL0);
         s3 = "/";
      }else
         s2 = s3 = n_empty;

      n_err("%s: %s%s%s\n", V_(ccp), s2, s3,
         n_shexp_quote_cp(sgcp->sgc_patdat, FAL0));
   }
   goto jleave;
}

static int
a_shexp__globsort(void const *cvpa, void const *cvpb){
   int rv;
   struct n_strlist const * const *slpa, * const *slpb;
   n_NYD2_IN;

   slpa = cvpa;
   slpb = cvpb;
   rv = su_cs_cmp_case((*slpa)->sl_dat, (*slpb)->sl_dat);
   n_NYD2_OU;
   return rv;
}
#endif /* mx_HAVE_FNMATCH */

static void
a_shexp__quote(struct a_shexp_quote_ctx *sqcp, struct a_shexp_quote_lvl *sqlp){
   /* XXX Because of the problems caused by ISO C multibyte interface we cannot
    * XXX use the recursive implementation because of stateful encodings.
    * XXX I.e., if a quoted substring cannot be self-contained - the data after
    * XXX the quote relies on "the former state", then this doesn't make sense.
    * XXX Therefore this is not fully programmed out but instead only detects
    * XXX the "most fancy" quoting necessary, and directly does that.
    * XXX As a result of this, T_REVSOL and T_DOUBLE are not even considered.
    * XXX Otherwise we rather have to convert to wide first and act on that,
    * XXX e.g., call visual_info(n_VISUAL_INFO_WOUT_CREATE) on entire input */
#undef a_SHEXP_QUOTE_RECURSE /* XXX (Needs complete revisit, then) */
#ifdef a_SHEXP_QUOTE_RECURSE
# define jrecurse jrecurse
   struct a_shexp_quote_lvl sql;
#else
# define jrecurse jstep
#endif
   struct n_visual_info_ctx vic;
   union {struct a_shexp_quote_lvl *head; struct n_string *store;} u;
   ui32_t flags;
   size_t il;
   char const *ib, *ib_base;
   n_NYD2_IN;

   ib_base = ib = sqlp->sql_dat.s;
   il = sqlp->sql_dat.l;
   flags = sqlp->sql_flags;

   /* Iterate over the entire input, classify characters and type of quotes
    * along the way.  Whenever a quote change has to be applied, adjust flags
    * for the new situation -, setup sql.* and recurse- */
   while(il > 0){
      char c;

      c = *ib;
      if(su_cs_is_cntrl(c)){
         if(flags & a_SHEXP_QUOTE_T_DOLLAR)
            goto jstep;
         if(c == '\t' && (flags & (a_SHEXP_QUOTE_T_REVSOL |
               a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOUBLE)))
            goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
         ++sqcp->sqc_cnt_dollar;
#endif
         flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
         goto jrecurse;
      }else if(su_cs_is_space(c) || c == '|' || c == '&' || c == ';' ||
            /* Whereas we don't support those, quote them for the sh(1)ell */
            c == '(' || c == ')' || c == '<' || c == '>' ||
            c == '"' || c == '$'){
         if(flags & a_SHEXP_QUOTE_T_MASK)
            goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
         ++sqcp->sqc_cnt_single;
#endif
         flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_SINGLE;
         goto jrecurse;
      }else if(c == '\''){
         if(flags & (a_SHEXP_QUOTE_T_MASK & ~a_SHEXP_QUOTE_T_SINGLE))
            goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
         ++sqcp->sqc_cnt_dollar;
#endif
         flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
         goto jrecurse;
      }else if(c == '\\' || (c == '#' && ib == ib_base)){
         if(flags & a_SHEXP_QUOTE_T_MASK)
            goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
         ++sqcp->sqc_cnt_single;
#endif
         flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_SINGLE;
         goto jrecurse;
      }else if(!su_cs_is_ascii(c)){
         /* Need to keep together multibytes */
#ifdef a_SHEXP_QUOTE_RECURSE
         memset(&vic, 0, sizeof vic);
         vic.vic_indat = ib;
         vic.vic_inlen = il;
         n_visual_info(&vic,
            n_VISUAL_INFO_ONE_CHAR | n_VISUAL_INFO_SKIP_ERRORS);
#endif
         /* xxx check whether resulting \u would be ASCII */
         if(!(flags & a_SHEXP_QUOTE_ROUNDTRIP) ||
               (flags & a_SHEXP_QUOTE_T_DOLLAR)){
#ifdef a_SHEXP_QUOTE_RECURSE
            ib = vic.vic_oudat;
            il = vic.vic_oulen;
            continue;
#else
            goto jstep;
#endif
         }
#ifdef a_SHEXP_QUOTE_RECURSE
         ++sqcp->sqc_cnt_dollar;
#endif
         flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
         goto jrecurse;
      }else
jstep:
         ++ib, --il;
   }
   sqlp->sql_flags = flags;

   /* Level made the great and completed processing input.  Reverse the list of
    * levels, detect the "most fancy" quote type needed along this way */
   /* XXX Due to restriction as above very crude */
   for(flags = 0, il = 0, u.head = NULL; sqlp != NULL;){
      struct a_shexp_quote_lvl *tmp;

      tmp = sqlp->sql_link;
      sqlp->sql_link = u.head;
      u.head = sqlp;
      il += sqlp->sql_dat.l;
      if(sqlp->sql_flags & a_SHEXP_QUOTE_T_MASK)
         il += (sqlp->sql_dat.l >> 1);
      flags |= sqlp->sql_flags;
      sqlp = tmp;
   }
   sqlp = u.head;

   /* Finally work the substrings in the correct order, adjusting quotes along
    * the way as necessary.  Start off with the "most fancy" quote, so that
    * the user sees an overall boundary she can orientate herself on.
    * We do it like that to be able to give the user some "encapsulation
    * experience", to address what strikes me is a problem of sh(1)ell quoting:
    * different to, e.g., perl(1), where you see at a glance where a string
    * starts and ends, sh(1) quoting occurs at the "top level", disrupting the
    * visual appearance of "a string" as such */
   u.store = n_string_reserve(sqcp->sqc_store, il);

   if(flags & a_SHEXP_QUOTE_T_DOLLAR){
      u.store = n_string_push_buf(u.store, "$'", sizeof("$'") -1);
      flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
   }else if(flags & a_SHEXP_QUOTE_T_DOUBLE){
      u.store = n_string_push_c(u.store, '"');
      flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOUBLE;
   }else if(flags & a_SHEXP_QUOTE_T_SINGLE){
      u.store = n_string_push_c(u.store, '\'');
      flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_SINGLE;
   }else /*if(flags & a_SHEXP_QUOTE_T_REVSOL)*/
      flags &= ~a_SHEXP_QUOTE_T_MASK;

   /* Work all the levels */
   for(; sqlp != NULL; sqlp = sqlp->sql_link){
      /* As necessary update our mode of quoting */
#ifdef a_SHEXP_QUOTE_RECURSE
      il = 0;

      switch(sqlp->sql_flags & a_SHEXP_QUOTE_T_MASK){
      case a_SHEXP_QUOTE_T_DOLLAR:
         if(!(flags & a_SHEXP_QUOTE_T_DOLLAR))
            il = a_SHEXP_QUOTE_T_DOLLAR;
         break;
      case a_SHEXP_QUOTE_T_DOUBLE:
         if(!(flags & (a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR)))
            il = a_SHEXP_QUOTE_T_DOLLAR;
         break;
      case a_SHEXP_QUOTE_T_SINGLE:
         if(!(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE |
               a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR)))
            il = a_SHEXP_QUOTE_T_SINGLE;
         break;
      default:
      case a_SHEXP_QUOTE_T_REVSOL:
         if(!(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE |
               a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR)))
            il = a_SHEXP_QUOTE_T_REVSOL;
         break;
      }

      if(il != 0){
         if(flags & (a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOLLAR))
            u.store = n_string_push_c(u.store, '\'');
         else if(flags & a_SHEXP_QUOTE_T_DOUBLE)
            u.store = n_string_push_c(u.store, '"');
         flags &= ~a_SHEXP_QUOTE_T_MASK;

         flags |= (ui32_t)il;
         if(flags & a_SHEXP_QUOTE_T_DOLLAR)
            u.store = n_string_push_buf(u.store, "$'", sizeof("$'") -1);
         else if(flags & a_SHEXP_QUOTE_T_DOUBLE)
            u.store = n_string_push_c(u.store, '"');
         else if(flags & a_SHEXP_QUOTE_T_SINGLE)
            u.store = n_string_push_c(u.store, '\'');
      }
#endif /* a_SHEXP_QUOTE_RECURSE */

      /* Work the level's substring */
      ib = sqlp->sql_dat.s;
      il = sqlp->sql_dat.l;

      while(il > 0){
         char c2, c;

         c = *ib;

         if(su_cs_is_cntrl(c)){
            assert(c == '\t' || (flags & a_SHEXP_QUOTE_T_DOLLAR));
            assert((flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE |
               a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR)));
            switch((c2 = c)){
            case 0x07: c = 'a'; break;
            case 0x08: c = 'b'; break;
            case 0x0A: c = 'n'; break;
            case 0x0B: c = 'v'; break;
            case 0x0C: c = 'f'; break;
            case 0x0D: c = 'r'; break;
            case 0x1B: c = 'E'; break;
            default: break;
            case 0x09:
               if(flags & a_SHEXP_QUOTE_T_DOLLAR){
                  c = 't';
                  break;
               }
               if(flags & a_SHEXP_QUOTE_T_REVSOL)
                  u.store = n_string_push_c(u.store, '\\');
               goto jpush;
            }
            u.store = n_string_push_c(u.store, '\\');
            if(c == c2){
               u.store = n_string_push_c(u.store, 'c');
               c ^= 0x40;
            }
            goto jpush;
         }else if(su_cs_is_space(c) || c == '|' || c == '&' || c == ';' ||
               /* Whereas we do not support those, quote them for sh(1)ell */
               c == '(' || c == ')' || c == '<' || c == '>' ||
               c == '"' || c == '$'){
            if(flags & (a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOLLAR))
               goto jpush;
            assert(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_DOUBLE));
            u.store = n_string_push_c(u.store, '\\');
            goto jpush;
         }else if(c == '\''){
            if(flags & a_SHEXP_QUOTE_T_DOUBLE)
               goto jpush;
            assert(!(flags & a_SHEXP_QUOTE_T_SINGLE));
            u.store = n_string_push_c(u.store, '\\');
            goto jpush;
         }else if(c == '\\' || (c == '#' && ib == ib_base)){
            if(flags & a_SHEXP_QUOTE_T_SINGLE)
               goto jpush;
            assert(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_DOUBLE |
               a_SHEXP_QUOTE_T_DOLLAR));
            u.store = n_string_push_c(u.store, '\\');
            goto jpush;
         }else if(su_cs_is_ascii(c)){
            /* Shorthand: we can simply push that thing out */
jpush:
            u.store = n_string_push_c(u.store, c);
            ++ib, --il;
         }else{
            /* Not an ASCII character, take care not to split up multibyte
             * sequences etc.  For the sake of compile testing, don't enwrap in
             * mx_HAVE_ALWAYS_UNICODE_LOCALE || mx_HAVE_NATCH_CHAR */
            if(n_psonce & n_PSO_UNICODE){
               ui32_t uc;
               char const *ib2;
               size_t il2, il3;

               ib2 = ib;
               il2 = il;
               if((uc = su_utf_8_to_32(&ib2, &il2)) != UI32_MAX){
                  char itoa[32];
                  char const *cp;

                  il2 = PTR2SIZE(&ib2[0] - &ib[0]);
                  if((flags & a_SHEXP_QUOTE_ROUNDTRIP) || uc == 0xFFFDu){
                     /* Use padding to make ambiguities impossible */
                     il3 = snprintf(itoa, sizeof itoa, "\\%c%0*X",
                           (uc > 0xFFFFu ? 'U' : 'u'),
                           (int)(uc > 0xFFFFu ? 8 : 4), uc);
                     cp = itoa;
                  }else{
                     il3 = il2;
                     cp = &ib[0];
                  }
                  u.store = n_string_push_buf(u.store, cp, il3);
                  ib += il2, il -= il2;
                  continue;
               }
            }

            memset(&vic, 0, sizeof vic);
            vic.vic_indat = ib;
            vic.vic_inlen = il;
            n_visual_info(&vic,
               n_VISUAL_INFO_ONE_CHAR | n_VISUAL_INFO_SKIP_ERRORS);

            /* Work this substring as sensitive as possible */
            il -= vic.vic_oulen;
            if(!(flags & a_SHEXP_QUOTE_ROUNDTRIP))
               u.store = n_string_push_buf(u.store, ib, il);
#ifdef mx_HAVE_ICONV
            else if((vic.vic_indat = n_iconv_onetime_cp(n_ICONV_NONE,
                  "utf-8", ok_vlook(ttycharset), savestrbuf(ib, il))) != NULL){
               ui32_t uc;
               char const *ib2;
               size_t il2, il3;

               il2 = su_cs_len(ib2 = vic.vic_indat);
               if((uc = su_utf_8_to_32(&ib2, &il2)) != UI32_MAX){
                  char itoa[32];

                  il2 = PTR2SIZE(&ib2[0] - &vic.vic_indat[0]);
                  /* Use padding to make ambiguities impossible */
                  il3 = snprintf(itoa, sizeof itoa, "\\%c%0*X",
                        (uc > 0xFFFFu ? 'U' : 'u'),
                        (int)(uc > 0xFFFFu ? 8 : 4), uc);
                  u.store = n_string_push_buf(u.store, itoa, il3);
               }else
                  goto Jxseq;
            }
#endif
            else
#ifdef mx_HAVE_ICONV
                 Jxseq:
#endif
                        while(il-- > 0){
               u.store = n_string_push_buf(u.store, "\\xFF",
                     sizeof("\\xFF") -1);
               n_c_to_hex_base16(&u.store->s_dat[u.store->s_len - 2], *ib++);
            }

            ib = vic.vic_oudat;
            il = vic.vic_oulen;
         }
      }
   }

   /* Close an open quote */
   if(flags & (a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOLLAR))
      u.store = n_string_push_c(u.store, '\'');
   else if(flags & a_SHEXP_QUOTE_T_DOUBLE)
      u.store = n_string_push_c(u.store, '"');
#ifdef a_SHEXP_QUOTE_RECURSE
jleave:
#endif
   n_NYD2_OU;
   return;

#ifdef a_SHEXP_QUOTE_RECURSE
jrecurse:
   sqlp->sql_dat.l -= il;

   sql.sql_link = sqlp;
   sql.sql_dat.s = n_UNCONST(ib);
   sql.sql_dat.l = il;
   sql.sql_flags = flags;
   a_shexp__quote(sqcp, &sql);
   goto jleave;
#endif

#undef jrecurse
#undef a_SHEXP_QUOTE_RECURSE
}

FL char *
fexpand(char const *name, enum fexp_mode fexpm) /* TODO in parts: -> URL::!! */
{
   struct str proto, s;
   char const *res, *cp;
   bool_t dyn, haveproto;
   n_NYD_IN;

   n_pstate &= ~n_PS_EXPAND_MULTIRESULT;
   dyn = FAL0;

   /* The order of evaluation is "%" and "#" expand into constants.
    * "&" can expand into "+".  "+" can expand into shell meta characters.
    * Shell meta characters expand into constants.
    * This way, we make no recursive expansion */
   if((fexpm & FEXP_NSHORTCUT) || (res = shortcut_expand(name)) == NULL)
      res = n_UNCONST(name);

jprotonext:
   n_UNINIT(proto.s, NULL), n_UNINIT(proto.l, 0);
   haveproto = FAL0;
   for(cp = res; *cp && *cp != ':'; ++cp)
      if(!su_cs_is_alnum(*cp))
         goto jnoproto;
   if(cp[0] == ':' && cp[1] == '/' && cp[2] == '/'){
      haveproto = TRU1;
      proto.s = n_UNCONST(res);
      cp += 3;
      proto.l = PTR2SIZE(cp - res);
      res = cp;
   }

jnoproto:
   if(!(fexpm & FEXP_NSPECIAL)){
jnext:
      dyn = FAL0;
      switch(*res){
      case '%':
         if(res[1] == ':' && res[2] != '\0'){
            res = &res[2];
            goto jprotonext;
         }else{
            bool_t force;

            force = (res[1] != '\0');
            res = a_shexp_findmail((force ? &res[1] : ok_vlook(LOGNAME)),
                  force);
            if(force)
               goto jislocal;
         }
         goto jnext;
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
      default:
         break;
      }
   }

#ifdef mx_HAVE_IMAP
   if(res[0] == '@' && which_protocol(mailname, FAL0, FAL0, NULL)
         == PROTO_IMAP){
      res = str_concat_csvl(&s, protbase(mailname), "/", &res[1], NULL)->s;
      dyn = TRU1;
   }
#endif

   /* POSIX: if *folder* unset or null, "+" shall be retained */
   if(!(fexpm & FEXP_NFOLDER) && *res == '+' &&
         *(cp = n_folder_query()) != '\0'){
      res = str_concat_csvl(&s, cp, &res[1], NULL)->s;
      dyn = TRU1;
   }

   /* Do some meta expansions */
   if((fexpm & (FEXP_NSHELL | FEXP_NVAR)) != FEXP_NVAR &&
         ((fexpm & FEXP_NSHELL) ? (su_cs_find_c(res, '$') != NULL)
          : (su_cs_first_of(res, "{}[]*?$") != su_UZ_MAX))){
      bool_t doexp;

      if(fexpm & FEXP_NOPROTO)
         doexp = TRU1;
      else{
         cp = haveproto ? savecat(savestrbuf(proto.s, proto.l), res) : res;

         switch(which_protocol(cp, TRU1, FAL0, NULL)){
         case PROTO_FILE:
         case PROTO_MAILDIR:
            doexp = TRU1;
            break;
         default:
            doexp = FAL0;
            break;
         }
      }

      if(doexp){
         struct str shin;
         struct n_string shou, *shoup;

         shin.s = n_UNCONST(res);
         shin.l = UIZ_MAX;
         shoup = n_string_creat_auto(&shou);
         for(;;){
            enum n_shexp_state shs;

            /* TODO shexp: take care: not include backtick eval once avail! */
            shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG_D_V |
                  n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
                  n_SHEXP_PARSE_QUOTE_AUTO_DQ |
                  n_SHEXP_PARSE_QUOTE_AUTO_CLOSE), shoup, &shin, NULL);
            if(shs & n_SHEXP_STATE_STOP)
               break;
         }
         res = n_string_cp(shoup);
         /*shoup = n_string_drop_ownership(shoup);*/
         dyn = TRU1;

         if(res[0] == '~')
            res = a_shexp_tilde(res);

         if(!(fexpm & FEXP_NSHELL) &&
               (res = a_shexp_globname(res, fexpm)) == NULL)
            goto jleave;
         dyn = TRU1;
      }/* else no tilde */
   }else if(res[0] == '~'){
      res = a_shexp_tilde(res);
      dyn = TRU1;
   }

jislocal:
   if(res != NULL && haveproto){
      res = savecat(savestrbuf(proto.s, proto.l), res);
      dyn = TRU1;
   }

   if(fexpm & (FEXP_LOCAL | FEXP_LOCAL_FILE)){
      switch (which_protocol(res, FAL0, FAL0, &cp)) {
      case PROTO_MAILDIR:
         if(!(fexpm & FEXP_LOCAL_FILE)){
         /* FALLTHRU */
      case PROTO_FILE:
            if(fexpm & FEXP_LOCAL_FILE){
               res = cp;
               dyn = FAL0;
            }
            break;
         }
         /* FALLTHRU */
      default:
         n_err(_("Not a local file or directory: %s\n"),
            n_shexp_quote_cp(name, FAL0));
         res = NULL;
         break;
      }
   }

jleave:
   if(res != NULL && !dyn)
      res = savestr(res);
   n_NYD_OU;
   return n_UNCONST(res);
}

FL enum n_shexp_state
n_shexp_parse_token(enum n_shexp_parse_flags flags, struct n_string *store,
      struct str *input, void const **cookie){
   /* TODO shexp_parse_token: WCHAR
    * TODO This needs to be rewritten in order to support $(( )) and $( )
    * TODO and ${xyYZ} and the possibly infinite recursion they bring along,
    * TODO too.  We need a carrier struct, then, and can nicely split this
    * TODO big big thing up in little pieces!
    * TODO This means it should produce a tree of objects, so that callees
    * TODO can recognize whether something happened inside single/double etc.
    * TODO quotes; e.g., to requote "'[a-z]'" to, e.g., "\[a-z]", etc.! */
   ui32_t last_known_meta_trim_len;
   char c2, c, quotec, utf[8];
   enum n_shexp_state rv;
   size_t i, il;
   char const *ifs, *ifs_ws, *ib_save, *ib;
   enum{
      a_NONE = 0,
      a_SKIPQ = 1u<<0,     /* Skip rest of this quote (\u0 ..) */
      a_SKIPT = 1u<<1,     /* Skip entire token (\c@) */
      a_SKIPMASK = a_SKIPQ | a_SKIPT,
      a_SURPLUS = 1u<<2,   /* Extended sequence interpretation */
      a_NTOKEN = 1u<<3,    /* "New token": e.g., comments are possible */
      a_BRACE = 1u<<4,     /* Variable substitution: brace enclosed */
      a_DIGIT1 = 1u<<5,    /* ..first character was digit */
      a_NONDIGIT = 1u<<6,  /* ..has seen any non-digits */
      a_VARSUBST_MASK = n_BITENUM_MASK(4, 6),

      a_ROUND_MASK = a_SKIPT | (int)~n_BITENUM_MASK(0, 7),
      a_COOKIE = 1u<<8,
      a_EXPLODE = 1u<<9,
      a_CONSUME = 1u<<10,  /* When done, "consume" remaining input */
      a_TMP = 1u<<30
   } state;
   n_NYD2_IN;

   assert((flags & n_SHEXP_PARSE_DRYRUN) || store != NULL);
   assert(input != NULL);
   assert(input->l == 0 || input->s != NULL);
   assert(!(flags & n_SHEXP_PARSE_LOG) || !(flags & n_SHEXP_PARSE_LOG_D_V));
   assert(!(flags & n_SHEXP_PARSE_IFS_ADD_COMMA) ||
      !(flags & n_SHEXP_PARSE_IFS_IS_COMMA));
   assert(!(flags & n_SHEXP_PARSE_QUOTE_AUTO_FIXED) ||
      (flags & n__SHEXP_PARSE_QUOTE_AUTO_MASK));

   if((flags & n_SHEXP_PARSE_LOG_D_V) && (n_poption & n_PO_D_V))
      flags |= n_SHEXP_PARSE_LOG;
   if(flags & n_SHEXP_PARSE_QUOTE_AUTO_FIXED)
      flags |= n_SHEXP_PARSE_QUOTE_AUTO_CLOSE;

   if((flags & n_SHEXP_PARSE_TRUNC) && store != NULL)
      store = n_string_trunc(store, 0);

   if(flags & (n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_TRIM_IFSSPACE)){
      ifs = ok_vlook(ifs);
      ifs_ws = ok_vlook(ifs_ws);
   }else{
      n_UNINIT(ifs, n_empty);
      n_UNINIT(ifs_ws, n_empty);
   }

   state = a_NONE;
   ib = input->s;
   if((il = input->l) == UIZ_MAX)
      input->l = il = su_cs_len(ib);
   n_UNINIT(c, '\0');

   if(cookie != NULL && *cookie != NULL){
      assert(!(flags & n_SHEXP_PARSE_DRYRUN));
      state |= a_COOKIE;
   }

   rv = n_SHEXP_STATE_NONE;
jrestart_empty:
   rv &= n_SHEXP_STATE_WS_LEAD;
   state &= a_ROUND_MASK;

   /* In cookie mode, the next ARGV entry is the token already, unchanged,
    * since it has already been expanded before! */
   if(state & a_COOKIE){
      char const * const *xcookie, *cp;

      i = store->s_len;
      xcookie = *cookie;
      if((store = n_string_push_cp(store, *xcookie))->s_len > 0)
         rv |= n_SHEXP_STATE_OUTPUT;
      if(*++xcookie == NULL){
         *cookie = NULL;
         state &= ~a_COOKIE;
         flags |= n_SHEXP_PARSE_QUOTE_AUTO_DQ; /* ..why we are here! */
      }else
         *cookie = n_UNCONST(xcookie);

      for(cp = &n_string_cp(store)[i]; (c = *cp++) != '\0';)
         if(su_cs_is_cntrl(c)){
            rv |= n_SHEXP_STATE_CONTROL;
            break;
         }

      /* The last exploded cookie will join with the yielded input token, so
       * simply fall through in this case */
      if(state & a_COOKIE)
         goto jleave_quick;
   }else{
jrestart:
      if(flags & n_SHEXP_PARSE_TRIM_SPACE){
         for(; il > 0; ++ib, --il){
            if(!su_cs_is_space(*ib))
               break;
            rv |= n_SHEXP_STATE_WS_LEAD;
         }
      }

      if(flags & n_SHEXP_PARSE_TRIM_IFSSPACE){
         for(; il > 0; ++ib, --il){
            if(su_cs_find_c(ifs_ws, *ib) == NULL)
               break;
            rv |= n_SHEXP_STATE_WS_LEAD;
         }
      }

      input->s = n_UNCONST(ib);
      input->l = il;
   }

   if(il == 0){
      rv |= n_SHEXP_STATE_STOP;
      goto jleave;
   }

   if(store != NULL)
      store = n_string_reserve(store, n_MIN(il, 32)); /* XXX */

   switch(flags & n__SHEXP_PARSE_QUOTE_AUTO_MASK){
   case n_SHEXP_PARSE_QUOTE_AUTO_SQ:
      quotec = '\'';
      rv |= n_SHEXP_STATE_QUOTE;
      break;
   case n_SHEXP_PARSE_QUOTE_AUTO_DQ:
      quotec = '"';
      if(0){
   case n_SHEXP_PARSE_QUOTE_AUTO_DSQ:
         quotec = '\'';
      }
      rv |= n_SHEXP_STATE_QUOTE;
      state |= a_SURPLUS;
      break;
   default:
      quotec = '\0';
      state |= a_NTOKEN;
      break;
   }

   /* TODO n_SHEXP_PARSE_META_SEMICOLON++, well, hack: we are not the shell,
    * TODO we are not a language, and therefore the general *ifs-ws* and normal
    * TODO whitespace trimming that input lines undergo (in a_go_evaluate())
    * TODO has already happened, our result will be used *as is*, and therefore
    * TODO we need to be aware of and remove trailing unquoted WS that would
    * TODO otherwise remain, after we have seen a semicolon sequencer.
    * By sheer luck we only need to track this in non-quote-mode */
   last_known_meta_trim_len = UI32_MAX;

   while(il > 0){ /* {{{ */
      --il, c = *ib++;

      /* If no quote-mode active.. */
      if(quotec == '\0'){
         if(c == '"' || c == '\''){
            quotec = c;
            if(c == '"')
               state |= a_SURPLUS;
            else
               state &= ~a_SURPLUS;
            state &= ~a_NTOKEN;
            last_known_meta_trim_len = UI32_MAX;
            rv |= n_SHEXP_STATE_QUOTE;
            continue;
         }else if(c == '$'){
            if(il > 0){
               state &= ~a_NTOKEN;
               last_known_meta_trim_len = UI32_MAX;
               if(*ib == '\''){
                  --il, ++ib;
                  quotec = '\'';
                  state |= a_SURPLUS;
                  rv |= n_SHEXP_STATE_QUOTE;
                  continue;
               }else
                  goto J_var_expand;
            }
         }else if(c == '\\'){
            /* Outside of quotes this just escapes any next character, but
             * a sole <reverse solidus> at EOS is left unchanged */
             if(il > 0)
               --il, c = *ib++;
            state &= ~a_NTOKEN;
            last_known_meta_trim_len = UI32_MAX;
         }
         /* A comment may it be if no token has yet started */
         else if(c == '#' && (state & a_NTOKEN)){
            rv |= n_SHEXP_STATE_STOP;
            /*last_known_meta_trim_len = UI32_MAX;*/
            goto jleave;
         }
         /* Metacharacters that separate tokens must be turned on explicitly */
         else if(c == '|' && (flags & n_SHEXP_PARSE_META_VERTBAR)){
            rv |= n_SHEXP_STATE_META_VERTBAR;

            /* The parsed sequence may be _the_ output, so ensure we don't
             * include the metacharacter, then. */
            if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP))
               ++il, --ib;
            /*last_known_meta_trim_len = UI32_MAX;*/
            break;
         }else if(c == '&' && (flags & n_SHEXP_PARSE_META_AMPERSAND)){
            rv |= n_SHEXP_STATE_META_AMPERSAND;

            /* The parsed sequence may be _the_ output, so ensure we don't
             * include the metacharacter, then. */
            if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP))
               ++il, --ib;
            /*last_known_meta_trim_len = UI32_MAX;*/
            break;
         }else if(c == ';' && (flags & n_SHEXP_PARSE_META_SEMICOLON)){
            if(il > 0)
               n_go_input_inject(n_GO_INPUT_INJECT_COMMIT, ib, il);
            rv |= n_SHEXP_STATE_META_SEMICOLON | n_SHEXP_STATE_STOP;
            state |= a_CONSUME;
            if(!(flags & n_SHEXP_PARSE_DRYRUN) &&
                  (rv & n_SHEXP_STATE_OUTPUT) &&
                  last_known_meta_trim_len != UI32_MAX)
               store = n_string_trunc(store, last_known_meta_trim_len);

            /* The parsed sequence may be _the_ output, so ensure we don't
             * include the metacharacter, then. */
            if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP))
               ++il, --ib;
            /*last_known_meta_trim_len = UI32_MAX;*/
            break;
         }else if(c == ',' && (flags &
               (n_SHEXP_PARSE_IFS_ADD_COMMA | n_SHEXP_PARSE_IFS_IS_COMMA))){
            /* The parsed sequence may be _the_ output, so ensure we don't
             * include the metacharacter, then. */
            if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP))
               ++il, --ib;
            /*last_known_meta_trim_len = UI32_MAX;*/
            break;
         }else{
            ui8_t blnk;

            blnk = su_cs_is_blank(c) ? 1 : 0;
            blnk |= ((flags & (n_SHEXP_PARSE_IFS_VAR |
                     n_SHEXP_PARSE_TRIM_IFSSPACE)) &&
                  su_cs_find_c(ifs_ws, c) != NULL) ? 2 : 0;

            if((!(flags & n_SHEXP_PARSE_IFS_VAR) && (blnk & 1)) ||
                  ((flags & n_SHEXP_PARSE_IFS_VAR) &&
                     ((blnk & 2) || su_cs_find_c(ifs, c) != NULL))){
               if(!(flags & n_SHEXP_PARSE_IFS_IS_COMMA)){
                  /* The parsed sequence may be _the_ output, so ensure we do
                   * not include the metacharacter, then. */
                  if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP))
                     ++il, --ib;
                  /*last_known_meta_trim_len = UI32_MAX;*/
                  break;
               }
               state |= a_NTOKEN;
            }else
               state &= ~a_NTOKEN;

            if(blnk && store != NULL){
               if(last_known_meta_trim_len == UI32_MAX)
                  last_known_meta_trim_len = store->s_len;
            }else
               last_known_meta_trim_len = UI32_MAX;
         }
      }else{
         /* Quote-mode */
         assert(!(state & a_NTOKEN));
         if(c == quotec && !(flags & n_SHEXP_PARSE_QUOTE_AUTO_FIXED)){
            state &= a_ROUND_MASK;
            quotec = '\0';
            /* Users may need to recognize the presence of empty quotes */
            rv |= n_SHEXP_STATE_OUTPUT;
            continue;
         }else if(c == '\\' && (state & a_SURPLUS)){
            ib_save = ib - 1;
            /* A sole <reverse solidus> at EOS is treated as-is!  This is ok
             * since the "closing quote" error will occur next, anyway */
            if(il == 0)
               ;
            else if((c2 = *ib) == quotec){
               --il, ++ib;
               c = quotec;
            }else if(quotec == '"'){
               /* Double quotes, POSIX says:
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
                  if(state & a_SKIPMASK)
                     continue;
                  /* ASCII C0: 0..1F, 7F <- @.._ (+ a-z -> A-Z), ? */
                  c = su_cs_to_upper(c2) ^ 0x40;
                  if((ui8_t)c > 0x1F && c != 0x7F){
                     if(flags & n_SHEXP_PARSE_LOG)
                        n_err(_("Invalid \\c notation: %.*s: %.*s\n"),
                           (int)input->l, input->s,
                           (int)PTR2SIZE(ib - ib_save), ib_save);
                     rv |= n_SHEXP_STATE_ERR_CONTROL;
                  }
                  /* As an implementation-defined extension, support \c@
                   * EQ printf(1) alike \c */
                  if(c == '\0'){
                     state |= a_SKIPT;
                     continue;
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
                     if(!(state & a_SKIPMASK) && (ui8_t)c2 > 0x1F){
                        rv |= n_SHEXP_STATE_ERR_NUMBER;
                        --il, ++ib;
                        if(flags & n_SHEXP_PARSE_LOG)
                           n_err(_("\\0 argument exceeds byte: %.*s: %.*s\n"),
                              (int)input->l, input->s,
                              (int)PTR2SIZE(ib - ib_save), ib_save);
                        /* Write unchanged */
jerr_ib_save:
                        rv |= n_SHEXP_STATE_OUTPUT;
                        if(!(flags & n_SHEXP_PARSE_DRYRUN))
                           store = n_string_push_buf(store, ib_save,
                                 PTR2SIZE(ib - ib_save));
                        continue;
                     }
                     c2 = (c2 << 3) | (c -= '0');
                     --il, ++ib;
                  }
                  if(state & a_SKIPMASK)
                     continue;
                  if((c = c2) == '\0'){
                     state |= a_SKIPQ;
                     continue;
                  }
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

                     i = n_MIN(il, i);
                     for(no = j = 0; i-- > 0; --il, ++ib, ++j){
                        c = *ib;
                        if(su_cs_is_xdigit(c)){
                           no <<= 4;
                           no += hexatoi[(ui8_t)((c) - ((c) <= '9' ? 48
                                 : ((c) <= 'F' ? 55 : 87)))];
                        }else if(j == 0){
                           if(state & a_SKIPMASK)
                              break;
                           c2 = (c2 == 'U' || c2 == 'u') ? 'u' : 'x';
                           if(flags & n_SHEXP_PARSE_LOG)
                              n_err(_("Invalid \\%c notation: %.*s: %.*s\n"),
                                 c2, (int)input->l, input->s,
                                 (int)PTR2SIZE(ib - ib_save), ib_save);
                           rv |= n_SHEXP_STATE_ERR_NUMBER;
                           goto jerr_ib_save;
                        }else
                           break;
                     }

                     /* Unicode massage */
                     if((c2 != 'U' && c2 != 'u') || su_cs_is_ascii(no)){
                        if((c = (char)no) == '\0')
                           state |= a_SKIPQ;
                     }else if(no == 0)
                        state |= a_SKIPQ;
                     else if(!(state & a_SKIPMASK)){
                        if(!(flags & n_SHEXP_PARSE_DRYRUN))
                           store = n_string_reserve(store, n_MAX(j, 4));

                        if(no > 0x10FFFF){ /* XXX magic; CText */
                           if(flags & n_SHEXP_PARSE_LOG)
                              n_err(_("\\U argument exceeds 0x10FFFF: %.*s: "
                                    "%.*s\n"),
                                 (int)input->l, input->s,
                                 (int)PTR2SIZE(ib - ib_save), ib_save);
                           rv |= n_SHEXP_STATE_ERR_NUMBER;
                           /* But normalize the output anyway */
                           goto Jerr_uni_norm;
                        }

                        j = su_utf_32_to_8(no, utf);

                        if(n_psonce & n_PSO_UNICODE){
                           rv |= n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_UNICODE;
                           if(!(flags & n_SHEXP_PARSE_DRYRUN))
                              store = n_string_push_buf(store, utf, j);
                           continue;
                        }
#ifdef mx_HAVE_ICONV
                        else{
                           char *icp;

                           icp = n_iconv_onetime_cp(n_ICONV_NONE,
                                 NULL, NULL, utf);
                           if(icp != NULL){
                              rv |= n_SHEXP_STATE_OUTPUT;
                              if(!(flags & n_SHEXP_PARSE_DRYRUN))
                                 store = n_string_push_cp(store, icp);
                              continue;
                           }
                        }
#endif
                        if(!(flags & n_SHEXP_PARSE_DRYRUN)) Jerr_uni_norm:{
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
                     if(state & a_SKIPMASK)
                        continue;
                  }
                  break;

               /* Extension: \$ can be used to expand a variable.
                * B(ug|ad) effect: if conversion fails, not written "as-is" */
               case '$':
                  if(il == 0)
                     goto j_dollar_ungetc;
                  goto J_var_expand;

               default:
j_dollar_ungetc:
                  /* Follow bash(1) behaviour, print sequence unchanged */
                  ++il, --ib;
                  break;
               }
            }
         }else if(c == '$' && quotec == '"' && il > 0) J_var_expand:{
            state &= ~a_VARSUBST_MASK;
            if(*ib == '{')
               state |= a_BRACE;

            /* Scan variable name */
            if(!(state & a_BRACE) || il > 1){
               char const *cp, *vp;

               ib_save = ib - 1;
               if(state & a_BRACE)
                  --il, ++ib;
               vp = ib;
               state &= ~a_EXPLODE;

               for(i = 0; il > 0; --il, ++ib, ++i){
                  /* We have some special cases regarding special parameters,
                   * so ensure these don't cause failure.  This code has
                   * counterparts in code that manages internal variables! */
                  c = *ib;
                  if(!a_SHEXP_ISVARC(c)){
                     if(i == 0){
                        /* Simply skip over multiplexer */
                        if(c == '^')
                           continue;
                        if(c == '*' || c == '@' || c == '#' || c == '?' ||
                              c == '!'){
                           if(c == '@'){
                              if(quotec == '"')
                                 state |= a_EXPLODE;
                           }
                           --il, ++ib;
                           ++i;
                        }
                     }
                     break;
                  }else if(a_SHEXP_ISVARC_BAD1ST(c)){
                     if(i == 0)
                        state |= a_DIGIT1;
                  }else
                     state |= a_NONDIGIT;
               }

               /* In skip mode, be easy and.. skip over */
               if(state & a_SKIPMASK){
                  if((state & a_BRACE) && il > 0 && *ib == '}')
                     --il, ++ib;
                  continue;
               }

               /* Handle the scan error cases */
               if((state & (a_DIGIT1 | a_NONDIGIT)) ==
                     (a_DIGIT1 | a_NONDIGIT)){
                  if(state & a_BRACE){
                     if(il > 0 && *ib == '}')
                        --il, ++ib;
                     else
                        rv |= n_SHEXP_STATE_ERR_GROUPOPEN;
                  }
                  if(flags & n_SHEXP_PARSE_LOG)
                     n_err(_("Invalid identifier for ${}: %.*s: %.*s\n"),
                        (int)input->l, input->s,
                        (int)PTR2SIZE(ib - ib_save), ib_save);
                  rv |= n_SHEXP_STATE_ERR_IDENTIFIER;
                  goto jerr_ib_save;
               }else if(i == 0){
                  if(state & a_BRACE){
                     if(il == 0 || *ib != '}'){
                        if(flags & n_SHEXP_PARSE_LOG)
                           n_err(_("No closing brace for ${}: %.*s: %.*s\n"),
                              (int)input->l, input->s,
                              (int)PTR2SIZE(ib - ib_save), ib_save);
                        rv |= n_SHEXP_STATE_ERR_GROUPOPEN;
                        goto jerr_ib_save;
                     }
                     --il, ++ib;

                     if(i == 0){
                        if(flags & n_SHEXP_PARSE_LOG)
                           n_err(_("Bad substitution for ${}: %.*s: %.*s\n"),
                              (int)input->l, input->s,
                              (int)PTR2SIZE(ib - ib_save), ib_save);
                        rv |= n_SHEXP_STATE_ERR_BADSUB;
                        goto jerr_ib_save;
                     }
                  }
                  /* Simply write dollar as-is? */
                  c = '$';
               }else{
                  if(state & a_BRACE){
                     if(il == 0 || *ib != '}'){
                        if(flags & n_SHEXP_PARSE_LOG)
                           n_err(_("No closing brace for ${}: %.*s: %.*s\n"),
                              (int)input->l, input->s,
                              (int)PTR2SIZE(ib - ib_save), ib_save);
                        rv |= n_SHEXP_STATE_ERR_GROUPOPEN;
                        goto jerr_ib_save;
                     }
                     --il, ++ib;

                     if(i == 0){
                        if(flags & n_SHEXP_PARSE_LOG)
                           n_err(_("Bad substitution for ${}: %.*s: %.*s\n"),
                              (int)input->l, input->s,
                              (int)PTR2SIZE(ib - ib_save), ib_save);
                        rv |= n_SHEXP_STATE_ERR_BADSUB;
                        goto jerr_ib_save;
                     }
                  }

                  if(flags & n_SHEXP_PARSE_DRYRUN)
                     continue;

                  /* We may shall explode "${@}" to a series of successive,
                   * properly quoted tokens (instead).  The first exploded
                   * cookie will join with the current token */
                  if(n_UNLIKELY(state & a_EXPLODE) &&
                        !(flags & n_SHEXP_PARSE_DRYRUN) && cookie != NULL){
                     if(n_var_vexplode(cookie))
                        state |= a_COOKIE;
                     /* On the other hand, if $@ expands to nothing and is the
                      * sole content of this quote then act like the shell does
                      * and throw away the entire atxplode construct */
                     else if(!(rv & n_SHEXP_STATE_OUTPUT) &&
                           il == 1 && *ib == '"' &&
                           ib_save == &input->s[1] && ib_save[-1] == '"')
                        ++ib, --il;
                     else
                        continue;
                     input->s = n_UNCONST(ib);
                     input->l = il;
                     goto jrestart_empty;
                  }

                  /* Check getenv(3) shall no internal variable exist!
                   * XXX We have some common idioms, avoid memory for them
                   * XXX Even better would be var_vlook_buf()! */
                  if(i == 1){
                     switch(*vp){
                     case '?': vp = n_qm; break;
                     case '!': vp = n_em; break;
                     case '*': vp = n_star; break;
                     case '@': vp = n_at; break;
                     case '#': vp = n_ns; break;
                     default: goto j_var_look_buf;
                     }
                  }else
j_var_look_buf:
                     vp = savestrbuf(vp, i);

                  if((cp = n_var_vlook(vp, TRU1)) != NULL){
                     rv |= n_SHEXP_STATE_OUTPUT;
                     store = n_string_push_cp(store, cp);
                     for(; (c = *cp) != '\0'; ++cp)
                        if(su_cs_is_cntrl(c)){
                           rv |= n_SHEXP_STATE_CONTROL;
                           break;
                        }
                  }
                  continue;
               }
            }
         }else if(c == '`' && quotec == '"' && il > 0){ /* TODO sh command */
            continue;
         }
      }

      if(!(state & a_SKIPMASK)){
         rv |= n_SHEXP_STATE_OUTPUT;
         if(su_cs_is_cntrl(c))
            rv |= n_SHEXP_STATE_CONTROL;
         if(!(flags & n_SHEXP_PARSE_DRYRUN))
            store = n_string_push_c(store, c);
      }
   } /* }}} */

   if(quotec != '\0' && !(flags & n_SHEXP_PARSE_QUOTE_AUTO_CLOSE)){
      if(flags & n_SHEXP_PARSE_LOG)
         n_err(_("No closing quote: %.*s\n"), (int)input->l, input->s);
      rv |= n_SHEXP_STATE_ERR_QUOTEOPEN;
   }

jleave:
   assert(!(state & a_COOKIE));
   if((flags & n_SHEXP_PARSE_DRYRUN) && store != NULL){
      store = n_string_push_buf(store, input->s, PTR2SIZE(ib - input->s));
      rv |= n_SHEXP_STATE_OUTPUT;
   }

   if(state & a_CONSUME){
      input->s = n_UNCONST(&ib[il]);
      input->l = 0;
   }else{
      if(flags & n_SHEXP_PARSE_TRIM_SPACE){
         for(; il > 0; ++ib, --il){
            if(!su_cs_is_space(*ib))
               break;
            rv |= n_SHEXP_STATE_WS_TRAIL;
         }
      }

      if(flags & n_SHEXP_PARSE_TRIM_IFSSPACE){
         for(; il > 0; ++ib, --il){
            if(su_cs_find_c(ifs_ws, *ib) == NULL)
               break;
            rv |= n_SHEXP_STATE_WS_TRAIL;
         }
      }

      input->l = il;
      input->s = n_UNCONST(ib);
   }

   if(!(rv & n_SHEXP_STATE_STOP)){
      if(!(rv & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_META_MASK)) &&
            (flags & n_SHEXP_PARSE_IGNORE_EMPTY) && il > 0)
         goto jrestart_empty;
      if(/*!(rv & n_SHEXP_STATE_OUTPUT) &&*/ il == 0)
         rv |= n_SHEXP_STATE_STOP;
   }

   if((state & a_SKIPT) && !(rv & n_SHEXP_STATE_STOP) &&
         (flags & n_SHEXP_PARSE_META_MASK))
      goto jrestart;
jleave_quick:
   assert((rv & n_SHEXP_STATE_OUTPUT) || !(rv & n_SHEXP_STATE_UNICODE));
   assert((rv & n_SHEXP_STATE_OUTPUT) || !(rv & n_SHEXP_STATE_CONTROL));
   n_NYD2_OU;
   return rv;
}

FL char *
n_shexp_parse_token_cp(enum n_shexp_parse_flags flags, char const **cp){
   struct str input;
   struct n_string sou, *soup;
   char *rv;
   enum n_shexp_state shs;
   n_NYD2_IN;

   assert(cp != NULL);

   input.s = n_UNCONST(*cp);
   input.l = UIZ_MAX;
   soup = n_string_creat_auto(&sou);

   shs = n_shexp_parse_token(flags, soup, &input, NULL);
   if(shs & n_SHEXP_STATE_ERR_MASK){
      soup = n_string_assign_cp(soup, *cp);
      *cp = NULL;
   }else
      *cp = input.s;

   rv = n_string_cp(soup);
   /*n_string_gut(n_string_drop_ownership(soup));*/
   n_NYD2_OU;
   return rv;
}

FL struct n_string *
n_shexp_quote(struct n_string *store, struct str const *input, bool_t rndtrip){
   struct a_shexp_quote_lvl sql;
   struct a_shexp_quote_ctx sqc;
   n_NYD2_IN;

   assert(store != NULL);
   assert(input != NULL);
   assert(input->l == 0 || input->s != NULL);

   memset(&sqc, 0, sizeof sqc);
   sqc.sqc_store = store;
   sqc.sqc_input.s = input->s;
   if((sqc.sqc_input.l = input->l) == UIZ_MAX)
      sqc.sqc_input.l = su_cs_len(input->s);
   sqc.sqc_flags = rndtrip ? a_SHEXP_QUOTE_ROUNDTRIP : a_SHEXP_QUOTE_NONE;

   if(sqc.sqc_input.l == 0)
      store = n_string_push_buf(store, "''", sizeof("''") -1);
   else{
      memset(&sql, 0, sizeof sql);
      sql.sql_dat = sqc.sqc_input;
      sql.sql_flags = sqc.sqc_flags;
      a_shexp__quote(&sqc, &sql);
   }
   n_NYD2_OU;
   return store;
}

FL char *
n_shexp_quote_cp(char const *cp, bool_t rndtrip){
   struct n_string store;
   struct str input;
   char *rv;
   n_NYD2_IN;

   assert(cp != NULL);

   input.s = n_UNCONST(cp);
   input.l = UIZ_MAX;
   rv = n_string_cp(n_shexp_quote(n_string_creat_auto(&store), &input,
         rndtrip));
   n_string_gut(n_string_drop_ownership(&store));
   n_NYD2_OU;
   return rv;
}

FL bool_t
n_shexp_is_valid_varname(char const *name){
   char lc, c;
   bool_t rv;
   n_NYD2_IN;

   rv = FAL0;

   for(lc = '\0'; (c = *name++) != '\0'; lc = c)
      if(!a_SHEXP_ISVARC(c))
         goto jleave;
      else if(lc == '\0' && a_SHEXP_ISVARC_BAD1ST(c))
         goto jleave;
   if(a_SHEXP_ISVARC_BADNST(lc))
      goto jleave;

   rv = TRU1;
jleave:
   n_NYD2_OU;
   return rv;
}

FL int
c_shcodec(void *vp){
   struct str in;
   struct n_string sou_b, *soup;
   si32_t nerrn;
   size_t alen;
   bool_t norndtrip;
   char const **argv, *varname, *act, *cp;

   soup = n_string_creat_auto(&sou_b);
   argv = vp;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;

   act = *argv;
   for(cp = act; *cp != '\0' && !su_cs_is_space(*cp); ++cp)
      ;
   if((norndtrip = (*act == '+')))
      ++act;
   if(act == cp)
      goto jesynopsis;
   alen = PTR2SIZE(cp - act);
   if(*cp != '\0')
      ++cp;

   in.l = su_cs_len(in.s = n_UNCONST(cp));
   nerrn = su_ERR_NONE;

   if(su_cs_starts_with_case_n("encode", act, alen))
      soup = n_shexp_quote(soup, &in, !norndtrip);
   else if(!norndtrip && su_cs_starts_with_case_n("decode", act, alen)){
      for(;;){
         enum n_shexp_state shs;

         shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
               n_SHEXP_PARSE_IGNORE_EMPTY), soup, &in, NULL);
         if(shs & n_SHEXP_STATE_ERR_MASK){
            soup = n_string_assign_cp(soup, cp);
            nerrn = su_ERR_CANCELED;
            vp = NULL;
            break;
         }
         if(shs & n_SHEXP_STATE_STOP)
            break;
      }
   }else
      goto jesynopsis;

   if(varname != NULL){
      cp = n_string_cp(soup);
      if(!n_var_vset(varname, (uintptr_t)cp)){
         nerrn = su_ERR_NOTSUP;
         vp = NULL;
      }
   }else{
      struct str out;

      in.s = n_string_cp(soup);
      in.l = soup->s_len;
      makeprint(&in, &out);
      if(fprintf(n_stdout, "%s\n", out.s) < 0){
         nerrn = su_err_no();
         vp = NULL;
      }
      n_free(out.s);
   }

jleave:
   n_pstate_err_no = nerrn;
   n_NYD_OU;
   return (vp != NULL ? 0 : 1);
jesynopsis:
   n_err(_("Synopsis: shcodec: <[+]e[ncode]|d[ecode]> <rest-of-line>\n"));
   nerrn = su_ERR_INVAL;
   vp = NULL;
   goto jleave;
}

/* s-it-mode */
