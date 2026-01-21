/* FunPay Vertex core event bus implementation. */

#include "fpv_core/fpv_event_bus.h"

#include <stdlib.h>

#include "core/base/fpv_platform.h"

#include "core/base/fpv_string.h"


typedef struct fpv_event_node {
  fpv_event_t* event;
  struct fpv_event_node* next;
} fpv_event_node_t;

struct fpv_event_bus {
  fpv_event_node_t* head;
  fpv_event_node_t* tail;
  size_t count;
  fpv_mutex_t mutex;
};

static fpv_event_node_t* fpv_event_node_create(fpv_event_t* event) {
  fpv_event_node_t* node = (fpv_event_node_t*)calloc(1, sizeof(*node));
  if (!node) {
    return NULL;
  }
  node->event = event;
  return node;
}

fpv_event_bus_t* fpv_event_bus_create(void) {
  fpv_event_bus_t* bus = (fpv_event_bus_t*)calloc(1, sizeof(*bus));
  if (!bus) {
    return NULL;
  }
  if (!fpv_mutex_init(&bus->mutex)) {
    free(bus);
    return NULL;
  }
  return bus;
}

void fpv_event_bus_destroy(fpv_event_bus_t* bus) {
  fpv_event_node_t* node = NULL;

  if (!bus) {
    return;
  }

  fpv_mutex_lock(&bus->mutex);
  node = bus->head;
  bus->head = NULL;
  bus->tail = NULL;
  bus->count = 0;
  fpv_mutex_unlock(&bus->mutex);

  while (node) {
    fpv_event_node_t* next = node->next;
    fpv_event_destroy(node->event);
    free(node);
    node = next;
  }

  fpv_mutex_destroy(&bus->mutex);
  free(bus);
}

fpv_result_t fpv_event_bus_publish(fpv_event_bus_t* bus, fpv_event_t* event) {
  fpv_event_node_t* node = NULL;

  if (!bus || !event) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  node = fpv_event_node_create(event);
  if (!node) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_mutex_lock(&bus->mutex);
  if (bus->tail) {
    bus->tail->next = node;
  } else {
    bus->head = node;
  }
  bus->tail = node;
  bus->count++;
  fpv_mutex_unlock(&bus->mutex);

  return FPV_OK;
}

fpv_event_batch_t fpv_event_bus_drain(fpv_event_bus_t* bus) {
  fpv_event_batch_t batch;
  fpv_event_node_t* node = NULL;
  fpv_event_t** events = NULL;
  size_t count = 0;
  size_t index = 0;

  batch.events = NULL;
  batch.count = 0;

  if (!bus) {
    return batch;
  }

  fpv_mutex_lock(&bus->mutex);
  if (bus->count == 0) {
    fpv_mutex_unlock(&bus->mutex);
    return batch;
  }

  count = bus->count;
  events = (fpv_event_t**)calloc(count, sizeof(fpv_event_t*));
  if (!events) {
    fpv_mutex_unlock(&bus->mutex);
    return batch;
  }

  node = bus->head;
  bus->head = NULL;
  bus->tail = NULL;
  bus->count = 0;
  fpv_mutex_unlock(&bus->mutex);

  while (node && index < count) {
    fpv_event_node_t* next = node->next;
    events[index++] = node->event;
    free(node);
    node = next;
  }

  batch.events = events;
  batch.count = index;
  return batch;
}

void fpv_event_batch_destroy(fpv_event_batch_t* batch) {
  size_t index = 0;

  if (!batch || !batch->events) {
    return;
  }

  for (index = 0; index < batch->count; index++) {
    fpv_event_destroy(batch->events[index]);
  }

  free(batch->events);
  batch->events = NULL;
  batch->count = 0;
}

