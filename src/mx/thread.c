/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message threading. TODO thread handling needs rewrite, m_collapsed must go
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause
 */
/*
 * Copyright (c) 2004
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
#define su_FILE thread
#define mx_SOURCE
#define mx_SOURCE_THREAD

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/prime.h>

#include "mx/compat.h"
#include "mx/names.h"
#include "mx/ui-str.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Open addressing is used for Message-IDs because the maximum number of
 * messages in the table is known in advance (== msgCount) */
struct mitem {
   struct message *mi_data;
   char           *mi_id;
};
#define NOT_AN_ID ((struct mitem*)-1)

struct msort {
   union {
#ifdef mx_HAVE_SPAM
      u32   ms_ui;
#endif
      long     ms_long;
      char     *ms_char;
   }           ms_u;
   int         ms_n;
};

/* Return the hash value for a message id modulo mprime, or mprime if the
 * passed string does not look like a message-id */
static u32           _mhash(char const *cp, u32 mprime);

/* Look up a message id. Returns NOT_AN_ID if the passed string does not look
 * like a message-id */
static struct mitem *   _mlook(char *id, struct mitem *mt,
                           struct message *mdata, u32 mprime);

/* Child is to be adopted by parent.  A thread tree is structured as follows:
 *
 *  ------       m_child       ------        m_child
 *  |    |-------------------->|    |------------------------> . . .
 *  |    |<--------------------|    |<-----------------------  . . .
 *  ------      m_parent       ------       m_parent
 *     ^^                       |  ^
 *     | \____        m_younger |  |
 *     |      \                 |  |
 *     |       ----             |  |
 *     |           \            |  | m_elder
 *     |   m_parent ----        |  |
 *                      \       |  |
 *                       ----   |  |
 *                           \  +  |
 *                             ------        m_child
 *                             |    |------------------------> . . .
 *                             |    |<-----------------------  . . .
 *                             ------       m_parent
 *                              |  ^
 *                              . . .
 *
 * The base message of a thread does not have a m_parent link.  Elements
 * connected by m_younger/m_elder links are replies to the same message, which
 * is connected to them by m_parent links.  The first reply to a message gets
 * the m_child link */
static void             _adopt(struct message *parent, struct message *child,
                           int dist);

/* Connect all msgs on the lowest thread level with m_younger/m_elder links */
static struct message * _interlink(struct message *m, u32 cnt, int nmail);

static void             _finalize(struct message *mp);

/* Several sort comparison PTFs */
#ifdef mx_HAVE_SPAM
static int              _mui32lt(void const *a, void const *b);
#endif
static int              _mlonglt(void const *a, void const *b);
static int              _mcharlt(void const *a, void const *b);

static void             _lookup(struct message *m, struct mitem *mi,
                           u32 mprime);
static void             _makethreads(struct message *m, u32 cnt, int nmail);
static int              _colpt(int *msgvec, int cl);
static void             _colps(struct message *b, int cl);
static void             _colpm(struct message *m, int cl, int *cc, int *uncc);

static u32
_mhash(char const *cp, u32 mprime)
{
   u32 h = 0, g, at = 0;
   NYD2_IN;

   for (--cp; *++cp != '\0';) {
      /* Pay attention not to hash characters which are irrelevant for
       * Message-ID semantics */
      if (*cp == '(') {
         cp = skip_comment(cp + 1) - 1;
         continue;
      }
      if (*cp == '"' || *cp == '\\')
         continue;
      if (*cp == '@')
         ++at;
      /* TODO torek hash */
      h = ((h << 4) & 0xffffffff) + su_cs_to_lower(*cp);
      if ((g = h & 0xf0000000) != 0) {
         h = h ^ (g >> 24);
         h = h ^ g;
      }
   }
   NYD2_OU;
   return (at ? h % mprime : mprime);
}

