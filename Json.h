#ifndef __JSON_H__
#define __JSON_H__

// private
typedef struct _Json_val_t Json_val_t;
typedef struct _Json_pair_t Json_pair_t;
typedef struct _Json_decode_ctx Json_decode_ctx;

typedef struct _Json_str_t {
	uint32_t is_alloc:1;
	uint32_t is_escapes:1;
	uint32_t len:30;
	const char *str;
} Json_str_t;

typedef struct _Json_arr_t {
	uint32_t	len;
	Json_val_t	*arr;
} Json_arr_t;

typedef struct _Json_obj_t {
	uint32_t is_sort:1;
	uint32_t len:31;
	Json_pair_t *objects;
} Json_obj_t;

typedef struct _Json_numraw_t {
	uint8_t int_len;
	uint8_t frace_len;
	uint8_t exp_len;
	const char *number;
} Json_num_t;

struct _Json_val_t {
	uint8_t		   var_type;
	union {
		double	   real;
		int64_t    integer;
		Json_num_t numraw;
		Json_str_t string;
		Json_obj_t object;
		Json_arr_t array;
	} v;
};

struct _Json_pair_t {
	Json_val_t	name;
	Json_val_t	value;
};

enum { JT_NONE, JT_NULL, JT_TRUE, JT_FALSE, JT_NUM_RAW, JT_INT, JT_REAL, JT_STRING, JT_OBJECT, JT_ARRAY };

// public
#define OBJECT_FIELDS_SORT_NUM	16
typedef struct _Json_t { Json_val_t root; } Json_t;

Json_decode_ctx *Json_decode_create(void);
void Json_decode_destroy(Json_decode_ctx *ctx);

void Json_destroy(Json_t *json);

Json_t *Json_parse(Json_decode_ctx *ctx, char *json);

Json_val_t *Json_query(Json_val_t *root, const char *path[]);
Json_val_t *Json_array_index(Json_val_t *root, size_t pos);
Json_val_t *Json_object_value(Json_val_t *object, const char *field);

#endif // __JSON_H__
