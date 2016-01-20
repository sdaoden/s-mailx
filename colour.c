/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `colour' and `mono' commands, and anything working with it.
 *
 * Copyright (c) 2014 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#undef n_FILE
#define n_FILE colour

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE()
#ifdef HAVE_COLOUR

CTA(n_COLOUR_ID_RESET > _n_COLOUR_ID_VIEW_USER_HEADERS);
CTA(n_COLOUR_ID_RESET > n_COLOUR_ID_HSUM_THREAD);

/* Create an ISO 6429 (ECMA-48/ANSI) terminal control escape sequence */
static char *  _colour_iso6429(char const *wish);

static char *
_colour_iso6429(char const *wish)
{
   struct isodesc {
      char  id_name[15];
      char  id_modc;
   } const fta[] = {
      {"bold", '1'}, {"underline", '4'}, {"reverse", '7'}
   }, ca[] = {
      {"black", '0'}, {"red", '1'}, {"green", '2'}, {"brown", '3'},
      {"blue", '4'}, {"magenta", '5'}, {"cyan", '6'}, {"white", '7'}
   }, *idp;
   char const * const wish_orig = wish;
   char *xwish, *cp, fg[3], cfg[3] = {0, 0, 0};
   ui8_t ftno_base, ftno;
   NYD_ENTER;

   /* Since we use salloc(), reuse the n_strsep() buffer also for the return
    * value, ensure we have enough room for that */
   {
      size_t i = strlen(wish) +1;
      xwish = salloc(MAX(i, sizeof("\033[1;4;7;30;40m")));
      memcpy(xwish, wish, i);
      wish = xwish;
   }

   /* Iterate over the colour spec */
   ftno = 0;
   while ((cp = n_strsep(&xwish, ',', TRU1)) != NULL) {
      char *y, *x = strchr(cp, '=');
      if (x == NULL) {
jbail:
         n_err(_("Invalid colour specification \"%s\": %s\n"), wish_orig, cp);
         continue;
      }
      *x++ = '\0';

      if (!asccasecmp(cp, "ft")) {
         if (!asccasecmp(x, "inverse")) {
            OBSOLETE(_("please use \"reverse\" not \"inverse\" for ft= fonts"));
            x = UNCONST("reverse");
         }
         for (idp = fta;; ++idp)
            if (idp == fta + NELEM(fta))
               goto jbail;
            else if (!asccasecmp(x, idp->id_name)) {
               if (ftno < NELEM(fg))
                  fg[ftno++] = idp->id_modc;
               else
                  goto jbail;
               break;
            }
      } else if (!asccasecmp(cp, "fg")) {
         y = cfg + 1;
         goto jiter_colour;
      } else if (!asccasecmp(cp, "bg")) {
         y = cfg + 2;
jiter_colour:
         for (idp = ca;; ++idp)
            if (idp == ca + NELEM(ca))
               goto jbail;
            else if (!asccasecmp(x, idp->id_name)) {
               *y = idp->id_modc;
               break;
            }
      } else
         goto jbail;
   }

   /* Restore our salloc() buffer, create return value */
   xwish = UNCONST(wish);
   if (ftno > 0 || cfg[1] || cfg[2]) {
      xwish[0] = '\033';
      xwish[1] = '[';
      xwish += 2;

      for (ftno_base = ftno; ftno > 0;) {
         if (ftno-- != ftno_base)
            *xwish++ = ';';
         *xwish++ = fg[ftno];
      }

      if (cfg[1]) {
         if (ftno_base > 0)
            *xwish++ = ';';
         xwish[0] = '3';
         xwish[1] = cfg[1];
         xwish += 2;
      }

      if (cfg[2]) {
         if (ftno_base > 0 || cfg[1])
            *xwish++ = ';';
         xwish[0] = '4';
         xwish[1] = cfg[2];
         xwish += 2;
      }

      *xwish++ = 'm';
   }
   *xwish = '\0';
   NYD_LEAVE;
   return UNCONST(wish);
}

