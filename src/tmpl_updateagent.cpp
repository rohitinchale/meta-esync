/**
 * @file tmpl_updateagent.cpp
 *
 * Copyright(c) 2024 Excelfore Corporation, - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Proprietary and confidential.
 * Its use or disclosure, in whole or in part, without
 * written permission of Excelfore Corp. is prohibited.
 *
 */

#include "tmpl_updateagent.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <fstream>
#include "util.h"
#include "utlist.h"
#ifdef USE_JSON_RENAME
#include "json-c-rename.h"
#endif

#include <json-c/json.h>

#include <fcntl.h>
#include <libxl4bus/low_level.h>

#ifdef __cplusplus
extern "C" {
#endif
tcu_handle_t* gHandle = NULL;

#define XL4_SUCCESS ( 0)
#define XL4_ERR     (-1)

char pkg__name[PATH_MAX];
char pkg__version[PATH_MAX];
char ROLLBACK_INFO[PATH_MAX];
char SWITCH_FILE[PATH_MAX];
char REC_FILE[PATH_MAX];
char g_cache_location[PATH_MAX];
char flash_directory[PATH_MAX];
char cont_file_location[PATH_MAX];
char cont_file_name[PATH_MAX];
char cota_file_location[PATH_MAX];
char gDownloadPath[128] = {0};
int wait_time=0;
int hmservice_wait_time=5;
char gContainerName[128] = {0};
char gVersion[64] = {0};
char gType[64] = {0};
char gCheckSum[128] = {0};
char ContainerJson[128] = {0};

char inventory_file_location[PATH_MAX];

int custom_msg_error = E_UA_OK;
int send_current_report_flag = 0;
int timer_count = 0;
bool is_update_timer_expired = false;
bool is_sync_timer_expired = false;
uint32_t update_time_out_value;
uint32_t sync_time_out_value;

int persistent_file_exist(const char* filename)
{
	struct stat buffer;
	int error = XL4_SUCCESS;

	if (stat(filename, &buffer) != XL4_SUCCESS)
	{
		A_INFO_MSG("TML : tcu-ua %s : file not present\n", filename);
		error = XL4_ERR;
	}
	return error;
}

int directory_exists(const char *path) {
    struct stat path_stat;

	if (stat(path, &path_stat) == 0) {
        if (S_ISDIR(path_stat.st_mode)) {
            printf("Directory already exists: %s\n", path);
            return 0;
        } else {
            printf("File exists with same name. Deleting file: %s\n", path);
            if (remove(path) != 0) {
                perror("Failed to remove file");
                return -1;
            }
            printf("File removed successfully. Creating directory...\n");
        }
    } else {
        printf("Directory does not exist. Creating directory: %s\n", path);
    }

	char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);

    if (system(cmd) == 0) {
        printf("Directory created: %s\n", path);
        return 0;
    } else {
        printf("Failed to create directory: %s\n", path);
        return -1;
    }
}

static void send_uds_inventory_reset(void)
{
    json_object *responseObj = json_object_new_object();
    json_object *bodyObj = json_object_new_object();

    json_object_object_add(responseObj, "body", bodyObj);
    json_object_object_add(responseObj, "type", json_object_new_string("esync.uds-ecu-inventory.reset"));

    A_INFO_MSG("TCU: Sending to uds-agent: %s\n", json_object_to_json_string(responseObj));

    xl4bus_address_t *address = NULL;
	std::string msgStr = json_object_to_json_string(responseObj);

    if ((xl4bus_chain_address(&address, XL4BAT_UPDATE_AGENT, "/TML/UDS/", 1) != E_XL4BUS_OK) ||
        (ua_send_message_string_with_address(&msgStr[0], address) != E_UA_OK))
    {
        A_INFO_MSG("TCU: Failed to send 'uds-ecu-inventory.reset' message\n");
        json_object_put(responseObj);
        return;
    }

    json_object_put(responseObj);
    A_INFO_MSG("TCU: Successfully sent 'uds-ecu-inventory.reset' to UDS agent\n");
}

char* read_inventory_file_version_from_file(void) {

    struct json_object *inventory_json = json_object_from_file(inventory_file_location);
    if (!inventory_json) {
        A_INFO_MSG("Failed to parse %s\n", inventory_file_location);
        return NULL;
    }

    struct json_object *file_version_obj = NULL;
    if (!json_object_object_get_ex(inventory_json, "inventory-file-version", &file_version_obj)) {
        A_INFO_MSG("Field 'inventory-file-version' not found, using default version\n");
        json_object_put(inventory_json);
        return strdup("0.0");
    }

    const char *ver_str = json_object_get_string(file_version_obj);
    if (!ver_str) {
        A_INFO_MSG("'inventory-file-version' is null\n");
        json_object_put(inventory_json);
        return NULL;
    }

    char *version_copy = strdup(ver_str);
	if (!version_copy) {
		A_INFO_MSG("Memory allocation failed for version\n");
		json_object_put(inventory_json);
		return NULL;
	}
    json_object_put(inventory_json);
    return version_copy;
}

bool copy_json_file(const char* src, const char* dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;

    std::ofstream out(dst, std::ios::binary);
    if (!out) return false;

    out << in.rdbuf();
    return true;
}


// cppcheck-suppress functionConst
int update_inventory_file(ua_callback_ctl_t* ctl) {

	char download_file[PATH_MAX] = {0};
    snprintf(download_file, PATH_MAX, "%s/inventory.json", g_cache_location);

    char tmp_file[PATH_MAX] = "/emmc/misc/data/ecu_config/.inventory.json.tmp";
    const char backup_file[PATH_MAX] = "/emmc/misc/data/ecu_config/.inventory.json.bak";

    struct json_object* jObj = json_object_from_file(download_file);
    if (!jObj) {
        A_INFO_MSG("Invalid JSON in extracted inventory\n");
		char *inv_msg_err = "Invalid JSON in extracted inventory";
		sent_ua_custom_message(ctl->pkg_name, inv_msg_err);
        return E_UA_ERR;
    }
    json_object_put(jObj);

    if (copy_file(download_file, tmp_file)) {
        A_INFO_MSG("Failed to copy to temp inventory file\n");
		char *cp_file_err = "Failed to copy as .tmp inventory file";
		sent_ua_custom_message(ctl->pkg_name,cp_file_err);
        return E_UA_ERR;
    }

    int fd = open(tmp_file, O_RDWR);
    if (fd < 0) {
        A_INFO_MSG("Failed to open temp inventory file for fsync: %s\n", strerror(errno));
		char *file_open_err = "Failed to open temp inventory file for fsync";
		 sent_ua_custom_message(ctl->pkg_name, file_open_err);
        return E_UA_ERR;
    }
    if (fsync(fd) < 0) {
        A_INFO_MSG("Failed to fsync temp inventory file: %s\n", strerror(errno));
		char *sync_err = "Failed to fsync temp inventory file";
		sent_ua_custom_message(ctl->pkg_name, sync_err);
        close(fd);
        return E_UA_ERR;
    }
    close(fd);

    if (copy_file(inventory_file_location, backup_file) != 0) {
        A_INFO_MSG( "Failed to create backup.\n");
		char *backup_err = "Failed to backup inventory file";
		sent_ua_custom_message(ctl->pkg_name, backup_err);
        remove(tmp_file);
        return E_UA_ERR;
    }

    if (rename(tmp_file, inventory_file_location) != 0) {
        A_INFO_MSG("Failed to move temp inventory to final location\n");
		char *mv_err = "Failed to move temp file to final inventory location";
		sent_ua_custom_message(ctl->pkg_name, mv_err);
        remove(tmp_file);
        return E_UA_ERR;
    }
    sync();

    if (compare_md5sum(download_file, inventory_file_location) != 0) {
        A_INFO_MSG("MD5 checksum mismatch after install, restoring backup\n");
		char *md5_err = "MD5 mismatch after inventory update";
		sent_ua_custom_message(ctl->pkg_name, md5_err);
        copy_file(backup_file, inventory_file_location);
        remove(tmp_file);
        return E_UA_ERR;
    }

    return E_UA_OK;
}

static int store_package_info(const char* file_path, const char* pkgName, const char* version)
{
	int err;
	FILE* fp;

	fp = fopen(file_path, "w+");
	if (fp != NULL)
	{
		A_INFO_MSG("TML : tcu-ua FILE OPEN SUCCESS : %s\r\n", file_path);

		if (strlen(version) == 0) {
			fprintf(fp, "%s\n", pkgName);
		} else {
			fprintf(fp, "%s\n%s\n", pkgName, version);
		}
		fclose(fp);
		err = E_UA_OK;
	}
	else
	{
		A_INFO_MSG("TML : tcu-ua ERROR in store_package_info %s \r\n", file_path);
		err = E_UA_ERR;
	}
	return(err);
}

// cppcheck-suppress functionConst
int remove_cache_dir(ua_callback_ctl_t* ctl)
{
	if(strcmp(ctl->type,"/TML/TCU/TCU") == 0){
		remove_dir(g_cache_location);
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0) {
		if (remove_dir(cont_file_location) == 0) {
        	A_INFO_MSG("Directory '%s' removed successfully.\n", cont_file_location);
   	    } else {
        	A_INFO_MSG("rmdir failed");
    	}
	} else if(strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0){
		if(remove_dir(cota_file_location) == 0) {
			A_INFO_MSG("Directory '%s' removed successfully.\n",cota_file_location);
		} else {
			A_INFO_MSG("rmdir failed");
		}
	} else if(strcmp(ctl->type,"/TML/TCU/ECUI") == 0){
		remove_dir(g_cache_location);
	} else {
		A_INFO_MSG("Type not Registered.\n");
	}
	return 0;
}

char* getVersionFromResponse(const char* containerResponse) {

	struct json_object* jsonResponse = json_tokener_parse(containerResponse);
	if (jsonResponse == NULL) {
		printf("Error parsing container response.\n");
		return NULL;
	}

	struct json_object* containerVersionReport;
	if (json_object_object_get_ex(jsonResponse, "containerVersionReport", &containerVersionReport) &&
		json_object_is_type(containerVersionReport, json_type_array)) {

		struct json_object* containerInfo = json_object_array_get_idx(containerVersionReport, 0);
		if (containerInfo != NULL) {
			struct json_object* containerName;
			struct json_object* version;

			if (json_object_object_get_ex(containerInfo, "ContainerName", &containerName)) {
				strncpy(gContainerName, json_object_get_string(containerName), sizeof(gContainerName) - 1);
			}

			if (json_object_object_get_ex(containerInfo, "Version", &version)) {
				strncpy(gVersion, json_object_get_string(version), sizeof(gVersion) - 1);
				json_object_put(jsonResponse);
				return gVersion;
			}
		}
	}

	json_object_put(jsonResponse);
	return NULL;
}

