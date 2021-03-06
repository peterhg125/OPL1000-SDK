/******************************************************************************
*  Copyright 2017 - 2018, Opulinks Technology Ltd.
*  ----------------------------------------------------------------------------
*  Statement:
*  ----------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of Opulinks Technology Ltd. (C) 2018
******************************************************************************/

#include "blewifi_wifi_api.h"
#include "blewifi_ctrl.h"
#include "blewifi_data.h"
#include "blewifi_configuration.h"
#include "blewifi_app.h"
#include "blewifi_server_app.h"
#include "blewifi_ble_api.h"

#include "cmsis_os.h"
#include "wifi_api.h"
#include "wifi_nvm.h"
#include "event_loop.h"
#include "lwip_helper.h"

extern uint8_t g_ubAppCtrlRequestRetryTimes;

wifi_config_t wifi_config_req_connect;

void BleWifi_Wifi_DoScan(uint8_t *data, int len)
{
    wifi_scan_config_t scan_config = {0};
    scan_config.show_hidden = data[0];
    scan_config.scan_type = (wifi_scan_type_t)data[1];

    wifi_scan_start(&scan_config, NULL);
}

void BleWifi_Wifi_DoConnect(uint8_t *data, int len)
{
    uint8_t ubAPPAutoConnectGetApNum = 0;
    uint32_t i = 0;
    wifi_auto_connect_info_t *info = NULL;
    uint8_t ubAPPConnect = 1;

    g_ubAppCtrlRequestRetryTimes = 0;
    memcpy(wifi_config_req_connect.sta_config.bssid, &data[0], WIFI_MAC_ADDRESS_LENGTH);

    if (len >= 8)
    {
        // Determine the connected column
        if (data[WIFI_MAC_ADDRESS_LENGTH])
        {
            wifi_auto_connect_get_ap_num(&ubAPPAutoConnectGetApNum);
            if (ubAPPAutoConnectGetApNum)
            {
                info = (wifi_auto_connect_info_t *)malloc(sizeof(wifi_auto_connect_info_t));
                if (!info)
                {
                    printf("malloc fail, info is NULL\r\n");
                    BleWifi_Ble_SendResponse(BLEWIFI_RSP_CONNECT, BLEWIFI_WIFI_CONNECTED_FAIL);
                    return;
                }

                memset(info, 0, sizeof(wifi_auto_connect_info_t));
                for (i = 0; i < ubAPPAutoConnectGetApNum; i++)
                {
                    wifi_auto_connect_get_ap_info(i, info);
                    if(!MemCmp(wifi_config_req_connect.sta_config.bssid, info->bssid, sizeof(info->bssid)))
                    {
                        wifi_connection_connect_from_ac_index(i);
                        ubAPPConnect = 0;
                        return;
                    }
                }
                free(info); 
            }   

        }

        // Can't find ap in the ap record list
        if (ubAPPConnect)
        {
            wifi_config_req_connect.sta_config.password_length = data[7];
            memcpy((char *)wifi_config_req_connect.sta_config.password, &data[8], wifi_config_req_connect.sta_config.password_length);

            BLEWIFI_INFO("BLEWIFI: Recv Connect Request\r\n");
            wifi_set_config(WIFI_MODE_STA, &wifi_config_req_connect);
            wifi_connection_connect(&wifi_config_req_connect);
        }
    }
    else
    {
        BleWifi_Ble_SendResponse(BLEWIFI_RSP_CONNECT, BLEWIFI_WIFI_CONNECTED_FAIL);
    }
}

void BleWifi_Wifi_DoDisconnect(void)
{
    wifi_connection_disconnect_ap();
}

static int BleWifi_Wifi_GetManufName(uint8_t *name)
{
    uint16_t ret = false;
    
    if (name == NULL)
        return ret;

    memset(name, 0, STA_INFO_MAX_MANUF_NAME_SIZE);
    ret = wifi_nvm_sta_info_read(WIFI_NVM_STA_INFO_MANUFACTURE_NAME, STA_INFO_MAX_MANUF_NAME_SIZE, name);
    
    return ret;
}

