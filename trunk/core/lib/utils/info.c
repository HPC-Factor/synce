/* $Id$ */
#define _BSD_SOURCE 1
#ifdef HAVE_CONFIG_H
#include "synce_config.h"
#endif
#include "synce.h"
#include "synce_log.h"
#include "config/config.h"
#if ENABLE_ODCCM_SUPPORT || ENABLE_HAL_SUPPORT || ENABLE_UDEV_SUPPORT
#define DBUS_API_SUBJECT_TO_CHANGE 1
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#if ENABLE_HAL_SUPPORT
#include <dbus/dbus-glib-lowlevel.h>
#include <libhal.h>
#endif

struct _SynceInfo
{
  pid_t dccm_pid;
  char* device_ip;
  char* local_iface_ip;
  char* password;
  int key;
  int os_major;
  int os_minor;
  int build_number;
  int processor_type;
  int partner_id_1;
  int partner_id_2;
  char* name;
  char* os_name;
  char* model;
  char* transport;
  char* object_path;
};

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

#define FREE(x)     if(x) free(x)

#if ENABLE_DCCM_FILE_SUPPORT
static char *STRDUP(const char* str)
{
  return str ? strdup(str) : NULL;
}

static SynceInfo* synce_info_from_file(SynceInfoIdField field, const char* data)
{
  SynceInfo* result = calloc(1, sizeof(SynceInfo));
  bool success = false;
  char* connection_filename;
  struct configFile* config = NULL;

  if (data) {
    if (field == INFO_NAME) {
      char *synce_dir;
      if (!synce_get_directory(&synce_dir)) {
        synce_error("unable to determine synce directory");
        goto exit;
      }
      int path_len = strlen(synce_dir) + strlen(data) + 2;
      connection_filename = (char *) malloc(sizeof(char) * path_len);

      if (snprintf(connection_filename, path_len, "%s/%s", synce_dir, data) >= path_len) {
        FREE(synce_dir);
        synce_error("error determining synce connection filename");
        goto exit;
      }
    }
    if (field == INFO_OBJECT_PATH) {
      connection_filename = strdup(data);
    }
  }
  else
    synce_get_connection_filename(&connection_filename);

  config = readConfigFile(connection_filename);
  if (!config)
    {
      synce_error("unable to open file: %s", connection_filename);
      goto exit;
    }

  result->dccm_pid        = getConfigInt(config, "dccm",   "pid");

  result->key             = getConfigInt(config, "device", "key");
  result->os_major        = getConfigInt(config, "device", "os_version");
  result->os_minor        = getConfigInt(config, "device", "os_minor");
  result->build_number    = getConfigInt(config, "device", "build_number");
  result->processor_type  = getConfigInt(config, "device", "processor_type");
  result->partner_id_1    = getConfigInt(config, "device", "partner_id_1");
  result->partner_id_2    = getConfigInt(config, "device", "partner_id_2");

  result->device_ip       = STRDUP(getConfigString(config, "device", "ip"));
  result->local_iface_ip  = NULL;
  result->password  = STRDUP(getConfigString(config, "device", "password"));
  result->name      = STRDUP(getConfigString(config, "device", "name"));
  result->object_path     = STRDUP(connection_filename);

  if (!(result->os_name = STRDUP(getConfigString(config, "device", "os_name"))))
          result->os_name = STRDUP(getConfigString(config, "device", "class"));

  if (!(result->model = STRDUP(getConfigString(config, "device", "model"))))
          result->model = STRDUP(getConfigString(config, "device", "hardware"));

  result->transport = STRDUP(getConfigString(config, "connection", "transport"));

  success = true;

exit:
  FREE(connection_filename);

  if (config)
    unloadConfigFile(config);

  if (success)
    return result;
  else
  {
    synce_info_destroy(result);
    return NULL;
  }
}

#endif /* ENABLE_DCCM_FILE_SUPPORT */

#if ENABLE_ODCCM_SUPPORT || ENABLE_UDEV_SUPPORT

#define DCCM_TYPE_OBJECT_PATH_ARRAY \
  (dbus_g_type_get_collection("GPtrArray", DBUS_TYPE_G_OBJECT_PATH))