static struct mitem *
_mlook(char *id, struct mitem *mt, struct message *mdata, u32 mprime)
{
   struct mitem *mp = NULL;
   u32 h, c, n = 0;
   NYD2_IN;

   if (id == NULL) {
      if ((id = hfield1("message-id", mdata)) == NULL)
         goto jleave;
      /* Normalize, what hfield1() doesn't do (TODO should now GREF, too!!) */
      if (id[0] == '<') {
         id[su_cs_len(id) -1] = '\0';
         if (*id != '\0')
            ++id;
      }
   }

   if (mdata != NULL && mdata->m_idhash)
      h = ~mdata->m_idhash;
   else {
      h = _mhash(id, mprime);
      if (h == mprime) {
         mp = NOT_AN_ID;
         goto jleave;
      }
   }

   mp = mt + (c = h);
   while (mp->mi_id != NULL) {
      if (!msgidcmp(mp->mi_id, id))
         break;
      c += (n & 1) ? -((n+1)/2) * ((n+1)/2) : ((n+1)/2) * ((n+1)/2);
      ++n;
      if ((s32)c < 0)
         c = 0;
      else while (c >= mprime)
         c -= mprime;
      mp = mt + c;
   }

   if (mdata != NULL && mp->mi_id == NULL) {
      mp->mi_id = su_cs_dup(id, 0);
      mp->mi_data = mdata;
      mdata->m_idhash = ~h;
   }
   if (mp->mi_id == NULL)
      mp = NULL;
jleave:
   NYD2_OU;
   return mp;
}

static void
_adopt(struct message *parent, struct message *child, int dist)
{
   struct message *mp, *mq;
   NYD2_IN;

   for (mp = parent; mp != NULL; mp = mp->m_parent)
      if (mp == child)
         goto jleave;

   child->m_level = dist; /* temporarily store distance */
   child->m_parent = parent;

   if (parent->m_child != NULL) {
      mq = NULL;
      for (mp = parent->m_child; mp != NULL; mp = mp->m_younger) {
         if (mp->m_date >= child->m_date) {
            if (mp->m_elder != NULL)
               mp->m_elder->m_younger = child;
            child->m_elder = mp->m_elder;
            mp->m_elder = child;
            child->m_younger = mp;
            if (mp == parent->m_child)
               parent->m_child = child;
            goto jleave;
         }
         mq = mp;
      }
      mq->m_younger = child;
      child->m_elder = mq;
   } else
      parent->m_child = child;
jleave:
   NYD2_OU;
}

static struct message *
_interlink(struct message *m, u32 cnt, int nmail)
{
   struct message *root;
   u32 n;
   struct msort *ms;
   int i, autocollapse;
   NYD2_IN;

   autocollapse = (!nmail && !(n_pstate & n_PS_HOOK_NEWMAIL) &&
         ok_blook(autocollapse));
   ms = n_alloc(sizeof *ms * cnt);

   for (n = 0, i = 0; UCMP(32, i, <, cnt); ++i) {
      if (m[i].m_parent == NULL) {
         if (autocollapse)
            _colps(m + i, 1);
         ms[n].ms_u.ms_long = m[i].m_date;
         ms[n].ms_n = i;
         ++n;
      }
   }

   if (n > 0) {
      qsort(ms, n, sizeof *ms, &_mlonglt);
      root = m + ms[0].ms_n;
      for (i = 1; UCMP(32, i, <, n); ++i) {
         m[ms[i-1].ms_n].m_younger = m + ms[i].ms_n;
         m[ms[i].ms_n].m_elder = m + ms[i - 1].ms_n;
      }
   } else
      root = NULL;

   n_free(ms);
   NYD2_OU;
   return root;
}

static void
_finalize(struct message *mp)
{
   long n;
   NYD2_IN;

   for (n = 0; mp; mp = next_in_thread(mp)) {
      mp->m_threadpos = ++n;
      mp->m_level = mp->m_parent ? mp->m_level + mp->m_parent->m_level : 0;
   }
   NYD2_OU;
}

#ifdef mx_HAVE_SPAM
static int
_mui32lt(void const *a, void const *b)
{
   struct msort const *xa = a, *xb = b;
   int i;
   NYD2_IN;

   i = (int)(xa->ms_u.ms_ui - xb->ms_u.ms_ui);
   if (i == 0)
      i = xa->ms_n - xb->ms_n;
   NYD2_OU;
   return i;
}
#endif

