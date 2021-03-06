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

static json_t *decode_request(FILE *file, const char *buf, size_t len,
		json_t **_request)
{
	json_t *request;
	json_error_t err;

	if (file) {
		request = json_loadf(file, 0, &err);
	} else {
		request = json_loadb(buf, len, 0, &err);
	}
	if (!request) {
		return jsonrpc_error_object_str(ERR_PARSE_ERROR, err.text);
	}

	*_request = request;

	return NULL;
}

static json_t *validate_request(json_t *req, json_t **_method, json_t **_params,
		json_t **_id)
{
	int rc;
	json_error_t err;
	char *jsonrpc;
	json_t *method = NULL, *params = NULL, *id = NULL;

	rc = json_unpack_ex(req, &err, 0, "{s:s,s:o,s?o,s?o}",
			"jsonrpc", &jsonrpc,
			"method", &method,
			"params", &params,
			"id", &id);

	if (rc) {
		return jsonrpc_error_object_str(ERR_INVALID_REQUEST, err.text);
	}

	if (id && !json_is_string(id) && !json_is_number(id) && !json_is_null(id)) {
		return jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"\"id\" must contain a string, number, or NULL value");
	}

	if (strcmp(jsonrpc, "2.0")) {
		return jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"\"jsonrpc\" must be exactly \"2.0\"");
	}

	if (!json_is_string(method)) {
		return jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"\"method\" must be a string");
	}

	if (params && !json_is_array(params) && !json_is_object(params)) {
		return jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"\"params\" must be a an array or an object");
	}

	*_id = json_incref(id);
	*_method = json_incref(method);
	*_params = json_incref(params);

	return NULL;
}

static json_t *dispatch_request(const char* method, json_t *params,
		json_t **_result)
{
	jsonrpc_ret_t ret;
	json_t *result = NULL;
	struct rpc_callback *walk;

	/* find callback */
	for (walk = rpc_callbacks; walk; walk = walk->next) {
		if (!strcmp(method, walk->name)) {
			break;
		}
	}

	if (!walk) {
		return jsonrpc_error_object(ERR_METHOD_NOT_FOUND, NULL);
	}

	/* call callback */
	ret = walk->cb(params);
	if (!ret) {
		return jsonrpc_error_object(ERR_INTERNAL_ERROR, NULL);
	} else if (ret->type == JSONRPC_ERROR) {
		json_t *err = ret->obj;
		free(ret);
		return err;
	} else if (ret->type == JSONRPC_RESULT) {
		result = ret->obj;
	} else {
		return jsonrpc_error_object(ERR_INTERNAL_ERROR, NULL);
	}
	free(ret);
	*_result = result;

	return NULL;
}

static char *encode_response(json_t *response)
{
	size_t flags = 0;

	if (config & JSONRPC_ORDERED_RESPONSE) {
		flags |= JSON_PRESERVE_ORDER;
	}

	return json_dumps(response, flags);
}

static json_t *_jsonrpc_handle_single_request(json_t *request)
{
	json_t *error;
	json_t *method = NULL, *params = NULL, *id = NULL;
	json_t *result = NULL;
	json_t *response = NULL;

	error = validate_request(request, &method, &params, &id);
	if (error) {
		/* if there was an parse error or an invalid request error, the id must
		 * be set to null */
		id = json_null();
	}

	if (!error) {
		error = dispatch_request(json_string_value(method), params, &result);
		json_decref(method);
		json_decref(params);
	}

	if (!id) {
		/* this is a notification, no response is sent */
		json_decref(result);
		json_decref(error);
		return NULL;
	}

	response = jsonrpc_response_object(result, error, id);
	json_decref(result);
	json_decref(error);
	json_decref(id);

	return response;
}

static json_t *_jsonrpc_handle_multiple_requests(json_t *requests)
{
	int i;
	json_t *responses;

	responses = json_array();
	for (i = 0; i < json_array_size(requests); i++) {
		json_t *request = json_array_get(requests, i);
		json_t *response = _jsonrpc_handle_single_request(request);
		if (response) {
			json_array_append_new(responses, response);
		}
	}

	if (json_array_size(responses) == 0) {
		json_decref(responses);
		responses = NULL;
	}

	return responses;
}

static char *_jsonrpc_handle_request(FILE* file, const char *buf, size_t len)
{
	char *ret;
	json_t *id, *error, *request = NULL, *response;

	error = decode_request(file, buf, len, &request);
	if (error) {
		goto error;
	}

	if (json_is_array(request) && json_array_size(request) == 0) {
		json_decref(request);
		error = jsonrpc_error_object_str(ERR_INVALID_REQUEST,
				"Request must not be an empty array.");
		goto error;
	}

	if (!json_is_array(request)) {
		response = _jsonrpc_handle_single_request(request);
	} else {
		response = _jsonrpc_handle_multiple_requests(request);
	}
	json_decref(request);

	ret = encode_response(response);
	json_decref(response);

	return ret;

error:
	id = json_null();
	response = jsonrpc_response_object(NULL, error, id);
	json_decref(id);
	json_decref(error);
	ret = encode_response(response);
	json_decref(response);

	return ret;
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

static void __attribute__((destructor)) _jsonrpc_unregister_all(void)
{
	struct rpc_callback *next, *walk = rpc_callbacks;

	while (walk) {
		next = walk->next;
		free(walk);
		walk = next;
	}
}

void jsonrpc_config_set(jsonrpc_confflags_t flags)
{
	config = flags;
}