const char* extractCotaType(const char* fullType) {
    if (!fullType) {
        fprintf(stderr, "extractCotaType: fullType is NULL\n");
        return NULL;
    }

    const char *lastSlash = strrchr(fullType, '/');
    if (!lastSlash || lastSlash == fullType) {
        fprintf(stderr, "extractCotaType: invalid format (no slash found)\n");
        return NULL;
    }

    const char *secondLastSlash = NULL;
    for (const char *p = lastSlash - 1; p > fullType; --p) {
        if (*p == '/') {
            secondLastSlash = p;
            break;
        }
    }

    if (!secondLastSlash || *(secondLastSlash + 1) == '\0') {
        fprintf(stderr, "extractCotaType: unable to find COTA type segment\n");
        return NULL;
    }

    static char cotaType[64];
    memset(cotaType, 0, sizeof(cotaType));

    size_t typeLen = lastSlash - (secondLastSlash + 1);
    if (typeLen >= sizeof(cotaType))
        typeLen = sizeof(cotaType) - 1;

    strncpy(cotaType, secondLastSlash + 1, typeLen);
    cotaType[typeLen] = '\0';

    return cotaType;
}

const char* extractTableName(const char* fullType) {
    if (!fullType) {
        fprintf(stderr, "extractTableName: fullType is NULL\n");
        return NULL;
    }

    const char *lastSlash = strrchr(fullType, '/');
    if (!lastSlash || *(lastSlash + 1) == '\0') {
        fprintf(stderr, "extractTableName: invalid format or empty table name\n");
        return NULL;
    }

    static char tableName[64];
    memset(tableName, 0, sizeof(tableName));

    strncpy(tableName, lastSlash + 1, sizeof(tableName) - 1);
    tableName[sizeof(tableName) - 1] = '\0';

    return tableName;
}

int write_cota_status_if_needed(ua_callback_ctl_t* ctl)
{
    if (!ctl || !ctl->type) {
        A_INFO_MSG("Invalid ctl or ctl->type\n");
        return -1;
    }

    struct json_object *root = json_object_from_file(COTA_STATUS_FILE);

    if (!root) {
        root = json_object_new_object();
        json_object_object_add(root, "is_cota",json_object_new_boolean(1));
        json_object_object_add(root, "type_handler",json_object_new_string(ctl->type));

        json_object_to_file(COTA_STATUS_FILE, root);
        json_object_put(root);
        return 0;
    }

    struct json_object *type_handler;
    const char *existing = NULL;

    if (json_object_object_get_ex(root, "type_handler", &type_handler)) {
        existing = json_object_get_string(type_handler);
    }

    if (!existing || strcmp(existing, ctl->type) != 0) {
        json_object_object_add(root, "type_handler",
                               json_object_new_string(ctl->type));
        json_object_to_file(COTA_STATUS_FILE, root);
    }

    json_object_put(root);
    return 0;
}


static int get_tcu_version(ua_callback_ctl_t* ctl)
{
	A_INFO_MSG("TML : --- tcu-ua get_version\n");
	A_INFO_MSG("TML : --- type %s and pkgname  %s \n", ctl->type,ctl->pkg_name);

	if(strcmp(ctl->type,"/TML/TCU/TCU") == 0) {
		ua_config_rollback_with_empty_backup(ctl->pkg_name, 1, 0);

		int check_sm = 0;
		char staticBuffer[128];
		char* versionInfo = staticBuffer;

		check_sm = check_sm_service_status();

		if(check_sm == 1){
			int reqType = 0;
			int result = readTOSVersion(reqType, &versionInfo, sizeof(staticBuffer));

			if(result == 0) {
				if ( versionInfo == NULL) {
					char *ver_msg = "tcu-ua failed to read version from version API";
					int custom_msg_error = sent_ua_custom_message(ctl->pkg_name, ver_msg);
					if (custom_msg_error != E_UA_OK) {
						A_INFO_MSG("TML : tcu-ua ua_set_custom_message error/failed : %d\n", custom_msg_error);
					}
					return E_UA_ERR;
				}
				ctl->version = strdup(versionInfo);
				if (versionInfo != staticBuffer) {
					delete[] versionInfo;
				}
			} else {
				A_INFO_MSG("Failed to get T.OS version\n");
				char *ver_err = "Failed to get T.OS version";
				sent_ua_custom_message(ctl->pkg_name,ver_err);
				return E_UA_ERR;
			}
			return E_UA_OK;
		} else {
			A_INFO_MSG("TML : System manager is not running\n");
			if(versionInfo[0] == '\0'){
				char *sm_msg = "failure : System manager is not running.";
				custom_msg_error = sent_ua_custom_message(ctl->pkg_name,sm_msg);

				if (custom_msg_error != E_UA_OK) {
					A_INFO_MSG("TML : tcu-ua ua_set_custom_message error/failed : %d\n", custom_msg_error);
				}
				return E_UA_ERR;
			}
			return E_UA_ERR;
		}
		return E_UA_OK;
	} else if (strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0) {

		A_INFO_MSG("TML: COTA get version..\n");
		ua_config_rollback_with_empty_backup(ctl->pkg_name, 1, 0);

		if (write_cota_status_if_needed(ctl) != 0) {
			A_INFO_MSG("Failed to write/update COTA status\n");
		}

		const char *fullType = ctl->type;
		const char *cotaTypeStr = extractCotaType(fullType);
		const char *tableName = extractTableName(fullType);

		if (!cotaTypeStr || !tableName) {
			A_INFO_MSG("COTA type or table name parsing FAILED\n");
			char *cota_fail = "COTA type or table name parsing FAILED";
			sent_ua_custom_message(ctl->pkg_name,cota_fail);
			return INSTALL_FAILED;
		}

		A_INFO_MSG("Extracted COTA Type   = %s", cotaTypeStr);
		A_INFO_MSG("Extracted Table Name  = %s", tableName);

		ctl->version = cota_get_version(tableName, cotaTypeStr);

		if (ctl->version == NULL) {
			A_INFO_MSG("TML : COTA version is not available. Setting default version");
			ctl->version = strdup("dummy");
		}

		return E_UA_OK;
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0) {
		A_INFO_MSG("TML: Container get version..\n");
		ua_config_rollback_with_empty_backup(ctl->pkg_name, 1, 0);
		bool req = true;
		char staticbuffer[4096];
		char* containerResponse = staticbuffer;

		int result = cont_get_version(req,&containerResponse, sizeof(staticbuffer));

		if(result == 0){
			A_INFO_MSG("Received container details %s\n",containerResponse);
			const char* version = getVersionFromResponse(containerResponse);

			if (version != NULL) {
				A_INFO_MSG("Extracted Version: %s\n", version);
				ctl->version = strdup(version);
       		} else {
				A_INFO_MSG("Failed to extract Version from container response setting default version.\n");
				version = "1.0";
				ctl->version = strdup(version);
       		}

			if (containerResponse != staticbuffer) {
				delete[] containerResponse;
			}
			return E_UA_OK;
		} else {
			A_INFO_MSG("Failed to get container version\n");
        	return E_UA_ERR;
		}
	}  else if (strncmp(ctl->type, "/TML/TCU/ECUI", strlen("/TML/TCU/ECUI")) == 0) {
		A_INFO_MSG("TML: ECU get version..\n");
		ua_config_rollback_with_empty_backup(ctl->pkg_name, 1, 1);
		char *version = read_inventory_file_version_from_file();
		A_INFO_MSG("TML: ctl->version in on_get_version before assigning..%s\n\n",ctl->version);
		if (version) {
			ctl->version = version;
			return E_UA_OK;
		} else {
			A_INFO_MSG("Failed to read inventory-file-version\n");
			return E_UA_ERR;
		}
	} else {
		A_INFO_MSG("type handler not registered..\n");
		return E_UA_ERR;
	}
}

static install_state_t do_tcu_pre_install(ua_callback_ctl_t* ctl)
{
	if(strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0){
		A_INFO_MSG(" TML : tcu-ua do_pre_install\n");
		XL4_UNUSED(ctl);
		return INSTALL_IN_PROGRESS;
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0) {
		A_INFO_MSG(" TML : tcu-ua do_pre_install\n");
		XL4_UNUSED(ctl);
		return INSTALL_IN_PROGRESS;
	} else if (strncmp(ctl->type, "/TML/TCU/ECUI", strlen("/TML/TCU/ECUI")) == 0) {
		A_INFO_MSG(" TML : tcu-ua do_pre_install\n");
		XL4_UNUSED(ctl);
		return INSTALL_IN_PROGRESS;
	} else {
		A_INFO_MSG("Type not Registerd..\n");
		return INSTALL_FAILED;
	}
}

int parseContainerDetailsFromFile(const char* filename) {

    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        A_INFO_MSG("Error opening file\n");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char* fileContent = static_cast<char*>(malloc(fileSize + 1));
    if (fileContent == NULL) {
        A_INFO_MSG("Memory allocation failed\n");
        fclose(file);
        return -1;
    }

    size_t bytesRead = fread(fileContent, 1, fileSize, file);
    fileContent[bytesRead] = '\0';
    fclose(file);

    struct json_object* jsonResponse = json_tokener_parse(fileContent);
    free(fileContent);
    if (jsonResponse == NULL) {
        printf("Error parsing JSON file.\n");
        return -1;
    }

    struct json_object* containerDetails;
    if (json_object_object_get_ex(jsonResponse, "containerPackage", &containerDetails) &&
        json_object_is_type(containerDetails, json_type_array)) {

        struct json_object* containerInfo = json_object_array_get_idx(containerDetails, 0);
        if (containerInfo != NULL) {
            struct json_object* type;
            struct json_object* checkSum;
			struct json_object* version;
			struct json_object* fullcontainerName;
			struct json_object* containerName;

            if (json_object_object_get_ex(containerInfo, "Type", &type)) {
                strncpy(gType, json_object_get_string(type), sizeof(gType) - 1);
                gType[sizeof(gType) - 1] = '\0';
            } else {
                A_INFO_MSG("Type field not found\n");
            }

            if (json_object_object_get_ex(containerInfo, "CheckSum", &checkSum)) {
                strncpy(gCheckSum, json_object_get_string(checkSum), sizeof(gCheckSum) - 1);
                gCheckSum[sizeof(gCheckSum) - 1] = '\0';
            } else {
                A_INFO_MSG("CheckSum field not found\n");
            }

			if (json_object_object_get_ex(containerInfo, "Version", &version)) {
				strncpy(gVersion, json_object_get_string(version), sizeof(gVersion) - 1);
				gCheckSum[sizeof(gVersion) - 1] = '\0';
			} else {
				A_INFO_MSG("version field not found\n");
			}

			if (json_object_object_get_ex(containerInfo, "ContainerName", &fullcontainerName)) {
				strncpy(gContainerName, json_object_get_string(fullcontainerName), sizeof(gContainerName) - 1);
				gCheckSum[sizeof(gContainerName) - 1] = '\0';
			} else {
				A_INFO_MSG("version field not found\n");
			}

			if (json_object_object_get_ex(containerInfo, "ContainerName", &containerName)) {
				const char* fullName = json_object_get_string(containerName);
				const char* colon = strchr(fullName, ':');

				if (colon) {
					snprintf(ContainerJson, colon - fullName + 1, "%s", fullName);
				} else {
					strncpy(ContainerJson, fullName, sizeof(ContainerJson) - 1);
					ContainerJson[sizeof(ContainerJson) - 1] = '\0';
				}
			} else {
				A_INFO_MSG("ContainerName field not found\n");
			}

            json_object_put(jsonResponse);
            return 0;
        }
    }

    json_object_put(jsonResponse);
    A_INFO_MSG("Invalid or missing containerPackage array\n");
    return -1;
}

