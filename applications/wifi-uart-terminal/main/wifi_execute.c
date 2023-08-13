#include "wifi_execute.h"

#include "hosal_uart.h"
#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/_intsup.h>

#include <bl_romfs.h>
#include "aos_vfs.h"
#include "uart.h"
#include "bl_gpio.h"

#define STA_SSID "SSID"
#define STA_PASSWORD "PASSWORD"

// client message
#define CMD_INPUT '0'
#define CMD_RESIZE_TERMINAL '1'
#define CMD_PAUSE '2'
#define CMD_RESUME '3'
#define CMD_JSON_DATA '{'
#define CMD_DETECT_BAUD 'B'
#define CMD_SEND_BREAK 'b'

// server message
#define CMD_OUTPUT '0'
#define CMD_SET_WINDOW_TITLE '1'
// Defined as a string to be concatenated below
#define CMD_SET_PREFERENCES "2"

#define CMD_SERVER_PAUSE 'S'
#define CMD_SERVER_RESUME 'Q'
#define CMD_SERVER_DETECTED_BAUD 'B'

static const char *s_listen_on = "ws://0.0.0.0:80";
static const char *s_web_root = "/romfs";

static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if (ev == MG_EV_OPEN) {
    // c->is_hexdumping = 1;
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    if (mg_http_match_uri(hm, "/terminal.html/ws")) {
      mg_ws_upgrade(c, hm, NULL);
    } else if (mg_http_match_uri(hm, "/terminal.html/token")) {
      mg_http_reply(c, 200, "Content-Type: application/json;charset=utf-8\r\n", "{%m:%m}\n", MG_ESC("token"), MG_ESC(""));
    } else if (mg_http_match_uri(hm, "/save")) {
      int baud = 0;
      char serial_config[16];
      char buf[64];
      if (mg_http_get_var(&hm->body, "baud", buf, sizeof(buf)) > 0) {
        baud = atoi(buf);
      }
      mg_http_get_var(&hm->body, "config", serial_config, sizeof(serial_config));
      hosal_uart_ioctl(&uart_dev, HOSAL_UART_BAUD_SET, (void*)baud);
      //printf("handle save, baud %d, config %s\r\n", baud, serial_config);
      mg_http_reply(c, 200, "", "ok\n");
    } else if (mg_http_match_uri(hm, "/gpio/set")) {
      long id, output;
      struct mg_str json = hm->body;
      id = mg_json_get_long(json, "$.id", -1);
      output = mg_json_get_long(json, "$.output", -1);
      bl_gpio_enable_output(id, 0, 0);
      bl_gpio_output_set(id, output);
      //printf("handle gpio set, id %d, output %d\r\n", id, output);
      mg_http_reply(c, 200, "", "");
    } else {
      struct mg_http_serve_opts opts = {.root_dir = s_web_root, .fs = &aos_vfs};
      mg_http_serve_dir(c, ev_data, &opts);
    }
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    char command = wm->data.ptr[0];
    switch (command) {
    case CMD_JSON_DATA:
        {
            printf("ws recv cmd json data\r\n");
            char windowTitle[100] = {0};
            windowTitle[0] = CMD_SET_WINDOW_TITLE;
            size_t n = 1 + snprintf(windowTitle+1, 99, "baud 115200");
            mg_ws_send(c, windowTitle, n, WEBSOCKET_OP_BINARY);
            const char clientConfig[] = "2" "{\"disableLeaveAlert\": true}";
            mg_ws_send(c, clientConfig, sizeof(clientConfig), WEBSOCKET_OP_BINARY);
        }
        // init
        break;
    case CMD_INPUT:
        hosal_uart_send(&uart_dev, wm->data.ptr+1, wm->data.len-1);
        break;
    case CMD_DETECT_BAUD:
        printf("TTY Requesting baudrate detection\r\n");
        break;
    case CMD_SEND_BREAK:
        printf("TTY Send break\r\n");
        //UART_COMM.sendBreak();
        break;
    case CMD_PAUSE:
        printf("TTY Send pause\r\n");
        //flowControlUartRequestStop(FLOW_CTL_SRC_REMOTE);
        break;
    case CMD_RESUME:
        printf("TTY Send resume\r\n");
        //flowControlUartRequestResume(FLOW_CTL_SRC_REMOTE);
        break;
    case CMD_RESIZE_TERMINAL:
        // Resize isn't implemented since... well... people in the 80's didn't predict we'd be resizing terminals in 2021
        break;
    default:
        printf("ws recv command %d\r\n", (int)command);
    }
    c->recv.len = 0;
  }
  (void) fn_data;
}

void http_server_start(void *pvParameters) {
  romfs_register();
  init_aos_vfs();
  init_uart_dev();
  struct mg_mgr mgr;  // Event manager
  mg_mgr_init(&mgr);  // Initialise event manager
  printf("Starting WS listener on %s/websocket\n", s_listen_on);
  mg_http_listen(&mgr, s_listen_on, fn, NULL);  // Create HTTP listener
  for (;;) {
    mg_mgr_poll(&mgr, 0);

    char buf[128] = {CMD_OUTPUT};
    int recv = hosal_uart_receive(&uart_dev, buf + 1, sizeof(buf) - 1);
    if (recv > 0) {
      for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next) {
        if (c->is_websocket) {
          mg_ws_send(c, buf, recv+1, WEBSOCKET_OP_BINARY);
        }
      }
    }
  }
  mg_mgr_free(&mgr);
}

static wifi_conf_t conf = {
    .country_code = "CN",
};

