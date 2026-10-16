/*
 * sys.c - the system control records (manual, chapters 5-7): **MONT
 * (special characters, dates, answers), **INST (installation),
 * **PNST (printers), the messages of **TEXT, **TEXT2 and **TEXTP, and
 * the tables *CASE, *SORT and *CONV.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lex.h"

struct mont	M;
struct inst	I;
struct pnst	P;
char		montkey[11] = "**MONT", instkey[11] = "**INST", pnstkey[11] = "**PNST";

static unsigned char	up[256], low[256], sortt[256], conv[256];

/* The defaults: LEX-68's Swedish set (manual table 5.1, German/Scandinavian). */
static const int deffield1[22] = {
	60, 59, 62, 0, 0, 0, 40, 36, 58, 39, 34, 33, 64, 60, 61, 62, 58, 33, 40, 41, 38, 21
};
static const int engfield1[20] = {
	91, 92, 93, 123, 124, 125, 60, 36, 62, 39, 34, 33, 64, 60, 61, 62, 58, 33, 40, 41
};

static void
setfield1(const int *v)
{
	M.startkey = v[0]; M.ret = v[1]; M.endkey = v[2];
	M.left = v[3]; M.tab = v[4]; M.right = v[5];
	M.vstart = v[6]; M.vesc = v[7]; M.vend = v[8]; M.vctl = v[9];
	M.lit = v[10]; M.qmark = v[11]; M.next = v[12];
	M.op_lt = v[13]; M.op_eq = v[14]; M.op_gt = v[15]; M.op_has = v[16];
	M.pause = v[17]; M.sel_l = v[18]; M.sel_r = v[19];
}

static void
mont_defaults(void)
{
	static const char *mon[12] = { "-01-", "-02-", "-03-", "-04-", "-05-", "-06-",
		"-07-", "-08-", "-09-", "-10-", "-11-", "-12-" };
	int i;

	memset(&M, 0, sizeof M);
	t_national = 1;
	setfield1(deffield1);
	M.cont = '&';
	M.menuexit = 21;
	for (i = 0; i < 12; i++)
		strcpy(M.month[i], mon[i]);
	M.style = 'Y';
	M.yes = 'J'; M.no = 'N'; M.query = 'Q'; M.from = 'F'; M.to = 'T';
	M.odd = 'U'; M.even = 'J';
	M.onespace = 1;
	M.hyphlang = 'N';
	M.paint = '_';
	M.g_or = '/'; M.g_and = '&'; M.g_col = '\\';
	M.attrterm = 255;
	M.kill = 'K';
	M.timesep = ':';
	strcpy(M.shortdate, "-janfebmaraprmajjunjulaugsepoktnovdec");
	strcpy(M.vowels, "AEIOUY[\\]");
	M.case_lo[0] = 96; M.case_hi[0] = 126;
	M.case_lo[1] = 224; M.case_hi[1] = 254;
	strcpy(M.stat, "SRVK");
	strcpy(M.help, "H");
}

static int
num3(const char *s, int i)
{
	int k, v = 0;

	for (k = 0; k < 3; k++) {
		if (s[3 * i + k] < '0' || s[3 * i + k] > '9')
			return -1;
		v = v * 10 + s[3 * i + k] - '0';
	}
	return v;
}

/* the fields of a record: separated by Return and by sep */
static int
fields(char *t, char **f, int max, int sep)
{
	int n = 0;
	char *p = t;

	f[n++] = t;
	for (; *p && n < max; p++)
		if (*p == '\n' || (sep && *p == sep)) {
			*p = 0;
			f[n++] = p + 1;
		}
	return n;
}

