/* $Id: rapi_api.c 3812 2009-07-19 18:54:50Z mark_ellis $ */
#undef __STRICT_ANSI__
#define _GNU_SOURCE
#if HAVE_CONFIG_H
#include "config.h"
#endif

 
/** 
 * @defgroup RAPI2 RAPI2 public API
 * @ingroup RAPI 
 * @brief The public RAPI2 API
 *
 * @{ 
 */ 
 

#include "rapi_ops.h"
#include "rapi_context.h"
#include "rapi2.h"

#include <string.h>
#include <stdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#define DBUS_SERVICE "org.freedesktop.DBus"
#define DBUS_IFACE   "org.freedesktop.DBus"
#define DBUS_PATH    "/org/freedesktop/DBus"

#if ENABLE_UDEV_SUPPORT
#define DCCM_SERVICE   "org.synce.dccm"
#define DCCM_MGR_PATH  "/org/synce/dccm/DeviceManager"
#define DCCM_MGR_IFACE "org.synce.dccm.DeviceManager"
#define DCCM_DEV_IFACE "org.synce.dccm.Device"
#endif

/* we only need one instance of IRAPIDesktop */
static IRAPIDesktop *irapi_desktop = NULL;



/*
 * utility functions
 */

static bool
guid_from_string(const char *str, GUID *guid)
{
  int matches = 0;
  matches = sscanf(str, "{%08X-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}",
		   &guid->Data1, &guid->Data2, &guid->Data3,
		   &guid->Data4[0], &guid->Data4[1], &guid->Data4[2], &guid->Data4[3],
		   &guid->Data4[4], &guid->Data4[5], &guid->Data4[6], &guid->Data4[7]);

  if (matches == 11)
    return TRUE;

  return FALSE;
}

static char *
guid_to_string(GUID *guid)
{
  char *guid_str = malloc(sizeof(char) * 39);
  int len;
  len = snprintf(guid_str, 39, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		 guid->Data1, guid->Data2, guid->Data3,
		 guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		 guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);

  if (len != 38) {
    free(guid_str);
    return NULL;
  }

  return guid_str;
}


/*
 * IRAPISession
 */

/**
 * @defgroup IRAPISession IRAPISession public API
 * @ingroup RAPI2 
 * 
 * An object representing a command session to a device.
 *
 *@{ 
 */

/** @typedef struct _IRAPISession IRAPISession
 * @brief A command session to a device
 * 
 * This is an opaque structure, representing a command session
 * to a connected device.
 * It's contents should be accessed via the IRAPISession_*
 * series of functions.
 * 
 */ 
struct _IRAPISession {
        IRAPIDevice *device;
        RapiContext *context;
        int refcount;
};


/** @brief Add a reference to the IRAPISession object
 * 
 * This function increases the reference count of the IRAPISession
 * object by one.
 * 
 * @param[in] self address of the object of which to increase the ref count
 */ 
void
IRAPISession_AddRef(IRAPISession *session)
{
        session->refcount++;
        return;
}

/** @brief Remove a reference to the IRAPISession object
 * 
 * This function decreases the reference count of the IRAPISession
 * object by one.
 * 
 * @param[in] self address of the object of which to decrease the ref count
 */ 
void
IRAPISession_Release(IRAPISession *session)
{
        session->refcount--;
        if (session->refcount > 0)
                return;

        IRAPIDevice_Release(session->device);
        rapi_context_disconnect(session->context);
        rapi_context_unref(session->context);
        free(session);
        return;
}

/** @brief Obtain a reference to the session's device object
 * 
 * Obtain a reference to the IRAPIDevice object representing the
 * device the session is connected to.
 * 
 * @param[in] self address of the session object
 * @return a reference to the device object
 */ 
IRAPIDevice *
IRAPISession_get_device(IRAPISession *session)
{
        IRAPIDevice_AddRef(session->device);
        return session->device;
}

/*
 * Standard rapi-calls valid for PocketPC 2002/2003 and Windows Mobile 5
 */

/** @brief Free a buffer
 * 
 * Free a buffer returned by a previous function call
 * 
 * @param[in] self address of the session object
 * @param[in] self address of the buffer to free
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPISession_CeRapiFreeBuffer(IRAPISession *session SYNCE_UNUSED,
                              LPVOID Buffer)
{
        free(Buffer);
        return S_OK;
}

/** @brief Initialise connection
 * 
 * Initialise the connection to a device using the
 * specified Session
 * 
 * @param[in] self address of the session object
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPISession_CeRapiInit(IRAPISession *session)
{
  return rapi_context_connect(session->context);
}

/** @brief Close connection
 * 
 * Close the connection to a device using the
 * specified Session. Note this does not disconnect
 * the device, merely closes the session's connection
 * 
 * @param[in] self address of the session object
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPISession_CeRapiUninit(IRAPISession *session)
{
  return rapi_context_disconnect(session->context);
}

/** @brief Get RAPI call error
 * 
 * Get the last RAPI error caused by a call to the device
 * using the specified Session. This should be used in
 * conjunction with IRAPISession_CeGetLastError for errors
 * that are not RAPI specific.
 * 
 * @param[in] self address of the session object
 * @return an HRESULT indicating the error
 */ 
HRESULT
IRAPISession_CeRapiGetError(IRAPISession *session)
{
  return session->context->rapi_error;
}

/** @brief Get last call error
 * 
 * Get the last system (non-RAPI) error caused by a call to the device
 * using the specified Session. This should be used in
 * conjunction with IRAPISession_CeRapiGetError for errors
 * that are RAPI specific.
 * 
 * @param[in] self address of the session object
 * @return a DWORD indicating the error
 */ 
DWORD
IRAPISession_CeGetLastError(IRAPISession *session)
{
  return session->context->last_error;
}


/*
 * Implementation of calls that differ on WM5 and pre-WM5
 * devices, requires indirect calls to the correct function
 */

#ifndef SWIG

/** @brief Close a file handle on device
 *
 * Closes a previously opened handle on a device. Handles closed by
 * this function include files, databases, processes and sockets, but
 * not Find handles.
 * 
 * @param[in] session address of the session object
 * @param[in] hObject handle to close
 * @return TRUE on success, FALSE on failure
 */ 
BOOL
IRAPISession_CeCloseHandle(IRAPISession *session,
                           HANDLE hObject)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeCloseHandle ) ( context, hObject );
}

/** @brief Open a file on device
 *
 * Creates, opens, or truncates a file, pipe, communications
 * resource, disk device, or console on a device.
 * 
 * dwCreationDisposition determines what action is taken if the file
 * does or does not exist, and must be one of the following:
 *  - CREATE_NEW - creates the file, fails if it already exists
 *  - CREATE_ALWAYS - creates the file, overwrites it if it exists and clears any attributes
 *  - OPEN_EXISTING - fails if the file does not exist
 *  - OPEN_ALWAYS - opens an existing file, otherwise creates it
 *  - TRUNCATE_EXISTING - opens the file and truncates it to zero, fails if it doesn't exist
 *
 * @param[in] session address of the session object
 * @param[in] lpFileName name of the file or object
 * @param[in] dwDesiredAccess access required, GENERIC_READ and/or GENERIC_WRITE
 * @param[in] dwShareMode 0 for unshared, or FILE_SHARE_READ or FILE_SHARE_WRITE
 * @param[in] lpSecurityAttributes ignored, set to NULL
 * @param[in] dwCreationDisposition action to take when files exist or don't exist
 * @param[in] dwFlagsAndAttributes any combination of FILE_ATTRIBUTE_ flags
 * @param[in] hTemplateFile ignored
 * @return a valid HANDLE on success, or INVALID_HANDLE_VALUE on failure
 */ 
HANDLE
IRAPISession_CeCreateFile(IRAPISession *session,
                          LPCWSTR lpFileName,
                          DWORD dwDesiredAccess,
                          DWORD dwShareMode,
                          LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                          DWORD dwCreationDisposition,
                          DWORD dwFlagsAndAttributes,
                          HANDLE hTemplateFile )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return INVALID_HANDLE_VALUE;
        }

        return ( *context->rapi_ops->CeCreateFile ) (context,
                                                     lpFileName,
                                                     dwDesiredAccess,
                                                     dwShareMode,
                                                     lpSecurityAttributes,
                                                     dwCreationDisposition,
                                                     dwFlagsAndAttributes,
                                                     hTemplateFile );
}


