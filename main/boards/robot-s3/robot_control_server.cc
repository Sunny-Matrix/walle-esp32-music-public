#include "robot_control_server.h"

#include "config.h"

#include <cJSON.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <led_strip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

namespace {
constexpr char kTag[] = "RobotControl";
constexpr int kServoFrequency = 50;
constexpr int kServoResolution = LEDC_TIMER_14_BIT;
constexpr ledc_timer_t kServoTimer = LEDC_TIMER_1;
constexpr int kServoMinUs = 500;
constexpr int kServoMaxUs = 2500;

httpd_handle_t server = nullptr;
led_strip_handle_t eye_strip = nullptr;
int servo_values[5] = {0, 0, 90, 90, 90};
bool lights_on = false;
uint8_t light_rgb[3] = {242, 106, 46};

struct LearningEvent {
    uint32_t id;
    std::string role;
    std::string content;
};

std::mutex learning_mutex;
std::deque<LearningEvent> learning_events;
uint32_t learning_event_id = 0;

enum ServoIndex { kLeftMotor, kRightMotor, kLeftArm, kRightArm, kHead };

int ChannelForServo(ServoIndex index) {
    return static_cast<int>(LEDC_CHANNEL_1) + static_cast<int>(index);
}

int ClampValue(int value, int min, int max) {
    return std::max(min, std::min(value, max));
}

bool SetServoByName(const char* name, int value);
esp_err_t SendJson(httpd_req_t* req, cJSON* root);

void SetServo(ServoIndex index, int value) {
    const bool wheel = index == kLeftMotor || index == kRightMotor;
    value = ClampValue(value, wheel ? -100 : 0, wheel ? 100 : 180);
    const int angle = wheel ? 90 + value * 90 / 100 : value;
    const int pulse_us = kServoMinUs + (kServoMaxUs - kServoMinUs) * angle / 180;
    const uint32_t duty = static_cast<uint32_t>(pulse_us) * ((1 << 14) - 1) / 20000;
    const auto channel = static_cast<ledc_channel_t>(ChannelForServo(index));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
    servo_values[index] = value;
}

bool ReadQueryValue(httpd_req_t* req, const char* key, int* value) {
    const size_t length = httpd_req_get_url_query_len(req);
    if (length == 0 || length >= 128) return false;
    char query[128] = {};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    char text[24] = {};
    if (httpd_query_key_value(query, key, text, sizeof(text)) != ESP_OK) return false;
    char* end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0') return false;
    *value = static_cast<int>(parsed);
    return true;
}

esp_err_t FastServoHandler(httpd_req_t* req) {
    const size_t length = httpd_req_get_url_query_len(req);
    if (length == 0 || length >= 128) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "query required");
        return ESP_OK;
    }
    char query[128] = {};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char name[24] = {};
    if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name required");
        return ESP_OK;
    }
    int value = 0;
    if (!ReadQueryValue(req, "value", &value) || !SetServoByName(name, value)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid servo");
        return ESP_OK;
    }
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "value", value);
    return SendJson(req, result);
}

esp_err_t FastDriveHandler(httpd_req_t* req) {
    int left = 0;
    int right = 0;
    if (!ReadQueryValue(req, "left", &left) || !ReadQueryValue(req, "right", &right)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "left and right required");
        return ESP_OK;
    }
    SetServo(kLeftMotor, left);
    SetServo(kRightMotor, right);
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    return SendJson(req, result);
}

void SetLights(bool on, uint8_t r, uint8_t g, uint8_t b) {
    lights_on = on;
    light_rgb[0] = r;
    light_rgb[1] = g;
    light_rgb[2] = b;
    if (eye_strip == nullptr) return;
    for (int i = 0; i < EYE_LED_NUM; ++i) {
        led_strip_set_pixel(eye_strip, i, on ? r : 0, on ? g : 0, on ? b : 0);
    }
    led_strip_refresh(eye_strip);
}

bool SetServoByName(const char* name, int value) {
    if (name == nullptr) return false;
    ServoIndex index;
    if (strcmp(name, "leftMotor") == 0) index = kLeftMotor;
    else if (strcmp(name, "rightMotor") == 0) index = kRightMotor;
    else if (strcmp(name, "leftArm") == 0) index = kLeftArm;
    else if (strcmp(name, "rightArm") == 0) index = kRightArm;
    else if (strcmp(name, "head") == 0) index = kHead;
    else return false;
    SetServo(index, value);
    return true;
}

