/* FunPay Vertex core data models. */

#include "fpv_core/fpv_models.h"

#include <stdlib.h>

#include "core/base/fpv_string.h"


static void fpv_string_array_destroy(char** items, size_t count) {
  if (!items) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(items[i]);
  }
  fpv_free(items);
}

static char** fpv_string_array_clone(
    const char* const* items,
    size_t count) {
  if (!items || count == 0) {
    return NULL;
  }
  char** copy = (char**)calloc(count, sizeof(*copy));
  if (!copy) {
    return NULL;
  }
  for (size_t i = 0; i < count; i++) {
    if (!items[i]) {
      continue;
    }
    copy[i] = fpv_strdup(items[i]);
    if (!copy[i]) {
      fpv_string_array_destroy(copy, count);
      return NULL;
    }
  }
  return copy;
}

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

fpv_organization_t* fpv_organization_create(
    const char* id,
    const char* name,
    const char* timezone,
    const char* currency,
    fpv_product_tier_t tier,
    const fpv_data_retention_policy_t* retention,
    bool price_change_approval_required,
    uint64_t created_at_ms,
    uint64_t updated_at_ms) {
  fpv_organization_t* organization =
      (fpv_organization_t*)calloc(1, sizeof(*organization));
  if (!organization) {
    return NULL;
  }

  organization->id = fpv_strdup(id);
  if (id && !organization->id) {
    fpv_organization_destroy(organization);
    return NULL;
  }

  organization->name = fpv_strdup(name);
  if (name && !organization->name) {
    fpv_organization_destroy(organization);
    return NULL;
  }

  organization->timezone = fpv_strdup(timezone);
  if (timezone && !organization->timezone) {
    fpv_organization_destroy(organization);
    return NULL;
  }

  organization->currency = fpv_strdup(currency);
  if (currency && !organization->currency) {
    fpv_organization_destroy(organization);
    return NULL;
  }

  organization->tier = tier;
  if (retention) {
    organization->retention = *retention;
  }

  organization->price_change_approval_required = price_change_approval_required;
  organization->created_at_ms = created_at_ms;
  organization->updated_at_ms = updated_at_ms;
  return organization;
}

fpv_organization_t* fpv_organization_clone(
    const fpv_organization_t* organization) {
  if (!organization) {
    return NULL;
  }
  return fpv_organization_create(
      organization->id,
      organization->name,
      organization->timezone,
      organization->currency,
      organization->tier,
      &organization->retention,
      organization->price_change_approval_required,
      organization->created_at_ms,
      organization->updated_at_ms);
}

void fpv_organization_destroy(fpv_organization_t* organization) {
  if (!organization) {
    return;
  }
  fpv_free(organization->id);
  fpv_free(organization->name);
  fpv_free(organization->timezone);
  fpv_free(organization->currency);
  free(organization);
}

fpv_team_t* fpv_team_create(
    const char* id,
    const char* organization_id,
    const char* name,
    bool active,
    uint64_t created_at_ms,
    uint64_t updated_at_ms) {
  fpv_team_t* team = (fpv_team_t*)calloc(1, sizeof(*team));
  if (!team) {
    return NULL;
  }

  team->id = fpv_strdup(id);
  if (id && !team->id) {
    fpv_team_destroy(team);
    return NULL;
  }

  team->organization_id = fpv_strdup(organization_id);
  if (organization_id && !team->organization_id) {
    fpv_team_destroy(team);
    return NULL;
  }

  team->name = fpv_strdup(name);
  if (name && !team->name) {
    fpv_team_destroy(team);
    return NULL;
  }

  team->active = active;
  team->created_at_ms = created_at_ms;
  team->updated_at_ms = updated_at_ms;
  return team;
}

fpv_team_t* fpv_team_clone(const fpv_team_t* team) {
  if (!team) {
    return NULL;
  }
  return fpv_team_create(
      team->id,
      team->organization_id,
      team->name,
      team->active,
      team->created_at_ms,
      team->updated_at_ms);
}

void fpv_team_destroy(fpv_team_t* team) {
  if (!team) {
    return;
  }
  fpv_free(team->id);
  fpv_free(team->organization_id);
  fpv_free(team->name);
  free(team);
}

