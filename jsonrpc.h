#ifndef __JSONRPC_H
#define __JSONRPC_H

typedef struct jsonrpc_ret *jsonrpc_ret_t;
typedef jsonrpc_ret_t (*rpc_callback)(json_t *root);
typedef enum {
	JSONRPC_DISABLE_ERROR_TEXT = (1<<0),
	JSONRPC_ORDERED_RESPONSE   = (1<<1),
} jsonrpc_confflags_t;

void jsonrpc_config_set(jsonrpc_confflags_t flags);
char *jsonrpc_handle_request(const char *buf, size_t len);
char *jsonrpc_handle_request_from_file(FILE *file);
void _jsonrpc_register(const char *name, rpc_callback cb);
jsonrpc_ret_t jsonrpc_result(json_t *result);
jsonrpc_ret_t jsonrpc_error_internal_error(json_t *data);
jsonrpc_ret_t jsonrpc_error_invalid_params(json_t *data);

#define jsonrpc_register_name(func, name) \
static void __attribute__((constructor)) __jsonrpc_register_ ## func(void) { \
	_jsonrpc_register(name, func); \
}

#define jsonrpc_register(func) \
	jsonrpc_register_name(func, #func)

#endif /* __JSONRPC_H */
