#include "esp_system.h"
#include "esp_log.h"
#include "improv.h"
#include "tools.h"
#include "string.h"
static ImprovCommandStruct_t last_command;

static callback_table_t callbacks[] = {
    {IMPROV_CMD_UNKNOWN, NULL},
    {IMPROV_CMD_WIFI_SETTINGS, NULL},
    {IMPROV_CMD_GET_CURRENT_STATE, NULL},
    {IMPROV_CMD_GET_DEVICE_INFO, NULL},
    {IMPROV_CMD_GET_WIFI_NETWORKS, NULL},
    {IMPROV_CMD_BAD_CHECKSUM, NULL},
    {-1, NULL}};
const char *improv_get_error_desc(ImprovError_t error)
{
  switch (error)
  {
    ENUM_TO_STRING(IMPROV_ERROR_NONE)
    ENUM_TO_STRING(IMPROV_ERROR_INVALID_RPC)
    ENUM_TO_STRING(IMPROV_ERROR_UNKNOWN_RPC)
    ENUM_TO_STRING(IMPROV_ERROR_UNABLE_TO_CONNECT)
    ENUM_TO_STRING(IMPROV_ERROR_NOT_AUTHORIZED)
    ENUM_TO_STRING(IMPROV_ERROR_UNKNOWN)
  }
  return "";
}
const char *improv_get_command_desc(ImprovCommand_t command)
{
  switch (command)
  {
    ENUM_TO_STRING(IMPROV_CMD_UNKNOWN)
    ENUM_TO_STRING(IMPROV_CMD_WIFI_SETTINGS)
    ENUM_TO_STRING(IMPROV_CMD_GET_CURRENT_STATE)
    ENUM_TO_STRING(IMPROV_CMD_GET_DEVICE_INFO)
    ENUM_TO_STRING(IMPROV_CMD_GET_WIFI_NETWORKS)
    ENUM_TO_STRING(IMPROV_CMD_BAD_CHECKSUM)
  }
  return "";
};
static improv_send_callback_t send_callback = NULL;
const uint8_t improv_prefix[] = {'I', 'M', 'P', 'R', 'O', 'V', IMPROV_SERIAL_VERSION};
typedef struct __attribute__((__packed__))
{
  uint8_t prefix[6];
  uint8_t version;
  uint8_t packet_type;
  uint8_t data_len;
} improv_packet_t;
#define PACKET_CHECKSUM_SIZE sizeof(uint8_t)
#define PACKET_PAYLOAD(packet) ((uint8_t *)packet) + sizeof(improv_packet_t)

static ImprovAPListStruct_t *ap_list = NULL;
static size_t ap_list_size = 0;
static size_t ap_list_actual = 0;
void improv_wifi_list_free()
{
  ap_list_actual = 0;
  ImprovAPListStruct_t *current = ap_list;
  for (int i = 0; i < ap_list_actual; i++)
  {
    if (!current)
    {
      break;
    }
    FREE_AND_NULL(current->rssi);
    FREE_AND_NULL(current->ssid);
    FREE_AND_NULL(current->auth_req);
    current++;
  }
  FREE_AND_NULL(ap_list);
}
bool improv_wifi_list_allocate(size_t num_entries)
{
  improv_wifi_list_free();
  ap_list = malloc_init_external(num_entries * sizeof(ImprovAPListStruct_t) + 1); // last byte will always be null
  ap_list_size = num_entries;
  ap_list_actual = 0;
  return ap_list != NULL;
}

bool improv_wifi_list_add(const char *ssid, int8_t rssi, bool auth_req)
{
  const size_t yes_no_length = 4;
  ImprovAPListStruct_t *current = ap_list + ap_list_actual;
  if (ap_list_actual > ap_list_size || !current)
  {
    return false;
  }
  current->ssid = strdup_psram(ssid);
  size_t length = snprintf(NULL, 0, "%02d", rssi) + 1;
  current->auth_req = malloc_init_external(yes_no_length); // enough for YES/NO to fit
  current->rssi = (char *)malloc_init_external(length);
  if (!current->rssi || !current->auth_req)
  {
    return false;
  }
  snprintf(current->rssi, length, "%02d", rssi);
  snprintf(current->auth_req, yes_no_length, "%s", auth_req ? "YES" : "NO");
  ap_list_actual++;
  return true;
}