char* createJsonMessage(const char* ContainerName) {

    struct json_object* UpdateReqjson = json_object_new_object();

    struct json_object* containerPackageArray = json_object_new_array();
    struct json_object* containerObject = json_object_new_object();
	snprintf(gDownloadPath, sizeof(gDownloadPath), "/emmc/misc/data/container_update/%s.tar", ContainerName);

    json_object_object_add(containerObject, "ContainerName", json_object_new_string(gContainerName));
    json_object_object_add(containerObject, "Version", json_object_new_string(gVersion));
    json_object_object_add(containerObject, "Type", json_object_new_string(gType));
    json_object_object_add(containerObject, "DownloadPath", json_object_new_string(gDownloadPath));
    json_object_object_add(containerObject, "CheckSum", json_object_new_string(gCheckSum));

    json_object_array_add(containerPackageArray, containerObject);

    json_object_object_add(UpdateReqjson, "containerPackage", containerPackageArray);

    const char* jsonString = json_object_to_json_string_ext(UpdateReqjson, JSON_C_TO_STRING_PRETTY);

    char* result = strdup(jsonString);

    json_object_put(UpdateReqjson);

    return result;
}

int update_timer_cb(void *arg) {
	tml_timer_t *t = (tml_timer_t *)arg;
    if (timer_count-- == 1) {
        if (t->timer.fd > 0) {
            diag_timer_delete(t->timerHead, t->timer.fd);
        }
        A_INFO_MSG("Timer expired, Calling Cancle Update API...\n");
		tcu_CancelOption();
		is_update_timer_expired = true;
    }
    return 0;
}

int sync_timer_cb(void *arg) {
	tml_timer_t *t = (tml_timer_t *)arg;
    if (timer_count-- == 1) {
        if (t->timer.fd > 0) {
            diag_timer_delete(t->timerHead, t->timer.fd);
        }
		is_sync_timer_expired = true;
        A_INFO_MSG("TML Synchronizing partition Timer expired...\n");
    }
    return 0;
}

void initiate_timer(tml_timer_t *t, uint32_t timeout , char *type, int (*cb)(void *arg)) {

	t->timerHead = gHandle->timerHead;
	t->timer.count_ = 2;
	t->timer.user_data_ = (void *)t;
	t->timer.timer_internal_ = timeout;
	t->timer.name = type;
	t->timer.timer_cb_ = cb;
	timer_count = t->timer.count_;
    is_update_timer_expired = false;
    is_sync_timer_expired = false;
}

void clear_timer(tml_timer_t *timer_info, bool *is_timer_expired)
{
    if (timer_info == NULL) {
		A_INFO_MSG("TML : Timer is NULL\n");
        return;
    }

	if (timer_info->timer.fd > 0) {
    	diag_timer_delete(timer_info->timerHead, timer_info->timer.fd);
	}
    *is_timer_expired = false;
	A_INFO_MSG("TML : Timer cleared \n");
	return;
}

