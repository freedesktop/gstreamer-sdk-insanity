#include "config.h"

#include <dbus/dbus.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "insanity.h"

/* TODO:
  - indent, this originally came from sample code that had 3 spaces indent
  - remove exit calls and do proper error reporting
  - logs ?
  - gather timings at every step validated ?
*/

/* getrusage is Unix API */
#define USE_CPU_LOAD

#ifdef USE_CPU_LOAD
#include <sys/time.h>
#include <sys/resource.h>
#endif

/* if global vars are good enough for gstreamer, it's good enough for insanity */
static guint setup_signal;
static guint start_signal;
static guint stop_signal;

struct InsanityTestPrivateData {
  DBusConnection *conn;
#ifdef USE_CPU_LOAD
  struct timeval start;
  struct rusage rusage;
#endif
  char name[128];
  DBusMessage *args;
  int cpu_load;
  gboolean done;
};

static void
insanity_cclosure_marshal_BOOLEAN__VOID (GClosure     *closure,
                                   GValue       *return_value G_GNUC_UNUSED,
                                   guint         n_param_values,
                                   const GValue *param_values,
                                   gpointer      invocation_hint G_GNUC_UNUSED,
                                   gpointer      marshal_data)
{
  typedef gboolean (*GMarshalFunc_BOOLEAN__VOID) (gpointer     data1,
                                                  gpointer     data2);
  register GMarshalFunc_BOOLEAN__VOID callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;
  gboolean v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 1);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  }
  else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback = (GMarshalFunc_BOOLEAN__VOID) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1, data2);

  g_value_set_boolean (return_value, v_return);
}

static gboolean default_insanity_user_setup(InsanityTest *test)
{
  (void)test;
  printf("insanity_setup\n");
  return TRUE;
}

static gboolean default_insanity_user_start(InsanityTest *test)
{
  (void)test;
  printf("insanity_start\n");
  return TRUE;
}

static void default_insanity_user_stop(InsanityTest *test)
{
  (void)test;
  printf("insanity_stop\n");
}

static void insanity_test_connect (InsanityTest *test, DBusConnection *conn, const char *uuid)
{
  if (test->priv->conn)
    dbus_connection_unref (test->priv->conn);
  test->priv->conn = dbus_connection_ref (conn);
  snprintf(test->priv->name, sizeof(test->priv->name), "/net/gstreamer/Insanity/Test/Test%s", uuid);
}

static void insanity_test_set_args (InsanityTest *test, DBusMessage *msg)
{
  if (test->priv->args) {
    dbus_message_unref (test->priv->args);
    test->priv->args = NULL;
  }
  if (msg) {
    test->priv->args = dbus_message_ref (msg);
  }
}

static void insanity_test_record_start_time (InsanityTest *test)
{
#ifdef USE_CPU_LOAD
  gettimeofday(&test->priv->start,NULL);
  getrusage(RUSAGE_SELF, &test->priv->rusage);
#endif
}

#ifdef USE_CPU_LOAD
static long tv_us_diff(const struct timeval *t0, const struct timeval *t1)
{
  return (t1->tv_sec - t0->tv_sec) * 1000000 + t1->tv_usec - t0->tv_usec;
}
#endif

static void insanity_test_record_stop_time(InsanityTest *test)
{
#ifdef USE_CPU_LOAD
  struct rusage rusage;
  struct timeval end;
  unsigned long us;

  getrusage(RUSAGE_SELF, &rusage);
  gettimeofday(&end,NULL);
  us = tv_us_diff(&test->priv->rusage.ru_utime, &rusage.ru_utime)
     + tv_us_diff(&test->priv->rusage.ru_stime, &rusage.ru_stime);
  test->priv->cpu_load = 100 * us / tv_us_diff (&test->priv->start, &end);
#endif
}

// TODO: add the full API
#define INSANITY_TEST_INTERFACE "net.gstreamer.Insanity.Test"
static const char *introspect_response_template=" \
  <!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \
  \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\"> \
  <node name=\"/net/gstreamer/Insanity/Test/Test%s\"> \
    <interface name=\"org.freedesktop.DBus.Introspectable\"> \
      <method name=\"Introspect\"> \
        <arg direction=\"out\" type=\"s\" /> \
      </method> \
    </interface> \
    <interface name=\""INSANITY_TEST_INTERFACE"\"> \
      <method name=\"remoteSetUp\"> \
        <arg direction=\"in\" type=\"a{sv}\" /> \
      </method> \
    </interface> \
  </node> \
";