static int
_mlonglt(void const *a, void const *b)
{
   struct msort const *xa = a, *xb = b;
   int i;
   NYD2_IN;

   i = (int)(xa->ms_u.ms_long - xb->ms_u.ms_long);
   if (i == 0)
      i = xa->ms_n - xb->ms_n;
   NYD2_OU;
   return i;
}

static int
_mcharlt(void const *a, void const *b)
{
   struct msort const *xa = a, *xb = b;
   int i;
   NYD2_IN;

   i = strcoll(xa->ms_u.ms_char, xb->ms_u.ms_char);
   if (i == 0)
      i = xa->ms_n - xb->ms_n;
   NYD2_OU;
   return i;
}

static void
_lookup(struct message *m, struct mitem *mi, u32 mprime)
{
   struct mx_name *np;
   struct mitem *ip;
   char *cp;
   long dist;
   NYD2_IN;

   if (m->m_flag & MHIDDEN)
      goto jleave;

   dist = 1;
   if ((cp = hfield1("in-reply-to", m)) != NULL) {
      if ((np = extract(cp, GREF)) != NULL)
         do {
            if ((ip = _mlook(np->n_name, mi, NULL, mprime)) != NULL &&
                  ip != NOT_AN_ID) {
               _adopt(ip->mi_data, m, 1);
               goto jleave;
            }
         } while ((np = np->n_flink) != NULL);
   }

   if ((cp = hfield1("references", m)) != NULL) {
      if ((np = extract(cp, GREF)) != NULL) {
         while (np->n_flink != NULL)
            np = np->n_flink;
         do {
            if ((ip = _mlook(np->n_name, mi, NULL, mprime)) != NULL) {
               if (ip == NOT_AN_ID)
                  continue; /* skip dist++ */
               _adopt(ip->mi_data, m, dist);
               goto jleave;
            }
            ++dist;
         } while ((np = np->n_blink) != NULL);
      }
   }
jleave:
   NYD2_OU;
}

static void
_makethreads(struct message *m, u32 cnt, int nmail)
{
   struct mitem *mt;
   char *cp;
   u32 i, mprime;
   NYD2_IN;

   if (cnt == 0)
      goto jleave;

   /* It is performance crucial to space this large enough in order to minimize
    * bucket sharing */
   mprime = su_prime_lookup_next((cnt < U32_MAX >> 3) ? cnt << 2 : cnt);
   mt = n_calloc(mprime, sizeof *mt);

   srelax_hold();

   for (i = 0; i < cnt; ++i) {
      if (!(m[i].m_flag & MHIDDEN)) {
         _mlook(NULL, mt, m + i, mprime);
         if (m[i].m_date == 0) {
            if ((cp = hfield1("date", m + i)) != NULL)
               m[i].m_date = rfctime(cp);
         }
      }
      m[i].m_child = m[i].m_younger = m[i].m_elder = m[i].m_parent = NULL;
      m[i].m_level = 0;
      if (!nmail && !(n_pstate & n_PS_HOOK_NEWMAIL))
         m[i].m_collapsed = 0;
      srelax();
   }

   /* Most folders contain the eldest messages first.  Traversing them in
    * descending order makes it more likely that younger brothers are found
    * first, so elder ones can be prepended to the brother list, which is
    * faster.  The worst case is still in O(n^2) and occurs when all but one
    * messages in a folder are replies to the one message, and are sorted such
    * that youngest messages occur first */
   for (i = cnt; i > 0; --i) {
      _lookup(m + i - 1, mt, mprime);
      srelax();
   }

   srelax_rele();

   threadroot = _interlink(m, cnt, nmail);
   _finalize(threadroot);

   for (i = 0; i < mprime; ++i)
      if (mt[i].mi_id != NULL)
         n_free(mt[i].mi_id);

   n_free(mt);
   mb.mb_threaded = 1;
jleave:
   NYD2_OU;
}

static int
_colpt(int *msgvec, int cl)
{
   int *ip, rv;
   NYD2_IN;

   if (mb.mb_threaded != 1) {
      fputs("Not in threaded mode.\n", n_stdout);
      rv = 1;
   } else {
      for (ip = msgvec; *ip != 0; ++ip)
         _colps(message + *ip - 1, cl);
      rv = 0;
   }
   NYD2_OU;
   return rv;
}

