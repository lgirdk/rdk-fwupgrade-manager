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

#include "plugin_main_apis.h"
#include "fwupgrademgr_dml.h"
#include "ssp_global.h"

extern PBACKEND_MANAGER_OBJECT               g_pBEManager;


/**********************************************************************  
    caller:     owner of this object 
    prototype: 
        ULONG
        FirmwareUpgrade_GetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pValue,
                ULONG*                      pUlSize
            );
    description:
        This function is called to retrieve string parameter value; 
    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;
                char*                       ParamName,
                The parameter name;
                char*                       pValue,
                The string value buffer;
                ULONG*                      pUlSize
                The buffer of length of string value;
                Usually size of 1023 will be used.
                If it's not big enough, put required size here and return 1;
    return:     0 if succeeded;
                1 if short of buffer size; (*pUlSize = required size)
                -1 if not supported.
**********************************************************************/

ULONG
FirmwareUpgrade_GetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pValue,
        ULONG*                      pUlSize
    )
{
    char DL_Status[128]={0};
    char Protocol[16]={0};
    PDEVICE_INFO pMyObject = (PDEVICE_INFO) g_pBEManager->pDeviceInfo;

    /* check the parameter name and return the corresponding value */
    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareDownloadStatus", TRUE) )
    {
        if(pMyObject->Download_Control_Flag)
        {
            /* collect value */
            FwDlDmlDIGetDLStatus((ANSC_HANDLE)pMyObject, DL_Status);
            if ( strlen(DL_Status) >= *pUlSize )
            {
                *pUlSize = strlen(DL_Status);
                return 1;
            }

            AnscCopyString(pValue, DL_Status);
        }
        return 0;
    }

    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareDownloadProtocol", TRUE) )
    {
        if(pMyObject->Download_Control_Flag)
        {
            /* collect value */
            FwDlDmlDIGetProtocol((ANSC_HANDLE)pMyObject, Protocol);
            if ( strlen(Protocol) >= *pUlSize )
            {
                *pUlSize = strlen(Protocol);
                return 1;
            }

            AnscCopyString(pValue, Protocol);
        }
        return 0;
    }

    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareDownloadURL", TRUE) )
    {
        if(pMyObject->Download_Control_Flag)
        {
            /* collect value */
            if ( strlen(pMyObject->DownloadURL) >= *pUlSize )
            {
                *pUlSize = strlen(pMyObject->DownloadURL);
                return 1;
            }

            AnscCopyString(pValue, pMyObject->DownloadURL);
        }
        return 0;
    }

    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareToDownload", TRUE) )
    {
        if(pMyObject->Download_Control_Flag)
        {
            /* collect value */
            if ( strlen(pMyObject->Firmware_To_Download) >= *pUlSize )
            {
                *pUlSize = strlen(pMyObject->Firmware_To_Download);
                return 1;
            }

            AnscCopyString(pValue, pMyObject->Firmware_To_Download);
        }
        return 0;
    }
    return -1;
}

/**********************************************************************  
    caller:     owner of this object 
    prototype: 
        BOOL
        FirmwareUpgrade_SetParamStringValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                char*                       pString
            );
    description:
        This function is called to set string parameter value; 
    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;
                char*                       ParamName,
                The parameter name;
                char*                       pString
                The updated string value;
    return:     TRUE if succeeded.
**********************************************************************/
BOOL
FirmwareUpgrade_SetParamStringValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        char*                       pString
    )
{
    PDEVICE_INFO pMyObject = (PDEVICE_INFO) g_pBEManager->pDeviceInfo;
    /* check the parameter name and set the corresponding value */
    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareDownloadURL", TRUE))
    {
        if(pMyObject->Download_Control_Flag)
        {
            FwDlDmlDISetURL((ANSC_HANDLE)pMyObject, pString);
        }
        else
        {
            CcspTraceError(("FW DL is not allowed for this image type \n"));
        }
        return 1;
    }

    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareToDownload", TRUE))
    {
        if(pMyObject->Download_Control_Flag)
        {
            FwDlDmlDISetImage((ANSC_HANDLE)pMyObject, pString);
        }
        else
        {
            CcspTraceError(("FW DL is not allowed for this image type \n"));
        }
        return 1;
    }
    return 0;
}


/**********************************************************************
    caller:     owner of this object

    prototype:
        BOOL
        FirmwareUpgrade_GetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL*                       pBool
            );

    description:
        This function is called to retrieve Boolean parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL*                       pBool
                The buffer of returned boolean value;

    return:     TRUE if succeeded.
**********************************************************************/
BOOL
FirmwareUpgrade_GetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL*                       pBool
    )
{
    PDEVICE_INFO pMyObject = (PDEVICE_INFO) g_pBEManager->pDeviceInfo;
    /* check the parameter name and return the corresponding value */
    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareDownloadNow", TRUE) )
    {
         if(pMyObject->Download_Control_Flag)
         {
                *pBool = FALSE;
         }
        return 1;
    }
}

/**********************************************************************
    caller:     owner of this object

    prototype:

        BOOL
        FirmwareUpgrade_SetParamBoolValue
            (
                ANSC_HANDLE                 hInsContext,
                char*                       ParamName,
                BOOL                        bValue
            );

    description:

        This function is called to set BOOL parameter value;

    argument:   ANSC_HANDLE                 hInsContext,
                The instance handle;

                char*                       ParamName,
                The parameter name;

                BOOL                        bValue
                The updated BOOL value;

    return:     TRUE if succeeded.
**********************************************************************/
BOOL
FirmwareUpgrade_SetParamBoolValue
    (
        ANSC_HANDLE                 hInsContext,
        char*                       ParamName,
        BOOL                        bValue
    )
{
    PDEVICE_INFO pMyObject = (PDEVICE_INFO) g_pBEManager->pDeviceInfo;
    /* check the parameter name and set the corresponding value */
    if( AnscEqualString(ParamName, "X_RDKCENTRAL-COM_FirmwareDownloadNow", TRUE))
    {
        if(pMyObject->Download_Control_Flag)
        {
            if(bValue)
            {
                FwDlDmlDIDownloadNow((ANSC_HANDLE)pMyObject);
            }
        }
        else
        {
            CcspTraceError(("FW DL is not allowed for this image type \n"));
        }
        return 1;
    }
    return 0;
}
