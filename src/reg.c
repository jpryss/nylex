/*
 * reg.c - document names and the document index (manual chapter 14):
 * one record per document, key ':' + 6 characters of the name + 3 of
 * the extension, 220 characters of fixed fields as LEX-68 writes them:
 *
 *	1	file name 40, title 40, detail 40
 *	121	created 7 (15okt26), edited 7, times 2, accessed 7, times 2,
 *		printed 7, times 2
 *	155	last session hhmmss 6, total time hhmmss 6, lines 6,
 *		words 8, characters 8
 *	189	created, edited, accessed, printed as yymmdd, 6 each
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef USE_TERMIO
#include <dirent.h>
#endif
#include "lex.h"

#define RECLEN	220

/* PROV -> PROV.BRV, in the case **INST asks for; a "/" "*.EXT" suffix is left on */
void
docname_norm(char *name, const char *defext)
{
	char *p, *dot, *slash, suffix[16];

	while (*name == ' ')
		memmove(name, name + 1, strlen(name));
	for (p = name; *p; p++)
		if (I.fcase == 'U')
			*p = (char)upcase((unsigned char)*p);
		else if (I.fcase == 'L')
			*p = (char)lowcase((unsigned char)*p);
	while (p > name && p[-1] == ' ')
		*--p = 0;
	slash = strstr(name, "/*");
	suffix[0] = 0;
	if (slash) {
		strncpy(suffix, slash, 15);
		suffix[15] = 0;
		*slash = 0;
	}
	dot = strrchr(name, '.');
	if ((dot == NULL || strchr(dot, '/')) && defext && strlen(name) + strlen(defext) < MAXNAME)
		strcat(name, defext[0] == '.' ? defext : ".");
	if (suffix[0] && strlen(name) + strlen(suffix) < MAXNAME)
		strcat(name, suffix);
}

/* the file of a document: lower case (UNIX), "X." is "x"; no suffix */
void
docname_file(char *path, const char *name)
{
	const char *s;
	char *p = path;

	for (s = name; *s && !(s[0] == '/' && s[1] == '*'); s++)
		*p++ = (char)(I.fcase == 'M' ? *s : lowcase((unsigned char)*s));
	*p = 0;
	if (p > path && p[-1] == '.')
		p[-1] = 0;
#ifndef USE_TERMIO
	/* not there in lower case: a file of that name in other capitals
	   (README.amiga for README.AMIGA), as LEX's systems did not have */
	if (!fexists(path)) {
		char dir[300], *base = strrchr(path, '/');
		DIR *d;
		struct dirent *e;

		if (base) {
			memcpy(dir, path, base - path);
			dir[base - path] = 0;
			base++;
		} else {
			strcpy(dir, ".");
			base = path;
		}
		if ((d = opendir(dir[0] ? dir : "/")) != NULL) {
			while ((e = readdir(d)) != NULL) {
				const char *a = e->d_name, *b = base;

				while (*a && *b && lowcase((unsigned char)*a) == lowcase((unsigned char)*b))
					a++, b++;
				if (!*a && !*b) {
					strcpy(base, e->d_name);
					break;
				}
			}
			closedir(d);
		}
	}
#endif
}

/* the extension of a name (".BRV"), or of a "/" "*.EXT" after it */
void
docname_ext(const char *name, char *ext)
{
	const char *f = strstr(name, "/*"), *dot;

	if (f && f[2] == '.') {
		strncpy(ext, f + 2, 7);
		ext[7] = 0;
		return;
	}
	dot = strrchr(name, '.');
	if (dot && (f == NULL || dot < f)) {
		int n = f ? (int)(f - dot) : (int)strlen(dot);

		if (n > 4)
			n = 4;
		memcpy(ext, dot, n);
		ext[n] = 0;
		{
			char *p;

			for (p = ext; *p; p++)
				*p = (char)upcase((unsigned char)*p);
		}
		return;
	}
	strcpy(ext, ".");
}

void
reg_key(char *key, const char *name)
{
	const char *dot, *base = strrchr(name, '/');
	int i, n, t;

	base = base && base[1] != '*' ? base + 1 : name;
	dot = strrchr(base, '.');
	n = dot ? (int)(dot - base) : (int)strlen(base);
	t = dot ? (int)strlen(dot + 1) : 0;
	key[0] = ':';
	for (i = 0; i < 6; i++)
		key[1 + i] = (char)(i < n ? upcase((unsigned char)base[i]) : ' ');
	for (i = 0; i < 3; i++)
		key[7 + i] = (char)(i < t && dot[1 + i] != '/' ? upcase((unsigned char)dot[1 + i]) : ' ');
	key[10] = 0;
}

static void
field(char *dst, const char *src, int off, int len, long rlen)
{
	int i;

	for (i = 0; i < len && off + i < rlen; i++)
		dst[i] = src[off + i] == '\n' ? ' ' : src[off + i];
	dst[i] = 0;
	while (i > 0 && dst[i - 1] == ' ')
		dst[--i] = 0;
}

static long
hms(const char *b)
{
	long v = atol(b);

	return (v / 10000) * 3600 + (v / 100 % 100) * 60 + v % 100;
}