void improv_parse_data(ImprovCommandStruct_t *improv_command, const uint8_t *data, size_t length, bool check_checksum)
{

  ImprovCommand_t command = (ImprovCommand_t)data[0];
  uint8_t data_length = data[1];

  if (data_length != length - 2 - check_checksum)
  {
    improv_command->command = IMPROV_CMD_UNKNOWN;
    return;
  }

  if (check_checksum)
  {
    uint8_t checksum = data[length - 1];

    uint32_t calculated_checksum = 0;
    for (uint8_t i = 0; i < length - 1; i++)
    {
      calculated_checksum += data[i];
    }

    if ((uint8_t)calculated_checksum != checksum)
    {
      improv_command->command = IMPROV_CMD_BAD_CHECKSUM;
      return;
    }
  }

  if (command == IMPROV_CMD_WIFI_SETTINGS)
  {
    uint8_t ssid_length = data[2];
    uint8_t ssid_start = 3;
    size_t ssid_end = ssid_start + ssid_length;

    uint8_t pass_length = data[ssid_end];
    size_t pass_start = ssid_end + 1;
    size_t pass_end = pass_start + pass_length;

    improv_command->ssid = malloc(ssid_length + 1);
    memset(improv_command->ssid, 0x00, ssid_length + 1);
    memcpy(improv_command->ssid, &data[ssid_start], ssid_length);
    improv_command->password = NULL;
    if (pass_length > 0)
    {
      improv_command->password = malloc(pass_length + 1);
      memset(improv_command->password, 0x00, pass_length + 1);
      memcpy(improv_command->password, &data[pass_start], pass_length);
    }
  }

  improv_command->command = command;
}

bool improv_parse_serial_byte(size_t position, uint8_t byte, const uint8_t *buffer,
                              improv_command_callback_t callback, on_error_callback_t on_error)
{
  ImprovCommandStruct_t command = {0};
  if (position < 7)
    return byte == improv_prefix[position];
  if (position <= 8)
    return true;

  uint8_t command_type = buffer[7];
  uint8_t data_len = buffer[8];

  if (position <= 8 + data_len)
    return true;

  if (position == 8 + data_len + 1)
  {
    uint8_t checksum = 0x00;
    for (size_t i = 0; i < position; i++)
      checksum += buffer[i];

    if (checksum != byte)
    {
      on_error(IMPROV_ERROR_INVALID_RPC);
      return false;
    }

    if (command_type == IMPROV_PACKET_TYPE_RPC)
    {

      improv_parse_data(&command, &buffer[9], data_len, false);
      callback(&command);
    }
  }

  return false;
}

void improv_set_send_callback(improv_send_callback_t callback)
{
  send_callback = callback;
}

bool improv_set_callback(ImprovCommand_t command, improv_command_callback_t callback)
{
  callback_table_t *pCt = &callbacks;
  while (pCt->index > -1)
  {
    if (pCt->index == command)
    {
      pCt->callback = callback;
      return true;
    }
  }
  return false;
}
bool improv_handle_callback(ImprovCommandStruct_t *command)
{
  const callback_table_t *pCt = &callbacks;
  while (pCt->index > -1)
  {
    if (pCt->index == command->command)
    {
      return pCt->callback && pCt->callback(command);
    }
  }
  return false;
}
bool improv_send_packet(uint8_t *packet, size_t msg_len)
{
  bool result = false;
  if (send_callback && packet && msg_len > 0)
  {
    result = send_callback(packet, msg_len);
  }
  return result;
}
bool improv_send_byte(ImprovSerialType_t packet_type, uint8_t data)
{
  size_t msg_len;
  uint8_t *packet = improv_build_response(packet_type, (const char *)&data, 1, &msg_len);
  bool result = improv_send_packet(packet, msg_len);
  FREE_AND_NULL(packet);
  return result;
}

