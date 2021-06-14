/*@ Implementation of code.h: (unavoidable) basics.
 *@ TODO Log: domain should be configurable
 *@ TODO Assert: the C++ lib has per-thread assertion states, s_nolog to
 *@ TODO    suppress log, test_state(), test_and_clear_state(): for unit tests!
 *@ TODO su_program: if set, the PID should be logged, too!
 *
 * Copyright (c) 2019 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_core_code
#define su_SOURCE
#define su_SOURCE_CORE_CODE
#define su_MASTER

#include "su/code.h"

#include <errno.h> /* XXX Grrrr */
#include <stdarg.h>
#include <stdio.h> /* TODO Get rid */
#include <stdlib.h>
#include <unistd.h> /* TODO POSIX module! */

#include "su/icodec.h"

/*#include "su/code.h"*/
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Normally in config.h */
#if (defined su_HAVE_DEBUG || defined su_HAVE_DEVEL) && !defined su_NYD_ENTRIES
# define su_NYD_ENTRIES 1000
#endif

#if defined su_HAVE_DEBUG || defined su_HAVE_DEVEL
struct a_core_nyd_info{
   char const *cni_file;
   char const *cni_fun;
   u32 cni_chirp_line;
   u32 cni_level;
};
#endif

static char const a_core_lvlnames[][8] = {
   FIELD_INITI(su_LOG_EMERG) "emerg",
   FIELD_INITI(su_LOG_ALERT) "alert",
   FIELD_INITI(su_LOG_CRIT) "crit",
   FIELD_INITI(su_LOG_ERR) "error",
   FIELD_INITI(su_LOG_WARN) "warning",
   FIELD_INITI(su_LOG_NOTICE) "notice",
   FIELD_INITI(su_LOG_INFO) "info",
   FIELD_INITI(su_LOG_DEBUG) "debug"
};
/* You can deduce the value from the offset */
CTAV(su_LOG_EMERG == 0);
CTAV(su_LOG_DEBUG == 7);

union su__bom_union const su__bom_little = {{'\xFF', '\xFE'}};
union su__bom_union const su__bom_big = {{'\xFE', '\xFF'}};

#if DVLOR(1, 0)
CTAV(su_NYD_ACTION_ENTER == 0);
CTAV(su_NYD_ACTION_LEAVE == 1);
CTAV(su_NYD_ACTION_ANYWHERE == 2);
static char const a_core_nyd_desc[3] = "><=";

MT( static uz a_core_glock_recno[su__GLOCK_MAX +1]; )

static u32 a_core_nyd_curr, a_core_nyd_level;
static boole a_core_nyd_skip;
static struct a_core_nyd_info a_core_nyd_infos[su_NYD_ENTRIES];
#endif

uz su__state;

char const su_empty[] = "";
char const su_reproducible_build[] = "reproducible_build";
u16 const su_bom = su_BOM;

char const *su_program;

/* TODO Eventually all the I/O is SU based, then this will vanish!
 * TODO We need some _USECASE_ hook to store readily prepared lines then.
 * TODO Also, our log does not yet prepend "su_progam: " to each output line,
 * TODO because of all that (port FmtEncCtx, use rounds!!) */
su_SINLINE void a_evlog(BITENUM_IS(u32,su_log_level) lvl, char const *fmt,
      va_list ap);

/* */
#if DVLOR(1, 0)
static void a_core_nyd_printone(void (*ptf)(up cookie, char const *buf,
      uz blen), up cookie, struct a_core_nyd_info const *cnip);
#endif

