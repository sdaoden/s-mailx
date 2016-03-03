/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Terminal capability interaction.
 *@ For encapsulation purposes provide a basic foundation even without
 *@ HAVE_TERMCAP, but with nail.h:n_HAVE_TCAP.
 *
 * Copyright (c) 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#define n_FILE termcap

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE()
#ifdef n_HAVE_TCAP
/* If available, curses.h must be included before term.h! */
#ifdef HAVE_TERMCAP
# ifdef HAVE_TERMCAP_CURSES
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
 * TODO After I/O layer rewrite, "output to STDIN_FILENO".
 */

/* For newer ncurses based termcap emulation the ENTRYSIZE_MAX buffer will
 * remain unused, for elder non-emulated ones really weird things will happen
 * if an entry would require more than 1024 bytes, so don't mind.
 * Things are more unserious for CMDBUF, but our needs should be satisfied with
 * far less than the original limit.  So use a ui16_t for storage.
 * (Note however that *termcap* is rejected if it doesn't fit in CMDBUF.) */
#define a_TERMCAP_ENTRYMAX ((2668 + 511) & ~511) /* As of ncurses 6.0 */
#define a_TERMCAP_CMDBUF 2048

n_CTA(a_TERMCAP_CMDBUF < UI16_MAX,
   "Chosen buffer size exceeds datatype capability");
/* The real reason however is that we (re-)use our entry buffer as a temporary,
 * writable storage for *termcap* while parsing that one, i.e., vice versa */
n_CTA(a_TERMCAP_CMDBUF <= a_TERMCAP_ENTRYMAX,
   "Command buffer smaller than maximum entry size");

enum a_termcap_cmd_flags{
   a_TERMCAP_CF_NONE,
   a_TERMCAP_CF_T_BOOL = 0<<1,   /* Boolean cap. type */
   a_TERMCAP_CF_T_NUM = 1<<1,    /* ..numeric */
   a_TERMCAP_CF_T_STR = 2<<1,    /* ..string */
   a_TERMCAP_CF_T_MASK = 3<<1,

   a_TERMCAP_CF_NOTSUPP = 1<<4,  /* _cmd() may fail with TRUM1 */
   a_TERMCAP_CF_DISABLED = 1<<5, /* User explicitly disabled command */
   a_TERMCAP_CF_ALTERN = 1<<6,   /* Not available, but has alternative */

   /* _cmd() argument interpretion (_T_STR) */
   a_TERMCAP_CF_A_IDX1 = 1<<11,  /* Argument 1 used, and is an index */
   a_TERMCAP_CF_A_IDX2 = 1<<12,
   a_TERMCAP_CF_A_CNT = 1<<13    /* .., and is a count */
};

struct a_termcap_control{
   char tc_cap[2];   /* Not \0 terminated! */
   ui16_t tc_flags;
};

struct a_termcap_ent{
   ui16_t te_flags;
   ui16_t te_off;    /* In a_termcap_g.tg_buf, or value for T_BOOL and T_NUM */
};

struct a_termcap_g{
   struct a_termcap_ent tg_ents[n__TERMCAP_CMD_MAX];
   char tg_buf[VFIELD_SIZE(0)];  /* Data storage for entry strings */
};

/* Update the *termcap* member documentation on changes! */
static struct a_termcap_control const a_termcap_control[] = {
#ifdef HAVE_COLOUR
   {"Co", a_TERMCAP_CF_T_NUM},
#endif

#ifdef HAVE_TERMCAP
   {"te", a_TERMCAP_CF_T_STR},
   {"ti", a_TERMCAP_CF_T_STR},

   {"cd", a_TERMCAP_CF_T_STR},
   {"cl", a_TERMCAP_CF_T_STR},
   /*{"cm", a_TERMCAP_CF_T_STR | a_TERMCAP_CF_A_IDX1 | a_TERMCAP_CF_A_IDX2},*/
   {"ho", a_TERMCAP_CF_T_STR},
#endif
};

n_CTA(n__TERMCAP_CMD_MAX == NELEM(a_termcap_control),
   "Control array doesn't match command array to be controlled");

static struct a_termcap_g *a_termcap_g;

#ifdef HAVE_TERMCAP
static int a_termcap_putc(int c);
#endif

#ifdef HAVE_TERMCAP
static int
a_termcap_putc(int c){
   return putchar(c);
}
#endif

