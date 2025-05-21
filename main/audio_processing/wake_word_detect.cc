#include "wake_word_detect.h"
#include "application.h"

#include <esp_log.h>
#include <model_path.h>
#include <arpa/inet.h>
#include <sstream>

#include "wifi_station.h"
#include "assets/lang_config.h"
#include "esp_mn_models.h"
#include "audio_codec.h"
#include <driver/gpio.h>
#include "iot/thing_manager.h"
#include "settings.h"
#include "led/user_wsrgb.h"

#define DETECTION_RUNNING_EVENT 1

static const char* TAG = "WakeWordDetect";

extern UserWsrgb *ledStrip_;
static int offline_wakeup_flag = 0;

#if 1 //离线语音
typedef struct {
    wakenet_state_t wakeup_state;
    esp_mn_state_t mn_state;
    int command_id;
}sr_result_t;

const char *cmd_phoneme[] = {
    "da kai fen wei deng",
    "da kai deng",
    "kai deng",

    "guan bi fen wei deng",
    "guan fen wei deng",
    "guan deng",

    "da kai xiang xun",
    "kai xiang xun",

    "guan bi xiang xun",
    "guan xiang xun",

    "tiao gao yin liang",
    "tiao di yin liang",
    "da sheng yi dian",
    "xiao sheng yi dian",
    "yin liang tiao dao zui da",
    "yin liang tiao dao zui xiao",
    "zui da sheng",
    "zui xiao sheng",
};
#endif

WakeWordDetect::WakeWordDetect()
    : afe_data_(nullptr),
      wake_word_pcm_(),
      wake_word_opus_() {

    event_group_ = xEventGroupCreate();
}

WakeWordDetect::~WakeWordDetect() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    vEventGroupDelete(event_group_);
}

void WakeWordDetect::Initialize(AudioCodec* codec) {
    codec_ = codec;
    int ref_num = codec_->input_reference() ? 1 : 0;
    ESP_LOGI(TAG,"Initialize wake word detect, input reference: %d", ref_num);

    srmodel_list_t *models = esp_srmodel_init("model");
    for (int i = 0; i < models->num; i++) {
        ESP_LOGI(TAG, "Model %d: %s", i, models->model_name[i]);
        if (strstr(models->model_name[i], ESP_WN_PREFIX) != NULL) {
            wakenet_model_ = models->model_name[i];
            auto words = esp_srmodel_get_wake_words(models, wakenet_model_);
            // split by ";" to get all wake words
            std::stringstream ss(words);
            std::string word;
            while (std::getline(ss, word, ';')) {
                wake_words_.push_back(word);
            }
        }
    }

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }
    ESP_LOGI(TAG, "Input format: %s", input_format.c_str());

    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    afe_config->aec_init = codec_->input_reference();
    afe_config->aec_mode = AEC_MODE_SR_HIGH_PERF;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    ESP_LOGI(TAG,"load wakenet :%s",afe_config->wakenet_model_name);

    char* mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, NULL);
    multinet_ = esp_mn_handle_from_name(mn_name);
    model_data_ = multinet_->create(mn_name,6000);
    ESP_LOGI(TAG,"load multinet :%s",mn_name);

    esp_mn_commands_clear();
    for(int i=0;i<sizeof(cmd_phoneme)/sizeof(cmd_phoneme[0]);i++){
        esp_mn_commands_add(i,(char*)cmd_phoneme[i]);
    }
    esp_mn_commands_update();
    esp_mn_commands_print();
    multinet_->print_active_speech_commands(model_data_);

    result_queue_ = xQueueCreate(1, sizeof(sr_result_t));

    xTaskCreate(SrHandlerTask,"sr_handler_task",1024*4,result_queue_,2,NULL);

    xTaskCreate([](void* arg) {
        auto this_ = (WakeWordDetect*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 4096, this, 3, nullptr);
}

void WakeWordDetect::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void WakeWordDetect::StartDetection() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void WakeWordDetect::StopDetection() {
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
}

bool WakeWordDetect::IsDetectionRunning() {
    return xEventGroupGetBits(event_group_) & DETECTION_RUNNING_EVENT;
}

void WakeWordDetect::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr) {
        return;
    }
    afe_iface_->feed(afe_data_, data.data());
}

