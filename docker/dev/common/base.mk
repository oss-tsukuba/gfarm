# This file need config.mk

include $(ROOTDIR)/config-default.mk
include $(ROOTDIR)/config.mk

.PHONY: help ps build up down start stop shell shell-root shell-user \
		regress test-fo systest test-all valgrind-gfmd docker-compose \
		gen

ifndef BUILD_IMAGE_ORDER
$(error BUILD_IMAGE_ORDER is not defined)
endif

ifndef GFDOCKER_IP_VERSION
$(error GFDOCKER_IP_VERSION is not defined)
endif

ifndef GFDOCKER_SUBNET
$(error GFDOCKER_SUBNET is not defined)
endif

ifndef GFDOCKER_START_HOST_ADDR
$(error GFDOCKER_START_HOST_ADDR is not defined)
endif

ifneq ($(and $(GFDOCKER_PROXY_HOST),$(GFDOCKER_PROXY_PORT)),)
GFDOCKER_ENABLE_PROXY = true
endif

PROXY_URL = http://$(GFDOCKER_PROXY_HOST):$(GFDOCKER_PROXY_PORT)/
GFDOCKER_USERNAME_PREFIX = user
GFDOCKER_PRIMARY_USER = $(GFDOCKER_USERNAME_PREFIX)1
PRIMARY_CLIENT_CONTAINER = $(GFDOCKER_HOSTNAME_PREFIX_CLIENT)1
TOP = $(ROOTDIR)/../..

#COMPOSE_YML = $(TOP)/docker/dev/docker-compose.yml
COMPOSE_YML = $(ROOTDIR)/docker-compose.yml

ifneq ($(GFDOCKER_NO_CACHE), 0)
NO_CACHE = --no-cache
else
NO_CACHE =
endif
DOCKER_BUILD_ENV =
DOCKER_BUILD_FLAGS = \
		$(NO_CACHE) \
		--build-arg TZ='$(TZ)' \
		--build-arg LANG='$(LANG)' \
		--build-arg GFDOCKER_NUM_JOBS='$(GFDOCKER_NUM_JOBS)' \
		--build-arg GFDOCKER_USERNAME_PREFIX='$(GFDOCKER_USERNAME_PREFIX)' \
		--build-arg GFDOCKER_PRIMARY_USER='$(GFDOCKER_PRIMARY_USER)' \
		--build-arg GFDOCKER_NUM_GFMDS='$(GFDOCKER_NUM_GFMDS)' \
		--build-arg GFDOCKER_NUM_GFSDS='$(GFDOCKER_NUM_GFSDS)' \
		--build-arg GFDOCKER_NUM_USERS='$(GFDOCKER_NUM_USERS)' \
		--build-arg GFDOCKER_HOSTNAME_PREFIX_GFMD='$(GFDOCKER_HOSTNAME_PREFIX_GFMD)' \
		--build-arg GFDOCKER_HOSTNAME_PREFIX_GFSD='$(GFDOCKER_HOSTNAME_PREFIX_GFSD)' \
		--build-arg GFDOCKER_HOSTNAME_SUFFIX='$(GFDOCKER_HOSTNAME_SUFFIX)' \
		--build-arg GFDOCKER_USE_SAN_FOR_GFSD='$(GFDOCKER_USE_SAN_FOR_GFSD)'

ifdef GFDOCKER_ENABLE_PROXY
DOCKER_BUILD_ENV = \
               http_proxy='$(PROXY_URL)' \
               https_proxy='$(PROXY_URL)' \
               no_proxy='$(GFDOCKER_NO_PROXY)' \
               HTTP_PROXY='$(PROXY_URL)' \
               HTTPS_PROXY='$(PROXY_URL)' \
               NO_PROXY='$(GFDOCKER_NO_PROXY)'