bool improv_send_current_state(ImprovState_t state)
{
  return improv_send_byte(IMPROV_PACKET_TYPE_CURRENT_STATE, (uint8_t)state);
}
bool improv_send_error(ImprovError_t error)
{
  return improv_send_byte(IMPROV_PACKET_TYPE_ERROR_STATE, (uint8_t)error);
}
size_t improv_wifi_get_wifi_list_count(){
  return ap_list_actual;
}
bool improv_wifi_list_send()
{
  size_t msglen = 0;
  bool result = true;
  if (ap_list_actual == 0)
  {
    return false;
  }
  for (int i = 0; i < ap_list_actual && result; i++)
  {
    uint8_t *packet = improv_build_rpc_response(IMPROV_CMD_GET_WIFI_NETWORKS,(const char **) &ap_list[i], IMPROV_AP_STRUCT_NUM_STR, &msglen);
    result = improv_send_packet(packet, msglen);
    FREE_AND_NULL(packet);
  }
  uint8_t *packet = improv_build_rpc_response(IMPROV_CMD_GET_WIFI_NETWORKS, NULL, 0, &msglen);
  result = improv_send_packet(packet, msglen);
  FREE_AND_NULL(packet);
  return result;
}
bool improv_send_device_url(ImprovCommand_t from_command, const char *url)
{
  size_t msglen = 0;
  uint8_t *packet = NULL;
  bool result = false;
  if (url && strlen(url))
  {
    packet = improv_build_rpc_response(from_command, &url, 1, &msglen);
    if (!packet)
      return false;
    result = improv_send_packet(packet, msglen);
      FREE_AND_NULL(packet);
  }
  packet = improv_build_rpc_response(from_command, "", 0, &msglen);
  if (!packet)
    return false;
  result = improv_send_packet(packet, msglen);

  

  return result;
}
bool improv_send_device_info(const char *firmware_name, const char *firmware_version, const char *hardware_chip_variant, const char *device_name)
{
  ImprovDeviceInfoStruct_t device_info;
  size_t msglen = 0;
  device_info.device_name = device_name;
  device_info.firmware_name = firmware_name;
  device_info.firmware_version = firmware_version;
  device_info.hardware_chip_variant = hardware_chip_variant;
  device_info.nullptr = NULL;
  uint8_t *packet = improv_build_rpc_response(IMPROV_CMD_GET_DEVICE_INFO, &device_info, IMPROV_DEVICE_INFO_NUM_STRINGS, &msglen);
  if (!packet)
    return false;
  bool result = improv_send_packet(packet, msglen);
  FREE_AND_NULL(packet);
  return true;
}
bool parse_improv_serial_line(const uint8_t *buffer)
{
  const uint8_t *b = buffer;
  const uint8_t *p = improv_prefix;
  const uint8_t *data = NULL;
  uint8_t checksum = 0x00;
  uint8_t rec_checksum = 0x00;

  while (*p != '\0' && *b != '\0')
  {
    // check if line prefix matches the standard
    if (*p++ != *b++)
    {
      return false;
    }
  }

  uint8_t command_type = *p++;
  if (command_type == 0)
    return false;
  uint8_t data_len = *p++;
  data = p;
  rec_checksum = buffer[sizeof(improv_prefix) + data_len];
  for (size_t i = 0; i < sizeof(improv_prefix) + data_len; i++)
  {
    checksum += buffer[i];
  }

  if (checksum != rec_checksum)
  {
    improv_send_error(IMPROV_ERROR_INVALID_RPC);
    return false;
  }

  if (command_type == IMPROV_PACKET_TYPE_RPC)
  {
    improv_parse_data(&last_command, &data, data_len, false);
    return improv_handle_callback(&last_command);
  }

  return false;
}
// Improv packet format
// 1-6	Header will equal IMPROV
// 7	Version CURRENT VERSION = 1
// 8	Type (see below)
// 9	Length
// 10...X	Data
// X + 10	Checksum
improv_packet_t *improv_alloc_prefix(size_t data_len, ImprovSerialType_t packet_type, size_t *out_len)
{
  size_t buffer_len = sizeof(improv_packet_t) + data_len + 1; // one byte for checksum
  if (out_len)
  {
    *out_len = buffer_len;
  }
  improv_packet_t *out = (improv_packet_t *)malloc_init_external(buffer_len + 1);
  memcpy(out, improv_prefix, sizeof(improv_prefix));
  out->packet_type = (uint8_t)packet_type;
  out->data_len = (uint8_t)data_len;
  return out;
}
uint8_t improv_set_checksum(improv_packet_t *data, size_t buffer_len)
{
  uint32_t calculated_checksum = 0;
  for (int b = 0; b < buffer_len - 1; b++)
  {
    calculated_checksum += ((uint8_t *)data)[b];
  }
  calculated_checksum = calculated_checksum & 0xFF;
  ((uint8_t *)data)[buffer_len - 1] = (uint8_t)calculated_checksum;
  return calculated_checksum;
}
uint8_t *improv_build_response(ImprovSerialType_t packet_type, const char *datum, size_t len, size_t *out_len)
{
  size_t buffer_len = 0;
  improv_packet_t *improv_packet = improv_alloc_prefix(len, packet_type, &buffer_len);
  if (out_len)
  {
    *out_len = buffer_len;
  }
  uint8_t *p = PACKET_PAYLOAD(improv_packet);
  for (int i = 0; i < len; i++)
  {
    *p++ = datum[i]; // string 1
  }
  improv_set_checksum(improv_packet, buffer_len);
  return (uint8_t *)improv_packet;
}
uint8_t *improv_build_rpc_response(ImprovCommand_t command, const char **results, size_t num_strings, size_t *out_len)
{
  size_t buffer_len = 0;
  size_t total_string_len = 0;
  size_t string_buffer_len = 0;
  for (int i = 0; i < num_strings && (results[i] && (results[i])[0] != '\0'); i++)
  {
    size_t l = strlen(results[i]);
    total_string_len += l;
    string_buffer_len += l + 1; // length of the string plus byte for length
  }

  improv_packet_t *improv_packet = improv_alloc_prefix(string_buffer_len + 2, IMPROV_PACKET_TYPE_RPC_RESPONSE, &buffer_len); // 2 bytes for command and length of all strings
  if (out_len)
  {
    *out_len = buffer_len;
  }
  uint8_t *p = PACKET_PAYLOAD(improv_packet);
  *p++ = (uint8_t)command;           // command being responded to
  *p++ = (uint8_t)string_buffer_len; //
  for (int i = 0; i < num_strings && results[i]; i++)
  {
    uint8_t curlel = (uint8_t)strlen(results[i]);
    *p++ = curlel;
    memcpy(p, results[i], curlel);
    p += curlel;
  }
  improv_set_checksum(improv_packet, buffer_len);
  return (uint8_t *)improv_packet;
}