static void send_signal(DBusConnection *conn, const char *signal_name, const char *path_name, int type,...)
{
   DBusMessage *msg;
   dbus_uint32_t serial = 0;
   va_list ap;

   msg = dbus_message_new_signal(path_name, INSANITY_TEST_INTERFACE, signal_name);
   if (NULL == msg) 
   { 
      fprintf(stderr, "Message Null\n"); 
      exit(1); 
   }

  // append any arguments onto signal
  if (type != DBUS_TYPE_INVALID) {
    va_start (ap, type);
    if (!dbus_message_append_args_valist (msg, type, ap)) {
      fprintf(stderr, "Out Of Memory!\n"); 
      exit(1);
    }
    va_end(ap);
  }

   // send the message and flush the connection
   if (!dbus_connection_send(conn, msg, &serial)) {
      fprintf(stderr, "Out Of Memory!\n"); 
      exit(1);
   }
   dbus_connection_flush(conn);
   
   //printf("Signal %s sent from %s\n", signal_name, path_name);
   
   // free the message and close the connection
   dbus_message_unref(msg);
}

void insanity_test_validate_step(InsanityTest *test, const char *name, gboolean success)
{
  send_signal (test->priv->conn,"remoteValidateStepSignal",test->priv->name,DBUS_TYPE_STRING,&name,DBUS_TYPE_BOOLEAN,&success,DBUS_TYPE_INVALID);
}

void insanity_test_add_extra_info(InsanityTest *test, const char *name, const GValue *data)
{
  GType glib_type;
  int dbus_type;
  dbus_int32_t int32_value;
  dbus_int64_t int64_value;
  const char *string_value;
  void *dataptr = NULL;

  glib_type = G_VALUE_TYPE (data);
  if (glib_type == G_TYPE_INT) {
    int32_value = g_value_get_int (data);
    dbus_type = DBUS_TYPE_INT32;
    dataptr = &int32_value;
  } else if (glib_type == G_TYPE_INT64) {
    int64_value = g_value_get_int64 (data);
    dbus_type = DBUS_TYPE_INT64;
    dataptr = &int64_value;
  } else if (glib_type == G_TYPE_STRING) {
    string_value = g_value_get_string (data);
    dbus_type = DBUS_TYPE_STRING;
    dataptr = &string_value;
  } else {
    /* Add more if needed, there doesn't seem to be a glib "glib to dbus" conversion public API,
       but if I missed one, it could replace the above. */
  }

  if (dataptr) {
    send_signal (test->priv->conn,"remoteExtraInfoSignal",test->priv->name,DBUS_TYPE_STRING,&name,dbus_type,dataptr,DBUS_TYPE_INVALID);
  }
  else {
    char *s = g_strdup_value_contents (data);
    fprintf(stderr, "Unsupported extra info: %s\n", s);
    g_free (s);
  }
}

static void gather_end_of_test_info(InsanityTest *test)
{
  GValue value;

  insanity_test_record_stop_time(test);

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, test->priv->cpu_load);
  insanity_test_add_extra_info (test, "cpu-load", &value);
  g_value_unset (&value);
}

void insanity_test_done(InsanityTest *test)
{
  gather_end_of_test_info(test);
  send_signal (test->priv->conn, "remoteStopSignal", test->priv->name, DBUS_TYPE_INVALID);
}

static gboolean on_setup(InsanityTest *test)
{
  gboolean ret = TRUE;
  g_signal_emit (test, setup_signal, 0, &ret);
  if (!ret) {
    send_signal (test->priv->conn, "remoteStopSignal", test->priv->name, DBUS_TYPE_INVALID);
  }
  else {
    send_signal (test->priv->conn, "remoteReadySignal", test->priv->name, DBUS_TYPE_INVALID);
  }
  return ret;
}

static gboolean on_start(InsanityTest *test)
{
  gboolean ret = TRUE;
  insanity_test_record_start_time(test);
  g_signal_emit (test, start_signal, 0, &ret);
  return ret;
}

static void on_stop(InsanityTest *test)
{
  gboolean ret = TRUE;
  g_signal_emit (test, stop_signal, 0, &ret);

  gather_end_of_test_info(test);
  test->priv->done = TRUE;
}

