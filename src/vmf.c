/*
 * vmf.c - LEX system files (.VMF), after the LEX technical manual,
 * chapter 2:
 *
 *	block 0		header (structure mark 9f0a; the rest kept as found)
 *	block 1-	top-level index: BOB 255, keys of 10 bytes (51 per
 *			block), the highest key of each second-level block;
 *			the last byte is 0 in the last block
 *	then		second-level index blocks: 255, lock byte, next and
 *			previous block (16 bits), 36 entries of 14 bytes:
 *			key 10, pointer 3 (block + 1, 16 bits, top bit the
 *			lock; position), flags 1 (0x80: deleted)
 *	then		data blocks: byte 0 the BOB (next free position; a
 *			position p is at offset 2p + 1), records of a forward
 *			pointer (3), a length (1) and up to 124 bytes; the
 *			chain is a ring, the index points at its last record
 *
 * The file is read whole.  Saving writes all of it anew, as LEX's own
 * reorganisation does, keeping the size of the index and of the file as
 * they were found so that LEX can go on using it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vmf.h"

#define BLK		512
#define ENTLEN		14
#define ENTMAX		36
#define TLIKEYS		51
#define CHUNKMAX	124

static unsigned char hdr0[32] = {
	0x9f, 0x0a, 0x04, 0x5e, 0x04, 0x59, 0x00, 0xf7,
	0x00, 0x0d, 0x04, 0x6b, 0x00, 0x1e, 0x03, 0x30,
	0x02, 0x5e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void *
xrealloc(void *p, unsigned long n)
{
	void *q = p ? realloc(p, n ? n : 1) : malloc(n ? n : 1);

	if (q == NULL) {
		fprintf(stderr, "nylex: out of memory\n");
		exit(1);
	}
	return q;
}

/* at most n characters of s to d, and a 0 after them (d holds n + 1) */
void
scopy(char *d, const char *s, int n)
{
	int i;

	for (i = 0; i < n && s[i]; i++)
		d[i] = s[i];
	d[i] = 0;
}

void
xfree(void *p)
{
	if (p)
		free(p);
}

void
vmf_mkkey(char *out, const char *name)
{
	int i;

	for (i = 0; i < VMF_KEYLEN; i++)
		out[i] = (name && *name) ? *name++ : ' ';
}

static int
addent(struct vmf *v, const char *key, unsigned blk1, int pos, int flags, int lock)
{
	struct vmfent *e;

	if (v->nent == v->maxent) {
		v->maxent = v->maxent ? v->maxent * 2 : 512;
		v->ent = (struct vmfent *)xrealloc(v->ent, v->maxent * sizeof(struct vmfent));
	}
	e = &v->ent[v->nent++];
	memset(e, 0, sizeof *e);
	memcpy(e->key, key, VMF_KEYLEN);
	e->blk1 = blk1;
	e->pos = pos;
	e->flags = flags;
	e->lock = lock;
	return v->nent - 1;
}

static int
keycmp(const void *a, const void *b)
{
	int c = memcmp(((const struct vmfent *)a)->key, ((const struct vmfent *)b)->key, VMF_KEYLEN);

	if (c)
		return c;
	/* a live entry before a deleted one with the same key */
	return (((const struct vmfent *)a)->flags & VMF_DELETED) - (((const struct vmfent *)b)->flags & VMF_DELETED);
}

struct vmf *
vmf_new(const char *path)
{
	struct vmf *v;

	v = (struct vmf *)xrealloc(NULL, sizeof *v);
	memset(v, 0, sizeof *v);
	v->path = (char *)xrealloc(NULL, strlen(path) + 1);
	strcpy(v->path, path);
	memcpy(v->hdr, hdr0, 32);
	return v;
}

