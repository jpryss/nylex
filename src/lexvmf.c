/*
 * lexvmf - list and change LEX-68 system files (.VMF).
 *
 *	lexvmf -l file.vmf		list the records
 *	lexvmf -x file.vmf key		write a record to standard output
 *	lexvmf -t file.vmf key		the same as text: runs of the squash
 *					character "_" opened, NUL as newline
 *	lexvmf -p file.vmf key [file]	put a record (from file or stdin)
 *	lexvmf -T file.vmf key [file]	put a record from text (squashed)
 *	lexvmf -d file.vmf key		delete a record
 *	lexvmf -r file.vmf		rewrite (reorganise) the file, deleted
 *					records dropped
 *
 * Keys are given as typed; they are padded with blanks to 10
 * characters.  \xNN in a key stands for the byte NN.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vmf.h"

static void
mkkey(char *k, const char *s)
{
	int i = 0;

	while (*s && i < VMF_KEYLEN) {
		if (s[0] == '\\' && s[1] == 'x' && s[2] && s[3]) {
			char h[3];

			h[0] = s[2];
			h[1] = s[3];
			h[2] = 0;
			k[i++] = (char)strtol(h, NULL, 16);
			s += 4;
		} else
			k[i++] = *s++;
	}
	while (i < VMF_KEYLEN)
		k[i++] = ' ';
}

static void
pkey(const char *k)
{
	int i;

	for (i = 0; i < VMF_KEYLEN; i++) {
		unsigned char c = k[i];

		if (c < ' ' || c >= 0x7f)
			printf("\\x%02x", c);
		else
			putchar(c);
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: lexvmf -l|-r file.vmf\n"
	    "       lexvmf -x|-t|-d file.vmf key\n"
	    "       lexvmf -p|-T file.vmf key [file]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	struct vmf *v;
	char key[VMF_KEYLEN];
	int i;

	if (argc < 3 || argv[1][0] != '-')
		usage();
	if ((v = vmf_open(argv[2])) == NULL) {
		if ((argv[1][1] == 'p' || argv[1][1] == 'T') && (v = vmf_new(argv[2])) != NULL)
			;
		else {
			fprintf(stderr, "lexvmf: cannot read %s\n", argv[2]);
			return 1;
		}
	}
	switch (argv[1][1]) {
	case 'l':
		for (i = 0; i < v->nent; i++) {
			long len = 0;

			vmf_get(v, v->ent[i].key, &len);
			pkey(v->ent[i].key);
			printf(" %6ld\n", len);
		}
		break;
	case 'x':
	case 't':
		if (argc < 4)
			usage();
		mkkey(key, argv[3]);
		{
			long len;
			unsigned char *d = vmf_get(v, key, &len);

			if (d == NULL) {
				fprintf(stderr, "lexvmf: no record %s\n", argv[3]);
				return 1;
			}
			if (argv[1][1] == 't') {
				long j;

				for (j = 0; j < len; j++)
					if (d[j] == 0)
						putchar('\n');
					else if (d[j] < 32) {
						int r;

						for (r = 0; r < d[j]; r++)
							putchar('_');
					} else
						putchar(d[j]);
				if (len == 0 || d[len - 1] != 0)
					putchar('\n');
			} else
				fwrite(d, 1, len, stdout);
		}
		break;
	case 'p':
	case 'T':
		if (argc < 4)
			usage();
		mkkey(key, argv[3]);
		{
			FILE *f = argc > 4 ? fopen(argv[4], "rb") : stdin;
			unsigned char *buf = NULL;
			long n = 0, m = 0;
			int c;

			if (f == NULL) {
				perror(argv[4]);
				return 1;
			}
			while ((c = getc(f)) != EOF) {
				if (n == m) {
					m = m ? m * 2 : 4096;
					buf = (unsigned char *)xrealloc(buf, m);
				}
				buf[n++] = c;
			}
			if (argv[1][1] == 'T') {
				/* newlines as NUL, runs of "_" squashed */
				long j, o = 0;

				if (n > 0 && buf[n - 1] == '\n')
					n--;
				for (j = 0; j < n;) {
					if (buf[j] == '\n') {
						buf[o++] = 0;
						j++;
					} else if (buf[j] == '_') {
						int r = 0;

						while (j < n && buf[j] == '_' && r < 31)
							j++, r++;
						buf[o++] = (unsigned char)r;
					} else
						buf[o++] = buf[j++];
				}
				n = o;
			}
			if (vmf_put(v, key, buf ? buf : (unsigned char *)"", n) < 0 || vmf_save(v) < 0) {
				fprintf(stderr, "lexvmf: cannot write %s\n", argv[2]);
				return 1;
			}
		}
		break;
	case 'd':
		if (argc < 4)
			usage();
		mkkey(key, argv[3]);
		if (vmf_delete(v, key) < 0 || vmf_save(v) < 0) {
			fprintf(stderr, "lexvmf: cannot delete %s\n", argv[3]);
			return 1;
		}
		break;
	case 'r':
		if (vmf_write(v, argv[2], 1, 0, 0) < 0) {
			fprintf(stderr, "lexvmf: cannot write %s\n", argv[2]);
			return 1;
		}
		break;
	default:
		usage();
	}
	vmf_close(v);
	return 0;
}