static void
mont_load(void)
{
	char *t, *f[64];
	long len;
	int n, i, v[22];

	mont_defaults();
	if (rec_text(montkey, &t, &len, '_') < 0)
		return;
	n = fields(t, f, 64, '\\');
	if (n > 0 && strlen(f[0]) >= 3) {
		int nf = (int)strlen(f[0]) / 3;

		for (i = 0; i < 22; i++)
			v[i] = deffield1[i];
		if (num3(f[0], 0) == 91) {
			/* an English **MONT: [ \ ] { | } are ASCII again */
			for (i = 0; i < 20; i++)
				v[i] = engfield1[i];
			t_national = 0;
		}
		else
			for (i = 0; i < 20 && i < nf; i++) {
				int x = num3(f[0], i);

				if (x < 0)
					break;
				v[i] = x;
			}
		for (i = 20; i < 22 && i < nf; i++)
			if (num3(f[0], i) >= 0)
				v[i] = num3(f[0], i);
		setfield1(v);
		M.cont = v[20];
		M.menuexit = v[21];
	}
	for (i = 0; i < 12 && 1 + i < n; i++) {
		strncpy(M.month[i], f[1 + i], 15);
		M.month[i][15] = 0;
	}
	for (i = 0; i < 5; i++) {
		if (13 + i < n) {
			strncpy(M.pre[i], f[13 + i], 2);
			M.pre[i][2] = 0;
		}
		if (18 + i < n) {
			strncpy(M.suf[i], f[18 + i], 2);
			M.suf[i][2] = 0;
		}
	}
	if (23 < n)
		M.style = f[23][0];
	if (25 < n && f[25][0])
		M.yes = upcase((unsigned char)f[25][0]);
	if (26 < n && f[26][0])
		M.no = upcase((unsigned char)f[26][0]);
	if (27 < n) {
		char *q = f[27];

		if (q[0]) M.query = q[0];
		if (q[0] && q[1]) M.from = q[1];
		if (q[0] && q[1] && q[2]) M.to = q[2];
		if (q[0] && q[1] && q[2] && q[3]) M.odd = q[3];
		if (q[0] && q[1] && q[2] && q[3] && q[4]) M.even = q[4];
	}
	if (28 < n)
		M.onespace = f[28][0] == 'S';
	if (29 < n)
		M.hyphlang = f[29][0];
	/* English hyphenation: an English system file, whose text is ASCII */
	if (M.hyphlang == 'E')
		t_national = 0;
	if (30 < n && strlen(f[30]) >= 3) {
		int x[11], k, nf = (int)strlen(f[30]) / 3;

		for (k = 0; k < 11; k++)
			x[k] = k < nf ? num3(f[30], k) : -1;
		if (x[0] >= 0) M.aust_on = x[0];
		if (x[1] >= 0) M.aust_off = x[1];
		if (x[2] >= 0) M.up = x[2];
		if (x[3] >= 0) M.down = x[3];
		if (x[4] > 0) M.paint = x[4];
		if (x[5] >= 0) M.sysret = x[5];
		if (x[6] >= 0) M.g_or = x[6];
		if (x[7] >= 0) M.g_and = x[7];
		if (x[8] >= 0) M.g_col = x[8];
		if (x[9] > 0) M.attrterm = x[9];
	}
	if (32 < n)
		M.kill = f[32][0];
	if (33 < n && f[33][0])
		M.timesep = f[33][0];
	if (34 < n && strlen(f[34]) >= 37) {
		strncpy(M.shortdate, f[34], 37);
		M.shortdate[37] = 0;
	}
	if (35 < n)
		M.yearadd = atoi(f[35]);
	if (36 < n && f[36][0]) {
		strncpy(M.vowels, f[36], 31);
		M.vowels[31] = 0;
	}
	if (38 < n && strlen(f[38]) >= 12) {
		M.case_lo[0] = num3(f[38], 0);
		M.case_hi[0] = num3(f[38], 1);
		M.case_lo[1] = num3(f[38], 2);
		M.case_hi[1] = num3(f[38], 3);
	}
	/* the last fields: the status characters and the help word */
	for (i = n - 1; i > 36; i--)
		if (strlen(f[i]) == 4) {
			strcpy(M.stat, f[i]);
			if (i + 1 < n && f[i + 1][0]) {
				strncpy(M.help, f[i + 1], 15);
				M.help[15] = 0;
			}
			break;
		}
	xfree(t);
}

static void
copyfield(char *to, const char *rec, long len, int off, int n)
{
	int i;

	for (i = 0; i < n; i++)
		to[i] = off + i < len && rec[off + i] != '_' && rec[off + i] != '\n' ? rec[off + i] : ' ';
	to[n] = 0;
	while (n > 0 && to[n - 1] == ' ')
		to[--n] = 0;
}

static int
flag(const char *rec, long len, int off)
{
	return off < len && is_yes((unsigned char)rec[off]);
}

