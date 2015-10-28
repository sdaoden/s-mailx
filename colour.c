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

/* Not needed publically, but extends a set from nail.h */
#define n_COLOUR_TAG_ERR ((char*)-1)
#define a_COLOUR_TAG_IS_SPECIAL(P) (PTR2SIZE(P) >= PTR2SIZE(-3))

enum a_colour_type{
   a_COLOUR_T_COLOUR,
   a_COLOUR_T_MONO,
   a_COLOUR_T_TYPES = 2
};

enum a_colour_tag_type{
   a_COLOUR_TT_NONE,
   a_COLOUR_TT_DOT = 1<<0,       /* "dot" */
   a_COLOUR_TT_OLDER = 1<<1,     /* "older" */
   a_COLOUR_TT_HEADERS = 1<<2,   /* Comma-separated list of headers allowed */

   a_COLOUR_TT_SUM = a_COLOUR_TT_DOT | a_COLOUR_TT_OLDER,
   a_COLOUR_TT_VIEW = a_COLOUR_TT_HEADERS
};

struct a_colour_map_id{
   ui8_t cmi_group;              /* enum n_colour_group */
   ui8_t cmi_id;                 /* enum n_colour_id */
   ui8_t cmi_tt;                 /* enum a_colour_tag_type */
   char const cmi_name[13];
};
CTA(_n_COLOUR_IDS <= UI8_MAX);

struct n_colour_pen{
   struct str cp_dat;   /* Pre-prepared ISO 6429 escape sequence */
};

struct a_colour_map /* : public n_colour_pen */{
   struct n_colour_pen cm_pen;   /* Points into .cm_buf */
   struct a_colour_map *cm_next;
   char const *cm_tag;           /* Colour tag or NULL for default (last) */
   struct a_colour_map_id const *cm_cmi;
#ifdef HAVE_REGEX
   regex_t *cm_regex;
#endif
   ui32_t cm_refcnt;             /* Beware of reference drops in recursions */
   ui32_t cm_user_off;           /* User input offset in .cm_buf */
   char cm_buf[VFIELD_SIZE(0)];
};

struct a_colour_g{
   /* TODO cg_has_env not used, we have to go for PS_COLOUR_ACTIVE */
   bool_t cg_has_env;
   ui8_t cg_group;                  /* If .cg_has_env, enum n_colour_group */
   ui8_t __cg_pad[6];
   struct a_colour_map *cg_active;  /* The currently active colour */
   /* Active mapping: .cg_colour_maps on colour terminal, _mono_ otherwise */
   struct a_colour_map *(*cg_maps)[_n_COLOUR_GROUPS][_n_COLOUR_IDS];
   struct n_colour_pen cg_reset;    /* The reset sequence */
   struct a_colour_map *cg_colour_maps[_n_COLOUR_GROUPS][_n_COLOUR_IDS];
   struct a_colour_map *cg_mono_maps[_n_COLOUR_GROUPS][_n_COLOUR_IDS];
   char cg_reset_buf[sizeof("\033[0m")];
};

/* TODO The colour environment simply should be a pointer into an
 * TODO carrier structure in equal spirit to the fio.c stack, which gets
 * TODO created for each execute() cycle (long in TODO), and carries along
 * TODO all the information, memory allocations and also output (filter)
 * TODO chains, so that we could actually decide wether we could simply
 * TODO suspend output for a chain, need to place reset sequences, etc.
 * TODO For now, since we have no such carrier to know where colour
 * TODO sequences have to be written, creating a colour environment requires
 * TODO that the current colour state is "reset", because we wouldn't know
 * TODO where to place reset sequences and ditto, reestablish colour.
 * TODO This should be no problem in practice, however */
struct a_colour_env{
   struct a_colour_env *ce_last;
   ui8_t ce_group;               /* enum n_colour_group active upon switch */
   bool_t ce_is_active;          /* Was colour active in outer level? */
   ui8_t __ce_pad[6];
};

/* C99: use [INDEX]={} */
CTA(n_COLOUR_GROUP_SUM == 0);
CTA(n_COLOUR_GROUP_VIEW == 1);
static char const a_colour_group_prefixes[_n_COLOUR_GROUPS][8] = {
   "sum-", "view-"
};