size_t WakeWordDetect::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
}

void WakeWordDetect::SrHandlerTask(void* pvParam) {
    QueueHandle_t resQueue = (QueueHandle_t)pvParam;

    while(true){
        sr_result_t result;
        xQueueReceive(resQueue, &result, portMAX_DELAY);
        
        ESP_LOGI(TAG, "wakeup_state: %d, mn_state: %d, command_id: %d",result.wakeup_state, result.mn_state, result.command_id);

        if(result.mn_state == ESP_MN_STATE_TIMEOUT){
            ESP_LOGI(TAG,"Sr result timeout");
            continue;
        }

        if(result.wakeup_state == WAKENET_DETECTED){
            ESP_LOGI(TAG,"Sr result wakeup detected");
            continue;
        }

        if(result.mn_state == ESP_MN_STATE_DETECTED){
            switch(result.command_id){
                //开氛围灯
                case 0:
                case 1:
                case 2:
                {
                    ESP_LOGI(TAG, "SR result command turnon the light");
                    if(ledStrip_->GetLedPowerState()){
                        ESP_LOGI(TAG, "Led already on");
                    }
                    else{
                        Settings settings("led_strip");
                        int brightness = settings.GetInt("brightness", 4);
                        ledStrip_->TurnOn((uint8_t)brightness);
                    }
                }break;

                //关氛围灯
                case 3:
                case 4:
                case 5:
                {
                    ESP_LOGI(TAG, "SR result command turnoff the light");
                    if(ledStrip_->GetLedPowerState()){
                        ledStrip_->TurnOff();
                    }
                    else{
                        ESP_LOGI(TAG, "Led already off");
                    }
                }break;

                //打开香熏
                case 6:
                case 7:
                {
                    ESP_LOGI(TAG, "SR result command turnon the aroma");

                    gpio_set_level(GPIO_NUM_39, 1);
                }break;

                //关闭香熏
                case 8:
                case 9:
                {
                    ESP_LOGI(TAG, "SR result command turnoff the aroma");
                    gpio_set_level(GPIO_NUM_39, 0);
                }break;

                //调高音量
                case 10:
                case 12:
                {
                    ESP_LOGI(TAG, "SR result command increase the volume");
                    auto codec = Board::GetInstance().GetAudioCodec();
                    int currVolume = codec->output_volume();
                    if(currVolume < 90){
                        currVolume += 10;
                        if(currVolume >= 90){
                            currVolume = 90;
                        }
                        codec->SetOutputVolume(currVolume);
                    }
                }break;

                //调低音量
                case 11:
                case 13:
                {
                    ESP_LOGI(TAG, "SR result command decrease the volume");
                    auto codec = Board::GetInstance().GetAudioCodec();
                    int currVolume = codec->output_volume();
                    if(currVolume > 50){
                        currVolume -= 10;
                        if(currVolume <= 50){
                            currVolume = 50;
                        }
                        codec->SetOutputVolume(currVolume);
                    }
                }break;

                //最大音量
                case 14:
                case 16:
                {
                    ESP_LOGI(TAG, "SR result command max the volume");
                    auto codec = Board::GetInstance().GetAudioCodec();
                    codec->SetOutputVolume(90);
                }break;

                //最小音量
                case 15:
                case 17:
                {
                    ESP_LOGI(TAG, "SR result command min the volume");
                    auto codec = Board::GetInstance().GetAudioCodec();
                    codec->SetOutputVolume(50);
                }break;

                default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

void WakeWordDetect::AudioDetectionTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio detection task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;;
        }

        // Store the wake word data for voice recognition, like who is speaking
        StoreWakeWordData((uint16_t*)res->data, res->data_size / sizeof(uint16_t));

        if (res->wakeup_state == WAKENET_DETECTED) {
            if(WifiStation::GetInstance().IsConnected()){
                StopDetection();
                last_detected_wake_word_ = wake_words_[res->wake_word_index - 1];

                if(wake_word_detected_callback_){
                    wake_word_detected_callback_(last_detected_wake_word_);
                }
            }
            else{
                multinet_->clean(model_data_);
            }
        }

        if((res->raw_data_channels==1) && (res->wakeup_state==WAKENET_DETECTED)){
            ESP_LOGI(TAG,"wakenet detected");
            
            if(WifiStation::GetInstance().IsConnected()){
                offline_wakeup_flag = 0;
            }
            else{
                auto &application = Application::GetInstance();
                application.Alert(Lang::Strings::LISTENING, Lang::Strings::LISTENING, "", Lang::Sounds::P3_1);

                sr_result_t _result = {
                    .wakeup_state = WAKENET_DETECTED,
                    .mn_state = ESP_MN_STATE_DETECTING,
                    .command_id = 0,
                };
                xQueueSend(result_queue_, &_result, 10);
                offline_wakeup_flag = 1;
            }
        }
        else if((res->raw_data_channels>1) && (res->wakeup_state==WAKENET_CHANNEL_VERIFIED)){
            ESP_LOGI(TAG,"wakenet channel verified");
            offline_wakeup_flag = 1;
            afe_iface_->disable_wakenet(afe_data_); //TODO: 上面的判断逻辑里面是否也需要加这行代码
        }

        if(offline_wakeup_flag == 1){
            esp_mn_state_t mn_state = multinet_->detect(model_data_, res->data);

            if(mn_state == ESP_MN_STATE_DETECTING){
                continue;
            }

            if(mn_state == ESP_MN_STATE_DETECTED){
                esp_mn_results_t *mn_res = multinet_->get_results(model_data_);
                
                int sr_command_id = mn_res->command_id[0];
                sr_result_t result = {
                   .wakeup_state = WAKENET_NO_DETECT,
                   .mn_state = mn_state,
                   .command_id = sr_command_id,
                };
                xQueueSend(result_queue_, &result, 10);
                ESP_LOGI(TAG,"Detect command id:%d",sr_command_id);
            }

            if(mn_state == ESP_MN_STATE_TIMEOUT){
                ESP_LOGI(TAG,"Multinet result timeout");
                esp_mn_results_t *mn_res = multinet_->get_results(model_data_);
                sr_result_t result = {
                    .wakeup_state = WAKENET_NO_DETECT,
                    .mn_state = mn_state,
                    .command_id = 0,
                };
                xQueueSend(result_queue_, &result, 10);
                offline_wakeup_flag = 0;
                afe_iface_->enable_wakenet(afe_data_);
                continue;
            }
        }
    }
}

void WakeWordDetect::StoreWakeWordData(uint16_t* data, size_t samples) {
    // store audio data to wake_word_pcm_
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    // keep about 2 seconds of data, detect duration is 32ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 32) {
        wake_word_pcm_.pop_front();
    }
}

void WakeWordDetect::EncodeWakeWordData() {
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(4096 * 8, MALLOC_CAP_SPIRAM);
    }
    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (WakeWordDetect*)arg;
        {
            auto start_time = esp_timer_get_time();
            auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
            encoder->SetComplexity(0); // 0 is the fastest

            for (auto& pcm: this_->wake_word_pcm_) {
                encoder->Encode(std::move(pcm), [this_](std::vector<uint8_t>&& opus) {
                    std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                    this_->wake_word_opus_.emplace_back(std::move(opus));
                    this_->wake_word_cv_.notify_all();
                });
            }
            this_->wake_word_pcm_.clear();

            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %zu packets in %lld ms",
                this_->wake_word_opus_.size(), (end_time - start_time) / 1000);

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_detect_packets", 4096 * 8, this, 2, wake_word_encode_task_stack_, &wake_word_encode_task_buffer_);
}

bool WakeWordDetect::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
