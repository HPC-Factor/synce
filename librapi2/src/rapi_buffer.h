/* $Id$ */
#ifndef __rapi_buffer_h__
#define __rapi_buffer_h__

#include "rapi_internal.h"
#include "rapi_socket.h"

struct _RapiBuffer;
typedef struct _RapiBuffer RapiBuffer;

/**
 * Allocate new buffer
 */
RapiBuffer* rapi_buffer_new();

/**
 * Free the contents of a buffer, but keep the buffer object
 */
void rapi_buffer_free_data(RapiBuffer* buffer);

/**
 * Free an allocated buffer
 */
void rapi_buffer_free(RapiBuffer* buffer);

/**
 * Allocate new buffer with copy of data
 */
bool rapi_buffer_reset(RapiBuffer* buffer, unsigned char* data, size_t size);

/**
 * Get size of data in buffer
 */
size_t rapi_buffer_get_size(RapiBuffer* buffer);

/**
 * Get raw data access in buffer
 */
unsigned char* rapi_buffer_get_raw(RapiBuffer* buffer);

/**
 * Append raw data to buffer
 */
bool rapi_buffer_write_data(RapiBuffer* buffer, const void* data, size_t size);

/**
 * Append a WORD parameter to buffer, with adjustment for endianness
 */
bool rapi_buffer_write_uint16(RapiBuffer* buffer, uint16_t value);

/**
 * Append a DWORD parameter to buffer, with adjustment for endianness
 */
bool rapi_buffer_write_uint32(RapiBuffer* buffer, uint32_t value);

/**
 * Write a string with length
 */
bool rapi_buffer_write_string(RapiBuffer* buffer, LPCWSTR unicode);

/**
 * Write an optional string with length
 */
bool rapi_buffer_write_optional_string(RapiBuffer* buffer, LPCWSTR unicode);

/*
 * Write an optional DWORD parameter, with adjustment for endianness
 */
bool rapi_buffer_write_optional_uint32(RapiBuffer* buffer, uint32_t* data, bool send_data);

/**
 * Write an optional parameter
 */
bool rapi_buffer_write_optional(RapiBuffer* buffer, void* data, size_t size, bool send_data);

/**
 * Write an optional input parameter by reference
 */
bool rapi_buffer_write_optional_in(RapiBuffer* buffer, const void* data, size_t size);

/**
 * Write an optional output parameter by reference
 */
bool rapi_buffer_write_optional_out(RapiBuffer* buffer, void* data, size_t size);

/**
 * Write an optional input/output parameter by reference
 */
bool rapi_buffer_write_optional_inout(RapiBuffer* buffer, void* data, size_t size);

/**
 * Write an optional parameter by reference with no size information
 */
bool rapi_buffer_write_optional_no_size(RapiBuffer* buffer, const void* data, size_t size);

/**
 * Get raw data from buffer
 */
bool rapi_buffer_read_data(RapiBuffer* buffer, void* data, size_t size);

/**
 * Get a WORD parameter from buffer, with adjustment for endianness
 */
bool rapi_buffer_read_uint16(RapiBuffer* buffer, uint16_t* value);

/**
 * Get a DWORD parameter from buffer, with adjustment for endianness
 */
bool rapi_buffer_read_uint32(RapiBuffer* buffer, uint32_t* value);

/**
 * Get string with length in number of wide chars
 */
bool rapi_buffer_read_string(RapiBuffer* buffer, LPWSTR unicode, size_t* size);

/**
 * Read an optional parameter
 */
bool rapi_buffer_read_optional(RapiBuffer* buffer, void* data, size_t max_size);

/**
 * Read an optional DWORD parameter
 */
bool rapi_buffer_read_optional_uint32(RapiBuffer* buffer, uint32_t* value);

/**
 * Read an optional FILETIME parameter
 */
bool rapi_buffer_read_optional_filetime(RapiBuffer* buffer, FILETIME* lpftLastWriteTime);

/**
 * Send a buffer on the socket
 */
bool rapi_buffer_send(RapiBuffer* buffer, RapiSocket* socket);

/**
 * Receive a buffer on the socket
 */
bool rapi_buffer_recv(RapiBuffer* buffer, RapiSocket* socket);


#endif