struct vmf *
vmf_open(const char *path)
{
	FILE *f;
	struct vmf *v;
	long n, nb, b;
	unsigned char *x;
	int o, ntli, first2li, guard;
	char *seen;

	if ((f = fopen(path, "rb")) == NULL)
		return NULL;
	v = vmf_new(path);
	fseek(f, 0L, 2);
	v->size = ftell(f);
	fseek(f, 0L, 0);
	v->file = (unsigned char *)xrealloc(NULL, v->size ? v->size : 1);
	n = (long)fread(v->file, 1, (size_t)v->size, f);
	fclose(f);
	if (n != v->size || v->size < 3 * BLK || v->file[BLK] != 0xff) {
		vmf_close(v);
		return NULL;
	}
	{
		FILE *w = fopen(path, "r+b");

		if (w == NULL)
			v->readonly = 1;
		else
			fclose(w);
	}
	nb = v->size / BLK;
	v->nblocks = nb;
	memcpy(v->hdr, v->file, 32);
	/* the top-level index: blocks from 1 until one ends in 0 */
	for (ntli = 1; 1 + ntli < nb && v->file[(long)ntli * BLK + BLK - 1] != 0 && ntli < 64; ntli++)
		;
	v->ntli = ntli;
	first2li = 1 + ntli;
	/* the second-level blocks: their chain from the first, which takes
	   in the overflow blocks too */
	seen = (char *)xrealloc(NULL, nb);
	memset(seen, 0, nb);
	b = first2li;
	guard = 0;
	v->n2li = 0;
	while (b > 0 && b < nb && !seen[b] && guard++ < 20000) {
		x = v->file + b * BLK;
		if (x[0] != 0xff)
			break;
		seen[b] = 1;
		if (b == first2li + v->n2li)
			v->n2li++;
		else
			v->overflow = 1;
		for (o = 6; o + ENTLEN <= BLK && x[o] != 0xff; o += ENTLEN) {
			unsigned p = (x[o + 10] << 8) | x[o + 11];

			addent(v, (char *)x + o, p & 0x7fff, x[o + 12], x[o + 13] & VMF_DELETED, (p & 0x8000) != 0);
		}
		b = (x[2] << 8) | x[3];
	}
	/* blocks of the index past the chain (none in a sound file) */
	for (b = first2li + v->n2li; b < nb; b++) {
		x = v->file + b * BLK;
		if (x[0] != 0xff || seen[b] || x[1] > 1 || ((x[2] << 8) | x[3]) >= nb)
			break;
		if (x[6] == 0xff && x[2] == 0 && x[3] == 0) {
			v->n2li++;		/* an empty one */
			continue;
		}
		break;
	}
	xfree(seen);
	qsort(v->ent, v->nent, sizeof(struct vmfent), keycmp);
	return v;
}

void
vmf_close(struct vmf *v)
{
	int i;

	if (v == NULL)
		return;
	for (i = 0; i < v->nent; i++)
		xfree(v->ent[i].data);
	xfree(v->ent);
	xfree(v->file);
	xfree(v->path);
	xfree(v);
}

/* The first entry with a key >= key. */
int
vmf_seek(struct vmf *v, const char *key)
{
	int lo = 0, hi = v->nent;

	while (lo < hi) {
		int m = (lo + hi) / 2;

		if (memcmp(v->ent[m].key, key, VMF_KEYLEN) < 0)
			lo = m + 1;
		else
			hi = m;
	}
	return lo;
}

int
vmf_find(struct vmf *v, const char *key, int deleted_too)
{
	int i = vmf_seek(v, key);

	for (; i < v->nent && memcmp(v->ent[i].key, key, VMF_KEYLEN) == 0; i++)
		if (deleted_too || !(v->ent[i].flags & VMF_DELETED))
			return i;
	return -1;
}

/* the record at block + 1 b1, position p: its forward pointer, data, length */
static unsigned char *
chunk(struct vmf *v, unsigned b1, int p, unsigned *nb1, int *np, int *len)
{
	long o;
	unsigned char *x;

	if (b1 == 0)
		return NULL;
	o = (long)(b1 - 1) * BLK + 2 * p + 1;
	if (o < BLK || o + 4 > v->size)
		return NULL;
	x = v->file + o;
	if (o + 4 + x[3] > v->size)
		return NULL;
	*nb1 = ((x[0] << 8) | x[1]) & 0x7fff;
	*np = x[2];
	*len = x[3];
	return x + 4;
}

