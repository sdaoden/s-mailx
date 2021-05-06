/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Account, macro and variable handling; `vexpr' and `vpospar'.
 *@ HOWTO add a new non-dynamic boolean or value option:
 *@ - add an entry to nail.h:enum okeys
 *@ - run make-okey-map.pl (which is highly related..)
 *@ - update the manual!
 *@ TODO . Split in macro / variable, use specific headers.
 .@ TODO . `global' command modifier.
 *@ TODO . Drop the VIP stuff.  Allow PTF callbacks to be specified, call them.
 *@ TODO . Optimize: with the dynamic hashmaps, and the object based approach
 *@ TODO   it should become possible to strip down the implementation again.
 *@ TODO   E.g., FREEZE is much too complicated: use an overlay object ptr,
 *@ TODO   UNLIKELY() it, and add a OnProgramStartupCompletedEvent to
 *@ TODO   incorporate what it tracks, then drop it.  Etc.
 *@ TODO   Global -> Scope -> Local, all "overlay" objects.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE accmacvar
#define mx_SOURCE
#define mx_SOURCE_ACCMACVAR

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/path.h>
#include <su/sort.h>

#include "mx/cmd.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/iconv.h"
#include "mx/names.h"
#include "mx/sigs.h"
#include "mx/ui-str.h"
#include "mx/url.h"

/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#if !defined mx_HAVE_SETENV && !defined mx_HAVE_PUTENV
# error Exactly one of mx_HAVE_SETENV and mx_HAVE_PUTENV
#endif

/* Positional parameter maximum (macro arguments, `vexpr' "splitifs") */
#define a_AMV_POSPAR_MAX S16_MAX

/* Special "pseudo macro" that stabs you from the back */
#define a_AMV_MACKY_MACK R(struct a_amv_mac*,-1)

/* Note: changing the hash function must be reflected in `vexpr' "hash32",
 * because that is used by the hashtable creator scripts! */
#define a_AMV_PRIME 23 /* TODO cs_dict! */
#define a_AMV_NAME2HASH(N) S(u32,su_cs_hash(N))
#define a_AMV_HASH2PRIME(H) ((H) % a_AMV_PRIME)

enum a_amv_mac_flags{
   a_AMV_MF_NONE = 0,
   a_AMV_MF_ACCOUNT = 1u<<0, /* This macro is an `account' */
   a_AMV_MF_TYPE_MASK = a_AMV_MF_ACCOUNT,
   a_AMV_MF_UNDEF = 1u<<1, /* Unlink after lookup */
   a_AMV_MF_DELETE = 1u<<7, /* Delete in progress, free once refcnt==0 */
   a_AMV_MF__MAX = 0xFFu
};

enum a_amv_loflags{
   a_AMV_LF_NONE = 0,
   a_AMV_LF_SCOPE = 1u<<0, /* Current scope `localopts' on */
   a_AMV_LF_SCOPE_FIXATE = 1u<<1, /* Ditto, but fixated */
   a_AMV_LF_SCOPE_MASK = a_AMV_LF_SCOPE | a_AMV_LF_SCOPE_FIXATE,
   a_AMV_LF_CALL = 1u<<2, /* `localopts' on for `call'ed scopes */
   a_AMV_LF_CALL_FIXATE = 1u<<3, /* Ditto, but fixated */
   a_AMV_LF_CALL_MASK = a_AMV_LF_CALL | a_AMV_LF_CALL_FIXATE,
   /* Convert loflags _CALL bits (also) carried in a _lostack level to the
    * _SCOPE bits needed for newly created _mac_call_args instance */
   a_AMV_LF_CALL_TO_SCOPE_SHIFT = 2
};

/* mk/make-okey-map.pl ensures that _VIRT implies _RDONLY and _NODEL,
 * and that _IMPORT implies _ENV; it does not verify anything...
 * More description at nail.h:enum okeys */
enum a_amv_var_flags{
   a_AMV_VF_NONE = 0,

   /* The basic set of flags, also present in struct a_amv_var_map.avm_flags */
   a_AMV_VF_BOOL = 1u<<0, /* ok_b_* */
   a_AMV_VF_CHAIN = 1u<<1, /* Has -HOST and/or -USER@HOST variants */
   a_AMV_VF_VIRT = 1u<<2, /* "Stateless" automatic variable */
   a_AMV_VF_VIP = 1u<<3, /* Wants _var_check_vips() evaluation */
   a_AMV_VF_RDONLY = 1u<<4, /* May not be set by user */
   a_AMV_VF_NODEL = 1u<<5, /* May not be deleted */
   a_AMV_VF_I3VAL = 1u<<6, /* Has an initial value */
   a_AMV_VF_DEFVAL = 1u<<7, /* Has a default value */
   a_AMV_VF_IMPORT = 1u<<8, /* Import ONLY from env (pre n_PSO_STARTED) */
   a_AMV_VF_ENV = 1u<<9, /* Update environment on change */
   a_AMV_VF_NOLOPTS = 1u<<10, /* May not be tracked by `localopts' */
   a_AMV_VF_NOTEMPTY = 1u<<11, /* May not be assigned an empty value */
   /* TODO _VF_NUM, _VF_POSNUM: we also need 64-bit limit numbers! */
   a_AMV_VF_NUM = 1u<<12, /* Value must be a 32-bit number */
   a_AMV_VF_POSNUM = 1u<<13, /* Value must be positive 32-bit number */
   a_AMV_VF_LOWER = 1u<<14, /* Values will be stored in lowercase version */
   a_AMV_VF_OBSOLETE = 1u<<15, /* Is obsolete? */
   a_AMV_VF__MASK = (1u<<(15+1)) - 1,

   /* Extended flags, not part of struct a_amv_var_map.avm_flags */
   /* Indicates the instance is actually a variant of a _VF_CHAIN, it thus uses
    * the a_amv_var_map of the base variable, but it is not the base itself and
    * therefore care must be taken */
   a_AMV_VF_EXT_CHAIN = 1u<<22,
   a_AMV_VF_EXT_LOCAL = 1u<<23, /* `local' */
   a_AMV_VF_EXT_LINKED = 1u<<24, /* `environ' link'ed */
   a_AMV_VF_EXT_FROZEN = 1u<<25, /* Has been set by -S,.. */
   a_AMV_VF_EXT_FROZEN_UNSET = 1u<<26, /* ..and was used to unset a variable */
   a_AMV_VF_EXT__FROZEN_MASK = a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET,
   a_AMV_VF_EXT__MASK = (1u<<(26+1)) - 1,
   /* All the flags that could be set for customs / `local' variables */
   a_AMV_VF_EXT__CUSTOM_MASK = a_AMV_VF_ENV |
         a_AMV_VF_EXT_LOCAL | a_AMV_VF_EXT_LINKED | a_AMV_VF_EXT_FROZEN,
   a_AMV_VF_EXT__LOCAL_MASK = a_AMV_VF_EXT_LOCAL | a_AMV_VF_EXT_LINKED
};

enum a_amv_var_lookup_flags{
   a_AMV_VLOOK_NONE = 0,
   a_AMV_VLOOK_LOCAL = 1u<<0, /* Query `local' layer first */
   a_AMV_VLOOK_LOCAL_ONLY = 1u<<1, /* MUST be a `local' variable */
   /* Do not allocate new var for _I3VAL, see var_lookup() for more.
    * I.e., set by variable _set() and _clear() request */
   a_AMV_VLOOK_I3VAL_NONEW = 1u<<2,
   a_AMV_VLOOK_I3VAL_NONEW_REPORT = 1u<<3,
   /* And then we must be able to detect endless recursion, for example if
    * $TMPDIR is set to non-existent we can use the VAL_TMPDIR config default,
    * but if this also fails (filesystem read-only for example), then all bets
    * are off, and we must not enter an endless loop */
   a_AMV_VLOOK_BELLA_CIAO_CIAO_CIAO = 1u<<29,
   /* xxx We over-#define this to _LOOK_NONE unless a_AMV_VAR_HAS_OBSOLETE
    * xxx is defined, which simplifies using this bit */
   a_AMV_VLOOK_LOG_OBSOLETE = 1u<<30
};

enum a_amv_var_setclr_flags{
   a_AMV_VSETCLR_NONE = 0,
   a_AMV_VSETCLR_LOCAL = 1u<<1, /* `local' scope (aka `localopts') */
   a_AMV_VSETCLR_ENV = 1u<<2, /* `environ' or otherwise environ */
   a_AMV_VSETCLR_UNROLL = 1u<<3, /* Currently unrolling */
   a_AMV_VSETCLR_UNROLL_ENV_LINKED = 1u<<4 /* ..only restore _EXT_LINKED */
};

/* We support some special parameter names for one(+)-letter variable names;
 * note these have counterparts in the code that manages shell expansion!
 * All these special variables are solely backed by var_vlook(), beside
 * this there is only a_amv_var_revlookup() which knows about them */
enum a_amv_var_special_category{
   a_AMV_VSC_NONE, /* Normal variable, no special treatment */
   a_AMV_VSC_GLOBAL, /* ${[?!]} are specially mapped, but global */
   a_AMV_VSC_MULTIPLEX, /* ${^.*} circumflex accent multiplexer */
   a_AMV_VSC_POSPAR, /* ${[1-9][0-9]*} positional parameters */
   a_AMV_VSC_POSPAR_ENV /* ${[*@#]} positional parameter support variables */
};

enum a_amv_var_special_type{
   /* _VSC_GLOBAL */
   a_AMV_VST_QM, /* ? */
   a_AMV_VST_EM, /* ! */
   /* _VSC_MULTIPLEX */
   /* This is special in that it is a multiplex indicator, the ^ is followed by
    * a normal variable */
   a_AMV_VST_CACC, /* ^ (circumflex accent) */
   /* _VSC_POSPAR_ENV */
   a_AMV_VST_STAR, /* * */
   a_AMV_VST_AT, /* @ */
   a_AMV_VST_NOSIGN /* # */
};

enum a_amv_var_vip_mode{
   a_AMV_VIP_SET_PRE,
   a_AMV_VIP_SET_POST,
   a_AMV_VIP_CLEAR
};

struct a_amv_pospar{
   u16 app_maxcount; /* == slots in .app_dat */
   u16 app_count; /* Maximum a_AMV_POSPAR_MAX */
   u16 app_idx; /* `shift' moves this one, decs .app_count */
   boole app_not_heap; /* .app_dat stuff not dynamically allocated */
   u8 app__dummy[1];
   char const **app_dat; /* NIL terminated (for "$@" explosion support) */
};
CTA(a_AMV_POSPAR_MAX <= S16_MAX, "Limit exceeds datatype capabilities");

struct a_amv_mac{
   struct a_amv_mac *am_next;
   u32 am_maxlen; /* ..of any line in .am_line_dat */
   u32 am_line_cnt; /* ..of *.am_line_dat (but NIL terminated) */
   struct a_amv_mac_line **am_line_dat; /* TODO use deque? */
   struct a_amv_var *am_lopts; /* `localopts' unroll list */
   u32 am_refcnt; /* 0-based for `un{account,define}' purposes */
   BITENUM_IS(u8,a_amv_mac_flags) am_flags;
   char am_name[VFIELD_SIZE(3)]; /* ..of this macro */
};
CTA(a_AMV_MF__MAX <= U8_MAX, "Enumeration excesses storage datatype");

struct a_amv_mac_line{
   u32 aml_len;
   u32 aml_prespc; /* Number of leading SPACEs, for display purposes */
   char aml_dat[VFIELD_SIZE(0)];
};

struct a_amv_mac_call_args{
   char const *amca_name; /* For MACKY_MACK, this is *0*! */
   struct a_amv_mac *amca_amp; /* "const", but for am_refcnt */
   struct a_amv_var **amca_unroller;
   u8 amca_loflags;
   boole amca_ps_hook_mask;
   boole amca_no_xcall; /* XXX We want GO_INPUT_NO_XCALL for this */
   boole amca_ignerr; /* XXX Use GO_INPUT_IGNERR for evaluating commands */
   u8 amca__pad[4];
   struct a_amv_var *(*amca_local_vars)[a_AMV_PRIME]; /* `local's, or NIL */
   struct a_amv_pospar *amca_pospar;
   struct a_amv_pospar amca__pospar;
};

struct a_amv_lostack{
   struct a_amv_lostack *as_global_saved; /* Saved global XXX due to jump */
   struct a_amv_mac_call_args *as_amcap;
   struct a_amv_lostack *as_up; /* Outer context */
   struct a_amv_var *as_lopts;
   BITENUM_IS(u8,a_amv_mac_loflags) as_loflags;
   u8 avs__pad[7];
};

struct a_amv_var{
   struct a_amv_var *av_link;
   char *av_value;
#ifdef mx_HAVE_PUTENV
   char *av_env; /* Actively managed putenv(3) memory, or NIL */
#endif
   BITENUM_IS(u32,a_amv_var_flags) av_flags; /* Plus extended bits */
   char av_name[VFIELD_SIZE(4)];
};
CTA(a_AMV_VF_EXT__MASK <= U32_MAX, "Enumeration excesses storage datatype");

/* After inclusion of gen-okeys.h we ASSERT keyoff fits in 16-bit */
struct a_amv_var_map{
   u32 avm_hash;
   u16 avm_keyoff;
   BITENUM_IS(u16,a_amv_var_flags) avm_flags; /* Without extended bits */
};
CTA(a_AMV_VF__MASK <= U16_MAX, "Enumeration excesses storage datatype");

/* XXX Since there is no indicator character used for variable chains, we just
 * XXX cannot do better than using expensive detection.
 * The length of avcmb_prefix is highly hardwired with make-okey-map.pl etc. */
struct a_amv_var_chain_map_bsrch{
   char avcmb_prefix[4];
   u16 avcmb_chain_map_off;
   u16 avcmb_chain_map_eokey; /* This is an enum okeys */
};

/* Use 16-bit for enum okeys, which should always be sufficient; all around
 * here we use 32-bit for it instead, but that owed to faster access (?) */
struct a_amv_var_chain_map{
   u16 avcm_keyoff;
   u16 avcm_okey;
};
/* Not <= because we have _S_MAILX_TEST */
CTA(n_OKEYS_MAX < U16_MAX, "Enumeration excesses storage datatype");

struct a_amv_var_virt{
   u32 avv_okey;
   u8 avv__dummy[4];
   struct a_amv_var const *avv_var;
};

struct a_amv_var_defval{
   u32 avdv_okey;
   u8 avdv__pad[4];
   char const *avdv_value; /* Only for !BOOL (otherwise plain existence) */
};

struct a_amv_var_carrier{
   char const *avc_name;
   u32 avc_hash;
   u32 avc_prime;
   struct a_amv_var *avc_var;
   struct a_amv_var_map const *avc_map;
   enum okeys avc_okey;
   boole avc_is_chain_variant; /* Base is a chain, this a variant thereof */
   u8 avc_special_cat;
   /* Numerical parameter name if .avc_special_cat=a_AMV_VSC_POSPAR,
    * otherwise a enum a_amv_var_special_type */
   u16 avc_special_prop;
};

/* Include constant make-okey-map.pl output, and the generated version data */
#include "mx/gen-version.h" /* - */
#include "mx/gen-okeys.h" /* $(MX_SRCDIR) */

/* As above, to simplify its usage, over-define, now that we know */
#ifndef a_AMV_VAR_HAS_OBSOLETE
# define a_AMV_VLOOK_LOG_OBSOLETE a_AMV_VLOOK_NONE
#endif

/* As promised above, CTAs to protect our structures */
CTA(a_AMV_VAR_NAME_KEY_MAXOFF <= U16_MAX,
   "Enumeration excesses storage datatype");

/* The currently active account */
static struct a_amv_mac *a_amv_acc_curr;

static struct a_amv_mac *a_amv_macs[a_AMV_PRIME]; /* TODO dynamically spaced */

/* Unroll list of currently running macro stack (XXX grrr global byypass) */
static struct a_amv_lostack *a_amv_lopts;
#define a_AMV_HAVE_LOPTS_AKA_LOCAL() /* TODO */\
   (/*mx_go_ctx_is_macro() || a_amv_folder_hooks_lopts != NIL || */\
   /*a_amv_compose_lostack != NIL*/ a_amv_lopts != NIL)

static struct a_amv_var *a_amv_vars[a_AMV_PRIME]; /* TODO dynamically spaced */

/* The global a_AMV_VSC_POSPAR stack */
static struct a_amv_pospar a_amv_pospar;

/* TODO We really deserve localopts support for *folder-hook*s, so hack it in
 * TODO today via a static lostack, it should be a field in mailbox, once that
 * TODO is a real multi-instance object */
static struct a_amv_var *a_amv_folder_hook_lopts;

/* TODO Rather ditto (except for storage -> cmd_ctx), compose hooks */
static struct a_amv_lostack *a_amv_compose_lostack;

/* Lookup for macros/accounts: if newamp is not NIL it will be linked in the
 * map, if _MF_UNDEF is set a possibly existing entry will be removed (first).
 * Returns NIL if a lookup failed, or if newamp was set, the found entry in
 * plain lookup cases or when _UNDEF was performed on a currently active entry
 * (the entry will have been unlinked, and the _MF_DELETE will be honoured once
 * the reference count reaches 0), and (*)-1 if an _UNDEF was performed */
