/* $Id: password.c 2355 2006-04-07 18:47:20Z voc $ */
#undef __STRICT_ANSI__
#define _GNU_SOURCE
#include "rapi_internal.h"
#include "rapi2_api.h"
#include "rapi_buffer.h"
#include "rapi_context.h"
#include <stdlib.h>


BOOL _NotImplementedCeCheckPassword2( /*{{{*/
		LPWSTR lpszPassword)
{
  RapiContext* context = rapi_context_current();
  context->rapi_error = E_NOTIMPL;
  context->last_error = ERROR_SUCCESS;
  return 0;
}/*}}}*/

