/* $Id$ */
#undef __STRICT_ANSI__
#define _GNU_SOURCE
#if HAVE_CONFIG_H
#include "rapi_config.h"
#endif
#include "rapi_context.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#if ENABLE_ODCCM_SUPPORT || ENABLE_HAL_SUPPORT || ENABLE_UDEV_SUPPORT
#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define USE_THREAD_SAFE_VERSION 1
#ifdef USE_THREAD_SAFE_VERSION
#include <pthread.h>
#endif

#define RAPI_PORT  990

#define RAPI_CONTEXT_DEBUG 0

#if ENABLE_ODCCM_SUPPORT || ENABLE_UDEV_SUPPORT
static const char* const DBUS_SERVICE       = "org.freedesktop.DBus";
static const char* const DBUS_IFACE         = "org.freedesktop.DBus";
static const char* const DBUS_PATH          = "/org/freedesktop/DBus";
#endif

#if ENABLE_ODCCM_SUPPORT
static const char* const ODCCM_SERVICE      = "org.synce.odccm";
static const char* const ODCCM_MGR_PATH     = "/org/synce/odccm/DeviceManager";
static const char* const ODCCM_MGR_IFACE    = "org.synce.odccm.DeviceManager";
static const char* const ODCCM_DEV_IFACE    = "org.synce.odccm.Device";
#endif

#if ENABLE_UDEV_SUPPORT
static const char* const DCCM_SERVICE      = "org.synce.dccm";
static const char* const DCCM_MGR_PATH     = "/org/synce/dccm/DeviceManager";
static const char* const DCCM_MGR_IFACE    = "org.synce.dccm.DeviceManager";
static const char* const DCCM_DEV_IFACE    = "org.synce.dccm.Device";
#endif


#if RAPI_CONTEXT_DEBUG
#define rapi_context_trace(args...)    synce_trace(args)
#define rapi_context_warning(args...)  synce_warning(args)
#else
#define rapi_context_trace(args...)    synce_trace(args)
#define rapi_context_warning(args...)  synce_warning(args)
#endif
#define rapi_context_error(args...)    synce_error(args)



#ifdef USE_THREAD_SAFE_VERSION
/*This holds the value for the key where the contexts are stored*/
static pthread_key_t context_key = -1;
/*This makes sure that the a key is created only once, we can also use pthread for this*/
static pthread_once_t key_is_created = PTHREAD_ONCE_INIT;
#else
static RapiContext* current_context = NULL;

#endif


extern struct rapi_ops_s rapi_ops;
extern struct rapi_ops_s rapi2_ops;


#ifdef USE_THREAD_SAFE_VERSION
/* This method will create a key to hold the context_key
 * with which each thread can access the thread local 
 * rapi_context.
 * It must be called ONLY once, there for this method
 * will always be used in combination with the 
 * pthread_once method.
 * Do not run it multiple times, this will result in 
 * unpredictable behaviour and might crash things!
 */
void create_pthread_key(){
	pthread_key_create( &context_key, NULL ) ;
}
#endif

static RapiContext *
get_current_context()
{
#ifdef USE_THREAD_SAFE_VERSION
	/* If the key for the thread_local context variables was not initalized yet,
	 * do that now
	 */
	(void) pthread_once(&key_is_created, create_pthread_key);

	/* Get the thread local current_context */
	RapiContext* thread_current_context = (RapiContext*)pthread_getspecific(context_key) ;

	return thread_current_context;
#else
	return current_context;
#endif
}

static void
set_current_context(RapiContext *context)
{
#ifdef USE_THREAD_SAFE_VERSION
	/* If the key for the thread_local context variables was not initalized yet,
	 * do that now
	 */
	(void) pthread_once(&key_is_created, create_pthread_key);
	
	/* Set the context for this thread in its local thread variable */
	pthread_setspecific(context_key, (void*) context ) ;
#else
	current_context = context;
#endif
}


RapiContext* rapi_context_current()/*{{{*/
{
	RapiContext *context = NULL;

	context = get_current_context();

	/* If the current context is not initialized, setup
	    a new one and return it
	 */
	if (!context)
	{
		RapiContext *new_context = rapi_context_new();
		set_current_context(new_context);
		context = get_current_context();
	}
	
	return context;

}/*}}}*/

