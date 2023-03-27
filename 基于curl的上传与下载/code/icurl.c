#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "curl/curl.h"

#include "init.h"
#include "icurl_api.h"
#include "shell.h"
#include "log.h"

#define CURL_URL            "http://test.device.bjev.com.cn:60526"
#define CURL_CHECK          "api/diagnosis/tboxLog/check"
#define CURL_UPLD           "api/diagnosis/tboxLog/uploadTBoxFile"
#define CURL_DNLD           "file/tbox-file/"   

#if LIBCURL_VERSION_NUM >= 0x073d00
    /* In libcurl 7.61.0, support was added for extracting the time in plain
    microseconds. Older libcurl versions are stuck in using 'double' for this
    information so we complicate this example a bit by supporting either
    approach. */
    #define ICURL_TIME_IN_US 1
    #define ICURL_TIMETYPE curl_off_t
    #define ICURL_TIMEOPT CURLINFO_TOTAL_TIME_T
    #define CURL_MIN_PCT_INTERVAL     1000000 //1s
#else
    #define ICURL_TIMETYPE double
    #define ICURL_TIMEOPT CURLINFO_TOTAL_TIME
    #define CURL_MIN_PCT_INTERVAL     1 //1s
#endif
#define CURL_MAX_LIMIT_BYTES      31457280 //30M

#define CURL_MAX                5
#define CURL_DEFAULT_RETRY      5
#define CURL_DEFAULT_TIMEOUT    60
static pthread_mutex_t icurl_mtx;
#define CURL_LOCK()          pthread_mutex_lock(&icurl_mtx)
#define CURL_UNLOCK()        pthread_mutex_unlock(&icurl_mtx)

typedef struct {
    icurl_req_t curl_req;
    int used;   //0:unuse 1:used
    char url[512];
    char local_path[128];
    FILE *fp;
    pthread_t tid;
    CURL *curl;
    int index;
    ICURL_TIMETYPE lastruntime;
    int lastpct;
} __attribute__((packed)) icurl_info_t;

static icurl_info_t icurl_info[CURL_MAX];

/************************************************************************* 
 * function     : icurl_info_reset
 * description  : 初始化 info 结构体
 * param        : (icurl_info_t) *info icurl的信息结构体
 * return       : 无
*************************************************************************/
static int icurl_info_reset(icurl_info_t *info)
{
    memset(&info->curl_req, 0, sizeof(icurl_req_t));
    info->used = 0;
    memset(info->url, 0, sizeof(info->url));
    memset(info->local_path, 0, sizeof(info->local_path));
    info->fp = NULL;
    info->tid = -1;
    info->curl = NULL;
    info->index = (int)(info - icurl_info);
    info->lastruntime = 0;
    info->lastpct = 0;
    return 0;
}

static int icurl_progress_function(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    CURLcode res;

    if (!clientp) {
        return 0;
    }

    icurl_info_t *info = (icurl_info_t *)clientp;
    ICURL_TIMETYPE curtime = 0;
    int curpct = 0;

    if ((info->curl_req.dir == CURL_REQ_GET) && (0 != dltotal)) {
        curpct = ((float)dlnow / (float)dltotal) * 100;
    }

    if ((info->curl_req.dir == CURL_REQ_PUT) && (0 != ultotal)) {
        curpct = ((float)ulnow / (float)ultotal) * 100;
    }

    /* get curl time infomation */
    res = curl_easy_getinfo(info->curl, ICURL_TIMEOPT, &curtime);
    if (res != CURLE_OK) {
        ERROR("Fail to get curl infomation ICURL_TIMEOPT :%d", res);
    }

    if ((curtime - info->lastruntime) >= CURL_MIN_PCT_INTERVAL \
        || ((info->lastpct != curpct) && (100 == curpct))) {
        info->lastruntime = curtime;
        /* print load  percentage*/
        if (info->lastpct != curpct) {
            info->lastpct = curpct;
            info->curl_req.callback(CURL_EX_PCT, curpct);
        }
    }

    /* check file size limit */
    if (dlnow > CURL_MAX_LIMIT_BYTES) {
        return 1;
    }

    return 0;
}