/** @brief Read from a file
 *
 * Read from a file at the current position of the file pointer,
 * which is moved by the read operation.
 * 
 * @param[in] session address of the session object
 * @param[in] hFile handle to the file
 * @param[out] lpBuffer address of the buffer to receive the data
 * @param[in] nNumberOfBytesToRead bytes to read from the file
 * @param[out] lpNumberOfBytesRead address of a location to receive the number of bytes read
 * @param[in] lpOverlapped unsupported, set to NULL
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeReadFile(IRAPISession *session,
                        HANDLE hFile,
                        LPVOID lpBuffer,
                        DWORD nNumberOfBytesToRead,
                        LPDWORD lpNumberOfBytesRead,
                        LPOVERLAPPED lpOverlapped )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeReadFile ) (context,
                                                   hFile,
                                                   lpBuffer,
                                                   nNumberOfBytesToRead,
                                                   lpNumberOfBytesRead,
                                                   lpOverlapped );
}


/** @brief Write to a file
 *
 * Write to a file at the current position of the file pointer,
 * which is moved by the write operation.
 * 
 * @param[in] session address of the session object
 * @param[in] hFile handle to the file
 * @param[in] lpBuffer address of the buffer containing the data
 * @param[in] nNumberOfBytesToWrite bytes to write to the file
 * @param[out] lpNumberOfBytesWritten address of a location to receive the number of bytes written
 * @param[in] lpOverlapped unsupported, set to NULL
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeWriteFile(IRAPISession *session,
                         HANDLE hFile,
                         LPCVOID lpBuffer,
                         DWORD nNumberOfBytesToWrite,
                         LPDWORD lpNumberOfBytesWritten,
                         LPOVERLAPPED lpOverlapped )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeWriteFile ) (context,
                                                    hFile,
                                                    lpBuffer,
                                                    nNumberOfBytesToWrite,
                                                    lpNumberOfBytesWritten,
                                                    lpOverlapped );
}


DWORD
IRAPISession_CeSetFilePointer(IRAPISession *session,
                              HANDLE hFile,
                              LONG lDistanceToMove,
                              PLONG lpDistanceToMoveHigh,
                              DWORD dwMoveMethod )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0xFFFFFFFF;
        }

        return ( *context->rapi_ops->CeSetFilePointer ) (
                                                         context,
                                                         hFile,
                                                         lDistanceToMove,
                                                         lpDistanceToMoveHigh,
                                                         dwMoveMethod );
}

/** @brief Set end of file
 *
 * Set the end of file at the current file pointer position.
 * If this is before the existing end of file, the file is truncated.
 * If it is after the existing position, the file is extended, and the
 * intervening contents are undefined.
 * 
 * The file handle must have been created with GENERIC_WRITE.
 * 
 * @param[in] session address of the session object
 * @param[in] hFile handle to the file
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeSetEndOfFile(IRAPISession *session,
                            HANDLE hFile)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeSetEndOfFile ) ( context, hFile );
}

/** @brief Get file time stamps
 *
 * Get creation time, last access time, and last modified time
 * for an open file.
 * 
 * The file handle must have been created with GENERIC_READ.
 * 
 * @param[in] session address of the session object
 * @param[in] hFile handle to the file
 * @param[out] lpCreationTime location to receive creation time, can be NULL
 * @param[out] lpLastAccessTime location to receive last access time, can be NULL
 * @param[out] lpLastWriteTime location to receive last modified time, can be NULL
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeGetFileTime(IRAPISession *session,
                           HANDLE hFile,
                           LPFILETIME lpCreationTime,
                           LPFILETIME lpLastAccessTime,
                           LPFILETIME lpLastWriteTime )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeGetFileTime ) (context,
                                                      hFile,
                                                      lpCreationTime,
                                                      lpLastAccessTime,
                                                      lpLastWriteTime );
}

/** @brief Set file time stamps
 *
 * Set creation time, last access time, and last modified time
 * for an open file.
 * 
 * The file handle must have been created with GENERIC_WRITE.
 * 
 * @param[in] session address of the session object
 * @param[in] hFile handle to the file
 * @param[out] lpCreationTime creation time to set, can be NULL
 * @param[out] lpLastAccessTime last access time to set, can be NULL
 * @param[out] lpLastWriteTime last modified time to set, can be NULL
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeSetFileTime(IRAPISession *session,
                           HANDLE hFile,
                           LPFILETIME lpCreationTime,
                           LPFILETIME lpLastAccessTime,
                           LPFILETIME lpLastWriteTime )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeSetFileTime ) (context,
                                                      hFile,
                                                      lpCreationTime,
                                                      lpLastAccessTime,
                                                      lpLastWriteTime );
}

#endif /* SWIG */


/*
* File management functions
*/

#ifndef SWIG

/** @brief Copy a remote file
 *
 * Copy an existing file on the device to a new file.
 * 
 * @param[in] session address of the session object
 * @param[in] lpExistingFileName name of the existing file
 * @param[in] lpNewFileName name of the new file
 * @param[in] bfailIfExists if new file already exists, overwrite if FALSE, or fail if TRUE
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeCopyFile(IRAPISession *session,
                        LPCWSTR lpExistingFileName,
                        LPCWSTR lpNewFileName,
                        BOOL bFailIfExists )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeCopyFile ) (context,
                                                   lpExistingFileName,
                                                   lpNewFileName,
                                                   bFailIfExists );
}


/** @brief Create a remote directory
 *
 * Create a directory on the device.
 * 
 * @param[in] session address of the session object
 * @param[in] lpPathName name of the directory to create
 * @param[in] lpSecurityAttributes ignored, set to NULL
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeCreateDirectory(IRAPISession *session,
                               LPCWSTR lpPathName,
                               LPSECURITY_ATTRIBUTES lpSecurityAttributes )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeCreateDirectory ) (context,
                                                          lpPathName,
                                                          lpSecurityAttributes );
}


/** @brief Delete a remote file
 *
 * Delete a file on the device.
 *
 * This will fail if the file does not exist, or if the
 * file is open.
 * 
 * @param[in] session address of the session object
 * @param[in] lpFileName name of the file to delete
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeDeleteFile(IRAPISession *session,
                          LPCWSTR lpFileName )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeDeleteFile ) (context,
                                                     lpFileName );
}


/** @brief Get contents of a directory
 *
 * Retrieve information about the contents, files and directories
 * of the given directory.
 *
 * The dwFlags parameter is a combination of flags to determine the type
 * of items to search for (filter flags), and what information to retrieve
 * about them (retrieval flags).
 * 
 * Filter flags
 * - FAF_ATTRIB_CHILDREN - ignore directories with no child items
 * - FAF_ATTRIB_NO_HIDDEN - ignore entries that are hidden
 * - FAF_FOLDERS_ONLY - only directories, no files
 * - FAF_NO_HIDDEN_SYS_ROMMODULES - ignore ROM files and directories
 *
 * Retrieval flags
 * - FAF_ATTRIBUTES - retrieve file attributes
 * - FAF_CREATION_TIME - retrieve creation time
 * - FAF_LASTACCESS_TIME - retrieve last access time
 * - FAF_LASTWRITE_TIME - retrieve last modified time
 * - FAF_SIZE_HIGH - retrieve high order DWORD of the file size
 * - FAF_SIZE_LOW - retrieve low order DWORD of the file size
 * - FAF_OID - retrieve the object ID
 * - FAF_NAME - retrieve the file name
 *
 * @param[in] session address of the session object
 * @param[in] szPath path to search for files
 * @param[in] dwFlags a combination of filter and retrieval flags
 * @param[out] lpdwFoundCount location to store number of found items
 * @param[out] ppFindDataArray pointer to the pointer to store the returned array of CE_FIND_DATA
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeFindAllFiles(IRAPISession *session,
                            LPCWSTR szPath,
                            DWORD dwFlags,
                            LPDWORD lpdwFoundCount,
                            LPLPCE_FIND_DATA ppFindDataArray )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeFindAllFiles ) (context,
                                                       szPath,
                                                       dwFlags,
                                                       lpdwFoundCount,
                                                       ppFindDataArray );
}


/** @brief Search for a file or directory
 *
 * Retrieve information about the specified file or directory.
 *
 * @param[in] session address of the session object
 * @param[in] lpFileName directory or file path to search for, which can contain wildcards
 * @param[out] lpFindFileData location of a CE_FIND_DATA to store the file information
 * @return a valid search HANDLE, or INVALID_HANDLE_VALUE on failure
 */ 