static install_state_t do_tcu_install(ua_callback_ctl_t* ctl)
{
	A_INFO_MSG("TML : do_tcu_install");
	ua_clear_custom_message(ctl->pkg_name);

	if(strcmp(ctl->type,"/TML/TCU/TCU") == 0){

		if (tcu_GetFOTAState() != NULL && !strcmp("UPDATE_COMPLETE_SWITCH_PENDING", tcu_GetFOTAState()))
		{
			A_INFO_MSG("TML : Fota state is %s hence switching partition\n",tcu_GetFOTAState());
			store_package_info(SWITCH_FILE, ctl->pkg_name, "");
			sleep(2);
			tcu_SwitchPartition();
			sleep(60);
		}

		char Buffer[128];
		char installed_version[128] = {0};
   		char* current_ver = Buffer;
		int result = readTOSVersion(0, &current_ver, sizeof(Buffer));
		if(result == 0){
			strncpy(installed_version, current_ver, sizeof(installed_version) - 1);
			installed_version[sizeof(installed_version) - 1] = '\0';
			if (current_ver != Buffer) {
				delete[] current_ver;
			}
		} else {
			A_INFO_MSG("Failed to get T.OS version\n");
			char *tos_ver_err = "Failed to get T.OS version";
			sent_ua_custom_message(ctl->pkg_name, tos_ver_err);
			return INSTALL_FAILED;
		}

		if(strlen(installed_version) == 0){
			A_INFO_MSG("TML : System Manager is not running\n");
			return INSTALL_FAILED;
		}

		if (tcu_GetFOTAState() != NULL && !strcmp("ACTIVE_SYSTEM_SWITCHED_SYNC_PENDING", tcu_GetFOTAState()))   {
			A_INFO_MSG("TML : Current TCU state is %s\n", tcu_GetFOTAState());
			char sync_buffer[64] = {0};
			tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
			tml_timer_t t;
			initiate_timer(&t, sync_time_out_value, SYNC_TIMER, sync_timer_cb);
			if (diag_timer_add(t.timerHead, &t.timer) == 0) {
				A_INFO_MSG("TML : Timer added successfully\n");
			} else {
				A_INFO_MSG("TML : Timer addition failed\n");
			}
			while (	!is_sync_timer_expired)
				{
					A_INFO_MSG("TML : Synchronizing partition state to trigger FotaUpdate.... !\n");
					if (tcu_GetFOTAState() != NULL && !strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState()))
					{
						clear_timer(&t, &is_sync_timer_expired);
						A_INFO_MSG("sync completed...\n");
						break;
					}
					if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
					{
						clear_timer(&t, &is_sync_timer_expired);
						A_INFO_MSG("sync failed...\n");
						break;
					}
					sleep(1);
				}
				if(is_sync_timer_expired && tcu_GetFOTAState() != NULL && strcmp("FAILED", tcu_GetFOTAState()) && strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState())){
					A_INFO_MSG("TML : Synchronizing partition failed because of timeout of %d mins\n", sync_time_out_value);
					char sync_failure[128];
					snprintf(sync_failure,sizeof(sync_failure),"Synchronizing partition failed because of timeout of %d secs",sync_time_out_value);
					sent_ua_custom_message(ctl->pkg_name, sync_failure);
					clear_timer(&t, &is_sync_timer_expired);
					remove_cache_dir(ctl);
					return INSTALL_FAILED;
				}
				if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
				{
					A_INFO_MSG("TML : Synchronizing partition failed\n");
					char *sync_fail = "Synchronizing partition failed";
					sent_ua_custom_message(ctl->pkg_name, sync_fail);
					return INSTALL_FAILED;
				}
		}

		A_INFO_MSG("TML : Checking file exists");

		int ret =  persistent_file_exist(ctl->pkg_path);

		if (ret == XL4_SUCCESS) {

			A_INFO_MSG("Downloaded file exists..!\n");

			remove_dir(g_cache_location);

			ret = ua_unzip(ctl->pkg_path, g_cache_location);
			if (ret < 0) {
				char *pkg_msg = "tcu-ua unable to unzip the package from package path";
				custom_msg_error = sent_ua_custom_message(ctl->pkg_name,pkg_msg);
				if (custom_msg_error != E_UA_OK) {
					A_INFO_MSG("TML : tcu-ua ua_set_custom_message error/failed : %d\n", custom_msg_error);
				}
				remove_cache_dir(ctl);
				return INSTALL_FAILED;
			}

			char update_zip[PATH_MAX];
			char update_file[PATH_MAX];
			char SWDL_file1[PATH_MAX];
			char SWDL_file2[PATH_MAX];
			char *error_state = NULL;

			snprintf(update_zip, (PATH_MAX-1), "%s/DeltaPackage.zip", g_cache_location);

			char flash_update_zip[PATH_MAX];
			char flash_update_file[PATH_MAX]; //Changed by Rohit
			snprintf(flash_update_zip, (PATH_MAX-1), "%s/DeltaPackage.zip", flash_directory);
			snprintf(flash_update_file, (PATH_MAX-1), "%s/DeltaPackage", flash_directory); //Changed by Rohit
			snprintf(update_file, (PATH_MAX-1), "%s/DeltaPackage/update.zip",flash_directory);
			snprintf(SWDL_file1, (PATH_MAX-1), "%s/DeltaPackage/IOC_SWDL/TATA_5G_TCU_AS_IOC_AppA.bin",flash_directory);
			snprintf(SWDL_file2, (PATH_MAX-1), "%s/DeltaPackage/IOC_SWDL/TATA_5G_TCU_AS_IOC_AppB.bin",flash_directory);

			ret = persistent_file_exist(update_zip);
			if ( ret == XL4_SUCCESS) {
				char mv_update[PATH_MAX];
				char rm_delta[PATH_MAX];
				char error_msg[PATH_MAX] = {0};

				snprintf(rm_delta, (PATH_MAX - 1), "rm -rf %s/DeltaPackage.zip", flash_directory);

				int zip_exists = persistent_file_exist(flash_update_zip); // Changed by Rohit
				if(f_is_dir(flash_update_file) == 0){
					A_INFO_MSG("TML: Old DeltaPackage Dir is present, Removing it");
					remove_dir(flash_update_file);
				}

				if (zip_exists == XL4_SUCCESS) { // Changed by Rohit
					system(rm_delta);
					A_INFO_MSG("TML: Removed old DeltaPackage.zip file.");
				}

				snprintf(mv_update, (PATH_MAX - 1), "mv %s %s", update_zip, flash_update_zip);
				system(mv_update);
				ret = persistent_file_exist(flash_update_zip);
				if ( ret == XL4_SUCCESS) {
					if ( (strcmp(tcu_GetFOTAExitState(), "E_UPDATE_PACKAGE_NOEXIST")) ) {
						if (tcu_GetFOTAState() != NULL && (strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState())) || (! strcmp("FAILED", tcu_GetFOTAState())) ) {
							tml_timer_t t;
							initiate_timer(&t, sync_time_out_value, SYNC_TIMER, sync_timer_cb);
							A_INFO_MSG("TML : Synchronizing partition\n");


							// Changed by Rohit from here
							char sync_buffer[64] = {0};

							tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
							A_INFO_MSG("ROHIT:Received error code from sync API %s",sync_buffer);

							if (strcmp(sync_buffer, "E_NO_ERROR") != 0)
							{
								A_INFO_MSG("TML : Sync Partition failed to start: %s\n", sync_buffer);

								char sync_failure[128];
								snprintf(sync_failure, sizeof(sync_failure),
										"Synchronizing partition failed to start. Error: %s",
										sync_buffer);

								sent_ua_custom_message(ctl->pkg_name, sync_failure);
								remove_cache_dir(ctl);
								return INSTALL_FAILED;
							} else {
								if (diag_timer_add(t.timerHead, &t.timer) == 0) {
									A_INFO_MSG("TML : Timer added successfully\n");
								} else {
									A_INFO_MSG("TML : Timer addition failed\n");
								}
								while (!is_sync_timer_expired)
								{
									A_INFO_MSG("TML : Synchronizing partition state .... !\n");
									if (tcu_GetFOTAState() != NULL && !strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState()))
									{
										clear_timer(&t, &is_sync_timer_expired);
										A_INFO_MSG("sync completed...\n");
										break;
									}
									if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
									{
										clear_timer(&t, &is_sync_timer_expired);
										A_INFO_MSG("sync failed...\n");
										break;
									}
									sleep(1);
								}
							}
							// to here

							if(is_sync_timer_expired && tcu_GetFOTAState() != NULL && strcmp("FAILED", tcu_GetFOTAState()) && strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState())){
								A_INFO_MSG("TML : Synchronizing partition failed because of timeout of %d mins\n", sync_time_out_value);
								char sync_failure[128];
								snprintf(sync_failure,sizeof(sync_failure),"Synchronizing partition failed because of timeout of %d mins",sync_time_out_value);
								sent_ua_custom_message(ctl->pkg_name, sync_failure);
								clear_timer(&t, &is_sync_timer_expired);
								remove_cache_dir(ctl);
								return INSTALL_FAILED;
							}
							if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
							{
								A_INFO_MSG("TML : Synchronizing partition failed\n");
								char *sync_failure = "Synchronizing partition failed";
								sent_ua_custom_message(ctl->pkg_name, sync_failure);
								remove_cache_dir(ctl);
								return INSTALL_FAILED;
							}
						}
					}
					A_INFO_MSG("TML : calling tcu_StartFotaUpdate\n");
					int res = tcu_StartFotaUpdate(flash_update_zip, ctl->pkg_name, ctl->version, &error_state, error_msg);

					if(res == -1) {
						if (persistent_file_exist(SWDL_file1) !=0 || persistent_file_exist(SWDL_file2) !=0 || persistent_file_exist(update_file) != 0){
							snprintf(error_msg,sizeof(error_msg),"Update is failed due to error code %s",error_state);
							sent_ua_custom_message(ctl->pkg_name, error_msg);
						} else {
							snprintf(error_msg,sizeof(error_msg),"Update is failed due to error code %s",error_state);
							sent_ua_custom_message(ctl->pkg_name, error_msg);
						}
						remove_cache_dir(ctl);
						return INSTALL_FAILED;
					}
					A_INFO_MSG("Subscribing OTA event\n");
					tml_timer_t t;
					initiate_timer(&t, update_time_out_value, UPDATE_TIMER, update_timer_cb);
					if (diag_timer_add(t.timerHead, &t.timer) == 0) {
						A_INFO_MSG("TML : Timer added successfully\n");
					} else {
						A_INFO_MSG("TML : Timer addition failed\n");
					}
					subscribe_ota_event();
					while (!is_update_timer_expired)
					{
						A_INFO_MSG("TML : checking the Partition status\n");
						int perc = get_ota_percentage();
						printf("OTA percentage: %d\n", perc);

						const char *state = tcu_GetFOTAState();

						sent_ua_progress(ctl->pkg_name, ctl->version, perc);
						sleep(1);
						if (state != NULL) {
							A_INFO_MSG("Received state %s\n",state);
						}
						if (tcu_GetFOTAState() != NULL && !strcmp("UPDATE_COMPLETE_SWITCH_PENDING", tcu_GetFOTAState()) && perc == 100)
						{
							A_INFO_MSG("TML : Updated InActive Partition\n");
							clear_timer(&t, &is_update_timer_expired);
							break;
						} else if(state != NULL && !strcmp("UPDATE_IOC_FAILED", state)) {
							clear_timer(&t, &is_update_timer_expired);
							A_INFO_MSG("TML  : Update failed to E_IOC_UPDATE_FAILED");
							if(!strcmp("NAD_IOC", tcu_GetFOTAUpdatetype())) {
								char sync_buffer[64] = {0};
								tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
								initiate_timer(&t, sync_time_out_value, SYNC_TIMER, sync_timer_cb);
								if (diag_timer_add(t.timerHead, &t.timer) == 0) {
									A_INFO_MSG("TML : Timer added successfully\n");
								} else {
									A_INFO_MSG("TML : Timer addition failed\n");
								}
								while (!is_sync_timer_expired)
								{
									A_INFO_MSG("TML : checking the Synchronizing partition state\n");
									if (tcu_GetFOTAState() != NULL && !strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState()))
									{
										clear_timer(&t, &is_sync_timer_expired);
										A_INFO_MSG("sync completed...\n");
										break;
									}
									if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
									{
										clear_timer(&t, &is_sync_timer_expired);
										A_INFO_MSG("sync failed...\n");
										break;
									}
									sleep(1);
								}
								if(is_sync_timer_expired && tcu_GetFOTAState() != NULL && strcmp("FAILED", tcu_GetFOTAState()) && strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState())){
									A_INFO_MSG("TML : Synchronizing partition failed because of timeout of %d mins\n", sync_time_out_value);
									char sync_failure[128];
									snprintf(sync_failure,sizeof(sync_failure),"Synchronizing partition failed because of timeout of %d mins",sync_time_out_value);
									sent_ua_custom_message(ctl->pkg_name, sync_failure);
									clear_timer(&t, &is_sync_timer_expired);
									remove_cache_dir(ctl);
									return INSTALL_FAILED;
								}
								if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
								{
									A_INFO_MSG("TML : Synchronizing partition failed\n");
									char *sync_failure = "Synchronizing partition failed";
									sent_ua_custom_message(ctl->pkg_name, sync_failure);
									remove_cache_dir(ctl);
									return INSTALL_FAILED;
								}
								char *ioc_fail = "Update failed with error code E_IOC_UPDATE_FAILED";
								sent_ua_custom_message(ctl->pkg_name,ioc_fail);
								return INSTALL_FAILED;
							} else {
								char *ioc_fail = "Update failed with error code E_IOC_UPDATE_FAILED";
								sent_ua_custom_message(ctl->pkg_name,ioc_fail);
								return INSTALL_FAILED;
							}
						} else if(tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState())) {
							clear_timer(&t, &is_update_timer_expired);
							if (persistent_file_exist(update_file) != 0 && (!strcmp("NAD_ONLY", tcu_GetFOTAUpdatetype()) || !strcmp("NAD_IOC", tcu_GetFOTAUpdatetype()))){
								A_INFO_MSG("TML : FOTA State Exit State:E_UPDATE_PACKAGE_NOEXIST \n");
								char sync_buffer[64] = {0};
								tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
								A_INFO_MSG("ROHIT:Received error code from sync API %s",sync_buffer);

								if (strcmp(sync_buffer, "E_NO_ERROR") != 0)
								{
									A_INFO_MSG("TML : Sync Partition failed to start: %s\n", sync_buffer);

									char sync_failure[128];
									snprintf(sync_failure, sizeof(sync_failure),
											"Synchronizing partition failed to start. Error: %s",
											sync_buffer);

									sent_ua_custom_message(ctl->pkg_name, sync_failure);
									remove_cache_dir(ctl);
									return INSTALL_FAILED;
								} else {
									initiate_timer(&t, sync_time_out_value, SYNC_TIMER, sync_timer_cb);
									if (diag_timer_add(t.timerHead, &t.timer) == 0) {
										A_INFO_MSG("TML : Timer added successfully\n");
									} else {
										A_INFO_MSG("TML : Timer addition failed\n");
									}
									while (!is_sync_timer_expired)
									{
										A_INFO_MSG("TML : checking the Synchronizing partition state\n");
										if (tcu_GetFOTAState() != NULL && !strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState()))
										{
											clear_timer(&t, &is_sync_timer_expired);
											A_INFO_MSG("sync completed...\n");
											break;
										}
										if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
										{
											clear_timer(&t, &is_sync_timer_expired);
											A_INFO_MSG("sync failed...\n");
											break;
										}
										sleep(1);
									} 
								}
								if(is_sync_timer_expired && tcu_GetFOTAState() != NULL && strcmp("FAILED", tcu_GetFOTAState()) && strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState())){
									A_INFO_MSG("TML : Synchronizing partition failed because of timeout of %d mins\n", sync_time_out_value);
									char sync_failure[128];
									snprintf(sync_failure,sizeof(sync_failure),"Synchronizing partition failed because of timeout of %d mins",sync_time_out_value);
									sent_ua_custom_message(ctl->pkg_name, sync_failure);
									clear_timer(&t, &is_sync_timer_expired);
									remove_cache_dir(ctl);
									return INSTALL_FAILED;
								}
								if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
								{
									A_INFO_MSG("TML : Synchronizing partition failed\n");
									char *sync_failure = "Synchronizing partition failed";
									sent_ua_custom_message(ctl->pkg_name, sync_failure);
									remove_cache_dir(ctl);
									return INSTALL_FAILED;
								}
								char *update_failed = "Update failed due to E_UPDATE_PACKAGE_NOEXIST";
								sent_ua_custom_message(ctl->pkg_name, update_failed);
								remove_cache_dir(ctl);
								return INSTALL_FAILED;
							} else if(persistent_file_exist(update_file) == 0 && (strcmp("INVALID_UPDATE_TYPE", tcu_GetFOTAUpdatetype()))) {
								//if (strcmp(error_state, "E_FILENOTEXIST") == 0 || strcmp(error_state, "E_DIFFUBIUNATTACH") == 0 || strcmp(error_state, "E_BSPATCHFAILED") == 0 || strcmp(error_state, "E_NOTFINDPARTITION") == 0 || strcmp(error_state, "E_UBIVOLUMEEROR") == 0 || strcmp(error_state, "E_IOC_UPDATE_FAILED") == 0){
								if(error_state != NULL){
									A_INFO_MSG("TML : FOTA State Exit State is %s\n",error_state);
									char sync_buffer[64] = {0};
									tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
									initiate_timer(&t, sync_time_out_value, SYNC_TIMER, sync_timer_cb);
									if (diag_timer_add(t.timerHead, &t.timer) == 0) {
										A_INFO_MSG("TML : Timer added successfully\n");
									} else {
										A_INFO_MSG("TML : Timer addition failed\n");
									}
									while (!is_sync_timer_expired)
									{
										A_INFO_MSG("TML : checking the Synchronizing partition state\n");
										if (tcu_GetFOTAState() != NULL && !strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState()))
										{
											clear_timer(&t, &is_sync_timer_expired);
											A_INFO_MSG("sync completed...\n");
											break;
										}
										if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
										{
											clear_timer(&t, &is_sync_timer_expired);
											A_INFO_MSG("sync failed...\n");
											break;
										}
										sleep(1);
									}
									if(is_sync_timer_expired && tcu_GetFOTAState() != NULL && strcmp("FAILED", tcu_GetFOTAState()) && strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState())){
										A_INFO_MSG("TML : Synchronizing partition failed because of timeout of %d mins\n", sync_time_out_value);
										char sync_failure[128];
										snprintf(sync_failure,sizeof(sync_failure),"Synchronizing partition failed because of timeout of %d mins",sync_time_out_value);
										sent_ua_custom_message(ctl->pkg_name, sync_failure);
										clear_timer(&t, &is_sync_timer_expired);
										remove_cache_dir(ctl);
										return INSTALL_FAILED;
									}
									if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
									{
										A_INFO_MSG("TML : Synchronizing partition failed\n");
										char *sync_failure = "Synchronizing partition failed";
										sent_ua_custom_message(ctl->pkg_name, sync_failure);
										remove_cache_dir(ctl);
										return INSTALL_FAILED;
									}
									snprintf(error_msg,sizeof(error_msg),"Update is failed due to error code %s",error_state);
									sent_ua_custom_message(ctl->pkg_name,error_msg);
									remove_cache_dir(ctl);
									return INSTALL_FAILED;
								}
								else {
									A_INFO_MSG("TML : FOTA State Exit State is %s,No need of sync\n",tcu_GetFOTAExitState());
									snprintf(error_msg,sizeof(error_msg),"Update is failed due to error code %s",tcu_GetFOTAExitState());
									sent_ua_custom_message(ctl->pkg_name,error_msg);
									remove_cache_dir(ctl);
									return INSTALL_FAILED;
								}
							} else if ((persistent_file_exist(SWDL_file1) !=0 || persistent_file_exist(SWDL_file2) !=0) && (!strcmp("IOC_ONLY", tcu_GetFOTAUpdatetype()) || !strcmp("NAD_IOC", tcu_GetFOTAUpdatetype()))){
								A_INFO_MSG("TML : FOTA State Exit State:E_IOC_UPDATE_FAILED \n");
								char ioc_write[] = "IOC update failed due to flashing binaries are not exist or IOC writing into inactive partition failed";
								sent_ua_custom_message(ctl->pkg_name, ioc_write);
								remove_cache_dir(ctl);
								return INSTALL_FAILED;
							} else if( tcu_GetFOTAState() != NULL && !strcmp("E_INCOMPATIBLE_UPDATE_REQUEST", tcu_GetFOTAState())) {
								char inc_err_msg[] = "Updated failed due to error code E_INCOMPATIBLE_UPDATE_REQUEST!";
								sent_ua_custom_message(ctl->pkg_name, inc_err_msg);
								remove_cache_dir(ctl);
								return INSTALL_FAILED;
							}
							sleep(1);
						} else if (tcu_GetFOTAState() != NULL && !strcmp("INVALID_UPDATE_TYPE", tcu_GetFOTAUpdatetype())) {
								char inc_err_msg[] = "Updated failed due to INVALID_UPDATE_TYPE!";
								sent_ua_custom_message(ctl->pkg_name, inc_err_msg);
								remove_cache_dir(ctl);
								return INSTALL_FAILED;
						}
						sleep(1);
					}
					if (is_update_timer_expired)
					{
						A_INFO_MSG("Update is failed due to Time out tcu FOTA State: %s\n",tcu_GetFOTAState());
						char *err_msg = "Update is failed due to Time out";
						sent_ua_custom_message(ctl->pkg_name, err_msg);
						clear_timer(&t, &is_update_timer_expired);
						remove_cache_dir(ctl);
						return INSTALL_FAILED;
					}
				}
					A_INFO_MSG("TML : calling tcu_SwitchPartition\n");
					tcu_SwitchPartition();
					sleep(60);
			} else {
					char *delta_zip = "tcu-ua unable to find DeltaPackage.zip";
					custom_msg_error = sent_ua_custom_message(ctl->pkg_name, delta_zip);
					if (custom_msg_error != E_UA_OK) {
						A_INFO_MSG("TML : tcu-ua ua_set_custom_message error/failed : %d\n", custom_msg_error);
					}
					remove_cache_dir(ctl);
					return INSTALL_FAILED;
				}
		} else {
				char *update_zip = "tcu-ua unable to find update.zip";
				custom_msg_error = sent_ua_custom_message(ctl->pkg_name, update_zip);
				if (custom_msg_error != E_UA_OK) {
					A_INFO_MSG("TML : tcu-ua ua_set_custom_message error/failed : %d\n", custom_msg_error);
				}
				remove_cache_dir(ctl);
				return INSTALL_FAILED;
			}
			return INSTALL_IN_PROGRESS;
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0){
		A_INFO_MSG("TML : Checking file exists");
		int status = -1;
		char staticbuffer[4096];
		char* containerUpdateResponse = staticbuffer;

		if(persistent_file_exist(ctl->pkg_path) != 0){
			A_INFO_MSG("pkg path not exist..%s\n",ctl->pkg_path);
			return INSTALL_FAILED;
		}

		int ret = ua_unzip(ctl->pkg_path, cont_file_location);

		if (ret < 0) {
			char *cont_pkg = "tcu-ua unable to unzip the package";
			custom_msg_error = sent_ua_custom_message(ctl->pkg_name, cont_pkg);
			if (custom_msg_error != E_UA_OK) {
				A_INFO_MSG("TML : tcu-ua ua_set_custom_message error/failed : %d\n", custom_msg_error);
			}
			//remove_cache_dir();
			return INSTALL_FAILED;
		}

		if (parseContainerDetailsFromFile(cont_file_name) == 0) {
			A_INFO_MSG("Update Type: %s\n", gType);
			A_INFO_MSG("CheckSum: %s\n", gCheckSum);
			A_INFO_MSG("Container Name from json: %s\n", ContainerJson);
		} else {
			A_INFO_MSG("Failed to parse container details.\n");
		}

		char* jsonMessage = createJsonMessage(ContainerJson);

		if (jsonMessage != NULL) {
			A_INFO_MSG("Generated JSON Message for updatecontainer:\n%s\n", jsonMessage);
		}

		int response = container_update(jsonMessage, &containerUpdateResponse, sizeof(staticbuffer));

		if (response == 0) {
			A_INFO_MSG("Update Status JSON %s\n",containerUpdateResponse);
			if (containerUpdateResponse != staticbuffer) {
				delete[] containerUpdateResponse;
			}
		} else {
			A_INFO_MSG("Container update failed!\n");
			remove_cache_dir(ctl);
			return INSTALL_FAILED;
		}

		struct json_object *jsonresp = json_tokener_parse(containerUpdateResponse);

		if (jsonresp == NULL) {
            std::cerr << "Failed to parse JSON!" << std::endl;
			remove_cache_dir(ctl);
			return INSTALL_FAILED;
		} else {
			struct json_object *status_report_array = NULL;
			if (json_object_object_get_ex(jsonresp, "containerUpdateStatusReport", &status_report_array) &&
                json_object_get_type(status_report_array) == json_type_array) {

                struct json_object *first_container = json_object_array_get_idx(status_report_array, 0);
                if (first_container != NULL && json_object_get_type(first_container) == json_type_object) {
                    // Extract "UpdateStatus"
                    struct json_object *update_status_obj = NULL;
                    if (json_object_object_get_ex(first_container, "UpdateStatus", &update_status_obj)) {
                        const char *update_status = json_object_get_string(update_status_obj);
						A_INFO_MSG("UpdateStatus %s\n",update_status);
						if(strcmp(update_status,"Success") == 0){
							A_INFO_MSG("Container update is successful\n");
							status = 0;
						}else{
							A_INFO_MSG("Container update failed\n");
						}
                    } else {
						A_INFO_MSG("UpdateStatus` key not found in the container object!");
                    }
				}
			}
		}
		json_object_put(jsonresp);
		if(status < 0){
			remove_cache_dir(ctl);
			return INSTALL_FAILED;
		}else{
			remove_cache_dir(ctl);
			return INSTALL_COMPLETED;
		}
	} else if (strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0) {

		if(persistent_file_exist(ctl->pkg_path) != 0){
			A_INFO_MSG("pkg path not exist..%s\n",ctl->pkg_path);
			return INSTALL_FAILED;
		}

		A_INFO_MSG("COTA file location %s\n", cota_file_location);

		int ret = ua_unzip(ctl->pkg_path, cota_file_location);
		if (ret < 0) {
			char *cota_pkg = "tcu-ua unable to unzip the package";
			sent_ua_custom_message(ctl->pkg_name, cota_pkg);
			return INSTALL_FAILED;
		}

		const char *fullType = ctl->type;
		const char *cotaTypeStr = extractCotaType(fullType);
		const char *tableName = extractTableName(fullType);

		if (!cotaTypeStr || !tableName) {
			A_INFO_MSG("COTA type or table name parsing FAILED\n");
			char *cota_fail = "COTA type or table name parsing FAILED";
			sent_ua_custom_message(ctl->pkg_name,cota_fail);
			return INSTALL_FAILED;
		}

		A_INFO_MSG("Extracted COTA Type   = %s", cotaTypeStr);
		A_INFO_MSG("Extracted Table Name  = %s", tableName);

		char jsonFileName[PATH_MAX];
		snprintf(jsonFileName,sizeof(jsonFileName),"%s.json",tableName);

		int callStatus = 0;
		int updateStatus = 0;
		int result = updateConfig(jsonFileName, cotaTypeStr, callStatus, updateStatus);
		char errmsg[PATH_MAX] = {0};

		if (result == 0) {
			// char *current_version = cota_get_version(tableName, cotaTypeStr);
			// if(current_version && (strcmp(ctl->version,current_version)==0)) {
				A_INFO_MSG("COTA update successful!\n");
				// free(current_version);
				return INSTALL_COMPLETED;
		} else if (result == -1) {
			A_INFO_MSG("COTA Proxy may not available or COTA type not recognized !\n");
			snprintf(errmsg,sizeof(errmsg), "COTA Proxy may not available or COTA type not recognized !!");
			sent_ua_custom_message(ctl->pkg_name, errmsg);
			return INSTALL_FAILED;
		} else if (result == -2) {
			A_INFO_MSG("updateConfigRequest API call Succeeded, but Update Failed!\n");
			const char *statusFile = "/emmc/misc/data/cota_update_status.txt";
			FILE *fp = fopen(statusFile, "w");
			if (fp) {
				fprintf(fp, "FAILURE\n");
				fclose(fp);
			} else {
				A_INFO_MSG("Failed to write update result to %s\n", statusFile);
			}
			snprintf(errmsg,sizeof(errmsg), "updateConfigRequest API call Succeeded, but Update Failed!");
			sent_ua_custom_message(ctl->pkg_name, errmsg);
			return INSTALL_FAILED;
		} else {
			A_INFO_MSG("COTA Update Failed..\n");
			const char *statusFile = "/emmc/misc/data/cota_update_status.txt";
			FILE *fp = fopen(statusFile, "w");
			if (fp) {
				fprintf(fp, "FAILURE\n");
				fclose(fp);
			} else {
				A_INFO_MSG("Failed to write update result to %s\n", statusFile);
			}
			snprintf(errmsg,sizeof(errmsg), "COTA Update Failed");
			sent_ua_custom_message(ctl->pkg_name, errmsg);
			return INSTALL_FAILED;
		}
	} else if(strcmp(ctl->type,"/TML/TCU/ECUI") == 0) {
		A_INFO_MSG("TML : Inside Inventory file Install..\n");
		char errmsg[PATH_MAX] = {0};
		int ret = persistent_file_exist(ctl->pkg_path);
		if (ret < 0) {
			A_INFO_MSG("Inventory package not found: %s\n", ctl->pkg_path);
			return INSTALL_FAILED;
		}

		ret = ua_unzip(ctl->pkg_path, g_cache_location);
		if (ret < 0) {
			char *inv_pkg = "tcu-ua unable to unzip the package";
			custom_msg_error = sent_ua_custom_message(ctl->pkg_name, inv_pkg);
			if (custom_msg_error != E_UA_OK) {
				A_INFO_MSG("TML : tcu-ua ua_set_custom_message error/failed : %d\n", custom_msg_error);
			}
			return INSTALL_FAILED;
		}
		int result = update_inventory_file(ctl);
		if (result == 0) {
			const char *current_version = read_inventory_file_version_from_file();
			if (current_version && strcmp(ctl->version, current_version) == 0) {
				A_INFO_MSG("Inventory file updated successfully\n");
				send_uds_inventory_reset();
				return INSTALL_COMPLETED;
			} else {
				snprintf(errmsg,sizeof(errmsg), "Installed version(%s) and deployed version(%s) did not match!!",current_version ? current_version : "NULL",ctl->version);
				sent_ua_custom_message(ctl->pkg_name, errmsg);
			}
		}
		return INSTALL_FAILED;
	} else {
		A_INFO_MSG("Type not defined\n");
		return INSTALL_FAILED;
	}
}