static void
_colps(struct message *b, int cl)
{
   struct message *m;
   int cc = 0, uncc = 0;
   NYD2_IN;

   if (cl && (b->m_collapsed > 0 || (b->m_flag & (MNEW | MREAD)) == MNEW))
      goto jleave;

   if (b->m_child != NULL) {
      m = b->m_child;
      _colpm(m, cl, &cc, &uncc);
      for (m = m->m_younger; m != NULL; m = m->m_younger)
         _colpm(m, cl, &cc, &uncc);
   }

   if (cl) {
      b->m_collapsed = -cc;
      for (m = b->m_parent; m != NULL; m = m->m_parent)
         if (m->m_collapsed <= -uncc) {
            m->m_collapsed += uncc;
            break;
         }
   } else {
      if (b->m_collapsed > 0) {
         b->m_collapsed = 0;
         ++uncc;
      }
      for (m = b; m != NULL; m = m->m_parent)
         if (m->m_collapsed <= -uncc) {
            m->m_collapsed += uncc;
            break;
         }
   }
jleave:
   NYD2_OU;
}

static void
_colpm(struct message *m, int cl, int *cc, int *uncc)
{
   NYD2_IN;
   if (cl) {
      if (m->m_collapsed > 0)
         ++(*uncc);
      if ((m->m_flag & (MNEW | MREAD)) != MNEW || m->m_collapsed < 0)
         m->m_collapsed = 1;
      if (m->m_collapsed > 0)
         ++(*cc);
   } else {
      if (m->m_collapsed > 0) {
         m->m_collapsed = 0;
         ++(*uncc);
      }
   }

   if (m->m_child != NULL) {
      m = m->m_child;
      _colpm(m, cl, cc, uncc);
      for (m = m->m_younger; m != NULL; m = m->m_younger)
         _colpm(m, cl, cc, uncc);
   }
   NYD2_OU;
}

FL int
c_thread(void *vp)
{
   int rv;
   NYD_IN;

   if (mb.mb_threaded != 1 || vp == NULL || vp == (void*)-1) {
#ifdef mx_HAVE_IMAP
      if (mb.mb_type == MB_IMAP)
         imap_getheaders(1, msgCount);
#endif
      _makethreads(message, msgCount, (vp == (void*)-1));
      if (mb.mb_sorted != NULL)
         n_free(mb.mb_sorted);
      mb.mb_sorted = su_cs_dup("thread", 0);
   }

   if (vp != NULL && vp != (void*)-1 && !(n_pstate & n_PS_HOOK_MASK) &&
         ok_blook(header))
      rv = print_header_group(vp);
   else
      rv = 0;
   NYD_OU;
   return rv;
}

FL int
c_unthread(void *vp)
{
   struct message *m;
   int rv;
   NYD_IN;

   mb.mb_threaded = 0;
   if (mb.mb_sorted != NULL)
      n_free(mb.mb_sorted);
   mb.mb_sorted = NULL;

   for (m = message; PCMP(m, <, message + msgCount); ++m)
      m->m_collapsed = 0;

   if (vp && !(n_pstate & n_PS_HOOK_MASK) && ok_blook(header))
      rv = print_header_group(vp);
   else
      rv = 0;
   NYD_OU;
   return rv;
}

FL struct message *
next_in_thread(struct message *mp)
{
   struct message *rv;
   NYD2_IN;

   if ((rv = mp->m_child) != NULL)
      goto jleave;
   if ((rv = mp->m_younger) != NULL)
      goto jleave;

   while ((rv = mp->m_parent) != NULL) {
      mp = rv;
      if ((rv = rv->m_younger) != NULL)
         goto jleave;
   }
jleave:
   NYD2_OU;
   return rv;
}

