#include "platform_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "nvs.h" 
#include "nvs_flash.h"
#include "pthread.h"
#include "platform_esp32.h"
#include "cmd_decl.h"
#include "trace.h"
#include "platform_config.h"
#include "telnet.h" 
#include "tools.h"
#include "improv.h"
#include "messaging.h"
#include "config.h"
#include "improv_console.h"
#include "network_status.h"

static const char * TAG ="improv_console";
const time_t improv_timeout_ms = 50;
TickType_t improv_timeout_tick = pdMS_TO_TICKS(improv_timeout_ms);
ImprovState_t improv_state = IMPROV_STATE_READY_AUTHORIZED; 
const size_t improv_buffer_size = 121;
size_t improv_buffer_len = 0;
uint8_t * improv_buffer_data = NULL;
TickType_t improv_delay = portMAX_DELAY;


void cb_improv_got_ip(nm_state_t new_state, int sub_state){
	if(improv_state == IMPROV_STATE_PROVISIONING){
		char * url = network_status_alloc_get_system_url();
		ESP_LOGI(TAG,"Signaling improv state connected state with url: %s",STR_OR_BLANK(url));
		improv_send_device_url(IMPROV_CMD_WIFI_SETTINGS,url);
		FREE_AND_NULL(url);
	}
}
void cb_improv_disconnected(nm_state_t new_state, int sub_state){
	if(improv_state == IMPROV_STATE_PROVISIONING){
		ESP_LOGI(TAG,"Signalling improv connect failure ");
		improv_state = IMPROV_STATE_READY_AUTHORIZED;
		improv_send_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
	}
	
}
bool on_improv_command(ImprovCommandStruct_t *command){
	esp_err_t err = ESP_OK;
	wifi_connect_state_t wifi_state = network_wifi_get_connect_state();
	const esp_app_desc_t* desc = esp_ota_get_app_description();
	improv_buffer_len = 0;
	char * url=NULL;
	char * host_name = NULL;
	ESP_LOGI(TAG, "Processing improv command %s",improv_get_command_desc(command->command));
	if(!command){
		return false;
	}
	switch (command->command)
	{
		case IMPROV_CMD_WIFI_SETTINGS:
			// attempt to connect to the provided SSID+password
            improv_state = IMPROV_STATE_PROVISIONING;
			ESP_LOGI(TAG,"Improv connect to %s",command->ssid );
			network_async_connect(command->ssid, command->password);
			FREE_AND_NULL(command->ssid);
			FREE_AND_NULL(command->password);

			break;
		case IMPROV_CMD_GET_CURRENT_STATE:
			if(wifi_state !=NETWORK_WIFI_STATE_CONNECTING){
				network_async_scan();
			}
			switch (wifi_state)
			{
				case NETWORK_WIFI_STATE_CONNECTING:
					ESP_LOGI(TAG,"Signaling improv state " );
  					return improv_send_current_state(improv_state);
					break;
				case NETWORK_WIFI_STATE_INVALID_CONFIG:
					improv_state = IMPROV_STATE_READY_AUTHORIZED;
					ESP_LOGW(TAG,"Signaling improv state IMPROV_ERROR_UNABLE_TO_CONNECT" );
					return improv_send_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
					break;
				case NETWORK_WIFI_STATE_FAILED:
					ESP_LOGW(TAG,"Signaling improv state IMPROV_ERROR_NOT_AUTHORIZED" );
					network_async_scan();
					improv_state = IMPROV_STATE_READY_AUTHORIZED;
					return improv_send_error(IMPROV_ERROR_NOT_AUTHORIZED);
					break;
				case NETWORK_WIFI_STATE_CONNECTED:
					network_async_scan();
					url = network_status_alloc_get_system_url();
					ESP_LOGI(TAG,"Signaling improv state connected state with url: %s",STR_OR_BLANK(url));
					improv_state = IMPROV_STATE_PROVISIONED;
					improv_send_current_state(improv_state);
					// also send url
					improv_send_device_url(IMPROV_CMD_GET_CURRENT_STATE,url);
					FREE_AND_NULL(url);
					break;
			default:
				ESP_LOGI(TAG,"Signaling improv state " );
				return improv_send_current_state(improv_state);
				break;
			}
		break;
		case IMPROV_CMD_GET_DEVICE_INFO:
			ESP_LOGI(TAG,"Signaling improv with device info. Firmware Name: %s, Version: %s  ",desc->project_name,desc->version );
			host_name = config_alloc_get_str("host_name",NULL,"Squeezelite");
			improv_send_device_info(desc->project_name,desc->version,"ESP32",host_name);
			FREE_AND_NULL(host_name);
		break;
		case IMPROV_CMD_GET_WIFI_NETWORKS:
		ESP_LOGI(TAG,"Signaling improv with list of wifi networks " );
		improv_wifi_list_send();
		break;
		default:
			ESP_LOGE(TAG,"Signaling improv with invalid RPC call received" );
			improv_send_error(IMPROV_ERROR_INVALID_RPC);
			break;
	}
	return false;
}
void on_improv_error(ImprovError_t error){
	improv_send_error(error);
	ESP_LOGE(TAG,"Error processing improv-wifi packet : %s", improv_get_error_desc(error));

}

#if BUFFER_DEBUG
void dump_buffer(const char * prefix, const char * buff, size_t len){

	printf("\n%s (%d): ",prefix, len);
	for(int i=0;i<len;i++){
		printf("    %c ",isprint(buff[i])?buff[i]:'.');
	}
	printf("\n%s (%d): ",prefix, len);
	for(int i=0;i<len;i++){
		printf("0x%03x ",buff[i]);
	}
	printf("\n");

}
#else
#define dump_buffer(prefix,buff,size)
#endif
bool improv_send_callback(uint8_t * buffer, size_t length){
	dump_buffer("send", (const char *) buffer, length);
	uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM,buffer,length );
	return true;
}
void improv_console_init(){
	ESP_LOGI(TAG,"Initializing improv callbacks");
    network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE,WIFI_CONNECTED_STATE, "improv_got_ip", &cb_improv_got_ip);
    network_register_state_callback(NETWORK_WIFI_ACTIVE_STATE,WIFI_CONNECTING_NEW_FAILED_STATE, "improv_disconnect", &cb_improv_disconnected);
    network_register_state_callback(NETWORK_WIFI_CONFIGURING_ACTIVE_STATE,WIFI_CONFIGURING_CONNECT_FAILED_STATE, "improv_disconnect", &cb_improv_disconnected);  
}