DOCKER_BUILD_FLAGS += \
		--build-arg http_proxy='$(PROXY_URL)' \
		--build-arg https_proxy='$(PROXY_URL)' \
		--build-arg no_proxy='$(GFDOCKER_NO_PROXY)' \
		--build-arg HTTP_PROXY='$(PROXY_URL)' \
		--build-arg HTTPS_PROXY='$(PROXY_URL)' \
		--build-arg NO_PROXY='$(GFDOCKER_NO_PROXY)' \
		--build-arg GFDOCKER_PROXY_HOST='$(GFDOCKER_PROXY_HOST)' \
		--build-arg GFDOCKER_PROXY_PORT='$(GFDOCKER_PROXY_PORT)' \
		--build-arg GFDOCKER_ENABLE_PROXY='$(GFDOCKER_ENABLE_PROXY)' \
                --build-arg MAVEN_OPTS='-Dhttp.proxyHost=$(GFDOCKER_PROXY_HOST) \
                        -Dhttp.proxyPort=$(GFDOCKER_PROXY_PORT) \
                        -Dhttps.proxyHost=$(GFDOCKER_PROXY_HOST) \
                        -Dhttps.proxyPort=$(GFDOCKER_PROXY_PORT)'
endif

IMAGE_BASENAME = gfarm-dev

STOP_TIMEOUT = 3

DOCKER = $(SUDO) $(DOCKER_BUILD_ENV) $(DOCKER_CMD)

GFDOCKER_PRJ_NAME_FULL=gfarm-$(GFDOCKER_PRJ_NAME)

COMPOSE = $(SUDO) $(DOCKER_BUILD_ENV) \
	COMPOSE_PROJECT_NAME=$(GFDOCKER_PRJ_NAME_FULL) \
	GFDOCKER_PRJ_NAME=$(GFDOCKER_PRJ_NAME) $(DOCKER_COMPOSE_CMD) \
	-f $(COMPOSE_YML)
CONTSHELL_FLAGS = \
		--env TZ='$(TZ)' \
		--env LANG='$(LANG)' \
		$${TERM+--env TERM='$(TERM)'}\
		--env GFDOCKER_PRJ_NAME='$(GFDOCKER_PRJ_NAME)' \
		--env GFDOCKER_SUBNET='$(GFDOCKER_SUBNET)' \
		--env GFDOCKER_START_HOST_ADDR='$(GFDOCKER_START_HOST_ADDR)' \
		--env GFDOCKER_USERNAME_PREFIX='$(GFDOCKER_USERNAME_PREFIX)' \
		--env GFDOCKER_PRIMARY_USER='$(GFDOCKER_PRIMARY_USER)' \
		--env GFDOCKER_NUM_GFMDS='$(GFDOCKER_NUM_GFMDS)' \
		--env GFDOCKER_NUM_GFSDS='$(GFDOCKER_NUM_GFSDS)' \
		--env GFDOCKER_NUM_CLIENTS='$(GFDOCKER_NUM_CLIENTS)' \
		--env GFDOCKER_NUM_USERS='$(GFDOCKER_NUM_USERS)' \
		--env GFDOCKER_HOSTNAME_PREFIX_GFMD='$(GFDOCKER_HOSTNAME_PREFIX_GFMD)' \
		--env GFDOCKER_HOSTNAME_PREFIX_GFSD='$(GFDOCKER_HOSTNAME_PREFIX_GFSD)' \
		--env GFDOCKER_HOSTNAME_PREFIX_CLIENT='$(GFDOCKER_HOSTNAME_PREFIX_CLIENT)' \
		--env GFDOCKER_HOSTNAME_SUFFIX='$(GFDOCKER_HOSTNAME_SUFFIX)' \
		--env GFDOCKER_SASL_MECH_LIST='$(GFDOCKER_SASL_MECH_LIST)' \
		--env GFDOCKER_SASL_LOG_LEVEL='$(GFDOCKER_SASL_LOG_LEVEL)' \
		--env GFDOCKER_SASL_HPCI_SECET='$(GFDOCKER_SASL_HPCI_SECET)' \
		--env GFDOCKER_SASL_PASSPHRASE='$(GFDOCKER_SASL_PASSPHRASE)' \
		--env GFDOCKER_SASL_XOAUTH2_SCOPE='$(GFDOCKER_SASL_XOAUTH2_SCOPE)' \
		--env GFDOCKER_SASL_XOAUTH2_SCOPE='$(GFDOCKER_SASL_XOAUTH2_SCOPE)' \
		--env GFDOCKER_SASL_XOAUTH2_AUD='$(GFDOCKER_SASL_XOAUTH2_AUD)' \
		--env GFDOCKER_SASL_XOAUTH2_USER_CLAIM='$(GFDOCKER_SASL_XOAUTH2_USER_CLAIM)'