int sent_ua_custom_message(const char* pkg_name, char* msg)
{
	int ret = 0 ;
	A_INFO_MSG( "TML : sent_ua_custom_message : %d \n", ret = ua_set_custom_message(pkg_name, msg));
	return ret;
}

int sent_ua_progress(const char* pkg_name, char* version, int percent)
{
	A_INFO_MSG( "TML : sent_ua_progress : %d percent : %d\n", ua_send_install_progress( pkg_name, version,false, percent), percent);
	return 0;
}

static void do_tcu_post_install(ua_callback_ctl_t* ctl)
{
	A_INFO_MSG("TML : tcu-ua do_post_install\n");
	if (strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0) {
		remove_cache_dir(ctl);
		return;
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0) {
		remove_cache_dir(ctl);
		return;
	} else if (strncmp(ctl->type, "/TML/TCU/ECUI", strlen("/TML/TCU/ECUI")) == 0) {
		remove_cache_dir(ctl);
		return;
	}
}

static void do_tcu_confirm_update(ua_callback_ctl_t* ctl)
{
	if(strcmp(ctl->type,"/TML/TCU/TCU") == 0){

		A_INFO_MSG("TML : tcu-ua do_confirm_update\n");
		char backupPath[PATH_MAX];
		snprintf(backupPath, sizeof(backupPath), "%s/../data/backup/%s", flash_directory, ctl->pkg_name);
		A_INFO_MSG("Backup Dir: %s\n", backupPath);
		if (directory_exists(backupPath) == 0) {
			if(remove_dir(backupPath) == 0) {
				A_INFO_MSG("Directory '%s' removed successfully.\n",cota_file_location);
			} else {
				A_INFO_MSG("rmdir failed");
			}
		} else {
			printf("Directory does not exist: %s\n", backupPath);
		}

		return;
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0){
		A_INFO_MSG("TML : tcu container agent do_confirm_update\n");
		return;
	} else if(strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0){
		A_INFO_MSG("TML : tcu-ua do_confirm_update\n");
		return;
	} else if(strcmp(ctl->type,"/TML/TCU/ECUI") == 0){
		A_INFO_MSG("TML : tcu-ua do_confirm_update\n");
		XL4_UNUSED(ctl);
		return;
	} else {
		A_INFO_MSG("Type not Registered.\n");
		return;
	}

	if(ctl->version) {
		free(ctl->version);
	}
}

