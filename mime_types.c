/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `mimetypes' and other mime.types(5) related facilities.
 *
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

enum mime_type {
   _MT_APPLICATION,
   _MT_AUDIO,
   _MT_IMAGE,
   _MT_MESSAGE,
   _MT_MULTIPART,
   _MT_TEXT,
   _MT_VIDEO,
   _MT_OTHER,
   __MT_TMIN   = 0,
   __MT_TMAX   = _MT_OTHER,
   __MT_TMASK  = 0x07,

   _MT_REGEX   = 1<< 3,       /* Line contains regex */
   _MT_LOADED  = 1<< 4,       /* Not struct mtbltin */
   _MT_USR     = 1<< 5,       /* MIME_TYPES_USR */
   _MT_SYS     = 1<< 6        /* MIME_TYPES_SYS */
};

struct mtbltin {
   ui32_t         mtb_flags;
   ui32_t         mtb_mtlen;
   char const     *mtb_line;
};

struct mtnode {
   struct mtnode  *mt_next;
   ui32_t         mt_flags;
   ui32_t         mt_mtlen;   /* Length of MIME type string, rest thereafter */
#ifdef HAVE_REGEX
   regex_t        *mt_regex;
#endif
   /* C99 forbids flexible arrays in union, so unfortunately we waste a pointer
    * that could already store character data here */
   char const     *mt_line;
};

static struct mtbltin const   _mt_bltin[] = {
#include "mime_types.h"
};

static char const             _mt_typnames[][16] = {
   "application/", "audio/", "image/",
   "message/", "multipart/", "text/",
   "video/"
};
CTA(_MT_APPLICATION == 0 && _MT_AUDIO == 1 && _MT_IMAGE == 2 &&
   _MT_MESSAGE == 3 && _MT_MULTIPART == 4 && _MT_TEXT == 5 &&
   _MT_VIDEO == 6);

static struct mtnode    *_mt_list;

/* Initialize MIME type list / (lazy) create regular expression.
 * __mt_load_file will prepend all entries of file onto *inject in order.
 * Note: _mt_regcomp() will destruct mtnp if regex can't be compiled! */
static void             _mt_init(void);
static bool_t           __mt_load_file(ui32_t orflags,
                           char const *file, char **line, size_t *linesize);
static struct mtnode *  __mt_add_line(ui32_t orflags,
                           char const *line, size_t len);
#ifdef HAVE_REGEX
static bool_t           _mt_regcomp(struct mtnode *mtnp);
#endif
static void             _mtnode_free(struct mtnode *mtnp);

static void
_mt_init(void)
{
   struct mtnode *tail;
   char c, *line; /* TODO line pool (below) */
   size_t linesize;
   ui32_t i, j;
   char const *srcs_arr[10], *ccp, **srcs;
   NYD_ENTER;

   /* Always load our builtins */
   for (tail = NULL, i = 0; i < NELEM(_mt_bltin); ++i) {
      struct mtbltin const *mtbp = _mt_bltin + i;
      struct mtnode *mtnp = smalloc(sizeof *mtnp);

      if (tail != NULL)
         tail->mt_next = mtnp;
      else
         _mt_list = mtnp;
      tail = mtnp;
      mtnp->mt_next = NULL;
      mtnp->mt_flags = mtbp->mtb_flags;
      mtnp->mt_mtlen = mtbp->mtb_mtlen;
#ifdef HAVE_REGEX
      mtnp->mt_regex = NULL;
#endif
      mtnp->mt_line = mtbp->mtb_line;
   }

   /* Decide which files sources have to be loaded */
   if ((ccp = ok_vlook(mimetypes_load_control)) == NULL)
      ccp = "US";
   else if (*ccp == '\0')
      goto jpolish;

   srcs = srcs_arr + 2;
   srcs[-1] = srcs[-2] = NULL;

#ifdef HAVE_REGEX
   if (strchr(ccp, '=') != NULL) {
      line = savestr(ccp);

      while ((ccp = n_strsep(&line, ',', TRU1)) != NULL) {
         switch ((c = *ccp)) {
         case 'S': case 's':
            srcs_arr[1] = MIME_TYPES_SYS;
            if (0) {
               /* FALLTHRU */
         case 'U': case 'u':
               srcs_arr[0] = MIME_TYPES_USR;
            }
            if (ccp[1] != '\0')
               goto jecontent;
            break;
         case 'F': case 'f':
            if (*++ccp == '=' && *++ccp != '\0') {
               if (PTR2SIZE(srcs - srcs_arr) < NELEM(srcs_arr))
                  *srcs++ = ccp;
               else if (options & OPT_D_V)
                  fprintf(stderr,
                     _("*mimetypes-load-control*: too many sources, "
                        "skipping \"%s\"\n"), ccp);
               continue;
            }
            /* FALLTHRU */
         default:
jecontent:
            fprintf(stderr,
               _("*mimetypes-load-control*: unsupported content: \"%s\"\n"),
               ccp);
            break;
         }
      }
   } else
#endif /* HAVE_REGEX */
   for (i = 0; (c = ccp[i]) != '\0'; ++i)
      switch (c) {
      case 'S': case 's': srcs_arr[1] = MIME_TYPES_SYS; break;
      case 'U': case 'u': srcs_arr[0] = MIME_TYPES_USR; break;
      default:
         fprintf(stderr,
            _("*mimetypes-load-control*: unsupported content: \"%s\"\n"), ccp);
         break;
      }

   /* Load all file-based sources in the desired order */
   line = NULL;
   linesize = 0;
   for (j = 0, i = (ui32_t)PTR2SIZE(srcs - srcs_arr), srcs = srcs_arr; i > 0;
         ++j, ++srcs, --i)
      if (*srcs == NULL)
         continue;
      else if (!__mt_load_file(
            (j == 0 ? _MT_USR : (j == 1 ? _MT_SYS : _MT_REGEX)),
            *srcs, &line, &linesize)) {
         if ((options & OPT_D_V) || j > 1)
            fprintf(stderr,
               _("*mimetypes-load-control*: can't open or load \"%s\"\n"),
               *srcs);
      }
   if (line != NULL)
      free(line);

   /* Finally, in debug or very verbose mode, compile regular expressions right
    * away in order to be able to produce compact block of info messages */
jpolish:
#ifdef HAVE_REGEX
   if (options & OPT_D_VV)
      for (tail = _mt_list; tail != NULL;) {
         struct mtnode *xnp = tail;
         tail = tail->mt_next;
         if (xnp->mt_flags & _MT_REGEX)
            _mt_regcomp(xnp);
      }
#endif
   NYD_LEAVE;
}

static bool_t
__mt_load_file(ui32_t orflags, char const *file, char **line, size_t *linesize)
{
   char const *cp;
   FILE *fp;
   struct mtnode *head, *tail, *mtnp;
   size_t len;
   NYD_ENTER;

   if ((cp = file_expand(file)) == NULL || (fp = Fopen(cp, "r")) == NULL) {
      cp = NULL;
      goto jleave;
   }

   for (head = tail = NULL; fgetline(line, linesize, NULL, &len, fp, 0) != 0;)
      if ((mtnp = __mt_add_line(orflags, *line, len)) != NULL) {
         if (head == NULL)
            head = tail = mtnp;
         else
            tail->mt_next = mtnp;
         tail = mtnp;
      }
   if (head != NULL) {
      tail->mt_next = _mt_list;
      _mt_list = head;
   }

   Fclose(fp);
jleave:
   NYD_LEAVE;
   return (cp != NULL);
}

static struct mtnode *
__mt_add_line(ui32_t orflags, char const *line, size_t len)
{
   struct mtnode *mtnp = NULL;
   char const *typ, *subtyp;
   size_t tlen, i;
   NYD_ENTER;

   /* Drop anything after a comment first */
   if ((typ = memchr(line, '#', len)) != NULL)
      len = PTR2SIZE(typ - line);

   /* Then trim any trailing whitespace from line (including NL/CR) */
   while (len > 0 && spacechar(line[len - 1]))
      --len;

   /* Isolate MIME type, trim any whitespace from it */
   while (len > 0 && blankchar(*line))
      ++line, --len;
   typ = line;
   while (len > 0 && !blankchar(*line))
      ++line, --len;
   if (len == 0)
      goto jleave;
   tlen = PTR2SIZE(line - typ);
   /* Ignore empty lines */
   if (tlen == 0)
      goto jleave;

   if ((subtyp = memchr(typ, '/', tlen)) == NULL) {
      if (options & OPT_D_V)
         fprintf(stderr, "mime.types(5): invalid MIME type: \"%s\"\n", typ);
      goto jleave;
   }
   ++subtyp;

   /* Map to mime_type */
   tlen = PTR2SIZE(subtyp - typ);
   for (i = __MT_TMIN;;) {
      if (!ascncasecmp(_mt_typnames[i], typ, tlen)) {
         orflags |= i;
         tlen = PTR2SIZE(line - subtyp);
         typ = subtyp;
         break;
      }
      if (++i == __MT_TMAX) {
         orflags |= _MT_OTHER;
         tlen = PTR2SIZE(line - typ);
         break;
      }
   }

   /* Strip leading whitespace from the list of extensions / the regex;
    * trailing WS has already been trimmed away above.
    * Be silent on slots which define a mimetype without any value */
   while (len > 0 && blankchar(*line))
      ++line, --len;
   if (len == 0)
      goto jleave;

   /*  */
   mtnp = smalloc(sizeof(*mtnp) + tlen + len +1);
   mtnp->mt_next = NULL;
   mtnp->mt_flags = (orflags |= _MT_LOADED);
   mtnp->mt_mtlen = (ui32_t)tlen;
#ifdef HAVE_REGEX
   mtnp->mt_regex = NULL;
#endif
   {  char *l = (char*)(mtnp + 1);
      mtnp->mt_line = l;
      memcpy(l, typ, tlen);
      memcpy(l + tlen, line, len);
      tlen += len;
      l[tlen] = '\0';
   }

jleave:
   NYD_LEAVE;
   return mtnp;
}

#ifdef HAVE_REGEX
static bool_t
_mt_regcomp(struct mtnode *mtnp)
{
   regex_t *rep;
   char const *line;
   NYD2_ENTER;

   assert(mtnp->mt_flags & _MT_REGEX);

   /* Lines of the dynamically loaded f=FILE will only be compiled as regular
    * expressions if any "magical" regular expression characters are seen */
   line = mtnp->mt_line + mtnp->mt_mtlen;
   if ((mtnp->mt_flags & (_MT_LOADED | _MT_USR | _MT_SYS)) == _MT_LOADED &&
         !is_maybe_regex(line)) {
      mtnp->mt_flags &= ~_MT_REGEX;
      goto jleave;
   }

   rep = smalloc(sizeof *rep);

   if (regcomp(rep, line, REG_EXTENDED | REG_ICASE | REG_NOSUB) == 0)
      mtnp->mt_regex = rep;
   else {
      free(rep);
      fprintf(stderr, _("Invalid regular expression: \"%s\"\n"), line);

      if (mtnp == _mt_list)
         _mt_list = mtnp->mt_next;
      else {
         struct mtnode *x = _mt_list;
         while (x->mt_next != mtnp)
            x = x->mt_next;
         x->mt_next = mtnp->mt_next;
      }

      free(mtnp);
      mtnp = NULL;
   }

jleave:
   NYD2_LEAVE;
   return (mtnp != NULL);
}
#endif /* HAVE_REGEX */

static void
_mtnode_free(struct mtnode *mtnp)
{
   NYD2_ENTER;
#ifdef HAVE_REGEX
   if ((mtnp->mt_flags & _MT_REGEX) && mtnp->mt_regex != NULL) {
      regfree(mtnp->mt_regex);
      free(mtnp->mt_regex);
   }
#endif
   free(mtnp);
   NYD2_LEAVE;
}

FL int
c_mimetypes(void *v)
{
   char **argv = v;
   struct mtnode *lnp, *mtnp;
   NYD_ENTER;

   /* Null or one argument forms */
   if (*argv == NULL || !asccasecmp(*argv, "show")) {
      FILE *fp;
      size_t l;

      if (argv[0] != NULL && argv[1] != NULL)
         goto jerr;

      if (_mt_list == NULL)
         _mt_init();
      if (_mt_list == (struct mtnode*)-1) {
         printf(_("*mimetypes-load-control*: no mime.types(5) available\n"));
         goto jleave;
      }

      if ((fp = Ftmp(NULL, "mimelist", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
            == NULL) {
         perror("tmpfile");
         v = NULL;
         goto jleave;
      }

      for (l = 0, mtnp = _mt_list; mtnp != NULL; ++l, mtnp = mtnp->mt_next) {
         char const *typ = ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER)
               ? "" : _mt_typnames[mtnp->mt_flags & __MT_TMASK];

         fprintf(fp, "%c%c %s%.*s <%s>\n",
            (mtnp->mt_flags & _MT_REGEX
               ? (mtnp->mt_regex != NULL ? '*' : '?') : ' '),
            (mtnp->mt_flags & _MT_USR ? 'U'
               : (mtnp->mt_flags & _MT_SYS ? 'S'
               : (mtnp->mt_flags & _MT_LOADED ? 'F' : '@'))),
            typ, (int)mtnp->mt_mtlen, mtnp->mt_line,
            mtnp->mt_line + mtnp->mt_mtlen);
      }

      page_or_print(fp, l);
      Fclose(fp);
   } else if (!asccasecmp(*argv, "clear")) {
      if (argv[1] != NULL)
         goto jerr;

      if (NELEM(_mt_bltin) == 0 && _mt_list == (struct mtnode*)-1)
         _mt_list = NULL;
      while ((mtnp = _mt_list) != NULL) {
         _mt_list = mtnp->mt_next;
         _mtnode_free(mtnp);
      }
   }
   /* Two argument forms */
   else if (argv[1] == NULL)
      goto jerr;
   else if (!asccasecmp(*argv, "add")) {
      if (_mt_list == NULL)
         _mt_init();
      if (_mt_list == (struct mtnode*)-1)
         _mt_list = NULL;

      ++argv;
      mtnp = __mt_add_line(_MT_LOADED | _MT_REGEX, *argv, strlen(*argv));
      if ((v = mtnp) != NULL) {
         mtnp->mt_next = _mt_list;
         _mt_list = mtnp;
#ifdef HAVE_REGEX
         if (!_mt_regcomp(mtnp))
            v = NULL;
#endif
      }
   } else if (!asccasecmp(*argv, "delete") || !asccasecmp(*argv, "deleteall")) {
      bool_t delall = (argv[0][6] != '\0');

      v = NULL;
      if (_mt_list == NULL || _mt_list == (struct mtnode*)-1)
         ;
      else for (++argv, lnp = NULL, mtnp = _mt_list; mtnp != NULL;) {
         char const *typ;
         char *val;
         size_t i;

         if ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            typ = "";
            i = 0;
         } else {
            typ = _mt_typnames[mtnp->mt_flags & __MT_TMASK];
            i = strlen(typ);
         }

         val = ac_alloc(i + mtnp->mt_mtlen +1);
         memcpy(val, typ, i);
         memcpy(val + i, mtnp->mt_line, mtnp->mt_mtlen);
         val[i += mtnp->mt_mtlen] = '\0';
         i = asccasecmp(val, *argv);
         ac_free(val);

         if (!i) {
            struct mtnode *nnp = mtnp->mt_next;
            if (lnp == NULL)
               _mt_list = nnp;
            else
               lnp->mt_next = nnp;
            _mtnode_free(mtnp);
            mtnp = nnp;
            v = (void*)0x1;
            if (!delall)
               break;
         } else
            lnp = mtnp, mtnp = mtnp->mt_next;
      }
   } else {
jerr:
      fprintf(stderr, "Synopsis: mimetypes: %s\n",
         _("<show> or <clear> type list; <add> or <delete[all]> <type>s"));
      v = NULL;
   }
jleave:
   NYD_LEAVE;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL char *
mime_classify_content_type_by_filename(char const *name)
{
   char *content = NULL;
   struct mtnode *mtnp;
   char const *n_ext;
   size_t nlen, elen, i;
   NYD_ENTER;

   if (_mt_list == NULL)
      _mt_init();
   if (NELEM(_mt_bltin) == 0 && _mt_list == (struct mtnode*)-1)
      goto jleave;

   if ((nlen = strlen(name)) == 0)
      goto jleave;

   /* Try to isolate an extension */
   for (n_ext = name + nlen, elen = 0;; ++elen)
      if (n_ext[-1] == '.')
         break;
      else if (--n_ext == name) {
         elen = 0;
         DBG( n_ext = name + nlen; )
         break;
      }

   /* And then check all the MIME types */
   for (mtnp = _mt_list; mtnp != NULL;) {
      char const *line, *m_ext, *cp;
      struct mtnode *xnp = mtnp;

      mtnp = mtnp->mt_next; /* (_mt_regcomp() may drop current node) */
      line = xnp->mt_line;

#ifdef HAVE_REGEX
      if (xnp->mt_flags & _MT_REGEX) {
         if ((xnp->mt_regex == NULL && !_mt_regcomp(xnp)) ||
               regexec(xnp->mt_regex, name, 0,NULL, 0) == REG_NOMATCH)
            continue;
         goto jfound;
      } else
#endif
      for (m_ext = line + xnp->mt_mtlen;; m_ext = cp) {
         cp = m_ext;
         while (whitechar(*cp))
            ++cp;
         m_ext = cp;
         while (!whitechar(*cp) && *cp != '\0')
            ++cp;

         if ((i = PTR2SIZE(cp - m_ext)) == 0)
            break;
         else if (elen != i || ascncasecmp(n_ext, m_ext, elen))
            continue;

         /* Found it */
#ifdef HAVE_REGEX
jfound:
#endif
         i = xnp->mt_mtlen;
         if ((xnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            name = "";
            elen = 0;
         } else {
            name = _mt_typnames[xnp->mt_flags & __MT_TMASK];
            elen = strlen(name);
         }
         content = salloc(i + elen +1);
         if (elen)
            memcpy(content, name, elen);
         memcpy(content + elen, line, i);
         content[elen += i] = '\0';
         goto jleave;
      }
   }
jleave:
   NYD_LEAVE;
   return content;
}

/* s-it-mode */
