/**
 * @file main_updateagent.cpp
 *
 * Copyright(c) 2024 Excelfore Corporation, - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited.
 * Proprietary and confidential.
 * Its use or disclosure, in whole or in part, without
 * written permission of Excelfore Corp. is prohibited.
 *
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include "tcu_ua_version.h"
#include "tmpl_updateagent.h"
#include <xl4clib/loggit.h>


#define BASE_TEN_CONVERSION 10

int ua_debug_lvl = 0;

ua_handler_t uah[] = {
	{(char *)"", get_tcu_routine}
};

static int stop = 0;

static void sig_handler(int num)
{
	stop = 1;
	XL4_UNUSED(num);
}

static void _help(const char* app)
{
	printf("Usage: %s [OPTION...]\n\n%s", app,
	       "Options:\n"
	       "  -k         : path to certificate directory(default: \"/usrdata/data/InVehicle-Certs/pki/certs/tcu_ua\")\n"
	       "  -a <cap>   : delta capability\n"
	       "  -b <path>  : path to backup directory (default: \"/data/sota/esync/\")\n"
	       "  -c <path>  : path to cache directory (default: \"/tmp/esync/\")\n"
	       "  -e         : enable error msg\n"
	       "  -w         : enable warning msg\n"
	       "  -i         : enable information msg\n"
	       "  -d         : enable all debug msg\n"
	       "  -D         : disable delta reconstruction\n"
	       "  -F         : enable fake rollback version\n"
		   "  -l         : max retry count (default is 1)\n"
		   "  -T         : wait value for installation\n"
		   "  -p         : proxy time out value (ms)\n"
		   "  -U         : update time out value (in minutes Default: 25 min)\n"
		   "  -s         : synchronizing time out value (in minutes Default: 16 min)\n"
	       "  -h         : display this help and exit\n"
	       );
	_exit(1);
}

int main(int argc, char** argv)
{
	printf("TML : tcu updateagent : %s build on %s\n", BUILD_VERSION, BUILD_DATETIME);
	printf("updateagent %s, xl4bus %s\n", UA_VERSION, ua_get_xl4bus_version());

	int c     = 0;
	char* end = NULL;
	ua_cfg_t cfg;
	memset(&cfg, 0, sizeof(ua_cfg_t));

	cfg.debug          = 0;
	cfg.delta          = 1;
	cfg.cert_dir       = "./../pki/certs/updateagent";
	cfg.url            = "tcp://localhost:9133";
	cfg.cache_dir      = "/tmp/esync/";
	cfg.backup_dir     = "/data/sota/esync/";
	cfg.delta_config   = new (delta_cfg_t);
	cfg.reboot_support = 1;
	cfg.max_retry      = 1;
	cfg.qp_failure_response = 1;
	char cache_location[PATH_MAX];
	const char* flash_directory_location = "/emmc/misc/SW_Update_package";
	tcu_time_out_info_t timer;
	timer.install_wait_time = 24 * 60 * 60;
	timer.hmservice_time_out = 10;
	timer.proxy_time_out = 5000;
	timer.update_time_out = 25 * SECONDS_PER_MINUTE;
	timer.sync_time_out = 16 * SECONDS_PER_MINUTE;

	while ((c = getopt(argc, argv, ":k:t:f:u:b:c:a:m:t:p:s:U:T:H:l:C:ewidDFh")) != -1) {
		switch (c) {
			case 'k':
				cfg.cert_dir = optarg;
				break;
			case 'u':
				cfg.url = optarg;
				break;
			case 'b':
				cfg.backup_dir = optarg;
				break;
			case 'c':
				cfg.cache_dir = optarg;
				break;
			case 'f':
				flash_directory_location = optarg;
				break;
			case 'a':
				cfg.delta_config->delta_cap = optarg;
				break;
			case 't':
				uah[0].type_handler = optarg;
				break;
			case 'e':
				cfg.debug = 1;
				break;
			case 'w':
				cfg.debug = 2;
				break;
			case 'i':
				cfg.debug = 3;
				break;
			case 'd':
				cfg.debug = 4;
				break;
			case 'D':
				cfg.delta = 0;
				break;
			case 'F':
				cfg.enable_fake_rb_ver = 1;
				break;
			case 'T':
				timer.install_wait_time = strtol(optarg, NULL, 10);
				break;
			case 'H':
				timer.hmservice_time_out = strtol(optarg, NULL, 10);
				break;
			case 'l':
				cfg.max_retry = strtol(optarg, NULL, 10);
				break;
			case 'p':
				timer.proxy_time_out = strtol(optarg, NULL, 10);
				break;
			case 'U':
				timer.update_time_out = MINUTES_TO_SECONDS(strtol(optarg, NULL, 10));
				break;
			case 's':
				timer.sync_time_out = MINUTES_TO_SECONDS(strtol(optarg, NULL, 10));
				break;
			case 'm':
				if ((cfg.rw_buffer_size = strtol(optarg, &end, BASE_TEN_CONVERSION)) > 0)
				break;
			case 'h':
			default:
				_help(argv[0]);
				break;
		}
	}
	printf("Type handler..........%s\n",uah[0].type_handler);
	snprintf(cache_location, (PATH_MAX-1), "%s", cfg.cache_dir);

	signal(SIGINT, sig_handler);

	ua_debug_lvl = cfg.debug;

	int l = sizeof(uah)/sizeof(ua_handler_t);

	tcu_handle_t* handle = tcu_ua_agent_init(uah, cfg, l, cache_location, flash_directory_location, &timer);

	pause();

	tcu_ua_deinit(handle, uah, l);

}
