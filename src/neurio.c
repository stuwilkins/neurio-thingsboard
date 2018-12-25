#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define M_PI        3.14159265358979323846

#define true -1
#define false 0

struct MemoryStruct {
  char *memory;
  size_t size;
};

struct sensor_reading {
  int n;
  double P;
  double Q;
  double V;
  double phi;
  double pf;
};

int string_to_epoch(const char* str, unsigned long *epoch)
{
  struct tm _ts;
  char *_tz;
  char buf[255];

  tzset();
  memset(&_ts, 0, sizeof(struct tm));

  _tz = strptime(str, "%FT%T", &_ts); 
  if(strcmp(_tz, "Z"))
  {
    return false;
  }

  strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S", &_ts);
  *epoch = (unsigned long)mktime(&_ts) - timezone;

  fprintf(stderr, "Timestamp = %s\n", buf);
  fprintf(stderr, "Unix timestamp = %ld\n", *epoch);

  return true;
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */
    fprintf(stderr, "WriteMemoryCallback() : not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

int main(void)
{
  CURL *curl_handle;
  CURLcode res;

  struct MemoryStruct chunk;

  chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
  chunk.size = 0;    /* no data at this point */

  curl_global_init(CURL_GLOBAL_ALL);

  /* init the curl session */
  curl_handle = curl_easy_init();

  /* specify URL to get */
  curl_easy_setopt(curl_handle, CURLOPT_URL, "http://neurio.lan/current-sample");

  /* send all data to this function  */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

  /* we pass our 'chunk' struct to the callback function */
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

  /* some servers don't like requests that are made without a user-agent
     field, so we provide one */
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

  /* get it! */
  res = curl_easy_perform(curl_handle);

  /* check for errors */
  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  } else {
    struct json_object *jobj;
    jobj = json_tokener_parse(chunk.memory);

    fprintf(stderr, "jobj from str:\n---\n%s\n---\n", 
        json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_SPACED | 
          JSON_C_TO_STRING_PRETTY));

    // Get the timestamp
    struct json_object *ts;
    json_object_object_get_ex(jobj, "timestamp", &ts);
    const char *_tss;
    _tss = json_object_get_string(ts);
    unsigned long epoch;
    string_to_epoch(_tss, &epoch);

    struct json_object *cts;
    json_object_object_get_ex(jobj, "cts", &cts);

    int arraylen;
    arraylen = json_object_array_length(cts);

    for(int i=0;i<arraylen;i++)
    {
      struct json_object *_cts;
      _cts = json_object_array_get_idx(cts, i);

      struct json_object *q_VAR, *v_V, *p_W, *ct;
      json_object_object_get_ex(_cts, "ct", &ct);
      json_object_object_get_ex(_cts, "v_V", &v_V);
      json_object_object_get_ex(_cts, "p_W", &p_W);
      json_object_object_get_ex(_cts, "q_VAR", &q_VAR);

      struct sensor_reading reading;
    
      reading.n = json_object_get_int(ct);
      reading.V = json_object_get_double(v_V);
      reading.P = json_object_get_double(p_W);
      reading.Q = json_object_get_double(q_VAR);
      reading.phi = asin(reading.Q / reading.P);
      reading.pf = reading.P / pow(pow(reading.P,2) + pow(reading.Q,2), 0.5);

      printf("%d : V\t=%lf\n", reading.n, reading.V);
      printf("%d : W\t=%lf\n", reading.n, reading.P);
      printf("%d : Q\t=%lf\n", reading.n, reading.Q);
      printf("%d : phi\t=%lf deg\n", reading.n, reading.phi * 180.0 / M_PI);
      printf("%d : pf\t=%lf deg\n", reading.n, reading.pf);
    }
  }

  /* cleanup curl stuff */
  curl_easy_cleanup(curl_handle);
  free(chunk.memory);

  /* we're done with libcurl, so clean it up */
  curl_global_cleanup();

  return 0;
}