static int icurl_download(icurl_info_t *info, char *resstr)
{
    CURL *curl;
    CURLcode res = 0xff;
    int ret = 0;
    //char nm_dev_name[256];//net devices name

    if (!resstr) {
        ERROR("debug buf is empty.");
        ret = 1;
        goto __icurl_down_exit3;
    }
    /* open download file */
    info->fp = fopen(info->local_path, "w+");
    if (!info->fp) {
        sprintf(resstr, "Fail to open \"%s\":%s\r\n", info->local_path, strerror(errno));
        ret = 1;
        goto __icurl_down_exit3;
    }

    if ((curl = curl_easy_init()) == NULL) {
        sprintf(resstr, "Fail to create curl!");
        ret = 1;
        goto __icurl_down_exit2;
    }
    info->curl = curl;

    /* set curl options */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_URL, info->url); //set url
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, info->curl_req.timeout ? info->curl_req.timeout : CURL_DEFAULT_TIMEOUT); //set timeout limit,60s
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)info->fp); //set download data write fp
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);//enable 4xx errors
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);//enable progress pecent display
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)info);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, icurl_progress_function);

    /* start download */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        sprintf(resstr, "Failed to download, error code:%d, cause:%s\r\n", res, curl_easy_strerror(res));
        ret = 1;
    }
    curl_easy_cleanup(curl);
    fflush(info->fp);

__icurl_down_exit2:
    fclose(info->fp);
    if (res != CURLE_OK) {
        remove(info->local_path);
    }

__icurl_down_exit3:
    return ret;
}

static size_t up_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    memcpy(userdata, ptr, size*nmemb);
    return size*nmemb;
}

/* Debug */
/*
static int curl_dbg_info(CURL *curl, curl_infotype itype, char * pData, size_t size, void *usrp)
{
    char buf[128*1024] = {0};
    
    FILE* fp = fopen("debug.txt", "a+");
	if(fp == NULL)
	{
		ERROR("open failed\n");
		return -1;
	}
	if(itype == CURLINFO_TEXT)
	{
		strcat(buf, "[TEXT]:\n");
	}
	else if(itype == CURLINFO_HEADER_IN)
	{
		strcat(buf, "[HEADER_IN]:\n");
	}
	else if(itype == CURLINFO_HEADER_OUT)
	{
		strcat(buf, "[HEADER_OUT]:\n");
	}
	else if(itype == CURLINFO_DATA_IN)
	{
		strcat(buf, "[DATA_IN]:\n");
	}
	else if(itype == CURLINFO_DATA_OUT)
	{
		strcat(buf, "[DATA_OUT]:\n");
	}

	strcat(buf, pData);
	strcat(buf, "\r\n");
	fwrite((const void*)buf, 1, strlen(buf), fp);
	fflush(fp);
	fclose(fp);

	return 0;
}
*/

/************************************************************************* 
 * function     : icurl_upload_single
 * description  : 单个文件上传
 * param        : (icurl_info_t) *info icurl的信息结构体
 * param        : (char) *resstr 执行文件上传过程中产生的日志信息
 * return       : 0-成功 other-失败
*************************************************************************/
static int icurl_upload_single(icurl_info_t *info, char *resstr)
{
    int ret = 0;
    long long filesize = 0;
    struct curl_slist *headerlist = NULL;
    curl_mime *form = NULL;
    curl_mimepart *file_field = NULL;
    curl_mimepart *type_field = NULL;
    CURL *curl;
    CURLcode res = 0xff;
    char resp[1024], *body = NULL;

    if (!resstr) {
        ERROR("debug buf is empty.");
        ret = 1;
        goto __icurl_single_exit3;
    }

    info->fp = fopen(info->local_path, "r");
    if (!info->fp) {
        sprintf(resstr, "Fail to open \"%s\":%s\r\n", info->local_path, strerror(errno));
        ret = 1;
        goto __icurl_single_exit3;
    }

    /* get file size */
    fseek(info->fp, 0, SEEK_END);
    filesize = ftell(info->fp);
    fseek(info->fp, 0, SEEK_SET);
    INFOR("File size: %lld\r\n", filesize);

    /* create curl handle */
    if ((curl = curl_easy_init()) == NULL) {
        sprintf(resstr, "Fail to create curl!");
        ret = 1;
        goto __icurl_single_exit2;
    }
    info->curl = curl;

    memset(resp, 0, sizeof(resp));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    //curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_dbg_info); //test
    curl_easy_setopt(curl, CURLOPT_URL, info->url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)resp);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, up_write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, info->curl_req.timeout ? info->curl_req.timeout : CURL_DEFAULT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)filesize);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);//enable 4xx errors
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);//enable progress pecent display
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, icurl_progress_function);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)info);
    //curl_easy_setopt(curl, CURLOPT_HEADER, 1); //test

    /* 设置headers */			
    headerlist = curl_slist_append(headerlist, "Cache-Control: no-cache");
    headerlist = curl_slist_append(headerlist, "Connection: Keep-Alive");
    headerlist = curl_slist_append(headerlist, "Content-Type: multipart/form-data");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

    /* 创建表单 */
    form = curl_mime_init(curl);
    /* 添加文件表单数据 */
    file_field = curl_mime_addpart(form);
    curl_mime_name(file_field, "file");
    curl_mime_type(file_field,"text/plain");
    curl_mime_filedata(file_field, info->local_path);

    type_field = curl_mime_addpart(form);
    curl_mime_name(type_field, "type");
    curl_mime_data(type_field, (const char *)info->curl_req.log_type, 1);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    /* start post */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        sprintf(resstr, "Failed to upload, error code:%d, cause:%s\r\n", res, curl_easy_strerror(res));
        ret = 1;
    }

    curl_slist_free_all(headerlist);
    curl_mime_free(form);
    curl_easy_cleanup(curl);
    INFOR("resp body: %s", resp);
    if ((body = strstr(resp, "code")) != NULL) {    // code":0
        INFOR("body:%s", body);
        if (body[6] != '0') {
            ret = 2;
            sprintf(resstr, "resp code value isn't 0");
        }
    } else {
        ret = 3;
        sprintf(resstr, "resp body error");
    }