static int foreach_dbus_array (DBusMessageIter *iter, int (*f)(const char *key, int type, void *value, guintptr userdata), guintptr userdata)
{
  DBusMessageIter subiter, subsubiter, subsubsubiter;
  const char *key;
  const char *string_value;
  dbus_uint32_t uint32_value;
  dbus_int32_t int32_value;
  int boolean_value;
  dbus_uint64_t uint64_value;
  dbus_int64_t int64_value;
  double double_value;
  DBusMessageIter array_value;
  int type;
  int ret;
  void *ptr;

  type = dbus_message_iter_get_arg_type (iter);
  if (type != DBUS_TYPE_ARRAY) {
    fprintf(stderr, "Expected array, got %c\n", type);
    exit(1);
  }
  dbus_message_iter_recurse (iter, &subiter);
  do {
    type = dbus_message_iter_get_arg_type (&subiter);
    if (type != DBUS_TYPE_DICT_ENTRY) {
      fprintf(stderr, "Expected dict entry, got %c\n", type);
      exit(1);
    }
    dbus_message_iter_recurse (&subiter, &subsubiter);

    type = dbus_message_iter_get_arg_type (&subsubiter);
    if (type != DBUS_TYPE_STRING) {
      fprintf(stderr, "Expected string, got %c\n", type);
      exit(1);
    }
    dbus_message_iter_get_basic (&subsubiter,&key);
    if (!dbus_message_iter_next (&subsubiter)) {
      fprintf(stderr, "Value not present\n");
      exit(1);
    }
    type = dbus_message_iter_get_arg_type (&subsubiter);
    if (type == DBUS_TYPE_STRING) {
      dbus_message_iter_get_basic (&subsubiter,&string_value);
      ptr = &string_value;
    }
    else if (type == DBUS_TYPE_VARIANT) {
      dbus_message_iter_recurse (&subsubiter, &subsubsubiter);

      type = dbus_message_iter_get_arg_type (&subsubsubiter);

      switch (type) {
        case DBUS_TYPE_STRING:
          dbus_message_iter_get_basic (&subsubsubiter,&string_value);
          ptr = &string_value;
          break;
        case DBUS_TYPE_INT32:
          dbus_message_iter_get_basic (&subsubsubiter,&int32_value);
          ptr = &int32_value;
          break;
        case DBUS_TYPE_UINT32:
          dbus_message_iter_get_basic (&subsubsubiter,&uint32_value);
          ptr = &uint32_value;
          break;
        case DBUS_TYPE_INT64:
          dbus_message_iter_get_basic (&subsubsubiter,&int64_value);
          ptr = &int64_value;
          break;
        case DBUS_TYPE_UINT64:
          dbus_message_iter_get_basic (&subsubsubiter,&uint64_value);
          ptr = &uint64_value;
          break;
        case DBUS_TYPE_DOUBLE:
          dbus_message_iter_get_basic (&subsubsubiter,&double_value);
          ptr = &double_value;
          break;
        case DBUS_TYPE_BOOLEAN:
          dbus_message_iter_get_basic (&subsubsubiter,&boolean_value);
          ptr = &boolean_value;
          break;
        case DBUS_TYPE_ARRAY:
          array_value = subsubsubiter;
          ptr = &array_value;
          break;
        default:
          fprintf(stderr, "Unsupported type: %c\n", type);
          exit(1);
          break;
      }
    }
    else {
      fprintf(stderr, "Expected variant, got %c\n", type);
      exit(1);
    }

    /* < 0 -> error, 0 -> continue, > 0 -> stop */
    ret = (*f)(key, type, ptr, userdata);
    if (ret)
      return ret;

  } while (dbus_message_iter_next (&subiter));

  return 0;
}

int foreach_dbus_args (InsanityTest *test, int (*f)(const char *key, int type, void *value, guintptr userdata), guintptr userdata)
{
  DBusMessageIter iter;

  dbus_message_iter_init (test->priv->args, &iter);
  return foreach_dbus_array (&iter, f, userdata);
}

struct finder_data {
  const char *key;
  int type;
  void *value;
};

static int typed_finder(const char *key, int type, void *value, guintptr userdata)
{
  struct finder_data *fd = (struct finder_data *)userdata;
  if (strcmp (key, fd->key))
    return 0;
  if (type != fd->type) {
    fprintf(stderr, "Key '%s' was found, but not of the expected type (was %c, expected %c)\n", key, type, fd->type);
    return -1;
  }
  fd->value = value;
  return 1;
}

const char *insanity_test_get_string_argument(InsanityTest *test, const char *key)
{
  struct finder_data fd;
  int ret;

  fd.key = key;
  fd.type = DBUS_TYPE_STRING;
  fd.value = NULL;
  ret = foreach_dbus_args(test, &typed_finder, (guintptr)&fd);
  return (ret>0 && fd.value) ? * (const char **)fd.value : NULL;
}

