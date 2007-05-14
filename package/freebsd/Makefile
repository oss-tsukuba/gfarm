# New ports collection makefile for:	gfarm
# Date created:		13 July 2005
# Whom:			SODA Noriyuki <soda@sra.co.jp>
#
# $FreeBSD$
#

PORTNAME=	gfarm
PORTVERSION=	1.4.99
CATEGORIES=	net
MASTER_SITES=	http://datafarm.apgrid.org/software/${PORTVERSION}/

PATCH_SITES=	${MASTER_SITES}

MAINTAINER=	soda@sra.co.jp
COMMENT=	User-level grid filesystem and parallel process management

GNU_CONFIGURE=	yes
USE_GMAKE=	yes
USE_OPENSSL=	yes
USE_OPENLDAP=	yes
USE_LIBTOOL_VER=15
INSTALLS_SHLIB= yes
SYSCONFDIR?=	${PREFIX}/etc
CONFIGURE_ARGS+=--sysconfdir=${SYSCONFDIR}

LIB_DEPENDS=	pq.4:${PORTSDIR}/${POSTGRESQL_PORT}
POSTGRESQL_PORT?=	databases/postgresql80-client

USE_RC_SUBR=	yes
RC_SCRIPTS_SUB=	PREFIX=${PREFIX} SYSCONFDIR=${SYSCONFDIR} RC_SUBR=${RC_SUBR}
post-patch:
	@${SED} ${RC_SCRIPTS_SUB:S/$/!g/:S/^/ -e s!%%/:S/=/%%!/} \
		${FILESDIR}/gfmd.sh > ${WRKDIR}/gfmd.sh
	@${SED} ${RC_SCRIPTS_SUB:S/$/!g/:S/^/ -e s!%%/:S/=/%%!/} \
		${FILESDIR}/gfsd.sh > ${WRKDIR}/gfsd.sh
post-install:
	@${INSTALL_SCRIPT} ${WRKDIR}/gfmd.sh ${PREFIX}/etc/rc.d/
	@${INSTALL_SCRIPT} ${WRKDIR}/gfsd.sh ${PREFIX}/etc/rc.d/

.if defined(WITH_GLOBUS)

GPT_LOCATION=	${LOCALBASE}
GLOBUS_LOCATION=${LOCALBASE}/globus
GLOBUS_FLAVOR?=	gcc32dbg

RC_SCRIPTS_SUB+=	GLOBUS_LOCATION=${GLOBUS_LOCATION}

.if defined(LDCONFIG_R_DOES_WORK_FOR_GLOBUS)
# XXX This doesn't work, because "ldconfig -r" doesn't show the libraries.
LIB_DEPENDS+=	globus_gssapi_gsi_${GLOBUS_FLAVOR}:${PORTSDIR}/misc/globus2
.else # So, workaround the problem
BUILD_DEPENDS=	${GLOBUS_LOCATION}/bin/globus-makefile-header:${PORTSDIR}/misc/globus2
RUN_DEPENDS=	${BUILD_DEPENDS}
.endif

# XXX Workaround a mistake in ports/misc/globus2.
# Without the following environment, globus-makefile-header complains
# that it can't locate Grid/GPT/Dependencies.pm in @INC.
PERL5LIB_FIX=	PERL5LIB=${LOCALBASE}/lib/perl

# globus-makefile-header will be called from configure
pre-configure:
	@if ! ${SETENV} ${PERL5LIB_FIX} GLOBUS_LOCATION=${GLOBUS_LOCATION} \
	    ${GLOBUS_LOCATION}/bin/globus-makefile-header \
	    -flavor=${GLOBUS_FLAVOR} globus_gssapi_gsi >/dev/null; then \
		${ECHO_CMD} "${PKGNAME}: globus-makefile-header failed."; \
		${ECHO_CMD} "Probably you need to run the following command:"; \
		${ECHO_CMD} "	${SETENV} GPT_LOCATION=${GPT_LOCATION} GLOBUS_LOCATION=${GLOBUS_LOCATION} ${GPT_LOCATION}/sbin/gpt-postinstall"; \
		${FALSE}; \
	fi

CONFIGURE_ARGS+=--with-globus=${GLOBUS_LOCATION} \
		--with-globus-flavor=${GLOBUS_FLAVOR}

CONFIGURE_ENV+=	${PERL5LIB_FIX}

# XXX
# GSI authentication doesn't work, if gfarm is linked with OpenLDAP in ports.
# Probably this is because of OpenSSL conflicts between Globus and ports
# (OpenLDAP depends on OpenSSL in ports).
CONFIGURE_ARGS+=--without-openldap

.endif # WITH_GLOBUS

# TODO:
# - split into fine-grained packages.
#   e.g. gfarm-{libs,client,fsnode,server,devel}

.include <bsd.port.mk>