ifdef GFDOCKER_ENABLE_PROXY
CONTSHELL_FLAGS += \
		--env GFDOCKER_ENABLE_PROXY='$(GFDOCKER_ENABLE_PROXY)' \
		--env http_proxy='$(PROXY_URL)' \
		--env https_proxy='$(PROXY_URL)' \
		--env no_proxy='$(GFDOCKER_NO_PROXY)' \
		--env HTTP_PROXY='$(PROXY_URL)' \
		--env HTTPS_PROXY='$(PROXY_URL)' \
		--env NO_PROXY='$(GFDOCKER_NO_PROXY)'
endif

# ifeq ($(GFDOCKER_USE_SAN_FOR_GFSD), 1)
# CONTSHELL_FLAGS += --env GLOBUS_GSSAPI_NAME_COMPATIBILITY=HYBRID
# endif

CONTSHELL_COMMON = $(COMPOSE) exec $(CONTSHELL_FLAGS) \
	-u '$(GFDOCKER_PRIMARY_USER)'
CONTEXEC = $(CONTSHELL_COMMON) '$(PRIMARY_CLIENT_CONTAINER)'
CONTEXEC_GFMD1 = $(CONTSHELL_COMMON) gfmd1
### "bash -l" to load /etc/profile.d/gfarm.sh
CONTSHELL = $(CONTSHELL_COMMON) '$(PRIMARY_CLIENT_CONTAINER)' bash
CONTSHELL_GFMD1 = $(CONTSHELL_COMMON) gfmd1 bash

# overridable
CONTSHELL_ARGS :=  -c 'cd ~ && bash'

DOCKER_RUN = $(DOCKER) run $(CONTSHELL_FLAGS)

HOME_DIR = /home/$(GFDOCKER_PRIMARY_USER)
GFARM_SRC_DIR = $(HOME_DIR)/gfarm
SCRIPTS = $(GFARM_SRC_DIR)/docker/dev/common

help:
	@echo 'Usage:'
	@echo '  make help'
	@echo '  make ps'
	@echo '  make build'
	@echo '  make down'
	@echo '  make prune'
	@echo '  make REMOVE_ALL_IMAGES'
	@echo '  make reborn'
	@echo '  make start'
	@echo '  make stop'
	@echo '  make kerberos-setup'
	@echo '  make shell'
	@echo '  make shell-user'
	@echo '  make shell-root'
	@echo '  make regress'
	@echo '  make test-fo'
	@echo '  make systest'
	@echo '  make systest-all'
	@echo '  ARGS="docker-compose args..." make docker-compose'
	@echo '  make test-all'
	@echo '  make valgrind-gfmd'
	@echo '  make centos7'
	@echo '  make opensuse'

define check_config
if [ ! -d $(TOP)/gfarm2fs ]; then \
	echo '<Gfarm source directory>/gfarm2fs does not exist.' 1>&2; \
	false; \
fi
if ! [ -f $(TOP)/docker/dev/.shadow.config.mk ]; then \
	echo '.shadow.config.mk does not exist.' \
		'Containers are maybe down.' \
		'Please execute "make reborn".' 1>&2; \
	false; \
fi \
&& \
if ! diff -u $(TOP)/docker/dev/.shadow.config.mk \
		$(TOP)/docker/dev/config.mk 1>&2; then \
	echo 'Unexpected change of config.mk.' \
		'Change of config.mk can only be when container is down.' \
		1>&2; \
	false; \
fi
endef

ps:
	$(check_config)
	$(COMPOSE) ps

define build_common
for TAG in $(BUILD_IMAGE_ORDER); do \
	$(DOCKER) build -t "$(IMAGE_BASENAME):$${TAG}" \
		$(DOCKER_BUILD_FLAGS) $${DOCKER_BUILD_FLAGS2} \
		-f "$(TOP)/docker/dev/common/$${TAG}-Dockerfile" \
		'$(TOP)' || exit 1; \
done \
  && $(COMPOSE) build $(DOCKER_BUILD_FLAGS)
endef

