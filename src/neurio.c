/*
 * =====================================================================================
 *
 *       Filename:  neurio.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  12/25/2018 02:40:13 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Stuart B. Wilkins (sbw), stuwilkins@mac.com
 *   Organization:  
 *
 * =====================================================================================
 */

#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200201L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h> 
#include <unistd.h>
#include <signal.h>

#include <MQTTClient.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <libconfig.h>

#include "neurio.h"

#ifdef DEBUG
static int verbose_flag = 1;
#else
static int verbose_flag = 0;
#endif

volatile int sigterm = false;
volatile MQTTClient_deliveryToken deliveredtoken;

void term(int signum)
{
  sigterm = true;
}

int string_to_epoch(const char* str, unsigned long *epoch)
{
  struct tm _ts;
  char *_tz;

  tzset();
  memset(&_ts, 0, sizeof(struct tm));

  _tz = strptime(str, "%Y-%m-%dT%H:%M:%S", &_ts); 

#ifdef DEBUG
  char _buf[80];
  strftime(_buf, 80, "%Y-%m-%d %H:%M:%S", &_ts);
  debug_print("strptime returned with \"%s\" to time %s\n", _tz, _buf);
#endif

  if(strcmp(_tz, "Z"))
  {
    return false;
  }

  *epoch = (unsigned long)mktime(&_ts) - timezone;
  debug_print("Unix timestamp = %ld\n", *epoch);

  return true;
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct DataStruct *data = (struct DataStruct *)userp;

  debug_print("size = %d, nmemb = %d\n", 
      (int)size, (int)nmemb);
  debug_print("buffer = %p, buffer_counter = %d\n", 
      data->buffer, (int)data->buffer_counter);

  if((realsize + data->buffer_counter) >= data->buffer_size){
    return 0;
  }

  memcpy(&(data->buffer[data->buffer_counter]), contents, realsize);
  data->buffer_counter += realsize;
  data->buffer[data->buffer_counter] = 0; // Add null terminator

  return realsize;
}

