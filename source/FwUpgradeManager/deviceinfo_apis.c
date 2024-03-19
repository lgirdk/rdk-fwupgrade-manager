/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 Sky
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
/*
 * Copyright [2014] [Cisco Systems, Inc.]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     LICENSE-2.0" target="_blank" rel="nofollow">http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <stdio.h>
#include <string.h>
#include "deviceinfo_apis.h"
#include "ssp_global.h"
#include "fwupgrade_hal.h"
#include "cap.h"
#include <syscfg/syscfg.h>
#include <ccsp_syslog.h>
#include "ccsp_psm_helper.h"
#ifdef FEATURE_RDKB_LED_MANAGER
#include <sysevent/sysevent.h>
extern int sysevent_fd ;
extern token_t sysevent_token;
#define SYSEVENT_LED_STATE    "led_event"
#define FW_DOWNLOAD_START_EVENT "rdkb_fwdownload_start"
#define FW_DOWNLOAD_STOP_EVENT "rdkb_fwdownload_stop"
#define FW_UPDATE_STOP_EVENT "rdkb_fwupdate_stop"
#define FW_UPDATE_COMPLETE_EVENT "rdkb_fwupdate_complete"
#ifdef FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL
#define HTTP_LED_FLASH_DISABLE_FLAG "/tmp/.dwd_led_blink_disable"
#define FW_DOWNLOAD_STOP_CAPTIVEMODE "rdkb_fwdownload_stop_captivemode"
#endif
#endif

extern cap_user appcaps;

extern char g_Subsystem[32];
extern ANSC_HANDLE bus_handle;

static char valid_fw[256 + 1];

static void checkCallStatus(void)
{
    char *paramNames[]={ "Device.Services.VoiceService.1.CallControl.Line.1.CallStatus", "Device.Services.VoiceService.1.CallControl.Line.2.CallStatus" };
    parameterValStruct_t **valStrCallStatus = NULL;
    char *compo = "eRT.com.cisco.spvtg.ccsp.telcovoicemanager";
    char *bus = "/com/cisco/spvtg/ccsp/telcovoicemanager";
    int  nval = 0;
    int ret = CCSP_FAILURE;

    for (int i = 0; i <= 1440; i++) /* Check whether it reached 24hrs (24 * 60) and if so, proceed to reboot irrespective of voice status */
    {
        ret = CcspBaseIf_getParameterValues(bus_handle,
                                                compo,
                                                bus,
                                                paramNames,
                                                2,
                                                &nval,
                                                &valStrCallStatus);
        if ((CCSP_SUCCESS == ret) && (nval == 2))
        {
            if (strcmp(valStrCallStatus[0]->parameterValue, "Connected") == 0 ||
                strcmp(valStrCallStatus[0]->parameterValue, "Dialing") == 0 ||
                strcmp(valStrCallStatus[0]->parameterValue, "Delivered") == 0 ||
                strcmp(valStrCallStatus[0]->parameterValue, "Alerting") == 0 ||
                strcmp(valStrCallStatus[1]->parameterValue, "Connected") == 0 ||
                strcmp(valStrCallStatus[1]->parameterValue, "Dialing") == 0 ||
                strcmp(valStrCallStatus[1]->parameterValue, "Delivered") == 0 ||
                strcmp(valStrCallStatus[1]->parameterValue, "Alerting") == 0)
            {
                CcspTraceError(("Reboot is delayed due to active call or call establishment in progress.\n"));
                sleep(60);
            }
            else if ((strcmp(valStrCallStatus[0]->parameterValue, "Idle") == 0 ||
                      strcmp(valStrCallStatus[0]->parameterValue, "Disconnected") == 0) &&
                     (strcmp(valStrCallStatus[1]->parameterValue, "Idle") == 0 ||
                      strcmp(valStrCallStatus[1]->parameterValue, "Disconnected") == 0))
            {
                CcspTraceInfo(("Line is not in a call.\n"));
                break;
            }
            else
            {
                CcspTraceError(("Unknown call status.\n"));
            }

            CcspTraceInfo(("CallStatus of Line 1 & Line 2 are %s & %s\n", valStrCallStatus[0]->parameterValue, valStrCallStatus[1]->parameterValue));
        }
        else
        {
            CcspTraceError(("Failed to get call status. Exiting loop.\n"));
            break;
        }

        if( valStrCallStatus )
        {
            free_parameterValStruct_t (bus_handle, nval, valStrCallStatus);
        }

    }
}