HANDLE
IRAPISession_CeFindFirstFile(IRAPISession *session,
                             LPCWSTR lpFileName,
                             LPCE_FIND_DATA lpFindFileData )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return INVALID_HANDLE_VALUE;
        }

        return ( *context->rapi_ops->CeFindFirstFile ) (context,
                                                        lpFileName,
                                                        lpFindFileData );
}


/** @brief Search for a subsequent file or directory
 *
 * Retrieve information about the next file or directory
 * in a search context initiated by IRAPISession_CeFindFirstFile().
 *
 * @param[in] session address of the session object
 * @param[in] hFindFile search handle returned from IRAPISession_CeFindFirstFile()
 * @param[out] lpFindFileData location of a CE_FIND_DATA to store the file information
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeFindNextFile(IRAPISession *session,
                            HANDLE hFindFile,
                            LPCE_FIND_DATA lpFindFileData )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeFindNextFile ) (context,
                                                       hFindFile,
                                                       lpFindFileData );
}


/** @brief Close a search handle
 *
 * Closes a search handle previously opened by IRAPISession_CeFindFirstFile().
 *
 * @param[in] session address of the session object
 * @param[in] hFindFile search handle returned from IRAPISession_CeFindFirstFile()
 * @return non-zero on success, zero on failure
 */ 
BOOL
IRAPISession_CeFindClose(IRAPISession *session,
                         HANDLE hFindFile )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeFindClose ) ( context, hFindFile );
}


/** @brief Get file attributes of a file or directory
 *
 * Retrieve the file attributes of the specified file or directory.
 *
 * @param[in] session address of the session object
 * @param[in] lpFileName directory or file path
 * @return DWORD value of the file attributes FILE_ATTRIBUTE_*, or 0xFFFFFFFF on failure
 */ 
DWORD
IRAPISession_CeGetFileAttributes(IRAPISession *session,
                                 LPCWSTR lpFileName )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0xFFFFFFFF;
        }

        return ( *context->rapi_ops->CeGetFileAttributes ) ( context, lpFileName );
}


DWORD
IRAPISession_CeGetFileSize(IRAPISession *session,
                           HANDLE hFile,
                           LPDWORD lpFileSizeHigh )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return INVALID_FILE_SIZE;
        }

        return ( *context->rapi_ops->CeGetFileSize ) (context,
                                                      hFile,
                                                      lpFileSizeHigh );
}


DWORD
IRAPISession_CeGetSpecialFolderPath(IRAPISession *session,
                                    int nFolder,
                                    DWORD nBufferLength,
                                    LPWSTR lpBuffer )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0;
        }

        return ( *context->rapi_ops->CeGetSpecialFolderPath ) (context,
                                                               nFolder,
                                                               nBufferLength,
                                                               lpBuffer );
}


BOOL
IRAPISession_CeMoveFile(IRAPISession *session,
                        LPCWSTR lpExistingFileName,
                        LPCWSTR lpNewFileName )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeMoveFile ) (context,
                                                   lpExistingFileName,
                                                   lpNewFileName );
}


BOOL
IRAPISession_CeRemoveDirectory(IRAPISession *session,
                               LPCWSTR lpPathName )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeRemoveDirectory ) ( context, lpPathName );
}


BOOL
IRAPISession_CeSetFileAttributes(IRAPISession *session,
                                 LPCWSTR lpFileName,
                                 DWORD dwFileAttributes )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeSetFileAttributes ) (context,
                                                            lpFileName,
                                                            dwFileAttributes );
}


BOOL
IRAPISession_CeSHCreateShortcut(IRAPISession *session,
                                LPCWSTR lpszShortcut,
                                LPCWSTR lpszTarget )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeSHCreateShortcut ) (context,
                                                           lpszShortcut,
                                                           lpszTarget );
}


BOOL
IRAPISession_CeSyncTimeToPc(IRAPISession *session)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeSyncTimeToPc ) ( context );
}


#endif /* SWIG */


/*
* Database functions
*/

#ifndef SWIG

CEOID
IRAPISession_CeCreateDatabase(IRAPISession *session,
                              LPWSTR lpszName,
                              DWORD dwDbaseType,
                              WORD wNumSortOrder,
                              SORTORDERSPEC *rgSortSpecs )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0;
        }

        return ( *context->rapi_ops->CeCreateDatabase ) (context,
                                                         lpszName,
                                                         dwDbaseType,
                                                         wNumSortOrder,
                                                         rgSortSpecs );
}


BOOL
IRAPISession_CeDeleteDatabase(IRAPISession *session, CEOID oid )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeDeleteDatabase ) ( context, oid );
}


BOOL
IRAPISession_CeFindAllDatabases(IRAPISession *session,
                                DWORD dwDbaseType,
                                WORD wFlags,
                                LPWORD cFindData,
                                LPLPCEDB_FIND_DATA ppFindData )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeFindAllDatabases ) (context,
                                                           dwDbaseType,
                                                           wFlags,
                                                           cFindData,
                                                           ppFindData );
}


HANDLE
IRAPISession_CeFindFirstDatabase(IRAPISession *session,
                                 DWORD dwDbaseType )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return INVALID_HANDLE_VALUE;
        }

        return ( *context->rapi_ops->CeFindFirstDatabase ) (context,
                                                            dwDbaseType );
}


CEOID
IRAPISession_CeFindNextDatabase(IRAPISession *session,
                                HANDLE hEnum )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0;
        }

        return ( *context->rapi_ops->CeFindNextDatabase ) (context,
                                                           hEnum );
}


HANDLE
IRAPISession_CeOpenDatabase(IRAPISession *session,
                            PCEOID poid,
                            LPWSTR lpszName,
                            CEPROPID propid,
                            DWORD dwFlags,
                            HWND hwndNotify )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return INVALID_HANDLE_VALUE;
        }

        return ( *context->rapi_ops->CeOpenDatabase ) (context,
                                                       poid,
                                                       lpszName,
                                                       propid,
                                                       dwFlags,
                                                       hwndNotify );
}


CEOID
IRAPISession_CeReadRecordProps(IRAPISession *session,
                               HANDLE hDbase,
                               DWORD dwFlags,
                               LPWORD lpcPropID,
                               CEPROPID *rgPropID,
                               LPBYTE *lplpBuffer,
                               LPDWORD lpcbBuffer )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0;
        }

        return ( *context->rapi_ops->CeReadRecordProps ) (context,
                                                          hDbase,
                                                          dwFlags,
                                                          lpcPropID,
                                                          rgPropID,
                                                          lplpBuffer,
                                                          lpcbBuffer );
}


CEOID
IRAPISession_CeSeekDatabase(IRAPISession *session,
                            HANDLE hDatabase,
                            DWORD dwSeekType,
                            DWORD dwValue,
                            LPDWORD lpdwIndex )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0;
        }

        return ( *context->rapi_ops->CeSeekDatabase ) (context,
                                                       hDatabase,
                                                       dwSeekType,
                                                       dwValue,
                                                       lpdwIndex );
}


CEOID
IRAPISession_CeWriteRecordProps(IRAPISession *session,
                                HANDLE hDbase,
                                CEOID oidRecord,
                                WORD cPropID,
                                CEPROPVAL *rgPropVal )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 0;
        }

        return ( *context->rapi_ops->CeWriteRecordProps ) (context,
                                                           hDbase,
                                                           oidRecord,
                                                           cPropID,
                                                           rgPropVal );
}


BOOL
IRAPISession_CeDeleteRecord(IRAPISession *session,
                            HANDLE hDatabase,
                            CEOID oidRecord )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeDeleteRecord ) (context,
                                                       hDatabase, oidRecord );
}


BOOL
IRAPISession_CeSetDatabaseInfo(IRAPISession *session,
                               CEOID oidDbase,
                               CEDBASEINFO* pNewInfo )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeSetDatabaseInfo ) (context,
                                                          oidDbase,
                                                          pNewInfo );
}