void rapi_context_set(RapiContext* context)
{
	RapiContext *old_context = NULL;

	/* Get the current context, if any */
	old_context = get_current_context();

	set_current_context(context);

	if (context)
		rapi_context_ref(context);
	if (old_context)
		rapi_context_unref(old_context);
}

static void rapi_context_free(RapiContext* context)/*{{{*/
{
	if (!context)
		return;

	/* check it against the current context,
	 * this should never happen
	 */
	RapiContext* check_context = get_current_context();
	if (check_context == context)
		set_current_context(NULL);

	rapi_buffer_free(context->send_buffer);
	rapi_buffer_free(context->recv_buffer);
	synce_socket_free(context->socket);
	if (context->own_info && context->info)
		synce_info_destroy(context->info);
	free(context);

}/*}}}*/

RapiContext* rapi_context_new()/*{{{*/
{
	RapiContext* context = calloc(1, sizeof(RapiContext));

	if (context)
	{
		memset(context, 0, sizeof(RapiContext));
		if (!((context->send_buffer  = rapi_buffer_new()) &&
		      (context->recv_buffer = rapi_buffer_new()) &&
		      (context->socket = synce_socket_new())
		      ))
		{
			rapi_context_free(context);
			return NULL;
		}
	}

	context->info = NULL;
	context->own_info = false;
	context->refcount = 1;

	return context;
}/*}}}*/

void rapi_context_ref(RapiContext* context)/*{{{*/
{
	if (context)
		(context->refcount)++;
}/*}}}*/

void rapi_context_unref(RapiContext* context)/*{{{*/
{
	if (context)
	{
		(context->refcount)--;
		if (context->refcount < 1)
			rapi_context_free(context);
	}
}/*}}}*/


#if ENABLE_ODCCM_SUPPORT || ENABLE_HAL_SUPPORT || ENABLE_UDEV_SUPPORT