static void BleWifi_Wifi_SendDeviceInfo(blewifi_device_info_t *dev_info)
{
    uint8_t *data;
    int data_len;
    uint8_t *pos;

    pos = data = malloc(sizeof(blewifi_scan_info_t));
    if (data == NULL) {
        printf("malloc error\r\n");
        return;
    }

    memcpy(data, dev_info->device_id, WIFI_MAC_ADDRESS_LENGTH);
    pos += 6;

    if (dev_info->name_len > BLEWIFI_MANUFACTURER_NAME_LEN)
        dev_info->name_len = BLEWIFI_MANUFACTURER_NAME_LEN;

    *pos++ = dev_info->name_len;
    memcpy(pos, dev_info->manufacturer_name, dev_info->name_len);
    pos += dev_info->name_len;
    data_len = (pos - data);

    BLEWIFI_DUMP("device info data", data, data_len);

    /* create device info data packet */
    BleWifi_Ble_DataSendEncap(BLEWIFI_RSP_READ_DEVICE_INFO, data, data_len);

    free(data);
}

void BleWifi_Wifi_ReadDeviceInfo(void)
{
    blewifi_device_info_t dev_info = {0};
    char manufacturer_name[33] = {0};

    wifi_config_get_mac_address(WIFI_MODE_STA, &dev_info.device_id[0]);
    BleWifi_Wifi_GetManufName((uint8_t *)&manufacturer_name);

    dev_info.name_len = strlen(manufacturer_name);
    memcpy(dev_info.manufacturer_name, manufacturer_name, dev_info.name_len);
    BleWifi_Wifi_SendDeviceInfo(&dev_info);
}

static int BleWifi_Wifi_SetManufName(uint8_t *name)
{
    uint16_t ret = false;
    uint8_t len;
    
    if (name == NULL)
        return ret;

    len = strlen((char *)name);
    if (len > STA_INFO_MAX_MANUF_NAME_SIZE)
        len = STA_INFO_MAX_MANUF_NAME_SIZE;
        
    ret = wifi_nvm_sta_info_write(WIFI_NVM_STA_INFO_MANUFACTURE_NAME, len, name);
    
    return ret;
}

void BleWifi_Wifi_WriteDeviceInfo(uint8_t *data, int len)
{
    blewifi_device_info_t dev_info;

    memset(&dev_info, 0, sizeof(blewifi_device_info_t));
    memcpy(dev_info.device_id, &data[0], WIFI_MAC_ADDRESS_LENGTH);
    dev_info.name_len = data[6];
    memcpy(dev_info.manufacturer_name, &data[7], dev_info.name_len);

    wifi_config_set_mac_address(WIFI_MODE_STA, dev_info.device_id);
    BleWifi_Wifi_SetManufName(dev_info.manufacturer_name);

    BleWifi_Ble_SendResponse(BLEWIFI_RSP_WRITE_DEVICE_INFO, 0);

    BLEWIFI_INFO("BLEWIFI: Device ID: \""MACSTR"\"\r\n", MAC2STR(dev_info.device_id));
    BLEWIFI_INFO("BLEWIFI: Device Manufacturer: %s",dev_info.manufacturer_name);
}

void BleWifi_Wifi_SendStatusInfo(uint16_t uwType)
{
    uint8_t *pubData, *pubPos;
    uint8_t ubStatus = 0, ubStrLen = 0;
    uint16_t uwDataLen;
    uint8_t ubaIp[4], ubaNetMask[4], ubaGateway[4];

    struct netif *iface = netif_find("st1");
    wifi_scan_info_t tInfo;

    ubaIp[0] = (iface->ip_addr.u_addr.ip4.addr >> 0) & 0xFF;
    ubaIp[1] = (iface->ip_addr.u_addr.ip4.addr >> 8) & 0xFF;
    ubaIp[2] = (iface->ip_addr.u_addr.ip4.addr >> 16) & 0xFF;
    ubaIp[3] = (iface->ip_addr.u_addr.ip4.addr >> 24) & 0xFF;

    ubaNetMask[0] = (iface->netmask.u_addr.ip4.addr >> 0) & 0xFF;
    ubaNetMask[1] = (iface->netmask.u_addr.ip4.addr >> 8) & 0xFF;
    ubaNetMask[2] = (iface->netmask.u_addr.ip4.addr >> 16) & 0xFF;
    ubaNetMask[3] = (iface->netmask.u_addr.ip4.addr >> 24) & 0xFF;

    ubaGateway[0] = (iface->gw.u_addr.ip4.addr >> 0) & 0xFF;
    ubaGateway[1] = (iface->gw.u_addr.ip4.addr >> 8) & 0xFF;
    ubaGateway[2] = (iface->gw.u_addr.ip4.addr >> 16) & 0xFF;
    ubaGateway[3] = (iface->gw.u_addr.ip4.addr >> 24) & 0xFF;

    wifi_sta_get_ap_info(&tInfo);

    pubPos = pubData = malloc(sizeof(blewifi_wifi_status_info_t));
    if (pubData == NULL) {
        printf("malloc error\r\n");
        return;
    }

    ubStrLen = strlen((char *)&tInfo.ssid);

    if (ubStrLen == 0)
    {
        ubStatus = 1; // Return Failure
        if (uwType == BLEWIFI_IND_IP_STATUS_NOTIFY)     // if failure, don't notify the status
            goto release;
    }
    else
    {
        ubStatus = 0; // Return success
    }

    /* Status */
    *pubPos++ = ubStatus; 

    /* ssid length */
    *pubPos++ = ubStrLen; 

   /* SSID */
    if (ubStrLen != 0)
    {
        memcpy(pubPos, tInfo.ssid, ubStrLen);
        pubPos += ubStrLen;
    }

   /* BSSID */
    memcpy(pubPos, tInfo.bssid, 6);
    pubPos += 6;

    /* IP */
    memcpy(pubPos, (char *)ubaIp, 4);
    pubPos += 4;

    /* MASK */
    memcpy(pubPos,  (char *)ubaNetMask, 4);
    pubPos += 4;                      

    /* GATEWAY */
    memcpy(pubPos,  (char *)ubaGateway, 4);
    pubPos += 4;                       

    uwDataLen = (pubPos - pubData);

    BLEWIFI_DUMP("Wi-Fi status info data", pubData, uwDataLen);
    /* create Wi-Fi status info data packet */
    BleWifi_Ble_DataSendEncap(uwType, pubData, uwDataLen);

release:	
    free(pubData);
}

