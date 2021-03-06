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

#ifndef PHP_MYREDIS_H
#define PHP_MYREDIS_H

PHP_METHOD(Myredis, __construct);
PHP_METHOD(Myredis, __destruct);
PHP_METHOD(Myredis, connect);
PHP_METHOD(Myredis, close);
PHP_METHOD(Myredis, get);
PHP_METHOD(Myredis, set);

extern zend_module_entry myredis_module_entry;
#define phpext_myredis_ptr &myredis_module_entry

#define PHP_MYREDIS_VERSION "0.1.0" /* Replace with version number for your extension */

#ifdef PHP_WIN32
#	define PHP_MYREDIS_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_MYREDIS_API __attribute__ ((visibility("default")))
#else
#	define PHP_MYREDIS_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(myredis);
PHP_MSHUTDOWN_FUNCTION(myredis);
PHP_RINIT_FUNCTION(myredis);
PHP_RSHUTDOWN_FUNCTION(myredis);
PHP_MINFO_FUNCTION(myredis);

PHP_FUNCTION(confirm_myredis_compiled);	/* For testing, remove later. */

/* 
  	Declare any global variables you may need between the BEGIN
	and END macros here:     

ZEND_BEGIN_MODULE_GLOBALS(myredis)
	long  global_value;
	char *global_string;
ZEND_END_MODULE_GLOBALS(myredis)
*/

/* In every utility function you add that needs to use variables 
   in php_myredis_globals, call TSRMLS_FETCH(); after declaring other 
   variables used by that function, or better yet, pass in TSRMLS_CC
   after the last function argument and declare your utility function
   with TSRMLS_DC after the last declared argument.  Always refer to
   the globals in your function as MYREDIS_G(variable).  You are 
   encouraged to rename these macros something shorter, see
   examples in any other php module directory.
*/

#ifdef ZTS
#define MYREDIS_G(v) TSRMG(myredis_globals_id, zend_myredis_globals *, v)
#else
#define MYREDIS_G(v) (myredis_globals.v)
#endif

#endif	/* PHP_MYREDIS_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