static struct a_amv_mac *a_amv_mac_lookup(char const *name,
      struct a_amv_mac *newamp, BITENUM_IS(u32,a_amv_mac_flags) amf);

/* `call', `call_if' (and `xcall' via go.c -> c_xcall()).
 * lospopts may only come in via the weird ways of `xcall' */
static int a_amv_mac_call(void *vp, boole silent_nexist,
      void *lospopts_or_nil);

/* Execute a macro; amcap must reside in LOFI memory */
static boole a_amv_mac_exec(struct a_amv_mac_call_args *amcap,
      void *lospopts_or_nil);

static void a_amv_mac__finalize(void *vp);

/* User display helpers */
static boole a_amv_mac_show(BITENUM_IS(u32,a_amv_mac_flags) amf);

/* _def() returns error for faulty definitions and already existing * names,
 * _undef() returns error if a named thing does not exist */
static boole a_amv_mac_def(char const *name,
      BITENUM_IS(u32,a_amv_mac_flags) amf);
static boole a_amv_mac_undef(char const *name,
      BITENUM_IS(u32,a_amv_mac_flags) amf);

/* */
static void a_amv_mac_free(struct a_amv_mac *amp);

/* Update replay-log.
 * If enforce_scope is set the current scope is used, regardless of whether
 * `localopts' is actually enabled for it (`local' convenience shortcut) */
static void a_amv_lopts_add(struct a_amv_lostack *alp, char const *name,
      struct a_amv_var const *oavp, boole enforce_scope);
static void a_amv_lopts_unroll(struct a_amv_var **avpp);

/* Special cased value string allocation */
static char *a_amv_var_copy(char const *str);
static void a_amv_var_free(char *cp);

/* Check for special housekeeping.  _VIP_SET_POST and _VIP_CLEAR do not fail
 * (or propagate errors), _VIP_SET_PRE may and should cause abortion */
static boole a_amv_var_check_vips(enum a_amv_var_vip_mode avvm,
      enum okeys okey, char const **val);

/* _VF_NUM / _VF_POSNUM */
static boole a_amv_var_check_num(char const *val, boole posnum);

/* Verify that the given name is an acceptable variable name */
static boole a_amv_var_check_name(char const *name, boole forenviron);

/* Try to reverse lookup a name to an enum okeys mapping, zeroing avcp.
 * Updates .avc_name and .avc_hash; .avc_map is NIL if none found.
 * We may try_harder to identify name: it may be an extended chain.
 * That test only is actually performed by the latter(, then) */
static boole a_amv_var_revlookup(struct a_amv_var_carrier *avcp,
      char const *name, boole try_harder);
static boole a_amv_var_revlookup_chain(struct a_amv_var_carrier *avcp,
      char const *name);

/* Lookup a variable from .avc_(map|name|hash), return whether it was found.
 * Sets .avc_prime; .avc_var is NIL if not found.
 * Here it is where we care for _I3VAL and _DEFVAL.
 * An _I3VAL will be "consumed" as necessary anyway, but it will not be used to
 * create a new variable if _VLOOK_I3VAL_NONEW is set; if
 * _VLOOK_I3VAL_NONEW_REPORT is set then we set .avc_var to -1 and return true
 * if that was the case, otherwise we will return FAL0, then!
 * xxx freezing: One more special case with freezing: a frozen clearance
 * request in conjunction with AMV_VLOOK_I3VAL_NONEW will return NIL and set
 * .avc_var to the set variable */
static boole a_amv_var_lookup(struct a_amv_var_carrier *avcp,
      BITENUM_IS(u32,a_amv_var_lookup_flags) avlf);

/* Lookup functions for special category variables, _pospar drives all
 * positional parameter etc. special categories */
static char const *a_amv_var_vsc_global(struct a_amv_var_carrier *avcp);
static char const *a_amv_var_vsc_multiplex(struct a_amv_var_carrier *avcp);
static char const *a_amv_var_vsc_pospar(struct a_amv_var_carrier *avcp);

/* Set var from .avc_(map|name|hash), return success */
static boole a_amv_var_set(struct a_amv_var_carrier *avcp, char const *value,
      BITENUM_IS(u32,a_amv_var_setclr_flags) avscf);

static boole a_amv_var__putenv(struct a_amv_var_carrier *avcp,
      struct a_amv_var *avp);

/* Clear var from .avc_(map|name|hash); sets .avc_var=NIL, return success */
static boole a_amv_var_clear(struct a_amv_var_carrier *avcp,
      BITENUM_IS(u32,a_amv_var_setclr_flags) avscf);

static boole a_amv_var__clearenv(char const *name, struct a_amv_var *avp);

/* For showing "all", instantiate first-time-inits and default values here */
static void a_amv_var_show_instantiate_all(void);

/* List all `set' variables */
static void a_amv_var_show_all(void);

/* Actually do print one, return number of lines written */
static uz a_amv_var_show(char const *name, FILE *fp, struct n_string *msgp,
      boole local);

/* Shared c_set() and c_environ():set impl, return success */
static boole a_amv_var_c_set(char const **ap,
      BITENUM_IS(u32,a_amv_var_setclr_flags) avscf);

/* */
#ifdef a_AMV_VAR_HAS_OBSOLETE
static void a_amv_var_obsolete(char const *name);
#endif

static struct a_amv_mac *
a_amv_mac_lookup(char const *name, struct a_amv_mac *newamp,
      BITENUM_IS(u32,a_amv_mac_flags) amf){
   struct a_amv_mac *amp, **ampp, **ampp_base;
   BITENUM_IS(u32,a_amv_mac_flags) save_amf;
   NYD2_IN;

   save_amf = amf;
   amf &= a_AMV_MF_TYPE_MASK;
   /* C99 */{
      u32 h;

      h = a_AMV_NAME2HASH(name);
      h = a_AMV_HASH2PRIME(h);
      ampp = ampp_base = &a_amv_macs[h];
   }

   for(amp = *ampp; amp != NIL; ampp = &(*ampp)->am_next, amp = amp->am_next){
      if((amp->am_flags & a_AMV_MF_TYPE_MASK) == amf &&
            !su_cs_cmp(amp->am_name, name)){
         if(LIKELY((save_amf & a_AMV_MF_UNDEF) == 0))
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
            amp = R(struct a_amv_mac*,-1);
         }
         break;
      }
   }

   if(newamp != NIL){
      newamp->am_next = *ampp_base;
      *ampp_base = newamp;
      amp = NIL;
   }

jleave:
   NYD2_OU;
   return amp;
}

static int
a_amv_mac_call(void *vp, boole silent_nexist, void *lospopts_or_nil){
   struct a_amv_mac *amp;
   int rv;
   char const *name;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   cacp = vp;
   name = cacp->cac_arg->ca_arg.ca_str.s;

   if(UNLIKELY(cacp->cac_no > a_AMV_POSPAR_MAX)){
      n_err(_("Too many arguments to macro `call': %s\n"), name);
      n_pstate_err_no = su_ERR_OVERFLOW;
      rv = 1;
   }else if(UNLIKELY((amp = a_amv_mac_lookup(name, NIL, a_AMV_MF_NONE)
         ) == NIL)){
      if(!silent_nexist)
         n_err(_("Undefined macro called: %s\n"),
            n_shexp_quote_cp(name, FAL0));
      n_pstate_err_no = su_ERR_NOENT;
      rv = 1;
   }else{
      char const **argv;
      struct a_amv_mac_call_args *amcap;
      u32 argc;

      argc = cacp->cac_no + 1;
      amcap = su_LOFI_ALLOC(sizeof *amcap + (S(uz,argc) * sizeof *argv));
      argv = S(void*,&amcap[1]);

      for(argc = 0; (cacp->cac_arg = cacp->cac_arg->ca_next) != NIL; ++argc)
         argv[argc] = cacp->cac_arg->ca_arg.ca_str.s;
      argv[argc] = NIL;

      su_mem_set(amcap, 0, sizeof *amcap);
      amcap->amca_name = name;
      amcap->amca_amp = amp;

      if(n_pstate & n_PS_ARGMOD_LOCAL)
         amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
      else if(a_AMV_HAVE_LOPTS_AKA_LOCAL())
         amcap->amca_loflags = (a_amv_lopts->as_loflags & a_AMV_LF_CALL_MASK
               ) >> a_AMV_LF_CALL_TO_SCOPE_SHIFT;

      amcap->amca_pospar = &amcap->amca__pospar;
      if(argc > 0){
         amcap->amca__pospar.app_count = S(u16,argc);
         amcap->amca__pospar.app_not_heap = TRU1;
         amcap->amca__pospar.app_dat = argv;
      }

      (void)a_amv_mac_exec(amcap, lospopts_or_nil);
      rv = n_pstate_ex_no;
   }

   NYD_OU;
   return rv;
}