fpv_user_t* fpv_user_create(
    const char* id,
    const char* email,
    const char* display_name,
    bool email_verified,
    bool active,
    uint64_t created_at_ms,
    uint64_t last_login_ms) {
  fpv_user_t* user = (fpv_user_t*)calloc(1, sizeof(*user));
  if (!user) {
    return NULL;
  }

  user->id = fpv_strdup(id);
  if (id && !user->id) {
    fpv_user_destroy(user);
    return NULL;
  }

  user->email = fpv_strdup(email);
  if (email && !user->email) {
    fpv_user_destroy(user);
    return NULL;
  }

  user->display_name = fpv_strdup(display_name);
  if (display_name && !user->display_name) {
    fpv_user_destroy(user);
    return NULL;
  }

  user->email_verified = email_verified;
  user->active = active;
  user->created_at_ms = created_at_ms;
  user->last_login_ms = last_login_ms;
  return user;
}

fpv_user_t* fpv_user_clone(const fpv_user_t* user) {
  if (!user) {
    return NULL;
  }
  return fpv_user_create(
      user->id,
      user->email,
      user->display_name,
      user->email_verified,
      user->active,
      user->created_at_ms,
      user->last_login_ms);
}

void fpv_user_destroy(fpv_user_t* user) {
  if (!user) {
    return;
  }
  fpv_free(user->id);
  fpv_free(user->email);
  fpv_free(user->display_name);
  free(user);
}

fpv_user_role_t* fpv_user_role_create(
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t role,
    uint64_t assigned_at_ms) {
  fpv_user_role_t* assignment =
      (fpv_user_role_t*)calloc(1, sizeof(*assignment));
  if (!assignment) {
    return NULL;
  }

  assignment->user_id = fpv_strdup(user_id);
  if (user_id && !assignment->user_id) {
    fpv_user_role_destroy(assignment);
    return NULL;
  }

  assignment->organization_id = fpv_strdup(organization_id);
  if (organization_id && !assignment->organization_id) {
    fpv_user_role_destroy(assignment);
    return NULL;
  }

  assignment->team_id = fpv_strdup(team_id);
  if (team_id && !assignment->team_id) {
    fpv_user_role_destroy(assignment);
    return NULL;
  }

  assignment->role = role;
  assignment->assigned_at_ms = assigned_at_ms;
  return assignment;
}

fpv_user_role_t* fpv_user_role_clone(const fpv_user_role_t* role) {
  if (!role) {
    return NULL;
  }
  return fpv_user_role_create(
      role->user_id,
      role->organization_id,
      role->team_id,
      role->role,
      role->assigned_at_ms);
}

void fpv_user_role_destroy(fpv_user_role_t* role) {
  if (!role) {
    return;
  }
  fpv_free(role->user_id);
  fpv_free(role->organization_id);
  fpv_free(role->team_id);
  free(role);
}

fpv_account_t* fpv_account_create(
    const char* id,
    const char* organization_id,
    const char* team_id,
    const char* funpay_user_id,
    const char* funpay_username,
    const char* display_name,
    const char* currency,
    bool active,
    uint64_t linked_at_ms,
    uint64_t last_sync_at_ms) {
  fpv_account_t* account = (fpv_account_t*)calloc(1, sizeof(*account));
  if (!account) {
    return NULL;
  }

  account->id = fpv_strdup(id);
  if (id && !account->id) {
    fpv_account_destroy(account);
    return NULL;
  }

  account->organization_id = fpv_strdup(organization_id);
  if (organization_id && !account->organization_id) {
    fpv_account_destroy(account);
    return NULL;
  }

  account->team_id = fpv_strdup(team_id);
  if (team_id && !account->team_id) {
    fpv_account_destroy(account);
    return NULL;
  }

  account->funpay_user_id = fpv_strdup(funpay_user_id);
  if (funpay_user_id && !account->funpay_user_id) {
    fpv_account_destroy(account);
    return NULL;
  }

  account->funpay_username = fpv_strdup(funpay_username);
  if (funpay_username && !account->funpay_username) {
    fpv_account_destroy(account);
    return NULL;
  }

  account->display_name = fpv_strdup(display_name);
  if (display_name && !account->display_name) {
    fpv_account_destroy(account);
    return NULL;
  }

  account->currency = fpv_strdup(currency);
  if (currency && !account->currency) {
    fpv_account_destroy(account);
    return NULL;
  }

  account->active = active;
  account->linked_at_ms = linked_at_ms;
  account->last_sync_at_ms = last_sync_at_ms;
  return account;
}