static install_state_t do_tcu_prepare_install(ua_callback_ctl_t* ctl)
{
	if(strcmp(ctl->type,"/TML/TCU/TCU") == 0){
		A_INFO_MSG("TML : tcu-ua do_prepare_install\n");
		XL4_UNUSED(ctl);
		return INSTALL_READY;
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0){
		A_INFO_MSG("TML : tcu container agent do_prepare_install\n");
		return INSTALL_READY;
	} else if(strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0){
		A_INFO_MSG("TML : tcu-cota do_prepare_install\n");
		XL4_UNUSED(ctl);
		return INSTALL_READY;
	} else if(strcmp(ctl->type,"/TML/TCU/ECUI") == 0){
		A_INFO_MSG("TML : tcu inventory-file-update do_prepare_install\n");
		XL4_UNUSED(ctl);
		return INSTALL_READY;
	}  else {
		A_INFO_MSG("Type not Registered.\n");
	}
}

#if ESYNC_ALLIANCE
#define BMT_PREFIX "esync."
#else
#define BMT_PREFIX "xl4."
#endif

#define BMT_UPDATE_STATUS BMT_PREFIX "update-status"

int ua_send_message(json_object* message);

char* get_version_from_tos_file(int reqType) {
    char buffer[4096] = {0};
    char* versionPtr = buffer;

    int ret = readTOSVersion(reqType, &versionPtr, sizeof(buffer));
    if (ret == 0 && strlen(versionPtr) > 0) {
        char* result = strdup(versionPtr);
		if (versionPtr != buffer) {
			delete[] versionPtr;
		}
        return result;
    } else {
        return strdup("Unavailable");
    }
}

static void send_current_report() {

// #ifdef TOS_VER

	if(send_current_report_flag == 0) {
		send_current_report_flag = 1;

		int ret = check_sm_service_status();
		if( ret == 1) {
			char *result;
			json_object * jObject = json_object_new_object();
			json_object * bodyObject = json_object_new_object();

			result = get_version_from_tos_file(1);
			json_object_object_add(bodyObject, "Quectel Version", json_object_new_string(result));
			free(result);

			result = get_version_from_tos_file(2);
			json_object_object_add(bodyObject, "Harman Version", json_object_new_string(result));
			free(result);

			result = get_version_from_tos_file(3);
			json_object_object_add(bodyObject, "IOC Version", json_object_new_string(result));
			free(result);

			result = get_version_from_tos_file(4);
			json_object_object_add(bodyObject, "T.OS core Version", json_object_new_string(result));
			free(result);

			result = get_version_from_tos_file(5);
			json_object_object_add(bodyObject, "Esync Version", json_object_new_string(result));
			free(result);

			json_object_object_add(jObject, "body", bodyObject);

			char* version = get_version_from_tos_file(0);

			const char *cur_report = json_object_to_json_string(jObject);
			ua_send_current_report_with_ecu_data("TCU.SW",version,cur_report,NULL);

			json_object_put(jObject);
			free(version);

		} else {

			json_object* pkgObject = json_object_new_object();
			json_object_object_add(pkgObject, "error_message", json_object_new_string("System manager is not running"));
			json_object_object_add(pkgObject, "status", json_object_new_string("CURRENT_REPORT"));

			json_object* bodyObject = json_object_new_object();
			json_object_object_add(bodyObject, "package", pkgObject);

			json_object* jObject = json_object_new_object();
			json_object_object_add(jObject, "type", json_object_new_string(BMT_UPDATE_STATUS));
			json_object_object_add(jObject, "body", bodyObject);

			int err = ua_send_message(jObject);

			json_object_put(jObject);

			A_INFO_MSG("TML : send message current report err: %d\n", err);
		}
	}
	send_current_report_flag = 0;
}

// cppcheck-suppress constParameterPointer
static int do_dmc_presence(dmc_presence_t* dp)
{
	A_INFO_MSG("TML : on_dmc_presence\n");

	if (dp == NULL) {
		A_INFO_MSG("TML : dmclient presence dp is NULL\n");
		return E_UA_ERR;
	}
	A_INFO_MSG("TML : DMclient connection : %s\n", (dp->state == DMCLIENT_CONNECTED) ? "connected" : "disconnected");

	if (dp->state == DMCLIENT_CONNECTED) {
		if (persistent_file_exist(REC_FILE) == 0){
			//intentionally blank
		} else {
			send_current_report();
		}
	}
	return E_UA_OK;
}

