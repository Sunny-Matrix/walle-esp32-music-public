#include "robot_s3_display.h"

#include "application.h"
#include "font_awesome_symbols.h"
#include "robot_control_server.h"

#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_16_4);

namespace {
constexpr uint32_t kHeaderColor = 0x263746;
constexpr uint32_t kAccentColor = 0xF26A2E;
constexpr uint32_t kChatBackground = 0xEDEDED;
constexpr uint32_t kUserBubble = 0x95EC69;
constexpr uint32_t kTextColor = 0x25313A;
constexpr char kWeatherUrl[] =
    "http://api.open-meteo.com/v1/forecast?latitude=38.87&longitude=115.48&"
    "current=temperature_2m,weather_code&timezone=Asia%2FShanghai";
constexpr int kWeatherRefreshMs = 10 * 60 * 1000;

struct HttpResponse {
    std::string body;
};

esp_err_t WeatherHttpEventHandler(esp_http_client_event_t* event) {
    auto* response = static_cast<HttpResponse*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && response != nullptr &&
        event->data != nullptr && event->data_len > 0) {
        response->body.append(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}

const char* WeatherText(int code) {
    if (code == 0) return "晴";
    if (code == 1 || code == 2) return "多云";
    if (code == 3) return "阴";
    if (code == 45 || code == 48) return "雾";
    if ((code >= 51 && code <= 57)) return "小雨";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "雨";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "雪";
    if (code >= 95 && code <= 99) return "雷雨";
    return "天气";
}

void MakePlain(lv_obj_t* obj) {
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* MakeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                    lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}
}  // namespace

RobotS3Display::RobotS3Display(esp_lcd_panel_io_handle_t panel_io,
                               esp_lcd_panel_handle_t panel, int width, int height,
                               int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy, DisplayFonts fonts)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy, fonts) {
    BuildUi();
}

RobotS3Display::~RobotS3Display() {
    if (clock_timer_ != nullptr) {
        lv_timer_delete(clock_timer_);
        clock_timer_ = nullptr;
    }
    if (weather_timer_ != nullptr) {
        lv_timer_delete(weather_timer_);
        weather_timer_ = nullptr;
    }
}

