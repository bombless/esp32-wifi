/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_http_server.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// HTTP事件处理程序
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADERS_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // 打印接收到的数据
                printf("%.*s\n", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
            
    }
    return ESP_OK;
}

extern const char server_cert_pem_start[] asm("_binary_server_cert_pem_start");

void https_request()
{
    ESP_LOGI(TAG, "https_request called");
    esp_http_client_config_t config = {
        .url = "https://chat.deepseek.com",
        .event_handler = _http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach, // 关键配置
        .buffer_size = 4096,       // 增大缓冲区
        .timeout_ms = 10000,       // 延长超时
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // 发起GET请求
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
}

/* 简单的请求处理函数 */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char* resp_str = "Hello from ESP32!";
    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

/* 带参数的请求处理 */
static esp_err_t echo_get_handler(httpd_req_t *req)
{
    char buffer[100];
    
    /* 从URL查询字符串获取参数 */
    if (httpd_req_get_url_query_str(req, buffer, sizeof(buffer)) == ESP_OK) {
        ESP_LOGI(TAG, "Found URL query => %s", buffer);
        
        char param[32];
        /* 获取指定参数值 */
        if (httpd_query_key_value(buffer, "name", param, sizeof(param)) == ESP_OK) {
            ESP_LOGI(TAG, "Found URL parameter => name=%s", param);
            
            char response[100];
            snprintf(response, sizeof(response), "Hello, %s!", param);
            httpd_resp_send(req, response, strlen(response));
            return ESP_OK;
        }
    }
    
    httpd_resp_send(req, "Please provide name parameter", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* 定义URI处理结构 */
static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t echo = {
    .uri       = "/echo",
    .method    = HTTP_GET,
    .handler   = echo_get_handler,
    .user_ctx  = NULL
};

/* 启动HTTP服务器 */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true; // 在内存不足时清理闲置连接
    
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        // 注册URI处理程序
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &echo);
        ESP_LOGI(TAG, "HTTP server started");
        return server;
    }
    
    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    ESP_LOGI(TAG, "xEventGroupWaitBits returned");
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
        https_request();
        start_webserver();
        void http_get_temperature(void*);
        xTaskCreate(&http_get_temperature, "http_get_task0", 4096, (void*)0, 5, NULL);
        xTaskCreate(&http_get_temperature, "http_get_task1", 4096, (void*)1, 5, NULL);
        xTaskCreate(&http_get_temperature, "http_get_task2", 4096, (void*)2, 5, NULL);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

int extract_temperature(const char *response) {
    const char *pattern = "\"temperature\":\"";
    const char *ptr = strstr(response, pattern);
    
    if (ptr) {
        ptr += strlen(pattern); // 移动到温度值开始的位置
        const char *end_ptr = strchr(ptr, '"'); // 查找结束引号
        
        if (end_ptr) {
            char temp_str[10];
            size_t len = end_ptr - ptr;
            if (len > sizeof(temp_str) - 1) {
                len = sizeof(temp_str) - 1;
            }
            
            strncpy(temp_str, ptr, len);
            temp_str[len] = '\0';
            
            // ESP_LOGI(TAG, "len: %d", len);
            ESP_LOGI(TAG, "提取到的温度: %s°C", temp_str);
            int ret = atoi(temp_str);
            // ESP_LOGI(TAG, "嗯，提取到的温度: %d°C", ret);
            return ret;
        } else {
            ESP_LOGE(TAG, "未找到温度值的结束引号");
        }
    } else {
        ESP_LOGE(TAG, "未找到温度字段");
    }
    return 0;
}

// 用于存储HTTP响应数据的缓冲区
#define MAX_HTTP_RECV_BUFFER 4096
static char response_data[3][MAX_HTTP_RECV_BUFFER];
static int response_len[3] = {0};
#define MAX_HTTP_OUTPUT_BUFFER 512
esp_err_t _http_event_handler_short(esp_http_client_event_t *evt)
{
    int index = (int)evt->user_data; // 获取用户数据指针
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // 只处理数据事件，并且只在缓冲区有空间时复制数据
            if (response_len[index] < MAX_HTTP_OUTPUT_BUFFER - 1) {
                int copy_len = evt->data_len > MAX_HTTP_OUTPUT_BUFFER - response_len[index] ? MAX_HTTP_OUTPUT_BUFFER - response_len[index] : evt->data_len;
                memcpy(response_data[index] + response_len[index], evt->data, copy_len);
                response_len[index] += copy_len;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}
struct info {
    int temperature;
    char *city;
    char *url;
};

static struct info weather_info[3] = {
    {0, "北京", "https://weathernew.pae.baidu.com/weathernew/pc?query=%E5%8C%97%E4%BA%AC%E5%A4%A9%E6%B0%94&srcid=4982&forecast=long_day_forecast"},
    {0, "上海", "https://weathernew.pae.baidu.com/weathernew/pc?query=%E4%B8%8A%E6%B5%B7%E5%A4%A9%E6%B0%94&srcid=4982&forecast=long_day_forecast"},
    {0, "广州", "https://weathernew.pae.baidu.com/weathernew/pc?query=%E5%B9%BF%E4%B8%9C%E5%B9%BF%E5%B7%9E%E5%A4%A9%E6%B0%94&srcid=4982&forecast=long_day_forecast"}};

void http_get_temperature(void* param)
{
    int index = (int)param; // 获取城市索引

    esp_http_client_config_t config = {
        .url = weather_info[index].url,
        .event_handler = _http_event_handler_short,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
        .crt_bundle_attach = esp_crt_bundle_attach, // 关键配置
        .timeout_ms = 10000,       // 延长超时
        .user_data = (void*)index, // 将响应数据缓冲区传递给用户数据
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // 清空响应缓冲区
    memset(response_data[index], 0, sizeof(response_data[index]));
    response_len[index] = 0;
    
    // 执行GET请求
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        // ESP_LOGI(TAG, "HTTP GET状态 = %d, 内容长度 = %lld",
        //         esp_http_client_get_status_code(client),
        //         esp_http_client_get_content_length(client));
        
        // 确保响应以null结尾
        if (response_len[index] < MAX_HTTP_RECV_BUFFER) {
            response_data[index][response_len[index]] = '\0';
        } else {
            response_data[index][MAX_HTTP_RECV_BUFFER - 1] = '\0';
        }
        // ESP_LOGI(TAG, "城市: %s 下标: %d 地址：%p 数据: %s", weather_info[index].city, index, response_data[index], response_data[index]);
        // 提取温度数据
        int temperature = extract_temperature(response_data[index]);
        weather_info[index].temperature = temperature;
        ESP_LOGI(TAG, "城市: %s 温度: %d°C", weather_info[index].city, temperature);
    } else {
        ESP_LOGE(TAG, "HTTP GET请求失败: %s", esp_err_to_name(err));
    }
    
    // 清理
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
}
