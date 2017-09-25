/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2014 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_myext.h"

/* If you declare any globals in php_myext.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(myext)
*/

/* True global resources - no need for thread safety here */
static int le_myext;

zend_class_entry *ext_ce;

const zend_function_entry myext_functions[] = {
	PHP_ME(Myext, __construct, NULL, ZEND_ACC_CTOR | ZEND_ACC_PUBLIC)
	PHP_ME(Myext, __destruct, NULL, ZEND_ACC_DTOR | ZEND_ACC_PUBLIC)
	PHP_ME(Myext, close, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Myext, getFile, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Myext, getHttp, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

zend_module_entry myext_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"myext",
	NULL,
	PHP_MINIT(myext),
	PHP_MSHUTDOWN(myext),
	PHP_RINIT(myext),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(myext),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(myext),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_MYEXT_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MYEXT
ZEND_GET_MODULE(myext)
#endif

PHP_MINIT_FUNCTION(myext)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	zend_class_entry ext_class_entry;
    
	/* ext class */
	INIT_CLASS_ENTRY(ext_class_entry, "Myext", myext_functions);
    ext_ce = zend_register_internal_class(&ext_class_entry TSRMLS_CC);

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(myext)
{
	return SUCCESS;
}

PHP_RINIT_FUNCTION(myext)
{
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(myext)
{
	return SUCCESS;
}

PHP_MINFO_FUNCTION(myext)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "myext support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}

PHP_METHOD(Myext, __construct)
{
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
        RETURN_FALSE;
    }
}

PHP_METHOD(Myext,__destruct) {
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
		RETURN_FALSE;
	}
}

PHP_METHOD(Myext, close)
{
    RETURN_FALSE;
}

char* mystrcat(char *s1, char *s2)  
{  
    char *result = malloc(strlen(s1)+strlen(s2)+1);//+1 for the zero-terminator  
    //in real code you would check for errors in malloc here  
    if (result == NULL) exit (1);  
  
    strcpy(result, s1);  
    strcat(result, s2);  
  
    return result;  
}  


PHP_METHOD(Myext, getFile) {
    zval *object;
    char *path = NULL, *mode = NULL;
    int path_len, mode_len;
	char *content = "";
	char inbuf[1024];

    // Make sure the arguments are correct
    if(zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss",
                                    &object, ext_ce, &path, &path_len, &mode, &mode_len) == FAILURE)
    {
        RETURN_FALSE;
    }
	
	php_stream *stream;
    stream = php_stream_open_wrapper(path, mode, ENFORCE_SAFE_MODE | REPORT_ERRORS, NULL);  
    if (!stream) {  
        RETURN_FALSE;  
    }

	while(php_stream_gets(stream, inbuf, sizeof(inbuf)) != NULL) {
		//拼接字符串
        content = mystrcat(content, inbuf);
		//printf("%s", content);
    }  
    //php_stream_to_zval(stream, return_value);   //返回流对象
	RETURN_STRING(content, 1); //返回字符串 PHP返回值怎么定义的呢？
}

PHP_METHOD(Myext, getHttp){
	php_stream *stream;
	char inbuf[1024];
	zval *object;
    char *host, *path, *errstr = NULL;
    int host_len, path_len, implicit_tcp = 1, errcode = 0;
    long port;
    int options = ENFORCE_SAFE_MODE;
	char *content = "";
    int flags = STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT;
    if(zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Oss",
                                    &object, ext_ce, &host, &host_len, &path, &path_len) == FAILURE)
    {
        RETURN_FALSE;
    }
    
    stream = php_stream_xport_create(host, host_len,
            options, flags,
            NULL, NULL, NULL, &errstr, &errcode);
    if (errstr) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "[%d] %s",
                errcode, errstr);
        efree(errstr);
    }
    if (!stream) {
        RETURN_FALSE;
    }
	
	//GET请求方式
	char getContent[4096];
    sprintf(getContent, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; zh-CN; rv:1.9.2.3) Gecko/20100401 Firefox/3.6.3\r\nConnection:close\r\n\r\n" ,path,host);
	php_stream_write(stream, getContent, sizeof(getContent));
    //php_stream_to_zval(stream, return_value);
	while(php_stream_gets(stream, inbuf, sizeof(inbuf)) != NULL) {
		//拼接字符串
        content = mystrcat(content, inbuf);
    }  
    //php_stream_to_zval(stream, return_value);   //返回流对象
	RETURN_STRING(content, 1); //返回字符串 PHP返回值怎么定义的呢？
}