__icurl_single_exit2:
    fclose(info->fp);

__icurl_single_exit3:
    return ret;
}

/************************************************************************* 
 * function     : icurl_upload_chunk
 * description  : 大文件分片上传, 断点续传
 * param        : (icurl_info_t) *info icurl的信息结构体
 * param        : (char) *resstr 执行文件上传过程中产生的日志信息
 * return       : 0-成功 other-失败
 * 说明：此接口尚未调通
*************************************************************************/
static int icurl_upload_chunk(icurl_info_t *info, char *resstr)
{
    int ret = 0;
    long long filesize = 0;
    struct curl_slist *headerlist = NULL;
    curl_mime *form = NULL;
    curl_mimepart *file_field = NULL;
    CURL *curl;
    CURLcode res = 0xff;
    char resp[1024], url[512], param[128];
    char cIndex[5], cSize[5];
//    int iIndex, iSize;
    int len = 0, code = -1;

    if (!resstr) {
        ERROR("debug buf is empty.");
        ret = 1;
        goto __icurl_chunk_exit3;
    }

    info->fp = fopen(info->local_path, "r");
    if (!info->fp) {
        sprintf(resstr, "Fail to open \"%s\":%s\r\n", info->local_path, strerror(errno));
        ret = 1;
        goto __icurl_chunk_exit3;
    }

    /* get file size */
    fseek(info->fp, 0, SEEK_END);
    filesize = ftell(info->fp);
    fseek(info->fp, 0, SEEK_SET);
    INFOR("File size: %lld\r\n", filesize);

    /* create curl handle */
    if ((curl = curl_easy_init()) == NULL) {
        sprintf(resstr, "Fail to create curl!");
        ret = 1;
        goto __icurl_chunk_exit2;
    }
    info->curl = curl;

    memset(resp, 0, sizeof(resp));
    memset(url, 0, sizeof(url));
    memset(param, 0, sizeof(param));
    memcpy(url, CURL_URL"/"CURL_CHECK, strlen(CURL_URL"/"CURL_CHECK));
    sprintf(param, "?key=%s", info->curl_req.md5);
    strncat(url, param, strlen(param));
    INFOR("makeup url: %s", url);
    /* set curl options */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_URL, url); //set url
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)resp);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, up_write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, info->curl_req.timeout ? info->curl_req.timeout : CURL_DEFAULT_TIMEOUT); //set timeout limit,60s
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);//enable 4xx errors
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);//enable progress pecent display
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)info);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, icurl_progress_function);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        sprintf(resstr, "Failed to upload, error code:%d, cause:%s\r\n", res, curl_easy_strerror(res));
        ret = 1;
        goto __icurl_chunk_exit1;
    }
    INFOR("check resp body: %s", resp);
    len = sscanf(resp, "{\"code\":%d,\"message\":%s,\"data\":{\"id\":%s,\"path\":%s,\"name\":%s,\"suffix\":%s,\"size\":%s,\"createdAt\":%s,\"updatedAt\":%s,\"shardIndex\":%s,\"shardSize\":%s,\"shardTotal\":%s,\"fileKey\":%s}}",
            &code, param, param, param, param, param, param, param, param, cIndex, cSize, param, param);
    INFOR("CODE:%d", code);
    if (len == 13) {
        if (code == 0) {
            if (memcmp(cIndex, "null", 4) != 0) {
                // iIndex = atoi(cIndex);
                // iSize = atoi(cSize);
            } else {
                // iIndex = -1;
                // iSize = -1;
            }
        } else {
            ret = 2;
            sprintf(resstr, "tsp resp error.");
            goto __icurl_chunk_exit1;
        }
    } else {
        ret = 2;
        sprintf(resstr, "param error form tsp to resp. param len:%d", len);
        goto __icurl_chunk_exit1;
    }
    curl_easy_cleanup(curl);

    memset(resp, 0, sizeof(resp));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
    curl_easy_setopt(curl, CURLOPT_URL, info->url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)resp);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, up_write_callback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, info->curl_req.timeout ? info->curl_req.timeout : CURL_DEFAULT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)filesize);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);//enable 4xx errors
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);//enable progress pecent display
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, icurl_progress_function);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)info);

    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, 0L);

    /* 设置headers */			
    headerlist = curl_slist_append(headerlist, "Cache-Control: no-cache");
    headerlist = curl_slist_append(headerlist, "Connection: Keep-Alive");
    headerlist = curl_slist_append(headerlist, "Content-Type: multipart/form-data");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);

    /* 创建表单 */
    form = curl_mime_init(curl);
    /* 添加表单数据 */
    file_field = curl_mime_addpart(form);
    curl_mime_name(file_field, "file");
    curl_mime_filedata(file_field, info->local_path);
    curl_mime_name(file_field, "shardIndex");
    curl_mime_filedata(file_field, "1");
    curl_mime_name(file_field, "shardSize");
    curl_mime_filedata(file_field, "1");
    curl_mime_name(file_field, "shardTotal");
    curl_mime_filedata(file_field, "1");
    curl_mime_name(file_field, "size");
    curl_mime_filedata(file_field, (const char *)&filesize);
    curl_mime_name(file_field, "suffix");
    curl_mime_filedata(file_field, ".gz");
    curl_mime_name(file_field, "key");
    curl_mime_filedata(file_field, info->curl_req.md5);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

    /* start post */
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        sprintf(resstr, "Failed to upload, error code:%d, cause:%s\r\n", res, curl_easy_strerror(res));
        ret = -1;
    }
    INFOR("up resp body: %s", resp);

