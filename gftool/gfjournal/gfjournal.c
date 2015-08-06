/*
 * $Id$
 */

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>	/* more portable than <stdint.h> on UNIX variants */
#include <time.h>
#include <sys/types.h>
#include <sys/param.h>

#include <gfarm/gfarm.h>

#include "internal_host_info.h"	/* for struct gfarm_internal_host_info
				 * in db_ops.h */

#include "nanosec.h"
#include "gfutil.h"

#include "context.h"
#include "config.h"
#include "metadb_common.h"
#include "metadb_server.h"
#include "xattr_info.h"
#include "quota_info.h"
#include "gfm_proto.h"
#include "quota.h"

#include "journal_file.h"
#include "db_common.h"
#include "db_ops.h"
#include "db_access.h"
#include "db_journal.h"

static char *program_name = "gfjournal";
static int opt_verbose = 0, opt_record_only = 0;
static size_t min_reclen = UINT32_MAX, max_reclen, ave_reclen;
static size_t num_rec = 0;
static gfarm_uint64_t min_seqnum = UINT64_MAX;
static gfarm_uint64_t max_seqnum = GFARM_METADB_SERVER_SEQNUM_INVALID;
static struct journal_file *jf;
static struct journal_file_reader *reader;
static off_t file_size;
static off_t file_offset;

/* dummy definition to link successfully without db_journal_apply.o */
const struct db_ops db_journal_apply_ops;
/* dummy definition to link successfully without mdhost.o */
void mdhost_self_is_master(void) {}
void mdhost_self_is_readonly_unlocked(void) {}

static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-dhlmrv?] <journal file path>\n",
		program_name);
	fprintf(stderr,
		"       -l : list records\n"
		"       -m : print the maximum sequence number only\n"
		"       -r : suppress header and summary\n"
		"       -d : set log level to debug\n"
		"       -v : verbose output\n"
		"       -h,-? : show this message\n");
	exit(EXIT_FAILURE);
}

static gfarm_error_t
post_read_aggregate(void *op_arg, gfarm_uint64_t seqnum,
	enum journal_operation ope, void *arg, void *closure, size_t length,
	int *needs_freep)
{
	ave_reclen += length;
	++num_rec;
	if (min_seqnum > seqnum)
		min_seqnum = seqnum;
	if (max_seqnum < seqnum)
		max_seqnum = seqnum;
	if (min_reclen > length)
		min_reclen = length;
	if (max_reclen < length)
		max_reclen = length;

	return (GFARM_ERR_NO_ERROR);
}

static void
print_stringlist(char *msg, char **strs)
{
	char **s;
	int c = 0;

	printf(";%s=(", msg);
	for (s = strs; *s; ++s) {
		if (c)
			printf(",");
		printf("%s", *s);
		c = 1;
	}
	printf(")");
}

struct modflag_info {
	int flag;
	const char *name;
};

static struct modflag_info host_modflag_info[] = {
	{ DB_HOST_MOD_ARCHITECTURE, "ARCHITECTURE" },
	{ DB_HOST_MOD_NCPU, "NCPU" },
	{ 0, NULL },
};

static struct modflag_info user_modflag_info[] = {
	{ DB_USER_MOD_REALNAME, "REALNAME" },
	{ DB_USER_MOD_HOMEDIR, "HOMEDIR" },
	{ DB_USER_MOD_GSI_DN, "GSI_DN" },
	{ 0, NULL },
};

static void
print_modflags(int flags, struct modflag_info *mis)
{
	int o = 0;
	struct modflag_info *mi;

	printf(";modflags=%x", flags);
	if (mis == NULL)
		return;
	printf(" (");
	for (mi = mis; mi->flag; ++mi) {
		if ((mi->flag & flags) != 0) {
			if (o == 0)
				o = 1;
			else
				printf("|");
			printf("%s", mi->name);
		}
	}
	printf(")");
}

static void
print_host(struct gfarm_host_info *hi)
{
	printf("hostname=%s", hi->hostname);
	if (!opt_verbose)
		return;
	printf(";port=%d;architecture=%s;ncpu=%d;flags=0x%x",
	    hi->port, hi->architecture, hi->ncpu, hi->flags);
	if (hi->nhostaliases > 0)
		print_stringlist("aliases", hi->hostaliases);
}

static void
print_user(struct gfarm_user_info *ui)
{
	printf("username=%s", ui->username);
	if (!opt_verbose)
		return;
	printf(";realname=%s;homedir=%s;gsi_dn=%s",
	    ui->realname, ui->homedir, ui->gsi_dn);
}

