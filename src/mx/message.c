/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message, message array, n_getmsglist(), and related operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE message
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/cmd.h"
#include "mx/cmd-mlist.h"
#include "mx/file-streams.h"
#include "mx/mime.h"
#include "mx/names.h"
#include "mx/net-pop3.h"
#include "mx/termios.h"

/* TODO fake */
#include "su/code-in.h"

/* Token values returned by the scanner used for argument lists.
 * Also, sizes of scanner-related things */
enum a_msg_token{
   a_MSG_T_EOL,      /* End of the command line */
   a_MSG_T_NUMBER,   /* Message number */
   a_MSG_T_MINUS,    /* - */
   a_MSG_T_STRING,   /* A string (possibly containing -) */
   a_MSG_T_DOT,      /* . */
   a_MSG_T_UP,       /* ^ */
   a_MSG_T_DOLLAR,   /* $ */
   a_MSG_T_ASTER,    /* * */
   a_MSG_T_OPEN,     /* ( */
   a_MSG_T_CLOSE,    /* ) */
   a_MSG_T_PLUS,     /* + */
   a_MSG_T_COMMA,    /* , */
   a_MSG_T_SEMI,     /* ; */
   a_MSG_T_BACK,     /* ` */
   a_MSG_T_ERROR     /* Lexical error */
};

enum a_msg_idfield{
   a_MSG_ID_REFERENCES,
   a_MSG_ID_IN_REPLY_TO
};

enum a_msg_state{
   a_MSG_S_NEW = 1u<<0,
   a_MSG_S_OLD = 1u<<1,
   a_MSG_S_UNREAD = 1u<<2,
   a_MSG_S_DELETED = 1u<<3,
   a_MSG_S_READ = 1u<<4,
   a_MSG_S_FLAG = 1u<<5,
   a_MSG_S_ANSWERED = 1u<<6,
   a_MSG_S_DRAFT = 1u<<7,
   a_MSG_S_SPAM = 1u<<8,
   a_MSG_S_SPAMUNSURE = 1u<<9,
   a_MSG_S_MLIST = 1u<<10,
   a_MSG_S_MLSUBSCRIBE = 1u<<11
};

struct a_msg_coltab{
   char mco_char; /* What to find past : */
   u8 mco__dummy[3];
   int mco_bit;   /* Associated modifier bit */
   int mco_mask;  /* m_status bits to mask */
   int mco_equal; /* ... must equal this */
};

struct a_msg_lex{
   char ml_char;
   u8 ml_token;
};

struct a_msg_speclex{
   char *msl_str;             /* If parsed a string */
   int msl_no;                /* If parsed a number TODO sz! */
   char msl__smallstrbuf[4];
   /* We directly adjust pointer in .ca_arg.ca_str.s, do not adjust .l */
   struct mx_cmd_arg *msl_cap;
   char const *msl_input_orig;
};

static struct a_msg_coltab const a_msg_coltabs[] = {
   {'n', {0,}, a_MSG_S_NEW, MNEW, MNEW},
   {'o', {0,}, a_MSG_S_OLD, MNEW, 0},
   {'u', {0,}, a_MSG_S_UNREAD, MREAD, 0},
   {'d', {0,}, a_MSG_S_DELETED, MDELETED, MDELETED},
   {'r', {0,}, a_MSG_S_READ, MREAD, MREAD},
   {'f', {0,}, a_MSG_S_FLAG, MFLAGGED, MFLAGGED},
   {'a', {0,}, a_MSG_S_ANSWERED, MANSWERED, MANSWERED},
   {'t', {0,}, a_MSG_S_DRAFT, MDRAFTED, MDRAFTED},
   {'s', {0,}, a_MSG_S_SPAM, MSPAM, MSPAM},
   {'S', {0,}, a_MSG_S_SPAMUNSURE, MSPAMUNSURE, MSPAMUNSURE},
   /* These have no per-message flags, but must be evaluated */
   {'l', {0,}, a_MSG_S_MLIST, 0, 0},
   {'L', {0,}, a_MSG_S_MLSUBSCRIBE, 0, 0},
};

static struct a_msg_lex const a_msg_singles[] = {
   {'$', a_MSG_T_DOLLAR},
   {'.', a_MSG_T_DOT},
   {'^', a_MSG_T_UP},
   {'*', a_MSG_T_ASTER},
   {'-', a_MSG_T_MINUS},
   {'+', a_MSG_T_PLUS},
   {'(', a_MSG_T_OPEN},
   {')', a_MSG_T_CLOSE},
   {',', a_MSG_T_COMMA},
   {';', a_MSG_T_SEMI},
   {'`', a_MSG_T_BACK}
};

/* Slots in ::message */
static uz a_msg_mem_space;

/* Mark entire threads */
static boole a_msg_threadflag;

/* :d on its way HACK TODO */
static boole a_msg_list_saw_d, a_msg_list_last_saw_d;

/* Lazy load message header fields */
static enum okay a_msg_get_header(struct message *mp);

/* Append, taking care of resizes TODO vector */
static char **a_msg_add_to_nmadat(char ***nmadat, uz *nmasize,
               char **np, char *string);

/* Mark all messages that the user wanted from the command line in the message
 * structure.  Return 0 on success, -1 on error */
static int a_msg_markall(char const *orig, struct mx_cmd_arg *cap, int f);

/* Turn the character after a colon modifier into a bit value */
static int a_msg_evalcol(int col);

/* Check the passed message number for legality and proper flags.  Unless f is
 * MDELETED the message has to be undeleted */
static boole a_msg_check(int mno, int f);

/* Scan out a single lexical item and return its token number, update *mslp */
static int a_msg_scan(struct a_msg_speclex *mslp);

/* See if the passed name sent the passed message */
static boole a_msg_match_sender(struct message *mp, char const *str,
               boole allnet);

/* Check whether the given message-id or references match */
static boole a_msg_match_mid(struct message *mp, char const *id,
               enum a_msg_idfield idfield);

/* See if the given string matches.
 * For the purpose of the scan, we ignore case differences.
 * This is the engine behind the "/" search */
static boole a_msg_match_dash(struct message *mp, char const *str);

/* See if the given search expression matches.
 * For the purpose of the scan, we ignore case differences.
 * This is the engine behind the "@[..@].." search */
static boole a_msg_match_at(struct message *mp, struct search_expr *sep);

/* Unmark the named message */
static void a_msg_unmark(int mesg);

/* Return the message number corresponding to the passed meta character */
static int a_msg_metamess(int meta, int f);

/* Helper for mark(): self valid, threading enabled */
static void a_msg__threadmark(struct message *self, int f);

