/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of termcap.h.
 *@ For encapsulation purposes provide a basic foundation even without
 *@ HOWTO add a new non-dynamic command or query:
 *@ - add an entry to enum mx_termcap_{cmd,query}
 *@ - run make-tcap-map.pl
 *@ - update the *termcap* member documentation on changes!
 *@ Bug: in case of clashes of two-letter names terminfo(5) wins.
 *
 * Copyright (c) 2016 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE termcap
#define mx_SOURCE
#define mx_SOURCE_TERMCAP

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#include "mx/termcap.h"
#ifdef mx_HAVE_TCAP

/* If available, curses.h must be included before term.h! */
#ifdef mx_HAVE_TERMCAP
# ifdef mx_HAVE_TERMCAP_CURSES
#  include <curses.h>
# endif
# include <term.h>
#endif
#undef lines /* xxx */

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/compat.h"
#include "mx/termios.h"
#include "mx/tty.h"

/* Already: #include "mx/termcap.h"*/
/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/*
 * xxx We are not really compatible with very old and strange terminals since
 * we do not care at all for circumstances indicated by terminal flags: if we
 * find a capability we use it and assume it works.  E.g., if "Co" indicates
 * colours we simply use ISO 6429 also for font attributes etc.  That is,
 * we do not use the ncurses/terminfo interface with all its internal logic.
 */

/* Unless mx_HAVE_TERMINFO or mx_HAVE_TGETENT_NULL_BUF are defined we use this
 * value to space the buffer we pass through to tgetent(3).
 * Since for (such) elder non-emulated terminals really weird things will
 * happen if an entry would require more than 1024 bytes, do not really mind.
 * Use a u16 for storage */
#define a_TERMCAP_ENTRYSIZE_MAX ((2668u + 128) & ~127u) /* As of ncurses 6.0 */

CTA(a_TERMCAP_ENTRYSIZE_MAX < U16_MAX,
   "Chosen buffer size exceeds datatype capability");

/* For simplicity we store commands and queries in single continuous control
 * and entry structure arrays: to index queries one has to add
 * mx__TERMCAP_CMD_MAX1 first!  And do not confound with ENTRYSIZE_MAX! */
enum{
   a_TERMCAP_ENT_MAX1 = mx__TERMCAP_CMD_MAX1 + mx__TERMCAP_QUERY_MAX1
};

enum a_termcap_flags{
   a_TERMCAP_F_NONE,
   /* enum mx_termcap_captype values stored here.
    * Note presence of a type in an a_termcap_ent signals initialization */
   a_TERMCAP_F_TYPE_MASK = (1u<<4) - 1,

   a_TERMCAP_F_QUERY = 1u<<4, /* A query rather than a command */
   a_TERMCAP_F_DISABLED = 1u<<5, /* User explicitly disabled command/query */
   a_TERMCAP_F_ALTERN = 1u<<6, /* Not available, but has alternative */
   a_TERMCAP_F_NOENT = 1u<<7, /* Not available */

   /* _cmd() argument interpretation (_T_STR) */
   a_TERMCAP_F_ARG_IDX1 = 1u<<11, /* Argument 1 used, and is an index */
   a_TERMCAP_F_ARG_IDX2 = 1u<<12,
   a_TERMCAP_F_ARG_CNT = 1u<<13, /* .., and is a count */

   a_TERMCAP_F__LAST = a_TERMCAP_F_ARG_CNT
};
CTA(S(u32,mx__TERMCAP_CAPTYPE_MAX1) <= S(u32,a_TERMCAP_F_TYPE_MASK),
   "enum mx_termcap_captype exceeds bit range of a_termcap_flags");

struct a_termcap_control{
   u16 tc_flags;
   /* Offset base into a_termcap_namedat[], which stores the two-letter
    * termcap(5) name directly followed by a NUL terminated terminfo(5) name.
    * A termcap(5) name may consist of two NULs meaning ERR_NOENT,
    * a terminfo(5) name may be empty for the same purpose */
   u16 tc_off;
};
CTA(a_TERMCAP_F__LAST <= U16_MAX,
   "a_termcap_flags exceed storage datatype in a_termcap_control");

struct a_termcap_ent{
   u16 te_flags;
   u16 te_off;    /* in a_termcap_g->tg_dat / value for T_BOOL and T_NUM */
};
CTA(a_TERMCAP_F__LAST <= U16_MAX,
   "a_termcap_flags exceed storage datatype in a_termcap_ent");

/* Structure for extended queries, which do not have an entry constant in
 * mx_termcap_query (to allow free query/binding of keycodes) */
