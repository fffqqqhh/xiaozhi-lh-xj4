#include "user_wsrgb.h"
#include "application.h"
#include <driver/gpio.h>

#define TAG "UserWsrgb"

#define DEFAUT_BRIGHTNESS   5

#define BRIGHT_MAX          10000
#define BRIGHT_ATTE         1

UserWsrgb::UserWsrgb(gpio_num_t gpio, uint8_t maxLeds) : maxLeds_(maxLeds) {
    rgbColors_.resize(maxLeds_);
    hsvColors_.resize(maxLeds_);
    
    power_ = false;

    for(int i = 0; i < maxLeds_; i++){
        hsvColors_[i].h = 0;
        hsvColors_[i].s = 0;
        hsvColors_[i].v = 0;
        
        rgbColors_[i].r = 0;
        rgbColors_[i].g = 0;
        rgbColors_[i].b = 0;
    }
    brightness_ = DEFAUT_BRIGHTNESS;

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = maxLeds_;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10*1000*1000;
    
    led_strip_new_rmt_device(&strip_config, &rmt_config, &ledStrip_);
    led_strip_clear(ledStrip_);
    
    esp_timer_create_args_t strip_timer_args = {
       .callback = [](void *arg){
            auto strip = static_cast<UserWsrgb*>(arg);
            if(strip->stripCallback_ != nullptr){
                strip->stripCallback_();
            }
       },
       .arg = this,
       .dispatch_method = ESP_TIMER_TASK,
       .name = "strip_timer"
    };
    esp_timer_create(&strip_timer_args, &stripTimer_);
}

UserWsrgb::~UserWsrgb() {
    if(ledStrip_ != nullptr){
        led_strip_del(ledStrip_);
    }
    if(stripTimer_ != nullptr){
        esp_timer_delete(stripTimer_);
    }
}

/// @brief HSV转RGB算法，输入HSV值，输出RGB值
/// @param h 色度(0~359)
/// @param s 饱和度(0~255)
/// @param v 亮度(0~BRIGHT_MAX)
/// @param R 红色(0~255)
/// @param G 绿色(0~255)
/// @param B 蓝色(0~255)
void UserWsrgb::HsvToRgb(uint16_t h, uint16_t s, uint16_t v, uint8_t* R, uint8_t* G, uint8_t* B){
    float C = 0.0,X = 0.0,Y = 0.0,Z = 0.0;
    float temp_r = 0.0,temp_g = 0.0,temp_b = 0.0;
    int i = 0;
    float H,S,V;
    
    //屏蔽黑色的情况
    if(h > 359){
        h = 359;
    }

    H = (float)h;
    S = (float)s/255.0; //把s缩放到0～1之间
    V = (float)(v*BRIGHT_ATTE)/BRIGHT_MAX; //把v缩放到0～1之间

    if(S == 0){
        temp_r = V;
        temp_g = V;
        temp_b = V;
    }
    else{
        H = H/60;
        i = (int)H;
        C = H-i;
        
        X = V*(1-S);
        Y = V*(1-S*C);
        Z = V*(1-S*(1-C));

        switch(i){
            case 0:
                temp_r = V;
                temp_g = Z;
                temp_b = X;
                break;
            case 1:
                temp_r = Y;
                temp_g = V;
                temp_b = X;
                break;
            case 2:
                temp_r = X;
                temp_g = V;
                temp_b = Z;
                break;
            case 3:
                temp_r = X;
                temp_g = Y;
                temp_b = V;
                break;
            case 4:
                temp_r = Z;
                temp_g = X;
                temp_b = V;
                break;
            case 5:
                temp_r = V;
                temp_g = X;
                temp_b = Y;
                break;
            default:
                break;
        }
    }

    //如果需要转换到0-255,只需要把后面的乘1000改成255即可
    //如果需要转换到0-1000，只需要把后面的乘255改成1000即可
    *R = (uint8_t)(temp_r*255);
    *G = (uint8_t)(temp_g*255);
    *B = (uint8_t)(temp_b*255);
}