int
reg_get(const char *name, struct regent *r)
{
	char key[12], b[12], *d;
	long len;

	memset(r, 0, sizeof *r);
	reg_key(key, name);
	if (rec_text(key, &d, &len, '_') < 0)
		return -1;
	field(r->name, d, 1, 40, len);
	field(r->title, d, 41, 40, len);
	field(r->detail, d, 81, 40, len);
	field(r->created, d, 121, 7, len);
	field(r->edited, d, 128, 7, len);
	field(b, d, 135, 2, len); r->nedit = b[0] ? atoi(b) : -1;
	field(r->accessed, d, 137, 7, len);
	field(b, d, 144, 2, len); r->nacc = b[0] ? atoi(b) : -1;
	field(r->printed, d, 146, 7, len);
	field(b, d, 153, 2, len); r->nprint = b[0] ? atoi(b) : -1;
	field(r->last, d, 155, 6, len);
	field(b, d, 161, 6, len); r->tottime = b[0] ? hms(b) : -1;
	field(b, d, 167, 6, len); r->lines = b[0] ? atol(b) : -1;
	field(b, d, 173, 8, len); r->words = b[0] ? atol(b) : -1;
	field(b, d, 181, 8, len); r->chars = b[0] ? atol(b) : -1;
	field(r->dcreated, d, 189, 6, len);
	field(r->dedited, d, 195, 6, len);
	field(r->daccessed, d, 201, 6, len);
	field(r->dprinted, d, 207, 6, len);
	if (r->name[0] == 0)
		strncpy(r->name, name, 40);
	xfree(d);
	return 0;
}

static void
put(char *rec, int off, int len, const char *s)
{
	int n = (int)strlen(s);

	if (n > len)
		n = len;
	memcpy(rec + off, s, n);
}

static void
putn(char *rec, int off, int len, long v)
{
	char b[24];

	if (v < 0)
		return;
	sprintf(b, "%0*ld", len, v);
	put(rec, off, len, b + ((int)strlen(b) > len ? (int)strlen(b) - len : 0));
}

int
reg_put(const struct regent *r)
{
	char rec[RECLEN + 1], key[12];
	unsigned char *sq;
	long n;
	int ret;

	if (!I.titling)
		return 0;
	memset(rec, ' ', RECLEN);
	put(rec, 1, 40, r->name);
	put(rec, 41, 40, r->title);
	put(rec, 81, 40, r->detail);
	put(rec, 121, 7, r->created);
	put(rec, 128, 7, r->edited);
	putn(rec, 135, 2, r->nedit);
	put(rec, 137, 7, r->accessed);
	putn(rec, 144, 2, r->nacc);
	put(rec, 146, 7, r->printed);
	putn(rec, 153, 2, r->nprint);
	put(rec, 155, 6, r->last);
	if (r->tottime >= 0)
		putn(rec, 161, 6, (r->tottime / 3600 % 100) * 10000 + (r->tottime / 60 % 60) * 100 + r->tottime % 60);
	if (I.counts) {
		putn(rec, 167, 6, r->lines);
		putn(rec, 173, 8, r->words);
		putn(rec, 181, 8, r->chars);
	}
	put(rec, 189, 6, r->dcreated);
	put(rec, 195, 6, r->dedited);
	put(rec, 201, 6, r->daccessed);
	put(rec, 207, 6, r->dprinted);
	reg_key(key, r->name);
	squash(rec, RECLEN, &sq, &n, '_');
	ret = rec_put(key, sq, n);
	xfree(sq);
	return ret;
}

int
reg_delete(const char *name)
{
	char key[12], k[KEYLEN];

	reg_key(key, name);
	mkkey(k, key);
	if (sysvmf == NULL)
		return -1;
	return vmf_delete(sysvmf, k);
}

void
reg_new(struct regent *r, const char *name)
{
	memset(r, 0, sizeof *r);
	r->nacc = r->nedit = r->nprint = -1;
	r->tottime = r->lines = r->words = r->chars = -1;
	strncpy(r->name, name, 40);
	date_ddmmmyy(r->created);
	date_yymmdd(r->dcreated);
}

/* what was done: 'E' edited, 'A' accessed (viewed, copied), 'P' printed */
void
reg_touch(struct regent *r, int what, long secs)
{
	char now[8], d6[8];

	date_ddmmmyy(now);
	date_yymmdd(d6);
	strcpy(r->accessed, now);
	strcpy(r->daccessed, d6);
	r->nacc = r->nacc < 0 ? 1 : (r->nacc + 1) % 100;
	if (what == 'E') {
		strcpy(r->edited, now);
		strcpy(r->dedited, d6);
		r->nedit = r->nedit < 0 ? 1 : (r->nedit + 1) % 100;
		r->tottime = (r->tottime < 0 ? 0 : r->tottime) + secs;
	} else if (what == 'P') {
		strcpy(r->printed, now);
		strcpy(r->dprinted, d6);
		r->nprint = r->nprint < 0 ? 1 : (r->nprint + 1) % 100;
	}
	time_hms(r->last, 0);
}