int do_resume_from_reboot(ua_callback_ctl_t* ctl) {

	ua_clear_custom_message(ctl->pkg_name);
	if(strcmp(ctl->type,"/TML/TCU/TCU") == 0) {
	A_INFO_MSG("TML : do_resume_from_reboot\n");

		int err = E_UA_ERR;
		const char *old_version = NULL;
		char installed_version[128] = {0};
		int check_sm = 0;
		char staticBuffer[128];
		char* versionInfo = staticBuffer;
		char custom_err_msg[512] = {0};

		check_sm = check_sm_service_status();

		if(check_sm == 1) {
			int reqType = 0;
			int result = readTOSVersion(reqType, &versionInfo, sizeof(staticBuffer));
			if(result == 0) {
				strncpy(installed_version, versionInfo, sizeof(installed_version) - 1);
        		installed_version[sizeof(installed_version) - 1] = '\0';
				if (versionInfo != staticBuffer) {
					delete[] versionInfo;
				}
			} else {
				A_INFO_MSG("Failed to get T.OS version\n");
			}
		}
		old_version = ctl->original_version;

		A_INFO_MSG("TML : OLD version %s\n", old_version);
		A_INFO_MSG("TML : Installed version %s\n", installed_version);
		A_INFO_MSG("TML : Deployed version %s\n", ctl->version);
		A_INFO_MSG("TML : Update type is %s\n",tcu_GetFOTAUpdatetype())

		if(tcu_GetFOTAUpdatetype() != NULL && !strcmp("IOC_ONLY",tcu_GetFOTAUpdatetype())) {

			if(strcmp(ctl->version,installed_version) == 0) {
				A_INFO_MSG("TML : IOC version is updated\n");
				snprintf(custom_err_msg,sizeof(custom_err_msg),"IOC version is updted to:%s",installed_version);
				if (tcu_GetFOTAState() != NULL && !strcmp("ACTIVE_SYSTEM_SWITCHED_SYNC_PENDING", tcu_GetFOTAState())) {
					A_INFO_MSG("TML : Triggering Sync partition and exiting\n");
					char sync_buffer[64] = {0};
					tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
					remove_cache_dir(ctl);
					err = E_UA_OK;
				}
			} else {
				A_INFO_MSG("TML : IOC not updated\n");
				snprintf(custom_err_msg,sizeof(custom_err_msg),"IOC not updated:deployed version(%s) and tcu reported version(%s) are not same",ctl->version,installed_version);
				// char *ioc_update = " Deployed version and tcu reported versions are not same";
				// sent_ua_custom_message(ctl->pkg_name, ioc_update);
				json_object* pkgObject = json_object_new_object();
				json_object_object_add(pkgObject, "name", json_object_new_string("TCU.SW"));
				json_object_object_add(pkgObject, "version", json_object_new_string(ctl->version));
				json_object_object_add(pkgObject, "message",json_object_new_string("Deployed version and tcu reported versions are not same"));
				json_object_object_add(pkgObject, "status", json_object_new_string("INSTALL_FAILED"));

				json_object* bodyObject = json_object_new_object();
				json_object_object_add(bodyObject, "package", pkgObject);
				json_object* jObject = json_object_new_object();
				json_object_object_add(jObject, "type", json_object_new_string("esync.update-status"));
				json_object_object_add(jObject, "body", bodyObject);

				char *ioc_update_fail = const_cast<char*>(json_object_to_json_string(jObject));
				ua_send_message_string(ioc_update_fail);
				json_object_put(jObject);
				err = E_UA_ERR;
				tcu_SwitchPartition();
				sleep(60);
			}

			sent_ua_custom_message(ctl->pkg_name,custom_err_msg);
			return err;
		}

		if (persistent_file_exist(SWITCH_FILE) == 0) {
				A_INFO_MSG("TML :  %s %s \n", SWITCH_FILE, REC_FILE);
				unlink(SWITCH_FILE);
				unlink(REC_FILE);
				tcu_SwitchPartition();
				sleep(60);
		}

		if(strlen(installed_version) == 0) {
			//sent_ua_custom_message(ctl->pkg_name, installed_version);
			snprintf(custom_err_msg,sizeof(custom_err_msg),"failure:System manager is not running, installed version:%s",installed_version);
			json_object* pkgObject = json_object_new_object();
			json_object_object_add(pkgObject, "name", json_object_new_string("TCU.SW"));
			json_object_object_add(pkgObject, "version", json_object_new_string(ctl->version));
			json_object_object_add(pkgObject, "message",json_object_new_string("failure:System manager is not running"));
			json_object_object_add(pkgObject, "status", json_object_new_string("INSTALL_FAILED"));

			json_object* bodyObject = json_object_new_object();
			json_object_object_add(bodyObject, "package", pkgObject);
			json_object* jObject = json_object_new_object();
			json_object_object_add(jObject, "type", json_object_new_string("esync.update-status"));
			json_object_object_add(jObject, "body", bodyObject);

			char *install_ver_fail = const_cast<char*>(json_object_to_json_string(jObject));
			ua_send_message_string(install_ver_fail);
			A_INFO_MSG("TML : Received NULL installed version\n");
			json_object_put(jObject);
			err = E_UA_ERR;
			tcu_SwitchPartition();
			sleep(60);
		}

		if ( strlen(installed_version) != 0 && strcmp(installed_version, ctl->version) == 0) {

			A_INFO_MSG("TML : Install succeeded\n");
			snprintf(custom_err_msg,sizeof(custom_err_msg),"Install succeeded version:%s",installed_version);
			if (tcu_GetFOTAState() != NULL && !strcmp("ACTIVE_SYSTEM_SWITCHED_SYNC_PENDING", tcu_GetFOTAState()))   {
				A_INFO_MSG("TML : Synchronizing partition and exiting\n");
				char sync_buffer[64] = {0};
				tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
				remove_cache_dir(ctl);
				err = E_UA_OK;
				send_current_report();
			}
		} else if ( strlen(installed_version) != 0  && (strcmp(old_version, installed_version) == 0)) {
			A_INFO_MSG("TML :  OLD version %s and tcu reported version %s are same\n",old_version, installed_version);
			snprintf(custom_err_msg,sizeof(custom_err_msg),"OLD version %s and tcu reported version %s are same",old_version,installed_version);
			json_object* pkgObject = json_object_new_object();
			json_object_object_add(pkgObject, "name", json_object_new_string("TCU.SW"));
			json_object_object_add(pkgObject, "version", json_object_new_string(ctl->version));
			json_object_object_add(pkgObject, "message",json_object_new_string("OLD version and tcu reported version are same"));
			json_object_object_add(pkgObject, "status", json_object_new_string("INSTALL_FAILED"));

			json_object* bodyObject = json_object_new_object();
			json_object_object_add(bodyObject, "package", pkgObject);
			json_object* jObject = json_object_new_object();
			json_object_object_add(jObject, "type", json_object_new_string("esync.update-status"));
			json_object_object_add(jObject, "body", bodyObject);

			char *json_ver_mismatch = const_cast<char*>(json_object_to_json_string(jObject));
			ua_send_message_string(json_ver_mismatch);
			// A_INFO_MSG("TML : install message %s\n", (char*)json_object_to_json_string(jObject));
			json_object_put(jObject);
			if (persistent_file_exist(ROLLBACK_INFO) == 0)
			{
				A_INFO_MSG("TML : rollback file present and Synchronizing partition\n");
				tml_timer_t t;
				initiate_timer(&t, sync_time_out_value, SYNC_TIMER, sync_timer_cb);
				if (diag_timer_add(t.timerHead, &t.timer) == 0) {
					A_INFO_MSG("TML : Timer added successfully\n");
				} else {
					A_INFO_MSG("TML : Timer addition failed\n");
				}
				char sync_buffer[64] = {0};
				tcu_SynchronizePartition(sync_buffer, sizeof(sync_buffer));
				while (!is_sync_timer_expired)
				{
					A_INFO_MSG("TML : checking the Synchronizing partition state\n");
					if (tcu_GetFOTAState() != NULL && !strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState()))
					{
						clear_timer(&t, &is_sync_timer_expired);
						A_INFO_MSG("sync completed...\n");
						err = E_UA_ERR;	
						break;
					}
					if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
					{
						clear_timer(&t, &is_sync_timer_expired);
						A_INFO_MSG("sync failed...\n");
						err = E_UA_ERR;
						break;
					}
					sleep(1);
				}
				if(is_sync_timer_expired && tcu_GetFOTAState() != NULL && strcmp("FAILED", tcu_GetFOTAState()) && strcmp("SUCCEEDED_SYNCHRONIZED", tcu_GetFOTAState())){
					A_INFO_MSG("TML : Synchronizing partition failed because of timeout of %d mins\n", sync_time_out_value);
					char sync_failure[128];
					snprintf(sync_failure,sizeof(sync_failure),"Synchronizing partition failed because of timeout of %d mins",sync_time_out_value);;
					sent_ua_custom_message(ctl->pkg_name, sync_failure);
					clear_timer(&t, &is_sync_timer_expired);
					remove_cache_dir(ctl);
					return INSTALL_FAILED;
				}
				if (tcu_GetFOTAState() != NULL && !strcmp("FAILED", tcu_GetFOTAState()))
				{
					A_INFO_MSG("TML : Synchronizing partition failed\n");
					char *sync_failure = "Synchronizing partition failed";
					sent_ua_custom_message(ctl->pkg_name, sync_failure);
					remove_cache_dir(ctl);
					return INSTALL_FAILED;
				}
			}
			remove_cache_dir(ctl);
		} else if ( strlen(installed_version) != 0 && !(strcmp(old_version, installed_version) == 0)) {
			A_INFO_MSG("TML : Deployed version %s and tcu reported version %s are not same hence switching partition for rollback\n", ctl->version, installed_version);
			snprintf(custom_err_msg,sizeof(custom_err_msg),"TML : Deployed version %s and tcu reported version %s are not same hence switching partition for rollback",ctl->version,installed_version);
			store_package_info(ROLLBACK_INFO, ctl->pkg_name, "");
			json_object* pkgObject = json_object_new_object();
			json_object_object_add(pkgObject, "name", json_object_new_string("TCU.SW"));
			json_object_object_add(pkgObject, "version", json_object_new_string(ctl->version));
			json_object_object_add(pkgObject, "message",json_object_new_string("Deployed version and tcu reported versions are not same"));
			json_object_object_add(pkgObject, "status", json_object_new_string("INSTALL_FAILED"));

			json_object* bodyObject = json_object_new_object();
			json_object_object_add(bodyObject, "package", pkgObject);
			json_object* jObject = json_object_new_object();
			json_object_object_add(jObject, "type", json_object_new_string("esync.update-status"));
			json_object_object_add(jObject, "body", bodyObject);

			char *ver_comp_fail = const_cast<char*>(json_object_to_json_string(jObject));
			ua_send_message_string(ver_comp_fail);
			// A_INFO_MSG("TML : install message %s\n", (char*)json_object_to_json_string(jObject));
			sleep(10);

			json_object_put(jObject);
			err = E_UA_ERR;
			tcu_SwitchPartition();
			sleep(60);
		}
		sent_ua_custom_message(ctl->pkg_name,custom_err_msg);
		return err;
	} else if (strncmp(ctl->type, "/TML/TCU/CONT", strlen("/TML/TCU/CONT")) == 0) {
		A_INFO_MSG("Inside do_resume_reboot of container update..\n");
		return E_UA_OK;
	} else if(strncmp(ctl->type, "/TML/TCU/COTA", strlen("/TML/TCU/COTA")) == 0) {
			A_INFO_MSG("TML : tcu ua do_resume_from_reboot\n");
			XL4_UNUSED(ctl);
			return E_UA_OK;
	}  else if(strcmp(ctl->type,"/TML/TCU/ECUI") == 0) {
			A_INFO_MSG("TML : tcu ua do_resume_from_reboot\n");
			XL4_UNUSED(ctl);
			return E_UA_OK;
	}
	A_INFO_MSG("Unknown update type is present\n");
	return E_UA_ERR;
}