void AddCors(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

esp_err_t OptionsHandler(httpd_req_t* req) {
    AddCors(req);
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t SendJson(httpd_req_t* req, cJSON* root) {
    AddCors(req);
    char* text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) return ESP_FAIL;
    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    free(text);
    return result;
}

esp_err_t StatusHandler(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "online", true);
    cJSON_AddBoolToObject(root, "lightsOn", lights_on);
    cJSON* rgb = cJSON_AddArrayToObject(root, "rgb");
    for (uint8_t value : light_rgb) cJSON_AddItemToArray(rgb, cJSON_CreateNumber(value));
    cJSON* servos = cJSON_AddObjectToObject(root, "servos");
    cJSON_AddNumberToObject(servos, "leftMotor", servo_values[kLeftMotor]);
    cJSON_AddNumberToObject(servos, "rightMotor", servo_values[kRightMotor]);
    cJSON_AddNumberToObject(servos, "leftArm", servo_values[kLeftArm]);
    cJSON_AddNumberToObject(servos, "rightArm", servo_values[kRightArm]);
    cJSON_AddNumberToObject(servos, "head", servo_values[kHead]);
    return SendJson(req, root);
}

esp_err_t LearningRecentHandler(httpd_req_t* req) {
    cJSON* root = cJSON_CreateObject();
    cJSON* events = cJSON_AddArrayToObject(root, "events");
    {
        std::lock_guard<std::mutex> lock(learning_mutex);
        for (const auto& event : learning_events) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "id", event.id);
            cJSON_AddStringToObject(item, "role", event.role.c_str());
            cJSON_AddStringToObject(item, "content", event.content.c_str());
            cJSON_AddItemToArray(events, item);
        }
    }
    return SendJson(req, root);
}

bool ReadJson(httpd_req_t* req, cJSON** root) {
    if (req->content_len <= 0 || req->content_len > 2048) return false;
    char* body = static_cast<char*>(calloc(1, req->content_len + 1));
    if (body == nullptr) return false;
    int received = httpd_req_recv(req, body, req->content_len);
    *root = received == req->content_len ? cJSON_Parse(body) : nullptr;
    free(body);
    return *root != nullptr;
}

esp_err_t ServoHandler(httpd_req_t* req) {
    cJSON* root = nullptr;
    if (!ReadJson(req, &root)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_OK;
    }
    const cJSON* name = cJSON_GetObjectItem(root, "name");
    const cJSON* value = cJSON_GetObjectItem(root, "value");
    if (!cJSON_IsString(name) || !cJSON_IsNumber(value)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "name and value required");
        return ESP_OK;
    }
    const char* key = name->valuestring;
    if (!SetServoByName(key, value->valueint)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown servo");
        return ESP_OK;
    }
    ServoIndex index = kHead;
    if (strcmp(key, "leftMotor") == 0) index = kLeftMotor;
    else if (strcmp(key, "rightMotor") == 0) index = kRightMotor;
    else if (strcmp(key, "leftArm") == 0) index = kLeftArm;
    else if (strcmp(key, "rightArm") == 0) index = kRightArm;
    cJSON_Delete(root);
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "value", servo_values[index]);
    return SendJson(req, result);
}

esp_err_t DriveHandler(httpd_req_t* req) {
    cJSON* root = nullptr;
    if (!ReadJson(req, &root)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_OK;
    }
    cJSON* left = cJSON_GetObjectItem(root, "left");
    cJSON* right = cJSON_GetObjectItem(root, "right");
    if (cJSON_IsNumber(left)) SetServo(kLeftMotor, left->valueint);
    if (cJSON_IsNumber(right)) SetServo(kRightMotor, right->valueint);
    cJSON_Delete(root);
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    return SendJson(req, result);
}

esp_err_t LightsHandler(httpd_req_t* req) {
    cJSON* root = nullptr;
    if (!ReadJson(req, &root)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_OK;
    }
    cJSON* on = cJSON_GetObjectItem(root, "on");
    cJSON* r = cJSON_GetObjectItem(root, "r");
    cJSON* g = cJSON_GetObjectItem(root, "g");
    cJSON* b = cJSON_GetObjectItem(root, "b");
    SetLights(cJSON_IsTrue(on),
              cJSON_IsNumber(r) ? ClampValue(r->valueint, 0, 255) : light_rgb[0],
              cJSON_IsNumber(g) ? ClampValue(g->valueint, 0, 255) : light_rgb[1],
              cJSON_IsNumber(b) ? ClampValue(b->valueint, 0, 255) : light_rgb[2]);
    cJSON_Delete(root);
    cJSON* result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    return SendJson(req, result);
}