static gboolean
synce_info_fields_from_dbus(SynceInfo *result, DBusGProxy *proxy)
{
  GError *error = NULL;
  gchar *name;
  guint os_major;
  guint os_minor;
  guint cpu_type;
  gchar* os_name;
  gchar* model;
  gchar* ip;

  if (!dbus_g_proxy_call(proxy, "GetName", &error,
			 G_TYPE_INVALID,
			 G_TYPE_STRING, &name,
			 G_TYPE_INVALID))
    {
      g_warning("%s: Failed to get device name: %s", G_STRFUNC, error->message);
      goto ERROR;
    }

  result->name = name;

  if (!dbus_g_proxy_call(proxy, "GetOsVersion", &error,
			 G_TYPE_INVALID,
			 G_TYPE_UINT, &os_major,
			 G_TYPE_UINT, &os_minor,
			 G_TYPE_INVALID))
    {
      g_warning("%s: Failed to get device OS for %s: %s", G_STRFUNC, result->name, error->message);
      goto ERROR;
    }
  result->os_major = os_major;
  result->os_minor = os_minor;

  if (!dbus_g_proxy_call(proxy, "GetCpuType", &error,
			 G_TYPE_INVALID,
			 G_TYPE_UINT, &cpu_type,
			 G_TYPE_INVALID))
    {
      g_warning("%s: Failed to get device cpu type for %s: %s", G_STRFUNC, result->name, error->message);
      goto ERROR;
    }
  result->processor_type = cpu_type;

  if (!dbus_g_proxy_call(proxy, "GetPlatformName", &error,
			 G_TYPE_INVALID,
			 G_TYPE_STRING, &os_name,
			 G_TYPE_INVALID))
    {
      g_warning("%s: Failed to get device platform name for %s: %s", result->name, G_STRFUNC, error->message);
      goto ERROR;
    }
  result->os_name = os_name;

  if (!dbus_g_proxy_call(proxy, "GetModelName", &error,
			 G_TYPE_INVALID,
			 G_TYPE_STRING, &model,
			 G_TYPE_INVALID))
    {
      g_warning("%s: Failed to get device model name for %s: %s", result->name, G_STRFUNC, error->message);
      goto ERROR;
    }
  result->model = model;

  if (!dbus_g_proxy_call(proxy, "GetIpAddress", &error,
			 G_TYPE_INVALID,
			 G_TYPE_STRING, &ip,
			 G_TYPE_INVALID))
    {
      g_warning("%s: Failed to get device IP address for %s: %s", result->name, G_STRFUNC, error->message);
      goto ERROR;
    }
  result->device_ip = ip;

  return TRUE;

ERROR:
  if (error != NULL)
    g_error_free(error);
  return FALSE;
}

#endif

#if ENABLE_ODCCM_SUPPORT

