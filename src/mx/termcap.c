/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Terminal capability interaction.
 *@ For encapsulation purposes provide a basic foundation even without
 *@ mx_HAVE_TERMCAP, but with config.h:n_HAVE_TCAP.
 *@ HOWTO add a new non-dynamic command or query:
 *@ - add an entry to nail.h:enum n_termcap_{cmd,query}
 *@ - run make-tcap-map.pl
 *@ - update the *termcap* member documentation on changes!
 *@ Bug: in case of clashes of two-letter names terminfo(5) wins.
 *
 * Copyright (c) 2016 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE termcap

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

EMPTY_FILE()
#ifdef n_HAVE_TCAP
/* If available, curses.h must be included before term.h! */
#ifdef mx_HAVE_TERMCAP
# ifdef mx_HAVE_TERMCAP_CURSES
#  include <curses.h>
# endif
# include <term.h>
#endif

/*
 * xxx We are not really compatible with very old and strange terminals since
 * we don't care at all for circumstances indicated by terminal flags: if we
 * find a capability we use it and assume it works.  E.g., if "Co" indicates
 * colours we simply use ISO 6429 also for font attributes etc.  That is,
 * we don't use the ncurses/terminfo interface with all its internal logic.
 */

/* Unless mx_HAVE_TERMINFO or mx_HAVE_TGETENT_NULL_BUF are defined we use this
 * value to space the buffer we pass through to tgetent(3).
 * Since for (such) elder non-emulated terminals really weird things will
 * happen if an entry would require more than 1024 bytes, don't really mind.
 * Use a ui16_t for storage */
#define a_TERMCAP_ENTRYSIZE_MAX ((2668 + 128) & ~127) /* As of ncurses 6.0 */

n_CTA(a_TERMCAP_ENTRYSIZE_MAX < UI16_MAX,
   "Chosen buffer size exceeds datatype capability");

/* For simplicity we store commands and queries in single continuous control
 * and entry structure arrays: to index queries one has to add
 * n__TERMCAP_CMD_MAX1 first!  And don't confound with ENTRYSIZE_MAX! */
enum{
   a_TERMCAP_ENT_MAX1 = n__TERMCAP_CMD_MAX1 + n__TERMCAP_QUERY_MAX1
};

enum a_termcap_flags{
   a_TERMCAP_F_NONE,
   /* enum n_termcap_captype values stored here.
    * Note presence of a type in an a_termcap_ent signals initialization */
   a_TERMCAP_F_TYPE_MASK = (1<<4) - 1,

   a_TERMCAP_F_QUERY = 1<<4,     /* A query rather than a command */
   a_TERMCAP_F_DISABLED = 1<<5,  /* User explicitly disabled command/query */
   a_TERMCAP_F_ALTERN = 1<<6,    /* Not available, but has alternative */
   a_TERMCAP_F_NOENT = 1<<7,     /* Not available */

   /* _cmd() argument interpretion (_T_STR) */
   a_TERMCAP_F_ARG_IDX1 = 1<<11, /* Argument 1 used, and is an index */
   a_TERMCAP_F_ARG_IDX2 = 1<<12,
   a_TERMCAP_F_ARG_CNT = 1<<13,  /* .., and is a count */

   a_TERMCAP_F__LAST = a_TERMCAP_F_ARG_CNT
};
n_CTA((ui32_t)n__TERMCAP_CAPTYPE_MAX1 <= (ui32_t)a_TERMCAP_F_TYPE_MASK,
   "enum n_termcap_captype exceeds bit range of a_termcap_flags");

struct a_termcap_control{
   ui16_t tc_flags;
   /* Offset base into a_termcap_namedat[], which stores the two-letter
    * termcap(5) name directly followed by a NUL terminated terminfo(5) name.
    * A termcap(5) name may consist of two NULs meaning ERR_NOENT,
    * a terminfo(5) name may be empty for the same purpose */
   ui16_t tc_off;
};
n_CTA(a_TERMCAP_F__LAST <= UI16_MAX,
   "a_termcap_flags exceed storage datatype in a_termcap_control");

struct a_termcap_ent{
   ui16_t te_flags;
   ui16_t te_off;    /* in a_termcap_g->tg_dat / value for T_BOOL and T_NUM */
};
n_CTA(a_TERMCAP_F__LAST <= UI16_MAX,
   "a_termcap_flags exceed storage datatype in a_termcap_ent");

/* Structure for extended queries, which don't have an entry constant in
 * n_termcap_query (to allow free query/binding of keycodes) */
struct a_termcap_ext_ent{
   struct a_termcap_ent tee_super;
   ui8_t tee__dummy[4];
   struct a_termcap_ext_ent *tee_next;
   /* Resolvable termcap(5)/terminfo(5) name as given by user; the actual data
    * is stored just like for normal queries */
   char tee_name[n_VFIELD_SIZE(0)];
};