ANSC_STATUS FwDlDmlDIGetDLFlag(ANSC_HANDLE hContext)
{
    PDEVICE_INFO      pMyObject    = (PDEVICE_INFO)hContext;
    FILE *fp;
    char buff[64]={0};

    pMyObject->Download_Control_Flag = FALSE;

    if((fp = fopen("/etc/device.properties", "r")) == NULL)
    {
        CcspTraceError(("Error while opening the file device.properties \n"));
        CcspTraceInfo((" Download_Control_Flag is %d \n", pMyObject->Download_Control_Flag));
        return ANSC_STATUS_FAILURE;
    }

    while(fgets(buff, 64, fp) != NULL)
    {
        if(strstr(buff, "BUILD_TYPE") != NULL)
        {
            //RDKB-22703: Enabled support to allow TR-181 firmware download on PROD builds.
            if((strcasestr(buff, "dev") != NULL) || (strcasestr(buff, "vbn") != NULL) || (strcasestr(buff, "prod") != NULL))
            {
                pMyObject->Download_Control_Flag = TRUE;
                break;
            }
        }
    }

    if(fp)
        fclose(fp);

    CcspTraceInfo((" Download_Control_Flag is %d \n", pMyObject->Download_Control_Flag));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS FwDlDmlDIGetFWVersion(ANSC_HANDLE hContext)
{
    PDEVICE_INFO pMyObject = (PDEVICE_INFO) hContext;

    syscfg_get(NULL, "firmwarename", pMyObject->Current_Firmware, sizeof(pMyObject->Current_Firmware));

    CcspTraceInfo((" Current FW Version is %s\n", (pMyObject->Current_Firmware[0]) ? pMyObject->Current_Firmware : "UNKNOWN"));
    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS FwDlDmlDIGetDLStatus(ANSC_HANDLE hContext, char *DL_Status, size_t len)
{
    syscfg_get(NULL, "FWDWLD_status", DL_Status, len);

    CcspTraceDebug((" Download status is %s\n", DL_Status));

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS FwDlDmlDIGetProtocol(ANSC_HANDLE hContext, char *Protocol)
{
    PDEVICE_INFO     pMyObject = (PDEVICE_INFO)hContext;

    if (strlen(pMyObject->DownloadURL) == 0)
        AnscCopyString(Protocol, "");
    else if (strncasecmp(pMyObject->DownloadURL, "https", 5) == 0)
        AnscCopyString(Protocol, "HTTPS");
    else if (strncasecmp(pMyObject->DownloadURL, "http", 4) == 0)
        AnscCopyString(Protocol, "HTTP");
    else
        AnscCopyString(Protocol, "INVALID");

    CcspTraceInfo((" Download Protocol is %s \n", Protocol));

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS FwDlDmlDIDownloadNow(ANSC_HANDLE hContext)
{
    PDEVICE_INFO     pMyObject = (PDEVICE_INFO)hContext;
    int dl_status = 0;
    int ret = ANSC_STATUS_FAILURE, res = 0;
    char pHttpUrl[CM_HTTPURL_LEN] = {'0'};
    int downloadUrlLen = 0;
    int HttpUrlLen = 0;

    if(strlen(pMyObject->Firmware_To_Download) && strlen(pMyObject->DownloadURL))
    {
        convert_to_validFW(pMyObject->Firmware_To_Download,valid_fw);
        if (strcasecmp(valid_fw, pMyObject->Current_Firmware) == 0)
        {
            CcspTraceError((" Current FW is same, Ignoring request \n"));
#if defined(FEATURE_NETWORK_LOGS)
            syslog_networklog("NETWORK",LOG_NOTICE,"%s","Current FW is same, Ignoring request");
#endif
            return ANSC_STATUS_FAILURE;
        }

        strncpy(pHttpUrl, "'", 1);
	
        downloadUrlLen = strlen(pMyObject->DownloadURL);
        HttpUrlLen = strlen(pHttpUrl);

        if ((downloadUrlLen + HttpUrlLen) < CM_HTTPURL_LEN )
            strncat(pHttpUrl, pMyObject->DownloadURL, downloadUrlLen);

        strncat(pHttpUrl, "/", 1);

        downloadUrlLen = strlen(pMyObject->Firmware_To_Download);
        HttpUrlLen = strlen(pHttpUrl);

        if ((downloadUrlLen + HttpUrlLen) < CM_HTTPURL_LEN )
            strncat(pHttpUrl, pMyObject->Firmware_To_Download, downloadUrlLen);
        
        strncat(pHttpUrl, "'", 1);

        ret = ANSC_STATUS_FAILURE;
        ret = fwupgrade_hal_set_download_url(pHttpUrl , pMyObject->Firmware_To_Download);

        if( ret == ANSC_STATUS_FAILURE)
        {
            CcspTraceError((" Failed to set URL, Ignoring request \n"));
#if defined(FEATURE_NETWORK_LOGS)
            syslog_networklog("NETWORK",LOG_ERR,"%s","Failed to set URL, Ignoring request");
#endif

            return ANSC_STATUS_FAILURE;
        }

    }
    else
    {
        CcspTraceError((" URL or FW Name is missing, Ignoring request \n"));
#if defined(FEATURE_NETWORK_LOGS)
            syslog_networklog("NETWORK",LOG_ERR,"%s","URL or FW Name is missing, Ignoring request");
#endif
        return ANSC_STATUS_FAILURE;
    }

    dl_status = fwupgrade_hal_get_download_status();
    if(dl_status > 0 && dl_status <= 100)
    {
        CcspTraceError((" Already Downloading In Progress, Ignoring request \n"));
        return ANSC_STATUS_FAILURE;
    }
    else if(dl_status == 200)
    {
        CcspTraceError((" Image is already downloaded, Ignoring request \n"));
#if defined(FEATURE_NETWORK_LOGS)
        syslog_networklog("NETWORK",LOG_ERR,"%s","Image is already downloaded, Ignoring request");
#endif
        return ANSC_STATUS_FAILURE;
    }

    ret = ANSC_STATUS_FAILURE;
    ret = fwupgrade_hal_set_download_interface(1); // interface=0 for wan0, interface=1 for erouter0

    if( ret == ANSC_STATUS_FAILURE)
    {
        CcspTraceError((" Failed to set Interface, Ignoring request \n"));
        return ANSC_STATUS_FAILURE;
    }

    pthread_t FWDL_Thread;
    res = pthread_create(&FWDL_Thread, NULL, FwDl_ThreadFunc, "FwDl_ThreadFunc");
    if(res != 0)
    {
        CcspTraceError(("Create FWDL_Thread error %d\n", res));
    }
    else
    {
        CcspTraceInfo(("Image downloading triggered successfully \n"));
    }

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS FwDlDmlDIDownloadAndFactoryReset(ANSC_HANDLE hContext)
{
    PDEVICE_INFO     pMyObject = (PDEVICE_INFO)hContext;
    int dl_status = 0;
    int ret = ANSC_STATUS_FAILURE, res = 0;
    char pHttpUrl[CM_HTTPURL_LEN] = {'0'};
    int downloadUrlLen = 0;
    int HttpUrlLen = 0;

    if(strlen(pMyObject->Firmware_To_Download) && strlen(pMyObject->DownloadURL))
    {
        convert_to_validFW(pMyObject->Firmware_To_Download,valid_fw);
        if (strcasecmp(valid_fw, pMyObject->Current_Firmware) == 0)
        {
            CcspTraceError((" Current FW is same, Ignoring request \n"));
            return ANSC_STATUS_FAILURE;
        }

        strncpy(pHttpUrl, "'", 1);

        downloadUrlLen = strlen(pMyObject->DownloadURL);
        HttpUrlLen = strlen(pHttpUrl);

       if ((downloadUrlLen + HttpUrlLen) < CM_HTTPURL_LEN )
            strncat(pHttpUrl, pMyObject->DownloadURL, downloadUrlLen);

        strncat(pHttpUrl, "/", 1);

        downloadUrlLen = strlen(pMyObject->Firmware_To_Download);
        HttpUrlLen = strlen(pHttpUrl);

       if ((downloadUrlLen + HttpUrlLen) < CM_HTTPURL_LEN )
            strncat(pHttpUrl, pMyObject->Firmware_To_Download, CM_HTTPURL_LEN - 1);
        
        strncat(pHttpUrl, "'", 1);

        ret = ANSC_STATUS_FAILURE;
        ret = fwupgrade_hal_set_download_url(pHttpUrl , pMyObject->Firmware_To_Download);

        if( ret == ANSC_STATUS_FAILURE)
        {
            CcspTraceError((" Failed to set URL, Ignoring request \n"));
            return ANSC_STATUS_FAILURE;
        }

    }
    else
    {
        CcspTraceError((" URL or FW Name is missing, Ignoring request \n"));
        return ANSC_STATUS_FAILURE;
    }

    dl_status = fwupgrade_hal_get_download_status();
    if(dl_status > 0 && dl_status <= 100)
    {
        CcspTraceError((" Already Downloading In Progress, Ignoring request \n"));
        return ANSC_STATUS_FAILURE;
    }
    else if(dl_status == 200)
    {
        CcspTraceError((" Image is already downloaded, Ignoring request \n"));
        return ANSC_STATUS_FAILURE;
    }

    ret = ANSC_STATUS_FAILURE;
    ret = fwupgrade_hal_set_download_interface(1); // interface=0 for wan0, interface=1 for erouter0

    if( ret == ANSC_STATUS_FAILURE)
    {
        CcspTraceError((" Failed to set Interface, Ignoring request \n"));
        return ANSC_STATUS_FAILURE;
    }

    pthread_t FWDL_Thread;
    res = pthread_create(&FWDL_Thread, NULL, FwDlAndFR_ThreadFunc, "FwDlAndFR_ThreadFunc");
    if(res != 0)
    {
        CcspTraceError(("Create FWDL_Thread error %d\n", res));
    }
    else
    {
        CcspTraceInfo(("Image downloading triggered successfully \n"));
    }

    return ANSC_STATUS_SUCCESS;
}

static void SaveTCintoPSM(void)
{
    int retPsmGet = CCSP_SUCCESS;	
    char *param_value = NULL;

    retPsmGet = PSM_Get_Record_Value2(bus_handle,g_Subsystem, "CommandKey", NULL, &param_value);

    if (retPsmGet == CCSP_SUCCESS)
    {	    
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, "eRT.com.cisco.spvtg.ccsp.tr069pa.Undelivered_TC.1.CommandKey", ccsp_string, param_value);

        if (param_value)
        {
            AnscFreeMemory(param_value);
        }   
        PSM_Del_Record(bus_handle, g_Subsystem, "CommandKey");
    }
    else
    {
        PSM_Set_Record_Value2(bus_handle,g_Subsystem, "eRT.com.cisco.spvtg.ccsp.tr069pa.Undelivered_TC.1.CommandKey", ccsp_string, "");
    }	

    PSM_Set_Record_Value2(bus_handle,g_Subsystem, "eRT.com.cisco.spvtg.ccsp.tr069pa.Undelivered_TC.1.IsDownload", ccsp_boolean, "1");
}	

void FwDl_ThreadFunc()
{
    int dl_status = 0;
    int ret = ANSC_STATUS_FAILURE;
    ULONG reboot_ready_status = 0;
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
    char redirFlag[10]={0};
    char captivePortalEnable[10]={0};
    bool led_disable = false;
    FILE *fp = fopen(HTTP_LED_FLASH_DISABLE_FLAG, "r");
    if(fp != NULL)
	    led_disable = true;
#endif 
    cap_user app_caps;
    char sysbuf[16];

    pthread_detach(pthread_self());
    CcspTraceInfo(("Gaining root permission to download and write the code to flash \n"));
    gain_root_privilege();
    // Set download led here
#if defined (FEATURE_RDKB_LED_MANAGER)
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
    if (!led_disable) {
	    printf("Led Flashing Not Disabled \n");
	    if (sysevent_fd != -1) {
		    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_START_EVENT, 0);
	    }
    }
#else
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_START_EVENT, 0);
#endif
#endif

    syscfg_set_commit(NULL, "FWDWLD_status", "In Progress");

    ret = fwupgrade_hal_download ();
    if( ret == ANSC_STATUS_FAILURE)
    {
        CcspTraceError((" Failed to start download \n"));

        dl_status = fwupgrade_hal_get_download_status();
        if(dl_status == 109)
        {
            syscfg_set_commit(NULL, "FWDWLD_status", "Request Denied");
        }
        else if(dl_status == 122)
        {
            syscfg_set_commit(NULL, "FWDWLD_status", "File Not Available");
        }
        else if(dl_status == 101)
        {
            syscfg_set_commit(NULL, "FWDWLD_status", "Flash Error");
        }
        else if(dl_status == 102)
        {
            syscfg_set_commit(NULL, "FWDWLD_status", "Incorrect Signature");
        }
        else if(dl_status == 199)
        {
            syscfg_set_commit(NULL, "FWDWLD_status", "Retry");
        }
        else if((dl_status >= 400) || !(dl_status >= 0 && dl_status <= 100))
        {
            CcspTraceError((" FW DL is failed with status %d \n", dl_status));
            syscfg_set_commit(NULL, "FWDWLD_status", "Failed");
#if defined(FEATURE_NETWORK_LOGS)
            syslog_networklog("NETWORK",LOG_ERR,"%s %d","Firmware download is failed with status ",dl_status);
#endif
        }

#if defined (FEATURE_RDKB_LED_MANAGER)
    /* Either image download or flashing failed. set previous state */
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
            if (!led_disable) {
            printf("Led Flashing Not Disabled \n");
            if (!syscfg_get(NULL, "redirection_flag", redirFlag, sizeof(redirFlag)) &&
               !syscfg_get(NULL, "CaptivePortal_Enable", captivePortalEnable, sizeof(captivePortalEnable))) {
              if (!strncmp(redirFlag, "true", 4) && !strncmp(captivePortalEnable, "true", 4)) {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_CAPTIVEMODE, 0);
                  }
              } else {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_EVENT, 0);
                  }
              }
            }
            } 
