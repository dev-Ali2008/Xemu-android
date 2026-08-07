#include "iconv.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct iconv_state {
  int passthrough;
};

iconv_t iconv_open(const char* tocode, const char* fromcode) {
  if (!tocode || !fromcode) {
    errno = EINVAL;
    return (iconv_t)-1;
  }

  struct iconv_state* state = (struct iconv_state*)malloc(sizeof(struct iconv_state));
  if (!state) {
    errno = ENOMEM;
    return (iconv_t)-1;
  }

  state->passthrough = (strcmp(tocode, fromcode) == 0);
  return (iconv_t)state;
}

size_t iconv(iconv_t cd, char** inbuf, size_t* inbytesleft, char** outbuf, size_t* outbytesleft) {
  if (cd == (iconv_t)-1) {
    errno = EINVAL;
    return (size_t)-1;
  }

  struct iconv_state* state = (struct iconv_state*)cd;
  if (!state->passthrough) {
    errno = EINVAL;
    return (size_t)-1;
  }

  if (!inbuf || !outbuf || !*inbuf || !*outbuf || !inbytesleft || !outbytesleft) {
    errno = EINVAL;
    return (size_t)-1;
  }

  size_t to_copy = (*inbytesleft < *outbytesleft) ? *inbytesleft : *outbytesleft;
  if (to_copy == 0) {
    return 0;
  }

  memcpy(*outbuf, *inbuf, to_copy);
  *inbuf += to_copy;
  *outbuf += to_copy;
  *inbytesleft -= to_copy;
  *outbytesleft -= to_copy;
  return 0;
}

int iconv_close(iconv_t cd) {
  if (cd == (iconv_t)-1) {
    errno = EINVAL;
    return -1;
  }

  free(cd);
  return 0;
}
