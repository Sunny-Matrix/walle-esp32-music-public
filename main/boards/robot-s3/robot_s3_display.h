#ifndef ROBOT_S3_DISPLAY_H
#define ROBOT_S3_DISPLAY_H

#include "display/lcd_display.h"

#include <atomic>
#include <string>

class RobotS3Display : public SpiLcdDisplay {
public:
    RobotS3Display(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy,
                   DisplayFonts fonts);
    ~RobotS3Display() override;

    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void SetEmotion(const char* emotion) override;
    void SetIcon(const char* icon) override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetMusicInfo(const char* song_name) override;
    void UpdateStatusBar(bool update_all = false) override;
    bool IsMusicActive() const { return music_active_; }
    void SetLearningMission(int stage, const char* title, const char* prompt);
    void ClearLearningMission();

private:
    static constexpr int kMaxMessages = 10;

    lv_obj_t* idle_page_ = nullptr;
    lv_obj_t* chat_page_ = nullptr;
    lv_obj_t* idle_time_label_ = nullptr;
    lv_obj_t* idle_date_label_ = nullptr;
    lv_obj_t* location_label_ = nullptr;
    lv_obj_t* weather_label_ = nullptr;
    lv_obj_t* temperature_label_ = nullptr;
    lv_obj_t* chat_time_label_ = nullptr;
    lv_obj_t* music_card_ = nullptr;
    lv_obj_t* music_label_ = nullptr;
    lv_obj_t* mission_card_ = nullptr;
    lv_obj_t* mission_stage_label_ = nullptr;
    lv_obj_t* mission_title_label_ = nullptr;
    lv_obj_t* mission_detail_label_ = nullptr;
    lv_obj_t* mission_progress_fill_ = nullptr;
    lv_obj_t* lyric_label_ = nullptr;
    lv_obj_t* lyric_row_ = nullptr;
    lv_timer_t* clock_timer_ = nullptr;
    lv_timer_t* weather_timer_ = nullptr;
    std::atomic<bool> weather_fetching_{false};
    int message_count_ = 0;
    int mission_step_ = 0;
    bool music_active_ = false;
    bool mission_active_ = false;

    void BuildUi();
    void ShowIdlePage();
    void ShowChatPage();
    void UpdateClockLabels();
    void StartWeatherFetch();
    void FetchWeather();
    void UpdateWeatherLabels(const char* weather, float temperature);
    void AddMessageBubble(const char* role, const char* content);
    void TrimMessages();
    void UpdateLearningMission(const char* title = nullptr, const char* prompt = nullptr);
    void MaybeStartLearningMission(const char* role, const char* content);

    static void ClockTimerCallback(lv_timer_t* timer);
    static void WeatherTimerCallback(lv_timer_t* timer);
};

#endif