void BleWifi_Wifi_ResetRecord(void)
{
    uint8_t ubResetResult = 0;
    
    ubResetResult = wifi_auto_connect_reset();
    BleWifi_Ble_SendResponse(BLEWIFI_RSP_RESET, ubResetResult);
    
    wifi_connection_disconnect_ap();
}

static void BleWifi_Wifi_SendSingleScanReport(uint16_t apCount, blewifi_scan_info_t *ap_list)
{
    uint8_t *data;
    int data_len;
    uint8_t *pos;
    int malloc_size = sizeof(blewifi_scan_info_t) * apCount;

    pos = data = malloc(malloc_size);
    if (data == NULL) {
        printf("malloc error\r\n");
        return;
    }

    for (int i = 0; i < apCount; ++i)
    {
        uint8_t len = ap_list[i].ssid_length;
        data_len = (pos - data);

        *pos++ = len;
        memcpy(pos, ap_list[i].ssid, len);
        pos += len;
        memcpy(pos, ap_list[i].bssid,6);
        pos += 6;
        *pos++ = ap_list[i].auth_mode;
        *pos++ = ap_list[i].rssi;
        *pos++ = ap_list[i].connected;
    }

    data_len = (pos - data);

    BLEWIFI_DUMP("scan report data", data, data_len);

    /* create scan report data packet */
    BleWifi_Ble_DataSendEncap(BLEWIFI_RSP_SCAN_REPORT, data, data_len);

    free(data);
}

