/*
 * calc.c - the calculator (Appendix D): entered with ESC + or by Tab
 * into a field of # in the ruler.  Numbers and results are shown in the
 * text, ending at the cursor (or filling the # field); 36 memories 0-9
 * and A-Z are kept between documents.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"

double	calcmem[36];

static int	fs, fe;		/* the field: first and last column */
static int	prec, thou, nosig, isfield;
static double	acc, cur;
static int	op, entering, havecur;
static char	ent[40];
static int	nent;
static struct {
	double	acc;
	int	op;
} stack[4];
static int	depth;

/* the square root, by iteration (no maths library needed) */
static double
root(double v)
{
	double x = v > 1 ? v / 2 : 1;
	int i;

	for (i = 0; i < 60; i++)
		x = (x + v / x) / 2;
	return x;
}

static int
memidx(int c)
{
	c = upcase(c);
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'Z')
		return 10 + c - 'A';
	return -1;
}

double
calc_get(int c)
{
	int i = memidx(c);

	return i < 0 ? 0 : calcmem[i];
}

void
calc_clearall(void)
{
	int i;

	for (i = 0; i < 36; i++)
		calcmem[i] = 0;
}

int
calc_used(char *list)
{
	int i, n = 0;

	for (i = 0; i < 36; i++)
		if (calcmem[i] != 0)
			list[n++] = (char)(i < 10 ? '0' + i : 'A' + i - 10);
	list[n] = 0;
	return n;
}

static void
fmt(char *out, double v)
{
	char b[64], *p, *dot;
	int neg = v < 0, i, n, k;
	char res[80];

	sprintf(b, "%.*f", prec, neg ? -v : v);
	if (nosig && strchr(b, '.')) {
		n = (int)strlen(b);
		while (n > 0 && b[n - 1] == '0')
			b[--n] = 0;
		if (n > 0 && b[n - 1] == '.')
			b[--n] = 0;
	}
	dot = strchr(b, '.');
	if (dot)
		*dot = (char)(I.decpt ? I.decpt : '.');
	/* the thousands marker */
	n = dot ? (int)(dot - b) : (int)strlen(b);
	k = 0;
	for (i = 0; i < n; i++) {
		res[k++] = b[i];
		if (thou && (n - i - 1) % 3 == 0 && i < n - 1)
			res[k++] = (char)thou;
	}
	strcpy(res + k, b + n);
	p = res;
	if (neg) {
		if (I.neg == '(' )
			sprintf(out, "(%s)", p);
		else if (I.neg && I.neg != '-')
			sprintf(out, "%s%c", p, I.neg);
		else
			sprintf(out, "-%s", p);
	} else
		strcpy(out, p);
}

/* the field: from fs to fe, the text right-aligned in it */
static void
display(const char *s)
{
	struct line *l = ed_line(ED.cl);
	int n = (int)strlen(s), w = fe - fs + 1, k;
	char b[80];

	if (n > w) {
		error("CO");
		for (k = 0; k < w && k < 79; k++)
			b[k] = '*';
		b[k] = 0;
		s = b;
		n = w;
	}
	for (k = fs; k <= fe; k++)
		line_put(l, k, ' ', line_at(l, k) & (AT_UNPROT | AT_BG));
	for (k = 0; k < n; k++)
		line_put(l, fe - n + 1 + k, (unsigned char)s[k], (line_at(l, fe - n + 1 + k) & (AT_UNPROT | AT_BG)) | ED.attr);
	line_trim(l);
	ED.cc = fe;
	ED.d->changed = 1;
}

static void
showval(double v)
{
	char b[80];

	fmt(b, v);
	display(b);
}

static void
showent(void)
{
	ent[nent] = 0;
	display(nent ? ent : "0");
}

static double
entval(void)
{
	char b[40];
	int i;

	for (i = 0; i < nent; i++)
		b[i] = ent[i] == I.decpt ? '.' : ent[i];
	b[nent] = 0;
	return atof(b);
}

static double
apply(double a, int o, double b)
{
	switch (o) {
	case '+': return a + b;
	case '-': return a - b;
	case '*': return a * b;
	case '/':
		if (b == 0) {
			error("CE");
			return 0;
		}
		return a / b;
	}
	return b;
}

static double
value(void)
{
	return entering ? entval() : cur;
}

/* the number of the field under the cursor (for V, H, ^Y) */
static double
numat(struct line *l, int c, int *found)
{
	int b = c, e = c, k, n = 0;
	char s[64];

	*found = 0;
	if (line_ch(l, c) == ' ') {
		/* the field may end left of c */
		while (b > 0 && line_ch(l, b) == ' ' && b > c - 1)
			b--;
	}
	while (b > 0 && line_ch(l, b - 1) != ' ')
		b--;
	while (e + 1 < l->len && line_ch(l, e + 1) != ' ')
		e++;
	for (k = b; k <= e && n < 63; k++) {
		int ch = line_ch(l, k);

		if (ch == I.decpt)
			s[n++] = '.';
		else if ((ch >= '0' && ch <= '9') || ch == '-')
			s[n++] = (char)ch;
		else if (ch == thou || ch == '(' || ch == ')' || ch == '*')
			continue;
		else if (ch != ' ')
			return 0;
	}
	s[n] = 0;
	if (n == 0)
		return 0;
	*found = 1;
	return (line_ch(l, b) == '(' ? -1 : 1) * atof(s);
}