void UserWsrgb::RgbToHsv(uint8_t R, uint8_t G, uint8_t B, uint16_t* h, uint16_t* s, uint16_t* v){
    float r = (float)R/255.0f;
    float g = (float)G/255.0f;
    float b = (float)B/255.0f;
    float max = r > g ? (r > b ? r : b) : (g > b ? g : b); //max(R,G,B)
    float min = r < g ? (r < b ? r : b) : (g < b ? g : b); //min(R,G,B)
    float delta = max - min; //delta = max - min
    float H = 0.0f,S = 0.0f,V = max;

    if(delta != 0){
        if(max == r){
            H = 60*((g - b)/delta);
        }
        else if(max == g){
            H = 60*(((b - r)/delta) + 2.0f);
        }
        else if(max == b){
            H = 60*(((r - g)/delta) + 4.0f);
        }

        if(H < 0){
            H += 359.0f;
        }

        S = max!=0 ? (delta/max) : 0;
    }

    //将H(0-359),S(0-1),V(0-1) 缩放到 (0-359),(0-255),(0,brightness_max)
    *h = (uint16_t)H;
    *s = (uint16_t)(S*255);
    *v = (uint16_t)(V*BRIGHT_MAX);
}

bool UserWsrgb::GetLedPowerState() const{
    return power_;
}

void UserWsrgb::SetLedPowerState(bool power){
    if(power_ == power){
        return;
    }
    power_ = power;
}

uint8_t UserWsrgb::GetBrightness() const{
    return brightness_;
}

void UserWsrgb::SetBrightness(uint8_t brightness){
    if(brightness_ == brightness){
        return;
    }
    brightness_ = brightness;

    if(true == power_){
        for(int i = 0; i < maxLeds_; i++){
            hsvColors_[i].v = brightness_*1000;

            HsvToRgb(hsvColors_[i].h, hsvColors_[i].s, hsvColors_[i].v, &rgbColors_[i].r, &rgbColors_[i].g, &rgbColors_[i].b);
            led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
        }
        led_strip_refresh(ledStrip_);
    }
    else{
        power_ = true;
        //这里判断上一次灯光颜色，但是只判断1颗灯，这样可能会有问题
        //如果上一次灯光没有颜色，则默认给个桔黄色
        if((rgbColors_[0].r==0) && (rgbColors_[0].g==0) && (rgbColors_[0].b==0)){
            for(int i = 0; i < maxLeds_; i++){
                rgbColors_[i].r = 255;
                rgbColors_[i].g = 165;
                rgbColors_[i].b = 0;
                RgbToHsv(rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b, &hsvColors_[i].h, &hsvColors_[i].s, &hsvColors_[i].v);

                hsvColors_[i].v = brightness_*1000;
                HsvToRgb(hsvColors_[i].h, hsvColors_[i].s, hsvColors_[i].v, &rgbColors_[i].r, &rgbColors_[i].g, &rgbColors_[i].b);

                led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
            }
        }
        else{
            for(int i = 0; i < maxLeds_; i++){
                hsvColors_[i].v = brightness_*1000;
                HsvToRgb(hsvColors_[i].h, hsvColors_[i].s, hsvColors_[i].v, &rgbColors_[i].r, &rgbColors_[i].g, &rgbColors_[i].b);
                led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
            }
        }
        led_strip_refresh(ledStrip_);
    }
}

void UserWsrgb::TurnOn(uint8_t brightness){
    brightness_ = brightness;
    power_ = true;

    if((rgbColors_[1].r==0) && (rgbColors_[1].g==0) && (rgbColors_[1].b==0)){
        for(int i = 0; i < maxLeds_; i++){
            rgbColors_[i].r = 255;
            rgbColors_[i].g = 165;
            rgbColors_[i].b = 0;
            RgbToHsv(rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b, &hsvColors_[i].h, &hsvColors_[i].s, &hsvColors_[i].v);

            hsvColors_[i].v = brightness_*1000;
            HsvToRgb(hsvColors_[i].h, hsvColors_[i].s, hsvColors_[i].v, &rgbColors_[i].r, &rgbColors_[i].g, &rgbColors_[i].b);

            led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
        }
    }
    else{
        for(int i = 0; i < maxLeds_; i++){
            hsvColors_[i].v = brightness_*1000;
            HsvToRgb(hsvColors_[i].h, hsvColors_[i].s, hsvColors_[i].v, &rgbColors_[i].r, &rgbColors_[i].g, &rgbColors_[i].b);
            led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
        }
    }

    led_strip_refresh(ledStrip_);
}