fpv_account_t* fpv_account_clone(const fpv_account_t* account) {
  if (!account) {
    return NULL;
  }
  return fpv_account_create(
      account->id,
      account->organization_id,
      account->team_id,
      account->funpay_user_id,
      account->funpay_username,
      account->display_name,
      account->currency,
      account->active,
      account->linked_at_ms,
      account->last_sync_at_ms);
}

void fpv_account_destroy(fpv_account_t* account) {
  if (!account) {
    return;
  }
  fpv_free(account->id);
  fpv_free(account->organization_id);
  fpv_free(account->team_id);
  fpv_free(account->funpay_user_id);
  fpv_free(account->funpay_username);
  fpv_free(account->display_name);
  fpv_free(account->currency);
  free(account);
}

fpv_item_t* fpv_item_create(
    const char* id,
    const char* organization_id,
    const char* title,
    const char* normalized_title,
    const char* category,
    const char* subcategory,
    const char* description,
    const char* const* tags,
    size_t tag_count,
    uint64_t created_at_ms,
    uint64_t updated_at_ms) {
  if (tag_count > 0 && !tags) {
    return NULL;
  }

  fpv_item_t* item = (fpv_item_t*)calloc(1, sizeof(*item));
  if (!item) {
    return NULL;
  }

  item->id = fpv_strdup(id);
  if (id && !item->id) {
    fpv_item_destroy(item);
    return NULL;
  }

  item->organization_id = fpv_strdup(organization_id);
  if (organization_id && !item->organization_id) {
    fpv_item_destroy(item);
    return NULL;
  }

  item->title = fpv_strdup(title);
  if (title && !item->title) {
    fpv_item_destroy(item);
    return NULL;
  }

  item->normalized_title = fpv_strdup(normalized_title);
  if (normalized_title && !item->normalized_title) {
    fpv_item_destroy(item);
    return NULL;
  }

  item->category = fpv_strdup(category);
  if (category && !item->category) {
    fpv_item_destroy(item);
    return NULL;
  }

  item->subcategory = fpv_strdup(subcategory);
  if (subcategory && !item->subcategory) {
    fpv_item_destroy(item);
    return NULL;
  }

  item->description = fpv_strdup(description);
  if (description && !item->description) {
    fpv_item_destroy(item);
    return NULL;
  }

  if (tag_count > 0) {
    item->tags = fpv_string_array_clone(tags, tag_count);
    if (!item->tags) {
      fpv_item_destroy(item);
      return NULL;
    }
    item->tag_count = tag_count;
  }

  item->created_at_ms = created_at_ms;
  item->updated_at_ms = updated_at_ms;
  return item;
}

fpv_item_t* fpv_item_clone(const fpv_item_t* item) {
  if (!item) {
    return NULL;
  }
  return fpv_item_create(
      item->id,
      item->organization_id,
      item->title,
      item->normalized_title,
      item->category,
      item->subcategory,
      item->description,
      (const char* const*)item->tags,
      item->tag_count,
      item->created_at_ms,
      item->updated_at_ms);
}

void fpv_item_destroy(fpv_item_t* item) {
  if (!item) {
    return;
  }
  fpv_free(item->id);
  fpv_free(item->organization_id);
  fpv_free(item->title);
  fpv_free(item->normalized_title);
  fpv_free(item->category);
  fpv_free(item->subcategory);
  fpv_free(item->description);
  fpv_string_array_destroy(item->tags, item->tag_count);
  free(item);
}