static void
setfield(void)
{
	struct ruler r;
	int k;

	ed_ruler(&r);
	isfield = 0;
	if (ED.cc < r.len && r.s[ED.cc] == '#') {
		for (k = ED.cc; k > 0 && (r.s[k - 1] == '#' || r.s[k - 1] == ',' ||
		    (r.s[k - 1] == '.' && k > 1 && r.s[k - 2] == '#')); k--)
			;
		fs = k;
		for (k = ED.cc; k + 1 < r.len && (r.s[k + 1] == '#' || r.s[k + 1] == ',' ||
		    (r.s[k + 1] == '.' && k + 2 < r.len && r.s[k + 2] == '#')); k++)
			;
		fe = k;
		prec = 0;
		for (k = fs; k <= fe; k++)
			if (r.s[k] == '.')
				prec = fe - k;
		thou = 0;
		for (k = fs; k <= fe; k++)
			if (r.s[k] == ',')
				thou = I.thou ? I.thou : ',';
		isfield = 1;
	} else {
		/* ESC + outside a # field: the number ends at the cursor and
		   has the blanks to the left of it (LEX-68 wants four; the
		   blank after the text before them is kept) */
		struct line *l = ed_line(ED.cl);

		fe = ED.cc;
		for (k = fe - 1; k >= 0 && line_ch(l, k) == ' '; k--)
			;
		fs = k < 0 ? 0 : k + 2;
		prec = I.decs;
		thou = I.thou;
	}
}

/*
 * The calculator: keys until one leaves it.  field: entered by Tab into
 * a # field (the cursor on its last column).
 */