FL void
n_termcap_init(void){ /* XXX logical split */
   char buf[a_TERMCAP_ENTRYMAX], cmdbuf[a_TERMCAP_CMDBUF], *cbp, *cp, *bp;
   struct a_termcap_ent ents[n__TERMCAP_CMD_MAX], *tep;
   size_t i;
   char const *ccp;
   NYD_ENTER;

   assert((options & (OPT_INTERACTIVE | OPT_QUICKRUN_MASK)) == OPT_INTERACTIVE);

   memset(ents, 0, sizeof ents);
   cbp = cmdbuf;

   /* First incorporate user settings */
   if((ccp = ok_vlook(termcap)) == NULL)
      goto jtermcap;
   if((i = strlen(ccp) +1) > a_TERMCAP_CMDBUF){
      n_err(_("*termcap*: length excesses internal limit, skipping"));
      goto jtermcap;
   }
   memcpy(bp = buf, ccp, i);

   for(; (ccp = n_strsep(&bp, ',', TRU1)) != NULL;){
      ui16_t f;
      char const *v;

      /* Separate key/value, if any */
      if(/* no empties ccp[0] == '\0' ||*/ ccp[1] == '\0'){
jeinvent:
         n_err(_("*termcap*: invalid entry: \"%s\"\n"), ccp);
         continue;
      }else if(ccp[2] == '\0'){
         f = a_TERMCAP_CF_T_BOOL;
         UNINIT(v, NULL);
      }else{
         if(ccp[2] == '#')
            f = a_TERMCAP_CF_T_NUM;
         else if(ccp[2] == '=')
            f = a_TERMCAP_CF_T_STR;
         else
            goto jeinvent;
         v = ccp + 3;
      }

      /* Do we know about this one? */
      for(i = n__TERMCAP_CMD_MAX;;){
         struct a_termcap_control const *tcp;

         if(i-- == 0){
            if(options & OPT_D_V)
               n_err(_("*termcap*: unknown capability: \"%c%c\"\n"),
                  ccp[0], ccp[1]);
            break;
         }
         if((tcp = &a_termcap_control[i])->tc_cap[0] != ccp[0] ||
               tcp->tc_cap[1] != ccp[1])
            continue;

         /* That's the right one, take it over */
         if((tcp->tc_flags & a_TERMCAP_CF_T_MASK) != f){
            n_err(_("*termcap*: entry type mismatch: \"%s\"\n"), ccp);
            break;
         }
         tep = &ents[i];
         tep->te_flags = tcp->tc_flags;
         tep->te_off = (ui16_t)PTR2SIZE(cbp - cmdbuf);

         if((f & a_TERMCAP_CF_T_MASK) == a_TERMCAP_CF_T_BOOL)
            break;
         if(*v == '\0')
            tep->te_flags |= a_TERMCAP_CF_DISABLED;
         else if((f & a_TERMCAP_CF_T_MASK) == a_TERMCAP_CF_T_NUM){
            char *eptr;
            long l = strtol(v, &eptr, 10);

            if(*eptr != '\0' || l < 0 || UICMP(32, l, >=, UI16_MAX))
               goto jeinvent;
            tep->te_off = (ui16_t)l;
            break;
         }else for(;;){
            int c = n_shell_expand_escape(&v, FAL0);

            *cbp++ = (char)c;
            if(c == '\0')
               break;
         }
         break;
      }
   }

   /* Catch some inter-dependencies the user may have triggered */
#ifdef HAVE_TERMCAP
   if(ents[n_TERMCAP_CMD_te].te_flags & a_TERMCAP_CF_DISABLED)
      ents[n_TERMCAP_CMD_ti].te_flags = a_TERMCAP_CF_DISABLED;
   else if(ents[n_TERMCAP_CMD_ti].te_flags & a_TERMCAP_CF_DISABLED)
      ents[n_TERMCAP_CMD_te].te_flags = a_TERMCAP_CF_DISABLED;
#endif

   /* After the user is worked, deal with termcap(5) */
jtermcap:
#ifdef HAVE_TERMCAP
   if(ok_blook(termcap_disable)){
      pstate |= PS_TERMCAP_DISABLE;
      goto jdumb;
   }

   if((ccp = env_vlook("TERM", FAL0)) == NULL){
      n_err(_("Environment variable $TERM is not set, using only *termcap*\n"));
      pstate |= PS_TERMCAP_DISABLE;
      goto jdumb;
   }
   /* ncurses may return -1 */
   if(tgetent(bp = buf, ccp) <= 0){
      n_err(_("Unknown ${TERM}inal \"%s\", using only *termcap*\n"), ccp);
      pstate |= PS_TERMCAP_DISABLE;
      goto jdumb;
   }

   /* Query termcap(5) for each command slot that is not yet set */
   for(i = n__TERMCAP_CMD_MAX;;){
      struct a_termcap_control const *tcp;

      if(i-- == 0)
         break;
      if((tep = &ents[i])->te_flags != 0)
         continue;
      tcp = &a_termcap_control[i];

      switch(tcp->tc_flags & a_TERMCAP_CF_T_MASK){
      case a_TERMCAP_CF_T_BOOL:
         tep->te_flags = tcp->tc_flags;
         tep->te_off = (tgetflag(tcp->tc_cap) > 0);
         break;
      case a_TERMCAP_CF_T_NUM:{
         int r = tgetnum(tcp->tc_cap);

         if(r >= 0){
            tep->te_flags = tcp->tc_flags;
            tep->te_off = (ui16_t)MIN(UI16_MAX, r);
         }
      }  break;
      case a_TERMCAP_CF_T_STR:
      default:
         cp = cbp;
         if(tgetstr(tcp->tc_cap, &cbp) != NULL){
            tep->te_flags = tcp->tc_flags;
            tep->te_off = (ui16_t)PTR2SIZE(cp - cmdbuf);
            assert(PTR2SIZE(cbp - cmdbuf) < a_TERMCAP_CMDBUF);
         }
         break;
      }
   }
#endif /* HAVE_TERMCAP */

jdumb:
   /* After user and termcap(5) queries have been performed, define fallback
    * strategies for commands which are not yet set.  But first of all null
    * user disabled entries again, that no longer matters */
   for(i = n__TERMCAP_CMD_MAX;;){
      if(i-- == 0)
         break;
      if((tep = &ents[i])->te_flags & a_TERMCAP_CF_DISABLED)
         tep->te_flags = 0;
   }

   /* xxx Use table-based approach for fallback strategies */
#ifdef HAVE_TERMCAP
   /* cl == ho+cd */
   if((tep = &ents[n_TERMCAP_CMD_cl])->te_flags == 0 &&
         ents[n_TERMCAP_CMD_cd].te_flags != 0 &&
         ents[n_TERMCAP_CMD_ho].te_flags != 0)
      tep->te_flags = a_TERMCAP_CF_ALTERN;
#endif

   /* Finally create our global structure */
   assert(PTR2SIZE(cbp - cmdbuf) < a_TERMCAP_CMDBUF);
   a_termcap_g = smalloc(sizeof(struct a_termcap_g) -
         VFIELD_SIZEOF(struct a_termcap_g, tg_buf) + PTR2SIZE(cbp - cmdbuf));
   memcpy(a_termcap_g->tg_ents, ents, sizeof ents);
   memcpy(a_termcap_g->tg_buf, cmdbuf, PTR2SIZE(cbp - cmdbuf));

#ifdef HAVE_TERMCAP
   if(a_termcap_g->tg_ents[n_TERMCAP_CMD_te].te_flags != 0)
      pstate |= PS_TERMCAP_CA_MODE;
#endif
   n_TERMCAP_RESUME(TRU1);
   NYD_LEAVE;
}

