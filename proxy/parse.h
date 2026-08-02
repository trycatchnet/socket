#ifndef PARSE_H
#define PARSE_H

#include "./util.h"

#define DEBUG 1

// HTTP request header
struct parsed_request {
    char *method;
    char *protocol;
    char *host;
    char *port;
    char *path;
    char *version;
    char *buf;
    size_t buflen;
    struct parsed_header *headers;
    size_t header_used;
    size_t headers_len;
};

/* Any header after the request line is a key-value pair with the
   format "key:value\r\n" and is maintained in the parsed_header linked
   list within parsed_request
*/
struct parsed_header {
    char *key;
    size_t key_len;
    char *value;
    size_t value_len;
};

struct parsed_request *ParsedRequest_create();
int ParsedRequest_parse(struct parsed_request *parse, const char *buf, int buflen);
void ParsedRequest_destroy(struct parsed_request *pr);

int ParsedRequest_unparse(struct parsed_request *pr, char *buf, size_t buflen);
int ParsedRequest_unparse_headers(struct parsed_request *pr, char *buf, size_t buflen);

size_t ParsedRequest_totalLen(struct parsed_request *pr);
size_t ParsedRequest_headersLen(struct parsed_request *pr);

int ParsedHeader_set(struct parsed_request *pr, const char *key, const char *value);
struct parsed_header *ParsedHeader_get(struct parsed_request *pr, const char *key);
int ParsedHeader_remove(struct parsed_request *pr, const char *key);

void debug(const char *format, ...);

#endif // PARSE_H