su_SINLINE void
a_evlog(BITENUM_IS(u32,su_log_level) lvl, char const *fmt, va_list ap){
#ifdef su_USECASE_MX
# ifndef mx_HAVE_AMALGAMATION
   /*extern*/ void n_err(char const *, ...);
   /*extern*/ void n_verr(char const *, va_list);
# endif
#endif
   char buf[su_IENC_BUFFER_SIZE];
   char const *cp, *xfmt;
   u32 f;

   f = lvl & ~su__LOG_MASK;
   lvl &= su__LOG_MASK;

   if(!(f & su_LOG_F_CORE)){
#ifdef su_USECASE_MX
      if(lvl != su_LOG_EMERG)
         goto jnostd;
#endif
   }

   /* TODO ensure each line has the prefix; use FormatEncodeCtx */
   if(su_program != NIL){
      if(su_state_has(su_STATE_LOG_SHOW_PID)){
         cp = su_ienc_u32(buf, getpid(), 10);
         xfmt = "%s[%s]: ";
      }else{
         cp = su_empty;
         xfmt = "%s: ";
      }
      fprintf(stderr, xfmt, su_program, cp);
   }

   if(su_state_has(su_STATE_LOG_SHOW_LEVEL))
      fprintf(stderr, "[%s] ",
         a_core_lvlnames[lvl]);

   vfprintf(stderr, fmt, ap);

#ifdef su_USECASE_MX
   goto jnomx;
jnostd:
   n_verr(fmt, ap);
jnomx:
#endif

   if(lvl == su_LOG_EMERG)
      abort(); /* TODO configurable */
}

#if DVLOR(1, 0)
static void
a_core_nyd_printone(void (*ptf)(up cookie, char const *buf, uz blen),
      up cookie, struct a_core_nyd_info const *cnip){
   char buf[80 +1], c;
   union {int i; uz z;} u;
   char const *sep, *cp;

   /* Ensure actual file name can be seen, unless multibyte comes into play */
   sep = su_empty;
   cp = cnip->cni_file;
   for(u.z = 0; (c = cp[u.z]) != '\0'; ++u.z)
      if(S(uc,c) & 0x80){
         u.z = 0;
         break;
      }
   if(u.z > 40){
      cp += -(38 - u.z);
      sep = "..";
   }

   u.i = snprintf(buf, sizeof(buf) - 1,
         "%c[%2" PRIu32 "] %.25s (%s%.40s:%" PRIu32 ")\n",
         a_core_nyd_desc[(cnip->cni_chirp_line >> su__NYD_ACTION_SHIFT
            ) & su__NYD_ACTION_MASK], cnip->cni_level,
         cnip->cni_fun, sep, cp,
         (cnip->cni_chirp_line & su__NYD_ACTION_SHIFT_MASK));
   if(u.i > 0){
      u.z = u.i;
      if(u.z >= sizeof(buf) -1){
         buf[sizeof(buf) - 2] = '\n';
         buf[sizeof(buf) - 1] = '\0';
         u.z = sizeof(buf) -1; /* (Skip \0) */
      }
      (*ptf)(cookie, buf, u.z);
   }
}
#endif /* DVLOR(1, 0) */

#ifdef su_HAVE_MT
void
su__glock(enum su__glock_type gt){
   NYD2_IN;

   switch(gt){
   case su__GLOCK_STATE: /* XXX spinlock */
      break;
   case su__GLOCK_LOG: /* XXX mutex */
      break;
   }

# if DVLOR(1, 0)
   ASSERT(a_core_glock_recno[gt] != UZ_MAX);
   ++a_core_glock_recno[gt];
# endif

   NYD2_OU;
}

void
su__gunlock(enum su__glock_type gt){
   NYD2_IN;

   switch(gt){
   case su__GLOCK_STATE: /* XXX spinlock */
      break;
   case su__GLOCK_LOG: /* XXX mutex */
      break;
   }

# if DVLOR(1, 0)
   ASSERT(a_core_glock_recno[gt] > 0);
   --a_core_glock_recno[gt];
# endif

   NYD2_OU;
}
#endif /* su_HAVE_MT */

s32
su_state_err(enum su_state_err_type err,
      BITENUM_IS(uz,su_state_err_flags) state, char const *msg_or_nil){
   static char const intro_nomem[] = N_("Out of memory: %s\n"),
      intro_overflow[] = N_("Datatype overflow: %s\n");

   enum su_log_level lvl;
   char const *introp;
   s32 eno;
   u32 xerr;
   NYD2_IN;

   if(msg_or_nil == NIL)
      msg_or_nil = N_("(no error information)");
   state &= su_STATE_ERR_MASK;

   xerr = err;
   switch(xerr &= su_STATE_ERR_TYPE_MASK){
   default:
      ASSERT(0);
      /* FALLTHRU */
   case su_STATE_ERR_NOMEM:
      eno = su_ERR_NOMEM;
      introp = intro_nomem;
      break;
   case su_STATE_ERR_OVERFLOW:
      eno = su_ERR_OVERFLOW;
      introp = intro_overflow;
      break;
   }

   lvl = su_LOG_EMERG;
   if(state & su_STATE_ERR_NOPASS)
      goto jdolog;
   if(state & su_STATE_ERR_PASS)
      lvl = su_LOG_DEBUG;
   else if((state & xerr) || su_state_has(xerr))
      lvl = su_LOG_ALERT;

   if(su_log_would_write(lvl))
jdolog:
      su_log_write(lvl, V_(introp), V_(msg_or_nil));

   if(!(state & su_STATE_ERR_NOERRNO))
      su_err_set_no(eno);

   NYD2_OU;
   return eno;
}

