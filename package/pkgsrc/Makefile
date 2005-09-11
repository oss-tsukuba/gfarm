# $NetBSD$

DISTNAME=	gfarm-${GFARM_VERSION}
CATEGORIES=	parallel net
MASTER_SITES=	http://datafarm.apgrid.org/software/${GFARM_VERSION}/

PATCH_SITES=	${MASTER_SITES}
PATCHFILES=	gfarm-1.2-patch1.diff.gz \
		gfarm-1.2-patch2.diff.gz \
		gfarm-1.2-patch3.diff.gz
PATCH_DIST_STRIP= -p1

MAINTAINER=	soda@NetBSD.org
HOMEPAGE=	http://datafarm.apgrid.org/
COMMENT=	User-level grid filesystem and parallel process management

GFARM_VERSION=	1.2

USE_PKGINSTALL=	yes
USE_TOOLS+=	gmake # NetBSD-make works too, but pkgsrc supports non-NetBSD.
USE_LIBTOOL=	yes
GNU_CONFIGURE=	yes
CONFIGURE_ARGS+=--sysconfdir=${PKG_SYSCONFDIR}

default_htmldir=${PREFIX}/share/doc/html/gfarm
ja_mandir=	${PREFIX}/man/ja_JP.eucJP
MAKEFLAGS+=	default_htmldir=${default_htmldir} ja_mandir=${ja_mandir}

RCD_SCRIPTS=	gfmd gfsd
GFARM_PID_DIR=	${VARBASE}/run	# default directory for PID files
FILES_SUBST+=	GFARM_PID_DIR=${GFARM_PID_DIR}

.include "../../security/openssl/buildlink3.mk"
.include "../../databases/openldap/buildlink3.mk"
.include "../../mk/pthread.buildlink3.mk"
.include "../../mk/bsd.pkg.mk"

# TODO:
# - enable GSI, but this requires Globus in pkgsrc.
# - provide scripts to create configuration files.
# - split into fine-grained packages.
#   e.g. gfarm-{libs,client,fsnode,server,devel}
