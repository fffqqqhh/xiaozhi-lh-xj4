#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>

#include <driver/pulse_cnt.h>
#include "hal/pcnt_types.h"

#include "led/user_wsrgb.h"
#include "led_strip_ctl.h"

#define TAG "LanheXJ4Board"

UserWsrgb* ledStrip_;
// UserWsrgb* ledStrip_ = new UserWsrgb(GPIO_NUM_40, 12);
// auto ledStripCtl_ = new LedStripCtl(ledStrip_);

pcnt_unit_handle_t pcnt_unit = NULL;
QueueHandle_t queue = xQueueCreate(10, sizeof(int));

static bool PcntOnReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *value, void* ctx){
    BaseType_t high_task_wakeup;
    QueueHandle_t queue = (QueueHandle_t)ctx;

    //send event data to queue , from this interrupt callback
    xQueueSendFromISR(queue, &(value->watch_point_value), &high_task_wakeup);
    return (high_task_wakeup == pdTRUE);
}

static void encoder_task(void *arg){
// QueueHandle_t queue = (QueueHandle_t)arg;
    int pulse_count = 0;
    int event_count = 0;

    static int last_count = 0;

    static int ledBrightnessTemp = 0;
    static int volumeTemp = 0;
    // ledBrightnessTemp = led_strip_->GetBrightness();

    while (1) {
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(100))) {
            ESP_LOGI(TAG,"Watch point event,count:%d",event_count);
        }
        else{

            ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
            ledBrightnessTemp = ledStrip_->GetBrightness()*1000;
            auto codec = Board::GetInstance().GetAudioCodec();
            volumeTemp = codec->output_volume();
            // ESP_LOGI(TAG,"ledBrightnessTemp:%d,volumeTemp:%d",ledBrightnessTemp,volumeTemp);
            if(pulse_count > last_count){

                // ESP_LOGI(TAG,"currenr brighness:%d",ledBrightnessTemp);
                if(ledStrip_->GetLedPowerState()){
                    ledBrightnessTemp += 1000;
                    if(ledBrightnessTemp > 10000){
                        ledBrightnessTemp = 10000;
                    }
                    ledStrip_->SetBrightness(ledBrightnessTemp/1000);
                }
                else{
                    
                    // ESP_LOGI(TAG,"need turn on led power.currenr brighness:%d",ledBrightnessTemp);
                }

                auto device_state = Application::GetInstance().GetDeviceState();
                if((device_state == kDeviceStateSpeaking) || (device_state == kDeviceStateListening)){
                    volumeTemp += 4;
                    if(volumeTemp > 90){
                        volumeTemp = 90;
                    }
                    codec->SetOutputVolume(volumeTemp);
                }
                else{
                    ESP_LOGI(TAG,"非对话模式");
                }
            }
            else if(pulse_count < last_count){
                // ESP_LOGI(TAG,"direction:%d",direction);
                
                // ESP_LOGI(TAG,"currenr brighness:%d",ledBrightnessTemp);
                if(ledStrip_->GetLedPowerState()){
                    ledBrightnessTemp -= 1000;
                    if(ledBrightnessTemp < 1000){
                        ledBrightnessTemp = 1000;
                    }
                    ledStrip_->SetBrightness(ledBrightnessTemp/1000);
                }
                else{
                    
                    // ESP_LOGI(TAG,"need turn on led power.currenr brighness:%d",ledBrightnessTemp);
                }

                auto device_state = Application::GetInstance().GetDeviceState();
                if((device_state == kDeviceStateSpeaking) || (device_state == kDeviceStateListening)){
                    volumeTemp -= 4;
                    if(volumeTemp < 50){
                        volumeTemp = 50;
                    }
                    codec->SetOutputVolume(volumeTemp);
                }
                else{
                    ESP_LOGI(TAG,"非对话模式");
                }

            }
            last_count = pulse_count;

            // ESP_LOGI(TAG,"Pusle count:%d",pulse_count);
            
        }
    }
}

class LanheXJ4Board : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button user_button_;

    void InitializeI2c(){
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeButtons() {
        user_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if((app.GetDeviceState() == kDeviceStateStarting) && (!WifiStation::GetInstance().IsConnected())) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }
#if 1
    void InitializeEncoder(){
        pcnt_unit_config_t unit_config = {
            .low_limit = -100,
            .high_limit = 100, //后续要更改成hsv中v的范围
        };
        // pcnt_unit_handle_t pcnt_unit = NULL;
        ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

        pcnt_glitch_filter_config_t filter_config = {
            .max_glitch_ns = 1000,
        };
        ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

        pcnt_chan_config_t chan_a_config = {
            .edge_gpio_num = GPIO_NUM_16,
            .level_gpio_num = GPIO_NUM_15,
        };
        pcnt_channel_handle_t pcnt_chan_a = NULL;
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

        pcnt_chan_config_t chan_b_config = {
            .edge_gpio_num = GPIO_NUM_15,
            .level_gpio_num = GPIO_NUM_16,
        };
        pcnt_channel_handle_t pcnt_chan_b = NULL;
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));


        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a,PCNT_CHANNEL_EDGE_ACTION_DECREASE,PCNT_CHANNEL_EDGE_ACTION_INCREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a,PCNT_CHANNEL_LEVEL_ACTION_KEEP,PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b,PCNT_CHANNEL_EDGE_ACTION_INCREASE,PCNT_CHANNEL_EDGE_ACTION_DECREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b,PCNT_CHANNEL_LEVEL_ACTION_KEEP,PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

        pcnt_event_callbacks_t cbs = {
            .on_reach = PcntOnReach,
        };
        // QueueHandle_t queue = xQueueCreate(10, sizeof(int));
        ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue));

        ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
        ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

        xTaskCreate(encoder_task, "encoder_task", 4096, (void*)queue, 10, NULL);
    }
#endif
    //iot设备注册
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        
        ledStrip_ = new UserWsrgb(GPIO_NUM_40, 12);
        auto ledStripCtl_ = new LedStripCtl(ledStrip_);
        thing_manager.AddThing(ledStripCtl_);
    }

public:
    LanheXJ4Board():user_button_(BOOT_BUTTON_GPIO){
        InitializeI2c();
        InitializeEncoder();
        InitializeButtons();
        InitializeIot();
    }

    virtual AudioCodec *GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            codec_i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Led* GetLed() override{
        return ledStrip_;
    }
};

DECLARE_BOARD(LanheXJ4Board);