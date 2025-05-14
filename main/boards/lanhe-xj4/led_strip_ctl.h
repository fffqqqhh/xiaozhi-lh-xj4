#ifndef LED_STRIP_CTL_H
#define LED_STRIP_CTL_H

#include "iot/thing.h"
#include "led/user_wsrgb.h"

using namespace iot;

class LedStripCtl : public Thing {
public:
    explicit LedStripCtl(UserWsrgb* ledStrip);
private:
    UserWsrgb* ledStrip_;
    bool ledState;
    uint8_t brightnessLevel;

    RGBColor RgbToColor(uint8_t r, uint8_t g, uint8_t b); //将RGB值转换为StripColor结构体
};

#endif // LED_STRIP_CTL_H
