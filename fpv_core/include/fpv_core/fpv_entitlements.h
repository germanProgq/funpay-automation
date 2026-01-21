/* FunPay Vertex product tiers and feature entitlements. */

#ifndef FPV_ENTITLEMENTS_H
#define FPV_ENTITLEMENTS_H

#include <stdbool.h>
#include <stdint.h>

#include "fpv_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fpv_product_tier {
  FPV_TIER_BASIC = 1,
  FPV_TIER_ADVANCED = 2,
  FPV_TIER_ULTIMATE = 3
} fpv_product_tier_t;

typedef enum fpv_feature_flag {
  FPV_FEATURE_AUTO_DELIVERY = 0,
  FPV_FEATURE_MULTI_DELIVERY = 1,
  FPV_FEATURE_AUTO_RAISE = 2,
  FPV_FEATURE_AUTO_RESPONSE = 3,
  FPV_FEATURE_GOLDEN_KEY = 4,
  FPV_FEATURE_TELEGRAM_NOTIFICATIONS = 5,
  FPV_FEATURE_PROXY_IPV4 = 6,
  FPV_FEATURE_BUYER_BLACKLIST = 7,
  FPV_FEATURE_WATERMARK = 8,
  FPV_FEATURE_SRAS_MONITOR = 9,
  FPV_FEATURE_AUTO_REFUND = 10,
  FPV_FEATURE_FEEDBACK_REMINDER = 11,
  FPV_FEATURE_LOT_CLONER = 12,
  FPV_FEATURE_OLD_ORDERS_SCANNER = 13,
  FPV_FEATURE_BUYER_NOTES = 14,
  FPV_FEATURE_SALES_STATS = 15,
  FPV_FEATURE_NET_PROFIT_FORECAST = 16,
  FPV_FEATURE_TOP_CUSTOMER_ANALYTICS = 17,
  FPV_FEATURE_REVIEW_ANALYTICS = 18,
  FPV_FEATURE_MASS_PRICE_EDITOR = 19,
  FPV_FEATURE_COPY_LOTS_MANAGER = 20,
  FPV_FEATURE_DELETE_LOTS = 21,
  FPV_FEATURE_WORD_REPLACER = 22,
  FPV_FEATURE_STALE_LOT_DETECTOR = 23,
  FPV_FEATURE_STATUS_MANAGER = 24,
  FPV_FEATURE_AUTO_DUMPING = 25,
  FPV_FEATURE_AUTOROBUX_GAMEPASS = 26,
  FPV_FEATURE_STEAM_AUTOPOINTS = 27,
  FPV_FEATURE_AUTO_SEND_BROADCASTS = 28,
  FPV_FEATURE_AUTO_SMM = 29,
  FPV_FEATURE_AI_REVIEWS = 30,
  FPV_FEATURE_MANAGER_SYSTEM = 31,
  FPV_FEATURE_PRIORITY_SUPPORT = 32
} fpv_feature_flag_t;

typedef uint64_t fpv_feature_mask_t;

FPV_CORE_API fpv_feature_mask_t fpv_feature_mask_for_tier(
    fpv_product_tier_t tier);
FPV_CORE_API bool fpv_feature_mask_has(
    fpv_feature_mask_t mask,
    fpv_feature_flag_t feature);
FPV_CORE_API fpv_product_tier_t fpv_tier_from_string(const char* value);
FPV_CORE_API const char* fpv_tier_to_string(fpv_product_tier_t tier);

#ifdef __cplusplus
}
#endif

#endif
