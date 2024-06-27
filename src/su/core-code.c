/*@ Implementation of code.h: (unavoidable) basics.
 *@ And data instantiations from internal.h.
 *@ TODO Log: domain should be configurable; we need log domain objects!
 *@ TODO Assert: the C++ lib has per-thread assertion states, s_nolog to
 *@ TODO    suppress log, test_state(), test_and_clear_state(): for unit tests!
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
#include <stdlib.h> /* TODO Get rid */
#include <unistd.h> /* TODO POSIX module! */

#include "su/cs.h"
#include "su/icodec.h"
#include "su/thread.h"

#ifdef su_USECASE_SU
# include <su/atomic.h>
# include <su/mutex.h>
# include <su/spinlock.h>
#endif

#include "su/internal.h" /* $(SU_SRCDIR) */

/*#include "su/code.h"*/
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

static char const a_core_lvlnames[][8] = {
   FII(su_LOG_EMERG) "emerg", FII(su_LOG_ALERT) "alert",
   FII(su_LOG_CRIT) "crit", FII(su_LOG_ERR) "error",
   FII(su_LOG_WARN) "warning", FII(su_LOG_NOTICE) "notice",
   FII(su_LOG_INFO) "info", FII(su_LOG_DEBUG) "debug"
};
/* You can deduce the value from the offset */
CTAV(su_LOG_EMERG == 0);
CTAV(su_LOG_DEBUG == 7);

#if DVLOR(1, 0)
CTAV(su_NYD_ACTION_ENTER == 0);
CTAV(su_NYD_ACTION_LEAVE == 1);
CTAV(su_NYD_ACTION_ANYWHERE == 2);
static char const a_core_nyd_desc[3] = {'>','<','='};
#endif

static su_log_write_fun a_core_log_write;

#ifdef su_USECASE_SU
static struct su_spinlock a_core_glck_state;
static struct su_mutex a_core_glck_gi9r;
static struct su_mutex a_core_glck_log;
#endif

DVL( static struct su__nyd_control a_core_thread_main_nyd; )

union su__bom_union const su__bom_little = {{'\xFF', '\xFE'}};
union su__bom_union const su__bom_big = {{'\xFE', '\xFF'}};
char const su_empty[] = "";
char const su_reproducible_build[] = "reproducible_build";
u16 const su_bom = su_BOM;

uz su__state;
char const *su_program;

/* internal.h */
#if defined su_USECASE_SU && !su_ATOMIC_IS_REAL
struct su_mutex su__atomic_cas_mtx;
struct su_mutex su__atomic_xchg_mtx;
#endif
#if DVLDBGOR(1, 0)
u8 su__mem_filler;
#endif
struct su__state_on_gut *su__state_on_gut;
struct su__state_on_gut *su__state_on_gut_final;

/* */
SINLINE void a_core_evlog(u32 lvl_a_flags, char const *fmt, va_list ap);

/* */
#if DVLOR(1, 0)
static void a_core_nyd_printone(void (*ptf)(up cookie, char const *buf,
      uz blen), up cookie, struct su__nyd_info const *nip);
#endif

