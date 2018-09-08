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

uz su__state;

char const su_empty[] = "";
char const su_reproducible_build[sizeof "reproducible_build"] =
      "reproducible_build";
u16 const su_bom = su_BOM;

char const *su_program;

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
su_assert(char const *expr, char const *file, s32 line, char const *fun,
      boole crash){
   char const *pre;

   pre = (su_program != NIL) ? su_program : su_empty;
   a_ELOG
      "%s: SU assert failed: %.60s\n"
      "%s:   File %.60s, line %d\n"
      "%s:   Function %.142s\n",
      pre, expr,
      pre, file, line,
      pre, fun a_E;

   if(crash)
      abort();
}

#undef a_ELOG
#undef a_EVLOG
#undef a_E

#include "su/code-ou.h"
/* s-it-mode */