static int
numfield(const char *rec, long len, int off, int n, int dflt)
{
	char b[16];

	copyfield(b, rec, len, off, n);
	return b[0] ? atoi(b) : dflt;
}

static void
inst_load(void)
{
	char *t, b[16];
	long len;

	memset(&I, 0, sizeof I);
	strcpy(I.vdu, "VT100");
	I.worksize = 128;
	strcpy(I.defext, ".BRV");
	strcpy(I.bakext, ".BAK");
	strcpy(I.tmpext, ".TMP");
	strcpy(I.vmfbak, ".VMB");
	strcpy(I.pcc, "............");
	I.fcase = 'U';
	I.titling = 1;
	I.counts = 1;
	I.summary = 1;
	I.helpsel = 1;
	I.showsel = 1;
	I.nsel = 1;
	I.sleep = 1;
	I.blockwarn = 5;
	I.decpt = ',';
	I.thou = '.';
	I.decs = 2;
	if (rec_text(instkey, &t, &len, '_') < 0)
		return;
	copyfield(I.vdu, t, len, 1, 9);
	copyfield(I.device, t, len, 10, 32);
	I.worksize = numfield(t, len, 42, 3, 128);
	copyfield(I.prtkey, t, len, 45, 10);
	copyfield(I.defext, t, len, 77, 4);
	copyfield(I.bakext, t, len, 81, 4);
	copyfield(I.tmpext, t, len, 85, 4);
	copyfield(I.vmfbak, t, len, 89, 4);
	copyfield(b, t, len, 93, 12);
	if (b[0]) {
		int i;

		for (i = 0; i < 12; i++)
			I.pcc[i] = 93 + i < len && t[93 + i] != '_' ? t[93 + i] : '.';
		I.pcc[12] = 0;
	}
	I.multi = flag(t, len, 111);
	I.keeptmp = flag(t, len, 112);
	I.versions = flag(t, len, 113);
	I.freeform = flag(t, len, 114);
	I.fcase = 115 < len && t[115] != '_' ? upcase((unsigned char)t[115]) : 'U';
	I.titling = flag(t, len, 116);
	I.counts = flag(t, len, 117);
	I.detail = flag(t, len, 118);
	I.summary = flag(t, len, 119);
	I.helpcopy = flag(t, len, 120);
	I.helpsel = flag(t, len, 121);
	I.running = flag(t, len, 122);
	I.sure = flag(t, len, 123);
	I.showkeys = flag(t, len, 124);
	I.showpage = flag(t, len, 125);
	I.freecmp = flag(t, len, 126);
	I.showsel = flag(t, len, 128);
	I.nsel = flag(t, len, 129);
	I.delay = numfield(t, len, 130, 3, 0);
	I.sleep = numfield(t, len, 133, 3, 1);
	I.blockwarn = numfield(t, len, 136, 3, 5);
	I.autoerr = flag(t, len, 142);
	I.statline = flag(t, len, 143);
	I.pagewarn = flag(t, len, 144);
	if (146 < len && t[146] != '_')
		I.decpt = t[146];
	if (147 < len)
		I.thou = t[147] != '_' ? t[147] : 0;
	if (148 < len)
		I.neg = t[148] != '_' && t[148] != ' ' ? t[148] : 0;
	if (150 < len && t[150] >= '0' && t[150] <= '9')
		I.decs = t[150] - '0';
	xfree(t);
}

