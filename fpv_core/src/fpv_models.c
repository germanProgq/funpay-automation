/* FunPay Vertex core data models. */

#include "fpv_core/fpv_models.h"

#include <stdlib.h>

#include "fpv_string.h"

fpv_user_profile_t* fpv_user_profile_create(
    const char* id,
    const char* username,
    const char* display_name,
    double balance,
    const char* currency,
    double rating,
    uint64_t registered_at_ms) {
  fpv_user_profile_t* profile =
      (fpv_user_profile_t*)calloc(1, sizeof(fpv_user_profile_t));
  if (!profile) {
    return NULL;
  }

  profile->id = fpv_strdup(id);
  if (id && !profile->id) {
    fpv_user_profile_destroy(profile);
    return NULL;
  }

  profile->username = fpv_strdup(username);
  if (username && !profile->username) {
    fpv_user_profile_destroy(profile);
    return NULL;
  }

  profile->display_name = fpv_strdup(display_name);
  if (display_name && !profile->display_name) {
    fpv_user_profile_destroy(profile);
    return NULL;
  }

  profile->currency = fpv_strdup(currency);
  if (currency && !profile->currency) {
    fpv_user_profile_destroy(profile);
    return NULL;
  }

  profile->balance = balance;
  profile->rating = rating;
  profile->registered_at_ms = registered_at_ms;
  return profile;
}

fpv_user_profile_t* fpv_user_profile_clone(const fpv_user_profile_t* profile) {
  if (!profile) {
    return NULL;
  }
  return fpv_user_profile_create(
      profile->id,
      profile->username,
      profile->display_name,
      profile->balance,
      profile->currency,
      profile->rating,
      profile->registered_at_ms);
}

void fpv_user_profile_destroy(fpv_user_profile_t* profile) {
  if (!profile) {
    return;
  }
  fpv_free(profile->id);
  fpv_free(profile->username);
  fpv_free(profile->display_name);
  fpv_free(profile->currency);
  free(profile);
}

fpv_order_t* fpv_order_create(
    const char* id,
    const char* lot_id,
    const char* chat_id,
    const char* buyer_id,
    const char* buyer_username,
    fpv_order_status_t status,
    double amount,
    const char* currency,
    uint64_t created_at_ms,
    uint64_t updated_at_ms,
    uint32_t quantity,
    const char* title,
    const char* subcategory) {
  fpv_order_t* order = (fpv_order_t*)calloc(1, sizeof(fpv_order_t));
  if (!order) {
    return NULL;
  }

  order->id = fpv_strdup(id);
  if (id && !order->id) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->lot_id = fpv_strdup(lot_id);
  if (lot_id && !order->lot_id) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->chat_id = fpv_strdup(chat_id);
  if (chat_id && !order->chat_id) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->buyer_id = fpv_strdup(buyer_id);
  if (buyer_id && !order->buyer_id) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->buyer_username = fpv_strdup(buyer_username);
  if (buyer_username && !order->buyer_username) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->currency = fpv_strdup(currency);
  if (currency && !order->currency) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->title = fpv_strdup(title);
  if (title && !order->title) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->subcategory = fpv_strdup(subcategory);
  if (subcategory && !order->subcategory) {
    fpv_order_destroy(order);
    return NULL;
  }

  order->status = status;
  order->amount = amount;
  order->created_at_ms = created_at_ms;
  order->updated_at_ms = updated_at_ms;
  order->quantity = quantity;
  return order;
}

fpv_order_t* fpv_order_clone(const fpv_order_t* order) {
  if (!order) {
    return NULL;
  }
  return fpv_order_create(
      order->id,
      order->lot_id,
      order->chat_id,
      order->buyer_id,
      order->buyer_username,
      order->status,
      order->amount,
      order->currency,
      order->created_at_ms,
      order->updated_at_ms,
      order->quantity,
      order->title,
      order->subcategory);
}

void fpv_order_destroy(fpv_order_t* order) {
  if (!order) {
    return;
  }
  fpv_free(order->id);
  fpv_free(order->lot_id);
  fpv_free(order->chat_id);
  fpv_free(order->buyer_id);
  fpv_free(order->buyer_username);
  fpv_free(order->currency);
  fpv_free(order->title);
  fpv_free(order->subcategory);
  free(order);
}

