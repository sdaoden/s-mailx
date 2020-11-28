/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Actual command table, `help', `list', etc., and the cmd_arg() parser.
 *
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_H
#define mx_CMD_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

enum mx_cmd_arg_flags{ /* TODO Most of these need to change, in fact in v15
   * TODO i rather see the mechanism that is used in c_bind() extended and used
   * TODO anywhere, i.e. n_cmd_arg_parse().
   * TODO Note we may NOT support arguments with su_cs_len()>=U32_MAX (?) */
   mx_CMD_ARG_TYPE_MSGLIST = 0, /* Message list type */
   mx_CMD_ARG_TYPE_NDMLIST = 1, /* Message list, no defaults */
   mx_CMD_ARG_TYPE_RAWDAT = 2, /* The plain string in an argv[] */
     mx_CMD_ARG_TYPE_STRING = 3, /* A pure string TODO obsolete */
   mx_CMD_ARG_TYPE_WYSH = 4, /* getrawlist(), sh(1) compatible */
      mx_CMD_ARG_TYPE_RAWLIST = 5, /* getrawlist(), old style TODO obsolete */
     mx_CMD_ARG_TYPE_WYRA = 6, /* _RAWLIST or _WYSH (with `wysh') TODO obs. */
   mx_CMD_ARG_TYPE_ARG = 7, /* n_cmd_arg_desc/n_cmd_arg() new-style */
   mx_CMD_ARG_TYPE_MASK = 7, /* Mask of the above */

   mx_CMD_ARG_A = 1u<<4, /* Needs an active mailbox */
   mx_CMD_ARG_F = 1u<<5, /* Is a conditional command */
   mx_CMD_ARG_G = 1u<<6,/* Supports `global' prefix */
   mx_CMD_ARG_HGABBY = 1u<<7, /* Is supposed to produce "gabby" history */
   mx_CMD_ARG_I = 1u<<8, /* Interactive command bit */
   mx_CMD_ARG_L = 1u<<9, /* Supports `local' prefix (only WYSH/WYRA) */
   mx_CMD_ARG_M = 1u<<10, /* Legal from send mode bit */
   mx_CMD_ARG_O = 1u<<11, /* n_OBSOLETE()d command */
   mx_CMD_ARG_P = 1u<<12, /* Autoprint dot after command */
   mx_CMD_ARG_R = 1u<<13, /* Forbidden in compose mode recursion */
   mx_CMD_ARG_SC = 1u<<14, /* Forbidden pre-n_PSO_STARTED_CONFIG */
   mx_CMD_ARG_S = 1u<<15, /* Forbidden pre-n_PSO_STARTED (POSIX) */
   mx_CMD_ARG_T = 1u<<16, /* Transparent command (<> PS_SAW_COMMAND) */
   mx_CMD_ARG_U = 1u<<17, /* Supports `u' prefix */
   mx_CMD_ARG_V = 1u<<18, /* Supports `vput' prefix */
   mx_CMD_ARG_W = 1u<<19, /* Invalid when read only bit */
   mx_CMD_ARG_X = 1u<<20, /* Valid command in n_PS_COMPOSE_FORKHOOK mode */
   mx_CMD_ARG_NEEDMAC = 1u<<21, /* Only within a macro/account */

   /* TODO Never place in `history': should be replaced by cmd_ctx flag stating
    * TODO do not place "this" invocation in history! */
   mx_CMD_ARG_NOHIST = 1u<<29,
   /* XXX Note that CMD_ARG_EM implies a _real_ return value for $! */
   mx_CMD_ARG_EM = 1u<<30 /* If error: n_pstate_err_no (4 $! aka. ok_v___em) */
};

enum mx_cmd_arg_desc_flags{
   /* - A type */
   mx_CMD_ARG_DESC_SHEXP = 1u<<0, /* sh(1)ell-style token */
   /* TODO All MSGLIST arguments can only be used last and are always greedy
    * TODO (but MUST be _GREEDY, and MUST NOT be _OPTION too!).
    * MSGLIST_AND_TARGET may create a NULL target */
   mx_CMD_ARG_DESC_MSGLIST = 1u<<1,  /* Message specification(s) */
   mx_CMD_ARG_DESC_NDMSGLIST = 1u<<2,
   mx_CMD_ARG_DESC_MSGLIST_AND_TARGET = 1u<<3,