static SynceInfo *synce_info_from_odccm(SynceInfoIdField field, const char* data)
{
  SynceInfo *result = NULL;
  GError *error = NULL;
  DBusGConnection *bus = NULL;
  DBusGProxy *dbus_proxy = NULL;
  DBusGProxy *mgr_proxy = NULL;
  GPtrArray *devices = NULL;
  guint i;
  gboolean odccm_running = FALSE;

  g_type_init();

  bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
  if (bus == NULL)
  {
    g_warning("%s: Failed to connect to system bus: %s", G_STRFUNC, error->message);
    goto ERROR;
  }

  dbus_proxy = dbus_g_proxy_new_for_name (bus,
                                          DBUS_SERVICE,
                                          DBUS_PATH,
                                          DBUS_IFACE);
  if (dbus_proxy == NULL) {
    g_warning("%s: Failed to get dbus proxy object", G_STRFUNC);
    goto ERROR;
  }

  if (!(dbus_g_proxy_call(dbus_proxy, "NameHasOwner",
                          &error,
                          G_TYPE_STRING, ODCCM_SERVICE,
                          G_TYPE_INVALID,
                          G_TYPE_BOOLEAN, &odccm_running,
                          G_TYPE_INVALID))) {
          g_critical("%s: Error checking owner of service %s: %s", G_STRFUNC, ODCCM_SERVICE, error->message);
          g_object_unref(dbus_proxy);
          goto ERROR;
  }

  g_object_unref(dbus_proxy);
  if (!odccm_running) {
          g_message("Odccm is not running, ignoring");
          goto ERROR;
  }

  mgr_proxy = dbus_g_proxy_new_for_name(bus, ODCCM_SERVICE,
                                        ODCCM_MGR_PATH,
                                        ODCCM_MGR_IFACE);
  if (mgr_proxy == NULL) {
    g_warning("%s: Failed to get DeviceManager proxy object", G_STRFUNC);
    goto ERROR;
  }

  if (!dbus_g_proxy_call(mgr_proxy, "GetConnectedDevices", &error,
                         G_TYPE_INVALID,
                         DCCM_TYPE_OBJECT_PATH_ARRAY, &devices,
                         G_TYPE_INVALID))
  {
    g_warning("%s: Failed to get devices: %s", G_STRFUNC, error->message);
    goto ERROR;
  }

  if (devices->len == 0) {
    g_message("No devices connected to odccm");
    goto ERROR;
  }

  for (i = 0; i < devices->len; i++) {
    gchar *obj_path = g_ptr_array_index(devices, i);
    gchar *match_data = NULL;
    DBusGProxy *proxy = dbus_g_proxy_new_for_name(bus, ODCCM_SERVICE,
                                                  obj_path,
                                                  ODCCM_DEV_IFACE);
    if (proxy == NULL) {
      g_warning("%s: Failed to get proxy for device '%s'", G_STRFUNC, obj_path);
      goto ERROR;
    }

    if (data != NULL)
    {
      switch (field)
        {
        case INFO_NAME:
          if (!dbus_g_proxy_call(proxy, "GetName", &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &match_data,
                                 G_TYPE_INVALID))
            {
              g_warning("%s: Failed to get device name: %s", G_STRFUNC, error->message);
              g_object_unref(proxy);
              goto ERROR;
            }
          break;
        case INFO_OBJECT_PATH:
          match_data = g_strdup(obj_path);
          break;
        }

      if (strcasecmp(data, match_data) != 0) {
        g_free(match_data);
        continue;
      }
    }

    g_free(match_data);

    if (!(result = calloc(1, sizeof(SynceInfo)))) {
      g_critical("%s: Failed to allocate SynceInfo", G_STRFUNC);
      g_object_unref(proxy);
      goto ERROR;
    }

    result->object_path = g_strdup(obj_path);

    if (!synce_info_fields_from_dbus(result, proxy)) {
      g_object_unref(proxy);
      goto ERROR;
    }

    g_object_unref(proxy);

    result->local_iface_ip = NULL;
    result->transport = g_strdup("odccm");

    break;
  }

  goto OUT;

ERROR:
  if (error != NULL)
    g_error_free(error);
  if (result) synce_info_destroy(result);
  result = NULL;

OUT:
  if (devices != NULL) {
    for (i = 0; i < devices->len; i++)
      g_free(g_ptr_array_index(devices, i));

    g_ptr_array_free(devices, TRUE);
  }

  if (mgr_proxy != NULL)
    g_object_unref (mgr_proxy);
  if (bus != NULL)
    dbus_g_connection_unref (bus);

  return result;
}
#endif /* ENABLE_ODCCM_SUPPORT */


#if ENABLE_UDEV_SUPPORT