static void
print_group(struct gfarm_group_info *gi)
{
	printf("groupname=%s", gi->groupname);
	if (!opt_verbose)
		return;
	if (gi->nusers > 0)
		print_stringlist("usernames", gi->usernames);
}

static void
print_mdhost(struct gfarm_metadb_server *ms)
{
	printf("name=%s", ms->name);
	if (!opt_verbose)
		return;
	printf(";port=%d;clustername=%s;flags=0x%x",
	    ms->port, ms->clustername, ms->flags);
}

static void
print_timespec(const char *name, struct gfarm_timespec *t)
{
	struct tm *tm;
	char buf[BUFSIZ];
	time_t tsec;

	tsec = t->tv_sec;
	tm = gmtime(&tsec);
	strftime(buf, BUFSIZ, "%Y%m%d-%H%M%S", tm);
	printf(";%s=%s.%06dGMT",
	    name, buf, (t->tv_nsec / GFARM_MICROSEC_BY_NANOSEC));
}

static void
print_time(const char *name, gfarm_time_t t)
{
	printf(";%s=%" GFARM_PRId64, name, t);
}

static void
print_stat(struct gfs_stat *st)
{
	printf("ino=%" GFARM_PRId64, st->st_ino);
	if (!opt_verbose)
		return;
	printf(
	    ";gen=%" GFARM_PRId64 ";mode=%d"
	    ";nlink=%" GFARM_PRId64 ";size=%" GFARM_PRId64 ";user=%s;group=%s",
	    st->st_gen, st->st_mode, st->st_nlink, st->st_size,
	    st->st_user, st->st_group);
	print_timespec("atime", &st->st_atimespec);
	print_timespec("ctime", &st->st_ctimespec);
	print_timespec("mtime", &st->st_mtimespec);
}

static void
print_quota(struct quota *q)
{
	printf(
	    ";space=%" GFARM_PRId64 ";space_soft=%" GFARM_PRId64
	    ";space_hard=%" GFARM_PRId64 ";num=%" GFARM_PRId64
	    ";num_soft=%" GFARM_PRId64 ";num_hard=%" GFARM_PRId64
	    ";phy_space=%" GFARM_PRId64 ";phy_space_soft=%" GFARM_PRId64
	    ";phy_space_hard=%" GFARM_PRId64 ";phy_num=%" GFARM_PRId64
	    ";phy_num_soft=%" GFARM_PRId64 ";phy_num_hard=%" GFARM_PRId64,
	    q->space, q->space_soft, q->space_hard,
	    q->num, q->num_soft, q->num_hard,
	    q->phy_space, q->phy_space_soft, q->phy_space_hard,
	    q->phy_num, q->phy_num_soft, q->phy_num_hard);

	print_time("grace_period", q->grace_period);
	print_time("space_exceed", q->space_exceed);
	print_time("num_exceed", q->num_exceed);
	print_time("phy_space_exceed", q->phy_space_exceed);
	print_time("phy_num_exceed", q->phy_num_exceed);
}

static void
print_bin_value(const char *name, const char *s, size_t sz)
{
#define ABBREV_LEN 16
	size_t i = 0, n = sz > ABBREV_LEN ? ABBREV_LEN : sz;

	printf(";%s=\"", name);
	for (i = 0; i < n; ++i) {
		char c = (32 <= n && n <= 126) ? n : '.';
		printf("%c", c);
	}
	if (sz > ABBREV_LEN)
		printf("...");
	printf("\"");
}

static void
print_fsngroup_modify(const char *hostname, const char *fsngroupname)
{
	printf("hostname=%s;fsngroupname=%s", hostname, fsngroupname);
}

