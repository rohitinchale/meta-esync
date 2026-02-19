/**
 * @file tmpl_updateagent.h
 *
 * Copyright(c) 2024 Excelfore Corporation, - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Proprietary and confidential.
 * Its use or disclosure, in whole or in part, without
 * written permission of Excelfore Corp. is prohibited.
 *
 */

#ifndef _TMPL_UPDATEAGENT_H_
#define _TMPL_UPDATEAGENT_H_

#include "tcu_updateagent.h"

#ifdef __cplusplus
extern "C" {
#endif



// #include <libxl4bus/low_level.h>
// #include "json-c-rename.h"
// #include "json.h"

#ifdef LIBUA_VER_2_0
#include "esyncua.h"
#else
#include "xl4ua.h"
#endif //LIBUA_VER_2_0

#include <string.h>
#include <uds_utils.h>
#include "util.h"

#ifndef BUILD_VERSION
#define BUILD_VERSION  "1.0"
#endif
#define BUILD_DATETIME __DATE__ " " __TIME__
#define SECONDS_PER_MINUTE   60U
#define MINUTES_TO_SECONDS(m) ((unsigned int)(m) * SECONDS_PER_MINUTE)
#define SYNC_TIMER "Start TML : Synchronizing partition..."
#define UPDATE_TIMER "Start TML : Updating partition..."

typedef struct tcu_node {
	char* tcu_type;
	char* hardware_version;
	char* sw_version_file;
	char* sw_flash_file;
	char* flash_args;
	char* swupdate_path;
	char* package_info;
	char* unzip_bin_path;
	char* unzip_dir_path;
	struct tcu_node* next;
}tcu_node_t;

typedef struct tcu_handle {
	ua_callback_ctl_t* ucc;
	tcu_node_t* tcuHead;
	struct diag_timer *timerHead;
}tcu_handle_t;

struct DiagnosticData {
    char *req_type;
    char *u_string;
};

typedef struct tml_timer
{
    struct diag_timer *timerHead;
    diag_timer_client_t timer;
}tml_timer_t;

typedef struct tcu_time_out_info
{
	uint32_t install_wait_time;
	uint16_t hmservice_time_out;
	uint32_t proxy_time_out;
	uint32_t update_time_out;
	uint32_t sync_time_out;
}tcu_time_out_info_t;

#define XL4_UNUSED(x) (void)x;

ua_routine_t* get_tcu_routine(void);
tcu_handle_t* tcu_ua_agent_init(ua_handler_t* uah, ua_cfg_t cfg, int l,const char* cache_location, const char* flash_directory_location, tcu_time_out_info_t *timer);
void tcu_ua_deinit(tcu_handle_t* handle, ua_handler_t* uah, int l);
const char* tcu_get_version(int hmservice_wait_time);
const char* tcu_GetFOTAState();

#ifdef __cplusplus
}
#endif

#endif /* _TMPL_UPDATEAGENT_H_ */
