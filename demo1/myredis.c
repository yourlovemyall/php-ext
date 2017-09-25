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

#include "common.h"
#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_myredis.h"
#include "library.h"
#include <zend_exceptions.h>
#include "php_network.h"
#include <sys/types.h>
#ifndef _MSC_VER
#include <netinet/tcp.h>  /* TCP_NODELAY */
#include <sys/socket.h>
#endif

/* If you declare any globals in php_myredis.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(myredis)
*/

int le_redis_sock;
/* True global resources - no need for thread safety here */
static int le_myredis;

zend_class_entry *redis_ce;
zend_class_entry *redis_exception_ce;
zend_class_entry *spl_ce_RuntimeException = NULL;

/* {{{ myredis_functions[]
 *
 * Every user visible function must have an entry in myredis_functions[].
 */
const zend_function_entry myredis_functions[] = {
	PHP_ME(Myredis, __construct, NULL, ZEND_ACC_CTOR | ZEND_ACC_PUBLIC)
	PHP_ME(Myredis, __destruct, NULL, ZEND_ACC_DTOR | ZEND_ACC_PUBLIC)
	PHP_ME(Myredis, connect, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Myredis, close, NULL, ZEND_ACC_PUBLIC)
	//PHP_ME(Myredis, get, NULL, ZEND_ACC_PUBLIC)
	PHP_ME(Myredis, set, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ myredis_module_entry
 */
zend_module_entry myredis_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"myredis",
	NULL, //如果输入myredis_functions 这个地方和INIT_CLASS_ENTRY冲突
	PHP_MINIT(myredis),
	PHP_MSHUTDOWN(myredis),
	PHP_RINIT(myredis),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(myredis),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(myredis),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_MYREDIS_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_MYREDIS
ZEND_GET_MODULE(myredis)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("myredis.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_myredis_globals, myredis_globals)
    STD_PHP_INI_ENTRY("myredis.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_myredis_globals, myredis_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_myredis_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_myredis_init_globals(zend_myredis_globals *myredis_globals)
{
	myredis_globals->global_value = 0;
	myredis_globals->global_string = NULL;
}
*/
/* }}} */

PHPAPI zend_class_entry *redis_get_exception_base(int root TSRMLS_DC)
{
#if HAVE_SPL
        if (!root) {
                if (!spl_ce_RuntimeException) {
                        zend_class_entry **pce;

                        if (zend_hash_find(CG(class_table), "runtimeexception",
                                                           sizeof("RuntimeException"), (void **) &pce) == SUCCESS) {
                                spl_ce_RuntimeException = *pce;
                                return *pce;
                        }
                } else {
                        return spl_ce_RuntimeException;
                }
        }
#endif
#if (PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 2)
        return zend_exception_get_default();
#else
        return zend_exception_get_default(TSRMLS_C);
#endif
}

/**
 * redis_destructor_redis_sock
 */
static void redis_destructor_redis_sock(zend_rsrc_list_entry * rsrc TSRMLS_DC)
{
    RedisSock *redis_sock = (RedisSock *) rsrc->ptr;
    redis_sock_disconnect(redis_sock TSRMLS_CC);
    redis_free_socket(redis_sock);
}

/**
 * redis_sock_get
 */
PHPAPI int redis_sock_get(zval *id, RedisSock **redis_sock TSRMLS_DC, int no_throw)
{

    zval **socket;
    int resource_type;

    if (Z_TYPE_P(id) != IS_OBJECT || zend_hash_find(Z_OBJPROP_P(id), "socket",
                                  sizeof("socket"), (void **) &socket) == FAILURE) {
    	// Throw an exception unless we've been requested not to
        if(!no_throw) {
        	zend_throw_exception(redis_exception_ce, "Redis server went away", 0 TSRMLS_CC);
        }
        return -1;
    }

    *redis_sock = (RedisSock *) zend_list_find(Z_LVAL_PP(socket), &resource_type);

    if (!*redis_sock || resource_type != le_redis_sock) {
		// Throw an exception unless we've been requested not to
    	if(!no_throw) {
    		zend_throw_exception(redis_exception_ce, "Redis server went away", 0 TSRMLS_CC);
    	}
		return -1;
    }
    if ((*redis_sock)->lazy_connect)
    {
        (*redis_sock)->lazy_connect = 0;
        if (redis_sock_server_open(*redis_sock, 1 TSRMLS_CC) < 0) {
            return -1;
        }
    }

    return Z_LVAL_PP(socket);
}

/**
 * Send a static DISCARD in case we're in MULTI mode.
 */
static int send_discard_static(RedisSock *redis_sock TSRMLS_DC) {

	int result = FAILURE;
	char *cmd, *response;
   	int response_len, cmd_len;

   	/* format our discard command */
   	cmd_len = redis_cmd_format_static(&cmd, "DISCARD", "");

   	/* send our DISCARD command */
   	if (redis_sock_write(redis_sock, cmd, cmd_len TSRMLS_CC) >= 0 &&
   	   (response = redis_sock_read(redis_sock, &response_len TSRMLS_CC)) != NULL) {

   		/* success if we get OK */
   		result = (response_len == 3 && strncmp(response,"+OK", 3) == 0) ? SUCCESS : FAILURE;

   		/* free our response */
   		efree(response);
   	}

   	/* free our command */
   	efree(cmd);

   	/* return success/failure */
   	return result;
}

/**
 * redis_sock_connect
 */
PHPAPI RedisSock* redis_sock_connect2(char *host, int host_len, unsigned short port,
                                    double timeout, int persistent, char *persistent_id,
                                    long retry_interval,
                                    zend_bool lazy_connect TSRMLS_DC)
{
	RedisSock *redis_sock;
    redis_sock         = ecalloc(1, sizeof(RedisSock));
    redis_sock->stream = NULL;
    redis_sock->status = REDIS_SOCK_STATUS_DISCONNECTED;
    redis_sock->watching = 0;
    redis_sock->dbNumber = 0;
    redis_sock->retry_interval = retry_interval * 1000;
    redis_sock->lazy_connect = lazy_connect;
    redis_sock->serializer = REDIS_SERIALIZER_NONE;
    redis_sock->mode = ATOMIC;
    redis_sock->head = NULL;
    redis_sock->err = NULL;
    redis_sock->err_len = 0;

    char *errstr = NULL;
    int err = 0;
	php_netstream_data_t *sock;
	int tcp_flag = 1;
	
	//类似c中的strdup，然后在PHP由新的函数进行统一处理
    host   = estrndup(host, host_len);
    host[host_len] = '\0';
	//组装tcp访问地址
    if(host[0] == '/' && port < 1) {
	    host_len = spprintf(&host, 0, "unix://%s", host);
    } else {
	    if(port == 0)
	    	port = 6379;
	    host_len = spprintf(&host, 0, "%s:%d", host, port);
    }
	
	//持久化参数
	if(persistent_id) {
		size_t persistent_id_len = strlen(persistent_id);
        redis_sock->persistent_id = ecalloc(persistent_id_len + 1, 1);
        memcpy(redis_sock->persistent_id, persistent_id, persistent_id_len);
    } else {
        redis_sock->persistent_id = NULL;
    }
    if (persistent) {
	  redis_sock->persistent = persistent;
      if (redis_sock->persistent_id) {
        spprintf(&persistent_id, 0, "phpredis:%s:%s", host, redis_sock->persistent_id);
      } else {
        spprintf(&persistent_id, 0, "phpredis:%s:%f", host, timeout);
      }
    }
	
	struct timeval tv, *tv_ptr = NULL;
    tv.tv_sec  = (time_t) timeout;
    tv.tv_usec = (int)((timeout - tv.tv_sec) * 1000000);
    if(tv.tv_sec != 0 || tv.tv_usec != 0) {
	    tv_ptr = &tv;
    }
    redis_sock->stream = php_stream_xport_create(host, host_len, ENFORCE_SAFE_MODE,
							 STREAM_XPORT_CLIENT
							 | STREAM_XPORT_CONNECT,
							 persistent_id, tv_ptr, NULL, &errstr, &err
							);

    if (persistent_id) {
      efree(persistent_id);
    }

    efree(host);

    if (!redis_sock->stream) {
        efree(errstr);
		zend_throw_exception(redis_exception_ce, "Redis_sock Failed", 0 TSRMLS_CC);
    }

    /* set TCP_NODELAY */
	sock = (php_netstream_data_t*)redis_sock->stream->abstract;
    setsockopt(sock->socket, IPPROTO_TCP, TCP_NODELAY, (char *) &tcp_flag, sizeof(int));

    php_stream_auto_cleanup(redis_sock->stream);
	
	//设置套接字流读超时时间
    if(tv.tv_sec != 0 || tv.tv_usec != 0) {
		struct timeval read_tv;
		read_tv.tv_sec  = (time_t)timeout;
		read_tv.tv_usec = (int)((timeout - read_tv.tv_sec) * 1000000);
        php_stream_set_option(redis_sock->stream, PHP_STREAM_OPTION_READ_TIMEOUT,
                              0, &read_tv);
    }
	//设置套接字流写空间
    php_stream_set_option(redis_sock->stream,
                          PHP_STREAM_OPTION_WRITE_BUFFER,
                          PHP_STREAM_BUFFER_NONE, NULL);
	//设置链接状态
    redis_sock->status = REDIS_SOCK_STATUS_CONNECTED;

    return redis_sock;
}

PHPAPI int redis_connect(INTERNAL_FUNCTION_PARAMETERS, int persistent) {
	zval *object;
	zval **socket;
	int host_len, id;
	char *host = NULL;
	long port = -1;
	long retry_interval = 0;

	char *persistent_id = NULL;
	int persistent_id_len = -1;
	
	double timeout = 0.0;
	RedisSock *redis_sock  = NULL;

#ifdef ZTS
	/* not sure how in threaded mode this works so disabled persistents at first */
    persistent = 0;
#endif

	if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Os|ldsl",
				&object, redis_ce, &host, &host_len, &port,
				&timeout, &persistent_id, &persistent_id_len,
				&retry_interval) == FAILURE) {
		return FAILURE;
	}

	if (timeout < 0L || timeout > INT_MAX) {
		zend_throw_exception(redis_exception_ce, "Invalid timeout", 0 TSRMLS_CC);
		return FAILURE;
	}

	if (retry_interval < 0L || retry_interval > INT_MAX) {
		zend_throw_exception(redis_exception_ce, "Invalid retry interval", 0 TSRMLS_CC);
		return FAILURE;
	}

	if(port == -1 && host_len && host[0] != '/') { /* not unix socket, set to default value */
		port = 6379;
	}

	/* if there is a redis sock already we have to remove it from the list */
	if (redis_sock_get(object, &redis_sock TSRMLS_CC, 1) > 0) {
		if (zend_hash_find(Z_OBJPROP_P(object), "socket",
					sizeof("socket"), (void **) &socket) == FAILURE)
		{
			/* maybe there is a socket but the id isn't known.. what to do? */
		} else {
			zend_list_delete(Z_LVAL_PP(socket)); /* the refcount should be decreased and the detructor called */
		}
	}
	/*
	redis_sock = redis_sock_create(host, host_len, port, timeout, persistent, persistent_id, retry_interval, 0);

	if (redis_sock_server_open(redis_sock, 1 TSRMLS_CC) < 0) {
		redis_free_socket(redis_sock);
		return FAILURE;
	}*/
	redis_sock = redis_sock_connect2(host, host_len, port, timeout, persistent, persistent_id, retry_interval, 0);
	

#if PHP_VERSION_ID >= 50400
	id = zend_list_insert(redis_sock, le_redis_sock TSRMLS_CC);
#else
	id = zend_list_insert(redis_sock, le_redis_sock);
#endif
	add_property_resource(object, "socket", id);

	return SUCCESS;
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(myredis)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	zend_class_entry redis_class_entry;
    zend_class_entry redis_exception_class_entry;

	//REGISTER_INI_ENTRIES();
    
	/* Redis class */
	INIT_CLASS_ENTRY(redis_class_entry, "Myredis", myredis_functions);
    redis_ce = zend_register_internal_class(&redis_class_entry TSRMLS_CC);

	/* RedisException class */
    INIT_CLASS_ENTRY(redis_exception_class_entry, "RedisException", NULL);
    redis_exception_ce = zend_register_internal_class_ex(
        &redis_exception_class_entry,
        redis_get_exception_base(0 TSRMLS_CC),
        NULL TSRMLS_CC
    );

    le_redis_sock = zend_register_list_destructors_ex(
        redis_destructor_redis_sock,
        NULL,
        redis_sock_name, module_number
    );

	add_constant_long(redis_ce, "REDIS_NOT_FOUND", REDIS_NOT_FOUND);
	add_constant_long(redis_ce, "REDIS_STRING", REDIS_STRING);
	add_constant_long(redis_ce, "REDIS_SET", REDIS_SET);
	add_constant_long(redis_ce, "REDIS_LIST", REDIS_LIST);
	add_constant_long(redis_ce, "REDIS_ZSET", REDIS_ZSET);
	add_constant_long(redis_ce, "REDIS_HASH", REDIS_HASH);

	add_constant_long(redis_ce, "ATOMIC", ATOMIC);
	add_constant_long(redis_ce, "MULTI", MULTI);
	add_constant_long(redis_ce, "PIPELINE", PIPELINE);

    // options 
    add_constant_long(redis_ce, "OPT_SERIALIZER", REDIS_OPT_SERIALIZER);
    add_constant_long(redis_ce, "OPT_PREFIX", REDIS_OPT_PREFIX);
    add_constant_long(redis_ce, "OPT_READ_TIMEOUT", REDIS_OPT_READ_TIMEOUT);

    // serializer 
    add_constant_long(redis_ce, "SERIALIZER_NONE", REDIS_SERIALIZER_NONE);
    add_constant_long(redis_ce, "SERIALIZER_PHP", REDIS_SERIALIZER_PHP);
#ifdef HAVE_REDIS_IGBINARY
    add_constant_long(redis_ce, "SERIALIZER_IGBINARY", REDIS_SERIALIZER_IGBINARY);
#endif

	zend_declare_class_constant_stringl(redis_ce, "AFTER", 5, "after", 5 TSRMLS_CC);
	zend_declare_class_constant_stringl(redis_ce, "BEFORE", 6, "before", 6 TSRMLS_CC);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(myredis)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(myredis)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(myredis)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(myredis)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "myredis support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/* {{{ proto Redis Redis::__construct()
    Public constructor */
PHP_METHOD(Myredis, __construct)
{
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
        RETURN_FALSE;
    }
}
/* }}} */

/* {{{ proto Redis Redis::__destruct()
    Public Destructor
 */
PHP_METHOD(Myredis,__destruct) {
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
		RETURN_FALSE;
	}

	// Grab our socket
	RedisSock *redis_sock;
	if (redis_sock_get(getThis(), &redis_sock TSRMLS_CC, 1) < 0) {
		RETURN_FALSE;
	}

	// If we think we're in MULTI mode, send a discard
	if(redis_sock->mode == MULTI) {
		// Discard any multi commands, and free any callbacks that have been queued
		send_discard_static(redis_sock TSRMLS_CC);
		free_reply_callbacks(getThis(), redis_sock);
	}
}

/* {{{ proto boolean Redis::connect(string host, int port [, double timeout [, long retry_interval]])
 */
PHP_METHOD(Myredis, connect)
{
	if (redis_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0) == FAILURE) {
		RETURN_FALSE;
	} else {
		RETURN_TRUE;
	}
}
/* }}} */

