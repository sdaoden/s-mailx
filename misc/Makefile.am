# Makefile.am of the `misc' subdirectory for nail, a mail user agent.
# Written by Gunnar Ritter.

# automake generates dependencies that fail on non-GNU systems.
AUTOMAKE_OPTIONS=no-dependencies foreign
EXTRA_DIST = nail.help nail.rc nail.tildehelp

install-data-local:
	if test -f $(sysconfdir)/nail.rc; \
		then mv $(sysconfdir)/nail.rc $(sysconfdir)/nail.rc.old; fi
	$(mkinstalldirs) $(sysconfdir)
	$(INSTALL_DATA) $(srcdir)/nail.rc $(sysconfdir)/nail.rc
	$(mkinstalldirs) $(libdir)
	$(INSTALL_DATA) $(srcdir)/nail.help $(libdir)/nail.help
	$(INSTALL_DATA) $(srcdir)/nail.tildehelp $(libdir)/nail.tildehelp

uninstall-local:
	rm -f $(libdir)/nail.help $(libdir)/nail.tildehelp