void
calc_run(int field)
{
	int k, c, done = 0;
	char key[40];
	int nkey = -1;

	setfield();
	if (field && !isfield)
		return;
	if (!isfield && fe - fs + 1 < 4) {
		/* no room for a number */
		error("CN");
		return;
	}
	ED.calc = 1;
	acc = cur = 0;
	op = 0;
	entering = havecur = 0;
	nent = 0;
	depth = 0;
	nosig = 0;
	{
		/* a number already in the field is the first one */
		struct line *l = ed_line(ED.cl);
		int f;
		double v = numat(l, fe, &f);

		if (f && isfield) {
			cur = v;
			havecur = 1;
		} else
			showval(0);
	}
	while (!done) {
		ed_fix();
		k = in_key();
		if (k == K_NONE)
			break;
		c = KC(k);
		if (nkey >= 0) {
			/* a record key: [KEY] */
			if (c == M.endkey && !(k & K_LIT)) {
				key[nkey] = 0;
				nkey = -1;
				perform(key, 0);
			} else if (nkey < 39)
				key[nkey++] = (char)c;
			continue;
		}
		if (c == M.startkey) {
			nkey = 0;
			continue;
		}
		if (!(k & K_LIT) && (k == TAB || k == CR || k == CTL('H') || k == CTL('R') || k == LF)) {
			double v = op ? apply(acc, op, value()) : value();

			if (entering || op)
				showval(v);
			ED.calc = 0;
			if (k == TAB) {
				/* on to the next stop of the ruler, or just
				   past the field when there is none */
				struct ruler r;

				ed_ruler(&r);
				if (ruler_next(&r, fe, 0) > fe)
					ed_tab(0);
				else
					ED.cc = fe + 1;
			} else if (k == CR)
				ed_cr(0);
			else
				ed_ctl(k);
			return;
		}
		if (!(k & K_LIT) && (k == CTL('U') || k == CTL('J'))) {
			double v = op ? apply(acc, op, value()) : value();

			if (entering || op)
				showval(v);
			ED.cl += k == CTL('U') ? -1 : 1;
			if (ED.cl < 0)
				ED.cl = 0;
			entering = 0;
			nent = 0;
			op = 0;
			continue;
		}
		if (!(k & K_LIT) && (k == CTL('B') || k == CTL('F'))) {
			struct ruler r;
			int n;
			double v = op ? apply(acc, op, value()) : value();

			if (entering || op)
				showval(v);
			ed_ruler(&r);
			n = k == CTL('F') ? ruler_next(&r, fe, 0) : ruler_prev(&r, fs);
			ED.calc = 0;
			if (n >= 0 && r.s[n] == '#') {
				ED.cc = n;
				calc_run(1);
				return;
			}
			ed_ctl(k);
			return;
		}
		if (k == CTL('Y')) {
			int f;
			double v;

			if (ED.cl > 0) {
				v = numat(ed_line(ED.cl - 1), fe, &f);
				if (f) {
					cur = v;
					entering = 0;
					havecur = 1;
					showval(v);
				}
			}
			continue;
		}
		if (k == ESC) {
			int e = upcase(KC(in_key()));

			if (e == 'E') {
				double v = op ? apply(acc, op, value()) : value();

				if (entering || op)
					showval(v);
				break;
			}
			if (e >= '0' && e <= '9') {
				in_prog(keystore[e - '0'], (long)strlen(keystore[e - '0']), 0);
				continue;
			}
			error("CC");
			continue;
		}
		if (c == ' ') {
			double v = op ? apply(acc, op, value()) : value();

			if (entering || op)
				showval(v);
			ED.cc = fe + 1;
			break;
		}
		if ((c >= '0' && c <= '9') || c == I.decpt || (c == '.' && I.decpt != '.' && 0)) {
			if (!entering) {
				entering = 1;
				nent = 0;
			}
			if (nent < 16)
				ent[nent++] = (char)c;
			showent();
			continue;
		}
		if (c == '#' || c == '$')
			continue;
		switch (upcase(c)) {
		case 'P':
			k = KC(in_key());
			if (k >= '0' && k <= '9')
				prec = k - '0';
			break;
		case 'K':
			k = upcase(KC(in_key()));
			if (k == 'S')
				thou = KC(in_key());
			else if (k == 'U')
				thou = 0;
			break;
		case 'C':
			nent = 0;
			entering = 1;
			showent();
			break;
		case 'Z':
			acc = cur = 0;
			op = 0;
			nent = 0;
			entering = 0;
			depth = 0;
			showval(0);
			break;
		case 'F':
			nosig = 1;
			showval(value());
			break;
		case 'I':
			cur = (double)(long)value();
			entering = 0;
			showval(cur);
			break;
		case 'N':
			cur = -value();
			entering = 0;
			showval(cur);
			break;
		case '%':
			cur = value() / 100;
			entering = 0;
			break;
		case 'Q':
			{
				double v = value();

				cur = v > 0 ? root(v) : 0;
				entering = 0;
				showval(cur);
			}
			break;
		case '+':
		case '-':
		case '*':
		case '/':
			acc = op ? apply(acc, op, value()) : value();
			op = c;
			entering = 0;
			cur = acc;
			showval(acc);
			break;
		case '=':
			cur = op ? apply(acc, op, value()) : value();
			acc = cur;
			op = 0;
			entering = 0;
			showval(cur);
			break;
		case '(':
			if (depth < 4) {
				stack[depth].acc = acc;
				stack[depth].op = op;
				depth++;
				acc = 0;
				op = 0;
				entering = 0;
				cur = 0;
			} else
				error("CE");
			break;
		case ')':
			if (depth > 0) {
				double v = op ? apply(acc, op, value()) : value();

				depth--;
				acc = stack[depth].acc;
				op = stack[depth].op;
				cur = v;
				entering = 0;
				showval(v);
			} else
				error("CE");
			break;
		case 'S':
		case 'A':
		case 'R':
		case 'E':
			{
				int m = KC(in_key()), i = memidx(m), u = upcase(c);
				double v = value();

				if (u == 'E' && upcase(m) == 'Z') {
					calc_clearall();
					break;
				}
				if (i < 0) {
					error("CC");
					break;
				}
				if (u == 'S')
					calcmem[i] = v;
				else if (u == 'A')
					calcmem[i] += v;
				else if (u == 'E')
					calcmem[i] = 0;
				else {
					cur = calcmem[i];
					entering = 0;
					showval(cur);
				}
			}
			break;
		case 'H':
			{
				struct line *l = ed_line(ED.cl);
				double sum = 0;
				int p = fs - 1, f;

				while (p >= 0) {
					while (p >= 0 && line_ch(l, p) == ' ')
						p--;
					if (p < 0)
						break;
					{
						double v = numat(l, p, &f);

						if (f)
							sum += v;
					}
					while (p >= 0 && line_ch(l, p) != ' ')
						p--;
				}
				cur = sum;
				entering = 0;
				showval(sum);
			}
			break;
		case 'V':
			{
				double sum = 0;
				int i, f;

				for (i = ED.cl - 1; i >= 0; i--) {
					struct line *l = ed_line(i);
					double v;

					if (l->flags & LF_RULER)
						continue;
					v = numat(l, fe, &f);
					if (!f) {
						if (line_ch(l, fe) == ' ' && line_ch(l, fe - 1) == ' ')
							continue;
						break;
					}
					sum += v;
				}
				cur = sum;
				entering = 0;
				showval(sum);
			}
			break;
		default:
			error("CC");
		}
	}
	ED.calc = 0;
}
