/*
 * $Id$
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <gfarm/gfarm.h>
#include "metadb_access.h"

enum {
	PATH_INFO,
	SECTION_INFO,
	SECTION_COPY_INFO
};

static int verbose = 0;

static char *
print_file_section_copy_info(struct gfarm_file_section_copy_info *info, FILE *f)
{
	fprintf(f, "C:");

	fprintf(f, " %s", info->pathname);
	fprintf(f, " %s", info->section);
	fprintf(f, " %s", info->hostname);
	fputc('\n', f);
	return (NULL);
}

static char *
print_file_section_info(struct gfarm_file_section_info *info, FILE *f)
{
	fprintf(f, "S:");

	fprintf(f, " %s", info->pathname);
	fprintf(f, " %s", info->section);
	fprintf(f, " %" PR_FILE_OFFSET, info->filesize);
	fprintf(f, " %s %s", info->checksum_type, info->checksum);
	fputc('\n', f);
	return (NULL);
}

static char *
print_path_info(struct gfarm_path_info *info, FILE *f)
{
	struct gfs_stat *st = &info->status;

	fprintf(f, "P:");

	fprintf(f, " %s", info->pathname);
	fprintf(f, " %d", st->st_mode);
	fprintf(f, " %s %s", st->st_user, st->st_group);
	fprintf(f, " %d %d", st->st_atimespec.tv_sec, st->st_atimespec.tv_nsec);
	fprintf(f, " %d %d", st->st_mtimespec.tv_sec, st->st_mtimespec.tv_nsec);
	fprintf(f, " %d %d", st->st_ctimespec.tv_sec, st->st_ctimespec.tv_nsec);
	fprintf(f, " %d", st->st_nsections);
	fputc('\n', f);
	return (NULL);
}

static void
dump_int32(gfarm_int32_t i, FILE *f)
{
	gfarm_int32_t ii;

	ii = htonl(i);
	fwrite(&ii, sizeof(gfarm_int32_t), 1, f);
}

static void
dump_timespec(struct gfarm_timespec *t, FILE *f)
{
	struct gfarm_timespec tt;

	tt.tv_sec = htonl(t->tv_sec);
	tt.tv_nsec = htonl(t->tv_nsec);
	fwrite(&tt, sizeof(struct gfarm_timespec), 1, f);
}

static void
dump_string(char *string, FILE *f)
{
	int len = strlen(string) + 1; /* includes '\0' */

	dump_int32(len, f);
	fwrite(string, sizeof(char), len, f);
}

#if FILE_OFFSET_T_IS_FLOAT
#include <math.h>

#define POWER2_32	4294967296.0		/* 2^32 */
#endif

static void
dump_offset_t(file_offset_t o, FILE *f)
{
	gfarm_uint32_t ov[2];
#if FILE_OFFSET_T_IS_FLOAT
	int minus;
#endif

#if FILE_OFFSET_T_IS_FLOAT
	minus = o < 0;
	if (minus)
		o = -o;
	ov[0] = o / POWER2_32;
	ov[1] = o - ov[0] * POWER2_32;
	if (minus) {
		ov[0] = ~ov[0];
		ov[1] = ~ov[1];
		if (++ov[1] == 0)
			++ov[0];
	}
#else
	ov[0] = o >> 32;
	ov[1] = o;
#endif
	ov[0] = htonl(ov[0]);
	ov[1] = htonl(ov[1]);
	fwrite(ov, sizeof(ov), 1, f);
}

static char *
dump_file_section_copy_info(struct gfarm_file_section_copy_info *info, FILE *f)
{
	if (verbose)
		print_file_section_copy_info(info, stderr);

	fputc(SECTION_COPY_INFO, f);

	dump_string(info->pathname, f);
	dump_string(info->section, f);
	dump_string(info->hostname, f);
	return (NULL);
}

static char *
dump_file_section_info(struct gfarm_file_section_info *info, FILE *f)
{
	if (verbose)
		print_file_section_info(info, stderr);

	fputc(SECTION_INFO, f);

	dump_string(info->pathname, f);
	dump_string(info->section, f);
	dump_offset_t(info->filesize, f);
	dump_string(info->checksum_type, f);
	dump_string(info->checksum, f);
	return (NULL);
}

static char *
dump_path_info(struct gfarm_path_info *info, FILE *f)
{
	struct gfs_stat *st = &info->status;

	if (verbose)
		print_path_info(info, stderr);

	fputc(PATH_INFO, f);

	dump_string(info->pathname, f);
	dump_int32(st->st_mode, f);
	dump_string(st->st_user, f);
	dump_string(st->st_group, f);
	dump_timespec(&st->st_atimespec, f);
	dump_timespec(&st->st_mtimespec, f);
	dump_timespec(&st->st_ctimespec, f);
	dump_int32(st->st_nsections, f);
	return (NULL);
}

static char *
restore_path_info(struct gfarm_path_info *info, FILE *f)
{
	if (verbose)
		print_path_info(info, stdout);

	return (gfarm_metadb_path_info_set(info->pathname, info));
}