static void
print_obj(enum journal_operation ope, void *obj)
{
	switch (ope) {
	case GFM_JOURNAL_BEGIN:
	case GFM_JOURNAL_END:
		break;
	case GFM_JOURNAL_HOST_ADD:
		print_host(obj);
		break;
	case GFM_JOURNAL_HOST_MODIFY: {
		struct db_host_modify_arg *m = obj;
		print_host(&m->hi);
		if (opt_verbose) {
			print_modflags(m->modflags, host_modflag_info);
			if (m->add_count > 0)
				print_stringlist("add_aliases",
				    m->add_aliases);
			if (m->del_count > 0)
				print_stringlist("del_aliases",
				    m->del_aliases);
		}
		break;
	}
	case GFM_JOURNAL_USER_ADD:
		print_user(obj);
		break;
	case GFM_JOURNAL_USER_MODIFY: {
		struct db_user_modify_arg *m = obj;
		print_user(&m->ui);
		if (opt_verbose)
			print_modflags(m->modflags, user_modflag_info);
		break;
	}
	case GFM_JOURNAL_GROUP_ADD:
		print_group(obj);
		break;
	case GFM_JOURNAL_GROUP_MODIFY: {
		struct db_group_modify_arg *m = obj;
		print_group(&m->gi);
		if (opt_verbose) {
			print_modflags(m->modflags, NULL);
			if (m->add_count > 0)
				print_stringlist("add_users",
				    m->add_users);
			if (m->del_count > 0)
				print_stringlist("del_users",
				    m->del_users);
		}
		break;
	}
	case GFM_JOURNAL_HOST_REMOVE:
	case GFM_JOURNAL_USER_REMOVE:
	case GFM_JOURNAL_GROUP_REMOVE:
	case GFM_JOURNAL_MDHOST_REMOVE:
		printf("name=%s", (const char *)obj);
		break;
	case GFM_JOURNAL_INODE_ADD:
	case GFM_JOURNAL_INODE_MODIFY:
		print_stat(obj);
		break;
	case GFM_JOURNAL_INODE_GEN_MODIFY:
	case GFM_JOURNAL_INODE_NLINK_MODIFY:
	case GFM_JOURNAL_INODE_SIZE_MODIFY: {
		struct db_inode_uint64_modify_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			printf(";uint64=%" GFARM_PRId64 "", m->uint64);
		break;
	}
	case GFM_JOURNAL_INODE_MODE_MODIFY: {
		struct db_inode_uint32_modify_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			printf(";uint32=%d", m->uint32);
		break;
	}
	case GFM_JOURNAL_INODE_USER_MODIFY:
	case GFM_JOURNAL_INODE_GROUP_MODIFY: {
		struct db_inode_string_modify_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			printf(";string=%s", m->string);
		break;
	}
	case GFM_JOURNAL_INODE_ATIME_MODIFY:
	case GFM_JOURNAL_INODE_MTIME_MODIFY:
	case GFM_JOURNAL_INODE_CTIME_MODIFY: {
		struct db_inode_timespec_modify_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			print_timespec("time", &m->time);
		break;
	}
	case GFM_JOURNAL_INODE_CKSUM_ADD:
	case GFM_JOURNAL_INODE_CKSUM_MODIFY: {
		struct db_inode_cksum_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			printf(";type=%s;len=%lu", m->type,
			    (unsigned long)m->len);
		break;
	}
	case GFM_JOURNAL_INODE_CKSUM_REMOVE:
	case GFM_JOURNAL_SYMLINK_REMOVE: {
		struct db_inode_inum_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		break;
	}
	case GFM_JOURNAL_FILECOPY_ADD:
	case GFM_JOURNAL_FILECOPY_REMOVE: {
		struct db_filecopy_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			printf(";hostname=%s", m->hostname);
		break;
	}
	case GFM_JOURNAL_DEADFILECOPY_ADD:
	case GFM_JOURNAL_DEADFILECOPY_REMOVE: {
		struct db_deadfilecopy_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			printf(";igen=%" GFARM_PRId64 ";hostname=%s",
			    m->igen, m->hostname);
		break;
	}
	case GFM_JOURNAL_DIRENTRY_ADD:
	case GFM_JOURNAL_DIRENTRY_REMOVE: {
		struct db_direntry_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->dir_inum);
		if (opt_verbose)
			printf(";entry_ino=%" GFARM_PRId64
			    ";entry_name=%s;entry_len=%d",
			    m->entry_inum, m->entry_name, m->entry_len);
		break;
	}
	case GFM_JOURNAL_SYMLINK_ADD: {
		struct db_symlink_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose)
			printf(";source_path=%s", m->source_path);
		break;
	}
	case GFM_JOURNAL_XATTR_ADD:
	case GFM_JOURNAL_XATTR_MODIFY:
	case GFM_JOURNAL_XATTR_REMOVE:
	case GFM_JOURNAL_XATTR_REMOVEALL: {
		struct db_xattr_arg *m = obj;
		printf("ino=%" GFARM_PRId64, m->inum);
		if (opt_verbose) {
			printf(";xml_mode=%d;attrname=%s;size=%lu",
			    m->xmlMode, m->attrname, (unsigned long)m->size);
			print_bin_value("value", m->value, m->size);
		}
		break;
	}
	case GFM_JOURNAL_QUOTA_ADD:
	case GFM_JOURNAL_QUOTA_MODIFY: {
		struct db_quota_arg *m = obj;
		printf("name=%s;is_group=%d", m->name, m->is_group);
		if (opt_verbose)
			print_quota(&m->quota);
		break;
	}
	case GFM_JOURNAL_QUOTA_REMOVE: {
		struct db_quota_remove_arg *m = obj;
		printf("name=%s;is_group=%d", m->name, m->is_group);
		break;
	}
	case GFM_JOURNAL_MDHOST_ADD:
		print_mdhost(obj);
		break;
	case GFM_JOURNAL_MDHOST_MODIFY: {
		struct db_mdhost_modify_arg *m = obj;
		print_mdhost(&m->ms);
		break;
	}
	case GFM_JOURNAL_FSNGROUP_MODIFY: {
		struct db_fsngroup_modify_arg *m = obj;
		print_fsngroup_modify(m->hostname, m->fsngroupname);
		break;
	}
	default:
		break;
	}
}

