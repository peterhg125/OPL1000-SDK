/******************************************************************************
*  Copyright 2017 - 2018, Opulinks Technology Ltd.
*  ---------------------------------------------------------------------------
*  Statement:
*  ----------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of Opulinks Technology Ltd. (C) 2018
******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>
#include "opl1000.h"
#include "os.h"
#include "at_cmd.h"
#include "at_cmd_sys.h"
#include "at_cmd_wifi.h"
#include "at_cmd_ble.h"
#include "at_cmd_tcpip.h"
#include "at_cmd_rf.h"
#include "at_cmd_pip.h"
#include "at_cmd_others.h"
#include "le_ctrl.h"
#include "hal_uart.h"
#include "at_cmd_common.h"
#include "hal_dbg_uart.h"
#include "hal_uart.h"
#include "hal_pin.h"
#include "hal_pin_def.h"
#include "at_cmd_task_patch.h"
#include "at_cmd_common_patch.h"
#include "hal_pin_config_patch.h"

#if defined(__AT_CMD_TASK__)
#include "at_cmd_task.h"
#endif
#include "at_cmd_data_process.h"
#include "wpa_supplicant_i.h"
//#include "le_cmd_app_cmd.h"


E_IO01_UART_MODE g_eIO01UartMode;

extern at_uart_buffer_t at_rx_buf;
extern int at_echo_on;

extern uint8_t at_state;
extern uint8_t *pDataLine;
extern uint8_t  at_data_line[AT_DATA_LEN_MAX];
extern uint16_t at_send_len;
extern uint8_t sending_id;

/*
 * @brief UART1 RX Data Handler of TCP/IP (Reserved)
 *
 * @param [in] u32Data 4bytes data from UART1 interrupt
 */
void _uart1_rx_int_at_data_receive_tcpip_patch(uint32_t u32Data)
{
    int send_len = 0;
    bool sendex_flag = FALSE;
    at_socket_t *link = at_link_get_id(sending_id);

    send_len = data_process_data_len_get();

    *pDataLine = (u32Data & 0xFF);

    //ToDo:
    //Enter transparent transmission, with a 20-ms
    //interval between each packet, and a maximum of
    //2048 bytes per packet.
    //When a single packet containing +++ is received,
    //returns to normal command mode.

    if (at_state == AT_STATE_SENDING_RECV)
    {
        //if not transparent transmission mode, display back
        //if(( (u32Data & 0xFF) != '\n') && (at_echo_on))
        {
            Hal_Uart_DataSend(UART_IDX_1, (u32Data & 0xFF));
        }

        if (link->send_mode == AT_SEND_MODE_SENDEX)
        {
            uint32_t index = 0;
            for (index = 0; index < pDataLine - at_data_line; index++)
            {
                if ((at_data_line[index] == '\\') && (at_data_line[index + 1] == '0'))
                {
                    at_data_line[index] = '\0';
                    sendex_flag = TRUE;
                    send_len = index ;
                    break;
                }
            }
        }

        if ((pDataLine >= &at_data_line[send_len - 1]) || (pDataLine >= &at_data_line[AT_DATA_LEN_MAX - 1]) || (sendex_flag == TRUE))
        {
            at_event_msg_t msg = {0};

            msg.event = AT_DATA_TX_EVENT;
            msg.length = send_len;
            msg.param = at_data_line;
            at_data_task_send(&msg);
            at_state = AT_STATE_SENDING_DATA;
            pDataLine = at_data_line;
            sendex_flag = FALSE;
        }
        else
        {
            pDataLine++;
        }
    }
    else //AT_STATE_SENDING_DATA
    {
        at_uart1_printf((char *)"\r\nbusy p...\r\n");
    }
}

