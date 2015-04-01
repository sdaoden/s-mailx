/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message (search a.k.a. argument) list handling.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
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
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
#define n_FILE list

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Token values returned by the scanner used for argument lists.
 * Also, sizes of scanner-related things */
enum ltoken {
   TEOL           = 0,        /* End of the command line */
   TNUMBER        = 1,        /* A message number */
   TDASH          = 2,        /* A simple dash */
   TSTRING        = 3,        /* A string (possibly containing -) */
   TDOT           = 4,        /* A "." */
   TUP            = 5,        /* A "^" */
   TDOLLAR        = 6,        /* A "$" */
   TSTAR          = 7,        /* A "*" */
   TOPEN          = 8,        /* A '(' */
   TCLOSE         = 9,        /* A ')' */
   TPLUS          = 10,       /* A '+' */
   TERROR         = 11,       /* A lexical error */
   TCOMMA         = 12,       /* A ',' */
   TSEMI          = 13,       /* A ';' */
   TBACK          = 14        /* A '`' */
};

#define REGDEP          2     /* Maximum regret depth. */

enum idfield {
   ID_REFERENCES,
   ID_IN_REPLY_TO
};

enum {
   CMNEW    = 1<<0,  /* New messages */
   CMOLD    = 1<<1,  /* Old messages */
   CMUNREAD = 1<<2,  /* Unread messages */
   CMDELETED =1<<3,  /* Deleted messages */
   CMREAD   = 1<<4,  /* Read messages */
   CMFLAG   = 1<<5,  /* Flagged messages */
   CMANSWER = 1<<6,  /* Answered messages */
   CMDRAFT  = 1<<7,  /* Draft messages */
   CMSPAM   = 1<<8,  /* Spam messages */
   CMSPAMUN = 1<<9   /* Maybe spam messages (unsure) */
};

struct coltab {
   char  co_char;    /* What to find past : */
   int   co_bit;     /* Associated modifier bit */
   int   co_mask;    /* m_status bits to mask */
   int   co_equal;   /* ... must equal this */
};

struct lex {
   char        l_char;
   enum ltoken l_token;
};

static struct coltab const _coltab[] = {
   { 'n',   CMNEW,      MNEW,       MNEW },
   { 'o',   CMOLD,      MNEW,       0 },
   { 'u',   CMUNREAD,   MREAD,      0 },
   { 'd',   CMDELETED,  MDELETED,   MDELETED },
   { 'r',   CMREAD,     MREAD,      MREAD },
   { 'f',   CMFLAG,     MFLAGGED,   MFLAGGED },
   { 'a',   CMANSWER,   MANSWERED,  MANSWERED },
   { 't',   CMDRAFT,    MDRAFTED,   MDRAFTED },
   { 's',   CMSPAM,     MSPAM,      MSPAM },
   { 'S',   CMSPAMUN,   MSPAMUNSURE, MSPAMUNSURE },
   { '\0',  0,          0,          0 }
};

static struct lex const    _singles[] = {
   { '$',   TDOLLAR },
   { '.',   TDOT },
   { '^',   TUP },
   { '*',   TSTAR },
   { '-',   TDASH },
   { '+',   TPLUS },
   { '(',   TOPEN },
   { ')',   TCLOSE },
   { ',',   TCOMMA },
   { ';',   TSEMI },
   { '`',   TBACK },
   { '\0',  0 }
};

static int     lastcolmod;
static size_t  STRINGLEN;
static int     lexnumber;              /* Number of TNUMBER from scan() */
static char    *lexstring;             /* String from TSTRING, scan() */
static int     regretp;                /* Pointer to TOS of regret tokens */
static int     regretstack[REGDEP];    /* Stack of regretted tokens */
static char    *string_stack[REGDEP];  /* Stack of regretted strings */
static int     numberstack[REGDEP];    /* Stack of regretted numbers */
static int     threadflag;             /* mark entire threads */

/* Append, taking care of resizes */
static char ** add_to_namelist(char ***namelist, size_t *nmlsize,
                  char **np, char *string);

/* Mark all messages that the user wanted from the command line in the message
 * structure.  Return 0 on success, -1 on error */
static int     markall(char *buf, int f);

/* Turn the character after a colon modifier into a bit value */
static int     evalcol(int col);

/* Check the passed message number for legality and proper flags.  If f is
 * MDELETED, then either kind will do.  Otherwise, the message has to be
 * undeleted */
static int     check(int mesg, int f);

/* Scan out a single lexical item and return its token number, updating the
 * string pointer passed **sp.  Also, store the value of the number or string
 * scanned in lexnumber or lexstring as appropriate.  In any event, store the
 * scanned `thing' in lexstring */
