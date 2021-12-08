/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of colour.h.
 *@ TODO As stated in header:
 *@ TODO All the colour (pen etc. ) interfaces below have to vanish.
 *@ TODO What we need here is a series of query functions which take context,
 *@ TODO like a message* for the _VIEW_ series, and which return a 64-bit
 *@ TODO flag carrier which returns font-attributes as well as foreground and
 *@ TODO background colours (at least 24-bit each).
 *@ TODO And the actual drawing stuff is up to the backend, maybe in termios,
 *@ TODO or better termcap, or in ui-str.  I do not know.
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
#define su_FILE colour
#define mx_SOURCE
#define mx_SOURCE_COLOUR

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_COLOUR

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#ifdef mx_HAVE_REGEX
# include <su/re.h>
#endif

#include "mx/go.h"
#include "mx/sigs.h"
#include "mx/termcap.h"

/* TODO fake */
#include "mx/colour.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Not needed publicly, but extends a public set */
#define mx_COLOUR_TAG_ERR R(char*,-1)
#define a_COLOUR_TAG_IS_SPECIAL(P) (P2UZ(P) >= P2UZ(-3))

enum a_colour_type{
   a_COLOUR_T_256,
   a_COLOUR_T_8,
   a_COLOUR_T_1,
   a_COLOUR_T_NONE, /* EQ largest real colour + 1! */
   a_COLOUR_T_UNKNOWN /* Initial value: real one queried before 1st use */
};

enum a_colour_tag_type{
   a_COLOUR_TT_NONE,
   a_COLOUR_TT_DOT = 1u<<0, /* "dot" */
   a_COLOUR_TT_OLDER = 1u<<1, /* "older" */
   a_COLOUR_TT_HEADERS = 1u<<2, /* Comma-separated list of headers allowed */

   a_COLOUR_TT_SUM = a_COLOUR_TT_DOT | a_COLOUR_TT_OLDER,
   a_COLOUR_TT_VIEW = a_COLOUR_TT_HEADERS
};

struct a_colour_type_map{
   u8 ctm_type; /* a_colour_type */
   char ctm_name[7];
};

struct a_colour_map_id{
   u8 cmi_ctx; /* enum mx_colour_ctx */
   u8 cmi_id; /* enum mx_colour_id */
   u8 cmi_tt; /* enum a_colour_tag_type */
   char const cmi_name[13];
};
CTA(mx__COLOUR_IDS <= U8_MAX, "Enumeration exceeds storage datatype");

struct mx_colour_pen{
   struct str cp_dat; /* Pre-prepared ISO 6429 escape sequence */
};

struct a_colour_map /* : public mx_colour_pen */{
   struct mx_colour_pen cm_pen; /* Points into .cm_buf */
   struct a_colour_map *cm_next;
   char const *cm_tag; /* Colour tag or NIL for default (last) */
   struct a_colour_map_id const *cm_cmi;
#ifdef mx_HAVE_REGEX
   struct su_re *cm_re;
#endif
   u32 cm_refcnt; /* Beware of reference drops in recursions */
   u32 cm_user_off; /* User input offset in .cm_buf */
   char cm_buf[VFIELD_SIZE(0)];
};

struct a_colour_g{
   boole cg_is_init;
   u8 cg_type; /* a_colour_type */
   u8 __cg_pad[1];
   boole cg_v15_pager_warned;
   u32 cg_count; /* XXX Only for cg_v15_pager_warned */
   struct mx_colour_pen cg_reset; /* The reset sequence */
   struct a_colour_map
      *cg_maps[a_COLOUR_T_NONE][mx__COLOUR_CTX_MAX1][mx__COLOUR_IDS];
   char cg__reset_buf[Z_ALIGN(sizeof("\033[0m"))];
};

/* C99: use [INDEX]={} */
/* */
CTA(a_COLOUR_T_256 == 0, "Unexpected value of constant");
CTA(a_COLOUR_T_8 == 1, "Unexpected value of constant");
CTA(a_COLOUR_T_1 == 2, "Unexpected value of constant");
static char const a_colour_types[][8] = {"256", "iso", "mono"};

static struct a_colour_type_map const a_colour_type_maps[] = {
   {a_COLOUR_T_256, "256"},
   {a_COLOUR_T_8, "8"}, {a_COLOUR_T_8, "iso"}, {a_COLOUR_T_8, "ansi"},
   {a_COLOUR_T_1, "1"}, {a_COLOUR_T_1, "mono"}
};

CTAV(mx_COLOUR_CTX_SUM == 0);
CTAV(mx_COLOUR_CTX_VIEW == 1);
CTAV(mx_COLOUR_CTX_MLE == 2);
static char const a_colour_ctx_prefixes[mx__COLOUR_CTX_MAX1][8] = {
   "sum-", "view-", "mle-"
};