fpv_listing_t* fpv_listing_create(
    const char* id,
    const char* item_id,
    const char* account_id,
    const char* title,
    const char* category,
    const char* subcategory,
    const char* status,
    double price,
    const char* currency,
    uint32_t quantity,
    const char* delivery_type,
    uint64_t last_updated_ms,
    const char* description,
    const char* const* tags,
    size_t tag_count) {
  if (tag_count > 0 && !tags) {
    return NULL;
  }

  fpv_listing_t* listing = (fpv_listing_t*)calloc(1, sizeof(*listing));
  if (!listing) {
    return NULL;
  }

  listing->id = fpv_strdup(id);
  if (id && !listing->id) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->item_id = fpv_strdup(item_id);
  if (item_id && !listing->item_id) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->account_id = fpv_strdup(account_id);
  if (account_id && !listing->account_id) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->title = fpv_strdup(title);
  if (title && !listing->title) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->category = fpv_strdup(category);
  if (category && !listing->category) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->subcategory = fpv_strdup(subcategory);
  if (subcategory && !listing->subcategory) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->status = fpv_strdup(status);
  if (status && !listing->status) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->currency = fpv_strdup(currency);
  if (currency && !listing->currency) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->delivery_type = fpv_strdup(delivery_type);
  if (delivery_type && !listing->delivery_type) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  listing->description = fpv_strdup(description);
  if (description && !listing->description) {
    fpv_listing_destroy(listing);
    return NULL;
  }

  if (tag_count > 0) {
    listing->tags = fpv_string_array_clone(tags, tag_count);
    if (!listing->tags) {
      fpv_listing_destroy(listing);
      return NULL;
    }
    listing->tag_count = tag_count;
  }

  listing->price = price;
  listing->quantity = quantity;
  listing->last_updated_ms = last_updated_ms;
  return listing;
}

fpv_listing_t* fpv_listing_clone(const fpv_listing_t* listing) {
  if (!listing) {
    return NULL;
  }
  return fpv_listing_create(
      listing->id,
      listing->item_id,
      listing->account_id,
      listing->title,
      listing->category,
      listing->subcategory,
      listing->status,
      listing->price,
      listing->currency,
      listing->quantity,
      listing->delivery_type,
      listing->last_updated_ms,
      listing->description,
      (const char* const*)listing->tags,
      listing->tag_count);
}

void fpv_listing_destroy(fpv_listing_t* listing) {
  if (!listing) {
    return;
  }
  fpv_free(listing->id);
  fpv_free(listing->item_id);
  fpv_free(listing->account_id);
  fpv_free(listing->title);
  fpv_free(listing->category);
  fpv_free(listing->subcategory);
  fpv_free(listing->status);
  fpv_free(listing->currency);
  fpv_free(listing->delivery_type);
  fpv_free(listing->description);
  fpv_string_array_destroy(listing->tags, listing->tag_count);
  free(listing);
}

fpv_competitor_listing_t* fpv_competitor_listing_create(
    const char* id,
    const char* item_id,
    const char* seller_id,
    double price,
    const char* currency,
    bool available,
    const char* delivery_type,
    double seller_rating,
    uint64_t last_seen_ms,
    const char* listing_url) {
  fpv_competitor_listing_t* listing =
      (fpv_competitor_listing_t*)calloc(1, sizeof(*listing));
  if (!listing) {
    return NULL;
  }

  listing->id = fpv_strdup(id);
  if (id && !listing->id) {
    fpv_competitor_listing_destroy(listing);
    return NULL;
  }

  listing->item_id = fpv_strdup(item_id);
  if (item_id && !listing->item_id) {
    fpv_competitor_listing_destroy(listing);
    return NULL;
  }

  listing->seller_id = fpv_strdup(seller_id);
  if (seller_id && !listing->seller_id) {
    fpv_competitor_listing_destroy(listing);
    return NULL;
  }

  listing->currency = fpv_strdup(currency);
  if (currency && !listing->currency) {
    fpv_competitor_listing_destroy(listing);
    return NULL;
  }

  listing->delivery_type = fpv_strdup(delivery_type);
  if (delivery_type && !listing->delivery_type) {
    fpv_competitor_listing_destroy(listing);
    return NULL;
  }

  listing->listing_url = fpv_strdup(listing_url);
  if (listing_url && !listing->listing_url) {
    fpv_competitor_listing_destroy(listing);
    return NULL;
  }

  listing->price = price;
  listing->available = available;
  listing->seller_rating = seller_rating;
  listing->last_seen_ms = last_seen_ms;
  return listing;
}

fpv_competitor_listing_t* fpv_competitor_listing_clone(
    const fpv_competitor_listing_t* listing) {
  if (!listing) {
    return NULL;
  }
  return fpv_competitor_listing_create(
      listing->id,
      listing->item_id,
      listing->seller_id,
      listing->price,
      listing->currency,
      listing->available,
      listing->delivery_type,
      listing->seller_rating,
      listing->last_seen_ms,
      listing->listing_url);
}

