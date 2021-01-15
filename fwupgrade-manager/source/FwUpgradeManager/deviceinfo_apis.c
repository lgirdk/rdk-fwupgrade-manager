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

#include "deviceinfo_apis.h"
#include "ssp_global.h"
#include "fwupgrade_hal.h"

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
    PDEVICE_INFO      pMyObject    = (PDEVICE_INFO)hContext;
    FILE *fp;
    char buff[128]={0};

    if((fp = fopen("/version.txt", "r")) == NULL)
    {
        CcspTraceError(("Error while opening the file version.txt \n"));
        return ANSC_STATUS_FAILURE;
    }

    while(fgets(buff, 128, fp) != NULL)
    {
        if(strstr(buff, "imagename") != NULL)
        {
            int i = 0;
            while((i < sizeof(buff)-10) && (buff[i+10] != '\n') && (buff[i+10] != '\r') && (buff[i+10] != '\0'))
            {
                pMyObject->Current_Firmware[i] = buff[i+10];
                i++;
            }
            pMyObject->Current_Firmware[i] = '\0';
            break;
        }
    }

    if(fp)
        fclose(fp);

    CcspTraceInfo((" Current FW Version is %s \n", pMyObject->Current_Firmware));
    return ANSC_STATUS_SUCCESS;
}


ANSC_STATUS FwDlDmlDIGetDLStatus(ANSC_HANDLE hContext, char *DL_Status)
{
    int dl_status = 0;

    dl_status = fwupgrade_hal_get_download_status();
    CcspTraceInfo((" Download status is %d \n", dl_status));

    if(dl_status == 0)
        AnscCopyString(DL_Status, "Not Started");
    else if(dl_status > 0 && dl_status <= 100)
        AnscCopyString(DL_Status, "In Progress");
    else if(dl_status == 200)
        AnscCopyString(DL_Status, "Completed");
    else if(dl_status >= 400)
        AnscCopyString(DL_Status, "Failed");

    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS FwDlDmlDIGetProtocol(ANSC_HANDLE hContext, char *Protocol)
{
    PDEVICE_INFO     pMyObject = (PDEVICE_INFO)hContext;

    if(strlen(pMyObject->DownloadURL) == 0)
        AnscCopyString(Protocol, "");
    else if(AnscEqualString2(pMyObject->DownloadURL,"https", 5, FALSE))
        AnscCopyString(Protocol, "HTTPS");
    else if(AnscEqualString2(pMyObject->DownloadURL,"http", 4, FALSE))
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
    char valid_fw[128]={0};
    char pHttpUrl[CM_HTTPURL_LEN] = {'0'};

    if(strlen(pMyObject->Firmware_To_Download) && strlen(pMyObject->DownloadURL))
    {
        convert_to_validFW(pMyObject->Firmware_To_Download,valid_fw);
        if(AnscEqualString(valid_fw, pMyObject->Current_Firmware, FALSE))
        {
            CcspTraceError((" Current FW is same, Ignoring request \n"));
            return ANSC_STATUS_FAILURE;
        }

        strcpy(pHttpUrl, "'");
        strncat(pHttpUrl, pMyObject->DownloadURL, CM_HTTPURL_LEN - 1);
        strcat(pHttpUrl, "/");
        strncat(pHttpUrl, pMyObject->Firmware_To_Download, CM_HTTPURL_LEN - 1);
        strcat(pHttpUrl, "'");

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

ANSC_STATUS FwDlDmlDISetURL(ANSC_HANDLE hContext, char *URL)
{
    PDEVICE_INFO     pMyObject = (PDEVICE_INFO)hContext;

    AnscCopyString(pMyObject->DownloadURL, URL);
    CcspTraceInfo((" URL is %s \n", pMyObject->DownloadURL));
    return ANSC_STATUS_SUCCESS;
}

ANSC_STATUS FwDlDmlDISetImage(ANSC_HANDLE hContext, char *Image)
{
    PDEVICE_INFO     pMyObject = (PDEVICE_INFO)hContext;

    AnscCopyString(pMyObject->Firmware_To_Download, Image);
    CcspTraceInfo((" FW DL is %s \n", pMyObject->Firmware_To_Download));
    return ANSC_STATUS_SUCCESS;
}

void FwDl_ThreadFunc()
{
    int dl_status = 0;
    int ret = ANSC_STATUS_FAILURE;
    ULONG reboot_ready_status = 0;

    pthread_detach(pthread_self());

    ret = fwupgrade_hal_download ();
    if( ret == ANSC_STATUS_FAILURE)
    {
        CcspTraceError((" Failed to start download \n"));
        return;
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
                break;
            else if(dl_status >= 400)
            {
                CcspTraceError((" FW DL is failed with status %d \n", dl_status));
                return;
            }
        }

        CcspTraceInfo((" FW DL is over \n"));

        CcspTraceInfo((" Waiting for reboot ready ... \n"));
        while(1)
        {
            ret = fwupgrade_hal_reboot_ready(&reboot_ready_status);

            if(ret == ANSC_STATUS_SUCCESS && reboot_ready_status == 1)
                break;
            else
                sleep(5);
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
}

convert_to_validFW(char *fw,char *valid_fw)
{
    /* Valid FW names
       xxxx_20170717081507sdy
       xxxx_20170717081507sdy.bin
       xxxx_20170717081507sdy_signed.bin
       xxxx_20170717081507sdy_signed.bin.ccs
     */

    char *buff = NULL;
    int buff_len = 0;

    if(buff = strstr(fw,"_signed"));
    else if(buff = strstr(fw,"-signed"));
    else if(buff = strstr(fw,"."));

    if(buff)
        buff_len = strlen(buff);

    strncpy(valid_fw,fw,strlen(fw)-buff_len);

    CcspTraceInfo((" Converted image name = %s \n", valid_fw));
}