static void
pnst_load(void)
{
	char *t, b[4];
	long len;
	int i, o;

	memset(&P, 0, sizeof P);
	strcpy(P.prompt, "Vilken skrivare (1 Systemets skrivare  (2 Filen utskrift.lst");
	strcpy(P.p[0].type, "");
	strcpy(P.p[0].device, "lp -s");
	strcpy(P.p[1].device, "utskrift.lst");
	P.p[0].lastff = P.p[1].lastff = 1;
	P.pagestart = 1;
	if (rec_text(pnstkey, &t, &len, '_') < 0)
		return;
	{
		char a[80], c[80];

		copyfield(a, t, len, 1, 78);
		copyfield(c, t, len, 79, 78);
		strcpy(P.prompt, a);
		if (c[0]) {
			strcat(P.prompt, "\n");
			strcat(P.prompt, c);
		}
	}
	for (i = 0; i < 10; i++) {
		o = 158 + 64 * i;
		copyfield(P.p[i].type, t, len, o, 8);
		copyfield(P.p[i].petal, t, len, o + 8, 8);
		P.p[i].spool = flag(t, len, o + 16);
		P.p[i].pause = flag(t, len, o + 17);
		P.p[i].vdu = flag(t, len, o + 18);
		P.p[i].lastff = flag(t, len, o + 19);
		copyfield(P.p[i].device, t, len, o + 24, 40);
		{
			/* "_" in a device name stands for a blank, as the form says */
			char *p;

			for (p = P.p[i].device; *p; p++)
				if (*p == '_')
					*p = ' ';
		}
	}
	copyfield(P.spooldev, t, len, 798, 16);
	copyfield(b, t, len, 814, 1);
	P.pagestart = b[0] == '0' ? 0 : 1;
	copyfield(P.ndxext, t, len, 815, 4);
	xfree(t);
}

/* Messages */

#define NTEXT	3
static char	*textbuf[NTEXT];
static char	*textf[NTEXT][80];
static int	ntextf[NTEXT];

static const char *deftext0[] = {
	"", "Dokumentnamn: ", "Rubrik: ", "Beskrivning: ",
	"Skapad  [ndrad ggr L{st   ggr Utskr  ggr Senast Tottid  Rader      Ord   Tecken",
	"Fr}n ", "  Till", "  Fr}n", "  Till", "ENDAST VISNING", "Ny arbetsfil",
	"VMF-filen n{stan full. Reorganisera  Lediga block ", "Ingen skrivr{tt till VMF-filen",
	"Arbetsfilen f|r liten", "Inte en arbetsfil", "Upptagen",
	"Felaktigt filnamn", " finns redan", " finns ej", "Backupfiler kan inte {ndras",
	"Menyn saknas", "Felaktig styrlinje i dokumentets format", "Dokumentindex l}st",
	"Bildsk{rmstyp ", "Nyckel ", "Svara H f|r hj{lp",
	"Tryck en tangent f|r att forts{tta", "LEX kopierar ", "Borttaget",
	" {r du s{ker ", "(J/N) ", "vald. V{nta ...", "S|ker efter ",
	"slutet av dokumentet", "str{ng ", "F|rdr|jning ", "STATUS ", "Fel {r ",
	"Upprepa ", "Avstavat: ", "Nyckel {r ", "Yttre fil {r ", "Mask {r ",
	"L{s in ", "Skriv ut ", "fram till ", "till ", "bara ", "till slutet av ",
	"Finn ", "S|k ", "S|k bak}t ", "|ppna (skriv |ver): ", "|ppna (ny): ",
	"QS-nyckel ", "QT-kommando ", "Ny filtyp: ", "Attribut p}: ",
	"Avsluta ", "Avbryt ", "Spara, b|rja om ", "Spara, forts{tt ", ""
};
static const char *deftext1[] = {
	"", "Reorganisation av VMF-filen ", "Anv{nda datablock  ", "Lediga datablock   ",
	"Anv{nda nycklar    ", "Nycklar i index    ", "Nycklar i overflow ",
	"Nytt VMF-namn ", "Automatisk reorganisation (J/N) ", "Nytt antal datablock ",
	"Nytt antal nycklar i index ", "Filen skapas. Block skrivna ",
	"Kopiering av poster. Aktuell fil {r ", "Andra VMF-filens namn ",
	"Fr}n aktuell fil (F) eller till aktuell fil (T) (F/T) ",
	"Nyckel ", "Ny nyckel ", " kopierad", " finns ej", " finns redan - kopiera {nd} ",
	" {r ingen VMF-fil", "Kopiera alla poster (J/N) ", " VMF-filen full",
	"Patchprogram ", "Fil att patcha ", " finns ej", "LEX status ", "VMF {r ",
	"Arbetsfil {r ", "Menyposition ", "Anv{nda klipp-buffertar ",
	"Yttre fil ", "VMF-statistik ", "Totalt antal block ", "Anv{nda kalkylatorminnen ",
	"Reorganisationen klar. Byt namn och anv{nd nya VMF-filen (J/N) ",
	"L}s upp poster i VMF-filen ", "Nyckel l}st ", "L}sen tas bort. V{nta ...",
	"Kan inte byta namn mellan kataloger", "Installationsposter ", "flera anv{ndare",
	"en anv{ndare"
};
static const char *deftext2[] = {
	"",
	"OCH     &&ELLER   /&INTE    @@NYCKEL  KSPOSTTYP kSDATUM   DSSIDA    dSSRECS   zSTRECS   ZSTID     tS",
	"Formatet finns ej", "Ordlistan finns ej", " finns ej i ordlistan",
	"F{ltnamn att v{lja p}: ", "Urval: ", "V{lj ", "Felaktigt urval",
	"LEX s|ker ", "LEX sorterar ", "Senaste nyckel ", "Antal valda poster: ",
	"Summor: ", "Antal kopior ", "Stanna vid varje sida ",
	"St{ll in papperet och tryck en tangent", "Kodkonvertering ", "Sida ",
	"Numrera fr}n ", "Skriv fr}n sida ", "Sista sida ",
	"Udda eller j{mna sidor (U/J) ", "Enheten upptagen", "Utskriftsfil ",
	"Felaktigt format"
};