const char *insanity_test_get_output_filename(InsanityTest *test, const char *key)
{
  struct finder_data fd;
  int ret;
  DBusMessageIter array;

  fd.key = "outputfiles";
  fd.type = DBUS_TYPE_ARRAY;
  fd.value = NULL;
  ret = foreach_dbus_args(test, &typed_finder, (guintptr)&fd);
  if (ret <= 0)
    return NULL;

  array = *(DBusMessageIter*)fd.value;
  fd.key = key;
  fd.type = DBUS_TYPE_STRING;
  fd.value = NULL;
  ret = foreach_dbus_array (&array, &typed_finder, (guintptr)&fd);
  return (ret>0 && fd.value) ? * (const char **)fd.value : NULL;
}

static void insanity_test_dbus_handler_remoteSetup(InsanityTest *test, DBusMessage *msg)
{
  insanity_test_set_args (test, msg);
  on_setup(test);
}

static void insanity_test_dbus_handler_remoteStart(InsanityTest *test, DBusMessage *msg)
{
  (void)msg;
  on_start(test);
}

static void insanity_test_dbus_handler_remoteStop(InsanityTest *test, DBusMessage *msg)
{
  (void)msg;
  on_stop(test);
}

static const struct {
  const char *method;
  void (*handler)(InsanityTest*, DBusMessage*);
} dbus_test_handlers[] = {
  {"remoteSetUp", &insanity_test_dbus_handler_remoteSetup},
  {"remoteStart", &insanity_test_dbus_handler_remoteStart},
  {"remoteStop", &insanity_test_dbus_handler_remoteStop},
  {"remoteInfo", NULL},
};

static gboolean insanity_call_interface (InsanityTest *test, DBusMessage *msg)
{
  size_t n;
  const char *method = dbus_message_get_member (msg);
  for (n=0; n<sizeof(dbus_test_handlers)/sizeof(dbus_test_handlers[0]); ++n) {
    if (!strcmp (method, dbus_test_handlers[n].method)) {
      dbus_uint32_t serial = 0;
      DBusMessage *reply = dbus_message_new_method_return(msg);
      if (!dbus_connection_send(test->priv->conn, reply, &serial)) {
         fprintf(stderr, "Out Of Memory!\n"); 
         exit(1);
      }
      dbus_connection_flush(test->priv->conn);
      dbus_message_unref(reply);

      if (dbus_test_handlers[n].handler)
        (*dbus_test_handlers[n].handler)(test, msg);
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean listen(InsanityTest *test, const char *bus_address,const char *uuid)
{
   DBusMessage* msg;
   DBusMessage* reply;
   DBusMessageIter args;
   DBusConnection* conn;
   DBusError err;
   int ret;
   char object_name[128];
   dbus_uint32_t serial = 0;

   // initialise the error
   dbus_error_init(&err);
   
   // connect to the bus and check for errors
   conn = dbus_connection_open(bus_address, &err);
   if (dbus_error_is_set(&err)) { 
      fprintf(stderr, "Connection Error (%s)\n", err.message); 
      dbus_error_free(&err); 
   }
   if (NULL == conn) {
      fprintf(stderr, "Connection Null\n"); 
      exit(1); 
   }

   ret = dbus_bus_register (conn, &err);
   if (dbus_error_is_set(&err)) { 
      fprintf(stderr, "Failed to register bus (%s)\n", err.message); 
      dbus_error_free(&err); 
   }

   // request our name on the bus and check for errors
   snprintf(object_name, sizeof(object_name), INSANITY_TEST_INTERFACE ".Test%s", uuid);
   //printf("Using object name %s\n",object_name);
   ret = dbus_bus_request_name(conn, object_name, DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
   if (dbus_error_is_set(&err)) { 
      fprintf(stderr, "Name Error (%s)\n", err.message); 
      dbus_error_free(&err);
   }
   if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) { 
      fprintf(stderr, "Not Primary Owner (%d)\n", ret);
      exit(1); 
   }

   insanity_test_connect (test, conn, uuid);

   // loop, testing for new messages
   test->priv->done = FALSE;
   while (1) {
      // barely blocking update of dbus
      dbus_connection_read_write(conn, 100);

      if (test->priv->done)
        break;

      // see if we have a message to handle
      msg = dbus_connection_pop_message(conn);
      if (NULL == msg) {
         continue; 
      }
      
#if 0
      printf("Got message:\n");
      printf("  type %d\n", dbus_message_get_type (msg));
      printf("  path %s\n", dbus_message_get_path (msg));
      printf("  interface %s\n", dbus_message_get_interface (msg));
      printf("  member %s\n", dbus_message_get_member (msg));
      printf("  sender %s\n", dbus_message_get_sender (msg));
      printf("  destination %s\n", dbus_message_get_destination (msg));
      printf("  signature %s\n", dbus_message_get_signature (msg));
#endif

      // check this is a method call for the right interface & method
      if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect"))  {
        char *introspect_response = malloc (strlen(introspect_response_template)+strlen(uuid)+1);
        sprintf (introspect_response, introspect_response_template, uuid);
        //printf("Got 'Introspect', answering introspect response\n");
        reply = dbus_message_new_method_return(msg);
        dbus_message_iter_init_append(reply, &args);
        if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &introspect_response)) { 
           fprintf(stderr, "Out Of Memory!\n"); 
           exit(1);
        }
        free (introspect_response);
        if (!dbus_connection_send(conn, reply, &serial)) {
           fprintf(stderr, "Out Of Memory!\n"); 
           exit(1);
        }
        dbus_connection_flush(conn);
        dbus_message_unref(reply);
      }
      else if (!strcmp (dbus_message_get_interface (msg), INSANITY_TEST_INTERFACE)) {
        insanity_call_interface (test, msg);
      }
      else {
        //printf("Got unhandled method call: interface %s, method %s\n", dbus_message_get_interface(msg), dbus_message_get_member(msg));
      }

      // free the message
      dbus_message_unref(msg);
   }

   dbus_connection_unref (conn);

  return TRUE;
}