   mx__CMD_ARG_DESC_TYPE_MASK = mx_CMD_ARG_DESC_SHEXP |
         mx_CMD_ARG_DESC_MSGLIST | mx_CMD_ARG_DESC_NDMSGLIST |
         mx_CMD_ARG_DESC_MSGLIST_AND_TARGET,

   /* - Optional flags */
   /* It is not an error if an optional argument is missing; once an argument
    * has been declared optional only optional arguments may follow */
   mx_CMD_ARG_DESC_OPTION = 1u<<16,
   /* GREEDY: parse as many of that type as possible; must be last entry */
   mx_CMD_ARG_DESC_GREEDY = 1u<<17,
   /* If greedy, join all given arguments separated by ASCII SP right away */
   mx_CMD_ARG_DESC_GREEDY_JOIN = 1u<<18,
   /* Honour an overall "stop" request in one of the arguments (\c@ or #) */
   mx_CMD_ARG_DESC_HONOUR_STOP = 1u<<19,
   /* With any MSGLIST, only one message may be give or ERR_NOTSUP (default) */
   mx_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE = 1u<<20,

   mx__CMD_ARG_DESC_FLAG_MASK = mx_CMD_ARG_DESC_OPTION |
         mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_GREEDY_JOIN |
         mx_CMD_ARG_DESC_HONOUR_STOP |
         mx_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE,

   /* We may include something for n_pstate_err_no */
   mx_CMD_ARG_DESC_ERRNO_SHIFT = 21u,
   mx_CMD_ARG_DESC_ERRNO_MASK = (1u<<10) - 1
};
#define mx_CMD_ARG_DESC_ERRNO_TO_ORBITS(ENO) \
   (S(u32,ENO) << mx_CMD_ARG_DESC_ERRNO)
#define mx_CMD_ARG_DESC_TO_ERRNO(FLAGCARRIER) \
   ((S(u32,FLAGCARRIER) >> mx_CMD_ARG_DESC_ERRNO_SHIFT) &\
      mx_CMD_ARG_DESC_ERRNO_MASK)

struct mx_cmd_arg_desc{
   char cad_name[12]; /* Name of command */
   u32 cad_no; /* Number of entries in cad_ent_flags */
   /* [enum cmd_arg_desc_flags,arg-dep] */
   u32 cad_ent_flags[VFIELD_SIZE(0)][2];
};

/* ISO C(99) does not allow initialization of "flex array" */
#define mx_CMD_ARG_DESC_SUBCLASS_DEF(CMD,NO,VAR) \
   mx_CMD_ARG_DESC_SUBCLASS_DEF_NAME(CMD, su_STRING(CMD), NO, VAR)