FL struct message *
prev_in_thread(struct message *mp)
{
   struct message *rv;
   NYD2_IN;

   if ((rv = mp->m_elder) != NULL) {
      for (mp = rv; (rv = mp->m_child) != NULL;) {
         mp = rv;
         while ((rv = mp->m_younger) != NULL)
            mp = rv;
      }
      rv = mp;
      goto jleave;
   }
   rv = mp->m_parent;
jleave:
   NYD2_OU;
   return rv;
}

FL struct message *
this_in_thread(struct message *mp, long n)
{
   struct message *rv;
   NYD2_IN;

   if (n == -1) { /* find end of thread */
      while (mp != NULL) {
         if ((rv = mp->m_younger) != NULL) {
            mp = rv;
            continue;
         }
         rv = next_in_thread(mp);
         if (rv == NULL || rv->m_threadpos < mp->m_threadpos) {
            rv = mp;
            goto jleave;
         }
         mp = rv;
      }
      rv = mp;
      goto jleave;
   }

   while (mp != NULL && mp->m_threadpos < n) {
      if ((rv = mp->m_younger) != NULL && rv->m_threadpos <= n) {
         mp = rv;
         continue;
      }
      mp = next_in_thread(mp);
   }
   rv = (mp != NULL && mp->m_threadpos == n) ? mp : NULL;
jleave:
   NYD2_OU;
   return rv;
}

