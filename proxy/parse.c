#include "parse.h"

#define DEFAULT_NHDRS 8
#define MAX_REQ_LEN 65535
#define MIN_REQ_LEN 4

static const char *root_abs_path = "/";

/* private function declartions */
int ParsedRequest_printRequestLine(struct parsed_request *pr, char *buf,
                                   size_t buflen, size_t *tmp);
size_t ParsedRequest_requestLineLen(struct parsed_request *pr);

/*
 * debug() prints out debugging info if DEBUG is set to 1
 *
 * parameter format: same as printf
 *
 */
void debug(const char *format, ...) {
    va_list args;
    if (DEBUG) {
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}

/*
 *  parsed_header Public Methods
 */

/* Set a header with key and value */
int ParsedHeader_set(struct parsed_request *pr, const char *key,
                     const char *value) {
    struct parsed_header *ph;
    ParsedHeader_remove(pr, key);

    if (pr->headers_len <= pr->header_used + 1) {
        pr->headers_len = pr->headers_len * 2;
        pr->headers = (struct parsed_header *)realloc(
            pr->headers, pr->headers_len * sizeof(struct parsed_header));
        if (!pr->headers)
            return -1;
    }

    ph = pr->headers + pr->header_used;
    pr->header_used += 1;

    ph->key = (char *)malloc(strlen(key) + 1);
    memcpy(ph->key, key, strlen(key));
    ph->key[strlen(key)] = '\0';

    ph->value = (char *)malloc(strlen(value) + 1);
    memcpy(ph->value, value, strlen(value));
    ph->value[strlen(value)] = '\0';

    ph->key_len = strlen(key) + 1;
    ph->value_len = strlen(value) + 1;
    return 0;
}

/* get the parsed_header with the specified key or NULL */
struct parsed_header *ParsedHeader_get(struct parsed_request *pr, const char *key) {
    size_t i = 0;
    struct parsed_header *tmp;
    while (pr->header_used > i) {
        tmp = pr->headers + i;
        if (tmp->key && key && strcmp(tmp->key, key) == 0) {
            return tmp;
        }
        i++;
    }
    return NULL;
}

/* remove the specified key from parsed_header */
int ParsedHeader_remove(struct parsed_request *pr, const char *key) {
    struct parsed_header *tmp;
    tmp = ParsedHeader_get(pr, key);
    if (tmp == NULL)
        return -1;

    free(tmp->key);
    free(tmp->value);
    tmp->key = NULL;
    return 0;
}

/* modify the header with given key, giving it a new value
 * return 1 on success and 0 if no such header found
 *
int ParsedHeader_modify(struct parsed_request *pr, const char * key,
            const char *newValue)
{
     struct parsed_header *tmp;
     tmp = ParsedHeader_get(pr, key);
     if(tmp != NULL)
     {
      if(tmp->value_len < strlen(newValue)+1)
      {
           tmp->value_len = strlen(newValue)+1;
           tmp->value = (char *) realloc(tmp->value,
                         tmp->value_len * sizeof(char));
      }
      strcpy(tmp->value, newValue);
      return 1;
     }
     return 0;
}
*/

/*
  parsed_header Private Methods
*/

void ParsedHeader_create(struct parsed_request *pr) {
    pr->headers = (struct parsed_header *)malloc(sizeof(struct parsed_header) *
                                                DEFAULT_NHDRS);
    pr->headers_len = DEFAULT_NHDRS;
    pr->header_used = 0;
}

size_t ParsedHeader_lineLen(struct parsed_header *ph) {
    if (ph->key != NULL) {
        return strlen(ph->key) + strlen(ph->value) + 4;
    }
    return 0;
}

size_t ParsedHeader_headersLen(struct parsed_request *pr) {
    if (!pr || !pr->buf)
        return 0;

    size_t i = 0;
    int len = 0;
    while (pr->header_used > i) {
        len += ParsedHeader_lineLen(pr->headers + i);
        i++;
    }
    len += 2;
    return len;
}

int ParsedHeader_printHeaders(struct parsed_request *pr, char *buf, size_t len) {
    char *current = buf;
    struct parsed_header *ph;
    size_t i = 0;

    if (len < ParsedHeader_headersLen(pr)) {
        debug("buffer for printing headers too small\n");
        return -1;
    }

    while (pr->header_used > i) {
        ph = pr->headers + i;
        if (ph->key) {
            memcpy(current, ph->key, strlen(ph->key));
            memcpy(current + strlen(ph->key), ": ", 2);
            memcpy(current + strlen(ph->key) + 2, ph->value, strlen(ph->value));
            memcpy(current + strlen(ph->key) + 2 + strlen(ph->value), "\r\n",
                   2);
            current += strlen(ph->key) + strlen(ph->value) + 4;
        }
        i++;
    }
    memcpy(current, "\r\n", 2);
    return 0;
}

void ParsedHeader_destroyOne(struct parsed_header *ph) {
    if (ph->key != NULL) {
        free(ph->key);
        ph->key = NULL;
        free(ph->value);
        ph->value = NULL;
        ph->key_len = 0;
        ph->value_len = 0;
    }
}

void ParsedHeader_destroy(struct parsed_request *pr) {
    size_t i = 0;
    while (pr->header_used > i) {
        ParsedHeader_destroyOne(pr->headers + i);
        i++;
    }
    pr->header_used = 0;

    free(pr->headers);
    pr->headers_len = 0;
}

int ParsedHeader_parse(struct parsed_request *pr, char *line) {
    char *key;
    char *value;
    char *index1;
    char *index2;

    index1 = index(line, ':');
    if (index1 == NULL) {
        debug("No colon found\n");
        return -1;
    }
    key = (char *)malloc((index1 - line + 1) * sizeof(char));
    memcpy(key, line, index1 - line);
    key[index1 - line] = '\0';

    index1 += 2;
    index2 = strstr(index1, "\r\n");
    value = (char *)malloc((index2 - index1 + 1) * sizeof(char));
    memcpy(value, index1, (index2 - index1));
    value[index2 - index1] = '\0';

    ParsedHeader_set(pr, key, value);
    free(key);
    free(value);
    return 0;
}

/*
  parsed_request Public Methods
*/

void ParsedRequest_destroy(struct parsed_request *pr) {
    if (pr->buf != NULL) {
        free(pr->buf);
    }
    if (pr->path != NULL) {
        free(pr->path);
    }
    /* For absolute-form requests host/protocol/port point inside buf and
       are freed with it. For origin-form requests they are separately
       malloc'd, so free them only when they live outside buf. */
    if (pr->buf != NULL) {
        char *start = pr->buf;
        char *end = pr->buf + pr->buflen;
        if (pr->protocol != NULL && (pr->protocol < start || pr->protocol >= end))
            free(pr->protocol);
        if (pr->host != NULL && (pr->host < start || pr->host >= end))
            free(pr->host);
        if (pr->port != NULL && (pr->port < start || pr->port >= end))
            free(pr->port);
    }
    if (pr->headers_len > 0) {
        ParsedHeader_destroy(pr);
    }
    free(pr);
}

struct parsed_request *ParsedRequest_create() {
    struct parsed_request *pr;
    pr = (struct parsed_request *)malloc(sizeof(struct parsed_request));
    if (pr != NULL) {
        ParsedHeader_create(pr);
        pr->buf = NULL;
        pr->method = NULL;
        pr->protocol = NULL;
        pr->host = NULL;
        pr->port = NULL;
        pr->path = NULL;
        pr->version = NULL;
        pr->buf = NULL;
        pr->buflen = 0;
    }
    return pr;
}

/*
   Recreate the entire buffer from a parsed request object.
   buf must be allocated
*/
int ParsedRequest_unparse(struct parsed_request *pr, char *buf, size_t buflen) {
    if (!pr || !pr->buf)
        return -1;

    size_t tmp;
    if (ParsedRequest_printRequestLine(pr, buf, buflen, &tmp) < 0)
        return -1;
    if (ParsedHeader_printHeaders(pr, buf + tmp, buflen - tmp) < 0)
        return -1;
    return 0;
}

/*
   Recreate the headers from a parsed request object.
   buf must be allocated
*/
int ParsedRequest_unparse_headers(struct parsed_request *pr, char *buf,
                                  size_t buflen) {
    if (!pr || !pr->buf)
        return -1;

    if (ParsedHeader_printHeaders(pr, buf, buflen) < 0)
        return -1;
    return 0;
}

/* Size of the headers if unparsed into a string */
size_t ParsedRequest_totalLen(struct parsed_request *pr) {
    if (!pr || !pr->buf)
        return 0;
    return ParsedRequest_requestLineLen(pr) + ParsedHeader_headersLen(pr);
}

/*
   Parse request buffer

   Parameters:
   parse: ptr to a newly created parsed_request object
   buf: ptr to the buffer containing the request (need not be NUL terminated)
   and the trailing \r\n\r\n
   buflen: length of the buffer including the trailing \r\n\r\n

   Return values:
   -1: failure
   0: success
*/
int ParsedRequest_parse(struct parsed_request *parse, const char *buf,
                        int buflen) {
    char *full_addr;
    char *saveptr;
    char *index;
    char *currentHeader;

    if (parse->buf != NULL) {
        debug("parse object already assigned to a request\n");
        return -1;
    }

    if (buflen < MIN_REQ_LEN || buflen > MAX_REQ_LEN) {
        debug("invalid buflen %d", buflen);
        return -1;
    }

    /* Create NUL terminated tmp buffer */
    char *tmp_buf = (char *)malloc(buflen + 1); /* including NUL */
    memcpy(tmp_buf, buf, buflen);
    tmp_buf[buflen] = '\0';

    index = strstr(tmp_buf, "\r\n\r\n");
    if (index == NULL) {
        debug("invalid request line, no end of header\n");
        free(tmp_buf);
        return -1;
    }

    /* Copy request line into parse->buf */
    index = strstr(tmp_buf, "\r\n");
    if (parse->buf == NULL) {
        parse->buf = (char *)malloc((index - tmp_buf) + 1);
        parse->buflen = (index - tmp_buf) + 1;
    }
    memcpy(parse->buf, tmp_buf, index - tmp_buf);
    parse->buf[index - tmp_buf] = '\0';

    /* Parse request line */
    parse->method = strtok_r(parse->buf, " ", &saveptr);
    if (parse->method == NULL) {
        debug("invalid request line, no whitespace\n");
        free(tmp_buf);
        free(parse->buf);
        parse->buf = NULL;
        return -1;
    }
    if (strcmp(parse->method, "GET")) {
        debug("invalid request line, method not 'GET': %s\n", parse->method);
        free(tmp_buf);
        free(parse->buf);
        parse->buf = NULL;
        return -1;
    }

    full_addr = strtok_r(NULL, " ", &saveptr);

    if (full_addr == NULL) {
        debug("invalid request line, no full address\n");
        free(tmp_buf);
        free(parse->buf);
        parse->buf = NULL;
        return -1;
    }

    parse->version = full_addr + strlen(full_addr) + 1;

    if (parse->version == NULL) {
        debug("invalid request line, missing version\n");
        free(tmp_buf);
        free(parse->buf);
        parse->buf = NULL;
        return -1;
    }
    if (strncmp(parse->version, "HTTP/", 5)) {
        debug("invalid request line, unsupported version %s\n", parse->version);
        free(tmp_buf);
        free(parse->buf);
        parse->buf = NULL;
        return -1;
    }

    if (strstr(full_addr, "://") == NULL) {
        /* origin-form request line: "GET /path HTTP/1.1"
           the host will be taken from the "Host" header below */
        parse->protocol = (char *)malloc(5);
        strcpy(parse->protocol, "http");
        parse->path = (char *)malloc(strlen(full_addr) + 1);
        strcpy(parse->path, full_addr);
    } else {
        parse->protocol = strtok_r(full_addr, "://", &saveptr);
        if (parse->protocol == NULL) {
            debug("invalid request line, missing host\n");
            free(tmp_buf);
            free(parse->buf);
            parse->buf = NULL;
            return -1;
        }

        const char *rem = full_addr + strlen(parse->protocol) + strlen("://");
        size_t abs_uri_len = strlen(rem);

        parse->host = strtok_r(NULL, "/", &saveptr);
        if (parse->host == NULL) {
            debug("invalid request line, missing host\n");
            free(tmp_buf);
            free(parse->buf);
            parse->buf = NULL;
            return -1;
        }

        if (strlen(parse->host) == abs_uri_len) {
            debug("invalid request line, missing absolute path\n");
            free(tmp_buf);
            free(parse->buf);
            parse->buf = NULL;
            return -1;
        }

        parse->path = strtok_r(NULL, " ", &saveptr);
        if (parse->path == NULL) { // replace empty abs_path with "/"
            int rlen = strlen(root_abs_path);
            parse->path = (char *)malloc(rlen + 1);
            strncpy(parse->path, root_abs_path, rlen + 1);
        } else if (strncmp(parse->path, root_abs_path, strlen(root_abs_path)) ==
                   0) {
            debug("invalid request line, path cannot begin "
                  "with two slash characters\n");
            free(tmp_buf);
            free(parse->buf);
            parse->buf = NULL;
            parse->path = NULL;
            return -1;
        } else {
            // copy parse->path, prefix with a slash
            char *tmp_path = parse->path;
            int rlen = strlen(root_abs_path);
            int plen = strlen(parse->path);
            parse->path = (char *)malloc(rlen + plen + 1);
            strncpy(parse->path, root_abs_path, rlen);
            strncpy(parse->path + rlen, tmp_path, plen + 1);
        }

        parse->host = strtok_r(parse->host, ":", &saveptr);
        parse->port = strtok_r(NULL, "/", &saveptr);

        if (parse->host == NULL) {
            debug("invalid request line, missing host\n");
            free(tmp_buf);
            free(parse->buf);
            free(parse->path);
            parse->buf = NULL;
            parse->path = NULL;
            return -1;
        }

        if (parse->port != NULL) {
            int port = strtol(parse->port, (char **)NULL, 10);
            if (port == 0 && errno == EINVAL) {
                debug("invalid request line, bad port: %s\n", parse->port);
                free(tmp_buf);
                free(parse->buf);
                free(parse->path);
                parse->buf = NULL;
                parse->path = NULL;
                return -1;
            }
        }
    }

    /* Parse headers */
    int ret = 0;
    currentHeader = strstr(tmp_buf, "\r\n") + 2;
    while (currentHeader[0] != '\0' &&
           !(currentHeader[0] == '\r' && currentHeader[1] == '\n')) {

        // debug("line %s %s", parse->version, currentHeader);

        if (ParsedHeader_parse(parse, currentHeader)) {
            ret = -1;
            break;
        }

        currentHeader = strstr(currentHeader, "\r\n");
        if (currentHeader == NULL || strlen(currentHeader) < 2)
            break;

        currentHeader += 2;
    }

    /* For origin-form requests (no URL in the request line) the host
       must come from the "Host" header */
    if (ret == 0 && parse->host == NULL) {
        struct parsed_header *host_hdr = ParsedHeader_get(parse, "Host");
        if (host_hdr == NULL) {
            debug("invalid request line, missing host\n");
            free(parse->buf);
            free(parse->path);
            parse->buf = NULL;
            parse->path = NULL;
            free(tmp_buf);
            return -1;
        }
        parse->host = (char *)malloc(strlen(host_hdr->value) + 1);
        strcpy(parse->host, host_hdr->value);

        char *colon = strchr(parse->host, ':');
        if (colon != NULL) {
            *colon = '\0';
            parse->port = (char *)malloc(strlen(colon + 1) + 1);
            strcpy(parse->port, colon + 1);
        }

        /* "Web proxy" URL-prefix usage: the browser is pointed at
           http://localhost:8080/<host>[/path], so the Host header is the
           proxy's own address and the real target sits in the request path.
           Example:  GET /betulbiyoloji.com/foo   +   Host: localhost:8080  */
        if (!strcmp(parse->host, "localhost") ||
            !strcmp(parse->host, "127.0.0.1") ||
            !strcmp(parse->host, "0.0.0.0")) {
            char *target = (parse->path != NULL && parse->path[0] == '/')
                               ? parse->path + 1
                               : NULL;

            if (target != NULL && target[0] != '\0') {
                char *host_seg = target;
                char *path_seg = strchr(target, '/');
                if (path_seg != NULL) {
                    *path_seg = '\0';
                    path_seg++;
                }

                /* only treat it as the target host if it looks like a domain */
                if (strchr(host_seg, '.') != NULL) {
                    /* the port (if any) belongs to the proxy's own address */
                    free(parse->port);
                    parse->port = NULL;

                    char *host_colon = strchr(host_seg, ':');
                    if (host_colon != NULL) {
                        *host_colon = '\0';
                        parse->port = (char *)malloc(strlen(host_colon + 1) + 1);
                        strcpy(parse->port, host_colon + 1);
                    }

                    free(parse->host);
                    parse->host = (char *)malloc(strlen(host_seg) + 1);
                    strcpy(parse->host, host_seg);

                    free(parse->path);
                    if (path_seg != NULL && path_seg[0] != '\0') {
                        parse->path = (char *)malloc(strlen(path_seg) + 2);
                        parse->path[0] = '/';
                        strcpy(parse->path + 1, path_seg);
                    } else {
                        parse->path = (char *)malloc(2);
                        strcpy(parse->path, "/");
                    }
                }
            }
        }
    }

    free(tmp_buf);
    return ret;
}

/*
   parsed_request Private Methods
*/

size_t ParsedRequest_requestLineLen(struct parsed_request *pr) {
    if (!pr || !pr->buf)
        return 0;

    size_t len = strlen(pr->method) + 1 + strlen(pr->protocol) + 3 +
                 strlen(pr->host) + 1 + strlen(pr->version) + 2;
    if (pr->port != NULL) {
        len += strlen(pr->port) + 1;
    }
    /* path is at least a slash */
    len += strlen(pr->path);
    return len;
}

int ParsedRequest_printRequestLine(struct parsed_request *pr, char *buf,
                                   size_t buflen, size_t *tmp) {
    char *current = buf;

    if (buflen < ParsedRequest_requestLineLen(pr)) {
        debug("not enough memory for first line\n");
        return -1;
    }
    memcpy(current, pr->method, strlen(pr->method));
    current += strlen(pr->method);
    current[0] = ' ';
    current += 1;

    memcpy(current, pr->protocol, strlen(pr->protocol));
    current += strlen(pr->protocol);
    memcpy(current, "://", 3);
    current += 3;
    memcpy(current, pr->host, strlen(pr->host));
    current += strlen(pr->host);
    if (pr->port != NULL) {
        current[0] = ':';
        current += 1;
        memcpy(current, pr->port, strlen(pr->port));
        current += strlen(pr->port);
    }
    /* path is at least a slash */
    memcpy(current, pr->path, strlen(pr->path));
    current += strlen(pr->path);

    current[0] = ' ';
    current += 1;

    memcpy(current, pr->version, strlen(pr->version));
    current += strlen(pr->version);
    memcpy(current, "\r\n", 2);
    current += 2;
    *tmp = current - buf;
    return 0;
}
