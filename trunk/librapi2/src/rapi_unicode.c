/* $Id$ */
#include "rapi_unicode.h"
#include <stdlib.h>

#if HAVE_ICONV_H
#include <iconv.h>
#endif

#define RAPI_UNICODE_WIDE   "UCS-2LE"
#define RAPI_UNICODE_ASCII  "iso8859-1"

#define INVALID_ICONV_HANDLE ((iconv_t)(-1))

char* rapi_unicode_to_ascii(const uchar* inbuf)
{
	size_t length = rapi_unicode_string_length(inbuf);
	size_t inbytesleft = length * 2, outbytesleft = length;
	char* outbuf = malloc(outbytesleft+1);
  char* outbuf_iterator = outbuf;
  char* inbuf_iterator = (char*)inbuf;
	size_t result;
	iconv_t cd = INVALID_ICONV_HANDLE;

	if (!inbuf)
		return NULL;
	
  cd = iconv_open(RAPI_UNICODE_ASCII, RAPI_UNICODE_WIDE);
	if (INVALID_ICONV_HANDLE == cd)
		return false;

  result = iconv(cd, &inbuf_iterator, &inbytesleft, &outbuf_iterator, &outbytesleft);
  iconv_close(cd);

  if ((size_t)-1 == result)
	{
		rapi_unicode_free_string(outbuf);
		return NULL;
	}
    
	outbuf[length] = 0;

  return outbuf;
}

uchar* rapi_unicode_from_ascii(const char* inbuf)
{
	size_t length = strlen(inbuf);
	size_t inbytesleft = length, outbytesleft = (length+1)* 2;
	char * inbuf_iterator = (char*)inbuf;
	uchar* outbuf = malloc(outbytesleft+sizeof(uchar));
	uchar* outbuf_iterator = outbuf;
	size_t result;
	iconv_t cd = INVALID_ICONV_HANDLE;

	if (!inbuf)
		return NULL;
	
	cd = iconv_open(RAPI_UNICODE_WIDE, RAPI_UNICODE_WIDE);
	if (INVALID_ICONV_HANDLE == cd)
		return false;

	result = iconv(cd, &inbuf_iterator, &inbytesleft, (char**)&outbuf_iterator, &outbytesleft);
	iconv_close(cd);

	if ((size_t)-1 == result)
	{
		rapi_unicode_free_string(outbuf);
		return NULL;
	}

	outbuf[length] = 0;

	return outbuf;
}

void rapi_unicode_free_string(void* str)
{
	if (str)
		free(str);
}

size_t rapi_unicode_string_length(const uchar* unicode)
{
	unsigned length = 0;

	if (!unicode)
		return 0;
	
	while (*unicode++)
		length++;
	return length;
}