static struct a_colour_map_id const
      a_colour_map_ids[mx__COLOUR_CTX_MAX1][mx__COLOUR_IDS] = {{
   {mx_COLOUR_CTX_SUM, mx_COLOUR_ID_SUM_DOTMARK, a_COLOUR_TT_SUM, "dotmark"},
   {mx_COLOUR_CTX_SUM, mx_COLOUR_ID_SUM_HEADER, a_COLOUR_TT_SUM, "header"},
   {mx_COLOUR_CTX_SUM, mx_COLOUR_ID_SUM_THREAD, a_COLOUR_TT_SUM, "thread"},
   }, {
   {mx_COLOUR_CTX_VIEW, mx_COLOUR_ID_VIEW_FROM_, a_COLOUR_TT_NONE, "from_"},
   {mx_COLOUR_CTX_VIEW, mx_COLOUR_ID_VIEW_HEADER, a_COLOUR_TT_VIEW, "header"},
   {mx_COLOUR_CTX_VIEW, mx_COLOUR_ID_VIEW_MSGINFO, a_COLOUR_TT_NONE,
      "msginfo"},
   {mx_COLOUR_CTX_VIEW, mx_COLOUR_ID_VIEW_PARTINFO, a_COLOUR_TT_NONE,
      "partinfo"},
   }, {
   {mx_COLOUR_CTX_MLE, mx_COLOUR_ID_MLE_POSITION, a_COLOUR_TT_NONE,
      "position"},
   {mx_COLOUR_CTX_MLE, mx_COLOUR_ID_MLE_PROMPT, a_COLOUR_TT_NONE, "prompt"},
   {mx_COLOUR_CTX_MLE, mx_COLOUR_ID_MLE_ERROR, a_COLOUR_TT_NONE, "error"},
}};
#define a_COLOUR_MAP_SHOW_FIELDWIDTH \
   S(int,sizeof("view-")-1 + sizeof("partinfo")-1)

static struct a_colour_g a_colour_g;

/* */
static void a_colour_init(void);
static boole a_colour_termcap_init(void);

DVL( static void a_colour__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags); )

/* May we work with colour at the very moment? */
SINLINE boole a_colour_ok_to_go(u32 get_flags);

/* Find the type or -1 */
static enum a_colour_type a_colour_type_find(char const *name);

/* `(un)?colour' implementations */
static boole a_colour_mux(char const * const *argv);
static boole a_colour_unmux(char const * const *argv);

static boole a_colour__show(enum a_colour_type ct);
/* (regexpp may be NIL) */
static char const *a_colour__tag_identify(struct a_colour_map_id const *cmip,
      char const *ctag, void **regexpp);

/* Try to find a mapping identity for user given slotname */
static struct a_colour_map_id const *a_colour_map_id_find(
      char const *slotname);

/* Find an existing mapping for the given combination */
static struct a_colour_map *a_colour_map_find(enum mx_colour_ctx cctx,
      enum mx_colour_id cid, char const *ctag);

/* In-/Decrement reference counter, destroy if counts gets zero */
#define a_colour_map_ref(SELF) do{ ++(SELF)->cm_refcnt; }while(0)
static void a_colour_map_unref(struct a_colour_map *self);

/* Create an ISO 6429 (ECMA-48/ANSI) terminal control escape sequence from user
 * input spec, store it or on error message in *store */
static boole a_colour_iso6429(enum a_colour_type ct, char **store,
      char const *spec);

static void
a_colour_init(void){
   NYD2_IN;

   a_colour_g.cg_is_init = TRU1;
   su_mem_copy(a_colour_g.cg_reset.cp_dat.s = a_colour_g.cg__reset_buf,
      "\033[0m",
      a_colour_g.cg_reset.cp_dat.l = sizeof("\033[0m") -1); /* (calloc) */
   a_colour_g.cg_type = a_COLOUR_T_UNKNOWN;

   DVL( su_state_on_gut_install(&a_colour__on_gut, FAL0,
      su_STATE_ERR_NOPASS); )

   NYD2_OU;
}

static boole
a_colour_termcap_init(void){
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if(n_psonce & n_PSO_STARTED){
      struct mx_termcap_value tv;

      if(!mx_termcap_query(mx_TERMCAP_QUERY_colors, &tv))
         a_colour_g.cg_type = a_COLOUR_T_NONE;
      else{
         rv = TRU1;
         switch(tv.tv_data.tvd_numeric){
         case 256: a_colour_g.cg_type = a_COLOUR_T_256; break;
         case 8: a_colour_g.cg_type = a_COLOUR_T_8; break;
         case 1: a_colour_g.cg_type = a_COLOUR_T_1; break;
         default:
            if(n_poption & n_PO_D_V)
               n_err(_("Ignoring unsupported termcap entry for Co(lors)\n"));
            /* FALLTHRU */
         case 0:
            a_colour_g.cg_type = a_COLOUR_T_NONE;
            rv = FAL0;
            break;
         }
      }
   }

   NYD2_OU;
   return rv;
}

#if DVLOR(1, 0)
static void
a_colour__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags){
   NYD2_IN;

   if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
      char const * const argv[3] = {n_star, n_star, NIL};

      a_colour_unmux(argv);
   }

   su_mem_set(&a_colour_g, 0, sizeof a_colour_g);

   NYD2_OU;
}
#endif