#else
    if(sysevent_fd != -1)
    {
	    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_UPDATE_STOP_EVENT, 0);
    }
#endif
#endif 
    }
    else
    {
        ret = ANSC_STATUS_FAILURE;

        /* Sleeping since the status returned is 500 on immediate status query */
        CcspTraceInfo((" Sleeping to prevent 500 error \n"));
        sleep(10);

        /* Check if the /tmp/wget.log file was created, if not wait an adidtional time */
        if (access("/tmp/wget.log", F_OK) != 0)
        {
            CcspTraceInfo(("/tmp/wget.log doesn't exist. Sleeping an additional 10 seconds \n"));
            sleep(10);
        }
        else
        {
            CcspTraceInfo(("/tmp/wget.log created . Continue ...\n"));
        }

        CcspTraceInfo((" Waiting for FW DL ... \n"));
        while(1)
        {
            dl_status = fwupgrade_hal_get_download_status();

            if(dl_status >= 0 && dl_status <= 100)
                sleep(2);
            else if(dl_status == 200)
            {
                syscfg_set(NULL, "firmwarename", valid_fw);
                syscfg_set_commit(NULL, "FWDWLD_status", "Completed");
#if defined(FEATURE_NETWORK_LOGS)
                syslog_networklog("NETWORK",LOG_ERR,"%s %s","Firmware update successful status with firmware version ",valid_fw);
#endif
                break;
            }
            else if(dl_status >= 400)
            {
                CcspTraceError((" FW DL is failed with status %d \n", dl_status));
#if defined (FEATURE_RDKB_LED_MANAGER)
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
                /* Image download is failed */
	    if (!led_disable) {	
            printf("Led Flashing Not Disabled \n");
            if (!syscfg_get(NULL, "redirection_flag", redirFlag, sizeof(redirFlag)) &&
               !syscfg_get(NULL, "CaptivePortal_Enable", captivePortalEnable, sizeof(captivePortalEnable))) {
              if (!strncmp(redirFlag, "true", 4) && !strncmp(captivePortalEnable, "true", 4)) {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_CAPTIVEMODE, 0);
                  }
              } else {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_EVENT, 0);
                  }
              }
            }
            }