define buildx_common
if ! $(SUDO) $(TOP)/docker/dev/common/qemu-user-static.sh check; then \
	echo "Please run 'make enable-qemu'"; \
	exit 1; \
fi && \
for TAG in $(BUILD_IMAGE_ORDER); do \
	$(DOCKER) buildx build -t "$(IMAGE_BASENAME):$${TAG}" \
		--platform $(GFDOCKER_PLATFORM) \
		$(DOCKER_BUILD_FLAGS) $${DOCKER_BUILD_FLAGS2} \
		-f "$(TOP)/docker/dev/common/$${TAG}-Dockerfile" \
		'$(TOP)' || exit 1; \
done \
  && $(COMPOSE) build $(DOCKER_BUILD_FLAGS)
endef

enable-qemu:
	$(SUDO) $(TOP)/docker/dev/common/qemu-user-static.sh enable

define build_switch
if [ -n "$(GFDOCKER_PLATFORM)" ]; then \
	$(buildx_common); \
else \
	$(build_common); \
fi
endef

define build
	DOCKER_BUILD_FLAGS2=""; \
	$(build_switch)
endef

define build_nocache
	DOCKER_BUILD_FLAGS2="--no-cache"; \
	$(build_switch)
endef

build:
	$(build) \

build-nocache:
	$(build_nocache)

# --remove-orphans may not be suppported
define down
	($(COMPOSE) down --volumes --remove-orphans -t $(STOP_TIMEOUT) \
	|| $(COMPOSE) down --volumes -t $(STOP_TIMEOUT) || true) \
	&& rm -f $(TOP)/docker/dev/.shadow.config.mk
endef

down:
	$(down)

define prune
	$(DOCKER) system prune -f
endef

prune:
	$(prune)

REMOVE_ALL_IMAGES:
	$(DOCKER) system prune -a

REMOVE_ALL_VOLUMES:
	$(DOCKER) system prune --volumes

define gen
TOP='$(TOP)' \
	GFDOCKER_PRIMARY_USER='$(GFDOCKER_PRIMARY_USER)' \
	GFDOCKER_NUM_GFMDS='$(GFDOCKER_NUM_GFMDS)' \
	GFDOCKER_NUM_GFSDS='$(GFDOCKER_NUM_GFSDS)' \
	GFDOCKER_NUM_CLIENTS='$(GFDOCKER_NUM_CLIENTS)' \
	GFDOCKER_IP_VERSION='$(GFDOCKER_IP_VERSION)' \
	GFDOCKER_SUBNET='$(GFDOCKER_SUBNET)' \
	GFDOCKER_START_HOST_ADDR='$(GFDOCKER_START_HOST_ADDR)' \
	GFDOCKER_HOSTNAME_PREFIX_GFMD='$(GFDOCKER_HOSTNAME_PREFIX_GFMD)' \
	GFDOCKER_HOSTNAME_PREFIX_GFSD='$(GFDOCKER_HOSTNAME_PREFIX_GFSD)' \
	GFDOCKER_HOSTNAME_PREFIX_CLIENT='$(GFDOCKER_HOSTNAME_PREFIX_CLIENT)' \
	GFDOCKER_HOSTNAME_SUFFIX='$(GFDOCKER_HOSTNAME_SUFFIX)' \
	GFDOCKER_HOSTPORT_S3_HTTP='$(GFDOCKER_HOSTPORT_S3_HTTP)' \
	GFDOCKER_HOSTPORT_S3_HTTPS='$(GFDOCKER_HOSTPORT_S3_HTTPS)' \
	GFDOCKER_HOSTPORT_S3_DIRECT='$(GFDOCKER_HOSTPORT_S3_DIRECT)' \
	GFDOCKER_AUTH_TYPE='$(GFDOCKER_AUTH_TYPE)' \
	GFDOCKER_GFMD_JOURNAL_DIR='$(GFDOCKER_GFMD_JOURNAL_DIR)' \
	GFDOCKER_PRJ_NAME='$(GFDOCKER_PRJ_NAME)' \
	GFDOCKER_SASL_USE_KEYCLOAK='$(GFDOCKER_SASL_USE_KEYCLOAK)' \
	GFDOCKER_SASL_HPCI_SECET='$(GFDOCKER_SASL_HPCI_SECET)' \
	GFDOCKER_SASL_PASSPHRASE='$(GFDOCKER_SASL_PASSPHRASE)' \
	'$(TOP)/docker/dev/common/gen.sh'
	cp $(TOP)/docker/dev/config.mk $(TOP)/docker/dev/.shadow.config.mk