FL int
c_sort(void *vp)
{
   enum method {SORT_SUBJECT, SORT_DATE, SORT_STATUS, SORT_SIZE, SORT_FROM,
      SORT_TO, SORT_SPAM, SORT_THREAD} method;
   struct {
      char const *me_name;
      enum method me_method;
      int         (*me_func)(void const *, void const *);
   } const methnames[] = {
      {"date", SORT_DATE, &_mlonglt},
      {"from", SORT_FROM, &_mcharlt},
      {"to", SORT_TO, &_mcharlt},
      {"subject", SORT_SUBJECT, &_mcharlt},
      {"size", SORT_SIZE, &_mlonglt},
#ifdef mx_HAVE_SPAM
      {"spam", SORT_SPAM, &_mui32lt},
#endif
      {"status", SORT_STATUS, &_mlonglt},
      {"thread", SORT_THREAD, NULL}
   };

   char *_args[2], *cp, **args = vp;
   int msgvec[2], i, n;
   int (*func)(void const *, void const *);
   struct msort *ms;
   struct message *mp;
   boole showname;
   NYD_IN;

   if (vp == NULL || vp == (void*)-1) {
      _args[0] = savestr((mb.mb_sorted != NULL) ? mb.mb_sorted : "unsorted");
      _args[1] = NULL;
      args = _args;
   } else if (args[0] == NULL) {
      fprintf(n_stdout, "Current sorting criterion is: %s\n",
            (mb.mb_sorted != NULL) ? mb.mb_sorted : "unsorted");
      i = 0;
      goto jleave;
   }

   i = 0;
   for (;;) {
      if (*args[0] != '\0' && su_cs_starts_with(methnames[i].me_name, args[0]))
         break;
      if (UCMP(z, ++i, >=, NELEM(methnames))) {
         n_err(_("Unknown sorting method: %s\n"), args[0]);
         i = 1;
         goto jleave;
      }
   }

   if (mb.mb_sorted != NULL)
      n_free(mb.mb_sorted);
   mb.mb_sorted = su_cs_dup(args[0], 0);

   method = methnames[i].me_method;
   func = methnames[i].me_func;
   msgvec[0] = (int)P2UZ(dot - message + 1);
   msgvec[1] = 0;

   if (method == SORT_THREAD) {
      i = c_thread((vp != NULL && vp != (void*)-1) ? msgvec : vp);
      goto jleave;
   }

   showname = ok_blook(showname);
   ms = n_lofi_alloc(sizeof *ms * msgCount);
#ifdef mx_HAVE_IMAP
   switch (method) {
   case SORT_SUBJECT:
   case SORT_DATE:
   case SORT_FROM:
   case SORT_TO:
      if (mb.mb_type == MB_IMAP)
         imap_getheaders(1, msgCount);
      break;
   default:
      break;
   }
#endif

   srelax_hold();
   for (n = 0, i = 0; i < msgCount; ++i) {
      mp = message + i;
      if (!(mp->m_flag & MHIDDEN)) {
         switch (method) {
         case SORT_DATE:
            if (mp->m_date == 0 && (cp = hfield1("date", mp)) != NULL)
               mp->m_date = rfctime(cp);
            ms[n].ms_u.ms_long = mp->m_date;
            break;
         case SORT_STATUS:
            if (mp->m_flag & MDELETED)
               ms[n].ms_u.ms_long = 1;
            else if ((mp->m_flag & (MNEW | MREAD)) == MNEW)
               ms[n].ms_u.ms_long = 90;
            else if (mp->m_flag & MFLAGGED)
               ms[n].ms_u.ms_long = 85;
            else if ((mp->m_flag & (MNEW | MBOX)) == MBOX)
               ms[n].ms_u.ms_long = 70;
            else if (mp->m_flag & MNEW)
               ms[n].ms_u.ms_long = 80;
            else if (mp->m_flag & MREAD)
               ms[n].ms_u.ms_long = 40;
            else
               ms[n].ms_u.ms_long = 60;
            break;
         case SORT_SIZE:
            ms[n].ms_u.ms_long = mp->m_xsize;
            break;
#ifdef mx_HAVE_SPAM
         case SORT_SPAM:
            ms[n].ms_u.ms_ui = mp->m_spamscore;
            break;
#endif
         case SORT_FROM:
         case SORT_TO:
            if ((cp = hfield1((method == SORT_FROM ?  "from" : "to"), mp)
                  ) != NULL) {
               ms[n].ms_u.ms_char = su_cs_dup((showname ? realname(cp)
                     : skin(cp)), 0);
               mx_makelow(ms[n].ms_u.ms_char);
            } else
               ms[n].ms_u.ms_char = su_cs_dup(n_empty, 0);
            break;
         default:
         case SORT_SUBJECT:
            if((cp = hfield1("subject", mp)) != NIL)
               cp = mx_header_subject_edit(cp,
                     (mx_HEADER_SUBJECT_EDIT_DECODE_MIME |
                      mx_HEADER_SUBJECT_EDIT_TRIM_ALL));
            else
               cp = UNCONST(char*,su_empty);
            mx_makelow(ms[n].ms_u.ms_char = su_cs_dup(cp, 0));
            break;
         }
         ms[n++].ms_n = i;
      }
      mp->m_child = mp->m_younger = mp->m_elder = mp->m_parent = NULL;
      mp->m_level = 0;
      mp->m_collapsed = 0;
      srelax();
   }
   srelax_rele();

   if (n > 0) {
      qsort(ms, n, sizeof *ms, func);
      threadroot = message + ms[0].ms_n;
      for (i = 1; i < n; ++i) {
         message[ms[i - 1].ms_n].m_younger = message + ms[i].ms_n;
         message[ms[i].ms_n].m_elder = message + ms[i - 1].ms_n;
      }
   } else
      threadroot = NULL;

   _finalize(threadroot);
   mb.mb_threaded = 2;

   switch (method) {
   case SORT_FROM:
   case SORT_TO:
   case SORT_SUBJECT:
      for (i = 0; i < n; ++i)
         n_free(ms[i].ms_u.ms_char);
      /* FALLTHRU */
   default:
      break;
   }
   n_lofi_free(ms);

   i = ((vp != NULL && vp != (void*)-1 && !(n_pstate & n_PS_HOOK_MASK) &&
      ok_blook(header)) ? print_header_group(msgvec) : 0);
jleave:
   NYD_OU;
   return i;
}

FL int
c_collapse(void *v)
{
   int rv;
   NYD_IN;

   rv = _colpt(v, 1);
   NYD_OU;
   return rv;
}

FL int
c_uncollapse(void *v)
{
   int rv;
   NYD_IN;

   rv = _colpt(v, 0);
   NYD_OU;
   return rv;
}

FL void
uncollapse1(struct message *mp, int always)
{
   NYD_IN;
   if (mb.mb_threaded == 1 && (always || mp->m_collapsed > 0))
      _colps(mp, 0);
   NYD_OU;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_THREAD
/* s-it-mode */