static char *
restore_file_section_info(struct gfarm_file_section_info *info, FILE *f)
{
	if (verbose)
		print_file_section_info(info, stdout);

	return (gfarm_metadb_file_section_info_set(
			info->pathname, info->section, info));
}

static char *
restore_file_section_copy_info(
	struct gfarm_file_section_copy_info *info, FILE *f)
{
	if (verbose)
		print_file_section_copy_info(info, stdout);

	return (gfarm_metadb_file_section_copy_info_set(
			info->pathname, info->section, info->hostname, info));
}

struct gfdump_ops {
	char *(*path_info)(struct gfarm_path_info *, FILE *);
	char *(*file_section_info)(struct gfarm_file_section_info *, FILE *);
	char *(*file_section_copy_info)(
		struct gfarm_file_section_copy_info *, FILE *);
};
struct gfdump_args {
	FILE *f;
	gfarm_stringlist *listp;
	struct gfdump_ops *ops;
};

struct gfdump_ops print_ops = {
	print_path_info,
	print_file_section_info,
	print_file_section_copy_info
};
struct gfdump_ops dump_ops = {
	dump_path_info,
	dump_file_section_info,
	dump_file_section_copy_info
};
struct gfdump_ops restore_ops = {
	restore_path_info,
	restore_file_section_info,
	restore_file_section_copy_info
};

void
gfdump_callback(void *arg, struct gfarm_path_info *info)
{
	struct gfdump_args *a = arg;

	a->ops->path_info(info, a->f);
	gfarm_stringlist_add(a->listp, strdup(info->pathname));
	return;
}

static void
dump_section_info_all(struct gfdump_args *a)
{
	char *e, *pathname;
	int i, j, k, nsections, ncopies;
	struct gfarm_file_section_info *sections;
	struct gfarm_file_section_copy_info *copies;
	FILE *f = a->f;

	for (i = 0; i < gfarm_stringlist_length(a->listp); ++i) {
		pathname = gfarm_stringlist_elem(a->listp, i);
		e = gfarm_file_section_info_get_all_by_file(
			pathname, &nsections, &sections);
		if (e != NULL)
			continue;

		for (j = 0; j < nsections; ++j) {
			a->ops->file_section_info(&sections[j], f);

			e = gfarm_file_section_copy_info_get_all_by_section(
				pathname, sections[j].section,
				&ncopies, &copies);
			if (e != NULL)
				continue;

			for (k = 0; k < ncopies; ++k)
				a->ops->file_section_copy_info(&copies[k], f);

			gfarm_file_section_copy_info_free_all(ncopies, copies);
		}
		gfarm_file_section_info_free_all(nsections, sections);
	}
}

static char *
dump_all(char *filename, struct gfdump_ops *ops)
{
	struct gfdump_args a;
	gfarm_stringlist list;
	FILE *f;
	char *e;

	if (filename == NULL)
		f = stdout;
	else
		f = fopen(filename, "w");
	if (f == NULL) {
		perror(filename);
		exit(1);
	}
	a.f = f;
	e = gfarm_stringlist_init(&list);
	a.listp = &list;
	a.ops = ops;
	/* dump path info */
	e = gfarm_metadb_path_info_get_all_foreach(gfdump_callback, &a);
	if (e != NULL)
		goto fclose_f;
	/* dump section info and section copy info */
	dump_section_info_all(&a);
	gfarm_stringlist_free_deeply(&list);
fclose_f:
	if (f != stdin)
		fclose(f);
	return (e);
}

static char *
read_int32(gfarm_int32_t *i, FILE *f)
{
	if (fread(i, sizeof(gfarm_int32_t), 1, f) != 1)
		return (GFARM_ERR_INPUT_OUTPUT);
	*i = ntohl(*i);
	return (NULL);
}

static char *
read_timespec(struct gfarm_timespec *t, FILE *f)
{
	if (fread(t, sizeof(struct gfarm_timespec), 1, f) != 1)
		return (GFARM_ERR_INPUT_OUTPUT);
	t->tv_sec = ntohl(t->tv_sec);
	t->tv_nsec = ntohl(t->tv_nsec);
	return (NULL);
}

static char *
read_string(char **string, FILE *f)
{
	int len;
	char *e, *s;

	e = read_int32(&len, f);
	if (e != NULL)
		return (e);
	GFARM_MALLOC_ARRAY(s, len);
	if (s == NULL)
		return (GFARM_ERR_NO_MEMORY);
	e = fread(s, sizeof(char), len, f) == len
		? NULL : GFARM_ERR_INPUT_OUTPUT;
	if (e == NULL) {
		if (*string != NULL)
			free(*string);
		*string = s;
	}
	return (e);
}

