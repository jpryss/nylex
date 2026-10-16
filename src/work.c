/*
 * work.c - the work file: what LEX keeps between sessions of a user
 * (keystores, rulers 6-9, the strings of find, repeat and merge, the
 * calculator's memories, the cut buffers, the last document and the
 * menu position).  /Z changes to another, /2 starts it again.
 *
 * It is a text file of tagged lines ("NYLEX-WORK 1" first), $HOME/
 * nylex.wrk unless /W or $LEXWORK names another.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

char	workname[256];

void	cut_save(FILE *f);
int	cut_load1(FILE *f, int i, int n, int sentence);

static void
putstr(FILE *f, const char *tag, int n, const char *s)
{
	fprintf(f, "%s %d %d ", tag, n, (int)strlen(s));
	fputs(s, f);
	putc('\n', f);
}

static int
getstr(FILE *f, char *s, int max)
{
	int len, i, c;

	if (fscanf(f, "%d", &len) != 1 || getc(f) != ' ')
		return -1;
	for (i = 0; i < len; i++) {
		if ((c = getc(f)) == EOF)
			return -1;
		if (i < max)
			s[i] = (char)c;
	}
	s[len < max ? len : max] = 0;
	return 0;
}

int
work_save(void)
{
	FILE *f;
	int i;

	if (!workname[0])
		return 0;
	if ((f = fopen(workname, "w")) == NULL)
		return -1;
	fprintf(f, "NYLEX-WORK 1\n");
	for (i = 0; i < NKS; i++)
		if (keystore[i][0])
			putstr(f, "K", i, keystore[i]);
	for (i = 6; i <= 9; i++)
		if (ED.rulers[i][0])
			putstr(f, "R", i, ED.rulers[i]);
	for (i = 0; i < 4; i++)
		if (funcstr[i][0])
			putstr(f, "S", i, funcstr[i]);
	for (i = 0; i < 36; i++)
		if (calcmem[i] != 0)
			fprintf(f, "C %d %.15g\n", i, calcmem[i]);
	if (lastdoc[0])
		putstr(f, "D", 0, lastdoc);
	cut_save(f);
	return fclose(f);
}

int
work_load(void)
{
	FILE *f;
	char tag[16], b[MAXW + 1];
	int n, depth = 0;

	if (!workname[0] || (f = fopen(workname, "r")) == NULL)
		return -1;
	if (fgets(b, sizeof b, f) == NULL || strncmp(b, "NYLEX-WORK", 10) != 0) {
		fclose(f);
		error("NW");
		return -1;
	}
	while (fscanf(f, "%15s %d", tag, &n) == 2) {
		switch (tag[0]) {
		case 'K':
			if (getstr(f, b, KSLEN) == 0 && n >= 0 && n < NKS)
				strcpy(keystore[n], b);
			break;
		case 'R':
			if (getstr(f, b, MAXW) == 0 && n >= 6 && n <= 9)
				strcpy(ED.rulers[n], b);
			break;
		case 'S':
			if (getstr(f, b, KSLEN) == 0 && n >= 0 && n < 4)
				strcpy(funcstr[n], b);
			break;
		case 'C':
			{
				double v;

				if (fscanf(f, "%lf", &v) == 1 && n >= 0 && n < 36)
					calcmem[n] = v;
			}
			break;
		case 'D':
			if (getstr(f, b, MAXNAME) == 0)
				strcpy(lastdoc, b);
			break;
		case 'M':
			if (getstr(f, b, KEYLEN) == 0 && depth < MAXMENU)
				strcpy(menustack[depth++], b);
			break;
		case 'B':
			{
				int nl, sent;

				if (fscanf(f, "%d %d", &nl, &sent) != 2 || getc(f) != '\n' || cut_load1(f, n, nl, sent) < 0)
					goto out;
				continue;
			}
		default:
			goto out;
		}
		if (getc(f) != '\n')
			break;
	}
out:
	fclose(f);
	if (depth > 0)
		menudepth = depth;
	return 0;
}
