#pragma once
// This is the description of the Improv Wi-Fi protocol using a serial port.
// The device needs to be connected to the computer via a USB/UART serial port.
// The protocol has two actors: the Improv service running on the gadget and the Improv client.
// The Improv service will receive Wi-Fi credentials from the client via the serial connection.
// The Improv client asks for the current state and sends the Wi-Fi credentials.

// =========================================================================================
// Packet format
// ======================================
// Byte	  Purpose
// ----   -------------------------------
// 1-6	  Header will equal IMPROV
// 7	    Version CURRENT VERSION = 1
// 8	    Type (see below)
// 9	    Length
// 10...X	Data
// X + 10	Checksum

// =========================================================================================
// Packet types
// ======================================
// Type	Description	    Direction
// ---- ------------    -----------------
// 0x01	Current state   Device to Client
// 0x02	Error state	    Device to Client
// 0x03	RPC Command	    Device to Client
// 0x04	RPC Result	    Client to Device
typedef enum  {
    IMPROV_PACKET_TYPE_CURRENT_STATE = 0x01,
    IMPROV_PACKET_TYPE_ERROR_STATE = 0x02,
    IMPROV_PACKET_TYPE_RPC = 0x03,
    IMPROV_PACKET_TYPE_RPC_RESPONSE = 0x04
} ImprovSerialType_t;

// =========================================================================================
// Packet: Current State
// ======================================
// Type: 0x01
// Direction: Device to Client
// --------------------------------------
// The data of this packet is a single byte and contains the current status of the provisioning
// service. It is to be written to any listening clients for instant feedback.

// Byte	Description
// 1	current state
// The current state can be the following values:

// Value	State	              Purpose
// -----  ------------------  -----------------------------------------
// 0x02	  Ready (Authorized)	Ready to accept credentials.
// 0x03	  Provisioning	      Credentials received, attempt to connect.
// 0x04	  Provisioned	        Connection successful.
typedef enum   {
  IMPROV_STATE_READY_AUTHORIZED = 0x02,
  IMPROV_STATE_PROVISIONING = 0x03,
  IMPROV_STATE_PROVISIONED = 0x04,
} ImprovState_t;

// =========================================================================================
// Packet: Error state
// ======================================
// Type: 0x02
// Direction: Device to client
// --------------------------------------
// The data of this packet is a single byte and contains the current status of the 
// provisioning service. Whenever it changes the device needs to sent it to any listening 
// clients for instant feedback.

// Byte	Description
// 1	error state
// Error state can be the following values:

// Value	State	              Purpose
// -----  ------------------  -----------------------------------------
// 0x00	  No error	          This shows there is no current error state.
// 0x01	  Invalid RPC packet	RPC packet was malformed/invalid.
// 0x02	  Unknown RPC command	The command sent is unknown.
// 0x03	  Unable to connect	  The credentials have been received and an attempt to connect 
//                            to the network has failed.
// 0xFF	  Unknown Error	
typedef enum  {
  IMPROV_ERROR_NONE = 0x00,
  IMPROV_ERROR_INVALID_RPC = 0x01,
  IMPROV_ERROR_UNKNOWN_RPC = 0x02,
  IMPROV_ERROR_UNABLE_TO_CONNECT = 0x03,
  IMPROV_ERROR_NOT_AUTHORIZED = 0x04,
  IMPROV_ERROR_UNKNOWN = 0xFF,
} ImprovError_t;


// =========================================================================================
// Packet: RPC Command
// Type: 0x03
// Direction: Client to device
// --------------------------------------
// This packet type is used for the client to send commands to the device. When an RPC 
// command is sent, the device should sent an update to the client to set the error state to 
// 0 (no error). The response will either be an RPC result packet or an error state update.

// Byte	  Description
// -----  ---------------------
// 1	    Command (see below)
// 2	    Data length
// 3...X	Data
typedef enum   {
  IMPROV_CMD_UNKNOWN = 0x00,
  IMPROV_CMD_WIFI_SETTINGS = 0x01,
  IMPROV_CMD_GET_CURRENT_STATE = 0x02,
  IMPROV_CMD_GET_DEVICE_INFO = 0x03,
  IMPROV_CMD_GET_WIFI_NETWORKS = 0x04,
  IMPROV_CMD_BAD_CHECKSUM = 0xFF,
} ImprovCommand_t;

// ======================================
// RPC Command: Send Wi-Fi settings
// Submit Wi-Fi credentials to the Improv Service to attempt to connect to.
// Type: 0x03
// Command ID: 0x01

// Byte	  Description
// -----  ----------------
// 1	    command (0x01)
// 2	    data length
// 3	    ssid length
// 4...X	ssid bytes
// X	    password length
// X...Y	password bytes

// Example: SSID = MyWirelessAP, Password = mysecurepassword
// 01 1E 0C {MyWirelessAP} 10 {mysecurepassword}
// This command will generate an RPC result. The first entry in the list is an URL to 
// redirect the user to. 
// If there is no URL, omit the entry or add an empty string.