FL void
n_colour_table_create(bool_t pager_used, bool_t headerview)
{
   union {char *cp; char const *ccp; void *vp; struct n_colour_table *ctp;} u;
   size_t i;
   struct n_colour_table *ct;
   NYD_ENTER;

   if (ok_blook(colour_disable) || (pager_used && !ok_blook(colour_pager)))
      goto jleave;
   else {
      char *term, *okterms;

      if ((term = env_vlook("TERM", FAL0)) == NULL)
         goto jleave;
      /* terminfo rocks: if we find "color", assume it's right */
      if (strstr(term, "color") != NULL)
         goto jok;
      if ((okterms = ok_vlook(colour_terms)) == NULL)
         okterms = UNCONST(n_COLOUR_TERMS);
      okterms = savestr(okterms);

      i = strlen(term);
      while ((u.cp = n_strsep(&okterms, ',', TRU1)) != NULL)
         if (!strncmp(u.cp, term, i))
            goto jok;
      goto jleave;
   }

jok:
   n_colour_table = ct = salloc(sizeof *ct); /* XXX lex.c yet resets (FILTER!) */
   {  static struct {
         enum okeys        okey;
         enum n_colour_id  cid;
         char const        *defval;
      } const
      /* Header summary set */
      hsum_map[] = {
         {ok_v_colour_hsum_current,
            n_COLOUR_ID_HSUM_CURRENT,    n_COLOUR_HSUM_CURRENT},
         {ok_v_colour_hsum_dot,
            n_COLOUR_ID_HSUM_DOT,        n_COLOUR_HSUM_DOT},
         {ok_v_colour_hsum_dot_mark,
            n_COLOUR_ID_HSUM_DOT_MARK,   n_COLOUR_HSUM_DOT_MARK},
         {ok_v_colour_hsum_dot_thread,
            n_COLOUR_ID_HSUM_DOT_THREAD, n_COLOUR_HSUM_DOT_THREAD},
         {ok_v_colour_hsum_older,
            n_COLOUR_ID_HSUM_OLDER,      n_COLOUR_HSUM_OLDER},
         {ok_v_colour_hsum_thread,
            n_COLOUR_ID_HSUM_THREAD,     n_COLOUR_HSUM_THREAD}
      }, view_map[] = {
         {ok_v_colour_view_msginfo,
            n_COLOUR_ID_VIEW_MSGINFO,    n_COLOUR_VIEW_MSGINFO},
         {ok_v_colour_view_partinfo,
            n_COLOUR_ID_VIEW_PARTINFO,   n_COLOUR_VIEW_PARTINFO},
         {ok_v_colour_view_from_,
            n_COLOUR_ID_VIEW_FROM_,      n_COLOUR_VIEW_FROM_},
         {ok_v_colour_view_header,
            n_COLOUR_ID_VIEW_HEADER,     n_COLOUR_VIEW_HEADER},
         {ok_v_colour_view_uheader,
            n_COLOUR_ID_VIEW_UHEADER,    n_COLOUR_VIEW_UHEADER},
         {ok_v_colour_view_user_headers,
            _n_COLOUR_ID_VIEW_USER_HEADERS, n_COLOUR_VIEW_USER_HEADERS}
      }, oview_map[] = { /* TODO Message display set, !*v15-compat* */
         {ok_v_colour_msginfo,
            n_COLOUR_ID_VIEW_MSGINFO,    n_COLOUR_VIEW_MSGINFO},
         {ok_v_colour_partinfo,
            n_COLOUR_ID_VIEW_PARTINFO,   n_COLOUR_VIEW_PARTINFO},
         {ok_v_colour_from_,
            n_COLOUR_ID_VIEW_FROM_,      n_COLOUR_VIEW_FROM_},
         {ok_v_colour_header,
            n_COLOUR_ID_VIEW_HEADER,     n_COLOUR_VIEW_HEADER},
         {ok_v_colour_uheader,
            n_COLOUR_ID_VIEW_UHEADER,    n_COLOUR_VIEW_UHEADER},
         {ok_v_colour_user_headers,
            _n_COLOUR_ID_VIEW_USER_HEADERS, n_COLOUR_VIEW_USER_HEADERS}
      }, * map;
      size_t nelem;
      enum okeys v_nocolor; /* *-user-headers* is a string list */
      bool_t v15noted; /* TODO v15-compat */

      if (headerview) {
         map = hsum_map;
         nelem = NELEM(hsum_map);
         v15noted = TRU1;
      } else if (ok_blook(v15_compat)) {
         map = view_map;
         nelem = NELEM(view_map);
         v_nocolor = ok_v_colour_view_user_headers;
         v15noted = TRU1;
      } else {
         map = oview_map;
         nelem = NELEM(oview_map);
         v_nocolor = ok_v_colour_user_headers;
         v15noted = FAL0;
      }

      for (i = 0; i < nelem; ++i) {
         if ((u.cp = _var_oklook(map[i].okey)) == NULL)
            u.ccp = map[i].defval;
         else if (!v15noted) {
            OBSOLETE(_("please use *colour-view-XY* instead of *colour-XY*"));
            v15noted = TRU1;
         }
         if (u.ccp[0] != '\0') {
            if (headerview || map[i].okey != v_nocolor)
               u.cp = _colour_iso6429(u.ccp);
            else
               u.cp = savestr(u.cp);
            if ((ct->ct_csinfo[map[i].cid].l = strlen(u.cp)) == 0)
               u.cp = NULL;
            ct->ct_csinfo[map[i].cid].s = u.cp;
         } else {
            ct->ct_csinfo[map[i].cid].l = 0;
            ct->ct_csinfo[map[i].cid].s = NULL;
         }
      }
   }

   /* XXX using [0m is hard, we should selectively turn off what is on */
   ct->ct_csinfo[n_COLOUR_ID_RESET].l = sizeof("\033[0m") -1;
   ct->ct_csinfo[n_COLOUR_ID_RESET].s = UNCONST("\033[0m");
jleave:
   NYD_LEAVE;
}