FL void
n_termcap_destroy(void){
   NYD_ENTER;
   assert((options & (OPT_INTERACTIVE | OPT_QUICKRUN_MASK)) == OPT_INTERACTIVE);

   n_TERMCAP_SUSPEND(TRU1);

#ifdef HAVE_DEBUG
   free(a_termcap_g);
   a_termcap_g = NULL;
#endif
   NYD_LEAVE;
}

#ifdef HAVE_TERMCAP
FL void
n_termcap_resume(bool_t complete){
   NYD_ENTER;
   if(!(pstate & PS_TERMCAP_DISABLE) &&
         (options & (OPT_INTERACTIVE | OPT_QUICKRUN_MASK)) == OPT_INTERACTIVE){
      if(complete && (pstate & PS_TERMCAP_CA_MODE))
         n_termcap_cmdx(n_TERMCAP_CMD_ti);
      n_termcap_cmdx(n_TERMCAP_CMD_ks);
      fflush(stdout);
   }
   NYD_LEAVE;
}

FL void
n_termcap_suspend(bool_t complete){
   NYD_ENTER;
   if(!(pstate & PS_TERMCAP_DISABLE) &&
         (options & (OPT_INTERACTIVE | OPT_QUICKRUN_MASK)) == OPT_INTERACTIVE){
      if(complete && (pstate & PS_TERMCAP_CA_MODE))
         n_termcap_cmdx(n_TERMCAP_CMD_ke);
      n_termcap_cmdx(n_TERMCAP_CMD_te);
      fflush(stdout);
   }
   NYD_LEAVE;
}
#endif /* HAVE_TERMCAP */