fpv_chat_t* fpv_chat_create(
    const char* id,
    const char* title,
    const char* last_message_text,
    const char* last_message_time,
    uint32_t unread_count,
    const char* last_message_id) {
  fpv_chat_t* chat = (fpv_chat_t*)calloc(1, sizeof(fpv_chat_t));
  if (!chat) {
    return NULL;
  }

  chat->id = fpv_strdup(id);
  if (id && !chat->id) {
    fpv_chat_destroy(chat);
    return NULL;
  }

  chat->title = fpv_strdup(title);
  if (title && !chat->title) {
    fpv_chat_destroy(chat);
    return NULL;
  }

  chat->last_message_text = fpv_strdup(last_message_text);
  if (last_message_text && !chat->last_message_text) {
    fpv_chat_destroy(chat);
    return NULL;
  }

  chat->last_message_time = fpv_strdup(last_message_time);
  if (last_message_time && !chat->last_message_time) {
    fpv_chat_destroy(chat);
    return NULL;
  }

  chat->last_message_id = fpv_strdup(last_message_id);
  if (last_message_id && !chat->last_message_id) {
    fpv_chat_destroy(chat);
    return NULL;
  }

  chat->unread_count = unread_count;
  return chat;
}

fpv_chat_t* fpv_chat_clone(const fpv_chat_t* chat) {
  if (!chat) {
    return NULL;
  }
  return fpv_chat_create(
      chat->id,
      chat->title,
      chat->last_message_text,
      chat->last_message_time,
      chat->unread_count,
      chat->last_message_id);
}

void fpv_chat_destroy(fpv_chat_t* chat) {
  if (!chat) {
    return;
  }
  fpv_free(chat->id);
  fpv_free(chat->title);
  fpv_free(chat->last_message_text);
  fpv_free(chat->last_message_time);
  fpv_free(chat->last_message_id);
  free(chat);
}

fpv_lot_t* fpv_lot_create(
    const char* id,
    const char* title,
    double price,
    const char* currency,
    uint32_t stock,
    bool active) {
  fpv_lot_t* lot = (fpv_lot_t*)calloc(1, sizeof(fpv_lot_t));
  if (!lot) {
    return NULL;
  }

  lot->id = fpv_strdup(id);
  if (id && !lot->id) {
    fpv_lot_destroy(lot);
    return NULL;
  }

  lot->title = fpv_strdup(title);
  if (title && !lot->title) {
    fpv_lot_destroy(lot);
    return NULL;
  }

  lot->currency = fpv_strdup(currency);
  if (currency && !lot->currency) {
    fpv_lot_destroy(lot);
    return NULL;
  }

  lot->price = price;
  lot->stock = stock;
  lot->active = active;
  return lot;
}

fpv_lot_t* fpv_lot_clone(const fpv_lot_t* lot) {
  if (!lot) {
    return NULL;
  }
  return fpv_lot_create(
      lot->id,
      lot->title,
      lot->price,
      lot->currency,
      lot->stock,
      lot->active);
}

void fpv_lot_destroy(fpv_lot_t* lot) {
  if (!lot) {
    return;
  }
  fpv_free(lot->id);
  fpv_free(lot->title);
  fpv_free(lot->currency);
  free(lot);
}

fpv_message_t* fpv_message_create(
    const char* id,
    const char* chat_id,
    const char* chat_name,
    const char* sender_id,
    const char* sender_name,
    const char* text,
    const char* image_url,
    const char* badge,
    fpv_message_direction_t direction,
    bool by_bot,
    uint64_t created_at_ms) {
  fpv_message_t* message = (fpv_message_t*)calloc(1, sizeof(fpv_message_t));
  if (!message) {
    return NULL;
  }

  message->id = fpv_strdup(id);
  if (id && !message->id) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->chat_id = fpv_strdup(chat_id);
  if (chat_id && !message->chat_id) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->chat_name = fpv_strdup(chat_name);
  if (chat_name && !message->chat_name) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->sender_id = fpv_strdup(sender_id);
  if (sender_id && !message->sender_id) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->sender_name = fpv_strdup(sender_name);
  if (sender_name && !message->sender_name) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->text = fpv_strdup(text);
  if (text && !message->text) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->image_url = fpv_strdup(image_url);
  if (image_url && !message->image_url) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->badge = fpv_strdup(badge);
  if (badge && !message->badge) {
    fpv_message_destroy(message);
    return NULL;
  }

  message->direction = direction;
  message->by_bot = by_bot;
  message->created_at_ms = created_at_ms;
  return message;
}

