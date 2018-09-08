/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Account, macro and variable handling; `vexpr' and `vpospar'.
 *@ HOWTO add a new non-dynamic boolean or value option:
 *@ - add an entry to nail.h:enum okeys
 *@ - run make-okey-map.pl (which is highly related..)
 *@ - update the manual!
 *@ TODO . `localopts' should act like an automatic permanent `scope' command
 *@ TODO    modifier!  We need an OnScopeLeaveEvent, then.
 *@ TODO   Also see the a_GO_SPLICE comment in go.c.
 *@ TODO . Optimize: with the dynamic hashmaps, and the object based approach
 *@ TODO   it should become possible to strip down the implementation again.
 *@ TODO   E.g., FREEZE is much too complicated: use an overlay object ptr,
 *@ TODO   UNLIKELY() it, and add a OnProgramStartupCompletedEvent to
 *@ TODO   incorporate what it tracks, then drop it.  Etc.
 *@ TODO   Global -> Scope -> Local, all "overlay" objects.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef su_FILE
#define su_FILE accmacvar
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#if !defined mx_HAVE_SETENV && !defined mx_HAVE_PUTENV
# error Exactly one of mx_HAVE_SETENV and mx_HAVE_PUTENV
#endif

/* Positional parameter maximum (macro arguments, `vexpr' "splitifs") */
#define a_AMV_POSPAR_MAX SI16_MAX

/* Special "pseudo macro" that stabs you from the back */
#define a_AMV_MACKY_MACK ((struct a_amv_mac*)-1)

/* Note: changing the hash function must be reflected in `vexpr' "hash",
 * because that is used by the hashtable creator scripts! */
#define a_AMV_PRIME HSHSIZE
#define a_AMV_NAME2HASH(N) n_torek_hash(N)
#define a_AMV_HASH2PRIME(H) ((H) % a_AMV_PRIME)

enum a_amv_mac_flags{
   a_AMV_MF_NONE = 0,
   a_AMV_MF_ACCOUNT = 1u<<0,  /* This macro is an `account' */
   a_AMV_MF_TYPE_MASK = a_AMV_MF_ACCOUNT,
   a_AMV_MF_UNDEF = 1u<<1,    /* Unlink after lookup */
   a_AMV_MF_DELETE = 1u<<7,   /* Delete in progress, free once refcnt==0 */
   a_AMV_MF__MAX = 0xFFu
};

enum a_amv_loflags{
   a_AMV_LF_NONE = 0,
   a_AMV_LF_SCOPE = 1u<<0,          /* Current scope `localopts' on */
   a_AMV_LF_SCOPE_FIXATE = 1u<<1,   /* Ditto, but fixated */
   a_AMV_LF_SCOPE_MASK = a_AMV_LF_SCOPE | a_AMV_LF_SCOPE_FIXATE,
   a_AMV_LF_CALL = 1u<<2,           /* `localopts' on for `call'ed scopes */
   a_AMV_LF_CALL_FIXATE = 1u<<3,    /* Ditto, but fixated */
   a_AMV_LF_CALL_MASK = a_AMV_LF_CALL | a_AMV_LF_CALL_FIXATE,
   a_AMV_LF_CALL_TO_SCOPE_SHIFT = 2
};

/* make-okey-map.pl ensures that _VIRT implies _RDONLY and _NODEL, and that
 * _IMPORT implies _ENV; it doesn't verify anything...
 * More description at nail.h:enum okeys */
enum a_amv_var_flags{
   a_AMV_VF_NONE = 0,

   /* The basic set of flags, also present in struct a_amv_var_map.avm_flags */
   a_AMV_VF_BOOL = 1u<<0,     /* ok_b_* */
   a_AMV_VF_CHAIN = 1u<<1,    /* Has -HOST and/or -USER@HOST variants */
   a_AMV_VF_VIRT = 1u<<2,     /* "Stateless" automatic variable */
   a_AMV_VF_VIP = 1u<<3,      /* Wants _var_check_vips() evaluation */
   a_AMV_VF_RDONLY = 1u<<4,   /* May not be set by user */
   a_AMV_VF_NODEL = 1u<<5,    /* May not be deleted */
   a_AMV_VF_I3VAL = 1u<<6,    /* Has an initial value */
   a_AMV_VF_DEFVAL = 1u<<7,   /* Has a default value */
   a_AMV_VF_IMPORT = 1u<<8,   /* Import ONLY from env (pre n_PSO_STARTED) */
   a_AMV_VF_ENV = 1u<<9,      /* Update environment on change */
   a_AMV_VF_NOLOPTS = 1u<<10, /* May not be tracked by `localopts' */
   a_AMV_VF_NOTEMPTY = 1u<<11, /* May not be assigned an empty value */
   /* TODO _VF_NUM, _VF_POSNUM: we also need 64-bit limit numbers! */
   a_AMV_VF_NUM = 1u<<12,     /* Value must be a 32-bit number */
   a_AMV_VF_POSNUM = 1u<<13,  /* Value must be positive 32-bit number */
   a_AMV_VF_LOWER = 1u<<14,   /* Values will be stored in lowercase version */
   a_AMV_VF_OBSOLETE = 1u<<15, /* Is obsolete? */
   a_AMV_VF__MASK = (1u<<(15+1)) - 1,

   /* Extended flags, not part of struct a_amv_var_map.avm_flags */
   /* This flag indicates the instance is actually a variant of a _VF_CHAIN,
    * it thus uses the a_amv_var_map of the base variable, but it is not the
    * base itself and therefore care must be taken */
   a_AMV_VF_EXT_CHAIN = 1u<<22,
   a_AMV_VF_EXT_LOCAL = 1u<<23,        /* `local' */
   a_AMV_VF_EXT_LINKED = 1u<<24,       /* `environ' link'ed */
   a_AMV_VF_EXT_FROZEN = 1u<<25,       /* Has been set by -S,.. */
   a_AMV_VF_EXT_FROZEN_UNSET = 1u<<26, /* ..and was used to unset a variable */
   a_AMV_VF_EXT__FROZEN_MASK = a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET,
   a_AMV_VF_EXT__MASK = (1u<<(26+1)) - 1
};

enum a_amv_var_lookup_flags{
   a_AMV_VLOOK_NONE = 0,
   a_AMV_VLOOK_LOCAL = 1u<<0,       /* Query `local' layer first */
   a_AMV_VLOOK_LOCAL_ONLY = 1u<<1,  /* MUST be a `local' variable */
   /* Do not allocate new var for _I3VAL, see _var_lookup() for more */
   a_AMV_VLOOK_I3VAL_NONEW = 1u<<2,
   a_AMV_VLOOK_I3VAL_NONEW_REPORT = 1u<<3
};

enum a_amv_var_setclr_flags{
   a_AMV_VSETCLR_NONE = 0,
   a_AMV_VSETCLR_LOCAL = 1u<<0,     /* Use `local' variables only */
   /* XXX Maybe something for only non-local? */
   a_AMV_VSETCLR_ENV = 1u<<1        /* `environ' or otherwise environ */
};

/* We support some special parameter names for one(+)-letter variable names;
 * note these have counterparts in the code that manages shell expansion!
 * All these special variables are solely backed by n_var_vlook(), and besides
 * there is only a_amv_var_revlookup() which knows about them */
enum a_amv_var_special_category{
   a_AMV_VSC_NONE,      /* Normal variable, no special treatment */
   a_AMV_VSC_GLOBAL,    /* ${[?!]} are specially mapped, but global */
   a_AMV_VSC_MULTIPLEX, /* ${^.*} circumflex accent multiplexer */
   a_AMV_VSC_POSPAR,    /* ${[1-9][0-9]*} positional parameters */
   a_AMV_VSC_POSPAR_ENV /* ${[*@#]} positional parameter support variables */
};

enum a_amv_var_special_type{
   /* _VSC_GLOBAL */
   a_AMV_VST_QM,     /* ? */
   a_AMV_VST_EM,     /* ! */
   /* _VSC_MULTIPLEX */
   /* This is special in that it is a multiplex indicator, the ^ is followed by
    * a normal variable */
   a_AMV_VST_CACC,   /* ^ (circumflex accent) */
   /* _VSC_POSPAR_ENV */
   a_AMV_VST_STAR,   /* * */
   a_AMV_VST_AT,     /* @ */
   a_AMV_VST_NOSIGN  /* # */
};

enum a_amv_var_vip_mode{
   a_AMV_VIP_SET_PRE,
   a_AMV_VIP_SET_POST,
   a_AMV_VIP_CLEAR
};

struct a_amv_pospar{
   ui16_t app_maxcount;    /* == slots in .app_dat */
   ui16_t app_count;       /* Maximum a_AMV_POSPAR_MAX */
   ui16_t app_idx;         /* `shift' moves this one, decs .app_count */
   bool_t app_not_heap;    /* .app_dat stuff not dynamically allocated */
   ui8_t app__dummy[1];
   char const **app_dat;   /* NULL terminated (for "$@" explosion support) */
};
n_CTA(a_AMV_POSPAR_MAX <= SI16_MAX, "Limit exceeds datatype capabilities");

struct a_amv_mac{
   struct a_amv_mac *am_next;
   ui32_t am_maxlen;             /* of any line in .am_line_dat */
   ui32_t am_line_cnt;           /* of *.am_line_dat (but NULL terminated) */
   struct a_amv_mac_line **am_line_dat; /* TODO use deque? */
   struct a_amv_var *am_lopts;   /* `localopts' unroll list */
   ui32_t am_refcnt;             /* 0-based for `un{account,define}' purposes */
   ui8_t am_flags;               /* enum a_amv_mac_flags */
   char am_name[n_VFIELD_SIZE(3)]; /* of this macro */
};
n_CTA(a_AMV_MF__MAX <= UI8_MAX, "Enumeration excesses storage datatype");

struct a_amv_mac_line{
   ui32_t aml_len;
   ui32_t aml_prespc;   /* Number of leading SPACEs, for display purposes */
   char aml_dat[n_VFIELD_SIZE(0)];
};

struct a_amv_mac_call_args{
   char const *amca_name;        /* For MACKY_MACK, this is *0*! */
   struct a_amv_mac *amca_amp;   /* "const", but for am_refcnt */
   struct a_amv_var **amca_unroller;
   void (*amca_hook_pre)(void *);
   void *amca_hook_arg;
   ui8_t amca_loflags;
   bool_t amca_ps_hook_mask;
   bool_t amca_no_xcall;         /* We want n_GO_INPUT_NO_XCALL for this */
   ui8_t amca__pad[5];
   struct a_amv_var *(*amca_local_vars)[a_AMV_PRIME]; /* `local's, or NULL */
   struct a_amv_pospar amca_pospar;
};

struct a_amv_lostack{
   struct a_amv_lostack *as_global_saved; /* Saved global XXX due to jump */
   struct a_amv_mac_call_args *as_amcap;
   struct a_amv_lostack *as_up;  /* Outer context */
   struct a_amv_var *as_lopts;
   ui8_t as_loflags;             /* enum a_amv_mac_loflags */
   ui8_t avs__pad[7];
};

struct a_amv_var{
   struct a_amv_var *av_link;
   char *av_value;
#ifdef mx_HAVE_PUTENV
   char *av_env;              /* Actively managed putenv(3) memory, or NULL */
#endif
   ui32_t av_flags;           /* enum a_amv_var_flags inclusive extended bits */
   char av_name[n_VFIELD_SIZE(4)];
};
n_CTA(a_AMV_VF_EXT__MASK <= UI32_MAX, "Enumeration excesses storage datatype");

/* After inclusion of gen-okeys.h we assert keyoff fits in 16-bit */
struct a_amv_var_map{
   ui32_t avm_hash;
   ui16_t avm_keyoff;
   ui16_t avm_flags;    /* enum a_amv_var_flags without extended bits */
};
n_CTA(a_AMV_VF__MASK <= UI16_MAX, "Enumeration excesses storage datatype");

/* XXX Since there is no indicator character used for variable chains, we just
 * XXX cannot do better than using some s....y detection.
 * The length of avcmb_prefix is highly hardwired with make-okey-map.pl etc. */
struct a_amv_var_chain_map_bsrch{
   char avcmb_prefix[4];
   ui16_t avcmb_chain_map_off;
   ui16_t avcmb_chain_map_eokey; /* This is an enum okey */
};

/* Use 16-bit for enum okeys, which should always be sufficient; all around
 * here we use 32-bit for it instead, but that owed to faster access (?) */
struct a_amv_var_chain_map{
   ui16_t avcm_keyoff;
   ui16_t avcm_okey;
};
n_CTA(n_OKEYS_MAX <= UI16_MAX, "Enumeration excesses storage datatype");

struct a_amv_var_virt{
   ui32_t avv_okey;
   ui8_t avv__dummy[4];
   struct a_amv_var const *avv_var;
};

struct a_amv_var_defval{
   ui32_t avdv_okey;
   ui8_t avdv__pad[4];
   char const *avdv_value; /* Only for !BOOL (otherwise plain existence) */
};

struct a_amv_var_carrier{
   char const *avc_name;
   ui32_t avc_hash;
   ui32_t avc_prime;
   struct a_amv_var *avc_var;
   struct a_amv_var_map const *avc_map;
   enum okeys avc_okey;
   bool_t avc_is_chain_variant;  /* Base is a chain, this a variant thereof */
   ui8_t avc_special_cat;
   /* Numerical parameter name if .avc_special_cat=a_AMV_VSC_POSPAR,
    * otherwise a enum a_amv_var_special_type */
   ui16_t avc_special_prop;
};

/* Include constant make-okey-map.pl output, and the generated version data */
#include "mx/gen-version.h"
#include "mx/gen-okeys.h"

/* As promised above, CTAs to protect our structures */
n_CTA(a_AMV_VAR_NAME_KEY_MAXOFF <= UI16_MAX,
   "Enumeration excesses storage datatype");

/* The currently active account */
static struct a_amv_mac *a_amv_acc_curr;

static struct a_amv_mac *a_amv_macs[a_AMV_PRIME]; /* TODO dynamically spaced */

/* Unroll list of currently running macro stack */
static struct a_amv_lostack *a_amv_lopts;

static struct a_amv_var *a_amv_vars[a_AMV_PRIME]; /* TODO dynamically spaced */

/* Global (i.e., non-local) a_AMV_VSC_POSPAR stack */
static struct a_amv_pospar a_amv_pospar;

/* TODO We really deserve localopts support for *folder-hook*s, so hack it in
 * TODO today via a static lostack, it should be a field in mailbox, once that
 * TODO is a real multi-instance object */
static struct a_amv_var *a_amv_folder_hook_lopts;

/* TODO Rather ditto (except for storage -> cmd_ctx), compose hooks */
static struct a_amv_var *a_amv_compose_lopts;

/* Lookup for macros/accounts: if newamp is not NULL it will be linked in the
 * map, if _MF_UNDEF is set a possibly existing entry will be removed (first).
 * Returns NULL if a lookup failed, or if newamp was set, the found entry in
 * plain lookup cases or when _UNDEF was performed on a currently active entry
 * (the entry will have been unlinked, and the _MF_DELETE will be honoured once
 * the reference count reaches 0), and (*)-1 if an _UNDEF was performed */
static struct a_amv_mac *a_amv_mac_lookup(char const *name,
                           struct a_amv_mac *newamp, enum a_amv_mac_flags amf);

/* `call', `call_if' (and `xcall' via go.c -> c_call()) */
static int a_amv_mac_call(void *v, bool_t silent_nexist);

/* Execute a macro; amcap must reside in LOFI memory */
static bool_t a_amv_mac_exec(struct a_amv_mac_call_args *amcap);

static void a_amv_mac__finalize(void *vp);

/* User display helpers */
static bool_t a_amv_mac_show(enum a_amv_mac_flags amf);

/* _def() returns error for faulty definitions and already existing * names,
 * _undef() returns error if a named thing doesn't exist */
static bool_t a_amv_mac_def(char const *name, enum a_amv_mac_flags amf);
static bool_t a_amv_mac_undef(char const *name, enum a_amv_mac_flags amf);

/* */
static void a_amv_mac_free(struct a_amv_mac *amp);

/* Update replay-log */
static void a_amv_lopts_add(struct a_amv_lostack *alp, char const *name,
               struct a_amv_var *oavp);
static void a_amv_lopts_unroll(struct a_amv_var **avpp);

/* Special cased value string allocation */
static char *a_amv_var_copy(char const *str);
static void a_amv_var_free(char *cp);

/* Check for special housekeeping.  _VIP_SET_POST and _VIP_CLEAR do not fail
 * (or propagate errors), _VIP_SET_PRE may and should case abortion */
static bool_t a_amv_var_check_vips(enum a_amv_var_vip_mode avvm,
               enum okeys okey, char const **val);

/* _VF_NUM / _VF_POSNUM */
static bool_t a_amv_var_check_num(char const *val, bool_t posnum);

/* Try to reverse lookup a name to an enum okeys mapping, zeroing avcp.
 * Updates .avc_name and .avc_hash; .avc_map is NULL if none found.
 * We may try_harder to identify name: it may be an extended chain.
 * That test only is actually performed by the latter(, then) */
static bool_t a_amv_var_revlookup(struct a_amv_var_carrier *avcp,
               char const *name, bool_t try_harder);
static bool_t a_amv_var_revlookup_chain(struct a_amv_var_carrier *avcp,
               char const *name);

/* Lookup a variable from .avc_(map|name|hash), return whether it was found.
 * Sets .avc_prime; .avc_var is NULL if not found.
 * Here it is where we care for _I3VAL and _DEFVAL.
 * An _I3VAL will be "consumed" as necessary anyway, but it won't be used to
 * create a new variable if _VLOOK_I3VAL_NONEW is set; if
 * _VLOOK_I3VAL_NONEW_REPORT is set then we set .avc_var to -1 and return true
 * if that was the case, otherwise we'll return FAL0, then! */
static bool_t a_amv_var_lookup(struct a_amv_var_carrier *avcp,
               enum a_amv_var_lookup_flags avlf);

/* Lookup functions for special category variables, _pospar drives all
 * positional parameter etc. special categories */
static char const *a_amv_var_vsc_global(struct a_amv_var_carrier *avcp);
static char const *a_amv_var_vsc_multiplex(struct a_amv_var_carrier *avcp);
static char const *a_amv_var_vsc_pospar(struct a_amv_var_carrier *avcp);

/* Set var from .avc_(map|name|hash), return success */
static bool_t a_amv_var_set(struct a_amv_var_carrier *avcp, char const *value,
               enum a_amv_var_setclr_flags avscf);

static bool_t a_amv_var__putenv(struct a_amv_var_carrier *avcp,
               struct a_amv_var *avp);

/* Clear var from .avc_(map|name|hash); sets .avc_var=NULL, return success */
static bool_t a_amv_var_clear(struct a_amv_var_carrier *avcp,
               enum a_amv_var_setclr_flags avscf);

static bool_t a_amv_var__clearenv(char const *name, struct a_amv_var *avp);

/* List all variables */
static void a_amv_var_show_all(void);