struct a_termcap_g{
   struct a_termcap_ext_ent *tg_ext_ents; /* List of extended queries */
   struct a_termcap_ent tg_ents[a_TERMCAP_ENT_MAX1];
   struct n_string tg_dat;                /* Storage for resolved caps */
# if !defined mx_HAVE_TGETENT_NULL_BUF && !defined mx_HAVE_TERMINFO
   char tg_lib_buf[a_TERMCAP_ENTRYSIZE_MAX];
# endif
};

/* Include the constant make-tcap-map.pl output */
#include <mx/gen-tcaps.h>
n_CTA(sizeof a_termcap_namedat <= UI16_MAX,
   "Termcap command and query name data exceed storage datatype");
n_CTA(a_TERMCAP_ENT_MAX1 == n_NELEM(a_termcap_control),
   "Control array does not match command/query array to be controlled");

static struct a_termcap_g *a_termcap_g;

/* Query *termcap*, parse it and incorporate into a_termcap_g */
static void a_termcap_init_var(struct str const *termvar);

/* Expand ^CNTRL, \[Ee] and \OCT.  False for parse error and empty results */
static bool_t a_termcap__strexp(struct n_string *store, char const *ibuf);

/* Initialize any _ent for which we have _F_ALTERN and which isn't yet set */
static void a_termcap_init_altern(void);

#ifdef mx_HAVE_TERMCAP
/* Setup the library we use to work with term */
static bool_t a_termcap_load(char const *term);

/* Query the capability tcp and fill in tep (upon success) */
static bool_t a_termcap_ent_query(struct a_termcap_ent *tep,
               char const *cname, ui16_t cflags);
n_INLINE bool_t a_termcap_ent_query_tcp(struct a_termcap_ent *tep,
                  struct a_termcap_control const *tcp);

/* Output PTF for both, termcap(5) and terminfo(5) */
static int a_termcap_putc(int c);
#endif

/* Get n_termcap_cmd or n_termcap_query constant belonging to (nlen bytes of)
 * name, -1 if not found.  min and max have to be used to cramp the result */
static si32_t a_termcap_enum_for_name(char const *name, size_t nlen,
               si32_t min, si32_t max);
#define a_termcap_cmd_for_name(NB,NL) \
   a_termcap_enum_for_name(NB, NL, 0, n__TERMCAP_CMD_MAX1)
#define a_termcap_query_for_name(NB,NL) \
   a_termcap_enum_for_name(NB, NL, n__TERMCAP_CMD_MAX1, a_TERMCAP_ENT_MAX1)