static int     scan(char **sp);

/* Unscan the named token by pushing it onto the regret stack */
static void    regret(int token);

/* Reset all the scanner global variables */
static void    scaninit(void);

/* See if the passed name sent the passed message */
static bool_t  _matchsender(struct message *mp, char const *str, bool_t allnet);

static bool_t  _matchmid(struct message *mp, char *id, enum idfield idfield);

/* See if the given string matches.
 * For the purpose of the scan, we ignore case differences.
 * This is the engine behind the `/' search */
static bool_t  _match_dash(struct message *mp, char const *str);

/* See if the given search expression matches.
 * For the purpose of the scan, we ignore case differences.
 * This is the engine behind the `@[..]@' search */
static bool_t  _match_at(struct message *mp, struct search_expr *sep);

/* Unmark the named message */
static void    unmark(int mesg);

/* Return the message number corresponding to the passed meta character */
static int     metamess(int meta, int f);

static char **
add_to_namelist(char ***namelist, size_t *nmlsize, char **np, char *string)
{
   size_t idx;
   NYD_ENTER;

   if ((idx = PTR2SIZE(np - *namelist)) >= *nmlsize) {
      *namelist = srealloc(*namelist, (*nmlsize += 8) * sizeof *np);
      np = *namelist + idx;
   }
   *np++ = string;
   NYD_LEAVE;
   return np;
}