#endif /* SWIG */

/*
* Registry
*/

#ifndef SWIG

LONG
IRAPISession_CeRegCreateKeyEx(IRAPISession *session,
                              HKEY hKey,
                              LPCWSTR lpszSubKey,
                              DWORD Reserved,
                              LPWSTR lpszClass,
                              DWORD ulOptions,
                              REGSAM samDesired,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                              PHKEY phkResult,
                              LPDWORD lpdwDisposition )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegCreateKeyEx ) (context,
                                                         hKey,
                                                         lpszSubKey,
                                                         Reserved,
                                                         lpszClass,
                                                         ulOptions,
                                                         samDesired,
                                                         lpSecurityAttributes,
                                                         phkResult,
                                                         lpdwDisposition );
}


LONG
IRAPISession_CeRegOpenKeyEx(IRAPISession *session,
                            HKEY hKey,
                            LPCWSTR lpszSubKey,
                            DWORD ulOptions,
                            REGSAM samDesired,
                            PHKEY phkResult )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegOpenKeyEx ) (context,
                                                       hKey,
                                                       lpszSubKey,
                                                       ulOptions,
                                                       samDesired,
                                                       phkResult );
}


LONG
IRAPISession_CeRegCloseKey(IRAPISession *session,
                           HKEY hKey )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegCloseKey ) (context,
                                                      hKey );
}


LONG
IRAPISession_CeRegDeleteKey(IRAPISession *session,
                            HKEY hKey,
                            LPCWSTR lpszSubKey )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegDeleteKey ) (context,
                                                       hKey,
                                                       lpszSubKey);
}


LONG
IRAPISession_CeRegDeleteValue(IRAPISession *session,
                              HKEY hKey,
                              LPCWSTR lpszValueName )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegDeleteValue ) (context,
                                                         hKey,
                                                         lpszValueName);
}


DWORD
IRAPISession_CeGetDiskFreeSpaceEx(IRAPISession *session,
                                  LPCTSTR _lpDirectoryName, 
                                  PULARGE_INTEGER lpFreeBytesAvailable, 
                                  PULARGE_INTEGER lpTotalNumberOfBytes, 
                                  PULARGE_INTEGER lpTotalNumberOfFreeBytes)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeGetDiskFreeSpaceEx ) (context,
                                                             _lpDirectoryName,
                                                             lpFreeBytesAvailable,
                                                             lpTotalNumberOfBytes,
                                                             lpTotalNumberOfFreeBytes);
}


LONG
IRAPISession_CeRegQueryInfoKey(IRAPISession *session,
                               HKEY hKey,
                               LPWSTR lpClass,
                               LPDWORD lpcbClass,
                               LPDWORD lpReserved,
                               LPDWORD lpcSubKeys,
                               LPDWORD lpcbMaxSubKeyLen,
                               LPDWORD lpcbMaxClassLen,
                               LPDWORD lpcValues,
                               LPDWORD lpcbMaxValueNameLen,
                               LPDWORD lpcbMaxValueLen,
                               LPDWORD lpcbSecurityDescriptor,
                               PFILETIME lpftLastWriteTime )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegQueryInfoKey ) (context,
                                                          hKey,
                                                          lpClass,
                                                          lpcbClass,
                                                          lpReserved,
                                                          lpcSubKeys,
                                                          lpcbMaxSubKeyLen,
                                                          lpcbMaxClassLen,
                                                          lpcValues,
                                                          lpcbMaxValueNameLen,
                                                          lpcbMaxValueLen,
                                                          lpcbSecurityDescriptor,
                                                          lpftLastWriteTime );
}


LONG
IRAPISession_CeRegQueryValueEx(IRAPISession *session,
                               HKEY hKey,
                               LPCWSTR lpValueName,
                               LPDWORD lpReserved,
                               LPDWORD lpType,
                               LPBYTE lpData,
                               LPDWORD lpcbData )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegQueryValueEx ) (context,
                                                          hKey,
                                                          lpValueName,
                                                          lpReserved,
                                                          lpType,
                                                          lpData,
                                                          lpcbData );
}


LONG
IRAPISession_CeRegEnumValue(IRAPISession *session,
                            HKEY hKey,
                            DWORD dwIndex,
                            LPWSTR lpszValueName,
                            LPDWORD lpcbValueName,
                            LPDWORD lpReserved,
                            LPDWORD lpType,
                            LPBYTE lpData,
                            LPDWORD lpcbData )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegEnumValue ) (context,
                                                       hKey,
                                                       dwIndex,
                                                       lpszValueName,
                                                       lpcbValueName,
                                                       lpReserved,
                                                       lpType,
                                                       lpData,
                                                       lpcbData );
}


LONG
IRAPISession_CeRegEnumKeyEx(IRAPISession *session,
                            HKEY hKey,
                            DWORD dwIndex,
                            LPWSTR lpName,
                            LPDWORD lpcbName,
                            LPDWORD lpReserved,
                            LPWSTR lpClass,
                            LPDWORD lpcbClass,
                            PFILETIME lpftLastWriteTime )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegEnumKeyEx ) (context,
                                                       hKey,
                                                       dwIndex,
                                                       lpName,
                                                       lpcbName,
                                                       lpReserved,
                                                       lpClass,
                                                       lpcbClass,
                                                       lpftLastWriteTime );
}


LONG
IRAPISession_CeRegSetValueEx(IRAPISession *session,
                             HKEY hKey,
                             LPCWSTR lpValueName,
                             DWORD Reserved,
                             DWORD dwType,
                             const BYTE *lpData,
                             DWORD cbData )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return ERROR_NOT_CONNECTED;
        }

        return ( *context->rapi_ops->CeRegSetValueEx ) (context,
                                                        hKey,
                                                        lpValueName,
                                                        Reserved,
                                                        dwType,
                                                        lpData,
                                                        cbData );
}

#endif /* SWIG */

/*
* Misc functions
*/

#ifndef SWIG

BOOL
IRAPISession_CeCheckPassword(IRAPISession *session,
                             LPWSTR lpszPassword )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeCheckPassword ) ( context, lpszPassword );
}


BOOL
IRAPISession_CeCreateProcess(IRAPISession *session,
                             LPCWSTR lpApplicationName,
                             LPCWSTR lpCommandLine,
                             void* lpProcessAttributes,
                             void* lpThreadAttributes,
                             BOOL bInheritHandles,
                             DWORD dwCreationFlags,
                             LPVOID lpEnvironment,
                             LPWSTR lpCurrentDirectory,
                             void* lpStartupInfo,
                             LPPROCESS_INFORMATION lpProcessInformation )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeCreateProcess ) (context,
                                                        lpApplicationName,
                                                        lpCommandLine,
                                                        lpProcessAttributes,
                                                        lpThreadAttributes,
                                                        bInheritHandles,
                                                        dwCreationFlags,
                                                        lpEnvironment,
                                                        lpCurrentDirectory,
                                                        lpStartupInfo,
                                                        lpProcessInformation );
}


BOOL
IRAPISession_CeGetStoreInformation(IRAPISession *session,
                                   LPSTORE_INFORMATION lpsi )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeGetStoreInformation ) ( context, lpsi );
}


void
IRAPISession_CeGetSystemInfo(IRAPISession *session,
                             LPSYSTEM_INFO lpSystemInfo )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return;
        }

        return ( *context->rapi_ops->CeGetSystemInfo ) (context,
                                                        lpSystemInfo );
}


BOOL
IRAPISession_CeGetSystemPowerStatusEx(IRAPISession *session,
                                      PSYSTEM_POWER_STATUS_EX pSystemPowerStatus,
                                      BOOL refresh )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeGetSystemPowerStatusEx ) ( context, pSystemPowerStatus, refresh );
}


BOOL
IRAPISession_CeGetVersionEx(IRAPISession *session,
                            LPCEOSVERSIONINFO lpVersionInformation )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeGetVersionEx ) (context,
                                                       lpVersionInformation );
}


BOOL
IRAPISession_CeOidGetInfo(IRAPISession *session,
                          CEOID oid,
                          CEOIDINFO *poidInfo )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeOidGetInfo ) (context,
                                                     oid,
                                                     poidInfo );
}