void fpv_competitor_listing_destroy(fpv_competitor_listing_t* listing) {
  if (!listing) {
    return;
  }
  fpv_free(listing->id);
  fpv_free(listing->item_id);
  fpv_free(listing->seller_id);
  fpv_free(listing->currency);
  fpv_free(listing->delivery_type);
  fpv_free(listing->listing_url);
  free(listing);
}

fpv_price_rule_t* fpv_price_rule_create(
    const char* id,
    fpv_price_rule_scope_t scope,
    const char* organization_id,
    const char* team_id,
    const char* item_id,
    const char* listing_id,
    double min_margin,
    double min_price,
    fpv_price_undercut_type_t undercut_type,
    double undercut_value,
    bool enabled,
    uint64_t created_at_ms,
    uint64_t updated_at_ms) {
  fpv_price_rule_t* rule = (fpv_price_rule_t*)calloc(1, sizeof(*rule));
  if (!rule) {
    return NULL;
  }

  rule->id = fpv_strdup(id);
  if (id && !rule->id) {
    fpv_price_rule_destroy(rule);
    return NULL;
  }

  rule->organization_id = fpv_strdup(organization_id);
  if (organization_id && !rule->organization_id) {
    fpv_price_rule_destroy(rule);
    return NULL;
  }

  rule->team_id = fpv_strdup(team_id);
  if (team_id && !rule->team_id) {
    fpv_price_rule_destroy(rule);
    return NULL;
  }

  rule->item_id = fpv_strdup(item_id);
  if (item_id && !rule->item_id) {
    fpv_price_rule_destroy(rule);
    return NULL;
  }

  rule->listing_id = fpv_strdup(listing_id);
  if (listing_id && !rule->listing_id) {
    fpv_price_rule_destroy(rule);
    return NULL;
  }

  rule->scope = scope;
  rule->min_margin = min_margin;
  rule->min_price = min_price;
  rule->undercut_type = undercut_type;
  rule->undercut_value = undercut_value;
  rule->enabled = enabled;
  rule->created_at_ms = created_at_ms;
  rule->updated_at_ms = updated_at_ms;
  return rule;
}

fpv_price_rule_t* fpv_price_rule_clone(const fpv_price_rule_t* rule) {
  if (!rule) {
    return NULL;
  }
  return fpv_price_rule_create(
      rule->id,
      rule->scope,
      rule->organization_id,
      rule->team_id,
      rule->item_id,
      rule->listing_id,
      rule->min_margin,
      rule->min_price,
      rule->undercut_type,
      rule->undercut_value,
      rule->enabled,
      rule->created_at_ms,
      rule->updated_at_ms);
}

void fpv_price_rule_destroy(fpv_price_rule_t* rule) {
  if (!rule) {
    return;
  }
  fpv_free(rule->id);
  fpv_free(rule->organization_id);
  fpv_free(rule->team_id);
  fpv_free(rule->item_id);
  fpv_free(rule->listing_id);
  free(rule);
}

fpv_price_history_t* fpv_price_history_create(
    const char* id,
    const char* listing_id,
    const char* price_rule_id,
    double competitor_median,
    double recommended_price,
    double applied_price,
    const char* currency,
    const char* reason,
    uint64_t created_at_ms) {
  fpv_price_history_t* history =
      (fpv_price_history_t*)calloc(1, sizeof(*history));
  if (!history) {
    return NULL;
  }

  history->id = fpv_strdup(id);
  if (id && !history->id) {
    fpv_price_history_destroy(history);
    return NULL;
  }

  history->listing_id = fpv_strdup(listing_id);
  if (listing_id && !history->listing_id) {
    fpv_price_history_destroy(history);
    return NULL;
  }

  history->price_rule_id = fpv_strdup(price_rule_id);
  if (price_rule_id && !history->price_rule_id) {
    fpv_price_history_destroy(history);
    return NULL;
  }

  history->currency = fpv_strdup(currency);
  if (currency && !history->currency) {
    fpv_price_history_destroy(history);
    return NULL;
  }

  history->reason = fpv_strdup(reason);
  if (reason && !history->reason) {
    fpv_price_history_destroy(history);
    return NULL;
  }

  history->competitor_median = competitor_median;
  history->recommended_price = recommended_price;
  history->applied_price = applied_price;
  history->created_at_ms = created_at_ms;
  return history;
}

