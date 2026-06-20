/**
 * @file    test_esp8266.c
 * @brief   ESP8266 透传测试 —— USART1 ↔ ESP8266 (USART3) 双向桥接 (IDLE 回调驱动)
 */

#include "test.h"
#include "bsp_esp8266.h"
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE  256U
#define CMD_SIZE  64U

static uint32_t esp_baudrate = 115200U;

/* ---- 文件级状态 ---- */
static uint8_t fwd_buf[BUF_SIZE];
static uint8_t line_start = 1;

/* ================================================================
 * 波特率切换
 * ================================================================ */
static void esp_set_baudrate(uint32_t new_baud)
{
    esp_baudrate = new_baud;
    BSP_USART_Init(BSP_USART1, new_baud);
    BSP_ESP8266_Init(new_baud);
}

/* ================================================================
 * 本地命令
 * ================================================================ */
static void process_local_cmd(const char *cmd)
{
    if (strcmp(cmd, "help") == 0) {
        printf("\r\n=== Commands ===\r\n");
        printf("  !help | !reset | !info | !baud <rate>\r\n\r\n");
    } else if (strcmp(cmd, "reset") == 0) {
        printf("\r\n[Reset...]\r\n");
        BSP_ESP8266_Reset();
        printf("[Done]\r\n\r\n");
    } else if (strcmp(cmd, "info") == 0) {
        printf("\r\nBaud:%lu EN:%s\r\n\r\n",
               (unsigned long)esp_baudrate,
               BSP_ESP8266_IsEnabled() ? "ON" : "OFF");
    } else if (strncmp(cmd, "baud ", 5) == 0) {
        uint32_t r = (uint32_t)atoi(cmd + 5);
        if (r == 9600 || r == 19200 || r == 38400
            || r == 57600 || r == 115200 || r == 230400) {
            printf("\r\n[Switch to %lu...]\r\n", (unsigned long)r);
            esp_set_baudrate(r);
            printf("[Done]\r\n\r\n");
        } else {
            printf("\r\n[Invalid baud]\r\n\r\n");
        }
    } else {
        printf("\r\n[?] !%s\r\n\r\n", cmd);
    }
}

/* ================================================================
 * USART1 (PC 终端) IDLE 回调
 * ================================================================ */
static void on_usart1_rx(BSP_USART_Id_t id)
{
    uint8_t buf[BUF_SIZE];
    uint16_t len = BSP_USART_Available(id);
    if (len == 0) return;
    if (len > BUF_SIZE) len = BUF_SIZE;
    len = BSP_USART_ReadBytes(id, buf, len);
    if (len == 0) return;

    uint16_t fwd_len = 0;
    uint16_t i = 0;

    while (i < len) {
        uint8_t ch = buf[i];

        /* 行首 '!' → 本地命令 */
        if (line_start && ch == '!') {
            i++;
            char cmd[CMD_SIZE];
            uint8_t ci = 0;
            while (i < len && buf[i] != '\r' && buf[i] != '\n') {
                if (ci < CMD_SIZE - 1 && buf[i] >= 0x20 && buf[i] <= 0x7E)
                    cmd[ci++] = (char)buf[i];
                i++;
            }
            cmd[ci] = '\0';

            if (i < len && (buf[i] == '\r' || buf[i] == '\n')) {
                if (ci > 0) process_local_cmd(cmd);
                if (buf[i] == '\r') i++;
                if (i < len && buf[i] == '\n') i++;
                line_start = 1;
            } else {
                fwd_buf[fwd_len++] = '!';
                for (uint8_t j = 0; j < ci; j++)
                    fwd_buf[fwd_len++] = (uint8_t)cmd[j];
                line_start = 0;
            }
            continue;
        }

        /* \r → \r\n */
        if (ch == '\r') {
            fwd_buf[fwd_len++] = '\r';
            if (i + 1 < len && buf[i + 1] == '\n') {
                i++;
                fwd_buf[fwd_len++] = '\n';
            } else {
                fwd_buf[fwd_len++] = '\n';
            }
            line_start = 1;
        } else if (ch == '\n') {
            fwd_buf[fwd_len++] = '\n';
            line_start = 1;
        } else {
            fwd_buf[fwd_len++] = ch;
            line_start = 0;
        }
        i++;
    }

    if (fwd_len > 0) {
        BSP_ESP8266_Flush(100);
        BSP_ESP8266_Send(fwd_buf, fwd_len);
    }
}

/* ================================================================
 * ESP8266 (USART3) IDLE 回调
 * ================================================================ */
static void on_esp8266_rx(BSP_USART_Id_t id)
{
    (void)id;
    uint16_t len = BSP_ESP8266_Available();
    if (len == 0) return;
    if (len > BUF_SIZE) len = BUF_SIZE;
    len = BSP_ESP8266_ReadBytes(fwd_buf, len);
    if (len > 0) {
        BSP_USART_Flush(BSP_USART1, 100);
        BSP_USART_Send(BSP_USART1, fwd_buf, len);
    }
}

/* ================================================================
 * 主函数
 * ================================================================ */
void Test_ESP8266_Passthrough(void)
{
    /* ---- USART1: 注册 IDLE 回调 ---- */
    BSP_USART_Config_t usart1_cfg = {
        .baudrate         = esp_baudrate,
        .word_length      = BSP_USART_WORD_8B,
        .stop_bits        = BSP_USART_STOP_1,
        .parity           = BSP_USART_PARITY_NONE,
        .rx_idle_callback = on_usart1_rx,
    };
    BSP_USART_InitEx(BSP_USART1, &usart1_cfg);

    /* ---- ESP8266: 注册 IDLE 回调 ---- */
    BSP_ESP8266_Config_t esp_cfg = {
        .baudrate         = esp_baudrate,
        .word_length      = BSP_USART_WORD_8B,
        .stop_bits        = BSP_USART_STOP_1,
        .parity           = BSP_USART_PARITY_NONE,
        .rx_idle_callback = on_esp8266_rx,
    };
    BSP_ESP8266_InitEx(&esp_cfg);

    printf("\r\n=== ESP8266 Passthrough (%lubaud) ===\r\n",
           (unsigned long)esp_baudrate);
    printf("!help | !reset | !info | !baud <rate>\r\n\r\n");

    while (1) {
        /* 全部工作由 IDLE 回调在 ISR 中完成 */
    }
}