static struct a_colour_map_id const
      a_colour_map_ids[_n_COLOUR_GROUPS][_n_COLOUR_IDS] = {{
   {n_COLOUR_GROUP_SUM, n_COLOUR_ID_SUM_DOTMARK, a_COLOUR_TT_SUM, "dotmark"},
   {n_COLOUR_GROUP_SUM, n_COLOUR_ID_SUM_HEADER, a_COLOUR_TT_SUM, "header"},
   {n_COLOUR_GROUP_SUM, n_COLOUR_ID_SUM_THREAD, a_COLOUR_TT_SUM, "thread"},
   }, {
   {n_COLOUR_GROUP_VIEW, n_COLOUR_ID_VIEW_FROM_, a_COLOUR_TT_NONE, "from_"},
   {n_COLOUR_GROUP_VIEW, n_COLOUR_ID_VIEW_HEADER, a_COLOUR_TT_VIEW, "header"},
   {n_COLOUR_GROUP_VIEW, n_COLOUR_ID_VIEW_MSGINFO, a_COLOUR_TT_NONE, "msginfo"},
   {n_COLOUR_GROUP_VIEW, n_COLOUR_ID_VIEW_PARTINFO, a_COLOUR_TT_NONE,
      "partinfo"},
}};
#define a_COLOUR_MAP_SHOW_FIELDWIDTH \
   (int)(sizeof("view-")-1 + sizeof("partinfo")-1)

static struct a_colour_g *a_colour_g;
static struct a_colour_env *a_colour_env;

static void a_colour_init(void);
DBG( static void a_colour_atexit(void); )

/* Shared `(un)?(colour|mono)' implementations */
static bool_t a_colour_mux(enum a_colour_type ct, char **argv);
static bool_t a_colour_unmux(enum a_colour_type ct, char **argv);

static bool_t a_colour__show(enum a_colour_type tp);
/* (regexpp may be NULL) */
static char const *a_colour__tag_identify(struct a_colour_map_id const *cmip,
                     char const *ctag, void **regexpp);

/* Try to find a mapping identity for user given slotname */
static struct a_colour_map_id const *a_colour_map_id_find(char const *slotname);

/* Find an existing mapping for the given combination */
static struct a_colour_map *a_colour_map_find(enum n_colour_id cid,
                              char const *ctag);

/* In-/Decrement reference counter, destroy if counts gets zero */
#define a_colour_map_ref(SELF) do{ ++(SELF)->cm_refcnt; }while(0)
static void a_colour_map_unref(struct a_colour_map *self);

/* Create an ISO 6429 (ECMA-48/ANSI) terminal control escape sequence from user
 * input spec, store it or on error message in *store */
static bool_t a_colour_iso6429(enum a_colour_type ct, char **store,
               char const *spec);

static void
a_colour_init(void){
   char const *term, *cp;
   char *okterms;
   NYD2_ENTER;

   a_colour_g = scalloc(1, sizeof *a_colour_g);

   if((term = env_vlook("TERM", FAL0)) == NULL || !asccasecmp(term, "dumb"))
      goto jleave;

   memcpy(a_colour_g->cg_reset.cp_dat.s = a_colour_g->cg_reset_buf, "\033[0m",
      a_colour_g->cg_reset.cp_dat.l = sizeof("\033[0m") -1); /* (calloc) */

   /* terminfo rocks: if we find "color", assume it's right; don't case care */
   a_colour_g->cg_maps = &a_colour_g->cg_colour_maps;

   if(asccasestr(term, "color") != NULL)
      goto jmaps;

   if((cp = ok_vlook(colour_terms)) == NULL)
      cp = n_COLOUR_TERMS;
   okterms = savestr(cp);
   while((cp = n_strsep(&okterms, ',', TRU1)) != NULL)
      if(!asccasecmp(cp, term))
         goto jmaps;

   a_colour_g->cg_maps = &a_colour_g->cg_mono_maps;
jmaps:
   DBG( atexit(&a_colour_atexit); ) /* TODO prog-global atexit event */
jleave:
   NYD2_LEAVE;
}