void
text_init(void)
{
	static const char *keys[NTEXT] = { "**TEXT", "**TEXT2", "**TEXTP" };
	int r, i;
	long len;
	char *t;

	for (r = 0; r < NTEXT; r++) {
		xfree(textbuf[r]);
		textbuf[r] = NULL;
		ntextf[r] = 0;
		if (rec_text(keys[r], &t, &len, '_') < 0)
			continue;
		textbuf[r] = t;
		ntextf[r] = fields(t, textf[r], 80, M.ret);
		for (i = 0; i < ntextf[r]; i++) {
			char *p = textf[r][i];

			if (r == 2 && i == 1)
				continue;		/* the operands keep their characters */
			for (; *p; p++) {
				if (*p == '_' || *p == '^')
					*p = ' ';
				else if (*p == '!')
					*p = '?';
			}
		}
	}
}

const char *
text(int rec, int n)
{
	const char **def = rec == 0 ? deftext0 : rec == 1 ? deftext1 : deftext2;
	int ndef = rec == 0 ? (int)(sizeof deftext0 / sizeof *deftext0) :
	    rec == 1 ? (int)(sizeof deftext1 / sizeof *deftext1) : (int)(sizeof deftext2 / sizeof *deftext2);

	if (rec >= 0 && rec < NTEXT && textbuf[rec] && n >= 0 && n < ntextf[rec])
		return textf[rec][n];
	return n >= 0 && n < ndef ? def[n] : "";
}

int
is_yes(int c)
{
	c = upcase(c);
	return c == M.yes || c == 'Y';
}

int
is_no(int c)
{
	c = upcase(c);
	return c == M.no;
}

/* Dates and times */

static struct tm *
now(void)
{
	time_t t = time(NULL);

	return localtime(&t);
}

static void
squeeze(char *b)
{
	char *s = b, *d = b;

	while (*s == ' ')
		s++;
	while (*s) {
		if (*s == ' ' && (s[1] == ' ' || s[1] == 0)) {
			s++;
			continue;
		}
		*d++ = *s++;
	}
	*d = 0;
}

void
date_full(char *b)
{
	struct tm *tm = now();
	int d = tm->tm_mday, k;
	char day[12];

	k = d == 1 ? 0 : (d == 21 || d == 31) ? 1 : d == 2 ? 2 : d == 3 ? 3 : 4;
	sprintf(day, "%s%d%s", M.pre[k], d, M.suf[k]);
	if (M.style == 'Y')
		/* year-month-day: LEX-68 writes 19 before the two digits of
		   the year (1926-10-16 in 2026); this is the year itself */
		sprintf(b, "%04d%s%02d", tm->tm_year + 1900 + M.yearadd, M.month[tm->tm_mon], d);
	else if (M.style == 'M')
		sprintf(b, "%s %s %d", M.month[tm->tm_mon], day, tm->tm_year + 1900 + M.yearadd);
	else
		sprintf(b, "%s %s %d", day, M.month[tm->tm_mon], tm->tm_year + 1900 + M.yearadd);
	squeeze(b);
}

