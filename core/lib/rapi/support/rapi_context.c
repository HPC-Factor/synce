/* $Id$ */
#undef __STRICT_ANSI__
#define _GNU_SOURCE
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include "rapi_context.h"
#include "synce.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#if ENABLE_UDEV_SUPPORT
#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <gio/gunixconnection.h>
#include "synce_gerrors.h"
#endif

#define USE_THREAD_SAFE_VERSION 1
#ifdef USE_THREAD_SAFE_VERSION
#include <pthread.h>
#endif

#define RAPI_PORT  990

#define RAPI_CONTEXT_DEBUG 0

#if ENABLE_UDEV_SUPPORT
static const char* const DBUS_SERVICE       = "org.freedesktop.DBus";
static const char* const DBUS_IFACE         = "org.freedesktop.DBus";
static const char* const DBUS_PATH          = "/org/freedesktop/DBus";

static const char* const DCCM_SERVICE      = "org.synce.dccm";
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


#if ENABLE_UDEV_SUPPORT

static gint
get_socket_from_dccm(const gchar *unix_path)
{
  int dev_fd = -1;
  GError *error = NULL;
  GSocketClient *client = NULL;
  GSocketAddress *addr = NULL;
  GSocketConnection *conn = NULL;

  client = g_socket_client_new();
  addr = g_unix_socket_address_new(unix_path);
  conn = g_socket_client_connect (client, G_SOCKET_CONNECTABLE(addr), NULL, &error);
  if (!conn) {
    g_warning("%s: failed to connect to socket %s: %s", G_STRFUNC, unix_path, error->message);
    g_clear_error(&error);
    goto OUT;
  }

  dev_fd = g_unix_connection_receive_fd(G_UNIX_CONNECTION(conn), NULL, &error);
  if (dev_fd < 0) {
    g_warning("%s: failed to receive device file descriptor: %s", G_STRFUNC, error->message);
    g_clear_error(&error);
    goto OUT;
  }

OUT:
  g_object_unref(client);
  g_object_unref(addr);
  if (conn) g_object_unref(conn);

  return dev_fd;
}


/*
 * synce_glib_init:
 *
 * Does various one-time init things such as
 *
 *  - registering the SYNCE_DCCM_ERROR error domain
 */
static void
synce_glib_init(void)
{
  static volatile gsize initialized = 0;

  if (g_once_init_enter (&initialized))
  {
    volatile GQuark synce_dccm_error_domain;

    synce_dccm_error_domain = SYNCE_DCCM_ERROR;
    (synce_dccm_error_domain); /* To avoid -Wunused-but-set-variable */

    g_once_init_leave (&initialized, 1);
  }
}


static HRESULT
get_connection_from_udev(SynceInfo *info, int *sockfd)
{
  GError *error = NULL;
  GDBusProxy *dbus_proxy = NULL;
  GDBusProxy *dev_proxy = NULL;
  gboolean dccm_running = FALSE;
  gchar *unix_path = NULL;
  gint fd = -1;
  GVariant *ret = NULL;
  HRESULT result = E_FAIL;

  /* Ensure that e.g. SYNCE_DCCM_ERROR is registered using g_dbus_error_register_error() */
  synce_glib_init();

#if !GLIB_CHECK_VERSION (2, 36, 0)
  g_type_init();
#endif

  dbus_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
					      G_DBUS_PROXY_FLAGS_NONE,
					      NULL, /* GDBusInterfaceInfo */
					      DBUS_SERVICE,
					      DBUS_PATH,
					      DBUS_IFACE,
					      NULL, /* GCancellable */
					      &error);
  if (dbus_proxy == NULL) {
    g_warning("%s: Failed to get dbus proxy object: %s", G_STRFUNC, error->message);
    goto ERROR;
  }

  ret = g_dbus_proxy_call_sync (dbus_proxy,
				"NameHasOwner",
				g_variant_new ("(s)", DCCM_SERVICE),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				&error);
  if (!ret) {
    g_critical("%s: Error checking owner of service %s: %s", G_STRFUNC, DCCM_SERVICE, error->message);
    g_object_unref(dbus_proxy);
    goto ERROR;
  }
  g_variant_get (ret, "(b)", &dccm_running);
  g_variant_unref (ret);

  g_object_unref(dbus_proxy);
  if (!dccm_running) {
    synce_info("Udev dccm is not running, ignoring");
    goto ERROR;
  }

  dev_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
					    G_DBUS_PROXY_FLAGS_NONE,
					    NULL, /* GDBusInterfaceInfo */
					    DCCM_SERVICE,
					    synce_info_get_object_path(info),
					    DCCM_DEV_IFACE,
					    NULL, /* GCancellable */
					    &error);
  if (dev_proxy == NULL) {
    g_warning("%s: Failed to get proxy for device '%s': %s", G_STRFUNC, synce_info_get_object_path(info), error->message);
    goto ERROR;
  }


  ret = g_dbus_proxy_call_sync (dev_proxy,
				"RequestConnection",
				g_variant_new ("()"),
				G_DBUS_CALL_FLAGS_NONE,
				-1,
				NULL,
				&error);
  if (!ret) {
    synce_warning("%s: Failed to get a connection for %s: %s", G_STRFUNC, synce_info_get_name(info), error->message);
    g_object_unref(dev_proxy);

    /* INVALID_PASSWORD isn't perfect, but seems to be the best we have */
    if (g_error_matches(error, SYNCE_DCCM_ERROR, SYNCE_DCCM_ERROR_DEVICE_LOCKED))
      result = HRESULT_FROM_WIN32(ERROR_INVALID_PASSWORD);

    goto ERROR;
  }
  g_variant_get (ret, "(s)", &unix_path);
  g_variant_unref (ret);

  g_object_unref(dev_proxy);

  fd = get_socket_from_dccm(unix_path);
  g_free(unix_path);

  if (fd < 0)
  {
    synce_warning("%s: Failed to get file-descriptor from udev dccm for %s", G_STRFUNC, synce_info_get_name(info));
    goto ERROR;
  }

  result = S_OK;
  goto OUT;

ERROR:
  if (error != NULL)
    g_error_free(error);

OUT:

  *sockfd = fd;
  return result;
}

#endif /* ENABLE_UDEV_SUPPORT */


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
     *  original dccm or vdccm, sanity checking
     */
    if (transport == NULL || ( strcmp(transport, "udev") != 0 ) ) {
        pid_t dccm_pid = 0;
        if (!(dccm_pid = synce_info_get_dccm_pid(info)))
        {
            synce_error("DCCM PID entry not found for current connection");
            goto fail;
        }

        if (kill(dccm_pid, 0) < 0)
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

    /*
     *  original dccm or vdccm
     */
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
         *  udev, or proxy ?
         */
#if ENABLE_UDEV_SUPPORT
        if (strcmp(transport, "udev") == 0) {
	  int fd = -1;
	  HRESULT fd_result = get_connection_from_udev(info, &fd);
	  if (fd_result != S_OK)
	  {
	    synce_error("failed to get context fd from udev: %08x: %s", fd_result, synce_strerror(HRESULT_CODE(fd_result)));
	    result = fd_result;
	    goto fail;
	  }
	  synce_socket_take_descriptor(context->socket, fd);
        }
        else
#endif
	if ( !synce_socket_connect_proxy(context->socket, synce_info_get_device_ip(info)) )
        {
            synce_error("failed to connect to proxy for %s", synce_info_get_device_ip(info));
            goto fail;
        }

	/* rapi 2 seems to be used on devices with OS version of 5.1 or greater */

        unsigned int os_major = 0, os_minor = 0;
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