#ifdef HAVE_DEBUG
static void
a_colour_atexit(void){
   NYD_ENTER;
   if(a_colour_env != NULL)
      n_colour_env_pop(TRU1);
   free(a_colour_g);
   a_colour_g = NULL;
   NYD_LEAVE;
}
#endif

static bool_t
a_colour_mux(enum a_colour_type ct, char **argv){
   void *regexp;
   char const *cmd, *mapname, *ctag;
   struct a_colour_map *(*mapp)[_n_COLOUR_GROUPS][_n_COLOUR_IDS], **cmap,
      *blcmp, *lcmp, *cmp;
   struct a_colour_map_id const *cmip;
   bool_t rv;
   NYD2_ENTER;

   if(a_colour_g == NULL)
      a_colour_init();

   if(*argv == NULL){
      rv = a_colour__show(ct);
      goto jleave;
   }

   rv = FAL0;
   regexp = NULL;
   cmd = (ct == a_COLOUR_T_COLOUR) ? "colour" : "mono";
   mapp = (ct == a_COLOUR_T_COLOUR) ? &a_colour_g->cg_colour_maps
         : &a_colour_g->cg_mono_maps;

   if((cmip = a_colour_map_id_find(mapname = argv[0])) == NULL){
      n_err(_("`%s': non-existing mapping: \"%s\"\n"), cmd, mapname);
      goto jleave;
   }

   if(argv[1] == NULL){
      n_err(_("`%s': \"%s\": missing attributes argument\n"), cmd, mapname);
      goto jleave;
   }

   /* Check wether preconditions are at all allowed, verify them as far as
    * possible as necessary */
   if((ctag = argv[2]) != NULL){
      char const *xtag;

      if(cmip->cmi_tt == a_COLOUR_TT_NONE){
         n_err(_("`%s': \"%s\" doesn't support preconditions\n"), cmd, mapname);
         goto jleave;
      }else if((xtag = a_colour__tag_identify(cmip, ctag, &regexp)) ==
            n_COLOUR_TAG_ERR){
         /* I18N: ..of colour mapping */
         n_err(_("`%s': \"%s\": invalid precondition: \"%s\"\n"),
            cmd, mapname, ctag);
         goto jleave;
      }
      ctag = xtag;
   }

   /* At this time we have all the information to be able to query wether such
    * a mapping is yet established. If so, destroy it */
   for(blcmp = lcmp = NULL,
            cmp = *(cmap = &(*mapp)[cmip->cmi_group][cmip->cmi_id]);
         cmp != NULL; blcmp = lcmp, lcmp = cmp, cmp = cmp->cm_next){
      char const *xctag = cmp->cm_tag;

      if(xctag == ctag ||
            (ctag != NULL && !a_COLOUR_TAG_IS_SPECIAL(ctag) &&
             xctag != NULL && !a_COLOUR_TAG_IS_SPECIAL(xctag) &&
             !strcmp(xctag, ctag))){
         if(lcmp == NULL)
            *cmap = cmp->cm_next;
         else
            lcmp->cm_next = cmp->cm_next;
         a_colour_map_unref(cmp);
         break;
      }
   }

   /* Create mapping */
   /* C99 */{
      size_t tl, ul, cl;
      char *bp, *cp;

      if(!a_colour_iso6429(ct, &cp, argv[1])){
         /* I18N: colour command: mapping: error message: user argument */
         n_err(_("`%s': \"%s\": %s: \"%s\"\n"), cmd, mapname, cp, argv[1]);
         goto jleave;
      }

      tl = (ctag != NULL && !a_COLOUR_TAG_IS_SPECIAL(ctag)) ? strlen(ctag) : 0;
      cmp = smalloc(sizeof(*cmp) - VFIELD_SIZEOF(struct a_colour_map, cm_buf) +
            tl +1 + (ul = strlen(argv[1])) +1 + (cl = strlen(cp)) +1);

      /* .cm_buf stuff */
      cmp->cm_pen.cp_dat.s = bp = cmp->cm_buf;
      cmp->cm_pen.cp_dat.l = cl;
      memcpy(bp, cp, ++cl);
      bp += cl;

      cmp->cm_user_off = (ui32_t)PTR2SIZE(bp - cmp->cm_buf);
      memcpy(bp, argv[1], ++ul);
      bp += ul;

      if(tl > 0){
         cmp->cm_tag = bp;
         memcpy(bp, ctag, ++tl);
         bp += tl;
      }else
         cmp->cm_tag = ctag;

      /* Non-buf stuff; default mapping */
      if(lcmp != NULL){
         /* Default mappings must be last */
         if(ctag == NULL){
            while(lcmp->cm_next != NULL)
               lcmp = lcmp->cm_next;
         }else if(lcmp->cm_next == NULL && lcmp->cm_tag == NULL){
            if((lcmp = blcmp) == NULL)
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
#ifdef HAVE_REGEX
      cmp->cm_regex = regexp;
#endif
      cmp->cm_refcnt = 0;
      a_colour_map_ref(cmp);
   }
   rv = TRU1;
jleave:
   NYD2_LEAVE;
   return rv;
}

static bool_t
a_colour_unmux(enum a_colour_type ct, char **argv){
   char const *cmd, *mapname, *ctag, *xtag;
   struct a_colour_map *(*mapp)[_n_COLOUR_GROUPS][_n_COLOUR_IDS], **cmap,
      *lcmp, *cmp;
   struct a_colour_map_id const *cmip;
   bool_t rv;
   NYD2_ENTER;

   cmd = (ct == a_COLOUR_T_COLOUR) ? "uncolour" : "unmono";
   mapname = argv[0];
   ctag = (mapname != NULL) ? argv[1] : mapname;

   rv = TRU1;
   if(a_colour_g == NULL)
      goto jemap;
   rv = FAL0;

   mapp = (ct == a_COLOUR_T_COLOUR)
         ? &a_colour_g->cg_colour_maps : &a_colour_g->cg_mono_maps;

   /* Delete anything? */
   if(ctag == NULL && mapname[0] == '*' && mapname[1] == '\0'){
      size_t i1, i2;
      struct a_colour_map *tmp;

      for(i1 = 0; i1 < _n_COLOUR_GROUPS; ++i1)
         for(i2 = 0; i2 < _n_COLOUR_IDS; ++i2)
            for(cmp = *(cmap = &(*mapp)[i1][i2]), *cmap = NULL; cmp != NULL;){
               tmp = cmp;
               cmp = cmp->cm_next;
               a_colour_map_unref(tmp);
            }
   }else{
      if(a_colour_g == NULL){
jemap:
         /* I18N: colour command, mapping and precondition (option in quotes) */
         n_err(_("`%s': non-existing mapping: \"%s\"%s%s%s\n"),
            cmd, mapname, (ctag == NULL ? "" : " \""),
            (ctag == NULL ? "" : ctag), (ctag == NULL ? "" : "\""));
         goto jleave;
      }

      if((cmip = a_colour_map_id_find(mapname)) == NULL)
         goto jemap;

      if((xtag = ctag) != NULL){
         if(cmip->cmi_tt == a_COLOUR_TT_NONE){
            n_err(_("`%s': \"%s\" doesn't support preconditions\n"),
               cmd, mapname);
            goto jleave;
         }else if((xtag = a_colour__tag_identify(cmip, ctag, NULL)) ==
               n_COLOUR_TAG_ERR){
            n_err(_("`%s': \"%s\": invalid precondition: \"%s\"\n"),
               cmd, mapname, ctag);
            goto jleave;
         }
         /* (Improve user experience) */
         if(xtag != NULL && !a_COLOUR_TAG_IS_SPECIAL(xtag))
            ctag = xtag;
      }

      lcmp = NULL;
      cmp = *(cmap = &(*mapp)[cmip->cmi_group][cmip->cmi_id]);
      for(;;){
         char const *xctag;

         if(cmp == NULL)
            goto jemap;
         if((xctag = cmp->cm_tag) == ctag)
            break;
         if(ctag != NULL && !a_COLOUR_TAG_IS_SPECIAL(ctag) &&
               xctag != NULL && !a_COLOUR_TAG_IS_SPECIAL(xctag) &&
               !strcmp(xctag, ctag))
            break;
         lcmp = cmp;
         cmp = cmp->cm_next;
      }

      if(lcmp == NULL)
         *cmap = cmp->cm_next;
      else
         lcmp->cm_next = cmp->cm_next;
      a_colour_map_unref(cmp);
   }
   rv = TRU1;
jleave:
   NYD2_LEAVE;
   return rv;
}

static bool_t
a_colour__show(enum a_colour_type tp){
   char const *cmd;
   struct a_colour_map *(*mapp)[_n_COLOUR_GROUPS][_n_COLOUR_IDS], *cmp;
   size_t i1, i2;
   bool_t rv;
   NYD2_ENTER;

   if(tp == a_COLOUR_T_COLOUR){
      cmd = "colour";
      mapp = &a_colour_g->cg_colour_maps;
   }else{
      cmd = "mono";
      mapp = &a_colour_g->cg_mono_maps;
   }

   for(i1 = 0; i1 < _n_COLOUR_GROUPS; ++i1)
      for(i2 = 0; i2 < _n_COLOUR_IDS; ++i2){
         if((cmp = (*mapp)[i1][i2]) == NULL)
            continue;

         while(cmp != NULL){
            char const *tagann, *tag;

            tagann = "";
            if((tag = cmp->cm_tag) == NULL)
               tag = "";
            else if(tag == n_COLOUR_TAG_SUM_DOT)
               tag = "dot";
            else if(tag == n_COLOUR_TAG_SUM_OLDER)
               tag = "older";
#ifdef HAVE_REGEX
            else if(cmp->cm_regex != NULL)
               tagann = "[rx] ";
#endif
            printf("%s %-*s %s %s%s\n",
               cmd, a_COLOUR_MAP_SHOW_FIELDWIDTH,
               savecat(a_colour_group_prefixes[i1],
                  a_colour_map_ids[i1][i2].cmi_name),
               (char const*)cmp->cm_buf + cmp->cm_user_off,
               tagann, tag);
            cmp = cmp->cm_next;
         }
      }
   rv = TRU1;
   NYD2_LEAVE;
   return rv;
}

static char const *
a_colour__tag_identify(struct a_colour_map_id const *cmip, char const *ctag,
      void **regexpp){
   NYD2_ENTER;
   UNUSED(regexpp);

   if((cmip->cmi_tt & a_COLOUR_TT_DOT) && !asccasecmp(ctag, "dot"))
      ctag = n_COLOUR_TAG_SUM_DOT;
   else if((cmip->cmi_tt & a_COLOUR_TT_OLDER) && !asccasecmp(ctag, "older"))
      ctag = n_COLOUR_TAG_SUM_OLDER;
   else if(cmip->cmi_tt & a_COLOUR_TT_HEADERS){
      char *cp, c;
      size_t i;

      /* Can this be a valid list of headers?  However, with regular expressions
       * simply use the input as such if it appears to be a regex */
#ifdef HAVE_REGEX
      if(is_maybe_regex(ctag)){
         if(regexpp != NULL && regcomp(*regexpp = smalloc(sizeof(regex_t)),
               ctag, REG_EXTENDED | REG_ICASE | REG_NOSUB)){
            free(*regexpp);
            goto jetag;
         }
      }else
#endif
      {
         /* Normalize to lowercase and strip any whitespace before use */
         i = strlen(ctag);
         cp = salloc(i +1);

         for(i = 0; (c = *ctag++) != '\0';){
            bool_t isblspc = blankspacechar(c);

            if(!isblspc && !alnumchar(c) && c != '-' && c != ',')
               goto jetag;
            /* Since we compare header names as they come from the message this
             * lowercasing is however redundant: we need to asccasecmp() them */
            if(!isblspc)
               cp[i++] = lowerconv(c);
         }
         cp[i] = '\0';
         ctag = cp;
      }
   }else
jetag:
      ctag = n_COLOUR_TAG_ERR;
   NYD2_LEAVE;
   return ctag;
}

static struct a_colour_map_id const *
a_colour_map_id_find(char const *cp){
   size_t i;
   struct a_colour_map_id const (*cmip)[_n_COLOUR_IDS], *rv;
   NYD2_ENTER;

   rv = NULL;

   for(i = 0;; ++i){
      if(i == _n_COLOUR_IDS)
         goto jleave;
      else{
         size_t j = strlen(a_colour_group_prefixes[i]);
         if(!ascncasecmp(cp, a_colour_group_prefixes[i], j)){
            cp += j;
            break;
         }
      }
   }
   cmip = &a_colour_map_ids[i];

   for(i = 0;; ++i){
      if(i == _n_COLOUR_IDS || (rv = &(*cmip)[i])->cmi_name[0] == '\0'){
         rv = NULL;
         break;
      }
      if(!asccasecmp(cp, rv->cmi_name))
         break;
   }
jleave:
   NYD2_LEAVE;
   return rv;
}

static struct a_colour_map *
a_colour_map_find(enum n_colour_id cid, char const *ctag){
   struct a_colour_map *cmp;
   NYD2_ENTER;

   for(cmp = (*a_colour_g->cg_maps)[a_colour_g->cg_group][cid]; cmp != NULL;
         cmp = cmp->cm_next){
      char const *xtag = cmp->cm_tag;

      if(xtag == ctag)
         break;
      if(xtag == NULL)
         break;
      if(ctag == NULL || a_COLOUR_TAG_IS_SPECIAL(ctag))
         continue;
#ifdef HAVE_REGEX
      if(cmp->cm_regex != NULL){
         if(regexec(cmp->cm_regex, ctag, 0,NULL, 0) != REG_NOMATCH)
            break;
      }else
#endif
      if(cmp->cm_cmi->cmi_tt & a_COLOUR_TT_HEADERS){
         char *hlist = savestr(xtag), *cp;

         while((cp = n_strsep(&hlist, ',', TRU1)) != NULL){
            if(!asccasecmp(cp, ctag))
               break;
         }
         if(cp != NULL)
            break;
      }
   }
   NYD2_LEAVE;
   return cmp;
}

static void
a_colour_map_unref(struct a_colour_map *self){
   NYD2_ENTER;
   if(--self->cm_refcnt == 0){
#ifdef HAVE_REGEX
      if(self->cm_regex != NULL){
         regfree(self->cm_regex);
         free(self->cm_regex);
      }
#endif
      free(self);
   }
   NYD2_LEAVE;
}

static bool_t
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
   ui8_t ftno_base, ftno;
   bool_t rv;
   NYD_ENTER;

   rv = FAL0;
   /* 0/1 indicate usage, thereafter possibly 256 color sequences */
   cfg[0] = cfg[1] = 0;

   /* Since we use salloc(), reuse the n_strsep() buffer also for the return
    * value, ensure we have enough room for that */
   /* C99 */{
      size_t i = strlen(spec) +1;
      xspec = salloc(MAX(i, sizeof("\033[1;4;7;38;5;255;48;5;255m")));
      memcpy(xspec, spec, i);
      spec = xspec;
   }

   /* Iterate over the colour spec */
   ftno = 0;
   while((cp = n_strsep(&xspec, ',', TRU1)) != NULL){
      char *y, *x = strchr(cp, '=');
      if(x == NULL){
jbail:
         *store = UNCONST(_("invalid attribute list"));
         goto jleave;
      }
      *x++ = '\0';

      if(!asccasecmp(cp, "ft")){
         if(!asccasecmp(x, "inverse")){
            OBSOLETE(_("please use \"reverse\" not \"inverse\" for ft= fonts"));
            x = UNCONST("reverse");
         }
         for(idp = fta;; ++idp)
            if(idp == fta + NELEM(fta)){
               *store = UNCONST(_("invalid font attribute"));
               goto jleave;
            }else if(!asccasecmp(x, idp->id_name)){
               if(ftno < NELEM(fg))
                  fg[ftno++] = idp->id_modc;
               else{
                  *store = UNCONST(_("too many font attributes"));
                  goto jleave;
               }
               break;
            }
      }else if(!asccasecmp(cp, "fg")){
         y = cfg + 0;
         goto jiter_colour;
      }else if(!asccasecmp(cp, "bg")){
         y = cfg + 1;
jiter_colour:
         if(ct == a_COLOUR_T_MONO){
            *store = UNCONST(_("colours are not allowed"));
            goto jleave;
         }
         /* Maybe 256 color spec TODO allow user some query to check
          * TODO wether this is applicable, we know with TERMCAP,
          * TODO we might now otherwise; macros with arguments/returns or
          * TODO special variable or whatever */
         if(digitchar(x[0])){
            sl_i xv;

            xv = strtol(x, &cp, 10);
            if(xv < 0 || xv > 255 || *cp != '\0' || PTRCMP(&x[3], <, cp)){
               *store = UNCONST(_("invalid 256-colour specification"));
               goto jleave;
            }
            y[0] = 5;
            memcpy((y == &cfg[0] ? y + 2 : y + 1 + sizeof("255")), x,
               (x[1] == '\0' ? 2 : (x[2] == '\0' ? 3 : 4)));
         }else for(idp = ca;; ++idp)
            if(idp == ca + NELEM(ca)){
               *store = UNCONST(_("invalid colour attribute"));
               goto jleave;
            }else if(!asccasecmp(x, idp->id_name)){
               y[0] = 1;
               y[2] = idp->id_modc;
               break;
            }
      }else
         goto jbail;
   }

   /* Restore our salloc() buffer, create return value */
   xspec = UNCONST(spec);
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
            memcpy(xspec + 1, "8;5;", 4);
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
            memcpy(xspec + 1, "8;5;", 4);
            xspec += 5;
            for(ftno = 2 + sizeof("255"); cfg[ftno] != '\0'; ++ftno)
               *xspec++ = cfg[ftno];
         }
      }

      *xspec++ = 'm';
   }
   *xspec = '\0';
   *store = UNCONST(spec);
   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_colour(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_colour_mux(a_COLOUR_T_COLOUR, v);
   NYD_LEAVE;
   return rv;
}