static boole
a_amv_mac_exec(struct a_amv_mac_call_args *amcap, void *lospopts_or_nil){
   struct a_amv_lostack *losp;
   struct a_amv_mac_line **amlp;
   char **args_base, **args;
   struct a_amv_mac *amp;
   boole rv;
   NYD2_IN;

   amp = amcap->amca_amp;
   ASSERT(amp != NIL && amp != a_AMV_MACKY_MACK);
   ++amp->am_refcnt;
   /* TODO Unfortunately we yet need to dup the macro lines! :( */
   args_base = args = su_ALLOC(sizeof(*args) * (amp->am_line_cnt +1));
   for(amlp = amp->am_line_dat; *amlp != NIL; ++amlp)
      *(args++) = su_cs_dup_cbuf((*amlp)->aml_dat, (*amlp)->aml_len, 0);
   *args = NIL;

   losp = su_LOFI_ALLOC(sizeof *losp);
   losp->as_global_saved = a_amv_lopts;
   if((losp->as_amcap = amcap)->amca_unroller == NIL){
      losp->as_up = losp->as_global_saved;
      losp->as_lopts = lospopts_or_nil;
   }else{
      ASSERT(lospopts_or_nil == NIL);
      losp->as_up = NIL;
      losp->as_lopts = *amcap->amca_unroller;
   }
   losp->as_loflags = amcap->amca_loflags;

   a_amv_lopts = losp;
   rv = mx_go_macro((mx_GO_INPUT_NONE |
            (amcap->amca_no_xcall ? mx_GO_INPUT_NO_XCALL : 0) |
            (amcap->amca_ignerr ? mx_GO_INPUT_IGNERR : 0)),
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
   if(amcap->amca_pospar == &amcap->amca__pospar &&
         !amcap->amca__pospar.app_not_heap &&
         amcap->amca__pospar.app_maxcount > 0){
      u16 i;

      for(i = amcap->amca__pospar.app_maxcount; i-- != 0;)
         su_FREE(UNCONST(char*,amcap->amca__pospar.app_dat[i]));
      su_FREE(amcap->amca__pospar.app_dat);
   }

   /* `local' variable hashmap.  These have no environment map, never */
   if(amcap->amca_local_vars != NIL){
      struct a_amv_var **avpp_base, **avpp, *avp;

      for(avpp_base = *amcap->amca_local_vars, avpp = &avpp_base[a_AMV_PRIME];
            avpp-- != avpp_base;){
         while((avp = *avpp)){
            ASSERT(!(avp->av_flags & ~a_AMV_VF_EXT__LOCAL_MASK));
            *avpp = avp->av_link;
            a_amv_var_free(avp->av_value);
            su_FREE(avp);
         }
      }

      su_FREE(avpp_base);
   }

   /* Unroll `localopts', if applicable; some callees may want to keep the
    * unroll stack around to undo it later on when their scope is left */
   if(amcap->amca_unroller == NIL){
      if(losp->as_lopts != NIL)
         a_amv_lopts_unroll(&losp->as_lopts);
   }else
      *amcap->amca_unroller = losp->as_lopts;

   if(amcap->amca_ps_hook_mask)
      n_pstate &= ~n_PS_HOOK_MASK;

   /* Good time to cleanup `un(account|define)'d macros */
   if((amp = amcap->amca_amp) != a_AMV_MACKY_MACK && amp != NIL &&
         --amp->am_refcnt == 0 && (amp->am_flags & a_AMV_MF_DELETE))
      a_amv_mac_free(amp);

   su_LOFI_FREE(losp);
   su_LOFI_FREE(amcap);

   NYD2_OU;
}

static boole
a_amv_mac_show(BITENUM_IS(u32,a_amv_mac_flags) amf){
   uz lc, mc, ti, i;
   char const *typestr;
   FILE *fp;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if((fp = mx_fs_tmp_open(NIL, "deflist", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL)
      fp = n_stdout;

   amf &= a_AMV_MF_TYPE_MASK;
   typestr = (amf & a_AMV_MF_ACCOUNT) ? "account" : "define";

   for(lc = mc = ti = 0; ti < a_AMV_PRIME; ++ti){
      struct a_amv_mac *amp;

      for(amp = a_amv_macs[ti]; amp != NIL; amp = amp->am_next){
         if((amp->am_flags & a_AMV_MF_TYPE_MASK) == amf){
            struct a_amv_mac_line **amlpp;

            if(++mc > 1){
               putc('\n', fp);
               ++lc;
            }
            ++lc;
            fprintf(fp, "%s %s {\n", typestr, amp->am_name);
            for(amlpp = amp->am_line_dat; *amlpp != NIL; ++lc, ++amlpp){
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

   if(fp != n_stdout){
      if(mc > 0)
         page_or_print(fp, lc);

      rv = (ferror(fp) == 0);
      mx_fs_close(fp);
   }else{
      clearerr(fp);
      rv = TRU1;
   }

   NYD2_OU;
   return rv;
}

static boole
a_amv_mac_def(char const *name, BITENUM_IS(u32,a_amv_mac_flags) amf){
   struct str line;
   u32 line_cnt, maxlen;
   struct linelist{
      struct linelist *ll_next;
      struct a_amv_mac_line *ll_amlp;
   } *llp, *ll_head, *ll_tail;
   union {uz s; int i; u32 ui; uz l;} n;
   struct a_amv_mac *amp;
   boole rv;
   NYD2_IN;

   mx_fs_linepool_aquire(&line.s, &line.l);
   rv = FAL0;
   amp = NIL;

   /* TODO We should have our input state machine which emits Line events,
    * TODO and hook different consumers dependent on our content, as stated
    * TODO in i think go.c: like this local macros etc. would become
    * TODO possible (from the input side) */
   /* Read in the lines which form the macro content */
   for(ll_tail = ll_head = NIL, line_cnt = maxlen = 0;;){
      u32 leaspc;
      char *cp;

      n.i = mx_go_input(mx_GO_INPUT_CTX_DEFAULT | mx_GO_INPUT_NL_ESC,
            su_empty, &line.s, &line.l, NIL, NIL);
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
      for(; n.ui > 0 && su_cs_is_space(cp[n.ui - 1]); --n.ui)
         ;
      if(n.ui == 0)
         continue;

      maxlen = MAX(maxlen, n.ui);
      cp[n.ui++] = '\0';

      /* The closing brace? */
      if(*cp == '}')
         break;

      if(LIKELY(++line_cnt < U32_MAX)){
         struct a_amv_mac_line *amlp;

         llp = su_AUTO_ALLOC(sizeof *llp);
         if(ll_head == NIL)
            ll_head = llp;
         else
            ll_tail->ll_next = llp;
         ll_tail = llp;
         llp->ll_next = NIL;
         llp->ll_amlp = amlp = su_ALLOC(VSTRUCT_SIZEOF(struct a_amv_mac_line,
               aml_dat) + n.ui);
         amlp->aml_len = n.ui -1;
         amlp->aml_prespc = leaspc;
         su_mem_copy(amlp->aml_dat, cp, n.ui);
      }else{
         n_err(_("Too much content in %s definition: %s\n"),
            (amf & a_AMV_MF_ACCOUNT ? "account" : "macro"), name);
         goto jerr;
      }
   }

   /* Create the new macro */
   n.s = su_cs_len(name) +1;
   amp = su_ALLOC(VSTRUCT_SIZEOF(struct a_amv_mac,am_name) + n.s);
   su_mem_set(amp, 0, VSTRUCT_SIZEOF(struct a_amv_mac,am_name));
   amp->am_maxlen = maxlen;
   amp->am_line_cnt = line_cnt;
   amp->am_flags = amf;
   su_mem_copy(amp->am_name, name, n.s);
   /* C99 */{
      struct a_amv_mac_line **amlpp;

      amp->am_line_dat = amlpp = su_ALLOC(sizeof(*amlpp) * ++line_cnt);
      for(llp = ll_head; llp != NIL; llp = llp->ll_next)
         *amlpp++ = llp->ll_amlp;
      *amlpp = NIL;
   }

   /* Create entry, replace a yet existing one as necessary */
   a_amv_mac_lookup(name, amp, amf | a_AMV_MF_UNDEF);
   rv = TRU1;

jleave:
   mx_fs_linepool_release(line.s, line.l);
   NYD2_OU;
   return rv;

jerr:
   for(llp = ll_head; llp != NIL; llp = llp->ll_next)
      su_FREE(llp->ll_amlp);
   /*
    * if(amp != NIL){
    *   su_FREE(amp->am_line_dat);
    *   su_FREE(amp);
    *}*/
   goto jleave;
}

static boole
a_amv_mac_undef(char const *name, BITENUM_IS(u32,a_amv_mac_flags) amf){
   struct a_amv_mac *amp;
   boole rv;
   NYD2_IN;

   rv = TRU1;

   if(LIKELY(name[0] != '*' || name[1] != '\0')){
      if((amp = a_amv_mac_lookup(name, NIL, amf | a_AMV_MF_UNDEF)) == NIL){
         n_err(_("%s not defined: %s\n"),
            (amf & a_AMV_MF_ACCOUNT ? "Account" : "Macro"), name);
         rv = FAL0;
      }
   }else{
      struct a_amv_mac **ampp, *lamp;

      for(ampp = a_amv_macs; PCMP(ampp, <, &a_amv_macs[NELEM(a_amv_macs)]);
            ++ampp)
         for(lamp = NIL, amp = *ampp; amp != NIL;){
            if((amp->am_flags & a_AMV_MF_TYPE_MASK) == amf){
               /* xxx Expensive but rare: be simple */
               a_amv_mac_lookup(amp->am_name, NIL, amf | a_AMV_MF_UNDEF);
               amp = (lamp == NIL) ? *ampp : lamp->am_next;
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

   for(amlpp = amp->am_line_dat; *amlpp != NIL; ++amlpp)
      su_FREE(*amlpp);
   su_FREE(amp->am_line_dat);
   su_FREE(amp);

   NYD2_OU;
}

static void
a_amv_lopts_add(struct a_amv_lostack *alp, char const *name,
      struct a_amv_var const *oavp, boole enforce_scope){
   struct a_amv_var *avp;
   uz nl, vl;
   NYD2_IN;

   /* Propagate unrolling up the stack, as necessary.
    * Stop for stack level where scoping is desired */
   ASSERT(alp != NIL);
   if(!enforce_scope)
      for(;;){
         if(alp->as_loflags & a_AMV_LF_SCOPE_MASK)
            break;
         if((alp = alp->as_up) == NIL)
            goto jleave;
      }

   /* Check whether variable is handled yet at this level
    * TODO Boost lopts: use cs_dict! */
   for(avp = alp->as_lopts; avp != NIL; avp = avp->av_link)
      if(!su_cs_cmp(avp->av_name, name))
         goto jleave;

   nl = su_cs_len(name) +1;
   vl = (oavp != NIL) ? su_cs_len(oavp->av_value) +1 : 0;
   avp = su_ALLOC(VSTRUCT_SIZEOF(struct a_amv_var,av_name) + nl + vl);
   su_mem_set(avp, 0, VSTRUCT_SIZEOF(struct a_amv_var,av_name));
   avp->av_link = alp->as_lopts;
   alp->as_lopts = avp;
   if(oavp != NIL){
      avp->av_flags = oavp->av_flags;
      if(vl != 0)
         su_mem_copy(avp->av_value = &avp->av_name[nl], oavp->av_value, vl);
   }
   su_mem_copy(avp->av_name, name, nl);

jleave:
   NYD2_OU;
}

static void
a_amv_lopts_unroll(struct a_amv_var **avpp){
   struct a_amv_var_carrier avc;
   struct a_amv_var *x, *avp;
   NYD2_IN;

   avp = *avpp;
   *avpp = NIL;

   while(avp != NIL){
      BITENUM_IS(u32,a_amv_var_setclr_flags) avscf;

      x = avp;
      avp = avp->av_link;

      a_amv_var_revlookup(&avc, x->av_name, TRU1);

      avscf = a_AMV_VSETCLR_UNROLL;
      if(x->av_flags & a_AMV_VF_ENV)
         avscf |= a_AMV_VSETCLR_ENV;
      if(x->av_flags & a_AMV_VF_EXT_LINKED)
         avscf |= a_AMV_VSETCLR_UNROLL_ENV_LINKED;

      a_amv_var_set(&avc,
         (/*R(up,x->av_value) == 0x1 ? su_empty:*/ R(char const*,x->av_value)),
         avscf);

      su_FREE(x);
   }

   NYD2_OU;
}

static char *
a_amv_var_copy(char const *str){
   char *news;
   uz len;
   NYD2_IN;

   if(*str == '\0')
      news = UNCONST(char*,su_empty);
   else if(str[1] == '\0'){
      if(str[0] == '1')
         news = UNCONST(char*,n_1);
      else if(str[0] == '0')
         news = UNCONST(char*,n_0);
      else
         goto jheap;
   }else if(str[2] == '\0' && str[0] == '-' && str[1] == '1')
      news = UNCONST(char*,n_m1);
   else{
jheap:
      len = su_cs_len(str) +1;
      news = su_ALLOC(len);
      su_mem_copy(news, str, len);
   }

   NYD2_OU;
   return news;
}

static void
a_amv_var_free(char *cp){
   NYD2_IN;

   if(cp[0] != '\0' && cp != n_0 && cp != n_1 && cp != n_m1)
      su_FREE(cp);

   NYD2_OU;
}

static boole
a_amv_var_check_vips(enum a_amv_var_vip_mode avvm, enum okeys okey,
      char const **val){
   char const *emsg;
   boole ok;
   NYD2_IN;

   ok = TRU1;
   emsg = NIL;

   if(avvm == a_AMV_VIP_SET_PRE){
      switch(okey){
      default:
         break;
      case ok_v_charset_7bit:
      case ok_v_charset_8bit:
      case ok_v_charset_unknown_8bit:
      case ok_v_ttycharset:
         if((*val = n_iconv_normalize_name(*val)) == NIL)
            ok = FAL0;
         break;
      case ok_v_customhdr:{
         char const *vp;
         char *buf;
         struct n_header_field *hflp, **hflpp, *hfp;

         buf = savestr(*val);
         hflp = NIL;
         hflpp = &hflp;

         while((vp = su_cs_sep_escable_c(&buf, ',', TRU1)) != NIL){
            if(!n_header_add_custom(hflpp, vp, TRU1)){
               emsg = N_("Invalid *customhdr* entry: %s\n");
               break;
            }
            hflpp = &(*hflpp)->hf_next;
         }

         hflpp = (emsg == NIL) ? &n_customhdr_list : &hflp;
         while((hfp = *hflpp) != NIL){
            *hflpp = hfp->hf_next;
            su_FREE(hfp);
         }
         if(emsg)
            goto jerr;
         n_customhdr_list = hflp;
         }break;
      case ok_v_from:
      case ok_v_sender:{
         struct mx_name *np;

         np = (okey == ok_v_sender ? n_extract_single : lextract
               )(*val, GEXTRA | GFULL);
         if(np == NIL){
jefrom:
            emsg = N_("*from* / *sender*: invalid  address(es): %s\n");
            goto jerr;
         }else for(; np != NIL; np = np->n_flink)
            if(is_addr_invalid(np, EACM_STRICT | EACM_NOLOG | EACM_NONAME))
               goto jefrom;
         }break;
      case ok_v_HOME:
         /* Note this gets called from main.c during initialization, and they
          * simply set this to pw_dir as a fallback: do not verify _that_ call.
          * See main.c! */
         if(!(n_pstate & n_PS_ROOT) && !su_path_is_dir(*val, TRUM1)){
            emsg = N_("$HOME is not a directory or not accessible: %s\n");
            goto jerr;
         }
         break;
#ifdef mx_HAVE_IDNA
      case ok_v_hostname:
      case ok_v_smtp_hostname:
         if(**val != '\0'){
            struct n_string cnv;

            n_string_creat_auto(&cnv);
            if(!n_idna_to_ascii(&cnv, *val, UZ_MAX)){
               /*n_string_gut(&res);*/
               emsg = N_("*hostname*/*smtp_hostname*: "
                     "IDNA encoding failed: %s\n");
               goto jerr;
            }
            *val = n_string_cp(&cnv);
            /*n_string_drop_ownership(&cnv);*/
         }
         break;
#endif
      case ok_v_quote_chars:{
         char c;
         char const *cp;

         for(cp = *val; (c = *cp++) != '\0';)
            if(!su_cs_is_ascii(c) || su_cs_is_space(c)){
               ok = FAL0;
               break;
            }
         }break;
      case ok_v_sendcharsets:{
         struct n_string s_b, *s = &s_b;
         char *csv, *cp;

         s = n_string_creat_auto(s);
         csv = savestr(*val);

         while((cp = su_cs_sep_c(&csv, ',', TRU1)) != NIL){
            if((cp = n_iconv_normalize_name(cp)) == NIL){
               ok = FAL0;
               break;
            }
            if(s->s_len > 0)
               s = n_string_push_c(s, ',');
            s = n_string_push_cp(s, cp);
         }

         *val = n_string_cp(s);
         /* n_string_drop_ownership(so); */
         }break;
      case ok_v_TMPDIR:
         if(!su_path_is_dir(*val, TRU1)){
            emsg = N_("$TMPDIR is not a directory or not accessible: %s\n");
            goto jerr;
         }
         break;
      case ok_v_umask:
         if(**val != '\0'){
            u64 uib;

            su_idec_u64_cp(&uib, *val, 0, NIL);
            if(uib & ~0777u){ /* (is valid _VF_POSNUM) */
               emsg = N_("Invalid *umask* setting: %s\n");
               goto jerr;
            }
         }
         break;
      case ok_v_verbose:{
         u64 uib;

         /* Initially a boolean variable, we want to keep compat forever ;> */
         if(**val != '\0')
            su_idec_u64_cp(&uib, *val, 0, NIL);
         else switch(n_poption & n_PO_V_MASK){
         case 0: uib = 1; break;
         case n_PO_V: uib = 2; break;
         default: uib = 3; break;
         }

         switch(uib){
         case 0: *val = su_empty; break;
         case 1: *val = n_1; break;
         case 2: *val = "2"; break;
         default: *val = "3"; break;
         }
         }break;
      }
   }else if(avvm == a_AMV_VIP_SET_POST){
      switch(okey){
      default:
         break;
      case ok_b_ask:
         ok_bset(asksub);
         break;
      case ok_v_bind_timeout: /* v15-compat: drop this */
         n_OBSOLETE("*bind-timeout*: please set *bind-inter-byte-timeout*, "
            "doing this for you (for now)");
         n_PS_ROOT_BLOCK(ok_vset(bind_inter_byte_timeout, *val));
         break;
      case ok_b_debug:
         n_poption |= n_PO_D;
         su_log_set_level(su_LOG_DEBUG);
# define a_DEBUG_MEMCONF su_MEM_CONF_DEBUG | su_MEM_CONF_LINGER_FREE
         DBG( su_mem_set_conf(a_DEBUG_MEMCONF, TRU1); )
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
         x_b = x = su_AUTO_ALLOC(su_cs_len(cp) +1);
         while((c = *cp++) != '\0')
            if(su_cs_is_space(c))
               *x++ = c;
         *x = '\0';
         n_PS_ROOT_BLOCK(ok_vset(ifs_ws, x_b));
         }break;
#ifdef mx_HAVE_SETLOCALE
      case ok_v_LANG:
      case ok_v_LC_ALL:
      case ok_v_LC_CTYPE:
         mx_locale_init();
         break;
#endif
      case ok_b_memdebug:
         DBG( su_mem_set_conf(a_DEBUG_MEMCONF |
            su_MEM_CONF_ON_ERROR_EMERG, TRU1); )
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
      case ok_v_SOCKS5_PROXY: /* <-> *socks-proxy* */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_vset(socks_proxy, *val));
         break;
      case ok_v_socks_proxy: /* <-> $SOCKS5_PROXY */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_vset(SOCKS5_PROXY, *val));
         break;
      case ok_b_typescript_mode:
         ok_bset(colour_disable);
         ok_bset(line_editor_disable);
         if(!(n_psonce & n_PSO_STARTED))
            ok_bset(termcap_disable);
         break;
      case ok_v_umask:
         if(**val != '\0'){
            u64 uib;

            su_idec_u64_cp(&uib, *val, 0, NIL);
            umask(S(mode_t,uib));
         }
         break;
      case ok_v_verbose:{
         /* Work out what the PRE VIP did */
         u32 i;

         /* Initially a boolean variable, we want to keep compat forever :> */
         i = 0;
         switch(**val){
         default: i |= n_PO_VVV; /* FALLTHRU */
         case '2': i |= n_PO_VV; /* FALLTHRU */
         case '1': i |= n_PO_V; /* FALLTHRU */
         case '\0': break;
         }

         if(i != 0){
            n_poption &= ~n_PO_V_MASK;
            n_poption |= i;
            if(!(n_poption & n_PO_D))
               su_log_set_level(su_LOG_INFO);
         }else
            ok_vclear(verbose);
         }break;
      }
   }else{
      switch(okey){
      default:
         break;
      case ok_b_ask:
         ok_bclear(asksub);
         break;
      case ok_b_debug:
         n_poption &= ~n_PO_D;
         su_log_set_level((n_poption & n_PO_V) ? su_LOG_INFO : n_LOG_LEVEL);
         DBG( if(!ok_blook(memdebug))
            su_mem_set_conf(a_DEBUG_MEMCONF, FAL0); )
         break;
      case ok_v_customhdr:{
         struct n_header_field *hfp;

         while((hfp = n_customhdr_list) != NIL){
            n_customhdr_list = hfp->hf_next;
            su_FREE(hfp);
         }
         }break;
      case ok_v_HOME:
         /* Invalidate any resolved folder then, too
          * FALLTHRU */
      case ok_v_folder:
         n_PS_ROOT_BLOCK(ok_vclear(folder_resolved));
         break;
      case ok_b_memdebug:
         DBG( su_mem_set_conf((ok_blook(debug) ? 0 : a_DEBUG_MEMCONF) |
               su_MEM_CONF_ON_ERROR_EMERG, FAL0); )
#undef a_DEBUG_MEMCONF
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
      case ok_v_SOCKS5_PROXY: /* <-> *socks-proxy* */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_vclear(socks_proxy));
         break;
      case ok_v_socks_proxy: /* <-> $SOCKS5_PROXY */
         if(!(n_pstate & n_PS_ROOT))
            n_PS_ROOT_BLOCK(ok_vclear(SOCKS5_PROXY));
         break;
      case ok_v_verbose:
         n_poption &= ~n_PO_V_MASK;
         if(!(n_poption & n_PO_D))
            su_log_set_level(n_LOG_LEVEL);
         break;
      }
   }

jleave:
   NYD2_OU;
   return ok;
jerr:
   emsg = V_(emsg);
   n_err(emsg, n_shexp_quote_cp(*val, FAL0));
   ok = FAL0;
   goto jleave;
}

static boole
a_amv_var_check_num(char const *val, boole posnum){
   /* TODO The internal/environment variables which are num= or posnum= should
    * TODO gain special lookup functions, or the return should be void* and
    * TODO castable to integer; i.e. no more strtoX() should be needed.
    * TODO I.e., the result of this function should instead be stored */
   boole rv;
   NYD2_IN;

   rv = TRU1;

   if(*val != '\0'){ /* Would be _VF_NOTEMPTY if not allowed */
      u64 uib;
      BITENUM_IS(u32,su_idec_state) ids;

      ids = su_idec_cp(&uib, val, 0,
            (su_IDEC_MODE_LIMIT_32BIT |
             (posnum ?  su_IDEC_MODE_SIGNED_TYPE : su_IDEC_MODE_NONE)), NIL);
      if((ids & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED)
         rv = FAL0;
      /* TODO Unless we store integers we need to look and forbid, because
       * TODO callee may not be able to swallow, e.g., "-1" */
      if(posnum && (ids & su_IDEC_STATE_SEEN_MINUS))
         rv = FAL0;
   }

   NYD2_OU;
   return rv;
}

static boole
a_amv_var_check_name(char const *name, boole forenviron){
   char c;
   char const *cp;
   boole rv;
   NYD2_IN;

   rv = TRU1;

   /* Empty name not tested, as documented */
   for(cp = name; (c = *cp) != '\0'; ++cp)
      if(c == '=' || su_cs_is_space(c) || su_cs_is_cntrl(c)){
         n_err(_("Variable names may not contain =, space or control "
            "characters: %s\n"), n_shexp_quote_cp(name, TRU1));
         rv = FAL0;
         goto jleave;
      }

   if(rv && forenviron && !(rv = n_shexp_is_valid_varname(name, TRU1)))
      n_err(_("Invalid environment variable: %s\n"),
         n_shexp_quote_cp(name, TRU1));

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_amv_var_revlookup(struct a_amv_var_carrier *avcp, char const *name,
      boole try_harder){
   u32 hash, i, j;
   struct a_amv_var_map const *avmp;
   char c;
   NYD2_IN;

   su_mem_set(avcp, 0, sizeof *avcp); /* XXX overkill, just set chain */

   /* It may be a special a.k.a. macro-local or one-letter parameter */
   c = name[0];
   if(UNLIKELY(su_cs_is_digit(c))){
      /* (Inline dec. atoi, ugh) */
      for(j = S(u8,c) - '0', i = 1;; ++i){
         c = name[i];
         if(c == '\0')
            break;
         if(!su_cs_is_digit(c))
            goto jno_special_param;
         j = j * 10 + S(u8,c) - '0';
      }
      if(j <= a_AMV_POSPAR_MAX){
         avcp->avc_special_cat = a_AMV_VSC_POSPAR;
         goto jspecial_param;
      }
   }else if(UNLIKELY(name[1] == '\0')){
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
   ASSERT(a_AMV_VSC_NONE == 0);/*avcp->avc_special_cat = a_AMV_VSC_NONE;*/
   avcp->avc_name = name;
   avcp->avc_hash = hash = a_AMV_NAME2HASH(name);

   /* One of okeys?  Walk over the hashtable */
   for(i = hash % a_AMV_VAR_REV_PRIME, j = 0; j <= a_AMV_VAR_REV_LONGEST; ++j){
      u32 x;

      if((x = a_amv_var_revmap[i]) == a_AMV_VAR_REV_ILL)
         break;

      avmp = &a_amv_var_map[x];
      if(avmp->avm_hash == hash &&
            !su_cs_cmp(&a_amv_var_names[avmp->avm_keyoff], name)){
         avcp->avc_map = avmp;
         avcp->avc_okey = S(enum okeys,x);
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
    * We possibly want to know for a variety of reasons */
   if(try_harder && a_amv_var_revlookup_chain(avcp, name))
      goto jleave;

   ASSERT(avcp->avc_map == NIL);/*avcp->avc_map = NIL;*/
   avcp = NIL;
jleave:
   ASSERT(avcp == NIL || avcp->avc_map != NIL ||
      avcp->avc_special_cat == a_AMV_VSC_NONE);

   NYD2_OU;
   return (avcp != NIL);

   /* All these are mapped to *--special-param* */
jspecial_param:
   avcp->avc_name = name;
   avcp->avc_special_prop = S(u16,j);
   avmp = &a_amv_var_map[a_AMV_VAR__SPECIAL_PARAM_MAP_IDX];
   avcp->avc_hash = avmp->avm_hash;
   avcp->avc_map = avmp;
   avcp->avc_okey = ok_v___special_param;
   goto jleave;
}

static boole
a_amv_var_revlookup_chain(struct a_amv_var_carrier *avcp, char const *name){
   uz i;
   struct a_amv_var_chain_map_bsrch const *avcmbp, *avcmbp_x;
   NYD_IN;

   if(su_cs_len(name) <
         FIELD_SIZEOF(struct a_amv_var_chain_map_bsrch,avcmb_prefix)){
      avcp = NIL;
      goto jleave;
   }

   avcmbp = &a_amv_var_chain_map_bsrch[0];
   i = a_AMV_VAR_CHAIN_MAP_BSRCH_CNT - 0;
   do{ /* while((i >>= 1) > 0) */
      int cres;

      avcmbp_x = &avcmbp[i >> 1];
      cres = su_mem_cmp(name, avcmbp_x->avcmb_prefix,
            FIELD_SIZEOF(struct a_amv_var_chain_map_bsrch,avcmb_prefix));
      if(cres != 0){
         /* Go right instead? */
         if(cres > 0){
            avcmbp = ++avcmbp_x;
            --i;
         }
      }else{
         /* Once the binary search found the right prefix we instead have to
          * use a linear walk, because there is no "trigger" character:
          * anything could be something free-form or a chain-extension, we
          * just do not know.  Unfortunately.  Luckily cramping the walk to
          * a small window is possible */
         struct a_amv_var_chain_map const *avcmp, *avcmp_hit;

         avcmp = &a_amv_var_chain_map[avcmbp_x->avcmb_chain_map_off];
         avcmp_hit = NIL;
         do{
            char c;
            char const *cp, *ncp;

            cp = &a_amv_var_names[avcmp->avcm_keyoff +
                  FIELD_SIZEOF(struct a_amv_var_chain_map_bsrch,avcmb_prefix)];
            ncp = &name[FIELD_SIZEOF(struct a_amv_var_chain_map_bsrch,
                     avcmb_prefix)];
            for(;; ++ncp, ++cp)
               if(*ncp != (c = *cp) || c == '\0')
                  break;
            /* Is it a chain extension of this key? */
            if(c == '\0' && *ncp == '-')
               avcmp_hit = avcmp;
            else if(avcmp_hit != NIL)
               break;
         }while((avcmp++)->avcm_okey < avcmbp_x->avcmb_chain_map_eokey);

         if(avcmp_hit != NIL){
            avcp->avc_map = &a_amv_var_map[avcp->avc_okey =
                  S(enum okeys,avcmp_hit->avcm_okey)];
            avcp->avc_is_chain_variant = TRU1;
            goto jleave;
         }
         break;
      }
   }while((i >>= 1) > 0);

   avcp = NIL;
jleave:
   NYD_OU;
   return (avcp != NIL);
}

static boole
a_amv_var_lookup(struct a_amv_var_carrier *avcp, /* XXX too complicated! */
      BITENUM_IS(u32,a_amv_var_lookup_flags) avlf){
   uz i;
   char const *cp;
   u32 f;
   struct a_amv_var *avp;
   struct a_amv_var_map const *avmp;
   NYD2_IN;

   ASSERT(!(avlf & a_AMV_VLOOK_LOCAL_ONLY) || (avlf & a_AMV_VLOOK_LOCAL));
   ASSERT(!(avlf & a_AMV_VLOOK_I3VAL_NONEW_REPORT) ||
      (avlf & a_AMV_VLOOK_I3VAL_NONEW));
   ASSERT(!(avlf & a_AMV_VLOOK_BELLA_CIAO_CIAO_CIAO));

   avcp->avc_prime = a_AMV_HASH2PRIME(avcp->avc_hash);
   avmp = avcp->avc_map;

   /* Built-in internal variables are never on `local' level */
   if(LIKELY(avmp != NIL))
      avlf &= ~a_AMV_VLOOK_LOCAL;

   /* C99 */{
      struct a_amv_var **avpp, *lavp;

      /* Optionally macro-`local' non-built-in variables first */
      if(avlf & a_AMV_VLOOK_LOCAL){
         if(a_AMV_HAVE_LOPTS_AKA_LOCAL() &&
               (avpp = *a_amv_lopts->as_amcap->amca_local_vars) != NIL){
            avpp += avcp->avc_prime;

            for(lavp = NIL, avp = *avpp; avp != NIL;
                  lavp = avp, avp = avp->av_link)
               if(!su_cs_cmp(avp->av_name, avcp->avc_name)){
                  ASSERT(avp->av_flags & a_AMV_VF_EXT_LOCAL);

                  /* Relink as head, hope it "sorts on usage" over time.
                   * The code relies on this behaviour! */
                  if(lavp != NIL){
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

      for(lavp = NIL, avp = *avpp; avp != NIL; lavp = avp, avp = avp->av_link){
         if(!su_cs_cmp(avp->av_name, avcp->avc_name)){
            ASSERT(!(avp->av_flags & a_AMV_VF_EXT_LOCAL));

            /* Relink as head, hope it "sorts on usage" over time.
             * Note: code below *relies* on this behaviour! */
            if(lavp != NIL){
               lavp->av_link = avp->av_link;
               avp->av_link = *avpp;
               *avpp = avp;
            }

            /* If this setting has been established via -S and we still have
             * not reached the _STARTED_CONFIG program state, it may have been
             * an explicit "clearance" that is to be treated as unset.
             * Because that is a special condition that (has been hacked in
             * later and) needs to be encapsulated in lower levels */
            switch(avp->av_flags & a_AMV_VF_EXT__FROZEN_MASK){
            case a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET:
               /* However: only care if not called by _set() or _clear()! */
               if(!(avlf & a_AMV_VLOOK_I3VAL_NONEW)){
                  avcp->avc_var = avp;
                  avp = NIL;
                  goto j_leave;
               }
               /* FALLTHRU */
            default:
               break;
            }
            goto jleave;
         }
      }
   }

   /* If this is not an assembled variable we need to consider some special
    * initialization cases and eventually create the variable anew */
   if(LIKELY((avmp = avcp->avc_map) != NIL)){
      f = avmp->avm_flags;

      /* Does it have an import-from-environment flag? */
      if(UNLIKELY((f & (a_AMV_VF_IMPORT | a_AMV_VF_ENV)) != 0)){
         if(LIKELY((cp = getenv(avcp->avc_name)) != NIL)){
            /* May be better not to use that one, though? */
            /* TODO Outsource the tests into a _shared_ test function!
             * TODO _var_test(AVMP,[FLAG,]VALUE ..) */
            boole isempty, isbltin;

            isempty = (*cp == '\0' && (f & a_AMV_VF_NOTEMPTY) != 0);
            isbltin = ((f & (a_AMV_VF_I3VAL | a_AMV_VF_DEFVAL)) != 0);

            if(UNLIKELY(isempty)){
               n_err(_("Environment variable must not be empty: %s\n"),
                  avcp->avc_name);
               if(!isbltin)
                  goto jerr;
            }else if(LIKELY(*cp != '\0')){
               if(UNLIKELY((f & a_AMV_VF_NUM) &&
                     !a_amv_var_check_num(cp, FAL0))){
                  n_err(_("Environment variable value not a number "
                     "or out of range: %s\n"), avcp->avc_name);
                  goto jerr;
               }
               if(UNLIKELY((f & a_AMV_VF_POSNUM) &&
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
      if(UNLIKELY((f & a_AMV_VF_I3VAL) != 0)){
         static struct a_amv_var_defval const **arr,
               *arr_base[a_AMV_VAR_I3VALS_CNT +1];

         if(UNLIKELY(arr == NIL)){
            arr = &arr_base[0];
            arr[i = a_AMV_VAR_I3VALS_CNT] = NIL;
            while(i-- > 0){
               arr[i] = &a_amv_var_i3vals[i];
               /* Assure case-insensitive sorting */
               ASSERT(i + 1 == a_AMV_VAR_I3VALS_CNT ||
                  arr[i] < arr[i + 1]);
            }
         }

         for(i = 0; arr[i] != NIL; ++i){
            u16 xo;

            xo = arr[i]->avdv_okey;

            if(avcp->avc_okey == xo){
               cp = (f & a_AMV_VF_BOOL) ? n_1 : arr[i]->avdv_value;
               /* Remove this entry, hope entire block becomes no-op asap */
               do
                  arr[i] = arr[i + 1];
               while(arr[i++] != NIL);

               if(!(avlf & a_AMV_VLOOK_I3VAL_NONEW))
                  goto jnewval;
               if(avlf & a_AMV_VLOOK_I3VAL_NONEW_REPORT)
                  avp = R(struct a_amv_var*,-1);
               goto jleave;
            }

            /* Case insensitively sorted, break asap */
            if(avcp->avc_okey < xo)
               break;
         }
      }

      /* */
jdefval:
      if(UNLIKELY(f & a_AMV_VF_DEFVAL) != 0){
         /* TODO Use at least the su/prime.c approach to speed this up! */
         for(i = 0; i < a_AMV_VAR_DEFVALS_CNT; ++i){
            if(a_amv_var_defvals[i].avdv_okey == avcp->avc_okey){
               cp = (f & a_AMV_VF_BOOL) ? n_1
                     : a_amv_var_defvals[i].avdv_value;
               goto jnewval;
            }
         }
         ASSERT(0); /* Not reached */
      }

      /* The virtual variables */
      if(UNLIKELY((f & a_AMV_VF_VIRT) != 0)){
         /* TODO Use at least the su/prime.c approach to speed this up! */
         for(i = 0; i < a_AMV_VAR_VIRTS_CNT; ++i)
            if(a_amv_var_virts[i].avv_okey == avcp->avc_okey){
               avp = UNCONST(struct a_amv_var*,a_amv_var_virts[i].avv_var);
               goto jleave;
            }
         ASSERT(0); /* Not reached */
      }
   }

jerr:
   avp = NIL;
jleave:
   avcp->avc_var = avp;

j_leave:
#ifdef a_AMV_VAR_HAS_OBSOLETE
   if(UNLIKELY((avmp = avcp->avc_map) != NIL &&
         (avmp->avm_flags & a_AMV_VF_OBSOLETE) != 0) &&
         ((avlf & a_AMV_VLOOK_LOG_OBSOLETE) ||
            (avp != NIL && avp != R(struct a_amv_var*,-1))))
      a_amv_var_obsolete(avcp->avc_name);
#endif

   if(UNLIKELY(!(avlf & a_AMV_VLOOK_I3VAL_NONEW)) &&
         UNLIKELY(n_poption & n_PO_VVV) &&
         avp != R(struct a_amv_var*,-1) && avcp->avc_okey != ok_v_log_prefix){
      /* I18N: Variable "name" is set to "value" */
      n_err(_("*%s* is %s\n"),
         n_shexp_quote_cp(avcp->avc_name, FAL0),
         (avp == NIL ? _("not set")
            : ((avp->av_flags & a_AMV_VF_BOOL) ? _("boolean set")
               : n_shexp_quote_cp(avp->av_value, FAL0))));
   }

   NYD2_OU;
   return (avp != NIL);

jnewval:
   ASSERT(avmp != NIL);
      ASSERT(!(avlf & a_AMV_VLOOK_LOCAL));
   ASSERT(f == avmp->avm_flags);
   ASSERT(!(f & a_AMV_VF_EXT_LOCAL));

   /* E.g., $TMPDIR may be set to non-existent, so we need to be able to catch
    * that and redirect to a possible default value */
   if((f & a_AMV_VF_VIP) &&
         !a_amv_var_check_vips(a_AMV_VIP_SET_PRE, avcp->avc_okey, &cp)){
#ifdef mx_HAVE_SETENV
      if(f & (a_AMV_VF_IMPORT | a_AMV_VF_ENV))
         unsetenv(avcp->avc_name);
#endif
      if(UNLIKELY(f & a_AMV_VF_DEFVAL) != 0){
         if(avlf & a_AMV_VLOOK_BELLA_CIAO_CIAO_CIAO)
            n_panic(_("Cannot set *%s* to default value: %s"),
               n_shexp_quote_cp(avcp->avc_name, FAL0),
               n_shexp_quote_cp((cp == NIL ? su_empty : cp), FAL0));
         avlf |= a_AMV_VLOOK_BELLA_CIAO_CIAO_CIAO;
         goto jdefval;
      }
      goto jerr;
   }else{
      struct a_amv_var **avpp;
      uz l;

      l = su_cs_len(avcp->avc_name) +1;
      avcp->avc_var =
      avp = su_ALLOC(VSTRUCT_SIZEOF(struct a_amv_var,av_name) + l);
      su_mem_set(avp, 0, VSTRUCT_SIZEOF(struct a_amv_var,av_name));
      avp->av_link = *(avpp = &a_amv_vars[avcp->avc_prime]);
      *avpp = avp;
      ASSERT(!avcp->avc_is_chain_variant);
      avp->av_flags = f;
      avp->av_value = a_amv_var_copy(cp);
      su_mem_copy(avp->av_name, avcp->avc_name, l);

      if(f & a_AMV_VF_ENV)
         a_amv_var__putenv(avcp, avp);
      if(f & a_AMV_VF_VIP)
         a_amv_var_check_vips(a_AMV_VIP_SET_POST, avcp->avc_okey, &cp);
      goto jleave;
   }
}

static char const *
a_amv_var_vsc_global(struct a_amv_var_carrier *avcp){
   char iencbuf[su_IENC_BUFFER_SIZE];
   char const *rv;
   s32 *ep;
   struct a_amv_var_map const *avmp;
   NYD2_IN;

   /* Not function local, TODO but lazy evaluated for now */
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
      rv = su_ienc(iencbuf, *ep, 10, su_IENC_MODE_SIGNED_TYPE);
      break;
   }
   n_PS_ROOT_BLOCK(n_var_okset(avcp->avc_okey, R(up,rv)));

   avcp->avc_hash = avmp->avm_hash;
   avcp->avc_map = avmp;
   rv = a_amv_var_lookup(avcp, a_AMV_VLOOK_NONE)
         ? avcp->avc_var->av_value : NIL;

   NYD2_OU;
   return rv;
}

static char const *
a_amv_var_vsc_multiplex(struct a_amv_var_carrier *avcp){
   char iencbuf[su_IENC_BUFFER_SIZE];
   s32 e;
   uz i;
   char const *rv;
   NYD2_IN;

   i = su_cs_len(rv = &avcp->avc_name[1]);

   /* ERR, ERRDOC, ERRNAME, plus *-NAME variants.
    * As well as ERRQUEUE-. */
   if(rv[0] == 'E' && i >= 3 && rv[1] == 'R' && rv[2] == 'R'){
      if(i == 3){
         e = n_pstate_err_no;
         goto jeno;
      }else if(rv[3] == '-'){
         e = su_err_from_name(&rv[4]);
jeno:
         switch(e){
         case 0: rv = n_0; break;
         case 1: rv = n_1; break;
         default:
            /* XXX Need to convert number to string yet */
            rv = savestr(su_ienc(iencbuf, e, 10, su_IENC_MODE_SIGNED_TYPE));
            break;
         }
         goto jleave;
      }else if(i >= 6){
         if(!su_mem_cmp(&rv[3], "DOC", 3)){
            rv += 6;
            switch(*rv){
            case '\0': e = n_pstate_err_no; break;
            case '-': e = su_err_from_name(&rv[1]); break;
            default: goto jerr;
            }
            rv = su_err_doc(e);
            goto jleave;
         }else if(i >= 7 && !su_mem_cmp(&rv[3], "NAME", 4)){
            rv += 7;
            switch(*rv){
            case '\0': e = n_pstate_err_no; break;
            case '-': e = su_err_from_name(&rv[1]); break;
            default: goto jerr;
            }
            rv = su_err_name(e);
            goto jleave;
         }else if(i >= 14){
            if(!su_mem_cmp(&rv[3], "QUEUE-COUNT", 11)){
               if(rv[14] == '\0'){
                  e = 0
#ifdef mx_HAVE_ERRORS
                        | n_pstate_err_cnt
#endif
                  ;
                  goto jeno;
               }
            }else if(i >= 15 && !su_mem_cmp(&rv[3], "QUEUE-EXISTS", 12)){
               if(rv[15] == '\0'){
#ifdef mx_HAVE_ERRORS
                  if(n_pstate_err_cnt != 0)
                     rv = V_(n_error);
                  else
#endif
                     rv = su_empty;
                  goto jleave;
               }
            }
         }
      }
   }

jerr:
   rv = NIL;

jleave:
   NYD2_OU;
   return rv;
}

static char const *
a_amv_var_vsc_pospar(struct a_amv_var_carrier *avcp){
   uz i, j;
   u16 argc;
   char const *rv, **argv;
   NYD2_IN;

   rv = NIL;

   /* If in a macro/xy.. */
   if(a_AMV_HAVE_LOPTS_AKA_LOCAL()/* TODO compose mode.. */){
      boole ismacky;
      struct a_amv_mac_call_args *amcap;

      amcap = a_amv_lopts->as_amcap;

      argc = amcap->amca_pospar->app_count;
      argv = amcap->amca_pospar->app_dat;
      argv += amcap->amca_pospar->app_idx;

      /* ..in a `call'ed macro only, to be exact.  Or in a_AMV_MACKY_MACK */
      if(!(ismacky = (amcap->amca_amp == a_AMV_MACKY_MACK)) &&
            amcap->amca_amp != NIL && (amcap->amca_ps_hook_mask ||
               (amcap->amca_amp->am_flags & a_AMV_MF_TYPE_MASK
                  ) == a_AMV_MF_ACCOUNT))
         goto jleave;

      if(avcp->avc_special_cat == a_AMV_VSC_POSPAR){
         if(avcp->avc_special_prop > 0){
            if(argc >= avcp->avc_special_prop)
               rv = argv[avcp->avc_special_prop - 1];
         }else if(ismacky || amcap->amca_amp == NIL)
            rv = amcap->amca_name;
         else if((rv = mx_go_ctx_parent_name()) == NIL)
            rv = su_empty;
         goto jleave;
      }
      /* MACKY_MACK does not know about [*@#] */
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
         j += su_cs_len(argv[i]) + 1;
      if(j == 0)
         rv = su_empty;
      else{
         char *cp;

         rv = cp = su_AUTO_ALLOC(j);
         for(i = j = 0; i < argc; ++i){
            j = su_cs_len(argv[i]);
            su_mem_copy(cp, argv[i], j);
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
      char iencbuf[su_IENC_BUFFER_SIZE];

      rv = savestr(su_ienc(iencbuf, argc, 10, su_IENC_MODE_NONE));
      }break;
   default:
      rv = su_empty;
      break;
   }

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_amv_var_set(struct a_amv_var_carrier *avcp, char const *value,
      BITENUM_IS(u32,a_amv_var_setclr_flags) avscf){
   struct a_amv_var *avp;
   char *oval;
   BITENUM_IS(u32,a_amv_var_flags) f;
   struct a_amv_var_map const *avmp;
   boole rv;
   NYD2_IN;

   if(value == NIL){
      rv = a_amv_var_clear(avcp, avscf);
      goto jleave;
   }

   avmp = avcp->avc_map;

   if(LIKELY(avmp != NIL)){
      rv = FAL0;
      f = avmp->avm_flags;

      if(LIKELY(!(avscf & a_AMV_VSETCLR_UNROLL))){
#ifdef a_AMV_VAR_HAS_OBSOLETE
         if(UNLIKELY((f & a_AMV_VF_OBSOLETE) != 0))/* TODO v15compat only D_V*/
            a_amv_var_obsolete(avcp->avc_name);
#endif

         /* Validity checks */
         if(UNLIKELY((f & a_AMV_VF_RDONLY) != 0 && !(n_pstate & n_PS_ROOT))){
            value = N_("Variable is read-only: %s\n");
            goto jeavmp;
         }
         if(UNLIKELY((f & a_AMV_VF_NOTEMPTY) && *value == '\0')){
            value = N_("Variable must not be empty: %s\n");
            goto jeavmp;
         }
         if(UNLIKELY((f & a_AMV_VF_NUM) && !a_amv_var_check_num(value, FAL0))){
            value = N_("Variable value not a number or out of range: %s\n");
            goto jeavmp;
         }
         if(UNLIKELY((f & a_AMV_VF_POSNUM) &&
               !a_amv_var_check_num(value, TRU1))){
            value = _("Variable value not a number, negative, "
                  "or out of range: %s\n");
            goto jeavmp;
         }

         if(UNLIKELY((f & a_AMV_VF_IMPORT) != 0 &&
               !(n_psonce & n_PSO_STARTED) && !(n_pstate & n_PS_ROOT))){
            value = N_("Variable cannot be set in a resource file: %s\n");
            goto jeavmp;
         }

         /* Transformations */
         if(UNLIKELY(f & a_AMV_VF_LOWER)){
            char c;

            oval = savestr(value);
            value = oval;
            for(; (c = *oval) != '\0'; ++oval)
               *oval = su_cs_to_lower(c);
         }
      }

      /* Any more complicated inter-dependency? */
      if(UNLIKELY((f & a_AMV_VF_VIP) != 0 &&
            !a_amv_var_check_vips(a_AMV_VIP_SET_PRE, avcp->avc_okey, &value))){
         value = N_("Assignment of variable aborted: %s\n");
jeavmp:
         n_err(V_(value), avcp->avc_name);
         goto jleave;
      }
   }

   /* Lookup possibly existing var */
   ASSERT(!(avscf & a_AMV_VSETCLR_UNROLL) || !(avscf & a_AMV_VSETCLR_LOCAL));
   rv = TRU1;
   a_amv_var_lookup(avcp, (a_AMV_VLOOK_I3VAL_NONEW |
         ((avmp != NIL || (avscf & (a_AMV_VSETCLR_ENV | a_AMV_VSETCLR_UNROLL)))
            ? a_AMV_VLOOK_NONE : (a_AMV_VLOOK_LOCAL |
               ((avscf & a_AMV_VSETCLR_LOCAL) ? a_AMV_VLOOK_LOCAL_ONLY
                  : a_AMV_VLOOK_NONE)))));
   avp = avcp->avc_var;

   /* A `local' setting is never covered by `localopts' nor frozen.
    * Ditto all that can be skipped when _UNROLLing */
   if(avscf & a_AMV_VSETCLR_UNROLL)
      goto jno_lopts;

   if((avscf & a_AMV_VSETCLR_LOCAL) &&
         avmp == NIL && !(avscf & a_AMV_VSETCLR_ENV))
      goto jno_lopts;

   if(UNLIKELY(avp != NIL)){
      if(avp->av_flags & a_AMV_VF_EXT_LOCAL){
         ASSERT(!(avp->av_flags & a_AMV_VF_EXT__FROZEN_MASK));
         goto jno_lopts;
      }

      /* If this setting had been established via -S and we still have not
       * reached the _STARTED_CONFIG program state, silently ignore request!
       * xxx freezing is painfully complicated: cs_dict overlay!! */
      if(UNLIKELY(avp != NIL) &&
            UNLIKELY((avp->av_flags & a_AMV_VF_EXT__FROZEN_MASK) != 0)){
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

         /* Otherwise, if -S freezing was an `unset' request, be very simple
          * and avoid tampering with that very special case we are not really
          * prepared for just one more line of code: throw old thing away! */
         if(!(avp->av_flags & a_AMV_VF_EXT_FROZEN_UNSET))
            avp->av_flags &= ~a_AMV_VF_EXT__FROZEN_MASK;
         else{
            ASSERT(avp->av_value == su_empty);
            ASSERT(a_amv_vars[avcp->avc_prime] == avp);
            a_amv_vars[avcp->avc_prime] = avp->av_link;
            su_FREE(avp);
            avcp->avc_var = avp = NIL;
         }
      }
   }

   /* Optionally cover by `localopts' */
   if(UNLIKELY(a_AMV_HAVE_LOPTS_AKA_LOCAL()) &&
         (avmp == NIL || !(avmp->avm_flags & a_AMV_VF_NOLOPTS))){
      ASSERT(!(avscf & a_AMV_VSETCLR_UNROLL));
      ASSERT(avp == NIL || !(avp->av_flags & a_AMV_VF_EXT_LOCAL));
      a_amv_lopts_add(a_amv_lopts, avcp->avc_name, avcp->avc_var,
         ((avscf & a_AMV_VSETCLR_LOCAL) != 0));
   }

jno_lopts:
   if(avp != NIL){
joval_and_go:
      oval = avp->av_value;
      f = avp->av_flags;
   }else{
      uz l;
      struct a_amv_var **avpp;

      if(avmp == NIL && (avscf & (a_AMV_VSETCLR_LOCAL | a_AMV_VSETCLR_ENV)
            ) == a_AMV_VSETCLR_LOCAL){
         if((avpp = *a_amv_lopts->as_amcap->amca_local_vars) == NIL)
            avpp = *(a_amv_lopts->as_amcap->amca_local_vars =
                  su_CALLOC(sizeof(*a_amv_lopts->as_amcap->amca_local_vars)));

         avpp += avcp->avc_prime;
         f = a_AMV_VF_EXT_LOCAL;
      }else{
         avpp = &a_amv_vars[avcp->avc_prime];
         f = (avmp != NIL) ? avmp->avm_flags : a_AMV_VF_NONE;
      }

      l = su_cs_len(avcp->avc_name) +1;
      avcp->avc_var = avp =
            su_ALLOC(VSTRUCT_SIZEOF(struct a_amv_var,av_name) + l);
      su_mem_set(avp, 0, VSTRUCT_SIZEOF(struct a_amv_var,av_name));
      avp->av_link = *avpp;
      *avpp = avp;
      su_mem_copy(avp->av_name, avcp->avc_name, l);
      oval = UNCONST(char*,su_empty); /* _var_free */
   }

   if(avcp->avc_is_chain_variant)
      f |= a_AMV_VF_EXT_CHAIN;
   if(avscf & a_AMV_VSETCLR_ENV)
      f |= a_AMV_VF_ENV;
   if(avscf & a_AMV_VSETCLR_UNROLL_ENV_LINKED)
      f |= a_AMV_VF_EXT_LINKED;

   if(avmp == NIL || !(f & a_AMV_VF_BOOL))
      avp->av_value = a_amv_var_copy(value);
   /* Via `set' etc. the user may give even boolean options non-boolean
    * values, ignore that and force boolean */
   else{
      if(!(avscf & a_AMV_VSETCLR_UNROLL) && !(n_pstate & n_PS_ROOT) &&
            *value != '\0')
         n_err(_("Ignoring value of boolean variable: %s: %s\n"),
            avcp->avc_name, value);
      avp->av_value = UNCONST(char*,n_1);
   }

   /* A `local' setting can skip all the crude special things */
   if(!(f & a_AMV_VF_EXT_LOCAL)){
      if(f & (a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED))
         rv = a_amv_var__putenv(avcp, avp);

      if(f & a_AMV_VF_VIP)
         a_amv_var_check_vips(a_AMV_VIP_SET_POST, avcp->avc_okey, &value);

      f &= ~a_AMV_VF_EXT__FROZEN_MASK;
      if(!(n_psonce & n_PSO_STARTED_GETOPT) &&
            (n_poption & n_PO_S_FLAG_TEMPORARY) != 0)
         f |= a_AMV_VF_EXT_FROZEN;
   }else{
      ASSERT(!(avscf & a_AMV_VSETCLR_ENV));
      ASSERT(!(avscf & a_AMV_VSETCLR_UNROLL));
   }

   avp->av_flags = f;

   a_amv_var_free(oval);

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_amv_var__putenv(struct a_amv_var_carrier *avcp, struct a_amv_var *avp){
#ifndef mx_HAVE_SETENV
   char *cp;
#endif
   boole rv;
   NYD2_IN;

#ifdef mx_HAVE_SETENV
   rv = (setenv(avcp->avc_name, avp->av_value, 1) == 0);
#else
   cp = su_cs_dup(savecatsep(avcp->avc_name, '=', avp->av_value), 0);

   if((rv = (putenv(cp) == 0))){
      char *ocp;

      ocp = avp->av_env;
      avp->av_env = cp;
      cp = ocp;
   }

   if(cp != NIL)
      su_FREE(cp);
#endif

   NYD2_OU;
   return rv;
}

static boole
a_amv_var_clear(struct a_amv_var_carrier *avcp,
      BITENUM_IS(u32,a_amv_var_setclr_flags) avscf){
   struct a_amv_var **avpp, *avp;
   BITENUM_IS(u32,a_amv_var_flags) f;
   boole rv;
   struct a_amv_var_map const *avmp;
   NYD2_IN;

   avmp = avcp->avc_map;

   if(LIKELY(avmp != NIL)){
      rv = FAL0;
      f = avmp->avm_flags;

      if(LIKELY(!(avscf & a_AMV_VSETCLR_UNROLL))){
#ifdef a_AMV_VAR_HAS_OBSOLETE
         if(UNLIKELY((f & a_AMV_VF_OBSOLETE) != 0))/* TODO v15compat only D_V*/
            a_amv_var_obsolete(avcp->avc_name);
#endif

         /* Validity checks */
         if(UNLIKELY((f & a_AMV_VF_NODEL) != 0 && !(n_pstate & n_PS_ROOT))){
            n_err(_("Variable may not be unset: %s\n"), avcp->avc_name);
            goto jleave;
         }
      }

      if(UNLIKELY((f & a_AMV_VF_VIP) != 0 &&
            !a_amv_var_check_vips(a_AMV_VIP_CLEAR, avcp->avc_okey, NIL))){
         n_err(_("Clearance of variable aborted: %s\n"), avcp->avc_name);
         goto jleave;
      }
   }

   rv = TRU1;
   if(UNLIKELY(!a_amv_var_lookup(avcp,
         (a_AMV_VLOOK_I3VAL_NONEW | a_AMV_VLOOK_I3VAL_NONEW_REPORT |
          ((avmp != NIL ||
               (avscf & (a_AMV_VSETCLR_ENV | a_AMV_VSETCLR_UNROLL)))
            ? a_AMV_VLOOK_NONE : (a_AMV_VLOOK_LOCAL |
               ((avscf & a_AMV_VSETCLR_LOCAL) ? a_AMV_VLOOK_LOCAL_ONLY
                  : a_AMV_VLOOK_NONE))))))){
      ASSERT(avcp->avc_var == NIL || (avcp->avc_var->av_flags &
         (a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET)));

      /* Nothing to be done for `local' variables and when unrolling */
      if(avscf & (a_AMV_VSETCLR_LOCAL | a_AMV_VSETCLR_UNROLL))
         goto jleave;

      /* This may be a clearance request from the command line, via -S, and we
       * need to keep track of that!  Unfortunately we are not prepared for
       * this, really, so we need to create a fake entry that is known and
       * handled correctly by the lowermost variable layer!
       * xxx freezing is painfully complicated: cs_dict overlay */
      if(UNLIKELY(!(n_psonce & n_PSO_STARTED_GETOPT)) &&
            (n_poption & n_PO_S_FLAG_TEMPORARY)) Jfreeze:{
         uz l;

         l = su_cs_len(avcp->avc_name) +1;
         avp = su_ALLOC(VSTRUCT_SIZEOF(struct a_amv_var,av_name) + l);
         su_mem_set(avp, 0, VSTRUCT_SIZEOF(struct a_amv_var,av_name));
         avp->av_link = *(avpp = &a_amv_vars[avcp->avc_prime]);
         *avpp = avp;
         avp->av_value = UNCONST(char*,su_empty); /* Covered by _var_free()! */
         f = (avmp != NIL) ? avmp->avm_flags : 0;
         avp->av_flags = f | a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET;
         su_mem_copy(avp->av_name, avcp->avc_name, l);

         if((avscf & a_AMV_VSETCLR_ENV) || (f & a_AMV_VF_ENV))
            a_amv_var__clearenv(avcp->avc_name, NIL);
      }else if(avscf & a_AMV_VSETCLR_ENV){
jforce_env:
         if(!(rv = a_amv_var__clearenv(avcp->avc_name, NIL)))
            goto jerr_env_unset;
      }else{
         /* TODO "cannot unset undefined variable" not echoed in "ROBOT" state,
          * TODO should only be like that with "ignerr"! */
jerr_env_unset:
         if(!(n_pstate & (n_PS_ROOT | n_PS_ROBOT)) && (n_poption & n_PO_D_V))
            n_err(_("Cannot unset undefined variable: %s\n"), avcp->avc_name);
      }

      goto jleave;
   }

   avp = avcp->avc_var;
   ASSERT(avp != NIL);

   if(UNLIKELY(avp == R(struct a_amv_var*,-1))){
      /* Clearance request from command line, via -S?  As above..
       * xxx freezing is painfully complicated: cs_dict overlay */
      if(UNLIKELY(!(n_psonce & n_PSO_STARTED_GETOPT) &&
            (n_poption & n_PO_S_FLAG_TEMPORARY) != 0))
         goto Jfreeze;
      avcp->avc_var = NIL;
      if(avscf & a_AMV_VSETCLR_ENV)
         goto jforce_env;
      goto jleave;
   }

   /* `local' variables bypass "frozen" checks and `localopts' coverage etc.
    * Likewise when _UNROLLing */
   f = avp->av_flags;
   if(UNLIKELY(avscf & a_AMV_VSETCLR_UNROLL) ||
         UNLIKELY(f & a_AMV_VF_EXT_LOCAL))
      goto jdefault_path;

   /* If this setting has been established via -S and we still have not reached
    * the _STARTED_CONFIG program state, silently ignore request!
    * xxx freezing is painfully complicated: cs_dict overlay */
   if(UNLIKELY((f & a_AMV_VF_EXT__FROZEN_MASK) != 0)){
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
               avp->av_value = UNCONST(char*,su_empty); /* _var_free()! */
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

   if(UNLIKELY(a_AMV_HAVE_LOPTS_AKA_LOCAL()) &&
         (avmp == NIL || !(avmp->avm_flags & a_AMV_VF_NOLOPTS))){
      ASSERT(!(avp->av_flags & a_AMV_VF_EXT_LOCAL));
      a_amv_lopts_add(a_amv_lopts, avcp->avc_name, avcp->avc_var,
         ((avscf & a_AMV_VSETCLR_LOCAL) != 0));
   }

jdefault_path:
   ASSERT(avp == avcp->avc_var);
   ASSERT(avp != NIL);
   avcp->avc_var = NIL;
   avpp = &(((f = avp->av_flags) & a_AMV_VF_EXT_LOCAL)
         ? *a_amv_lopts->as_amcap->amca_local_vars : a_amv_vars
         )[avcp->avc_prime];
   ASSERT(*avpp == avp); /* (always listhead after lookup()) */
   *avpp = (*avpp)->av_link;

   if(f & a_AMV_VF_ENV)
      rv = a_amv_var__clearenv(avp->av_name, avp);

   a_amv_var_free(avp->av_value);
   su_FREE(avp);

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_amv_var__clearenv(char const *name, struct a_amv_var *avp){
   extern char **environ;
   char **ecpp;
   boole rv;
   NYD2_IN;
   UNUSED(avp);

   rv = FAL0;
   ecpp = environ;

#ifndef mx_HAVE_SETENV
   if(avp != NIL && avp->av_env != NIL){
      for(; *ecpp != NIL; ++ecpp)
         if(*ecpp == avp->av_env){
            do
               ecpp[0] = ecpp[1];
            while(*ecpp++ != NIL);

            su_FREE(avp->av_env);
            avp->av_env = NIL;
            rv = TRU1;
            break;
         }
   }else
#endif
        {
      uz l;

      if((l = su_cs_len(name)) > 0){
         for(; *ecpp != NIL; ++ecpp)
            if(!su_cs_cmp_n(*ecpp, name, l) && (*ecpp)[l] == '='){
#ifdef mx_HAVE_SETENV
               unsetenv(name);
#else
               do
                  ecpp[0] = ecpp[1];
               while(*ecpp++ != NIL);
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
a_amv_var_show_instantiate_all(void){
   uz i;
   NYD2_IN;

   for(i = a_AMV_VAR_I3VALS_CNT; i-- > 0;)
      n_var_oklook(a_amv_var_i3vals[i].avdv_okey);

   for(i = a_AMV_VAR_DEFVALS_CNT; i-- > 0;)
      n_var_oklook(a_amv_var_defvals[i].avdv_okey);

   NYD2_OU;
}

static void
a_amv_var_show_all(void){
   struct n_string msg, *msgp;
   FILE *fp;
   uz no, i;
   struct a_amv_var *avp;
   char const **vacp, **cap;
   NYD2_IN;

   a_amv_var_show_instantiate_all();

   if((fp = mx_fs_tmp_open(NIL, "setlist", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL)
      fp = n_stdout;

   for(no = i = 0; i < a_AMV_PRIME; ++i)
      for(avp = a_amv_vars[i]; avp != NIL; avp = avp->av_link)
         ++no;
   no += a_AMV_VAR_VIRTS_CNT;

   vacp = su_AUTO_ALLOC(no * sizeof(*vacp));

   for(cap = vacp, i = 0; i < a_AMV_PRIME; ++i)
      for(avp = a_amv_vars[i]; avp != NIL; avp = avp->av_link)
         *cap++ = avp->av_name;
   for(i = a_AMV_VAR_VIRTS_CNT; i-- > 0;)
      *cap++ = a_amv_var_virts[i].avv_var->av_name;

   if(no > 1)
      su_sort_shell_vpp(S(void const**,vacp), no, su_cs_toolbox.tb_compare);

   msgp = &msg;
   msgp = n_string_reserve(n_string_creat(msgp), 80);
   for(i = 0, cap = vacp; no != 0; ++cap, --no)
      i += a_amv_var_show(*cap, fp, msgp, FAL0);
   n_string_gut(&msg);

   if(fp != n_stdout){
      page_or_print(fp, i);

      mx_fs_close(fp);
   }else
      clearerr(fp);

   NYD2_OU;
}

static uz
a_amv_var_show(char const *name, FILE *fp, struct n_string *msgp, boole local){
   /* XXX a_amv_var_show(): if we iterate over all the actually set variables
    * XXX via a_amv_var_show_all() there is no need to call
    * XXX a_amv_var_revlookup() at all!  Revisit this call chain */
   struct a_amv_var_carrier avc;
   char const *quote;
   struct a_amv_var *avp;
   boole isset;
   uz i;
   NYD2_IN;

   msgp = n_string_trunc(msgp, 0);
   i = 0;

   a_amv_var_revlookup(&avc, name, TRU1);
   isset = a_amv_var_lookup(&avc,
         (local ? a_AMV_VLOOK_LOCAL : a_AMV_VLOOK_NONE));
   avp = avc.avc_var;

   if(n_poption & n_PO_D_V){
      if(avc.avc_map == NIL){
         msgp = n_string_push_cp(msgp, "#* free-form custom variable");
         i = 1;

         if(isset){
            struct{
               u32 flag;
               char msg[24];
            } const tbase[] = {
               {a_AMV_VF_ENV, "sync-environ"},

               {a_AMV_VF_EXT_LOCAL, "`local'"},
               {a_AMV_VF_EXT_LINKED, "`environ' linked"},
               {a_AMV_VF_EXT_FROZEN, "frozen (set via -S)\0"}
            }, *tp;
            ASSERT((avp->av_flags & ~a_AMV_VF_EXT__CUSTOM_MASK) == 0);
            ASSERT(!(avp->av_flags & a_AMV_VF_EXT_LOCAL) ||
               (avp->av_flags & ~a_AMV_VF_EXT__LOCAL_MASK) == 0);

            for(tp = tbase; PCMP(tp, <, &tbase[NELEM(tbase)]); ++tp){
               if(avp->av_flags & tp->flag){
                  msgp = n_string_push_c(msgp, ',');
                  msgp = n_string_push_cp(msgp, tp->msg);
               }
            }
         }
      }else{
         struct{
            u32 flag;
            char msg[24];
         } const tbase[] = {
            {a_AMV_VF_CHAIN, "variable chain"},
            {a_AMV_VF_VIRT, "virtual"},
            {a_AMV_VF_RDONLY, "read-only"},
            {a_AMV_VF_NODEL, "nodelete"},
            {a_AMV_VF_I3VAL, "initial-value"},
            {a_AMV_VF_DEFVAL, "default-value"},
            {a_AMV_VF_IMPORT, "import-environ-first\0"},
            {a_AMV_VF_ENV, "sync-environ"},
            {a_AMV_VF_NOLOPTS, "no-localopts"},
            {a_AMV_VF_NOTEMPTY, "notempty"},
            {a_AMV_VF_NUM, "number"},
            {a_AMV_VF_POSNUM, "positive-number"},
            {a_AMV_VF_OBSOLETE, "obsoleted"},

            {a_AMV_VF_EXT_LINKED, "`environ' linked"},
            {a_AMV_VF_EXT_FROZEN, "frozen (set via -S)\0"}
         }, *tp;
         ASSERT(!isset || ((avp->av_flags & a_AMV_VF__MASK) ==
            (avc.avc_map->avm_flags & a_AMV_VF__MASK)));

         for(tp = tbase; PCMP(tp, <, &tbase[NELEM(tbase)]); ++tp){
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
      }

      if(isset){
         if(avp->av_flags & a_AMV_VF_EXT_FROZEN){
            msgp = n_string_push_c(msgp, (i++ == 0 ? '#' : ','));
            msgp = n_string_push_cp(msgp, "(un)?set via -S");
         }
      }

      if(i > 0)
         msgp = n_string_push_cp(msgp, ":\n  ");
   }

   /* (Read-only variables are generally shown via comments..) */
   if(!isset || (avp->av_flags & a_AMV_VF_RDONLY)){
      msgp = n_string_push_c(msgp, n_ns[0]);
      if(!isset){
         if(avc.avc_map != NIL && (avc.avc_map->avm_flags & a_AMV_VF_BOOL))
            msgp = n_string_push_cp(msgp, "boolean; ");
         msgp = n_string_push_cp(msgp, "unset: ");
         msgp = n_string_push_cp(msgp, n_shexp_quote_cp(name, FAL0));
         goto jleave;
      }
   }

   UNINIT(quote, NIL);
   /* C99 */{
   boole v15_compat; /* Temporary block */

   quote = ok_vlook(v15_compat);
   v15_compat = (quote != NIL && *quote != '\0');

   if(!(avp->av_flags & a_AMV_VF_BOOL)){
      quote = n_shexp_quote_cp(avp->av_value, TRU1);
      if(!v15_compat && su_cs_cmp(quote, avp->av_value))
         msgp = n_string_push_cp(msgp, "wysh ");
   }else if(!v15_compat && (n_poption & n_PO_D_V))
      msgp = n_string_push_cp(msgp, "wysh "); /* (for shell-style comment) */
   }

   if(avc.avc_map == NIL &&
         (avp->av_flags & (a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED)))
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

   NYD2_OU;
   return (i > 0 ? 2 : 1);
}

static boole
a_amv_var_c_set(char const **ap, BITENUM_IS(u32,a_amv_var_setclr_flags) avscf){
   char *cp2, *varbuf, c;
   char const *cp;
   boole rv;
   NYD2_IN;

   for(rv = TRU1; (cp = *ap++) != NIL;){
      /* Isolate key */
      cp2 = varbuf = su_AUTO_ALLOC(su_cs_len(cp) +1);

      for(; (c = *cp) != '=' && c != '\0'; ++cp)
         *cp2++ = c;
      *cp2 = '\0';
      if(c == '\0')
         cp = UNCONST(char*,su_empty);
      else
         ++cp;

      if(varbuf == cp2){
         n_err(_("Empty variable name ignored\n"));
         rv = FAL0;
      }else if(!a_amv_var_check_name(varbuf,
            ((avscf & a_AMV_VSETCLR_ENV) != 0))){
         /* Log done */
         rv = FAL0;
      }else{
         struct a_amv_var_carrier avc;
         boole isunset;

         if((isunset = (varbuf[0] == 'n' && varbuf[1] == 'o'))){
            if(c != '\0')
               n_err(_("Un`set'ting via \"no\" takes no value: %s=%s\n"),
                  varbuf, n_shexp_quote_cp(cp, FAL0));
            varbuf = &varbuf[2];
         }

         a_amv_var_revlookup(&avc, varbuf, TRU1);

         if(isunset)
            cp = NIL;
         if(!a_amv_var_set(&avc, cp, avscf))
            rv = FAL0;
      }
   }

   NYD2_OU;
   return rv;
}

#ifdef a_AMV_VAR_HAS_OBSOLETE
static void
a_amv_var_obsolete(char const *name){
   static struct su_cs_dict a_csd__obsol, *a_csd_obsol;
   NYD2_IN;

   if(!su_state_has(su_STATE_REPRODUCIBLE)){
      if(UNLIKELY(a_csd_obsol == NIL)) /* XXX atexit cleanup */
         a_csd_obsol = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_csd__obsol, (su_CS_DICT_POW2_SPACED |
                  su_CS_DICT_HEAD_RESORT | su_CS_DICT_ERR_PASS), NIL), 2);

      if(UNLIKELY(!su_cs_dict_has_key(a_csd_obsol, name))){
         su_cs_dict_insert(a_csd_obsol, name, NIL);
         n_err(_("Warning: variable superseded or obsoleted: %s\n"), name);
      }
   }

   NYD2_OU;
}
#endif

FL int
c_define(void *v){
   int rv;
   char **args;
   NYD_IN;

   rv = 1;

   if((args = v)[0] == NIL){
      rv = (a_amv_mac_show(a_AMV_MF_NONE) == FAL0);
      goto jleave;
   }

   if(args[1] == NIL || args[1][0] != '{' || args[1][1] != '\0' ||
         args[2] != NIL){
      mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("define"), NIL);
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
   while(*++args != NIL);

   NYD_OU;
   return rv;
}

FL int
c_call(void *vp){
   int rv;
   NYD_IN;

   rv = a_amv_mac_call(vp, FAL0, NIL);

   NYD_OU;
   return rv;
}

FL void *
mx_xcall_exchange_lopts(void *vp){
   struct a_amv_lostack *losp;
   NYD2_IN;

   losp = vp;
   vp = losp->as_lopts;
   losp->as_lopts = NIL;

   NYD2_OU;
   return vp;
}

FL int
mx_xcall(void *vp, void *lospopts){
   int rv;
   NYD_IN;

   rv = a_amv_mac_call(vp, FAL0, lospopts);

   NYD_OU;
   return rv;
}

FL int
c_call_if(void *vp){
   int rv;
   NYD_IN;

   rv = a_amv_mac_call(vp, TRU1, NIL);

   NYD_OU;
   return rv;
}

FL void
mx_account_leave(void){
   /* Note no care for *account* here */
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   char const *cp;
   char *var;
   NYD_IN;

   if(a_amv_acc_curr != NIL){
      /* Is there a cleanup hook? */
      var = savecat("on-account-cleanup-", a_amv_acc_curr->am_name);
      if((cp = n_var_vlook(var, FAL0)) != NIL ||
            (cp = ok_vlook(on_account_cleanup)) != NIL){
         if((amp = a_amv_mac_lookup(cp, NIL, a_AMV_MF_NONE)) != NIL){
            amcap = su_LOFI_CALLOC(sizeof *amcap);
            amcap->amca_name = cp;
            amcap->amca_amp = amp;
            amcap->amca_unroller = &a_amv_acc_curr->am_lopts;
            amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
            amcap->amca_no_xcall = TRU1;
            amcap->amca_pospar = &amcap->amca__pospar;
            n_pstate |= n_PS_HOOK;
            (void)a_amv_mac_exec(amcap, NIL);
            n_pstate &= ~n_PS_HOOK_MASK;
         }else
            n_err(_("*on-account-leave* hook %s does not exist\n"),
               n_shexp_quote_cp(cp, FAL0));
      }

      /* `localopts'? */
      if(a_amv_acc_curr->am_lopts != NIL)
         a_amv_lopts_unroll(&a_amv_acc_curr->am_lopts);

      /* For accounts this lingers */
      --a_amv_acc_curr->am_refcnt;
      if(a_amv_acc_curr->am_flags & a_AMV_MF_DELETE)
         a_amv_mac_free(a_amv_acc_curr);
   }

   NYD_OU;
}

FL int
c_account(void *v){
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   char **args;
   int rv, i, oqf, nqf;
   NYD_IN;

   rv = 1;

   if((args = v)[0] == NIL){
      rv = (a_amv_mac_show(a_AMV_MF_ACCOUNT) == FAL0);
      goto jleave;
   }

   if(args[1] && args[1][0] == '{' && args[1][1] == '\0'){
      if(args[2] != NIL){
         mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("account"), NIL);
         goto jleave;
      }
      if(!su_cs_cmp_case(args[0], ACCOUNT_NULL)){
         n_err(_("account: cannot use reserved name: %s\n"), ACCOUNT_NULL);
         goto jleave;
      }
      rv = (a_amv_mac_def(args[0], a_AMV_MF_ACCOUNT) == FAL0);
      goto jleave;
   }

   if(n_pstate & n_PS_HOOK_MASK){
      n_err(_("account: cannot change account from within a hook\n"));
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   amp = NIL;
   if(su_cs_cmp_case(args[0], ACCOUNT_NULL) != 0 &&
         (amp = a_amv_mac_lookup(args[0], NIL, a_AMV_MF_ACCOUNT)) == NIL){
      n_err(_("account: account does not exist: %s\n"), args[0]);
      goto jleave;
   }

   oqf = savequitflags();

   /* Shutdown the active account */
   if(a_amv_acc_curr != NIL)
      mx_account_leave();

   a_amv_acc_curr = amp;

   /* And switch to any non-"null" account */
   if(amp != NIL){
      ASSERT(amp->am_lopts == NIL);
      n_PS_ROOT_BLOCK(ok_vset(account, amp->am_name));
      amcap = su_LOFI_CALLOC(sizeof *amcap);
      amcap->amca_name = amp->am_name;
      amcap->amca_amp = amp;
      amcap->amca_unroller = &amp->am_lopts;
      amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
      amcap->amca_no_xcall = TRU1;
      amcap->amca_pospar = &amcap->amca__pospar;
      ++amp->am_refcnt; /* We may not run 0 to avoid being deleted! */
      if(!a_amv_mac_exec(amcap, NIL) || n_pstate_ex_no != 0){
         /* XXX account switch incomplete, unroll? */
         mx_account_leave();
         n_PS_ROOT_BLOCK(ok_vclear(account));
         n_err(_("account: failed to switch to account: %s\n"), amp->am_name);
         goto jleave;
      }
   }else
      n_PS_ROOT_BLOCK(ok_vclear(account));

   /* Otherwise likely initial setfile() in a_main_rcv_mode() will pick up */
   if(n_psonce & n_PSO_STARTED){
      ASSERT(!(n_pstate & n_PS_HOOK_MASK));
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
   while(*++args != NIL);

   NYD_OU;
   return rv;
}

FL int
c_localopts(void *vp){
   BITENUM_IS(u8,a_amv_loflags) alf, alm;
   char const **argv;
   int rv;
   NYD_IN;

   n_OBSOLETE("`localopts': please use \"local [environ] set\" for variables, "
      "and \"local call\" etc. for macros");

   rv = n_EXIT_ERR;

   if((argv = vp)[1] == NIL || su_cs_starts_with_case("scope", (++argv)[-1]))
      alf = alm = a_AMV_LF_SCOPE;
   else if(su_cs_starts_with_case("call", argv[-1]))
      alf = a_AMV_LF_CALL, alm = a_AMV_LF_CALL_MASK;
   else if(su_cs_starts_with_case("call-fixate", argv[-1]))
      alf = a_AMV_LF_CALL_FIXATE, alm = a_AMV_LF_CALL_MASK;
   else{
jesynopsis:
      mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("localopts"), NIL);
      goto jleave;
   }

   if(alf == a_AMV_LF_SCOPE &&
         (a_amv_lopts->as_loflags & a_AMV_LF_SCOPE_FIXATE)){
      if(n_poption & n_PO_D_V)
         n_err(_("Cannot turn off `localopts', setting is fixated\n"));
      goto jleave;
   }

   if((rv = n_boolify(*argv, UZ_MAX, FAL0)) < FAL0)
      goto jesynopsis;
   a_amv_lopts->as_loflags &= ~alm;
   if(rv > FAL0)
      a_amv_lopts->as_loflags |= alf;

   rv = n_EXIT_OK;
jleave:
   NYD_OU;
   return rv;
}

FL int
c_shift(void *vp){ /* xxx move to bottom, not in macro part! */
   struct a_amv_pospar *appp;
   u16 i;
   int rv;
   NYD_IN;

   rv = 1;

   if((vp = *S(char**,vp)) == NIL)
      i = 1;
   else{
      s16 sib;

      if((su_idec_s16_cp(&sib, vp, 10, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || sib < 0){
         n_err(_("shift: invalid argument: %s\n"), vp);
         goto jleave;
      }
      i = S(u16,sib);
   }

   /* If in in a macro/xy */
   if(mx_go_ctx_is_macro()){
      ASSERT(a_AMV_HAVE_LOPTS_AKA_LOCAL());
      appp = a_amv_lopts->as_amcap->amca_pospar;
   }else
      appp = &a_amv_pospar;

   if(i > appp->app_count){
      n_err(_("shift: cannot shift %hu of %hu parameters\n"),
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
   char const **argv;
   int rv;
   NYD_IN;

   mx_go_input_force_eof();

   n_pstate_err_no = su_ERR_NONE;
   rv = n_EXIT_OK;

   if((argv = vp)[0] != NIL){
      s32 i;

      if((su_idec_s32_cp(&i, argv[0], 10, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) == su_IDEC_STATE_CONSUMED && i >= 0)
         rv = S(int,i);
      else{
         n_err(_("return: return value argument is invalid: %s\n"),
            argv[0]);
         n_pstate_err_no = su_ERR_INVAL;
         rv = n_EXIT_ERR;
      }

      if(argv[1] != NIL){
         if((su_idec_s32_cp(&i, argv[1], 10, NIL
                  ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
               ) == su_IDEC_STATE_CONSUMED && i >= 0)
            n_pstate_err_no = i;
         else{
            n_err(_("return: error number argument is invalid: %s\n"),
               argv[1]);
            n_pstate_err_no = su_ERR_INVAL;
            rv = n_EXIT_ERR;
         }
      }
   }

   NYD_OU;
   return rv;
}

FL void
temporary_on_xy_hook_caller(char const *hname, char const *mac,
      boole sigs_held){
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   NYD_IN;

   if(mac != NIL){
      if((amp = a_amv_mac_lookup(mac, NIL, a_AMV_MF_NONE)) != NIL){
         if(sigs_held)
            mx_sigs_all_rele();
         amcap = su_LOFI_CALLOC(sizeof *amcap);
         amcap->amca_name = mac;
         amcap->amca_amp = amp;
         amcap->amca_ps_hook_mask = TRU1;
         amcap->amca_no_xcall = TRU1;
         amcap->amca_pospar = &amcap->amca__pospar;
         n_pstate &= ~n_PS_HOOK_MASK;
         n_pstate |= n_PS_HOOK;
         a_amv_mac_exec(amcap, NIL);
         if(sigs_held)
            mx_sigs_all_holdx();
      }else
         n_err(_("*%s* macro does not exist: %s\n"), hname, mac);
   }

   NYD_OU;
}

FL boole
temporary_folder_hook_check(boole nmail){ /* TODO temporary, v15: drop */
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   uz len;
   char const *cp;
   char *var;
   boole rv;
   NYD_IN;

   rv = TRU1;
   var = su_AUTO_ALLOC(len = su_cs_len(mailname) +
         sizeof("folder-hook-") -1 +1);

   /* First try the fully resolved path */
   snprintf(var, len, "folder-hook-%s", mailname);
   if((cp = n_var_vlook(var, FAL0)) != NIL)
      goto jmac;

   /* If we are under *folder*, try the usual +NAME syntax, too */
   if(displayname[0] == '+'){
      char *x;

      for(x = &mailname[len]; x != mailname; --x)
         if(x[-1] == '/'){
            snprintf(var, len, "folder-hook-+%s", x);
            if((cp = n_var_vlook(var, FAL0)) != NIL)
               goto jmac;
            break;
         }
   }

   /* Plain *folder-hook* is our last try */
   if((cp = ok_vlook(folder_hook)) == NIL)
      goto jleave;

jmac:
   if((amp = a_amv_mac_lookup(cp, NIL, a_AMV_MF_NONE)) == NIL){
      n_err(_("Cannot call *folder-hook* for %s: macro does not exist: %s\n"),
         n_shexp_quote_cp(displayname, FAL0), cp);
      rv = FAL0;
      goto jleave;
   }

   amcap = su_LOFI_CALLOC(sizeof *amcap);
   amcap->amca_name = cp;
   amcap->amca_amp = amp;
   n_pstate &= ~n_PS_HOOK_MASK;
   if(nmail)
      n_pstate |= n_PS_HOOK_NEWMAIL;
   else{
      amcap->amca_unroller = &a_amv_folder_hook_lopts;
      n_pstate |= n_PS_HOOK;
   }
   amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
   amcap->amca_ps_hook_mask = TRU1;
   amcap->amca_no_xcall = TRU1;
   amcap->amca_pospar = &amcap->amca__pospar;
   rv = a_amv_mac_exec(amcap, NIL);
   n_pstate &= ~n_PS_HOOK_MASK;

jleave:
   NYD_OU;
   return rv;
}

FL void
temporary_folder_hook_unroll(void){ /* XXX intermediate hack */
   NYD_IN;

   if(a_amv_folder_hook_lopts != NIL){
      void *save = a_amv_lopts;

      a_amv_lopts = NIL;
      a_amv_lopts_unroll(&a_amv_folder_hook_lopts);
      ASSERT(a_amv_folder_hook_lopts == NIL);
      a_amv_lopts = save;
   }

   NYD_OU;
}

FL void
temporary_compose_mode_hook_control(boole enable, boole local){ /* XXX hack */
   NYD_IN;

   if(enable){
      struct a_amv_mac_call_args *amcap;
      struct a_amv_lostack *cmh_losp;

      cmh_losp = su_LOFI_CALLOC(sizeof *cmh_losp);
      cmh_losp->as_global_saved = a_amv_lopts;
      cmh_losp->as_loflags = local ? a_AMV_LF_SCOPE : a_AMV_LF_NONE;

      amcap = su_LOFI_CALLOC(sizeof *amcap);
      amcap->amca_name = "compose mode";
      amcap->amca_amp = NIL;
      amcap->amca_unroller = &cmh_losp->as_lopts;
      amcap->amca_loflags = cmh_losp->as_loflags;
      amcap->amca_ps_hook_mask = FAL0;
      amcap->amca_no_xcall = TRU1;
      amcap->amca_pospar = a_AMV_HAVE_LOPTS_AKA_LOCAL()
            ? a_amv_lopts->as_amcap->amca_pospar : &a_amv_pospar;

      cmh_losp->as_amcap = amcap;
      a_amv_compose_lostack = a_amv_lopts = cmh_losp;
   }else{
      if(a_amv_compose_lostack->as_lopts != NIL){
         void *save;

         save = a_amv_lopts;
         a_amv_lopts = NIL;
         a_amv_lopts_unroll(&a_amv_compose_lostack->as_lopts);
         a_amv_lopts = save;
      }

      a_amv_lopts = a_amv_compose_lostack->as_global_saved;

      su_LOFI_FREE(a_amv_compose_lostack->as_amcap);
      su_LOFI_FREE(a_amv_compose_lostack);
      a_amv_compose_lostack = NIL;
   }

   NYD_OU;
}

FL void
temporary_compose_mode_hook_call(char const *macname){
   /* TODO compose_mode_hook_call() temporary, v15: generalize; see a_GO_SPLICE
    * TODO comment in go.c for the right way of doing things! */
   static struct a_amv_lostack *cmh_losp;
   struct a_amv_mac_call_args *amcap;
   struct a_amv_mac *amp;
   NYD_IN;

   amp = NIL;

   if(macname == R(char*,-1)){
      a_amv_mac__finalize(cmh_losp);
      cmh_losp = NIL;
   }else if(macname != NIL &&
         (amp = a_amv_mac_lookup(macname, NIL, a_AMV_MF_NONE)) == NIL)
      n_err(_("Cannot call *on-compose-**: macro does not exist: %s\n"),
         macname);
   else{
      amcap = su_LOFI_CALLOC(sizeof *amcap);
      amcap->amca_name = (macname != NIL) ? macname
            : "*on-compose-splice(-shell)?*";
      amcap->amca_amp = amp;
      amcap->amca_unroller = &a_amv_compose_lostack->as_lopts;
      amcap->amca_loflags = a_amv_compose_lostack->as_loflags;
      amcap->amca_ps_hook_mask = TRU1;
      amcap->amca_no_xcall = TRU1;
      amcap->amca_pospar = &amcap->amca__pospar;
      n_pstate &= ~n_PS_HOOK_MASK;
      n_pstate |= n_PS_HOOK;
      if(macname != NIL)
         a_amv_mac_exec(amcap, NIL);
      else{
         cmh_losp = su_LOFI_CALLOC(sizeof *cmh_losp);
         cmh_losp->as_global_saved = a_amv_lopts;
         cmh_losp->as_lopts = *(cmh_losp->as_amcap = amcap)->amca_unroller;
         cmh_losp->as_loflags = a_amv_compose_lostack->as_loflags;
         a_amv_lopts = cmh_losp;
      }
   }

   NYD_OU;
}

#ifdef mx_HAVE_HISTORY
FL boole
temporary_addhist_hook(char const *ctx, char const *gabby_type,
      char const *histent){
   /* XXX temporary_addhist_hook(): intermediate hack */
   struct a_amv_mac_call_args *amcap;
   s32 perrn, pexn;
   struct a_amv_mac *amp;
   char const *macname, *argv[4];
   boole rv;
   NYD_IN;

   if((macname = ok_vlook(on_history_addition)) == NIL)
      rv = TRUM1;
   else if((amp = a_amv_mac_lookup(macname, NIL, a_AMV_MF_NONE)) == NIL){
      n_err(_("Cannot call *on-history-addition*: macro does not exist: %s\n"),
         macname);
      rv = TRUM1;
   }else{
      perrn = n_pstate_err_no;
      pexn = n_pstate_ex_no;

      argv[0] = ctx;
      argv[1] = gabby_type;
      argv[2] = histent;
      argv[3] = NIL;

      amcap = su_LOFI_CALLOC(sizeof *amcap);
      amcap->amca_name = macname;
      amcap->amca_amp = amp;
      amcap->amca_loflags = a_AMV_LF_SCOPE_FIXATE;
      amcap->amca_no_xcall = amcap->amca_ignerr = TRU1;
      amcap->amca_pospar = &amcap->amca__pospar;
      amcap->amca__pospar.app_count = 3;
      amcap->amca__pospar.app_not_heap = TRU1;
      amcap->amca__pospar.app_dat = argv;

      if(!a_amv_mac_exec(amcap, NIL))
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

#ifdef mx_HAVE_REGEX
FL char *
temporary_pospar_access_hook(char const *name, char const **argv, u32 argc,
      char *(*hook)(void *uservp), void *uservp){
   struct a_amv_lostack los;
   struct a_amv_mac_call_args amca;
   char *rv;
   NYD_IN;

   su_mem_set(&amca, 0, sizeof amca);
   amca.amca_name = name;
   amca.amca_amp = a_AMV_MACKY_MACK;
   amca.amca_pospar = &amca.amca__pospar;
   amca.amca__pospar.app_count = argc;
   amca.amca__pospar.app_not_heap = TRU1;
   amca.amca__pospar.app_dat = argv;

   su_mem_set(&los, 0, sizeof los);

   mx_sigs_all_holdx(); /* TODO DISLIKE! */

   los.as_global_saved = a_amv_lopts;
   los.as_amcap = &amca;
   los.as_up = los.as_global_saved;
   a_amv_lopts = &los;

   rv = (*hook)(uservp);

   a_amv_lopts = los.as_global_saved;

   mx_sigs_all_rele(); /* TODO DISLIKE! */

   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_REGEX */

FL void
n_var_setup_batch_mode(void){
   NYD2_IN;

   n_pstate |= n_PS_ROBOT; /* (be silent unsetting undefined variables) */
   n_poption |= n_PO_S_FLAG_TEMPORARY;
   ok_vset(MAIL, su_path_dev_null);
   ok_vset(MBOX, su_path_dev_null);
   ok_bset(emptystart);
   ok_bclear(errexit);
   ok_bclear(header);
   ok_vset(inbox, su_path_dev_null);
   ok_bclear(posix);
   ok_bset(quiet);
   ok_vset(sendwait, su_empty);
   ok_bset(typescript_mode);
   n_poption &= ~n_PO_S_FLAG_TEMPORARY;
   n_pstate &= ~n_PS_ROBOT;

   NYD2_OU;
}

FL boole
n_var_is_user_writable(char const *name){
   struct a_amv_var_carrier avc;
   struct a_amv_var_map const *avmp;
   boole rv;
   NYD_IN;

   a_amv_var_revlookup(&avc, name, TRU1);
   if((avmp = avc.avc_map) == NIL)
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

   su_mem_set(&avc, 0, sizeof avc);
   avc.avc_map = avmp = &a_amv_var_map[okey];
   avc.avc_name = &a_amv_var_names[avmp->avm_keyoff];
   avc.avc_hash = avmp->avm_hash;
   avc.avc_okey = okey;

   if(a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE))
      rv = avc.avc_var->av_value;
   else
      rv = NIL;

   NYD_OU;
   return rv;
}

FL boole
n_var_okset(enum okeys okey, up val){
   struct a_amv_var_carrier avc;
   boole ok;
   struct a_amv_var_map const *avmp;
   NYD_IN;

   su_mem_set(&avc, 0, sizeof avc);
   avc.avc_map = avmp = &a_amv_var_map[okey];
   avc.avc_name = &a_amv_var_names[avmp->avm_keyoff];
   avc.avc_hash = avmp->avm_hash;
   avc.avc_okey = okey;

   ok = a_amv_var_set(&avc, (val == 0x1 ? su_empty : R(char const*,val)),
         a_AMV_VSETCLR_NONE);

   NYD_OU;
   return ok;
}

FL boole
n_var_okclear(enum okeys okey){
   struct a_amv_var_carrier avc;
   boole rv;
   struct a_amv_var_map const *avmp;
   NYD_IN;

   su_mem_set(&avc, 0, sizeof avc);
   avc.avc_map = avmp = &a_amv_var_map[okey];
   avc.avc_name = &a_amv_var_names[avmp->avm_keyoff];
   avc.avc_hash = avmp->avm_hash;
   avc.avc_okey = okey;

   rv = a_amv_var_clear(&avc, a_AMV_VSETCLR_NONE);

   NYD_OU;
   return rv;
}

FL char const *
n_var_vlook(char const *vokey, boole try_getenv){
   struct a_amv_var_carrier avc;
   char const *rv;
   NYD_IN;

   a_amv_var_revlookup(&avc, vokey, FAL0);

   switch(S(enum a_amv_var_special_category,avc.avc_special_cat)){
   default: /* silence CC */
   case a_AMV_VSC_NONE:
      rv = NIL;
      if(a_amv_var_lookup(&avc, (a_AMV_VLOOK_LOCAL |
            (try_getenv ? a_AMV_VLOOK_LOG_OBSOLETE : a_AMV_VLOOK_NONE))))
         rv = avc.avc_var->av_value;
      /* Only check the environment for something that is otherwise unknown */
      else if(try_getenv && avc.avc_map == NIL &&
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

FL boole
n_var_vexplode(void const **cookie){
   struct a_amv_pospar *appp;
   NYD_IN;

   appp = a_AMV_HAVE_LOPTS_AKA_LOCAL() ? a_amv_lopts->as_amcap->amca_pospar
         : &a_amv_pospar;
   *cookie = (appp->app_count > 0) ? &appp->app_dat[appp->app_idx] : NIL;

   NYD_OU;
   return (*cookie != NIL);
}

FL boole
n_var_vset(char const *vokey, up val, boole local){
   struct a_amv_var_carrier avc;
   boole ok;
   NYD_IN;

   a_amv_var_revlookup(&avc, vokey, TRU1);

   ok = a_amv_var_set(&avc, (val == 0x1 ? su_empty : R(char const*,val)),
         (local ? a_AMV_VSETCLR_LOCAL : a_AMV_VSETCLR_NONE));

   NYD_OU;
   return ok;
}

#ifdef mx_HAVE_NET
FL char *
n_var_xoklook(enum okeys okey, struct mx_url const *urlp,
      enum okey_xlook_mode oxm){
   struct a_amv_var_carrier avc;
   struct str const *usp;
   uz nlen;
   char *nbuf, *rv;
   NYD_IN;

   ASSERT(oxm & (OXM_PLAIN | OXM_H_P | OXM_U_H_P));

   /* For simplicity: allow this case too */
   if(!(oxm & (OXM_H_P | OXM_U_H_P))){
      nbuf = NIL;
      goto jplain;
   }

   su_mem_set(&avc, 0, sizeof avc);
   avc.avc_name = &a_amv_var_names[(avc.avc_map = &a_amv_var_map[okey]
         )->avm_keyoff];
   avc.avc_okey = okey;
   avc.avc_is_chain_variant = TRU1;

   usp = (oxm & OXM_U_H_P) ? &urlp->url_u_h_p : &urlp->url_h_p;
   nlen = su_cs_len(avc.avc_name);
   nbuf = su_LOFI_ALLOC(nlen + 1 + usp->l +1);
   su_mem_copy(nbuf, avc.avc_name, nlen);

   /* One of .url_u_h_p and .url_h_p we test in here */
   nbuf[nlen++] = '-';
   su_mem_copy(&nbuf[nlen], usp->s, usp->l +1);
   avc.avc_name = nbuf;
   avc.avc_hash = a_AMV_NAME2HASH(avc.avc_name);
   if(a_amv_var_lookup(&avc, a_AMV_VLOOK_NONE))
      goto jvar;

   /* The second */
   if((oxm & (OXM_U_H_P | OXM_H_P)) == (OXM_U_H_P | OXM_H_P)){
      usp = &urlp->url_h_p;
      su_mem_copy(&nbuf[nlen], usp->s, usp->l +1);
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
   rv = (oxm & OXM_PLAIN) ? n_var_oklook(okey) : NIL;

jleave:
   if(nbuf != NIL)
      su_LOFI_FREE(nbuf);

   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_NET */

FL int
c_set(void *vp){
   int err;
   char const **ap;
   NYD_IN;

   if(*(ap = vp) == NIL){
      a_amv_var_show_all();
      err = n_EXIT_OK;
   }else
      err = a_amv_var_c_set(ap, ((n_pstate & n_PS_ARGMOD_LOCAL)
               ? a_AMV_VSETCLR_LOCAL : a_AMV_VSETCLR_NONE)
            ) ? n_EXIT_OK : n_EXIT_ERR;

   NYD_OU;
   return err;
}

FL int
c_unset(void *vp){
   struct a_amv_var_carrier avc;
   char **ap;
   int err;
   BITENUM_IS(u32,a_amv_var_setclr_flags) avscf;
   NYD_IN;

   avscf = (n_pstate & n_PS_ARGMOD_LOCAL) ? a_AMV_VSETCLR_LOCAL
         : a_AMV_VSETCLR_NONE;

   for(err = n_EXIT_OK, ap = vp; *ap != NIL; ++ap){
      if(a_amv_var_check_name(*ap, FAL0)){
         a_amv_var_revlookup(&avc, *ap, FAL0);

         if(a_amv_var_clear(&avc, avscf))
            continue;
      }

      err = n_EXIT_ERR;
   }

   NYD_OU;
   return err;
}

FL int
c_varshow(void *vp){
   struct n_string msg, *msgp = &msg;
   char const **ap, *cp;
   uz i, no;
   FILE *fp;
   NYD_IN;

   if((fp = mx_fs_tmp_open(NIL, "varshow", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL)
      fp = n_stdout;

   msgp = n_string_creat(msgp);
   i = 0;

   if(*(ap = vp) == NIL){
      a_amv_var_show_instantiate_all();

      for(no = n_OKEYS_FIRST; no < n_OKEYS_MAX; ++no){
         cp = &a_amv_var_names[a_amv_var_map[no].avm_keyoff];
         i += a_amv_var_show(cp, fp, msgp, FAL0);
      }
   }else{
      for(; (cp = *ap) != NIL; ++ap)
         if(a_amv_var_check_name(cp, FAL0))
            i += a_amv_var_show(cp, fp, msgp, TRU1);
   }

   n_string_gut(msgp);

   if(fp != n_stdout){
      page_or_print(fp, i);

      mx_fs_close(fp);
   }else
      clearerr(fp);

   NYD_OU;
   return n_EXIT_OK;
}

FL int
c_environ(void *vp){
   struct a_amv_var_carrier avc;
   boole islnk;
   int rv;
   char const **ap;
   BITENUM_IS(u32,a_amv_var_setclr_flags) avscf;
   NYD_IN;

   n_pstate_err_no = su_ERR_NONE;

   avscf = a_AMV_VSETCLR_ENV | ((n_pstate & n_PS_ARGMOD_LOCAL)
         ? a_AMV_VSETCLR_LOCAL : a_AMV_VSETCLR_NONE);
   ap = vp;
   vp = (n_pstate & n_PS_ARGMOD_VPUT) ? UNCONST(void*,*ap++) : NIL;

   /* Lookup is special, do it first */
   if(su_cs_starts_with_case("lookup", *ap)){
      char const *ev;

      if(*++ap == NIL || ap[1] != NIL) /* XXX arg-parser, subcommand.. */
         goto jeuse;

      rv = n_EXIT_ERR;
      if((ev = getenv(*ap)) != NIL){
         if(vp == NIL){
            if(fprintf(n_stdout, "%s\n", ev) > 0)
               rv = n_EXIT_OK;
            else
               n_pstate_err_no = su_err_no();
         }else if(!n_var_vset(vp, R(up,ev),
               ((avscf & a_AMV_VSETCLR_LOCAL) != 0)))
            n_pstate_err_no = su_ERR_NOTSUP;
         else
            rv = n_EXIT_OK;
      }else
         n_pstate_err_no = su_ERR_NOENT;

      goto jleave;
   }

   if(vp != NIL){
      n_err(_("environ: `vput' only supported for `lookup' subcommand\n"));
      goto jeuse;
   }

   if((islnk = su_cs_starts_with_case("link", *ap)) ||
         su_cs_starts_with_case("unlink", *ap)){
      for(rv = n_EXIT_OK; *++ap != NIL;){
         if(!a_amv_var_check_name(*ap, TRU1)){
            rv = n_EXIT_ERR;
            continue;
         }

         a_amv_var_revlookup(&avc, *ap, TRU1);

         /* We may know about it */
         if(a_amv_var_lookup(&avc, (a_AMV_VLOOK_NONE |
               a_AMV_VLOOK_LOG_OBSOLETE)) && (islnk ||
               (avc.avc_var->av_flags & a_AMV_VF_EXT_LINKED))){
            BITENUM_IS(u32,a_amv_var_flags) f, f2;

            f = avc.avc_var->av_flags;

            if(!islnk){
               avc.avc_var->av_flags &= ~(a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED);
               goto jlopts_check;
            }else if(avc.avc_var->av_flags &
                  (a_AMV_VF_ENV | a_AMV_VF_EXT_LINKED)){
               if(!(n_pstate & (n_PS_ROOT | n_PS_ROBOT)) &&
                     (n_poption & n_PO_D_V))
                  n_err(_("environ: link: already established: %s\n"), *ap);
            }else{
               avc.avc_var->av_flags |= a_AMV_VF_EXT_LINKED;

               if(!(avc.avc_var->av_flags & a_AMV_VF_ENV))
                  a_amv_var__putenv(&avc, avc.avc_var);

jlopts_check:
               if(UNLIKELY(a_AMV_HAVE_LOPTS_AKA_LOCAL()) &&
                     (avc.avc_map == NIL ||
                      !(avc.avc_map->avm_flags & a_AMV_VF_NOLOPTS))){
                  f2 = avc.avc_var->av_flags;
                  ASSERT(!(f2 & a_AMV_VF_EXT_LOCAL));
                  avc.avc_var->av_flags = f;
                  a_amv_lopts_add(a_amv_lopts, avc.avc_name, avc.avc_var,
                     ((avscf & a_AMV_VSETCLR_LOCAL) != 0));
                  avc.avc_var->av_flags = f2;
               }
            }
         }else if(!islnk){
            if(!(n_pstate & (n_PS_ROOT | n_PS_ROBOT)) &&
                  (n_poption & n_PO_D_V))
               n_err(_("environ: unlink: no link established: %s\n"), *ap);
            rv = n_EXIT_ERR;
         }else{
            char const *evp;

            if((evp = getenv(*ap)) != NIL){
               if(!a_amv_var_set(&avc, evp, avscf))
                  rv = n_EXIT_ERR;
            }else{
               n_err(_("environ: link: cannot link to non-existent: %s\n"),
                  *ap);
               rv = n_EXIT_ERR;
            }
         }
      }
   }else if(su_cs_starts_with_case("set", *ap)){
      rv = a_amv_var_c_set(++ap, avscf) ? n_EXIT_OK : n_EXIT_ERR;
   }else if(su_cs_starts_with_case("unset", *ap)){
      for(rv = n_EXIT_OK; *++ap != NIL;){
         if(!a_amv_var_check_name(*ap, TRU1)){
            rv = n_EXIT_ERR;
            continue;
         }

         a_amv_var_revlookup(&avc, *ap, FAL0);

         if(!a_amv_var_clear(&avc, avscf))
            rv = n_EXIT_ERR;
      }
   }else{
jeuse:
      mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("environ"), NIL);
      rv = n_EXIT_ERR;
   }

   if(rv != n_EXIT_OK)
      n_pstate_err_no = su_ERR_INVAL;

jleave:
   NYD_OU;
   return rv;
}

FL int
c_vpospar(void *v){
   enum a_flags{
      a_NONE = 0,
      a_ERR = 1u<<0,
      a_SET = 1u<<1,
      a_CLEAR = 1u<<2,
      a_QUOTE = 1u<<3
   };

   struct mx_cmd_arg *cap;
   uz i;
   struct a_amv_pospar *appp;
   BITENUM_IS(u32,a_flags) f;
   char const *varres;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   n_pstate_err_no = su_ERR_NONE;
   UNINIT(varres, su_empty);
   cacp = v;
   cap = cacp->cac_arg;

   if(su_cs_starts_with_case("set", cap->ca_arg.ca_str.s))
      f = a_SET;
   else if(su_cs_starts_with_case("clear", cap->ca_arg.ca_str.s))
      f = a_CLEAR;
   else if(su_cs_starts_with_case("quote", cap->ca_arg.ca_str.s))
      f = a_QUOTE;
   else{
      n_err(_("vpospar: invalid subcommand: %s\n"),
         n_shexp_quote_cp(cap->ca_arg.ca_str.s, FAL0));
      mx_cmd_print_synopsis(mx_cmd_by_arg_desc(cacp->cac_desc), NIL);
      n_pstate_err_no = su_ERR_INVAL;
      f = a_ERR;
      goto jleave;
   }
   --cacp->cac_no;

   if((f & (a_CLEAR | a_QUOTE)) && cap->ca_next != NIL){
      n_err(_("vpospar: %s: takes no argument\n"), cap->ca_arg.ca_str.s);
      n_pstate_err_no = su_ERR_INVAL;
      f = a_ERR;
      goto jleave;
   }

   cap = cap->ca_next;

   /* If in a macro, we need to overwrite the local instead of global argv */
   appp = (a_AMV_HAVE_LOPTS_AKA_LOCAL()/* TODO compose mode.. */
         ? a_amv_lopts->as_amcap->amca_pospar : &a_amv_pospar);

   if(f & (a_SET | a_CLEAR)){
      if(cacp->cac_vput != NIL)/* XXX better argparse */
         n_err(_("vpospar: `vput' only supported for `quote' subcommand\n"));

      if(!appp->app_not_heap && appp->app_maxcount > 0){
         for(i = appp->app_maxcount; i-- != 0;)
            su_FREE(UNCONST(char**,appp->app_dat[i]));
         su_FREE(appp->app_dat);
      }
      su_mem_set(appp, 0, sizeof *appp);

      if(f & a_SET){
         if((i = cacp->cac_no) > a_AMV_POSPAR_MAX){
            n_err(_("vpospar: overflow: %" PRIuZ " arguments!\n"), i);
            n_pstate_err_no = su_ERR_OVERFLOW;
            f = a_ERR;
            goto jleave;
         }

         if(i > 0){
            appp->app_maxcount = appp->app_count = S(u16,i);
            /* XXX Optimize: store it all in one chunk! */
            ++i;
            i *= sizeof *appp->app_dat;
            appp->app_dat = su_ALLOC(i);

            for(i = 0; cap != NIL; ++i, cap = cap->ca_next){
               appp->app_dat[i] = su_ALLOC(cap->ca_arg.ca_str.l +1);
               su_mem_copy(UNCONST(char*,appp->app_dat[i]),
                  cap->ca_arg.ca_str.s, cap->ca_arg.ca_str.l +1);
            }

            appp->app_dat[i] = NIL;
         }
      }
   }else{
      if(appp->app_count == 0)
         varres = su_empty;
      else{
         struct str in;
         struct n_string s_b, *s;
         char sep1, sep2;

         s = n_string_creat_auto(&s_b);

         sep1 = *ok_vlook(ifs);
         sep2 = *ok_vlook(ifs_ws);
         if(sep1 == sep2)
            sep2 = '\0';
         if(sep1 == '\0')
            sep1 = ' ';

         for(i = 0; i < appp->app_count; ++i){
            if(s->s_len){
               if(!n_string_can_book(s, 2))
                  goto jeover;
               s = n_string_push_c(s, sep1);
               if(sep2 != '\0')
                  s = n_string_push_c(s, sep2);
            }
            in.l = su_cs_len(in.s =
                  UNCONST(char*,appp->app_dat[i + appp->app_idx]));

            if(!n_string_can_book(s, in.l)){
jeover:
               n_err(_("vpospar: overflow: string too long!\n"));
               n_pstate_err_no = su_ERR_OVERFLOW;
               f = a_ERR;
               goto jleave;
            }
            s = n_shexp_quote(s, &in, TRU1);
         }

         varres = n_string_cp(s);
      }

      if(cacp->cac_vput == NIL){
         if(fprintf(n_stdout, "%s\n", varres) < 0){
            n_pstate_err_no = su_err_no();
            f |= a_ERR;
         }
      }else if(!n_var_vset(cacp->cac_vput, R(up,varres), cacp->cac_cm_local)){
         n_pstate_err_no = su_ERR_NOTSUP;
         f |= a_ERR;
      }
   }

jleave:
   NYD_OU;
   return (f & a_ERR) ? n_EXIT_ERR : n_EXIT_OK;
}

#undef a_AMV_VLOOK_LOG_OBSOLETE

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_ACCMACVAR
/* s-it-mode */