static int a_amv_var__show_cmp(void const *s1, void const *s2);

/* Actually do print one, return number of lines written */
static size_t a_amv_var_show(char const *name, FILE *fp, struct n_string *msgp);

/* Shared c_set() and c_environ():set impl, return success */
static bool_t a_amv_var_c_set(char **ap, enum a_amv_var_setclr_flags avscf);

static struct a_amv_mac *
a_amv_mac_lookup(char const *name, struct a_amv_mac *newamp,
      enum a_amv_mac_flags amf){
   struct a_amv_mac *amp, **ampp;
   ui32_t h;
   enum a_amv_mac_flags save_amf;
   NYD2_IN;

   save_amf = amf;
   amf &= a_AMV_MF_TYPE_MASK;
   h = a_AMV_NAME2HASH(name);
   h = a_AMV_HASH2PRIME(h);
   ampp = &a_amv_macs[h];

   for(amp = *ampp; amp != NULL; ampp = &(*ampp)->am_next, amp = amp->am_next){
      if((amp->am_flags & a_AMV_MF_TYPE_MASK) == amf &&
            !strcmp(amp->am_name, name)){
         if(n_LIKELY((save_amf & a_AMV_MF_UNDEF) == 0))
            goto jleave;

         *ampp = amp->am_next;

         if(amp->am_refcnt > 0){
            amp->am_flags |= a_AMV_MF_DELETE;
            if(n_poption & n_PO_D_V)
               n_err(_("Delayed deletion of currently active %s: %s\n"),
                  (amp->am_flags & a_AMV_MF_ACCOUNT ? "account" : "define"),
                  name);
         }else{
            a_amv_mac_free(amp);
            amp = (struct a_amv_mac*)-1;
         }
         break;
      }
   }

   if(newamp != NULL){
      ampp = &a_amv_macs[h];
      newamp->am_next = *ampp;
      *ampp = newamp;
      amp = NULL;
   }
jleave:
   NYD2_OU;
   return amp;
}

static int
a_amv_mac_call(void *v, bool_t silent_nexist){
   struct a_amv_mac *amp;
   int rv;
   char const *name;
   struct n_cmd_arg_ctx *cacp;
   NYD_IN;

   cacp = v;

   if(cacp->cac_no == 0){
      n_err(_("Synopsis: call(_if)?: name [:<arguments>:]\n"));
      n_pstate_err_no = n_ERR_INVAL;
      rv = 1;
      goto jleave;
   }

   name = cacp->cac_arg->ca_arg.ca_str.s;

   if(n_UNLIKELY(cacp->cac_no > a_AMV_POSPAR_MAX)){
      n_err(_("Too many arguments to macro `call': %s\n"), name);
      n_pstate_err_no = n_ERR_OVERFLOW;
      rv = 1;
   }else if(n_UNLIKELY((amp = a_amv_mac_lookup(name, NULL, a_AMV_MF_NONE)
         ) == NULL)){
      if(!silent_nexist)
         n_err(_("Undefined macro called: %s\n"), n_shexp_quote_cp(name, FAL0));
      n_pstate_err_no = n_ERR_NOENT;
      rv = 1;
   }else{
      char const **argv;
      struct a_amv_mac_call_args *amcap;
      size_t argc;

      argc = cacp->cac_no + 1;
      amcap = n_lofi_alloc(sizeof *amcap + (argc * sizeof *argv));
      argv = (void*)&amcap[1];

      for(argc = 0; (cacp->cac_arg = cacp->cac_arg->ca_next) != NULL; ++argc)
         argv[argc] = cacp->cac_arg->ca_arg.ca_str.s;
      argv[argc] = NULL;

      memset(amcap, 0, sizeof *amcap);
      amcap->amca_name = name;
      amcap->amca_amp = amp;
      if(a_amv_lopts != NULL)
         amcap->amca_loflags = (a_amv_lopts->as_loflags & a_AMV_LF_CALL_MASK
               ) >> a_AMV_LF_CALL_TO_SCOPE_SHIFT;
      if(argc > 0){
         amcap->amca_pospar.app_count = (ui16_t)argc;
         amcap->amca_pospar.app_not_heap = TRU1;
         amcap->amca_pospar.app_dat = argv;
      }

      (void)a_amv_mac_exec(amcap);
      rv = n_pstate_ex_no;
   }
jleave:
   NYD_OU;
   return rv;
}

static bool_t
a_amv_mac_exec(struct a_amv_mac_call_args *amcap){
   struct a_amv_lostack *losp;
   struct a_amv_mac_line **amlp;
   char **args_base, **args;
   struct a_amv_mac *amp;
   bool_t rv;
   NYD2_IN;

   amp = amcap->amca_amp;
   assert(amp != NULL && amp != a_AMV_MACKY_MACK);
   ++amp->am_refcnt;
   /* XXX Unfortunately we yet need to dup the macro lines! :( */
   args_base = args = n_alloc(sizeof(*args) * (amp->am_line_cnt +1));
   for(amlp = amp->am_line_dat; *amlp != NULL; ++amlp)
      *(args++) = sbufdup((*amlp)->aml_dat, (*amlp)->aml_len);
   *args = NULL;

   losp = n_lofi_alloc(sizeof *losp);
   losp->as_global_saved = a_amv_lopts;
   if((losp->as_amcap = amcap)->amca_unroller == NULL){
      losp->as_up = losp->as_global_saved;
      losp->as_lopts = NULL;
   }else{
      losp->as_up = NULL;
      losp->as_lopts = *amcap->amca_unroller;
   }
   losp->as_loflags = amcap->amca_loflags;

   a_amv_lopts = losp;
   if(amcap->amca_hook_pre != NULL)
      n_PS_ROOT_BLOCK((*amcap->amca_hook_pre)(amcap->amca_hook_arg));
   rv = n_go_macro((n_GO_INPUT_NONE |
            (amcap->amca_no_xcall ? n_GO_INPUT_NO_XCALL : 0)),
         amp->am_name, args_base, &a_amv_mac__finalize, losp);
   NYD2_OU;
   return rv;
}

static void
a_amv_mac__finalize(void *vp){
   struct a_amv_mac *amp;
   struct a_amv_mac_call_args *amcap;
   struct a_amv_lostack *losp;
   NYD2_IN;

   losp = vp;
   a_amv_lopts = losp->as_global_saved;

   amcap = losp->as_amcap;

   /* Delete positional parameter stack */
   if(!amcap->amca_pospar.app_not_heap && amcap->amca_pospar.app_maxcount > 0){
      ui16_t i;

      for(i = amcap->amca_pospar.app_maxcount; i-- != 0;)
         n_free(n_UNCONST(amcap->amca_pospar.app_dat[i]));
      n_free(amcap->amca_pospar.app_dat);
   }

   /* `local' variable hashmap.  These have no environment map, never */
   if(amcap->amca_local_vars != NULL){
      struct a_amv_var **avpp_base, **avpp, *avp;

      for(avpp_base = *amcap->amca_local_vars, avpp = &avpp_base[a_AMV_PRIME];
            avpp-- != avpp_base;)
         while((avp = *avpp)){
            assert((avp->av_flags & (a_AMV_VF_NOLOPTS | a_AMV_VF_EXT_LOCAL)) ==
               (a_AMV_VF_NOLOPTS | a_AMV_VF_EXT_LOCAL));
            assert(!(avp->av_flags &
                  ((a_AMV_VF__MASK | a_AMV_VF_EXT__MASK) &
                     ~(a_AMV_VF_NOLOPTS | a_AMV_VF_EXT_LOCAL))));
            *avpp = avp->av_link;
            a_amv_var_free(avp->av_value);
            n_free(avp);
         }
      n_free(avpp_base);
   }

   /* Unroll `localopts', if applicable */
   if(amcap->amca_unroller == NULL){
      if(losp->as_lopts != NULL)
         a_amv_lopts_unroll(&losp->as_lopts);
   }else
      *amcap->amca_unroller = losp->as_lopts;

   if(amcap->amca_ps_hook_mask)
      n_pstate &= ~n_PS_HOOK_MASK;

   if((amp = amcap->amca_amp) != a_AMV_MACKY_MACK && amp != NULL &&
         --amp->am_refcnt == 0 && (amp->am_flags & a_AMV_MF_DELETE))
      a_amv_mac_free(amp);

   n_lofi_free(losp);
   n_lofi_free(amcap);
   NYD2_OU;
}

static bool_t
a_amv_mac_show(enum a_amv_mac_flags amf){
   size_t lc, mc, ti, i;
   char const *typestr;
   FILE *fp;
   bool_t rv;
   NYD2_IN;

   rv = FAL0;

   if((fp = Ftmp(NULL, "deflist", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL){
      n_perr(_("`define' or `account' list: cannot create temporary file"), 0);
      goto jleave;
   }

   amf &= a_AMV_MF_TYPE_MASK;
   typestr = (amf & a_AMV_MF_ACCOUNT) ? "account" : "define";

   for(lc = mc = ti = 0; ti < a_AMV_PRIME; ++ti){
      struct a_amv_mac *amp;

      for(amp = a_amv_macs[ti]; amp != NULL; amp = amp->am_next){
         if((amp->am_flags & a_AMV_MF_TYPE_MASK) == amf){
            struct a_amv_mac_line **amlpp;

            if(++mc > 1){
               putc('\n', fp);
               ++lc;
            }
            ++lc;
            fprintf(fp, "%s %s {\n", typestr, amp->am_name);
            for(amlpp = amp->am_line_dat; *amlpp != NULL; ++lc, ++amlpp){
               for(i = (*amlpp)->aml_prespc; i > 0; --i)
                  putc(' ', fp);
               fputs((*amlpp)->aml_dat, fp);
               putc('\n', fp);
            }
            fputs("}\n", fp);
            ++lc;
         }
      }
   }
   if(mc > 0)
      page_or_print(fp, lc);

   rv = (ferror(fp) == 0);
   Fclose(fp);
jleave:
   NYD2_OU;
   return rv;
}

static bool_t
a_amv_mac_def(char const *name, enum a_amv_mac_flags amf){
   struct str line;
   ui32_t line_cnt, maxlen;
   struct linelist{
      struct linelist *ll_next;
      struct a_amv_mac_line *ll_amlp;
   } *llp, *ll_head, *ll_tail;
   union {size_t s; int i; ui32_t ui; size_t l;} n;
   struct a_amv_mac *amp;
   bool_t rv;
   NYD2_IN;

   memset(&line, 0, sizeof line);
   rv = FAL0;
   amp = NULL;

   /* TODO We should have our input state machine which emits Line events,
    * TODO and hook different consumers dependent on our content, as stated
    * TODO in i think lex_input: like this local macros etc. would become
    * TODO possible from the input side */
   /* Read in the lines which form the macro content */
   for(ll_tail = ll_head = NULL, line_cnt = maxlen = 0;;){
      ui32_t leaspc;
      char *cp;

      n.i = n_go_input(n_GO_INPUT_CTX_DEFAULT | n_GO_INPUT_NL_ESC, n_empty,
            &line.s, &line.l, NULL, NULL);
      if(n.ui == 0)
         continue;
      if(n.i < 0){
         n_err(_("Unterminated %s definition: %s\n"),
            (amf & a_AMV_MF_ACCOUNT ? "account" : "macro"), name);
         goto jerr;
      }

      /* Trim WS, remember amount of leading spaces for display purposes */
      for(cp = line.s, leaspc = 0; n.ui > 0; ++cp, --n.ui)
         if(*cp == '\t')
            leaspc = (leaspc + 8u) & ~7u;
         else if(*cp == ' ')
            ++leaspc;
         else
            break;
      for(; n.ui > 0 && spacechar(cp[n.ui - 1]); --n.ui)
         ;
      if(n.ui == 0)
         continue;

      maxlen = n_MAX(maxlen, n.ui);
      cp[n.ui++] = '\0';

      /* Is is the closing brace? */
      if(*cp == '}')
         break;

      if(n_LIKELY(++line_cnt < UI32_MAX)){
         struct a_amv_mac_line *amlp;

         llp = n_autorec_alloc(sizeof *llp);
         if(ll_head == NULL)
            ll_head = llp;
         else
            ll_tail->ll_next = llp;
         ll_tail = llp;
         llp->ll_next = NULL;
         llp->ll_amlp = amlp = n_alloc(n_VSTRUCT_SIZEOF(struct a_amv_mac_line,
               aml_dat) + n.ui);
         amlp->aml_len = n.ui -1;
         amlp->aml_prespc = leaspc;
         memcpy(amlp->aml_dat, cp, n.ui);
      }else{
         n_err(_("Too much content in %s definition: %s\n"),
            (amf & a_AMV_MF_ACCOUNT ? "account" : "macro"), name);
         goto jerr;
      }
   }

   /* Create the new macro */
   n.s = strlen(name) +1;
   amp = n_alloc(n_VSTRUCT_SIZEOF(struct a_amv_mac, am_name) + n.s);
   memset(amp, 0, n_VSTRUCT_SIZEOF(struct a_amv_mac, am_name));
   amp->am_maxlen = maxlen;
   amp->am_line_cnt = line_cnt;
   amp->am_flags = amf;
   memcpy(amp->am_name, name, n.s);
   /* C99 */{
      struct a_amv_mac_line **amlpp;

      amp->am_line_dat = amlpp = n_alloc(sizeof(*amlpp) * ++line_cnt);
      for(llp = ll_head; llp != NULL; llp = llp->ll_next)
         *amlpp++ = llp->ll_amlp;
      *amlpp = NULL;
   }

   /* Create entry, replace a yet existing one as necessary */
   a_amv_mac_lookup(name, amp, amf | a_AMV_MF_UNDEF);
   rv = TRU1;
jleave:
   if(line.s != NULL)
      n_free(line.s);
   NYD2_OU;
   return rv;

jerr:
   for(llp = ll_head; llp != NULL; llp = llp->ll_next)
      n_free(llp->ll_amlp);
   /*
    * if(amp != NULL){
    *   n_free(amp->am_line_dat);
    *   n_free(amp);
    *}*/
   goto jleave;
}

static bool_t
a_amv_mac_undef(char const *name, enum a_amv_mac_flags amf){
   struct a_amv_mac *amp;
   bool_t rv;
   NYD2_IN;

   rv = TRU1;

   if(n_LIKELY(name[0] != '*' || name[1] != '\0')){
      if((amp = a_amv_mac_lookup(name, NULL, amf | a_AMV_MF_UNDEF)) == NULL){
         n_err(_("%s not defined: %s\n"),
            (amf & a_AMV_MF_ACCOUNT ? "Account" : "Macro"), name);
         rv = FAL0;
      }
   }else{
      struct a_amv_mac **ampp, *lamp;

      for(ampp = a_amv_macs; PTRCMP(ampp, <, &a_amv_macs[n_NELEM(a_amv_macs)]);
            ++ampp)
         for(lamp = NULL, amp = *ampp; amp != NULL;){
            if((amp->am_flags & a_AMV_MF_TYPE_MASK) == amf){
               /* xxx Expensive but rare: be simple */
               a_amv_mac_lookup(amp->am_name, NULL, amf | a_AMV_MF_UNDEF);
               amp = (lamp == NULL) ? *ampp : lamp->am_next;
            }else{
               lamp = amp;
               amp = amp->am_next;
            }
         }
   }
   NYD2_OU;
   return rv;
}

static void
a_amv_mac_free(struct a_amv_mac *amp){
   struct a_amv_mac_line **amlpp;
   NYD2_IN;

   for(amlpp = amp->am_line_dat; *amlpp != NULL; ++amlpp)
      n_free(*amlpp);
   n_free(amp->am_line_dat);
   n_free(amp);
   NYD2_OU;
}

static void
a_amv_lopts_add(struct a_amv_lostack *alp, char const *name,
      struct a_amv_var *oavp){
   struct a_amv_var *avp;
   size_t nl, vl;
   NYD2_IN;

   /* Propagate unrolling up the stack, as necessary */
   assert(alp != NULL);
   for(;;){
      if(alp->as_loflags & a_AMV_LF_SCOPE_MASK)
         break;
      if((alp = alp->as_up) == NULL)
         goto jleave;
   }

   /* Check whether this variable is handled yet XXX Boost: ID check etc.!! */
   for(avp = alp->as_lopts; avp != NULL; avp = avp->av_link)
      if(!strcmp(avp->av_name, name))
         goto jleave;

   nl = strlen(name) +1;
   vl = (oavp != NULL) ? strlen(oavp->av_value) +1 : 0;
   avp = n_calloc(1, n_VSTRUCT_SIZEOF(struct a_amv_var, av_name) + nl + vl);
   avp->av_link = alp->as_lopts;
   alp->as_lopts = avp;
   if(vl != 0){
      avp->av_value = &avp->av_name[nl];
      avp->av_flags = oavp->av_flags;
      memcpy(avp->av_value, oavp->av_value, vl);
   }
   memcpy(avp->av_name, name, nl);
jleave:
   NYD2_OU;
}

static void
a_amv_lopts_unroll(struct a_amv_var **avpp){
   struct a_amv_lostack *save_alp;
   struct a_amv_var *x, *avp;
   NYD2_IN;

   avp = *avpp;
   *avpp = NULL;

   save_alp = a_amv_lopts;
   a_amv_lopts = NULL;
   while(avp != NULL){
      x = avp;
      avp = avp->av_link;
      n_PS_ROOT_BLOCK(n_var_vset(x->av_name, (uintptr_t)x->av_value));
      n_free(x);
   }
   a_amv_lopts = save_alp;
   NYD2_OU;
}

static char *
a_amv_var_copy(char const *str){
   char *news;
   size_t len;
   NYD2_IN;

   if(*str == '\0')
      news = n_UNCONST(n_empty);
   else if(str[1] == '\0'){
      if(str[0] == '1')
         news = n_UNCONST(n_1);
      else if(str[0] == '0')
         news = n_UNCONST(n_0);
      else
         goto jheap;
   }else if(str[2] == '\0' && str[0] == '-' && str[1] == '1')
      news = n_UNCONST(n_m1);
   else{
jheap:
      len = strlen(str) +1;
      news = n_alloc(len);
      memcpy(news, str, len);
   }
   NYD2_OU;
   return news;
}

static void
a_amv_var_free(char *cp){
   NYD2_IN;
   if(cp[0] != '\0' && cp != n_0 && cp != n_1 && cp != n_m1)
      n_free(cp);
   NYD2_OU;
}

static bool_t
a_amv_var_check_vips(enum a_amv_var_vip_mode avvm, enum okeys okey,
      char const **val){
   char const *emsg;
   bool_t ok;
   NYD2_IN;

   ok = TRU1;

   if(avvm == a_AMV_VIP_SET_PRE){
      switch(okey){
      default:
         break;
      case ok_v_charset_7bit:
      case ok_v_charset_8bit:
      case ok_v_charset_unknown_8bit:
      case ok_v_ttycharset:
         if((*val = n_iconv_normalize_name(*val)) == NULL)
            ok = FAL0;
         break;
      case ok_v_customhdr:{
         char const *vp;
         char *buf;
         struct n_header_field *hflp, **hflpp, *hfp;
         NYD_IN;

         buf = savestr(*val);
         hflp = NULL;
         hflpp = &hflp;

         while((vp = n_strsep_esc(&buf, ',', TRU1)) != NULL){
            if(!n_header_add_custom(hflpp, vp, TRU1)){
               emsg = N_("Invalid *customhdr* entry: %s\n");
               ok = FAL0;
               break;
            }
            hflpp = &(*hflpp)->hf_next;
         }

         hflpp = ok ? &n_customhdr_list : &hflp;
         while((hfp = *hflpp) != NULL){
            *hflpp = hfp->hf_next;
            n_free(hfp);
         }
         if(!ok)
            goto jerr;
         n_customhdr_list = hflp;
         }break;
      case ok_v_from:
      case ok_v_sender:{
         struct name *np;

         if((np = lextract(*val, GEXTRA | GFULL)) == NULL){
jefrom:
            emsg = N_("*from* / *sender*: invalid  address(es): %s\n");
            goto jerr;
         }else if(okey == ok_v_sender && np->n_flink != NULL){
            emsg = N_("*sender*: may not contain multiple addresses: %s\n");
            goto jerr;
         }else for(; np != NULL; np = np->n_flink)
            if(is_addr_invalid(np, EACM_STRICT | EACM_NOLOG | EACM_NONAME))
               goto jefrom;
         }break;
      case ok_v_HOME:
         /* Note this gets called from main.c during initialization, and they
          * simply set this to pw_dir as a fallback: don't verify _that_ call.
          * See main.c! */
         if(!(n_pstate & n_PS_ROOT) && !n_is_dir(*val, TRUM1)){
            emsg = N_("$HOME is not a directory or not accessible: %s\n");
            goto jerr;
         }
         break;
      case ok_v_hostname:
      case ok_v_smtp_hostname:
#ifdef mx_HAVE_IDNA
         if(**val != '\0'){
            struct n_string cnv;

            n_string_creat_auto(&cnv);
            if(!n_idna_to_ascii(&cnv, *val, UIZ_MAX)){
               /*n_string_gut(&res);*/
               emsg = N_("*hostname*/*smtp_hostname*: "
                     "IDNA encoding failed: %s\n");
               goto jerr;
            }
            *val = n_string_cp(&cnv);
            /*n_string_drop_ownership(&cnv);*/
         }
#endif
         break;
      case ok_v_quote_chars:{
         char c;
         char const *cp;

         for(cp = *val; (c = *cp++) != '\0';)
            if(!asciichar(c) || blankspacechar(c)){
               ok = FAL0;
               break;
            }
         }break;
      case ok_v_sendcharsets:{
         struct n_string s, *sp = &s;
         char *csv, *cp;

         sp = n_string_creat_auto(sp);
         csv = savestr(*val);

         while((cp = n_strsep(&csv, ',', TRU1)) != NULL){
            if((cp = n_iconv_normalize_name(cp)) == NULL){
               ok = FAL0;
               break;
            }
            if(sp->s_len > 0)
               sp = n_string_push_c(sp, ',');
            sp = n_string_push_cp(sp, cp);
         }

         *val = n_string_cp(sp);
         /* n_string_drop_ownership(sp); */
         }break;
      case ok_v_TMPDIR:
         if(!n_is_dir(*val, TRU1)){
            emsg = N_("$TMPDIR is not a directory or not accessible: %s\n");
            goto jerr;
         }
         break;
      case ok_v_umask:
         if(**val != '\0'){
            ui64_t uib;

            n_idec_ui64_cp(&uib, *val, 0, NULL);
            if(uib & ~0777u){ /* (is valid _VF_POSNUM) */
               emsg = N_("Invalid *umask* setting: %s\n");
               goto jerr;
            }
         }
         break;
      }
   }else if(avvm == a_AMV_VIP_SET_POST){
      switch(okey){
      default:
         break;
      case ok_b_ask:
         ok_bset(asksub);
         break;
      case ok_b_debug:
         n_poption |= n_PO_DEBUG;
         su_log_set_level(su_LOG_DEBUG);
         break;
      case ok_v_HOME:
         /* Invalidate any resolved folder then, too
          * FALLTHRU */
      case ok_v_folder:
         n_PS_ROOT_BLOCK(ok_vclear(folder_resolved));
         break;
      case ok_v_ifs:{
         char *x_b, *x, c;
         char const *cp;

         cp = *val;
         x_b = x = n_autorec_alloc(strlen(cp) +1);
         while((c = *cp++) != '\0')
            if(spacechar(c))
               *x++ = c;
         *x = '\0';
         n_PS_ROOT_BLOCK(ok_vset(ifs_ws, x_b));
         }break;
#ifdef mx_HAVE_SETLOCALE
      case ok_v_LANG:
      case ok_v_LC_ALL:
      case ok_v_LC_CTYPE:
         n_locale_init();
         break;
#endif
      case ok_b_memdebug:
         n_poption |= n_PO_MEMDEBUG;
         break;
      case ok_b_POSIXLY_CORRECT: /* <-> *posix* */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_bset(posix));
         break;
      case ok_b_posix: /* <-> $POSIXLY_CORRECT */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_bset(POSIXLY_CORRECT));
         break;
      case ok_b_skipemptybody:
         n_poption |= n_PO_E_FLAG;
         break;
      case ok_b_typescript_mode:
         ok_bset(colour_disable);
         ok_bset(line_editor_disable);
         if(!(n_psonce & n_PSO_STARTED))
            ok_bset(termcap_disable);
         break;
      case ok_v_umask:
         if(**val != '\0'){
            ui64_t uib;

            n_idec_ui64_cp(&uib, *val, 0, NULL);
            umask((mode_t)uib);
         }
         break;
      case ok_b_verbose:
         n_poption |= (n_poption & n_PO_VERB) ? n_PO_VERBVERB : n_PO_VERB;
         if(!(n_poption & n_PO_DEBUG))
            su_log_set_level(su_LOG_INFO);
         break;
      }
   }else{
      switch(okey){
      default:
         break;
      case ok_b_ask:
         ok_bclear(asksub);
         break;
      case ok_b_debug:
         n_poption &= ~n_PO_DEBUG;
         su_log_set_level((n_poption & n_PO_VERB) ? su_LOG_INFO : n_LOG_LEVEL);
         break;
      case ok_v_customhdr:{
         struct n_header_field *hfp;

         while((hfp = n_customhdr_list) != NULL){
            n_customhdr_list = hfp->hf_next;
            n_free(hfp);
         }
         }break;
      case ok_v_HOME:
         /* Invalidate any resolved folder then, too
          * FALLTHRU */
      case ok_v_folder:
         n_PS_ROOT_BLOCK(ok_vclear(folder_resolved));
         break;
      case ok_b_memdebug:
         n_poption &= ~n_PO_MEMDEBUG;
         break;
      case ok_b_POSIXLY_CORRECT: /* <-> *posix* */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_bclear(posix));
         break;
      case ok_b_posix: /* <-> $POSIXLY_CORRECT */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_bclear(POSIXLY_CORRECT));
         break;
      case ok_b_skipemptybody:
         n_poption &= ~n_PO_E_FLAG;
         break;
      case ok_b_verbose:
         n_poption &= ~(n_PO_VERB | n_PO_VERBVERB);
         if(!(n_poption & n_PO_DEBUG))
            su_log_set_level(n_LOG_LEVEL);
         break;
      }
   }