fpv_price_history_t* fpv_price_history_clone(
    const fpv_price_history_t* history) {
  if (!history) {
    return NULL;
  }
  return fpv_price_history_create(
      history->id,
      history->listing_id,
      history->price_rule_id,
      history->competitor_median,
      history->recommended_price,
      history->applied_price,
      history->currency,
      history->reason,
      history->created_at_ms);
}

void fpv_price_history_destroy(fpv_price_history_t* history) {
  if (!history) {
    return;
  }
  fpv_free(history->id);
  fpv_free(history->listing_id);
  fpv_free(history->price_rule_id);
  fpv_free(history->currency);
  fpv_free(history->reason);
  free(history);
}

fpv_price_change_request_t* fpv_price_change_request_create(
    const char* id,
    const char* organization_id,
    const char* team_id,
    const char* listing_id,
    const char* price_rule_id,
    const char* requested_by_user_id,
    double current_price,
    double requested_price,
    const char* currency,
    const char* reason,
    fpv_price_change_status_t status,
    const char* reviewed_by_user_id,
    const char* review_note,
    uint64_t requested_at_ms,
    uint64_t reviewed_at_ms,
    uint64_t applied_at_ms) {
  fpv_price_change_request_t* request =
      (fpv_price_change_request_t*)calloc(1, sizeof(*request));
  if (!request) {
    return NULL;
  }

  request->id = fpv_strdup(id);
  if (id && !request->id) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->organization_id = fpv_strdup(organization_id);
  if (organization_id && !request->organization_id) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->team_id = fpv_strdup(team_id);
  if (team_id && !request->team_id) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->listing_id = fpv_strdup(listing_id);
  if (listing_id && !request->listing_id) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->price_rule_id = fpv_strdup(price_rule_id);
  if (price_rule_id && !request->price_rule_id) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->requested_by_user_id = fpv_strdup(requested_by_user_id);
  if (requested_by_user_id && !request->requested_by_user_id) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->currency = fpv_strdup(currency);
  if (currency && !request->currency) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->reason = fpv_strdup(reason);
  if (reason && !request->reason) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->reviewed_by_user_id = fpv_strdup(reviewed_by_user_id);
  if (reviewed_by_user_id && !request->reviewed_by_user_id) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->review_note = fpv_strdup(review_note);
  if (review_note && !request->review_note) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }

  request->current_price = current_price;
  request->requested_price = requested_price;
  request->status = status;
  request->requested_at_ms = requested_at_ms;
  request->reviewed_at_ms = reviewed_at_ms;
  request->applied_at_ms = applied_at_ms;
  return request;
}

fpv_price_change_request_t* fpv_price_change_request_clone(
    const fpv_price_change_request_t* request) {
  if (!request) {
    return NULL;
  }
  return fpv_price_change_request_create(
      request->id,
      request->organization_id,
      request->team_id,
      request->listing_id,
      request->price_rule_id,
      request->requested_by_user_id,
      request->current_price,
      request->requested_price,
      request->currency,
      request->reason,
      request->status,
      request->reviewed_by_user_id,
      request->review_note,
      request->requested_at_ms,
      request->reviewed_at_ms,
      request->applied_at_ms);
}

void fpv_price_change_request_destroy(fpv_price_change_request_t* request) {
  if (!request) {
    return;
  }
  fpv_free(request->id);
  fpv_free(request->organization_id);
  fpv_free(request->team_id);
  fpv_free(request->listing_id);
  fpv_free(request->price_rule_id);
  fpv_free(request->requested_by_user_id);
  fpv_free(request->currency);
  fpv_free(request->reason);
  fpv_free(request->reviewed_by_user_id);
  fpv_free(request->review_note);
  free(request);
}