static void parse_json_message(const char* message){
	if( message != NULL) {
		struct json_object *parsed_json = json_tokener_parse(message);
		if( parsed_json != NULL) {
			struct json_object *body_obj;
			struct json_object *ignition;
			if(json_object_object_get_ex(parsed_json,"body",&body_obj)){
				if(json_object_object_get_ex(body_obj,"ignition-on",&ignition)){
					bool ignition_on = json_object_get_boolean(ignition);
					A_INFO_MSG("boolean value %d", ignition_on);
					if(ignition_on == 1){
						if (send_current_report_flag == 0) {
							send_current_report();
						}
					}else{
						A_INFO_MSG("IGN OFF...\n");
					}
				}
			}
		}else {
			ERR("Parsing failed of json object\n");
		}
		json_object_put(parsed_json);
	}
}

static struct DiagnosticData* parse_diagnostic_json_message(const char* message){
	if( message != NULL) {
        struct DiagnosticData *diag_data = (struct DiagnosticData*)malloc(sizeof(struct DiagnosticData));
		diag_data->req_type = NULL;
        diag_data->u_string = NULL;

		struct json_object *parsed_json = json_tokener_parse(message);
		if(parsed_json != NULL){
			struct json_object *replyid, *body, *mode, *tcurequest, *tcu_type, *record;

			json_object_object_get_ex(parsed_json,"reply-id",&replyid);
			const char* rplyidstr = json_object_get_string(replyid);
			diag_data->u_string = strdup(rplyidstr);

			json_object_object_get_ex(parsed_json, "body", &body);
			json_object_object_get_ex(body,"mode",&mode);

			if(strcmp(json_object_get_string(mode),"TCU") == 0){
				json_object_object_get_ex(body,"tcu-request",&tcurequest);
				json_object_object_get_ex(tcurequest, "type", &tcu_type);
				json_object_object_get_ex(tcurequest, "record", &record);
				diag_data->req_type = strdup(json_object_get_string(record));

			}
			json_object_put(parsed_json);
		} else {
			ERR("Parsing failed of json object\n");
			free(diag_data);
			return NULL;
		}
		return diag_data;
	}
	return NULL;
}

bool is_cota_already_set(void)
{
    struct json_object *root = json_object_from_file(COTA_STATUS_FILE);
    if (!root) {
        return false;
    }

    struct json_object *is_cota;
    bool result = false;

    if (json_object_object_get_ex(root, "is_cota", &is_cota)) {
        result = json_object_get_boolean(is_cota);
    }

    json_object_put(root);
    return result;
}

char *get_cota_type_handler(void)
{
    struct json_object *root = json_object_from_file(COTA_STATUS_FILE);
    if (!root) {
        return NULL;
    }

    struct json_object *type_handler;
    char *result = NULL;

    if (json_object_object_get_ex(root, "type_handler", &type_handler)) {
        const char *tmp = json_object_get_string(type_handler);
        if (tmp) {
            result = strdup(tmp);
        }
    }

    json_object_put(root);
    return result;
}

static int set_cota_reboot_API(void) {

	int err = E_UA_OK;
	char *type = get_cota_type_handler();
    if (!type) {
        A_INFO_MSG("COTA type_handler not found in JSON\n");
        return E_UA_ERR;
    }
	A_INFO_MSG("Parsed type handler from json: %s\n", type);

    const char *cotaTypeStr = extractCotaType(type);
    if (!cotaTypeStr) {
        A_INFO_MSG("Failed to extract COTA type from %s\n", type);
        free(type);
        return E_UA_ERR;
    }

    A_INFO_MSG("Extracted COTA Type: %s\n", cotaTypeStr);

	if (is_cota_already_set()) {
		A_INFO_MSG("COTA file present");
		int res = handleCOTAStatusUpdate(cotaTypeStr);
		if(res < 0){
			A_INFO_MSG("Could not set COTA completion status.\n");
			err = E_UA_ERR;
		}
		A_INFO_MSG("Set COTA completion status successfully.\n");
	}

	free(type);
	return err;
}

static int do_on_message(const char* type, const char* message) {

	int err = E_UA_OK;

	if(!strcmp(type, "esync.fontana.tbox-ignition")) {
		parse_json_message(message);
	} else if(!strcmp(type, "esync.tcu-dignostics-query")) {
		char staticbuffer[4096];
		char err_msg[1024] = {0};
		char* diagnosticResponse = staticbuffer;
		const char *callstatus = NULL;

		struct DiagnosticData *diag_data = parse_diagnostic_json_message(message);
		if (diag_data != NULL) {
			char *req_type = diag_data->req_type;
			char *u_string = diag_data->u_string;

			int result = TCUDiagnostics(req_type, &diagnosticResponse, sizeof(staticbuffer), err_msg);

			if(result == 0){
				A_INFO_MSG("Diagnostic Response %s\n", diagnosticResponse);
				err = E_UA_OK;
				callstatus = "SUCCESS";
			} else {
				A_INFO_MSG("Failed to get Diagnostic response\n");
				callstatus = "FAILURE";
				diagnosticResponse = err_msg;
				err = E_UA_ERR;
			}

			if (req_type) {
				free(req_type);
			}

			struct json_object *response = json_object_new_object();
			json_object_object_add(response, "type", json_object_new_string("esync.tcu-dignostics-query-update"));

			json_object *json_str_obj = json_object_new_string(u_string);
			json_object_object_add(response, "reply-to",json_str_obj);

			struct json_object *body = json_object_new_object();
			json_object_object_add(response,"body",body);

			struct json_object *tcu_response = json_object_new_object();
			json_object_object_add(body,"tcu-response",tcu_response);

			json_object *callstatus_obj = json_object_new_string(callstatus);
			json_object_object_add(tcu_response,"status",callstatus_obj);

			json_object_object_add(tcu_response,"data", json_object_new_string(diagnosticResponse));

			const char *json_str = json_object_to_json_string(response);

			xl4bus_address_t *address = (xl4bus_address_t *)calloc( 1, sizeof( xl4bus_address_t ) );
			if ( address )
			{
				address->update_agent = strdup( "/RD/COLLECTOR" );
				address->type = XL4BAT_UPDATE_AGENT;
				address->next = NULL;

				int ret = ua_send_message_string_with_address( (char *)json_str, address );
				if ( ret )
				{
					ERR( " failed to send ua_send_message_string_with_address =  %d", ret );
					err = E_UA_ERR;
					return err;
				}
			}
			json_object_put(response);

			if (diagnosticResponse != staticbuffer && diagnosticResponse != err_msg) {
				delete[] diagnosticResponse;
			}

			if (u_string) {
				free(u_string);
			}
		}
		free(diag_data);
		return err;
	} else if(!strcmp(type, "esync.vehicle-update-status")){

		A_INFO_MSG("Received vehicle unlock info");
		A_INFO_MSG("Received message %s",message);

		int err = set_cota_reboot_API();
		if (err != E_UA_OK) {
			A_INFO_MSG("COTA reboot API failed\n");
			return err;
		}
	}
	return err;
}

ua_routine_t tmpl_rtns;

ua_routine_t* get_tcu_routine(void)
{
	tmpl_rtns.on_get_version     = get_tcu_version;
	tmpl_rtns.on_install         = do_tcu_install;
	tmpl_rtns.on_post_install    = do_tcu_post_install;
	tmpl_rtns.on_confirm_update  = do_tcu_confirm_update;
	tmpl_rtns.on_prepare_install = do_tcu_prepare_install;
	tmpl_rtns.on_dmc_presence    = do_dmc_presence; // cppcheck-suppress invalidFunctionPointerCast
	tmpl_rtns.on_resume_from_reboot = do_resume_from_reboot;
	tmpl_rtns.on_message         =  do_on_message;
	return &tmpl_rtns;
}

tcu_handle_t* tcu_ua_agent_init(ua_handler_t* uah, ua_cfg_t cfg, int l, const char* cache_location, const char* flash_directory_location, tcu_time_out_info_t *timer)
{
	tcu_handle_t* handle = (tcu_handle_t*)calloc(1, sizeof(tcu_handle_t));

	A_INFO_MSG("TML : tcu_ua_agent_init starts !\n");
	if (tcu_updateagent_init(timer->hmservice_time_out,timer->proxy_time_out) == 1)
    	std::cout << "TCU UA init OK\n";
	else
    	std::cout << "TCU UA init FAILED\n";

	do {
		if (handle == NULL) {
			A_INFO_MSG("TML : Memory Allocation failed\n");
			break;
		}

	} while (0);

	if (gHandle) {
		free(gHandle);
	}

	gHandle = handle;
	gHandle->timerHead = diag_timer_init(1024);
	update_time_out_value = timer->update_time_out;
	sync_time_out_value = timer->sync_time_out;

	if (ua_init(&cfg)) {
		A_INFO_MSG("TML : Updateagent failed!");
		exit(1);
	}

	ua_register(uah, l);

	snprintf(g_cache_location, (PATH_MAX-1), "%s/tmp/", cache_location);
	snprintf(ROLLBACK_INFO, (PATH_MAX - 1), "%s/ROLLBACK_INFO", g_cache_location);
	snprintf(flash_directory, (PATH_MAX-1), "%s", flash_directory_location);
	snprintf(SWITCH_FILE,(PATH_MAX-1),"%s/backup/SWITCH_FILE",cache_location);
	snprintf(REC_FILE,(PATH_MAX-1),"%s/backup/update.rec",cache_location);
	snprintf(cont_file_location, (PATH_MAX-1), "%s/container_update", cache_location);
    snprintf(cont_file_name,(PATH_MAX-1), "%s/container_update/Containerdetails.json", cache_location);
    snprintf(gDownloadPath, sizeof(gDownloadPath),"%s/container_update/",cache_location);
	snprintf(cota_file_location, (PATH_MAX-1),"%s/cms_config/",cache_location);
	snprintf(inventory_file_location, (PATH_MAX-1), "%s/ecu_config/inventory.json", cache_location);

	if(directory_exists(flash_directory) == 0) {
		A_INFO_MSG("Flash directory is present\n");
	}

	wait_time = timer->install_wait_time;
	hmservice_wait_time = timer->hmservice_time_out;

	return handle;
}

void tcu_ua_deinit(tcu_handle_t* handle, ua_handler_t* uah, int l)
{
	ua_unregister(uah, l);
	ua_stop();
	if (gHandle->timerHead) { diag_timer_deinit(gHandle->timerHead); }

	if (gHandle) {
		free(gHandle);
	}
}

#ifdef __cplusplus
}
#endif
