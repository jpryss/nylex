/*
 * nylex - a word processor in the style of LEX (Ace Microsystems Ltd,
 * written in Filetab-D; sold by DIAB as LEX-68 for its D-NIX machines),
 * written from scratch for the D-NIX Amiga port after LEX's technical
 * manual.  It reads and writes LEX's documents and its system files
 * (.VMF), LEX-68's own lex9a1.vmf among them.  It builds with DIAB's C
 * compiler and with any C89 compiler on Linux, BSD or macOS.
 *
 * usage: nylex [-f system.vmf] [-u terminal] [-w workfile]
 *		[-c swe7|latin1|utf8] [document]
 * and LEX's own way: nylex /Fsystem.vmf /Uterminal /Wworkfile //typed ahead//
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "lex.h"

int		demo;

#ifndef LEXLIB
#ifdef USE_TERMIO
#define LEXLIB	"/usr/modern/lib/nylex"		/* the D-NIX Amiga port */
#else
#define LEXLIB	"/usr/local/share/nylex"
#endif
#endif

void
fatal(const char *msg)
{
	t_close();
	fprintf(stderr, "nylex: %s\n", msg);
	exit(1);
}

char *
xstrdup(const char *s)
{
	char *p = (char *)xrealloc(NULL, strlen(s) + 1);

	strcpy(p, s);
	return p;
}

int
fexists(const char *path)
{
	FILE *f = fopen(path, "r");

	if (f == NULL)
		return 0;
	fclose(f);
	return 1;
}

int
copyfile(const char *from, const char *to)
{
	FILE *f = fopen(from, "rb"), *g;
	int c;

	if (f == NULL)
		return -1;
	if ((g = fopen(to, "wb")) == NULL) {
		fclose(f);
		return -1;
	}
	while ((c = getc(f)) != EOF)
		putc(c, g);
	fclose(f);
	return fclose(g);
}

/* Swedish when the locale is (sv_SE, sv_FI, ..._SE); without a locale,
   English, but on D-NIX (the Swedish D-NIX Amiga port) Swedish */
static int
swedish(void)
{
	const char *l = getenv("LC_ALL");

	if (l == NULL || !*l)
		l = getenv("LC_MESSAGES");
	if (l == NULL || !*l)
		l = getenv("LANG");
	if (l == NULL || !*l || strcmp(l, "C") == 0 || strcmp(l, "POSIX") == 0 || strncmp(l, "C.", 2) == 0)
#ifdef USE_TERMIO
		return 1;
#else
		return 0;
#endif
	return (l[0] == 's' && l[1] == 'v') || strstr(l, "_SE") != NULL;
}

static void
usage(void)
{
	fprintf(stderr, "usage: nylex [-f system.vmf] [-u terminal] [-w workfile] [-c swe7|latin1|utf8] [document]\n");
	exit(2);
}

static void
onsig(int s)
{
	(void)s;
	work_save();
	t_close();
	exit(1);
}