SINLINE boole
a_colour_ok_to_go(u32 get_flags){
   u32 po;
   boole rv;
   NYD2_IN;

   rv = FAL0;
   po = (n_poption & n_PO_V_MASK);/* TODO *colour-disable* */
   n_poption &= ~n_PO_V_MASK; /* TODO log too loud - need "no log" bit!! */

   /* xxx Entire preamble could later be a shared function */
   if(!(n_psonce & n_PSO_TTYANY) || !(n_psonce & n_PSO_STARTED) ||
         ok_blook(colour_disable) ||
         (!(get_flags & mx_COLOUR_GET_FORCED) && !mx_COLOUR_IS_ACTIVE()) ||
         ((get_flags & mx_COLOUR_PAGER_USED) && !ok_blook(colour_pager)))
      goto jleave;
   if(UNLIKELY(!a_colour_g.cg_is_init))
      a_colour_init();
   if(UNLIKELY(a_colour_g.cg_type == a_COLOUR_T_NONE) ||
         !a_colour_termcap_init())
      goto jleave;

   /* TODO v15: drop colour_pager TODO: drop pager_used argument, thus! */
   if(!a_colour_g.cg_v15_pager_warned && a_colour_g.cg_count > 0 &&
         !ok_blook(colour_disable) && !ok_blook(colour_pager)){
      a_colour_g.cg_v15_pager_warned = TRU1;
      n_OBSOLETE("In v15 *colour-pager* is implied without *colour-disable*!");
   }

   rv = TRU1;
jleave:
   n_poption |= po;
   NYD2_OU;
   return rv;
}

