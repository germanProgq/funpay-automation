/* FunPay Vertex Telegram profile and system info helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#endif
#endif

char* fpv_tg_build_profile_text(
    fpv_telegram_service_t* service,
    const fpv_funpay_balance_t* balance,
    uint32_t active_sales) {
  const char* username = service && service->account
      ? fpv_funpay_account_username(service->account)
      : "";
  uint64_t id = service && service->account
      ? fpv_funpay_account_id(service->account)
      : 0;
  uint32_t sales = active_sales;
  if (service && service->account) {
    sales = fpv_funpay_account_active_sales(service->account);
  }
  char time_buf[32];
  time_buf[0] = '\0';
  if (service && service->account) {
    uint64_t updated_ms = fpv_funpay_account_last_update_ms(service->account);
    if (updated_ms > 0) {
      fpv_tg_format_time_hms(updated_ms, time_buf, sizeof(time_buf));
    }
  }
  char buffer[2048];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xD0\xA1\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD0\xBA\xD0\xB0 \xD0\xB0\xD0\xBA\xD0\xBA\xD0\xB0\xD1\x83\xD0\xBD\xD1\x82\xD0\xB0 <b><i>%s</i></b>\n\n"
      "<b>ID:</b> <code>%" PRIu64 "</code>\n"
      "<b>\xD0\x9D\xD0\xB5\xD0\xB7\xD0\xB0\xD0\xB2\xD0\xB5\xD1\x80\xD1\x88\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD1\x85 \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xBE\xD0\xB2:</b> <code>%u</code>\n"
      "<b>\xD0\x91\xD0\xB0\xD0\xBB\xD0\xB0\xD0\xBD\xD1\x81:</b> \n"
      "    <b>\xE2\x82\xBD:</b> <code>%.2f\xE2\x82\xBD</code>, \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD0\xBE \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x8B\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0 <code>%.2f\xE2\x82\xBD</code>.\n"
      "    <b>$:</b> <code>%.2f$</code>, \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD0\xBE \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x8B\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0 <code>%.2f$</code>.\n"
      "    <b>\xE2\x82\xAC:</b> <code>%.2f\xE2\x82\xAC</code>, \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD0\xBE \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x8B\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0 <code>%.2f\xE2\x82\xAC</code>.\n\n"
      "<i>\xD0\x9E\xD0\xB1\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBE:</i>  <code>%s</code>",
      username ? username : "",
      id,
      sales,
      balance ? balance->total_rub : 0.0,
      balance ? balance->available_rub : 0.0,
      balance ? balance->total_usd : 0.0,
      balance ? balance->available_usd : 0.0,
      balance ? balance->total_eur : 0.0,
      balance ? balance->available_eur : 0.0,
      time_buf);
  return fpv_strdup(buffer);
}

static uint64_t fpv_tg_cpu_count(void) {
#if defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors > 0 ? info.dwNumberOfProcessors : 1;
#else
  long count = sysconf(_SC_NPROCESSORS_ONLN);
  if (count < 1) {
    return 1;
  }
  return (uint64_t)count;
#endif
}

#if defined(_WIN32)
static uint64_t fpv_tg_filetime_to_uint64(FILETIME value) {
  ULARGE_INTEGER combined;
  combined.LowPart = value.dwLowDateTime;
  combined.HighPart = value.dwHighDateTime;
  return combined.QuadPart;
}

static bool fpv_tg_read_system_cpu(uint64_t* total, uint64_t* idle) {
  FILETIME idle_time;
  FILETIME kernel_time;
  FILETIME user_time;
  if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
    return false;
  }
  uint64_t idle_ticks = fpv_tg_filetime_to_uint64(idle_time);
  uint64_t kernel_ticks = fpv_tg_filetime_to_uint64(kernel_time);
  uint64_t user_ticks = fpv_tg_filetime_to_uint64(user_time);
  if (total) {
    *total = kernel_ticks + user_ticks;
  }
  if (idle) {
    *idle = idle_ticks;
  }
  return true;
}

static bool fpv_tg_read_process_cpu(double* out_seconds) {
  FILETIME creation;
  FILETIME exit_time;
  FILETIME kernel_time;
  FILETIME user_time;
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time,
                       &kernel_time, &user_time)) {
    return false;
  }
  uint64_t kernel_ticks = fpv_tg_filetime_to_uint64(kernel_time);
  uint64_t user_ticks = fpv_tg_filetime_to_uint64(user_time);
  if (out_seconds) {
    *out_seconds = (double)(kernel_ticks + user_ticks) / 10000000.0;
  }
  return true;
}
#elif defined(__APPLE__)
static bool fpv_tg_read_system_cpu(uint64_t* total, uint64_t* idle) {
  host_cpu_load_info_data_t info;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                      (host_info_t)&info, &count) != KERN_SUCCESS) {
    return false;
  }
  uint64_t user = info.cpu_ticks[CPU_STATE_USER];
  uint64_t system = info.cpu_ticks[CPU_STATE_SYSTEM];
  uint64_t nice = info.cpu_ticks[CPU_STATE_NICE];
  uint64_t idle_ticks = info.cpu_ticks[CPU_STATE_IDLE];
  if (total) {
    *total = user + system + nice + idle_ticks;
  }
  if (idle) {
    *idle = idle_ticks;
  }
  return true;
}

static bool fpv_tg_read_process_cpu(double* out_seconds) {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return false;
  }
  double user = (double)usage.ru_utime.tv_sec +
      (double)usage.ru_utime.tv_usec / 1000000.0;
  double sys = (double)usage.ru_stime.tv_sec +
      (double)usage.ru_stime.tv_usec / 1000000.0;
  if (out_seconds) {
    *out_seconds = user + sys;
  }
  return true;
}
#else
static bool fpv_tg_read_system_cpu(uint64_t* total, uint64_t* idle) {
  FILE* file = fopen("/proc/stat", "r");
  if (!file) {
    return false;
  }
  char line[256];
  if (!fgets(line, sizeof(line), file)) {
    fclose(file);
    return false;
  }
  fclose(file);
  uint64_t user = 0;
  uint64_t nice = 0;
  uint64_t system = 0;
  uint64_t idle_ticks = 0;
  uint64_t iowait = 0;
  uint64_t irq = 0;
  uint64_t softirq = 0;
  uint64_t steal = 0;
  if (sscanf(line, "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
             " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
             &user, &nice, &system, &idle_ticks, &iowait, &irq, &softirq,
             &steal) < 4) {
    return false;
  }
  if (total) {
    *total = user + nice + system + idle_ticks + iowait + irq + softirq + steal;
  }
  if (idle) {
    *idle = idle_ticks + iowait;
  }
  return true;
}

static bool fpv_tg_read_process_cpu(double* out_seconds) {
  FILE* file = fopen("/proc/self/stat", "r");
  if (!file) {
    return false;
  }
  char buffer[4096];
  if (!fgets(buffer, sizeof(buffer), file)) {
    fclose(file);
    return false;
  }
  fclose(file);
  char* end = strrchr(buffer, ')');
  if (!end) {
    return false;
  }
  char* ptr = end + 2;
  unsigned long long utime = 0;
  unsigned long long stime = 0;
  for (int field = 3; field <= 15 && *ptr; field++) {
    while (*ptr == ' ') {
      ptr++;
    }
    char* next = NULL;
    unsigned long long value = strtoull(ptr, &next, 10);
    if (!next || next == ptr) {
      return false;
    }
    if (field == 14) {
      utime = value;
    } else if (field == 15) {
      stime = value;
      break;
    }
    ptr = next;
  }
  long ticks = sysconf(_SC_CLK_TCK);
  if (ticks <= 0) {
    return false;
  }
  if (out_seconds) {
    *out_seconds = (double)(utime + stime) / (double)ticks;
  }
  return true;
}
#endif

static bool fpv_tg_get_cpu_usage(double* out_total, double* out_process) {
  uint64_t total1 = 0;
  uint64_t idle1 = 0;
  double proc1 = 0.0;
  if (!fpv_tg_read_system_cpu(&total1, &idle1) ||
      !fpv_tg_read_process_cpu(&proc1)) {
    return false;
  }
  uint64_t start_ms = fpv_time_now_ms();
  fpv_tg_sleep_ms(150);
  uint64_t total2 = 0;
  uint64_t idle2 = 0;
  double proc2 = 0.0;
  if (!fpv_tg_read_system_cpu(&total2, &idle2) ||
      !fpv_tg_read_process_cpu(&proc2)) {
    return false;
  }
  uint64_t end_ms = fpv_time_now_ms();
  uint64_t total_delta = total2 > total1 ? total2 - total1 : 0;
  uint64_t idle_delta = idle2 > idle1 ? idle2 - idle1 : 0;
  if (total_delta == 0) {
    return false;
  }
  double total_usage =
      (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
  double interval_sec = (double)(end_ms - start_ms) / 1000.0;
  if (interval_sec <= 0.0) {
    interval_sec = 0.001;
  }
  uint64_t cpu_count = fpv_tg_cpu_count();
  double proc_delta = proc2 - proc1;
  double process_usage =
      (proc_delta / interval_sec) * 100.0 / (double)cpu_count;
  if (out_total) {
    *out_total = total_usage;
  }
  if (out_process) {
    *out_process = process_usage;
  }
  return true;
}

static bool fpv_tg_get_memory_stats(
    uint64_t* total_mb,
    uint64_t* used_mb,
    uint64_t* free_mb,
    uint64_t* process_mb) {
  uint64_t total = 0;
  uint64_t free_mem = 0;
  uint64_t used = 0;
  uint64_t proc = 0;
#if defined(_WIN32)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    total = (uint64_t)(status.ullTotalPhys / 1048576ULL);
    free_mem = (uint64_t)(status.ullAvailPhys / 1048576ULL);
    used = total > free_mem ? total - free_mem : 0;
  }
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    proc = (uint64_t)(pmc.WorkingSetSize / 1048576ULL);
  }
#elif defined(__APPLE__)
  vm_statistics64_data_t vm_stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        (host_info_t)&vm_stats, &count) == KERN_SUCCESS) {
    uint64_t page_size = 0;
    host_page_size(mach_host_self(), (vm_size_t*)&page_size);
    total = (uint64_t)((vm_stats.active_count + vm_stats.inactive_count +
                        vm_stats.wire_count + vm_stats.free_count) *
                       page_size / 1048576ULL);
    free_mem = (uint64_t)(vm_stats.free_count * page_size / 1048576ULL);
    used = total > free_mem ? total - free_mem : 0;
  }
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    proc = (uint64_t)(usage.ru_maxrss / 1048576ULL);
  }
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long free_pages = sysconf(_SC_AVPHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && free_pages >= 0 && page_size > 0) {
    total = (uint64_t)pages * (uint64_t)page_size / 1048576ULL;
    free_mem = (uint64_t)free_pages * (uint64_t)page_size / 1048576ULL;
    used = total > free_mem ? total - free_mem : 0;
  }
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    proc = (uint64_t)(usage.ru_maxrss / 1024ULL);
  }
#endif
  if (total_mb) {
    *total_mb = total;
  }
  if (used_mb) {
    *used_mb = used;
  }
  if (free_mb) {
    *free_mb = free_mem;
  }
  if (process_mb) {
    *process_mb = proc;
  }
  return total > 0;
}

static bool fpv_tg_format_uptime(
    uint64_t seconds,
    char* buffer,
    size_t buffer_len) {
  if (!buffer || buffer_len == 0) {
    return false;
  }
  uint64_t days = seconds / 86400ULL;
  uint64_t hours = (seconds % 86400ULL) / 3600ULL;
  uint64_t minutes = (seconds % 3600ULL) / 60ULL;
  uint64_t secs = seconds % 60ULL;
  if (days > 0) {
    int written = snprintf(
        buffer, buffer_len, "%" PRIu64 "d %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
        days, hours, minutes, secs);
    return written > 0 && (size_t)written < buffer_len;
  }
  int written = snprintf(
      buffer, buffer_len, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
      hours, minutes, secs);
  return written > 0 && (size_t)written < buffer_len;
}

typedef struct fpv_tg_adv_profile_entry {
  char* order_id;
  uint64_t time_sec;
  double price;
} fpv_tg_adv_profile_entry_t;

static void fpv_tg_adv_profile_entries_destroy(
    fpv_tg_adv_profile_entry_t* entries,
    size_t count) {
  if (!entries) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(entries[i].order_id);
  }
  fpv_free(entries);
}

static fpv_tg_adv_profile_entry_t* fpv_tg_load_adv_profile(
    const fpv_telegram_service_t* service,
    size_t* out_count) {
  if (out_count) {
    *out_count = 0;
  }
  if (!service || !service->storage.cache_dir) {
    return NULL;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "advProfileStat.json");
  if (!path) {
    return NULL;
  }
  if (!fpv_fs_exists(path)) {
    fpv_free(path);
    return NULL;
  }
  size_t size = 0;
  char* content = fpv_tg_read_file(path, &size);
  fpv_free(path);
  if (!content) {
    return NULL;
  }
  fpv_json_value_t* root = NULL;
  fpv_json_error_t error;
  fpv_result_t result = fpv_json_parse(content, size, &root, &error);
  fpv_free(content);
  if (result != FPV_OK || !root || !fpv_json_is_type(root, FPV_JSON_OBJECT)) {
    fpv_json_destroy(root);
    return NULL;
  }
  size_t count = fpv_json_object_size(root);
  fpv_tg_adv_profile_entry_t* entries =
      (fpv_tg_adv_profile_entry_t*)calloc(count, sizeof(*entries));
  if (!entries) {
    fpv_json_destroy(root);
    return NULL;
  }
  size_t used = 0;
  for (size_t i = 0; i < count; i++) {
    const char* key = fpv_json_object_key(root, i);
    const fpv_json_value_t* value = fpv_json_object_value(root, i);
    if (!key || !key[0] || !value || !fpv_json_is_type(value, FPV_JSON_OBJECT)) {
      continue;
    }
    uint64_t time_sec = 0;
    double price = 0.0;
    if (!fpv_json_number_to_uint64(fpv_json_object_get(value, "time"), &time_sec) ||
        !fpv_json_number_to_double(fpv_json_object_get(value, "price"), &price)) {
      continue;
    }
    entries[used].order_id = fpv_strdup(key);
    if (!entries[used].order_id) {
      continue;
    }
    entries[used].time_sec = time_sec;
    entries[used].price = price;
    used++;
  }
  fpv_json_destroy(root);
  if (out_count) {
    *out_count = used;
  }
  return entries;
}

typedef struct fpv_tg_order_stats {
  uint32_t sales_day;
  uint32_t sales_week;
  uint32_t sales_month;
  uint32_t sales_all;
  double sales_price_day;
  double sales_price_week;
  double sales_price_month;
  double sales_price_all;
  uint32_t refunds_day;
  uint32_t refunds_week;
  uint32_t refunds_month;
  uint32_t refunds_all;
  double refunds_price_day;
  double refunds_price_week;
  double refunds_price_month;
  double refunds_price_all;
} fpv_tg_order_stats_t;

static bool fpv_tg_collect_order_stats(
    fpv_telegram_service_t* service,
    fpv_tg_order_stats_t* stats) {
  if (!service || !service->account || !stats) {
    return false;
  }
  memset(stats, 0, sizeof(*stats));
  uint64_t now_ms = fpv_time_now_ms();
  const uint64_t day_ms = 86400000ULL;
  const uint64_t week_ms = 7ULL * day_ms;
  const uint64_t month_ms = 30ULL * day_ms;
  char* continue_from = NULL;
  bool ok = true;
  do {
    fpv_order_t** orders = NULL;
    size_t order_count = 0;
    char* next = NULL;
    fpv_funpay_error_t error;
    memset(&error, 0, sizeof(error));
    fpv_result_t result = fpv_funpay_account_get_orders_page(
        service->account,
        NULL,
        continue_from,
        &orders,
        &order_count,
        &next,
        &error);
    fpv_funpay_error_clear(&error);
    fpv_free(continue_from);
    continue_from = NULL;
    if (result != FPV_OK) {
      if (orders) {
        for (size_t i = 0; i < order_count; i++) {
          fpv_order_destroy(orders[i]);
        }
        fpv_free(orders);
      }
      fpv_free(next);
      ok = false;
      break;
    }
    for (size_t i = 0; i < order_count; i++) {
      fpv_order_t* order = orders[i];
      if (!order) {
        continue;
      }
      bool refunded = order->status == FPV_ORDER_REFUNDED;
      double amount = order->amount;
      if (refunded) {
        stats->refunds_all++;
        stats->refunds_price_all += amount;
      } else {
        stats->sales_all++;
        stats->sales_price_all += amount;
      }
      if (order->created_at_ms > 0 && now_ms >= order->created_at_ms) {
        uint64_t age_ms = now_ms - order->created_at_ms;
        if (age_ms <= day_ms) {
          if (refunded) {
            stats->refunds_day++;
            stats->refunds_week++;
            stats->refunds_month++;
            stats->refunds_price_day += amount;
            stats->refunds_price_week += amount;
            stats->refunds_price_month += amount;
          } else {
            stats->sales_day++;
            stats->sales_week++;
            stats->sales_month++;
            stats->sales_price_day += amount;
            stats->sales_price_week += amount;
            stats->sales_price_month += amount;
          }
        } else if (age_ms <= week_ms) {
          if (refunded) {
            stats->refunds_week++;
            stats->refunds_month++;
            stats->refunds_price_week += amount;
            stats->refunds_price_month += amount;
          } else {
            stats->sales_week++;
            stats->sales_month++;
            stats->sales_price_week += amount;
            stats->sales_price_month += amount;
          }
        } else if (age_ms <= month_ms) {
          if (refunded) {
            stats->refunds_month++;
            stats->refunds_price_month += amount;
          } else {
            stats->sales_month++;
            stats->sales_price_month += amount;
          }
        }
      }
    }
    if (orders) {
      for (size_t i = 0; i < order_count; i++) {
        fpv_order_destroy(orders[i]);
      }
      fpv_free(orders);
    }
    continue_from = next;
  } while (continue_from && continue_from[0]);
  fpv_free(continue_from);
  return ok;
}

char* fpv_tg_build_adv_profile_text(
    fpv_telegram_service_t* service,
    const fpv_funpay_balance_t* balance) {
  if (!service || !service->account) {
    return NULL;
  }
  const char* username = fpv_funpay_account_username(service->account);
  uint64_t id = fpv_funpay_account_id(service->account);
  uint32_t active_sales = fpv_funpay_account_active_sales(service->account);
  const char* currency = fpv_funpay_account_currency(service->account);
  const char* symbol = "\xE2\x82\xBD";
  double total_balance = balance ? balance->total_rub : 0.0;
  double available_balance = balance ? balance->available_rub : 0.0;
  if (currency && strcmp(currency, "USD") == 0) {
    symbol = "$";
    total_balance = balance ? balance->total_usd : 0.0;
    available_balance = balance ? balance->available_usd : 0.0;
  } else if (currency && strcmp(currency, "EUR") == 0) {
    symbol = "\xE2\x82\xAC";
    total_balance = balance ? balance->total_eur : 0.0;
    available_balance = balance ? balance->available_eur : 0.0;
  }

  double can_hour = 0.0;
  double can_day = 0.0;
  double can_2day = 0.0;
  size_t entry_count = 0;
  fpv_tg_adv_profile_entry_t* entries =
      fpv_tg_load_adv_profile(service, &entry_count);
  uint64_t now_sec = (uint64_t)time(NULL);
  for (size_t i = 0; i < entry_count; i++) {
    uint64_t age = now_sec > entries[i].time_sec
        ? now_sec - entries[i].time_sec
        : 0;
    if (age > 172800) {
      continue;
    }
    if (age > 169200) {
      can_hour += entries[i].price;
    } else if (age > 86400) {
      can_day += entries[i].price;
    } else {
      can_2day += entries[i].price;
    }
  }
  fpv_tg_adv_profile_entries_destroy(entries, entry_count);

  fpv_tg_order_stats_t stats;
  if (!fpv_tg_collect_order_stats(service, &stats)) {
    return NULL;
  }

  char time_buf[32];
  time_buf[0] = '\0';
  uint64_t updated_ms = fpv_funpay_account_last_update_ms(service->account);
  if (updated_ms > 0) {
    fpv_tg_format_time_hms(updated_ms, time_buf, sizeof(time_buf));
  }

  char* escaped_user = fpv_tg_escape_html(username ? username : "");
  char buffer[8192];
  snprintf(
      buffer,
      sizeof(buffer),
      "Account statistics <b><i>%s</i></b>\n\n"
      "<b>ID:</b> <code>%" PRIu64 "</code>\n"
      "<b>Balance:</b> <code>%.2f %s</code>\n"
      "<b>Active orders:</b> <code>%u</code>\n\n"
      "<b>Available to withdraw</b>\n"
      "<b>Now:</b> <code>%.1f %s</code>\n"
      "<b>In an hour:</b> <code>+%.1f %s</code>\n"
      "<b>In a day:</b> <code>+%.1f %s</code>\n"
      "<b>In 2 days:</b> <code>+%.1f %s</code>\n\n"
      "<b>Goods sold</b>\n"
      "<b>Day:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Week:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Month:</b> <code>%u (%.1f %s)</code>\n"
      "<b>All time:</b> <code>%u (%.1f %s)</code>\n\n"
      "<b>Goods refunded</b>\n"
      "<b>Day:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Week:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Month:</b> <code>%u (%.1f %s)</code>\n"
      "<b>All time:</b> <code>%u (%.1f %s)</code>\n\n"
      "<i>Updated:</i>  <code>%s</code>",
      escaped_user ? escaped_user : "",
      id,
      total_balance,
      symbol,
      active_sales,
      available_balance,
      symbol,
      can_hour,
      symbol,
      can_day,
      symbol,
      can_2day,
      symbol,
      stats.sales_day,
      stats.sales_price_day,
      symbol,
      stats.sales_week,
      stats.sales_price_week,
      symbol,
      stats.sales_month,
      stats.sales_price_month,
      symbol,
      stats.sales_all,
      stats.sales_price_all,
      symbol,
      stats.refunds_day,
      stats.refunds_price_day,
      symbol,
      stats.refunds_week,
      stats.refunds_price_week,
      symbol,
      stats.refunds_month,
      stats.refunds_price_month,
      symbol,
      stats.refunds_all,
      stats.refunds_price_all,
      symbol,
      time_buf);
  fpv_free(escaped_user);
  return fpv_strdup(buffer);
}

char* fpv_tg_build_sysinfo_text(
    fpv_telegram_service_t* service,
    uint64_t chat_id) {
  if (!service) {
    return NULL;
  }
  double total_cpu = 0.0;
  double process_cpu = 0.0;
  fpv_tg_get_cpu_usage(&total_cpu, &process_cpu);
  uint64_t total_mb = 0;
  uint64_t used_mb = 0;
  uint64_t free_mb = 0;
  uint64_t proc_mb = 0;
  fpv_tg_get_memory_stats(&total_mb, &used_mb, &free_mb, &proc_mb);
  char cpu_lines[128];
  snprintf(cpu_lines, sizeof(cpu_lines), "    CPU:  <code>%.1f%%</code>", total_cpu);
  char process_buf[32];
  snprintf(process_buf, sizeof(process_buf), "%.1f", process_cpu);
  char total_buf[32];
  char used_buf[32];
  char free_buf[32];
  char proc_buf[32];
  snprintf(total_buf, sizeof(total_buf), "%" PRIu64, total_mb);
  snprintf(used_buf, sizeof(used_buf), "%" PRIu64, used_mb);
  snprintf(free_buf, sizeof(free_buf), "%" PRIu64, free_mb);
  snprintf(proc_buf, sizeof(proc_buf), "%" PRIu64, proc_mb);
  uint64_t uptime_sec = 0;
  if (service->start_ms > 0) {
    uint64_t now_ms = fpv_time_now_ms();
    if (now_ms > service->start_ms) {
      uptime_sec = (now_ms - service->start_ms) / 1000ULL;
    }
  }
  char uptime_buf[64];
  fpv_tg_format_uptime(uptime_sec, uptime_buf, sizeof(uptime_buf));
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, chat_id);
  char* formatted = fpv_tg_loc_format(
      service,
      "sys_info",
      (const char*[]){
          cpu_lines,
          process_buf,
          total_buf,
          used_buf,
          free_buf,
          proc_buf,
          uptime_buf,
          chat_buf},
      8);
  if (formatted) {
    return formatted;
  }
  return fpv_strdup("");
}