static gint
get_socket_from_dccm(const gchar *unix_path)
{
  int fd = -1, dev_fd, ret;
  struct sockaddr_un sa;
  struct msghdr msg = { 0, 0, 0, 0, 0, 0, 0 };
  struct cmsghdr *cmsg;
  struct iovec iov;
  char cmsg_buf[512];
  char data_buf[512];

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    goto ERROR;

  sa.sun_family = AF_UNIX;
  strcpy(sa.sun_path, unix_path);

  if (connect(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0)
    goto ERROR;

  msg.msg_control = cmsg_buf;
  msg.msg_controllen = sizeof(cmsg_buf);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_flags = MSG_WAITALL;

  iov.iov_base = data_buf;
  iov.iov_len = sizeof(data_buf);

  ret = recvmsg(fd, &msg, 0);
  if (ret < 0)
    goto ERROR;

  cmsg = CMSG_FIRSTHDR (&msg);
  if (cmsg == NULL || cmsg->cmsg_type != SCM_RIGHTS)
    goto ERROR;

  dev_fd = *((int *) CMSG_DATA(cmsg));
  goto OUT;

ERROR:
  dev_fd = -1;

OUT:
  if (fd >= 0)
    close(fd);

  return dev_fd;
}
#endif /* ENABLE_ODCCM_SUPPORT || ENABLE_HAL_SUPPORT || ENABLE_UDEV_SUPPORT */


#if ENABLE_ODCCM_SUPPORT

static int
get_connection_from_odccm(SynceInfo *info)
{
  GError *error = NULL;
  DBusGConnection *bus = NULL;
  DBusGProxy *dbus_proxy = NULL;
  DBusGProxy *dev_proxy = NULL;
  gboolean odccm_running = FALSE;
  gchar *unix_path = NULL;
  gint fd = -1;

  g_type_init();

  bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
  if (bus == NULL)
  {
    synce_warning("%s: Failed to connect to system bus: %s", G_STRFUNC, error->message);
    goto ERROR;
  }

  dbus_proxy = dbus_g_proxy_new_for_name (bus,
                                          DBUS_SERVICE,
                                          DBUS_PATH,
                                          DBUS_IFACE);
  if (dbus_proxy == NULL) {
    synce_warning("%s: Failed to get dbus proxy object", G_STRFUNC);
    goto ERROR;
  }

  if (!(dbus_g_proxy_call(dbus_proxy, "NameHasOwner",
                          &error,
                          G_TYPE_STRING, ODCCM_SERVICE,
                          G_TYPE_INVALID,
                          G_TYPE_BOOLEAN, &odccm_running,
                          G_TYPE_INVALID))) {
    synce_warning("%s: Error checking owner of service %s: %s", G_STRFUNC, ODCCM_SERVICE, error->message);
    g_object_unref(dbus_proxy);
    goto ERROR;
  }

  g_object_unref(dbus_proxy);
  if (!odccm_running) {
    synce_info("Odccm is not running, ignoring");
    goto ERROR;
  }

  dev_proxy = dbus_g_proxy_new_for_name(bus, ODCCM_SERVICE,
                                        synce_info_get_object_path(info),
                                        ODCCM_DEV_IFACE);
  if (dev_proxy == NULL) {
    synce_warning("%s: Failed to get proxy for device '%s'", G_STRFUNC, synce_info_get_object_path(info));
    goto ERROR;
  }

  if (!dbus_g_proxy_call(dev_proxy, "RequestConnection", &error,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &unix_path,
                         G_TYPE_INVALID))
  {
    synce_warning("%s: Failed to get a connection for %s: %s", G_STRFUNC, synce_info_get_name(info), error->message);
    g_object_unref(dev_proxy);
    goto ERROR;
  }

  g_object_unref(dev_proxy);

  fd = get_socket_from_dccm(unix_path);
  g_free(unix_path);

  if (fd < 0)
  {
    synce_warning("%s: Failed to get file-descriptor from odccm for %s", G_STRFUNC, synce_info_get_name(info));
    goto ERROR;
  }

  goto OUT;

ERROR:
  if (error != NULL)
    g_error_free(error);

OUT:

  if (bus != NULL)
    dbus_g_connection_unref (bus);

  return fd;
}
#endif /* ENABLE_ODCCM_SUPPORT */


#if ENABLE_UDEV_SUPPORT

static int
get_connection_from_udev(SynceInfo *info)
{
  GError *error = NULL;
  DBusGConnection *bus = NULL;
  DBusGProxy *dbus_proxy = NULL;
  DBusGProxy *dev_proxy = NULL;
  gboolean dccm_running = FALSE;
  gchar *unix_path = NULL;
  gint fd = -1;

  g_type_init();

  bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
  if (bus == NULL)
  {
    synce_warning("%s: Failed to connect to system bus: %s", G_STRFUNC, error->message);
    goto ERROR;
  }

  dbus_proxy = dbus_g_proxy_new_for_name (bus,
                                          DBUS_SERVICE,
                                          DBUS_PATH,
                                          DBUS_IFACE);
  if (dbus_proxy == NULL) {
    synce_warning("%s: Failed to get dbus proxy object", G_STRFUNC);
    goto ERROR;
  }

  if (!(dbus_g_proxy_call(dbus_proxy, "NameHasOwner",
                          &error,
                          G_TYPE_STRING, DCCM_SERVICE,
                          G_TYPE_INVALID,
                          G_TYPE_BOOLEAN, &dccm_running,
                          G_TYPE_INVALID))) {
    synce_warning("%s: Error checking owner of service %s: %s", G_STRFUNC, DCCM_SERVICE, error->message);
    g_object_unref(dbus_proxy);
    goto ERROR;
  }

  g_object_unref(dbus_proxy);
  if (!dccm_running) {
    synce_info("dccm is not running, ignoring");
    goto ERROR;
  }

  dev_proxy = dbus_g_proxy_new_for_name(bus, DCCM_SERVICE,
                                        synce_info_get_object_path(info),
                                        DCCM_DEV_IFACE);
  if (dev_proxy == NULL) {
    synce_warning("%s: Failed to get proxy for device '%s'", G_STRFUNC, synce_info_get_object_path(info));
    goto ERROR;
  }

  if (!dbus_g_proxy_call(dev_proxy, "RequestConnection", &error,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &unix_path,
                         G_TYPE_INVALID))
  {
    synce_warning("%s: Failed to get a connection for %s: %s", G_STRFUNC, synce_info_get_name(info), error->message);
    g_object_unref(dev_proxy);
    goto ERROR;
  }

  g_object_unref(dev_proxy);

  fd = get_socket_from_dccm(unix_path);
  g_free(unix_path);

  if (fd < 0)
  {
    synce_warning("%s: Failed to get file-descriptor from dccm for %s", G_STRFUNC, synce_info_get_name(info));
    goto ERROR;
  }

  goto OUT;

ERROR:
  if (error != NULL)
    g_error_free(error);

OUT:

  if (bus != NULL)
    dbus_g_connection_unref (bus);

  return fd;
}
#endif /* ENABLE_UDEV_SUPPORT */


#if ENABLE_HAL_SUPPORT

static int
get_connection_from_hal(SynceInfo *info)
{
  DBusGConnection *system_bus = NULL;
  DBusGProxy *dev_proxy = NULL;
  GError *error = NULL;
  gchar *unix_path = NULL;
  gint fd = -1;

  g_type_init();

  if (!(system_bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error))) {
    synce_warning("%s: Failed to connect to system bus: %s", G_STRFUNC, error->message);
    goto error_exit;
  }

  dev_proxy = dbus_g_proxy_new_for_name(system_bus,
                                        "org.freedesktop.Hal",
                                        synce_info_get_object_path(info),
                                        "org.freedesktop.Hal.Device.Synce");
  if (dev_proxy == NULL) {
    synce_warning("%s: Failed to get proxy for device '%s'", G_STRFUNC, synce_info_get_object_path(info));
    goto error_exit;
  }

  if (!dbus_g_proxy_call(dev_proxy, "RequestConnection", &error,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &unix_path,
                         G_TYPE_INVALID))
  {
    synce_warning("%s: Failed to get a connection for %s: %s: %s", G_STRFUNC, synce_info_get_object_path(info), synce_info_get_name(info), error->message);
    g_object_unref(dev_proxy);
    goto error_exit;
  }

  g_object_unref(dev_proxy);

  fd = get_socket_from_dccm(unix_path);
  g_free(unix_path);

  if (fd < 0) {
    synce_warning("%s: Failed to get file-descriptor from dccm for %s", G_STRFUNC, synce_info_get_object_path(info));
    goto error_exit;
  }

  goto exit;

error_exit:
  if (error != NULL)
    g_error_free(error);

exit:
  if (system_bus != NULL)
    dbus_g_connection_unref (system_bus);

  return fd;
}
#endif /* ENABLE_HAL_SUPPORT */