FL void
n_colour_put(FILE *fp, enum n_colour_id cid)
{
   NYD_ENTER;
   if (n_colour_table != NULL) {
      struct str const *cp = n_colour_get(cid);

      if (cp != NULL)
         fwrite(cp->s, cp->l, 1, fp);
   }
   NYD_LEAVE;
}

FL void
n_colour_put_user_header(FILE *fp, char const *name)
{
   enum n_colour_id cid = n_COLOUR_ID_VIEW_HEADER;
   struct str const *uheads;
   char *cp, *cp_base, *x;
   size_t namelen;
   NYD_ENTER;

   if (n_colour_table == NULL)
      goto j_leave;

   /* Normal header colours if there are no user headers */
   uheads = n_colour_table->ct_csinfo + _n_COLOUR_ID_VIEW_USER_HEADERS;
   if (uheads->s == NULL)
      goto jleave;

   /* Iterate over all entries in the *colour-user-headers* list */
   cp = ac_alloc(uheads->l +1);
   memcpy(cp, uheads->s, uheads->l +1);
   cp_base = cp;
   namelen = strlen(name);
   while ((x = n_strsep(&cp, ',', TRU1)) != NULL) {
      size_t l = (cp != NULL) ? PTR2SIZE(cp - x) - 1 : strlen(x);
      if (l == namelen && !ascncasecmp(x, name, namelen)) {
         cid = n_COLOUR_ID_VIEW_UHEADER;
         break;
      }
   }
   ac_free(cp_base);
jleave:
   n_colour_put(fp, cid);
j_leave:
   NYD_LEAVE;
}

FL void
n_colour_reset(FILE *fp)
{
   NYD_ENTER;
   if (n_colour_table != NULL)
      fwrite("\033[0m", 4, 1, fp);
   NYD_LEAVE;
}

FL struct str const *
n_colour_get(enum n_colour_id cid)
{
   struct str const *rv;
   NYD_ENTER;

   if (n_colour_table != NULL) {
      if ((rv = n_colour_table->ct_csinfo + cid)->s == NULL)
         rv = NULL;
      assert(cid != n_COLOUR_ID_RESET || rv != NULL);
   } else
      rv = NULL;
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_COLOUR */

/* s-it-mode */
