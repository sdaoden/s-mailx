/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Command argument list parser(s). TODO partial: no msglist in here, etc.
 *
 * Copyright (c) 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
/* getrawlist(): */
/*
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE cmd_arg

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

FL int
getrawlist(bool_t wysh, char **res_dat, size_t res_size,
      char const *line, size_t linesize){
   int res_no;
   NYD_ENTER;

   pstate &= ~PS_ARGLIST_MASK;

   if(res_size == 0){
      res_no = -1;
      goto jleave;
   }else if(UICMP(z, res_size, >, INT_MAX))
      res_size = INT_MAX;
   else
      --res_size;
   res_no = 0;

   if(!wysh){
      /* And assuming result won't grow input */
      char c2, c, quotec, *cp2, *linebuf;

      linebuf = n_lofi_alloc(linesize);

      for(;;){
         for(; blankchar(*line); ++line)
            ;
         if(*line == '\0')
            break;

         if(UICMP(z, res_no, >=, res_size)){
            n_err(_("Too many input tokens for result storage\n"));
            res_no = -1;
            break;
         }

         cp2 = linebuf;
         quotec = '\0';

         /* TODO v15: complete switch in order mirror known behaviour */
         while((c = *line++) != '\0'){
            if(quotec != '\0'){
               if(c == quotec){
                  quotec = '\0';
                  continue;
               }else if(c == '\\'){
                  if((c2 = *line++) == quotec)
                     c = c2;
                  else
                     --line;
               }
            }else if(c == '"' || c == '\''){
               quotec = c;
               continue;
            }else if(c == '\\'){
               if((c2 = *line++) != '\0')
                  c = c2;
               else
                  --line;
            }else if(blankchar(c))
               break;
            *cp2++ = c;
         }

         res_dat[res_no++] = savestrbuf(linebuf, PTR2SIZE(cp2 - linebuf));
         if(c == '\0')
            break;
      }

      n_lofi_free(linebuf);
   }else{
      /* sh(1) compat mode.  Prepare shell token-wise */
      struct n_string store;
      struct str input;

      n_string_creat_auto(&store);
      input.s = n_UNCONST(line);
      input.l = linesize;

      for(;;){
         for(; blankchar(*line); ++line)
            ;
         if(*line == '\0')
            break;

         if(UICMP(z, res_no, >=, res_size)){
            n_err(_("Too many input tokens for result storage\n"));
            res_no = -1;
            break;
         }

         input.l -= PTR2SIZE(line - input.s);
         input.s = n_UNCONST(line);
         /* C99 */{
            enum n_shexp_state shs;

            if((shs = n_shexp_parse_token(&store, &input, n_SHEXP_PARSE_LOG)) &
                  n_SHEXP_STATE_ERR_MASK){
               /* Simply ignore Unicode error, just keep the normalized \[Uu] */
               if((shs & n_SHEXP_STATE_ERR_MASK) != n_SHEXP_STATE_ERR_UNICODE){
                  res_no = -1;
                  break;
               }
            }

            if(shs & n_SHEXP_STATE_OUTPUT){
               if(shs & n_SHEXP_STATE_CONTROL)
                  pstate |= PS_WYSHLIST_SAW_CONTROL;

               res_dat[res_no++] = n_string_cp(&store);
               n_string_drop_ownership(&store);
            }

            if(shs & n_SHEXP_STATE_STOP)
               break;
         }
         line = input.s;
      }

      n_string_gut(&store);
   }

   if(res_no >= 0)
      res_dat[(size_t)res_no] = NULL;
jleave:
   NYD_LEAVE;
   return res_no;
}