struct a_termcap_ext_ent{
   struct a_termcap_ent tee_super;
   su_64( u8 tee__pad[4]; )
   struct a_termcap_ext_ent *tee_next;
   /* Resolvable termcap(5)/terminfo(5) name as given by user; the actual data
    * is stored just like for normal queries */
   char tee_name[VFIELD_SIZE(0)];
};

struct a_termcap_g{
   /* xxx Make these flags? */
   boole tg_disabled;
   boole tg_ca_mode;
   boole tg_ca_mode_clear_screen;
   boole tg_fullwidth; /* <> PSO_TERMCAP_FULLWIDTH */
   su_64( u8 tg__pad[4]; )
   struct a_termcap_ext_ent *tg_ext_ents; /* List of extended queries */
   struct a_termcap_ent tg_ents[a_TERMCAP_ENT_MAX1];
   struct n_string tg_dat; /* Storage for resolved caps */
# if !defined mx_HAVE_TGETENT_NULL_BUF && !defined mx_HAVE_TERMINFO
   char tg_lib_buf[a_TERMCAP_ENTRYSIZE_MAX];
# endif
};

/* Include the constant make-tcap-map.pl output */
#include "mx/gen-tcaps.h" /* $(MX_SRCDIR) */
CTA(sizeof a_termcap_namedat <= U16_MAX,
   "Termcap command and query name data exceed storage datatype");
CTA(a_TERMCAP_ENT_MAX1 == NELEM(a_termcap_control),
   "Control array does not match command/query array to be controlled");

static struct a_termcap_g *a_termcap_g;

/* Query *termcap*, parse it and incorporate into a_termcap_g */
static void a_termcap_init_var(struct str const *termvar);

/* Expand ^CNTRL, \[Ee] and \OCT.  False for parse error and empty results */
static boole a_termcap__strexp(struct n_string *store, char const *ibuf);

/* Initialize any _ent for which we have _F_ALTERN and which is not yet set */
static void a_termcap_init_altern(void);

#ifdef mx_HAVE_TERMCAP
/* Setup the library we use to work with term */
static boole a_termcap_load(char const *term);

/* Query the capability tcp and fill in tep (upon success) */
static boole a_termcap_ent_query(struct a_termcap_ent *tep,
      char const *cname, u16 cflags);
SINLINE boole a_termcap_ent_query_tcp(struct a_termcap_ent *tep,
      struct a_termcap_control const *tcp);

/* Output PTF for both, termcap(5) and terminfo(5) */
static int a_termcap_putc(int c);
#endif

/* Get mx_termcap_cmd or mx_termcap_query constant belonging to (nlen bytes of)
 * name, -1 if not found.  min and max have to be used to cramp the result */
static s32 a_termcap_enum_for_name(char const *name, uz nlen,
      s32 min, s32 max);
#define a_termcap_cmd_for_name(NB,NL) \
   a_termcap_enum_for_name(NB, NL, 0, mx__TERMCAP_CMD_MAX1)
#define a_termcap_query_for_name(NB,NL) \
   a_termcap_enum_for_name(NB, NL, mx__TERMCAP_CMD_MAX1, a_TERMCAP_ENT_MAX1)