static gfarm_error_t
post_read_list(void *op_arg, gfarm_uint64_t seqnum, enum journal_operation ope,
	void *obj, void *closure, size_t length, int *needs_freep)
{
	gfarm_error_t e = GFARM_ERR_NO_ERROR;

	post_read_aggregate(NULL, seqnum, ope, obj, NULL, length, needs_freep);
	/* seqnum operation length */
	printf("%12" GFARM_PRId64 " %-22s %7lu ", seqnum,
	    journal_operation_name(ope), (unsigned long)length);
	if (opt_verbose) {
		/* offset */
		if (file_offset + length > file_size)
			file_offset = JOURNAL_FILE_HEADER_SIZE;
		printf("%10" GFARM_PRId64 " ",  (gfarm_off_t)file_offset);
		file_offset += length;
	}
	print_obj(ope, obj);
	printf("\n");

	return (e);
}

int
main(int argc, char **argv)
{
	gfarm_error_t e;
	int exit_code = EXIT_SUCCESS;
	int opt_list = 0;
	int opt_max_seqnum_only = 0;
	int c, eof;
	const char *path;
	journal_post_read_op_t post_read = post_read_aggregate;

	if (argc > 0)
		program_name = basename(argv[0]);
	gflog_initialize();
	gfarm_context_init();

	while ((c = getopt(argc, argv, "dhlmrv?")) != -1) {
		switch (c) {
		case 'd':
			gflog_set_message_verbose(2);
			gflog_set_priority_level(LOG_DEBUG);
			break;
		case 'l':
			opt_list = 1;
			post_read = post_read_list;
			break;
		case 'm':
			opt_max_seqnum_only = 1;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case 'r':
			opt_record_only = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();
	path = argv[0];

	if ((e = journal_file_open(path, 0, GFARM_METADB_SERVER_SEQNUM_INVALID,
	    &jf, GFARM_JOURNAL_RDONLY)) != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s : %s\n", program_name,
		    gfarm_error_string(e));
		exit_code = EXIT_FAILURE;
		goto end;
	}
	if (opt_max_seqnum_only) {
		max_seqnum = journal_file_get_inital_max_seqnum(jf);
		printf("%" GFARM_PRId64 "\n", max_seqnum);
		goto end;
	}

	reader = journal_file_main_reader(jf);
	file_size = journal_file_size(jf);
	file_offset = journal_file_reader_fd_pos(reader);

	if (opt_list && !opt_record_only) {
		printf(
		    "seqnum       operation              "
		    "length  ");
		if (opt_verbose)
			printf("offset     ");
		printf("argument\n");
	}

	while ((e = db_journal_read(reader, reader, post_read, NULL, &eof))
		== GFARM_ERR_NO_ERROR && eof == 0)
		;
	ave_reclen = num_rec > 0 ? ave_reclen / num_rec : 0;
	if (num_rec == 0)
		min_seqnum = 0;
	else if (!opt_record_only) {
		if (opt_list)
			printf("\n");
		printf("records  seqnum(min/max)            "
		    "record length(min/max/ave)\n"
		    "%7lu  %12" GFARM_PRId64 "/%12" GFARM_PRId64
		    "     %7lu/%7lu/%7lu\n",
		     (unsigned long)num_rec, min_seqnum, max_seqnum,
		     (unsigned long)min_reclen, (unsigned long)max_reclen,
		     (unsigned long)ave_reclen);
	}
	if (e != GFARM_ERR_NO_ERROR) {
		fprintf(stderr, "%s : %s\n", program_name,
		    gfarm_error_string(e));
		exit_code = EXIT_FAILURE;
	}
end:
	journal_file_close(jf);
	gfarm_context_term();
	gflog_terminate();
	return (exit_code);
}
