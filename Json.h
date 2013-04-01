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

// Json_val_t 的属性
enum {	JF_NONE		= 0,
	JF_ALLOC	= 0x1U,	// 动态分配的内存，需要释放
	JF_ESCAPES	= 0x2U,	// 包含了转义字符，需要转义
	JF_SORT		= 0x4U	// object对象是否排序过
};

// Json_val_t的类型
enum {	JT_NONE,
	JT_NULL,	// null类型
	JT_TRUE,	// bool值True
	JT_FALSE,	// bool值false
	JT_NUM_RAW,	// 数值的原始字符串, 类型是一个Json_str_t
	JT_INT,		// 整数
	JT_REAL,	// 浮点数
	JT_STRING,	// 字符串
	JT_OBJECT,	// object对象
	JT_ARRAY,	// 数组
	JT_RAW		// 内部使用的字符串
};

// 编码/解码的选项
enum {
	JSON_ENCODE_OPT_RAW_FIELDNAME	= 0x1, // 编码时对object的属性名不做转义处理(调用需保证合法)
	JSON_DECODE_OPT_UNESCAPE	= 0x2, // 解码时对字符串不做转义处理
	JSON_DEOCDE_OPT_RAW		= 0x4, // 解码时使用原始字符串保存值，不做转换，在查询时转换
};

// public
#define OBJECT_FIELDS_SORT_NUM	16	// object对象的属性超过这个数字将会排序
typedef struct _Json_t { Json_val_t root; } Json_t;

Json_decode_ctx *Json_decode_ctx_create(unsigned options);
void Json_decode_ctx_destroy(Json_decode_ctx *ctx);

Json_t *Json_parse(Json_decode_ctx *ctx, char *json);
void Json_destroy(Json_t *json);

// 查询一个数组的一项, 参数是数组的下标
Json_val_t *Json_array_index(Json_val_t *root, size_t pos);
// 查询对象的一个属性, 参数是对象的属性名
Json_val_t *Json_object_value(Json_val_t *object, const char *field);

/* fmt的格式字符
 * a: 查询一个数组的一项, 参数是数组的下标
 * o: 查询对象的一个属性, 参数是对象的属性名
 * Json_query(json, "ooaa", "field1", "field2", 1, 2) 等价于
 * Json_val_t *v = Json_object_value(json, "field1");
 * v = Json_object_value(v, field2);
 * v = Json_array_index(v, 1);
 * v = Json_array_index(v, 2);
 */
Json_val_t *Json_query(Json_val_t *root, const char *fmt, ...);

// 编码方法
Json_encode_ctx *Json_encode_ctx_create(size_t len, size_t max_depth, unsigned options);
void Json_encode_ctx_destroy(Json_encode_ctx *enc);
void Json_encode_ctx_clear(Json_encode_ctx *enc);

int Json_encode_begin_array(Json_encode_ctx *enc, const char *name, size_t len);
int Json_encode_end_array(Json_encode_ctx *enc);
int Json_encode_begin_object(Json_encode_ctx *enc, const char *name, size_t len);
int Json_encode_end_object(Json_encode_ctx *enc);

int Json_encode_append(Json_encode_ctx *enc, const char *name, size_t nlen, int type, union Json_val val);
int Json_encode_append_bool(Json_encode_ctx *enc, int b, const char *name, size_t nlen);
int Json_encode_append_null(Json_encode_ctx *enc, const char *name, size_t nlen);
int Json_encode_append_integer(Json_encode_ctx *enc, int64_t v, const char *name, size_t nlen);
int Json_encode_append_real(Json_encode_ctx *enc, double real, const char *name, size_t nlen);
int Json_encode_append_string(Json_encode_ctx *enc, const char *str, size_t len, const char *name, size_t nlen);

int Json_encode_get_result(Json_encode_ctx *enc, const char **result, size_t *len);
#endif // __JSON_H__
