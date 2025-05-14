#include "iot/thing.h"
#include "board.h"
#include "audio_codec.h"
#include <driver/gpio.h>
#include <esp_log.h>

#define TAG "UserFPC"

namespace iot
{
    class UserFPC : public Thing {
        private:
            bool power_ = false;

            void InitalizeGpio(){
                gpio_config_t io_conf;
                io_conf.intr_type = GPIO_INTR_DISABLE;
                io_conf.mode = GPIO_MODE_OUTPUT;
                io_conf.pin_bit_mask = (1ULL << GPIO_NUM_39);
                io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
                io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
                gpio_config(&io_conf);
                gpio_set_level(GPIO_NUM_39, 0);
            }
        public:
            UserFPC() : Thing("UserFPC","香熏"),power_(false){
                InitalizeGpio();

                //定义设备属性
                properties_.AddBooleanProperty("power", "香熏是否打开", [this]() -> bool {
                    return power_;
                });

                //定义设备可以被远程执行的指令
                methods_.AddMethod("TurnOn","打开香熏",ParameterList(),[this](const ParameterList& parameters){
                    power_ = true;
                    gpio_set_level(GPIO_NUM_39, 1);
                });

                methods_.AddMethod("TurnOff","关闭香熏",ParameterList(),[this](const ParameterList& parameters){
                    power_ = false;
                    gpio_set_level(GPIO_NUM_39, 0);
                });
            }
    };
} // namespace iot

DECLARE_THING(UserFPC);