void UserWsrgb::TurnOff(){
    power_ = false;
    led_strip_clear(ledStrip_);
    if(stripTimer_ != nullptr)
        esp_timer_stop(stripTimer_);
}

void UserWsrgb::SetSingleColor(uint8_t index,RGBColor color){
    if(stripTimer_ != nullptr)
        esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        RgbToHsv(color.r, color.g, color.b, &hsvColors_[i].h, &hsvColors_[i].s, &hsvColors_[i].v);
        led_strip_set_pixel(ledStrip_, i, color.r, color.g, color.b);
        led_strip_refresh(ledStrip_);
    }
}

void UserWsrgb::SetAllColor(RGBColor color){
    if(stripTimer_ != nullptr)
    esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        rgbColors_[i] = color;
        RgbToHsv(color.r, color.g, color.b, &hsvColors_[i].h, &hsvColors_[i].s, &hsvColors_[i].v);
        led_strip_set_pixel(ledStrip_, i, color.r, color.g, color.b);
    }
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::SetAlwaysMode(){
    if(stripTimer_ != nullptr)
        esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        RgbToHsv(rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b, &hsvColors_[i].h, &hsvColors_[i].s, &hsvColors_[i].v);
        led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
    }
    led_strip_refresh(ledStrip_);
}

void UserWsrgb::SetBlinkMode(RGBColor color, int intervalMs){
    if(stripTimer_ != nullptr)
        esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        RgbToHsv(color.r, color.g, color.b, &hsvColors_[i].h, &hsvColors_[i].s, &hsvColors_[i].v);
    }
    StartStripTimerTask(intervalMs,[this](){
        static bool isOn = true;
        if(isOn){
            for(int i = 0; i < maxLeds_; i++){
                led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
            }
            led_strip_refresh(ledStrip_);
        }
        else{
            led_strip_clear(ledStrip_);
        }
        isOn = !isOn;
    });
}

void UserWsrgb::SetBreatheMode(RGBColor color, int intervalMs){
    if(stripTimer_ != nullptr)
        esp_timer_stop(stripTimer_);
    for(int i = 0; i < maxLeds_; i++){
        rgbColors_[i] = color;
        RgbToHsv(color.r, color.g, color.b, &hsvColors_[i].h, &hsvColors_[i].s, &hsvColors_[i].v);
    }

    StartStripTimerTask(intervalMs,[this](){
        static bool increase = true;
        static uint16_t hsvVValue = 0;

        if(increase){
            if(hsvVValue < BRIGHT_MAX){
                hsvVValue += 1000;
            }

            if(hsvVValue >= BRIGHT_MAX){
                hsvVValue = BRIGHT_MAX;
                increase = false;
            }
        }
        else{
            if(hsvVValue > 2000){
                hsvVValue -= 1000;
            }

            if(hsvVValue <= 2000){
                hsvVValue = 2000;
                increase = true;
            }
        }

        for(int i = 0; i < maxLeds_; i++){
            hsvColors_[i].v = hsvVValue;
            HsvToRgb(hsvColors_[i].h, hsvColors_[i].s, hsvColors_[i].v, &rgbColors_[i].r, &rgbColors_[i].g, &rgbColors_[i].b);
            led_strip_set_pixel(ledStrip_, i, rgbColors_[i].r, rgbColors_[i].g, rgbColors_[i].b);
        }
        led_strip_refresh(ledStrip_);
    });
}

void UserWsrgb::StartStripTimerTask(int intervalMs, std::function<void()> callback){
    if(ledStrip_ == nullptr){
        return ;
    }
    if(stripTimer_ == nullptr){
        esp_timer_stop(stripTimer_);
    }
    stripCallback_ = callback;
    esp_timer_start_once(stripTimer_, intervalMs*1000);
}

void UserWsrgb::OnStateChanged(){
    static bool _firstTips = false;
    auto &app = Application::GetInstance();
    auto deviceState = app.GetDeviceState();
    if(deviceState == kDeviceStateIdle){
        if(false == _firstTips){
            _firstTips = true;

            power_ = true;

            RGBColor breathColor;
            breathColor.r = 0;
            breathColor.g = 0;
            breathColor.b = 255;

            SetBreatheMode(breathColor,200);
        }
    }
}