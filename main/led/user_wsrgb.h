#ifndef _USER_WSRGB_H_
#define _USER_WSRGB_H_

#include "led.h"
#include <driver/gpio.h>
#include <led_strip.h>
#include <esp_timer.h>
#include <vector>
#include <functional>

struct RGBColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct HSVColor {
    uint16_t h;
    uint16_t s;
    uint16_t v;
};

class UserWsrgb : public Led {
public:
    // UserWsrgb(gpio_num_t pin, uint16_t num_leds, uint8_t brightness = 5, uint8_t max_brightness = 10);
    UserWsrgb(gpio_num_t pin, uint8_t num_leds);
    ~UserWsrgb();

    void OnStateChanged() override;

    bool GetLedPowerState() const;
    void SetLedPowerState(bool state);

    uint8_t GetBrightness() const;
    void SetBrightness(uint8_t brightness);

    void SetSingleColor(uint8_t index,RGBColor color);
    void SetAllColor(RGBColor color);
    
    void SetBlinkMode(RGBColor color,int intervalMs);
    void SetAlwaysMode();
    void SetRandomMode(int intervalMs);
    void SetBreatheMode(RGBColor color,int intervalMs);
    
    void TurnOn(uint8_t brightness);
    void TurnOff();

    void HsvToRgb(uint16_t h, uint16_t s, uint16_t v, uint8_t* R, uint8_t* G, uint8_t* B);
    void RgbToHsv(uint8_t R, uint8_t G, uint8_t B, uint16_t* h, uint16_t* s, uint16_t* v);

private:
    led_strip_handle_t ledStrip_;
    esp_timer_handle_t stripTimer_;
    std::function<void()> stripCallback_ = nullptr;

    bool power_;
    uint8_t maxLeds_;
    uint8_t brightness_;

    std::vector<RGBColor> rgbColors_;
    std::vector<HSVColor> hsvColors_;

    void StartStripTimerTask(int intervalMs,std::function<void()> callback);
};

#endif // _USER_WSRGB_H_