FL bool_t
n_cmd_arg_parse(struct n_cmd_arg_ctx *cacp){
   struct n_cmd_arg ncap, *lcap;
   struct str shin_orig, shin;
   bool_t addca;
   size_t cad_no, parsed_args;
   struct n_cmd_arg_desc const *cadp;
   NYD_ENTER;

   assert(cacp->cac_inlen == 0 || cacp->cac_indat != NULL);
   assert(cacp->cac_desc->cad_no > 0);
#ifdef HAVE_DEBUG
   /* C99 */{
      bool_t opt_seen = FAL0;

      for(cadp = cacp->cac_desc, cad_no = 0; cad_no < cadp->cad_no; ++cad_no){
         assert(cadp->cad_ent_flags[cad_no][0] & n__CMD_ARG_DESC_TYPE_MASK);
         assert(!opt_seen ||
            (cadp->cad_ent_flags[cad_no][0] & n_CMD_ARG_DESC_OPTION));
         if(cadp->cad_ent_flags[cad_no][0] & n_CMD_ARG_DESC_OPTION)
            opt_seen = TRU1;
         assert(!(cadp->cad_ent_flags[cad_no][0] & n_CMD_ARG_DESC_GREEDY) ||
            cad_no + 1 == cadp->cad_no);
      }
   }
#endif

   shin.s = n_UNCONST(cacp->cac_indat); /* "logical" only */
   shin.l = (cacp->cac_inlen == UIZ_MAX ? strlen(shin.s) : cacp->cac_inlen);
   shin_orig = shin;
   cacp->cac_no = 0;
   cacp->cac_arg = lcap = NULL;

   parsed_args = 0;
   for(cadp = cacp->cac_desc, cad_no = 0; shin.l > 0 && cad_no < cadp->cad_no;
         ++cad_no){
jredo:
      memset(&ncap, 0, sizeof ncap);
      ncap.ca_indat = shin.s;
      /* >ca_inline once we know */
      memcpy(&ncap.ca_ent_flags[0], &cadp->cad_ent_flags[cad_no][0],
         sizeof ncap.ca_ent_flags);
      addca = FAL0;

      switch(ncap.ca_ent_flags[0] & n__CMD_ARG_DESC_TYPE_MASK){
      case n_CMD_ARG_DESC_STRING:{
         char /*const*/ *cp = shin.s;
         size_t i = shin.l;

         while(i > 0 && blankspacechar(*cp))
            ++cp, --i;

         ncap.ca_arg.ca_str.s = cp;
         while(i > 0 && !blankspacechar(*cp))
            ++cp, --i;
         ncap.ca_arg.ca_str.s = savestrbuf(ncap.ca_arg.ca_str.s,
               ncap.ca_arg.ca_str.l = PTR2SIZE(cp - ncap.ca_arg.ca_str.s));

         while(i > 0 && blankspacechar(*cp))
            ++cp, --i;
         ncap.ca_inlen = PTR2SIZE(cp - ncap.ca_indat);
         shin.s = cp;
         shin.l = i;
         addca = TRU1;
      }  break;
      default:
      case n_CMD_ARG_DESC_WYSH:{
         struct n_string shou, *shoup;
         enum n_shexp_state shs;

         shoup = n_string_creat_auto(&shou);
         ncap.ca_arg_flags =
         shs = n_shexp_parse_token(shoup, &shin, ncap.ca_ent_flags[1] |
               n_SHEXP_PARSE_TRIMSPACE | n_SHEXP_PARSE_LOG);
         ncap.ca_inlen = PTR2SIZE(shin.s - ncap.ca_indat);
         if((shs & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_ERR_MASK)) ==
               n_SHEXP_STATE_OUTPUT){
            ncap.ca_arg.ca_str.s = n_string_cp(shoup);
            ncap.ca_arg.ca_str.l = shou.s_len;
            shoup = n_string_drop_ownership(shoup);
         }
         n_string_gut(shoup);

         if(shs & n_SHEXP_STATE_ERR_MASK)
            goto jerr;
         if((shs & n_SHEXP_STATE_STOP) && /* XXX delay if output */
               (ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_HONOUR_STOP)){
            if(!(shs & n_SHEXP_STATE_OUTPUT))
               goto jleave;
            addca = TRUM1;
         }else
            addca = TRU1;
      }  break;
      }
      ++parsed_args;

      if(addca){
         struct n_cmd_arg *cap;

         cap = salloc(sizeof *cap);
         memcpy(cap, &ncap, sizeof ncap);
         if(lcap == NULL)
            cacp->cac_arg = cap;
         else
            lcap->ca_next = cap;
         lcap = cap;
         ++cacp->cac_no;

         if(addca == TRUM1)
            goto jleave;
      }

      if(shin.l > 0 && (ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_GREEDY))
         goto jredo;
   }

   if(cad_no < cadp->cad_no &&
         !(cadp->cad_ent_flags[cad_no][0] & n_CMD_ARG_DESC_OPTION))
      goto jerr;

jleave:
   NYD_LEAVE;
   return (lcap != NULL);

jerr:{
      size_t i;

      for(i = 0; (i < cadp->cad_no &&
            !(cadp->cad_ent_flags[i][0] & n_CMD_ARG_DESC_OPTION)); ++i)
         ;

      n_err(_("`%s': parsing stopped after %" PRIuZ " arguments "
            "(need %" PRIuZ "%s)\n"
            "     Input: %.*s\n"
            "   Stopped: %.*s\n"),
         cadp->cad_name, parsed_args, i, (i == cadp->cad_no ? "" : "+"),
         (int)shin_orig.l, shin_orig.s,
         (int)shin.l, shin.s);
   }
   lcap = NULL;
   goto jleave;
}

FL struct n_string *
n_cmd_arg_join_greedy(struct n_cmd_arg_ctx const *cacp, struct n_string *store){
   struct n_cmd_arg *cap;
   NYD_ENTER;

   for(cap = cacp->cac_arg;
         (cap != NULL && !(cap->ca_ent_flags[0] & n_CMD_ARG_DESC_GREEDY));
         cap = cap->ca_next)
      ;
   /* Can only join strings */
   assert(cap == NULL ||
      (cap->ca_ent_flags[0] & (n_CMD_ARG_DESC_STRING | n_CMD_ARG_DESC_WYSH)));

   while(cap != NULL){
      store = n_string_push_buf(store,
            cap->ca_arg.ca_str.s, cap->ca_arg.ca_str.l);
      if((cap = cap->ca_next) != NULL)
         store = n_string_push_c(store, ' ');
   }
   NYD_LEAVE;
   return store;
}

/* s-it-mode */
