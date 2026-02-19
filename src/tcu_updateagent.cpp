/**
 * @file tcu_updateagent.cpp
 *
 * Copyright(c) 2024 Excelfore Corporation, - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Proprietary and confidential.
 * Its use or disclosure, in whole or in part, without
 * written permission of Excelfore Corp. is prohibited.
 *
 */

#include "tcu_updateagent.h"
#include <unistd.h>
#include <memory>
#include <iostream>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>
#include <time.h>

#include <CommonAPI/CommonAPI.hpp>
#include <v1/com/harman/service/DeviceManagerProxy.hpp>
#include <v1/com/harman/service/DeviceManager.hpp>
#include <v1/commonapi/toscore/sdmservice/SelfDiagnosticManager.hpp>
#include <v1/commonapi/toscore/sdmservice/SelfDiagnosticManagerProxy.hpp>
#include <v1/commonapi/toscore/cmsservice/ConfigurationManagerProxy.hpp>
#include <v1/commonapi/toscore/cmsservice/ConfigurationManager.hpp>
#include <v1/commonapi/toscore/cmservice/ContainerManagerProxy.hpp>
#include <v1/commonapi/toscore/cmservice/ContainerManager.hpp>
#include <v0/commonapi/toscore/smservice/SystemManagerProxy.hpp>

#include <error_type/err_type.h>
#include <log/log.h>