endef

define up
mkdir -p $(ROOTDIR)/mnt
# readable for others
chmod 755 $(ROOTDIR)/mnt
$(COMPOSE) up -d --force-recreate\
  && $(CONTSHELL) -c '. ~/gfarm/docker/dev/common/up.rc'
endef

define reborn
	if [ -f $(COMPOSE_YML) ]; then \
		$(down); \
	else \
		echo 'warn: docker-compose does not exist.' 1>&2; \
	fi
	$(gen)
	$(COMPOSE) ps
	$(prune)
	if [ $(USE_NOCACHE) -eq 1 ]; then \
		$(build_nocache); \
	else \
		$(build); \
	fi
	$(up)
	if "$(GFDOCKER_SASL_USE_KEYCLOAK)"; then \
		$(COMPOSE) exec $(CONTSHELL_FLAGS) httpd /setup.sh; \
		$(COMPOSE) exec $(CONTSHELL_FLAGS) keycloak ./setup.sh; \
	fi
endef

reborn:
	$(reborn)
reborn: USE_NOCACHE = 0

reborn-nocache:
	$(reborn)
reborn-nocache: USE_NOCACHE = 1

reborn-without-build:
	$(down)
	$(up)

start:
	$(COMPOSE) start

stop:
	$(COMPOSE) stop -t $(STOP_TIMEOUT)

define shell_user
$(CONTSHELL) $(CONTSHELL_ARGS)
endef

shell:
	$(check_config)
	$(shell_user)

shell-user: shell

shell-root:
	echo "*** Please use sudo on shell-user ***"

shell-gfmd1:
	$(check_config)
	$(CONTSHELL_GFMD1) $(CONTSHELL_ARGS)

save-packages:
	$(check_config)
	$(CONTEXEC_GFMD1) $(SCRIPTS)/save_packages.sh

ECHO_ROOTDIR:
	@echo $(ROOTDIR)

ECHO_DOCKER:
	@echo $(DOCKER)

ECHO_COMPOSE:
	@echo $(COMPOSE)

COPY_FILES:
	$(ROOTDIR)/common/copy-updated-files-to-container.sh

REGEN_MANPAGES:
	$(ROOTDIR)/common/copy-manpages-from-container.sh

define regress
$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/regress.rc'
endef

regress:
	$(check_config)
	$(regress)

memcheck-gfmd memcheck-gfmd1 memcheck-gfmd2 memcheck-gfmd3 \
memcheck-gfsd memcheck-gfsd1 memcheck-gfsd2 memcheck-gfsd3 memcheck-gfsd4:
	$(check_config)
	target=`echo "$@" | sed 's/.*-//'`; \
	$(CONTSHELL) -c "hookconfig --$${target} memcheck"; \
	$(regress); \
	status=$$?; \
	$(CONTSHELL) -c "hookconfig --$${target} no-hook"; \
	exit $${status}

helgrind-gfmd helgrind-gfmd1 helgrind-gfmd2 helgrind-gfmd3:
	$(check_config)
	target=`echo "$@" | sed 's/.*-//'`; \
	$(CONTSHELL) -c "hookconfig --$${target} helgrind"; \
	$(CONTSHELL) -c '~/gfarm/docker/dev/common/regress.rc 3'; \
	status=$$?; \
	$(CONTSHELL) -c "hookconfig --$${target} no-hook"; \
	exit $${status}

memcheck-gfarm2fs:
	$(check_config)
	$(CONTSHELL) -c 'hookconfig --gfarm2fs memcheck.not-child'
	$(CONTSHELL) -c '~/gfarm/docker/dev/common/test_gfarm2fs.sh 1 3 '; \
	status=$$?; \
	$(CONTSHELL) -c 'hookconfig --gfarm2fs no-hook'; \
	exit $${status}