jleave:
   NYD2_OU;
   return ok;
jerr:
   n_err(V_(emsg), n_shexp_quote_cp(*val, FAL0));
   ok = FAL0;
   goto jleave;
}

static bool_t
a_amv_var_check_num(char const *val, bool_t posnum){
   /* TODO The internal/environment  variables which are num= or posnum= should
    * TODO gain special lookup functions, or the return should be void* and
    * TODO castable to integer; i.e. no more strtoX() should be needed.
    * TODO I.e., the result of this function should instead be stored */
   bool_t rv;
   NYD2_IN;

   rv = TRU1;

   if(*val != '\0'){ /* Would be _VF_NOTEMPTY if not allowed */
      ui64_t uib;
      enum n_idec_state ids;

      ids = n_idec_cp(&uib, val, 0,
            (n_IDEC_MODE_LIMIT_32BIT |
             (posnum ?  n_IDEC_MODE_SIGNED_TYPE : n_IDEC_MODE_NONE)), NULL);
      if((ids & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED)
         rv = FAL0;
      /* TODO Unless we store integers we need to look and forbid, because
       * TODO callee may not be able to swallow, e.g., "-1" */
      if(posnum && (ids & n_IDEC_STATE_SEEN_MINUS))
         rv = FAL0;
   }
   NYD2_OU;
   return rv;
}

static bool_t
a_amv_var_revlookup(struct a_amv_var_carrier *avcp, char const *name,
      bool_t try_harder){
   ui32_t hash, i, j;
   struct a_amv_var_map const *avmp;
   char c;
   NYD2_IN;

   memset(avcp, 0, sizeof *avcp); /* XXX overkill, just set chain */

   /* It may be a special a.k.a. macro-local or one-letter parameter */
   c = name[0];
   if(n_UNLIKELY(digitchar(c))){
      /* (Inline dec. atoi, ugh) */
      for(j = (ui8_t)c - '0', i = 1;; ++i){
         c = name[i];
         if(c == '\0')
            break;
         if(!digitchar(c))
            goto jno_special_param;
         j = j * 10 + (ui8_t)c - '0';
      }
      if(j <= a_AMV_POSPAR_MAX){
         avcp->avc_special_cat = a_AMV_VSC_POSPAR;
         goto jspecial_param;
      }
   }else if(n_UNLIKELY(name[1] == '\0')){
      switch(c){
      case '?':
      case '!':
         avcp->avc_special_cat = a_AMV_VSC_GLOBAL;
         j = (c == '?') ? a_AMV_VST_QM : a_AMV_VST_EM;
         goto jspecial_param;
      case '^':
         goto jmultiplex;
      case '*':
         avcp->avc_special_cat = a_AMV_VSC_POSPAR_ENV;
         j = a_AMV_VST_STAR;
         goto jspecial_param;
      case '@':
         avcp->avc_special_cat = a_AMV_VSC_POSPAR_ENV;
         j = a_AMV_VST_AT;
         goto jspecial_param;
      case '#':
         avcp->avc_special_cat = a_AMV_VSC_POSPAR_ENV;
         j = a_AMV_VST_NOSIGN;
         goto jspecial_param;
      default:
         break;
      }
   }else if(c == '^'){
jmultiplex:
      avcp->avc_special_cat = a_AMV_VSC_MULTIPLEX;
      j = a_AMV_VST_CACC;
      goto jspecial_param;
   }

   /* This is nothing special, but a plain variable */
jno_special_param:
   assert(a_AMV_VSC_NONE == 0);/*avcp->avc_special_cat = a_AMV_VSC_NONE;*/
   avcp->avc_name = name;
   avcp->avc_hash = hash = a_AMV_NAME2HASH(name);

   /* Is it a known okey?  Walk over the hashtable */
   for(i = hash % a_AMV_VAR_REV_PRIME, j = 0; j <= a_AMV_VAR_REV_LONGEST; ++j){
      ui32_t x;

      if((x = a_amv_var_revmap[i]) == a_AMV_VAR_REV_ILL)
         break;

      avmp = &a_amv_var_map[x];
      if(avmp->avm_hash == hash &&
            !strcmp(&a_amv_var_names[avmp->avm_keyoff], name)){
         avcp->avc_map = avmp;
         avcp->avc_okey = (enum okeys)x;
         goto jleave;
      }

      if(++i == a_AMV_VAR_REV_PRIME){
#ifdef a_AMV_VAR_REV_WRAPAROUND
         i = 0;
#else
         break;
#endif
      }
   }

   /* Not a known key, but it may be a chain extension of one.
    * We possibly wanna know for a variety of reasons */
   if(try_harder && a_amv_var_revlookup_chain(avcp, name))
      goto jleave;

   assert(avcp->avc_map == NULL);/*avcp->avc_map = NULL;*/
   avcp = NULL;
jleave:
   assert(avcp == NULL || avcp->avc_map != NULL ||
      avcp->avc_special_cat == a_AMV_VSC_NONE);
   NYD2_OU;
   return (avcp != NULL);

   /* All these are mapped to *--special-param* */
jspecial_param:
   avcp->avc_name = name;
   avcp->avc_special_prop = (ui16_t)j;
   avmp = &a_amv_var_map[a_AMV_VAR__SPECIAL_PARAM_MAP_IDX];
   avcp->avc_hash = avmp->avm_hash;
   avcp->avc_map = avmp;
   avcp->avc_okey = ok_v___special_param;
   goto jleave;
}

static bool_t
a_amv_var_revlookup_chain(struct a_amv_var_carrier *avcp, char const *name){
   uiz_t i;
   struct a_amv_var_chain_map_bsrch const *avcmbp, *avcmbp_x;
   NYD_IN;

   if(strlen(name) <
         n_SIZEOF_FIELD(struct a_amv_var_chain_map_bsrch, avcmb_prefix)){
      avcp = NULL;
      goto jleave;
   }

   avcmbp = &a_amv_var_chain_map_bsrch[0];
   i = a_AMV_VAR_CHAIN_MAP_BSRCH_CNT - 0;
   do{
      int cres;
      avcmbp_x = &avcmbp[i >> 1];
      cres = memcmp(name, avcmbp_x->avcmb_prefix,
            n_SIZEOF_FIELD(struct a_amv_var_chain_map_bsrch, avcmb_prefix));
      if(cres != 0){
         /* Go right instead? */
         if(cres > 0){
            avcmbp = ++avcmbp_x;
            --i;
         }
      }else{
         /* Once the binary search found the right prefix we have to use
          * a linear walk from then on, because there is no "trigger"
          * character: anything could be something free-form or
          * a chain-extension, we just do not know.  Unfortunately.
          * Luckily cramping the walk to a small window is possible */
         struct a_amv_var_chain_map const *avcmp, *avcmp_hit;

         avcmp = &a_amv_var_chain_map[avcmbp_x->avcmb_chain_map_off];
         avcmp_hit = NULL;
         do{
            char c;
            char const *cp, *ncp;

            cp = &a_amv_var_names[avcmp->avcm_keyoff +
                  n_SIZEOF_FIELD(struct a_amv_var_chain_map_bsrch,
                     avcmb_prefix)];
            ncp = &name[n_SIZEOF_FIELD(struct a_amv_var_chain_map_bsrch,
                  avcmb_prefix)];
            for(;; ++ncp, ++cp)
               if(*ncp != (c = *cp) || c == '\0')
                  break;
            /* Is it a chain extension of this key? */
            if(c == '\0' && *ncp == '-')
               avcmp_hit = avcmp;
            else if(avcmp_hit != NULL)
               break;
         }while((avcmp++)->avcm_okey < avcmbp_x->avcmb_chain_map_eokey);

         if(avcmp_hit != NULL){
            avcp->avc_map = &a_amv_var_map[avcp->avc_okey =
                  (enum okeys)avcmp_hit->avcm_okey];
            avcp->avc_is_chain_variant = TRU1;
            goto jleave;
         }
         break;
      }
   }while((i >>= 1) > 0);

   avcp = NULL;
jleave:
   NYD_OU;
   return (avcp != NULL);
}

static bool_t
a_amv_var_lookup(struct a_amv_var_carrier *avcp,
      enum a_amv_var_lookup_flags avlf){
   size_t i;
   char const *cp;
   ui32_t f;
   struct a_amv_var_map const *avmp;
   struct a_amv_var *avp;
   NYD2_IN;

   assert(!(avlf & a_AMV_VLOOK_LOCAL_ONLY) || (avlf & a_AMV_VLOOK_LOCAL));
   assert(!(avlf & a_AMV_VLOOK_I3VAL_NONEW_REPORT) ||
      (avlf & a_AMV_VLOOK_I3VAL_NONEW));

   /* C99 */{
      struct a_amv_var **avpp, *lavp;

      avcp->avc_prime = a_AMV_HASH2PRIME(avcp->avc_hash);

      /* Optionally macro-`local' variables first */
      if(avlf & a_AMV_VLOOK_LOCAL){
         if(a_amv_lopts != NULL &&
               (avpp = *a_amv_lopts->as_amcap->amca_local_vars) != NULL){
            avpp += avcp->avc_prime;

            for(lavp = NULL, avp = *avpp; avp != NULL;
                  lavp = avp, avp = avp->av_link)
               if(!strcmp(avp->av_name, avcp->avc_name)){
                  /* Relink as head, hope it "sorts on usage" over time.
                   * The code relies on this behaviour! */
                  if(lavp != NULL){
                     lavp->av_link = avp->av_link;
                     avp->av_link = *avpp;
                     *avpp = avp;
                  }
                  goto jleave;
               }
         }

         if(avlf & a_AMV_VLOOK_LOCAL_ONLY)
            goto jerr;
      }

      /* Global variable map */
      avpp = &a_amv_vars[avcp->avc_prime];

      for(lavp = NULL, avp = *avpp; avp != NULL; lavp = avp, avp = avp->av_link)
         if(!strcmp(avp->av_name, avcp->avc_name)){
            /* Relink as head, hope it "sorts on usage" over time.
             * The code relies on this behaviour! */
            if(lavp != NULL){
               lavp->av_link = avp->av_link;
               avp->av_link = *avpp;
               *avpp = avp;
            }

            /* If this setting has been established via -S and we still have
             * not reached the _STARTED_CONFIG program state, it may have been
             * an explicit "clearance" that is to be treated as unset.
             * Because that is a special condition that (has been hacked in
             * later and) needs to be encapsulated in lower levels, but not
             * of interest if _set() or _clear() called us */
            switch(avp->av_flags & a_AMV_VF_EXT__FROZEN_MASK){
            case a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET:
               if(!(avlf & a_AMV_VLOOK_I3VAL_NONEW)){
                  avcp->avc_var = avp;
                  avp = NULL;
                  goto j_leave;
               }
               /* FALLTHRU */
            default:
               break;
            }
            goto jleave;
         }
   }

   /* If this is not an assembled variable we need to consider some special
    * initialization cases and eventually create the variable anew */
   if(n_LIKELY((avmp = avcp->avc_map) != NULL)){
      f = avmp->avm_flags;

      /* Does it have an import-from-environment flag? */
      if(n_UNLIKELY((f & (a_AMV_VF_IMPORT | a_AMV_VF_ENV)) != 0)){
         if(n_LIKELY((cp = getenv(avcp->avc_name)) != NULL)){
            /* May be better not to use that one, though? */
            /* TODO Outsource the tests into a _shared_ test function! */
            bool_t isempty, isbltin;

            isempty = (*cp == '\0' && (f & a_AMV_VF_NOTEMPTY) != 0);
            isbltin = ((f & (a_AMV_VF_I3VAL | a_AMV_VF_DEFVAL)) != 0);

            if(n_UNLIKELY(isempty)){
               n_err(_("Environment variable must not be empty: %s\n"),
                  avcp->avc_name);
               if(!isbltin)
                  goto jerr;
            }else if(n_LIKELY(*cp != '\0')){
               if(n_UNLIKELY((f & a_AMV_VF_NUM) &&
                     !a_amv_var_check_num(cp, FAL0))){
                  n_err(_("Environment variable value not a number "
                     "or out of range: %s\n"), avcp->avc_name);
                  goto jerr;
               }
               if(n_UNLIKELY((f & a_AMV_VF_POSNUM) &&
                     !a_amv_var_check_num(cp, TRU1))){
                  n_err(_("Environment variable value not a number, "
                     "negative or out of range: %s\n"), avcp->avc_name);
                  goto jerr;
               }
               goto jnewval;
            }else
               goto jnewval;
         }
      }

      /* A first-time init switch is to be handled now and here */
      if(n_UNLIKELY((f & a_AMV_VF_I3VAL) != 0)){
         static struct a_amv_var_defval const **arr,
            *arr_base[a_AMV_VAR_I3VALS_CNT +1];

         if(arr == NULL){
            arr = &arr_base[0];
            arr[i = a_AMV_VAR_I3VALS_CNT] = NULL;
            while(i-- > 0)
               arr[i] = &a_amv_var_i3vals[i];
         }

         for(i = 0; arr[i] != NULL; ++i)
            if(arr[i]->avdv_okey == avcp->avc_okey){
               cp = (f & a_AMV_VF_BOOL) ? n_1 : arr[i]->avdv_value;
               /* Remove this entry, hope entire block becomes no-op asap */
               do
                  arr[i] = arr[i + 1];
               while(arr[i++] != NULL);

               if(!(avlf & a_AMV_VLOOK_I3VAL_NONEW))
                  goto jnewval;
               if(avlf & a_AMV_VLOOK_I3VAL_NONEW_REPORT)
                  avp = (struct a_amv_var*)-1;
               goto jleave;
            }
      }

      /* */
jdefval:
      if(n_UNLIKELY(f & a_AMV_VF_DEFVAL) != 0){
         for(i = 0; i < a_AMV_VAR_DEFVALS_CNT; ++i)
            if(a_amv_var_defvals[i].avdv_okey == avcp->avc_okey){
               cp = (f & a_AMV_VF_BOOL) ? n_1
                     : a_amv_var_defvals[i].avdv_value;
               goto jnewval;
            }
      }

      /* The virtual variables */
      if(n_UNLIKELY((f & a_AMV_VF_VIRT) != 0)){
         for(i = 0; i < a_AMV_VAR_VIRTS_CNT; ++i)
            if(a_amv_var_virts[i].avv_okey == avcp->avc_okey){
               avp = n_UNCONST(a_amv_var_virts[i].avv_var);
               goto jleave;
            }
         /* Not reached */
      }
   }

jerr:
   avp = NULL;
jleave:
   avcp->avc_var = avp;
j_leave:
   if(!(avlf & a_AMV_VLOOK_I3VAL_NONEW) && (n_poption & n_PO_VERBVERB) &&
         avp != (struct a_amv_var*)-1 && avcp->avc_okey != ok_v_log_prefix){
      /* I18N: Variable "name" is set to "value" */
      n_err(_("*%s* is %s\n"),
         n_shexp_quote_cp(avcp->avc_name, FAL0),
         (avp == NULL ? _("not set")
            : ((avp->av_flags & a_AMV_VF_BOOL) ? _("boolean set")
               : n_shexp_quote_cp(avp->av_value, FAL0))));
   }
   NYD2_OU;
   return (avp != NULL);

jnewval:
   assert(avmp != NULL);
   assert(f == avmp->avm_flags);
   /* E.g., $TMPDIR may be set to non-existent, so we need to be able to catch
    * that and redirect to a possible default value */
   if((f & a_AMV_VF_VIP) &&
         !a_amv_var_check_vips(a_AMV_VIP_SET_PRE, avcp->avc_okey, &cp)){
#ifdef mx_HAVE_SETENV
      if(f & (a_AMV_VF_IMPORT | a_AMV_VF_ENV))
         unsetenv(avcp->avc_name);
#endif
      if(n_UNLIKELY(f & a_AMV_VF_DEFVAL) != 0)
         goto jdefval;
      goto jerr;
   }else{
      struct a_amv_var **avpp;
      size_t l;

      l = strlen(avcp->avc_name) +1;
      avcp->avc_var =
      avp = n_calloc(1, n_VSTRUCT_SIZEOF(struct a_amv_var, av_name) + l);
      avp->av_link = *(avpp = &a_amv_vars[avcp->avc_prime]);
      *avpp = avp;
      assert(!avcp->avc_is_chain_variant);
      avp->av_flags = f;
      avp->av_value = a_amv_var_copy(cp);
      memcpy(avp->av_name, avcp->avc_name, l);

      if(f & a_AMV_VF_ENV)
         a_amv_var__putenv(avcp, avp);
      if(f & a_AMV_VF_VIP)
         a_amv_var_check_vips(a_AMV_VIP_SET_POST, avcp->avc_okey, &cp);
      goto jleave;
   }
}

static char const *
a_amv_var_vsc_global(struct a_amv_var_carrier *avcp){
   char iencbuf[n_IENC_BUFFER_SIZE];
   char const *rv;
   si32_t *ep;
   struct a_amv_var_map const *avmp;
   NYD2_IN;

   /* Not function local, TODO but lazy evaluted for now */
   if(avcp->avc_special_prop == a_AMV_VST_QM){
      avmp = &a_amv_var_map[a_AMV_VAR__QM_MAP_IDX];
      avcp->avc_okey = ok_v___qm;
      ep = &n_pstate_ex_no;
   }else{
      avmp = &a_amv_var_map[a_AMV_VAR__EM_MAP_IDX];
      avcp->avc_okey = ok_v___em;
      ep = &n_pstate_err_no;
   }

   /* XXX Oh heaven, we are responsible to ensure that $?/! is up-to-date
    * TODO we could num=1 ok_v___[qe]m, but the thing is still a string
    * TODO and thus conversion takes places over and over again; also
    * TODO for now that would have to occur before we set _that_ value
    * TODO so let's special treat it until we store ints as such */
   switch(*ep){
   case 0: rv = n_0; break;
   case 1: rv = n_1; break;
   default:
      rv = n_ienc_buf(iencbuf, *ep, 10, n_IENC_MODE_SIGNED_TYPE);
      break;
   }
   n_PS_ROOT_BLOCK(n_var_okset(avcp->avc_okey, (uintptr_t)rv));

   avcp->avc_hash = avmp->avm_hash;
   avcp->avc_map = avmp;
   rv = a_amv_var_lookup(avcp, a_AMV_VLOOK_NONE)
         ? avcp->avc_var->av_value : NULL;
   NYD2_OU;
   return rv;
}

static char const *
a_amv_var_vsc_multiplex(struct a_amv_var_carrier *avcp){
   char iencbuf[n_IENC_BUFFER_SIZE];
   si32_t e;
   size_t i;
   char const *rv;
   NYD2_IN;

   i = strlen(rv = &avcp->avc_name[1]);

   /* ERR, ERRDOC, ERRNAME, plus *-NAME variants */
   if(rv[0] == 'E' && i >= 3 && rv[1] == 'R' && rv[2] == 'R'){
      if(i == 3){
         e = n_pstate_err_no;
         goto jeno;
      }else if(rv[3] == '-'){
         e = n_err_from_name(&rv[4]);
jeno:
         switch(e){
         case 0: rv = n_0; break;
         case 1: rv = n_1; break;
         default:
            /* XXX Need to convert number to string yet */
            rv = savestr(n_ienc_buf(iencbuf, e, 10, n_IENC_MODE_SIGNED_TYPE));
            break;
         }
         goto jleave;
      }else if(i >= 6){
         if(!memcmp(&rv[3], "DOC", 3)){
            rv += 6;
            switch(*rv){
            case '\0': e = n_pstate_err_no; break;
            case '-': e = n_err_from_name(&rv[1]); break;
            default: goto jerr;
            }
            rv = n_err_to_doc(e);
            goto jleave;
         }else if(i >= 7 && !memcmp(&rv[3], "NAME", 4)){
            rv += 7;
            switch(*rv){
            case '\0': e = n_pstate_err_no; break;
            case '-': e = n_err_from_name(&rv[1]); break;
            default: goto jerr;
            }
            rv = n_err_to_name(e);
            goto jleave;
         }
      }
   }

jerr:
   rv = NULL;
jleave:
   NYD2_OU;
   return rv;
}

static char const *
a_amv_var_vsc_pospar(struct a_amv_var_carrier *avcp){
   size_t i, j;
   ui16_t argc;
   char const *rv, **argv;
   NYD2_IN;

   rv = NULL;

   /* If in a macro/xy.. */
   if(a_amv_lopts != NULL){
      bool_t ismacky;
      struct a_amv_mac_call_args *amcap;

      amcap = a_amv_lopts->as_amcap;
      argc = amcap->amca_pospar.app_count;
      argv = amcap->amca_pospar.app_dat;
      argv += amcap->amca_pospar.app_idx;

      /* ..in a `call'ed macro only, to be exact.  Or in a_AMV_MACKY_MACK */
      if(!(ismacky = (amcap->amca_amp == a_AMV_MACKY_MACK)) &&
            (amcap->amca_ps_hook_mask ||
             (assert(amcap->amca_amp != NULL),
              (amcap->amca_amp->am_flags & a_AMV_MF_TYPE_MASK
               ) == a_AMV_MF_ACCOUNT)))
         goto jleave;

      if(avcp->avc_special_cat == a_AMV_VSC_POSPAR){
         if(avcp->avc_special_prop > 0){
            if(argc >= avcp->avc_special_prop)
               rv = argv[avcp->avc_special_prop - 1];
         }else if(ismacky)
            rv = amcap->amca_name;
         else
            rv = (a_amv_lopts->as_up != NULL
                  ? a_amv_lopts->as_up->as_amcap->amca_name : n_empty);
         goto jleave;
      }
      /* MACKY_MACK doesn't know about [*@#] */
      /*else*/ if(ismacky){
         if(n_poption & n_PO_D_V)
            n_err(_("Cannot use $*/$@/$# in this context: %s\n"),
               n_shexp_quote_cp(avcp->avc_name, FAL0));
         goto jleave;
      }
   }else{
      argc = a_amv_pospar.app_count;
      argv = a_amv_pospar.app_dat;
      argv += a_amv_pospar.app_idx;

      if(avcp->avc_special_cat == a_AMV_VSC_POSPAR){
         if(avcp->avc_special_prop > 0){
            if(argc >= avcp->avc_special_prop)
               rv = argv[avcp->avc_special_prop - 1];
         }else
            rv = su_program;
         goto jleave;
      }
   }

   switch(avcp->avc_special_prop){ /* XXX OPTIMIZE */
   case a_AMV_VST_STAR:{
      char sep;

      sep = *ok_vlook(ifs);
      if(0){
   case a_AMV_VST_AT:
         sep = ' ';
      }
      for(i = j = 0; i < argc; ++i)
         j += strlen(argv[i]) + 1;
      if(j == 0)
         rv = n_empty;
      else{
         char *cp;

         rv = cp = n_autorec_alloc(j);
         for(i = j = 0; i < argc; ++i){
            j = strlen(argv[i]);
            memcpy(cp, argv[i], j);
            cp += j;
            if(sep != '\0')
               *cp++ = sep;
         }
         if(sep != '\0')
            --cp;
         *cp = '\0';
      }
      }break;
   case a_AMV_VST_NOSIGN:{
      char iencbuf[n_IENC_BUFFER_SIZE];

      rv = savestr(n_ienc_buf(iencbuf, argc, 10, n_IENC_MODE_NONE));
      }break;
   default:
      rv = n_empty;
      break;
   }
jleave:
   NYD2_OU;
   return rv;
}

static bool_t
a_amv_var_set(struct a_amv_var_carrier *avcp, char const *value,
      enum a_amv_var_setclr_flags avscf){
   struct a_amv_var *avp;
   char *oval;
   struct a_amv_var_map const *avmp;
   bool_t rv;
   NYD2_IN;

   if(value == NULL){
      rv = a_amv_var_clear(avcp, avscf);
      goto jleave;
   }

   if((avmp = avcp->avc_map) != NULL){
      rv = FAL0;

      /* Validity checks */
      if(n_UNLIKELY((avmp->avm_flags & a_AMV_VF_RDONLY) != 0 &&
            !(n_pstate & n_PS_ROOT))){
         value = N_("Variable is read-only: %s\n");
         goto jeavmp;
      }
      if(n_UNLIKELY((avmp->avm_flags & a_AMV_VF_NOTEMPTY) && *value == '\0')){
         value = N_("Variable must not be empty: %s\n");
         goto jeavmp;
      }
      if(n_UNLIKELY((avmp->avm_flags & a_AMV_VF_NUM) &&
            !a_amv_var_check_num(value, FAL0))){
         value = N_("Variable value not a number or out of range: %s\n");
         goto jeavmp;
      }
      if(n_UNLIKELY((avmp->avm_flags & a_AMV_VF_POSNUM) &&
            !a_amv_var_check_num(value, TRU1))){
         value = _("Variable value not a number, negative, "
               "or out of range: %s\n");
         goto jeavmp;
      }

      if(n_UNLIKELY((avmp->avm_flags & a_AMV_VF_IMPORT) != 0 &&
            !(n_psonce & n_PSO_STARTED) && !(n_pstate & n_PS_ROOT))){
         value = N_("Variable cannot be set in a resource file: %s\n");
         goto jeavmp;
      }

      /* Any more complicated inter-dependency? */
      if(n_UNLIKELY((avmp->avm_flags & a_AMV_VF_VIP) != 0 &&
            !a_amv_var_check_vips(a_AMV_VIP_SET_PRE, avcp->avc_okey, &value))){
         value = N_("Assignment of variable aborted: %s\n");
jeavmp:
         n_err(V_(value), avcp->avc_name);
         goto jleave;
      }

      /* Transformations */
      if(n_UNLIKELY(avmp->avm_flags & a_AMV_VF_LOWER)){
         char c;

         oval = savestr(value);
         value = oval;
         for(; (c = *oval) != '\0'; ++oval)
            *oval = lowerconv(c);
      }

      /* Obsoletion warning */
      if(n_UNLIKELY((avmp->avm_flags & a_AMV_VF_OBSOLETE) != 0))
         n_OBSOLETE2(_("variable superseded or obsoleted"), avcp->avc_name);
   }

   /* Lookup possibly existing var.  For */

   rv = TRU1;
   a_amv_var_lookup(avcp, (a_AMV_VLOOK_I3VAL_NONEW |
         ((avscf & a_AMV_VSETCLR_LOCAL)
          ? (a_AMV_VLOOK_LOCAL | a_AMV_VLOOK_LOCAL_ONLY)
          : a_AMV_VLOOK_LOCAL)));
   avp = avcp->avc_var;

   /* A `local' setting is never covered by `localopts' nor frozen */
   if(avscf & a_AMV_VSETCLR_LOCAL)
      goto jislocal;

   /* If this setting had been established via -S and we still have not reached
    * the _STARTED_CONFIG program state, silently ignore request! */
   if(n_UNLIKELY(avp != NULL) &&
         n_UNLIKELY((avp->av_flags & a_AMV_VF_EXT__FROZEN_MASK) != 0)){
      if(!(n_psonce & n_PSO_STARTED_CONFIG)){
         if((n_pstate & n_PS_ROOT) ||
               (!(n_psonce & n_PSO_STARTED_GETOPT) &&
                (n_poption & n_PO_S_FLAG_TEMPORARY)))
            goto joval_and_go;
         if(n_poption & n_PO_D_VV)
            n_err(_("Temporarily frozen by -S, not `set'ing: %s\n"),
               avcp->avc_name);
         goto jleave;
      }

      /* Otherwise, if -S freezing was an `unset' request, be very simple and
       * avoid tampering with that very special case we are not really prepared
       * for just one more line of code: throw the old thing away! */
      if(!(avp->av_flags & a_AMV_VF_EXT_FROZEN_UNSET))
         avp->av_flags &= ~a_AMV_VF_EXT__FROZEN_MASK;
      else{
         assert(avp->av_value == n_empty);
         assert(a_amv_vars[avcp->avc_prime] == avp);
         a_amv_vars[avcp->avc_prime] = avp->av_link;
         n_free(avp);
         avcp->avc_var = avp = NULL;
      }
   }

   /* Optionally cover by `localopts' */
   if(n_UNLIKELY(a_amv_lopts != NULL) &&
         (avmp == NULL || !(avmp->avm_flags & a_AMV_VF_NOLOPTS)))
      a_amv_lopts_add(a_amv_lopts, avcp->avc_name, avcp->avc_var);

jislocal:
   if(avp != NULL)
joval_and_go:
      oval = avp->av_value;
   else{
      size_t l;
      struct a_amv_var **avpp;

      if(avscf & a_AMV_VSETCLR_LOCAL){
         if((avpp = *a_amv_lopts->as_amcap->amca_local_vars) == NULL)
            avpp = *(a_amv_lopts->as_amcap->amca_local_vars =
                  n_calloc(1, sizeof(*a_amv_lopts->as_amcap->amca_local_vars)));
         avpp += avcp->avc_prime;
      }else
         avpp = &a_amv_vars[avcp->avc_prime];

      l = strlen(avcp->avc_name) +1;
      avcp->avc_var = avp = n_calloc(1,
            n_VSTRUCT_SIZEOF(struct a_amv_var, av_name) + l);
      avp->av_link = *avpp;
      *avpp = avp;
      avp->av_flags = (((avscf & a_AMV_VSETCLR_LOCAL)
               ? a_AMV_VF_NOLOPTS | a_AMV_VF_EXT_LOCAL
               : ((avmp != NULL) ? avmp->avm_flags : 0)) |
            (avcp->avc_is_chain_variant ? a_AMV_VF_EXT_CHAIN : a_AMV_VF_NONE));
      memcpy(avp->av_name, avcp->avc_name, l);
      oval = n_UNCONST(n_empty);
   }

   if(avmp == NULL)
      avp->av_value = a_amv_var_copy(value);
   else{
      assert(!(avscf & a_AMV_VSETCLR_LOCAL));
      /* Via `set' etc. the user may give even boolean options non-boolean
       * values, ignore that and force boolean */
      if(!(avp->av_flags & a_AMV_VF_BOOL))
         avp->av_value = a_amv_var_copy(value);
      else{
         if(!(n_pstate & n_PS_ROOT) && (n_poption & n_PO_D_V) &&
               *value != '\0')
            n_err(_("Ignoring value of boolean variable: %s: %s\n"),
               avcp->avc_name, value);
         avp->av_value = n_UNCONST(n_1);
      }
   }

   /* A `local' setting can skip all the crude special things */
   if(!(avscf & a_AMV_VSETCLR_LOCAL)){
      ui32_t f;

      f = avp->av_flags;

      if((avscf & a_AMV_VSETCLR_ENV) && !(f & a_AMV_VF_ENV))
         f |= a_AMV_VF_EXT_LINKED;
      if(f & (a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED))
         rv = a_amv_var__putenv(avcp, avp);
      if(f & a_AMV_VF_VIP)
         a_amv_var_check_vips(a_AMV_VIP_SET_POST, avcp->avc_okey, &value);

      f &= ~a_AMV_VF_EXT__FROZEN_MASK;
      if(!(n_psonce & n_PSO_STARTED_GETOPT) &&
            (n_poption & n_PO_S_FLAG_TEMPORARY) != 0)
         f |= a_AMV_VF_EXT_FROZEN;

      avp->av_flags = f;
   }

   a_amv_var_free(oval);
jleave:
   NYD2_OU;
   return rv;
}

static bool_t
a_amv_var__putenv(struct a_amv_var_carrier *avcp, struct a_amv_var *avp){
#ifndef mx_HAVE_SETENV
   char *cp;
#endif
   bool_t rv;
   NYD2_IN;

#ifdef mx_HAVE_SETENV
   rv = (setenv(avcp->avc_name, avp->av_value, 1) == 0);
#else
   cp = sstrdup(savecatsep(avcp->avc_name, '=', avp->av_value));

   if((rv = (putenv(cp) == 0))){
      char *ocp;

      ocp = avp->av_env;
      avp->av_env = cp;
      cp = ocp;
   }

   if(cp != NULL)
      n_free(cp);
#endif
   NYD2_OU;
   return rv;
}

static bool_t
a_amv_var_clear(struct a_amv_var_carrier *avcp,
      enum a_amv_var_setclr_flags avscf){
   struct a_amv_var **avpp, *avp;
   ui32_t f;
   struct a_amv_var_map const *avmp;
   bool_t rv;
   NYD2_IN;

   rv = FAL0;
   f = 0;

   if(n_LIKELY((avmp = avcp->avc_map) != NULL)){
      if(n_UNLIKELY(((f = avmp->avm_flags) & a_AMV_VF_NODEL) != 0 &&
            !(n_pstate & n_PS_ROOT))){
         n_err(_("Variable may not be unset: %s\n"), avcp->avc_name);
         goto jleave;
      }
      if(n_UNLIKELY((f & a_AMV_VF_VIP) != 0 &&
            !a_amv_var_check_vips(a_AMV_VIP_CLEAR, avcp->avc_okey, NULL))){
         n_err(_("Clearance of variable aborted: %s\n"), avcp->avc_name);
         goto jleave;
      }
   }

   rv = TRU1;

   if(n_UNLIKELY(!a_amv_var_lookup(avcp,
         (((avscf & a_AMV_VSETCLR_LOCAL)
            ? (a_AMV_VLOOK_LOCAL | a_AMV_VLOOK_LOCAL_ONLY)
            : a_AMV_VLOOK_LOCAL) |
          a_AMV_VLOOK_I3VAL_NONEW | a_AMV_VLOOK_I3VAL_NONEW_REPORT)))){
      assert(avcp->avc_var == NULL);
      /* This may be a clearance request from the command line, via -S, and we
       * need to keep track of that!  Unfortunately we are not prepared for
       * this, really, so we need to create a fake entry that is known and
       * handled correctly by the lowermost variable layer!
       * However, all this cannot happen for plain unset of `local' variables */
      if(avscf & a_AMV_VSETCLR_LOCAL)
         goto jleave;
      if(n_UNLIKELY(!(n_psonce & n_PSO_STARTED_GETOPT)) &&
            (n_poption & n_PO_S_FLAG_TEMPORARY)) Jfreeze:{
         size_t l;

         l = strlen(avcp->avc_name) +1;
         avp = n_calloc(1, n_VSTRUCT_SIZEOF(struct a_amv_var, av_name) + l);
         avp->av_link = *(avpp = &a_amv_vars[avcp->avc_prime]);
         *avpp = avp;
         avp->av_value = n_UNCONST(n_empty); /* Sth. covered by _var_free()! */
         assert(f == (avmp != NULL ? avmp->avm_flags : 0));
         avp->av_flags = f | a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET;
         memcpy(avp->av_name, avcp->avc_name, l);

         if((avscf & a_AMV_VSETCLR_ENV) || (f & a_AMV_VF_ENV))
            a_amv_var__clearenv(avcp->avc_name, NULL);
      }else if(avscf & a_AMV_VSETCLR_ENV){
jforce_env:
         if(!(rv = a_amv_var__clearenv(avcp->avc_name, NULL)))
            goto jerr_env_unset;
      }else{
         /* TODO "cannot unset undefined variable" not echoed in "ROBOT" state,
          * TODO should only be like that with "ignerr"! */
jerr_env_unset:
         if(!(n_pstate & (n_PS_ROOT | n_PS_ROBOT)) && (n_poption & n_PO_D_V))
            n_err(_("Cannot unset undefined variable: %s\n"), avcp->avc_name);
      }
      goto jleave;
   }else if((avp = avcp->avc_var) == (struct a_amv_var*)-1){
      /* Clearance request from command line, via -S?  As above.. */
      if(n_UNLIKELY(!(n_psonce & n_PSO_STARTED_GETOPT) &&
            (n_poption & n_PO_S_FLAG_TEMPORARY) != 0))
         goto Jfreeze;
      avcp->avc_var = NULL;
      if(avscf & a_AMV_VSETCLR_ENV)
         goto jforce_env;
      goto jleave;
   }
   assert(avcp->avc_var != NULL);

   /* `local' variables bypass "frozen" checks and `localopts' coverage etc. */
   if((f = avp->av_flags) & a_AMV_VF_EXT_LOCAL)
      goto jdefault_path;

   /* If this setting has been established via -S and we still have not reached
    * the _STARTED_CONFIG program state, silently ignore request!
    * XXX All this is very complicated for the tenth of a second */
   /*else*/ if(n_UNLIKELY((f & a_AMV_VF_EXT__FROZEN_MASK) != 0)){
      if(!(n_psonce & n_PSO_STARTED_CONFIG)){
         if((n_pstate & n_PS_ROOT) ||
               (!(n_psonce & n_PSO_STARTED_GETOPT) &&
                (n_poption & n_PO_S_FLAG_TEMPORARY))){
            /* Be aware this may turn a set into an unset! */
            if(!(f & a_AMV_VF_EXT_FROZEN_UNSET)){
               if(f & a_AMV_VF_DEFVAL)
                  goto jdefault_path;
               a_amv_var_free(avp->av_value);
               f |= a_AMV_VF_EXT_FROZEN_UNSET;
               avp->av_flags = f;
               avp->av_value = n_UNCONST(n_empty); /* _var_free() covered */
               if(f & (a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED))
                  goto jforce_env;
            }
            goto jleave;
         }
         if(n_poption & n_PO_D_VV)
            n_err(_("Temporarily frozen by -S, not `unset'ting: %s\n"),
               avcp->avc_name);
         goto jleave;
      }
      f &= ~a_AMV_VF_EXT__FROZEN_MASK;
      avp->av_flags = f;
   }

   if(n_UNLIKELY(a_amv_lopts != NULL) &&
         (avmp == NULL || !(avmp->avm_flags & a_AMV_VF_NOLOPTS)))
      a_amv_lopts_add(a_amv_lopts, avcp->avc_name, avcp->avc_var);

jdefault_path:
   assert(avp == avcp->avc_var);
   avcp->avc_var = NULL;
   avpp = &(((f = avp->av_flags) & a_AMV_VF_EXT_LOCAL)
         ? *a_amv_lopts->as_amcap->amca_local_vars : a_amv_vars
         )[avcp->avc_prime];
   assert(*avpp == avp); /* (always listhead after lookup()) */
   *avpp = (*avpp)->av_link;

   if(f & (a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED))
      rv = a_amv_var__clearenv(avp->av_name, avp);
   a_amv_var_free(avp->av_value);
   n_free(avp);

   /* XXX Fun part, extremely simple-minded for now: if this variable has
    * XXX a default value, immediately reinstantiate it!  TODO Heh? */
   /* xxx Simply assuming we will never have default values for actual
    * xxx -HOST or -USER@HOST chain extensions */
   if(n_UNLIKELY(avmp != NULL && (avmp->avm_flags & a_AMV_VF_DEFVAL) != 0)){
      a_amv_var_lookup(avcp, a_AMV_VLOOK_I3VAL_NONEW);
      if(n_UNLIKELY(!(n_psonce & n_PSO_STARTED_GETOPT)) &&
            (n_poption & n_PO_S_FLAG_TEMPORARY))
         avcp->avc_var->av_flags |= a_AMV_VF_EXT_FROZEN;
   }
jleave:
   NYD2_OU;
   return rv;
}

static bool_t
a_amv_var__clearenv(char const *name, struct a_amv_var *avp){
   extern char **environ;
   char **ecpp;
   bool_t rv;
   NYD2_IN;
   n_UNUSED(avp);

   rv = FAL0;
   ecpp = environ;

#ifndef mx_HAVE_SETENV
   if(avp != NULL && avp->av_env != NULL){
      for(; *ecpp != NULL; ++ecpp)
         if(*ecpp == avp->av_env){
            do
               ecpp[0] = ecpp[1];
            while(*ecpp++ != NULL);
            n_free(avp->av_env);
            avp->av_env = NULL;
            rv = TRU1;
            break;
         }
   }else
#endif
   {
      size_t l;

      if((l = strlen(name)) > 0){
         for(; *ecpp != NULL; ++ecpp)
            if(!strncmp(*ecpp, name, l) && (*ecpp)[l] == '='){
#ifdef mx_HAVE_SETENV
               unsetenv(name);
#else
               do
                  ecpp[0] = ecpp[1];
               while(*ecpp++ != NULL);
#endif
               rv = TRU1;
               break;
            }
      }
   }
   NYD2_OU;
   return rv;
}

static void
a_amv_var_show_all(void){
   struct n_string msg, *msgp;
   FILE *fp;
   size_t no, i;
   struct a_amv_var *avp;
   char const **vacp, **cap;
   NYD2_IN;

   if((fp = Ftmp(NULL, "setlist", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("`set' list: cannot create temporary file"), 0);
      goto jleave;
   }

   /* We need to instantiate first-time-inits and default values here, so that
    * they will be regular members of our _vars[] table */
   for(i = a_AMV_VAR_I3VALS_CNT; i-- > 0;)
      n_var_oklook(a_amv_var_i3vals[i].avdv_okey);
   for(i = a_AMV_VAR_DEFVALS_CNT; i-- > 0;)
      n_var_oklook(a_amv_var_defvals[i].avdv_okey);

   for(no = i = 0; i < a_AMV_PRIME; ++i)
      for(avp = a_amv_vars[i]; avp != NULL; avp = avp->av_link)
         ++no;
   no += a_AMV_VAR_VIRTS_CNT;

   vacp = n_autorec_alloc(no * sizeof(*vacp));

   for(cap = vacp, i = 0; i < a_AMV_PRIME; ++i)
      for(avp = a_amv_vars[i]; avp != NULL; avp = avp->av_link)
         *cap++ = avp->av_name;
   for(i = a_AMV_VAR_VIRTS_CNT; i-- > 0;)
      *cap++ = a_amv_var_virts[i].avv_var->av_name;

   if(no > 1)
      qsort(vacp, no, sizeof *vacp, &a_amv_var__show_cmp);

   msgp = &msg;
   msgp = n_string_reserve(n_string_creat(msgp), 80);
   for(i = 0, cap = vacp; no != 0; ++cap, --no)
      i += a_amv_var_show(*cap, fp, msgp);
   n_string_gut(&msg);

   page_or_print(fp, i);
   Fclose(fp);
jleave:
   NYD2_OU;
}

static int
a_amv_var__show_cmp(void const *s1, void const *s2){
   int rv;
   NYD2_IN;

   rv = strcmp(*(char**)n_UNCONST(s1), *(char**)n_UNCONST(s2));
   NYD2_OU;
   return rv;
}

static size_t
a_amv_var_show(char const *name, FILE *fp, struct n_string *msgp){
   /* XXX a_amv_var_show(): if we iterate over all the actually set variables
    * XXX via a_amv_var_show_all() there is no need to call
    * XXX a_amv_var_revlookup() at all!  Revisit this call chain */
   struct a_amv_var_carrier avc;
   char const *quote;
   struct a_amv_var *avp;
   bool_t isset;
   size_t i;
   NYD2_IN;

   msgp = n_string_trunc(msgp, 0);
   i = 0;

   a_amv_var_revlookup(&avc, name, TRU1);
   isset = a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE);
   avp = avc.avc_var;

   if(n_poption & n_PO_D_V){
      if(avc.avc_map == NULL){
         msgp = n_string_push_cp(msgp, "#assembled variable with value");
         i = 1;
      }else{
         struct{
            ui16_t flag;
            char msg[22];
         } const tbase[] = {
            {a_AMV_VF_CHAIN, "variable chain"},
            {a_AMV_VF_VIRT, "virtual"},
            {a_AMV_VF_RDONLY, "read-only"},
            {a_AMV_VF_NODEL, "nodelete"},
            {a_AMV_VF_I3VAL, "initial-value"},
            {a_AMV_VF_DEFVAL, "default-value"},
            {a_AMV_VF_IMPORT, "import-environ-first\0"}, /* assert NUL in max */
            {a_AMV_VF_ENV, "sync-environ"},
            {a_AMV_VF_NOLOPTS, "no-localopts"},
            {a_AMV_VF_NOTEMPTY, "notempty"},
            {a_AMV_VF_NUM, "number"},
            {a_AMV_VF_POSNUM, "positive-number"},
            {a_AMV_VF_OBSOLETE, "obsoleted"},
         }, *tp;
         assert(!isset || ((avp->av_flags & a_AMV_VF__MASK) ==
            (avc.avc_map->avm_flags & a_AMV_VF__MASK)));

         for(tp = tbase; PTRCMP(tp, <, &tbase[n_NELEM(tbase)]); ++tp)
            if(isset ? (avp->av_flags & tp->flag)
                  : (avc.avc_map->avm_flags & tp->flag)){
               msgp = n_string_push_c(msgp, (i++ == 0 ? '#' : ','));
               msgp = n_string_push_cp(msgp, tp->msg);
               if(isset){
                  if((tp->flag == a_AMV_VF_CHAIN) &&
                        (avp->av_flags & a_AMV_VF_EXT_CHAIN))
                     msgp = n_string_push_cp(msgp, " (extension)");
               }
            }
      }

      if(isset){
         if(avp->av_flags & a_AMV_VF_EXT_FROZEN){
            msgp = n_string_push_c(msgp, (i++ == 0 ? '#' : ','));
            msgp = n_string_push_cp(msgp, "(un)?set via -S");
         }
      }

      if(i > 0)
         msgp = n_string_push_cp(msgp, "\n  ");
   }

   /* (Read-only variables are generally shown via comments..) */
   if(!isset || (avp->av_flags & a_AMV_VF_RDONLY)){
      msgp = n_string_push_c(msgp, n_ns[0]);
      if(!isset){
         if(avc.avc_map != NULL && (avc.avc_map->avm_flags & a_AMV_VF_BOOL))
            msgp = n_string_push_cp(msgp, "boolean; ");
         msgp = n_string_push_cp(msgp, "variable not set: ");
         msgp = n_string_push_cp(msgp, n_shexp_quote_cp(name, FAL0));
         goto jleave;
      }
   }

   n_UNINIT(quote, NULL);
   if(!(avp->av_flags & a_AMV_VF_BOOL)){
      quote = n_shexp_quote_cp(avp->av_value, TRU1);
      if(strcmp(quote, avp->av_value))
         msgp = n_string_push_cp(msgp, "wysh ");
   }else if(n_poption & n_PO_D_V)
      msgp = n_string_push_cp(msgp, "wysh "); /* (for shell-style comment) */

   if(avp->av_flags & a_AMV_VF_EXT_LINKED)
      msgp = n_string_push_cp(msgp, "environ ");
   msgp = n_string_push_cp(msgp, "set ");
   msgp = n_string_push_cp(msgp, name);

   if(!(avp->av_flags & a_AMV_VF_BOOL)){
      msgp = n_string_push_c(msgp, '=');
      msgp = n_string_push_cp(msgp, quote);
   }else if(n_poption & n_PO_D_V)
      msgp = n_string_push_cp(msgp, " #boolean");

jleave:
   msgp = n_string_push_c(msgp, '\n');
   fputs(n_string_cp(msgp), fp);
   NYD2_IN;
   return (i > 0 ? 2 : 1);
}

static bool_t
a_amv_var_c_set(char **ap, enum a_amv_var_setclr_flags avscf){
   char *cp, *cp2, *varbuf, c;
   size_t errs;
   NYD2_IN;

   errs = 0;
jouter:
   while((cp = *ap++) != NULL){
      /* Isolate key */
      cp2 = varbuf = n_autorec_alloc(strlen(cp) +1);

      for(; (c = *cp) != '=' && c != '\0'; ++cp){
         if(cntrlchar(c) || spacechar(c)){
            n_err(_("Variable name with control or space character ignored: "
               "%s\n"), ap[-1]);
            ++errs;
            goto jouter;
         }
         *cp2++ = c;
      }
      *cp2 = '\0';
      if(c == '\0')
         cp = n_UNCONST(n_empty);
      else
         ++cp;

      if(varbuf == cp2){
         n_err(_("Empty variable name ignored\n"));
         ++errs;
      }else{
         struct a_amv_var_carrier avc;
         bool_t isunset;

         if((isunset = (varbuf[0] == 'n' && varbuf[1] == 'o')))
            varbuf = &varbuf[2];

         a_amv_var_revlookup(&avc, varbuf, TRU1);

         if((avscf & a_AMV_VSETCLR_LOCAL) && avc.avc_map != NULL){
            if(n_poption & n_PO_D_V)
               n_err(_("Builtin variable not overwritten by `local': %s\n"),
                  varbuf);
            ++errs;
         }else if(isunset)
            errs += !a_amv_var_clear(&avc, avscf);
         else
            errs += !a_amv_var_set(&avc, cp, avscf);
      }
   }
   NYD2_OU;
   return (errs == 0);
}

FL int
c_define(void *v){
   int rv;
   char **args;
   NYD_IN;

   rv = 1;

   if((args = v)[0] == NULL){
      rv = (a_amv_mac_show(a_AMV_MF_NONE) == FAL0);
      goto jleave;
   }

   if(args[1] == NULL || args[1][0] != '{' || args[1][1] != '\0' ||
         args[2] != NULL){
      n_err(_("Synopsis: define: <name> {\n"));
      goto jleave;
   }

   rv = (a_amv_mac_def(args[0], a_AMV_MF_NONE) == FAL0);
jleave:
   NYD_OU;
   return rv;
}

FL int
c_undefine(void *v){
   int rv;
   char **args;
   NYD_IN;

   rv = 0;
   args = v;
   do
      rv |= !a_amv_mac_undef(*args, a_AMV_MF_NONE);
   while(*++args != NULL);
   NYD_OU;
   return rv;
}

FL int
c_call(void *vp){
   int rv;
   NYD_IN;

   rv = a_amv_mac_call(vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_call_if(void *vp){
   int rv;
   NYD_IN;

   rv = a_amv_mac_call(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_account(void *v){
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   char **args;
   int rv, i, oqf, nqf;
   NYD_IN;

   rv = 1;

   if((args = v)[0] == NULL){
      rv = (a_amv_mac_show(a_AMV_MF_ACCOUNT) == FAL0);
      goto jleave;
   }

   if(args[1] && args[1][0] == '{' && args[1][1] == '\0'){
      if(args[2] != NULL){
         n_err(_("Synopsis: account: <name> {\n"));
         goto jleave;
      }
      if(!asccasecmp(args[0], ACCOUNT_NULL)){
         n_err(_("`account': cannot use reserved name: %s\n"),
            ACCOUNT_NULL);
         goto jleave;
      }
      rv = (a_amv_mac_def(args[0], a_AMV_MF_ACCOUNT) == FAL0);
      goto jleave;
   }

   if(n_pstate & n_PS_HOOK_MASK){
      n_err(_("`account': cannot change account from within a hook\n"));
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   amp = NULL;
   if(asccasecmp(args[0], ACCOUNT_NULL) != 0 &&
         (amp = a_amv_mac_lookup(args[0], NULL, a_AMV_MF_ACCOUNT)) == NULL){
      n_err(_("`account': account does not exist: %s\n"), args[0]);
      goto jleave;
   }

   oqf = savequitflags();

   /* Shutdown the active account */
   if(a_amv_acc_curr != NULL){
      char const *cp;
      char *var;

      /* Is there a cleanup hook? */
      var = savecat("on-account-cleanup-", a_amv_acc_curr->am_name);
      if((cp = n_var_vlook(var, FAL0)) != NULL ||
            (cp = ok_vlook(on_account_cleanup)) != NULL){
         struct a_amv_mac *amphook;

         if((amphook = a_amv_mac_lookup(cp, NULL, a_AMV_MF_NONE)) != NULL){
            amcap = n_lofi_alloc(sizeof *amcap);
            memset(amcap, 0, sizeof *amcap);
            amcap->amca_name = cp;
            amcap->amca_amp = amphook;
            amcap->amca_unroller = &a_amv_acc_curr->am_lopts;
            amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
            amcap->amca_no_xcall = TRU1;
            n_pstate |= n_PS_HOOK;
            rv = a_amv_mac_exec(amcap);
            n_pstate &= ~n_PS_HOOK_MASK;
         }else
            n_err(_("*on-account-leave* hook %s does not exist\n"),
               n_shexp_quote_cp(cp, FAL0));
      }

      /* `localopts'? */
      if(a_amv_acc_curr->am_lopts != NULL)
         a_amv_lopts_unroll(&a_amv_acc_curr->am_lopts);

      /* For accounts this lingers */
      --a_amv_acc_curr->am_refcnt;
      if(a_amv_acc_curr->am_flags & a_AMV_MF_DELETE)
         a_amv_mac_free(a_amv_acc_curr);
   }

   a_amv_acc_curr = amp;

   /* And switch to any non-"null" account */
   if(amp != NULL){
      assert(amp->am_lopts == NULL);
      amcap = n_lofi_alloc(sizeof *amcap);
      memset(amcap, 0, sizeof *amcap);
      amcap->amca_name = amp->am_name;
      amcap->amca_amp = amp;
      amcap->amca_unroller = &amp->am_lopts;
      amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
      amcap->amca_no_xcall = TRU1;
      ++amp->am_refcnt; /* We may not run 0 to avoid being deleted! */
      if(!a_amv_mac_exec(amcap)){
         /* XXX account switch incomplete, unroll? */
         n_err(_("`account': failed to switch to account: %s\n"), amp->am_name);
         goto jleave;
      }
   }

   n_PS_ROOT_BLOCK((amp != NULL ? ok_vset(account, amp->am_name)
      : ok_vclear(account)));

   /* Otherwise likely initial setfile() in a_main_rcv_mode() will pick up */
   if(n_psonce & n_PSO_STARTED){
      assert(!(n_pstate & n_PS_HOOK_MASK));
      nqf = savequitflags(); /* TODO obsolete (leave -> void -> new box!) */
      restorequitflags(oqf);
      i = setfile("%", FEDIT_SYSBOX | FEDIT_ACCOUNT);
      restorequitflags(nqf);
      if(i < 0)
         goto jleave;
      temporary_folder_hook_check(FAL0);
      if(i != 0 && !ok_blook(emptystart)) /* Avoid annoying "double message" */
         goto jleave;
      n_folder_announce(n_ANNOUNCE_CHANGE);
   }
   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

FL int
c_unaccount(void *v){
   int rv;
   char **args;
   NYD_IN;

   rv = 0;
   args = v;
   do
      rv |= !a_amv_mac_undef(*args, a_AMV_MF_ACCOUNT);
   while(*++args != NULL);
   NYD_OU;
   return rv;
}

FL int
c_localopts(void *vp){
   enum a_amv_loflags alf, alm;
   char const **argv;
   int rv;
   NYD_IN;

   rv = 1;

   if(a_amv_lopts == NULL){
      n_err(_("Cannot use `localopts' in this context\n"));
      goto jleave;
   }

   if((argv = vp)[1] == NULL || is_asccaseprefix((++argv)[-1], "scope"))
      alf = alm = a_AMV_LF_SCOPE;
   else if(is_asccaseprefix(argv[-1], "call"))
      alf = a_AMV_LF_CALL, alm = a_AMV_LF_CALL_MASK;
   else if(is_asccaseprefix(argv[-1], "call-fixate"))
      alf = a_AMV_LF_CALL_FIXATE, alm = a_AMV_LF_CALL_MASK;
   else{
jesynopsis:
      n_err(_("Synopsis: localopts: [<scope|call|call-fixate>] <boolean>\n"));
      goto jleave;
   }

   if(alf == a_AMV_LF_SCOPE &&
         (a_amv_lopts->as_loflags & a_AMV_LF_SCOPE_FIXATE)){
      if(n_poption & n_PO_D_V)
         n_err(_("Cannot turn off `localopts', setting is fixated\n"));
      goto jleave;
   }

   if((rv = n_boolify(*argv, UIZ_MAX, FAL0)) < FAL0)
      goto jesynopsis;
   a_amv_lopts->as_loflags &= ~alm;
   if(rv > FAL0)
      a_amv_lopts->as_loflags |= alf;
   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

FL int
c_shift(void *vp){ /* xxx move to bottom, not in macro part! */
   struct a_amv_pospar *appp;
   ui16_t i;
   int rv;
   NYD_IN;

   rv = 1;

   if((vp = *(char**)vp) == NULL)
      i = 1;
   else{
      si16_t sib;

      if((n_idec_si16_cp(&sib, vp, 10, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || sib < 0){
         n_err(_("`shift': invalid argument: %s\n"), vp);
         goto jleave;
      }
      i = (ui16_t)sib;
   }

   /* If in in a macro/xy */
   if(a_amv_lopts != NULL){
      struct a_amv_mac const *amp;
      struct a_amv_mac_call_args *amcap;

      /* Explicitly do allow `vpospar' created things! */
      amp = (amcap = a_amv_lopts->as_amcap)->amca_amp;
      if((amp == NULL || amcap->amca_ps_hook_mask ||
               (amp->am_flags & a_AMV_MF_TYPE_MASK) == a_AMV_MF_ACCOUNT) &&
            amcap->amca_pospar.app_not_heap){
         n_err(_("Cannot use `shift' in `account's or hook macros etc.\n"));
         goto jleave;
      }
      appp = &amcap->amca_pospar;
   }else
      appp = &a_amv_pospar;

   if(i > appp->app_count){
      n_err(_("`shift': cannot shift %hu of %hu parameters\n"),
         i, appp->app_count);
      goto jleave;
   }else{
      appp->app_idx += i;
      appp->app_count -= i;
      rv = 0;
   }
jleave:
   NYD_OU;
   return rv;
}

FL int
c_return(void *vp){ /* TODO the exit status should be m_si64! */
   int rv;
   NYD_IN;

   if(a_amv_lopts != NULL){
      char const **argv;

      n_go_input_force_eof();
      n_pstate_err_no = n_ERR_NONE;
      rv = 0;

      if((argv = vp)[0] != NULL){
         si32_t i;

         if((n_idec_si32_cp(&i, argv[0], 10, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) == n_IDEC_STATE_CONSUMED && i >= 0)
            rv = (int)i;
         else{
            n_err(_("`return': return value argument is invalid: %s\n"),
               argv[0]);
            n_pstate_err_no = n_ERR_INVAL;
            rv = 1;
         }

         if(argv[1] != NULL){
            if((n_idec_si32_cp(&i, argv[1], 10, NULL
                     ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
                  ) == n_IDEC_STATE_CONSUMED && i >= 0)
               n_pstate_err_no = i;
            else{
               n_err(_("`return': error number argument is invalid: %s\n"),
                  argv[1]);
               n_pstate_err_no = n_ERR_INVAL;
               rv = 1;
            }
         }
      }
   }else{
      n_err(_("Can only use `return' in a macro\n"));
      n_pstate_err_no = n_ERR_OPNOTSUPP;
      rv = 1;
   }
   NYD_OU;
   return rv;
}

FL bool_t
temporary_folder_hook_check(bool_t nmail){ /* TODO temporary, v15: drop */
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   size_t len;
   char const *cp;
   char *var;
   bool_t rv;
   NYD_IN;

   rv = TRU1;
   var = n_autorec_alloc(len = strlen(mailname) + sizeof("folder-hook-") -1 +1);

   /* First try the fully resolved path */
   snprintf(var, len, "folder-hook-%s", mailname);
   if((cp = n_var_vlook(var, FAL0)) != NULL)
      goto jmac;

   /* If we are under *folder*, try the usual +NAME syntax, too */
   if(displayname[0] == '+'){
      char *x;

      for(x = &mailname[len]; x != mailname; --x)
         if(x[-1] == '/'){
            snprintf(var, len, "folder-hook-+%s", x);
            if((cp = n_var_vlook(var, FAL0)) != NULL)
               goto jmac;
            break;
         }
   }

   /* Plain *folder-hook* is our last try */
   if((cp = ok_vlook(folder_hook)) == NULL)
      goto jleave;

jmac:
   if((amp = a_amv_mac_lookup(cp, NULL, a_AMV_MF_NONE)) == NULL){
      n_err(_("Cannot call *folder-hook* for %s: macro does not exist: %s\n"),
         n_shexp_quote_cp(displayname, FAL0), cp);
      rv = FAL0;
      goto jleave;
   }

   amcap = n_lofi_alloc(sizeof *amcap);
   memset(amcap, 0, sizeof *amcap);
   amcap->amca_name = cp;
   amcap->amca_amp = amp;
   n_pstate &= ~n_PS_HOOK_MASK;
   if(nmail){
      amcap->amca_unroller = NULL;
      n_pstate |= n_PS_HOOK_NEWMAIL;
   }else{
      amcap->amca_unroller = &a_amv_folder_hook_lopts;
      n_pstate |= n_PS_HOOK;
   }
   amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
   amcap->amca_ps_hook_mask = TRU1;
   amcap->amca_no_xcall = TRU1;
   rv = a_amv_mac_exec(amcap);
   n_pstate &= ~n_PS_HOOK_MASK;

jleave:
   NYD_OU;
   return rv;
}

FL void
temporary_folder_hook_unroll(void){ /* XXX intermediate hack */
   NYD_IN;
   if(a_amv_folder_hook_lopts != NULL){
      void *save = a_amv_lopts;

      a_amv_lopts = NULL;
      a_amv_lopts_unroll(&a_amv_folder_hook_lopts);
      assert(a_amv_folder_hook_lopts == NULL);
      a_amv_lopts = save;
   }
   NYD_OU;
}

FL void
temporary_compose_mode_hook_call(char const *macname,
      void (*hook_pre)(void *), void *hook_arg){
   /* TODO compose_mode_hook_call() temporary, v15: generalize; see a_GO_SPLICE
    * TODO comment in go.c for the right way of doing things! */
   static struct a_amv_lostack *cmh_losp;
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   NYD_IN;

   amp = NULL;

   if(macname == (char*)-1){
      a_amv_mac__finalize(cmh_losp);
      cmh_losp = NULL;
   }else if(macname != NULL &&
         (amp = a_amv_mac_lookup(macname, NULL, a_AMV_MF_NONE)) == NULL)
      n_err(_("Cannot call *on-compose-**: macro does not exist: %s\n"),
         macname);
   else{
      amcap = n_lofi_alloc(sizeof *amcap);
      memset(amcap, 0, sizeof *amcap);
      amcap->amca_name = (macname != NULL) ? macname
            : "*on-compose-splice(-shell)?*";
      amcap->amca_amp = amp;
      amcap->amca_unroller = &a_amv_compose_lopts;
      amcap->amca_hook_pre = hook_pre;
      amcap->amca_hook_arg = hook_arg;
      amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
      amcap->amca_ps_hook_mask = TRU1;
      amcap->amca_no_xcall = TRU1;
      n_pstate &= ~n_PS_HOOK_MASK;
      n_pstate |= n_PS_HOOK;
      if(macname != NULL)
         a_amv_mac_exec(amcap);
      else{
         cmh_losp = n_lofi_alloc(sizeof *cmh_losp);
         memset(cmh_losp, 0, sizeof *cmh_losp);
         cmh_losp->as_global_saved = a_amv_lopts;
         cmh_losp->as_lopts = *(cmh_losp->as_amcap = amcap)->amca_unroller;
         cmh_losp->as_loflags = a_AMV_LF_SCOPE_FIXATE;
         a_amv_lopts = cmh_losp;
      }
   }
   NYD_OU;
}

FL void
temporary_compose_mode_hook_unroll(void){ /* XXX intermediate hack */
   NYD_IN;
   if(a_amv_compose_lopts != NULL){
      void *save = a_amv_lopts;

      a_amv_lopts = NULL;
      a_amv_lopts_unroll(&a_amv_compose_lopts);
      assert(a_amv_compose_lopts == NULL);
      a_amv_lopts = save;
   }
   NYD_OU;
}

#ifdef mx_HAVE_HISTORY
FL bool_t
temporary_addhist_hook(char const *ctx, bool_t gabby, char const *histent){
   /* XXX temporary_addhist_hook(): intermediate hack */
   struct a_amv_mac_call_args *amcap;
   si32_t perrn, pexn;
   struct a_amv_mac *amp;
   char const *macname, *argv[4];
   bool_t rv;
   NYD_IN;

   if((macname = ok_vlook(on_history_addition)) == NULL)
      rv = TRUM1;
   else if((amp = a_amv_mac_lookup(macname, NULL, a_AMV_MF_NONE)) == NULL){
      n_err(_("Cannot call *on-history-addition*: macro does not exist: %s\n"),
         macname);
      rv = TRUM1;
   }else{
      perrn = n_pstate_err_no;
      pexn = n_pstate_ex_no;

      argv[0] = ctx;
      argv[1] = gabby ? n_1 : n_0;
      argv[2] = histent;
      argv[3] = NULL;

      amcap = n_lofi_alloc(sizeof *amcap);
      memset(amcap, 0, sizeof *amcap);
      amcap->amca_name = macname;
      amcap->amca_amp = amp;
      amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
      amcap->amca_no_xcall = TRU1;
      amcap->amca_pospar.app_count = 3;
      amcap->amca_pospar.app_not_heap = TRU1;
      amcap->amca_pospar.app_dat = argv;
      if(!a_amv_mac_exec(amcap))
         rv = TRUM1;
      else
         rv = (n_pstate_ex_no == 0);

      n_pstate_err_no = perrn;
      n_pstate_ex_no =  pexn;
   }
   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_HISTORY */

FL bool_t
n_var_is_user_writable(char const *name){
   struct a_amv_var_carrier avc;
   struct a_amv_var_map const *avmp;
   bool_t rv;
   NYD_IN;

   a_amv_var_revlookup(&avc, name, TRU1);
   if((avmp = avc.avc_map) == NULL)
      rv = TRU1;
   else
      rv = ((avmp->avm_flags & (a_AMV_VF_BOOL | a_AMV_VF_RDONLY)) == 0);
   NYD_OU;
   return rv;
}

FL char *
n_var_oklook(enum okeys okey){
   struct a_amv_var_carrier avc;
   char *rv;
   struct a_amv_var_map const *avmp;
   NYD_IN;

   memset(&avc, 0, sizeof avc);
   avc.avc_map = avmp = &a_amv_var_map[okey];
   avc.avc_name = &a_amv_var_names[avmp->avm_keyoff];
   avc.avc_hash = avmp->avm_hash;
   avc.avc_okey = okey;

   if(a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE))
      rv = avc.avc_var->av_value;
   else
      rv = NULL;
   NYD_OU;
   return rv;
}

FL bool_t
n_var_okset(enum okeys okey, uintptr_t val){
   struct a_amv_var_carrier avc;
   bool_t ok;
   struct a_amv_var_map const *avmp;
   NYD_IN;

   memset(&avc, 0, sizeof avc);
   avc.avc_map = avmp = &a_amv_var_map[okey];
   avc.avc_name = &a_amv_var_names[avmp->avm_keyoff];
   avc.avc_hash = avmp->avm_hash;
   avc.avc_okey = okey;

   ok = a_amv_var_set(&avc, (val == 0x1 ? n_empty : (char const*)val),
         a_AMV_VSETCLR_NONE);
   NYD_OU;
   return ok;
}

FL bool_t
n_var_okclear(enum okeys okey){
   struct a_amv_var_carrier avc;
   bool_t rv;
   struct a_amv_var_map const *avmp;
   NYD_IN;

   memset(&avc, 0, sizeof avc);
   avc.avc_map = avmp = &a_amv_var_map[okey];
   avc.avc_name = &a_amv_var_names[avmp->avm_keyoff];
   avc.avc_hash = avmp->avm_hash;
   avc.avc_okey = okey;

   rv = a_amv_var_clear(&avc, a_AMV_VSETCLR_NONE);
   NYD_OU;
   return rv;
}

FL char const *
n_var_vlook(char const *vokey, bool_t try_getenv){
   struct a_amv_var_carrier avc;
   char const *rv;
   NYD_IN;

   a_amv_var_revlookup(&avc, vokey, FAL0);

   switch((enum a_amv_var_special_category)avc.avc_special_cat){
   default: /* silence CC */
   case a_AMV_VSC_NONE:
      rv = NULL;
      if(a_amv_var_lookup(&avc, a_AMV_VLOOK_LOCAL))
         rv = avc.avc_var->av_value;
      /* Only check the environment for something that is otherwise unknown */
      else if(try_getenv && avc.avc_map == NULL &&
            !a_amv_var_revlookup_chain(&avc, vokey))
         rv = getenv(vokey);
      break;
   case a_AMV_VSC_GLOBAL:
      rv = a_amv_var_vsc_global(&avc);
      break;
   case a_AMV_VSC_MULTIPLEX:
      rv = a_amv_var_vsc_multiplex(&avc);
      break;
   case a_AMV_VSC_POSPAR:
   case a_AMV_VSC_POSPAR_ENV:
      rv = a_amv_var_vsc_pospar(&avc);
      break;
   }
   NYD_OU;
   return rv;
}

FL bool_t
n_var_vexplode(void const **cookie){
   struct a_amv_pospar *appp;
   NYD_IN;

   appp = (a_amv_lopts != NULL) ? &a_amv_lopts->as_amcap->amca_pospar
         : &a_amv_pospar;
   *cookie = (appp->app_count > 0) ? &appp->app_dat[appp->app_idx] : NULL;
   NYD_OU;
   return (*cookie != NULL);
}

FL bool_t
n_var_vset(char const *vokey, uintptr_t val){
   struct a_amv_var_carrier avc;
   bool_t ok;
   NYD_IN;

   a_amv_var_revlookup(&avc, vokey, TRU1);

   ok = a_amv_var_set(&avc, (val == 0x1 ? n_empty : (char const*)val),
         a_AMV_VSETCLR_NONE);
   NYD_OU;
   return ok;
}

FL bool_t
n_var_vclear(char const *vokey){
   struct a_amv_var_carrier avc;
   bool_t ok;
   NYD_IN;

   a_amv_var_revlookup(&avc, vokey, FAL0);

   ok = a_amv_var_clear(&avc, a_AMV_VSETCLR_NONE);
   NYD_OU;
   return ok;
}

#ifdef mx_HAVE_SOCKETS
FL char *
n_var_xoklook(enum okeys okey, struct url const *urlp,
      enum okey_xlook_mode oxm){
   struct a_amv_var_carrier avc;
   struct str const *us;
   size_t nlen;
   char *nbuf, *rv;
   NYD_IN;

   assert(oxm & (OXM_PLAIN | OXM_H_P | OXM_U_H_P));

   /* For simplicity: allow this case too */
   if(!(oxm & (OXM_H_P | OXM_U_H_P))){
      nbuf = NULL;
      goto jplain;
   }

   memset(&avc, 0, sizeof avc);
   avc.avc_name = &a_amv_var_names[(avc.avc_map = &a_amv_var_map[okey]
         )->avm_keyoff];
   avc.avc_okey = okey;
   avc.avc_is_chain_variant = TRU1;

   us = (oxm & OXM_U_H_P) ? &urlp->url_u_h_p : &urlp->url_h_p;
   nlen = strlen(avc.avc_name);
   nbuf = n_lofi_alloc(nlen + 1 + us->l +1);
   memcpy(nbuf, avc.avc_name, nlen);

   /* One of .url_u_h_p and .url_h_p we test in here */
   nbuf[nlen++] = '-';
   memcpy(&nbuf[nlen], us->s, us->l +1);
   avc.avc_name = nbuf;
   avc.avc_hash = a_AMV_NAME2HASH(avc.avc_name);
   if(a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE))
      goto jvar;

   /* The second */
   if((oxm & (OXM_U_H_P | OXM_H_P)) == (OXM_U_H_P | OXM_H_P)){
      us = &urlp->url_h_p;
      memcpy(&nbuf[nlen], us->s, us->l +1);
      avc.avc_name = nbuf;
      avc.avc_hash = a_AMV_NAME2HASH(avc.avc_name);
      if(a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE)){
jvar:
         rv = avc.avc_var->av_value;
         goto jleave;
      }
   }

jplain:
   /*avc.avc_is_chain_variant = FAL0;*/
   rv = (oxm & OXM_PLAIN) ? n_var_oklook(okey) : NULL;
jleave:
   if(nbuf != NULL)
      n_lofi_free(nbuf);
   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_SOCKETS */

FL int
c_set(void *vp){
   int err;
   char **ap;
   NYD_IN;

   if(*(ap = vp) == NULL){
      a_amv_var_show_all();
      err = 0;
   }else{
      enum a_amv_var_setclr_flags avscf;

      if(!(n_pstate & n_PS_ARGMOD_LOCAL))
         avscf = a_AMV_VSETCLR_NONE;
      else{
         if(a_amv_lopts == NULL){
            n_err(_("`set': cannot use `local' in this context\n"));
            err = 1;
            goto jleave;
         }
         avscf = a_AMV_VSETCLR_LOCAL;
      }
      err = !a_amv_var_c_set(ap, avscf);
   }
jleave:
   NYD_OU;
   return err;
}

FL int
c_unset(void *vp){
   struct a_amv_var_carrier avc;
   char **ap;
   int err;
   enum a_amv_var_setclr_flags avscf;
   NYD_IN;

   if(!(n_pstate & n_PS_ARGMOD_LOCAL))
      avscf = a_AMV_VSETCLR_NONE;
   else{
      if(a_amv_lopts == NULL){
         n_err(_("`unset': cannot use `local' in this context\n"));
         err = 1;
         goto jleave;
      }
      avscf = a_AMV_VSETCLR_LOCAL;
   }

   for(err = 0, ap = vp; *ap != NULL; ++ap){
      a_amv_var_revlookup(&avc, *ap, FAL0);

      err |= !a_amv_var_clear(&avc, avscf);
   }
jleave:
   NYD_OU;
   return err;
}

FL int
c_varshow(void *v){
   char **ap;
   NYD_IN;

   if(*(ap = v) == NULL)
      v = NULL;
   else{
      struct n_string msg, *msgp = &msg;

      msgp = n_string_creat(msgp);
      for(; *ap != NULL; ++ap)
         a_amv_var_show(*ap, n_stdout, msgp);
      n_string_gut(msgp);
   }
   NYD_OU;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL int
c_varedit(void *v){ /* TODO v15 drop */
   struct a_amv_var_carrier avc;
   FILE *of, *nf;
   char *val, **argv;
   int err;
   sighandler_type sigint;
   NYD_IN;

   sigint = safe_signal(SIGINT, SIG_IGN);

   for(err = 0, argv = v; *argv != NULL; ++argv){
      a_amv_var_revlookup(&avc, *argv, TRU1);

      if(avc.avc_map != NULL){
         if(avc.avc_map->avm_flags & a_AMV_VF_BOOL){
            n_err(_("`varedit': cannot edit boolean variable: %s\n"),
               avc.avc_name);
            continue;
         }
         if(avc.avc_map->avm_flags & a_AMV_VF_RDONLY){
            n_err(_("`varedit': cannot edit read-only variable: %s\n"),
               avc.avc_name);
            continue;
         }
      }

      a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE);

      if((of = Ftmp(NULL, "varedit", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
            NULL){
         n_perr(_("`varedit': cannot create temporary file"), 0);
         err = 1;
         break;
      }else if(avc.avc_var != NULL && *(val = avc.avc_var->av_value) != '\0' &&
            sizeof *val != fwrite(val, strlen(val), sizeof *val, of)){
         n_perr(_("`varedit' failed to write old value to temporary file"), 0);
         Fclose(of);
         err = 1;
         continue;
      }

      fflush_rewind(of);
      nf = n_run_editor(of, (off_t)-1, 'e', FAL0, NULL,NULL, SEND_MBOX, sigint,
            NULL);
      Fclose(of);

      if(nf != NULL){
         int c;
         char const *varres;
         off_t l;

         l = fsize(nf);
         if(UICMP(64, l, >=, UIZ_MAX -42)){
            n_err(_("`varedit': not enough memory to store variable: %s\n"),
               avc.avc_name);
            varres = n_empty;
            err = 1;
         }else{
            varres = val = n_autorec_alloc(l +1);
            for(; l > 0 && (c = getc(nf)) != EOF; --l)
               *val++ = c;
            *val++ = '\0';
            if(l != 0){
               n_err(_("`varedit': I/O while reading new value of: %s\n"),
                  avc.avc_name);
               err = 1;
            }
         }

         if(!a_amv_var_set(&avc, varres, a_AMV_VSETCLR_NONE))
            err = 1;

         Fclose(nf);
      }else{
         n_err(_("`varedit': cannot start $EDITOR\n"));
         err = 1;
         break;
      }
   }

   safe_signal(SIGINT, sigint);
   NYD_OU;
   return err;
}

FL int
c_environ(void *v){
   struct a_amv_var_carrier avc;
   int err;
   char **ap;
   bool_t islnk;
   NYD_IN;

   if((islnk = is_asccaseprefix(*(ap = v), "link")) ||
         is_asccaseprefix(*ap, "unlink")){
      for(err = 0; *++ap != NULL;){
         a_amv_var_revlookup(&avc, *ap, TRU1);

         if(a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE) && (islnk ||
               (avc.avc_var->av_flags & a_AMV_VF_EXT_LINKED))){
            if(!islnk){
               avc.avc_var->av_flags &= ~a_AMV_VF_EXT_LINKED;
               continue;
            }else if(avc.avc_var->av_flags &
                  (a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED)){
               if(n_poption & n_PO_D_V)
                  n_err(_("`environ': link: already established: %s\n"), *ap);
               continue;
            }
            avc.avc_var->av_flags |= a_AMV_VF_EXT_LINKED;
            if(!(avc.avc_var->av_flags & a_AMV_VF_ENV))
               a_amv_var__putenv(&avc, avc.avc_var);
         }else if(!islnk){
            n_err(_("`environ': unlink: no link established: %s\n"), *ap);
            err = 1;
         }else{
            char const *evp = getenv(*ap);

            if(evp != NULL)
               err |= !a_amv_var_set(&avc, evp, a_AMV_VSETCLR_ENV);
            else{
               n_err(_("`environ': link: cannot link to non-existent: %s\n"),
                  *ap);
               err = 1;
            }
         }
      }
   }else if(is_asccaseprefix(*ap, "set"))
      err = !a_amv_var_c_set(++ap, a_AMV_VSETCLR_ENV);
   else if(is_asccaseprefix(*ap, "unset")){
      for(err = 0; *++ap != NULL;){
         a_amv_var_revlookup(&avc, *ap, FAL0);

         if(!a_amv_var_clear(&avc, a_AMV_VSETCLR_ENV))
            err = 1;
      }
   }else{
      n_err(_("Synopsis: environ: <link|set|unset> <variable>...\n"));
      err = 1;
   }
   NYD_OU;
   return err;
}

FL int
c_vexpr(void *v){ /* TODO POSIX expr(1) comp. exit status; overly complicat. */
   char pbase, op, iencbuf[2+1/* BASE# prefix*/ + n_IENC_BUFFER_SIZE + 1];
   size_t i;
   enum n_idec_state ids;
   enum n_idec_mode idm;
   si64_t lhv, rhv;
   char const **argv, *varname, *varres, *cp;
   enum{
      a_ERR = 1u<<0,
      a_SOFTOVERFLOW = 1u<<1,
      a_ISNUM = 1u<<2,
      a_ISDECIMAL = 1u<<3,    /* Print only decimal result */
      a_SATURATED = 1u<<4,
      a_ICASE = 1u<<5,
      a_UNSIGNED_OP = 1u<<6,  /* Unsigned right shift (share bit ok) */
      a_PBASE = 1u<<7,        /* Print additional number base */
      a_TMP = 1u<<30
   } f;
   NYD_IN;

   f = a_ERR;
   argv = v;
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;
   n_UNINIT(varres, n_empty);
   n_UNINIT(pbase, '\0');

   if((cp = argv[0])[0] == '\0')
      goto jesubcmd;

   if(cp[1] == '\0'){
      op = cp[0];
jnumop:
      f |= a_ISNUM;
      switch(op){
      case '=':
      case '~':
         if(argv[1] == NULL || argv[2] != NULL)
            goto jesynopsis;

         if(*(cp = *++argv) == '\0')
            lhv = 0;
         else{
            idm = ((*cp == 'u' || *cp == 'U')
                  ? (++cp, n_IDEC_MODE_NONE)
                  : ((*cp == 's' || *cp == 'S')
                     ? (++cp, n_IDEC_MODE_SIGNED_TYPE)
                     : n_IDEC_MODE_SIGNED_TYPE |
                        n_IDEC_MODE_POW2BASE_UNSIGNED));
            if(((ids = n_idec_cp(&lhv, cp, 0, idm, NULL)
                     ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
                  ) != n_IDEC_STATE_CONSUMED){
               if(!(ids & n_IDEC_STATE_EOVERFLOW) || !(f & a_SATURATED))
                  goto jenum_range;
               f |= a_SOFTOVERFLOW;
               break;
            }
         }
         if(op == '~')
            lhv = ~lhv;
         break;

      case '+':
      case '-':
      case '*':
      case '/':
      case '%':
      case '|':
      case '&':
      case '^':
      case '<':
      case '>':
         if(argv[1] == NULL || argv[2] == NULL || argv[3] != NULL)
            goto jesynopsis;
         else{
            char xop;

            if(*(cp = *++argv) == '\0')
               lhv = 0;
            else{
               idm = ((*cp == 'u' || *cp == 'U')
                     ? (++cp, n_IDEC_MODE_NONE)
                     : ((*cp == 's' || *cp == 'S')
                        ? (++cp, n_IDEC_MODE_SIGNED_TYPE)
                        : n_IDEC_MODE_SIGNED_TYPE |
                           n_IDEC_MODE_POW2BASE_UNSIGNED));
               if(((ids = n_idec_cp(&lhv, cp, 0, idm, NULL)
                        ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
                     ) != n_IDEC_STATE_CONSUMED){
                  if(!(ids & n_IDEC_STATE_EOVERFLOW) || !(f & a_SATURATED))
                     goto jenum_range;
                  f |= a_SOFTOVERFLOW;
                  break;
               }
            }

            if(*(cp = *++argv) == '\0')
               rhv = 0;
            else{
               idm = ((*cp == 'u' || *cp == 'U')
                     ? (++cp, n_IDEC_MODE_NONE)
                     : ((*cp == 's' || *cp == 'S')
                        ? (++cp, n_IDEC_MODE_SIGNED_TYPE)
                        : n_IDEC_MODE_SIGNED_TYPE |
                           n_IDEC_MODE_POW2BASE_UNSIGNED));
               if(((ids = n_idec_cp(&rhv, cp, 0, idm, NULL)
                        ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
                     ) != n_IDEC_STATE_CONSUMED){
                  if(!(ids & n_IDEC_STATE_EOVERFLOW) || !(f & a_SATURATED))
                     goto jenum_range;
                  f |= a_SOFTOVERFLOW;
                  lhv = rhv;
                  break;
               }
            }

            xop = op;
jnumop_again:
            switch(xop){
            case '+':
               if(rhv < 0){
                  if(rhv != SI64_MIN){
                     rhv = -rhv;
                     xop = '-';
                     goto jnumop_again;
                  }else if(lhv < 0)
                     goto jenum_plusminus;
                  else if(lhv == 0){
                     lhv = rhv;
                     break;
                  }
               }else if(SI64_MAX - rhv < lhv)
                  goto jenum_plusminus;
               lhv += rhv;
               break;
            case '-':
               if(rhv < 0){
                  if(rhv != SI64_MIN){
                     rhv = -rhv;
                     xop = '+';
                     goto jnumop_again;
                  }else if(lhv > 0)
                     goto jenum_plusminus;
                  else if(lhv == 0){
                     lhv = rhv;
                     break;
                  }
               }else if(SI64_MIN + rhv > lhv){
jenum_plusminus:
                  if(!(f & a_SATURATED))
                     goto jenum_overflow;
                  f |= a_SOFTOVERFLOW;
                  lhv = (lhv < 0 || xop == '-') ? SI64_MIN : SI64_MAX;
                  break;
               }
               lhv -= rhv;
               break;
            case '*':
               /* Will the result be positive? */
               if((lhv < 0) == (rhv < 0)){
                  if(lhv > 0){
                     lhv = -lhv;
                     rhv = -rhv;
                  }
                  if(rhv != 0 && lhv != 0 && SI64_MAX / rhv > lhv){
                     if(!(f & a_SATURATED))
                        goto jenum_overflow;
                     f |= a_SOFTOVERFLOW;
                     lhv = SI64_MAX;
                  }else
                     lhv *= rhv;
               }else{
                  if(rhv > 0){
                     if(lhv != 0 && SI64_MIN / lhv < rhv){
                        if(!(f & a_SATURATED))
                           goto jenum_overflow;
                        f |= a_SOFTOVERFLOW;
                        lhv = SI64_MIN;
                     }else
                        lhv *= rhv;
                  }else{
                     if(rhv != 0 && lhv != 0 && SI64_MIN / rhv < lhv){
                        if(!(f & a_SATURATED))
                           goto jenum_overflow;
                        f |= a_SOFTOVERFLOW;
                        lhv = SI64_MIN;
                     }else
                        lhv *= rhv;
                  }
               }
               break;
            case '/':
               if(rhv == 0){
                  if(!(f & a_SATURATED))
                     goto jenum_range;
                  f |= a_SOFTOVERFLOW;
                  lhv = SI64_MAX;
               }else
                  lhv /= rhv;
               break;
            case '%':
               if(rhv == 0){
                  if(!(f & a_SATURATED))
                     goto jenum_range;
                  f |= a_SOFTOVERFLOW;
                  lhv = SI64_MAX;
               }else
                  lhv %= rhv;
               break;
            case '|':
               lhv |= rhv;
               break;
            case '&':
               lhv &= rhv;
               break;
            case '^':
               lhv ^= rhv;
               break;
            case '<':
            case '>':
               if(!(f & a_TMP)){
                  argv -= 2;
                  goto jesubcmd;
               }
               if(rhv > 63){ /* xxx 63? */
                  if(!(f & a_SATURATED))
                     goto jenum_overflow;
                  rhv = 63;
               }
               if(op == '<')
                  lhv <<= (ui8_t)rhv;
               else if(f & a_UNSIGNED_OP)
                  lhv = (ui64_t)lhv >> (ui8_t)rhv;
               else
                  lhv >>= (ui8_t)rhv;
               break;
            }
         }
         break;
      default:
         goto jesubcmd;
      }
   }else if(cp[0] == '<'){
      if(*++cp != '<')
         goto jesubcmd;
      if(*++cp == '@'){
         n_OBSOLETE(_("`vexpr': please use @ modifier as prefix not suffix"));
         f |= a_SATURATED;
         ++cp;
      }
      if(*cp != '\0')
         goto jesubcmd;
      f |= a_TMP;
      op = '<';
      goto jnumop;
   }else if(cp[0] == '>'){
      if(*++cp != '>')
         goto jesubcmd;
      if(*++cp == '>'){
         f |= a_UNSIGNED_OP;
         ++cp;
      }
      if(*cp == '@'){
         n_OBSOLETE(_("`vexpr': please use @ modifier as prefix not suffix"));
         f |= a_SATURATED;
         ++cp;
      }
      if(*cp != '\0')
         goto jesubcmd;
      f |= a_TMP;
      op = '>';
      goto jnumop;
   }else if(cp[2] == '\0' && cp[1] == '@'){
      n_OBSOLETE(_("`vexpr': please use @ modifier as prefix, not suffix"));
      f |= a_SATURATED;
      op = cp[0];
      goto jnumop;
   }else if(cp[0] == '@'){
      f |= a_SATURATED;
      op = *++cp;
      if(*++cp != '\0'){
         if(op != *cp)
            goto jesubcmd;
         switch(op){
         case '<':
            if(*++cp != '\0')
               goto jesubcmd;
            f |= a_TMP;
            break;
         case '>':
            if(*++cp != '\0'){
               if(*cp != '>' || *++cp != '\0')
                  goto jesubcmd;
               f |= a_UNSIGNED_OP;
            }
            f |= a_TMP;
            break;
         default:
            goto jesubcmd;
         }
      }
      goto jnumop;
   }else if(is_asccaseprefix(cp, "pbase")){
      if(argv[1] == NULL || argv[2] == NULL || argv[3] != NULL)
         goto jesynopsis;

      if(((ids = n_idec_si8_cp(&pbase, argv[1], 0, NULL)
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || pbase < 2 || pbase > 36)
         goto jenum_range;

      f |= a_PBASE;
      op = *(argv[0] = cp = "=");
      argv[1] = argv[2];
      argv[2] = NULL;
      goto jnumop;
   }else if(is_asccaseprefix(cp, "length")){
      f |= a_ISNUM | a_ISDECIMAL;
      if(argv[1] == NULL || argv[2] != NULL)
         goto jesynopsis;

      i = strlen(*++argv);
      if(UICMP(64, i, >, SI64_MAX))
         goto jestr_overflow;
      lhv = (si64_t)i;
   }else if(is_asccaseprefix(cp, "hash")){
      f |= a_ISNUM | a_ISDECIMAL;
      if(argv[1] == NULL || argv[2] != NULL)
         goto jesynopsis;

      i = n_torek_hash(*++argv);
      lhv = (si64_t)i;
   }else if(is_asccaseprefix(cp, "find")){
      f |= a_ISNUM | a_ISDECIMAL;
      if(argv[1] == NULL || argv[2] == NULL || argv[3] != NULL)
         goto jesynopsis;

      if((cp = strstr(argv[1], argv[2])) == NULL)
         goto jestr_nodata;
      i = PTR2SIZE(cp - argv[1]);
      if(UICMP(64, i, >, SI64_MAX))
         goto jestr_overflow;
      lhv = (si64_t)i;
   }else if(is_asccaseprefix(cp, "ifind")){
      f |= a_ISNUM | a_ISDECIMAL;
      if(argv[1] == NULL || argv[2] == NULL || argv[3] != NULL)
         goto jesynopsis;

      if((cp = asccasestr(argv[1], argv[2])) == NULL)
         goto jestr_nodata;
      i = PTR2SIZE(cp - argv[1]);
      if(UICMP(64, i, >, SI64_MAX))
         goto jestr_overflow;
      lhv = (si64_t)i;
   }else if(is_asccaseprefix(cp, "substring")){
      if(argv[1] == NULL || argv[2] == NULL)
         goto jesynopsis;
      if(argv[3] != NULL && argv[4] != NULL)
         goto jesynopsis;

      i = strlen(varres = *++argv);

      if(*(cp = *++argv) == '\0')
         lhv = 0;
      else if((n_idec_si64_cp(&lhv, cp, 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED)
         goto jestr_numrange;
      if(lhv < 0){
         if(UICMP(64, i, <, -lhv))
            goto jesubstring_off;
         lhv = i + lhv;
      }
      if(UICMP(64, i, >=, lhv)){
         i -= lhv;
         varres += lhv;
      }else{
jesubstring_off:
         if(n_poption & n_PO_D_V)
            n_err(_("`vexpr': substring: offset argument too large: %s\n"),
               n_shexp_quote_cp(argv[-1], FAL0));
         f |= a_SOFTOVERFLOW;
      }

      if(argv[1] != NULL){
         if(*(cp = *++argv) == '\0')
            lhv = 0;
         else if((n_idec_si64_cp(&lhv, cp, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED)
            goto jestr_numrange;
         if(lhv < 0){
            if(UICMP(64, i, <, -lhv))
               goto jesubstring_len;
            lhv = i + lhv;
         }
         if(UICMP(64, i, >=, lhv)){
            if(UICMP(64, i, !=, lhv))
               varres = savestrbuf(varres, (size_t)lhv);
         }else{
jesubstring_len:
            if(n_poption & n_PO_D_V)
               n_err(_("`vexpr': substring: length argument too large: %s\n"),
                  n_shexp_quote_cp(argv[-2], FAL0));
            f |= a_SOFTOVERFLOW;
         }
      }
   }else if(is_asccaseprefix(cp, "trim")) Jtrim: {
      struct str trim;
      enum n_str_trim_flags stf;

      if(argv[1] == NULL || argv[2] != NULL)
         goto jesynopsis;

      if(is_asccaseprefix(cp, "trim-front"))
         stf = n_STR_TRIM_FRONT;
      else if(is_asccaseprefix(cp, "trim-end"))
         stf = n_STR_TRIM_END;
      else
         stf = n_STR_TRIM_BOTH;

      trim.l = strlen(trim.s = n_UNCONST(argv[1]));
      (void)n_str_trim(&trim, stf);
      varres = savestrbuf(trim.s, trim.l);
   }else if(is_asccaseprefix(cp, "trim-front"))
      goto Jtrim;
   else if(is_asccaseprefix(cp, "trim-end"))
      goto Jtrim;
   else if(is_asccaseprefix(cp, "random")){
      if(argv[1] == NULL || argv[2] != NULL)
         goto jesynopsis;

      if((n_idec_si64_cp(&lhv, argv[1], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || lhv < 0 || lhv > PATH_MAX)
         goto jestr_numrange;
      if(lhv == 0)
         lhv = NAME_MAX;
      varres = n_random_create_cp((size_t)lhv, NULL);
   }else if(is_asccaseprefix(cp, "file-expand")){
      if(argv[1] == NULL || argv[2] != NULL)
         goto jesynopsis;

      if((varres = fexpand(argv[1], FEXP_NVAR | FEXP_NOPROTO)) == NULL)
         goto jestr_nodata;
   }else if(is_asccaseprefix(cp, "makeprint")){
      struct str sin, sout;

      if(argv[1] == NULL || argv[2] != NULL)
         goto jesynopsis;

      /* XXX using strlen for `vexpr makeprint' is wrong for UTF-16 */
      sin.l = strlen(sin.s = n_UNCONST(argv[1]));
      makeprint(&sin, &sout);
      varres = savestrbuf(sout.s, sout.l);
      n_free(sout.s);
   /* TODO `vexpr': (wide) string length, find, etc!! */
#ifdef mx_HAVE_REGEX
   }else if(is_asccaseprefix(cp, "regex")) Jregex:{
      regmatch_t rema[1 + n_VEXPR_REGEX_MAX];
      regex_t re;
      int reflrv;

      f |= a_ISNUM | a_ISDECIMAL;
      if(argv[1] == NULL || argv[2] == NULL ||
            (argv[3] != NULL && argv[4] != NULL))
         goto jesynopsis;

      reflrv = REG_EXTENDED;
      if(f & a_ICASE)
         reflrv |= REG_ICASE;
      if((reflrv = regcomp(&re, argv[2], reflrv))){
         n_err(_("`vexpr': invalid regular expression: %s: %s\n"),
            n_shexp_quote_cp(argv[2], FAL0), n_regex_err_to_doc(NULL, reflrv));
         assert(f & a_ERR);
         n_pstate_err_no = n_ERR_INVAL;
         goto jestr;
      }
      reflrv = regexec(&re, argv[1], n_NELEM(rema), rema, 0);
      regfree(&re);
      if(reflrv == REG_NOMATCH)
         goto jestr_nodata;

      /* Search only?  Else replace, which is a bit */
      if(argv[3] == NULL){
         if(UICMP(64, rema[0].rm_so, >, SI64_MAX))
            goto jestr_overflow;
         lhv = (si64_t)rema[0].rm_so;
      }else{
         /* We need to setup some kind of pseudo macro environment for this */
         struct a_amv_lostack los;
         struct a_amv_mac_call_args amca;
         char const **reargv;
         size_t cnt;

         memset(&amca, 0, sizeof amca);
         amca.amca_name = savestrbuf(&argv[1][rema[0].rm_so],
               rema[0].rm_eo - rema[0].rm_so);
         amca.amca_amp = a_AMV_MACKY_MACK;
         for(cnt = i = 1; i < n_NELEM(rema); ++i)
            if(rema[i].rm_so != -1)
               cnt = i;
         amca.amca_pospar.app_count = (ui32_t)cnt;
         amca.amca_pospar.app_not_heap = TRU1;
         amca.amca_pospar.app_dat =
               reargv = n_autorec_alloc(sizeof(char*) * (cnt +1));
         for(i = 1; i <= cnt; ++reargv, ++i)
            if(rema[i].rm_so != -1)
               *reargv = savestrbuf(&argv[1][rema[i].rm_so],
                     rema[i].rm_eo - rema[i].rm_so);
            else
               *reargv = n_empty;
         *reargv = NULL;

         memset(&los, 0, sizeof los);
         hold_all_sigs(); /* TODO DISLIKE! */
         los.as_global_saved = a_amv_lopts;
         los.as_amcap = &amca;
         los.as_up = los.as_global_saved;
         a_amv_lopts = &los;

         /* C99 */{
            struct str templ;
            struct n_string s_b;
            enum n_shexp_state shs;

            templ.s = n_UNCONST(argv[3]);
            templ.l = UIZ_MAX;
            shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
                  n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
                  n_SHEXP_PARSE_QUOTE_AUTO_DSQ),
                  n_string_creat_auto(&s_b), &templ, NULL);
            if((shs & (n_SHEXP_STATE_ERR_MASK | n_SHEXP_STATE_STOP)
                  ) == n_SHEXP_STATE_STOP){
               varres = n_string_cp(&s_b);
               n_string_drop_ownership(&s_b);
            }else
               varres = NULL;
         }

         a_amv_lopts = los.as_global_saved;
         rele_all_sigs(); /* TODO DISLIKE! */

         if(varres == NULL)
            goto jestr_nodata;
         f &= ~(a_ISNUM | a_ISDECIMAL);
      }
   }else if(is_asccaseprefix(argv[0], "iregex")){
      f |= a_ICASE;
      goto Jregex;
#endif /* mx_HAVE_REGEX */
   }else
      goto jesubcmd;

   n_pstate_err_no = (f & a_SOFTOVERFLOW) ? n_ERR_OVERFLOW : n_ERR_NONE;
   f &= ~a_ERR;

   /* Generate the variable value content for numerics.
    * Anticipate in our handling below!  (Don't do needless work) */
jleave:
   if((f & a_ISNUM) && ((f & (a_ISDECIMAL | a_PBASE)) || varname != NULL)){
      cp = n_ienc_buf(iencbuf, lhv, (f & a_PBASE ? pbase : 10),
            n_IENC_MODE_SIGNED_TYPE);
      if(cp != NULL)
         varres = cp;
      else{
         f |= a_ERR;
         varres = n_empty;
      }
   }

   if(varname == NULL){
      /* If there was no error and we are printing a numeric result, print some
       * more bases for the fun of it */
      if((f & (a_ERR | a_ISNUM | a_ISDECIMAL)) == a_ISNUM){
         char binabuf[64 + 64 / 8 +1];
         size_t j;

         for(j = 1, i = 0; i < 64; ++i){
            binabuf[63 + 64 / 8 -j - i] = (lhv & ((ui64_t)1 << i)) ? '1' : '0';
            if((i & 7) == 7 && i != 63){
               ++j;
               binabuf[63 + 64 / 8 -j - i] = ' ';
            }
         }
         binabuf[64 + 64 / 8 -1] = '\0';

         if(fprintf(n_stdout,
                  "0b %s\n0%" PRIo64 " | 0x%" PRIX64 " | %" PRId64 "\n",
                  binabuf, lhv, lhv, lhv) < 0 ||
               ((f & a_PBASE) && (assert(varres != NULL),
                fprintf(n_stdout, "%s\n", varres) < 0))){
            n_pstate_err_no = n_err_no;
            f |= a_ERR;
         }
      }else if(varres != NULL && fprintf(n_stdout, "%s\n", varres) < 0){
         n_pstate_err_no = n_err_no;
         f |= a_ERR;
      }
   }else if(!n_var_vset(varname, (uintptr_t)varres)){
      n_pstate_err_no = n_ERR_NOTSUP;
      f |= a_ERR;
   }
   NYD_OU;
   return (f & a_ERR) ? 1 : 0;

jerr:
   f = a_ERR | a_ISNUM;
   lhv = -1;
   goto jleave;
jesubcmd:
   n_err(_("`vexpr': invalid subcommand: %s\n"),
      n_shexp_quote_cp(*argv, FAL0));
   n_pstate_err_no = n_ERR_INVAL;
   goto jerr;
jesynopsis:
   n_err(_("Synopsis: vexpr: <operator> <:argument:>\n"));
   n_pstate_err_no = n_ERR_INVAL;
   goto jerr;

jenum_range:
   n_err(_("`vexpr': numeric argument invalid or out of range: %s\n"),
      n_shexp_quote_cp(*argv, FAL0));
   n_pstate_err_no = n_ERR_RANGE;
   goto jerr;
jenum_overflow:
   n_err(_("`vexpr': expression overflows datatype: %" PRId64 " %c %" PRId64
      "\n"), lhv, op, rhv);
   n_pstate_err_no = n_ERR_OVERFLOW;
   goto jerr;

jestr_numrange:
   n_err(_("`vexpr': numeric argument invalid or out of range: %s\n"),
      n_shexp_quote_cp(*argv, FAL0));
   n_pstate_err_no = n_ERR_RANGE;
   goto jestr;
jestr_overflow:
   n_err(_("`vexpr': string length or offset overflows datatype\n"));
   n_pstate_err_no = n_ERR_OVERFLOW;
   goto jestr;
jestr_nodata:
   n_pstate_err_no = n_ERR_NODATA;
   /* FALLTHRU */
jestr:
   varres = n_empty;
   f &= ~a_ISNUM;
   f |= a_ERR;
   goto jleave;
}

FL int
c_vpospar(void *v){
   struct n_cmd_arg *cap;
   size_t i;
   struct a_amv_pospar *appp;
   enum{
      a_NONE = 0,
      a_ERR = 1u<<0,
      a_SET = 1u<<1,
      a_CLEAR = 1u<<2,
      a_QUOTE = 1u<<3
   } f;
   char const *varres;
   struct n_cmd_arg_ctx *cacp;
   NYD_IN;

   n_pstate_err_no = n_ERR_NONE;
   n_UNINIT(varres, n_empty);
   cacp = v;
   cap = cacp->cac_arg;

   if(is_asccaseprefix(cap->ca_arg.ca_str.s, "set"))
      f = a_SET;
   else if(is_asccaseprefix(cap->ca_arg.ca_str.s, "clear"))
      f = a_CLEAR;
   else if(is_asccaseprefix(cap->ca_arg.ca_str.s, "quote"))
      f = a_QUOTE;
   else{
      n_err(_("`vpospar': invalid subcommand: %s\n"),
         n_shexp_quote_cp(cap->ca_arg.ca_str.s, FAL0));
      n_pstate_err_no = n_ERR_INVAL;
      f = a_ERR;
      goto jleave;
   }
   --cacp->cac_no;

   if((f & (a_CLEAR | a_QUOTE)) && cap->ca_next != NULL){
      n_err(_("`vpospar': `%s': takes no argument\n"), cap->ca_arg.ca_str.s);
      n_pstate_err_no = n_ERR_INVAL;
      f = a_ERR;
      goto jleave;
   }

   cap = cap->ca_next;

   /* If in a macro, we need to overwrite the local instead of global argv */
   appp = (a_amv_lopts != NULL) ? &a_amv_lopts->as_amcap->amca_pospar
         : &a_amv_pospar;

   if(f & (a_SET | a_CLEAR)){
      if(cacp->cac_vput != NULL)
         n_err(_("`vpospar': `vput' only supported for `quote' subcommand\n"));
      if(!appp->app_not_heap && appp->app_maxcount > 0){
         for(i = appp->app_maxcount; i-- != 0;)
            n_free(n_UNCONST(appp->app_dat[i]));
         n_free(appp->app_dat);
      }
      memset(appp, 0, sizeof *appp);

      if(f & a_SET){
         if((i = cacp->cac_no) > a_AMV_POSPAR_MAX){
            n_err(_("`vpospar': overflow: %" PRIuZ " arguments!\n"), i);
            n_pstate_err_no = n_ERR_OVERFLOW;
            f = a_ERR;
            goto jleave;
         }

         memset(appp, 0, sizeof *appp);
         if(i > 0){
            appp->app_maxcount = appp->app_count = (ui16_t)i;
            /* XXX Optimize: store it all in one chunk! */
            ++i;
            i *= sizeof *appp->app_dat;
            appp->app_dat = n_alloc(i);

            for(i = 0; cap != NULL; ++i, cap = cap->ca_next){
               appp->app_dat[i] = n_alloc(cap->ca_arg.ca_str.l +1);
               memcpy(n_UNCONST(appp->app_dat[i]), cap->ca_arg.ca_str.s,
                  cap->ca_arg.ca_str.l +1);
            }

            appp->app_dat[i] = NULL;
         }
      }
   }else{
      if(appp->app_count == 0)
         varres = n_empty;
      else{
         struct str in;
         struct n_string s, *sp;
         char sep1, sep2;

         sp = n_string_creat_auto(&s);

         sep1 = *ok_vlook(ifs);
         sep2 = *ok_vlook(ifs_ws);
         if(sep1 == sep2)
            sep2 = '\0';
         if(sep1 == '\0')
            sep1 = ' ';

         for(i = 0; i < appp->app_count; ++i){
            if(sp->s_len){
               if(!n_string_can_book(sp, 2))
                  goto jeover;
               sp = n_string_push_c(sp, sep1);
               if(sep2 != '\0')
                  sp = n_string_push_c(sp, sep2);
            }
            in.l = strlen(in.s = n_UNCONST(appp->app_dat[i + appp->app_idx]));

            if(!n_string_can_book(sp, in.l)){
jeover:
               n_err(_("`vpospar': overflow: string too long!\n"));
               n_pstate_err_no = n_ERR_OVERFLOW;
               f = a_ERR;
               goto jleave;
            }
            sp = n_shexp_quote(sp, &in, TRU1);
         }

         varres = n_string_cp(sp);
      }

      if(cacp->cac_vput == NULL){
         if(fprintf(n_stdout, "%s\n", varres) < 0){
            n_pstate_err_no = n_err_no;
            f |= a_ERR;
         }
      }else if(!n_var_vset(cacp->cac_vput, (uintptr_t)varres)){
         n_pstate_err_no = n_ERR_NOTSUP;
         f |= a_ERR;
      }
   }
jleave:
   NYD_OU;
   return (f & a_ERR) ? 1 : 0;
}

/* s-it-mode */
