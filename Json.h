#ifndef __JSON_H__
#define __JSON_H__
#include <stdint.h>

// private
typedef struct _Json_val_t Json_val_t;
typedef struct _Json_pair_t Json_pair_t;
typedef struct _Json_decode_ctx Json_decode_ctx;
typedef struct _Json_encode_ctx Json_encode_ctx;

typedef struct _Json_str_t {
	uint32_t len;
	const char *str;
} Json_str_t;

typedef struct _Json_arr_t {
	uint32_t	len;
	Json_val_t	*arr;
} Json_arr_t;

typedef struct _Json_obj_t {
	uint32_t len;
	Json_pair_t *objects;
} Json_obj_t;

typedef struct _Json_numraw_t {
	uint8_t nlen;
	uint8_t flen;
	uint8_t elen;
	const char *number;
} Json_num_t;

union Json_val {
	double	   real;
	int64_t    integer;
	Json_num_t numraw;
	Json_str_t string;
	Json_obj_t object;
	Json_arr_t array;
};

struct _Json_val_t {
	uint8_t		   val_type;
	uint16_t	   val_flag;
	union Json_val	   v;
};

struct _Json_pair_t {
	Json_val_t	name;
	Json_val_t	value;
};

enum { JF_NONE = 0, JF_IS_ALLOC = 0x1U, JF_IS_ESCAPES = 0x2U, JF_IS_SORT = 0x4U };
enum { JT_NONE, JT_NULL, JT_TRUE, JT_FALSE, JT_NUM_RAW, JT_INT, JT_REAL, JT_STRING, JT_OBJECT, JT_ARRAY, JT_RAW};

enum {	JSON_ENCODE_OPT_RAW_FIELDNAME = 0x1,
	JSON_DECODE_OPT_UNESCAPE = 0x2,
	JSON_DEOCDE_OPT_RAW = 0x4
};

// public
#define OBJECT_FIELDS_SORT_NUM	16
typedef struct _Json_t { Json_val_t root; } Json_t;

Json_decode_ctx *Json_decode_ctx_create(unsigned options);
void Json_decode_ctx_destroy(Json_decode_ctx *ctx);

void Json_destroy(Json_t *json);

Json_t *Json_parse(Json_decode_ctx *ctx, char *json);

Json_val_t *Json_array_index(Json_val_t *root, size_t pos);
Json_val_t *Json_object_value(Json_val_t *object, const char *field);

/* fmt:
 * a: index of array
 * o: field of object
 * Json_query(json, "ooaa", "field1", "field2", 1, 2);
 */
Json_val_t *Json_query(Json_val_t *root, const char *fmt, ...);

Json_encode_ctx *Json_encode_ctx_create(size_t init_len, size_t max_depth, unsigned option);
void Json_encode_ctx_destroy(Json_encode_ctx *enc);
void Json_encode_ctx_clear(Json_encode_ctx *enc);

int Json_encode_begin_array(Json_encode_ctx *enc, const char *name, size_t len);
int Json_encode_end_array(Json_encode_ctx *enc);
int Json_encode_begin_object(Json_encode_ctx *enc, const char *name, size_t len);
int Json_encode_end_object(Json_encode_ctx *enc);

int Json_encode_append(Json_encode_ctx *enc, const char *name, size_t nlen, int type, union Json_val val);
int Json_encode_append_bool(Json_encode_ctx *enc, int b, const char *name, size_t nlen);
const char *Json_encode_get_buffer(Json_encode_ctx *enc, size_t *len);
int Json_encode_append_null(Json_encode_ctx *enc, const char *name, size_t nlen);
int Json_encode_append_integer(Json_encode_ctx *enc, int64_t v, const char *name, size_t nlen);
int Json_encode_append_real(Json_encode_ctx *enc, double real, const char *name, size_t nlen);
int Json_encode_append_string(Json_encode_ctx *enc, const char *str, size_t len, const char *name, size_t nlen);
#endif // __JSON_H__