static SynceInfo *synce_info_from_udev(SynceInfoIdField field, const char* data)
{
  SynceInfo *result = NULL;
  GError *error = NULL;
  DBusGConnection *bus = NULL;
  DBusGProxy *dbus_proxy = NULL;
  DBusGProxy *mgr_proxy = NULL;
  GPtrArray *devices = NULL;
  guint i;
  gboolean dccm_running = FALSE;

  g_type_init();

  bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
  if (bus == NULL) {
    g_warning("%s: Failed to connect to system bus: %s", G_STRFUNC, error->message);
    goto ERROR;
  }

  dbus_proxy = dbus_g_proxy_new_for_name (bus,
                                          DBUS_SERVICE,
                                          DBUS_PATH,
                                          DBUS_IFACE);
  if (dbus_proxy == NULL) {
    g_warning("%s: Failed to get dbus proxy object", G_STRFUNC);
    goto ERROR;
  }

  if (!(dbus_g_proxy_call(dbus_proxy, "NameHasOwner",
                          &error,
                          G_TYPE_STRING, DCCM_SERVICE,
                          G_TYPE_INVALID,
                          G_TYPE_BOOLEAN, &dccm_running,
                          G_TYPE_INVALID))) {
          g_critical("%s: Error checking owner of service %s: %s", G_STRFUNC, DCCM_SERVICE, error->message);
          g_object_unref(dbus_proxy);
          goto ERROR;
  }

  g_object_unref(dbus_proxy);
  if (!dccm_running) {
          g_message("dccm is not running, ignoring");
          goto ERROR;
  }

  mgr_proxy = dbus_g_proxy_new_for_name(bus, DCCM_SERVICE,
                                        DCCM_MGR_PATH,
                                        DCCM_MGR_IFACE);
  if (mgr_proxy == NULL) {
    g_warning("%s: Failed to get DeviceManager proxy object", G_STRFUNC);
    goto ERROR;
  }

  if (!dbus_g_proxy_call(mgr_proxy, "GetConnectedDevices", &error,
                         G_TYPE_INVALID,
                         DCCM_TYPE_OBJECT_PATH_ARRAY, &devices,
                         G_TYPE_INVALID))
  {
    g_warning("%s: Failed to get devices: %s", G_STRFUNC, error->message);
    goto ERROR;
  }

  if (devices->len == 0) {
    g_message("No devices connected to dccm");
    goto ERROR;
  }

  for (i = 0; i < devices->len; i++) {
    gchar *obj_path = g_ptr_array_index(devices, i);
    gchar *match_data = NULL;
    DBusGProxy *proxy = dbus_g_proxy_new_for_name(bus, DCCM_SERVICE,
                                                  obj_path,
                                                  DCCM_DEV_IFACE);
    if (proxy == NULL) {
      g_warning("%s: Failed to get proxy for device '%s'", G_STRFUNC, obj_path);
      goto ERROR;
    }

    if (data != NULL)
    {
      switch (field)
        {
        case INFO_NAME:
          if (!dbus_g_proxy_call(proxy, "GetName", &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, &match_data,
                                 G_TYPE_INVALID))
            {
              g_warning("%s: Failed to get device name: %s", G_STRFUNC, error->message);
              g_object_unref(proxy);
              goto ERROR;
            }
          break;
        case INFO_OBJECT_PATH:
          match_data = g_strdup(obj_path);
          break;
        }

      if (strcasecmp(data, match_data) != 0) {
        g_free(match_data);
        continue;
      }
    }

    g_free(match_data);

    if (!(result = calloc(1, sizeof(SynceInfo)))) {
      g_critical("%s: Failed to allocate SynceInfo", G_STRFUNC);
      g_object_unref(proxy);
      goto ERROR;
    }

    result->object_path = g_strdup(obj_path);

    if (!synce_info_fields_from_dbus(result, proxy)) {
      g_object_unref(proxy);
      goto ERROR;
    }

    gchar* iface_ip;
    if (!dbus_g_proxy_call(proxy, "GetIfaceAddress", &error,
                           G_TYPE_INVALID,
                           G_TYPE_STRING, &iface_ip,
                           G_TYPE_INVALID))
    {
      g_warning("%s: Failed to get local interface IP address for %s: %s", result->name, G_STRFUNC, error->message);
      g_object_unref(proxy);
      goto ERROR;
    }
    result->local_iface_ip = iface_ip;

    g_object_unref(proxy);

    result->transport = g_strdup("udev");

    break;
  }

  goto OUT;

ERROR:
  if (error != NULL)
    g_error_free(error);
  if (result) synce_info_destroy(result);
  result = NULL;

OUT:
  if (devices != NULL) {
    for (i = 0; i < devices->len; i++)
      g_free(g_ptr_array_index(devices, i));

    g_ptr_array_free(devices, TRUE);
  }

  if (mgr_proxy != NULL)
    g_object_unref (mgr_proxy);
  if (bus != NULL)
    dbus_g_connection_unref (bus);

  return result;
}
#endif /* ENABLE_UDEV_SUPPORT */


#if ENABLE_HAL_SUPPORT

