/*@ Implementation of code.h: (unavoidable) basics.
 *@ TODO SMP locks: global_lock(), log_lock(); logging needs lock encapsul!
 *@ TODO Log: domain should be configurable
 *@ TODO Assert: the C++ lib has per-thread assertion states, s_nolog to
 *@ TODO    suppress log, test_state(), test_and_clear_state(): for unit tests!
 *@ TODO su_program: if set, the PID should be logged, too!
 *
 * Copyright (c) 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_core_code
#define su_SOURCE
#define su_SOURCE_CORE_CODE
#define su_MASTER

#include <errno.h> /* XXX Grrrr */
#include <stdarg.h>
#include <stdlib.h>

#ifndef su_USECASE_MX
# include <stdio.h> /* TODO Get rid */
#endif

#include "su/code.h"
#include "su/code-in.h"

#ifndef su_USECASE_MX
# define a_ELOG fprintf(stderr,
# define a_EVLOG vfprintf(stderr,
#else
   /* TODO Eventually all the I/O is SU based, then these vanish!
    * TODO We need some _USECASE_ hook to store readily prepared lines then */
# ifndef mx_HAVE_AMALGAMATION
void n_err(char const *format, ...);
void n_verr(char const *format, va_list ap);
# endif
# define a_ELOG n_err(
# define a_EVLOG n_verr(
#endif
#define a_E )

#define a_PRIMARY_DOLOG(LVL) \
   ((S(u32,LVL) /*& su__STATE_LOG_MASK*/) <= \
         (su__state & su__STATE_LOG_MASK) ||\
      (su__state & su__STATE_D_V))

#if defined su_HAVE_DEBUG || defined su_HAVE_DEVEL
struct a_core_nyd_info{
   char const *cni_file;
   char const *cni_fun;
   u32 cni_chirp_line;
   u32 cni_level;
};
#endif