static unsigned char *
readrec(struct vmf *v, struct vmfent *e, long *len)
{
	unsigned char *buf = NULL, *d;
	long n = 0;
	int l, p, guard = 0;
	unsigned b1;

	if (v->file == NULL || chunk(v, e->blk1, e->pos, &b1, &p, &l) == NULL)
		return NULL;
	for (;;) {			/* from the first record round to the last */
		unsigned cb = b1;
		int cp = p;

		d = chunk(v, cb, cp, &b1, &p, &l);
		if (d == NULL || guard++ > 20000) {
			xfree(buf);
			return NULL;
		}
		buf = (unsigned char *)xrealloc(buf, n + l + 1);
		memcpy(buf + n, d, l);
		n += l;
		if (cb == e->blk1 && cp == e->pos)
			break;
	}
	buf[n] = 0;
	*len = n;
	return buf;
}

unsigned char *
vmf_entdata(struct vmf *v, int i, long *len)
{
	struct vmfent *e = &v->ent[i];

	if (e->data == NULL) {
		e->data = readrec(v, e, &e->len);
		if (e->data == NULL) {
			e->data = (unsigned char *)xrealloc(NULL, 1);
			e->data[0] = 0;
			e->len = 0;
		}
	}
	if (len)
		*len = e->len;
	return e->data;
}

/* A record by key: a pointer to it (owned by v; 0-terminated). */
unsigned char *
vmf_get(struct vmf *v, const char *key, long *len)
{
	int i = vmf_find(v, key, 0);

	if (i < 0)
		return NULL;
	return vmf_entdata(v, i, len);
}

int
vmf_put(struct vmf *v, const char *key, const unsigned char *data, long len)
{
	int i = vmf_find(v, key, 1);
	struct vmfent *e;
	unsigned char *d;

	if (v->readonly)
		return -1;
	d = (unsigned char *)xrealloc(NULL, len + 1);
	memcpy(d, data, len);
	d[len] = 0;
	if (i < 0) {
		addent(v, key, 0, 0, 0, 0);
		qsort(v->ent, v->nent, sizeof(struct vmfent), keycmp);
		i = vmf_find(v, key, 1);
	} else {
		/* a deleted one of the same key is used again */
		int j;

		for (j = i + 1; j < v->nent && memcmp(v->ent[j].key, key, VMF_KEYLEN) == 0; j++)
			v->ent[j].flags |= VMF_DELETED;
	}
	e = &v->ent[i];
	xfree(e->data);
	e->data = d;
	e->len = len;
	e->flags &= ~VMF_DELETED;
	return vmf_save(v);
}

int
vmf_delete(struct vmf *v, const char *key)
{
	int i = vmf_find(v, key, 0);

	if (i < 0 || v->readonly)
		return -1;
	vmf_entdata(v, i, NULL);
	v->ent[i].flags |= VMF_DELETED;
	return vmf_save(v);
}

int
vmf_undelete(struct vmf *v, const char *key)
{
	int i;

	if (vmf_find(v, key, 0) >= 0)
		return -1;
	i = vmf_find(v, key, 1);
	if (i < 0 || v->readonly)
		return -1;
	vmf_entdata(v, i, NULL);
	v->ent[i].flags &= ~VMF_DELETED;
	return vmf_save(v);
}

/*
 * Writing.  out is the new file as blocks; the data blocks are filled in
 * key order, each chain in one stretch.
 */
struct wr {
	unsigned char	*buf;
	long		nblocks;	/* allocated */
	long		blk;		/* current data block */
	int		off;		/* the next free offset in it (odd) */
};

static unsigned char *
wblock(struct wr *w, long b)
{
	if (b >= w->nblocks) {
		long n = w->nblocks ? w->nblocks : 64, i;

		while (n <= b)
			n *= 2;
		w->buf = (unsigned char *)xrealloc(w->buf, n * BLK);
		memset(w->buf + w->nblocks * BLK, 0, (n - w->nblocks) * BLK);
		for (i = w->nblocks; i < n; i++)
			w->buf[i * BLK] = 1;		/* an empty data block */
		w->nblocks = n;
	}
	return w->buf + b * BLK;
}

static void
setptr(unsigned char *p, long blk, int pos)
{
	p[0] = (unsigned char)((blk + 1) >> 8) & 0x7f;
	p[1] = (unsigned char)((blk + 1) & 0xff);
	p[2] = (unsigned char)pos;
}