int
main(int argc, char **argv)
{
	char *doc = NULL, *cs = NULL, *home, *vmfpath = NULL, *term = NULL, *work = NULL;
	char ahead[512], buf[512];
	const char *lang;
	int i;

	ahead[0] = 0;
	for (i = 1; i < argc; i++) {
		char *a = argv[i];

		if (strcmp(a, "-f") == 0 && i + 1 < argc)
			vmfpath = argv[++i];
		else if (strcmp(a, "-u") == 0 && i + 1 < argc)
			term = argv[++i];
		else if (strcmp(a, "-w") == 0 && i + 1 < argc)
			work = argv[++i];
		else if (strcmp(a, "-c") == 0 && i + 1 < argc)
			cs = argv[++i];
		else if (strcmp(a, "-V") == 0) {
			printf("nylex %s\n", LEX_VERSION);
			return 0;
		} else if (a[0] == '/' && a[1] == '/') {
			/* typed ahead, to the closing // (the words may be split) */
			char *e;

			strcat(ahead, a + 2);
			while ((e = strstr(ahead, "//")) == NULL && i + 1 < argc && strlen(ahead) + strlen(argv[i + 1]) < sizeof ahead - 2) {
				strcat(ahead, " ");
				strcat(ahead, argv[++i]);
			}
			if ((e = strstr(ahead, "//")) != NULL)
				*e = 0;
		} else if (a[0] == '/' && (a[1] == 'F' || a[1] == 'f') && a[2])
			vmfpath = a + 2;
		else if (a[0] == '/' && (a[1] == 'U' || a[1] == 'u') && a[2])
			term = a + 2;
		else if (a[0] == '/' && (a[1] == 'W' || a[1] == 'w') && a[2])
			work = a + 2;
		else if (a[0] == '-')
			usage();
		else
			doc = a;
	}
	lang = getenv("LC_ALL");
	if (lang == NULL || !*lang)
		lang = getenv("LC_CTYPE");
	if (lang == NULL || !*lang)
		lang = getenv("LANG");
	t_charset = CS_LATIN1;
	if (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf8") || strstr(lang, "UTF8") || strstr(lang, "utf-8")))
		t_charset = CS_UTF8;
	if (cs) {
		if (strcmp(cs, "swe7") == 0)
			t_charset = CS_SWE7;
		else if (strcmp(cs, "latin1") == 0)
			t_charset = CS_LATIN1;
		else if (strcmp(cs, "utf8") == 0)
			t_charset = CS_UTF8;
		else
			usage();
	}
	home = getenv("HOME");
	/* the system file: given, $LEXVMF, or $HOME/nylex.vmf (made from
	   the one in LEXLIB the first time) */
	if (vmfpath == NULL)
		vmfpath = getenv("LEXVMF");
	if (vmfpath == NULL) {
		sprintf(buf, "%s/nylex.vmf", home ? home : ".");
		vmfpath = xstrdup(buf);
	}
	strncpy(vmfname, vmfpath, sizeof vmfname - 1);
	if ((sysvmf = vmf_open(vmfname)) == NULL) {
		if (fexists(vmfname)) {
			fprintf(stderr, "nylex: %s is not a LEX system file\n", vmfname);
			return 1;
		}
		/* a copy of the library's file of that name, or of nylex.vmf
		   (Swedish) or nylex-en.vmf (English) after the locale */
		{
			const char *base = strrchr(vmfname, '/') ? strrchr(vmfname, '/') + 1 : vmfname;
			const char *def = swedish() ? "nylex.vmf" : "nylex-en.vmf";

			sprintf(buf, "%s/%.200s", LEXLIB, base);
			if (!fexists(buf) || strcmp(base, "nylex.vmf") == 0)
				sprintf(buf, "%s/%s", LEXLIB, def);
			if (copyfile(buf, vmfname) == 0)
				sysvmf = vmf_open(vmfname);
		}
		if (sysvmf == NULL) {
			fprintf(stderr, "nylex: cannot make %s (no %s)\n", vmfname, buf);
			return 1;
		}
	}
	if (work == NULL)
		work = getenv("LEXWORK");
	if (work)
		strncpy(workname, work, sizeof workname - 1);
	else
		sprintf(workname, "%s/nylex.wrk", home ? home : ".");
	sys_load();
	signal(SIGHUP, onsig);
	signal(SIGTERM, onsig);
	if (t_open() < 0) {
		fprintf(stderr, "nylex: not a terminal\n");
		return 1;
	}
	/* the terminal record: given, $LEXVDU, $TERM (as LEX-68's lex68
	   script gives it: /U$TERM), then **INST's; each as it is and in
	   capitals; VT100 and the built-in VT100 keys last */
	{
		const char *cand[5];
		int nc = 0, k, done = 0;

		if (term)
			cand[nc++] = term;
		if (getenv("LEXVDU"))
			cand[nc++] = getenv("LEXVDU");
		if (getenv("TERM"))
			cand[nc++] = getenv("TERM");
		if (I.vdu[0])
			cand[nc++] = I.vdu;
		cand[nc++] = "VT100";
		for (k = 0; k < nc && !done; k++) {
			char up[16];
			int j;

			if (vdu_load(cand[k]) == 0) {
				done = 1;
				break;
			}
			for (j = 0; cand[k][j] && j < 9; j++)
				up[j] = (char)upcase((unsigned char)cand[k][j]);
			up[j] = 0;
			if (up[0] && vdu_load(up) == 0)
				done = 1;
		}
		if (!done)
			vdu_load("");
	}
	work_load();
	ED.ext[0] = 0;
	if (ahead[0])
		in_typeahead(ahead);
	if (rec_get("**START", NULL) != NULL && !doc) {
		/* **START: a program, in the playground, in shut up mode */
		shutup = 1;
		edit_run(ED_PLAY, NULL, NULL, "**START");
		shutup = 0;
	}
	if (doc && !quitting) {
		char path[300];
		struct regent r;

		strncpy(lastdoc, doc, MAXNAME);
		lastdoc[MAXNAME] = 0;
		docname_norm(lastdoc, I.defext);
		docname_file(path, lastdoc);
		/* a file named as it is (a path, other capitals) */
		if (!fexists(path) && fexists(doc)) {
			strncpy(path, doc, sizeof path - 1);
			path[sizeof path - 1] = 0;
		}
		docname_ext(lastdoc, ED.ext);
		if (reg_get(lastdoc, &r) < 0)
			reg_new(&r, lastdoc);
		if (edit_run(fexists(path) ? ED_EDIT : ED_NEW, path, lastdoc, NULL) == EX_EXIT) {
			reg_touch(&r, 'E', (long)time(NULL) - ED.started);
			r.lines = ED_lines;
			r.words = ED_words;
			r.chars = ED_chars;
			reg_put(&r);
		}
	} else if (!quitting)
		menu_loop();
	work_save();
	esc_closefiles();
	t_clear();
	t_refresh();
	t_close();
	vmf_close(sysvmf);
	return 0;
}