helgrind-gfarm2fs:
	$(check_config)
	$(CONTSHELL) -c 'hookconfig --gfarm2fs helgrind.not-child'
	$(CONTSHELL) -c '~/gfarm/docker/dev/common/test_gfarm2fs.sh 3 3'; \
	status=$$?; \
	$(CONTSHELL) -c 'hookconfig --gfarm2fs no-hook'; \
	exit $${status}

GFDOCKER_GFARMS3_ENV = \
	--env GFDOCKER_GFARMS3_FRONT_WEBSERVER='$(GFDOCKER_GFARMS3_FRONT_WEBSERVER)' \
	--env GFDOCKER_GFARMS3_CACHE_BASEDIR='$(GFDOCKER_GFARMS3_CACHE_BASEDIR)' \
	--env GFDOCKER_GFARMS3_CACHE_SIZE='$(GFDOCKER_GFARMS3_CACHE_SIZE)' \
	--env GFDOCKER_GFARMS3_WSGI_HOMEDIR='$(GFDOCKER_GFARMS3_WSGI_HOMEDIR)' \
	--env GFDOCKER_GFARMS3_WSGI_USER='$(GFDOCKER_GFARMS3_WSGI_USER)' \
	--env GFDOCKER_GFARMS3_WSGI_GROUP='$(GFDOCKER_GFARMS3_WSGI_GROUP)' \
	--env GFDOCKER_GFARMS3_WSGI_ADDR='$(GFDOCKER_GFARMS3_WSGI_ADDR)' \
	--env GFDOCKER_GFARMS3_ROUTER_HOMEDIR='$(GFDOCKER_GFARMS3_ROUTER_HOMEDIR)' \
	--env GFDOCKER_GFARMS3_ROUTER_ADDR='$(GFDOCKER_GFARMS3_ROUTER_ADDR)' \
	--env GFDOCKER_GFARMS3_USERS='$(GFDOCKER_GFARMS3_USERS)' \
	--env GFDOCKER_GFARMS3_MYPROXY_SERVER='$(GFDOCKER_GFARMS3_MYPROXY_SERVER)' \
	--env GFDOCKER_GFARMS3_SHARED_DIR='$(GFDOCKER_GFARMS3_SHARED_DIR)' \
	--env GFDOCKER_GFARMS3_SECRET_USER1='$(GFDOCKER_GFARMS3_SECRET_USER1)' \
	--env GFDOCKER_GFARMS3_SECRET_USER2='$(GFDOCKER_GFARMS3_SECRET_USER2)'


define hpcisetup
$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/s3/hpci.rc'
endef

hpci-setup:
	$(hpcisetup)

define s3setup
$(CONTSHELL) $(SCRIPTS)/s3/setup.sh
endef

# define s3uninstall
# $(CONTSHELL) -c '. ~/gfarm/docker/dev/common/s3/uninstall.rc'
# endef

s3setup:
	$(s3setup)
s3setup: CONTSHELL_FLAGS += $(GFDOCKER_GFARMS3_ENV)

s3update:
	$(s3setup)
s3update: CONTSHELL_FLAGS += $(GFDOCKER_GFARMS3_ENV) \
	--env GFDOCKER_GFARMS3_UPDATE_ONLY=1

# s3uninstall:
# 	$(s3setup)
# s3uninstall: CONTSHELL_FLAGS += $(GFDOCKER_GFARMS3_ENV)

define s3setuphpci
$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/s3/setup.rc'
endef

ifeq ($(MAKECMDGOALS),s3setup-for-hpci)

ifndef GFDOCKER_GFARMS3_USERS
$(error GFDOCKER_GFARMS3_USERS is not defined)
endif

ifndef GFDOCKER_GFARMS3_MYPROXY_SERVER
$(error GFDOCKER_GFARMS3_MYPROXY_SERVER is not defined)
endif

ifndef GFDOCKER_GFARMS3_SHARED_DIR
$(error GFDOCKER_GFARMS3_SHARED_DIR is not defined)
endif

endif

s3setup-for-hpci:
	$(s3setup)
s3setup-for-hpci: CONTSHELL_FLAGS += $(GFDOCKER_GFARMS3_ENV)

define s3test
$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/s3/test.rc'
endef

s3test:
	$(s3test)

