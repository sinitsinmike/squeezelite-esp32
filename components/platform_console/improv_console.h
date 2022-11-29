extern TickType_t improv_timeout_tick;
#pragma once
#include "network_manager.h"
#include "improv.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
#define BUFFER_DEBUG 0


extern TickType_t improv_timeout_tick;
extern const size_t improv_buffer_size;
extern size_t improv_buffer_len;
extern uint8_t * improv_buffer_data;
extern const time_t improv_timeout_ms;
extern TickType_t improv_delay;


void cb_improv_got_ip(nm_state_t new_state, int sub_state);
bool on_improv_command(ImprovCommandStruct_t *command);
void on_improv_error(ImprovError_t error);
void dump_buffer(const char * prefix, const char * buff, size_t len);
bool improv_send_callback(uint8_t * buffer, size_t length);
void improv_console_init();