HRESULT rapi_context_connect(RapiContext* context)
{
    HRESULT result = E_FAIL;
    SynceInfo* info = NULL;

    if (context->is_initialized)
    {
        /* Fail immediately */
            return CERAPI_E_ALREADYINITIALIZED;
    }

    if (context->info)
        info = context->info;
    else
        info = synce_info_new(NULL);
    if (!info)
    {
        synce_error("Failed to get connection info");
        goto fail;
    }

    const char *transport = synce_info_get_transport(info);
    /*
     *  original dccm or vdccm
     */
    if (transport == NULL || ( strcmp(transport, "odccm") != 0 && strcmp(transport, "hal") != 0 && strcmp(transport, "udev") != 0 ) ) {
        if (!synce_info_get_dccm_pid(info))
        {
            synce_error("DCCM PID entry not found for current connection");
            goto fail;
        }

        if (kill(synce_info_get_dccm_pid(info), 0) < 0)
        {
            if (errno != EPERM)
            {
                synce_error("DCCM not running with pid %i", synce_info_get_dccm_pid(info));
                goto fail;
            }
        }

        if (!synce_info_get_device_ip(info))
        {
            synce_error("IP entry not found for current connection");
            goto fail;
        }
    }

    if (transport == NULL || strncmp(transport,  "ppp", 3) == 0) {
        /*
         *  original dccm or vdccm
         */
        if ( !synce_socket_connect(context->socket, synce_info_get_device_ip(info), RAPI_PORT) )
        {
            synce_error("failed to connect to %s", synce_info_get_device_ip(info));
            goto fail;
        }

        const char *password = synce_info_get_password(info);
        if (password && strlen(password))
        {
            bool password_correct = false;

            if (!synce_password_send(context->socket, password, (unsigned char)synce_info_get_key(info)))
            {
                synce_error("failed to send password");
                result = E_ACCESSDENIED;
                goto fail;
            }

            if (!synce_password_recv_reply(context->socket, 1, &password_correct))
            {
                synce_error("failed to get password reply");
                result = E_ACCESSDENIED;
                goto fail;
            }

            if (!password_correct)
            {
                synce_error("invalid password");
                result = E_ACCESSDENIED;
                goto fail;
            }
        }
        context->rapi_ops = &rapi_ops;
    } else {
        /*
         *  odccm, synce-hal, udev, or proxy ?
         */
#if ENABLE_ODCCM_SUPPORT
        if (strcmp(transport, "odccm") == 0) {
            synce_socket_take_descriptor(context->socket, get_connection_from_odccm(info));
        }
        else
#endif
#if ENABLE_HAL_SUPPORT
        if (strcmp(transport, "hal") == 0) {
            synce_socket_take_descriptor(context->socket, get_connection_from_hal(info));
        }
        else
#endif
#if ENABLE_UDEV_SUPPORT
        if (strcmp(transport, "udev") == 0) {
            synce_socket_take_descriptor(context->socket, get_connection_from_udev(info));
        }
        else
#endif
	if ( !synce_socket_connect_proxy(context->socket, synce_info_get_device_ip(info)) )
        {
            synce_error("failed to connect to proxy for %s", synce_info_get_device_ip(info));
            goto fail;
        }

	/* rapi 2 seems to be used on devices with OS version of 5.1 or greater */

        int os_major = 0, os_minor = 0;
        synce_info_get_os_version(info, &os_major, &os_minor);
	if ((os_major > 4) && (os_minor > 0))
	  context->rapi_ops = &rapi2_ops;
	else
	  context->rapi_ops = &rapi_ops;
    }

    if (!context->info)
    {
      context->info = info;
      context->own_info = true;
    }

    context->is_initialized = true;
    result = S_OK;

fail:
    if (!context->info)
        synce_info_destroy(info);
    return result;
}

