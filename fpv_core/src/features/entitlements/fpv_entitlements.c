/* FunPay Vertex product tiers and feature entitlements implementation. */

#include "fpv_core/fpv_entitlements.h"

#include <string.h>

static int fpv_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

static int fpv_ascii_strcasecmp(const char* left, const char* right) {
  size_t index = 0;
  if (!left || !right) {
    return left == right ? 0 : (left ? 1 : -1);
  }
  while (left[index] != '\0' || right[index] != '\0') {
    int a = fpv_ascii_tolower((unsigned char)left[index]);
    int b = fpv_ascii_tolower((unsigned char)right[index]);
    if (a != b) {
      return a - b;
    }
    if (left[index] == '\0' || right[index] == '\0') {
      break;
    }
    index++;
  }
  return 0;
}

static fpv_feature_mask_t fpv_feature_bit(fpv_feature_flag_t feature) {
  return (fpv_feature_mask_t)1ULL << (uint64_t)feature;
}

static fpv_feature_mask_t fpv_basic_mask(void) {
  return fpv_feature_bit(FPV_FEATURE_AUTO_DELIVERY) |
      fpv_feature_bit(FPV_FEATURE_MULTI_DELIVERY) |
      fpv_feature_bit(FPV_FEATURE_AUTO_RAISE) |
      fpv_feature_bit(FPV_FEATURE_AUTO_RESPONSE) |
      fpv_feature_bit(FPV_FEATURE_GOLDEN_KEY) |
      fpv_feature_bit(FPV_FEATURE_TELEGRAM_NOTIFICATIONS) |
      fpv_feature_bit(FPV_FEATURE_PROXY_IPV4) |
      fpv_feature_bit(FPV_FEATURE_BUYER_BLACKLIST) |
      fpv_feature_bit(FPV_FEATURE_WATERMARK) |
      fpv_feature_bit(FPV_FEATURE_SRAS_MONITOR) |
      fpv_feature_bit(FPV_FEATURE_AUTO_REFUND) |
      fpv_feature_bit(FPV_FEATURE_FEEDBACK_REMINDER) |
      fpv_feature_bit(FPV_FEATURE_LOT_CLONER) |
      fpv_feature_bit(FPV_FEATURE_OLD_ORDERS_SCANNER) |
      fpv_feature_bit(FPV_FEATURE_BUYER_NOTES);
}

static fpv_feature_mask_t fpv_advanced_mask(void) {
  return fpv_basic_mask() |
      fpv_feature_bit(FPV_FEATURE_SALES_STATS) |
      fpv_feature_bit(FPV_FEATURE_NET_PROFIT_FORECAST) |
      fpv_feature_bit(FPV_FEATURE_TOP_CUSTOMER_ANALYTICS) |
      fpv_feature_bit(FPV_FEATURE_REVIEW_ANALYTICS) |
      fpv_feature_bit(FPV_FEATURE_MASS_PRICE_EDITOR) |
      fpv_feature_bit(FPV_FEATURE_COPY_LOTS_MANAGER) |
      fpv_feature_bit(FPV_FEATURE_DELETE_LOTS) |
      fpv_feature_bit(FPV_FEATURE_WORD_REPLACER) |
      fpv_feature_bit(FPV_FEATURE_STALE_LOT_DETECTOR) |
      fpv_feature_bit(FPV_FEATURE_STATUS_MANAGER);
}

static fpv_feature_mask_t fpv_ultimate_mask(void) {
  return fpv_advanced_mask() |
      fpv_feature_bit(FPV_FEATURE_AUTO_DUMPING) |
      fpv_feature_bit(FPV_FEATURE_AUTOROBUX_GAMEPASS) |
      fpv_feature_bit(FPV_FEATURE_STEAM_AUTOPOINTS) |
      fpv_feature_bit(FPV_FEATURE_AUTO_SEND_BROADCASTS) |
      fpv_feature_bit(FPV_FEATURE_AUTO_SMM) |
      fpv_feature_bit(FPV_FEATURE_AI_REVIEWS) |
      fpv_feature_bit(FPV_FEATURE_MANAGER_SYSTEM) |
      fpv_feature_bit(FPV_FEATURE_PRIORITY_SUPPORT);
}

fpv_feature_mask_t fpv_feature_mask_for_tier(fpv_product_tier_t tier) {
  switch (tier) {
    case FPV_TIER_ADVANCED:
      return fpv_advanced_mask();
    case FPV_TIER_ULTIMATE:
      return fpv_ultimate_mask();
    case FPV_TIER_BASIC:
    default:
      return fpv_basic_mask();
  }
}

bool fpv_feature_mask_has(
    fpv_feature_mask_t mask,
    fpv_feature_flag_t feature) {
  return (mask & fpv_feature_bit(feature)) != 0;
}

fpv_product_tier_t fpv_tier_from_string(const char* value) {
  if (!value || !value[0]) {
    return FPV_TIER_BASIC;
  }
  if (fpv_ascii_strcasecmp(value, "basic") == 0 ||
      fpv_ascii_strcasecmp(value, "starter") == 0) {
    return FPV_TIER_BASIC;
  }
  if (fpv_ascii_strcasecmp(value, "advanced") == 0) {
    return FPV_TIER_ADVANCED;
  }
  if (fpv_ascii_strcasecmp(value, "ultimate") == 0 ||
      fpv_ascii_strcasecmp(value, "team") == 0) {
    return FPV_TIER_ULTIMATE;
  }
  return FPV_TIER_BASIC;
}

const char* fpv_tier_to_string(fpv_product_tier_t tier) {
  switch (tier) {
    case FPV_TIER_ADVANCED:
      return "advanced";
    case FPV_TIER_ULTIMATE:
      return "ultimate";
    case FPV_TIER_BASIC:
    default:
      return "basic";
  }
}