#define mx_CMD_ARG_DESC_SUBCLASS_DEF_NAME(CMD,NAME,NO,VAR) \
   static struct mx_cmd_arg_desc_ ## CMD {\
      char cad_name[12];\
      u32 cad_no;\
      u32 cad_ent_flags[NO][2];\
   } const VAR = { NAME "\0", NO,
#define mx_CMD_ARG_DESC_SUBCLASS_DEF_END }
#define mx_CMD_ARG_DESC_SUBCLASS_CAST(P) su_R(struct mx_cmd_arg_desc const*,P)

struct mx_cmd_arg_ctx{
   struct mx_cmd_arg_desc const *cac_desc; /* Input: description of command */
   char const *cac_indat; /* Input that shall be parsed */
   uz cac_inlen; /* Input length (UZ_MAX: do a su_cs_len()) */
   u32 cac_msgflag; /* Input (option): required flags of messages */
   u32 cac_msgmask; /* Input (option): relevant flags of messages */
   u32 cac_no; /* Output: number of parsed arguments */
   boole cac_cm_local; /* "Output": `local' command modifier was set */
   u8 cac__pad[3];
   struct mx_cmd_arg *cac_arg; /* Output: parsed arguments */
   char const *cac_vput; /* "Output": `vput' command modifier used: varname */
};

struct mx_cmd_arg{
   struct mx_cmd_arg *ca_next;
   char const *ca_indat; /*[PRIV] Pointer into cmd_arg_ctx.cac_indat */
   uz ca_inlen; /*[PRIV] of .ca_indat of this arg (no NUL) */
   u32 ca_ent_flags[2]; /* Copy of cmd_arg_desc.cad_ent_flags[X] */
   u32 ca_arg_flags; /* [Output: _WYSH: copy of parse result flags] */
   u8 ca__dummy[4];
   union{
      struct str ca_str; /* _CMD_ARG_DESC_SHEXP */
      int *ca_msglist; /* _CMD_ARG_DESC_MSGLIST+ */
   } ca_arg; /* Output: parsed result */
};

struct mx_cmd_desc{
   char const *cd_name; /* Name of command */
   int (*cd_func)(void*); /* Implementor of command */
   enum mx_cmd_arg_flags cd_caflags;
   u32 cd_mflags_o_minargs; /* Message flags or min. args WYSH/WYRA/RAWLIST */
   u32 cd_mmask_o_maxargs; /* Ditto, mask or max. args */
   /* XXX requires cmd.h initializer changes u8 cd__pad[4];*/
   struct mx_cmd_arg_desc const *cd_cadp;
#ifdef mx_HAVE_DOCSTRINGS
   char const *cd_doc; /* One line doc for command */
#endif
};

/* Isolate the command from the arguments, return pointer to end of cmd name */
EXPORT char const *mx_cmd_isolate_name(char const *cmd);

/* Whether cmd is a valid command name (and not a modifier, for example) */
EXPORT boole mx_cmd_is_valid_name(char const *cmd);

/* First command which fits for cmd, or NULL */
EXPORT struct mx_cmd_desc const *mx_cmd_firstfit(char const *cmd);

/* Get the default command for the empty line */
EXPORT struct mx_cmd_desc const *mx_cmd_default(void);

/* Or NIL if cmd is invalid, or su_empty if !HAVE_DOCSTRINGS */
INLINE char const *mx_cmd_get_brief_doc(struct mx_cmd_desc const *cdp_or_nil){
   char const *rv;

   if(cdp_or_nil != NIL){
#ifdef mx_HAVE_DOCSTRINGS
      rv = cdp_or_nil->cd_doc;
#else
      rv = su_empty;
#endif
   }else
      rv = NIL;
   return rv;
}

/* True if I/O succeeded (or nothing was printed).
 * If fp is NIL we dump via err(), and return true. */
EXPORT boole mx_cmd_print_synopsis(struct mx_cmd_desc const *cdp_or_nil,
      FILE *fp_or_nil);

/* Scan an entire command argument line, return whether result can be used,
 * otherwise no resources are allocated (in ->cac_arg).
 * For _WYSH arguments the flags _TRIM_SPACE (v15 _not_ _TRIM_IFSSPACE) and
 * _LOG are implicit, _META_SEMICOLON is starting with the last (non-optional)
 * argument, and then a trailing empty argument is ignored, too */
EXPORT boole mx_cmd_arg_parse(struct mx_cmd_arg_ctx *cacp);

/* Save away the data from/to AUTO memory to mem_bag vp */
EXPORT void *mx_cmd_arg_save_to_bag(struct mx_cmd_arg_ctx const *cacp,
      void *vp);

/* Scan out the list of string arguments according to rm, return -1 on error;
 * res_dat is NULL terminated unless res_size is 0 or error occurred */
EXPORT int /* TODO v15*/ getrawlist(boole wysh, char **res_dat, uz res_size,
                  char const *line, uz linesize);

/* Logic behind `eval' and `~ $', cnt is number of expansions to be performed.
 * *io will be set to AUTO_ALLOC() result upon success, optionally prepended by
 * prefix_or_nil and an ASCII SPC */
EXPORT boole mx_cmd_eval(struct str *io, u32 cnt, char const *prefix_or_nil);

#include <su/code-ou.h>
#endif /* mx_CMD_H */
/* s-it-mode */
