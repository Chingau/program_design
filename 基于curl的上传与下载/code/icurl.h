#ifndef __ICURL_API_H__
#define __ICURL_API_H__      

enum {
    CURL_REQ_GET = 0,       //download
    CURL_REQ_PUT,           //upload
    CURL_REQ_NOT_SUPPORT,
};

enum {
    CURL_EX_OK = 0,         //download or upload success
    CURL_EX_FAIL,           //download or upload fail
    CURL_EX_PCT,            //download or upload percentage
    CURL_EX_MAX,
};

enum {
    CURL_UPLOAD_SINGLE = 0,
    CURL_UPLOAD_CHUNK,
};

enum {
    CURL_NET_PRIVATE = 0,
    CURL_NET_PUBLIC,
    CURL_NET_NOT_SUPPORT,
};

typedef struct {
    int dir;
    const char *url;
    const char *local_path;
    void (*callback)(int, int);
    int retry_times;
    int nm_device;
    int timeout;
    int type;                   //单文件上传；大文件分片上传（暂时未使用）
    char md5[64];               //待上传的大文件的md5值 (暂时未使用)
    char log_type[1];           //针对日志上传时，区别日志是原始日志还是ECU日志 "1"tbox日志 "1"ecu日志
} __attribute__((packed)) icurl_req_t;

int icurl_request(icurl_req_t *icurl_req);

#endif