kerberos-setup:
	$(CONTEXEC_GFMD1) $(SCRIPTS)/kerberos-setup-server.sh
	for i in $$(seq 1 $(GFDOCKER_NUM_GFMDS)); do \
	    h="$(GFDOCKER_HOSTNAME_PREFIX_GFMD)$${i}"; \
	    $(CONTSHELL_COMMON) $${h} $(SCRIPTS)/kerberos-setup-host.sh gfmd; \
	done
	for i in $$(seq 1 $(GFDOCKER_NUM_GFSDS)); do \
	    h="$(GFDOCKER_HOSTNAME_PREFIX_GFSD)$${i}"; \
	    $(CONTSHELL_COMMON) $${h} $(SCRIPTS)/kerberos-setup-host.sh gfsd; \
	done
	for i in $$(seq 1 $(GFDOCKER_NUM_CLIENTS)); do \
	    h="$(GFDOCKER_HOSTNAME_PREFIX_CLIENT)$${i}"; \
	    $(CONTSHELL_COMMON) $${h} \
	        $(SCRIPTS)/kerberos-setup-host.sh client; \
	done

# DO NOT USE THIS, currently this does not work.
# on CentOS 8:
#	kinit: Password incorrect while getting initial credentials
kerberos-keytab-regen:
	for i in $$(seq 1 $(GFDOCKER_NUM_CLIENTS)); do \
	    h="$(GFDOCKER_HOSTNAME_PREFIX_CLIENT)$${i}"; \
	    if $(CONTSHELL_COMMON) $${h} bash -c \
	        'kinit -k -t "$(HOME_DIR)/.keytab" "$(GFDOCKER_PRIMARY_USER)" \
	            2>&1 | grep "Password incorrect" >/dev/null'; \
	        then \
	            echo "NOTICE: regenerating $(HOME_DIR)/.keytab on $${h}"; \
	            $(CONTSHELL_COMMON) $${h} \
	                $(SCRIPTS)/kerberos-setup-host.sh client; \
	        fi; \
	done

# DO NOT USE THIS, because currently kerberos-keytab-regen does not work.
kerberos-kinit:
	for i in $$(seq 1 $(GFDOCKER_NUM_CLIENTS)); do \
	    h="$(GFDOCKER_HOSTNAME_PREFIX_CLIENT)$${i}"; \
	    echo "on $${h}:"; \
	    $(CONTSHELL_COMMON) $${h} \
	        kinit -k -t "$(HOME_DIR)/.keytab" "$(GFDOCKER_PRIMARY_USER)"; \
	done

kerberos-kdestroy:
	for i in $$(seq 1 $(GFDOCKER_NUM_CLIENTS)); do \
	    h="$(GFDOCKER_HOSTNAME_PREFIX_CLIENT)$${i}"; \
	    $(CONTSHELL_COMMON) $${h} kdestroy; \
	done

gridftp-setup:
	$(CONTEXEC_GFMD1) $(SCRIPTS)/gridftp-setup-server.sh
	$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/gridftp-setup-client.rc'

gridftp-test:
	$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/gridftp-test.rc'


NEXTCLOUD_GFARM_SRC = $(ROOTDIR)/mnt/work/nextcloud-gfarm
NEXTCLOUD_GFARM_CONF = $(ROOTDIR)/common/nextcloud

COMPOSE_NEXTCLOUD = $(COMPOSE) \
	-f $(COMPOSE_YML) \
	-f $(NEXTCLOUD_GFARM_SRC)/docker-compose.yml \
	-f $(NEXTCLOUD_GFARM_CONF)/docker-compose.nextcloud-gfarm.override.yml

down-with-nextcloud:
	$(COMPOSE_NEXTCLOUD) down --volumes --remove-orphans
	$(down)

nextcloud-backup:
	@if $(DOCKER) ps --filter status=running --filter name=$(GFDOCKER_PRJ_NAME_FULL)_nextcloud_1 | grep -q nextcloud; \
	then $(COMPOSE_NEXTCLOUD) exec -u www-data nextcloud /backup.sh; \
	else echo "skip nextcloud-backup"; \
	fi

nextcloud-rm:
	$(MAKE) nextcloud-backup
	$(COMPOSE_NEXTCLOUD) rm --force --stop -v mariadb nextcloud