/* {{{ proto boolean Redis::close()
 */
PHP_METHOD(Myredis, close)
{
    zval *object;
    RedisSock *redis_sock = NULL;

    if (zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "O",
        &object, redis_ce) == FAILURE) {
        RETURN_FALSE;
    }

    if (redis_sock_get(object, &redis_sock TSRMLS_CC, 0) < 0) {
        RETURN_FALSE;
    }

    if (redis_sock_disconnect(redis_sock TSRMLS_CC)) {
        RETURN_TRUE;
    }

    RETURN_FALSE;
}
/* }}} */

/* {{{ proto boolean Redis::set(string key, mixed value, long timeout | array options) */
PHP_METHOD(Myredis, set) {
    zval *object;
    RedisSock *redis_sock;
    char *key = NULL, *val = NULL, *cmd, *exp_type = NULL, *set_type = NULL;
    int key_len, val_len, cmd_len;
    long expire = -1;
    int val_free = 0, key_free = 0;
    zval *z_value, *z_opts = NULL;

    // Make sure the arguments are correct
    if(zend_parse_method_parameters(ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "Osz|z",
                                    &object, redis_ce, &key, &key_len, &z_value,
                                    &z_opts) == FAILURE)
    {
        RETURN_FALSE;
    }

    // Ensure we can grab our redis socket
    if(redis_sock_get(object, &redis_sock TSRMLS_CC, 0) < 0) {
        RETURN_FALSE;
    }

    /* Our optional argument can either be a long (to support legacy SETEX */
    /* redirection), or an array with Redis >= 2.6.12 set options */
    if(z_opts && Z_TYPE_P(z_opts) != IS_LONG && Z_TYPE_P(z_opts) != IS_ARRAY) {
        RETURN_FALSE;
    }

    /* Serialization, key prefixing */
    val_free = redis_serialize(redis_sock, z_value, &val, &val_len TSRMLS_CC);
    key_free = redis_key_prefix(redis_sock, &key, &key_len TSRMLS_CC);

    if(z_opts && Z_TYPE_P(z_opts) == IS_ARRAY) {
        HashTable *kt = Z_ARRVAL_P(z_opts);
        int type;
        unsigned int ht_key_len;
        unsigned long idx;
        char *k;
        zval **v;

        /* Iterate our option array */
        for(zend_hash_internal_pointer_reset(kt);
            zend_hash_has_more_elements(kt) == SUCCESS;
            zend_hash_move_forward(kt))
        {
            // Grab key and value
            type = zend_hash_get_current_key_ex(kt, &k, &ht_key_len, &idx, 0, NULL);
            zend_hash_get_current_data(kt, (void**)&v);

            if(type == HASH_KEY_IS_STRING && (Z_TYPE_PP(v) == IS_LONG) &&
               (Z_LVAL_PP(v) > 0) && IS_EX_PX_ARG(k))
            {
                exp_type = k;
                expire = Z_LVAL_PP(v);
            } else if(Z_TYPE_PP(v) == IS_STRING && IS_NX_XX_ARG(Z_STRVAL_PP(v))) {
                set_type = Z_STRVAL_PP(v);
            }
        }
    } else if(z_opts && Z_TYPE_P(z_opts) == IS_LONG) {
        expire = Z_LVAL_P(z_opts);
    }

    /* Now let's construct the command we want */
    if(exp_type && set_type) {
        /* SET <key> <value> NX|XX PX|EX <timeout> */
        cmd_len = redis_cmd_format_static(&cmd, "SET", "ssssl", key, key_len,
                                          val, val_len, set_type, 2, exp_type,
                                          2, expire);
    } else if(exp_type) {
        /* SET <key> <value> PX|EX <timeout> */
        cmd_len = redis_cmd_format_static(&cmd, "SET", "sssl", key, key_len,
                                          val, val_len, exp_type, 2, expire);
    } else if(set_type) {
        /* SET <key> <value> NX|XX */
        cmd_len = redis_cmd_format_static(&cmd, "SET", "sss", key, key_len,
                                          val, val_len, set_type, 2);
    } else if(expire > 0) {
        /* Backward compatible SETEX redirection */
        cmd_len = redis_cmd_format_static(&cmd, "SETEX", "sds", key, key_len,
                                          expire, val, val_len);
    } else {
        /* SET <key> <value> */
        cmd_len = redis_cmd_format_static(&cmd, "SET", "ss", key, key_len,
                                          val, val_len);
    }

    /* Free our key or value if we prefixed/serialized */
    if(key_free) efree(key);
    if(val_free) efree(val);

    /* Kick off the command */
    REDIS_PROCESS_REQUEST(redis_sock, cmd, cmd_len);
    IF_ATOMIC() {
        redis_boolean_response(INTERNAL_FUNCTION_PARAM_PASSTHRU, redis_sock, NULL, NULL);
    }
    REDIS_PROCESS_RESPONSE(redis_boolean_response);
}
/* }}} */
