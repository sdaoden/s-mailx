/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `(un)?mimetype' and other mime.types(5) related facilities.
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

   _MT_LOADED  = 1<< 4,       /* Not struct mtbltin */
   _MT_USR     = 1<< 5,       /* MIME_TYPES_USR */
   _MT_SYS     = 1<< 6,       /* MIME_TYPES_SYS */
   _MT_PLAIN   = 1<< 7        /* Without pipe handler display as text */
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

/* */
static bool_t           _mt_is_init;
static struct mtnode    *_mt_list;

/* Initialize MIME type list in order */
static void             _mt_init(void);
static bool_t           __mt_load_file(ui32_t orflags,
                           char const *file, char **line, size_t *linesize);

/* Create (prepend) a new MIME type; cmdcalled results in a bit more verbosity
 * for `mimetype' */
static struct mtnode *  _mt_create(bool_t cmdcalled, ui32_t orflags,
                           char const *line, size_t len);

static void
_mt_init(void)
{
   struct mtnode *tail;
   char c, *line; /* TODO line pool (below) */
   size_t linesize;
   ui32_t i, j;
   char const *srcs_arr[10], *ccp, **srcs;
   NYD_ENTER;

   /*if (_mt_is_init)
    *  goto jleave;*/

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
      mtnp->mt_line = mtbp->mtb_line;
   }

   /* Decide which files sources have to be loaded */
   if ((ccp = ok_vlook(mimetypes_load_control)) == NULL)
      ccp = "US";
   else if (*ccp == '\0')
      goto jleave;

   srcs = srcs_arr + 2;
   srcs[-1] = srcs[-2] = NULL;

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
               else
                  fprintf(stderr,
                     _("*mimetypes-load-control*: too many sources, "
                        "skipping \"%s\"\n"), ccp);
               continue;
            }
            /* FALLTHRU */
         default:
            goto jecontent;
         }
      }
   } else for (i = 0; (c = ccp[i]) != '\0'; ++i)
      switch (c) {
      case 'S': case 's': srcs_arr[1] = MIME_TYPES_SYS; break;
      case 'U': case 'u': srcs_arr[0] = MIME_TYPES_USR; break;
      default:
jecontent:
         fprintf(stderr,
            _("*mimetypes-load-control*: unsupported content: \"%s\"\n"), ccp);
         goto jleave;
      }

   /* Load all file-based sources in the desired order */
   line = NULL;
   linesize = 0;
   for (j = 0, i = (ui32_t)PTR2SIZE(srcs - srcs_arr), srcs = srcs_arr;
         i > 0; ++j, ++srcs, --i)
      if (*srcs == NULL)
         continue;
      else if (!__mt_load_file((j == 0 ? _MT_USR : (j == 1 ? _MT_SYS : 0)),
            *srcs, &line, &linesize)) {
         if ((options & OPT_D_V) || j > 1)
            fprintf(stderr,
               _("*mimetypes-load-control*: can't open or load \"%s\"\n"),
               *srcs);
      }
   if (line != NULL)
      free(line);
