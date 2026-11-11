#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "node_config.h"
#include "protocol/node_packet.h"
#include "radio/node_radio.h"
#include "sx127x.h"
#include "sx127x_registers.h"
#include "sx127x_spi.h"

#define RADIO_SPI_HOST SPI2_HOST
#define RADIO_IRQ_QUEUE_LENGTH 8U
#define RADIO_RESET_LOW_US 1000U
#define RADIO_RESET_RELEASE_US 5000U
#define RADIO_US_PER_MS INT64_C(1000)
#define RADIO_US_PER_SECOND INT64_C(1000000)
#define RADIO_LOW_FREQUENCY_MODE 0x08U
#define RADIO_IRQ_TX_DONE 0x08U
#define RADIO_IRQ_RX_DONE 0x40U
#define RADIO_IRQ_CRC_ERROR 0x20U
#define RADIO_IRQ_CLEAR_ALL 0xFFU
#define RADIO_RX_HAS_PAYLOAD_CRC 0x40U
#define RADIO_DIO0_MASK 0x3FU
#define RADIO_INVERT_IQ_MASK 0xBEU
#define RADIO_IQ_TX_NORMAL 0x01U
#define RADIO_REG_INVERT_IQ2 0x3B
#define RADIO_IQ2_NORMAL 0x1DU

/* Profile radio được chốt cùng gateway. Nếu đổi profile, cập nhật mapping
 * enum/library và test thay vì vô tình giữ giá trị hard-code không còn khớp. */
_Static_assert(NODE_RADIO_BANDWIDTH_HZ == 125000U, "Radio profile requires BW125");
_Static_assert(NODE_RADIO_CODING_RATE_DENOMINATOR == 5U, "Radio profile requires CR4/5");
_Static_assert(NODE_RADIO_EXPLICIT_HEADER_ENABLED == 1, "Radio profile requires explicit header");
_Static_assert(NODE_RADIO_INVERT_IQ_ENABLED == 0, "Radio profile requires normal IQ");

typedef struct
{
    int64_t time_us;
    uint32_t epoch;
} radio_irq_event_t;

static const char *TAG = "node_radio";
static sx127x device;
static spi_device_handle_t spi_device;
static bool bus_owned;
static bool device_created;
static bool isr_service_owned;
static bool handler_installed;
static bool reset_pin_claimed;
static bool dio0_pin_claimed;
static bool ready;
static bool receiving;
static int64_t transaction_start_us;

/* Chỉ các biến này được ISR và task cùng truy cập, luôn dùng irq_lock.
 * Các handle, mode và dữ liệu còn lại do duy nhất task gọi API sở hữu. */
static portMUX_TYPE irq_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t irq_queue;
static uint32_t irq_epoch;
static bool irq_overflow;