static void
writerec(struct wr *w, struct vmfent *e, const unsigned char *data, long len, long *eb, int *ep)
{
	long done = 0, fb = -1, pb = -1;
	int fp = 0, po = 0;
	unsigned char *x;

	do {
		int room, l;

		if (w->off + 4 + 1 > BLK) {
			w->blk++;
			w->off = 3;
		}
		x = wblock(w, w->blk);
		room = BLK - w->off - 4;
		l = len - done > CHUNKMAX ? CHUNKMAX : (int)(len - done);
		if (l > room)
			l = room;
		/* the visible equivalent of a control key is not split */
		if (l > 1 && done + l < len && data[done + l - 1] == '\'')
			l--;
		if (l <= 0 && len > 0) {
			w->off = BLK;
			continue;
		}
		memcpy(x + w->off + 4, data + done, l);
		x[w->off + 3] = (unsigned char)l;
		if (pb >= 0)
			setptr(wblock(w, pb) + po, w->blk, (w->off - 1) / 2);
		else {
			fb = w->blk;
			fp = (w->off - 1) / 2;
		}
		pb = w->blk;
		po = w->off;
		done += l;
		w->off += 4 + l;
		if (!(w->off & 1))
			w->off++;
		x[0] = (unsigned char)((w->off - 1) / 2 > 255 ? 255 : (w->off - 1) / 2);
	} while (done < len);
	/* the last points to the first; the index to the last */
	setptr(wblock(w, pb) + po, fb, fp);
	*eb = pb;
	*ep = (po - 1) / 2;
	(void)e;
}