jleave:
   _mt_is_init = TRU1;
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
      if ((mtnp = _mt_create(FAL0, orflags, *line, len)) != NULL) {
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
_mt_create(bool_t cmdcalled, ui32_t orflags, char const *line, size_t len)
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

   /* (But wait - is there a type marker?) */
   if (!(orflags & (_MT_USR | _MT_SYS)) && *typ == '@') {
      if (len < 2 || typ[1] != ' ')
         goto jeinval;
      orflags |= _MT_PLAIN;
      typ += 2;
      len -= 2;
      line += 2;
   }

   while (len > 0 && !blankchar(*line))
      ++line, --len;
   /* Ignore empty lines and even incomplete specifications (only MIME type)
    * because this is quite common in mime.types(5) files */
   if (len == 0 || (tlen = PTR2SIZE(line - typ)) == 0) {
      if (cmdcalled)
         fprintf(stderr, _("Empty MIME type or no extensions given: \"%s\"\n"),
            (len == 0 ? _("(no value)") : line));
      goto jleave;
   }

   if ((subtyp = memchr(typ, '/', tlen)) == NULL) {
jeinval:
      if (cmdcalled || (options & OPT_D_V))
         fprintf(stderr, _("%s MIME type: \"%s\"\n"),
            (cmdcalled ? _("Invalid") : _("mime.types(5): invalid")), typ);
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

   /* Strip leading whitespace from the list of extensions;
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

FL int
c_mimetype(void *v)
{
   char **argv = v;
   struct mtnode *mtnp;
   NYD_ENTER;

   if (!_mt_is_init)
      _mt_init();

   if (*argv == NULL) {
      FILE *fp;
      size_t l;

      if (_mt_list == NULL) {
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
            (mtnp->mt_flags & _MT_USR ? 'U'
               : (mtnp->mt_flags & _MT_SYS ? 'S'
               : (mtnp->mt_flags & _MT_LOADED ? 'F' : 'B'))),
            (mtnp->mt_flags & _MT_PLAIN ? '@' : ' '),
            typ, (int)mtnp->mt_mtlen, mtnp->mt_line,
            mtnp->mt_line + mtnp->mt_mtlen);
      }

      page_or_print(fp, l);
      Fclose(fp);
   } else {
      for (; *argv != NULL; ++argv) {
         mtnp = _mt_create(TRU1, _MT_LOADED, *argv, strlen(*argv));
         if (mtnp != NULL) {
            mtnp->mt_next = _mt_list;
            _mt_list = mtnp;
         } else
            v = NULL;
      }
   }
jleave:
   NYD_LEAVE;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL int
c_unmimetype(void *v)
{
   char **argv = v;
   struct mtnode *lnp, *mtnp;
   bool_t match;
   NYD_ENTER;

   /* Need to load that first as necessary */
   if (!_mt_is_init)
      _mt_init();

   for (; *argv != NULL; ++argv) {
      if (!asccasecmp(*argv, "reset")) {
         _mt_is_init = FAL0;
         goto jdelall;
      }

      if (argv[0][0] == '*' && argv[0][1] == '\0') {
jdelall:
         while ((mtnp = _mt_list) != NULL) {
            _mt_list = mtnp->mt_next;
            free(mtnp);
         }
         continue;
      }

      for (match = FAL0, lnp = NULL, mtnp = _mt_list; mtnp != NULL;) {
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
            free(mtnp);
            mtnp = nnp;
            match = TRU1;
         } else
            lnp = mtnp, mtnp = mtnp->mt_next;
      }
      if (!match) {
         if (!(pstate & PS_IN_LOAD) || (options & OPT_D_V))
            fprintf(stderr, _("No such MIME type: \"%s\"\n"), *argv);
         v = NULL;
      }
   }
   NYD_LEAVE;
   return (v == NULL ? !STOP : !OKAY); /* xxx 1:bad 0:good -- do some */
}

FL char *
mime_classify_content_type_by_filename(char const *name)
{
   char *content = NULL;
   struct mtnode *mtnp;
   size_t nlen, i, j;
   char const *line, *ext, *cp;
   NYD_ENTER;

   if (!_mt_is_init)
      _mt_init();

   if ((nlen = strlen(name)) == 0)
      goto jleave;

   /* And then check all the MIME types */
   for (mtnp = _mt_list; mtnp != NULL; mtnp = mtnp->mt_next)
      for (ext = (line = mtnp->mt_line) + mtnp->mt_mtlen;; ext = cp) {
         cp = ext;
         while (whitechar(*cp))
            ++cp;
         ext = cp;
         while (!whitechar(*cp) && *cp != '\0')
            ++cp;

         if ((i = PTR2SIZE(cp - ext)) == 0)
            break;
         /* Don't allow neither of ".txt" or "txt" to match "txt" */
         else if (i + 1 >= nlen || name[(j = nlen - i) - 1] != '.' ||
               ascncasecmp(name + j, ext, i))
            continue;

         /* Found it */
         i = mtnp->mt_mtlen;
         if ((mtnp->mt_flags & __MT_TMASK) == _MT_OTHER) {
            name = "";
            j = 0;
         } else {
            name = _mt_typnames[mtnp->mt_flags & __MT_TMASK];
            j = strlen(name);
         }
         content = salloc(i + j +1);
         if (j > 0)
            memcpy(content, name, j);
         memcpy(content + j, line, i);
         content[j += i] = '\0';
         goto jleave;
      }
jleave:
   NYD_LEAVE;
   return content;
}

/* s-it-mode */