FL int
c_uncolour(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_colour_unmux(a_COLOUR_T_COLOUR, v);
   NYD_LEAVE;
   return rv;
}

FL int
c_mono(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_colour_mux(a_COLOUR_T_MONO, v);
   NYD_LEAVE;
   return rv;
}

FL int
c_unmono(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_colour_unmux(a_COLOUR_T_MONO, v);
   NYD_LEAVE;
   return rv;
}

FL void
n_colour_env_create(enum n_colour_group cgrp, bool_t pager_used){
   NYD_ENTER;
   if(!(options & OPT_INTERACTIVE))
      goto jleave;

   if (ok_blook(colour_disable) || (pager_used && !ok_blook(colour_pager))){
      n_colour_env_push(); /* FIXME lex.c only pops (always env */
      goto jleave;
   }

   if(a_colour_g == NULL)
      a_colour_init();
   if(a_colour_g->cg_maps == NULL)
      goto jleave;

   a_colour_g->cg_group = cgrp;
   a_colour_g->cg_active = NULL;
   pstate |= PS_COLOUR_ACTIVE;
jleave:
   NYD_LEAVE;
}

FL void
n_colour_env_push(void){
   struct a_colour_env *cep;
   NYD_ENTER;

   if(!(options & OPT_INTERACTIVE))
      goto jleave;

   cep = smalloc(sizeof *cep);
   cep->ce_last = a_colour_env;
   if(a_colour_g != NULL){
      cep->ce_group = a_colour_g->cg_group;
      a_colour_g->cg_active = NULL;
   }
   cep->ce_is_active = ((pstate & PS_COLOUR_ACTIVE) != 0);
   a_colour_env = cep;

   pstate &= ~PS_COLOUR_ACTIVE;
jleave:
   NYD_LEAVE;
}