s32
su_err_no(void){
   s32 rv;
   rv = errno; /* TODO a_core_eno */
   return rv;
}

s32
su_err_set_no(s32 eno){
   errno = eno; /* TODO a_core_eno; */
   return eno;
}

s32
su_err_no_by_errno(void){
   s32 rv;
   rv = /*TODO a_core_eno =*/errno;
   return rv;
}

void
su_log_write(BITENUM_IS(u32,su_log_level) lvl, char const *fmt, ...){
   va_list va;
   NYD_IN;

   if(su_log_would_write(lvl)){
      va_start(va, fmt);
      a_evlog(lvl, fmt, va);
      va_end(va);
   }

   NYD_OU;
}

void
su_log_vwrite(BITENUM_IS(u32,su_log_level) lvl, char const *fmt, void *vp){
   NYD_IN;

   if(su_log_would_write(lvl))
      a_evlog(lvl, fmt, *S(va_list*,vp));

   NYD_OU;
}

void
su_assert(char const *expr, char const *file, u32 line, char const *fun,
      boole crash){
   su_log_write((crash ? su_LOG_EMERG : su_LOG_ALERT),
      "SU assert failed: %.60s\n"
      "  File %.60s, line %" PRIu32 "\n"
      "  Function %.142s\n",
      expr, file, line, fun);
}

#if DVLOR(1, 0)
void
su_nyd_set_disabled(boole disabled){
   a_core_nyd_skip = (disabled != FAL0);
}

void
su_nyd_reset_level(u32 nlvl){
   if(nlvl < a_core_nyd_level)
      a_core_nyd_level = nlvl;
}

void
su_nyd_chirp(enum su_nyd_action act, char const *file, u32 line,
      char const *fun){
   LCTA(su__NYD_ACTION_MASK <= 3, "Value too large for bitshift");

   if(!a_core_nyd_skip){
      struct a_core_nyd_info *cnip;

      cnip = &a_core_nyd_infos[0];

      if(a_core_nyd_curr != NELEM(a_core_nyd_infos))
         cnip += a_core_nyd_curr++;
      else
         a_core_nyd_curr = 1;
      cnip->cni_file = file;
      cnip->cni_fun = fun;
      cnip->cni_chirp_line = (S(u32,act & su__NYD_ACTION_MASK
            ) << su__NYD_ACTION_SHIFT) | (line & su__NYD_ACTION_SHIFT_MASK);
      cnip->cni_level = ((act == su_NYD_ACTION_ANYWHERE)
            ? a_core_nyd_level /* TODO spinlock */
            : (act == su_NYD_ACTION_ENTER) ? ++a_core_nyd_level
               : a_core_nyd_level--);
   }
}

void
su_nyd_dump(void (*ptf)(up cookie, char const *buf, uz blen), up cookie){
   uz i;
   struct a_core_nyd_info const *cnip;

   a_core_nyd_skip = TRU1;

   if(a_core_nyd_infos[su_NELEM(a_core_nyd_infos) - 1].cni_file != NIL)
      for(i = a_core_nyd_curr, cnip = &a_core_nyd_infos[i];
            i < su_NELEM(a_core_nyd_infos); ++i)
         a_core_nyd_printone(ptf, cookie, cnip++);

   for(i = 0, cnip = a_core_nyd_infos; i < a_core_nyd_curr; ++i)
      a_core_nyd_printone(ptf, cookie, cnip++);
}
#endif /* DVLOR(1, 0) */

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CORE_CODE
#undef su_MASTER
/* s-it-mode */