void RobotS3Display::BuildUi() {
    DisplayLockGuard lock(this);
    lv_obj_t* screen = lv_screen_active();

    if (low_battery_popup_ != nullptr) {
        lv_obj_delete(low_battery_popup_);
    }
    if (container_ != nullptr) {
        lv_obj_delete(container_);
    }

    status_bar_ = nullptr;
    content_ = nullptr;
    emotion_label_ = nullptr;
    network_label_ = nullptr;
    status_label_ = nullptr;
    notification_label_ = nullptr;
    mute_label_ = nullptr;
    battery_label_ = nullptr;
    chat_message_label_ = nullptr;
    low_battery_popup_ = nullptr;
    low_battery_label_ = nullptr;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_text_font(screen, &font_puhui_16_4, 0);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_pos(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x101820), 0);
    MakePlain(container_);

    // Idle page, modeled after the supplied dashboard reference.
    idle_page_ = lv_obj_create(container_);
    lv_obj_set_size(idle_page_, width_, height_);
    lv_obj_set_pos(idle_page_, 0, 0);
    lv_obj_set_style_bg_color(idle_page_, lv_color_white(), 0);
    MakePlain(idle_page_);

    lv_obj_t* weather_header = lv_obj_create(idle_page_);
    lv_obj_set_size(weather_header, width_, 66);
    lv_obj_set_pos(weather_header, 0, 0);
    lv_obj_set_style_bg_color(weather_header, lv_color_hex(kHeaderColor), 0);
    MakePlain(weather_header);

    lv_obj_t* accent = lv_obj_create(weather_header);
    lv_obj_set_size(accent, 4, 43);
    lv_obj_set_pos(accent, 11, 11);
    lv_obj_set_style_bg_color(accent, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_set_style_border_width(accent, 0, 0);

    location_label_ = MakeLabel(weather_header, "保定", &font_puhui_16_4,
                                 lv_color_white());
    lv_obj_set_pos(location_label_, 23, 5);
    lv_obj_t* owner = MakeLabel(weather_header, "WALI", &font_puhui_16_4,
                                lv_color_hex(0xFFB064));
    lv_obj_set_pos(owner, 23, 35);

    weather_label_ = MakeLabel(weather_header, "天气...", &font_puhui_20_4,
                               lv_color_hex(0xD6ECF5));
    lv_obj_set_width(weather_label_, 58);
    lv_obj_set_style_text_align(weather_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(weather_label_, 86, 17);

    temperature_label_ = MakeLabel(weather_header, "--°C", &lv_font_montserrat_30,
                                   lv_color_hex(0xFF8A50));
    lv_obj_set_pos(temperature_label_, 169, 13);

    idle_time_label_ = MakeLabel(idle_page_, "--:--", &lv_font_montserrat_30,
                                 lv_color_hex(kTextColor));
    lv_obj_set_width(idle_time_label_, width_);
    lv_obj_set_style_text_align(idle_time_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(idle_time_label_, 0, 78);

    idle_date_label_ = MakeLabel(idle_page_, "正在同步时间", &font_puhui_16_4,
                                 lv_color_hex(0x536472));
    lv_obj_set_width(idle_date_label_, width_);
    lv_obj_set_style_text_align(idle_date_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(idle_date_label_, 0, 121);

    lv_obj_t* standby_badge = lv_obj_create(idle_page_);
    lv_obj_set_size(standby_badge, 100, 35);
    lv_obj_set_pos(standby_badge, 70, 151);
    lv_obj_set_style_bg_color(standby_badge, lv_color_hex(kAccentColor), 0);
    lv_obj_set_style_radius(standby_badge, 6, 0);
    lv_obj_set_style_border_width(standby_badge, 0, 0);
    lv_obj_set_style_outline_width(standby_badge, 0, 0);
    lv_obj_set_style_shadow_width(standby_badge, 0, 0);
    lv_obj_set_style_pad_all(standby_badge, 0, 0);
    lv_obj_set_scrollbar_mode(standby_badge, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(standby_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* standby_text = MakeLabel(standby_badge, "待机中", &font_puhui_16_4,
                                       lv_color_white());
    lv_obj_center(standby_text);

    lv_obj_t* eye_bridge = lv_obj_create(idle_page_);
    lv_obj_set_size(eye_bridge, 30, 3);
    lv_obj_set_pos(eye_bridge, 105, 213);
    lv_obj_set_style_bg_color(eye_bridge, lv_color_hex(0x172126), 0);
    lv_obj_set_style_border_width(eye_bridge, 0, 0);
    lv_obj_set_style_radius(eye_bridge, 2, 0);
    lv_obj_set_style_pad_all(eye_bridge, 0, 0);
    lv_obj_remove_flag(eye_bridge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* left_eye = lv_obj_create(idle_page_);
    lv_obj_set_size(left_eye, 42, 42);
    lv_obj_set_pos(left_eye, 66, 193);
    lv_obj_set_style_radius(left_eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(left_eye, lv_color_hex(0xEAF8FA), 0);
    lv_obj_set_style_border_width(left_eye, 3, 0);
    lv_obj_set_style_border_color(left_eye, lv_color_hex(0x172126), 0);
    lv_obj_set_style_outline_width(left_eye, 0, 0);
    lv_obj_set_style_shadow_width(left_eye, 0, 0);
    lv_obj_set_style_pad_all(left_eye, 0, 0);
    lv_obj_set_scrollbar_mode(left_eye, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(left_eye, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* right_eye = lv_obj_create(idle_page_);
    lv_obj_set_size(right_eye, 42, 42);
    lv_obj_set_pos(right_eye, 132, 193);
    lv_obj_set_style_radius(right_eye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(right_eye, lv_color_hex(0xEAF8FA), 0);
    lv_obj_set_style_border_width(right_eye, 3, 0);
    lv_obj_set_style_border_color(right_eye, lv_color_hex(0x172126), 0);
    lv_obj_set_style_outline_width(right_eye, 0, 0);
    lv_obj_set_style_shadow_width(right_eye, 0, 0);
    lv_obj_set_style_pad_all(right_eye, 0, 0);
    lv_obj_set_scrollbar_mode(right_eye, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(right_eye, LV_OBJ_FLAG_SCROLLABLE);

    for (lv_obj_t* eye : {left_eye, right_eye}) {
        lv_obj_t* pupil = lv_obj_create(eye);
        lv_obj_set_size(pupil, 11, 11);
        lv_obj_set_style_radius(pupil, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(pupil, lv_color_hex(0x172126), 0);
        lv_obj_set_style_border_width(pupil, 0, 0);
        lv_obj_set_style_outline_width(pupil, 0, 0);
        lv_obj_set_style_shadow_width(pupil, 0, 0);
        lv_obj_set_style_pad_all(pupil, 0, 0);
        lv_obj_remove_flag(pupil, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(pupil);

        lv_obj_t* glint = lv_obj_create(pupil);
        lv_obj_set_size(glint, 3, 3);
        lv_obj_set_pos(glint, 2, 2);
        lv_obj_set_style_radius(glint, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(glint, lv_color_white(), 0);
        lv_obj_set_style_border_width(glint, 0, 0);
        lv_obj_set_style_pad_all(glint, 0, 0);
        lv_obj_remove_flag(glint, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Conversation page, sized specifically for the 240x240 screen.
    chat_page_ = lv_obj_create(container_);
    lv_obj_set_size(chat_page_, width_, height_);
    lv_obj_set_pos(chat_page_, 0, 0);
    lv_obj_set_style_bg_color(chat_page_, lv_color_hex(kChatBackground), 0);
    MakePlain(chat_page_);

    status_bar_ = lv_obj_create(chat_page_);
    lv_obj_set_size(status_bar_, width_, 38);
    lv_obj_set_pos(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, lv_color_hex(kHeaderColor), 0);
    MakePlain(status_bar_);

    lv_obj_t* wifi_icon = MakeLabel(status_bar_, FONT_AWESOME_WIFI,
                                    &font_awesome_16_4, lv_color_hex(0xD6ECF5));
    lv_obj_set_pos(wifi_icon, 10, 10);

    status_label_ = MakeLabel(status_bar_, "小智", &font_puhui_16_4, lv_color_white());
    lv_obj_set_width(status_label_, width_);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(status_label_, 0, 10);
    chat_time_label_ = MakeLabel(status_bar_, "--:--", &font_puhui_16_4,
                                 lv_color_hex(0xD6ECF5));
    lv_obj_set_align(chat_time_label_, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(chat_time_label_, -10, 10);

    notification_label_ = MakeLabel(status_bar_, "", &font_puhui_16_4,
                                    lv_color_hex(0xFFB064));
    lv_obj_set_width(notification_label_, 120);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(notification_label_, 60, 10);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    music_card_ = lv_obj_create(chat_page_);
    lv_obj_set_size(music_card_, 224, 38);
    lv_obj_set_pos(music_card_, 8, 43);
    lv_obj_set_style_bg_color(music_card_, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(music_card_, 1, 0);
    lv_obj_set_style_border_color(music_card_, lv_color_hex(0xD4D4D4), 0);
    lv_obj_set_style_radius(music_card_, 6, 0);
    lv_obj_set_style_pad_all(music_card_, 0, 0);
    music_label_ = MakeLabel(music_card_, "", &font_puhui_16_4,
                             lv_color_hex(kTextColor));
    lv_obj_set_width(music_label_, 204);
    lv_label_set_long_mode(music_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(music_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(music_label_, 10, 10);
    lv_obj_add_flag(music_card_, LV_OBJ_FLAG_HIDDEN);

    // The cloud tutor updates this card through MCP, keeping the screen in
    // sync with the question that the learner actually hears.
    mission_card_ = lv_obj_create(chat_page_);
    lv_obj_set_size(mission_card_, 224, 66);
    lv_obj_set_pos(mission_card_, 8, 43);
    lv_obj_set_style_bg_color(mission_card_, lv_color_hex(0xF3FAFA), 0);
    lv_obj_set_style_border_width(mission_card_, 1, 0);
    lv_obj_set_style_border_color(mission_card_, lv_color_hex(0x9ACBC5), 0);
    lv_obj_set_style_radius(mission_card_, 6, 0);
    lv_obj_set_style_pad_all(mission_card_, 0, 0);
    lv_obj_remove_flag(mission_card_, LV_OBJ_FLAG_SCROLLABLE);
    mission_stage_label_ = MakeLabel(mission_card_, "探究 01/03", &font_puhui_16_4,
                                     lv_color_hex(0xFFFFFF));
    lv_obj_set_size(mission_stage_label_, 70, 19);
    lv_obj_set_pos(mission_stage_label_, 9, 7);
    lv_obj_set_style_bg_color(mission_stage_label_, lv_color_hex(0x267A78), 0);
    lv_obj_set_style_radius(mission_stage_label_, 4, 0);
    lv_obj_set_style_text_align(mission_stage_label_, LV_TEXT_ALIGN_CENTER, 0);
    mission_title_label_ = MakeLabel(mission_card_, "雨的形成", &font_puhui_16_4,
                                     lv_color_hex(0x185B5E));
    lv_obj_set_pos(mission_title_label_, 89, 8);
    mission_detail_label_ = MakeLabel(mission_card_, "瓦力正在准备问题...", &font_puhui_16_4,
                                      lv_color_hex(0x405B63));
    lv_obj_set_width(mission_detail_label_, 204);
    lv_label_set_long_mode(mission_detail_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(mission_detail_label_, 10, 31);
    lv_obj_t* mission_progress = lv_obj_create(mission_card_);
    lv_obj_set_size(mission_progress, 204, 4);
    lv_obj_set_pos(mission_progress, 10, 54);
    lv_obj_set_style_bg_color(mission_progress, lv_color_hex(0xD6E9E7), 0);
    lv_obj_set_style_radius(mission_progress, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(mission_progress, 0, 0);
    lv_obj_set_style_pad_all(mission_progress, 0, 0);
    lv_obj_remove_flag(mission_progress, LV_OBJ_FLAG_SCROLLABLE);
    mission_progress_fill_ = lv_obj_create(mission_progress);
    lv_obj_set_size(mission_progress_fill_, 68, 4);
    lv_obj_set_pos(mission_progress_fill_, 0, 0);
    lv_obj_set_style_bg_color(mission_progress_fill_, lv_color_hex(0x31A79A), 0);
    lv_obj_set_style_radius(mission_progress_fill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(mission_progress_fill_, 0, 0);
    lv_obj_set_style_pad_all(mission_progress_fill_, 0, 0);
    lv_obj_remove_flag(mission_progress_fill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mission_card_, LV_OBJ_FLAG_HIDDEN);

    content_ = lv_obj_create(chat_page_);
    lv_obj_set_size(content_, 240, 202);
    lv_obj_set_pos(content_, 0, 38);
    lv_obj_set_style_bg_color(content_, lv_color_hex(kChatBackground), 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 7, 0);
    lv_obj_set_style_pad_row(content_, 7, 0);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);

    ShowIdlePage();
    UpdateClockLabels();
    clock_timer_ = lv_timer_create(ClockTimerCallback, 1000, this);
    weather_timer_ = lv_timer_create(WeatherTimerCallback, kWeatherRefreshMs, this);
    StartWeatherFetch();
}

void RobotS3Display::ShowIdlePage() {
    lv_obj_clear_flag(idle_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(chat_page_, LV_OBJ_FLAG_HIDDEN);
    UpdateClockLabels();
}

void RobotS3Display::ShowChatPage() {
    lv_obj_add_flag(idle_page_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(chat_page_, LV_OBJ_FLAG_HIDDEN);
}

void RobotS3Display::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        return;
    }
    const char* safe_status = status == nullptr ? "" : status;
    lv_label_set_text(status_label_, safe_status);
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        ShowIdlePage();
    } else {
        ShowChatPage();
    }
}

void RobotS3Display::ShowNotification(const char* notification, int duration_ms) {
    (void)duration_ms;
    DisplayLockGuard lock(this);
    ShowChatPage();
    lv_label_set_text(notification_label_, notification == nullptr ? "" : notification);
    lv_obj_clear_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
}

void RobotS3Display::SetEmotion(const char* emotion) {
    (void)emotion;
}

void RobotS3Display::SetIcon(const char* icon) {
    (void)icon;
}

void RobotS3Display::SetChatMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr) {
        return;
    }
    if (content[0] == '\0') {
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            DisplayLockGuard lock(this);
            ShowIdlePage();
        }
        return;
    }
    if (strncmp(content, "% self.", 7) == 0 || strncmp(content, "% music.", 8) == 0) {
        return;
    }

    DisplayLockGuard lock(this);
    ShowChatPage();
    MaybeStartLearningMission(role, content);

    if (strcmp(role, "lyric") == 0) {
        if (lyric_label_ == nullptr) {
            AddMessageBubble("lyric", content);
        } else {
            lv_label_set_text(lyric_label_, content);
            lv_obj_scroll_to_view_recursive(lyric_row_, LV_ANIM_ON);
        }
        return;
    }

    AddMessageBubble(role, content);
    if (strcmp(role, "user") == 0 || strcmp(role, "assistant") == 0) {
        RecordLearningMessage(role, content);
    }
}

void RobotS3Display::MaybeStartLearningMission(const char* role, const char* content) {
    // Learning mode is selected by the cloud tutor through MCP. Do not infer
    // a subject from keywords here, so every school subject can use the same
    // guided-learning flow without changing the normal chat experience.
    (void)role;
    (void)content;
}

void RobotS3Display::UpdateLearningMission(const char* title, const char* prompt) {
    (void)title;
    (void)prompt;
    if (mission_card_ == nullptr) return;

    // Learning guidance is intentionally voice-led. Keep the MCP state for
    // the cloud tutor, but do not occupy the chat page with a second label.
    lv_obj_add_flag(mission_card_, LV_OBJ_FLAG_HIDDEN);
    if (!music_active_ && content_ != nullptr) {
        lv_obj_set_pos(content_, 0, 38);
        lv_obj_set_size(content_, 240, 202);
    }
}

void RobotS3Display::SetLearningMission(int stage, const char* title, const char* prompt) {
    DisplayLockGuard lock(this);
    mission_active_ = true;
    mission_step_ = std::clamp(stage, 0, 3);
    ShowChatPage();
    UpdateLearningMission(title, prompt);
}

void RobotS3Display::ClearLearningMission() {
    DisplayLockGuard lock(this);
    mission_active_ = false;
    mission_step_ = 0;
    if (mission_card_ != nullptr) {
        lv_obj_add_flag(mission_card_, LV_OBJ_FLAG_HIDDEN);
    }
    if (!music_active_ && content_ != nullptr) {
        lv_obj_set_pos(content_, 0, 38);
        lv_obj_set_size(content_, 240, 202);
    }
}

void RobotS3Display::AddMessageBubble(const char* role, const char* content) {
    const bool is_user = strcmp(role, "user") == 0;
    const bool is_system = strcmp(role, "system") == 0;
    const bool is_lyric = strcmp(role, "lyric") == 0;

    lv_obj_t* row = lv_obj_create(content_);
    lv_obj_set_width(row, 226);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 5, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, is_system ? LV_FLEX_ALIGN_CENTER
                                         : (is_user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START),
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    auto create_avatar = [row, is_user]() {
        lv_obj_t* avatar = lv_obj_create(row);
        lv_obj_set_size(avatar, 25, 25);
        lv_obj_set_style_radius(avatar, 5, 0);
        lv_obj_set_style_border_width(avatar, 0, 0);
        lv_obj_set_style_pad_all(avatar, 0, 0);
        lv_obj_set_style_bg_color(avatar,
                                  lv_color_hex(is_user ? 0x52B788 : 0x3A7CA5), 0);
        lv_obj_t* text = MakeLabel(avatar, is_user ? "我" : "瓦", &font_puhui_16_4,
                                   lv_color_white());
        lv_obj_center(text);
    };

    if (!is_user && !is_system) {
        create_avatar();
    }

    lv_obj_t* bubble = lv_obj_create(row);
    // Short messages stay compact; long messages use the available width and wrap.
    const lv_font_t* message_font = &font_puhui_16_4;
    const int max_bubble_width = is_system ? 150 : 171;
    const int min_bubble_width = is_system ? 48 : 52;
    const int horizontal_padding = is_system ? 10 : 14;
    const int measured_text_width = lv_txt_get_width(
        content, strlen(content), message_font, 0);
    const int bubble_width = std::clamp(
        measured_text_width + horizontal_padding, min_bubble_width, max_bubble_width);
    lv_obj_set_width(bubble, bubble_width);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, is_system ? 5 : 7, 0);
    lv_obj_set_style_border_width(bubble, is_user ? 0 : 1, 0);
    lv_obj_set_style_border_color(bubble, lv_color_hex(0xD2D2D2), 0);
    lv_obj_set_style_pad_all(bubble, is_system ? 5 : 7, 0);
    lv_obj_set_style_bg_color(
        bubble, lv_color_hex(is_system ? 0xD8D8D8 : (is_user ? kUserBubble : 0xFFFFFF)), 0);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* text = MakeLabel(bubble, content, message_font,
                               lv_color_hex(is_system ? 0x65727C : kTextColor));
    lv_obj_set_width(text, bubble_width - horizontal_padding);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(text, is_system ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);

    if (is_user) {
        create_avatar();
    }
    if (is_lyric) {
        lyric_row_ = row;
        lyric_label_ = text;
    }

    chat_message_label_ = text;
    ++message_count_;
    TrimMessages();
    lv_obj_scroll_to_view_recursive(row, LV_ANIM_ON);
}

void RobotS3Display::TrimMessages() {
    while (message_count_ > kMaxMessages && lv_obj_get_child_cnt(content_) > 0) {
        lv_obj_t* first = lv_obj_get_child(content_, 0);
        if (first == lyric_row_) {
            lyric_row_ = nullptr;
            lyric_label_ = nullptr;
        }
        lv_obj_delete(first);
        --message_count_;
    }
}

void RobotS3Display::SetMusicInfo(const char* song_name) {
    DisplayLockGuard lock(this);
    const bool has_song = song_name != nullptr && song_name[0] != '\0';
    music_active_ = has_song;
    if (has_song) {
        ShowChatPage();
        lv_obj_add_flag(mission_card_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(music_label_, song_name);
        lv_obj_clear_flag(music_card_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(content_, 0, 84);
        lv_obj_set_size(content_, 240, 156);
        lv_label_set_text(status_label_, "音乐播放中");
    } else {
        lv_obj_add_flag(music_card_, LV_OBJ_FLAG_HIDDEN);
        if (mission_active_) {
            UpdateLearningMission();
        } else {
            lv_obj_set_pos(content_, 0, 38);
            lv_obj_set_size(content_, 240, 202);
        }
        lyric_row_ = nullptr;
        lyric_label_ = nullptr;
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            ShowIdlePage();
        }
    }
}

void RobotS3Display::UpdateStatusBar(bool update_all) {
    (void)update_all;
    DisplayLockGuard lock(this);
    UpdateClockLabels();
}

void RobotS3Display::UpdateClockLabels() {
    time_t now = time(nullptr);
    struct tm time_info;
    localtime_r(&now, &time_info);
    if (time_info.tm_year < 125) {
        return;
    }

    char time_text[8];
    strftime(time_text, sizeof(time_text), "%H:%M", &time_info);
    lv_label_set_text(idle_time_label_, time_text);
    lv_label_set_text(chat_time_label_, time_text);

    static const char* weekdays[] = {"周日", "周一", "周二", "周三",
                                      "周四", "周五", "周六"};
    char date_text[48];
    snprintf(date_text, sizeof(date_text), "%d年%02d月%02d日  %s",
             time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
             weekdays[time_info.tm_wday]);
    lv_label_set_text(idle_date_label_, date_text);
}

void RobotS3Display::StartWeatherFetch() {
    bool expected = false;
    if (!weather_fetching_.compare_exchange_strong(expected, true)) {
        return;
    }

    xTaskCreate([](void* arg) {
        auto* self = static_cast<RobotS3Display*>(arg);
        // The display is constructed before the network stack finishes starting.
        // Wait before the first request so lwIP is ready.
        vTaskDelay(pdMS_TO_TICKS(15000));
        self->FetchWeather();
        self->weather_fetching_.store(false);
        vTaskDelete(nullptr);
    }, "weather_fetch", 6144, this, 2, nullptr);
}

void RobotS3Display::FetchWeather() {
    HttpResponse response;
    esp_http_client_config_t config = {};
    config.url = kWeatherUrl;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.event_handler = WeatherHttpEventHandler;
    config.user_data = &response;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGW("RobotS3Weather", "Failed to create HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW("RobotS3Weather", "Weather request failed: %s, status=%d",
                 esp_err_to_name(err), status);
        return;
    }

    cJSON* root = cJSON_Parse(response.body.c_str());
    cJSON* current = root == nullptr ? nullptr : cJSON_GetObjectItem(root, "current");
    cJSON* temperature = current == nullptr ? nullptr :
                         cJSON_GetObjectItem(current, "temperature_2m");
    cJSON* weather_code = current == nullptr ? nullptr :
                          cJSON_GetObjectItem(current, "weather_code");
    if (temperature == nullptr || !cJSON_IsNumber(temperature) ||
        weather_code == nullptr || !cJSON_IsNumber(weather_code)) {
        ESP_LOGW("RobotS3Weather", "Invalid weather response");
        if (root != nullptr) cJSON_Delete(root);
        return;
    }

    UpdateWeatherLabels(WeatherText(weather_code->valueint),
                        static_cast<float>(temperature->valuedouble));
    cJSON_Delete(root);
}

void RobotS3Display::UpdateWeatherLabels(const char* weather, float temperature) {
    DisplayLockGuard lock(this);
    if (weather_label_ == nullptr || temperature_label_ == nullptr) {
        return;
    }
    lv_label_set_text(weather_label_, weather);
    char temperature_text[16];
    snprintf(temperature_text, sizeof(temperature_text), "%.0f°C", temperature);
    lv_label_set_text(temperature_label_, temperature_text);
}

void RobotS3Display::ClockTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<RobotS3Display*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->UpdateClockLabels();
    }
}

void RobotS3Display::WeatherTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<RobotS3Display*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->StartWeatherFetch();
    }
}