HRESULT
IRAPISession_CeProcessConfig(IRAPISession *session,
                             LPCWSTR config,
                             DWORD flags,
                             LPWSTR *reply )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return E_UNEXPECTED;
        }

        return ( *context->rapi_ops->CeProcessConfig ) ( context, config, flags, reply );
}


BOOL
IRAPISession_CeStartReplication(IRAPISession *session)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeStartReplication ) ( context );
}


HRESULT
IRAPISession_CeSyncStart(IRAPISession *session, LPCWSTR params )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return E_UNEXPECTED;
        }

        return ( *context->rapi_ops->CeSyncStart ) ( context, params );
}


HRESULT
IRAPISession_CeSyncResume(IRAPISession *session)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return E_UNEXPECTED;
        }

        return ( *context->rapi_ops->CeSyncResume ) ( context );
}


HRESULT
IRAPISession_CeSyncPause(IRAPISession *session)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return E_UNEXPECTED;
        }

        return ( *context->rapi_ops->CeSyncPause ) ( context );
}


BOOL
IRAPISession_CeGetSystemMemoryDivision(IRAPISession *session,
                                       LPDWORD lpdwStoragePages,
                                       LPDWORD lpdwRamPages,
                                       LPDWORD lpdwPageSize )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeGetSystemMemoryDivision ) (context,
                                                                  lpdwStoragePages,
                                                                  lpdwRamPages,
                                                                  lpdwPageSize );
}


DWORD
IRAPISession_CeSetSystemMemoryDivision(IRAPISession *session,
                                       DWORD dwStoragePages )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return 3;
        }

        return ( *context->rapi_ops->CeSetSystemMemoryDivision ) ( context, dwStoragePages );
}


BOOL
IRAPISession_CeRegCopyFile(IRAPISession *session,
                           LPCWSTR filename )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeRegCopyFile ) ( context, filename );
}


BOOL
IRAPISession_CeRegRestoreFile(IRAPISession *session,
                              LPCWSTR filename )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeRegRestoreFile ) ( context, filename );
}


BOOL
IRAPISession_CeKillAllApps(IRAPISession *session)
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                context->rapi_error = S_OK;
                context->last_error = ERROR_NOT_CONNECTED;
                return false;
        }

        return ( *context->rapi_ops->CeKillAllApps ) ( context );
}


#endif /* SWIG */

/*
* CeRapiInvoke stuff
*/

#ifndef SWIG

HRESULT
IRAPISession_CeRapiInvoke(IRAPISession *session,
                          LPCWSTR pDllPath,
                          LPCWSTR pFunctionName,
                          DWORD cbInput,
                          const BYTE *pInput,
                          DWORD *pcbOutput,
                          BYTE **ppOutput,
                          IRAPIStream **ppIRAPIStream,
                          DWORD dwReserved )
{
        RapiContext * context = session->context;
        if (!context->is_initialized) {
                return E_UNEXPECTED;
        }

        return ( *context->rapi_ops->CeRapiInvoke ) (context,
                                                     pDllPath,
                                                     pFunctionName,
                                                     cbInput,
                                                     pInput,
                                                     pcbOutput,
                                                     ppOutput,
                                                     ppIRAPIStream,
                                                     dwReserved );
}

#endif /* SWIG */

/** @} */


/*
 * IRAPIDevice
 */

/**
 * @defgroup IRAPIDevice IRAPIDevice public API
 * @ingroup RAPI2 
 * 
 * An object representing a connected device.
 *
 *@{ 
 */

/** @typedef struct _IRAPIDevice IRAPIDevice
 * @brief A connected device
 * 
 * This is an opaque structure, representing a connected
 * device.
 * It's contents should be accessed via the IRAPIDevice_*
 * series of functions.
 * 
 */ 
struct _IRAPIDevice {
        IRAPIDesktop *desktop;
        char *obj_path;
        SynceInfo *info;
        int refcount;
        RAPI_DEVICESTATUS status;
};


/** @brief Add a reference to the IRAPIDevice object
 * 
 * This function increases the reference count of the IRAPIDevice
 * object by one.
 * 
 * @param[in] self address of the object of which to increase the ref count
 */ 
void
IRAPIDevice_AddRef(IRAPIDevice *self)
{
        self->refcount++;
        return;
}

/** @brief Remove a reference to the IRAPIDevice object
 * 
 * This function decreases the reference count of the IRAPIDevice
 * object by one.
 * 
 * @param[in] self address of the object of which to decrease the ref count
 */ 
void
IRAPIDevice_Release(IRAPIDevice *self)
{
        self->refcount--;
        if (self->refcount > 0)
                return;

        free(self->obj_path);
        if (self->info)
                synce_info_destroy(self->info);
        IRAPIDesktop_Release(self->desktop);
        free(self);
        return;
}


/** @brief Create a communication session with the device
 * 
 * This function creates an IRAPISession object with the device for remote calls,
 * with a reference count of one.
 * 
 * @param[in] self address of the device object
 * @param[out] ppISession address of the pointer to receive the reference to the session
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDevice_CreateSession(IRAPIDevice *self, IRAPISession** ppISession)
{
        IRAPISession *session = calloc(1, sizeof(IRAPISession));
        if (!session)
                return E_OUTOFMEMORY;

        session->context = rapi_context_new();

        if (!session->context) {
                synce_error("Failed to create RapiContext object");
                free(session);
                return E_FAIL;
        }

        session->context->info = self->info;
        IRAPIDevice_AddRef(self);
        session->device = self;
        session->refcount = 1;

        *ppISession = session;
        return S_OK;
}


/** @brief Obtain connection information
 * 
 * This function retrieves information about the connection in the form of a
 * RAPI_CONNECTIONINFO struct.
 * The struct differs from the Microsoft definition, due to differences in
 * the way SynCE handles the connection.
 * 
 * @param[in] self address of the device object
 * @param[out] pConnInfo address of the struct to receive connection information
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDevice_GetConnectionInfo(IRAPIDevice *self, RAPI_CONNECTIONINFO* pConnInfo)
{
  if (!pConnInfo)
    return E_INVALIDARG;

  pConnInfo->ipaddr = g_strdup(synce_info_get_device_ip(self->info));
  pConnInfo->hostIpaddr = g_strdup(synce_info_get_local_ip(self->info));

  pConnInfo->connectionType = RAPI_CONNECTION_USB;
  return S_OK;
}

/** @brief Obtain connection status
 * 
 * This function retrieves the connection status.
 * 
 * @param[in] self address of the device object
 * @param[out] pStat address of the variable to receive connection status
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDevice_GetConnectStat(IRAPIDevice *self, RAPI_DEVICESTATUS* pStat)
{
  *pStat = self->status;
  return S_OK;
}

/** @brief Obtain device information
 * 
 * This function retrieves information about the device in the form of a
 * RAPI_DEVICEINFO struct.
 * 
 * @param[in] self address of the device object
 * @param[out] pDevInfo address of the struct to receive device information
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDevice_GetDeviceInfo(IRAPIDevice *self, RAPI_DEVICEINFO* pDevInfo)
{
  if (!pDevInfo)
    return E_INVALIDARG;

  if (!synce_info_get_os_version(self->info, &(pDevInfo->dwOsVersionMajor), &(pDevInfo->dwOsVersionMinor)))
    return E_FAIL;

  pDevInfo->bstrName = g_strdup(synce_info_get_name(self->info));
  pDevInfo->bstrPlatform = g_strdup(synce_info_get_os_name(self->info));

  if (!guid_from_string(synce_info_get_guid(self->info), &(pDevInfo->DeviceId)))
    return E_FAIL;

  return S_OK;
}


/** @brief Obtain device name
 * 
 * This function retrieves the name of the device.
 * 
 * @param[in] self address of the device object
 * @return device name, or NULL on failure
 */ 
const char *
IRAPIDevice_get_name(IRAPIDevice *self)
{
  return synce_info_get_name(self->info);
}

/** @brief Obtain device OS version
 * 
 * This function retrieves the OS version of the device.
 * 
 * @param[in] self address of the device object
 * @param[out] os_major address of an int to recieve the major version
 * @param[out] os_minor address of an int to recieve the minor version
 * @return true on success, false on failure
 */ 
