/**
 * @file tcu_updateagent.h
 *
 * Copyright(c) 2024 Excelfore Corporation, - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Proprietary and confidential.
 * Its use or disclosure, in whole or in part, without
 * written permission of Excelfore Corp. is prohibited.
 *
 */

#include <unistd.h>
#include <memory>
#include <iostream>
#include <string>
#include <CommonAPI/CommonAPI.hpp>
#include <v1/com/harman/service/OTAProxy.hpp>
#ifdef __cplusplus
extern "C" {
#endif

#define COTA_STATUS_FILE "/emmc/misc/data/cota_update_status.json"

int tcu_updateagent_init(int hmservice_wait_time, int proxy_time_out);

int cont_get_version(bool Req,char** responseBuffer, size_t bufferSize);

char* cota_get_version(const char* tableName, const char* cotaTypeStr);

int tcu_isAvailable();

int tcu_StartFotaUpdate(const char* flash_directory, const char* pkg_name, const char* version, char **state, char *err_msg);

//int tcu_GetFOTAState();

int tcu_GetCurrentActivePartition();

int tcu_SwitchPartition();

int tcu_SynchronizePartition(char *buffer, size_t bufferSize);

int tcu_CancelOption();

int tcu_getEventOtaStateEvent();

int sent_ua_progress(const char* pkg_name, char* version, int percent);

int sent_ua_custom_message(const char* pkg_name, char* msg);

int check_dmc_service_status();

int check_sm_service_status();

const char* tcu_GetFOTAUpdatetype();

const char* tcu_GetFOTAExitState();

int TCUDiagnostics(const char *reqType, char** responseBuffer, size_t bufferSize, char *err_msg);

int container_update(char* container_package,char** responseBuffer, size_t bufferSize);

int readTOSVersion(int reqType, char** responseBuffer, size_t bufferSize);

int handleCOTAStatusUpdate(const std::string& cotaTypeStr);

int updateConfig(const char *filePath,
	const char *cotaTypeStr,
	int &internalCallStatus,
	int &updateValue);

void subscribe_ota_event();

int get_ota_percentage();

const char* get_ota_exit_code();

const char* get_ota_state();

#ifdef __cplusplus
}
#endif