static enum okay
a_msg_get_header(struct message *mp){
   enum okay rv;
   NYD2_IN;
   UNUSED(mp);

   switch(mb.mb_type){
   case MB_FILE:
   case MB_MAILDIR:
      rv = OKAY;
      break;
#ifdef mx_HAVE_POP3
   case MB_POP3:
      rv = mx_pop3_header(mp);
      break;
#endif
#ifdef mx_HAVE_IMAP
   case MB_IMAP:
   case MB_CACHE:
      rv = imap_header(mp);
      break;
#endif
   case MB_VOID:
   default:
      rv = STOP;
      break;
   }
   NYD2_OU;
   return rv;
}

static char **
a_msg_add_to_nmadat(char ***nmadat, uz *nmasize, /* TODO Vector */
      char **np, char *string){
   uz idx, i;
   NYD2_IN;

   if((idx = P2UZ(np - *nmadat)) >= *nmasize){
      char **narr;

      i = *nmasize << 1;
      *nmasize = i;
      narr = su_AUTO_ALLOC(i * sizeof *np);
      su_mem_copy(narr, *nmadat, i >>= 1);
      *nmadat = narr;
      np = &narr[idx];
   }
   *np++ = string;

   NYD2_OU;
   return np;
}

static int
a_msg_markall(char const *orig, struct mx_cmd_arg *cap, int f){
   struct a_msg_speclex msl;
   enum a_msg_idfield idfield;
   uz j, nmasize;
   char const *id;
   char **nmadat_lofi, **nmadat, **np, **nq, *cp;
   struct message *mp, *mx;
   int i, valdot, beg, colmod, tok, colresult;
   enum{
      a_NONE = 0,
      a_ALLNET = 1u<<0,    /* (CTA()d to be == TRU1 */
      a_ALLOC = 1u<<1,     /* Have allocated something */
      a_THREADED = 1u<<2,
      a_ERROR = 1u<<3,
      a_ANY = 1u<<4,       /* Have marked just ANY */
      a_RANGE = 1u<<5,     /* Seen dash, await close */
      a_ASTER = 1u<<8,
      a_TOPEN = 1u<<9,     /* ( used (and didn't match) */
      a_TBACK = 1u<<10,    /* ` used (and didn't match) */
#ifdef mx_HAVE_IMAP
      a_HAVE_IMAP_HEADERS = 1u<<14,
#endif
      a_LOG = 1u<<29,      /* Log errors */
      a_TMP = 1u<<30
   } flags;
   NYD_IN;
   LCTA((u32)a_ALLNET == (u32)TRU1,
      "Constant is converted to boole via AND, thus");

   /* Update message array: clear MMARK but remember its former state for ` */
   for(i = msgCount; i-- > 0;){
      enum mflag mf;

      mf = (mp = &message[i])->m_flag;
      if(mf & MMARK)
         mf |= MOLDMARK;
      else
         mf &= ~MOLDMARK;
      mf &= ~MMARK;
      mp->m_flag = mf;
   }

   su_mem_set(&msl, 0, sizeof msl);
   msl.msl_cap = cap;
   msl.msl_input_orig = orig;

   np = nmadat =
   nmadat_lofi = su_LOFI_ALLOC((nmasize = 64) * sizeof *np); /* TODO vector */
   UNINIT(beg, 0);
   UNINIT(idfield, a_MSG_ID_REFERENCES);
   a_msg_threadflag = FAL0;
   valdot = (int)P2UZ(dot - message + 1);
   colmod = 0;
   id = NULL;
   flags = a_ALLOC | (mb.mb_threaded ? a_THREADED : 0) |
         ((!(n_pstate & n_PS_HOOK_MASK) || (n_poption & n_PO_D_V))
            ? a_LOG : 0);

   while((tok = a_msg_scan(&msl)) != a_MSG_T_EOL){
      if((a_msg_threadflag = (tok < 0)))
         tok &= INT_MAX;

      switch(tok){
      case a_MSG_T_NUMBER:
         n_pstate |= n_PS_MSGLIST_GABBY;
jnumber:
         if(!a_msg_check(msl.msl_no, f)){
            i = su_ERR_BADMSG;
            goto jerr;
         }

         if(flags & a_RANGE){
            flags ^= a_RANGE;

            if(!(flags & a_THREADED)){
               if(beg < msl.msl_no)
                  i = beg;
               else{
                  i = msl.msl_no;
                  msl.msl_no = beg;
               }

               for(; i <= msl.msl_no; ++i){
                  mp = &message[i - 1];
                  if(!(mp->m_flag & MHIDDEN) &&
                         (f == MDELETED || !(mp->m_flag & MDELETED))){
                     mark(i, f);
                     flags |= a_ANY;
                  }
               }
            }else{
               /* TODO threaded ranges are a mess */
               enum{
                  a_T_NONE,
                  a_T_HOT = 1u<<0,
                  a_T_DIR_PREV = 1u<<1
               } tf;
               int i_base;

               if(beg < msl.msl_no)
                  i = beg;
               else{
                  i = msl.msl_no;
                  msl.msl_no = beg;
               }

               i_base = i;
               tf = a_T_NONE;
jnumber__thr:
               for(;;){
                  mp = &message[i - 1];
                  if(!(mp->m_flag & MHIDDEN) &&
                         (f == MDELETED || !(mp->m_flag & MDELETED))){
                     if(tf & a_T_HOT){
                        mark(i, f);
                        flags |= a_ANY;
                     }
                  }

                  /* We may have reached the endpoint.  If we were still
                   * detecting the direction to search for it, restart.
                   * Otherwise finished */
                  if(i == msl.msl_no){ /* XXX */
                     if(!(tf & a_T_HOT)){
                        tf |= a_T_HOT;
                        i = i_base;
                        goto jnumber__thr;
                     }
                     break;
                  }

                  mx = (tf & a_T_DIR_PREV) ? prev_in_thread(mp)
                        : next_in_thread(mp);
                  if(mx == NULL){
                     /* We anyway have failed to reach the endpoint in this
                      * direction; if we already switched that, report error */
                     if(!(tf & a_T_DIR_PREV)){
                        tf |= a_T_DIR_PREV;
                        i = i_base;
                        goto jnumber__thr;
                     }
                     id = N_("Range crosses multiple threads\n");
                     i = su_ERR_INVAL;
                     goto jerrmsg;
                  }
                  i = (int)P2UZ(mx - message + 1);
               }
            }

            beg = 0;
         }else{
            /* Could be an inclusive range? */
            if(msl.msl_cap != NULL &&
                  msl.msl_cap->ca_arg.ca_str.s[0] == '-'){
               if(*++msl.msl_cap->ca_arg.ca_str.s == '\0')
                  msl.msl_cap = msl.msl_cap->ca_next;
               beg = msl.msl_no;
               flags |= a_RANGE;
            }else{
               mark(msl.msl_no, f);
               flags |= a_ANY;
            }
         }
         break;
      case a_MSG_T_PLUS:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         /*n_pstate |= n_PS_MSGLIST_GABBY;*/
         i = valdot;
         do{
            if(flags & a_THREADED){
               mx = next_in_thread(&message[i - 1]);
               i = mx ? (int)P2UZ(mx - message + 1) : msgCount + 1;
            }else
               ++i;
            if(i > msgCount){
               id = N_("Referencing beyond last message\n");
               i = su_ERR_BADMSG;
               goto jerrmsg;
            }
         }while(message[i - 1].m_flag == MHIDDEN ||
            (message[i - 1].m_flag & MDELETED) != (unsigned)f);
         msl.msl_no = i;
         goto jnumber;
      case a_MSG_T_MINUS:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         /*n_pstate |= n_PS_MSGLIST_GABBY;*/
         i = valdot;
         do{
            if(flags & a_THREADED){
               mx = prev_in_thread(&message[i - 1]);
               i = mx ? (int)P2UZ(mx - message + 1) : 0;
            }else
               --i;
            if(i <= 0){
               id = N_("Referencing before first message\n");
               i = su_ERR_BADMSG;
               goto jerrmsg;
            }
         }while(message[i - 1].m_flag == MHIDDEN ||
            (message[i - 1].m_flag & MDELETED) != (unsigned)f);
         msl.msl_no = i;
         goto jnumber;
      case a_MSG_T_STRING:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         if(flags & a_RANGE)
            goto jebadrange;

         /* This may be a colon modifier */
         if((cp = msl.msl_str)[0] != ':')
            np = a_msg_add_to_nmadat(&nmadat, &nmasize, np,
                  savestr(msl.msl_str));
         else{
            if(cp[1] == '\0')
               goto jevalcol_err;
            while(*++cp != '\0'){
               colresult = a_msg_evalcol(*cp);
               if(colresult == 0){
jevalcol_err:
                  if(flags & a_LOG)
                     n_err(_("Unknown or empty colon modifier: %s\n"),
                        msl.msl_str);
                  i = su_ERR_INVAL;
                  goto jerr;
               }
               if(colresult == a_MSG_S_DELETED){
                  a_msg_list_saw_d = TRU1;
                  f |= MDELETED;
               }
               colmod |= colresult;
            }
         }
         break;
      case a_MSG_T_OPEN:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         if(flags & a_RANGE)
            goto jebadrange;
         flags |= a_TOPEN;

#ifdef mx_HAVE_IMAP_SEARCH
         /* C99 */{
            sz ires;

            if((ires = imap_search(msl.msl_str, f)) >= 0){
               if(ires > 0)
                  flags |= a_ANY;
               break;
            }
         }
#else
         if(flags & a_LOG)
            n_err(_("Optional selector not available: %s\n"), msl.msl_str);
#endif
         i = su_ERR_INVAL;
         goto jerr;
      case a_MSG_T_DOLLAR:
      case a_MSG_T_UP:
      case a_MSG_T_SEMI:
         /*n_pstate |= n_PS_MSGLIST_GABBY;*/
         /* FALLTHRU */
      case a_MSG_T_DOT:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         if((msl.msl_no = i = a_msg_metamess(msl.msl_str[0], f)) < 0){
            msl.msl_no = -1;
            i = -i;
            goto jerr;
         }
         goto jnumber;
      case a_MSG_T_BACK:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         if(flags & a_RANGE)
            goto jebadrange;

         flags |= a_TBACK;
         for(i = 0; i < msgCount; ++i){
            if((mp = &message[i])->m_flag & MHIDDEN)
               continue;
            if((mp->m_flag & MDELETED) != (unsigned)f){
               if(!a_msg_list_last_saw_d)
                  continue;
               a_msg_list_saw_d = TRU1;
            }
            if(mp->m_flag & MOLDMARK){
               mark(i + 1, f);
               flags &= ~a_TBACK;
               flags |= a_ANY;
            }
         }
         break;
      case a_MSG_T_ASTER:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         if(flags & a_RANGE)
            goto jebadrange;
         flags |= a_ASTER;
         break;
      case a_MSG_T_COMMA:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         /*n_pstate |= n_PS_MSGLIST_GABBY;*/
         if(flags & a_RANGE)
            goto jebadrange;

#ifdef mx_HAVE_IMAP
         if(!(flags & a_HAVE_IMAP_HEADERS) && mb.mb_type == MB_IMAP){
            flags |= a_HAVE_IMAP_HEADERS;
            imap_getheaders(1, msgCount);
         }
#endif

         if(id == NULL){
            if((cp = hfield1("in-reply-to", dot)) != NULL)
               idfield = a_MSG_ID_IN_REPLY_TO;
            else if((cp = hfield1("references", dot)) != NULL){
               struct mx_name *enp;

               if((enp = extract(cp, GREF)) != NULL){
                  while(enp->n_flink != NULL)
                     enp = enp->n_flink;
                  cp = enp->n_name;
                  idfield = a_MSG_ID_REFERENCES;
               }else
                  cp = NULL;
            }

            if(cp != NULL)
               id = savestr(cp);
            else{
               id = N_("Message-ID of parent of \"dot\" is indeterminable\n");
               i = su_ERR_CANCELED;
               goto jerrmsg;
            }
         }else if(flags & a_LOG)
            n_err(_("Ignoring redundant specification of , selector\n"));
         break;
      case a_MSG_T_ERROR:
         n_pstate &= ~n_PS_MSGLIST_DIRECT;
         n_pstate |= n_PS_MSGLIST_GABBY;
         i = su_ERR_INVAL;
         goto jerr;
      }

      /* Explicitly disallow invalid ranges for future safety */
      if(msl.msl_cap != NULL && msl.msl_cap->ca_arg.ca_str.s[0] == '-' &&
            !(flags & a_RANGE)){
         if(flags & a_LOG)
            n_err(_("Ignoring invalid range in: %s\n"), msl.msl_input_orig);
         if(*++msl.msl_cap->ca_arg.ca_str.s == '\0')
            msl.msl_cap = msl.msl_cap->ca_next;
      }
   }
   if(flags & a_RANGE){
      id = N_("Missing second range argument\n");
      i = su_ERR_INVAL;
      goto jerrmsg;
   }

   np = a_msg_add_to_nmadat(&nmadat, &nmasize, np, NULL);
   --np;

   /* * is special at this point, after we have parsed the entire line */
   if(flags & a_ASTER){
      for(i = 0; i < msgCount; ++i){
         if((mp = &message[i])->m_flag & MHIDDEN)
            continue;
         if(!a_msg_list_saw_d && (mp->m_flag & MDELETED) != (unsigned)f)
            continue;
         mark(i + 1, f);
         flags |= a_ANY;
      }
      if(!(flags & a_ANY))
         goto jenoapp;
      goto jleave;
   }

   /* If any names were given, add any messages which match */
   if(np > nmadat || id != NULL){
      struct search_expr *sep;

      sep = NULL;

      /* The @ search works with struct search_expr, so build an array.
       * To simplify array, i.e., regex_t destruction, and optimize for the
       * common case we walk the entire array even in case of errors */
      /* XXX Like many other things around here: this should be outsourced */
      if(np > nmadat){
         j = P2UZ(np - nmadat) * sizeof(*sep);
         sep = su_LOFI_ALLOC(j);
         su_mem_set(sep, 0, j);

         for(j = 0, nq = nmadat; *nq != NULL; ++j, ++nq){
            char *xsave, *x, *y;

            sep[j].ss_body = x = xsave = *nq;
            if(*x != '@' || (flags & a_ERROR))
               continue;

            /* Cramp the namelist */
            for(y = &x[1];; ++y){
               if(*y == '\0'){
                  x = NULL;
                  break;
               }
               if(*y == '@'){
                  x = y;
                  break;
               }
            }
            if(x == NULL || &x[-1] == xsave)
jat_where_default:
               sep[j].ss_field = "subject";
            else{
               ++xsave;
               if(*xsave == '~'){
                  sep[j].ss_skin = TRU1;
                  if(++xsave >= x){
                     if(flags & a_LOG)
                        n_err(_("[@..]@ search expression: no namelist, "
                           "only \"~\" skin indicator\n"));
                     flags |= a_ERROR;
                     continue;
                  }
               }
               cp = savestrbuf(xsave, P2UZ(x - xsave));

               /* Namelist could be a regular expression, too */
#ifdef mx_HAVE_REGEX
               if(n_is_maybe_regex(cp)){
                  int s;

                  ASSERT(sep[j].ss_field == NULL);
                  if((s = regcomp(&sep[j].ss__fieldre_buf, cp,
                        REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0){
                     if(flags & a_LOG)
                        n_err(_("Invalid regular expression: %s: %s\n"),
                           n_shexp_quote_cp(cp, FAL0),
                           n_regex_err_to_doc(NULL, s));
                     flags |= a_ERROR;
                     continue;
                  }
                  sep[j].ss_fieldre = &sep[j].ss__fieldre_buf;
               }else
#endif
                    {
                  struct str sio;

                  /* Because of the special cases we need to trim explicitly
                   * here, they are not covered by su_cs_sep_c() */
                  sio.s = cp;
                  sio.l = P2UZ(x - xsave);
                  if(*(cp = n_str_trim(&sio, n_STR_TRIM_BOTH)->s) == '\0')
                     goto jat_where_default;
                  sep[j].ss_field = cp;
               }
            }

            /* The actual search expression.  If it is empty we only test the
             * field(s) for existence  */
            x = &(x == NULL ? *nq : x)[1];
            if(*x == '\0'){
               sep[j].ss_field_exists = TRU1;
#ifdef mx_HAVE_REGEX
            }else if(n_is_maybe_regex(x)){
               int s;

               sep[j].ss_body = NULL;
               if((s = regcomp(&sep[j].ss__bodyre_buf, x,
                     REG_EXTENDED | REG_ICASE | REG_NOSUB)) != 0){
                  if(flags & a_LOG)
                     n_err(_("Invalid regular expression: %s: %s\n"),
                        n_shexp_quote_cp(x, FAL0),
                        n_regex_err_to_doc(NULL, s));
                  flags |= a_ERROR;
                  continue;
               }
               sep[j].ss_bodyre = &sep[j].ss__bodyre_buf;
#endif
            }else
               sep[j].ss_body = x;
         }
         if(flags & a_ERROR)
            goto jnamesearch_sepfree;
      }

      /* Iterate the entire message array */
#ifdef mx_HAVE_IMAP
         if(!(flags & a_HAVE_IMAP_HEADERS) && mb.mb_type == MB_IMAP){
            flags |= a_HAVE_IMAP_HEADERS;
            imap_getheaders(1, msgCount);
         }
#endif
      if(ok_blook(allnet))
         flags |= a_ALLNET;

      su_mem_bag_auto_relax_create(su_MEM_BAG_SELF);
      for(i = 0; i < msgCount; ++i){
         if((mp = &message[i])->m_flag & (MMARK | MHIDDEN))
            continue;
         if(!a_msg_list_saw_d && (mp->m_flag & MDELETED) != (unsigned)f)
            continue;

         flags &= ~a_TMP;
         if(np > nmadat){
            for(nq = nmadat; *nq != NULL; ++nq){
               if(**nq == '@'){
                  if(a_msg_match_at(mp, &sep[P2UZ(nq - nmadat)])){
                     flags |= a_TMP;
                     break;
                  }
               }else if(**nq == '/'){
                  if(a_msg_match_dash(mp, *nq)){
                     flags |= a_TMP;
                     break;
                  }
               }else if(a_msg_match_sender(mp, *nq, (flags & a_ALLNET))){
                  flags |= a_TMP;
                  break;
               }
            }
         }
         if(!(flags & a_TMP) &&
               id != NULL && a_msg_match_mid(mp, id, idfield))
            flags |= a_TMP;

         if(flags & a_TMP){
            mark(i + 1, f);
            flags |= a_ANY;
         }
         su_mem_bag_auto_relax_unroll(su_MEM_BAG_SELF);
      }
      su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF);

jnamesearch_sepfree:
      if(sep != NULL){
#ifdef mx_HAVE_REGEX
         for(j = P2UZ(np - nmadat); j-- != 0;){
            if(sep[j].ss_fieldre != NULL)
               regfree(sep[j].ss_fieldre);
            if(sep[j].ss_bodyre != NULL)
               regfree(sep[j].ss_bodyre);
         }
#endif
         su_LOFI_FREE(sep);
      }
      if(flags & a_ERROR){
         i = su_ERR_INVAL;
         goto jerr;
      }
   }

   /* If any colon modifiers were given, go through and mark any messages which
    * do satisfy the modifiers */
   if(colmod != 0){
      for(i = 0; i < msgCount; ++i){
         struct a_msg_coltab const *colp;

         if((mp = &message[i])->m_flag & (MMARK | MHIDDEN))
            continue;
         if(!a_msg_list_saw_d && (mp->m_flag & MDELETED) != (unsigned)f)
            continue;

         for(colp = a_msg_coltabs;
               PCMP(colp, <, &a_msg_coltabs[NELEM(a_msg_coltabs)]); ++colp)
            if(colp->mco_bit & colmod){
               /* Is this a colon modifier that requires evaluation? */
               if(colp->mco_mask == 0){
                  if(colp->mco_bit & (a_MSG_S_MLIST |
                           a_MSG_S_MLSUBSCRIBE)){
                     enum mx_mlist_type what;

                     what = (colp->mco_bit & a_MSG_S_MLIST) ? mx_MLIST_KNOWN
                           : mx_MLIST_SUBSCRIBED;
                     if(what == mx_mlist_query_mp(mp, what))
                        goto jcolonmod_mark;
                  }
               }else if((mp->m_flag & colp->mco_mask
                     ) == (enum mflag)colp->mco_equal){
jcolonmod_mark:
                  mark(i + 1, f);
                  flags |= a_ANY;
                  break;
               }
            }
      }
   }

   /* It shall be an error if ` didn't match anything, and nothing else did */
   if((flags & (a_TBACK | a_ANY)) == a_TBACK){
      id = N_("No previously marked messages\n");
      i = su_ERR_BADMSG;
      goto jerrmsg;
   }else if(!(flags & a_ANY))
      goto jenoapp;

   ASSERT(!(flags & a_ERROR));
jleave:
   if(flags & a_ALLOC)
      su_LOFI_FREE(nmadat_lofi);

   NYD_OU;
   return (flags & a_ERROR) ? -1 : 0;

jebadrange:
   id = N_("Invalid range endpoint\n");
   i = su_ERR_INVAL;
   goto jerrmsg;
jenoapp:
   id = N_("No applicable messages\n");
   i = su_ERR_NOMSG;
jerrmsg:
   if(flags & a_LOG)
      n_err(V_(id));
jerr:
   n_pstate_err_no = i;
   flags |= a_ERROR;
   goto jleave;
}

static int
a_msg_evalcol(int col){
   struct a_msg_coltab const *colp;
   int rv;
   NYD2_IN;

   rv = 0;
   for(colp = a_msg_coltabs;
         PCMP(colp, <, &a_msg_coltabs[NELEM(a_msg_coltabs)]); ++colp)
      if(colp->mco_char == col){
         rv = colp->mco_bit;
         break;
      }
   NYD2_OU;
   return rv;
}

static boole
a_msg_check(int mno, int f){
   struct message *mp;
   NYD2_IN;

   if(mno < 1 || mno > msgCount){
      n_err(_("%d: Invalid message number\n"), mno);
      mno = 1;
   }else if(((mp = &message[mno - 1])->m_flag & MHIDDEN) ||
         (f != MDELETED && (mp->m_flag & MDELETED) != 0))
      n_err(_("%d: inappropriate message\n"), mno);
   else
      mno = 0;
   NYD2_OU;
   return (mno == 0);
}

static int
a_msg_scan(struct a_msg_speclex *mslp){
   struct a_msg_lex const *lp;
   char *cp, c;
   int rv;
   NYD_IN;

   rv = a_MSG_T_EOL;

   /* Empty cap's even for IGNORE_EMPTY (quoted empty tokens produce output) */
   for(;; mslp->msl_cap = mslp->msl_cap->ca_next){
      if(mslp->msl_cap == NULL)
         goto jleave;

      cp = mslp->msl_cap->ca_arg.ca_str.s;
      if((c = *cp++) != '\0')
         break;
   }

   /* Select members of a message thread */
   if(c == '&'){
      c = *cp;
      if(c == '\0' || su_cs_is_space(c)){
         mslp->msl_str = mslp->msl__smallstrbuf;
         mslp->msl_str[0] = '.';
         mslp->msl_str[1] = '\0';
         if(c == '\0')
            mslp->msl_cap = mslp->msl_cap->ca_next;
         else{
jshexp_err:
            n_err(_("Message list: invalid syntax: %s (in %s)\n"),
               n_shexp_quote_cp(cp, FAL0),
               n_shexp_quote_cp(mslp->msl_input_orig, FAL0));
            rv = a_MSG_T_ERROR;
            goto jleave;
         }
         rv = a_MSG_T_DOT | INT_MIN;
         goto jleave;
      }
      rv = INT_MIN;
      ++cp;
   }

   /* If the leading character is a digit, scan the number and convert it
    * on the fly.  Return a_MSG_T_NUMBER when done */
   if(su_cs_is_digit(c)){
      mslp->msl_no = 0;
      do
         mslp->msl_no = (mslp->msl_no * 10) + c - '0'; /* XXX inline atoi */
      while((c = *cp++, su_cs_is_digit(c)));

      if(c == '\0')
         mslp->msl_cap = mslp->msl_cap->ca_next;
      else{
         --cp;
         /* This could be a range */
         if(c == '-')
            mslp->msl_cap->ca_arg.ca_str.s = cp;
         else
            goto jshexp_err;
      }
      rv |= a_MSG_T_NUMBER;
      goto jleave;
   }

   /* An IMAP SEARCH list. Note that a_MSG_T_OPEN has always been included
    * in singles[] in Mail and mailx. Thus although there is no formal
    * definition for (LIST) lists, they do not collide with historical
    * practice because a subject string (LIST) could never been matched
    * this way */
   if (c == '(') {
      boole inquote;
      u32 level;
      char *tocp;

      (tocp = mslp->msl_str = mslp->msl_cap->ca_arg.ca_str.s)[0] = '(';
      ++tocp;
      level = 1;
      inquote = FAL0;
      do {
         if ((c = *cp++) == '\0') {
jmtop:
            n_err(_("Missing )\n"));
            n_err(_("P.S.: message specifications are now shell tokens, "
                  "making it necessary\n"
               "to escape/shell quote parenthesis, e.g., '(from \"me\")'\n"
               "Please read the manual section "
                  "\"Shell-style argument quoting\"\n"));
            rv = a_MSG_T_ERROR;
            goto jleave;
         }
         if (inquote && c == '\\') {
            *tocp++ = c;
            c = *cp++;
            if (c == '\0')
               goto jmtop;
         } else if (c == '"')
            inquote = !inquote;
         else if (inquote)
            /*EMPTY*/;
         else if (c == '(')
            ++level;
         else if (c == ')')
            --level;
         else if (su_cs_is_space(c)) {
            /* Replace unquoted whitespace by single space characters, to make
             * the string IMAP SEARCH conformant */
            c = ' ';
            if (tocp[-1] == ' ')
               --tocp;
         }
         *tocp++ = c;
      } while (c != ')' || level > 0);
      *tocp = '\0';
      if(*cp != '\0')
         goto jshexp_err;
      mslp->msl_cap = mslp->msl_cap->ca_next;
      rv |= a_MSG_T_OPEN;
      goto jleave;
   }

   /* Check for single character tokens; return such if found */
   for(lp = a_msg_singles;
         PCMP(lp, <, &a_msg_singles[NELEM(a_msg_singles)]); ++lp){
      if(c == lp->ml_char){
         mslp->msl_str = mslp->msl__smallstrbuf;
         mslp->msl_str[0] = c;
         mslp->msl_str[1] = '\0';
         if(*cp != '\0')
            goto jshexp_err;
         mslp->msl_cap = mslp->msl_cap->ca_next;
         rv = lp->ml_token;
         goto jleave;
      }
   }

   mslp->msl_cap = mslp->msl_cap->ca_next;
   mslp->msl_str = --cp;
   rv = a_MSG_T_STRING;
jleave:
   NYD_OU;
   return rv;
}

static boole
a_msg_match_sender(struct message *mp, char const *str, boole allnet){
   char const *str_base, *np_base, *np;
   char c, nc;
   struct mx_name *namep;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   /* Empty string doesn't match */
   namep = lextract(n_header_senderfield_of(mp), GFULL | GSKIN);

   if(namep == NULL || *(str_base = str) == '\0')
      goto jleave;

   /* *allnet* is POSIX and, since it explicitly mentions login and user names,
    * most likely case-sensitive.  XXX Still allow substr matching, though
    * XXX possibly the first letter should be case-insensitive, then? */
   if(allnet){
      for(; namep != NULL; str = str_base, namep = namep->n_flink){
         for(np_base = np = namep->n_name;;){
            if((c = *str++) == '@')
               c = '\0';
            if((nc = *np++) == '@' || nc == '\0' || c == '\0'){
               if((rv = (c == '\0')))
                  goto jleave;
               break;
            }
            if(c != nc){
               np = ++np_base;
               str = str_base;
            }
         }
      }
   }else{
      /* TODO POSIX says ~"match any address as shown in header overview",
       * TODO but a normalized match would be more sane i guess.
       * TODO mx_name should gain a comparison method, normalize realname
       * TODO content (in TODO) and thus match as likewise
       * TODO "Buddy (Today) <here>" and "(Now) Buddy <here>" */
      boole again_base, again;

      again_base = ok_blook(showname);

      for(; namep != NULL; str = str_base, namep = namep->n_flink){
         again = again_base;
jagain:
         np_base = np = again ? namep->n_fullname : namep->n_name;
         str = str_base;
         for(;;){
            c = *str++;
            if((nc = *np++) == '\0' || c == '\0'){
               if((rv = (c == '\0')))
                  goto jleave;
               break;
            }
            c = su_cs_to_upper(c);
            nc = su_cs_to_upper(nc);
            if(c != nc){
               np = ++np_base;
               str = str_base;
            }
         }

         /* And really if i want to match 'on@' then i want it to match even if
          * *showname* is set! */
         if(again){
            again = FAL0;
            goto jagain;
         }
      }
   }
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_msg_match_mid(struct message *mp, char const *id,
      enum a_msg_idfield idfield){
   char const *cp;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if((cp = hfield1("message-id", mp)) != NULL){
      switch(idfield){
      case a_MSG_ID_REFERENCES:
         if(!msgidcmp(id, cp))
            rv = TRU1;
         break;
      case a_MSG_ID_IN_REPLY_TO:{
         struct mx_name *np;

         if((np = extract(id, GREF)) != NULL)
            do{
               if(!msgidcmp(np->n_name, cp)){
                  rv = TRU1;
                  break;
               }
            }while((np = np->n_flink) != NULL);
         break;
      }
      }
   }
   NYD2_OU;
   return rv;
}

static boole
a_msg_match_dash(struct message *mp, char const *str){
   static char lastscan[128];

   struct str in, out;
   char *hfield, *hbody;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if(*++str == '\0')
      str = lastscan;
   else
      su_cs_pcopy_n(lastscan, str, sizeof lastscan); /* XXX use n_str! */

   /* Now look, ignoring case, for the word in the string */
   if(ok_blook(searchheaders) && (hfield = su_cs_find_c(str, ':'))){
      uz l;

      l = P2UZ(hfield - str);
      hfield = su_LOFI_ALLOC(l +1);
      su_mem_copy(hfield, str, l);
      hfield[l] = '\0';
      hbody = hfieldX(hfield, mp);
      su_LOFI_FREE(hfield);
      hfield = UNCONST(char*,str + l + 1);
   }else{
      hfield = UNCONST(char*,str);
      hbody = hfield1("subject", mp);
   }
   if(hbody == NIL)
      goto jleave;

   in.l = su_cs_len(in.s = hbody);
   mx_mime_display_from_header(&in, &out, mx_MIME_DISPLAY_ICONV);
   rv = substr(out.s, hfield);
   su_FREE(out.s);

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_msg_match_at(struct message *mp, struct search_expr *sep){
   char const *field;
   boole rv;
   NYD2_IN;

   /* Namelist regex only matches headers.
    * And there are the special cases header/<, "body"/> and "text"/=, the
    * latter two of which need to be handled in here */
   if((field = sep->ss_field) != NULL){
      if(!su_cs_cmp_case(field, "body") ||
            (field[1] == '\0' && field[0] == '>')){
         rv = FAL0;
jmsg:
         rv = message_match(mp, sep, rv);
         goto jleave;
      }else if(!su_cs_cmp_case(field, "text") ||
            (field[1] == '\0' && field[0] == '=')){
         rv = TRU1;
         goto jmsg;
      }
   }

   rv = n_header_match(mp, sep);
jleave:
   NYD2_OU;
   return rv;
}

static void
a_msg_unmark(int mesg){
   uz i;
   NYD2_IN;

   i = (uz)mesg;
   if(i < 1 || UCMP(z, i, >, msgCount))
      n_panic(_("Bad message number to unmark"));
   message[--i].m_flag &= ~MMARK;
   NYD2_OU;
}

static int
a_msg_metamess(int meta, int f){
   struct message *mp;
   int c, m;
   char const *emsg;
   NYD2_IN;

   emsg = NIL;

   switch((c = meta)){
   case '^': /* First 'good' message left */
      mp = mb.mb_threaded ? threadroot : message;
      while(PCMP(mp, <, message + msgCount)){
         if(!(mp->m_flag & MHIDDEN) && (mp->m_flag & MDELETED) == S(u32,f)){
            c = S(int,P2UZ(mp - message + 1));
            goto jleave;
         }
         if(mb.mb_threaded){
            if((mp = next_in_thread(mp)) == NIL)
               break;
         }else
            ++mp;
      }
      emsg = N_("No applicable messages\n");
      c = -su_ERR_NOMSG;
      break;

   case '$': /* Last 'good message left */
      mp = mb.mb_threaded
            ? this_in_thread(threadroot, -1) : &message[msgCount - 1];
      while(mp >= message){
         if(!(mp->m_flag & MHIDDEN) && (mp->m_flag & MDELETED) == S(u32,f)){
            c = S(int,P2UZ(mp - message + 1));
            goto jleave;
         }
         if(mb.mb_threaded){
            if((mp = prev_in_thread(mp)) == NIL)
               break;
         }else
            --mp;
      }
      emsg = N_("No applicable messages\n");
      c = -su_ERR_NOMSG;
      break;

   case '.': /* Current message */
      m = S(int,P2UZ(dot - message + 1));
      if((dot->m_flag & MHIDDEN) || (dot->m_flag & MDELETED) != S(u32,f))
         goto jeinappr;
      c = m;
      break;

   case ';': /* Previously current message */
      if(prevdot == NIL){
         emsg = N_("No previously current message\n");
         c = -su_ERR_BADMSG;
         break;
      }
      m = S(int,P2UZ(prevdot - message + 1));
      if((prevdot->m_flag & MHIDDEN) ||
            (prevdot->m_flag & MDELETED) != S(u32,f)){
jeinappr:
         n_err(_("%d: inappropriate message\n"), m);
         c = -su_ERR_BADMSG;
         goto jleave;
      }
      c = m;
      break;

   default:
      n_err(_("Unknown message specifier: %c\n"), c);
      c = -su_ERR_INVAL;
      break;
   }

   if(emsg != NIL && !(n_pstate & n_PS_HOOK_MASK))
      n_err(V_(emsg));
jleave:
   NYD2_OU;
   return c;
}

static void
a_msg__threadmark(struct message *self, int f){
   NYD2_IN;
   if(!(self->m_flag & MHIDDEN) &&
         (f == MDELETED || !(self->m_flag & MDELETED) || a_msg_list_saw_d)){
      self->m_flag |= MMARK;
      if(n_msgmark1 == NIL)
         n_msgmark1 = self;
   }

   if((self = self->m_child) != NULL){
      goto jcall;
      while((self = self->m_younger) != NULL)
         if(self->m_child != NULL)
jcall:
            a_msg__threadmark(self, f);
         else{
            self->m_flag |= MMARK;
            if(n_msgmark1 == NIL)
               n_msgmark1 = self;
         }
   }
   NYD2_OU;
}

FL FILE *
setinput(struct mailbox *mp, struct message *m, enum needspec need){
   enum okay ok;
   FILE *rv;
   NYD_IN;

   rv = NULL;

   switch(need){
   case NEED_HEADER:
      ok = (m->m_content_info & CI_HAVE_HEADER) ? OKAY
            : a_msg_get_header(m);
      break;
   case NEED_BODY:
      ok = (m->m_content_info & CI_HAVE_BODY) ? OKAY : get_body(m);
      break;
   default:
   case NEED_UNSPEC:
      ok = OKAY;
      break;
   }
   if(ok != OKAY)
      goto jleave;

   fflush(mp->mb_otf);
   if(fseek(mp->mb_itf, (long)mailx_positionof(m->m_block, m->m_offset),
         SEEK_SET) == -1){
      n_perr(_("fseek"), 0);
      n_panic(_("temporary file seek"));
   }
   rv = mp->mb_itf;
jleave:
   NYD_OU;
   return rv;
}

FL enum okay
get_body(struct message *mp){
   enum okay rv;
   NYD_IN;
   UNUSED(mp);

   switch(mb.mb_type){
   case MB_FILE:
   case MB_MAILDIR:
      rv = OKAY;
      break;
#ifdef mx_HAVE_POP3
   case MB_POP3:
      rv = mx_pop3_body(mp);
      break;
#endif
#ifdef mx_HAVE_IMAP
   case MB_IMAP:
   case MB_CACHE:
      rv = imap_body(mp);
      break;
#endif
   case MB_VOID:
   default:
      rv = STOP;
      break;
   }
   NYD_OU;
   return rv;
}

FL void
message_reset(void){
   NYD_IN;

   if(message != NIL){
      su_FREE(message);
      message = NIL;
   }
   msgCount = 0;
   a_msg_mem_space = 0;

   NYD_OU;
}

FL void
message_append(struct message *mp){
   NYD_IN;

   if(UCMP(z, msgCount + 1, >=, a_msg_mem_space)){
      /* XXX remove _mem_space magics (or use s_Vector) */
      a_msg_mem_space = ((a_msg_mem_space >= 128 &&
               a_msg_mem_space <= 1000000)
            ? a_msg_mem_space << 1 : a_msg_mem_space + 64);
      message = su_REALLOC(message, a_msg_mem_space * sizeof(*message));
   }

   if(msgCount > 0){
      if(mp != NIL)
         message[msgCount - 1] = *mp;
      else
         su_mem_set(&message[msgCount - 1], 0, sizeof *message);
   }

   NYD_OU;
}

FL void
message_append_null(void){
   NYD_IN;

   if(msgCount == 0)
      message_append(NIL);

   setdot(message, FAL0);
   message[msgCount].m_size = 0;
   message[msgCount].m_lines = 0;

   NYD_OU;
}

FL boole
message_match(struct message *mp, struct search_expr const *sep,
      boole with_headers){
   char *line;
   uz linesize, cnt;
   FILE *fp;
   boole rv;
   NYD_IN;

   rv = FAL0;

   if((fp = mx_fs_tmp_open("mpmatch", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL)
      goto j_leave;

   mx_fs_linepool_aquire(&line, &linesize);

   if(sendmp(mp, fp, NULL, NULL, SEND_TOSRCH, NULL) < 0)
      goto jleave;
   fflush_rewind(fp);
   cnt = fsize(fp);

   if(!with_headers){
      for(;;){
         if(fgetline(&line, &linesize, &cnt, NIL, fp, FAL0) == NIL)
            goto jleave;
         if(*line == '\n')
            break;
      }
   }

   while(fgetline(&line, &linesize, &cnt, NIL, fp, FAL0) != NIL){
#ifdef mx_HAVE_REGEX
      if(sep->ss_bodyre != NULL){
         if(regexec(sep->ss_bodyre, line, 0,NULL, 0) == REG_NOMATCH)
            continue;
      }else
#endif
            if(!substr(line, sep->ss_body))
         continue;
      rv = TRU1;
      break;
   }
   if(ferror(fp))
      rv = FAL0; /* XXX This does not stop overall searching though?! */

jleave:
   mx_fs_linepool_release(line, linesize);

   mx_fs_close(fp);
j_leave:
   NYD_OU;
   return rv;
}

FL struct message *
setdot(struct message *mp, boole set_ps_did_print_dot){
   NYD_IN;

   if(dot != mp){
      prevdot = dot;
      n_pstate &= ~n_PS_DID_PRINT_DOT;
   }

   uncollapse1(dot = mp, 0);

   if(set_ps_did_print_dot)
      n_pstate |= n_PS_DID_PRINT_DOT;

   NYD_OU;
   return dot;
}

FL void
touch(struct message *mp){
   NYD_IN;

   mp->m_flag |= MTOUCH;
   if(!(mp->m_flag & MREAD))
      mp->m_flag |= MREAD | MSTATUS;

   NYD_OU;
}

FL int
n_getmsglist(char const *buf, int *vector, int flags,
   struct mx_cmd_arg **capp_or_null)
{
   int *ip, mc;
   struct message *mp;
   NYD_IN;

   n_pstate &= ~n_PS_ARGLIST_MASK;
   n_pstate |= n_PS_MSGLIST_DIRECT;
   n_msgmark1 = NIL;
   a_msg_list_last_saw_d = a_msg_list_saw_d;
   a_msg_list_saw_d = FAL0;

   *vector = 0;
   if(capp_or_null != NULL)
      *capp_or_null = NULL;
   if(*buf == '\0'){
      mc = 0;
      goto jleave;
   }

   /* TODO Parse the message spec into an ARGV; this should not happen here,
    * TODO but instead cmd_arg_parse() should feed in the list of parsed tokens
    * TODO to getmsglist(); as of today there are multiple getmsglist() users
    * TODO though, and they need to deal with that, then, too */
   /* C99 */{
      mx_CMD_ARG_DESC_SUBCLASS_DEF(getmsglist, 1, pseudo_cad){
         {mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION |
               mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_HONOUR_STOP,
            n_SHEXP_PARSE_TRIM_IFSSPACE | n_SHEXP_PARSE_IFS_VAR |
               n_SHEXP_PARSE_IGNORE_EMPTY}
      }mx_CMD_ARG_DESC_SUBCLASS_DEF_END;
      struct mx_cmd_arg_ctx cac;

      cac.cac_desc = mx_CMD_ARG_DESC_SUBCLASS_CAST(&pseudo_cad);
      cac.cac_indat = buf;
      cac.cac_inlen = UZ_MAX;
      cac.cac_msgflag = flags;
      cac.cac_msgmask = 0;
      if(!mx_cmd_arg_parse(&cac)){
         mc = -1;
         goto jleave;
      }else if(cac.cac_no == 0){
         mc = 0;
         goto jleave;
      }else{
         /* Is this indeed a (maybe optional) message list and a target? */
         if(capp_or_null != NULL){
            struct mx_cmd_arg *cap, **lcapp;

            if((cap = cac.cac_arg)->ca_next == NULL){
               *capp_or_null = cap;
               mc = 0;
               goto jleave;
            }
            for(;;){
               lcapp = &cap->ca_next;
               if((cap = *lcapp)->ca_next == NULL)
                  break;
            }
            *capp_or_null = cap;
            *lcapp = NULL;

            /* In the list-and-target mode we have to take special care, since
             * some commands use special call conventions historically (use the
             * MBOX, search for a message, whatever).
             * Thus, to allow things like "certsave '' bla" or "save '' ''",
             * watch out for two argument form with empty token first.
             * This special case is documented at the prototype */
            if(cac.cac_arg->ca_next == NULL &&
                  cac.cac_arg->ca_arg.ca_str.s[0] == '\0'){
               mc = 0;
               goto jleave;
            }
         }

         if(msgCount == 0){
            mc = 0;
            goto jleave;
         }else if((mc = a_msg_markall(buf, cac.cac_arg, flags)) < 0){
            mc = -1;
            goto jleave;
         }
      }
   }

   ip = vector;
   if(n_pstate & n_PS_HOOK_NEWMAIL){
      mc = 0;
      for(mp = message; mp < &message[msgCount]; ++mp)
         if(mp->m_flag & MMARK){
            if(!(mp->m_flag & MNEWEST))
               a_msg_unmark((int)P2UZ(mp - message + 1));
            else
               ++mc;
         }
      if(mc == 0){
         mc = -1;
         goto jleave;
      }
   }

   if(mb.mb_threaded == 0){
      for(mp = message; mp < &message[msgCount]; ++mp)
         if(mp->m_flag & MMARK)
            *ip++ = (int)P2UZ(mp - message + 1);
   }else{
      for(mp = threadroot; mp != NULL; mp = next_in_thread(mp))
         if(mp->m_flag & MMARK)
            *ip++ = (int)P2UZ(mp - message + 1);
   }
   *ip = 0;
   mc = (int)P2UZ(ip - vector);
   if(mc != 1)
      n_pstate &= ~n_PS_MSGLIST_DIRECT;
jleave:
   NYD_OU;
   return mc;
}

FL int
first(int f, int m)
{
   struct message *mp;
   int rv;
   NYD_IN;

   if (msgCount == 0) {
      rv = 0;
      goto jleave;
   }

   f &= MDELETED;
   m &= MDELETED;
   for (mp = dot;
         mb.mb_threaded ? (mp != NULL) : PCMP(mp, <, message + msgCount);
         mb.mb_threaded ? (mp = next_in_thread(mp)) : ++mp) {
      if (!(mp->m_flag & MHIDDEN) && (mp->m_flag & m) == (u32)f) {
         rv = (int)P2UZ(mp - message + 1);
         goto jleave;
      }
   }

   if (dot > message) {
      for (mp = dot - 1; (mb.mb_threaded ? (mp != NULL) : (mp >= message));
            mb.mb_threaded ? (mp = prev_in_thread(mp)) : --mp) {
         if (!(mp->m_flag & MHIDDEN) && (mp->m_flag & m) == (u32)f) {
            rv = (int)P2UZ(mp - message + 1);
            goto jleave;
         }
      }
   }
   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

FL void
mark(int mno, int f){
   struct message *mp;
   int i;
   NYD_IN;

   i = mno;
   if(i < 1 || i > msgCount)
      n_panic(_("Bad message number to mark"));
   mp = &message[--i];

   if(mb.mb_threaded == 1 && a_msg_threadflag)
      a_msg__threadmark(mp, f);
   else{
      ASSERT(!(mp->m_flag & MHIDDEN));
      mp->m_flag |= MMARK;
      if(n_msgmark1 == NIL)
         n_msgmark1 = mp;
   }
   NYD_OU;
}

#include "su/code-ou.h"
/* s-it-mode */