static int
markall(char *buf, int f)
{
#define markall_ret(i) do { rv = i; goto jleave; } while (0);

   /* TODO use a bit carrier for all the states */
   char **np, **nq, **namelist, *bufp, *id = NULL, *cp;
   int rv = 0, i, tok, beg, other, valdot, colmod, colresult;
   struct message *mp, *mx;
   bool_t mc, star, topen, tback;
   size_t j, nmlsize;
   enum idfield idfield = ID_REFERENCES;
#ifdef HAVE_IMAP
   int gotheaders;
#endif
   NYD_ENTER;

   lexstring = ac_alloc(STRINGLEN = 2 * strlen(buf) +1);
   valdot = (int)PTR2SIZE(dot - message + 1);
   colmod = 0;

   for (i = 1; i <= msgCount; ++i) {
      enum mflag mf;

      mf = message[i - 1].m_flag;
      if (mf & MMARK)
         mf |= MOLDMARK;
      else
         mf &= ~MOLDMARK;
      mf &= ~MMARK;
      message[i - 1].m_flag = mf;
   }

   np = namelist = smalloc((nmlsize = 8) * sizeof *namelist);
   scaninit();
   bufp = buf;
   mc = FAL0;
   beg = star = other = topen = tback = FAL0;
#ifdef HAVE_IMAP
   gotheaders = 0;
#endif

   for (tok = scan(&bufp); tok != TEOL;) {
      switch (tok) {
      case TNUMBER:
number:
         if (star) {
            fprintf(stderr, _("No numbers mixed with *\n"));
            markall_ret(-1)
         }
         pstate |= PS_MSGLIST_SAW_NO;
         mc = TRU1;
         ++other;
         if (beg != 0) {
            if (check(lexnumber, f))
               markall_ret(-1)
            i = beg;
            while (mb.mb_threaded ? 1 : i <= lexnumber) {
               if (!(message[i - 1].m_flag & MHIDDEN) &&
                     (f == MDELETED || !(message[i - 1].m_flag & MDELETED)))
                  mark(i, f);
               if (mb.mb_threaded) {
                  if (i == lexnumber)
                     break;
                  mx = next_in_thread(&message[i - 1]);
                  if (mx == NULL)
                     markall_ret(-1)
                  i = (int)PTR2SIZE(mx - message + 1);
               } else
                  ++i;
            }
            beg = 0;
            break;
         }
         beg = lexnumber;
         if (check(beg, f))
            markall_ret(-1)
         tok = scan(&bufp);
         regret(tok);
         if (tok != TDASH) {
            mark(beg, f);
            beg = 0;
         }
         break;

      case TPLUS:
         pstate &= ~PS_MSGLIST_DIRECT;
         if (beg != 0) {
            printf(_("Non-numeric second argument\n"));
            markall_ret(-1)
         }
         i = valdot;
         do {
            if (mb.mb_threaded) {
               mx = next_in_thread(message + i - 1);
               i = mx ? (int)PTR2SIZE(mx - message + 1) : msgCount + 1;
            } else
               ++i;
            if (i > msgCount) {
               fprintf(stderr, _("Referencing beyond EOF\n"));
               markall_ret(-1)
            }
         } while (message[i - 1].m_flag == MHIDDEN ||
               (message[i - 1].m_flag & MDELETED) != (unsigned)f);
         mark(i, f);
         break;

      case TDASH:
         pstate &= ~PS_MSGLIST_DIRECT;
         if (beg == 0) {
            i = valdot;
            do {
               if (mb.mb_threaded) {
                  mx = prev_in_thread(message + i - 1);
                  i = mx ? (int)PTR2SIZE(mx - message + 1) : 0;
               } else
                  --i;
               if (i <= 0) {
                  fprintf(stderr, _("Referencing before 1\n"));
                  markall_ret(-1)
               }
            } while ((message[i - 1].m_flag & MHIDDEN) ||
                  (message[i - 1].m_flag & MDELETED) != (unsigned)f);
            mark(i, f);
         }
         break;

      case TSTRING:
         pstate &= ~PS_MSGLIST_DIRECT;
         if (beg != 0) {
            fprintf(stderr, _("Non-numeric second argument\n"));
            markall_ret(-1)
         }
         ++other;
         if ((cp = lexstring)[0] == ':') {
            while (*++cp != '\0') {
               colresult = evalcol(*cp);
               if (colresult == 0) {
                  fprintf(stderr, _("Unknown colon modifier \"%s\"\n"),
                     lexstring);
                  markall_ret(-1)
               }
               colmod |= colresult;
            }
         } else
            np = add_to_namelist(&namelist, &nmlsize, np, savestr(lexstring));
         break;

      case TOPEN:
#ifdef HAVE_IMAP_SEARCH
         pstate &= ~PS_MSGLIST_DIRECT;
         if (imap_search(lexstring, f) == STOP)
            markall_ret(-1)
         topen = TRU1;
#else
         fprintf(stderr, _(
            "`%s': the used selector is optional and not available\n"),
            lexstring);
         markall_ret(-1)
#endif
         break;

      case TDOLLAR:
      case TUP:
      case TDOT:
      case TSEMI:
         pstate &= ~PS_MSGLIST_DIRECT;
         lexnumber = metamess(lexstring[0], f);
         if (lexnumber == -1)
            markall_ret(-1)
         goto number;

      case TBACK:
         pstate &= ~PS_MSGLIST_DIRECT;
         tback = TRU1;
         for (i = 1; i <= msgCount; i++) {
            if ((message[i - 1].m_flag & MHIDDEN) ||
                  (message[i - 1].m_flag & MDELETED) != (unsigned)f)
               continue;
            if (message[i - 1].m_flag & MOLDMARK)
               mark(i, f);
         }
         break;

      case TSTAR:
         pstate &= ~PS_MSGLIST_DIRECT;
         if (other) {
            fprintf(stderr, _("Can't mix \"*\" with anything\n"));
            markall_ret(-1)
         }
         star = TRU1;
         break;

      case TCOMMA:
         pstate &= ~PS_MSGLIST_DIRECT;
#ifdef HAVE_IMAP
         if (mb.mb_type == MB_IMAP && gotheaders++ == 0)
            imap_getheaders(1, msgCount);
#endif
         if (id == NULL && (cp = hfield1("in-reply-to", dot)) != NULL) {
            id = savestr(cp);
            idfield = ID_IN_REPLY_TO;
         }
         if (id == NULL && (cp = hfield1("references", dot)) != NULL) {
            struct name *enp;

            if ((enp = extract(cp, GREF)) != NULL) {
               while (enp->n_flink != NULL)
                  enp = enp->n_flink;
               id = savestr(enp->n_name);
               idfield = ID_REFERENCES;
            }
         }
         if (id == NULL) {
            printf(_(
               "Cannot determine parent Message-ID of the current message\n"));
            markall_ret(-1)
         }
         break;

      case TERROR:
         pstate &= ~PS_MSGLIST_DIRECT;
         pstate |= PS_MSGLIST_SAW_NO;
         markall_ret(-1)
      }
      threadflag = 0;
      tok = scan(&bufp);
   }

   lastcolmod = colmod;
   np = add_to_namelist(&namelist, &nmlsize, np, NULL);
   --np;
   mc = FAL0;
   if (star) {
      for (i = 0; i < msgCount; ++i) {
         if (!(message[i].m_flag & MHIDDEN) &&
               (message[i].m_flag & MDELETED) == (unsigned)f) {
            mark(i + 1, f);
            mc = TRU1;
         }
      }
      if (!mc) {
         if (!(pstate & PS_IN_HOOK))
            printf(_("No applicable messages.\n"));
         markall_ret(-1)
      }
      markall_ret(0)
   }

   if ((topen || tback) && !mc) {
      for (i = 0; i < msgCount; ++i)
         if (message[i].m_flag & MMARK)
            mc = TRU1;
      if (!mc) {
         if (!(pstate & PS_IN_HOOK)) {
            if (tback)
               fprintf(stderr, _("No previously marked messages.\n"));
            else
               printf("No messages satisfy (criteria).\n");/*TODO tr*/
         }
         markall_ret(-1)
      }
   }

   /* If no numbers were given, mark all messages, so that we can unmark
    * any whose sender was not selected if any user names were given */
   if ((np > namelist || colmod != 0 || id) && !mc) {
      for (i = 1; i <= msgCount; ++i) {
         if (!(message[i - 1].m_flag & MHIDDEN) &&
               (message[i - 1].m_flag & MDELETED) == (unsigned)f)
            mark(i, f);
      }
   }

   /* If any names were given, eliminate any messages which don't match */
   if (np > namelist || id) {
      struct search_expr *sep = NULL;
      bool_t allnet;

      /* The `@' search works with struct search_expr, so build an array.
       * To simplify array, i.e., regex_t destruction, and optimize for the
       * common case we walk the entire array even in case of errors */
      if (np > namelist) {
         sep = scalloc(PTR2SIZE(np - namelist), sizeof(*sep));
         for (j = 0, nq = namelist; *nq != NULL; ++j, ++nq) {
            char *x = *nq, *y;

            sep[j].ss_sexpr = x;
            if (*x != '@' || rv < 0)
               continue;

            for (y = x + 1;; ++y) {
               if (*y == '\0' || !fieldnamechar(*y)) {
                  x = NULL;
                  break;
               }
               if (*y == '@') {
                  x = y;
                  break;
               }
            }
            sep[j].ss_where = (x == NULL || x - 1 == *nq)
                  ? "subject" : savestrbuf(*nq + 1, PTR2SIZE(x - *nq) - 1);

            x = (x == NULL ? *nq : x) + 1;
            if (*x == '\0') { /* XXX Simply remove from list instead? */
               fprintf(stderr, _("Empty `[@..]@' search expression\n"));
               rv = -1;
               continue;
            }
#ifdef HAVE_REGEX
            if (is_maybe_regex(x)) {
               sep[j].ss_sexpr = NULL;
               if (regcomp(&sep[j].ss_regex, x,
                     REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0) {
                  fprintf(stderr, _(
                     "Invalid regular expression: >>> %s <<<\n"), x);
                  rv = -1;
                  continue;
               }
            } else
#endif
               sep[j].ss_sexpr = x;
         }
         if (rv < 0)
            goto jnamesearch_sepfree;
      }

#ifdef HAVE_IMAP
      if (mb.mb_type == MB_IMAP && gotheaders++ == 0)
         imap_getheaders(1, msgCount);
#endif
      srelax_hold();
      allnet = ok_blook(allnet);
      for (i = 1; i <= msgCount; ++i) {
         mp = message + i - 1;
         j = 0;
         if (np > namelist) {
            for (nq = namelist; *nq != NULL; ++nq) {
               if (**nq == '@') {
                  if (_match_at(mp, sep + PTR2SIZE(nq - namelist))) {
                     ++j;
                     break;
                  }
               } else if (**nq == '/') {
                  if (_match_dash(mp, *nq)) {
                     ++j;
                     break;
                  }
               } else if (_matchsender(mp, *nq, allnet)) {
                  ++j;
                  break;
               }
            }
         }
         if (j == 0 && id && _matchmid(mp, id, idfield))
            ++j;
         if (j == 0)
            mp->m_flag &= ~MMARK;
         srelax();
      }
      srelax_rele();

      /* Make sure we got some decent messages */
      j = 0;
      for (i = 1; i <= msgCount; ++i)
         if (message[i - 1].m_flag & MMARK) {
            ++j;
            break;
         }
      if (j == 0) {
         if (!(pstate & PS_IN_HOOK) && np > namelist) {
            printf(_("No applicable messages from {%s"), namelist[0]);
            for (nq = namelist + 1; *nq != NULL; ++nq)
               printf(_(", %s"), *nq);
            printf(_("}\n"));
         } else if (id)
            printf(_("Parent message not found\n"));
         rv = -1;
         goto jnamesearch_sepfree;
      }

jnamesearch_sepfree:
      if (sep != NULL) {
#ifdef HAVE_REGEX
         for (j = PTR2SIZE(np - namelist); j-- != 0;)
            if (sep[j].ss_sexpr == NULL)
               regfree(&sep[j].ss_regex);
#endif
         free(sep);
      }
      if (rv < 0)
         goto jleave;
   }

   /* If any colon modifiers were given, go through and unmark any
    * messages which do not satisfy the modifiers */
   if (colmod != 0) {
      for (i = 1; i <= msgCount; ++i) {
         struct coltab const *colp;
         bool_t bad = TRU1;

         mp = message + i - 1;
         for (colp = _coltab; colp->co_char != '\0'; ++colp)
            if ((colp->co_bit & colmod) &&
                  ((mp->m_flag & colp->co_mask) == (unsigned)colp->co_equal))
               bad = FAL0;
         if (bad)
            unmark(i);
      }

      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if (mp->m_flag & MMARK)
            break;

      if (PTRCMP(mp, >=, message + msgCount)) {
         struct coltab const *colp;

         if (!(pstate & PS_IN_HOOK)) {
            printf(_("No messages satisfy"));
            for (colp = _coltab; colp->co_char != '\0'; ++colp)
               if (colp->co_bit & colmod)
                  printf(" :%c", colp->co_char);
            printf("\n");
         }
         markall_ret(-1)
      }
   }

   markall_ret(0)
jleave:
   free(namelist);
   ac_free(lexstring);
   NYD_LEAVE;
   return rv;

#undef markall_ret
}

static int
evalcol(int col)
{
   struct coltab const *colp;
   int rv;
   NYD_ENTER;

   if (col == 0)
      rv = lastcolmod;
   else {
      rv = 0;
      for (colp = _coltab; colp->co_char != '\0'; ++colp)
         if (colp->co_char == col) {
            rv = colp->co_bit;
            break;
         }
   }
   NYD_LEAVE;
   return rv;
}

static int
check(int mesg, int f)
{
   struct message *mp;
   NYD_ENTER;

   if (mesg < 1 || mesg > msgCount) {
      printf(_("%d: Invalid message number\n"), mesg);
      goto jem1;
   }
   mp = message + mesg - 1;
   if (mp->m_flag & MHIDDEN ||
         (f != MDELETED && (mp->m_flag & MDELETED) != 0)) {
      fprintf(stderr, _("%d: Inappropriate message\n"), mesg);
      goto jem1;
   }
   f = 0;
jleave:
   NYD_LEAVE;
   return f;
jem1:
   f = -1;
   goto jleave;
}

static int
scan(char **sp)
{
   char *cp, *cp2;
   int rv, c, inquote, quotec;
   struct lex const *lp;
   NYD_ENTER;

   if (regretp >= 0) {
      strncpy(lexstring, string_stack[regretp], STRINGLEN);
      lexstring[STRINGLEN -1] = '\0';
      lexnumber = numberstack[regretp];
      rv = regretstack[regretp--];
      goto jleave;
   }

   cp = *sp;
   cp2 = lexstring;
   c = *cp++;

   /* strip away leading white space */
   while (blankchar(c))
      c = *cp++;

   /* If no characters remain, we are at end of line, so report that */
   if (c == '\0') {
      *sp = --cp;
      rv = TEOL;
      goto jleave;
   }

   /* Select members of a message thread */
   if (c == '&') {
      threadflag = 1;
      if (*cp == '\0' || spacechar(*cp)) {
         lexstring[0] = '.';
         lexstring[1] = '\0';
         *sp = cp;
         rv = TDOT;
         goto jleave;
      }
      c = *cp++;
   }

   /* If the leading character is a digit, scan the number and convert it
    * on the fly.  Return TNUMBER when done */
   if (digitchar(c)) {
      lexnumber = 0;
      while (digitchar(c)) {
         lexnumber = lexnumber*10 + c - '0';
         *cp2++ = c;
         c = *cp++;
      }
      *cp2 = '\0';
      *sp = --cp;
      rv = TNUMBER;
      goto jleave;
   }

   /* An IMAP SEARCH list. Note that TOPEN has always been included in
    * singles[] in Mail and mailx. Thus although there is no formal
    * definition for (LIST) lists, they do not collide with historical
    * practice because a subject string (LIST) could never been matched
    * this way */
   if (c == '(') {
      ui32_t level = 1;
      inquote = 0;
      *cp2++ = c;
      do {
         if ((c = *cp++&0377) == '\0') {
jmtop:
            fprintf(stderr, "Missing \")\".\n");
            rv = TERROR;
            goto jleave;
         }
         if (inquote && c == '\\') {
            *cp2++ = c;
            c = *cp++&0377;
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
         else if (spacechar(c)) {
            /* Replace unquoted whitespace by single space characters, to make
             * the string IMAP SEARCH conformant */
            c = ' ';
            if (cp2[-1] == ' ')
               --cp2;
         }
         *cp2++ = c;
      } while (c != ')' || level > 0);
      *cp2 = '\0';
      *sp = cp;
      rv = TOPEN;
      goto jleave;
   }

   /* Check for single character tokens; return such if found */
   for (lp = _singles; lp->l_char != '\0'; ++lp)
      if (c == lp->l_char) {
         lexstring[0] = c;
         lexstring[1] = '\0';
         *sp = cp;
         rv = lp->l_token;
         goto jleave;
      }

   /* We've got a string!  Copy all the characters of the string into
    * lexstring, until we see a null, space, or tab.  If the lead character is
    * a " or ', save it and scan until you get another */
   quotec = 0;
   if (c == '\'' || c == '"') {
      quotec = c;
      c = *cp++;
   }
   while (c != '\0') {
      if (quotec == 0 && c == '\\' && *cp != '\0')
         c = *cp++;
      if (c == quotec) {
         ++cp;
         break;
      }
      if (quotec == 0 && blankchar(c))
         break;
      if (PTRCMP(cp2 - lexstring, <, STRINGLEN - 1))
         *cp2++ = c;
      c = *cp++;
   }
   if (quotec && c == 0) {
      fprintf(stderr, _("Missing %c\n"), quotec);
      rv = TERROR;
      goto jleave;
   }
   *sp = --cp;
   *cp2 = '\0';
   rv = TSTRING;
jleave:
   NYD_LEAVE;
   return rv;
}

static void
regret(int token)
{
   NYD_ENTER;
   if (++regretp >= REGDEP)
      panic(_("Too many regrets"));
   regretstack[regretp] = token;
   lexstring[STRINGLEN -1] = '\0';
   string_stack[regretp] = savestr(lexstring);
   numberstack[regretp] = lexnumber;
   NYD_LEAVE;
}

static void
scaninit(void)
{
   NYD_ENTER;
   regretp = -1;
   threadflag = 0;
   NYD_LEAVE;
}

static bool_t
_matchsender(struct message *mp, char const *str, bool_t allnet)
{
   bool_t rv;
   NYD_ENTER;

   if (allnet) {
      char *cp = nameof(mp, 0);

      do {
         if ((*cp == '@' || *cp == '\0') && (*str == '@' || *str == '\0')) {
            rv = TRU1;
            goto jleave;
         }
         if (*cp != *str)
            break;
      } while (++cp, *str++ != '\0');
      rv = FAL0;
      goto jleave;
   }
   rv = !strcmp(str, (*(ok_blook(showname) ? &realname : &skin))(name1(mp, 0)));
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_matchmid(struct message *mp, char *id, enum idfield idfield)
{
   char *cp;
   bool_t rv;
   NYD_ENTER;

   if ((cp = hfield1("message-id", mp)) != NULL) {
      switch (idfield) {
      case ID_REFERENCES:
         rv = !msgidcmp(id, cp);
         goto jleave;
      case ID_IN_REPLY_TO: {
         struct name *np;

         if ((np = extract(id, GREF)) != NULL)
            do {
               if (!msgidcmp(np->n_name, cp)) {
                  rv = TRU1;
                  goto jleave;
               }
            } while ((np = np->n_flink) != NULL);
         break;
      }
      }
   }
   rv = FAL0;
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_match_dash(struct message *mp, char const *str)
{
   static char lastscan[128];

   struct str in, out;
   char *hfield, *hbody;
   bool_t rv;
   NYD_ENTER;

   if (*++str == '\0') {
      str = lastscan;
   } else {
      strncpy(lastscan, str, sizeof lastscan); /* XXX use new n_str object! */
      lastscan[sizeof lastscan -1] = '\0';
   }

   /* Now look, ignoring case, for the word in the string */
   if (ok_blook(searchheaders) && (hfield = strchr(str, ':'))) {
      size_t l = PTR2SIZE(hfield - str);
      hfield = ac_alloc(l +1);
      memcpy(hfield, str, l);
      hfield[l] = '\0';
      hbody = hfieldX(hfield, mp);
      ac_free(hfield);
      hfield = UNCONST(str + l + 1);
   } else {
      hfield = UNCONST(str);
      hbody = hfield1("subject", mp);
   }
   if (hbody == NULL) {
      rv = FAL0;
      goto jleave;
   }

   in.s = hbody;
   in.l = strlen(hbody);
   mime_fromhdr(&in, &out, TD_ICONV);
   rv = substr(out.s, hfield);
   free(out.s);
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_match_at(struct message *mp, struct search_expr *sep)
{
   struct str in, out;
   char *nfield, *cfield;
   bool_t rv = FAL0;
   NYD_ENTER;

   nfield = savestr(sep->ss_where);

   while ((cfield = n_strsep(&nfield, ',', TRU1)) != NULL) {
      if (!asccasecmp(cfield, "body") ||
            (cfield[1] == '\0' && cfield[0] == '>')) {
         rv = FAL0;
jmsg:
         if ((rv = message_match(mp, sep, rv)))
            break;
      } else if (!asccasecmp(cfield, "text") ||
            (cfield[1] == '\0' && cfield[0] == '=')) {
         rv = TRU1;
         goto jmsg;
      } else if (!asccasecmp(cfield, "header") ||
            (cfield[1] == '\0' && cfield[0] == '<')) {
         if ((rv = header_match(mp, sep)))
            break;
      } else if ((in.s = hfieldX(cfield, mp)) == NULL)
         continue;
      else {
         in.l = strlen(in.s);
         mime_fromhdr(&in, &out, TD_ICONV);
#ifdef HAVE_REGEX
         if (sep->ss_sexpr == NULL)
            rv = (regexec(&sep->ss_regex, out.s, 0,NULL, 0) != REG_NOMATCH);
         else
#endif
            rv = substr(out.s, sep->ss_sexpr);
         free(out.s);
         if (rv)
            break;
      }
   }
   NYD_LEAVE;
   return rv;
}

static void
unmark(int mesg)
{
   size_t i;
   NYD_ENTER;

   i = (size_t)mesg;
   if (i < 1 || UICMP(z, i, >, msgCount))
      panic(_("Bad message number to unmark"));
   message[i - 1].m_flag &= ~MMARK;
   NYD_LEAVE;
}

static int
metamess(int meta, int f)
{
   int c, m;
   struct message *mp;
   NYD_ENTER;

   c = meta;
   switch (c) {
   case '^': /* First 'good' message left */
      mp = mb.mb_threaded ? threadroot : message;
      while (PTRCMP(mp, <, message + msgCount)) {
         if (!(mp->m_flag & MHIDDEN) && (mp->m_flag & MDELETED) == (ui32_t)f) {
            c = (int)PTR2SIZE(mp - message + 1);
            goto jleave;
         }
         if (mb.mb_threaded) {
            mp = next_in_thread(mp);
            if (mp == NULL)
               break;
         } else
            ++mp;
      }
      if (!(pstate & PS_IN_HOOK))
         printf(_("No applicable messages\n"));
      goto jem1;

   case '$': /* Last 'good message left */
      mp = mb.mb_threaded
            ? this_in_thread(threadroot, -1) : message + msgCount - 1;
      while (mp >= message) {
         if (!(mp->m_flag & MHIDDEN) && (mp->m_flag & MDELETED) == (ui32_t)f) {
            c = (int)PTR2SIZE(mp - message + 1);
            goto jleave;
         }
         if (mb.mb_threaded) {
            mp = prev_in_thread(mp);
            if (mp == NULL)
               break;
         } else
            --mp;
      }
      if (!(pstate & PS_IN_HOOK))
         printf(_("No applicable messages\n"));
      goto jem1;

   case '.':
      /* Current message */
      m = dot - message + 1;
      if ((dot->m_flag & MHIDDEN) || (dot->m_flag & MDELETED) != (ui32_t)f) {
         printf(_("%d: Inappropriate message\n"), m);
         goto jem1;
      }
      c = m;
      break;

   case ';':
      /* Previously current message */
      if (prevdot == NULL) {
         fprintf(stderr, _("No previously current message\n"));
         goto jem1;
      }
      m = prevdot - message + 1;
      if ((prevdot->m_flag & MHIDDEN) ||
            (prevdot->m_flag & MDELETED) != (ui32_t)f) {
         fprintf(stderr, _("%d: Inappropriate message\n"), m);
         goto jem1;
      }
      c = m;
      break;

   default:
      fprintf(stderr, _("Unknown metachar (%c)\n"), c);
      goto jem1;
   }
jleave:
   NYD_LEAVE;
   return c;
jem1:
   c = -1;
   goto jleave;
}

FL int
getmsglist(char *buf, int *vector, int flags)
{
   int *ip, mc;
   struct message *mp;
   NYD_ENTER;

   pstate &= ~PS_MSGLIST_MASK;

   if (msgCount == 0) {
      *vector = 0;
      mc = 0;
      goto jleave;
   }

   pstate |= PS_MSGLIST_DIRECT;

   if (markall(buf, flags) < 0) {
      mc = -1;
      goto jleave;
   }

   ip = vector;
   if (pstate & PS_HOOK_NEWMAIL) {
      mc = 0;
      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if (mp->m_flag & MMARK) {
            if (!(mp->m_flag & MNEWEST))
               unmark((int)PTR2SIZE(mp - message + 1));
            else
               ++mc;
         }
      if (mc == 0) {
         mc = -1;
         goto jleave;
      }
   }

   if (mb.mb_threaded == 0) {
      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if (mp->m_flag & MMARK)
            *ip++ = (int)PTR2SIZE(mp - message + 1);
   } else {
      for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
         if (mp->m_flag & MMARK)
            *ip++ = (int)PTR2SIZE(mp - message + 1);
   }
   *ip = 0;
   mc = (int)PTR2SIZE(ip - vector);
   if (mc != 1)
      pstate &= ~PS_MSGLIST_DIRECT;
jleave:
   NYD_LEAVE;
   return mc;
}

FL int
getrawlist(char const *line, size_t linesize, char **argv, int argc,
   int echolist)
{
   char c, *cp2, quotec, *linebuf;
   int argn;
   NYD_ENTER;

   pstate &= ~PS_MSGLIST_MASK;

   linebuf = ac_alloc(linesize);

   for (argn = 0;;) {
      if (!argn || !echolist) {
         for (; blankchar(*line); ++line)
            ;
         if (*line == '\0')
            break;
      }
      if (argn >= argc - 1) {
         fprintf(stderr,
            _("Too many elements in the list; excess discarded.\n"));
         break;
      }

      cp2 = linebuf;
      for (quotec = '\0'; ((c = *line++) != '\0');) {
         if (quotec != '\0') {
            if (c == quotec) {
               quotec = '\0';
               if (echolist)
                  *cp2++ = c;
            } else if (c == '\\') {
               switch (c = *line++) {
               case '\0':
                  *cp2++ = '\\';
                  --line;
                  break;
               default:
                  if (line[-1] != quotec || echolist)
                     *cp2++ = '\\';
                  *cp2++ = c;
               }
            } else
               *cp2++ = c;
         } else if (c == '"' || c == '\'') {
            if (echolist)
               *cp2++ = c;
            quotec = c;
         } else if (c == '\\' && !echolist)
            *cp2++ = (*line != '\0') ? *line++ : c;
         else if (blankchar(c))
            break;
         else
            *cp2++ = c;
      }
      argv[argn++] = savestrbuf(linebuf, PTR2SIZE(cp2 - linebuf));
      if (c == '\0')
         break;
   }
   argv[argn] = NULL;

   ac_free(linebuf);
   NYD_LEAVE;
   return argn;
}

FL int
first(int f, int m)
{
   struct message *mp;
   int rv;
   NYD_ENTER;

   if (msgCount == 0) {
      rv = 0;
      goto jleave;
   }

   f &= MDELETED;
   m &= MDELETED;
   for (mp = dot;
         mb.mb_threaded ? (mp != NULL) : PTRCMP(mp, <, message + msgCount);
         mb.mb_threaded ? (mp = next_in_thread(mp)) : ++mp) {
      if (!(mp->m_flag & MHIDDEN) && (mp->m_flag & m) == (ui32_t)f) {
         rv = (int)PTR2SIZE(mp - message + 1);
         goto jleave;
      }
   }

   if (dot > message) {
      for (mp = dot - 1; (mb.mb_threaded ? (mp != NULL) : (mp >= message));
            mb.mb_threaded ? (mp = prev_in_thread(mp)) : --mp) {
         if (!(mp->m_flag & MHIDDEN) && (mp->m_flag & m) == (ui32_t)f) {
            rv = (int)PTR2SIZE(mp - message + 1);
            goto jleave;
         }
      }
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL void
mark(int mesg, int f)
{
   struct message *mp;
   int i;
   NYD_ENTER;

   i = mesg;
   if (i < 1 || i > msgCount)
      panic(_("Bad message number to mark"));
   if (mb.mb_threaded == 1 && threadflag) {
      if (!(message[i - 1].m_flag & MHIDDEN)) {
         if (f == MDELETED || !(message[i - 1].m_flag & MDELETED))
         message[i - 1].m_flag |= MMARK;
      }

      if (message[i - 1].m_child) {
         mp = message[i - 1].m_child;
         mark((int)PTR2SIZE(mp - message + 1), f);
         for (mp = mp->m_younger; mp != NULL; mp = mp->m_younger)
            mark((int)PTR2SIZE(mp - message + 1), f);
      }
   } else
      message[i - 1].m_flag |= MMARK;
   NYD_LEAVE;
}

/* s-it-mode */