FL void
n_colour_env_pop(bool_t any_env_till_root){
   NYD_ENTER;
   if(!(options & OPT_INTERACTIVE))
      goto jleave;

   while(a_colour_env != NULL){
      struct a_colour_env *cep;

      if((cep = a_colour_env)->ce_is_active)
         pstate |= PS_COLOUR_ACTIVE;
      else
         pstate &= ~PS_COLOUR_ACTIVE;

      if(a_colour_g != NULL){
         a_colour_g->cg_active = NULL;
         a_colour_g->cg_group = cep->ce_group;
      }
      a_colour_env = cep->ce_last;

      free(cep);
      if(!any_env_till_root)
         break;
   }

   if(any_env_till_root && a_colour_g != NULL && (pstate & PS_COLOUR_ACTIVE)){
      pstate &= ~PS_COLOUR_ACTIVE;
      a_colour_g->cg_active = NULL;
   }
jleave:
   NYD_LEAVE;
}

FL void
n_colour_env_gut(FILE *fp){
   NYD_ENTER;
   if((options & OPT_INTERACTIVE) && (pstate & PS_COLOUR_ACTIVE)){
      pstate &= ~PS_COLOUR_ACTIVE;

      if(a_colour_g->cg_active != NULL){
         a_colour_g->cg_active = NULL;
         if(fp != NULL)
            fwrite(a_colour_g->cg_reset.cp_dat.s, a_colour_g->cg_reset.cp_dat.l,
               1, fp);
      }
   }
   NYD_LEAVE;
}

