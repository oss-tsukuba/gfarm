# New ports collection makefile for:	gfarm
# Date created:		13 July 2005
# Whom:			SODA Noriyuki <soda@sra.co.jp>
#
# $FreeBSD$
#

PORTNAME=	gfarm
PORTVERSION=	1.1.2
CATEGORIES=	net
MASTER_SITES=	http://datafarm.apgrid.org/software/${PORTVERSION}/

MAINTAINER=	soda@sra.co.jp
COMMENT=	User-level grid filesystem and parallel process management

GNU_CONFIGURE=	yes
USE_GMAKE=	yes
USE_OPENSSL=	yes
USE_OPENLDAP=	yes
USE_LIBTOOL_VER=15
INSTALLS_SHLIB= yes

USE_RC_SUBR=	yes
RC_SCRIPTS_SUB=	PREFIX=${PREFIX} RC_SUBR=${RC_SUBR}
post-patch:
	@${SED} ${RC_SCRIPTS_SUB:S/$/!g/:S/^/ -e s!%%/:S/=/%%!/} \
		${FILESDIR}/gfmd.sh > ${WRKDIR}/gfmd.sh
	@${SED} ${RC_SCRIPTS_SUB:S/$/!g/:S/^/ -e s!%%/:S/=/%%!/} \
		${FILESDIR}/gfsd.sh > ${WRKDIR}/gfsd.sh
post-install:
	@${INSTALL_SCRIPT} ${WRKDIR}/gfmd.sh ${PREFIX}/etc/rc.d/
	@${INSTALL_SCRIPT} ${WRKDIR}/gfsd.sh ${PREFIX}/etc/rc.d/

# TODO:
# - link globus, and enable GSI
# - provide scripts to create configuration files.
# - split into fine-grained packages.
#   e.g. gfarm-{libs,client,fsnode,server,devel}

.include <bsd.port.mk>