static void IRAM_ATTR dio0_isr(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;
    portENTER_CRITICAL_ISR(&irq_lock);
    if (irq_queue != NULL)
    {
        const radio_irq_event_t event = {
            .time_us = esp_timer_get_time(),
            .epoch = irq_epoch,
        };
        if (xQueueSendFromISR(irq_queue, &event, &woken) != pdPASS)
        {
            irq_overflow = true;
        }
    }
    portEXIT_CRITICAL_ISR(&irq_lock);
    if (woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static int write_register(int reg, uint8_t value)
{
    /* API này cập nhật shadow cache của dependency khi register có cache. */
    return sx127x_write_register(reg, value, &device.spi_device);
}

static int read_register(int reg, uint8_t *value)
{
    uint32_t raw = 0;
    /* IRQ, payload length, FIFO current và header CRC là trạng thái phần cứng
     * theo từng packet: đọc SPI thật, không dùng snapshot cache từ packet cũ. */
    const int result = sx127x_spi_read_registers(reg, spi_device, 1, &raw);
    if (result == SX127X_OK)
    {
        *value = (uint8_t)raw;
    }
    return result;
}

static int set_mode(sx127x_mode_t mode)
{
    if (mode == SX127X_MODE_TX || mode == SX127X_MODE_RX_CONT)
    {
        uint8_t mapping;
        if (read_register(REGDIOMAPPING1, &mapping) != SX127X_OK)
        {
            return SX127X_ERR_INVALID_ARG;
        }
        mapping = (uint8_t)((mapping & RADIO_DIO0_MASK) |
                  (mode == SX127X_MODE_TX ? SX127X_DIO0_TX_DONE : SX127X_DIO0_RX_DONE));
        if (write_register(REGDIOMAPPING1, mapping) != SX127X_OK)
        {
            return SX127X_ERR_INVALID_ARG;
        }
    }

    /* set_opmod của library 5.0.1 ghi mode|modulation, bỏ bit LF. Ghi mode có
     * LF ngay từ đầu để không bật TX ở profile sai rồi mới sửa sau đó. LoRa
     * đã được chọn trong Sleep khi init, không đổi modem lúc TX/RX đang chạy. */
    const uint8_t value = (uint8_t)(SX127X_MODULATION_LORA |
                                   RADIO_LOW_FREQUENCY_MODE | mode);
    const int result = write_register(REGOPMODE, value);
    if (result == SX127X_OK)
    {
        device.active_modem = SX127X_MODULATION_LORA;
        device.opmod = mode;
    }
    return result;
}

static node_result_t reset_events(void)
{
    if (gpio_intr_disable((gpio_num_t)BOARD_RADIO_DIO0_GPIO) != ESP_OK)
    {
        return NODE_IO_ERROR;
    }
    portENTER_CRITICAL(&irq_lock);
    ++irq_epoch;
    irq_overflow = false;
    const BaseType_t result = xQueueReset(irq_queue);
    portEXIT_CRITICAL(&irq_lock);
    return result == pdPASS ? NODE_OK : NODE_IO_ERROR;
}

static node_result_t release_resources(void)
{
    node_result_t status = NODE_OK;
    ready = false;
    receiving = false;
    if (handler_installed)
    {
        if (gpio_intr_disable((gpio_num_t)BOARD_RADIO_DIO0_GPIO) != ESP_OK)
        {
            ESP_LOGE(TAG, "Cannot disable DIO0 interrupt during cleanup");
            status = NODE_IO_ERROR;
        }
        if (gpio_isr_handler_remove((gpio_num_t)BOARD_RADIO_DIO0_GPIO) != ESP_OK)
        {
            /* ISR có thể còn dùng queue, giữ queue và tài nguyên để thử lại. */
            ESP_LOGE(TAG, "Cannot remove DIO0 handler, resources retained");
            return NODE_IO_ERROR;
        }
        handler_installed = false;
    }

    portENTER_CRITICAL(&irq_lock);
    QueueHandle_t old_queue = irq_queue;
    irq_queue = NULL;
    ++irq_epoch;
    irq_overflow = false;
    portEXIT_CRITICAL(&irq_lock);
    if (old_queue != NULL)
    {
        /* ISR giữ irq_lock trong suốt lúc dùng queue. Detach ở trên phải chờ
         * ISR đang chạy rời critical, ISR đến sau chỉ thấy NULL. Vì vậy không
         * còn ISR giữ queue cũ khi giải phóng ở đây, kể cả trên lõi còn lại. */
        vQueueDelete(old_queue);
    }
    if (isr_service_owned)
    {
        gpio_uninstall_isr_service();
        isr_service_owned = false;
    }
    if (device_created && set_mode(SX127X_MODE_SLEEP) != SX127X_OK)
    {
        ESP_LOGE(TAG, "Cannot put radio to sleep during cleanup");
        status = NODE_IO_ERROR;
    }
    if (spi_device != NULL)
    {
        if (spi_bus_remove_device(spi_device) != ESP_OK)
        {
            ESP_LOGE(TAG, "Cannot remove SPI device, bus retained");
            return NODE_IO_ERROR;
        }
        spi_device = NULL;
        device_created = false;
    }
    if (bus_owned)
    {
        if (spi_bus_free(RADIO_SPI_HOST) != ESP_OK)
        {
            ESP_LOGE(TAG, "Cannot free owned SPI bus");
            return NODE_IO_ERROR;
        }
        bus_owned = false;
    }
    if (dio0_pin_claimed)
    {
        if (gpio_reset_pin((gpio_num_t)BOARD_RADIO_DIO0_GPIO) == ESP_OK)
        {
            dio0_pin_claimed = false;
        }
        else
        {
            ESP_LOGE(TAG, "Cannot reset DIO0 GPIO");
            status = NODE_IO_ERROR;
        }
    }
    if (reset_pin_claimed)
    {
        if (gpio_reset_pin((gpio_num_t)BOARD_RADIO_RESET_GPIO) == ESP_OK)
        {
            reset_pin_claimed = false;
        }
        else
        {
            ESP_LOGE(TAG, "Cannot release reset GPIO");
            status = NODE_IO_ERROR;
        }
    }
    return status;
}

void node_radio_deinit(void)
{
    if (release_resources() != NODE_OK)
    {
        ESP_LOGE(TAG, "Radio cleanup incomplete, retry deinit before reuse");
    }
}

static bool valid_pins(void)
{
    const int pins[] = {BOARD_RADIO_CS_GPIO, BOARD_RADIO_MOSI_GPIO,
                        BOARD_RADIO_SCK_GPIO, BOARD_RADIO_MISO_GPIO,
                        BOARD_RADIO_DIO0_GPIO, BOARD_RADIO_RESET_GPIO};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i)
    {
        if (!GPIO_IS_VALID_GPIO(pins[i]) || pins[i] == BOARD_DHT11_GPIO)
        {
            return false;
        }
        for (size_t j = 0; j < i; ++j)
        {
            if (pins[i] == pins[j])
            {
                return false;
            }
        }
    }
    return GPIO_IS_VALID_OUTPUT_GPIO(BOARD_RADIO_CS_GPIO) &&
           GPIO_IS_VALID_OUTPUT_GPIO(BOARD_RADIO_MOSI_GPIO) &&
           GPIO_IS_VALID_OUTPUT_GPIO(BOARD_RADIO_SCK_GPIO) &&
           GPIO_IS_VALID_OUTPUT_GPIO(BOARD_RADIO_RESET_GPIO);
}

static bool configure_reset_pin(void)
{
    const gpio_num_t pin = (gpio_num_t)BOARD_RADIO_RESET_GPIO;
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << BOARD_RADIO_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    reset_pin_claimed = true;
    if (gpio_config(&config) != ESP_OK || gpio_set_level(pin, 0) != ESP_OK)
    {
        return false;
    }
    esp_rom_delay_us(RADIO_RESET_LOW_US);
    if (gpio_set_direction(pin, GPIO_MODE_INPUT) != ESP_OK)
    {
        return false;
    }
    esp_rom_delay_us(RADIO_RESET_RELEASE_US);
    return true;
}

static bool configure_modem(void)
{
    const sx127x_tx_header_t header = {
        .enable_crc = NODE_RADIO_HARDWARE_CRC_ENABLED != 0,
        .coding_rate = SX127X_CR_4_5,
    };
    /* Chuyển modem trong Sleep bằng API library một lần, các lần đổi mode
     * tiếp theo đi qua set_mode để giữ LF, mapping DIO0 và state nhất quán. */
    if (sx127x_set_opmod(SX127X_MODE_SLEEP, SX127X_MODULATION_LORA, &device) != SX127X_OK ||
        set_mode(SX127X_MODE_STANDBY) != SX127X_OK ||
        sx127x_set_frequency(NODE_RADIO_FREQUENCY_HZ, &device) != SX127X_OK ||
        sx127x_lora_reset_fifo(&device) != SX127X_OK ||
        sx127x_lora_set_bandwidth(SX127X_BW_125000, &device) != SX127X_OK ||
        sx127x_lora_set_implicit_header(NULL, &device) != SX127X_OK ||
        sx127x_lora_set_spreading_factor((sx127x_sf_t)(NODE_RADIO_SPREADING_FACTOR << 4), &device) != SX127X_OK ||
        sx127x_lora_set_low_datarate_optimization(false, &device) != SX127X_OK ||
        sx127x_lora_set_syncword(NODE_RADIO_SYNC_WORD, &device) != SX127X_OK ||
        sx127x_set_preamble_length(NODE_RADIO_PREAMBLE_SYMBOLS, &device) != SX127X_OK ||
        sx127x_tx_set_pa_config(SX127X_PA_PIN_BOOST, NODE_RADIO_TX_POWER_DBM, &device) != SX127X_OK ||
        sx127x_rx_set_lna_gain(SX127X_LNA_GAIN_AUTO, &device) != SX127X_OK)
    {
        return false;
    }
    sx127x_tx_header_t tx_header = header;
    uint8_t iq;
    return sx127x_lora_tx_set_explicit_header(&tx_header, &device) == SX127X_OK &&
           read_register(REGINVERTIQ, &iq) == SX127X_OK &&
           write_register(REGINVERTIQ, (uint8_t)((iq & RADIO_INVERT_IQ_MASK) | RADIO_IQ_TX_NORMAL)) == SX127X_OK &&
           write_register(RADIO_REG_INVERT_IQ2, RADIO_IQ2_NORMAL) == SX127X_OK &&
           write_register(REGMAXPAYLOADLENGTH, NODE_PACKET_MAX_SIZE) == SX127X_OK &&
           write_register(REGIRQFLAGSMASK, 0) == SX127X_OK &&
           write_register(REGIRQFLAGS, RADIO_IRQ_CLEAR_ALL) == SX127X_OK;
}

node_result_t node_radio_init(void)
{
    if (ready)
    {
        return NODE_OK;
    }
    if (release_resources() != NODE_OK || !valid_pins())
    {
        return NODE_INIT_FAILED;
    }

    QueueHandle_t queue = xQueueCreate(RADIO_IRQ_QUEUE_LENGTH, sizeof(radio_irq_event_t));
    if (queue == NULL)
    {
        return NODE_INIT_FAILED;
    }
    portENTER_CRITICAL(&irq_lock);
    irq_queue = queue;
    portEXIT_CRITICAL(&irq_lock);

    const spi_bus_config_t bus_config = {
        .mosi_io_num = BOARD_RADIO_MOSI_GPIO,
        .miso_io_num = BOARD_RADIO_MISO_GPIO,
        .sclk_io_num = BOARD_RADIO_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = NODE_PACKET_MAX_SIZE + 1U,
    };
    const spi_device_interface_config_t spi_config = {
        .command_bits = 0,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = NODE_RADIO_SPI_CLOCK_HZ,
        .spics_io_num = BOARD_RADIO_CS_GPIO,
        .queue_size = 1,
    };
    if (spi_bus_initialize(RADIO_SPI_HOST, &bus_config, SPI_DMA_DISABLED) != ESP_OK)
    {
        goto fail;
    }
    bus_owned = true;
    if (spi_bus_add_device(RADIO_SPI_HOST, &spi_config, &spi_device) != ESP_OK)
    {
        goto fail;
    }
    if (!configure_reset_pin())
    {
        goto fail;
    }
    if (sx127x_create(spi_device, &device) != SX127X_OK)
    {
        goto fail;
    }
    device_created = true;
    if (device.chip_version != SX1276_VERSION || !configure_modem())
    {
        goto fail;
    }

    const gpio_config_t dio_config = {
        .pin_bit_mask = UINT64_C(1) << BOARD_RADIO_DIO0_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    dio0_pin_claimed = true;
    if (gpio_config(&dio_config) != ESP_OK)
    {
        goto fail;
    }
    const esp_err_t service_result = gpio_install_isr_service(0);
    if (service_result == ESP_OK)
    {
        isr_service_owned = true;
    }
    else if (service_result != ESP_ERR_INVALID_STATE)
    {
        goto fail;
    }
    if (gpio_isr_handler_add((gpio_num_t)BOARD_RADIO_DIO0_GPIO, dio0_isr, NULL) != ESP_OK)
    {
        goto fail;
    }
    handler_installed = true;
    if (gpio_set_intr_type((gpio_num_t)BOARD_RADIO_DIO0_GPIO, GPIO_INTR_POSEDGE) != ESP_OK ||
        gpio_intr_enable((gpio_num_t)BOARD_RADIO_DIO0_GPIO) != ESP_OK)
    {
        goto fail;
    }
    ready = true;
    return NODE_OK;

fail:
    if (release_resources() != NODE_OK)
    {
        ESP_LOGE(TAG, "Partial radio init cleanup failed");
    }
    return NODE_INIT_FAILED;
}

static node_result_t abort_operation(node_result_t reason)
{
    receiving = false;
    if (reset_events() != NODE_OK)
    {
        reason = NODE_IO_ERROR;
    }
    if (set_mode(SX127X_MODE_STANDBY) != SX127X_OK)
    {
        reason = NODE_IO_ERROR;
    }
    return reason;
}

static node_result_t wait_event(int64_t deadline_us, radio_irq_event_t *event)
{
    const int64_t tick_us = RADIO_US_PER_SECOND / configTICK_RATE_HZ;
    for (;;)
    {
        const int64_t now = esp_timer_get_time();
        if (now < 0)
        {
            return NODE_IO_ERROR;
        }
        if (now >= deadline_us)
        {
            return NODE_TIMEOUT;
        }

        portENTER_CRITICAL(&irq_lock);
        const bool overflow = irq_overflow;
        portEXIT_CRITICAL(&irq_lock);
        if (overflow)
        {
            return NODE_IO_ERROR;
        }

        const int64_t remaining = deadline_us - now;
        int64_t ticks = remaining / tick_us + (remaining % tick_us != 0);
        if (ticks >= (int64_t)portMAX_DELAY)
        {
            ticks = (int64_t)portMAX_DELAY - 1;
        }
        if (xQueueReceive(irq_queue, event, (TickType_t)ticks) != pdPASS)
        {
            continue;
        }

        portENTER_CRITICAL(&irq_lock);
        const bool lost_event = irq_overflow;
        const uint32_t epoch = irq_epoch;
        portEXIT_CRITICAL(&irq_lock);
        if (lost_event)
        {
            return NODE_IO_ERROR;
        }
        if (event->epoch != epoch || event->time_us < transaction_start_us)
        {
            continue;
        }
        if (event->time_us >= deadline_us || esp_timer_get_time() >= deadline_us)
        {
            return NODE_TIMEOUT;
        }
        return NODE_OK;
    }
}

node_result_t node_radio_send(const uint8_t *frame, size_t len,
                              uint32_t timeout_ms, int64_t *tx_done_us)
{
    if (tx_done_us != NULL)
    {
        *tx_done_us = 0;
    }
    if (frame == NULL || tx_done_us == NULL || len == 0 ||
        len > NODE_PACKET_MAX_SIZE || timeout_ms == 0)
    {
        return NODE_INVALID_ARG;
    }
    if (!ready)
    {
        return NODE_INIT_FAILED;
    }
    receiving = false;

    if (reset_events() != NODE_OK || set_mode(SX127X_MODE_STANDBY) != SX127X_OK ||
        write_register(REGIRQFLAGS, RADIO_IRQ_CLEAR_ALL) != SX127X_OK ||
        sx127x_lora_tx_set_for_transmission(frame, (uint8_t)len, &device) != SX127X_OK)
    {
        return abort_operation(NODE_IO_ERROR);
    }
    transaction_start_us = esp_timer_get_time();
    const int64_t timeout_us = (int64_t)timeout_ms * RADIO_US_PER_MS;
    if (transaction_start_us < 0 || transaction_start_us > INT64_MAX - timeout_us)
    {
        return abort_operation(NODE_INVALID_ARG);
    }
    const int64_t deadline = transaction_start_us + timeout_us;
    if (gpio_intr_enable((gpio_num_t)BOARD_RADIO_DIO0_GPIO) != ESP_OK ||
        set_mode(SX127X_MODE_TX) != SX127X_OK)
    {
        return abort_operation(NODE_IO_ERROR);
    }

    for (;;)
    {
        radio_irq_event_t event;
        node_result_t result = wait_event(deadline, &event);
        if (result != NODE_OK)
        {
            return abort_operation(result);
        }
        uint8_t flags;
        if (read_register(REGIRQFLAGS, &flags) != SX127X_OK)
        {
            return abort_operation(NODE_IO_ERROR);
        }
        if ((flags & RADIO_IRQ_TX_DONE) == 0)
        {
            if (write_register(REGIRQFLAGS, flags) != SX127X_OK)
            {
                return abort_operation(NODE_IO_ERROR);
            }
            continue;
        }
        /* Chỉ clear TX_DONE trước RX. Không clear mọi IRQ hoặc reset queue sau
         * khi vào RX: ACK có thể đã tới trước khi send() kịp trả về caller. */
        if (write_register(REGIRQFLAGS, RADIO_IRQ_TX_DONE) != SX127X_OK ||
            set_mode(SX127X_MODE_RX_CONT) != SX127X_OK)
        {
            return abort_operation(NODE_IO_ERROR);
        }
        receiving = true;
        *tx_done_us = event.time_us;
        return NODE_OK;
    }
}

static node_result_t begin_receive(void)
{
    if (reset_events() != NODE_OK || set_mode(SX127X_MODE_STANDBY) != SX127X_OK ||
        write_register(REGIRQFLAGS, RADIO_IRQ_CLEAR_ALL) != SX127X_OK)
    {
        return NODE_IO_ERROR;
    }
    transaction_start_us = esp_timer_get_time();
    if (gpio_intr_enable((gpio_num_t)BOARD_RADIO_DIO0_GPIO) != ESP_OK ||
        set_mode(SX127X_MODE_RX_CONT) != SX127X_OK)
    {
        return NODE_IO_ERROR;
    }
    receiving = true;
    return NODE_OK;
}

node_result_t node_radio_receive(uint8_t *out, size_t capacity, size_t *len,
                                 int64_t deadline_us)
{
    if (len != NULL)
    {
        *len = 0;
    }
    if (out == NULL || len == NULL || capacity == 0 || deadline_us < 0)
    {
        return NODE_INVALID_ARG;
    }
    if (!ready)
    {
        return NODE_INIT_FAILED;
    }
    if (esp_timer_get_time() >= deadline_us)
    {
        return NODE_TIMEOUT;
    }
    if (!receiving && begin_receive() != NODE_OK)
    {
        return abort_operation(NODE_IO_ERROR);
    }

    for (;;)
    {
        radio_irq_event_t event;
        node_result_t result = wait_event(deadline_us, &event);
        if (result != NODE_OK)
        {
            return abort_operation(result);
        }
        uint8_t flags;
        if (read_register(REGIRQFLAGS, &flags) != SX127X_OK)
        {
            return abort_operation(NODE_IO_ERROR);
        }
        if ((flags & RADIO_IRQ_RX_DONE) == 0)
        {
            if (write_register(REGIRQFLAGS, flags) != SX127X_OK)
            {
                return abort_operation(NODE_IO_ERROR);
            }
            continue;
        }

        /* Dừng nhận trước khi đọc FIFO để radio không sửa buffer trong lúc copy.
         * Bộ đệm local không được trả con trỏ cho caller, chỉ copy khi hợp lệ. */
        if (set_mode(SX127X_MODE_STANDBY) != SX127X_OK)
        {
            return abort_operation(NODE_IO_ERROR);
        }
        uint8_t packet_len;
        uint8_t header;
        uint8_t fifo_address;
        uint8_t packet[NODE_PACKET_MAX_SIZE];
        if (read_register(REGRXNBBYTES, &packet_len) != SX127X_OK ||
            read_register(REGHOPCHANNEL, &header) != SX127X_OK)
        {
            return abort_operation(NODE_IO_ERROR);
        }
        const bool valid = (flags & RADIO_IRQ_CRC_ERROR) == 0 &&
                           (header & RADIO_RX_HAS_PAYLOAD_CRC) != 0 &&
                           packet_len != 0 && packet_len <= NODE_PACKET_MAX_SIZE;
        if (valid)
        {
            if (read_register(REGFIFORXCURRENTADDR, &fifo_address) != SX127X_OK ||
                write_register(REGFIFOADDRPTR, fifo_address) != SX127X_OK ||
                sx127x_spi_read_buffer(REGFIFO, packet, packet_len, spi_device) != SX127X_OK)
            {
                return abort_operation(NODE_IO_ERROR);
            }
        }
        if (write_register(REGIRQFLAGS, flags) != SX127X_OK ||
            set_mode(SX127X_MODE_RX_CONT) != SX127X_OK)
        {
            return abort_operation(NODE_IO_ERROR);
        }
        if (esp_timer_get_time() >= deadline_us)
        {
            return NODE_TIMEOUT;
        }
        if (!valid)
        {
            continue;
        }
        if (packet_len > capacity)
        {
            return NODE_BUFFER_TOO_SMALL;
        }
        memcpy(out, packet, packet_len);
        *len = packet_len;
        return NODE_OK;
    }
}

node_result_t node_radio_sleep(void)
{
    if (!ready)
    {
        return NODE_INIT_FAILED;
    }
    receiving = false;
    if (reset_events() != NODE_OK ||
        write_register(REGIRQFLAGS, RADIO_IRQ_CLEAR_ALL) != SX127X_OK ||
        set_mode(SX127X_MODE_SLEEP) != SX127X_OK)
    {
        return NODE_IO_ERROR;
    }
    return NODE_OK;
}