gboolean insanity_test_run(InsanityTest *test, int argc, const char **argv)
{
  const char *private_dbus_address;
  const char *uuid;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <uuid>\n", argv[0]);
    return FALSE;
  }
  uuid = argv[1];
  private_dbus_address = getenv("PRIVATE_DBUS_ADDRESS");
  if (!private_dbus_address || !private_dbus_address[0]) {
    fprintf(stderr, "The PRIVATE_DBUS_ADDRESS environment variable must be set\n");
    return FALSE;
  }
#if 0
  printf("uuid: %s\n", uuid);
  printf("PRIVATE_DBUS_ADDRESS: %s\n",private_dbus_address);
#endif
  return listen(test, private_dbus_address, uuid);
}



G_DEFINE_TYPE (InsanityTest, insanity_test, G_TYPE_OBJECT);

static void insanity_test_finalize (GObject *gobject)
{
  InsanityTest *test = (InsanityTest *)gobject;
  InsanityTestPrivateData *priv = test->priv;
  if (priv->args)
    dbus_message_unref(priv->args);
  if (priv->conn)
    dbus_connection_unref(priv->conn);
  G_OBJECT_CLASS (insanity_test_parent_class)->finalize (gobject);
}

static void insanity_test_init (InsanityTest *test)
{
  InsanityTestPrivateData *priv = G_TYPE_INSTANCE_GET_PRIVATE (test,
      INSANITY_TEST_TYPE, InsanityTestPrivateData);

  test->priv = priv;
  priv->conn = NULL;
  strcpy (priv->name, "");
  priv->args = NULL;
  priv->done = FALSE;
}

static gboolean insanity_signal_stop_accumulator (GSignalInvocationHint *ihint,
                                                  GValue *return_accu,
                                                  const GValue *handler_return,
                                                  gpointer data)
{
  gboolean v;

  (void)ihint;
  (void)data;
  v = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, v);
  return v;
}

static void insanity_test_class_init (InsanityTestClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = insanity_test_finalize;

  klass->setup = &default_insanity_user_setup;
  klass->start = &default_insanity_user_start;
  klass->stop = &default_insanity_user_stop;

  g_type_class_add_private (klass, sizeof (InsanityTestPrivateData));

  setup_signal = g_signal_new ("setup",
                 G_TYPE_FROM_CLASS (gobject_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                 G_STRUCT_OFFSET (InsanityTestClass, setup),
                 &insanity_signal_stop_accumulator, NULL,
                 insanity_cclosure_marshal_BOOLEAN__VOID,
                 G_TYPE_BOOLEAN /* return_type */,
                 0, NULL);
  start_signal = g_signal_new ("start",
                 G_TYPE_FROM_CLASS (gobject_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                 G_STRUCT_OFFSET (InsanityTestClass, start),
                 &insanity_signal_stop_accumulator, NULL,
                 insanity_cclosure_marshal_BOOLEAN__VOID,
                 G_TYPE_BOOLEAN /* return_type */,
                 0, NULL);
   stop_signal = g_signal_new ("stop",
                 G_TYPE_FROM_CLASS (gobject_class),
                 G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                 G_STRUCT_OFFSET (InsanityTestClass, stop),
                 NULL, NULL,
                 g_cclosure_marshal_VOID__VOID,
                 G_TYPE_BOOLEAN /* return_type */,
                 0, NULL);
}

