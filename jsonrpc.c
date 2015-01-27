/*
 * Small JSON-RPC server implementation in C.
 *
 * Copyright (c) 2015, Michael Walle <michael@walle.cc>
 * See LICENSE for licensing terms.
 */

#include <string.h>
#include <assert.h>
#include <jansson.h>
#include <stdbool.h>

#include "jsonrpc.h"

struct rpc_callback {
	const char *name;
	rpc_callback cb;
	struct rpc_callback *next;
};

enum rsp_error {
	ERR_NO_ERR,
	ERR_PARSE_ERROR,
	ERR_INVALID_REQUEST,
	ERR_METHOD_NOT_FOUND,
	ERR_INVALID_PARAMS,
	ERR_INTERNAL_ERROR,
};

struct jsonrpc_ret {
	enum {
		JSONRPC_ERROR,
		JSONRPC_RESULT,
	} type;
	json_t *obj;
};

static jsonrpc_confflags_t config;
static struct rpc_callback *rpc_callbacks = NULL;

static json_t *jsonrpc_error_object(enum rsp_error err, json_t *data)
{
	json_t *errobj;

	static const int code[] = {
		[ERR_PARSE_ERROR] = -32700,
		[ERR_INVALID_REQUEST] = -32600,
		[ERR_METHOD_NOT_FOUND] = -32601,
		[ERR_INVALID_PARAMS] = -32602,
		[ERR_INTERNAL_ERROR] = -32603,
	};

	static const char *message[] = {
		[ERR_PARSE_ERROR] = "Parse error",
		[ERR_INVALID_REQUEST] = "Invalid Request",
		[ERR_METHOD_NOT_FOUND] = "Method not found",
		[ERR_INVALID_PARAMS] = "Invalid params",
		[ERR_INTERNAL_ERROR] = "Internal error",
	};

	errobj = json_object();
	json_object_set_new(errobj, "code", json_integer(code[err]));
	json_object_set_new(errobj, "message", json_string(message[err]));
	if (!(config & JSONRPC_DISABLE_ERROR_TEXT) && data) {
		json_object_set(errobj, "data", data);
	}

	return errobj;
}

static json_t *jsonrpc_error_object_str(enum rsp_error err, const char *str)
{
	json_t *errobj, *data;
	data = json_string(str);
	errobj = jsonrpc_error_object(err, data);
	json_decref(data);

	return errobj;
}

static json_t *jsonrpc_response_object(json_t *result, json_t *error,
		json_t *id)
{
	json_t *rspobj;

	assert((result == NULL && error != NULL) || (result != NULL && error == NULL));
	assert(id);

	rspobj = json_object();
	json_object_set_new(rspobj, "jsonrpc", json_string("2.0"));
	if (result) {
		json_object_set(rspobj, "result", result);
	}
	if (error) {
		json_object_set(rspobj, "error", error);
	}
	json_object_set(rspobj, "id", id);

	return rspobj;
}