void
date_short(char *b)
{
	struct tm *tm = now();
	char d = M.shortdate[0];

	sprintf(b, "%02d%c%.3s%c%02d", tm->tm_mday, d, M.shortdate + 1 + 3 * tm->tm_mon, d, tm->tm_year % 100);
}

void
date_ddmmmyy(char *b)
{
	struct tm *tm = now();

	sprintf(b, "%02d%.3s%02d", tm->tm_mday, M.shortdate + 1 + 3 * tm->tm_mon, tm->tm_year % 100);
}

void
date_yymmdd(char *b)
{
	struct tm *tm = now();

	sprintf(b, "%02d%02d%02d", tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday);
}

void
time_hms(char *b, int sep)
{
	struct tm *tm = now();

	if (sep)
		sprintf(b, "%02d%c%02d%c%02d", tm->tm_hour, sep, tm->tm_min, sep, tm->tm_sec);
	else
		sprintf(b, "%02d%02d%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/* Tables (chapter 7) */

int
upcase(int c)
{
	return up[c & 0xff];
}

int
lowcase(int c)
{
	return low[c & 0xff];
}

int
sortval(int c)
{
	return sortt[c & 0xff];
}

int
convchar(int c)
{
	return conv[c & 0xff];
}

static int
tabval(const char *t, long len, int i)
{
	char b[4];
	int k;

	for (k = 0; k < 3; k++)
		b[k] = 1 + 3 * i + k < len ? t[1 + 3 * i + k] : '_';
	b[3] = 0;
	if (b[0] == '_' || b[0] == ' ')
		return -1;
	if (b[0] >= '0' && b[0] <= '9' && b[1] >= '0' && b[1] <= '9')
		return atoi(b);
	return (unsigned char)b[0];
}

void
tables_load(void)
{
	int c, i, k, v;
	char *t;
	long len;

	for (c = 0; c < 256; c++)
		up[c] = low[c] = sortt[c] = conv[c] = (unsigned char)c;
	for (k = 0; k < 2; k++)
		if (M.case_lo[k] > 32 && M.case_hi[k] >= M.case_lo[k] && M.case_hi[k] < 256)
			for (c = M.case_lo[k]; c <= M.case_hi[k]; c++) {
				up[c] = (unsigned char)(c - 32);
				low[c - 32] = (unsigned char)c;
			}
	/* LEX ignores 0-32, 95, 127, 128 and 255 */
	up[95] = low[95] = 95;
	up[127] = low[127] = 127;
	if (rec_text("*CASE", &t, &len, '_') == 0) {
		for (i = 33; i < 256; i++) {
			if (i == 95 || i == 127 || i == 128 || i == 255)
				continue;
			v = tabval(t, len, i);
			if (v > 0 && v < 256) {
				up[i] = (unsigned char)v;
				low[v] = (unsigned char)i;
			}
		}
		xfree(t);
	}
	if (rec_text("*SORT", &t, &len, '_') == 0) {
		for (i = 0; i < 256; i++)
			if ((v = tabval(t, len, i)) >= 0 && v < 256)
				sortt[i] = (unsigned char)v;
		xfree(t);
	}
	if (rec_text("*CONV", &t, &len, '_') == 0) {
		for (i = 0; i < 256; i++)
			if ((v = tabval(t, len, i)) >= 0 && v < 256)
				conv[i] = (unsigned char)v;
		xfree(t);
	}
}

void
sys_load(void)
{
	int c;

	for (c = 0; c < 256; c++)
		up[c] = low[c] = (unsigned char)c;
	mont_load();
	tables_load();
	inst_load();
	pnst_load();
	text_init();
	showerr = I.autoerr;		/* automatic error show (**INST) */
}

/* /1: another **MONT, **INST or **PNST */
int
sys_switch(const char *key)
{
	char k[11];

	strncpy(k, key, 10);
	k[10] = 0;
	if (strncmp(k, "**M", 3) == 0)
		strcpy(montkey, k);
	else if (strncmp(k, "**I", 3) == 0)
		strcpy(instkey, k);
	else if (strncmp(k, "**P", 3) == 0)
		strcpy(pnstkey, k);
	else
		return -1;
	{
		char kk[VMF_KEYLEN];

		mkkey(kk, k);
		if (rec_get(k, NULL) == NULL)
			return -1;
	}
	sys_load();
	return 0;
}