bool
IRAPIDevice_get_os_version(IRAPIDevice *self, unsigned int *os_major, unsigned int *os_minor)
{
  return synce_info_get_os_version(self->info, os_major, os_minor);
}

/** @brief Obtain device build number
 * 
 * This function retrieves the build number of the device.
 * 
 * @param[in] self address of the device object
 * @return device build number, 0 on failure
 */ 
unsigned int
IRAPIDevice_get_build_number(IRAPIDevice *self)
{
  return synce_info_get_build_number(self->info);
}

/** @brief Obtain device processor type
 * 
 * This function retrieves processor type of the device.
 * 
 * @param[in] self address of the device object
 * @return device processor type, 0 on failure
 */ 
unsigned int
IRAPIDevice_get_processor_type(IRAPIDevice *self)
{
  return synce_info_get_processor_type(self->info);
}

/** @brief Obtain device OS name
 * 
 * This function retrieves the OS name of the device.
 * 
 * @param[in] self address of the device object
 * @return device OS name, or NULL on failure
 */ 
const char *
IRAPIDevice_get_os_name(IRAPIDevice *self)
{
  return synce_info_get_os_name(self->info);
}

/** @brief Obtain device model
 * 
 * This function retrieves the model of the device.
 * 
 * @param[in] self address of the device object
 * @return device model, or NULL on failure
 */ 
const char *
IRAPIDevice_get_model(IRAPIDevice *self)
{
  return synce_info_get_model(self->info);
}

/** @brief Obtain device IP
 * 
 * This function retrieves the IP address of the device,
 * in dotted quad notation.
 * 
 * @param[in] self address of the device object
 * @return device IP address, or NULL on failure
 */ 
const char *
IRAPIDevice_get_device_ip(IRAPIDevice *self)
{
  return synce_info_get_device_ip(self->info);
}

/** @brief Obtain local IP
 * 
 * This function retrieves the local IP address of the 
 * interface the device is connected to, in dotted quad
 * notation.
 * 
 * @param[in] self address of the device object
 * @return local IP address, or NULL on failure
 */ 
const char *
IRAPIDevice_get_local_ip(IRAPIDevice *self)
{
  return synce_info_get_local_ip(self->info);
}

/** @brief Obtain dbus object path
 * 
 * This function retrieves the dbus object path of the 
 * device.
 * 
 * @param[in] self address of the device object
 * @return dbus object path
 */ 
const char *
IRAPIDevice_get_obj_path(IRAPIDevice *self)
{
  return self->obj_path;
}

/** @} */

/*
 * IRAPIEnumDevices
 */

/**
 * @defgroup IRAPIEnumDevices IRAPIEnumDevices public API
 * @ingroup RAPI2 
 * 
 * An object representing an enumeration of connected devices.
 *
 *@{ 
 */

/** @typedef struct _IRAPIEnumDevices IRAPIEnumDevices
 * @brief An enumeration of devices
 * 
 * This is an opaque structure, used to enumerate connected
 * devices.
 * It's contents should be accessed via the IRAPIEnumDevices_*
 * series of functions.
 * 
 */ 
struct _IRAPIEnumDevices {
        GList *devices;
        unsigned count;
        GList *current;
        int refcount;
};


/** @brief Add a reference to the IRAPIEnumDevices object
 * 
 * This function increases the reference count of the IRAPIEnumDevices
 * object by one.
 * 
 * @param[in] self address of the object of which to increase the ref count
 */ 
void
IRAPIEnumDevices_AddRef(IRAPIEnumDevices *self)
{
        self->refcount++;
        return;
}

/** @brief Remove a reference to the IRAPIEnumDevices object
 * 
 * This function decreases the reference count of the IRAPIEnumDevices
 * object by one.
 * 
 * @param[in] self address of the object of which to decrease the ref count
 */ 
void
IRAPIEnumDevices_Release(IRAPIEnumDevices *self)
{
        self->refcount--;
        if (self->refcount > 0)
                return;

        GList *iter = self->devices;
        while (iter) {
                IRAPIDevice_Release(iter->data);
                iter = g_list_next(iter);
        }
        g_list_free(self->devices);

        free(self);
        return;
}

/** @brief Create a copy of the IRAPIEnumDevices object
 * 
 * This function creates a copy of the IRAPIEnumDevices object in the same 
 * enumeration state, with a reference count of one.
 * 
 * @param[in] self address of the object to copy
 * @param[out] ppIEnum address of the pointer to receive the reference to the new object
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIEnumDevices_Clone(IRAPIEnumDevices *self, IRAPIEnumDevices** ppIEnum)
{
        IRAPIEnumDevices *new_enum;
        new_enum = calloc(1, sizeof(IRAPIEnumDevices));
        if (!new_enum)
                return E_OUTOFMEMORY;
        memset(new_enum, 0, sizeof(IRAPIEnumDevices));
        new_enum->refcount = 1;

        GList *device = self->devices;
        while (device) {
                IRAPIDevice_AddRef(device->data);
                new_enum->devices = g_list_append(new_enum->devices, device->data);
                new_enum->count++;

                device = g_list_next(device);
        }


        new_enum->current = new_enum->devices;

        *ppIEnum = new_enum;
        return S_OK;
}

/** @brief Get the number of connected devices in the enumerator
 * 
 * This function returns the number of connected devices in the enumerator.
 * 
 * @param[in] self address of the enumerator
 * @param[out] pcElt address of the variable to receive device count
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIEnumDevices_GetCount(IRAPIEnumDevices *self, ULONG* pcElt)
{
        *pcElt = self->count;
        return S_OK;
}

/** @brief Retrieve the next device from the enumerator
 * 
 * This function retrieves the next device in the sequence of connected devices in the enumerator.
 * The reference to the device is owned by the enumerator, if you wish to use the object after
 * the enumerator has been destroyed, you should reference it yourself.
 * 
 * @param[in] self address of the enumerator
 * @param[out] ppIDevice address of the pointer to receive the reference to the device.
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIEnumDevices_Next(IRAPIEnumDevices *self, IRAPIDevice** ppIDevice)
{
        if (!(self->current))
                return MAKE_HRESULT(SEVERITY_ERROR,FACILITY_WIN32,ERROR_NO_MORE_DEVICES);

        *ppIDevice = self->current->data;

        self->current = g_list_next(self->current);

        return S_OK;
}

/** @brief Resets the enumerator sequence
 * 
 * This function resets the enumerator sequence back to the first device.
 * 
 * @param[in] self address of the enumerator
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIEnumDevices_Reset(IRAPIEnumDevices *self)
{
        self->current = self->devices;
        return S_OK;
}

/** @brief Skip devices in the enumerator
 * 
 * This function skips over the specified number of connected devices in the enumerator.
 * If there are insufficient devices to skip, an error is returned.
 * 
 * @param[in] self address of the enumerator
 * @param[out] cElt number of devices to skip
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIEnumDevices_Skip(IRAPIEnumDevices *self, ULONG cElt)
{
        uint i;
        for (i = 0; i < cElt; i++) {
                if (!(self->current))
                        return MAKE_HRESULT(SEVERITY_ERROR,FACILITY_WIN32,ERROR_NO_MORE_DEVICES);
                self->current = g_list_next(self->current);
        }

        return S_OK;
}

/** @} */


/*
 * IRAPIDesktop
*/

/**
 * @defgroup IRAPIDesktop IRAPIDesktop public API
 * @ingroup RAPI2 
 *  
 * The global object used by applications to find connected devices.
 *
 *@{ 
 */


/** @typedef struct _IRAPIDesktop IRAPIDesktop
 * @brief Store of connected devices
 * 
 * This is an opaque structure, used to discover connected
 * devices, obtained using IRAPIDesktop_Get().
 * It's contents should be accessed via the IRAPIDesktop_*
 * series of functions.
 * 
 */ 
struct _IRAPIDesktop {
        int refcount;

        /* udev */
        GDBusProxy *dbus_proxy;

#if ENABLE_UDEV_SUPPORT
        GDBusProxy *dev_mgr_proxy;
#endif

        /* list of IRAPIDevice objects */
        GList *devices;

        /* sinks to notify listeners */
        GHashTable *sinks;
        DWORD sink_seq_num;

};