static enum a_colour_type
a_colour_type_find(char const *name){
   struct a_colour_type_map const *ctmp;
   enum a_colour_type rv;
   NYD2_IN;

   ctmp = a_colour_type_maps;
   do if(!su_cs_cmp_case(ctmp->ctm_name, name)){
      rv = ctmp->ctm_type;
      goto jleave;
   }while(PCMP(++ctmp, !=, a_colour_type_maps + NELEM(a_colour_type_maps)));

   rv = R(enum a_colour_type,-1);
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_colour_mux(char const * const *argv){
   void *regexp;
   char const *mapname, *ctag;
   struct a_colour_map **cmap, *blcmp, *lcmp, *cmp;
   struct a_colour_map_id const *cmip;
   boole rv;
   enum a_colour_type ct;
   NYD2_IN;

   if(*argv == NIL)
      ct = R(enum a_colour_type,-1);
   else if((ct = a_colour_type_find(*argv++)) == R(enum a_colour_type,-1) &&
         (*argv != NIL || !n_is_all_or_aster(argv[-1]))){
      n_err(_("colour: invalid colour type %s\n"),
         n_shexp_quote_cp(argv[-1], FAL0));
      rv = FAL0;
      goto jleave;
   }

   if(!a_colour_g.cg_is_init)
      a_colour_init();

   if(*argv == NIL){
      rv = a_colour__show(ct);
      goto jleave;
   }

   rv = FAL0;
   regexp = NIL;

   if((cmip = a_colour_map_id_find(mapname = argv[0])) == NIL){
      n_err(_("colour: non-existing mapping: %s\n"),
         n_shexp_quote_cp(mapname, FAL0));
      goto jleave;
   }

   if(argv[1] == NIL){
      n_err(_("colour: %s: missing attribute argument\n"),
         n_shexp_quote_cp(mapname, FAL0));
      goto jleave;
   }

   /* Check whether preconditions are at all allowed, verify them as far as
    * possible as necessary.  For shell_quote() simplicity let's just ignore an
    * empty precondition */
   if((ctag = argv[2]) != NIL && *ctag != '\0'){
      char const *xtag;

      if(cmip->cmi_tt == a_COLOUR_TT_NONE){
         n_err(_("colour: %s does not support preconditions\n"),
            n_shexp_quote_cp(mapname, FAL0));
         goto jleave;
      }else if((xtag = a_colour__tag_identify(cmip, ctag, &regexp)
            ) == mx_COLOUR_TAG_ERR){
         /* I18N: ..of colour mapping */
         n_err(_("colour: %s: invalid precondition: %s\n"),
            n_shexp_quote_cp(mapname, FAL0), n_shexp_quote_cp(ctag, FAL0));
         goto jleave;
      }
      ctag = xtag;
   }

   /* At this time we have all the information to be able to query whether such
    * a mapping is yet established. If so, destroy it */
   for(blcmp = lcmp = NIL,
            cmp = *(cmap =
                  &a_colour_g.cg_maps[ct][cmip->cmi_ctx][cmip->cmi_id]);
         cmp != NIL; blcmp = lcmp, lcmp = cmp, cmp = cmp->cm_next){
      char const *xctag;

      if((xctag = cmp->cm_tag) == ctag ||
            (ctag != NIL && !a_COLOUR_TAG_IS_SPECIAL(ctag) &&
             xctag != NIL && !a_COLOUR_TAG_IS_SPECIAL(xctag) &&
             !su_cs_cmp(xctag, ctag))){
         if(lcmp == NIL)
            *cmap = cmp->cm_next;
         else
            lcmp->cm_next = cmp->cm_next;
         a_colour_map_unref(cmp);
         break;
      }
   }

   /* Create mapping */
   /* C99 */{
      uz tl, usrl, cl;
      char *bp, *cp;

      if(!a_colour_iso6429(ct, &cp, argv[1])){
         /* I18N: colour command: mapping: error message: user argument */
         n_err(_("colour: %s: %s: %s\n"), n_shexp_quote_cp(mapname, FAL0),
            cp, n_shexp_quote_cp(argv[1], FAL0));
         goto jleave;
      }

      tl = (ctag != NIL && !a_COLOUR_TAG_IS_SPECIAL(ctag))
            ? su_cs_len(ctag) : 0;
      cmp = su_ALLOC(VSTRUCT_SIZEOF(struct a_colour_map,cm_buf) +
            tl +1 + (usrl = su_cs_len(argv[1])) +1 + (cl = su_cs_len(cp)) +1);

      /* .cm_buf stuff */
      cmp->cm_pen.cp_dat.s = bp = cmp->cm_buf;
      cmp->cm_pen.cp_dat.l = cl;
      su_mem_copy(bp, cp, ++cl);
      bp += cl;

      cmp->cm_user_off = S(u32,P2UZ(bp - cmp->cm_buf));
      su_mem_copy(bp, argv[1], ++usrl);
      bp += usrl;

      if(tl > 0){
         cmp->cm_tag = bp;
         su_mem_copy(bp, ctag, ++tl);
         /*bp += tl;*/
      }else
         cmp->cm_tag = ctag;

      /* Non-buf stuff; default mapping */
      if(lcmp != NIL){
         /* Default mappings must be last */
         if(ctag == NIL){
            while(lcmp->cm_next != NIL)
               lcmp = lcmp->cm_next;
         }else if(lcmp->cm_next == NIL && lcmp->cm_tag == NIL){
            if((lcmp = blcmp) == NIL)
               goto jlinkhead;
         }
         cmp->cm_next = lcmp->cm_next;
         lcmp->cm_next = cmp;
      }else{
jlinkhead:
         cmp->cm_next = *cmap;
         *cmap = cmp;
      }
      cmp->cm_cmi = cmip;
#ifdef mx_HAVE_REGEX
      cmp->cm_re = regexp;
#endif
      cmp->cm_refcnt = 0;
      a_colour_map_ref(cmp);
      ++a_colour_g.cg_count;
   }

   rv = TRU1;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_colour_unmux(char const * const *argv){
   char const *mapname, *ctag, *xtag;
   struct a_colour_map **cmap, *lcmp, *cmp;
   struct a_colour_map_id const *cmip;
   enum a_colour_type ct;
   boole aster, rv;
   NYD2_IN;

   rv = TRU1;
   aster = FAL0;

   if((ct = a_colour_type_find(*argv++)) == R(enum a_colour_type,-1)){
      if(!n_is_all_or_aster(argv[-1])){
         n_err(_("uncolour: invalid colour type %s\n"),
            n_shexp_quote_cp(argv[-1], FAL0));
         rv = FAL0;
         goto j_leave;
      }
      aster = TRU1;
      ct = 0;
   }

   mapname = argv[0];
   ctag = argv[1];

   if(!a_colour_g.cg_is_init)
      goto jemap;

   /* Delete anything? */
jredo:
   if(ctag == NIL && mapname[0] == '*' && mapname[1] == '\0'){
      uz i1, i2;
      struct a_colour_map *tmp;

      for(i1 = 0; i1 < mx__COLOUR_CTX_MAX1; ++i1)
         for(i2 = 0; i2 < mx__COLOUR_IDS; ++i2)
            for(cmp = *(cmap = &a_colour_g.cg_maps[ct][i1][i2]), *cmap = NIL;
                  cmp != NIL;){
               tmp = cmp;
               cmp = cmp->cm_next;
               a_colour_map_unref(tmp);
               --a_colour_g.cg_count;
            }
   }else{
      if((cmip = a_colour_map_id_find(mapname)) == NIL){
         rv = FAL0;
jemap:
         /* I18N: colour cmd, mapping and precondition (option in quotes) */
         n_err(_("uncolour: non-existing mapping: %s%s%s\n"),
            n_shexp_quote_cp(mapname, FAL0), (ctag == NIL ? su_empty : " "),
            (ctag == NIL ? su_empty : n_shexp_quote_cp(ctag, FAL0)));
         goto jleave;
      }

      if((xtag = ctag) != NIL){
         if(cmip->cmi_tt == a_COLOUR_TT_NONE){
            n_err(_("uncolour: %s does not support preconditions\n"),
               n_shexp_quote_cp(mapname, FAL0));
            rv = FAL0;
            goto jleave;
         }else if((xtag = a_colour__tag_identify(cmip, ctag, NIL)
               ) == mx_COLOUR_TAG_ERR){
            n_err(_("uncolour: %s: invalid precondition: %s\n"),
               n_shexp_quote_cp(mapname, FAL0), n_shexp_quote_cp(ctag, FAL0));
            rv = FAL0;
            goto jleave;
         }

         /* (Improve user experience) */
         if(xtag != NIL && !a_COLOUR_TAG_IS_SPECIAL(xtag))
            ctag = xtag;
      }

      lcmp = NIL;
      cmp = *(cmap = &a_colour_g.cg_maps[ct][cmip->cmi_ctx][cmip->cmi_id]);
      for(;;){
         char const *xctag;

         if(cmp == NIL){
            rv = FAL0;
            goto jemap;
         }
         if((xctag = cmp->cm_tag) == ctag)
            break;
         if(ctag != NIL && !a_COLOUR_TAG_IS_SPECIAL(ctag) &&
               xctag != NIL && !a_COLOUR_TAG_IS_SPECIAL(xctag) &&
               !su_cs_cmp(xctag, ctag))
            break;
         lcmp = cmp;
         cmp = cmp->cm_next;
      }

      if(lcmp == NIL)
         *cmap = cmp->cm_next;
      else
         lcmp->cm_next = cmp->cm_next;
      a_colour_map_unref(cmp);
      --a_colour_g.cg_count;
   }

jleave:
   if(aster && ++ct != a_COLOUR_T_NONE)
      goto jredo;

j_leave:
   NYD2_OU;
   return rv;
}

static boole
a_colour__show(enum a_colour_type ct){
   struct a_colour_map *cmp;
   uz i1, i2;
   boole rv;
   NYD2_IN;

   /* Show all possible types? */
   if((rv = (ct == R(enum a_colour_type,-1) ? TRU1 : FAL0)))
      ct = 0;
jredo:
   for(i1 = 0; i1 < mx__COLOUR_CTX_MAX1; ++i1)
      for(i2 = 0; i2 < mx__COLOUR_IDS; ++i2){
         if((cmp = a_colour_g.cg_maps[ct][i1][i2]) == NIL)
            continue;

         for(; cmp != NIL; cmp = cmp->cm_next){
            char const *tag;

            if((tag = cmp->cm_tag) == NIL)
               tag = su_empty;
            else if(tag == mx_COLOUR_TAG_SUM_DOT)
               tag = "dot";
            else if(tag == mx_COLOUR_TAG_SUM_OLDER)
               tag = "older";

            fprintf(n_stdout, "colour %s %-*s %s %s\n",
               a_colour_types[ct], a_COLOUR_MAP_SHOW_FIELDWIDTH,
               savecat(a_colour_ctx_prefixes[i1],
                  a_colour_map_ids[i1][i2].cmi_name),
               (char const*)cmp->cm_buf + cmp->cm_user_off,
               n_shexp_quote_cp(tag, TRU1));
         }
      }

   if(rv && ++ct != a_COLOUR_T_NONE)
      goto jredo;

   rv = TRU1;
   NYD2_OU;
   return rv;
}

static char const *
a_colour__tag_identify(struct a_colour_map_id const *cmip, char const *ctag,
      void **regexpp){
   NYD2_IN;
   UNUSED(regexpp);

   if((cmip->cmi_tt & a_COLOUR_TT_DOT) && !su_cs_cmp_case(ctag, "dot"))
      ctag = mx_COLOUR_TAG_SUM_DOT;
   else if((cmip->cmi_tt & a_COLOUR_TT_OLDER) &&
         !su_cs_cmp_case(ctag, "older"))
      ctag = mx_COLOUR_TAG_SUM_OLDER;
   else if(cmip->cmi_tt & a_COLOUR_TT_HEADERS){
      char *cp, c;
      uz i;

      /* Can this be a valid list of headers? However, with regular expressions
       * simply use the input as such if it appears to be a regex */
#ifdef mx_HAVE_REGEX
      if(n_re_could_be_one_cp(ctag) && regexpp != NIL){
         struct su_re *rep;

         if(su_re_setup_cp((rep = su_ALLOC(sizeof(struct su_re))),
               ctag, (su_RE_SETUP_EXT | su_RE_SETUP_ICASE |
               su_RE_SETUP_TEST_ONLY)) == su_RE_ERROR_NONE)
            *regexpp = rep;
         else{
            n_err(_("colour: invalid regular expression: %s: %s\n"),
               n_shexp_quote_cp(ctag, FAL0), su_re_error_doc(rep));
            su_re_gut(rep);
            su_FREE(rep);
            goto jetag;
         }
      }else
#endif
      {
         /* Normalize to lowercase and strip any whitespace before use */
         i = su_cs_len(ctag);
         cp = su_AUTO_ALLOC(i +1);

         for(i = 0; (c = *ctag++) != '\0';){
            boole isblspc;

            isblspc = su_cs_is_space(c);

            if(!isblspc && !su_cs_is_alnum(c) && c != '-' && c != ',')
               goto jetag;
            /* Since we compare header names as they come from the message this
             * lowercasing is however redundant: we need to casecmp() them */
            if(!isblspc)
               cp[i++] = su_cs_to_lower(c);
         }
         cp[i] = '\0';
         ctag = cp;
      }
   }else
jetag:
      ctag = mx_COLOUR_TAG_ERR;

   NYD2_OU;
   return ctag;
}

static struct a_colour_map_id const *
a_colour_map_id_find(char const *cp){
   uz i;
   struct a_colour_map_id const (*cmip)[mx__COLOUR_IDS], *rv;
   NYD2_IN;

   rv = NIL;

   for(i = 0;; ++i){
      if(i == mx__COLOUR_IDS)
         goto jleave;
      else{
         uz j;

         j = su_cs_len(a_colour_ctx_prefixes[i]);
         if(!su_cs_cmp_case_n(cp, a_colour_ctx_prefixes[i], j)){
            cp += j;
            break;
         }
      }
   }
   cmip = &a_colour_map_ids[i];

   for(i = 0;; ++i){
      if(i == mx__COLOUR_IDS || (rv = &(*cmip)[i])->cmi_name[0] == '\0'){
         rv = NIL;
         break;
      }
      if(!su_cs_cmp_case(cp, rv->cmi_name))
         break;
   }

jleave:
   NYD2_OU;
   return rv;
}

static struct a_colour_map *
a_colour_map_find(enum mx_colour_ctx cctx, enum mx_colour_id cid,
      char const *ctag){
   struct a_colour_map *cmp;
   NYD2_IN;

   cmp = a_colour_g.cg_maps[a_colour_g.cg_type][cctx][cid];
   for(; cmp != NIL; cmp = cmp->cm_next){
      char const *xtag;

      if((xtag = cmp->cm_tag) == ctag)
         break;
      if(xtag == NIL)
         break;
      if(ctag == NIL || a_COLOUR_TAG_IS_SPECIAL(ctag))
         continue;
#ifdef mx_HAVE_REGEX
      if(cmp->cm_re != NIL){
         if(su_re_eval_cp(cmp->cm_re, ctag, su_RE_EVAL_NONE))
            break;
      }else
#endif
      if(cmp->cm_cmi->cmi_tt & a_COLOUR_TT_HEADERS){
         char *hlist, *cp;

         hlist = savestr(xtag);

         while((cp = su_cs_sep_c(&hlist, ',', TRU1)) != NIL){
            if(!su_cs_cmp_case(cp, ctag))
               break;
         }
         if(cp != NIL)
            break;
      }
   }

   NYD2_OU;
   return cmp;
}

static void
a_colour_map_unref(struct a_colour_map *self){
   NYD2_IN;

   if(--self->cm_refcnt == 0){
#ifdef mx_HAVE_REGEX
      if(self->cm_re != NIL){
         su_re_gut(self->cm_re);
         su_FREE(self->cm_re);
      }
#endif
      su_FREE(self);
   }

   NYD2_OU;
}

static boole
a_colour_iso6429(enum a_colour_type ct, char **store, char const *spec){
   struct isodesc{
      char id_name[15];
      char id_modc;
   } const fta[] = {
      {"bold", '1'}, {"underline", '4'}, {"reverse", '7'}
   }, ca[] = {
      {"black", '0'}, {"red", '1'}, {"green", '2'}, {"brown", '3'},
      {"blue", '4'}, {"magenta", '5'}, {"cyan", '6'}, {"white", '7'}
   }, *idp;
   char *xspec, *cp, fg[3], cfg[2 + 2*sizeof("255")];
   u8 ftno_base, ftno;
   boole rv;
   NYD_IN;

   rv = FAL0;
   /* 0/1 indicate usage, thereafter possibly 256 color sequences */
   cfg[0] = cfg[1] = 0;

   /* Since we use AUTO_ALLOC(), reuse the su_cs_sep_c() buffer also for the
    * return value, ensure we have enough room for that */
   /* C99 */{
      uz i;

      i = su_cs_len(spec) +1;
      xspec = su_AUTO_ALLOC(MAX(i, sizeof("\033[1;4;7;38;5;255;48;5;255m")));
      su_mem_copy(xspec, spec, i);
      spec = xspec;
   }

   /* Iterate over the colour spec */
   ftno = 0;
   while((cp = su_cs_sep_c(&xspec, ',', TRU1)) != NIL){
      char *y, *x;

      x = su_cs_find_c(cp, '=');
      if(x == NIL){
jbail:
         *store = UNCONST(char*,_("invalid attribute list"));
         goto jleave;
      }
      *x++ = '\0';

      if(!su_cs_cmp_case(cp, "ft")){
         if(!su_cs_cmp_case(x, "inverse")){
            n_OBSOLETE(_("please use reverse for ft= fonts, not inverse"));
            x = UNCONST(char*,"reverse");
         }

         for(idp = fta;; ++idp)
            if(idp == fta + NELEM(fta)){
               *store = UNCONST(char*,_("invalid font attribute"));
               goto jleave;
            }else if(!su_cs_cmp_case(x, idp->id_name)){
               if(ftno < NELEM(fg))
                  fg[ftno++] = idp->id_modc;
               else{
                  *store = UNCONST(char*,_("too many font attributes"));
                  goto jleave;
               }
               break;
            }
      }else if(!su_cs_cmp_case(cp, "fg")){
         y = cfg + 0;
         goto jiter_colour;
      }else if(!su_cs_cmp_case(cp, "bg")){
         y = cfg + 1;
jiter_colour:
         if(ct == a_COLOUR_T_1){
            *store = UNCONST(char*,_("colours are not allowed"));
            goto jleave;
         }
         /* Maybe 256 color spec */
         if(su_cs_is_digit(x[0])){
            u8 xv;

            if(ct == a_COLOUR_T_8){
               *store = UNCONST(char*,_("invalid colour for 8-colour mode"));
               goto jleave;
            }

            if((su_idec_u8_cp(&xv, x, 10, NIL
                     ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
                  ) != su_IDEC_STATE_CONSUMED){
               *store = UNCONST(char*,_("invalid 256-colour specification"));
               goto jleave;
            }
            y[0] = 5;
            su_mem_copy((y == &cfg[0] ? y + 2 : y + 1 + sizeof("255")), x,
               (x[1] == '\0' ? 2 : (x[2] == '\0' ? 3 : 4)));
         }else for(idp = ca;; ++idp)
            if(idp == ca + NELEM(ca)){
               *store = UNCONST(char*,_("invalid colour attribute"));
               goto jleave;
            }else if(!su_cs_cmp_case(x, idp->id_name)){
               y[0] = 1;
               y[2] = idp->id_modc;
               break;
            }
      }else
         goto jbail;
   }

   /* Restore our AUTO_ALLOC() buffer, create return value */
   xspec = UNCONST(char*,spec);
   if(ftno > 0 || cfg[0] || cfg[1]){ /* TODO unite/share colour setters */
      xspec[0] = '\033';
      xspec[1] = '[';
      xspec += 2;

      for(ftno_base = ftno; ftno > 0;){
         if(ftno-- != ftno_base)
            *xspec++ = ';';
         *xspec++ = fg[ftno];
      }

      if(cfg[0]){
         if(ftno_base > 0)
            *xspec++ = ';';
         xspec[0] = '3';
         if(cfg[0] == 1){
            xspec[1] = cfg[2];
            xspec += 2;
         }else{
            su_mem_copy(xspec + 1, "8;5;", 4);
            xspec += 5;
            for(ftno = 2; cfg[ftno] != '\0'; ++ftno)
               *xspec++ = cfg[ftno];
         }
      }

      if(cfg[1]){
         if(ftno_base > 0 || cfg[0])
            *xspec++ = ';';
         xspec[0] = '4';
         if(cfg[1] == 1){
            xspec[1] = cfg[3];
            xspec += 2;
         }else{
            su_mem_copy(xspec + 1, "8;5;", 4);
            xspec += 5;
            for(ftno = 2 + sizeof("255"); cfg[ftno] != '\0'; ++ftno)
               *xspec++ = cfg[ftno];
         }
      }

      *xspec++ = 'm';
   }
   *xspec = '\0';
   *store = UNCONST(char*,spec);

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
}

int
c_colour(void *v){
   int rv;
   NYD_IN;

   rv = !a_colour_mux(v);

   NYD_OU;
   return rv;
}

int
c_uncolour(void *v){
   int rv;
   NYD_IN;

   rv = !a_colour_unmux(v);

   NYD_OU;
   return rv;
}

void
mx_colour_stack_del(struct mx_go_data_ctx *gdcp){
   struct mx_colour_env *vp, *cep;
   NYD_IN;

   vp = gdcp->gdc_colour;
   gdcp->gdc_colour = NIL;
   gdcp->gdc_colour_active = FAL0;

   while((cep = vp) != NIL){
      vp = cep->ce_last;

      if(cep->ce_current != NIL && cep->ce_outfp == n_stdout){
         n_sighdl_t hdl;

         hdl = n_signal(SIGPIPE, SIG_IGN);
         fwrite(a_colour_g.cg_reset.cp_dat.s, a_colour_g.cg_reset.cp_dat.l, 1,
            cep->ce_outfp);
         fflush(cep->ce_outfp);
         n_signal(SIGPIPE, hdl);
      }
   }

   NYD_OU;
}

void
mx_colour_env_create(enum mx_colour_ctx cctx, FILE *fp, boole pager_used){
   struct mx_colour_env *cep;
   NYD_IN;

   if(!(n_psonce & n_PSO_TTYANY))
      goto jleave;
   if(!a_colour_g.cg_is_init)
      a_colour_init();

   /* TODO reset the outer level?  Iff ce_outfp==fp? */
   cep = su_AUTO_ALLOC(sizeof *cep);
   cep->ce_last = mx_go_data->gdc_colour;
   cep->ce_enabled = FAL0;
   cep->ce_ctx = cctx;
   cep->ce_ispipe = pager_used;
   cep->ce_outfp = fp;
   cep->ce_current = NIL;
   mx_go_data->gdc_colour_active = FAL0;
   mx_go_data->gdc_colour = cep;

   if(ok_blook(colour_disable) || (pager_used && !ok_blook(colour_pager)))
      goto jleave;
   if(a_colour_g.cg_type == a_COLOUR_T_NONE || !a_colour_termcap_init())
      goto jleave;

   /* TODO v15: drop colour_pager TODO: drop pager_used argument, thus! */
   if(!a_colour_g.cg_v15_pager_warned && a_colour_g.cg_count > 0 &&
         !ok_blook(colour_disable) && !ok_blook(colour_pager)){
      a_colour_g.cg_v15_pager_warned = TRU1;
      n_OBSOLETE("In v15 *colour-pager* is implied without *colour-disable*!");
   }

   mx_go_data->gdc_colour_active = cep->ce_enabled = TRU1;

jleave:
   NYD_OU;
}

void
mx_colour_env_gut(void){
   struct mx_colour_env *cep;
   NYD_IN;

   if(!(n_psonce & n_PSO_INTERACTIVE))
      goto jleave;

   /* TODO v15: Could happen because of jump, causing _stack_del().. */
   if((cep = mx_go_data->gdc_colour) == NIL)
      goto jleave;
   mx_go_data->gdc_colour_active = ((mx_go_data->gdc_colour = cep->ce_last
         ) != NIL && cep->ce_last->ce_enabled);

   if(cep->ce_current != NIL){
      n_sighdl_t hdl;

      hdl = n_signal(SIGPIPE, SIG_IGN);
      fwrite(a_colour_g.cg_reset.cp_dat.s, a_colour_g.cg_reset.cp_dat.l, 1,
         cep->ce_outfp);
      n_signal(SIGPIPE, hdl);
   }

jleave:
   NYD_OU;
}

void
mx_colour_put(enum mx_colour_id cid, char const *ctag){
   NYD_IN;

   if(mx_COLOUR_IS_ACTIVE()){
      struct mx_colour_env *cep;

      cep = mx_go_data->gdc_colour;

      if(cep->ce_current != NIL)
         fwrite(a_colour_g.cg_reset.cp_dat.s, a_colour_g.cg_reset.cp_dat.l, 1,
            cep->ce_outfp);

      if((cep->ce_current = a_colour_map_find(cep->ce_ctx, cid, ctag)) != NIL)
         fwrite(cep->ce_current->cm_pen.cp_dat.s,
            cep->ce_current->cm_pen.cp_dat.l, 1, cep->ce_outfp);
   }

   NYD_OU;
}

void
mx_colour_reset(void){
   NYD_IN;

   if(mx_COLOUR_IS_ACTIVE()){
      struct mx_colour_env *cep;

      cep = mx_go_data->gdc_colour;

      if(cep->ce_current != NIL){
         cep->ce_current = NIL;
         fwrite(a_colour_g.cg_reset.cp_dat.s, a_colour_g.cg_reset.cp_dat.l, 1,
            cep->ce_outfp);
      }
   }

   NYD_OU;
}

struct str const *
mx_colour_reset_to_str(void){
   struct str *rv;
   NYD_IN;

   if(mx_COLOUR_IS_ACTIVE())
      rv = &a_colour_g.cg_reset.cp_dat;
   else
      rv = NIL;

   NYD_OU;
   return rv;
}

struct mx_colour_pen *
mx_colour_pen_create(enum mx_colour_id cid, char const *ctag){
   struct a_colour_map *cmp;
   struct mx_colour_pen *rv;
   NYD_IN;

   if(mx_COLOUR_IS_ACTIVE() &&
         (cmp = a_colour_map_find(mx_go_data->gdc_colour->ce_ctx, cid, ctag)
               ) != NIL){
      union {void *vp; char *cp; struct mx_colour_pen *cpp;} u;

      u.vp = cmp;
      rv = u.cpp;
   }else
      rv = NIL;

   NYD_OU;
   return rv;
}

void
mx_colour_pen_put(struct mx_colour_pen *self){
   NYD_IN;

   if(mx_COLOUR_IS_ACTIVE()){
      union {void *vp; char *cp; struct a_colour_map *cmp;} u;
      struct mx_colour_env *cep;

      cep = mx_go_data->gdc_colour;
      u.vp = self;

      if(u.cmp != cep->ce_current){
         if(cep->ce_current != NIL)
            fwrite(a_colour_g.cg_reset.cp_dat.s, a_colour_g.cg_reset.cp_dat.l,
               1, cep->ce_outfp);

         if(u.cmp != NIL)
            fwrite(self->cp_dat.s, self->cp_dat.l, 1, cep->ce_outfp);
         cep->ce_current = u.cmp;
      }
   }

   NYD_OU;
}

struct str const *
mx_colour_pen_to_str(struct mx_colour_pen *self){
   struct str *rv;
   NYD_IN;

   if(mx_COLOUR_IS_ACTIVE() && self != NIL)
      rv = &self->cp_dat;
   else
      rv = NIL;

   NYD_OU;
   return rv;
}

struct str const *
mx_colour_get_reset_cseq(u32 get_flags){
   struct str const *rv;
   NYD_IN;

   rv = a_colour_ok_to_go(get_flags) ? &a_colour_g.cg_reset.cp_dat : NIL;

   NYD_OU;
   return rv;
}

struct mx_colour_pen *
mx_colour_get_pen(u32 get_flags, enum mx_colour_ctx cctx,
      enum mx_colour_id cid, char const *ctag){
   struct a_colour_map *cmp;
   struct mx_colour_pen *rv;
   NYD_IN;

   rv = NIL;

   if(a_colour_ok_to_go(get_flags) &&
         (cmp = a_colour_map_find(cctx, cid, ctag)) != NIL){
      union {void *v; struct mx_colour_pen *cp;} p;

      p.v = cmp;
      rv = p.cp;
   }

   NYD_OU;
   return rv;
}

struct str const *
mx_colour_pen_get_cseq(struct mx_colour_pen const *self){
   struct str const *rv;
   NYD2_IN;

   rv = (self != NIL) ? &self->cp_dat : 0;

   NYD2_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_COLOUR */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_COLOUR
/* s-it-mode */