int get_neurio_data(struct DataStruct *data)
{
  CURLcode res;

  data->buffer_counter = 0;
  res = curl_easy_perform(data->curl_handle);

  if(res != CURLE_OK) {
    debug_print("curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
    return false;
  } 

  return true;
}

int calculate_single(struct sensor_reading *reading)
{
  double phi;

  phi = atan2(reading->Q, reading->P);
  debug_print("phi = %lf\n", phi);

  reading->phi = phi * 180 / M_PI;
  reading->S = sqrt(pow(reading->P, 2) + pow(reading->Q, 2));
  reading->pf = fabs(cos(phi));
  reading->I = reading->P / (reading->V * cos(phi));

  return true;
}

int calculate_total(struct sensor_reading *reading)
{
  int n = NUM_SENSORS;
  reading[n].V = reading[0].V + reading[1].V; 
  reading[n].P = reading[0].P + reading[1].P; 
  reading[n].Q = reading[0].Q + reading[1].Q; 
  return true;
}

int parse_neurio_data(struct DataStruct *data)
{
  struct json_tokener *tok;
  struct json_object *jobj;

  tok = json_tokener_new();
  jobj = json_tokener_parse_ex(tok, data->buffer, strlen(data->buffer));

  enum json_tokener_error jerr = json_tokener_get_error(tok);
  if(jerr != json_tokener_success)
  {
    debug_print("Error parsing json : %s\n", json_tokener_error_desc(jerr));
    json_tokener_free(tok);
    return false;
  }

  debug_statement("Parsed json OK...\n");

  if(data->use_timestamp)
  {
    // Get the timestamp
    struct json_object *ts;
    const char *_tss;

    json_object_object_get_ex(jobj, "timestamp", &ts);
    _tss = json_object_get_string(ts);
    debug_print("Timestamp = %s\n", _tss);

    string_to_epoch(_tss, &data->timestamp);
  }

  // Get the sensors
  struct json_object *cts;
  json_object_object_get_ex(jobj, "cts", &cts);

  int arraylen;
  arraylen = json_object_array_length(cts);
  debug_print("arraylen = %d\n", arraylen);

  if(arraylen < NUM_SENSORS)
  {
    json_tokener_free(tok);
    return false;
  }

  for(int i=0;i<arraylen;i++)
  {
    struct json_object *_cts;
    _cts = json_object_array_get_idx(cts, i);

    struct json_object *q_VAR, *v_V, *p_W, *ct;
    json_object_object_get_ex(_cts, "ct", &ct);
    int n = json_object_get_int(ct) - 1;

    if(n >= NUM_SENSORS)
    {
      continue;
    }
   
    json_object_object_get_ex(_cts, "v_V", &v_V);
    json_object_object_get_ex(_cts, "p_W", &p_W);
    json_object_object_get_ex(_cts, "q_VAR", &q_VAR);

    data->reading[n].V = json_object_get_double(v_V);
    data->reading[n].P = json_object_get_double(p_W);
    data->reading[n].Q = json_object_get_double(q_VAR);

    debug_print("%d : V\t=%lf V\n", n, data->reading[n].V);
    debug_print("%d : W\t=%lf W\n", n, data->reading[n].P);
    debug_print("%d : Q\t=%lf W\n", n, data->reading[n].Q);

    calculate_single(&data->reading[n]);

    debug_print("%d : S\t=%lf W\n", n, data->reading[n].S);
    debug_print("%d : phi\t=%lf deg\n", n, data->reading[n].phi);
    debug_print("%d : pf\t=%lf\n", n, data->reading[n].pf);
    debug_print("%d : I\t=%lf A\n", n, data->reading[n].I);
  }

  calculate_total(data->reading);

  int n = NUM_SENSORS;
  calculate_single(&data->reading[n]);
  debug_print("total : V\t=%lf\n", data->reading[n].V);
  debug_print("total : W\t=%lf\n", data->reading[n].P);
  debug_print("total : Q\t=%lf\n", data->reading[n].Q);
  debug_print("total : S\t=%lf W\n", data->reading[n].S);
  debug_print("total : phi\t=%lf deg\n", data->reading[n].phi);
  debug_print("total : pf\t=%lf\n", data->reading[n].pf);
  debug_print("total : I\t=%lf A\n", data->reading[n].I);

  json_object_put(jobj); // Decrement reference count
  json_tokener_free(tok);
  return true;
}

void add_sensor(char *buffer, const char* name, struct sensor_reading *reading, 
    int last)
{
  char _buffer[PAYLOAD_MAX] = "";

  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s_voltage\":%lf,",
      name, reading->V);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s_real_power\":%lf,",
      name, reading->P);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s_imag_power\":%lf,",
      name, reading->Q);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s_pf\":%lf,",
      name, reading->pf);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s_phi\":%lf,",
      name, reading->phi);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s_app_power\":%lf,",
      name, reading->S);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s_current\":%lf",
      name, reading->I);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);

  if(!last)
  {
    strncat(buffer, ",", PAYLOAD_MAX-1);
  }
}

void mqtt_delivered(void *context, MQTTClient_deliveryToken dt)
{
  debug_print("Message with token value %d delivery confirmed\n", dt);
  deliveredtoken = dt;
}

int mqtt_msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
  debug_statement("Message arrived\n");
  debug_print("topic: %s\n", topicName);
  debug_print("message: %s", (char *)message->payload);

  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);
  return 1;
}

void mqtt_connlost(void *context, char *cause)
{
  debug_print("Connection lost cause: %s\n", cause);
}

