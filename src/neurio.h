/*
 * =====================================================================================
 *
 *       Filename:  neurio.h
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

#ifndef _NEURIO_H
#define _NEURIO_H

#include <MQTTClient.h>
#include <curl/curl.h>

#define M_PI                    3.14159265358979323846

#define true                    -1
#define false                   0

#define QOS                     1
#define TIMEOUT                 100000L

#define PAYLOAD_MAX             4096
#define STR_MAX                 1024
#define NUM_SENSORS             2

#define MQTT_CLIENT_ID          "neuro_tb"

#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_MQTT_HOST       "localhost"
#define DEFAULT_MQTT_USERNAME   ""
#define DEFAULT_MQTT_PASSWORD   ""
#define DEFAULT_MQTT_TOPIC      "v1/devices/me/telemetry"
#define DEFAULT_NEURIO_HOST     "192.168.1.1"
#define DEFAULT_SLEEP           2
#define DEFAULT_CONFIG_FILE     "neurio-thingsboard.cfg"
#define DEFAULT_NEURIO_USERNAME ""
#define DEFAULT_NEURIO_PASSWORD ""

#define debug_print(fmt, ...) \
  do { if (verbose_flag) fprintf(stderr, "%s:%d:%s(): " fmt, \
      __FILE__, __LINE__, __func__, __VA_ARGS__); } while (0)

#define debug_statement(statement) \
  do { if (verbose_flag) fprintf(stderr, "%s:%d:%s(): " statement, \
      __FILE__, __LINE__, __func__); } while (0)

struct sensor_reading {
  double P;
  double Q;
  double V;
  double S;
  double I;
  double phi;
  double pf;
};

struct DataStruct {
  // For config file
  char config_file[STR_MAX];

  // For Curl
  CURL *curl_handle;

  // For raw data
  char *buffer;
  size_t buffer_counter;
  size_t buffer_size;

  // For update
  struct timespec sleep;

  // For MQTT
  MQTTAsync mqtt_client;
  MQTTAsync_connectOptions mqtt_conn_opts;
  char mqtt_username[STR_MAX];
  char mqtt_password[STR_MAX];
  char mqtt_topic[STR_MAX];
  char mqtt_host[STR_MAX];
  char mqtt_client_id[STR_MAX];
  int mqtt_port;

  // For neurio
  char neurio_host[STR_MAX];
  char neurio_username[STR_MAX];
  char neurio_password[STR_MAX];

  // Data
  int use_timestamp;
  unsigned long timestamp;
  struct sensor_reading reading[NUM_SENSORS + 1]; 
  struct sensor_reading calc;
};

#endif
