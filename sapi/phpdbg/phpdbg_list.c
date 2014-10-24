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
   | Authors: Felipe Pena <felipe@php.net>                                |
   | Authors: Joe Watkins <joe.watkins@live.co.uk>                        |
   | Authors: Bob Weinand <bwoebi@php.net>                                |
   +----------------------------------------------------------------------+
*/

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#	include <sys/mman.h>
#	include <unistd.h>
#endif
#include <fcntl.h>
#include "phpdbg.h"
#include "phpdbg_list.h"
#include "phpdbg_utils.h"
#include "phpdbg_prompt.h"
#include "php_streams.h"

ZEND_EXTERN_MODULE_GLOBALS(phpdbg);

#define PHPDBG_LIST_COMMAND_D(f, h, a, m, l, s, flags) \
	PHPDBG_COMMAND_D_EXP(f, h, a, m, l, s, &phpdbg_prompt_commands[13], flags)

const phpdbg_command_t phpdbg_list_commands[] = {
	PHPDBG_LIST_COMMAND_D(lines,     "lists the specified lines",    'l', list_lines,  NULL, "l", PHPDBG_ASYNC_SAFE),
	PHPDBG_LIST_COMMAND_D(class,     "lists the specified class",    'c', list_class,  NULL, "s", PHPDBG_ASYNC_SAFE),
	PHPDBG_LIST_COMMAND_D(method,    "lists the specified method",   'm', list_method, NULL, "m", PHPDBG_ASYNC_SAFE),
	PHPDBG_LIST_COMMAND_D(func,      "lists the specified function", 'f', list_func,   NULL, "s", PHPDBG_ASYNC_SAFE),
	PHPDBG_END_COMMAND
};

PHPDBG_LIST(lines) /* {{{ */
{
	if (!PHPDBG_G(exec) && !zend_is_executing(TSRMLS_C)) {
		phpdbg_error("inactive", "type=\"execution\"", "Not executing, and execution context not set");
		return SUCCESS;
	}

	switch (param->type) {
		case NUMERIC_PARAM:
			phpdbg_list_file(phpdbg_current_file(TSRMLS_C),
				(param->num < 0 ? 1 - param->num : param->num),
				(param->num < 0 ? param->num : 0) + zend_get_executed_lineno(TSRMLS_C),
				0 TSRMLS_CC);
			break;

		case FILE_PARAM:
			phpdbg_list_file(param->file.name, param->file.line, 0, 0 TSRMLS_CC);
			break;

		phpdbg_default_switch_case();
	}

	return SUCCESS;
} /* }}} */

PHPDBG_LIST(func) /* {{{ */
{
	phpdbg_list_function_byname(param->str, param->len TSRMLS_CC);

	return SUCCESS;
} /* }}} */

PHPDBG_LIST(method) /* {{{ */
{
	zend_class_entry **ce;

	if (phpdbg_safe_class_lookup(param->method.class, strlen(param->method.class), &ce TSRMLS_CC) == SUCCESS) {
		zend_function *function;
		char *lcname = zend_str_tolower_dup(param->method.name, strlen(param->method.name));

		if (zend_hash_find(&(*ce)->function_table, lcname, strlen(lcname)+1, (void**) &function) == SUCCESS) {
			phpdbg_list_function(function TSRMLS_CC);
		} else {
			phpdbg_error("list", "type=\"notfound\" method=\"%s::%s\"", "Could not find %s::%s", param->method.class, param->method.name);
		}

		efree(lcname);
	} else {
		phpdbg_error("list", "type=\"notfound\" class=\"%s\"", "Could not find the class %s", param->method.class);
	}

	return SUCCESS;
} /* }}} */

PHPDBG_LIST(class) /* {{{ */
{
	zend_class_entry **ce;

	if (phpdbg_safe_class_lookup(param->str, param->len, &ce TSRMLS_CC) == SUCCESS) {
		if ((*ce)->type == ZEND_USER_CLASS) {
			if ((*ce)->info.user.filename) {
				phpdbg_list_file(
					(*ce)->info.user.filename,
					(*ce)->info.user.line_end - (*ce)->info.user.line_start + 1,
					(*ce)->info.user.line_start, 0 TSRMLS_CC
				);
			} else {
				phpdbg_error("list", "type=\"nosource\" class=\"%s\"", "The source of the requested class (%s) cannot be found", (*ce)->name);
			}
		} else {
			phpdbg_error("list", "type=\"internalclass\" class=\"%s\"", "The class requested (%s) is not user defined", (*ce)->name);
		}
	} else {
		phpdbg_error("list", "type=\"notfound\" class=\"%s\"", "The requested class (%s) could not be found", param->str);
	}

	return SUCCESS;
} /* }}} */