__icurl_chunk_exit1:
    if (headerlist)
        curl_slist_free_all(headerlist);
    if (form)
        curl_mime_free(form);
    curl_easy_cleanup(curl);

__icurl_chunk_exit2:
    fclose(info->fp);

__icurl_chunk_exit3:
    return ret;
}

/************************************************************************* 
 * function     : prepare_download
 * description  : 准备处理用户的下载任务
 * param        : (void) *arg icurl的信息结构体
 * return       : 无
*************************************************************************/
static void *prepare_download(void *arg)
{
    icurl_info_t *curl_info_temp = (icurl_info_t *)arg;
    int retry_cnt = 0;
    int errcode = 0;
    char debug_info[256];

    while (1) {
        memset(debug_info, 0, sizeof(debug_info));
        if ((errcode = icurl_download(curl_info_temp, debug_info))) {
            if (++retry_cnt >= (curl_info_temp->curl_req.retry_times ? curl_info_temp->curl_req.retry_times : CURL_DEFAULT_RETRY)) {
                ERROR("icurl index: %d, error info: %s", curl_info_temp->index, debug_info);
                curl_info_temp->curl_req.callback(CURL_EX_FAIL, errcode);
                break;
            } else {
                ERROR("icurl index: %d, error info: %s, retry times: %d", curl_info_temp->index, debug_info, retry_cnt);
            }
        } else {
            curl_info_temp->curl_req.callback(CURL_EX_OK, errcode);
            break;
        }
    }

    CURL_LOCK();
    icurl_info_reset(curl_info_temp);
    CURL_UNLOCK();

    return 0;
}

