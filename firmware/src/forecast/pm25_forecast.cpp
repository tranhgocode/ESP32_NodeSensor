#include "forecast/pm25_forecast.h"

#include <cinttypes>
#include <cmath>

#include "esp_log.h"
#include "esp_timer.h"
#include "forecast/model/scalers.h"
#include "forecast/pm25_history.h"
#include "protocol/node_payload.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

extern "C" const uint8_t pm25_model_start[];

namespace
{
constexpr const char *TAG = "pm25_forecast";
constexpr int64_t US_PER_MS = 1000;
constexpr float SENSOR_FIXED_POINT_SCALE = 10.0f;
constexpr size_t MODEL_OPERATOR_COUNT = 9;
/* Chừa bộ nhớ lập kế hoạch tensor và vùng nhớ cố định của các toán tử ESP-NN. */
constexpr size_t ARENA_BYTES = 160U * 1024U;
constexpr uint16_t MAX_WIRE_PM25 = NODE_PM25_UNAVAILABLE - 1;

enum class LogEvent
{
    none,
    inference_failed,
    ready,
};

/* Vùng nhớ suy luận, lịch sử và nhật ký chờ in thuộc tác vụ ứng dụng. */
alignas(16) uint8_t arena[ARENA_BYTES];
pm25_history_t history;
tflite::MicroInterpreter *interpreter;
float *input_data;
const float *output_data;
LogEvent log_event;
uint16_t logged_forecast[NODE_PM25_FORECAST_COUNT];
int64_t elapsed_us;

/** Đăng ký các toán tử của mô hình. Trả false nếu có toán tử đăng ký lỗi. */
bool register_model_ops(tflite::MicroMutableOpResolver<MODEL_OPERATOR_COUNT> &resolver)
{
    return resolver.AddReshape() == kTfLiteOk &&
           resolver.AddUnpack() == kTfLiteOk &&
           resolver.AddFullyConnected() == kTfLiteOk &&
           resolver.AddSplit() == kTfLiteOk &&
           resolver.AddLogistic() == kTfLiteOk &&
           resolver.AddTanh() == kTfLiteOk &&
           resolver.AddMul() == kTfLiteOk &&
           resolver.AddAdd() == kTfLiteOk &&
           resolver.AddPack() == kTfLiteOk;
}

/** Kiểm tra vùng dữ liệu, kiểu float32 và kích thước của tensor. */
bool float_tensor_matches(const TfLiteTensor *tensor, const int *shape, int rank)
{
    if (tensor == nullptr || tensor->type != kTfLiteFloat32 ||
        tensor->data.f == nullptr || tensor->dims == nullptr || tensor->dims->size != rank)
    {
        return false;
    }
    for (int dimension = 0; dimension < rank; ++dimension)
    {
        if (tensor->dims->data[dimension] != shape[dimension])
        {
            return false;
        }
    }
    return true;
}

/**
 * Đặt lại trạng thái mô hình và suy luận từ cửa sổ 24 × 3.
 * Loại cả kết quả nếu có giá trị không hữu hạn. Đổi về µg/m³, giới hạn
 * trong 0..65534 để chừa mã thiếu dữ liệu, rồi làm tròn.
 */
bool predict(const pm25_window_t &window, uint16_t out[NODE_PM25_FORECAST_COUNT])
{
    if (interpreter->Reset() != kTfLiteOk)
    {
        return false;
    }

    const int64_t start_us = esp_timer_get_time();
    for (size_t row = 0; row < PM25_HISTORY_ROWS; ++row)
    {
        for (size_t feature = 0; feature < PM25_HISTORY_FEATURES; ++feature)
        {
            input_data[row * PM25_HISTORY_FEATURES + feature] =
                (window.values[row][feature] - pm25_input_mean[feature]) / pm25_input_scale[feature];
        }
    }
    if (interpreter->Invoke() != kTfLiteOk)
    {
        return false;
    }

    for (size_t step = 0; step < NODE_PM25_FORECAST_COUNT; ++step)
    {
        const float value = output_data[step] * pm25_output_scale + pm25_output_mean;
        if (!std::isfinite(value))
        {
            return false;
        }
        const float bounded = std::fmin(std::fmax(value, 0.0f), static_cast<float>(MAX_WIRE_PM25));
        logged_forecast[step] = static_cast<uint16_t>(std::round(bounded));
    }
    elapsed_us = esp_timer_get_time() - start_us;
    for (size_t step = 0; step < NODE_PM25_FORECAST_COUNT; ++step)
    {
        out[step] = logged_forecast[step];
    }
    return true;
}
}