STDAPI rapi_context_disconnect(RapiContext* context)
{
    if (!context->is_initialized)
        return E_FAIL;

    context->rapi_ops = NULL;
    if (context->own_info) {
        synce_info_destroy(context->info);
        context->info = NULL;
        context->own_info = false;
    }

    synce_socket_close(context->socket);
    context->is_initialized = false;

    return S_OK;
}

bool rapi_context_begin_command(RapiContext* context, uint32_t command)/*{{{*/
{
	rapi_context_trace("command=0x%02x", command);

	rapi_buffer_free_data(context->send_buffer);

	if ( !rapi_buffer_write_uint32(context->send_buffer, command) )
		return false;

	return true;
}/*}}}*/

bool rapi_context_call(RapiContext* context)/*{{{*/
{
	context->rapi_error = E_UNEXPECTED;

	if ( !rapi_buffer_send(context->send_buffer, context->socket) )
	{
		rapi_context_error("rapi_buffer_send failed");
		context->rapi_error = E_FAIL;
		return false;
	}

	if ( !rapi_buffer_recv(context->recv_buffer, context->socket) )
	{
		rapi_context_error("rapi_buffer_recv failed");
		context->rapi_error = E_FAIL;
		return false;
	}

	/* this is a boolean? */
	if ( !rapi_buffer_read_uint32(context->recv_buffer, &context->result_1) )
	{
		rapi_context_error("reading result_1 failed");
		context->rapi_error = E_FAIL;
		return false;
	}

	rapi_context_trace("result 1 = 0x%08x", context->result_1);

	if (1 == context->result_1)
	{
		/* this is a HRESULT */
		if ( !rapi_buffer_read_uint32(context->recv_buffer, &context->result_2) )
		{
			rapi_context_error("reading result_2 failed");
			context->rapi_error = E_FAIL;
			return false;
		}

		rapi_context_error("result 2 = 0x%08x", context->result_2);

		context->rapi_error = context->result_2;
		if (context->result_2 != 0)
		  return false;
	}

	context->rapi_error = S_OK;
	return true;
}/*}}}*/

bool rapi2_context_call(RapiContext* context)/*{{{*/
{
    context->rapi_error = E_UNEXPECTED;

    if ( !rapi_buffer_send(context->send_buffer, context->socket) )
    {
        rapi_context_error("rapi_buffer_send failed");
        context->rapi_error = E_FAIL;
        return false;
    }

    if ( !rapi_buffer_recv(context->recv_buffer, context->socket) )
    {
        rapi_context_error("rapi_buffer_recv failed");
        context->rapi_error = E_FAIL;
        return false;
    }

    context->rapi_error = S_OK;
    return true;
}/*}}}*/