#else
                if(sysevent_fd != -1)
                {
                  sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_EVENT, 0);
                }
#endif
#endif
                syscfg_set_commit(NULL, "FWDWLD_status", "Failed");
            }
        }
#if defined (FEATURE_RDKB_LED_MANAGER) 
        /* we are here because fw download and flashing succeeded . Set previous led state just before reboot*/

#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
	    if (!led_disable) {
            printf("Led Flashing Not Disabled \n");
            if (!syscfg_get(NULL, "redirection_flag", redirFlag, sizeof(redirFlag)) &&
               !syscfg_get(NULL, "CaptivePortal_Enable", captivePortalEnable, sizeof(captivePortalEnable))) {
              if (!strncmp(redirFlag, "true", 4) && !strncmp(captivePortalEnable, "true", 4)) {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_CAPTIVEMODE, 0);
                  }
              } else {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_EVENT, 0);
                  }
              }
            }
          }
#else
        if(sysevent_fd != -1)
        {
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_UPDATE_COMPLETE_EVENT, 0);
	}
#endif
#endif
        CcspTraceInfo((" FW DL is over \n"));

        if (dl_status == 200)
        {
            CcspTraceInfo((" Waiting for reboot ready ... \n"));

            /* Save Transfer complete in PSM for download RPC success scenario */
            SaveTCintoPSM();	    

            while (1)
            {
                ret = fwupgrade_hal_reboot_ready(&reboot_ready_status);

                if (ret == ANSC_STATUS_SUCCESS && reboot_ready_status == 1)
                    break;
                else
                    sleep(5);
            }
            CcspTraceInfo((" Waiting for reboot ready over, setting last reboot reason \n"));

            system("dmcli eRT setv Device.DeviceInfo.X_RDKCENTRAL-COM_LastRebootReason string Forced_Software_upgrade");	    
        }
    }

    ret = ANSC_STATUS_FAILURE;

    syscfg_get(NULL, "FWDWLD_status", sysbuf, sizeof(sysbuf));
    CcspTraceInfo(("Firmware download status - %s\n", sysbuf));

    if (dl_status == 200)
    {
        checkCallStatus();
        ret = fwupgrade_hal_download_reboot_now();

        if(ret == ANSC_STATUS_SUCCESS)
        {
            CcspTraceInfo((" Rebooting the device now!\n"));
        }
        else
        {
            CcspTraceError((" Reboot Already in progress!\n"));
        }
    }

