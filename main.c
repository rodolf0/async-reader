#include <stdio.h>
#include "async-reader.h"

int main(int argc, char *argv[]) {

  async_reader_t *ar = ar_init(argv[1]);

  while (ar_getline(ar) > 0)
    printf("%s", ar_current_line(ar));

  ar_destroy(ar);

  return 0;
}

/* vim: set sw=2 sts=2 : */
