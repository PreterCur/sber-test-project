#include "measure.h"


static const char *ADC_TAG = "ADC";
static const char *CSV_TAG = "CSV";

static const char *WIFI_TAG = "WIFI";

static size_t generate_csv_in_ram(  const uint16_t *raw_buf, 
                                    uint32_t sample_count, 
                                    char *out_csv_text, 
                                    size_t max_text_len);

static void demonstrate_generated_csv(const char *csv_data, size_t csv_size_bytes);
        

#define CSV_MAX_SIZE                        (size_t)100000
char full_csv_buffer[CSV_MAX_SIZE] = {0,};



static void init_adc_continuous(adc_continuous_handle_t *out_handle, user_adc_t *user_adc_p)
{
    // 1. Allocate the continuous mode master handle
    adc_continuous_handle_cfg_t adc_config = 
    {
        .max_store_buf_size = user_adc_p->frame_buffer_size, // Internal pool for DMA descriptors
        .conv_frame_size    = user_adc_p->single_conv_frame_size,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, out_handle));

    // 2. Configure the hardware pattern table and digital controller
    adc_continuous_config_t dig_cfg = 
    {
        .sample_freq_hz = user_adc_p->sample_freq,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1, // Track Unit 1 only
        .format = user_adc_p->output_type,
    };

    // Define channel patterns to read
    adc_digi_pattern_config_t pattern_list[1] = 
    {
        {
            .atten = user_adc_p->adc_atten,
            .channel = user_adc_p->adc_ch,
            .unit = user_adc_p->adc_unit,
            .bit_width = ADC_BITWIDTH_12,
        }
    };

    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = pattern_list;
    ESP_ERROR_CHECK(adc_continuous_config(*out_handle, &dig_cfg));

    // 3. Register asynchronous conversion event callbacks
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = user_adc_p->callback_func,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(*out_handle, &cbs, (void *)user_adc_p));
}

//user data is registered in adc_continuous_register_event_callbacks, i use user_adc_t pointer
static bool IRAM_ATTR adc_coexist_cb(adc_continuous_handle_t handle, 
                                     const adc_continuous_evt_data_t *edata, 
                                     void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    user_adc_t *adc_str_p = (user_adc_t *)user_data;
    // Notify the processing task that new data is available in the DMA buffer
    xTaskNotifyFromISR(*adc_str_p->adc_task, BIT(MEASURE_BUF_OVERFLOW_CALLBACK), eSetBits, &high_task_wakeup);
    return high_task_wakeup;
}

void upload_csv_to_server(const char *csv_data, size_t data_len)
{
    // 1. Настраиваем конфигурацию клиента (прямо как в примерах ESP-IDF)
    esp_http_client_config_t config = {
        .url = "http://192.168.10.179:8080/upload", // URL твоего локального сервера
        .method = HTTP_METHOD_POST,               // Метод отправки
        .timeout_ms = 5000,                       // Таймаут ожидания ответа
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 2. Устанавливаем тип контента, чтобы Java-сервер не гадал, что это
    esp_http_client_set_header(client, "Content-Type", "text/csv");

    // 3. Привязываем наш готовый большой текстовый буфер к телу POST-запроса
    esp_http_client_set_post_field(client, csv_data, data_len);

    // 4. Выполняем отправку (отправляет данные и ждет ответ от сервера)
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI("HTTP", "Данные улетели! Статус ответа сервера: %d", status_code);
    } else {
        ESP_LOGE("HTTP", "Ошибка отправки: %s", esp_err_to_name(err));
    }

    // 5. Обязательно освобождаем ресурсы клиента
    esp_http_client_cleanup(client);
}


