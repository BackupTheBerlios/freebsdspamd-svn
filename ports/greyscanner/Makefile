# New ports collection makefile for:	greyscanner
# Date created:		05 Mai 2007
# Whom:			Olli Hauer
#
# $FreeBSD$
#

PORTNAME= greyscanner
PORTVERSION= 4.2
CATEGORIES=	mail
MASTER_SITES=
DISTFILES=

MAINTAINER=	ohauer@gmx.de
COMMENT=	Script that runs as daemon to cleanup OpenBSD spamd database

RUN_DEPENDS= ${LOCALBASE}/libexec/spamd:${PORTSDIR}/mail/spamd \
			${SITE_PERL}/Email/Valid.pm:${PORTSDIR}/mail/p5-Email-Valid

NO_BUILD= yes
PLIST_FILES= libexec/greyscanner
SUB_FILES= pkg-message
PORTEXAMPLES= spamd_rrd

USE_RC_SUBR= greyscanner.sh
SUB_LIST= PERL=${PERL}
USE_PERL5= yes

pre-patch:
	@${SED} -e 's|%%PERL%%|${PERL}|g' \
		-e 's|%%PREFIX%%|/usr/local|g' \
	    ${FILESDIR}/greyscanner > ${WRKDIR}/greyscanner

do-install:
	@${MKDIR} ${EXAMPLESDIR}
	${INSTALL_SCRIPT} -m 740 ${WRKDIR}/greyscanner ${PREFIX}/libexec
	${INSTALL_SCRIPT} -m 740 ${FILESDIR}/spamd_rrd ${EXAMPLESDIR}/

post-install:
	@${CAT} ${PKGMESSAGE}
.include <bsd.port.mk>
