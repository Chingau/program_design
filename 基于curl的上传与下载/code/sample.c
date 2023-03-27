#include "icurl.h"

static int file_trans_complete = -1;	//文件是否上传/下载成功标志
static void file_trans_cb(int evt, int errcode)
{
    switch (evt)
    {
        case CURL_EX_FAIL:
            INFOR( "trans fail! errcode:%d", errcode);
			file_trans_complete = errcode;
            break;

        case CURL_EX_OK:
            INFOR( "trans OK!");
			file_trans_complete = 0;
            break;

        case CURL_EX_PCT:
            INFOR( "trans percentage:%d%%", errcode);
            break;

        default:
            break;
    }

    return;	
}

/*****************************************************************************
 * 函数名称: upload_file
 * 函数描述: 上传文件
 * 输入参数: up_url-上传地址; file_path-待上传文件的路径; arg-用户自定义参数
 * 输出参数: 无
 * 返回参数: 0-成功 other-失败
 *****************************************************************************/
static int upload_file(const char *up_url, const char *file_path, void *arg)
{
	icurl_req_t http_req;
	int ret;
	char log_type = *((char *)(arg));

	INFOR("upload_file:%s", file_path);
	INFOR("upload_url:%s", up_url);
	INFOR("log_type:%c", log_type);
	if (access(file_path, F_OK) != 0) {
		INFOR("file absent");
		return -1;
	}

	file_trans_complete = -1;
	memset(&http_req, 0, sizeof(http_req));
	http_req.url = up_url;
	http_req.local_path = file_path;
	http_req.nm_device = CURL_NET_PUBLIC;
	http_req.dir = CURL_REQ_PUT;
	http_req.callback = file_trans_cb;
	http_req.timeout = 60;
	http_req.retry_times = 1;
	http_req.log_type[0] = log_type;
	if ((ret = icurl_request(&http_req)) < 0) {
		ERROR("Failed to create icurl upload eculog thread. ret = %d", ret);
		return -1;
	}

	int retry_times = 0;
	while (file_trans_complete == -1) {
		sleep(1);
		if (++retry_times >= 5)
			break;
	}

	if (file_trans_complete == -1 || file_trans_complete > 0 ) {
		ERROR("http upload failed");
		return -1;
	}

	return 0;
}

/*****************************************************************************
 * 函数名称: download_file
 * 函数描述: 下载文件
 * 输入参数: down_url-上传地址; file_path-待上传文件的路径
 * 输出参数: 无
 * 返回参数: 0-成功 other-失败
 *****************************************************************************/
static int download_file(const char *down_url, const char *file_path)
{
	icurl_req_t http_req;
	int ret;

	INFOR("down_file:%s", file_path);
	INFOR("down_url:%s", down_url);
	file_trans_complete = -1;
	memset(&http_req, 0, sizeof(http_req));
	http_req.url = down_url;
	http_req.local_path = file_path;
	http_req.nm_device = CURL_NET_PUBLIC;
	http_req.dir = CURL_REQ_GET;
	http_req.callback = file_trans_cb;
	http_req.timeout = 60;
	http_req.retry_times = 1;
	if ((ret = icurl_request(&http_req)) < 0) {
		ERROR("Failed to create icurl upload eculog thread. ret = %d", ret);
		return -1;
	}

	int retry_times = 0;
	while (file_trans_complete == -1) {
		sleep(1);
		if (++retry_times >= 5)
			break;
	}

	if (file_trans_complete == -1 || file_trans_complete > 0 ) {
		ERROR("http download failed");
		return -1;
	}

	return 0;
}

int main(int argc, const char **argv)
{
    download_file(const char *down_url, const char *file_path);
    upload_file(const char *up_url, const char *file_path, void *arg);
    return 0;
}
