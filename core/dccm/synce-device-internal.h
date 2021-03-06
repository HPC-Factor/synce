#ifndef SYNCE_DEVICE_INTERNAL_H
#define SYNCE_DEVICE_INTERNAL_H

#include <glib-object.h>
#include <gio/gio.h>

#include "synce-connection-broker.h"

G_BEGIN_DECLS

typedef enum {
  CTRL_STATE_HANDSHAKE,
  CTRL_STATE_GETTING_INFO,
  CTRL_STATE_GOT_INFO,
  CTRL_STATE_AUTH,
  CTRL_STATE_CONNECTED,
} ControlState;

typedef enum {
  SYNCE_DEVICE_PASSWORD_FLAG_UNSET             = 0,
  SYNCE_DEVICE_PASSWORD_FLAG_PROVIDE           = 1,
  SYNCE_DEVICE_PASSWORD_FLAG_PROVIDE_ON_DEVICE = 2,
  SYNCE_DEVICE_PASSWORD_FLAG_CHECKING          = 3,
  SYNCE_DEVICE_PASSWORD_FLAG_UNLOCKED          = 4,
} SynceDevicePasswordFlags;

typedef struct _SynceDevicePrivate SynceDevicePrivate;
struct _SynceDevicePrivate
{
  gboolean dispose_has_run;

  GSocketConnection *conn;
  gchar *iobuf;

  /* the sysfs path */
  gchar *device_path;

  gchar *guid;
  guint os_major;
  guint os_minor;
  gchar *name;
  guint version;
  guint cpu_type;
  guint cur_partner_id;
  guint id;
  gchar *platform_name;
  gchar *model_name;
  gchar *ip_address;
  gchar *iface_address;

  /* state */
  ControlState state;
  gint32 info_buf_size;
  SynceDevicePasswordFlags pw_flags;

  guint32 pw_key;

  /* the dbus object path */
  gchar *obj_path;

  SynceDbusDevice *interface;
  GDBusMethodInvocation *pw_ctx;

  GHashTable *requests;
  guint req_id;
};

#define SYNCE_DEVICE_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE((o), SYNCE_TYPE_DEVICE, SynceDevicePrivate))

/* header information */

gboolean synce_device_provide_password (SynceDbusDevice *interface, GDBusMethodInvocation *invocation, const gchar *password, gpointer userdata);
gboolean synce_device_request_connection (SynceDbusDevice *interface, GDBusMethodInvocation *invocation, gpointer userdata);
void synce_device_change_password_flags (SynceDevice *self, SynceDevicePasswordFlags new_flag);
void synce_device_conn_broker_done_cb (SynceConnectionBroker *broker, gpointer user_data);
void synce_device_dbus_init(SynceDevice *self);
void synce_device_conn_event_cb(GObject *istream, GAsyncResult *res, gpointer user_data);

G_END_DECLS

#endif /* SYNCE_DEVICE_INTERNAL_H */