int BleWifi_Wifi_SendScanReport(void)
{
    wifi_scan_info_t *ap_list = NULL;
    blewifi_scan_info_t *blewifi_ap_list = NULL;
    uint16_t apCount = 0;
    int8_t ubAppErr = 0;
    int32_t i = 0, j = 0;
    uint8_t ubAPPAutoConnectGetApNum = 0;
    wifi_auto_connect_info_t *info = NULL;

    wifi_scan_get_ap_num(&apCount);

    if (apCount == 0) {
        printf("No AP found\r\n");
        goto err;
    }
    printf("ap num = %d\n", apCount);
    ap_list = (wifi_scan_info_t *)malloc(sizeof(wifi_scan_info_t) * apCount);

    if (!ap_list) {
        printf("malloc fail, ap_list is NULL\r\n");
        ubAppErr = -1;
        goto err;
    }

    wifi_scan_get_ap_records(&apCount, ap_list);

    blewifi_ap_list = (blewifi_scan_info_t *)malloc(sizeof(blewifi_scan_info_t) *apCount);
    if (!blewifi_ap_list) {
        printf("malloc fail, blewifi_ap_list is NULL\r\n");
        ubAppErr = -1;
        goto err;
    }

    wifi_auto_connect_get_ap_num(&ubAPPAutoConnectGetApNum);
    if (ubAPPAutoConnectGetApNum)
    {
        info = (wifi_auto_connect_info_t *)malloc(sizeof(wifi_auto_connect_info_t) * ubAPPAutoConnectGetApNum);
        if (!info) {
            printf("malloc fail, info is NULL\r\n");
            ubAppErr = -1;
            goto err;
        }

        memset(info, 0, sizeof(wifi_auto_connect_info_t) * ubAPPAutoConnectGetApNum);

        for (i = 0; i < ubAPPAutoConnectGetApNum; i++)
        {
            wifi_auto_connect_get_ap_info(i, (info+i));
        }
    }

    /* build blewifi ap list */
    for (i = 0; i < apCount; ++i)
    {
        memcpy(blewifi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
        memcpy(blewifi_ap_list[i].bssid, ap_list[i].bssid, WIFI_MAC_ADDRESS_LENGTH);
        blewifi_ap_list[i].rssi = ap_list[i].rssi;
        blewifi_ap_list[i].auth_mode = ap_list[i].auth_mode;
        blewifi_ap_list[i].ssid_length = strlen((const char *)ap_list[i].ssid);
        blewifi_ap_list[i].connected = 0;        
        for (j = 0; j < ubAPPAutoConnectGetApNum; j++)
        {
            if ((info+j)->ap_channel)
            {
                if(!MemCmp(blewifi_ap_list[i].ssid, (info+j)->ssid, sizeof((info+j)->ssid)) && !MemCmp(blewifi_ap_list[i].bssid, (info+j)->bssid, sizeof((info+j)->bssid)))
                {
                    blewifi_ap_list[i].connected = 1;
                    break;
                }            
            }
        }
    }

    /* Send Data to BLE */
    /* Send AP inforamtion individually */
    for (i = 0; i < apCount; ++i)
    {
        BleWifi_Wifi_SendSingleScanReport(1, &blewifi_ap_list[i]);
        osDelay(100);
    }

err:
    if (ap_list)
        free(ap_list);
    
    if (blewifi_ap_list)
        free(blewifi_ap_list);
    
    if (info)    
        free(info);

    return ubAppErr; 

}

uint8_t BleWifi_Wifi_AutoConnectListNum(void)
{
    uint8_t ubApNum = 0;
    
    wifi_auto_connect_get_saved_ap_num(&ubApNum);
    return ubApNum;
}

void BleWifi_Wifi_DoAutoConnect(void)
{
    wifi_auto_connect_start();
}

void BleWifi_Wifi_ReqConnectRetry(void)
{
    wifi_connection_connect(&wifi_config_req_connect);
}

// it is used in the Wifi task
int BleWifi_Wifi_EventHandlerCb(wifi_event_id_t event_id, void *data, uint16_t length)
{
    uint8_t reason = *((uint8_t*)data);

    switch(event_id)
    {
        case WIFI_EVENT_STA_START:
            printf("\r\nWi-Fi Start \r\n");

            /* Tcpip stack and net interface initialization,  dhcp client process initialization. */
            lwip_network_init(WIFI_MODE_STA);

            /* DTIM: skip n-1 */
            if (BLEWIFI_WIFI_DTIM_INTERVAL == 0)
                wifi_config_set_skip_dtim(0);
            else
                wifi_config_set_skip_dtim(BLEWIFI_WIFI_DTIM_INTERVAL - 1);
            
            BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_INIT_COMPLETE, NULL, 0);
            break;

        case WIFI_EVENT_STA_CONNECTED:
            printf("\r\nWi-Fi Connected, reason %d \r\n", reason);
            lwip_net_start(WIFI_MODE_STA);
            BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_CONNECTION_IND, NULL, 0);
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            printf("\r\nWi-Fi Disconnected , reason %d\r\n", reason);
            BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_DISCONNECTION_IND, NULL, 0);
            break;

        case WIFI_EVENT_SCAN_COMPLETE:
            printf("\r\nWi-Fi Scan Done \r\n");
            BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_SCAN_DONE_IND, NULL, 0);
            break;

        case WIFI_EVENT_STA_GOT_IP:
            printf("\r\nWi-Fi Got IP \r\n");
            BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_GOT_IP_IND, NULL, 0);
            break;

        case WIFI_EVENT_STA_CONNECTION_FAILED:
            printf("\r\nWi-Fi Connected failed, reason %d\r\n", reason);
            BleWifi_Ctrl_MsgSend(BLEWIFI_CTRL_MSG_WIFI_DISCONNECTION_IND, NULL, 0);
            break;

        default:
            printf("\r\n Unknown Event %d \r\n", event_id);
            break;
    }
    return 0;
}

void BleWifi_Wifi_Init(void)
{
    /* Event Loop Initialization */
    wifi_event_loop_init((wifi_event_cb_t)BleWifi_Wifi_EventHandlerCb);

    /* Initialize wifi stack and register wifi init complete event handler */
    wifi_init(NULL, NULL);

    /* Wi-Fi operation start */
    wifi_start();
}