/************************************************************************* 
 * function     : prepare_upload
 * description  : 准备处理用户的上传任务
 * param        : (void) *arg icurl的信息结构体
 * return       : 无
*************************************************************************/
static void *prepare_upload(void *arg)
{
    icurl_info_t *curl_info_temp = (icurl_info_t *)arg;
    int retry_cnt = 0;
    char debug_info[256];
    int errcode = 0;
    int (*iupload_call)(icurl_info_t *, char *) = NULL;

    if (!curl_info_temp->url || !curl_info_temp->local_path) {
        ERROR("URL or Local path error!");
        goto __icurl_up_exit;
    }

    INFOR("upload url:%s", curl_info_temp->url);
    INFOR("upload file:%s", curl_info_temp->local_path);

    switch (curl_info_temp->curl_req.type) {
    case CURL_UPLOAD_SINGLE:
        iupload_call = icurl_upload_single;
    break;

    case CURL_UPLOAD_CHUNK:
        iupload_call = icurl_upload_chunk;
    break;

    default:
    break;
    }

    if (!iupload_call) {
        ERROR("upload type is error.");
        goto __icurl_up_exit;
    }

    while (1) {
        memset(debug_info, 0, sizeof(debug_info));
        if ((errcode = iupload_call(curl_info_temp, debug_info))) {
            if (++retry_cnt >= (curl_info_temp->curl_req.retry_times ? curl_info_temp->curl_req.retry_times : CURL_DEFAULT_RETRY)) {
                ERROR("icurl index: %d, error info: %s", curl_info_temp->index, debug_info);
                curl_info_temp->curl_req.callback(CURL_EX_FAIL, errcode);
                break;
            } else {
                ERROR("icurl index: %d, error info: %s, retry times: %d", curl_info_temp->index, debug_info, retry_cnt);
            }
        } else {
            curl_info_temp->curl_req.callback(CURL_EX_OK, errcode);
            break;
        }
    }

__icurl_up_exit:
    CURL_LOCK();
    icurl_info_reset(curl_info_temp);
    CURL_UNLOCK();
    return 0;
}

/************************************************************************* 
 * function     : icurl_request
 * description  : 检查用户的下载/上传请求参数是否合法；创建线程处理用户的请求
 * param        : (icurl_req_t) *icurl_req icurl的请求结构体
 * return       : 0-success other-failed
*************************************************************************/
int icurl_request(icurl_req_t *icurl_req)
{
    icurl_info_t *icurl_info_temp = NULL;
    pthread_attr_t ta;
    int i;

    if (icurl_req == NULL) {
        ERROR("icurl param error.");
        return -1;
    }

    if (icurl_req->dir >= CURL_REQ_NOT_SUPPORT) {
        ERROR("icurl dir not support.");
        return -2;
    }

    if (!icurl_req->url || !icurl_req->local_path) {
        ERROR("icurl url or path is empty.");
        return -3;
    }

    CURL_LOCK();
    for (i = 0; i < CURL_MAX; i++) {
        if (icurl_info[i].used == 0)
        {
            icurl_info_temp = icurl_info + i;
            icurl_info[i].used = 1;
            break;
        }
    }    
    CURL_UNLOCK();

    if (!icurl_info_temp) {
        ERROR("icurl is no space to use.");
        return -4;
    }

    strcpy(icurl_info_temp->url, icurl_req->url);
    strcpy(icurl_info_temp->local_path, icurl_req->local_path);
    strcpy(icurl_info_temp->curl_req.md5, icurl_req->md5);
    icurl_info_temp->curl_req.callback = icurl_req->callback;
    icurl_info_temp->curl_req.dir = icurl_req->dir;
    icurl_info_temp->curl_req.retry_times = icurl_req->retry_times;
    icurl_info_temp->curl_req.nm_device = icurl_req->nm_device;
    icurl_info_temp->curl_req.timeout = icurl_req->timeout;
    icurl_info_temp->curl_req.type = icurl_req->type;
    icurl_info_temp->curl_req.log_type[0] = icurl_req->log_type[0];

    pthread_attr_init(&ta);
    pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&icurl_info_temp->tid, &ta, 
        (icurl_info_temp->curl_req.dir ? prepare_upload : prepare_download), (void *)icurl_info_temp) < 0) {
        ERROR( "icurl creating thread fail");
        CURL_LOCK();
        icurl_info_reset(icurl_info_temp);
        CURL_UNLOCK();

        return -5;
    }

    return 0;
}

static int init_ICURL(void)
{
    int i = 0;
    
    INFOR("ICURL module init...");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    pthread_mutex_init(&icurl_mtx, NULL);

    CURL_LOCK();
    for (i = 0; i < CURL_MAX; i++) {
        icurl_info_reset(icurl_info + i);
    }
    CURL_UNLOCK();

    return 0;
}
DECLARE_INIT(ICURL, INIT_STEP_APPEXTR(3));