void _uart1_rx_int_do_at_patch(uint32_t u32Data)
{
    at_uart_buffer_t *p;
    xATMessage txMsg;
    int lock = data_process_lock_get();

    p = &at_rx_buf;

    //command input for all modules
    if (lock == LOCK_NONE)
    {
        if( p->in < (AT_RBUF_SIZE-1) ) // one reserved for '\0'
        {
            uint8_t u8Buff = 0;
            u8Buff = u32Data & 0xFF;

            if( (u8Buff == 0x0A) || (u8Buff == 0x0D) )
            {
                // 0x0A:LF, 0x0D:CR. "ENTER" key. Windows:CR+LF, Linux:CR and Mac:LF
                p->buf[ p->in ] = 0x00;

                /** send message */
                txMsg.pcMessage = (char *)p;
                txMsg.length = sizeof(at_uart_buffer_t);
                txMsg.event = AT_UART1_EVENT;

                /** Post the byte. */
                at_task_send( txMsg );
                at_clear_uart_buffer();
            }
            else if(u8Buff == 0x08)
            {
                // back space
                if (p->in > 0)
                {
                    p->in--;
                    p->buf[ p->in ] = 0x00;
                    Hal_Uart_DataSend(UART_IDX_1, 0x08);
                    Hal_Uart_DataSend(UART_IDX_1, 0x20);
                    Hal_Uart_DataSend(UART_IDX_1, 0x08);
                }
            }
            else
            {
                // Others 
                p->buf[ p->in ] = u8Buff;
                if(at_echo_on) 
                    Hal_Uart_DataSend(UART_IDX_1, p->buf[p->in]);
                p->in++;
            }
        }
        // if overflow, no action.
    }

    //data input for BLE or TCP/IP module
    else
    {
        //Set handler of BLE or TCP/IP module
        switch(lock)
        {
            case LOCK_BLE:
                _uart1_rx_int_at_data_receive_ble(u32Data);
                break;

            case LOCK_TCPIP:
                _uart1_rx_int_at_data_receive_tcpip(u32Data);
                break;

            default:
                break;
        }
    }
}

void at_cmd_switch_uart1_dbguart(void)
{
    osSemaphoreWait(g_tSwitchuartSem, osWaitForever);

    if (g_eIO01UartMode == IO01_UART_MODE_AT)
    {
        Hal_Pin_ConfigSet(0, PIN_TYPE_UART_APS_TX, PIN_DRIVING_FLOAT);
        Hal_Pin_ConfigSet(1, PIN_TYPE_UART_APS_RX, PIN_DRIVING_FLOAT);

        Hal_Pin_ConfigSet(8, PIN_TYPE_UART1_TX, PIN_DRIVING_FLOAT);
        Hal_Pin_ConfigSet(9, PIN_TYPE_UART1_RX, PIN_DRIVING_HIGH);
    }
    else
    {
        Hal_Pin_ConfigSet(8, PIN_TYPE_UART_APS_TX, PIN_DRIVING_FLOAT);
        Hal_Pin_ConfigSet(9, PIN_TYPE_UART_APS_RX, PIN_DRIVING_HIGH);
     
        Hal_Pin_ConfigSet(0, PIN_TYPE_UART1_TX, PIN_DRIVING_FLOAT);
        Hal_Pin_ConfigSet(1, PIN_TYPE_UART1_RX, PIN_DRIVING_FLOAT);
    }
    g_eIO01UartMode = (E_IO01_UART_MODE)!g_eIO01UartMode;
    osSemaphoreRelease(g_tSwitchuartSem);    
}

void at_io01_uart_mode_set(E_IO01_UART_MODE eMode)
{
    g_eIO01UartMode = eMode;
}


/*
 * @brief AT Common Interface Initialization (AT Common)
 *
 */
void at_cmd_common_func_init_patch(void)
{
	memset(&at_rx_buf, 0, sizeof(at_uart_buffer_t));
    at_io01_uart_mode_set(HAL_PIN_0_1_UART_MODE_PATCH);

    uart1_rx_int_do_at = _uart1_rx_int_do_at_patch;
    _uart1_rx_int_at_data_receive_tcpip = _uart1_rx_int_at_data_receive_tcpip_patch;
}