FL void
n_colour_put(FILE *fp, enum n_colour_id cid, char const *ctag){
   NYD_ENTER;
   if(pstate & PS_COLOUR_ACTIVE){
      if(a_colour_g->cg_active != NULL)
         fwrite(a_colour_g->cg_reset.cp_dat.s, a_colour_g->cg_reset.cp_dat.l, 1,
            fp);
      if((a_colour_g->cg_active = a_colour_map_find(cid, ctag)) != NULL)
         fwrite(a_colour_g->cg_active->cm_pen.cp_dat.s,
            a_colour_g->cg_active->cm_pen.cp_dat.l, 1, fp);
   }
   NYD_LEAVE;
}

FL void
n_colour_reset(FILE *fp){
   NYD_ENTER;
   if((pstate & PS_COLOUR_ACTIVE) && a_colour_g->cg_active != NULL){
      a_colour_g->cg_active = NULL;
      fwrite(a_colour_g->cg_reset.cp_dat.s, a_colour_g->cg_reset.cp_dat.l, 1,
         fp);
   }
   NYD_LEAVE;
}

FL struct str const *
n_colour_reset_to_str(void){
   struct str *rv;
   NYD_ENTER;

   if(pstate & PS_COLOUR_ACTIVE)
      rv = &a_colour_g->cg_reset.cp_dat;
   else
      rv = NULL;
   NYD_LEAVE;
   return rv;
}