FL ssize_t
n_termcap_cmd(enum n_termcap_cmd cmd, ssize_t a1, ssize_t a2){
   struct a_termcap_ent const *tep;
   enum n_termcap_cmd cmd_flags;
   ssize_t rv;
   NYD2_ENTER;
   UNUSED(a1);
   UNUSED(a2);

   rv = FAL0;
   if((options & (OPT_INTERACTIVE | OPT_QUICKRUN_MASK)) != OPT_INTERACTIVE)
      goto jleave;
   assert(a_termcap_g != NULL);

   cmd_flags = cmd & ~n__TERMCAP_CMD_MASK;
   cmd &= n__TERMCAP_CMD_MASK;
   tep = a_termcap_g->tg_ents;

   if((cmd_flags & n_TERMCAP_CMD_FLAG_CA_MODE) &&
         !(pstate & PS_TERMCAP_CA_MODE))
      rv = TRU1;
   else if((tep += cmd)->te_flags == 0)
      rv = TRUM1;
   else if(!(tep->te_flags & a_TERMCAP_CF_ALTERN)){
      switch(tep->te_flags & a_TERMCAP_CF_T_MASK){
      case a_TERMCAP_CF_T_BOOL:
         rv = (bool_t)tep->te_off;
         break;
      case a_TERMCAP_CF_T_NUM:
         rv = (ssize_t)tep->te_off;
         break;
      case a_TERMCAP_CF_T_STR:
      default:{
         char const *cp = a_termcap_g->tg_buf + tep->te_off;

#ifdef HAVE_TERMCAP
         if(tep->te_flags & (a_TERMCAP_CF_A_IDX1 | a_TERMCAP_CF_A_IDX2)){
            if(pstate & PS_TERMCAP_DISABLE){
               if(options & OPT_D_V)
                  n_err(_("*termcap-disable*d (or: $TERM not set / unknown): "
                     "cannot perform CAP \"%.2s\"\n"),
                  a_termcap_control[cmd].tc_cap);
               goto jleave;
            }

            /* curs_termcap.3:
             * The \fBtgoto\fP function swaps the order of parameters.
             * It does this also for calls requiring only a single parameter.
             * In that case, the first parameter is merely a placeholder. */
            if(!(tep->te_flags & a_TERMCAP_CF_A_IDX2)){
               a2 = a1;
               a1 = (ui32_t)-1;
            }
            if((cp = tgoto(cp, (int)a1, (int)a2)) == NULL)
               break;
         }
#endif
         for(;;){
#ifdef HAVE_TERMCAP
            if(!(pstate & PS_TERMCAP_DISABLE)){
               if(tputs(cp, 1, &a_termcap_putc) != OK)
                  break;
            }else
#endif
                  if(fputs(cp, stdout) == EOF)
               break;
            if(!(tep->te_flags & a_TERMCAP_CF_A_CNT) || --a1 <= 0){
               rv = TRU1;
               break;
            }
         }
         goto jflush;
      }  break;
      }
   }else{
      /* xxx Use table-based approach for fallback strategies */
#ifdef HAVE_TERMCAP
      /* cl = ho + cd */
      if(cmd == n_TERMCAP_CMD_cl){
         rv = n_termcap_cmdx(n_TERMCAP_CMD_ho);
         if(rv > 0)
            rv = n_termcap_cmdx(n_TERMCAP_CMD_cd | cmd_flags);
         goto jflush;
      }
#endif

jflush:
      if(cmd_flags & n_TERMCAP_CMD_FLAG_FLUSH)
         fflush(stdout);
      if(ferror(stdout))
         rv = FAL0;
   }

jleave:
   NYD2_LEAVE;
   return rv;
}
#endif /* n_HAVE_TCAP */

/* s-it-mode */