fpv_audit_log_entry_t* fpv_audit_log_entry_create(
    const char* id,
    const char* organization_id,
    const char* team_id,
    const char* account_id,
    const char* actor_user_id,
    fpv_role_t actor_role,
    const char* action,
    const char* target_type,
    const char* target_id,
    const char* summary,
    const char* metadata,
    const char* ip_address,
    const char* user_agent,
    uint64_t created_at_ms) {
  fpv_audit_log_entry_t* entry =
      (fpv_audit_log_entry_t*)calloc(1, sizeof(*entry));
  if (!entry) {
    return NULL;
  }

  entry->id = fpv_strdup(id);
  if (id && !entry->id) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->organization_id = fpv_strdup(organization_id);
  if (organization_id && !entry->organization_id) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->team_id = fpv_strdup(team_id);
  if (team_id && !entry->team_id) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->account_id = fpv_strdup(account_id);
  if (account_id && !entry->account_id) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->actor_user_id = fpv_strdup(actor_user_id);
  if (actor_user_id && !entry->actor_user_id) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->action = fpv_strdup(action);
  if (action && !entry->action) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->target_type = fpv_strdup(target_type);
  if (target_type && !entry->target_type) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->target_id = fpv_strdup(target_id);
  if (target_id && !entry->target_id) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->summary = fpv_strdup(summary);
  if (summary && !entry->summary) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->metadata = fpv_strdup(metadata);
  if (metadata && !entry->metadata) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->ip_address = fpv_strdup(ip_address);
  if (ip_address && !entry->ip_address) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->user_agent = fpv_strdup(user_agent);
  if (user_agent && !entry->user_agent) {
    fpv_audit_log_entry_destroy(entry);
    return NULL;
  }

  entry->actor_role = actor_role;
  entry->created_at_ms = created_at_ms;
  return entry;
}

fpv_audit_log_entry_t* fpv_audit_log_entry_clone(
    const fpv_audit_log_entry_t* entry) {
  if (!entry) {
    return NULL;
  }
  return fpv_audit_log_entry_create(
      entry->id,
      entry->organization_id,
      entry->team_id,
      entry->account_id,
      entry->actor_user_id,
      entry->actor_role,
      entry->action,
      entry->target_type,
      entry->target_id,
      entry->summary,
      entry->metadata,
      entry->ip_address,
      entry->user_agent,
      entry->created_at_ms);
}

void fpv_audit_log_entry_destroy(fpv_audit_log_entry_t* entry) {
  if (!entry) {
    return;
  }
  fpv_free(entry->id);
  fpv_free(entry->organization_id);
  fpv_free(entry->team_id);
  fpv_free(entry->account_id);
  fpv_free(entry->actor_user_id);
  fpv_free(entry->action);
  fpv_free(entry->target_type);
  fpv_free(entry->target_id);
  fpv_free(entry->summary);
  fpv_free(entry->metadata);
  fpv_free(entry->ip_address);
  fpv_free(entry->user_agent);
  free(entry);
}

fpv_access_review_t* fpv_access_review_create(
    const char* id,
    const char* organization_id,
    const char* reviewer_user_id,
    const char* note,
    uint64_t reviewed_at_ms) {
  fpv_access_review_t* review =
      (fpv_access_review_t*)calloc(1, sizeof(*review));
  if (!review) {
    return NULL;
  }

  review->id = fpv_strdup(id);
  if (id && !review->id) {
    fpv_access_review_destroy(review);
    return NULL;
  }

  review->organization_id = fpv_strdup(organization_id);
  if (organization_id && !review->organization_id) {
    fpv_access_review_destroy(review);
    return NULL;
  }

  review->reviewer_user_id = fpv_strdup(reviewer_user_id);
  if (reviewer_user_id && !review->reviewer_user_id) {
    fpv_access_review_destroy(review);
    return NULL;
  }

  review->note = fpv_strdup(note);
  if (note && !review->note) {
    fpv_access_review_destroy(review);
    return NULL;
  }

  review->reviewed_at_ms = reviewed_at_ms;
  return review;
}

fpv_access_review_t* fpv_access_review_clone(
    const fpv_access_review_t* review) {
  if (!review) {
    return NULL;
  }
  return fpv_access_review_create(
      review->id,
      review->organization_id,
      review->reviewer_user_id,
      review->note,
      review->reviewed_at_ms);
}

void fpv_access_review_destroy(fpv_access_review_t* review) {
  if (!review) {
    return;
  }
  fpv_free(review->id);
  fpv_free(review->organization_id);
  fpv_free(review->reviewer_user_id);
  fpv_free(review->note);
  free(review);
}
