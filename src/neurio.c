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

#include <MQTTClient.h>
#include <curl/curl.h>
#include <json-c/json.h>

#include "neurio.h"

#ifdef DEBUG
static int verbose_flag = 1;
#else
static int verbose_flag = 0;
#endif


int string_to_epoch(const char* str, unsigned long *epoch)
{
  struct tm _ts;
  char *_tz;
  char buf[255];

  tzset();
  debug_statement("tzset()\n");
  memset(&_ts, 0, sizeof(struct tm));

  _tz = strptime(str, "%FT%T", &_ts); 

#ifdef DEBUG
  char _buf[80];
  strftime(_buf, 80, "%Y:%m:%d %H:%M:%S", &_ts);
  debug_print("strptime returned with \"%s\" to time %s\n", _tz, _buf);
#endif

  if(strcmp(_tz, "Z"))
  {
    return false;
  }

  strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S", &_ts);
  *epoch = (unsigned long)mktime(&_ts) - timezone;

  debug_print("Timestamp = %s\n", buf);
  debug_print("Unix timestamp = %ld\n", *epoch);

  return true;
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct DataStruct *mem = (struct DataStruct *)userp;
  char *ptr = realloc(mem->raw_data, mem->raw_size + realsize + 1);

  debug_print("%p %d %d\n", ptr, (int)mem->raw_size, (int)realsize);

  if(ptr == NULL) {
    // Out of memory!
    debug_print("not enough memory (realloc returned NULL to %ld)\n", (long int)(size*nmemb));
    return 0;
  }

  mem->raw_data = ptr;
  memcpy(&(mem->raw_data[mem->raw_size]), contents, realsize);
  mem->raw_size += realsize;
  mem->raw_data[mem->raw_size] = 0;

  return realsize;
}


int get_neurio_data(struct DataStruct *data)
{
  CURL *curl_handle;
  CURLcode res;

  char _host[STR_MAX];
  snprintf(_host, STR_MAX, "http://%s/current-sample", data->neurio_host);
  debug_print("host = %s\n", _host);

  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_URL, _host);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)data);
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
  
  debug_statement("Setup curl\n");

  data->raw_size = 0;
  res = curl_easy_perform(curl_handle);

  debug_statement("Finished curl\n");

  curl_easy_cleanup(curl_handle);

  if(res != CURLE_OK) {
    debug_print("curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
    return false;
  } 

  return true;
}

int parse_neurio_data(struct DataStruct *data)
{
  struct json_object *jobj;

  debug_statement("Parsing neurio data ....\n");

  jobj = json_tokener_parse(data->raw_data);

  //debug_print("jobj from str:\n---\n%s\n---\n", 
  //    json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED | 
  //      JSON_C_TO_STRING_PRETTY));

  // Get the timestamp
  struct json_object *ts;
  json_object_object_get_ex(jobj, "timestamp", &ts);
  const char *_tss;
  _tss = json_object_get_string(ts);
  debug_print("Timestamp = %s\n", _tss);

  string_to_epoch(_tss, &data->timestamp);

  struct json_object *cts;
  json_object_object_get_ex(jobj, "cts", &cts);

  int arraylen;
  arraylen = json_object_array_length(cts);
  debug_print("arraylen = %d\n", arraylen);

  if(arraylen < NUM_SENSORS)
  {
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
    data->reading[n].phi = asin(data->reading[i].Q / data->reading[n].P) * 180.0 / M_PI;
    data->reading[n].pf = data->reading[n].P / pow(pow(data->reading[n].P,2) + pow(data->reading[n].Q,2), 0.5);

    debug_print("%d : V\t=%lf\n", n, data->reading[n].V);
    debug_print("%d : W\t=%lf\n", n, data->reading[n].P);
    debug_print("%d : Q\t=%lf\n", n, data->reading[n].Q);
    debug_print("%d : phi\t=%lf deg\n", n, data->reading[n].phi * 180.0 / M_PI);
    debug_print("%d : pf\t=%lf deg\n", n, data->reading[n].pf);
  }

  return true;
}

void add_sensor(char *buffer, const char* name, struct sensor_reading *reading, 
    int i, int last)
{
  char _buffer[PAYLOAD_MAX] = "";

  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s%d_voltage\":\"%lf\",",
      name, i + 1, reading->V);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);

  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s%d_real_power\":\"%lf\",",
      name, i + 1, reading->P);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s%d_imag_power\":\"%lf\",",
      name, i + 1, reading->Q);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s%d_pf\":\"%lf\",",
      name, i + 1, reading->pf);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);
  snprintf(_buffer, PAYLOAD_MAX, 
      "\"%s%d_phi\":\"%lf\"",
      name, i + 1, reading->phi);
  strncat(buffer, _buffer, PAYLOAD_MAX-1);

  if(!last)
  {
    strncat(buffer, ",", PAYLOAD_MAX-1);
  }
}