int
vmf_write(struct vmf *v, const char *path, int dropdeleted, long minblocks, long minkeys)
{
	struct wr w;
	int i, n, ntli, n2li, per, k, ok;
	long *eb, total, first;
	int *ep, *use;
	char *tmp;
	FILE *f;

	/* every record first: the old file is still needed */
	use = (int *)xrealloc(NULL, (v->nent + 1) * sizeof(int));
	n = 0;
	for (i = 0; i < v->nent; i++) {
		if (dropdeleted && (v->ent[i].flags & VMF_DELETED))
			continue;
		vmf_entdata(v, i, NULL);
		use[n++] = i;
	}
	/* the index: as large as before (LEX knows its size from the
	   header), larger only if the keys do not fit in it */
	n2li = v->n2li > 0 ? v->n2li : 28;
	if (minkeys > 0 && (minkeys + ENTMAX - 1) / ENTMAX > n2li)
		n2li = (int)((minkeys + ENTMAX - 1) / ENTMAX);
	while ((long)n2li * (ENTMAX - 1) < n)
		n2li++;
	per = (n + n2li - 1) / n2li;
	if (per < 1)
		per = 1;
	if (per < ENTMAX / 2 && n > (long)n2li * (ENTMAX / 2) / 2)
		per = ENTMAX / 2;
	ntli = (n2li - 1 + TLIKEYS - 1) / TLIKEYS;
	if (ntli < 1)
		ntli = 1;
	if (v->ntli > ntli && (v->ntli - 1) * TLIKEYS >= n2li - 1)
		ntli = v->ntli;
	first = 1 + ntli + n2li;
	memset(&w, 0, sizeof w);
	w.blk = first;
	w.off = 3;
	wblock(&w, first);
	eb = (long *)xrealloc(NULL, (n + 1) * sizeof(long));
	ep = (int *)xrealloc(NULL, (n + 1) * sizeof(int));
	for (k = 0; k < n; k++) {
		struct vmfent *e = &v->ent[use[k]];

		writerec(&w, e, e->data, e->len, &eb[k], &ep[k]);
	}
	total = w.blk + 1;
	if (total < minblocks)
		total = minblocks;
	if (total < v->nblocks)
		total = v->nblocks;
	wblock(&w, total);
	/* header */
	memset(w.buf, 0, BLK);
	memcpy(w.buf, v->hdr, 32);
	w.buf[12] = (unsigned char)(first >> 8);
	w.buf[13] = (unsigned char)first;
	/* the second-level index */
	for (i = 0; i < n2li; i++) {
		unsigned char *x = wblock(&w, 1 + ntli + i);
		long b = 1 + ntli + i;

		memset(x, 0xff, BLK);
		x[1] = 0;
		x[2] = (unsigned char)((i + 1 < n2li ? b + 1 : 0) >> 8);
		x[3] = (unsigned char)(i + 1 < n2li ? b + 1 : 0);
		x[4] = (unsigned char)((i ? b - 1 : 0) >> 8);
		x[5] = (unsigned char)(i ? b - 1 : 0);
	}
	for (k = 0; k < n; k++) {
		struct vmfent *e = &v->ent[use[k]];
		int bi = k / per, j = k % per;
		unsigned char *x;

		if (bi >= n2li) {
			bi = n2li - 1;
			j = k - bi * per;
		}
		x = wblock(&w, 1 + ntli + bi) + 6 + ENTLEN * j;
		memcpy(x, e->key, VMF_KEYLEN);
		setptr(x + 10, eb[k], ep[k]);
		x[13] = (unsigned char)(e->flags & VMF_DELETED);
		e->blk1 = (unsigned)(eb[k] + 1);
		e->pos = ep[k];
	}
	/* the top level: the highest key of each block but the last */
	for (i = 0; i < ntli; i++) {
		unsigned char *x = wblock(&w, 1 + i);

		memset(x, 0xff, BLK);
		x[BLK - 1] = (unsigned char)(i + 1 < ntli ? 1 : 0);
	}
	for (i = 0; i + 1 < n2li; i++) {
		int last = (i + 1) * per - 1;
		unsigned char *x;

		if (last >= n)
			break;
		x = wblock(&w, 1 + i / TLIKEYS) + 1 + VMF_KEYLEN * (i % TLIKEYS);
		memcpy(x, v->ent[use[last]].key, VMF_KEYLEN);
	}
	xfree(eb);
	xfree(ep);
	/* write it */
	tmp = (char *)xrealloc(NULL, strlen(path) + 5);
	sprintf(tmp, "%s.new", path);
	ok = 0;
	if ((f = fopen(tmp, "wb")) != NULL) {
		ok = fwrite(w.buf, 1, (size_t)(total * BLK), f) == (size_t)(total * BLK);
		if (fclose(f) != 0)
			ok = 0;
		if (ok) {
			remove(path);
			ok = rename(tmp, path) == 0;
		}
		if (!ok)
			remove(tmp);
	}
	xfree(tmp);
	if (!ok) {
		xfree(w.buf);
		xfree(use);
		return -1;
	}
	if (strcmp(path, v->path) == 0) {
		/* the entries now point into the new file */
		int m = 0;

		for (k = 0; k < n; k++)
			v->ent[m++] = v->ent[use[k]];
		for (i = 0; i < v->nent; i++) {
			int kept = 0;

			for (k = 0; k < n; k++)
				if (use[k] == i)
					kept = 1;
			if (!kept)
				xfree(v->ent[i].data);
		}
		v->nent = m;
		xfree(v->file);
		v->file = w.buf;
		v->size = total * BLK;
		v->nblocks = total;
		v->ntli = ntli;
		v->n2li = n2li;
		v->overflow = 0;
	} else
		xfree(w.buf);
	xfree(use);
	return 0;
}

int
vmf_save(struct vmf *v)
{
	if (v->readonly)
		return -1;
	return vmf_write(v, v->path, 0, 0, 0);
}

/* Statistics as /T shows them: data blocks used and free, keys. */
void
vmf_stats(struct vmf *v, long *used, long *freeb, long *keys, long *index, long *ovf, long *total)
{
	long b, first = 1 + v->ntli + v->n2li, u = 0, fr = 0;
	int i;

	for (b = first; b < v->nblocks; b++) {
		unsigned char x = v->file[b * BLK];

		if (x <= 1)
			fr++;
		else
			u++;
	}
	*used = u;
	*freeb = fr;
	*keys = 0;
	for (i = 0; i < v->nent; i++)
		if (!(v->ent[i].flags & VMF_DELETED))
			(*keys)++;
	*index = (long)v->n2li * 39;	/* what LEX-68 shows (1092 for 28 blocks) */
	*ovf = v->overflow;
	*total = v->nblocks;
}