SINLINE void
a_core_evlog(u32 lvl_a_flags, char const *fmt, va_list ap){
   /* TODO buffer size is 2048; syslog: RFC 3164 -> 1024, RFC 5424 -> flexible,
    * TODO assume at least 4096; FreeBSD has 8Kib since 2021-12-31 (again) */
   char buf[2048]/* TODO FmtCodec */, ibuf[su_IENC_BUFFER_SIZE],
      *xbuf, *cursor;
   u32 f;

   f = lvl_a_flags & ~S(u32,su_LOG_PRIMASK);
   lvl_a_flags &= su_LOG_PRIMASK;

   cursor = xbuf = buf;

   /* Prepare our line prefix TODO use FmtCodec */
   if(su_program != NIL){
      cursor = su_cs_pcopy_n(cursor, su_program, 33);
      if(cursor == NIL)
         cursor = &buf[32];

      if(su_state_has(su_STATE_LOG_SHOW_PID)){
         char const *cp;

         *cursor++ = '[';
         cp = su_ienc_u32(ibuf, getpid(), 10); /* XXX getpid()->process_id() */
         cursor = su_cs_pcopy(cursor, cp);
         *cursor++ = ']';
      }
   }

   if(su_state_has(su_STATE_LOG_SHOW_LEVEL)){
      *cursor++ = '[';
      cursor = su_cs_pcopy(cursor, a_core_lvlnames[lvl_a_flags]);
      *cursor++ = ']';
   }

   if(cursor != buf){
      *cursor++ = ':';
      *cursor++ = ' ';
   }

   /* Shall the prefix be CANcelled on the first line? */
   if(*fmt == '\x18'){
      ++fmt;
      xbuf = cursor;
   }
   vsnprintf(cursor, (sizeof(buf) - 1 - 1 - P2UZ(cursor - buf)), fmt, ap);

   /* For each line in fmt, write out one line */
   SU( su_MUTEX_LOCK(&a_core_glck_log); )
   for(;;){
      char *cp, c;

      /* Normalize control characters but HT */
      for(cp = cursor;; ++cp){
         if(LIKELY(!su_cs_is_cntrl(c = *cp)))
            continue;

         if(c == '\0' || c == '\n'){
            *cp++ = '\n';
            if(c != '\0')
               c = *cp;
            break;
         }

         /* Last newline can also be CANcelled */
         if(c != '\x18'){
            if(c != '\t')
jspace:
               *cp = ' ';
         }else if(cp[1] == '\0'){
            c = '\0';
            break;
         }else{
            /* XXX hint user on her error */
            goto jspace;
         }
      }
      *cp = '\0';

      if(a_core_log_write != NIL)
         (*a_core_log_write)((f | lvl_a_flags), xbuf, P2UZ(cp - xbuf));
      else
         fwrite(xbuf, sizeof(*xbuf), P2UZ(cp - xbuf), /*TODO STD*/stderr);

      if(c == '\0')
         break;

      xbuf = buf;
      *cp = c;
      su_cs_pcopy(cursor, cp);
   }
   SU( su_MUTEX_UNLOCK(&a_core_glck_log); )

   if(lvl_a_flags == su_LOG_EMERG)
      abort(); /* TODO configurable */
}

#if DVLOR(1, 0)
static void
a_core_nyd_printone(void (*ptf)(up cookie, char const *buf, uz blen),
      up cookie, struct su__nyd_info const *nip){
   char buf[80 +1], c;
   union {int i; uz z;} u;
   char const *sep, *cp;

   /* Ensure actual file name can be seen, unless multibyte comes into play */
   sep = su_empty;
   cp = nip->ni_file;
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
         a_core_nyd_desc[(nip->ni_chirp_line >> su__NYD_ACTION_SHIFT
            ) & su__NYD_ACTION_MASK], nip->ni_level,
         nip->ni_fun, sep, cp,
         (nip->ni_chirp_line & su__NYD_ACTION_SHIFT_MASK));
   if(u.i > 0){
      u.z = S(uz,u.i);
      if(u.z >= sizeof(buf) -1){
         buf[sizeof(buf) - 2] = '\n';
         buf[sizeof(buf) - 1] = '\0';
         u.z = sizeof(buf) -1; /* (Skip \0) */
      }
      (*ptf)(cookie, buf, u.z);
   }
}
#endif /* DVLOR(1, 0) */

#ifdef su_USECASE_SU
void
su__glck(enum su__glck_type gt){
   NYD2_IN;

   switch(gt){
   case su__GLCK_STATE:
      su_spinlock_lock(&a_core_glck_state);
      break;
   case su__GLCK_GI9R:
      su_MUTEX_LOCK(&a_core_glck_gi9r);
      break;
   case su__GLCK_LOG:
      su_MUTEX_LOCK(&a_core_glck_log);
      break;
   }

   NYD2_OU;
}

void
su__gnlck(enum su__glck_type gt){
   NYD2_IN;

   switch(gt){
   case su__GLCK_STATE:
      su_spinlock_unlock(&a_core_glck_state);
      break;
   case su__GLCK_GI9R:
      su_MUTEX_UNLOCK(&a_core_glck_gi9r);
      break;
   case su__GLCK_LOG:
      su_MUTEX_UNLOCK(&a_core_glck_log);
      break;
   }

   NYD2_OU;
}
#endif /* su_USECASE_SU */