#ifdef __cplusplus
extern "C" {
#endif

using ::v1::com::harman::service::OTAProxy;
using ::v1::com::harman::service::OTAProxyDefault;
using ::v1::com::harman::service::OTA;

std::shared_ptr<CommonAPI::Runtime> runtime;
CommonAPI::CallStatus callSTATUS;
CommonAPI::CallInfo callINFO;

int percentage = 0;
int old_percentage = 0;
std::string exit_code;
std::string Informations;
std::string update_type;
std::string v_version;

using namespace ::v1::com::harman::service;
using v1::com::harman::service::DeviceManagerProxy;
using v1::com::harman::service::DeviceManager;

using namespace ::v1::commonapi::toscore::cmsservice;
using namespace ::v1::commonapi::toscore::cmservice;
using namespace ::v0::commonapi::toscore::smservice;

std::shared_ptr<DeviceManagerProxy<> > dmProxy;
std::shared_ptr<OTAProxyDefault> myProxy;
std::shared_ptr<ConfigurationManagerProxy<>> cotaProxy;
std::shared_ptr<ContainerManagerProxy<>> contProxy;
std::shared_ptr<SystemManagerProxy<>> smProxy;

#include <stdio.h>
#include <string.h>
#include <linux/limits.h>

#define MAX_ERROR_MSG_LEN 1024

char software_version[PATH_MAX];
char FOTA_STATE[PATH_MAX];
int flag = 0;

#define E_UA_OK 0

const char* call_status_to_string(CommonAPI::CallStatus status)
{
    switch (status) {
        case CommonAPI::CallStatus::SUCCESS:
			return "SUCCESS";
        case CommonAPI::CallStatus::OUT_OF_MEMORY:
			return "OUT_OF_MEMORY";
        case CommonAPI::CallStatus::NOT_AVAILABLE:
			return "NOT_AVAILABLE";
        case CommonAPI::CallStatus::CONNECTION_FAILED:
			return "CONNECTION_FAILED";
        case CommonAPI::CallStatus::REMOTE_ERROR:
			return "REMOTE_ERROR";
        case CommonAPI::CallStatus::INVALID_VALUE:
			return "INVALID_VALUE";
        case CommonAPI::CallStatus::SUBSCRIPTION_REFUSED:
			return "SUBSCRIPTION_REFUSED";
        case CommonAPI::CallStatus::SERIALIZATION_ERROR:
			return "SERIALIZATION_ERROR";
        case CommonAPI::CallStatus::UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

int set_shared_ptr(int proxy_time_out)
{
    proxy_time_out = 1800000;
	runtime = CommonAPI::Runtime::get();

	std::string domain   = "local";
	std::string instance = "com.harman.service.OTA";
	std::string connection = "ota-manager";

	CommonAPI::CallStatus callStatus;
	CommonAPI::CallInfo callInfo;

	myProxy = runtime->buildProxy<OTAProxy>(domain, instance, connection);

	if (!myProxy) {
		std::cout << "TML : Could not build OTA proxy\n";
		return -1;
	}
	std::cout << "TML : Checking OTA availability!\n";
	int wait_ms = 0;
	const int timeout_ms = proxy_time_out;

	if (proxy_time_out == 0) {
		while (!myProxy->isAvailable()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	} else {
		while (!myProxy->isAvailable() && wait_ms < timeout_ms) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	}

	if (!myProxy->isAvailable()) {
		std::cout << "OTA Proxy not available after waiting " << proxy_time_out <<" seconds" << std::endl;
		return -1;
	}

	std::cout << "TML : OTA Proxy Available...\n";

	return 0;
}

int set_cont_shared_ptr(int proxy_time_out){

(void)proxy_time_out;
	CommonAPI::Runtime::setProperty("LogContext", "CMClient");
	CommonAPI::Runtime::setProperty("LogApplication", "CMClient");
	CommonAPI::Runtime::setProperty("LibraryBase", "CMService");

	runtime = CommonAPI::Runtime::get();

	std::string domain = "local";
	std::string instance = "commonapi.toscore.cmservice.ContainerManager";
	std::string connection = "container-manager";

	contProxy = runtime->buildProxy<ContainerManagerProxy>(domain, instance, connection);

	if (!contProxy) {
		std::cerr << "[ERROR] Failed to create proxy!" << std::endl;
		return -1;
	}


	std::cout << "Checking if Cont Proxy is Available..." << std::endl;
	int contProxyAvailable = contProxy->isAvailable();
	std::cout << "contProxyAvailable: " << contProxyAvailable << std::endl;
#if 0
	int wait_ms = 0;
	const int timeout_ms = proxy_time_out;

	std::cout << "TML : Checking Container Manager Proxy availability!\n";
	if (proxy_time_out == 0) {
		while (!contProxy->isAvailable()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	} else {
		while (!contProxy->isAvailable() && wait_ms < timeout_ms) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	}

	if (!contProxy->isAvailable()) {
		std::cout << "cm Proxy not available after waiting "<< proxy_time_out <<" seconds" << std::endl;
		return -1;
	}

	std::cout << "TML : cm Proxy Available...\n";
#endif

	return 0;
}

int set_cota_shared_ptr(int proxy_time_out)
{
(void)proxy_time_out;
	runtime = CommonAPI::Runtime::get();

	std::string domain = "local";
	std::string instance = "commonapi.toscore.cmsservice.ConfigurationManager";
	std::string connection = "client-sample";

	cotaProxy = runtime->buildProxy<ConfigurationManagerProxy>(domain, instance, connection);

	if (!cotaProxy) {
		std::cerr << "[ERROR] Failed to create proxy!" << std::endl;
		return -1;
    }

	std::cout << "Checking cota availability!" << std::endl;
	int cotaProxyAvailable = cotaProxy->isAvailable();
    std::cout << "cotaProxyAvailable..." <<  cotaProxyAvailable << std::endl;

#if 0
	int wait_ms = 0;
	const int timeout_ms = proxy_time_out;

	std::cout << "Checking cota proxy availability!" << std::endl;
	if (proxy_time_out == 0) {
			while (!cotaProxy->isAvailable()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	} else {
		while (!cotaProxy->isAvailable() && wait_ms < timeout_ms) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	}

	if (!cotaProxy->isAvailable()) {
		std::cout << "cms Proxy not available after waiting "<< proxy_time_out <<" seconds" << std::endl;
		return -1;
	}

	std::cout << "TML : cms Proxy Available...\n";
#endif
	return 0;
}

int set_sm_shared_ptr(int proxy_time_out){
    
	/* Rohit changed here for ticket NIS 766 LINE 250 To 255*/

    std::cout << "System manager Proxy not available value set in CMD"<< proxy_time_out <<" seconds" << std::endl; // Changed By Abhishek

	if (proxy_time_out >= 0) {
        std::cout << "System manager Proxy not available value set in CMD"<< proxy_time_out <<" seconds" << std::endl;   
    }
	else
	{
		proxy_time_out = 1800000;
	}
 
    std::cout << "System manager Proxy timeout set to "<< proxy_time_out << " ms" << std::endl;
	runtime = CommonAPI::Runtime::get();
    // proxy_time_out = 1800000; // Changed by Rohit
    std::string domain = "local";
    std::string instance = "commonapi.toscore.smservice.SystemManager";
    std::string connection = "client-sample";

	smProxy =  runtime->buildProxy<SystemManagerProxy>(domain, instance, connection);

    if (!smProxy) {
        std::cerr << "[ERROR] Failed to create proxy!" << std::endl;
        return -1;
    }

    int wait_ms = 0;
	const int timeout_ms = proxy_time_out;

	std::cout << "Checking sm proxy availability!" << std::endl;
	if (proxy_time_out == 0) {
		while (!smProxy->isAvailable()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	} else {
		while (!smProxy->isAvailable() && wait_ms < timeout_ms) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			wait_ms += 100;
		}
	}

	if (!smProxy->isAvailable()) {
		std::cout << "System manager Proxy not available after waiting "<< proxy_time_out <<" seconds" << std::endl;
		return -1;
	}

	std::cout << "TML : system manager Proxy Available...\n";

	return 0;
}

int tcu_updateagent_init(int hmservice_wait_time, int proxy_time_out)
{
	std::cout<<"TML : START tcu_updateagent_init--------------------------------------------\n";
    
	std::cout<<"Debug : To check proxy_time_out_value inside INIT : "<<proxy_time_out<<std::endl; // Changed By Abhishek

	set_shared_ptr(proxy_time_out);
	set_cont_shared_ptr(proxy_time_out);
	set_cota_shared_ptr(proxy_time_out);
	set_sm_shared_ptr(proxy_time_out);

	if (!myProxy) {
		std::cout << "TML : Could not build OTA proxy\n";
		return -1;
	}

	OTA::ABPartition partition;

	myProxy->GetCurrentActivePartition(callSTATUS, partition, &callINFO);
	std::cout<<"TML : Get Current Partition:"<<partition.toString()<<std::endl;

	OTA::OtaInformation ota_state;
	myProxy->GetOTAState(callSTATUS, ota_state, &callINFO);
	std::cout<<"TML : Get FOTA State Percentage:"<<ota_state.getPercentage()<<std::endl;
	std::cout<<"TML : Get FOTA State Exit State:"<<ota_state.getExit_code().toString()<<std::endl;
	std::cout<<"TML : Get FOTA State Information:"<<ota_state.getOta_state().toString()<<std::endl;

	int myProxyAvailable   = (myProxy   && myProxy->isAvailable())   ? 1 : 0;
	int contProxyAvailable = (contProxy && contProxy->isAvailable()) ? 1 : 0;
	int cotaProxyAvailable = (cotaProxy && cotaProxy->isAvailable()) ? 1 : 0;
	int smProxyAvailable   = (smProxy   && smProxy->isAvailable())   ? 1 : 0;
	std::cout << "contProxyAvailable: " << contProxyAvailable << std::endl;
	std::cout << "myProxyAvailable: " << myProxyAvailable << std::endl;
	std::cout << "cotaProxyAvailable: " << cotaProxyAvailable << std::endl;
	std::cout << "contProxyAvailable: " << contProxyAvailable << std::endl;
	if (myProxyAvailable == 1 && smProxyAvailable == 1 && cotaProxyAvailable == 1 && contProxyAvailable == 1 ){
		std::cout<<"TML : END tcu_updateagent_init--------------------------------------------\n";
		return 1;
	}

	if (!myProxyAvailable)
    std::cout << "  -> OTA Proxy (myProxy) NOT available" << std::endl;

	if (!contProxyAvailable)
		std::cout << "  -> Container Proxy (contProxy) NOT available" << std::endl;

	if (!smProxyAvailable)
		std::cout << "  -> System manager Proxy (smProxy) NOT available" << std::endl;

	if (!cotaProxyAvailable)
		std::cout << "  -> COTA Proxy (cotaProxy) NOT available" << std::endl;

	return -1;
}

int cont_get_version(bool Req,char** responseBuffer, size_t bufferSize){

	std::cout<<"TML : Cont version START--------------------------------------------\n";

	CommonAPI::CallStatus callStatus;
	std::string response;

	contProxy->getContainerVersion(Req,callStatus,response);

	if (callStatus != CommonAPI::CallStatus::SUCCESS) {
		std::cerr << "[ERROR] Failed to call getContainerVersion!" << std::endl;
		return -1;
	}

	std::cout << "Container Version (response): " << response << std::endl;

	if (response.size() >= bufferSize) {
		*responseBuffer = new char[response.size() + 1];
		strncpy(*responseBuffer, response.c_str(), response.size());
		(*responseBuffer)[response.size()] = '\0';
	} else {
		strncpy(*responseBuffer, response.c_str(), bufferSize - 1);
		(*responseBuffer)[bufferSize - 1] = '\0';
	}
	return 0;
}

static inline ConfigurationManager::CotaType
getCotaTypeFromString(const std::string& typePath,bool &success) {
    const auto pos = typePath.find_last_of('/');
    const std::string key = (pos == std::string::npos)
                          ? typePath
                          : typePath.substr(pos + 1);

	success = true;
    if (key == "KEYSTORE_INDIVIDUAL")
        return ConfigurationManager::CotaType::KEYSTORE_INDIVIDUAL;
    if (key == "CONFIGSTORE_INDIVIDUAL")
        return ConfigurationManager::CotaType::CONFIGSTORE_INDIVIDUAL;
    if (key == "KEYSTORE_MULTIPLE")
        return ConfigurationManager::CotaType::KEYSTORE_MULTIPLE;
    if (key == "CONFIGSTORE_MULTIPLE")
        return ConfigurationManager::CotaType::CONFIGSTORE_MULTIPLE;

	std::cout << "Invalid COTA type:  " << key.c_str() << std::endl;
	success = false;
	return ConfigurationManager::CotaType::CONFIGSTORE_INDIVIDUAL;
}

char* cota_get_version(const char* tableName, const char* cotaTypeStr) {
    std::cout << "TML : COTA version START--------------------------------------------\n";

    if (!tableName || !cotaTypeStr) {
        std::cerr << "[ERROR] cota_get_version: null argument(s)\n";
        return nullptr;
    }

    CommonAPI::CallStatus callStatus;
    std::string response;

	bool success = false;
    auto typevalue = getCotaTypeFromString(cotaTypeStr, success);

	if (!success) {
        std::cerr << "[ERROR] Invalid COTA type: '" << cotaTypeStr
                  << "' — not from known CotaType list.\n";
        return nullptr;
    }

    std::cout << "COTA TableName: " << tableName << " | CotaType: " << cotaTypeStr << std::endl;

    cotaProxy->getConfigVersion(std::string(tableName), typevalue, callStatus, response);

    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
        std::cerr << "[ERROR] Failed to call getConfigVersion!" << std::endl;
        return nullptr;
    }

    std::cout << "COTA Version (response): " << response << std::endl;
    return strdup(response.c_str());
}


int readTOSVersion(int reqType, char** responseBuffer, size_t bufferSize) {

	std::cout<<"TML : System Mananger version START--------------------------------------------\n";
	SystemManager::VersionReqType versionReqType(static_cast<SystemManager::VersionReqType::Literal>(reqType));

    CommonAPI::CallStatus callStatus;
    CommonAPI::CallInfo callInfo;
    SystemManager::VersionResponse versionResponse;

    auto startTime = std::chrono::steady_clock::now();
	if (!smProxy) {
        std::cerr << "[ERROR] Failed to create proxy!" << std::endl;
        return -1;
    }

    while (true) {
        smProxy->getTCUVersionInfo(versionReqType, callStatus, versionResponse, &callInfo);

        if (callStatus == CommonAPI::CallStatus::SUCCESS) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::string versionStr = versionResponse.getVersionInfo();
    auto status = versionResponse.getStatus();

    std::cout << "Version Info: " << versionStr << std::endl;
    std::cout << "Read Status: " << static_cast<int>(status) << std::endl;

    if (versionStr.size() >= bufferSize) {
        *responseBuffer = new char[versionStr.size() + 1];
        strncpy(*responseBuffer, versionStr.c_str(), versionStr.size());
        (*responseBuffer)[versionStr.size()] = '\0';
    } else {
        strncpy(*responseBuffer, versionStr.c_str(), bufferSize - 1);
        (*responseBuffer)[bufferSize - 1] = '\0';
    }
	return 0;
}

int container_update(char* container_package,char** responseBuffer, size_t bufferSize){

	std::cerr << "Inside container update!" << std::endl;
	if (container_package == NULL) {
		return -1;
	}

	std::string containerPackage(container_package);
	CommonAPI::CallStatus internalCallStatus;
	CommonAPI::CallInfo callInfo(100000);
	std::string containerUpdateStatusReport;

	contProxy->performContainerUpdate(containerPackage, internalCallStatus, containerUpdateStatusReport, &callInfo);

	std::cerr << "Internal call status: " << (internalCallStatus == CommonAPI::CallStatus::SUCCESS ? "Success" : "Failure") << std::endl;

	if (internalCallStatus != CommonAPI::CallStatus::SUCCESS) {
		std::cerr << "Error performing container update!" << std::endl;
		return -1;
	}

	std::cerr << "[DEBUG] containerUpdateStatusReport: " << containerUpdateStatusReport << std::endl;

	if (containerUpdateStatusReport.size() >= bufferSize) {
        *responseBuffer = new char[containerUpdateStatusReport.size() + 1];
        strncpy(*responseBuffer, containerUpdateStatusReport.c_str(), containerUpdateStatusReport.size());
        (*responseBuffer)[containerUpdateStatusReport.size()] = '\0';
    } else {
        strncpy(*responseBuffer, containerUpdateStatusReport.c_str(), bufferSize - 1);
        (*responseBuffer)[bufferSize - 1] = '\0';
    }

	return 0;
}

int tcu_isAvailable()
{
	return 0;
}

int tcu_StartFotaUpdate(const char* flash_directory, const char* pkg_name, const char* version, char **state, char *error_msg)
{
	std::cout<<"TML :   pkg_path :"<< pkg_name <<std::endl;
	exit_code = "E_NO_ERROR";
	Informations = "FAILED";
	update_type = "NO_UPDATE_AVAILABLE";
	std::cout<<"TML :   version :"<< version <<std::endl;
	static char internal_state[128];

	OTA::OtaExitCode status;

	std::cout << "TML : pkg_path : " << pkg_name << std::endl;
    std::cout << "TML : version   : " << version << std::endl;
    std::cout << "TML : flash_directory : " << flash_directory << std::endl;

	myProxy->selfTCUUpdate(flash_directory, callSTATUS, status, &callINFO);
	std::cout<<"TML : FOTA Update status:"<< status.toString() <<std::endl;

	OTA::OtaInformation ota_state;
	myProxy->GetOTAState(callSTATUS, ota_state, &callINFO);
	std::cout<<"TML : Get FOTA State Exit State:"<<ota_state.getExit_code().toString()<<std::endl;
	exit_code = ota_state.getExit_code().toString();
	std::cout<<"TML : Get FOTA State Information:"<<ota_state.getOta_state().toString()<<std::endl;
	Informations = ota_state.getOta_state().toString();
	std::cout<<"Get FOTA Update Type Information:"<<ota_state.getUpdate_type().toString()<<std::endl;
	update_type = ota_state.getUpdate_type().toString();

	if (status.toString() == "E_INCOMPATIBLE_UPDATE_REQUEST" || 
		status.toString() == "E_UPDATE_PACKAGE_NOEXIST" || 
		status.toString() == "E_DIFFUBIUNATTACH" || 
		status.toString() == "E_BSPATCHFAILED" || 
		status.toString() == "E_NOTFINDPARTITION" || 
		status.toString() == "E_UBIVOLUMEEROR" || 
		status.toString() == "E_IOC_UPDATE_FAILED" || 
		status.toString() == "E_FILENOTEXIST" || 
		status.toString() == "E_UPGRADE_ON_GOING" || 
		status.toString()== "E_FILECHECKFAILED" || 
		status.toString() == "E_DEVICE_UP_TO_DATE" ||
		status.toString() == "E_IOC_DOWNGRADE_REQUEST" || // Changed by Rohit - NIS-771
		status.toString() == "E_NAD_DOWNGRADE_REQUEST"){ // Changed by Rohit - NIS-771

		std::cerr << "[ERROR] FOTA Update failed due to " <<  status.toString() << std::endl;

		if (state != nullptr){
			snprintf(internal_state, sizeof(internal_state),"%s", status.toString());
			*state = internal_state;
			snprintf(error_msg, (PATH_MAX - 1),"ExitCode=%s,state=%s,type=%s", status.toString(), Informations.c_str(), update_type.c_str());
		}
		return -1;
	}

	if(update_type == "INVALID_UPDATE_TYPE"){
		snprintf(error_msg, (PATH_MAX - 1),"ExitCode=%s,state=%s,type:%s", status.toString(), Informations.c_str(), update_type.c_str());
		return -1;
	}

	if(exit_code == "E_DIFFUBIUNATTACH" || exit_code == "E_FILENOTEXIST" || exit_code == "E_UPGRADE_ON_GOING"|| exit_code == "E_BSPATCHFAILED" || exit_code == "E_NOTFINDPARTITION" || exit_code == "E_UBIVOLUMEEROR" || exit_code == "E_IOC_UPDATE_FAILED" || exit_code == "E_INCOMPATIBLE_UPDATE_REQUEST" || exit_code == "E_UPDATE_PACKAGE_NOEXIST"){
		std::cout << "Failed with exit code "<< exit_code << std::endl;
		if (state != nullptr)
		{
			snprintf(internal_state, sizeof(internal_state),"%s", exit_code.c_str());
			*state = internal_state;
			snprintf(error_msg, (PATH_MAX - 1),"ExitCode=%s,state=%s,type=%s", exit_code.c_str(), Informations.c_str(), update_type.c_str());
		}
		return -1;
	}

	std::cout<<"TML : END--------------------------------------------\n";

	return 0;

}

void subscribe_ota_event() {
    myProxy->getEventOtaStateEvent().subscribe(
        [](::v1::com::harman::service::OTA::OtaInformation name) {
            percentage = name.getPercentage();
            exit_code = name.getExit_code().toString();
            Informations = name.getOta_state().toString();
        }
    );
}

int get_ota_percentage() {
    return percentage;
}

const char* get_ota_exit_code() {
    return exit_code.c_str();
}

const char* get_ota_state() {
    return Informations.c_str();
}

const char* tcu_GetFOTAState()
{
	OTA::OtaInformation ota_state;
	myProxy->GetOTAState(callSTATUS, ota_state, &callINFO);
	std::cout<<"TML : Get FOTA State Exit State:"<<ota_state.getExit_code().toString()<<std::endl;
	std::cout<<"TML : Get FOTA State Information:"<<ota_state.getOta_state().toString()<<std::endl;
	std::cout<<"TML : Get FOTA Update Type Information:"<<ota_state.getUpdate_type().toString()<<std::endl;
	return ota_state.getOta_state().toString();
}

const char* tcu_GetFOTAUpdatetype()
{
	OTA::OtaInformation ota_state;
	myProxy->GetOTAState(callSTATUS, ota_state, &callINFO);
	std::cout<<"TML : Get FOTA State Exit State:"<<ota_state.getExit_code().toString()<<std::endl;
	std::cout<<"TML : Get FOTA State Information:"<<ota_state.getOta_state().toString()<<std::endl;
	std::cout<<"TML : Get FOTA Update Type Information:"<<ota_state.getUpdate_type().toString()<<std::endl;
	return ota_state.getUpdate_type().toString();
}

const char* tcu_GetFOTAExitState()
{
	OTA::OtaInformation ota_state;
	myProxy->GetOTAState(callSTATUS, ota_state, &callINFO);
	std::cout<<"TML : Get FOTA State Exit State:"<<ota_state.getExit_code().toString()<<std::endl;
	return ota_state.getExit_code().toString();
}

int tcu_GetCurrentActivePartition()
{
	return 0;
}

int tcu_SwitchPartition()
{
	OTA::OtaExitCode partition_switch;

	myProxy->SwitchPartition(callSTATUS, partition_switch, &callINFO);
	std::cout<<"TML : Switch Partition:"<<partition_switch.toString()<<std::endl;
	return 0;
}
/*
int tcu_SynchronizePartition()
{
	OTA::OtaExitCode _sync_success;

	myProxy->SynchronizePartition(callSTATUS, _sync_success, &callINFO);
	std::cout<<"TML : Sync Partition:"<<_sync_success.toString()<<std::endl;
	return 0;
}
*/

// Changed by Rohit
int tcu_SynchronizePartition(char *buffer, size_t bufferSize)
{
    OTA::OtaExitCode _sync_success;
    myProxy->SynchronizePartition(callSTATUS, _sync_success, &callINFO);
    std::cout << "TML : Sync Partition:" << _sync_success.toString() << std::endl;
    snprintf(buffer, bufferSize, "%s", _sync_success.toString());
    return 0;
}


int tcu_CancelOption()
{
	OTA::OtaExitCode _cancel_success;
	myProxy->selfTCUUpdateCancel(
		OTA::CancelOption::NEXT_UPDATE_FROM_BEGINNING, callSTATUS,
		_cancel_success, &callINFO);
	std::cout << "TML: Cancel Update:" << _cancel_success.toString()<< "Call status:" << call_status_to_string(callSTATUS) <<std::endl;
	return 0;
}

int tcu_getEventOtaStateEvent()
{
	return 0;
}

int check_sm_service_status(){

	if (smProxy == nullptr) {
        return -1;
    }

    if (smProxy->isAvailable()) {
        return 1;
    }
	std::cout << "SM proxy is not available" << std::endl;
    return -1;
}

int check_dmc_service_status(){

	if (dmProxy == nullptr) {
        return -1;
    }

    if (dmProxy->isAvailable()) {
        return 1;
    }
	std::cout << "DM proxy is not available" << std::endl;
    return -1;
}

int TCUDiagnostics(const char *reqType, char** responseBuffer, size_t bufferSize, char *err_msg) {

	using namespace std;
	using namespace ::v1::commonapi::toscore::sdmservice;

	std::string domain = "local";
	std::string instance = "commonapi.toscore.sdmservice.SelfDiagnosticManager";
	std::string connection = "SDM-client";

	std::shared_ptr<CommonAPI::Runtime> runtime = CommonAPI::Runtime::get();

    std::shared_ptr<v1::commonapi::toscore::sdmservice::SelfDiagnosticManagerProxy<>> sdmProxy;

    sdmProxy = runtime->buildProxy<v1::commonapi::toscore::sdmservice::SelfDiagnosticManagerProxy>(domain, instance, connection);
    if(!sdmProxy)
    {
        std::cout << "SDM_CLIENT : Could not build SDM proxy\n";
		snprintf(err_msg, MAX_ERROR_MSG_LEN,"SDM_CLIENT : Could not build SDM proxy");
        return -1;
    }
    std::cout << "SDM: Checking SDM availability!" << std::endl;
    while (!sdmProxy->isAvailable())
    {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    std::cout << "SDM: Available..." << std::endl;
	SelfDiagnosticManager::DiagnosticReqType req_type_enum;

	if (strcmp(reqType, "GET_ALL_TCU_DTC_INFO") == 0) {
		req_type_enum = SelfDiagnosticManager::DiagnosticReqType(SelfDiagnosticManager::DiagnosticReqType::Literal::GET_ALL_TCU_DTC_INFO);
	} else if (strcmp(reqType, "GET_TCU_HEALTH_INFO") == 0) {
		req_type_enum = SelfDiagnosticManager::DiagnosticReqType(SelfDiagnosticManager::DiagnosticReqType::Literal::GET_TCU_HEALTH_INFO);
	} else if (strcmp(reqType, "GET_ALL_TCU_COMPONENT_ERROR") == 0) {
		req_type_enum = SelfDiagnosticManager::DiagnosticReqType(SelfDiagnosticManager::DiagnosticReqType::Literal::GET_ALL_TCU_COMPONENT_ERROR);
	} else if (strcmp(reqType, "GET_ACTIVE_TCU_COMPONENT_ERROR") == 0) {
		req_type_enum = SelfDiagnosticManager::DiagnosticReqType(SelfDiagnosticManager::DiagnosticReqType::Literal::GET_ACTIVE_TCU_COMPONENT_ERROR);
	} else if (strcmp(reqType, "CLEAR_ALL_TCU_DTC") == 0) {
		req_type_enum = SelfDiagnosticManager::DiagnosticReqType(SelfDiagnosticManager::DiagnosticReqType::Literal::CLEAR_ALL_TCU_DTC);
	} else if (strcmp(reqType, "GET_TCU_HEALTH_INFO_AND_HEAL") == 0) {
		req_type_enum = SelfDiagnosticManager::DiagnosticReqType(SelfDiagnosticManager::DiagnosticReqType::Literal::GET_TCU_HEALTH_INFO_AND_HEAL);
	} else {
		std::cerr << "Error: Unknown diagnostic request type: " << reqType << std::endl;
		snprintf(err_msg, MAX_ERROR_MSG_LEN,"Unknown diagnostic request type:%s", reqType);
		return -1;
	}

	CommonAPI::CallStatus callStatus;
	CommonAPI::CallInfo callInfo(300000);
	std::string diagnosticResponse = "";
	do {

		std::cout << "TML:Perform TCU diagnostics..." << std::endl;
		sdmProxy->performTCUDiagnostics(req_type_enum, callStatus, diagnosticResponse, &callInfo);

		if (callStatus == CommonAPI::CallStatus::SUCCESS) {
			std::cout << "Diagnostics call succeeded." << std::endl;
			break;
		} else {
			std::cerr << "Error during diagnostics call: " << static_cast<int>(callStatus) << std::endl;
			snprintf(err_msg, MAX_ERROR_MSG_LEN,"TCU diagnostics failed due to SDM service issue: CommonAPI CallStatus=%s", call_status_to_string(callStatus));
			return -1;
		}

	} while (true);

	if (diagnosticResponse.size() >= bufferSize) {
        *responseBuffer = new char[diagnosticResponse.size() + 1];
        strncpy(*responseBuffer, diagnosticResponse.c_str(), diagnosticResponse.size());
        (*responseBuffer)[diagnosticResponse.size()] = '\0';
        return 0;
    } else {
        strncpy(*responseBuffer, diagnosticResponse.c_str(), bufferSize - 1);
        (*responseBuffer)[bufferSize - 1] = '\0';
        return 0;
    }

	return 0;

}

int updateConfig(const char *Filepath, const char *cotaTypeStr, int &internalCallStatus, int &updateValue) {

	if (!cotaProxy || !cotaProxy->isAvailable()) {
        std::cerr << "[ERROR] COTA Proxy not available!" << std::endl;
        return -1;
    }

    CommonAPI::CallStatus callStatus;
    ConfigurationManager::UpdateDBStatus updateStatus;
    CommonAPI::CallInfo info(100000);

    ConfigurationManager::CotaType typeValue;
	bool typeFound = true;

    if (strcmp(cotaTypeStr, "CONFIGSTORE_INDIVIDUAL") == 0)
        typeValue = ConfigurationManager::CotaType::CONFIGSTORE_INDIVIDUAL;
    else if (strcmp(cotaTypeStr, "KEYSTORE_INDIVIDUAL") == 0)
        typeValue = ConfigurationManager::CotaType::KEYSTORE_INDIVIDUAL;
    else if (strcmp(cotaTypeStr, "CONFIGSTORE_MULTIPLE") == 0)
        typeValue = ConfigurationManager::CotaType::CONFIGSTORE_MULTIPLE;
    else if (strcmp(cotaTypeStr, "KEYSTORE_MULTIPLE") == 0)
        typeValue = ConfigurationManager::CotaType::KEYSTORE_MULTIPLE;
	else {
		std::cerr << "Invalid COTA type: " << cotaTypeStr << std::endl;
		typeFound = false;
	}

	if (!typeFound) {
        std::cerr << "COTA type not recognized — aborting updateConfig().\n";
        return -1;
    }

	std::cout << "Filepath..: " << Filepath << std::endl;
	std::cout << "typevalue..: " << cotaTypeStr << std::endl;
	std::cout << "Enum Value : " << static_cast<int>(typeValue)
              << " (0=KEYSTORE_INDIVIDUAL, 1=CONFIGSTORE_INDIVIDUAL, 2=KEYSTORE_MULTIPLE, 3=CONFIGSTORE_MULTIPLE, 4=RESERVED)"
              << std::endl;

    cotaProxy->updateConfigRequest(std::string(Filepath), typeValue, callStatus, updateStatus, &info);

    std::cout << "[DEBUG] callStatus after updateConfigRequest: "
              << (callStatus == CommonAPI::CallStatus::SUCCESS ? "Success" : "Failure")
              << std::endl;

	internalCallStatus = static_cast<int>(callStatus);
	updateValue = static_cast<int>(updateStatus);

    std::string APICallStatus = (callStatus == CommonAPI::CallStatus::SUCCESS ? "SUCCESS" : "FAILURE");
    std::string updateStatusStr = updateStatus.toString();

    if (APICallStatus == "SUCCESS" && updateStatusStr == "UPDATE_SUCCESS") {
        return 0;
    } else if (APICallStatus == "SUCCESS") {
        return -2;
    } else {
        return -3;
    }
}

int handleCOTAStatusUpdate(const std::string& cotaTypeStr) {

	bool typeValid = false;
	auto typeValue = getCotaTypeFromString(cotaTypeStr, typeValid);
	std::cout << "COTA type string: " << cotaTypeStr << std::endl;

    if (!typeValid) {
        std::cout << "Invalid COTA type string: " << cotaTypeStr << std::endl;
        return -1;
	}

	ConfigurationManager::setCotaStateStatus statusToSend = ConfigurationManager::setCotaStateStatus::SETCOTASTATE_SUCCESS;

    CommonAPI::CallStatus callStatus;

    cotaProxy->setCOTACompletionStatus(typeValue, callStatus, statusToSend);

    if (callStatus != CommonAPI::CallStatus::SUCCESS) {
		std::cout << "CommonAPI call failed: "   << static_cast<int>(callStatus) << std::endl;
		remove(COTA_STATUS_FILE);
        return -1;
    }

	std::cout << "CMS acknowledged COTA completion: SUCCESS\n";

	if (remove(COTA_STATUS_FILE) != 0) {
        std::cout << "Warning: failed to delete COTA status file!" << std::endl;
    }

	return 0;
}

#ifdef __cplusplus
}
#endif