int publish_to_thingsboard(struct DataStruct *data)
{
  int rc;
  char payload_buffer[PAYLOAD_MAX] = "";
  char _buffer[PAYLOAD_MAX] = "";

  snprintf(_buffer, PAYLOAD_MAX,
      "{\"ts\":%lu000,\"values\":{", data->timestamp);
  strncat(payload_buffer, _buffer, PAYLOAD_MAX-1);

  for(int i=0;i<NUM_SENSORS;i++)
  {
    add_sensor(payload_buffer, "sensor", &data->reading[i], i, i == (NUM_SENSORS -1));
  }

  strncat(payload_buffer, "}}", PAYLOAD_MAX-1);
  debug_print("payload [%d] : %s\n", (int)strlen(payload_buffer), payload_buffer);

  MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
  MQTTClient_message pubmsg = MQTTClient_message_initializer;
  MQTTClient_deliveryToken token;

  conn_opts.keepAliveInterval = 20;
  conn_opts.cleansession = 1;
  conn_opts.username = data->mqtt_token;
  conn_opts.password = "";

  if ((rc = MQTTClient_connect(data->client, &conn_opts)) != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to connect, return code %d\n", rc);
    return false;
  }

  pubmsg.payload = payload_buffer;
  pubmsg.payloadlen = strlen(payload_buffer);
  pubmsg.qos = QOS;
  pubmsg.retained = 0;

  MQTTClient_publishMessage(data->client, TOPIC, &pubmsg, &token);

  debug_print("Waiting for up to %d seconds for publication of %s\n"
      "on topic %s for client with ClientID: %s\n",
      (int)(TIMEOUT/1000), payload_buffer, TOPIC, data->mqtt_client_id);

  rc = MQTTClient_waitForCompletion(data->client, token, TIMEOUT);

  debug_print("Message with delivery token %d delivered\n", token);

  MQTTClient_disconnect(data->client, 10000);

  return true;
}

int main(int argc, char* argv[])
{
  struct DataStruct data;

  data.raw_data =  NULL;
  data.raw_size = 0;

  debug_statement("Starting .....\n");

  // Set defaults

  data.mqtt_port = DEFAULT_MQTT_PORT;
  strncpy(data.mqtt_host, DEFAULT_MQTT_HOST, STR_MAX);
  strncpy(data.neurio_host, DEFAULT_NEURIO_HOST, STR_MAX);
  strncpy(data.mqtt_token, DEFAULT_MQTT_TOKEN, STR_MAX);
  data.sleep.tv_sec = DEFAULT_SLEEP;
  data.sleep.tv_nsec = 0;

  // Parse the command line

  while(1)
  {
    long int sleep;
		static struct option long_options[] =
		{
			/* These options set a flag. */
			{"verbose",   no_argument,       0, 'v'},
			{"breif",     no_argument,       0, 'b'},
			{"token",     required_argument, 0, 't'},
			{"host",      required_argument, 0, 'h'},
			{"port",      required_argument, 0, 'p'},
			{"neurio",    required_argument, 0, 'n'},
			{"sleep",     required_argument, 0, 's'},
			{0, 0, 0, 0}
		};

		int option_index = 0;
    int c = getopt_long(argc, argv, "vbt:h:p:n:s:", long_options, &option_index);

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
      case 'v':
        verbose_flag = 1;
        break;
      case 'b':
        verbose_flag = 0;
        break;
      case 't':
        strncpy(data.mqtt_token, optarg, STR_MAX);
        debug_print("mqtt_token = %s\n", data.mqtt_token);
        break;
      case 'h':
        strncpy(data.mqtt_host, optarg, STR_MAX);
        debug_print("mqtt_host = %s\n", data.mqtt_host);
        break;
      case 'p':
        sscanf(optarg, "%d", &data.mqtt_port);
        debug_print("mqtt_port = %d\n", data.mqtt_port);
        break;
      case 'n':
        strncpy(data.neurio_host, optarg, STR_MAX);
        debug_print("neurio_host = %s\n", data.neurio_host);
        break;
      case 's':
        sscanf(optarg, "%ld", &sleep);
        data.sleep.tv_sec = sleep / 1000;
        data.sleep.tv_nsec = (sleep % 1000) * 1000L;
        debug_print("sleep = %ld, %ld\n", data.sleep.tv_sec, 
            data.sleep.tv_nsec);
      case '?':
        break;
      default:
        debug_print("Exiting due to getopt ... (%c)\n", c);
        return EXIT_FAILURE;
    }
  }

  // Make client ID
  snprintf(data.mqtt_client_id, STR_MAX, "%s_%ld", 
      MQTT_CLIENT_ID, (long int)getpid());
  debug_print("mqtt_client_id = %s\n", data.mqtt_client_id);


  curl_global_init(CURL_GLOBAL_ALL);

  char _address[STR_MAX];
  snprintf(_address, STR_MAX, "tcp://%s:%d", data.mqtt_host, data.mqtt_port);

  MQTTClient_create(&data.client, _address, data.mqtt_client_id,
      MQTTCLIENT_PERSISTENCE_NONE, NULL);

  for(;;)
  {
    if(!get_neurio_data(&data))
    {
      continue;
    }
    if(!parse_neurio_data(&data))
    {
      continue;
    }
    publish_to_thingsboard(&data);
    nanosleep(&data.sleep, NULL);
  }

  free(data.raw_data);
  curl_global_cleanup();
  MQTTClient_destroy(&data.client);

  return 0;
}