static char *
read_offset_t(file_offset_t *op, FILE *f)
{
	gfarm_uint32_t ov[2];
#if FILE_OFFSET_T_IS_FLOAT
	int minus;
#endif
	char *e;

	e = fread(ov, sizeof(ov), 1, f) == 1
		? NULL : GFARM_ERR_INPUT_OUTPUT;
	if (e != NULL)
		return (e);
	ov[0] = ntohl(ov[0]);
	ov[1] = ntohl(ov[1]);
#if FILE_OFFSET_T_IS_FLOAT
	minus = ov[0] & 0x80000000;
	if (minus) {
		ov[0] = ~ov[0];
		ov[1] = ~ov[1];
		if (++ov[1] == 0)
			++ov[0];
	}
	*op = ov[0] * POWER2_32 + ov[1];
	if (minus)
		*op = -*op;
#else
	*op = ((file_offset_t)ov[0] << 32) | ov[1];
#endif
	return (e);
}

static char *
read_file_section_copy_info(struct gfdump_ops *ops, FILE *f)
{
	static struct gfarm_file_section_copy_info info;
	char *e;

	e = read_string(&info.pathname, f);
	if (e != NULL)
		return (e);
	e = read_string(&info.section, f);
	if (e != NULL)
		return (e);
	e = read_string(&info.hostname, f);
	if (e != NULL)
		return (e);

	return (ops->file_section_copy_info(&info, stdout));
}

static char *
read_file_section_info(struct gfdump_ops *ops, FILE *f)
{
	static struct gfarm_file_section_info section_info;
	char *e;

	e = read_string(&section_info.pathname, f);
	if (e != NULL)
		return (e);
	e = read_string(&section_info.section, f);
	if (e != NULL)
		return (e);
	e = read_offset_t(&section_info.filesize, f);
	if (e != NULL)
		return (e);
	e = read_string(&section_info.checksum_type, f);
	if (e != NULL)
		return (e);
	e = read_string(&section_info.checksum, f);
	if (e != NULL)
		return (e);

	return (ops->file_section_info(&section_info, stdout));
}

static char *
read_path_info(struct gfdump_ops *ops, FILE *f)
{
	static struct gfarm_path_info path_info;
	struct gfs_stat *st = &path_info.status;
	gfarm_int32_t i32;
	char *e;

	e = read_string(&path_info.pathname, f);
	if (e != NULL)
		return (e);
	e = read_int32(&i32, f);
	if (e != NULL)
		return (e);
	st->st_mode = i32;
	e = read_string(&st->st_user, f);
	if (e != NULL)
		return (e);
	e = read_string(&st->st_group, f);
	if (e != NULL)
		return (e);
	e = read_timespec(&st->st_atimespec, f);
	if (e != NULL)
		return (e);
	e = read_timespec(&st->st_mtimespec, f);
	if (e != NULL)
		return (e);
	e = read_timespec(&st->st_ctimespec, f);
	if (e != NULL)
		return (e);
	e = read_int32(&st->st_nsections, f);
	if (e != NULL)
		return (e);

	return (ops->path_info(&path_info, stdout));
}

static char *
restore_all(const char *filename, struct gfdump_ops *ops)
{
	FILE *f;
	int type;
	char *e = NULL;

	if (filename == NULL)
		f = stdin;
	else
		f = fopen(filename, "r");

	if (f == NULL) {
		perror(filename);
		exit(1);
	}

	while ((type = fgetc(f)) != EOF) {
		switch (type) {
		case PATH_INFO:
			e = read_path_info(ops, f);
			break;
		case SECTION_INFO:
			e = read_file_section_info(ops, f);
			break;
		case SECTION_COPY_INFO:
			e = read_file_section_copy_info(ops, f);
			break;
		default:
			e = "unknown type";
			break;
		}
		if (e != NULL)
			break;
	}
	if (f != stdin)
		fclose(f);
	return (e);
}

static void
usage(void)
{
	fprintf(stderr, "usage: gfdump -d [ -f filename ] [-v]\n");
	fprintf(stderr, "       gfdump -r [ -f filename ] [-v]\n");
	fflush(stderr);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *e, c, *filename = NULL;
	static enum { unknown, dump_mode, restore_mode } mode;
	struct gfdump_ops *ops = NULL;

	while ((c = getopt(argc, argv, "df:rtv")) != -1) {
		switch (c) {
		case 'd':
			mode = dump_mode;
			ops = &dump_ops;
			break;
		case 'f':
			filename = optarg;
			break;
		case 'r':
			mode = restore_mode;
			ops = &restore_ops;
			break;
		case 't':
			ops = &print_ops;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
	if (mode == unknown || ops == NULL)
		usage();

	e = gfarm_initialize(NULL, NULL);
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", argv[0], e);
		exit(1);
	}

	switch (mode) {
	case dump_mode:
		e = dump_all(filename, ops);
		break;
	case restore_mode:
		if (verbose)
			setvbuf(stdout, NULL, _IOLBF, 0);
		e = restore_all(filename, ops);
		break;
	default:
		usage();
	}
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", argv[0], e);
		exit(1);
	}
	e = gfarm_terminate();
	if (e != NULL) {
		fprintf(stderr, "%s: %s\n", argv[0], e);
		exit(1);
	}
	return (0);
}