void InitHardware() {
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = kServoTimer;
    timer.duty_resolution = static_cast<ledc_timer_bit_t>(kServoResolution);
    timer.freq_hz = kServoFrequency;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    const int pins[] = {L_MOTOR_PIN, R_MOTOR_PIN, L_ARM_PIN, R_ARM_PIN, HEAD_PIN};
    for (int i = 0; i < 5; ++i) {
        ledc_channel_config_t channel = {};
        channel.gpio_num = pins[i];
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        // LEDC channel 0 is reserved by PwmBacklight on GPIO3.
        // Keep all servo outputs on channels 1..5 so starting the robot
        // control task cannot detach the display backlight.
        channel.channel = static_cast<ledc_channel_t>(ChannelForServo(static_cast<ServoIndex>(i)));
        channel.intr_type = LEDC_INTR_DISABLE;
        channel.timer_sel = kServoTimer;
        channel.duty = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
        ESP_LOGI(kTag, "Servo %d: GPIO%d, LEDC channel %d", i, pins[i], channel.channel);
    }
    for (int i = 0; i < 5; ++i) SetServo(static_cast<ServoIndex>(i), servo_values[i]);
    ESP_LOGI(kTag, "Servo PWM initialized on LEDC channels 1..5; backlight remains on channel 0");

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = EYE_STRIP_GPIO;
    strip_config.max_leds = EYE_LED_NUM;
    strip_config.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.flags.with_dma = false;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &eye_strip));
    SetLights(false, light_rgb[0], light_rgb[1], light_rgb[2]);
}
}  // namespace

bool RobotDrive(int left, int right) {
    SetServo(kLeftMotor, left);
    SetServo(kRightMotor, right);
    return true;
}

bool RobotSetServo(const char* name, int value) {
    return SetServoByName(name, value);
}

bool RobotSetLights(bool on, int r, int g, int b) {
    SetLights(on, ClampValue(r, 0, 255), ClampValue(g, 0, 255), ClampValue(b, 0, 255));
    return true;
}

void StartRobotControlServer() {
    xTaskCreate([](void*) {
        // The board is constructed before the Wi-Fi/lwIP stack is ready.
        vTaskDelay(pdMS_TO_TICKS(15000));
        InitHardware();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start control server");
        return;
    }
    const httpd_uri_t options = {"/*", HTTP_OPTIONS, OptionsHandler, nullptr};
    const httpd_uri_t status = {"/api/status", HTTP_GET, StatusHandler, nullptr};
    const httpd_uri_t learning_recent = {"/api/learning/recent", HTTP_GET, LearningRecentHandler, nullptr};
    const httpd_uri_t servo = {"/api/control/servo", HTTP_POST, ServoHandler, nullptr};
    const httpd_uri_t fast_servo = {"/api/control/servo", HTTP_GET, FastServoHandler, nullptr};
    const httpd_uri_t drive = {"/api/control/drive", HTTP_POST, DriveHandler, nullptr};
    const httpd_uri_t fast_drive = {"/api/control/drive", HTTP_GET, FastDriveHandler, nullptr};
    const httpd_uri_t lights = {"/api/control/lights", HTTP_POST, LightsHandler, nullptr};
    httpd_register_uri_handler(server, &options);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &learning_recent);
    httpd_register_uri_handler(server, &servo);
    httpd_register_uri_handler(server, &fast_servo);
    httpd_register_uri_handler(server, &drive);
    httpd_register_uri_handler(server, &fast_drive);
    httpd_register_uri_handler(server, &lights);
    ESP_LOGI(kTag, "Robot control API listening on port 80");
        vTaskDelete(nullptr);
    }, "robot_control", 6144, nullptr, 2, nullptr);
}

void RecordLearningMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr || content[0] == '\0') return;
    if (strcmp(role, "user") != 0 && strcmp(role, "assistant") != 0) return;
    std::lock_guard<std::mutex> lock(learning_mutex);
    learning_events.push_back({++learning_event_id, role, content});
    while (learning_events.size() > 24) {
        learning_events.pop_front();
    }
}
