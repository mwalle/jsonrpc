#include <jansson.h>
#include "jsonrpc.h"

static jsonrpc_ret_t internal_error(json_t *params)
{
	return NULL;
}
jsonrpc_register(internal_error);

static jsonrpc_ret_t invalid_params(json_t *params)
{
	return jsonrpc_error_invalid_params(NULL);
}
jsonrpc_register(invalid_params);

static jsonrpc_ret_t noop(json_t *params)
{
	return jsonrpc_result(json_null());
}
jsonrpc_register(noop);

static jsonrpc_ret_t add(json_t *params)
{
	json_error_t err;
	int rc, a, b;

	rc = json_unpack_ex(params, &err, 0, "[ii]", &a, &b);
	if (rc) {
		rc = json_unpack_ex(params, &err, 0, "{s:i,s:i}",
				"a", &a,
				"b", &b);
	}
	if (rc) {
		return jsonrpc_error_invalid_params(json_string(err.text));
	}
	return jsonrpc_result(json_integer(a+b));
}
jsonrpc_register(add);

static jsonrpc_ret_t subtract(json_t *params)
{
	json_error_t err;
	int rc, a, b;

	rc = json_unpack_ex(params, &err, 0, "[ii]", &a, &b);
	if (rc) {
		rc = json_unpack_ex(params, &err, 0, "{s:i,s:i}",
				"minuend", &a,
				"subtrahend", &b);
	}
	if (rc) {
		return jsonrpc_error_invalid_params(json_string(err.text));
	}
	return jsonrpc_result(json_integer(a-b));
}
jsonrpc_register(subtract);

int main()
{
	char *buf;

	jsonrpc_config_set(
			  JSONRPC_DISABLE_ERROR_TEXT
			| JSONRPC_ORDERED_RESPONSE);

	buf = jsonrpc_handle_request_from_file(stdin);
	printf("%s\n", buf);
	free(buf);

	return 0;
}