#if ENABLE_UDEV_SUPPORT

static void
on_devmgr_proxy_signal (GDBusProxy *proxy SYNCE_UNUSED,
			gchar *sender_name SYNCE_UNUSED,
			gchar *signal_name,
			GVariant *parameters,
			gpointer user_data)
{
        IRAPIDesktop *self = (IRAPIDesktop*)user_data;

	gchar *obj_path = NULL;

        if (strcmp(signal_name, "DeviceConnected") == 0) {
	  g_variant_get (parameters, "(s)", &obj_path);

	  synce_debug("found device: %s", obj_path);

	  IRAPIDevice *newdev = calloc(1, sizeof(IRAPIDevice));
	  if (!newdev) {
	    synce_error("failed to allocate IRAPIDevice");
	    g_free(obj_path);
	    return;
	  }

          newdev->desktop = self;
          IRAPIDesktop_AddRef(self);
	  newdev->obj_path = obj_path;
	  newdev->info = synce_info_new_by_field(INFO_OBJECT_PATH, newdev->obj_path);
	  newdev->status = RAPI_DEVICE_CONNECTED;
	  newdev->refcount = 1;

	  self->devices = g_list_append(self->devices, newdev);

          GHashTableIter iter;
          gpointer key, value;
          HRESULT ret;

          g_hash_table_iter_init (&iter, self->sinks);
          while (g_hash_table_iter_next (&iter, &key, &value))
	  {

	    if ( ((IRAPISink*)value)->IRAPISink_OnDeviceConnected )
            {
              IRAPIDevice_AddRef(newdev);
              ret = ((IRAPISink*)value)->IRAPISink_OnDeviceConnected((IRAPISink*)value, newdev);
              /* what am I supposed to do about this return value ? */
              if (ret != S_OK)
                synce_debug("error reported from IRAPISink_OnDeviceConnected: %d: %s", ret, synce_strerror(ret));
            }
	  }

	  return;
	}

        if (strcmp(signal_name, "DeviceDisconnected") == 0) {
	  g_variant_get (parameters, "(s)", &obj_path);

          IRAPIDevice *device = NULL;
	  GList *device_el = self->devices;
	  while (device_el) {
	    if (strcmp(((IRAPIDevice*)device_el->data)->obj_path, obj_path) == 0)
	      break;

	    device_el = g_list_next(device_el);
	  }

	  if (!device_el) {
            synce_warning("Received disconnect from dccm for unfound device: %s", obj_path);
	    g_free(obj_path);
	    return;
	  }

	  synce_debug("Received device disconnected from dccm: %s", obj_path);

          device = ((IRAPIDevice*)device_el->data);
	  device->status = RAPI_DEVICE_DISCONNECTED;
	  self->devices = g_list_delete_link(self->devices, device_el);

          GHashTableIter iter;
          gpointer key, value;
          HRESULT ret;

          g_hash_table_iter_init (&iter, self->sinks);
          while (g_hash_table_iter_next (&iter, &key, &value))
	  {

	    if ( ((IRAPISink*)value)->IRAPISink_OnDeviceDisconnected )
            {
              IRAPIDevice_AddRef(device);
              ret = ((IRAPISink*)value)->IRAPISink_OnDeviceDisconnected((IRAPISink*)value, device);
              /* what am I supposed to do about this return value ? */
              if (ret != S_OK)
                synce_debug("error reported from IRAPISink_OnDeviceDisonnected: %d: %s", ret, synce_strerror(ret));
            }
	  }

	  IRAPIDevice_Release(device);

	  return;
	}

	return;
}

static void
udev_disconnect(IRAPIDesktop *self)
{

	g_signal_handlers_disconnect_by_func(self->dev_mgr_proxy, on_devmgr_proxy_signal, self);

	g_object_unref(self->dev_mgr_proxy);
	self->dev_mgr_proxy = NULL;

        GList *device = self->devices;
        while (device) {
                if (strncmp(((IRAPIDevice*)device->data)->obj_path, "/org/synce/dccm/", 16) == 0) {
                        synce_debug("removing device %s", ((IRAPIDevice*)device->data)->obj_path);
                        ((IRAPIDevice*)device->data)->status = RAPI_DEVICE_DISCONNECTED;
                        IRAPIDevice_Release((IRAPIDevice*)device->data);
                        self->devices = g_list_delete_link(self->devices, device);
                        device = self->devices;
                        continue;
                }
                device = g_list_next(device);
        }
}

static void
udev_connect(IRAPIDesktop *self)
{
        GError *error = NULL;
	GVariant *ret = NULL;
        gchar **dev_list = NULL;
        guint i;
        gchar *obj_path = NULL;

	self->dev_mgr_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
							     G_DBUS_PROXY_FLAGS_NONE,
							     NULL, /* GDBusInterfaceInfo */
							     DCCM_SERVICE,
							     DCCM_MGR_PATH,
							     DCCM_MGR_IFACE,
							     NULL, /* GCancellable */
							     &error);
	if (self->dev_mgr_proxy == NULL) {
	  synce_error("%s: Failed to create proxy to device manager: %s", G_STRFUNC, error->message);
	  return;
	}

	g_signal_connect (self->dev_mgr_proxy,
			  "g-signal",
			  G_CALLBACK (on_devmgr_proxy_signal),
			  self);

        /* currently connected devices */

	ret = g_dbus_proxy_call_sync (self->dev_mgr_proxy,
				      "GetConnectedDevices",
				      g_variant_new ("()"),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error);
	if (!ret) {
	  synce_error("Error getting device list from dccm: %s", error->message);
	  g_error_free(error);
	  return;
	}

	g_variant_get (ret, "(^ao)", &dev_list);
	g_variant_unref (ret);

        for (i = 0; i < g_strv_length(dev_list); i++) {
                obj_path = dev_list[i];
                synce_debug("found device: %s", obj_path);

                IRAPIDevice *newdev = calloc(1, sizeof(IRAPIDevice));
                if (!newdev) {
                        synce_error("failed to allocate IRAPIDevice");
                        break;
                }

                newdev->desktop = self;
                IRAPIDesktop_AddRef(self);
                newdev->obj_path = obj_path;
                newdev->info = synce_info_new_by_field(INFO_OBJECT_PATH, newdev->obj_path);
                newdev->status = RAPI_DEVICE_CONNECTED;
                newdev->refcount = 1;

                self->devices = g_list_append(self->devices, newdev);

        }
	g_strfreev(dev_list);

        return;
}

#endif /* ENABLE_UDEV_SUPPORT */


static void
on_dbus_proxy_signal (GDBusProxy *proxy SYNCE_UNUSED,
		      gchar *sender_name SYNCE_UNUSED,
		      gchar *signal_name,
		      GVariant *parameters,
		      gpointer user_data)
{
        IRAPIDesktop *self = (IRAPIDesktop*)user_data;

	gchar *name;
	gchar *old_owner;
	gchar *new_owner;

        if (strcmp(signal_name, "NameOwnerChanged") != 0)
	  return;

#if ENABLE_UDEV_SUPPORT
	g_variant_get (parameters, "(sss)", &name, &old_owner, &new_owner);

        if (strcmp(name, DCCM_SERVICE) == 0) {

	        /* If this parameter is empty, dccm just came online */

                if (strcmp(old_owner, "") == 0) {
                        synce_debug("%s: dccm came online", G_STRFUNC);
			udev_connect(self);

			return;
		}

                /* If this parameter is empty, dccm just went offline */

                if (strcmp(new_owner, "") == 0) {
		        g_debug("%s: dccm went offline", G_STRFUNC);
			udev_disconnect(self);

			return;
		}
	}
#endif
}


