# Sccsid @(#)nail.spec	1.5 (gritter) 11/2/02
Summary: A MIME capable implementation of the mailx command
Name: nail
Version: 10.2
Release: 1
License: BSD
Group: Applications/Internet
Source: nail-10.2.tar.gz
URL: <http://omnibus.ruf.uni-freiburg.de/~gritter/>
Vendor: Gunnar Ritter <g-r@bigfoot.de>
Packager: Didar Hussain <dhs@rediffmail.com>
BuildRoot: %{_tmppath}/%{name}-root

%description
Nail is derived from Berkeley Mail 8.1 and is intended provide the 
functionality of the POSIX.2 mailx command with additional support
for MIME messages, POP3 and SMTP.

Install nail if you need a command line tool with the ability to
handle MIME messages.

%prep
rm -fr %{buildroot}

%setup


%configure --prefix=/usr --with-openssl

%build
make

%install
make DESTDIR=%{buildroot} install
gzip -9 %{buildroot}/usr/share/man/man1/nail.1

%clean
cd ..; rm -fr %{_builddir}/%{name}-%{version}


%files
%doc COPYING AUTHORS INSTALL README TODO I18N ChangeLog
%config /etc/nail.rc
/usr/bin/nail
/usr/share/man/man1/nail.1.gz