country_code_type country_code = WIFI_COUNTRY_CODE_CN;
static wifi_interface_t g_wifi_sta_interface = NULL;
static int g_wifi_sta_is_connected = 0;
wifi_sta_reconnect_t sta_reconn_params = {15, 10}; // set connection interval = 15, reconntction times = 10

void wifi_background_init(country_code_type country_code)
{
    char *country_code_string[WIFI_COUNTRY_CODE_MAX] = {"CN", "JP", "US", "EU"};

    /* init wifi background*/
    strcpy(conf.country_code, country_code_string[country_code]);
    wifi_mgmr_start_background(&conf);

    /* enable scan hide ssid */
    wifi_mgmr_scan_filter_hidden_ssid(0);
}

int wifi_sta_connect(void)
{
    g_wifi_sta_interface = wifi_mgmr_sta_enable();

    if (g_wifi_sta_is_connected == 1)
    {
        printf("sta has connect\n");
        return 0;
    }
    else
    {
        wifi_mgmr_sta_autoconnect_enable();
        printf("connect to wifi %s\n", STA_SSID);
        return wifi_mgmr_sta_connect(g_wifi_sta_interface, STA_SSID, STA_PASSWORD, NULL, NULL, 0, 0);
    }
}

static void wifi_event_cb(input_event_t *event, void *private_data)
{
    static char *ssid;
    static char *password;

    printf("[APP] [EVT] event->code %d\r\n", event->code);

    printf("[SYS] Memory left is %d Bytes\r\n", xPortGetFreeHeapSize());

    switch (event->code)
    {
    case CODE_WIFI_ON_AP_STARTED:
    {
        printf("[APP] [EVT] AP INIT DONE %lld\r\n", aos_now_ms());
    }
    break;

    case CODE_WIFI_ON_AP_STOPPED:
    {
        printf("[APP] [EVT] AP STOP DONE %lld\r\n", aos_now_ms());
    }
    break;

    case CODE_WIFI_ON_INIT_DONE:
    {
        printf("[APP] [EVT] INIT DONE %lld\r\n", aos_now_ms());
        wifi_mgmr_start_background(&conf);
        wifi_sta_connect();
    }
    break;
    case CODE_WIFI_ON_MGMR_DONE:
    {
        printf("[APP] [EVT] MGMR DONE %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_SCAN_DONE:
    {
        printf("[APP] [EVT] SCAN Done %lld\r\n", aos_now_ms());
        // wifi_mgmr_cli_scanlist();
    }
    break;
    case CODE_WIFI_ON_DISCONNECT:
    {
        g_wifi_sta_is_connected = 0;
        printf("wifi sta disconnected\n");
        printf("[APP] [EVT] disconnect %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_CONNECTING:
    {
        printf("[APP] [EVT] Connecting %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_CMD_RECONNECT:
    {
        printf("[APP] [EVT] Reconnect %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_CONNECTED:
    {
        printf("wifi sta connected\n");
        printf("[APP] [EVT] connected %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_PRE_GOT_IP:
    {
        printf("[APP] [EVT] connected %lld\r\n", aos_now_ms());
    }
    break;
    case CODE_WIFI_ON_GOT_IP:
    {
        printf("WIFI STA GOT IP\n");
        printf("[APP] [EVT] GOT IP %lld\r\n", aos_now_ms());
        /* create http server task */
        xTaskCreate(http_server_start, (char *)"http server", 1024 * 4, NULL, 15, NULL);
        g_wifi_sta_is_connected = 1;
    }
    break;
    case CODE_WIFI_ON_PROV_SSID:
    {
        printf("[APP] [EVT] [PROV] [SSID] %lld: %s\r\n",
               aos_now_ms(),
               event->value ? (const char *)event->value : "UNKNOWN");
        if (ssid)
        {
            vPortFree(ssid);
            ssid = NULL;
        }
        ssid = (char *)event->value;
    }
    break;
    case CODE_WIFI_ON_PROV_BSSID:
    {
        printf("[APP] [EVT] [PROV] [BSSID] %lld: %s\r\n",
               aos_now_ms(),
               event->value ? (const char *)event->value : "UNKNOWN");
        if (event->value)
        {
            vPortFree((void *)event->value);
        }
    }
    break;
    case CODE_WIFI_ON_PROV_PASSWD:
    {
        printf("[APP] [EVT] [PROV] [PASSWD] %lld: %s\r\n", aos_now_ms(),
               event->value ? (const char *)event->value : "UNKNOWN");
        if (password)
        {
            vPortFree(password);
            password = NULL;
        }
        password = (char *)event->value;
    }
    break;
    case CODE_WIFI_ON_PROV_CONNECT:
    {
        printf("connecting to %s:%s...\r\n", ssid, password);
    }
    break;
    case CODE_WIFI_ON_PROV_DISCONNECT:
    {
        printf("[APP] [EVT] [PROV] [DISCONNECT] %lld\r\n", aos_now_ms());
    }
    break;
    default:
    {
        printf("[APP] [EVT] Unknown code %u, %lld\r\n", event->code, aos_now_ms());
        /*nothing*/
    }
    }
}

void wifi_execute(void *pvParameters)
{
    aos_register_event_filter(EV_WIFI, wifi_event_cb, NULL);
    static uint8_t stack_wifi_init = 0;

    if (1 == stack_wifi_init)
    {
        printf("Wi-Fi Stack Started already!!!\r\n");
        return;
    }
    stack_wifi_init = 1;
    printf("Wi-Fi init successful\r\n");
    hal_wifi_start_firmware_task();
    /*Trigger to start Wi-Fi*/
    aos_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);

    vTaskDelete(NULL);
}