void pm25_forecast_init(void)
{
    const tflite::Model *model = tflite::GetModel(pm25_model_start);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        ESP_LOGE(TAG, "Unexpected model schema version");
        return;
    }

    static tflite::MicroMutableOpResolver<MODEL_OPERATOR_COUNT> resolver;
    if (!register_model_ops(resolver))
    {
        ESP_LOGE(TAG, "Model operator registration failed");
        return;
    }

    static tflite::MicroInterpreter runtime(model, resolver, arena, sizeof(arena));
    if (runtime.AllocateTensors() != kTfLiteOk)
    {
        ESP_LOGE(TAG, "Model tensor allocation failed, arena=%u bytes", (unsigned)sizeof(arena));
        return;
    }

    const int input_shape[] = {1, PM25_HISTORY_ROWS, PM25_HISTORY_FEATURES};
    const int output_shape[] = {1, NODE_PM25_FORECAST_COUNT};
    const TfLiteTensor *input = runtime.input(0);
    const TfLiteTensor *output = runtime.output(0);
    if (runtime.inputs_size() != 1 || runtime.outputs_size() != 1 ||
        !float_tensor_matches(input, input_shape, 3) || !float_tensor_matches(output, output_shape, 2))
    {
        ESP_LOGE(TAG, "Unexpected model input/output tensors");
        return;
    }
    input_data = input->data.f;
    output_data = output->data.f;
    interpreter = &runtime;
    ESP_LOGI(TAG, "Model ready, tensor arena used=%u bytes, history=%u valid samples",
             (unsigned)runtime.arena_used_bytes(),
             PM25_HISTORY_ROWS);
}

bool pm25_forecast_sample(const node_forecast_sample_t *sample,
                          uint16_t out[NODE_PM25_FORECAST_COUNT])
{
    if (interpreter == nullptr)
    {
        return false;
    }

    const float values[PM25_HISTORY_FEATURES] = {
        static_cast<float>(sample->pm25_ug_m3),
        sample->temp_hum.temperature_x10 / SENSOR_FIXED_POINT_SCALE,
        sample->temp_hum.humidity_x10 / SENSOR_FIXED_POINT_SCALE,
    };
    const bool valid = sample->temp_hum_valid && sample->pm25_ug_m3 != NODE_PM25_UNAVAILABLE;
    if (!pm25_history_record(&history, values, valid))
    {
        return false;
    }

    const bool predicted = predict(history.window, out);
    log_event = predicted ? LogEvent::ready : LogEvent::inference_failed;
    return predicted;
}

void pm25_forecast_flush_log(void)
{
    switch (log_event)
    {
        case LogEvent::ready:
            ESP_LOGI(TAG, "Inference %" PRId64 ".%03" PRId64 " ms, PM2.5 ug/m3 model steps +1=%u +2=%u +3=%u +4=%u",
                     elapsed_us / US_PER_MS, elapsed_us % US_PER_MS,
                     (unsigned)logged_forecast[0], (unsigned)logged_forecast[1],
                     (unsigned)logged_forecast[2], (unsigned)logged_forecast[3]);
            break;
        case LogEvent::inference_failed:
            ESP_LOGW(TAG, "PM2.5 inference failed");
            break;
        case LogEvent::none:
            break;
    }
    log_event = LogEvent::none;
}