void phpdbg_list_file(const char *filename, uint count, int offset, uint highlight TSRMLS_DC) /* {{{ */
{
	uint line, lastline;
	phpdbg_file_source **data;

	if (zend_hash_find(&PHPDBG_G(file_sources), filename, strlen(filename), (void **) &data) == FAILURE) {
		phpdbg_error("list", "type=\"unknownfile\"", "Could not find information about included file...");
		return;
	}

	if (offset < 0) {
		count += offset;
		offset = 0;
	}

	lastline = offset + count;

	if (lastline > (*data)->lines) {
		lastline = (*data)->lines;
	}

	phpdbg_xml("<list %r file=\"%s\">", filename);

	for (line = offset; line < lastline;) {
		uint linestart = (*data)->line[line++];
		uint linelen = (*data)->line[line] - linestart;
		char *buffer = (*data)->buf + linestart;

		if (!highlight) {
			phpdbg_write("line", "line=\"%u\" code=\"%.*s\"", " %05u: %.*s", line, linelen, buffer);
		} else {
			if (highlight != line) {
				phpdbg_write("line", "line=\"%u\" code=\"%.*s\"", " %05u: %.*s", line, linelen, buffer);
			} else {
				phpdbg_write("line", "line=\"%u\" code=\"%.*s\" current=\"current\"", ">%05u: %.*s", line, linelen, buffer);
			}
		}

		if (*(buffer + linelen - 1) != '\n' || !linelen) {
			phpdbg_out("\n");
		}
	}

	phpdbg_xml("</list>");
} /* }}} */

void phpdbg_list_function(const zend_function *fbc TSRMLS_DC) /* {{{ */
{
	const zend_op_array *ops;

	if (fbc->type != ZEND_USER_FUNCTION) {
		phpdbg_error("list", "type=\"internalfunction\" function=\"%s\"", "The function requested (%s) is not user defined", fbc->common.function_name);
		return;
	}

	ops = (zend_op_array *)fbc;

	phpdbg_list_file(ops->filename, ops->line_end - ops->line_start + 1, ops->line_start, 0 TSRMLS_CC);
} /* }}} */

void phpdbg_list_function_byname(const char *str, size_t len TSRMLS_DC) /* {{{ */
{
	HashTable *func_table = EG(function_table);
	zend_function* fbc;
	char *func_name = (char*) str;
	size_t func_name_len = len;

	/* search active scope if begins with period */
	if (func_name[0] == '.') {
		if (EG(scope)) {
			func_name++;
			func_name_len--;

			func_table = &EG(scope)->function_table;
		} else {
			phpdbg_error("inactive", "type=\"noclasses\"", "No active class");
			return;
		}
	} else if (!EG(function_table)) {
		phpdbg_error("inactive", "type=\"function_table\"", "No function table loaded");
		return;
	} else {
		func_table = EG(function_table);
	}

	/* use lowercase names, case insensitive */
	func_name = zend_str_tolower_dup(func_name, func_name_len);

	phpdbg_try_access {
		if (zend_hash_find(func_table, func_name, func_name_len+1, (void**)&fbc) == SUCCESS) {
			phpdbg_list_function(fbc TSRMLS_CC);
		} else {
			phpdbg_error("list", "type=\"nofunction\" function=\"%s\"", "Function %s not found", func_name);
		}
	} phpdbg_catch_access {
		phpdbg_error("signalsegv", "function=\"%s\"", "Could not list function %s, invalid data source", func_name);
	} phpdbg_end_try_access();

	efree(func_name);
} /* }}} */

zend_op_array *phpdbg_compile_file(zend_file_handle *file, int type TSRMLS_DC) {
	phpdbg_file_source data, *dataptr;
	zend_file_handle fake = {0};
	zend_op_array *ret;
	char *filename = (char *)(file->opened_path ? file->opened_path : file->filename);
	uint line;
	char *bufptr, *endptr;

	zend_stream_fixup(file, &data.buf, &data.len TSRMLS_CC);

	data.filename = filename;
	data.line[0] = 0;

	if (file->handle.stream.mmap.old_closer) {
		/* do not unmap */
		file->handle.stream.closer = file->handle.stream.mmap.old_closer;
	}

#if HAVE_MMAP
	if (file->handle.stream.mmap.map) {
		data.map = file->handle.stream.mmap.map;
	}
#endif

	fake.type = ZEND_HANDLE_MAPPED;
	fake.handle.stream.mmap.buf = data.buf;
	fake.handle.stream.mmap.len = data.len;
	fake.free_filename = 0;
	fake.opened_path = file->opened_path;
	fake.filename = filename;
	fake.opened_path = file->opened_path;

	*(dataptr = emalloc(sizeof(phpdbg_file_source) + sizeof(uint) * data.len)) = data;
	zend_hash_add(&PHPDBG_G(file_sources), filename, strlen(filename), &dataptr, sizeof(phpdbg_file_source *), NULL);

	for (line = 0, bufptr = data.buf - 1, endptr = data.buf + data.len; ++bufptr < endptr;) {
		if (*bufptr == '\n') {
			dataptr->line[++line] = (uint)(bufptr - data.buf) + 1;
		}
	}
	dataptr->lines = ++line;
	dataptr->line[line] = endptr - data.buf;
	dataptr = erealloc(dataptr, sizeof(phpdbg_file_source) + sizeof(uint) * line);

	ret = PHPDBG_G(compile_file)(&fake, type TSRMLS_CC);

	fake.opened_path = NULL;
	zend_file_handle_dtor(&fake TSRMLS_CC);

	return ret;
}

void phpdbg_free_file_source(phpdbg_file_source *data) {
#if HAVE_MMAP
	if (data->map) {
		munmap(data->map, data->len + ZEND_MMAP_AHEAD);
	} else
#endif
	if (data->buf) {
		efree(data->buf);
	}

	efree(data);
}

void phpdbg_init_list(TSRMLS_D) {
	PHPDBG_G(compile_file) = zend_compile_file;
	zend_hash_init(&PHPDBG_G(file_sources), 1, NULL, (dtor_func_t) phpdbg_free_file_source, 0);
	zend_compile_file = phpdbg_compile_file;
}