EXIT:
    CcspTraceInfo(("Dropping root permission...\n"));
    init_capability();
    app_caps.caps = NULL;
    app_caps.user_name = NULL;
    drop_root_caps(&app_caps);
    update_process_caps(&app_caps);
    read_capability(&app_caps);
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
if (led_disable == true) {
    fclose(fp);
}
#endif
}

void FwDlAndFR_ThreadFunc()
{
    int dl_status = 0;
    int ret = ANSC_STATUS_FAILURE;
    ULONG reboot_ready_status = 0;
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
    char redirFlag[10]={0};
    char captivePortalEnable[10]={0};
    bool led_disable = false;
    FILE *fp = fopen(HTTP_LED_FLASH_DISABLE_FLAG, "r");
    if(fp != NULL)
            led_disable = true;
#endif    

    pthread_detach(pthread_self());
    /* Gain root privilge before flashing */
    gain_root_privilege();
    /* HAL layer will do downloand and flashing . Set download led */
#if defined (FEATURE_RDKB_LED_MANAGER)
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
    if (!led_disable) {
	    printf("Led Flashing Not Disabled \n");
	    if (sysevent_fd != -1) {
		    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_START_EVENT, 0);
	    }
    } 
#else 
    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_START_EVENT, 0);
#endif    
#endif
    ret = fwupgrade_hal_update_and_factoryreset ();
    if( ret == ANSC_STATUS_FAILURE)
    {
        CcspTraceError((" Failed to start download \n"));
        /* Drop the privilege */
        goto EXIT;
    }
    else
    {
        /* Sleeping since the status returned is 500 on immediate status query */
        CcspTraceInfo((" Sleeping to prevent 500 error \n"));
        sleep(10);

        /* Check if the /tmp/wget.log file was created, if not wait an adidtional time */
        if (access("/tmp/wget.log", F_OK) != 0)
        {
            CcspTraceInfo(("/tmp/wget.log doesn't exist. Sleeping an additional 10 seconds \n"));
            sleep(10);
        }
        else
        {
            CcspTraceInfo(("/tmp/wget.log created . Continue ...\n"));
        }

        CcspTraceInfo((" Waiting for FW DL ... \n"));
        while(1)
        {
            dl_status = fwupgrade_hal_get_download_status();

            if(dl_status >= 0 && dl_status <= 100)
                sleep(2);
            else if(dl_status == 200)
            {
                syscfg_set(NULL, "firmwarename", valid_fw);
                syscfg_set_commit(NULL, "FWDWLD_status", "Completed");
                break;
            }
            else if(dl_status >= 400)
            {
                CcspTraceError((" FW DL is failed with status %d \n", dl_status));
                /* Drop the privilege.*/
#if defined (FEATURE_RDKB_LED_MANAGER)
                // Download failed
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
            if (!led_disable) {
	    printf("Led Flashing Not Disabled \n");
            if (!syscfg_get(NULL, "redirection_flag", redirFlag, sizeof(redirFlag)) &&
               !syscfg_get(NULL, "CaptivePortal_Enable", captivePortalEnable, sizeof(captivePortalEnable))) {
              if (!strncmp(redirFlag, "true", 4) && !strncmp(captivePortalEnable, "true", 4)) {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_CAPTIVEMODE, 0);
                  }
              } else {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_EVENT, 0);
                  }
              }
            }
            }
