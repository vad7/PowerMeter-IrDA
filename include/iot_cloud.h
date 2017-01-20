
#define DEFAULT_TC_HOST_PORT 80
#define TCP_REQUEST_TIMEOUT	15 // sec

extern const uint8 iot_get_request_tpl[];
extern const uint8 key_http_ok[];
extern uint8 iot_cloud_ini[];
extern char iot_last_status[]; // last status/error returned by server
extern time_t iot_last_status_time;

char *iot_server_name; // = NULL; // set from ini: iot_server=
typedef struct _IOT_DATA {
	uint32	min_interval; 	// msec
	uint32	last_run;		// system_get_time()
	struct _IOT_DATA *next;
	char	iot_request[];		// set from ini: iot_add=
} IOT_DATA;
IOT_DATA *iot_data_first; // = NULL;
IOT_DATA *iot_data_processing; // = NULL;

