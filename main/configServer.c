#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <sys/param.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include "esp_spiffs.h"

const char TAGHTTPD[] = "waterMonitorHttpd";
char webBuff[35];
char lineBuff[100];
httpd_handle_t server;

struct respTypes{
    char* suffix;
    char* type;
} responseType[] = {
    {".html", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"}
};

int endsWith(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

char *getResponseType(const char *url) {
    int resp = 0;
    for (int i = 0; i < (sizeof(responseType) / sizeof(responseType[0])); i++) {
        if (endsWith(url, responseType[i].suffix)) {
            ESP_LOGD(TAGHTTPD, "suffix match %i", i);
            resp = i;
        }
    }
    ESP_LOGD(TAGHTTPD, "Return type for URL %s is %s", url, responseType[resp].type);
    return responseType[resp].type;
}


/* Our URI handler function to be called during GET /uri request */
esp_err_t get_handler(httpd_req_t *req)
{
    /* Send a simple response */
    const char resp[] = "URI GET Response";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

esp_err_t web_handler(httpd_req_t *req)
{
    /* Send a simple response */
    ESP_LOGD(TAGHTTPD, "Request for URL %s", req->uri);
    FILE *f = fopen(req->uri, "r");
    if (f) {
        // determine correct return type
        httpd_resp_set_type(req, getResponseType(req->uri));

        // send the file content back to the client
        int count = 0;
        int buffSize = sizeof(lineBuff);
        int ch;
        do {
            ch = fgetc(f);
            if (feof(f)) {
                break;
            }
            lineBuff[count++] = (char)ch;
            if (count == buffSize-1) {
                lineBuff[count] = 0;
                count = 0;
                httpd_resp_sendstr_chunk(req, lineBuff);
            }
        } while(1);
        if (count > 0) {
            lineBuff[count] = 0;
            httpd_resp_sendstr_chunk(req, lineBuff);
        }
        httpd_resp_send_chunk(req, "", 0);
        fclose(f);

    } else {
        httpd_resp_send_404(req);
    }
    httpd_resp_send(req, webBuff, strlen(webBuff));
    return ESP_OK;
}

/* Our URI handler function to be called during POST /uri request */
esp_err_t post_handler(httpd_req_t *req)
{
    /* Destination buffer for content of HTTP POST request.
     * httpd_req_recv() accepts char* only, but content could
     * as well be any binary data (needs type casting).
     * In case of string data, null termination will be absent, and
     * content length would give length of string */
    char content[100];

    /* Truncate if content length larger than the buffer */
    size_t recv_size = MIN(req->content_len, sizeof(content));

    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout one can choose to retry calling
             * httpd_req_recv(), but to keep it simple, here we
             * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
         * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }

    /* Send a simple response */
    const char resp[] = "URI POST Response";
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_get = {
    .uri      = "/uri",
    .method   = HTTP_GET,
    .handler  = get_handler,
    .user_ctx = NULL
};

/* URI handler structure for GET /uri */
httpd_uri_t uri_web = {
    .uri      = "/web*",
    .method   = HTTP_GET,
    .handler  = web_handler,
    .user_ctx = NULL
};

/* URI handler structure for POST /uri */
httpd_uri_t uri_post = {
    .uri      = "/uri",
    .method   = HTTP_POST,
    .handler  = post_handler,
    .user_ctx = NULL
};

/* Server configuration */
httpd_config_t config = {
    .task_priority      = tskIDLE_PRIORITY+5,
    .stack_size         = 4096,
    .server_port        = 80,
    .ctrl_port          = 32768,
    .max_open_sockets   = 7,
    .max_uri_handlers   = 8,
    .max_resp_headers   = 8,
    .backlog_conn       = 5,
    .lru_purge_enable   = false,
    .recv_wait_timeout  = 5,
    .send_wait_timeout  = 5,
    .global_user_ctx = NULL,
    .global_user_ctx_free_fn = NULL,
    .global_transport_ctx = NULL,
    .global_transport_ctx_free_fn = NULL,
    .open_fn = NULL,
    .close_fn = NULL,
    .uri_match_fn = httpd_uri_match_wildcard
};

/* Function for starting the webserver */
void start_WebServer(void)
{
    /* Empty handle to esp_http_server */
    httpd_handle_t server = NULL;

    /* Start the httpd server */
    if (httpd_start(&server, &config) == ESP_OK) {
        /* Register URI handlers */
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_web);
        httpd_register_uri_handler(server, &uri_post);
    }
}

/* Function for stopping the webserver */
void stop_WebServer(void)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
        server = NULL;
    }
}