s32
su_state_create_core(char const *program_or_nil, uz flags, u32 estate){
   s32 rv;
   UNUSED(estate);

   rv = su_STATE_NONE;

   flags &= (su__STATE_GLOBAL_MASK | su__STATE_LOG_MASK);
   su__state = flags;

   /* */
   su__thread_main.t_.flags = su_THREAD_MAIN;
   su__thread_main.t_.name = "main";
   DVL(
      su__thread_main.t_.nydctl = &a_core_thread_main_nyd;
      a_core_thread_main_nyd.nc_level = 0;
      a_core_thread_main_nyd.nc_curr = 0;
      a_core_thread_main_nyd.nc_skip = FAL0;
   )

   /* internal.h */
#if DVLDBGOR(1, 0)
   su__mem_filler = 0xAA;
#endif

#ifdef su_USECASE_SU
   if((rv = su_spinlock_create(&a_core_glck_state, "SU: GLCK_STATE", estate)))
      goto jerr;
   if((rv = su_mutex_create(&a_core_glck_gi9r, "SU: GLCK_GI9R", estate)))
      goto jerr;
   if((rv = su_mutex_create(&a_core_glck_log, "SU: GLCK_LOG", estate)))
      goto jerr;

# if !su_ATOMIC_IS_REAL
   if((rv = su_mutex_create(&su__atomic_cas_mtx, "SU: atomic CAS", estate)))
      goto jerr;
   if((rv = su_mutex_create(&su__atomic_xchg_mtx, "SU: atomic XCHG", estate)))
      goto jerr;
# endif
#endif

   /* Initialized! */
   su__state |= su__STATE_CREATED;

   /**/
   if(program_or_nil != NIL){
      char *cp;

      if((cp = su_cs_rfind_c(program_or_nil, '/')) != NIL && *++cp != '\0')
         program_or_nil = cp;

      su_program = program_or_nil;
   }

#ifdef su_USECASE_SU
jleave:
#endif
   return rv;

#ifdef su_USECASE_SU
jerr:
   if(!(estate & su_STATE_ERR_PASS) &&
         ((estate & su_STATE_ERR_NOPASS) ||
          !(S(u32,rv) & estate) || !(su__state & S(u32,rv)))){
      abort(); /* TODO configurable; NO ERROR LOG WHATSOEVER HERE!! */
   }
   goto jleave;
#endif
}