static char *_jsonrpc_handle_request(FILE* file, const char *buf, size_t len)
{
	int rc;
	jsonrpc_ret_t ret;
	json_error_t err;
	const char* jsonrpc;
	const char *method;
	json_t *req, *rsp, *params = NULL, *id = NULL, *nid = NULL;
	json_t *result = NULL, *error = NULL;
	struct rpc_callback *walk;
	char *rbuf;
	size_t flags = 0;

	/* decode */
	if (file) {
		req = json_loadf(file, 0, &err);
	} else {
		req = json_loadb(buf, len, 0, &err);
	}
	if (!req) {
		error = jsonrpc_error_object_str(ERR_PARSE_ERROR, err.text);
		goto send_rsp;
	}

	/* validate */
	rc = json_unpack_ex(req, &err, 0, "{s:s,s:s,s?o,s?o}",
			"jsonrpc", &jsonrpc,
			"method", &method,
			"params", &params,
			"id", &id);

	if (rc) {
		error = jsonrpc_error_object_str(ERR_INVALID_REQUEST, err.text);
		goto send_rsp;
	}

	if (strcmp(jsonrpc, "2.0")) {
		error = jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"\"jsonrpc\" must be exactly \"2.0\"");
		goto send_rsp;
	}

	if (id && !json_is_string(id) && !json_is_number(id) && !json_is_null(id)) {
		error = jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"\"id\" must contain a string, number, or NULL value");
		/* id is incorrect. we set it to NULL in which case null will be
		 * send in the response */
		id = NULL;
		goto send_rsp;
	}

	if (params && !json_is_array(params) && !json_is_object(params)) {
		error = jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"\"params\" must be a an array or an object");
		goto send_rsp;
	}

	/* find callback */
	for (walk = rpc_callbacks; walk; walk = walk->next) {
		if (!strcmp(method, walk->name)) {
			break;
		}
	}

	if (!walk) {
		error = jsonrpc_error_object(ERR_METHOD_NOT_FOUND, NULL);
		goto send_rsp;
	}

	/* call callback */
	ret = walk->cb(params);
	if (!ret) {
		error = jsonrpc_error_object(ERR_INTERNAL_ERROR, NULL);
	} else if (ret->type == JSONRPC_ERROR) {
		error = ret->obj;
	} else if (ret->type == JSONRPC_RESULT) {
		result = ret->obj;
	} else {
		error = jsonrpc_error_object(ERR_INTERNAL_ERROR, NULL);
	}
	free(ret);

send_rsp:
	/* every response must have an id member. if there was an error reading the
	 * id from the request the id must be null. */
	if (error && !id) {
		id = nid = json_null();
	}
	rsp = jsonrpc_response_object(result, error, id);
	json_decref(result);
	json_decref(error);
	json_decref(req);
	json_decref(nid);

	/* encode */
	if (config & JSONRPC_ORDERED_RESPONSE) {
		flags |= JSON_PRESERVE_ORDER;
	}
	rbuf = json_dumps(rsp, flags);

	assert(rbuf);
	json_decref(rsp);

	return rbuf;
}

char *jsonrpc_handle_request(const char *buf, size_t len)
{
	return _jsonrpc_handle_request(NULL, buf, len);
}

char *jsonrpc_handle_request_from_file(FILE *file)
{
	return _jsonrpc_handle_request(file, NULL, 0);
}

jsonrpc_ret_t jsonrpc_result(json_t *result)
{
	jsonrpc_ret_t ret;

	ret = malloc(sizeof(struct jsonrpc_ret));
	ret->type = JSONRPC_RESULT;
	ret->obj = result;
	return ret;
}

static jsonrpc_ret_t _jsonrpc_error(enum rsp_error err, json_t *data)
{
	jsonrpc_ret_t ret;

	ret = malloc(sizeof(struct jsonrpc_ret));
	ret->type = JSONRPC_ERROR;
	ret->obj = jsonrpc_error_object(err, data);
	json_decref(data);
	return ret;
}

jsonrpc_ret_t jsonrpc_error_invalid_params(json_t *data)
{
	return _jsonrpc_error(ERR_INVALID_PARAMS, data);
}

jsonrpc_ret_t jsonrpc_error_internal_error(json_t *data)
{
	return _jsonrpc_error(ERR_INTERNAL_ERROR, data);
}

void _jsonrpc_register(const char *name, rpc_callback cb)
{
	struct rpc_callback *new;

	new = malloc(sizeof(*new));
	assert(new);

	new->name = name;
	new->cb = cb;
	new->next = NULL;

	if (rpc_callbacks == NULL) {
		rpc_callbacks = new;
	} else {
		struct rpc_callback *walk = rpc_callbacks;
		while (walk->next) {
			walk = walk->next;
		}
		walk->next = new;
	}
}

void jsonrpc_config_set(jsonrpc_confflags_t flags)
{
	config = flags;
}