int publish_to_thingsboard(struct DataStruct *data)
{
  int rc;
  char payload_buffer[PAYLOAD_MAX] = "";
  char _buffer[PAYLOAD_MAX] = "";

  if(data->use_timestamp)
  {
    snprintf(_buffer, PAYLOAD_MAX,
        "{\"ts\":%lu000,\"values\":{", data->timestamp);
    strncat(payload_buffer, _buffer, PAYLOAD_MAX-1);
  } else {
    strncat(payload_buffer, "{", PAYLOAD_MAX-1);
  }


  for(int i=0;i<NUM_SENSORS;i++)
  {
    char name[20];
    snprintf(name, 20, "sensor%d", i + 1);
    add_sensor(payload_buffer, name, &data->reading[i], 0);
  }
  add_sensor(payload_buffer, "total", &data->reading[NUM_SENSORS], 1);

  strncat(payload_buffer, "}", PAYLOAD_MAX-1);
  if(data->use_timestamp)
  {
    strncat(payload_buffer, "}", PAYLOAD_MAX-1);
  }

  debug_print("payload [%d] : %s\n", (int)strlen(payload_buffer), payload_buffer);

  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  pubmsg.payload = payload_buffer;
  pubmsg.payloadlen = strlen(payload_buffer);
  pubmsg.qos = QOS;
  pubmsg.retained = 0;

  MQTTClient_deliveryToken token;
  rc = MQTTClient_publishMessage(data->client, TOPIC, &pubmsg, &token);
  if(rc != MQTTCLIENT_SUCCESS)
  {
    debug_print("MQTTClient_publishMessage failed %d\n", rc);
  }

  while(deliveredtoken != token)
  {
    if(sigterm)
    {
      break;
    }
  };

  debug_print("Message with delivery token %d delivered\n", token);
  return true;
}

int read_config(struct DataStruct *data, const char *config_file)
{
  config_t cfg;
  const char *str;

  config_init(&cfg);

  if(!config_read_file(&cfg, config_file))
  {
    fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
        config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return false;
  }

  long long sleep;
  if(config_lookup_int64(&cfg, "interval", &sleep))
  {
    data->sleep.tv_sec = sleep / 1000;
    data->sleep.tv_nsec = (sleep % 1000) * 1000000L;
  }

  int buffer_size;
  if(config_lookup_int(&cfg, "buffer_size", &buffer_size))
  {
    data->buffer_size = (size_t)buffer_size;
  }

  if(config_lookup_string(&cfg, "neurio.host", &str))
  {
    strncpy(data->neurio_host, str, STR_MAX);
  }
  if(config_lookup_string(&cfg, "mqtt.host", &str))
  {
    strncpy(data->mqtt_host, str, STR_MAX);
  }
  if(config_lookup_string(&cfg, "mqtt.token", &str))
  {
    strncpy(data->mqtt_username, str, STR_MAX);
  }
  config_lookup_int(&cfg, "mqtt.port", &data->mqtt_port);

  config_destroy(&cfg);
  return true;
}

