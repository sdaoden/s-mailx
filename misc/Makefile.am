# Makefile.am of the `misc' subdirectory for nail, a mail user agent.
# Written by Gunnar Ritter.

# automake generates dependencies that fail on non-GNU systems.
AUTOMAKE_OPTIONS=no-dependencies foreign
EXTRA_DIST = nail.help nail.rc nail.tildehelp

install-data-local:
	$(INSTALL_DATA) $(srcdir)/nail.rc $(sysconfdir)
	$(INSTALL_DATA) $(srcdir)/nail.help $(libdir)
	$(INSTALL_DATA) $(srcdir)/nail.tildehelp $(libdir)

uninstall-local:
	rm -f $(libdir)/nail.help $(libdir)/nail.tildehelp