void measure_task_handler(void *pvParameters)
{
    ESP_LOGI(ADC_TAG, "Measure task started\r\n");

    if (pvParameters == NULL)
    {
        ESP_LOGE(ADC_TAG, "MEASURE TASK ARG NULLPTR!\r\n");
        while (1);
    }
    measure_task_congif_t *measure_task_cfg = (measure_task_congif_t *)pvParameters;

    if (measure_task_cfg->evt_queue_h == NULL)
    {
        ESP_LOGE(ADC_TAG, "MEASURE EVT QUEUE NULLPTR!\r\n");
        while (1);
    }

    QueueHandle_t measure_evt_queue_h = *measure_task_cfg->evt_queue_h;
    user_adc_t *adc_conf_p = &measure_task_cfg->adc_config;

    //add callback here for now, maybe should move this initialization to main
    adc_conf_p->callback_func = adc_coexist_cb;
    if (adc_conf_p->callback_func == NULL)
    {
        ESP_LOGE(ADC_TAG, "ADC Callback NULLPTR\r\n");
        while (1);
    }

    adc_continuous_handle_t adc_h = NULL;

    init_adc_continuous(&adc_h, adc_conf_p);


    wifi_nvs_init_user();
    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(WIFI_TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();


    uint32_t measure_notify_val_bits = 0;

    bool is_measuring = false;
    TickType_t sleep_time = portMAX_DELAY;

    while (1)
    {
        if (is_measuring)
        {
            sleep_time = pdMS_TO_TICKS(100);
        }
        else
        {
            sleep_time = portMAX_DELAY;
        }

        UBaseType_t adc_wait_ret = xTaskNotifyWait(0x00, ULONG_MAX, &measure_notify_val_bits, sleep_time);
        if (adc_wait_ret == pdPASS)
        {
            uint32_t measure_cmd = log2(measure_notify_val_bits);
            // ESP_LOGI(ADC_TAG, "received %#X cmd, %#X bits\r\n", measure_cmd, measure_notify_val_bits);
            switch(measure_cmd)
            {
                case(MEASURE_START):
                {
                    ESP_LOGI(ADC_TAG, "Started ADC Measure, had %d measures read already\r\n", adc_conf_p->adc_read_num);
                    ESP_ERROR_CHECK(adc_continuous_flush_pool(adc_h));
                    ESP_ERROR_CHECK(adc_continuous_start(adc_h));
                    is_measuring = true;
                    adc_conf_p->adc_read_num = 0;
                    ESP_LOGI(ADC_TAG, "Measurement started\n");
                    ESP_LOGI(ADC_TAG,"ADC sample rate: %lu Hz\n", adc_conf_p->sample_freq);
                    ESP_LOGI(ADC_TAG,"ADC buffer size: %lu\n", adc_conf_p->frame_buffer_size);
                    
                }
                break;
                case(MEASURE_BTN_INTERRUPT):
                {
                    //notify from Director task                    
                    esp_err_t btn_stop_ret = adc_continuous_stop(adc_h);
                    if (btn_stop_ret != ESP_OK)
                    {
                        ESP_LOGW(ADC_TAG, "adc was already stopped via FULL CMD\r\n");
                        ESP_ERROR_CHECK(btn_stop_ret);
                    }
                    is_measuring = false;
                    ESP_LOGI(ADC_TAG, "Measurement stopped\n");

                    ESP_LOGI(ADC_TAG, "ADC BTN IRQ, ADC samples collected: %d\r\n", adc_conf_p->adc_read_num);
                    adc_conf_p->adc_read_num = 0;

                    generic_event_t adc_btn_stop_evt = 
                    {
                        .comp_id = COMP_ID_MEASURE,
                        .event_id = EVT_ADC_BTN_INTERRUPT,
                        .param = 0
                    };
                    BaseType_t adc_btn_irq_ret = xQueueSend(measure_evt_queue_h, &adc_btn_stop_evt, 0);
                    if (adc_btn_irq_ret != pdTRUE)
                    {
                        ESP_LOGE(ADC_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                        break;
                    }
                }
                break;
                case(MEASURE_BUF_FULL_CALLBACK):
                {
                    //notify from ADC ISR
                    is_measuring = false;
                    ESP_LOGI(ADC_TAG, "ADC BUF FULL, ADC samples collected: %d\r\n", adc_conf_p->adc_read_num);
                    esp_err_t full_adc_ret = adc_continuous_stop(adc_h);
                    if (full_adc_ret != ESP_OK)
                    {
                        ESP_LOGW(ADC_TAG, "adc was stopped before ADC FULL but still went into BTN CMD\r\n");
                        ESP_ERROR_CHECK(full_adc_ret);
                    }

                    ESP_LOGI(ADC_TAG, "Measurement stopped\n");

                    adc_conf_p->adc_read_num = 0;

                    generic_event_t adc_full_evt = 
                    {
                        .comp_id = COMP_ID_MEASURE,
                        .event_id = EVT_ADC_FULL_INTERRUPT,
                        .param = 0
                    };
                    BaseType_t full_queue_ret = xQueueSend(measure_evt_queue_h, &adc_full_evt, 0);
                    if (full_queue_ret != pdTRUE)
                    {
                        ESP_LOGE(ADC_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                        break;
                    }
                }
                break;
                case(MEASURE_BUF_OVERFLOW_CALLBACK):
                {
                    ESP_LOGE(ADC_TAG, "ADC BUF OVERFLOW ERROR\r\n");
                    generic_event_t adc_overflow_err_evt = 
                    {
                        .comp_id = COMP_ID_MEASURE,
                        .event_id = EVT_ADC_OVERFLOW_ERR,
                        .param = 0
                    };
                    BaseType_t full_queue_ret = xQueueSend(measure_evt_queue_h, &adc_overflow_err_evt, 0);
                    if (full_queue_ret != pdTRUE)
                    {
                        ESP_LOGE(ADC_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                        break;
                    }
                }
                break;
                case(MEASURE_GENERATE_CSV):
                {
                    ESP_LOGI(CSV_TAG, "CSV Created callback\r\n");
                    generic_event_t cvs_evt = 
                    {
                        .comp_id = COMP_ID_MEASURE,
                        
                        .param = 0
                    };
                    size_t csv_size = generate_csv_in_ram(adc_conf_p->adc_multiframe_buf, adc_conf_p->adc_read_num, full_csv_buffer, sizeof(full_csv_buffer));
                    if (csv_size == 0)
                    {
                        cvs_evt.event_id = EVT_CSV_CREATE_ERROR;
                        ESP_LOGE(CSV_TAG, "Failed CSV generation\r\n");
                        BaseType_t full_queue_ret = xQueueSend(measure_evt_queue_h, &cvs_evt, 0);
                        if (full_queue_ret != pdTRUE)
                        {
                            ESP_LOGE(CSV_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                            break;
                        }
                        break;
                    }

                    demonstrate_generated_csv(full_csv_buffer, csv_size);
                    cvs_evt.event_id = EVT_CSV_CREATED;

                    BaseType_t full_queue_ret = xQueueSend(measure_evt_queue_h, &cvs_evt, 0);
                    if (full_queue_ret != pdTRUE)
                    {
                        ESP_LOGE(ADC_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                        break;
                    }
                }
                break;
                case(MEASURE_WIFI_CONNECT):
                {
                    ESP_LOGI(CSV_TAG, "WIFI Connect callback\r\n");

                }
                break;
                case(MEASURE_START_UPLOAD):
                {
                    


                }
                break;
            }
        }

        if (is_measuring)
        {
            uint32_t bytes_read = 0;
            esp_err_t ret = adc_continuous_read(adc_h, (uint8_t *)adc_conf_p->adc_dma_frame, adc_conf_p->single_conv_frame_size, &bytes_read, 0);
            if (ret == ESP_OK && bytes_read > 0) 
            {
                uint32_t samples_received = bytes_read / SOC_ADC_DIGI_DATA_BYTES_PER_CONV;
                uint32_t buf_size_readings = adc_conf_p->frame_buffer_size / SOC_ADC_DIGI_DATA_BYTES_PER_CONV;
                uint32_t buf_size_bytes = adc_conf_p->frame_buffer_size;
                for (uint32_t i = 0; i < samples_received; i++) 
                {
                    //save adc data as uint16_t into a big buffer
                    adc_conf_p->adc_multiframe_buf[adc_conf_p->adc_read_num] = adc_conf_p->adc_dma_frame[i].type2.data;
                    adc_conf_p->adc_read_num++;
                    if (adc_conf_p->adc_read_num >= buf_size_readings)
                    {
                        break;
                    }
                }

                if (adc_conf_p->adc_read_num >= buf_size_readings)
                {
                    is_measuring = false;
                    xTaskNotify(*adc_conf_p->adc_task, (uint32_t)BIT(MEASURE_BUF_FULL_CALLBACK), eSetBits);
                    portYIELD();
                }
            }
        }
    }
}



/**
 * @brief Конвертирует сырой бинарный буфер АЦП в текстовый формат CSV
 * * @param raw_buf        Указатель на массив uint16_t с данными АЦП
 * @param sample_count   Количество точек (10000)
 * @param out_csv_text   Указатель на большой чаровый буфер, куда запишется текст
 * @param max_text_len   Максимальный размер выделенной под CSV памяти (например, 100000 байт)
 * @return size_t        Реальный размер сформированного CSV текста в байтах (без '\0')
 */
static size_t generate_csv_in_ram(const uint16_t *raw_buf, uint32_t sample_count, 
                           char *out_csv_text, size_t max_text_len)
{
    if (out_csv_text == NULL || raw_buf == NULL || max_text_len == 0) {
        return 0;
    }

    // 1. Записываем CSV заголовок
    const char *header = "index,value\n";
    size_t header_len = strlen(header);
    
    if (header_len >= max_text_len) return 0; // Защита от дурака
    
    strcpy(out_csv_text, header);
    size_t current_offset = header_len;

    // 2. Цикл сборки строк "индекс,значение\n"
    for (uint32_t i = 0; i < sample_count; i++) {
        // Считаем доступное место в буфере (оставляем 1 байт под финальный \0)
        size_t remaining_space = max_text_len - current_offset - 1;
        
        if (remaining_space <= 0) {
            ESP_LOGW("CSV_GEN", "Буфер CSV переполнен! Данные обрезаны на индексе %lu", (unsigned long)i);
            return 0;
        }

        // Печатаем строку прямо в тело общего буфера со смещением
        int written = snprintf(out_csv_text + current_offset, remaining_space, 
                               "%lu,%u\n", (unsigned long)i, raw_buf[i]);
        
        if (written < 0 || written >= remaining_space) {
            // Строка не поместилась целиком
            ESP_LOGW("CSV_GEN", "Буфер CSV закончился.");
            return 0;
        }
        
        current_offset += written;
    }

    return current_offset; // Возвращаем точную длину текста
}

/**
 * @brief Выводит лог измерения и превью готового текстового CSV-буфера по регламенту ТЗ
 * * @param csv_data          Указатель на сформированный текстовый буфер CSV в RAM
 * @param csv_size_bytes    Реальный размер CSV в байтах (вернула функция генерации)
 * @param sample_rate_hz    Частота дискретизации (например, 1000)
 * @param max_buffer_size   Целевой размер буфера (например, 10000)
 * @param collected_count   Фактически собранное количество точек
 */
void demonstrate_generated_csv(const char *csv_data, size_t csv_size_bytes)
{
    if (csv_data == NULL || csv_size_bytes == 0) {
        ESP_LOGE(CSV_TAG, "Ошибка: Данные CSV пусты или не были сформированы.\n");
        return;
    }

    // 1. Обязательный лог параметров сессии по ТЗ
    ESP_LOGI(CSV_TAG, "CSV created\n");
    ESP_LOGI(CSV_TAG, "CSV size: %zu bytes\n\n", csv_size_bytes);

    ESP_LOGI(CSV_TAG, "CSV preview:\n");

    // 2. Выводим первые строки (Заголовок "index,value\n" + первые 3 строки данных)
    // Нам нужно поймать ровно 4 переноса строки от начала файла
    int lines_to_print_top = 4; 
    int newline_count = 0;
    size_t top_index = 0;

    while (top_index < csv_size_bytes && newline_count < lines_to_print_top) {
        putchar(csv_data[top_index]);
        if (csv_data[top_index] == '\n') {
            newline_count++;
        }
        top_index++;
    }

    // Печатаем обязательное по заданию многоточие
    ESP_LOGI(CSV_TAG, "...\n");

    // 3. Выводим последние строки (последние 3 строки данных)
    // Сканируем буфер с конца назад, чтобы найти начало третьей строки снизу
    if (csv_size_bytes > 0) {
        size_t bottom_index = csv_size_bytes - 1;
        
        // Пропускаем самый последний \n на конце файла, если он там есть
        if (csv_data[bottom_index] == '\n' && bottom_index > 0) {
            bottom_index--;
        }

        int target_newlines_from_bottom = 3;
        int found_newlines = 0;

        while (bottom_index > 0) {
            if (csv_data[bottom_index] == '\n') {
                found_newlines++;
                if (found_newlines == target_newlines_from_bottom) {
                    bottom_index++; // Сдвигаемся на первый символ строки после найденного \n
                    break;
                }
            }
            bottom_index--;
        }

        // Выводим остаток буфера от найденного индекса до самого конца
        size_t bytes_to_write = csv_size_bytes - bottom_index;
        fwrite(&csv_data[bottom_index], 1, bytes_to_write, stdout);
    }
}