void
su_state_gut(BITENUM_IS(u32,su_state_gut_flags) flags){
   struct su__state_on_gut *sogp;
   NYD_IN;

   if(!(flags & su_STATE_GUT_NO_IO)){
   }

   if(!(flags & su_STATE_GUT_NO_HANDLERS))
      for(sogp = su__state_on_gut; sogp != NIL; sogp = sogp->sog_last)
         (*sogp->sog_cb)(flags);

   if(!(flags & su_STATE_GUT_NO_FINAL_HANDLERS)){
      /* More care since in DVL() mode the last one cleans it all up! */
      struct su__state_on_gut *tmp;

      for(sogp = su__state_on_gut_final; (tmp = sogp) != NIL;){
         sogp = tmp->sog_last;
         (*tmp->sog_cb)(flags);
      }
   }

   su__state_on_gut = su__state_on_gut_final = NIL;

   if(!(flags & su_STATE_GUT_NO_LOCKS) &&
         (flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
#ifdef su_USECASE_SU
# if !su_ATOMIC_IS_REAL
      su_mutex_gut(&su__atomic_xchg_mtx);
      su_mutex_gut(&su__atomic_cas_mtx);
# endif

      su_mutex_gut(&a_core_glck_log);
      su_mutex_gut(&a_core_glck_gi9r);
      su_spinlock_gut(&a_core_glck_state);
#endif

      su_CC_MEM_ZERO(&su__thread_main, sizeof(su__thread_main));
      DVL( su_CC_MEM_ZERO(&a_core_thread_main_nyd,
         sizeof(a_core_thread_main_nyd)); )
   }

   su_program = NIL;
   su__state = 0;

   NYD_OU;
}

s32
su_state_err(enum su_state_err_type err,
      BITENUM_IS(uz,su_state_err_flags) state, char const *msg_or_nil){
   static char const intro_nomem[] = N_("Out of memory: %s"),
      intro_overflow[] = N_("Datatype overflow: %s");

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
      FALLTHRU
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
su_err_no(void){ /* xxx INLINE? */
   return su_thread_get_err_no();
}

void
su_err_set_no(s32 eno){ /* xxx INLINE? */
   su_thread_set_err_no(su_thread_self(), eno);
}

s32
su_err_no_by_errno(void){
   s32 rv;

   su_thread_set_err_no(su_thread_self(), rv = errno);
   return rv;
}

su_log_write_fun
su_log_get_write_fun(void){
   su_log_write_fun rv;
   NYD_IN;

   rv = a_core_log_write;

   NYD_OU;
   return rv;
}

void
su_log_set_write_fun(su_log_write_fun fun){
   NYD_IN;

   a_core_log_write = fun;

   NYD_OU;
}

void
su_log_write(u32 lvl_a_flags, char const *fmt, ...){
   va_list va;
   NYD_IN;

   if(su_log_would_write(S(enum su_log_level,lvl_a_flags))){
      va_start(va, fmt);
      a_core_evlog(lvl_a_flags, fmt, va);
      va_end(va);
   }

   NYD_OU;
}

void
su_log_vwrite(u32 lvl_a_flags, char const *fmt, void *vp){
   NYD_IN;

   if(su_log_would_write(S(enum su_log_level,lvl_a_flags)))
      a_core_evlog(lvl_a_flags, fmt, *S(va_list*,vp));

   NYD_OU;
}

void
su_assert(char const *expr, char const *file, u32 line, char const *fun,
      boole crash){
   su_log_write((crash ? su_LOG_EMERG : su_LOG_ALERT),
      "SU assert failed: %.60s\n"
      "  File %.60s, line %" PRIu32 "\n"
      "  Function %.142s",
      expr, file, line, fun);
   su_err_set_no(su_ERR_FAULT);
}

#if DVLOR(1, 0)
void
su_nyd_set_disabled(boole disabled){
   struct su__nyd_control *ncp;

   ncp = S(struct su__nyd_control*,su_thread_self()->t_.nydctl);
   ncp->nc_skip = (disabled != FAL0);
}

void
su_nyd_reset_level(u32 nlvl){
   struct su__nyd_control *ncp;

   ncp = S(struct su__nyd_control*,su_thread_self()->t_.nydctl);

   if(nlvl < ncp->nc_level)
      ncp->nc_level = nlvl;
}

void
su_nyd_chirp(enum su_nyd_action act, char const *file, u32 line,
      char const *fun){
   struct su__nyd_control *ncp;

   LCTA(su__NYD_ACTION_MASK <= 3, "Value too large for bitshift");

   /* nyd_chirp() documented to be capable to deal */
   if(!(su__state & su__STATE_CREATED))
      goto jleave;

   ncp = S(struct su__nyd_control*,su_thread_self()->t_.nydctl);

   if(!ncp->nc_skip){
      struct su__nyd_info *nip;

      if(ncp->nc_curr == NELEM(ncp->nc_infos))
         ncp->nc_curr = 0;

      nip = &ncp->nc_infos[ncp->nc_curr++];
      nip->ni_file = file;
      nip->ni_fun = fun;
      nip->ni_chirp_line = (S(u32,act & su__NYD_ACTION_MASK
            ) << su__NYD_ACTION_SHIFT) | (line & su__NYD_ACTION_SHIFT_MASK);
      nip->ni_level = ((act == su_NYD_ACTION_ANYWHERE)
            ? ncp->nc_level
            : ((act == su_NYD_ACTION_ENTER) ? ++ncp->nc_level
               : ncp->nc_level--));
   }

jleave:;
}

void
su_nyd_dump(void (*ptf)(up cookie, char const *buf, uz blen), up cookie){
   uz i;
   struct su__nyd_info const *nip;
   struct su__nyd_control *ncp;

   LCTA(su__NYD_ACTION_MASK <= 3, "Value too large for bitshift");

   ncp = S(struct su__nyd_control*,su_thread_self()->t_.nydctl);
   ncp->nc_skip = TRU1;

   if((nip = ncp->nc_infos)[NELEM(ncp->nc_infos) - 1].ni_file != NIL)
      for(i = ncp->nc_curr, nip += i; i < NELEM(ncp->nc_infos); ++i)
         a_core_nyd_printone(ptf, cookie, nip++);

   for(i = 0, nip = ncp->nc_infos; i < ncp->nc_curr; ++i)
      a_core_nyd_printone(ptf, cookie, nip++);

   ncp->nc_skip = FAL0;
}
#endif /* DVLOR(1, 0) */

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CORE_CODE
#undef su_MASTER
/* s-it-mode */