static HRESULT
IRAPIDesktop_Init()
{
#if !GLIB_CHECK_VERSION (2, 36, 0)
        g_type_init();
#endif

        IRAPIDesktop *self = NULL;

        self = calloc(1, sizeof(IRAPIDesktop));
        if (!self)
                return E_OUTOFMEMORY;

        self->sinks = g_hash_table_new(g_direct_hash,g_direct_equal);
	self->sink_seq_num = 1;

        /*
           connect to dccm, odccm, vdccm ?
           set up callbacks from dccm and odccm 
           create initial devices
        */
        self->dbus_proxy = NULL;
#if ENABLE_UDEV_SUPPORT
        self->dev_mgr_proxy = NULL;
#endif
        self->devices = NULL;

        /* dccm */

        GError *error = NULL;
        gboolean has_owner = FALSE;

	self->dbus_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
							  G_DBUS_PROXY_FLAGS_NONE,
							  NULL, /* GDBusInterfaceInfo */
							  DBUS_SERVICE,
							  DBUS_PATH,
							  DBUS_IFACE,
							  NULL, /* GCancellable */
							  &error);
	if (self->dbus_proxy == NULL) {
	  g_warning("%s: Failed to get dbus proxy object: %s", G_STRFUNC, error->message);
	  g_error_free(error);
	  /* we don't yet have error handling here ? */
	}

	g_signal_connect (self->dbus_proxy,
			  "g-signal",
			  G_CALLBACK (on_dbus_proxy_signal),
			  self);

#if ENABLE_UDEV_SUPPORT
	GVariant *ret = NULL;
	ret = g_dbus_proxy_call_sync (self->dbus_proxy,
				      "NameHasOwner",
				      g_variant_new ("(s)", DCCM_SERVICE),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1,
				      NULL,
				      &error);
	if (!ret) {
	  synce_error("%s: Error checking owner of %s: %s", G_STRFUNC, DCCM_SERVICE, error->message);
	  g_error_free(error);
	  return E_FAIL;
	}
	g_variant_get (ret, "(b)", &has_owner);
	g_variant_unref (ret);

        if (has_owner)
                udev_connect(self);
#endif

        irapi_desktop = self;

        return S_OK;
}

static HRESULT
IRAPIDesktop_Uninit()
{
        /*
           destroy devices
           remove callbacks from dccm and odccm 
           disconnect from dccm, odccm, vdccm ?
        */

        IRAPIDesktop *self = irapi_desktop;

        g_hash_table_destroy(self->sinks);
	self->sinks = NULL;

#if ENABLE_UDEV_SUPPORT
	if (self->dev_mgr_proxy)
	  udev_disconnect(self);
#endif

        g_signal_handlers_disconnect_by_func(self->dbus_proxy, on_dbus_proxy_signal, self);

        g_object_unref(self->dbus_proxy);
        self->dbus_proxy = NULL;

        free(irapi_desktop);
        irapi_desktop = NULL;

        return S_OK;
}



/** @brief Obtain a reference to the global IRAPIDesktop object
 * 
 * This function obtains a reference to the global object 
 * used to find connected devices.
 * 
 * @param[out] ppIRAPIDesktop address of the pointer to receive the reference
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDesktop_Get(IRAPIDesktop **ppIRAPIDesktop)
{
        HRESULT hr;

        if (irapi_desktop != NULL) {
                IRAPIDesktop_AddRef(irapi_desktop);
                *ppIRAPIDesktop = irapi_desktop;
                return S_OK;
        }

        hr = IRAPIDesktop_Init();
        if (SUCCEEDED(hr)) {
                IRAPIDesktop_AddRef(irapi_desktop);
                *ppIRAPIDesktop = irapi_desktop;
                return S_OK;
        }
        return hr;
}

/** @brief Add a reference to the IRAPIDesktop object
 * 
 * This function increases the reference count of the IRAPIDesktop
 * object by one.
 * 
 * @param[in] self address of the object of which to increase the ref count
 */ 
void
IRAPIDesktop_AddRef(IRAPIDesktop *self)
{
        self->refcount++;
        return;
}

/** @brief Remove a reference to the IRAPIDesktop object
 * 
 * This function decreases the reference count of the IRAPIDesktop
 * object by one.
 * 
 * @param[in] self address of the object of which to decrease the ref count
 */ 
void
IRAPIDesktop_Release(IRAPIDesktop *self)
{
        self->refcount--;
        if (self->refcount > 0)
                return;

        IRAPIDesktop_Uninit();
        return;
}

/** @brief Registers an IRAPISink object to receive connection events
 * 
 * This function registers an IRAPISink object so that it receives
 * connection and disconnection events.
 * 
 * @param[in] self address of an IRAPIDesktop instance
 * @param[in] pISink address of the IRAPISink object
 * @param[out] pdwContext address of a DWORD to return the context id, this is required to cease delivery of events to this sink.
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDesktop_Advise(IRAPIDesktop *self, IRAPISink* pISink, DWORD* pdwContext)
{
  /*
    add sink to list of sinks;
  */

  while(TRUE) {
    if (self->sink_seq_num == 0xffffffff)
      self->sink_seq_num = 1;

    if (g_hash_table_lookup(self->sinks, GSIZE_TO_POINTER(self->sink_seq_num))) {
      self->sink_seq_num++;
      continue;
    }

    break;
  }

  g_hash_table_insert(self->sinks, GSIZE_TO_POINTER(self->sink_seq_num), pISink);

  *pdwContext = self->sink_seq_num;
  return S_OK;
}

/** @brief Unregisters an IRAPISink object
 * 
 * This function unregisters an IRAPISink object to cease notification of
 * connection and disconnection events.
 * 
 * @param[in] self address of an IRAPIDesktop instance
 * @param[in] dwContext the context id from IRAPIDesktop_Advise
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDesktop_UnAdvise(IRAPIDesktop *self, DWORD dwContext)
{
  if (g_hash_table_remove(self->sinks, GSIZE_TO_POINTER(dwContext)))
    return S_OK;

  synce_warning("Request to remove IRAPISink that was not registered");
  return S_OK;
}

/** @brief Obtain an enumeration object of connected devices
 * 
 * This function returns an IRAPIEnumDevices object to enumerate all
 * connected devices.
 * 
 * @param[in] self address of an IRAPIDesktop instance
 * @param[out] ppIEnum address of the pointer to receive the reference
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDesktop_EnumDevices(IRAPIDesktop *self, IRAPIEnumDevices** ppIEnum)
{
        IRAPIEnumDevices *enum_dev;
        enum_dev = calloc(1, sizeof(IRAPIEnumDevices));
        if (!enum_dev)
                return E_OUTOFMEMORY;
        memset(enum_dev, 0, sizeof(IRAPIEnumDevices));
        enum_dev->refcount = 1;

        GList *device = self->devices;
        while (device) {
                IRAPIDevice_AddRef(device->data);

                enum_dev->devices = g_list_append(enum_dev->devices, device->data);

                enum_dev->count++;

                device = g_list_next(device);
        }
        enum_dev->current = enum_dev->devices;

        *ppIEnum = enum_dev;
        return S_OK;
}

/*
 * we haven't got GUID (RAPIDEVICEID) for pre WM5 in dccm
 */
/** @brief Obtain a connected device specified by GUID
 * 
 * This function returns an IRAPIDevice object corresponding to the device
 * identified by the GUID.
 * Note, pre Windows Mobile 5 devices cannot be identified by GUID.
 * 
 * @param[in] self address of an IRAPIDesktop instance
 * @param[in] pDeviceID address of a RAPIDEVICEID struct containing the GUID
 * @param[in] opFlags A member of the RAPI_GETDEVICEOPCODE enumeration, indicates
 *       whether the call should block. This value is unused in this implementation.
 * @param[out] ppIDevice address of the pointer to receive the device reference
 * @return an HRESULT indicating success or an error
 */ 
HRESULT
IRAPIDesktop_FindDevice(IRAPIDesktop *self, RAPIDEVICEID *pDeviceID, RAPI_GETDEVICEOPCODE opFlags SYNCE_UNUSED, IRAPIDevice** ppIDevice)
{
  char *guidstr = guid_to_string(pDeviceID);
  if (!guidstr) {
    synce_warning("Failed to convert guid to string representation");
    return E_FAIL;
  }

  GList *device = self->devices;
  while (device) {
    if (strcmp(guidstr,synce_info_get_guid(((IRAPIDevice*)(device->data))->info)) == 0)
      break;
    device = g_list_next(device);
  }
  free(guidstr);

  if (!device)
    return MAKE_HRESULT(SEVERITY_ERROR,FACILITY_WIN32,ERROR_NO_MORE_DEVICES);

  IRAPIDevice_AddRef(device->data);
  *ppIDevice = device->data;
  return S_OK;
}

/** @} */

/** @} */