static SynceInfo *synce_info_from_hal(SynceInfoIdField field, const char* data)
{
  SynceInfo *result = NULL;
  DBusGConnection *system_bus = NULL;
  LibHalContext *hal_ctx = NULL;

  GError *error = NULL;
  DBusError dbus_error;

  gint i;
  gchar **device_list = NULL;
  gint num_devices;
  gboolean disabled;
  gchar *match_data = NULL;

  g_type_init();
  dbus_error_init(&dbus_error);

  if (!(system_bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error))) {
    g_critical("%s: Failed to connect to system bus: %s", G_STRFUNC, error->message);
    goto error_exit;
  }

  if (!(hal_ctx = libhal_ctx_new())) {
    g_critical("%s: Failed to get hal context", G_STRFUNC);
    goto error_exit;
  }

  if (!libhal_ctx_set_dbus_connection(hal_ctx, dbus_g_connection_get_connection(system_bus))) {
    g_critical("%s: Failed to set DBus connection for hal context", G_STRFUNC);
    goto error_exit;
  }

  if (!libhal_ctx_init(hal_ctx, &dbus_error)) {
    g_critical("%s: Failed to initialise hal context: %s: %s", G_STRFUNC, dbus_error.name, dbus_error.message);
    goto error_exit;
  }

  device_list = libhal_manager_find_device_string_match(hal_ctx,
							"pda.platform",
							"pocketpc",
							&num_devices,
							&dbus_error);
  if (dbus_error_is_set(&dbus_error)) {
    g_warning("%s: Failed to obtain list of attached devices: %s: %s", G_STRFUNC, dbus_error.name, dbus_error.message);
    goto error_exit;
  }

  if (num_devices == 0) {
    g_message("Hal reports no devices connected");
    goto exit;
  }

  for (i = 0; i < num_devices; i++) {

    /* discard unused ports for 4 endpoint serial devices */
    if (libhal_device_property_exists(hal_ctx, device_list[i], "pda.pocketpc.disabled", &dbus_error)) {

            disabled = libhal_device_get_property_bool(hal_ctx, device_list[i], "pda.pocketpc.disabled", &dbus_error);
            if (dbus_error_is_set(&dbus_error)) {
                    g_critical("%s: Failed to obtain property pda.pocketpc.disabled for device %s: %s: %s",
                               G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
                    goto error_exit;
            }

            if (disabled)
                    continue;
    }

    if (!(libhal_device_property_exists(hal_ctx, device_list[i], "pda.pocketpc.name", &dbus_error))) {
            if (dbus_error_is_set(&dbus_error)) {
                    g_critical("%s: Failed to check for property pda.pocketpc.name for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
                    goto error_exit;
            }
            g_message("Device %s not fully set in Hal, skipping", device_list[i]);
            continue;
    }


    if (data != NULL)
    {
      switch (field)
        {
        case INFO_NAME:

          match_data = libhal_device_get_property_string(hal_ctx, device_list[i], "pda.pocketpc.name", &dbus_error);
          if (dbus_error_is_set(&dbus_error)) {
                  g_critical("%s: Failed to obtain property pda.pocketpc.name for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
                  goto error_exit;
          }
          break;
        case INFO_OBJECT_PATH:
          match_data = g_strdup(device_list[i]);
          break;
        }

      if (strcasecmp(data, match_data) != 0) {
        libhal_free_string(match_data);
        continue;
      }
    }

    libhal_free_string(match_data);


    if (!(result = calloc(1, sizeof(SynceInfo)))) {
      g_critical("%s: Failed to allocate SynceInfo", G_STRFUNC);
      goto error_exit;
    }

    result->name = libhal_device_get_property_string(hal_ctx, device_list[i], "pda.pocketpc.name", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_critical("%s: Failed to obtain property pda.pocketpc.name for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      goto error_exit;
    }

    result->object_path = g_strdup(device_list[i]);

    result->os_major = libhal_device_get_property_uint64(hal_ctx, device_list[i], "pda.pocketpc.os_major", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_critical("%s: Failed to obtain property pda.pocketpc.os_major for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      goto error_exit;
    }

    result->os_minor = libhal_device_get_property_uint64(hal_ctx, device_list[i], "pda.pocketpc.os_minor", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_critical("%s: Failed to obtain property pda.pocketpc.os_minor for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      goto error_exit;
    }

    result->processor_type = libhal_device_get_property_uint64(hal_ctx, device_list[i], "pda.pocketpc.cpu_type", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_critical("%s: Failed to obtain property pda.pocketpc.cpu_type for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      goto error_exit;
    }

    result->device_ip = libhal_device_get_property_string(hal_ctx, device_list[i], "pda.pocketpc.ip_address", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_critical("%s: Failed to obtain property pda.pocketpc.ip_address for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      goto error_exit;
    }

    result->local_iface_ip = libhal_device_get_property_string(hal_ctx, device_list[i], "pda.pocketpc.iface_address", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_warning("%s: Failed to obtain property pda.pocketpc.iface_address for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      result->local_iface_ip = NULL;
      dbus_error_free(&dbus_error);
    }

    result->os_name = libhal_device_get_property_string(hal_ctx, device_list[i], "pda.pocketpc.platform", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_critical("%s: Failed to obtain property pda.pocketpc.platform for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      goto error_exit;
    }

    result->model = libhal_device_get_property_string(hal_ctx, device_list[i], "pda.pocketpc.model", &dbus_error);
    if (dbus_error_is_set(&dbus_error)) {
      g_critical("%s: Failed to obtain property pda.pocketpc.model for device %s: %s: %s", G_STRFUNC, device_list[i], dbus_error.name, dbus_error.message);
      goto error_exit;
    }

    result->transport = g_strdup("hal");

    break;
  }

  goto exit;

error_exit:
  if (error != NULL)
    g_error_free(error);
  if (dbus_error_is_set(&dbus_error))
    dbus_error_free(&dbus_error);
  if (result)
    synce_info_destroy(result);
  result = NULL;

exit:
  if (device_list != NULL)
    libhal_free_string_array(device_list);
  if (hal_ctx != NULL) {
    libhal_ctx_shutdown(hal_ctx, NULL);
    libhal_ctx_free(hal_ctx);
  }
  if (system_bus != NULL)
    dbus_g_connection_unref (system_bus);

  return result;
}
#endif /* ENABLE_HAL_SUPPORT */

SynceInfo* synce_info_new(const char* device_name)
{
  return synce_info_new_by_field(INFO_NAME, device_name);
}

SynceInfo* synce_info_new_by_field(SynceInfoIdField field, const char* data)
{
  SynceInfo* result = NULL;

#if ENABLE_UDEV_SUPPORT
  result = synce_info_from_udev(field, data);
#endif

#if ENABLE_HAL_SUPPORT
  if (!result)
    result = synce_info_from_hal(field, data);
#endif

#if ENABLE_ODCCM_SUPPORT
  if (!result)
    result = synce_info_from_odccm(field, data);

#if ENABLE_MIDASYNC
  if (!result)
    result = synce_info_from_midasyncd(field, data);
#endif
#endif

#if ENABLE_DCCM_FILE_SUPPORT
  if (!result)
    result = synce_info_from_file(field, data);
#endif

  return result;
}


void synce_info_destroy(SynceInfo* info)
{
  if (info)
  {
    FREE(info->device_ip);
    FREE(info->local_iface_ip);
    FREE(info->password);
    FREE(info->name);
    FREE(info->os_name);
    FREE(info->model);
    FREE(info->transport);
    FREE(info->object_path);
    free(info);
  }
}

const char *
synce_info_get_name(SynceInfo *info)
{
  if (!info) return NULL;
  return info->name;
}

bool
synce_info_get_os_version(SynceInfo *info, int *os_major, int *os_minor)
{
  if (!info) return NULL;
  if ((!os_major) || (!os_minor))
    return false;

  *os_major = info->os_major;
  *os_minor = info->os_minor;
  return true;
}

int
synce_info_get_build_number(SynceInfo *info)
{
  if (!info) return 0;
  return info->build_number;
}

int
synce_info_get_processor_type(SynceInfo *info)
{
  if (!info) return 0;
  return info->processor_type;
}

const char *
synce_info_get_os_name(SynceInfo *info)
{
  if (!info) return NULL;
  return info->os_name;
}

const char *
synce_info_get_model(SynceInfo *info)
{
  if (!info) return NULL;
  return info->model;
}

const char *
synce_info_get_device_ip(SynceInfo *info)
{
  if (!info) return NULL;
  return info->device_ip;
}

const char *
synce_info_get_local_ip(SynceInfo *info)
{
  if (!info) return NULL;
  return info->local_iface_ip;
}

int
synce_info_get_partner_id_1(SynceInfo *info)
{
  if (!info) return 0;
  return info->partner_id_1;
}

int
synce_info_get_partner_id_2(SynceInfo *info)
{
  if (!info) return 0;
  return info->partner_id_2;
}

const char *
synce_info_get_object_path(SynceInfo *info)
{
  if (!info) return NULL;
  return info->object_path;
}

pid_t
synce_info_get_dccm_pid(SynceInfo *info)
{
  if (!info) return 0;
  return info->dccm_pid;
}

const char *
synce_info_get_transport(SynceInfo *info)
{
  if (!info) return NULL;
  return info->transport;
}

const char *
synce_info_get_password(SynceInfo *info)
{
  if (!info) return NULL;
  return info->password;
}

int
synce_info_get_key(SynceInfo *info)
{
  if (!info) return 0;
  return info->key;
}