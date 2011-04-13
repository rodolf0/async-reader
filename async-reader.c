#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <aio.h>
#include <errno.h>

//#include <stdio.h>

#include "async-reader.h"

#define N_IO_BUF  10
#define IO_BUF_SZ 4096

struct async_reader_t {
  struct aiocb *ioinfo[N_IO_BUF];
  char *iobuf[N_IO_BUF];

  char *current_line;
  size_t current_line_ln; // valid lenght
  size_t current_line_sz; // malloc'd size

  int fd;
  off_t scanptr;

  /* current chunk info */
  ssize_t cur_buf_sz; // how much data is valid in the iobuf
  size_t cur_buf_ptr; // where we're standing in the iobuf
  ssize_t cur_io_buf; // which is the current iobuf

};

static void ar_read(async_reader_t *ar, int ioidx);
static int ar_sync_next_block(async_reader_t *ar);

/* get a new async reader on a file */
async_reader_t * ar_init(const char *file) {
  async_reader_t *ar = NULL;
  if (!(ar = malloc(sizeof(async_reader_t))))
    return NULL;

  memset(ar, 0, sizeof(async_reader_t));
  ar->fd = -1; // for propper clenaup if anything fails early

  int i;
  for (i = 0; i < N_IO_BUF; i++) {
    if (!(ar->ioinfo[i] = malloc(sizeof(struct aiocb)))) {
      //fprintf(stderr, "Failed to malloc\n");
      ar_destroy(ar);
      return NULL;
    }
    memset(ar->ioinfo[i], 0, sizeof(struct aiocb));

    if (!(ar->iobuf[i] = malloc(sizeof(char) * IO_BUF_SZ))) {
      //fprintf(stderr, "Failed to malloc\n");
      ar_destroy(ar);
      return NULL;
    }
    memset(ar->iobuf[i], 0, sizeof(char) * IO_BUF_SZ);
  }

  ar->current_line_sz = 256;
  ar->current_line_ln = 0;
  if (!(ar->current_line = malloc(sizeof(char) * 256))) {
    //fprintf(stderr, "Failed to malloc\n");
    ar_destroy(ar);
    return NULL;
  }

  ar->cur_io_buf = -1;
  ar->cur_buf_sz = ar->cur_buf_ptr = 0;

  if ((ar->fd = open(file, O_RDONLY | O_LARGEFILE)) == -1) {
    //fprintf(stderr, "Failed to open file\n");
    ar_destroy(ar);
    return NULL;
  }

  /* start reading in chunks */
  for (i = 0; i < N_IO_BUF; i++)
    ar_read(ar, i);

  return ar;
}


/* free everything */
void ar_destroy(async_reader_t *ar) {
  int i;

  if (!ar) return;

  if (ar->fd != -1) {
    for (i = 0; i < N_IO_BUF; i++) {
      struct aiocb *x = ar->ioinfo[i];
      if (x) {
        if (aio_error(x) == EINPROGRESS) {
          aio_cancel(ar->fd, x);
          aio_suspend((const struct aiocb * const *)&x, 1, NULL);
        }
        aio_return(x);
      }
    }
    close(ar->fd);
  }

  if (ar->current_line)
    free(ar->current_line);

  for (i = 0; i < N_IO_BUF; i++) {
    if (ar->iobuf[i])
      free(ar->iobuf[i]);
    if (ar->ioinfo[i])
      free(ar->ioinfo[i]);
  }

  free(ar);
  memset(ar, 0, sizeof(async_reader_t));
}


/* requests an async read. IMPORTANT: ioidx must be free */
static void ar_read(async_reader_t *ar, int ioidx) {
  struct aiocb *x = ar->ioinfo[ioidx];
  memset(x, 0, sizeof(struct aiocb));
  x->aio_fildes = ar->fd;
  x->aio_offset = ar->scanptr;
  x->aio_buf = ar->iobuf[ioidx];
  x->aio_nbytes = IO_BUF_SZ;
  x->aio_sigevent.sigev_notify = SIGEV_NONE;
  aio_read(x);
  ar->scanptr += IO_BUF_SZ;
}


/* ar_sync_next_block: get the next io block or sleep till available
 * returns how many read bytes (0 on EOF) */

static int ar_sync_next_block(async_reader_t *ar) {
  /* the current block is now free, issue a read request */
  if (ar->cur_io_buf != -1) // -1 on start
    ar_read(ar, ar->cur_io_buf);

  /* advance current block pointer */
  ar->cur_io_buf = (ar->cur_io_buf + 1) % N_IO_BUF;
  struct aiocb *x = ar->ioinfo[ar->cur_io_buf];

  /* wait for the aio block */
  if (aio_suspend((const struct aiocb * const *)&x, 1, NULL))
    return -1;

  /* request finished successfully */
  if (aio_error(x) == 0) {
    ar->cur_buf_ptr = 0;
    ar->cur_buf_sz = aio_return(x);
  } else
    return -1;

  return ar->cur_buf_sz;
}


/* ar_getline: read a line (including '\n') or whatever is available
 * returns: line length, or -1 if can't get a complete line (even on eof)
 *          this means the function never returns 0 (works like getline(3))
 */

ssize_t ar_getline(async_reader_t *ar) {
  /* reset current line buffer */
  int found_nl = 0;
  ar->current_line_ln = 0;

  while (!found_nl) {
    /* get next io block if we consumed current one */
    if (ar->cur_buf_ptr == ar->cur_buf_sz)
      if (ar_sync_next_block(ar) < 1)
        return -1; // sync_next_block returned EOF (0) or error (-1)

    /* scan current io block for nl */
    char *bufend = ar->iobuf[ar->cur_io_buf] + ar->cur_buf_sz;
    char *start = ar->iobuf[ar->cur_io_buf] + ar->cur_buf_ptr;
    char *cur = start;

    while (cur < bufend && *cur != '\n') cur++;

    if (cur < bufend) {
      found_nl = 1;
      cur++; // grab '\n' too
    }

    size_t len = cur - start;
    size_t newsz = ar->current_line_ln + len + 1; // '\0'
    /* check and see if we can hold the current info in our buf */
    if (newsz > ar->current_line_sz) {
      char *rr = realloc(ar->current_line, newsz + 32);
      ar->current_line = rr;
      ar->current_line_sz = newsz + 32;
    }
    /* copy to the current line buffer what we've found so far */
    memcpy(ar->current_line + ar->current_line_ln, start, len);
    ar->cur_buf_ptr += len;
    ar->current_line_ln += len;
    ar->current_line[ar->current_line_ln] = '\0';
  }

  return ar->current_line_ln;
}


const char * ar_current_line(async_reader_t *ar) { return ar->current_line; }

/* vim: set sw=2 sts=2 : */
