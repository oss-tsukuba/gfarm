# $NetBSD$

DISTNAME=	gfarm-${GFARM_VERSION}
CATEGORIES=	parallel net
MASTER_SITES=	http://datafarm.apgrid.org/software/${GFARM_VERSION}/

PATCH_SITES=	${MASTER_SITES}

MAINTAINER=	soda@NetBSD.org
HOMEPAGE=	http://datafarm.apgrid.org/
COMMENT=	User-level grid filesystem and parallel process management

GFARM_VERSION=	1.4

USE_PKGINSTALL=	yes
USE_TOOLS+=	gmake # NetBSD-make works too, but pkgsrc supports non-NetBSD.
USE_LIBTOOL=	yes
GNU_CONFIGURE=	yes
CONFIGURE_ARGS+=--sysconfdir=${PKG_SYSCONFDIR}

PGSQL_VERSIONS_ACCEPTED?=	81 80 # 74 # 7.4 has some minor problems

default_htmldir=${PREFIX}/share/doc/html/gfarm
ja_mandir=	${PREFIX}/man/ja_JP.eucJP
MAKEFLAGS+=	default_htmldir=${default_htmldir} ja_mandir=${ja_mandir}

RCD_SCRIPTS=	gfmd gfsd
GFARM_PID_DIR=	${VARBASE}/run	# default directory for PID files
FILES_SUBST+=	GFARM_PID_DIR=${GFARM_PID_DIR}

.include "../../security/openssl/buildlink3.mk"
.include "../../databases/openldap-client/buildlink3.mk"
.include "../../mk/pgsql.buildlink3.mk"
.include "../../mk/pthread.buildlink3.mk"
.include "../../mk/bsd.pkg.mk"

# TODO:
# - enable GSI, but this requires Globus in pkgsrc.
# - split into fine-grained packages.
#   e.g. gfarm-{libs,client,fsnode,server,devel}
