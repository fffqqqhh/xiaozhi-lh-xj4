#include "led_strip_ctl.h"
#include "settings.h"
#include <esp_log.h>

#define TAG "LED_STRIP_CTL"

RGBColor LedStripCtl::RgbToColor(uint8_t r, uint8_t g, uint8_t b){
    if(r <= 0)
        r = 0;
    if(g <= 0)
        g = 0;
    if(b <= 0)
        b = 0;
    if(r >= 255)
        r = 255;
    if(g >= 255)
        g = 255;
    if(b >= 255)
        b = 255;

    return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
}

LedStripCtl::LedStripCtl(UserWsrgb* ledStrip)
    : Thing("ledStripCtl","氛围灯带控制,一共有12颗灯"),ledStrip_(ledStrip){

        Settings settings("led_strip");
        brightnessLevel = settings.GetInt("brightness",5);

        ledState = false;

        //灯开关状态和亮度状态是在这个文件处理还是到下一层去处理??
        properties_.AddNumberProperty("brightness","氛围灯的亮度等级(2-10)",[this]()->int {
            return brightnessLevel;
        });

        properties_.AddBooleanProperty("ledState","氛围灯的开关状态",[this]()->bool {
            return ledState;
        });

        methods_.AddMethod("SetBrightness","设置氛围灯亮度等级(2-10)",ParameterList({
            Parameter("brightness","亮度等级(2-10)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters) {
            uint8_t brightness = static_cast<uint8_t>(parameters["brightness"].number());
            ESP_LOGI(TAG,"Set LedStrip Brightness to %d",brightness);

            if(brightness < 2)
                brightness = 2;
            if(brightness > 10)
                brightness = 10;

            brightnessLevel = brightness;
            ledStrip_->SetBrightness(brightnessLevel);

            //保存设置
            Settings settings("led_strip",true);
            settings.SetInt("brightness",brightnessLevel);
        });

        methods_.AddMethod("TurnOn","打开氛围灯",ParameterList({
            Parameter("brightness","亮度等级(2-10)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters) {
            uint8_t brightness = static_cast<uint8_t>(parameters["brightness"].number());
            ledState = true;
            ledStrip_->TurnOn(brightness);
            ESP_LOGI(TAG,"Turn On LedStrip,the brightness is %d",brightness);
        });

        methods_.AddMethod("TurnOff","关闭氛围灯",ParameterList(),[this](const ParameterList& parameters) {
            ledState = false;
            ledStrip_->TurnOff();
            ESP_LOGI(TAG,"Turn Off LedStrip");
        });

        methods_.AddMethod("SetSingleColor","设置单个灯的颜色",ParameterList({
            Parameter("index","灯的索引(0-11)",kValueTypeNumber,true),
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters) {
            uint8_t index = static_cast<uint8_t>(parameters["index"].number());
            RGBColor color = RgbToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ledStrip_->SetSingleColor(index,color);
        });

        methods_.AddMethod("SetAllColor","设置所有灯的颜色",ParameterList({
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters){
            RGBColor color = RgbToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ledStrip_->SetAllColor(color);
        });

        methods_.AddMethod("SetBlink","设置成闪烁模式", ParameterList({
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true),
            Parameter("interval","间隔时间(ms)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters){
            int interval = parameters["interval"].number();
            RGBColor color = RgbToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ledStrip_->SetBlinkMode(color, interval);
        });

        methods_.AddMethod("SetBreathe","设置成呼吸模式", ParameterList({
            Parameter("red","红色(0-255)",kValueTypeNumber,true),
            Parameter("green","绿色(0-255)",kValueTypeNumber,true),
            Parameter("blue","蓝色(0-255)",kValueTypeNumber,true),
            Parameter("interval","间隔时间(ms)",kValueTypeNumber,true)
        }),[this](const ParameterList& parameters){
            int interval = parameters["interval"].number();
            RGBColor color = RgbToColor(
                parameters["red"].number(),
                parameters["green"].number(),
                parameters["blue"].number()
            );
            ledStrip_->SetBreatheMode(color, interval);
        });

        methods_.AddMethod("SetAlways","设置成常亮模式", ParameterList(),[this](const ParameterList& parameters){
            ledStrip_->SetAlwaysMode();
        });

        methods_.AddMethod("Random","随机效果",ParameterList({
            Parameter("interval","间隔时间(ms)",kValueTypeNumber,false)
        }),[this](const ParameterList& parameters){
            int interval = parameters["interval"].number();
            ledStrip_->SetRandomMode(interval);
        });
    }