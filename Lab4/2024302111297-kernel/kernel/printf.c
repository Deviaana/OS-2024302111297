#include <stdarg.h>

#include "types.h"

void consputc(int);

static char digits[] = "0123456789abcdef";

static void
printint(long long value, int base, int sign)
{
  char buf[32];
  int i = 0;
  unsigned long long x;

  if (sign && value < 0)
    x = -(unsigned long long)value;
  else
    x = (unsigned long long)value;

  do {
    buf[i++] = digits[x % base];
    x /= base;
  } while (x != 0);

  if (sign && value < 0)
    consputc('-');

  while (--i >= 0)
    consputc(buf[i]);
}

void
printf(const char *fmt, ...)
{
  va_list ap;

  if (fmt == 0)
    return;

  va_start(ap, fmt);
  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      consputc(*fmt);
      continue;
    }

    fmt++;
    if (*fmt == 0)
      break;

    switch (*fmt) {
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'u':
      printint(va_arg(ap, uint), 10, 0);
      break;
    case 'x':
      printint(va_arg(ap, uint), 16, 0);
      break;
    case 'p':
      printint(va_arg(ap, uint64), 16, 0);
      break;
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (s == 0)
        s = "(null)";
      while (*s)
        consputc(*s++);
      break;
    }
    case 'c':
      consputc(va_arg(ap, int));
      break;
    case '%':
      consputc('%');
      break;
    default:
      consputc('%');
      consputc(*fmt);
      break;
    }
  }
  va_end(ap);
}