static void
a_termcap_init_var(struct str const *termvar){
   char *cbp_base, *cbp;
   size_t i;
   char const *ccp;
   NYD2_IN;

   if(termvar->l >= UI16_MAX){
      n_err(_("*termcap*: length excesses internal limit, skipping\n"));
      goto j_leave;
   }

   assert(termvar->s[termvar->l] == '\0');
   i = termvar->l +1;
   cbp_base = n_autorec_alloc(i);
   memcpy(cbp = cbp_base, termvar->s, i);

   for(; (ccp = n_strsep(&cbp, ',', TRU1)) != NULL;){
      struct a_termcap_ent *tep;
      size_t kl;
      char const *v;
      ui16_t f;

      /* Separate key/value, if any */
      if(/* no empties ccp[0] == '\0' ||*/ ccp[1] == '\0'){
jeinvent:
         n_err(_("*termcap*: invalid entry: %s\n"), ccp);
         continue;
      }
      for(kl = 2, v = &ccp[2];; ++kl, ++v){
         char c = *v;

         if(c == '\0'){
            f = n_TERMCAP_CAPTYPE_BOOL;
            break;
         }else if(c == '#'){
            f = n_TERMCAP_CAPTYPE_NUMERIC;
            ++v;
            break;
         }else if(c == '='){
            f = n_TERMCAP_CAPTYPE_STRING;
            ++v;
            break;
         }
      }

      /* Do we know about this one? */
      /* C99 */{
         struct a_termcap_control const *tcp;
         si32_t tci;

         tci = a_termcap_enum_for_name(ccp, kl, 0, a_TERMCAP_ENT_MAX1);
         if(tci < 0){
            /* For key binding purposes, save any given string */
#ifdef mx_HAVE_KEY_BINDINGS
            if((f & a_TERMCAP_F_TYPE_MASK) == n_TERMCAP_CAPTYPE_STRING){
               struct a_termcap_ext_ent *teep;

               teep = n_alloc(n_VSTRUCT_SIZEOF(struct a_termcap_ext_ent,
                     tee_name) + kl +1);
               teep->tee_next = a_termcap_g->tg_ext_ents;
               a_termcap_g->tg_ext_ents = teep;
               memcpy(teep->tee_name, ccp, kl);
               teep->tee_name[kl] = '\0';

               tep = &teep->tee_super;
               tep->te_flags = n_TERMCAP_CAPTYPE_STRING | a_TERMCAP_F_QUERY;
               tep->te_off = (ui16_t)a_termcap_g->tg_dat.s_len;
               if(!a_termcap__strexp(&a_termcap_g->tg_dat, v))
                  tep->te_flags |= a_TERMCAP_F_DISABLED;
               goto jlearned;
            }else
#endif /* mx_HAVE_KEY_BINDINGS */
                  if(n_poption & n_PO_D_V)
               n_err(_("*termcap*: unknown capability: %s\n"), ccp);
            continue;
         }
         i = (size_t)tci;

         tcp = &a_termcap_control[i];
         if((tcp->tc_flags & a_TERMCAP_F_TYPE_MASK) != f){
            n_err(_("*termcap*: entry type mismatch: %s\n"), ccp);
            break;
         }
         tep = &a_termcap_g->tg_ents[i];
         tep->te_flags = tcp->tc_flags;
         tep->te_off = (ui16_t)a_termcap_g->tg_dat.s_len;
      }

      if((f & a_TERMCAP_F_TYPE_MASK) == n_TERMCAP_CAPTYPE_BOOL)
         ;
      else if(*v == '\0')
         tep->te_flags |= a_TERMCAP_F_DISABLED;
      else if((f & a_TERMCAP_F_TYPE_MASK) == n_TERMCAP_CAPTYPE_NUMERIC){
         if((n_idec_ui16_cp(&tep->te_off, v, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED)
            goto jeinvent;
      }else if(!a_termcap__strexp(&a_termcap_g->tg_dat, v))
         tep->te_flags |= a_TERMCAP_F_DISABLED;
#ifdef mx_HAVE_KEY_BINDINGS
jlearned:
#endif
      if(n_poption & n_PO_D_VV)
         n_err(_("*termcap*: learned %.*s: %s\n"), (int)kl, ccp,
            (tep->te_flags & a_TERMCAP_F_DISABLED ? "<disabled>"
             : (f & a_TERMCAP_F_TYPE_MASK) == n_TERMCAP_CAPTYPE_BOOL ? "true"
               : v));
   }
   DBG( if(n_poption & n_PO_D_VV) n_err("*termcap* parsed: buffer used=%lu\n",
      (ul_i)a_termcap_g->tg_dat.s_len) );

   /* Catch some inter-dependencies the user may have triggered */
#ifdef mx_HAVE_TERMCAP
   if(a_termcap_g->tg_ents[n_TERMCAP_CMD_te].te_flags & a_TERMCAP_F_DISABLED)
      a_termcap_g->tg_ents[n_TERMCAP_CMD_ti].te_flags = a_TERMCAP_F_DISABLED;
   else if(a_termcap_g->tg_ents[n_TERMCAP_CMD_ti].te_flags &
         a_TERMCAP_F_DISABLED)
      a_termcap_g->tg_ents[n_TERMCAP_CMD_te].te_flags = a_TERMCAP_F_DISABLED;
#endif

j_leave:
   NYD2_OU;
}

static bool_t
a_termcap__strexp(struct n_string *store, char const *ibuf){ /* XXX ASCII */
   char c;
   char const *oibuf;
   size_t olen;
   NYD2_IN;

   olen = store->s_len;

   for(oibuf = ibuf; (c = *ibuf) != '\0';){
      if(c == '\\'){
         if((c = ibuf[1]) == '\0')
            goto jebsseq;

         if(c == 'E'){
            c = '\033';
            ibuf += 2;
            goto jpush;
         }

         if(octalchar(c)){
            char c2, c3;

            if((c2 = ibuf[2]) == '\0' || !octalchar(c2) ||
                  (c3 = ibuf[3]) == '\0' || !octalchar(c3)){
               n_err(_("*termcap*: invalid octal sequence: %s\n"), oibuf);
               goto jerr;
            }
            c -= '0', c2 -= '0', c3 -= '0';
            c <<= 3, c |= c2;
            if((ui8_t)c > 0x1F){
               n_err(_("*termcap*: octal number too large: %s\n"), oibuf);
               goto jerr;
            }
            c <<= 3, c |= c3;
            ibuf += 4;
            goto jpush;
         }
jebsseq:
         n_err(_("*termcap*: invalid reverse solidus \\ sequence: %s\n"),
            oibuf);
         goto jerr;
      }else if(c == '^'){
         if((c = ibuf[1]) == '\0'){
            n_err(_("*termcap*: incomplete ^CNTRL sequence: %s\n"), oibuf);
            goto jerr;
         }
         c = upperconv(c) ^ 0x40;
         if((ui8_t)c > 0x1F && c != 0x7F){ /* ASCII C0: 0..1F, 7F */
            n_err(_("*termcap*: invalid ^CNTRL sequence: %s\n"), oibuf);
            goto jerr;
         }
         ibuf += 2;
      }else
         ++ibuf;

jpush:
      store = n_string_push_c(store, c);
   }

   c = (store->s_len != olen) ? '\1' : '\0';
jleave:
   n_string_push_c(store, '\0');
   NYD2_OU;
   return (c != '\0');
jerr:
   store = n_string_trunc(store, olen);
   c = '\0';
   goto jleave;
}

static void
a_termcap_init_altern(void){
   /* We silently ignore user _F_DISABLED requests for those entries for which
    * we have fallback entries, and which we need to ensure proper functioning.
    * I.e., this allows users to explicitly disable some termcap(5) capability
    * and enforce usage of the built-in fallback */
   /* xxx Use table-based approach for fallback strategies */
#define a_OK(CMD) a_OOK(&a_termcap_g->tg_ents[CMD])
#define a_OOK(TEP) \
   ((TEP)->te_flags != 0 && !((TEP)->te_flags & a_TERMCAP_F_NOENT))
#define a_SET(TEP,CMD,ALT) \
   (TEP)->te_flags = a_termcap_control[CMD].tc_flags |\
      ((ALT) ? a_TERMCAP_F_ALTERN : 0)

   struct a_termcap_ent *tep;
   NYD2_IN;
   n_UNUSED(tep);

   /* For simplicity in the rest of this file null flags of disabled commands,
    * as we won't check and try to lazy query any command */
   /* C99 */{
      size_t i;

      for(i = n__TERMCAP_CMD_MAX1;;){
         if(i-- == 0)
            break;
         if((tep = &a_termcap_g->tg_ents[i])->te_flags & a_TERMCAP_F_DISABLED)
            tep->te_flags = 0;
      }
   }

#ifdef mx_HAVE_MLE
   /* ce == ch + [:SPACE:] (start column specified by argument) */
   tep = &a_termcap_g->tg_ents[n_TERMCAP_CMD_ce];
   if(!a_OOK(tep))
      a_SET(tep, n_TERMCAP_CMD_ce, TRU1);

   /* ch == cr[\r] + nd[:\033C:] */
   tep = &a_termcap_g->tg_ents[n_TERMCAP_CMD_ch];
   if(!a_OOK(tep))
      a_SET(tep, n_TERMCAP_CMD_ch, TRU1);

   /* cr == \r */
   tep = &a_termcap_g->tg_ents[n_TERMCAP_CMD_cr];
   if(!a_OOK(tep)){
      a_SET(tep, n_TERMCAP_CMD_cr, FAL0);
      tep->te_off = (ui16_t)a_termcap_g->tg_dat.s_len;
      n_string_push_c(n_string_push_c(&a_termcap_g->tg_dat, '\r'), '\0');
   }

   /* le == \b */
   tep = &a_termcap_g->tg_ents[n_TERMCAP_CMD_le];
   if(!a_OOK(tep)){
      a_SET(tep, n_TERMCAP_CMD_le, FAL0);
      tep->te_off = (ui16_t)a_termcap_g->tg_dat.s_len;
      n_string_push_c(n_string_push_c(&a_termcap_g->tg_dat, '\b'), '\0');
   }

   /* nd == \033[C (we may not fail, anyway, so use xterm sequence default) */
   tep = &a_termcap_g->tg_ents[n_TERMCAP_CMD_nd];
   if(!a_OOK(tep)){
      a_SET(tep, n_TERMCAP_CMD_nd, FAL0);
      tep->te_off = (ui16_t)a_termcap_g->tg_dat.s_len;
      n_string_push_buf(&a_termcap_g->tg_dat, "\033[C", sizeof("\033[C"));
   }

# ifdef mx_HAVE_TERMCAP
   /* cl == ho+cd */
   tep = &a_termcap_g->tg_ents[n_TERMCAP_CMD_cl];
   if(!a_OOK(tep)){
      if(a_OK(n_TERMCAP_CMD_cd) && a_OK(n_TERMCAP_CMD_ho))
         a_SET(tep, n_TERMCAP_CMD_cl, TRU1);
   }
# endif
#endif /* mx_HAVE_MLE */

   NYD2_OU;
#undef a_OK
#undef a_OOK
#undef a_SET
}

#ifdef mx_HAVE_TERMCAP
# ifdef mx_HAVE_TERMINFO
static bool_t
a_termcap_load(char const *term){
   bool_t rv;
   int err;
   NYD2_IN;

   if(!(rv = (setupterm(term, fileno(n_tty_fp), &err) == OK)))
      n_err(_("Unknown ${TERM}inal, using only *termcap*: %s\n"), term);
   NYD2_OU;
   return rv;
}

static bool_t
a_termcap_ent_query(struct a_termcap_ent *tep,
      char const *cname, ui16_t cflags){
   bool_t rv;
   NYD2_IN;
   assert(!(n_psonce & n_PSO_TERMCAP_DISABLE));

   if(n_UNLIKELY(*cname == '\0'))
      rv = FAL0;
   else switch((tep->te_flags = cflags) & a_TERMCAP_F_TYPE_MASK){
   case n_TERMCAP_CAPTYPE_BOOL:
      if(!(rv = (tigetflag(cname) > 0)))
         tep->te_flags |= a_TERMCAP_F_NOENT;
      tep->te_off = rv;
      break;
   case n_TERMCAP_CAPTYPE_NUMERIC:{
      int r = tigetnum(cname);

      if((rv = (r >= 0)))
         tep->te_off = (ui16_t)n_MIN(UI16_MAX, r);
      else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   default:
   case n_TERMCAP_CAPTYPE_STRING:{
      char *cp;

      cp = tigetstr(cname);
      if((rv = (cp != NULL && cp != (char*)-1))){
         tep->te_off = (ui16_t)a_termcap_g->tg_dat.s_len;
         n_string_push_buf(&a_termcap_g->tg_dat, cp, strlen(cp) +1);
      }else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   }
   NYD2_OU;
   return rv;
}

n_INLINE bool_t
a_termcap_ent_query_tcp(struct a_termcap_ent *tep,
      struct a_termcap_control const *tcp){
   assert(!(n_psonce & n_PSO_TERMCAP_DISABLE));
   return a_termcap_ent_query(tep, &a_termcap_namedat[tcp->tc_off] + 2,
      tcp->tc_flags);
}

# else /* mx_HAVE_TERMINFO */
static bool_t
a_termcap_load(char const *term){
   bool_t rv;
   NYD2_IN;

   /* ncurses may return -1 */
# ifndef mx_HAVE_TGETENT_NULL_BUF
#  define a_BUF &a_termcap_g->tg_lib_buf[0]
# else
#  define a_BUF NULL
# endif
   if(!(rv = tgetent(a_BUF, term) > 0))
      n_err(_("Unknown ${TERM}inal, using only *termcap*: %s\n"), term);
# undef a_BUF
   NYD2_OU;
   return rv;
}

static bool_t
a_termcap_ent_query(struct a_termcap_ent *tep,
      char const *cname, ui16_t cflags){
   bool_t rv;
   NYD2_IN;
   assert(!(n_psonce & n_PSO_TERMCAP_DISABLE));

   if(n_UNLIKELY(*cname == '\0'))
      rv = FAL0;
   else switch((tep->te_flags = cflags) & a_TERMCAP_F_TYPE_MASK){
   case n_TERMCAP_CAPTYPE_BOOL:
      if(!(rv = (tgetflag(cname) > 0)))
         tep->te_flags |= a_TERMCAP_F_NOENT;
      tep->te_off = rv;
      break;
   case n_TERMCAP_CAPTYPE_NUMERIC:{
      int r = tgetnum(cname);

      if((rv = (r >= 0)))
         tep->te_off = (ui16_t)n_MIN(UI16_MAX, r);
      else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   default:
   case n_TERMCAP_CAPTYPE_STRING:{
# ifndef mx_HAVE_TGETENT_NULL_BUF
      char buf_base[a_TERMCAP_ENTRYSIZE_MAX], *buf = &buf_base[0];
#  define a_BUF &buf
# else
#  define a_BUF NULL
# endif
      char *cp;

      if((rv = ((cp = tgetstr(cname, a_BUF)) != NULL))){
         tep->te_off = (ui16_t)a_termcap_g->tg_dat.s_len;
         n_string_push_buf(&a_termcap_g->tg_dat, cp, strlen(cp) +1);
# undef a_BUF
      }else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   }
   NYD2_OU;
   return rv;
}

n_INLINE bool_t
a_termcap_ent_query_tcp(struct a_termcap_ent *tep,
      struct a_termcap_control const *tcp){
   assert(!(n_psonce & n_PSO_TERMCAP_DISABLE));
   return a_termcap_ent_query(tep, &a_termcap_namedat[tcp->tc_off],
      tcp->tc_flags);
}
# endif /* !mx_HAVE_TERMINFO */

static int
a_termcap_putc(int c){
   return putc(c, n_tty_fp);
}
#endif /* mx_HAVE_TERMCAP */

static si32_t
a_termcap_enum_for_name(char const *name, size_t nlen, si32_t min, si32_t max){
   struct a_termcap_control const *tcp;
   char const *cnam;
   si32_t rv;
   NYD2_IN;

   /* Prefer terminfo(5) names */
   for(rv = max;;){
      if(rv-- == min){
         rv = -1;
         break;
      }

      tcp = &a_termcap_control[(ui32_t)rv];
      cnam = &a_termcap_namedat[tcp->tc_off];
      if(cnam[2] != '\0'){
         char const *xcp = cnam + 2;

         if(nlen == strlen(xcp) && !memcmp(xcp, name, nlen))
            break;
      }
      if(nlen == 2 && cnam[0] == name[0] && cnam[1] == name[1])
         break;
   }
   NYD2_OU;
   return rv;
}

FL void
n_termcap_init(void){
   struct n_termcap_value tv;
   struct str termvar;
   char const *ccp;
   NYD_IN;

   assert((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK));

   a_termcap_g = n_alloc(sizeof *a_termcap_g);
   a_termcap_g->tg_ext_ents = NULL;
   memset(&a_termcap_g->tg_ents[0], 0, sizeof(a_termcap_g->tg_ents));
   if((ccp = ok_vlook(termcap)) != NULL)
      termvar.l = strlen(termvar.s = n_UNCONST(ccp));
   else
      /*termvar.s = NULL,*/ termvar.l = 0;
   n_string_reserve(n_string_creat(&a_termcap_g->tg_dat),
      ((termvar.l + (256 - 64)) & ~127));

   if(termvar.l > 0)
      a_termcap_init_var(&termvar);

   if(ok_blook(termcap_disable))
      n_psonce |= n_PSO_TERMCAP_DISABLE;
#ifdef mx_HAVE_TERMCAP
   else if((ccp = ok_vlook(TERM)) == NULL){
      n_err(_("Environment variable $TERM not set, using only *termcap*\n"));
      n_psonce |= n_PSO_TERMCAP_DISABLE;
   }else if(!a_termcap_load(ccp))
      n_psonce |= n_PSO_TERMCAP_DISABLE;
   else{
      /* Query termcap(5) for each command slot that is not yet set */
      struct a_termcap_ent *tep;
      size_t i;

      for(i = n__TERMCAP_CMD_MAX1;;){
         if(i-- == 0)
            break;
         if((tep = &a_termcap_g->tg_ents[i])->te_flags == 0)
            a_termcap_ent_query_tcp(tep, &a_termcap_control[i]);
      }
   }
#endif /* mx_HAVE_TERMCAP */

   a_termcap_init_altern();

#ifdef mx_HAVE_TERMCAP
   if(a_termcap_g->tg_ents[n_TERMCAP_CMD_te].te_flags != 0 &&
         ok_blook(termcap_ca_mode))
      n_psonce |= n_PSO_TERMCAP_CA_MODE;
#endif

   /* TODO We do not handle !n_TERMCAP_QUERY_sam in this software! */
   if(!n_termcap_query(n_TERMCAP_QUERY_am, &tv) ||
         n_termcap_query(n_TERMCAP_QUERY_xenl, &tv))
      n_psonce |= n_PSO_TERMCAP_FULLWIDTH;

   n_TERMCAP_RESUME(TRU1);
   NYD_OU;
}

FL void
n_termcap_destroy(void){
   NYD_IN;
   assert((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK));
   assert(a_termcap_g != NULL);

   n_TERMCAP_SUSPEND(TRU1);

#ifdef mx_HAVE_DEBUG
   /* C99 */{
      struct a_termcap_ext_ent *tmp;

      while((tmp = a_termcap_g->tg_ext_ents) != NULL){
         a_termcap_g->tg_ext_ents = tmp->tee_next;
         n_free(tmp);
      }
   }
   n_string_gut(&a_termcap_g->tg_dat);
   n_free(a_termcap_g);
   a_termcap_g = NULL;
#endif
   NYD_OU;
}

#ifdef mx_HAVE_TERMCAP
FL void
n_termcap_resume(bool_t complete){
   NYD_IN;
   if(a_termcap_g != NULL && !(n_psonce & n_PSO_TERMCAP_DISABLE)){
      if(complete && (n_psonce & n_PSO_TERMCAP_CA_MODE))
         n_termcap_cmdx(n_TERMCAP_CMD_ti);
      n_termcap_cmdx(n_TERMCAP_CMD_ks);
      fflush(n_tty_fp);
   }
   NYD_OU;
}

FL void
n_termcap_suspend(bool_t complete){
   NYD_IN;
   if(a_termcap_g != NULL && !(n_psonce & n_PSO_TERMCAP_DISABLE)){
      n_termcap_cmdx(n_TERMCAP_CMD_ke);
      if(complete && (n_psonce & n_PSO_TERMCAP_CA_MODE))
         n_termcap_cmdx(n_TERMCAP_CMD_te);
      fflush(n_tty_fp);
   }
   NYD_OU;
}
#endif /* mx_HAVE_TERMCAP */

FL ssize_t
n_termcap_cmd(enum n_termcap_cmd cmd, ssize_t a1, ssize_t a2){
   /* Commands are not lazy queried */
   struct a_termcap_ent const *tep;
   enum a_termcap_flags flags;
   ssize_t rv;
   NYD2_IN;
   n_UNUSED(a1);
   n_UNUSED(a2);

   rv = FAL0;
   if(a_termcap_g == NULL)
      goto jleave;

   flags = cmd & ~n__TERMCAP_CMD_MASK;
   cmd &= n__TERMCAP_CMD_MASK;
   tep = a_termcap_g->tg_ents;

   if((flags & n_TERMCAP_CMD_FLAG_CA_MODE) &&
         !(n_psonce & n_PSO_TERMCAP_CA_MODE))
      rv = TRU1;
   else if((tep += cmd)->te_flags == 0 || (tep->te_flags & a_TERMCAP_F_NOENT))
      rv = TRUM1;
   else if(!(tep->te_flags & a_TERMCAP_F_ALTERN)){
      char const *cp;

      assert((tep->te_flags & a_TERMCAP_F_TYPE_MASK) ==
         n_TERMCAP_CAPTYPE_STRING);

      cp = &a_termcap_g->tg_dat.s_dat[tep->te_off];

#ifdef mx_HAVE_TERMCAP
      if(tep->te_flags & (a_TERMCAP_F_ARG_IDX1 | a_TERMCAP_F_ARG_IDX2)){
         if(n_psonce & n_PSO_TERMCAP_DISABLE){
            if(n_poption & n_PO_D_V){
               char const *cnam = &a_termcap_namedat[
                     a_termcap_control[cmd].tc_off];

               if(cnam[2] != '\0')
                  cnam += 2;
               n_err(_("*termcap-disable*d (/$TERM not set/unknown): "
                  "can't perform CAP: %s\n"), cnam);
            }
            goto jleave;
         }

         /* Follow Thomas Dickey's advise on pre-va_arg prototypes, add 0s */
# ifdef mx_HAVE_TERMINFO
         if((cp = tparm(cp, a1, a2, 0,0,0,0,0,0,0)) == NULL)
            goto jleave;
# else
         /* curs_termcap.3:
          * The \fBtgoto\fP function swaps the order of parameters.
          * It does this also for calls requiring only a single parameter.
          * In that case, the first parameter is merely a placeholder. */
         if(!(tep->te_flags & a_TERMCAP_F_ARG_IDX2)){
            a2 = a1;
            a1 = (ui32_t)-1;
         }
         if((cp = tgoto(cp, (int)a1, (int)a2)) == NULL)
            goto jleave;
# endif
      }
#endif /* mx_HAVE_TERMCAP */

      for(;;){
#ifdef mx_HAVE_TERMCAP
         if(!(n_psonce & n_PSO_TERMCAP_DISABLE)){
            if(tputs(cp, 1, &a_termcap_putc) != OK)
               break;
         }else
#endif
               if(fputs(cp, n_tty_fp) == EOF)
            break;
         if(!(tep->te_flags & a_TERMCAP_F_ARG_CNT) || --a1 <= 0){
            rv = TRU1;
            break;
         }
      }
      goto jflush;
   }else{
      switch(cmd){
      default:
         rv = TRUM1;
         break;

#ifdef mx_HAVE_MLE
      case n_TERMCAP_CMD_ce: /* ce == ch + [:SPACE:] */
         if(a1 > 0)
            --a1;
         if((rv = n_termcap_cmd(n_TERMCAP_CMD_ch, a1, 0)) > 0){
            for(a2 = n_scrnwidth - a1 - 1; a2 > 0; --a2)
               if(putc(' ', n_tty_fp) == EOF){
                  rv = FAL0;
                  break;
               }
            if(rv && n_termcap_cmd(n_TERMCAP_CMD_ch, a1, -1) != TRU1)
               rv = FAL0;
         }
         break;
      case n_TERMCAP_CMD_ch: /* ch == cr + nd */
         rv = n_termcap_cmdx(n_TERMCAP_CMD_cr);
         if(rv > 0 && a1 > 0){
            rv = n_termcap_cmd(n_TERMCAP_CMD_nd, a1, -1);
         }
         break;
# ifdef mx_HAVE_TERMCAP
      case n_TERMCAP_CMD_cl: /* cl = ho + cd */
         rv = n_termcap_cmdx(n_TERMCAP_CMD_ho);
         if(rv > 0)
            rv = n_termcap_cmdx(n_TERMCAP_CMD_cd | flags);
         break;
# endif
#endif /* mx_HAVE_MLE */
      }

jflush:
      if(flags & n_TERMCAP_CMD_FLAG_FLUSH)
         fflush(n_tty_fp);
      if(ferror(n_tty_fp))
         rv = FAL0;
   }

jleave:
   NYD2_OU;
   return rv;
}

FL bool_t
n_termcap_query(enum n_termcap_query query, struct n_termcap_value *tvp){
   /* Queries are lazy queried upon request */
   /* XXX n_termcap_query(): boole handling suboptimal, tvp used on success */
   struct a_termcap_ent const *tep;
   bool_t rv;
   NYD2_IN;

   assert(tvp != NULL);

   rv = FAL0;
   if(a_termcap_g == NULL)
      goto jleave;

   /* Is it a built-in query? */
   if(query != n__TERMCAP_QUERY_MAX1){
      tep = &a_termcap_g->tg_ents[n__TERMCAP_CMD_MAX1 + query];

      if(tep->te_flags == 0
#ifdef mx_HAVE_TERMCAP
            && ((n_psonce & n_PSO_TERMCAP_DISABLE) ||
               !a_termcap_ent_query_tcp(n_UNCONST(tep),
                  &a_termcap_control[n__TERMCAP_CMD_MAX1 + query]))
#endif
      )
         goto jleave;
   }else{
#ifdef mx_HAVE_TERMCAP
      size_t nlen;
#endif
      struct a_termcap_ext_ent *teep;
      char const *ndat = tvp->tv_data.tvd_string;

      for(teep = a_termcap_g->tg_ext_ents; teep != NULL; teep = teep->tee_next)
         if(!strcmp(teep->tee_name, ndat)){
            tep = &teep->tee_super;
            goto jextok;
         }

#ifdef mx_HAVE_TERMCAP
      if(n_psonce & n_PSO_TERMCAP_DISABLE)
#endif
         goto jleave;
#ifdef mx_HAVE_TERMCAP
      nlen = strlen(ndat) +1;
      teep = n_alloc(n_VSTRUCT_SIZEOF(struct a_termcap_ext_ent, tee_name) +
            nlen);
      tep = &teep->tee_super;
      teep->tee_next = a_termcap_g->tg_ext_ents;
      a_termcap_g->tg_ext_ents = teep;
      memcpy(teep->tee_name, ndat, nlen);

      if(!a_termcap_ent_query(n_UNCONST(tep), ndat,
               n_TERMCAP_CAPTYPE_STRING | a_TERMCAP_F_QUERY))
         goto jleave;
#endif
jextok:;
   }

   if(tep->te_flags & a_TERMCAP_F_NOENT)
      goto jleave;

   rv = (tep->te_flags & a_TERMCAP_F_ALTERN) ? TRUM1 : TRU1;

   switch((tvp->tv_captype = tep->te_flags & a_TERMCAP_F_TYPE_MASK)){
   case n_TERMCAP_CAPTYPE_BOOL:
      tvp->tv_data.tvd_bool = (bool_t)tep->te_off;
      break;
   case n_TERMCAP_CAPTYPE_NUMERIC:
      tvp->tv_data.tvd_numeric = (ui32_t)tep->te_off;
      break;
   default:
   case n_TERMCAP_CAPTYPE_STRING:
      tvp->tv_data.tvd_string = a_termcap_g->tg_dat.s_dat + tep->te_off;
      break;
   }
jleave:
   NYD2_OU;
   return rv;
}

#ifdef mx_HAVE_KEY_BINDINGS
FL si32_t
n_termcap_query_for_name(char const *name, enum n_termcap_captype type){
   si32_t rv;
   NYD2_IN;

   if((rv = a_termcap_query_for_name(name, strlen(name))) >= 0){
      struct a_termcap_control const *tcp = &a_termcap_control[(ui32_t)rv];

      if(type != n_TERMCAP_CAPTYPE_NONE &&
            (tcp->tc_flags & a_TERMCAP_F_TYPE_MASK) != type)
         rv = -2;
      else
         rv -= n__TERMCAP_CMD_MAX1;
   }
   NYD2_OU;
   return rv;
}

FL char const *
n_termcap_name_of_query(enum n_termcap_query query){
   char const *rv;
   NYD2_IN;

   rv = &a_termcap_namedat[
         a_termcap_control[n__TERMCAP_CMD_MAX1 + query].tc_off + 2];
   NYD2_OU;
   return rv;
}
#endif /* mx_HAVE_KEY_BINDINGS */
#endif /* n_HAVE_TCAP */

/* s-it-mode */
