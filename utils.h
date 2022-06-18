#ifndef MY_UTILS
#define MY_UTILS
#include <stdio.h>
#include <stdlib.h>

size_t getline(char **lineptr, size_t *n, FILE *stream)
{
  size_t pos = 0;
  int c;

  if (lineptr == NULL || stream == NULL || n == NULL)
  {
    errno = EINVAL;
    return -1;
  }

  c = getc(stream);
  if (c == EOF)
  {
    return -1;
  }
  if (*lineptr == NULL)
  {
    *lineptr = (char *)malloc(128);
    if (*lineptr == NULL)
    {
      return -1;
    }
    *n = 128;
  }
  pos = 0;
  while (c != EOF)
  {
    if ((pos + 1) > *n)
    {
      size_t new_size = *n + (*n >> 2);
      if (new_size < 128)
      {
        new_size = 128;
      }
      char *new_ptr = (char *)realloc(*lineptr, new_size);
      if (new_ptr == NULL)
      {
        return -1;
      }
      *n = new_size;
      *lineptr = new_ptr;
    }

    (*lineptr)[pos++] = c;
    if (c == '\n')
    {
      break;
    }
    c = getc(stream);
  }
  (*lineptr)[pos] = '\0';
  return pos;
}
#endif