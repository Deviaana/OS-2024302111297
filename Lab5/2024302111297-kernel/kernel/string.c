#include "types.h"
#include "defs.h"

void *
memset(void *dst, int c, uint n)
{
  char *d = (char *)dst;

  while (n-- > 0)
    *d++ = (char)c;
  return dst;
}

void *
memmove(void *dst, const void *src, int n)
{
  char *d = (char *)dst;
  const char *s = (const char *)src;

  if (s > d) {
    while (n-- > 0)
      *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n-- > 0)
      *--d = *--s;
  }
  return dst;
}

int
strlen(const char *s)
{
  int n;

  for (n = 0; s[n]; n++)
    ;
  return n;
}

int
strcmp(const char *p, const char *q)
{
  while (*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}