#else
                if(sysevent_fd != -1)
                {
                    sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_EVENT, 0);
                }
#endif
#endif
                syscfg_set_commit(NULL, "FWDWLD_status", "Failed");
            }
        }

#if defined (FEATURE_RDKB_LED_MANAGER) 
        /* we are here because fw download and flashing succeeded . Set previous led state just before reboot*/
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
	if (!led_disable) {
            printf("Led Flashing Not Disabled \n");
            if (!syscfg_get(NULL, "redirection_flag", redirFlag, sizeof(redirFlag)) &&
               !syscfg_get(NULL, "CaptivePortal_Enable", captivePortalEnable, sizeof(captivePortalEnable))) {
              if (!strncmp(redirFlag, "true", 4) && !strncmp(captivePortalEnable, "true", 4)) {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_CAPTIVEMODE, 0);
                  }
              } else {
                  if (sysevent_fd != -1) {
                      sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_DOWNLOAD_STOP_EVENT, 0);
                  }
              }
            }
          }
#else
        if(sysevent_fd != -1)
        {
            sysevent_set(sysevent_fd, sysevent_token, SYSEVENT_LED_STATE, FW_UPDATE_COMPLETE_EVENT, 0);
        }
#endif	
#endif

        if (dl_status == 200)
        {
            CcspTraceInfo((" Waiting for reboot ready ... \n"));
            while (1)
            {
                ret = fwupgrade_hal_reboot_ready(&reboot_ready_status);

                if (ret == ANSC_STATUS_SUCCESS && reboot_ready_status == 1)
                    break;
                else
                    sleep(5);
            }
        }
        CcspTraceInfo((" Waiting for reboot ready over, setting last reboot reason \n"));


        system("dmcli eRT setv Device.DeviceInfo.X_RDKCENTRAL-COM_LastRebootReason string Forced_Software_upgrade");

        ret = ANSC_STATUS_FAILURE;
        ret = fwupgrade_hal_download_reboot_now();

        if(ret == ANSC_STATUS_SUCCESS)
        {
            CcspTraceInfo((" Rebooting the device now!\n"));
        }
        else
        {
            CcspTraceError((" Reboot Already in progress!\n"));
        }
    }