FL struct n_colour_pen *
n_colour_pen_create(enum n_colour_id cid, char const *ctag){
   struct a_colour_map *cmp;
   struct n_colour_pen *rv;
   NYD_ENTER;

   if((pstate & PS_COLOUR_ACTIVE) &&
         (cmp = a_colour_map_find(cid, ctag)) != NULL){
      union {void *vp; char *cp; struct n_colour_pen *cpp;} u;
      u.vp = cmp;
      rv = u.cpp;
   }else
      rv = NULL;
   NYD_LEAVE;
   return rv;
}

FL void
n_colour_pen_put(struct n_colour_pen *self, FILE *fp){
   NYD_ENTER;
   if(pstate & PS_COLOUR_ACTIVE){
      union {void *vp; char *cp; struct a_colour_map *cmp;} u;

      u.vp = self;
      if(u.cmp != a_colour_g->cg_active){
         if(a_colour_g->cg_active != NULL)
            fwrite(a_colour_g->cg_reset.cp_dat.s, a_colour_g->cg_reset.cp_dat.l,
               1, fp);
         if(u.cmp != NULL)
            fwrite(self->cp_dat.s, self->cp_dat.l, 1, fp);
         a_colour_g->cg_active = u.cmp;
      }
   }
   NYD_LEAVE;
}

FL struct str const *
n_colour_pen_to_str(struct n_colour_pen *self){
   struct str *rv;
   NYD_ENTER;

   if((pstate & PS_COLOUR_ACTIVE) && self != NULL)
      rv = &self->cp_dat;
   else
      rv = NULL;
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_COLOUR */

/* s-it-mode */
