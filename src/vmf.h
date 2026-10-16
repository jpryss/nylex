/*
 * vmf.h - LEX system files (.VMF): keyed records in 512-byte blocks,
 * as the LEX technical manual (chapter 2) describes them.
 */
#ifndef VMF_H
#define VMF_H

#define VMF_KEYLEN	10
#define VMF_DELETED	0x80		/* index entry flag */

struct vmfent {
	char		key[VMF_KEYLEN];	/* blank padded */
	unsigned	blk1;			/* block + 1 of the last record of the chain */
	int		pos;			/* its position in the block */
	int		flags;			/* VMF_DELETED */
	int		lock;
	unsigned char	*data;			/* the record (squashed), once read */
	long		len;
};

struct vmf {
	char		*path;
	unsigned char	*file;			/* the whole file as read */
	long		size;
	struct vmfent	*ent;			/* sorted by key */
	int		nent, maxent;
	int		readonly;
	unsigned char	hdr[32];
	int		ntli, n2li;		/* as found: kept when writing */
	long		nblocks;		/* blocks in the file as found */
	int		overflow;		/* keys found in overflow blocks */
};

struct vmf	*vmf_open(const char *path);
struct vmf	*vmf_new(const char *path);
void		vmf_close(struct vmf *v);
int		vmf_find(struct vmf *v, const char *key, int deleted_too);
int		vmf_seek(struct vmf *v, const char *key);	/* first entry >= key */
unsigned char	*vmf_get(struct vmf *v, const char *key, long *len);
unsigned char	*vmf_entdata(struct vmf *v, int i, long *len);
int		vmf_put(struct vmf *v, const char *key, const unsigned char *data, long len);
int		vmf_delete(struct vmf *v, const char *key);
int		vmf_undelete(struct vmf *v, const char *key);
int		vmf_save(struct vmf *v);
int		vmf_write(struct vmf *v, const char *path, int dropdeleted, long minblocks, long minkeys);
void		vmf_stats(struct vmf *v, long *used, long *freeb, long *keys, long *index, long *ovf, long *total);
void		vmf_mkkey(char *out, const char *name);
void		*xrealloc(void *p, unsigned long n);	/* realloc that takes NULL (DIAB's does not) */
void		xfree(void *p);				/* free that takes NULL */
void		scopy(char *d, const char *s, int n);	/* strncpy that ends the string */

#endif