static char const a_c_lvlnames[][8] = { /* TODO no level name stuff yet*/
   FIELD_INITI(su_LOG_EMERG) "emerg",
   FIELD_INITI(su_LOG_ALERT) "alert",
   FIELD_INITI(su_LOG_CRIT) "crit",
   FIELD_INITI(su_LOG_ERR) "error",
   FIELD_INITI(su_LOG_WARN) "warning\0",
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
static u32 a_core_nyd_curr, a_core_nyd_level;
static boole a_core_nyd_skip;
static struct a_core_nyd_info a_core_nyd_infos[su_NYD_ENTRIES];
#endif

uz su__state;

char const su_empty[] = "";
char const su_reproducible_build[sizeof "reproducible_build"] =
      "reproducible_build";
u16 const su_bom = su_BOM;

char const *su_program;

/* */
#if DVLOR(1, 0)
static void a_core_nyd_printone(void (*ptf)(up cookie, char const *buf,
      uz blen), up cookie, struct a_core_nyd_info const *cnip);
#endif

#if DVLOR(1, 0)
static void
a_core_nyd_printone(void (*ptf)(up cookie, char const *buf, uz blen),
      up cookie, struct a_core_nyd_info const *cnip){
   char buf[80], c;
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
         "%c [%2" PRIu32 "] %.25s (%s%.40s:%" PRIu32 ")\n",
         "=><"[(cnip->cni_chirp_line >> 29) & 0x3], cnip->cni_level,
         cnip->cni_fun, sep, cp, (cnip->cni_chirp_line & 0x1FFFFFFFu));
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

s32
su_state_err(uz state, char const *msg_or_nil){
   static char const intro_nomem[] = N_("Out of memory: %s\n"),
      intro_overflow[] = N_("Datatype overflow: %s\n");

   enum su_log_level lvl;
   char const *introp;
   s32 err;
   NYD2_IN;
   ASSERT((state & su__STATE_ERR_MASK) && !(state & ~su__STATE_ERR_MASK));

   switch(state & su_STATE_ERR_TYPE_MASK){
   default:
      ASSERT(0);
      /* FALLTHRU */
   case su_STATE_ERR_NOMEM:
      err = su_ERR_NOMEM;
      introp = intro_nomem;
      break;
   case su_STATE_ERR_OVERFLOW:
      err = su_ERR_OVERFLOW;
      introp = intro_overflow;
      break;
   }
   if(msg_or_nil == NIL)
      msg_or_nil = N_("(no error information)");

   if(state & su_STATE_ERR_NOPASS){
      lvl = su_LOG_EMERG;
      goto jdolog;
   }
   lvl = state & su__STATE_LOG_MASK;
   if(state & su_STATE_ERR_PASS){
      lvl = su_LOG_DEBUG;
      goto jlog_check;
   }else if(su_state_has(state)){
      lvl = su_LOG_ALERT;
      goto jlog_check;
   }else{
jlog_check:
      if(a_PRIMARY_DOLOG(lvl))
jdolog:
         su_log_write(lvl, V_(introp), V_(msg_or_nil));
   }

   if(err != su_ERR_NONE && !(state & su_STATE_ERR_NOERRNO))
      su_err_set_no(err);
   NYD2_OU;
   return err;
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
su_err_no_via_errno(void){
   s32 rv;
   rv = /*TODO a_core_eno =*/errno;
   return rv;
}

void
su_log_write(enum su_log_level lvl, char const *fmt, ...){
   va_list va;
   NYD_IN;

   if(a_PRIMARY_DOLOG(lvl)){
#ifndef su_USECASE_MX
      if(su_program != NIL)
         a_ELOG "%s: ", su_program a_E;
#endif
      va_start(va, fmt);
      a_EVLOG fmt, va a_E;
      va_end(va);

      if(lvl == su_LOG_EMERG)
         abort(); /* TODO configurable */
   }
   NYD_OU;
}

void
su_log_vwrite(enum su_log_level lvl, char const *fmt, void *vp){
   NYD_IN;

   if(a_PRIMARY_DOLOG(lvl)){
#ifndef su_USECASE_MX
      if(su_program != NIL)
         a_ELOG "%s: ", su_program a_E;
#endif
      a_EVLOG fmt, *S(va_list*,vp) a_E;

      if(lvl == su_LOG_EMERG)
         abort(); /* TODO configurable */
   }
   NYD_OU;
}

void
su_assert(char const *expr, char const *file, u32 line, char const *fun,
      boole crash){
   char const *pre;

   pre = (su_program != NIL) ? su_program : su_empty;
   a_ELOG
      "%s: SU assert failed: %.60s\n"
      "%s:   File %.60s, line %" PRIu32 "\n"
      "%s:   Function %.142s\n",
      pre, expr,
      pre, file, line,
      pre, fun a_E;

   if(crash)
      abort();
}

#if DVLOR(1, 0)
void
su_nyd_chirp(u8 act, char const *file, u32 line, char const *fun){
   if(!a_core_nyd_skip){
      struct a_core_nyd_info *cnip;

      cnip = &a_core_nyd_infos[0];

      if(a_core_nyd_curr != su_NELEM(a_core_nyd_infos))
         cnip += a_core_nyd_curr++;
      else
         a_core_nyd_curr = 1;
      cnip->cni_file = file;
      cnip->cni_fun = fun;
      cnip->cni_chirp_line = (S(u32,act & 0x3) << 29) | (line & 0x1FFFFFFFu);
      cnip->cni_level = ((act == 0) ? a_core_nyd_level /* TODO spinlock */
            : (act == 1) ? ++a_core_nyd_level : a_core_nyd_level--);
   }
}

void
su_nyd_dump(void (*ptf)(up cookie, char const *buf, uz blen), up cookie){
   uz i;
   struct a_core_nyd_info const *cnip;

   a_core_nyd_skip = TRU1;
   if(a_core_nyd_infos[su_NELEM(a_core_nyd_infos) - 1].cni_file != NULL)
      for(i = a_core_nyd_curr, cnip = &a_core_nyd_infos[i];
            i < su_NELEM(a_core_nyd_infos); ++i)
         a_core_nyd_printone(ptf, cookie, cnip++);
   for(i = 0, cnip = a_core_nyd_infos; i < a_core_nyd_curr; ++i)
      a_core_nyd_printone(ptf, cookie, cnip++);
}
#endif /* DVLOR(1, 0) */

#undef a_ELOG
#undef a_EVLOG
#undef a_E

#include "su/code-ou.h"
/* s-it-mode */