static void
a_termcap_init_var(struct str const *termvar){
   char *cbp_base, *cbp;
   uz i;
   char const *ccp;
   NYD2_IN;

   if(termvar->l >= U16_MAX){
      n_err(_("*termcap*: length excesses internal limit, skipping\n"));
      goto j_leave;
   }

   ASSERT(termvar->s[termvar->l] == '\0');
   i = termvar->l +1;
   cbp_base = su_LOFI_ALLOC(i);
   su_mem_copy(cbp = cbp_base, termvar->s, i);

   for(; (ccp = su_cs_sep_c(&cbp, ',', TRU1)) != NIL;){
      struct a_termcap_ent *tep;
      uz kl;
      char const *v;
      u16 f;

      /* Separate key/value, if any */
      if(/* no empties ccp[0] == '\0' ||*/ ccp[1] == '\0'){
jeinvent:
         n_err(_("*termcap*: invalid entry: %s\n"), ccp);
         continue;
      }

      for(kl = 2, v = &ccp[2];; ++kl, ++v){
         char c;

         if((c = *v) == '\0'){
            f = mx_TERMCAP_CAPTYPE_BOOL;
            break;
         }else if(c == '#'){
            f = mx_TERMCAP_CAPTYPE_NUMERIC;
            ++v;
            break;
         }else if(c == '='){
            f = mx_TERMCAP_CAPTYPE_STRING;
            ++v;
            break;
         }
      }

      /* Do we know about this one? */
      /* C99 */{
         struct a_termcap_control const *tcp;
         s32 tci;

         tci = a_termcap_enum_for_name(ccp, kl, 0, a_TERMCAP_ENT_MAX1);
         if(tci < 0){
            /* For key binding purposes, save any given string */
#ifdef mx_HAVE_KEY_BINDINGS
            if((f & a_TERMCAP_F_TYPE_MASK) == mx_TERMCAP_CAPTYPE_STRING){
               struct a_termcap_ext_ent *teep;

               teep = su_ALLOC(VSTRUCT_SIZEOF(struct a_termcap_ext_ent,
                     tee_name) + kl +1);
               teep->tee_next = a_termcap_g->tg_ext_ents;
               a_termcap_g->tg_ext_ents = teep;
               su_mem_copy(teep->tee_name, ccp, kl);
               teep->tee_name[kl] = '\0';

               tep = &teep->tee_super;
               tep->te_flags = mx_TERMCAP_CAPTYPE_STRING | a_TERMCAP_F_QUERY;
               tep->te_off = S(u16,a_termcap_g->tg_dat.s_len);
               if(!a_termcap__strexp(&a_termcap_g->tg_dat, v))
                  tep->te_flags |= a_TERMCAP_F_DISABLED;
               goto jlearned;
            }else
#endif /* mx_HAVE_KEY_BINDINGS */
                  if(n_poption & n_PO_D_V)
               n_err(_("*termcap*: unknown capability: %s\n"), ccp);
            continue;
         }
         i = S(uz,tci);

         tcp = &a_termcap_control[i];
         if((tcp->tc_flags & a_TERMCAP_F_TYPE_MASK) != f){
            n_err(_("*termcap*: entry type mismatch: %s\n"), ccp);
            break;
         }
         tep = &a_termcap_g->tg_ents[i];
         tep->te_flags = tcp->tc_flags;
         tep->te_off = S(u16,a_termcap_g->tg_dat.s_len);
      }

      if((f & a_TERMCAP_F_TYPE_MASK) == mx_TERMCAP_CAPTYPE_BOOL)
         ;
      else if(*v == '\0')
         tep->te_flags |= a_TERMCAP_F_DISABLED;
      else if((f & a_TERMCAP_F_TYPE_MASK) == mx_TERMCAP_CAPTYPE_NUMERIC){
         if((su_idec_u16_cp(&tep->te_off, v, 0, NIL
                  ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
               ) != su_IDEC_STATE_CONSUMED)
            goto jeinvent;
      }else if(!a_termcap__strexp(&a_termcap_g->tg_dat, v))
         tep->te_flags |= a_TERMCAP_F_DISABLED;

#ifdef mx_HAVE_KEY_BINDINGS
jlearned:
#endif
      if(n_poption & n_PO_D_VV)
         n_err(_("*termcap*: learned %.*s: %s\n"), (int)kl, ccp,
            (tep->te_flags & a_TERMCAP_F_DISABLED ? "<disabled>"
             : (f & a_TERMCAP_F_TYPE_MASK) == mx_TERMCAP_CAPTYPE_BOOL ? "true"
               : v));
   }

   DBG( if(n_poption & n_PO_D_VV)
      n_err("*termcap* parsed: buffer used=%lu\n",
         S(ul,a_termcap_g->tg_dat.s_len)) );

   /* Catch some inter-dependencies the user may have triggered */
#ifdef mx_HAVE_TERMCAP
   if(a_termcap_g->tg_ents[mx_TERMCAP_CMD_te].te_flags & a_TERMCAP_F_DISABLED)
      a_termcap_g->tg_ents[mx_TERMCAP_CMD_ti].te_flags = a_TERMCAP_F_DISABLED;
   else if(a_termcap_g->tg_ents[mx_TERMCAP_CMD_ti].te_flags &
         a_TERMCAP_F_DISABLED)
      a_termcap_g->tg_ents[mx_TERMCAP_CMD_te].te_flags = a_TERMCAP_F_DISABLED;
#endif

   su_LOFI_FREE(cbp_base);

j_leave:
   NYD2_OU;
}

static boole
a_termcap__strexp(struct n_string *store, char const *ibuf){ /* XXX ASCII */
   char c;
   char const *oibuf;
   uz olen;
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

         if(su_cs_is_digit(c) && c <= '7'){
            char c2, c3;

            if((c2 = ibuf[2]) == '\0' || !su_cs_is_digit(c2) || c2 > '7' ||
                  (c3 = ibuf[3]) == '\0' || !su_cs_is_digit(c3) || c3 > '7'){
               n_err(_("*termcap*: invalid octal sequence: %s\n"), oibuf);
               goto jerr;
            }
            c -= '0', c2 -= '0', c3 -= '0';
            c <<= 3, c |= c2;
            if(S(u8,c) > 0x1F){
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
         c = su_cs_to_upper(c) ^ 0x40;
         if(S(u8,c) > 0x1F && c != 0x7F){ /* ASCII C0: 0..1F, 7F */
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
    * This allows users to explicitly disable some termcap(5) capability
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
   UNUSED(tep);

   /* For simplicity in the rest of this file null flags of disabled commands,
    * as we will not check and try to lazy query any command */
   /* C99 */{
      uz i;

      for(i = mx__TERMCAP_CMD_MAX1;;){
         if(i-- == 0)
            break;
         if((tep = &a_termcap_g->tg_ents[i])->te_flags & a_TERMCAP_F_DISABLED)
            tep->te_flags = 0;
      }
   }

#ifdef mx_HAVE_MLE
   /* ce == ch + [:SPACE:] (start column specified by argument) */
   tep = &a_termcap_g->tg_ents[mx_TERMCAP_CMD_ce];
   if(!a_OOK(tep))
      a_SET(tep, mx_TERMCAP_CMD_ce, TRU1);

   /* ch == cr[\r] + nd[:\033C:] */
   tep = &a_termcap_g->tg_ents[mx_TERMCAP_CMD_ch];
   if(!a_OOK(tep))
      a_SET(tep, mx_TERMCAP_CMD_ch, TRU1);

   /* cr == \r */
   tep = &a_termcap_g->tg_ents[mx_TERMCAP_CMD_cr];
   if(!a_OOK(tep)){
      a_SET(tep, mx_TERMCAP_CMD_cr, FAL0);
      tep->te_off = S(u16,a_termcap_g->tg_dat.s_len);
      n_string_push_c(n_string_push_c(&a_termcap_g->tg_dat, '\r'), '\0');
   }

   /* le == \b */
   tep = &a_termcap_g->tg_ents[mx_TERMCAP_CMD_le];
   if(!a_OOK(tep)){
      a_SET(tep, mx_TERMCAP_CMD_le, FAL0);
      tep->te_off = S(u16,a_termcap_g->tg_dat.s_len);
      n_string_push_c(n_string_push_c(&a_termcap_g->tg_dat, '\b'), '\0');
   }

   /* nd == \033[C (we may not fail, anyway, so use xterm sequence default) */
   tep = &a_termcap_g->tg_ents[mx_TERMCAP_CMD_nd];
   if(!a_OOK(tep)){
      a_SET(tep, mx_TERMCAP_CMD_nd, FAL0);
      tep->te_off = S(u16,a_termcap_g->tg_dat.s_len);
      n_string_push_buf(&a_termcap_g->tg_dat, "\033[C", sizeof("\033[C"));
   }

# ifdef mx_HAVE_TERMCAP
   /* cl == ho+cd */
   tep = &a_termcap_g->tg_ents[mx_TERMCAP_CMD_cl];
   if(!a_OOK(tep)){
      if(a_OK(mx_TERMCAP_CMD_cd) && a_OK(mx_TERMCAP_CMD_ho))
         a_SET(tep, mx_TERMCAP_CMD_cl, TRU1);
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
static boole
a_termcap_load(char const *term){
   boole rv;
   int err;
   NYD2_IN;

   if(!(rv = (setupterm(term, fileno(mx_tty_fp), &err) == OK)))
      n_err(_("Unknown ${TERM}inal, using only *termcap*: %s\n"), term);

   NYD2_OU;
   return rv;
}

static boole
a_termcap_ent_query(struct a_termcap_ent *tep, char const *cname, u16 cflags){
   boole rv;
   NYD2_IN;
   ASSERT(a_termcap_g != NIL && !a_termcap_g->tg_disabled);

   if(UNLIKELY(*cname == '\0'))
      rv = FAL0;
   else switch((tep->te_flags = cflags) & a_TERMCAP_F_TYPE_MASK){
   case mx_TERMCAP_CAPTYPE_BOOL:
      if(!(rv = (tigetflag(cname) > 0)))
         tep->te_flags |= a_TERMCAP_F_NOENT;
      tep->te_off = rv;
      break;
   case mx_TERMCAP_CAPTYPE_NUMERIC:{
      int r;

      r = tigetnum(cname);
      if((rv = (r >= 0)))
         tep->te_off = S(u16,MIN(U16_MAX, r));
      else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   default:
   case mx_TERMCAP_CAPTYPE_STRING:{
      char *cp;

      cp = tigetstr(cname);
      if((rv = (cp != NIL && cp != (char*)-1))){
         tep->te_off = S(u16,a_termcap_g->tg_dat.s_len);
         n_string_push_buf(&a_termcap_g->tg_dat, cp, su_cs_len(cp) +1);
      }else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   }

   NYD2_OU;
   return rv;
}

SINLINE boole
a_termcap_ent_query_tcp(struct a_termcap_ent *tep,
      struct a_termcap_control const *tcp){
   ASSERT(a_termcap_g != NIL && !a_termcap_g->tg_disabled);

   return a_termcap_ent_query(tep, &a_termcap_namedat[tcp->tc_off] + 2,
      tcp->tc_flags);
}

# else /* mx_HAVE_TERMINFO */
static boole
a_termcap_load(char const *term){
   boole rv;
   NYD2_IN;

   /* ncurses may return -1 */
# ifndef mx_HAVE_TGETENT_NULL_BUF
#  define a_BUF &a_termcap_g->tg_lib_buf[0]
# else
#  define a_BUF NIL
# endif

   if(!(rv = tgetent(a_BUF, term) > 0))
      n_err(_("Unknown ${TERM}inal, using only *termcap*: %s\n"), term);

# undef a_BUF

   NYD2_OU;
   return rv;
}

static boole
a_termcap_ent_query(struct a_termcap_ent *tep, char const *cname, u16 cflags){
   boole rv;
   NYD2_IN;
   ASSERT(a_termcap_g != NIL && !a_termcap_g->tg_disabled);

   if(UNLIKELY(*cname == '\0'))
      rv = FAL0;
   else switch((tep->te_flags = cflags) & a_TERMCAP_F_TYPE_MASK){
   case mx_TERMCAP_CAPTYPE_BOOL:
      if(!(rv = (tgetflag(cname) > 0)))
         tep->te_flags |= a_TERMCAP_F_NOENT;
      tep->te_off = rv;
      break;
   case mx_TERMCAP_CAPTYPE_NUMERIC:{
      int r;

      r = tgetnum(cname);
      if((rv = (r >= 0)))
         tep->te_off = S(u16,MIN(U16_MAX, r));
      else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   default:
   case mx_TERMCAP_CAPTYPE_STRING:{
# ifndef mx_HAVE_TGETENT_NULL_BUF
      char buf_base[a_TERMCAP_ENTRYSIZE_MAX], *buf = &buf_base[0];
#  define a_BUF &buf
# else
#  define a_BUF NIL
# endif
      char *cp;

      if((rv = ((cp = tgetstr(cname, a_BUF)) != NIL))){
         tep->te_off = S(u16,a_termcap_g->tg_dat.s_len);
         n_string_push_buf(&a_termcap_g->tg_dat, cp, su_cs_len(cp) +1);
# undef a_BUF
      }else
         tep->te_flags |= a_TERMCAP_F_NOENT;
      }break;
   }

   NYD2_OU;
   return rv;
}

SINLINE boole
a_termcap_ent_query_tcp(struct a_termcap_ent *tep,
      struct a_termcap_control const *tcp){
   ASSERT(a_termcap_g != NIL && !a_termcap_g->tg_disabled);

   return a_termcap_ent_query(tep, &a_termcap_namedat[tcp->tc_off],
      tcp->tc_flags);
}
# endif /* !mx_HAVE_TERMINFO */

static int
a_termcap_putc(int c){
   return putc(c, mx_tty_fp);
}
#endif /* mx_HAVE_TERMCAP */

static s32
a_termcap_enum_for_name(char const *name, uz nlen, s32 min, s32 max){
   struct a_termcap_control const *tcp;
   char const *cnam;
   s32 rv;
   NYD2_IN;

   /* Prefer terminfo(5) names */
   for(rv = max;;){
      if(rv-- == min){
         rv = -1;
         break;
      }

      tcp = &a_termcap_control[S(u32,rv)];
      cnam = &a_termcap_namedat[tcp->tc_off];

      if(cnam[2] != '\0'){
         char const *xcp;

         if(nlen == su_cs_len(xcp = &cnam[2]) && !su_mem_cmp(xcp, name, nlen))
            break;
      }

      if(nlen == 2 && cnam[0] == name[0] && cnam[1] == name[1])
         break;
   }

   NYD2_OU;
   return rv;
}

void
mx_termcap_init(void){
   struct mx_termcap_value tv;
   struct str termvar;
   char const *ccp;
   NYD_IN;
   ASSERT(n_psonce & n_PSO_TTYANY);

   a_termcap_g = su_TCALLOC(struct a_termcap_g, 1);
   a_termcap_g->tg_ext_ents = NIL;

   if((ccp = ok_vlook(termcap)) != NIL)
      termvar.l = su_cs_len(termvar.s = UNCONST(char*,ccp));
   else
      /*termvar.s = NIL,*/ termvar.l = 0;
   n_string_reserve(n_string_creat(&a_termcap_g->tg_dat),
      ((termvar.l + (256 - 64)) & ~127));

   if(termvar.l > 0)
      a_termcap_init_var(&termvar);

   if(ok_blook(termcap_disable))
      a_termcap_g->tg_disabled = TRU1;
#ifdef mx_HAVE_TERMCAP
   else if((ccp = ok_vlook(TERM)) == NIL){
      n_err(_("Environment variable $TERM not set, using only *termcap*\n"));
      a_termcap_g->tg_disabled = TRU1;
   }else if(!a_termcap_load(ccp))
      a_termcap_g->tg_disabled = TRU1;
   else{
      /* Query termcap(5) for each command slot that is not yet set */
      struct a_termcap_ent *tep;
      uz i;

      for(i = mx__TERMCAP_CMD_MAX1;;){
         if(i-- == 0)
            break;
         if((tep = &a_termcap_g->tg_ents[i])->te_flags == 0)
            a_termcap_ent_query_tcp(tep, &a_termcap_control[i]);
      }
   }
#endif /* mx_HAVE_TERMCAP */

   a_termcap_init_altern();

#ifdef mx_HAVE_TERMCAP
   if(a_termcap_g->tg_ents[mx_TERMCAP_CMD_te].te_flags != 0){
      char const *cam;

      if((cam = ok_vlook(termcap_ca_mode)) != NIL){
         a_termcap_g->tg_ca_mode = TRU1;
         if(*cam != '\0')
            a_termcap_g->tg_ca_mode_clear_screen = TRU1;
      }
   }
#endif

   /* TODO We do not handle !mx_TERMCAP_QUERY_sam in this software! */
   if(
#ifdef mx_HAVE_TERMCAP
      !mx_termcap_query(mx_TERMCAP_QUERY_am, &tv) ||
#endif
         mx_termcap_query(mx_TERMCAP_QUERY_xenl, &tv)){
      a_termcap_g->tg_fullwidth = TRU1;
      n_psonce |= n_PSO_TERMCAP_FULLWIDTH;

      /* Since termcap was not initialized when we did TERMIOS_SETUP_TERMSIZE
       * we need/should adjust the found setting to reality (without causing
       * a synthesized SIGWINCH or something even more expensive that is) */
      if(mx_termios_dimen.tiosd_width > 0)
         ++mx_termios_dimen.tiosd_width;
   }

   mx_TERMCAP_RESUME(TRU1);

   NYD_OU;
}

void
mx_termcap_destroy(void){
   NYD_IN;
   ASSERT(a_termcap_g != NIL);

   mx_TERMCAP_SUSPEND(TRU1);

#ifdef mx_HAVE_DEBUG
   /* C99 */{
      struct a_termcap_ext_ent *tmp;

      while((tmp = a_termcap_g->tg_ext_ents) != NIL){
         a_termcap_g->tg_ext_ents = tmp->tee_next;
         su_FREE(tmp);
      }
   }
   n_string_gut(&a_termcap_g->tg_dat);

   su_FREE(a_termcap_g);
   a_termcap_g = NIL;
#endif

   NYD_OU;
}

#ifdef mx_HAVE_TERMCAP
void
mx_termcap_resume(boole complete){
   NYD_IN;

   if(a_termcap_g != NIL && !a_termcap_g->tg_disabled){
      if(complete && a_termcap_g->tg_ca_mode)
         mx_termcap_cmdx(mx_TERMCAP_CMD_ti);

      mx_termcap_cmdx(mx_TERMCAP_CMD_ks);
      fflush(mx_tty_fp);
   }

   NYD_OU;
}

void
mx_termcap_suspend(boole complete){
   NYD_IN;

   if(a_termcap_g != NIL && !a_termcap_g->tg_disabled){
      if(complete && a_termcap_g->tg_ca_mode){
         if(a_termcap_g->tg_ca_mode_clear_screen)
            mx_termcap_cmdx(mx_TERMCAP_CMD_cl);
         mx_termcap_cmdx(mx_TERMCAP_CMD_te);
      }

      mx_termcap_cmdx(mx_TERMCAP_CMD_ke);
      fflush(mx_tty_fp);
   }

   NYD_OU;
}
#endif /* mx_HAVE_TERMCAP */

sz
mx_termcap_cmd(enum mx_termcap_cmd cmd, sz a1, sz a2){
   /* Commands are not lazy queried */
   struct a_termcap_ent const *tep;
   enum a_termcap_flags flags;
   sz rv;
   NYD2_IN;
   UNUSED(a1);
   UNUSED(a2);

   rv = FAL0;
   if(a_termcap_g == NIL)
      goto jleave;

   flags = cmd & ~mx__TERMCAP_CMD_MASK;
   cmd &= mx__TERMCAP_CMD_MASK;
   tep = a_termcap_g->tg_ents;

   if((flags & mx_TERMCAP_CMD_FLAG_CA_MODE) && !a_termcap_g->tg_ca_mode)
      rv = TRU1;
   else if((tep += cmd)->te_flags == 0 || (tep->te_flags & a_TERMCAP_F_NOENT))
      rv = TRUM1;
   else if(!(tep->te_flags & a_TERMCAP_F_ALTERN)){
      char const *cp;

      ASSERT((tep->te_flags & a_TERMCAP_F_TYPE_MASK) ==
         mx_TERMCAP_CAPTYPE_STRING);

      cp = &a_termcap_g->tg_dat.s_dat[tep->te_off];

#ifdef mx_HAVE_TERMCAP
      if(tep->te_flags & (a_TERMCAP_F_ARG_IDX1 | a_TERMCAP_F_ARG_IDX2)){
         if(a_termcap_g->tg_disabled){
            if(n_poption & n_PO_D_V){
               char const *cnam;

               cnam = &a_termcap_namedat[a_termcap_control[cmd].tc_off];
               if(cnam[2] != '\0')
                  cnam += 2;
               n_err(_("*termcap-disable*d (/$TERM not set/unknown): "
                  "cannot perform CAP: %s\n"), cnam);
            }
            goto jleave;
         }

         /* Follow Thomas Dickey's advise on pre-va_arg prototypes, add 0s */
# ifdef mx_HAVE_TERMINFO
         if((cp = tparm(cp, a1, a2, 0,0,0,0,0,0,0)) == NIL)
            goto jleave;
# else
         /* curs_termcap.3:
          * The \fBtgoto\fP function swaps the order of parameters.
          * It does this also for calls requiring only a single parameter.
          * In that case, the first parameter is merely a placeholder. */
         if(!(tep->te_flags & a_TERMCAP_F_ARG_IDX2)){
            a2 = a1;
            a1 = S(u32,-1);
         }
         if((cp = tgoto(cp, S(int,a1), S(int,a2))) == NIL)
            goto jleave;
# endif
      }
#endif /* mx_HAVE_TERMCAP */

      for(;;){
#ifdef mx_HAVE_TERMCAP
         if(!a_termcap_g->tg_disabled){
            if(tputs(cp, 1, &a_termcap_putc) != OK)
               break;
         }else
#endif
               if(fputs(cp, mx_tty_fp) == EOF)
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
      case mx_TERMCAP_CMD_ce: /* ce == ch + [:SPACE:] */
         if(a1 > 0)
            --a1;
         if((rv = mx_termcap_cmd(mx_TERMCAP_CMD_ch, a1, 0)) > 0){
            for(a2 = mx_termios_dimen.tiosd_width - a1; a2 > 0; --a2)
               if(putc(' ', mx_tty_fp) == EOF){
                  rv = FAL0;
                  break;
               }
            if(rv && mx_termcap_cmd(mx_TERMCAP_CMD_ch, a1, -1) != TRU1)
               rv = FAL0;
         }
         break;
      case mx_TERMCAP_CMD_ch: /* ch == cr + nd */
         rv = mx_termcap_cmdx(mx_TERMCAP_CMD_cr);
         if(rv > 0 && a1 > 0){
            rv = mx_termcap_cmd(mx_TERMCAP_CMD_nd, a1, -1);
         }
         break;
# ifdef mx_HAVE_TERMCAP
      case mx_TERMCAP_CMD_cl: /* cl = ho + cd */
         rv = mx_termcap_cmdx(mx_TERMCAP_CMD_ho);
         if(rv > 0)
            rv = mx_termcap_cmdx(mx_TERMCAP_CMD_cd | flags);
         break;
# endif
#endif /* mx_HAVE_MLE */
      }

jflush:
      if(flags & mx_TERMCAP_CMD_FLAG_FLUSH)
         fflush(mx_tty_fp);
      if(ferror(mx_tty_fp))
         rv = FAL0;
   }

jleave:
   NYD2_OU;
   return rv;
}

boole
mx_termcap_query(enum mx_termcap_query query, struct mx_termcap_value *tvp){
   /* Queries are lazy queried upon request */
   /* XXX mx_termcap_query(): boole handling suboptimal, tvp used on success */
   struct a_termcap_ent const *tep;
   boole rv;
   NYD2_IN;
   ASSERT(tvp != NIL);

   rv = FAL0;
   if(a_termcap_g == NIL)
      goto jleave;

   /* Is it a built-in query? */
   if(query != mx__TERMCAP_QUERY_MAX1){
      tep = &a_termcap_g->tg_ents[mx__TERMCAP_CMD_MAX1 + query];

      if(tep->te_flags == 0
#ifdef mx_HAVE_TERMCAP
            && (a_termcap_g->tg_disabled ||
               !a_termcap_ent_query_tcp(UNCONST(struct a_termcap_ent*,tep),
                  &a_termcap_control[mx__TERMCAP_CMD_MAX1 + query]))
#endif
      )
         goto jleave;
   }else{
#ifdef mx_HAVE_TERMCAP
      uz nlen;
#endif
      struct a_termcap_ext_ent *teep;
      char const *ndat;

      ndat = tvp->tv_data.tvd_string;

      for(teep = a_termcap_g->tg_ext_ents; teep != NIL; teep = teep->tee_next)
         if(!su_cs_cmp(teep->tee_name, ndat)){
            tep = &teep->tee_super;
            goto jextok;
         }

#ifdef mx_HAVE_TERMCAP
      if(a_termcap_g->tg_disabled)
#endif
         goto jleave;

#ifdef mx_HAVE_TERMCAP
      nlen = su_cs_len(ndat) +1;
      teep = su_ALLOC(VSTRUCT_SIZEOF(struct a_termcap_ext_ent,tee_name) +
            nlen);
      tep = &teep->tee_super;
      teep->tee_next = a_termcap_g->tg_ext_ents;
      a_termcap_g->tg_ext_ents = teep;
      su_mem_copy(teep->tee_name, ndat, nlen);

      if(!a_termcap_ent_query(UNCONST(struct a_termcap_ent*,tep), ndat,
               mx_TERMCAP_CAPTYPE_STRING | a_TERMCAP_F_QUERY))
         goto jleave;
#endif
jextok:;
   }

   if(tep->te_flags & a_TERMCAP_F_NOENT)
      goto jleave;

   rv = (tep->te_flags & a_TERMCAP_F_ALTERN) ? TRUM1 : TRU1;

   switch((tvp->tv_captype = tep->te_flags & a_TERMCAP_F_TYPE_MASK)){
   case mx_TERMCAP_CAPTYPE_BOOL:
      tvp->tv_data.tvd_bool = S(boole,tep->te_off);
      break;
   case mx_TERMCAP_CAPTYPE_NUMERIC:
      tvp->tv_data.tvd_numeric = S(u32,tep->te_off);
      break;
   default:
   case mx_TERMCAP_CAPTYPE_STRING:
      tvp->tv_data.tvd_string = &a_termcap_g->tg_dat.s_dat[tep->te_off];
      break;
   }

jleave:
   NYD2_OU;
   return rv;
}

#ifdef mx_HAVE_KEY_BINDINGS
s32
mx_termcap_query_for_name(char const *name, enum mx_termcap_captype type){
   s32 rv;
   NYD2_IN;

   if((rv = a_termcap_query_for_name(name, su_cs_len(name))) >= 0){
      struct a_termcap_control const *tcp;

      tcp = &a_termcap_control[(u32)rv];

      if(type != mx_TERMCAP_CAPTYPE_NONE &&
            (tcp->tc_flags & a_TERMCAP_F_TYPE_MASK) != type)
         rv = -2;
      else
         rv -= mx__TERMCAP_CMD_MAX1;
   }

   NYD2_OU;
   return rv;
}

char const *
mx_termcap_name_of_query(enum mx_termcap_query query){
   char const *rv;
   NYD2_IN;

   rv = &a_termcap_namedat[
         a_termcap_control[mx__TERMCAP_CMD_MAX1 + query].tc_off + 2];

   NYD2_OU;
   return rv;
}
#endif /* mx_HAVE_KEY_BINDINGS */

#include "su/code-ou.h"
#endif /* mx_HAVE_TCAP */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_TERMCAP
/* s-it-mode */
