/* FunPay Vertex Telegram service constants. */

#include "telegram/core/fpv_telegram_internal.h"


const char* fpv_tg_notification_ids[] = {
    "1",
    "2",
    "3",
    "4",
    "5",
    "5r",
    "6",
    "7",
    "8",
    "9",
    "10",
    "11",
    "12",
    "13",
    "14"};

const size_t fpv_tg_notification_count =
    sizeof(fpv_tg_notification_ids) / sizeof(fpv_tg_notification_ids[0]);

const size_t fpv_tg_cmd_page = 15;
const size_t fpv_tg_ad_page = 15;
const size_t fpv_tg_fp_lot_page = 15;
const size_t fpv_tg_products_page = 15;
const size_t fpv_tg_template_page = 15;
const int64_t fpv_tg_max_upload_size = 20 * 1024 * 1024;

const char* fpv_tg_cbt_main = "1";
const char* fpv_tg_cbt_category = "2";
const char* fpv_tg_cbt_switch = "3";
const char* fpv_tg_cbt_add_cmd = "4";
const char* fpv_tg_cbt_cmd_list = "5";
const char* fpv_tg_cbt_edit_cmd = "6";
const char* fpv_tg_cbt_edit_cmd_response = "7";
const char* fpv_tg_cbt_edit_cmd_notification = "8";
const char* fpv_tg_cbt_switch_cmd_notification = "9";
const char* fpv_tg_cbt_del_cmd = "10";
const char* fpv_tg_cbt_fp_lots = "11";
const char* fpv_tg_cbt_add_ad_lot = "12";
const char* fpv_tg_cbt_add_ad_lot_manual = "13";
const char* fpv_tg_cbt_ad_lots = "14";
const char* fpv_tg_cbt_edit_ad_lot = "15";
const char* fpv_tg_cbt_edit_lot_text = "16";
const char* fpv_tg_cbt_bind_products = "17";
const char* fpv_tg_cbt_del_ad_lot = "18";
const char* fpv_tg_cbt_products_list = "19";
const char* fpv_tg_cbt_edit_products_file = "20";
const char* fpv_tg_cbt_upload_products_file = "21";
const char* fpv_tg_cbt_create_products_file = "22";
const char* fpv_tg_cbt_add_products = "23";
const char* fpv_tg_cbt_download_cfg = "24";
const char* fpv_tg_cbt_template_list = "25";
const char* fpv_tg_cbt_template_list_ans = "26";
const char* fpv_tg_cbt_edit_template = "27";
const char* fpv_tg_cbt_del_template = "28";
const char* fpv_tg_cbt_add_template = "29";
const char* fpv_tg_cbt_send_template = "30";
const char* fpv_tg_cbt_switch_tg = "31";
const char* fpv_tg_cbt_request_refund = "32";
const char* fpv_tg_cbt_refund_confirmed = "33";
const char* fpv_tg_cbt_refund_cancelled = "34";
const char* fpv_tg_cbt_ban = "35";
const char* fpv_tg_cbt_unban = "36";
const char* fpv_tg_cbt_shutdown = "37";
const char* fpv_tg_cbt_cancel_shutdown = "38";
const char* fpv_tg_cbt_send_fp_message = "to_node";
const char* fpv_tg_cbt_upload_image = "upload_image";
const char* fpv_tg_cbt_update_profile = "39";
const char* fpv_tg_cbt_manual_ad_test = "40";
const char* fpv_tg_cbt_clear_state = "41";
const char* fpv_tg_cbt_back_to_reply = "42";
const char* fpv_tg_cbt_back_to_order = "43";
const char* fpv_tg_cbt_param_disabled = "53";
const char* fpv_tg_cbt_main2 = "54";
const char* fpv_tg_cbt_edit_greetings = "55";
const char* fpv_tg_cbt_edit_order_confirm = "56";
const char* fpv_tg_cbt_send_review_reply = "57";
const char* fpv_tg_cbt_edit_review_reply = "58";
const char* fpv_tg_cbt_edit_watermark = "59";
const char* fpv_tg_cbt_extend_chat = "60";
const char* fpv_tg_cbt_old_help = "61";
const char* fpv_tg_cbt_empty = "62";
const char* fpv_tg_cbt_lang = "63";
const char* fpv_tg_cbt_auth_login = "auth_login";
const char* fpv_tg_cbt_auth_join = "auth_join";
const char* fpv_tg_cbt_auth_create = "auth_create";
const char* fpv_tg_cbt_auth_timezone = "auth_tz";
const char* fpv_tg_cbt_auth_timezone_custom = "auth_tz_custom";
const char* fpv_tg_cbt_auth_currency = "auth_cur";
const char* fpv_tg_cbt_auth_currency_custom = "auth_cur_custom";
const char* fpv_tg_menu_core = "core";
const char* fpv_tg_menu_notify = "notify";
const char* fpv_tg_menu_reply = "reply";
const char* fpv_tg_menu_delivery = "delivery";
const char* fpv_tg_cb_update_profile = "update_profile";
const char* fpv_tg_cb_update_adv_profile = "update_adv_profile";
const char* fpv_tg_cb_config_loader = "config_loader";
const char* fpv_tg_cb_upload_main_config = "upload_main_config";
const char* fpv_tg_cb_upload_auto_response_config =
    "upload_auto_response_config";
const char* fpv_tg_cb_upload_auto_delivery_config =
    "upload_auto_delivery_config";
const char* fpv_tg_cb_switch_lot = "switch_lot";
const char* fpv_tg_cb_test_auto_delivery = "test_auto_delivery";
const char* fpv_tg_cb_update_funpay_lots = "update_funpay_lots";
const char* fpv_tg_cb_download_products_file = "download_products_file";
const char* fpv_tg_cb_delete_products_file = "del_products_file";
const char* fpv_tg_cb_confirm_delete_products_file =
    "confirm_del_products_file";
