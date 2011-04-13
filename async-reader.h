#ifndef _ASYNC_READER_H_
#define _ASYNC_READER_H_

/* just an opaque data type */
typedef struct async_reader_t async_reader_t;

async_reader_t * ar_init(const char *file);
void ar_destroy(async_reader_t *ar);

/*
 * ar_getline: read a line (including '\n') or whatever is available
 * returns: line length, or -1 if can't get a complete line (even on eof)
 *          this means the function never returns 0 (works like getline(3))
 */
ssize_t ar_getline(async_reader_t *ar);

/* retrieve pointer to current line (use after ar_getline) */
const char * ar_current_line(async_reader_t *ar);

#endif /* _ASYNC_READER_H_ */

/* vim: set sw=2 sts=2 : */