/* Drop the privilege when flashing error happens. In successful flashing case, CPE is rebooting anyway */
EXIT:
    CcspTraceInfo(("Dropping root permission...\n"));
    init_capability();
    drop_root_caps(&appcaps);
    update_process_caps(&appcaps);
#if defined (FEATURE_RDKB_LED_MANAGER_CAPTIVE_PORTAL)
if (led_disable == true) {
    fclose(fp);
}
#endif    

}

convert_to_validFW(char *fw,char *valid_fw)
{
    char *buff = NULL;
    int buff_len = 0;

#if defined(_LG_MV3_)
    /* Valid FW names
       xxxx.pkgtb
     */
    buff = strstr(fw,".pkgtb");

#else
    /* Valid FW names
       xxxx_20170717081507sdy
       xxxx_20170717081507sdy.bin
       xxxx_20170717081507sdy_signed.bin
       xxxx_20170717081507sdy_signed.bin.ccs
       xxxx_20230114003136sdy_GRT.bin
       xxxx_6.0s11_DEV_sey
     */

    // if the sw to be updated has no "_signed/-signed/.bin" extensions, take it as it is
    if(buff = strstr(fw,"_signed"));
    else if(buff = strstr(fw,"-signed"));
    else if(buff = strstr(fw,".bin"));
#endif

    if(buff)
        buff_len = strlen(buff);

    strncpy(valid_fw,fw,strlen(fw)-buff_len);

    CcspTraceInfo((" Converted image name = %s \n", valid_fw));
}

void FwDlDmlDISetDeferFWDownloadReboot(ULONG* DeferFWDownloadReboot, ULONG uValue)
{
    if (syscfg_set_u_commit(NULL, "DeferFWDownloadReboot", uValue) != 0)
    {
        CcspTraceWarning(("syscfg_set failed\n"));
    }
    else
    {
        *DeferFWDownloadReboot = uValue;
    }
}

void FwDlDmlDIGetDeferFWDownloadReboot(ULONG* puLong)
{
    char buf[12];

    if( 0 == syscfg_get( NULL, "DeferFWDownloadReboot", buf, sizeof( buf ) ) )
    {
        *puLong = atoi(buf);
    }
    else
    {
        CcspTraceWarning(("syscfg_get failed\n"));
    }
}