fpv_message_t* fpv_message_clone(const fpv_message_t* message) {
  if (!message) {
    return NULL;
  }
  return fpv_message_create(
      message->id,
      message->chat_id,
      message->chat_name,
      message->sender_id,
      message->sender_name,
      message->text,
      message->image_url,
      message->badge,
      message->direction,
      message->by_bot,
      message->created_at_ms);
}

void fpv_message_destroy(fpv_message_t* message) {
  if (!message) {
    return;
  }
  fpv_free(message->id);
  fpv_free(message->chat_id);
  fpv_free(message->chat_name);
  fpv_free(message->sender_id);
  fpv_free(message->sender_name);
  fpv_free(message->text);
  fpv_free(message->image_url);
  fpv_free(message->badge);
  free(message);
}

fpv_notification_t* fpv_notification_create(
    const char* id,
    const char* type,
    const char* message,
    fpv_notification_severity_t severity,
    uint64_t created_at_ms) {
  fpv_notification_t* notification =
      (fpv_notification_t*)calloc(1, sizeof(fpv_notification_t));
  if (!notification) {
    return NULL;
  }

  notification->id = fpv_strdup(id);
  if (id && !notification->id) {
    fpv_notification_destroy(notification);
    return NULL;
  }

  notification->type = fpv_strdup(type);
  if (type && !notification->type) {
    fpv_notification_destroy(notification);
    return NULL;
  }

  notification->message = fpv_strdup(message);
  if (message && !notification->message) {
    fpv_notification_destroy(notification);
    return NULL;
  }

  notification->severity = severity;
  notification->created_at_ms = created_at_ms;
  return notification;
}

fpv_notification_t* fpv_notification_clone(
    const fpv_notification_t* notification) {
  if (!notification) {
    return NULL;
  }
  return fpv_notification_create(
      notification->id,
      notification->type,
      notification->message,
      notification->severity,
      notification->created_at_ms);
}

void fpv_notification_destroy(fpv_notification_t* notification) {
  if (!notification) {
    return;
  }
  fpv_free(notification->id);
  fpv_free(notification->type);
  fpv_free(notification->message);
  free(notification);
}

fpv_plugin_t* fpv_plugin_create(
    const char* id,
    const char* name,
    const char* version,
    bool enabled) {
  fpv_plugin_t* plugin = (fpv_plugin_t*)calloc(1, sizeof(fpv_plugin_t));
  if (!plugin) {
    return NULL;
  }

  plugin->id = fpv_strdup(id);
  if (id && !plugin->id) {
    fpv_plugin_destroy(plugin);
    return NULL;
  }

  plugin->name = fpv_strdup(name);
  if (name && !plugin->name) {
    fpv_plugin_destroy(plugin);
    return NULL;
  }

  plugin->version = fpv_strdup(version);
  if (version && !plugin->version) {
    fpv_plugin_destroy(plugin);
    return NULL;
  }

  plugin->enabled = enabled;
  return plugin;
}

fpv_plugin_t* fpv_plugin_clone(const fpv_plugin_t* plugin) {
  if (!plugin) {
    return NULL;
  }
  return fpv_plugin_create(
      plugin->id, plugin->name, plugin->version, plugin->enabled);
}

void fpv_plugin_destroy(fpv_plugin_t* plugin) {
  if (!plugin) {
    return;
  }
  fpv_free(plugin->id);
  fpv_free(plugin->name);
  fpv_free(plugin->version);
  free(plugin);
}