nextcloud-rm-data:
	$(MAKE) nextcloud-rm
	$(DOCKER) volume rm --force \
	$(GFDOCKER_PRJ_NAME_FULL)_db \
	$(GFDOCKER_PRJ_NAME_FULL)_log \
	$(GFDOCKER_PRJ_NAME_FULL)_nextcloud

nextcloud-recreate:
	$(MAKE) nextcloud-rm
	$(COMPOSE_NEXTCLOUD) build $(DOCKER_BUILD_FLAGS) nextcloud
	$(COMPOSE_NEXTCLOUD) up -d nextcloud

nextcloud-reborn:
	$(MAKE) nextcloud-rm-data
	$(COMPOSE_NEXTCLOUD) build $(DOCKER_BUILD_FLAGS) nextcloud
	$(COMPOSE_NEXTCLOUD) up -d nextcloud

nextcloud-reborn-nocache:
	$(MAKE) nextcloud-rm-data
	$(COMPOSE_NEXTCLOUD) build $(DOCKER_BUILD_FLAGS) --no-cache nextcloud
	$(COMPOSE_NEXTCLOUD) up -d nextcloud

nextcloud-restart:
	$(MAKE) nextcloud-stop
	$(MAKE) nextcloud-start

nextcloud-start:
	$(COMPOSE_NEXTCLOUD) start mariadb nextcloud

nextcloud-stop:
	$(MAKE) nextcloud-backup
	$(COMPOSE_NEXTCLOUD) stop mariadb nextcloud

nextcloud-ps:
	$(COMPOSE_NEXTCLOUD) ps

nextcloud-logs:
	$(COMPOSE_NEXTCLOUD) logs

nextcloud-logs-less:
	$(COMPOSE_NEXTCLOUD) logs --no-color | less

nextcloud-logs-f:
	$(COMPOSE_NEXTCLOUD) logs --follow

nextcloud-shell:
	$(COMPOSE_NEXTCLOUD) exec -u www-data nextcloud /bin/bash

nextcloud-shell-root:
	$(COMPOSE_NEXTCLOUD) exec nextcloud /bin/bash

nextcloud-mariadb-shell:
	$(COMPOSE_NEXTCLOUD) exec mariadb /bin/bash


define test_fo
$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/test-fo.rc'
endef

test-fo:
	$(check_config)
	$(test_fo)

test-failover: test-fo

define systest
$(CONTSHELL) -c '. ~/gfarm/docker/dev/common/systest.rc'
endef

systest:
	$(check_config)
	$(systest)

systest-all:
	$(check_config)
	@echo 'This target is unimplemented.' 1>&2
	@false

docker-compose:
	$(check_config)
	$(COMPOSE) $$ARGS

test-all:
	$(check_config)
	$(regress)
	$(test_fo)
	$(systest)

valgrind-gfmd:
	$(check_config)
	@echo 'This target is unimplemented.' 1>&2
	@false

centos7:
	$(DOCKER_RUN) -it --rm 'centos:7' bash

centos8:
	$(DOCKER_RUN) -it --rm 'centos:8' bash

rockylinux8:
	$(DOCKER_RUN) -it --rm 'rockylinux/rockylinux:8' bash

almalinux8:
	$(DOCKER_RUN) -it --rm 'almalinux:8' bash

centos8stream:
	$(DOCKER_RUN) -it --rm 'quay.io/centos/centos:stream8' bash

centos9stream:
	$(DOCKER_RUN) -it --rm 'quay.io/centos/centos:stream9' bash

fedora33:
	$(DOCKER_RUN) -it --rm 'fedora:33' bash

opensuse:
	$(DOCKER_RUN) -it --rm 'opensuse/leap' bash

ubuntu1804:
	$(DOCKER_RUN) -it --rm 'ubuntu:18.04' bash

ubuntu2004:
	$(DOCKER_RUN) -it --rm 'ubuntu:20.04' bash

ubuntu2204:
	$(DOCKER_RUN) -it --rm 'ubuntu:22.04' bash

debian10:
	$(DOCKER_RUN) -it --rm 'debian:buster' bash

debian11:
	$(DOCKER_RUN) -it --rm 'debian:bullseye' bash