fpv_event_t* fpv_event_create_core_status(
    fpv_core_status_t status,
    const char* detail,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_core_status_event_t* payload =
      (fpv_core_status_event_t*)calloc(1, sizeof(*payload));

  if (!event || !payload) {
    free(payload);
    free(event);
    return NULL;
  }

  payload->status = status;
  payload->detail = fpv_strdup(detail);
  if (detail && !payload->detail) {
    free(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_CORE_STATUS;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_log(
    fpv_log_level_t level,
    const char* component,
    const char* message,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_log_entry_t* payload = (fpv_log_entry_t*)calloc(1, sizeof(*payload));

  if (!event || !payload) {
    free(payload);
    free(event);
    return NULL;
  }

  payload->level = level;
  payload->component = fpv_strdup(component);
  if (component && !payload->component) {
    free(payload);
    free(event);
    return NULL;
  }

  payload->message = fpv_strdup(message);
  if (message && !payload->message) {
    fpv_free(payload->component);
    free(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_LOG;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_user_profile(
    const fpv_user_profile_t* profile,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_user_profile_t* payload = fpv_user_profile_clone(profile);

  if (!event || !payload) {
    fpv_user_profile_destroy(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_USER_PROFILE;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_order(
    const fpv_order_t* order,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_order_t* payload = fpv_order_clone(order);

  if (!event || !payload) {
    fpv_order_destroy(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_ORDER;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_chat(
    const fpv_chat_t* chat,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_chat_t* payload = fpv_chat_clone(chat);

  if (!event || !payload) {
    fpv_chat_destroy(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_CHAT;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_lot(
    const fpv_lot_t* lot,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_lot_t* payload = fpv_lot_clone(lot);

  if (!event || !payload) {
    fpv_lot_destroy(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_LOT;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_message(
    const fpv_message_t* message,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_message_t* payload = fpv_message_clone(message);

  if (!event || !payload) {
    fpv_message_destroy(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_MESSAGE;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_notification(
    const fpv_notification_t* notification,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_notification_t* payload = fpv_notification_clone(notification);

  if (!event || !payload) {
    fpv_notification_destroy(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_NOTIFICATION;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

fpv_event_t* fpv_event_create_plugin(
    const fpv_plugin_t* plugin,
    uint64_t timestamp_ms) {
  fpv_event_t* event = (fpv_event_t*)calloc(1, sizeof(*event));
  fpv_plugin_t* payload = fpv_plugin_clone(plugin);

  if (!event || !payload) {
    fpv_plugin_destroy(payload);
    free(event);
    return NULL;
  }

  event->type = FPV_EVENT_PLUGIN;
  event->timestamp_ms = timestamp_ms;
  event->payload = payload;
  return event;
}

const fpv_core_status_event_t* fpv_event_as_core_status(
    const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_CORE_STATUS) {
    return NULL;
  }
  return (const fpv_core_status_event_t*)event->payload;
}

const fpv_log_entry_t* fpv_event_as_log(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_LOG) {
    return NULL;
  }
  return (const fpv_log_entry_t*)event->payload;
}

const fpv_user_profile_t* fpv_event_as_user_profile(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_USER_PROFILE) {
    return NULL;
  }
  return (const fpv_user_profile_t*)event->payload;
}

const fpv_order_t* fpv_event_as_order(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_ORDER) {
    return NULL;
  }
  return (const fpv_order_t*)event->payload;
}

const fpv_chat_t* fpv_event_as_chat(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_CHAT) {
    return NULL;
  }
  return (const fpv_chat_t*)event->payload;
}

const fpv_lot_t* fpv_event_as_lot(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_LOT) {
    return NULL;
  }
  return (const fpv_lot_t*)event->payload;
}

const fpv_message_t* fpv_event_as_message(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_MESSAGE) {
    return NULL;
  }
  return (const fpv_message_t*)event->payload;
}

const fpv_notification_t* fpv_event_as_notification(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_NOTIFICATION) {
    return NULL;
  }
  return (const fpv_notification_t*)event->payload;
}

const fpv_plugin_t* fpv_event_as_plugin(const fpv_event_t* event) {
  if (!event || event->type != FPV_EVENT_PLUGIN) {
    return NULL;
  }
  return (const fpv_plugin_t*)event->payload;
}

void fpv_event_destroy(fpv_event_t* event) {
  if (!event) {
    return;
  }

  switch (event->type) {
    case FPV_EVENT_CORE_STATUS: {
      fpv_core_status_event_t* payload =
          (fpv_core_status_event_t*)event->payload;
      if (payload) {
        fpv_free(payload->detail);
        free(payload);
      }
      break;
    }
    case FPV_EVENT_LOG: {
      fpv_log_entry_t* payload = (fpv_log_entry_t*)event->payload;
      if (payload) {
        fpv_free(payload->component);
        fpv_free(payload->message);
        free(payload);
      }
      break;
    }
    case FPV_EVENT_USER_PROFILE:
      fpv_user_profile_destroy((fpv_user_profile_t*)event->payload);
      break;
    case FPV_EVENT_ORDER:
      fpv_order_destroy((fpv_order_t*)event->payload);
      break;
    case FPV_EVENT_CHAT:
      fpv_chat_destroy((fpv_chat_t*)event->payload);
      break;
    case FPV_EVENT_LOT:
      fpv_lot_destroy((fpv_lot_t*)event->payload);
      break;
    case FPV_EVENT_MESSAGE:
      fpv_message_destroy((fpv_message_t*)event->payload);
      break;
    case FPV_EVENT_NOTIFICATION:
      fpv_notification_destroy((fpv_notification_t*)event->payload);
      break;
    case FPV_EVENT_PLUGIN:
      fpv_plugin_destroy((fpv_plugin_t*)event->payload);
      break;
    default:
      break;
  }

  free(event);
}