// ======================================
// RPC Command: Request current state
// Sends a request for the device to send the current state of improv to the client.

// Type: 0x03
// Command ID: 0x02

// Byte	  Description
// -----  ----------------
// 1	    command (0x02)
// 2	    data length (0)
// This command will trigger at least one packet, the Current State (see above) and if 
// already provisioned, 
// the same response you would get if device provisioning was successful (see below).

// ======================================
// RPC Command: Request device information
// Sends a request for the device to send information about itself.

// Type: 0x03
// Command ID: 0x03

// Byte	  Description
// -----  ----------------
// 1	    command (0x02)
// 2	    data length (0)

// This command will trigger one packet, the Device Information formatted as a RPC result. 
// This result will contain at least 4 strings.

// Order of strings: Firmware name, firmware version, hardware chip/variant, device name.

// Example: ESPHome, 2021.11.0, ESP32-C3, Temperature Monitor.

// ======================================
// RPC Command: Request scanned Wi-Fi networks
// Sends a request for the device to send the Wi-Fi networks it sees.

// Type: 0x03
// Command ID: 0x04

// Byte	  Description
// -----  ----------------
// 1	    command (0x02)
// 2	    data length (0)
// This command will trigger at least one RPC Response. Each response will contain at 
// least 3 strings.

// Order of strings: Wi-Fi SSID, RSSI, Auth required.

// Example: MyWirelessNetwork, -60, YES.

// The final response (or the first if no networks are found) will have 0 strings in the body.

// =========================================================================================
// Packet: RPC Result
// ======================================
// Type: 0x04
// Direction: Device to client
// --------------------------------------
// This packet type contains the response to an RPC command. Results are returned as a list 
// of strings. An empty list is allowed.

// Byte	  Description
// -----  ----------------
// 1	    Command being responded to (see above)
// 2	    Data length
// 3	    Length of string 1
// 4...X	String 1
// X	    Length of string 2
// X...Y	String 2
// ...	  etc









static const uint8_t CAPABILITY_IDENTIFY = 0x01;
static const uint8_t IMPROV_SERIAL_VERSION = 1;

#ifndef FREE_AND_NULL
#define FREE_AND_NULL(x) if(x) { free(x); x=NULL; }
#endif
#ifndef ENUM_TO_STRING
#define ENUM_TO_STRING(g) 	\
    case g:    				\
        return STR(g);    	\
        break;
#endif


typedef struct  {
  ImprovCommand_t command;
  char * ssid;
  char * password;
} ImprovCommandStruct_t;
typedef struct {
  char * ssid;
  char * rssi;
  char * auth_req; // YES/NO
} ImprovAPListStruct_t;
#define IMPROV_AP_STRUCT_NUM_STR 3
typedef struct {
  char * firmware_name;
  char * firmware_version;
  char * hardware_chip_variant;
  char * device_name;
  char * nullptr;
} ImprovDeviceInfoStruct_t;
#define IMPROV_DEVICE_INFO_NUM_STRINGS 4

typedef bool (*improv_command_callback_t)(ImprovCommandStruct_t *cmd);
typedef void (*on_error_callback_t)(ImprovError_t error);
typedef bool (*improv_send_callback_t)(uint8_t * buffer, size_t length);
typedef struct {
  int index ;
  improv_command_callback_t callback ;
} callback_table_t;

void improv_parse_data(ImprovCommandStruct_t * improv_command, const uint8_t *data, size_t length, bool check_checksum) ;
bool improv_parse_serial_byte(size_t position, uint8_t byte, const uint8_t *buffer,improv_command_callback_t callback, on_error_callback_t on_error);
bool parse_improv_serial_line( const uint8_t *buffer);
void improv_set_send_callback(improv_send_callback_t callback );

bool improv_set_callback(ImprovCommand_t command, improv_command_callback_t callback  );
bool improv_wifi_list_allocate(size_t num_entries);
bool improv_wifi_list_add(const char * ssid, int8_t rssi, bool auth_req );
bool improv_wifi_list_send(  );
size_t improv_wifi_get_wifi_list_count();
bool improv_send_device_info(  const char * firmware_name, const char * firmware_version,  const char * hardware_chip_variant,  const char * device_name);
uint8_t * improv_build_response(ImprovSerialType_t command, const char * datum, size_t len, size_t * out_len);
uint8_t * improv_build_rpc_response(ImprovCommand_t command, const char ** results, size_t num_strings, size_t * out_len);
bool improv_send_current_state(ImprovState_t state);
bool improv_send_error(ImprovError_t error);
const char * improv_get_error_desc(ImprovError_t error);
const char * improv_get_command_desc(ImprovCommand_t command);
bool improv_send_device_url( ImprovCommand_t from_command, const char * url);
// Improv Wi-Fi   –   Contact   –   GitHub
// Improv is an initiative by ESPHome & Home Assistant.
// Development funded by Nabu Casa.