int main(int argc, char* argv[])
{
  struct DataStruct data;
  int parse = 1;
  int publish = 1;

  debug_statement("Starting .....\n");

  // Set defaults

  data.mqtt_port = DEFAULT_MQTT_PORT;
  strncpy(data.mqtt_host, DEFAULT_MQTT_HOST, STR_MAX);
  strncpy(data.neurio_host, DEFAULT_NEURIO_HOST, STR_MAX);
  strncpy(data.mqtt_username, DEFAULT_MQTT_USERNAME, STR_MAX);
  strncpy(data.mqtt_password, DEFAULT_MQTT_PASSWORD, STR_MAX);
  strncpy(data.config_file, DEFAULT_CONFIG_FILE, STR_MAX);
  data.sleep.tv_sec = DEFAULT_SLEEP;
  data.sleep.tv_nsec = 0;
  data.buffer_size = 2048 * 1024;
  data.use_timestamp = false;

  // Parse the command line

  while(1)
  {
		static struct option long_options[] =
		{
			{"no-parse",     no_argument,       0, 'x'},
			{"no-publish",   no_argument,       0, 'y'},
			{"verbose",      no_argument,       0, 'v'},
			{"quiet",        no_argument,       0, 'q'},
			{"config-file",  required_argument, 0, 'c'},
			{0, 0, 0, 0}
		};

		int option_index = 0;
    int c = getopt_long(argc, argv, "c:xyqv", long_options, &option_index);

    if(c == -1)
    {
      break;
    }

    switch(c)
    {
      case 0:
        if(long_options[option_index].flag != 0)
        {
          break;
        }
        debug_print("option %s", long_options[option_index].name);
        break;
      case 'x':
        parse = false;
        break;
      case 'y':
        publish = false;
        break;
      case 'v':
        verbose_flag = true;
        break;
      case 'q':
        verbose_flag = false;
        break;
      case 'c':
        strncpy(data.config_file, optarg, STR_MAX);
        break;
      case '?':
        break;
      default:
        debug_print("Exiting due to getopt ... (%c)\n", c);
        return EXIT_FAILURE;
    }
  }

  debug_print("config_file = %s\n", data.config_file);
  read_config(&data, data.config_file);

  // Dump config 
  
  debug_print("mqtt_username = %s\n", data.mqtt_username);
  debug_print("mqtt_password = %s\n", data.mqtt_password);
  debug_print("mqtt_host = %s\n", data.mqtt_host);
  debug_print("mqtt_port = %d\n", data.mqtt_port);
  debug_print("neurio_host = %s\n", data.neurio_host);
  debug_print("sleep = %ld, %ld\n", data.sleep.tv_sec, 
      data.sleep.tv_nsec);
  debug_print("buffer_size = %d\n", (int)data.buffer_size);

  // Allocate CURL buffer.

  if(!(data.buffer = malloc(data.buffer_size)))
  {
    fprintf(stderr, "Unable to allocate memory for buffer.\n\n");
    return EXIT_FAILURE;
  }
  data.buffer_counter = 0;
  debug_print("Allocated buffer at %p of size %d\n", data.buffer, 
      (int)data.buffer_size);

  // Setup cURL lib
  curl_global_init(CURL_GLOBAL_NOTHING);
  data.curl_handle = curl_easy_init();
  if(!data.curl_handle)
  {
    fprintf(stderr, "Unable to setup cURL library.\n\n");
    return EXIT_FAILURE;
  }

  char _host[STR_MAX];
  snprintf(_host, STR_MAX, "http://%s/current-sample", data.neurio_host);
  debug_print("cURL host = %s\n", _host);

  curl_easy_setopt(data.curl_handle, CURLOPT_URL, _host);
  curl_easy_setopt(data.curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(data.curl_handle, CURLOPT_WRITEDATA, (void *)&data);
  curl_easy_setopt(data.curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  // Setup MQTT
  snprintf(data.mqtt_client_id, STR_MAX, "%s_%ld", 
      MQTT_CLIENT_ID, (long int)getpid());
  debug_print("mqtt_client_id = %s\n", data.mqtt_client_id);

  char _address[STR_MAX];
  snprintf(_address, STR_MAX, "tcp://%s:%d", data.mqtt_host, data.mqtt_port);

  MQTTClient_create(&data.client, _address, data.mqtt_client_id,
      MQTTCLIENT_PERSISTENCE_NONE, NULL);

  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

  conn_opts.keepAliveInterval = 20;
  conn_opts.cleansession = 1;
  conn_opts.username = data.mqtt_username;
  conn_opts.password = data.mqtt_password;

  MQTTClient_setCallbacks(data.client, NULL, mqtt_connlost, mqtt_msgarrvd, mqtt_delivered);

  int rc;
  if ((rc = MQTTClient_connect(data.client, &conn_opts)) != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to connect, return code %d\n", rc);
    return false;
  }


  // Now setup sigterm handler
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = term;
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT,  &action, NULL);

  for(;;)
  {
    if(!get_neurio_data(&data))
    {
      continue;
    }
    if(parse)
    {
      if(!parse_neurio_data(&data))
      {
        continue;
      }

      if(publish)
      {
        publish_to_thingsboard(&data);
      }
    }

    if(sigterm)
    {
      debug_statement("Exiting loop due to signal.\n");
      break;
    }
    nanosleep(&data.sleep, NULL);
  }

  free(data.buffer);

  curl_easy_cleanup(data.curl_handle);
  curl_global_cleanup();

  MQTTClient_disconnect(data.client, 10000);
  MQTTClient_destroy(&data.client);

  return 0;
}
