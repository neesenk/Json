#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

#include "Json.h"

static inline void Json_num_covert(Json_val_t *val);
Json_str_t *Json_str_value(Json_val_t *val);

typedef struct _darray darray_t;
struct _darray {
	darray_t  *next;
	size_t	   max;
	size_t	   len;
	Json_val_t arr[0];
};

struct _Json_decode_ctx {
	uint8_t last_type;
	const char *raw;
	const char *pos;
	darray_t *darray_list;
};

static darray_t *darray_create(size_t size)
{
	darray_t *n = calloc(1, sizeof(darray_t) + sizeof(Json_val_t) * size);
	if (n)
		n->max = size, n->len = 0, n->next = NULL;
	return n;
}

#define darray_destroy(da) free(da)
#define darray_clean(da)   (da)->len = 0

static int darray_push(darray_t **pda, Json_val_t *val)
{
	darray_t *da = *pda;
	if (da->max == da->len) {
		da = realloc(da, sizeof(darray_t) + sizeof(Json_val_t) * da->max * 2);
		if (da == NULL)
			return false;
		*pda = da, da->max *= 2;
	}

	da->arr[da->len++] = *val;
	return true;
}

static darray_t *darray_dequeue(Json_decode_ctx *ctx)
{
	if (ctx->darray_list) {
		darray_t *ret  = ctx->darray_list;
		ctx->darray_list = ret->next;
		return ret;
	}

	return darray_create(64);
}

static void darray_queue(Json_decode_ctx *ctx, darray_t *da)
{
	da->len = 0;
	da->next = ctx->darray_list;
	ctx->darray_list = da;
}

static int parse_any(Json_decode_ctx *ctx, Json_val_t *val);

#ifdef __SSE4_2__ // 支持sse4.2指令
#include <nmmintrin.h>

#define SSEMACH(w,s) _mm_cvtsi128_si32(_mm_cmpistrm(w, s, _SIDD_UBYTE_OPS | \
			_SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK | _SIDD_NEGATIVE_POLARITY))
static inline const char *_skip_blank(const char* p)
{
	static const char whitespace[16] = " \n\r\t";
	__m128i w = _mm_loadu_si128((const __m128i *)whitespace);
	for (;;) {
		__m128i s = _mm_loadu_si128((const __m128i *)p);
		unsigned r = SSEMACH(w, s);
		if (r != 0) // some of characters may be non-whitespace
			return p + __builtin_ffs(r) - 1;
		p += 16;
	}
}

static inline const char *_string_tail(const char *p, int *is_esc)
{
	static const char tail[16] = "\\\"";
	__m128i w = _mm_loadu_si128((const __m128i *)tail);
	for (;;) {
		__m128i s = _mm_loadu_si128((const __m128i *)p);
		unsigned r = SSEMACH(w, s);
		int off = 16;
		r = ~r & 0xffff;
		if (r != 0) {
			do {
				off = __builtin_ffs(r) - 1;
				if (p[off] == '\0' || p[off] == '\"' || (p[off] == '\\' && p[off + 1] == '\0'))
					return p + off;
				*is_esc = 1;
				r &= ~(0x3 << off);
				off += 2;
			} while (r != 0);
		}

		p += off;
	}
}

static inline const char *_skip_digits(const char* p)
{
	static const char digits[16] = "0123456789";
	__m128i w = _mm_loadu_si128((const __m128i *)digits);
	for (;;) {
		__m128i s = _mm_loadu_si128((const __m128i *)p);
		unsigned r = SSEMACH(w, s);
		if (r != 0) // some of characters may be non-whitespace
			return p + __builtin_ffs(r) - 1;
		p += 16;
	}
}
#else
static inline const char *_skip_blank(const char *pos)
{
	int c;
	for (c = *pos; c == ' ' || c == '\t' || c == '\r' || c == '\n'; c = *pos)
		pos++;
	return pos;
}

static inline const char *_string_tail(const char *pos, int *is_esc)
{
	for (;;) {
		int c;
		for (c = *pos; c != '\0' && c != '"' && c != '\\'; c = *pos)
			pos++;
		if (c != '\\' || pos[1] == '\0')
			break;
		pos += 2, *is_esc = 1;
	}

	return pos;
}

static inline const char *_skip_digits(const char *pos)
{
	while (isdigit(*pos))
		pos++;
	return pos;
}
#endif

static int parse_number(Json_decode_ctx *ctx, Json_val_t *val)
{
	const char *pos, *num;
	size_t frace_len = 0, exp_len = 0;

	ctx->last_type = JT_NUM_RAW;
	num = pos = _skip_digits(ctx->pos + 1);

	if (*ctx->pos == '-' && num == ctx->pos + 1)
		return false;
	if (*pos == '.') {
		pos = _skip_digits(pos + 1);
		frace_len = pos - num - 1;
	}

	if (*pos == 'E' || *pos == 'e') {
		const char *tmp = pos + !!(pos[1] == '+' || pos[1] == '-');
		if ((pos = _skip_digits(tmp + 1)) == tmp + 1)
			return false;
		exp_len = pos - tmp - !!(*tmp != '+' && *tmp != '-');
	}

	val->var_type = JT_NUM_RAW;
	val->v.numraw = (Json_num_t){num - ctx->pos, frace_len, exp_len, ctx->pos};

	ctx->pos = pos;

	Json_num_covert(val);
	return true;
}

static int parse_string(Json_decode_ctx *ctx, Json_val_t *val)
{
	int is_esc = 0;
	const char *pos = _string_tail(ctx->pos + 1, &is_esc);
	ctx->last_type = JT_STRING;
	if (*pos != '"')
		return false;
	val->var_type = JT_STRING;
	val->v.string = (Json_str_t) { 0, is_esc, pos - ctx->pos - 1, ctx->pos + 1};
	ctx->pos = pos + 1;

	Json_str_value(val);
	return true;
}

static int parse_true(Json_decode_ctx *ctx, Json_val_t *val)
{
	val->var_type = ctx->last_type = JT_TRUE;
	if (ctx->pos[1] != 'r' || ctx->pos[2] != 'u' || ctx->pos[3] != 'e')
		return false;
	ctx->pos += 4;
	return true;
}

static int parse_false(Json_decode_ctx *ctx, Json_val_t *val)
{
	val->var_type = ctx->last_type = JT_FALSE;
	if (ctx->pos[1] != 'a' || ctx->pos[2] != 'l' || ctx->pos[3] != 's' || ctx->pos[4] != 'e')
		return false;
	ctx->pos += 5;
	return true;
}

static int parse_null(Json_decode_ctx * ctx, Json_val_t *val)
{
	val->var_type = ctx->last_type = JT_NULL;
	if (ctx->pos[1] != 'u' || ctx->pos[2] != 'l' || ctx->pos[3] != 'l')
		return false;
	ctx->pos += 4;
	return true;
}

static inline void skip_blank(Json_decode_ctx *ctx)
{
	ctx->pos = _skip_blank(ctx->pos);
}

static void skip_comment(Json_decode_ctx *ctx)
{
	const char *pos = NULL;
	if (ctx->pos[1] == '*') {
		pos = ctx->pos + 2;
		for (;;) {
			if ((pos = strchr(pos , '*')) == NULL) {
				pos = ctx->pos + strlen(ctx->pos);
				break;
			}

			if (pos[1] == '/') {
				pos += 2;
				break;
			}
		}
	} else {
		if ((pos = strchr(ctx->pos,  '\n')) == NULL)
			pos = ctx->pos + strlen(ctx->pos);
	}
	ctx->pos = pos;
}

static inline void skip_content(Json_decode_ctx *ctx)
{
	for (;;) {
		skip_blank(ctx);
		if (*ctx->pos != '/')
			break;
		skip_comment(ctx);
	}
}

static int parse_array(Json_decode_ctx *ctx, Json_val_t *val)
{
	int ret = false, i;
	Json_val_t obj, *arr;
	darray_t *da = darray_dequeue(ctx);

	if (da == NULL)
		return false;

	ctx->last_type = JT_ARRAY;
	ctx->pos++; // skip [
	for (;;) {
		skip_content(ctx);
		if (*ctx->pos == ']')
			break;
		if (!parse_any(ctx, &obj) || !darray_push(&da, &obj))
			goto DONE;

		skip_content(ctx);
		if (*ctx->pos == ']')
			break;
		if (*ctx->pos != ',')
			goto DONE;
		ctx->pos++;
	}
	ctx->pos++; // skip ]

	if (!(arr = calloc(da->len, sizeof(*arr))))
		goto DONE;

	for (i = 0; i < da->len; i++)
		arr[i] = da->arr[i];

	val->var_type = JT_ARRAY;
	val->v.array.len = da->len, val->v.array.arr = arr;
	ret = true;
DONE:
	darray_queue(ctx, da);
	return ret;
}

static int parse_object(Json_decode_ctx *ctx, Json_val_t *val)
{
	Json_pair_t *objs = NULL;
	Json_val_t tmp;
	int ret = false, i = 0, j = 0;
	darray_t *da = darray_dequeue(ctx);
	if (da == NULL)
		return false;

	ctx->last_type = JT_OBJECT;
	ctx->pos++; // skip {
	for (;;) {
		skip_content(ctx);
		if (*ctx->pos == '}')
			break;
		if (*ctx->pos != '"' || !parse_string(ctx, &tmp) || !darray_push(&da, &tmp))
			goto DONE;

		skip_content(ctx);
		if (*ctx->pos != ':')
			goto DONE;
		ctx->pos++;

		skip_content(ctx);
		if (!parse_any(ctx, &tmp) || !darray_push(&da, &tmp))
			goto DONE;

		skip_content(ctx);
		if (*ctx->pos == '}')
			break;
		if (*ctx->pos != ',')
			goto DONE;
		ctx->pos++;
	}
	ctx->pos++; // skip }

	if (!(objs = calloc(da->len / 2, sizeof(*objs))))
		goto DONE;

	for (i = 0, j = 0; i < da->len; i += 2, j++) {
		objs[j].name  = da->arr[i + 0];
		objs[j].value = da->arr[i + 1];
	}

	val->var_type = JT_OBJECT;
	val->v.object = (Json_obj_t) {0, da->len/2, objs};
	ret = true;
DONE:
	darray_queue(ctx, da);
	return ret;
}

static int parse_any(Json_decode_ctx *ctx, Json_val_t *val)
{
	switch (*ctx->pos) {
	case '"': return parse_string(ctx, val);
	case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':case '-':
		return parse_number(ctx, val);
	case '[': return parse_array(ctx, val);
	case '{': return parse_object(ctx, val);
	case 't': return parse_true(ctx, val);
	case 'f': return parse_false(ctx, val);
	case 'n': return parse_null(ctx, val);
	}
	return false;
}

Json_t *Json_parse(Json_decode_ctx *ctx, char *json)
{
	Json_val_t val;
	Json_t *ret = NULL;
	ctx->pos = ctx->raw = json;
	ctx->last_type = 0;

	skip_content(ctx);
	if (!parse_any(ctx, &val))
		return NULL;
	skip_content(ctx);

	if (*ctx->pos != '\0')
		return NULL;
	ret = calloc(1, sizeof(*ret));
	if (ret)
		ret->root = val;
	return ret;
}

static void _Json_destroy(Json_val_t *root)
{
	size_t i = 0;
	switch (root->var_type) {
	case JT_STRING:
		if (root->v.string.is_alloc)
			free((void *)root->v.string.str);
		break;
	case JT_ARRAY:
		for (i = 0; i < root->v.array.len; i++)
			_Json_destroy(root->v.array.arr + i);
		free(root->v.array.arr);
		break;
	case JT_OBJECT:
		for (i = 0; i < root->v.object.len; i++) {
			_Json_destroy(&root->v.object.objects[i].name);
			_Json_destroy(&root->v.object.objects[i].value);
		}
		free(root->v.object.objects);
		break;
	}
}

void Json_destroy(Json_t *json)
{
	if (json)
		_Json_destroy(&json->root);
	free(json);
}

#define STRPTR(val) (val).v.string.str
#define STRLEN(val) (val).v.string.len
static inline int Json_strcmp(const char *str1, size_t len1, const char *str2, size_t len2)
{
	int ret = memcmp(str1, str2, len1 > len2 ? len2 : len1);
	return (!ret || len1 == len2) ? ret : (len1 > len2 ? 1 : -1);
}

static int Json_object_cmp(const void *o1, const void *o2)
{
	const Json_pair_t *a = (const Json_pair_t *)o1;
	const Json_pair_t *b = (const Json_pair_t *)o2;
	return Json_strcmp(STRPTR(a->name), STRLEN(a->name), STRPTR(b->name), STRLEN(b->name));
}

Json_val_t *Json_object_value(Json_val_t *object, const char *field)
{
	size_t i;
	Json_obj_t *obj	    = &object->v.object;
	Json_pair_t *fields = obj->objects, cmp;

	if (object->var_type != JT_OBJECT)
		return NULL;

	if (!obj->is_sort && obj->len >= OBJECT_FIELDS_SORT_NUM) {
		qsort(fields, obj->len, sizeof(*fields), Json_object_cmp);
		obj->is_sort = 1;
	}

	cmp.name.v.string.len = strlen(field);
	cmp.name.v.string.str = field;

	if (obj->is_sort) {
		fields = bsearch(&cmp, fields, obj->len, sizeof(*fields), Json_object_cmp);
		return fields ? &fields->value : NULL;
	}

	for (i = 0; i < obj->len; i++) {
		if (!Json_object_cmp(fields + i, &cmp))
			return &fields[i].value;
	}

	return NULL;
}

Json_val_t *Json_query(Json_val_t *root, const char *path[])
{
	while (*path) {
		if (!root)
			return NULL;
		root = Json_object_value(root, *path++);
	}

	return root;
}

Json_val_t *Json_array_index(Json_val_t *root, size_t pos)
{
	if (root->var_type != JT_ARRAY || pos >= root->v.array.len)
		return NULL;
	return root->v.array.arr + pos;
}

Json_decode_ctx *Json_decode_create(void)
{
	return calloc(1, sizeof(Json_decode_ctx));
}

void Json_decode_destroy(Json_decode_ctx *ctx)
{
	if (ctx) {
		darray_t *da, *next;
		for (da = ctx->darray_list; da != NULL; da = next) {
			next = da->next;
			free(da);
		}
		free(ctx);
	}
}

static inline unsigned hex2unicode(unsigned char *in)
{
	unsigned code = 0;
	int i = 0;
	for (i = 0; i < 4; i++) {
		code <<= 4;
		if (in[i] >= '0' && in[i] <= '9')
			code += in[i] - '0';
		else if (in[i] >= 'A' && in[i] <= 'F')
			code += in[i] - 'A' + 10;
		else if (in[i] >= 'a' && in[i] <= 'f')
			code += in[i] - 'a' + 10;
		else
			return 0xffffffff;
	}
	return code;
}

static inline int utf8_encode(unsigned char *ou, unsigned code)
{
	if (code <= 0x7F) {
		*ou = code & 0xFF;
		return 1;
	} else if (code <= 0x7FF) {
		ou[0] = 0xC0 | ((code >> 6) & 0xFF);
		ou[1] = 0x80 | ((code & 0x3F));
		return 2;
	} else if (code <= 0xFFFF) {
		ou[0] = 0xE0 | ((code >> 12) & 0xFF);
		ou[1] = 0x80 | ((code >> 6) & 0x3F);
		ou[2] = 0x80 | (code & 0x3F);
		return 3;
	} else {
		if (code <= 0x10FFFF)
			return 0;
		ou[0] = 0xF0 | ((code >> 18) & 0xFF);
		ou[1] = 0x80 | ((code >> 12) & 0x3F);
		ou[2] = 0x80 | ((code >> 6) & 0x3F);
		ou[3] = 0x80 | (code & 0x3F);
		return 4;
	}

	return 0;
}

static size_t Json_unescape(char *in, size_t len)
{
	unsigned char *wp = NULL, *rp = NULL, *ep = (unsigned char *)in + len;
	if ((wp = rp = memchr(in, '\\', len)) == NULL)
		return len;

	while (rp < ep) {
		if (*rp != '\\') {
			*wp++ = *rp++;
			continue;
		}

		if (rp + 1 >= ep)
			break;

		if (rp[1] != 'u') {
			switch (rp[1]) { // 转义处理
			case 'b': *wp++ = '\b'; break;
			case 'f': *wp++ = '\f'; break;
			case 'n': *wp++ = '\n'; break;
			case 'r': *wp++ = '\r'; break;
			case 't': *wp++ = '\t'; break;
			default:  *wp++ = rp[1]; break;
			}
			rp += 2;
		} else {
			unsigned code = 0;
			if (ep - rp < 6 || (code = hex2unicode(rp + 2)) == 0xffffffff)
				goto ERR1;
			if (code >= 0xD800 && code <= 0xDBFF) { // Handle UTF-16 surrogate pair
				unsigned code2;
				if (ep - rp < 12 || (rp[6] != '\\' || rp[7] != 'u'))
					goto ERR2;
				if ((code2 = hex2unicode(rp + 8)) == 0xffffffff)
					goto ERR2;
				if (code2 < 0xDC00 || code2 > 0xDFFF)
					goto ERR2;
				code = (((code - 0xD800) << 10) | (code2 - 0xDC00)) + 0x10000;
				if ((code = utf8_encode(wp, code)) == 0)
					goto ERR2;
				rp += 12, wp += code;
			} else {
				if ((code = utf8_encode(wp, code)) == 0)
					goto ERR1;
				rp += 6, wp += code;
			}
			continue;
		ERR1:
			code = ep - rp < 6 ? ep - rp : 6;
			goto ERR3;
		ERR2:
			code = ep - rp < 12 ? ep - rp : 12;
		ERR3:
			while (code--)
				*wp++ = *rp++;
			continue;
		}
	}

	return wp - (unsigned char *)in;
}

Json_str_t *Json_str_value(Json_val_t *val)
{
	Json_str_t *str = &val->v.string;
	if (val->var_type != JT_STRING)
		return NULL;

	if (str->is_escapes) {
		size_t len = Json_unescape((char *)str->str, str->len);
		assert(len < str->len);
		str->len = len, str->is_escapes = 0;
	}

	return str;
}

static const uint64_t power10int[] = {
	1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
	10000000000ULL, 100000000000ULL, 1000000000000ULL, 10000000000000ULL,
	100000000000000ULL, 1000000000000000ULL, 10000000000000000ULL,
	100000000000000000ULL, 1000000000000000000ULL, 10000000000000000000ULL,
};

static const double power10float[] = { // 1e+0...1e308: 617 * 8 bytes = 4936 bytes
	1e+0, 1e+1, 1e+2, 1e+3, 1e+4, 1e+5, 1e+6, 1e+7, 1e+8, 1e+9, 1e+10,
	1e+11, 1e+12, 1e+13, 1e+14, 1e+15, 1e+16, 1e+17, 1e+18, 1e+19, 1e+20,
	1e+21, 1e+22, 1e+23, 1e+24, 1e+25, 1e+26, 1e+27, 1e+28, 1e+29, 1e+30,
	1e+31, 1e+32, 1e+33, 1e+34, 1e+35, 1e+36, 1e+37, 1e+38, 1e+39, 1e+40,
	1e+41, 1e+42, 1e+43, 1e+44, 1e+45, 1e+46, 1e+47, 1e+48, 1e+49, 1e+50,
	1e+51, 1e+52, 1e+53, 1e+54, 1e+55, 1e+56, 1e+57, 1e+58, 1e+59, 1e+60,
	1e+61, 1e+62, 1e+63, 1e+64, 1e+65, 1e+66, 1e+67, 1e+68, 1e+69, 1e+70,
	1e+71, 1e+72, 1e+73, 1e+74, 1e+75, 1e+76, 1e+77, 1e+78, 1e+79, 1e+80,
	1e+81, 1e+82, 1e+83, 1e+84, 1e+85, 1e+86, 1e+87, 1e+88, 1e+89, 1e+90,
	1e+91, 1e+92, 1e+93, 1e+94, 1e+95, 1e+96, 1e+97, 1e+98, 1e+99,1e+100,
	1e+101,1e+102,1e+103,1e+104,1e+105,1e+106,1e+107,1e+108,1e+109,1e+110,
	1e+111,1e+112,1e+113,1e+114,1e+115,1e+116,1e+117,1e+118,1e+119,1e+120,
	1e+121,1e+122,1e+123,1e+124,1e+125,1e+126,1e+127,1e+128,1e+129,1e+130,
	1e+131,1e+132,1e+133,1e+134,1e+135,1e+136,1e+137,1e+138,1e+139,1e+140,
	1e+141,1e+142,1e+143,1e+144,1e+145,1e+146,1e+147,1e+148,1e+149,1e+150,
	1e+151,1e+152,1e+153,1e+154,1e+155,1e+156,1e+157,1e+158,1e+159,1e+160,
	1e+161,1e+162,1e+163,1e+164,1e+165,1e+166,1e+167,1e+168,1e+169,1e+170,
	1e+171,1e+172,1e+173,1e+174,1e+175,1e+176,1e+177,1e+178,1e+179,1e+180,
	1e+181,1e+182,1e+183,1e+184,1e+185,1e+186,1e+187,1e+188,1e+189,1e+190,
	1e+191,1e+192,1e+193,1e+194,1e+195,1e+196,1e+197,1e+198,1e+199,1e+200,
	1e+201,1e+202,1e+203,1e+204,1e+205,1e+206,1e+207,1e+208,1e+209,1e+210,
	1e+211,1e+212,1e+213,1e+214,1e+215,1e+216,1e+217,1e+218,1e+219,1e+220,
	1e+221,1e+222,1e+223,1e+224,1e+225,1e+226,1e+227,1e+228,1e+229,1e+230,
	1e+231,1e+232,1e+233,1e+234,1e+235,1e+236,1e+237,1e+238,1e+239,1e+240,
	1e+241,1e+242,1e+243,1e+244,1e+245,1e+246,1e+247,1e+248,1e+249,1e+250,
	1e+251,1e+252,1e+253,1e+254,1e+255,1e+256,1e+257,1e+258,1e+259,1e+260,
	1e+261,1e+262,1e+263,1e+264,1e+265,1e+266,1e+267,1e+268,1e+269,1e+270,
	1e+271,1e+272,1e+273,1e+274,1e+275,1e+276,1e+277,1e+278,1e+279,1e+280,
	1e+281,1e+282,1e+283,1e+284,1e+285,1e+286,1e+287,1e+288,1e+289,1e+290,
	1e+291,1e+292,1e+293,1e+294,1e+295,1e+296,1e+297,1e+298,1e+299,1e+300,
	1e+301,1e+302,1e+303,1e+304,1e+305,1e+306,1e+307,1e+308
};

static inline uint64_t Json_atoi(const char *n, int size)
{
	uint64_t ret = 0;
	switch (size) {
	default:
	case 19: ret = ret * 10 + *n++ - '0';
	case 18: ret = ret * 10 + *n++ - '0';
	case 17: ret = ret * 10 + *n++ - '0';
	case 16: ret = ret * 10 + *n++ - '0';
	case 15: ret = ret * 10 + *n++ - '0';
	case 14: ret = ret * 10 + *n++ - '0';
	case 13: ret = ret * 10 + *n++ - '0';
	case 12: ret = ret * 10 + *n++ - '0';
	case 11: ret = ret * 10 + *n++ - '0';
	case 10: ret = ret * 10 + *n++ - '0';
	case  9: ret = ret * 10 + *n++ - '0';
	case  8: ret = ret * 10 + *n++ - '0';
	case  7: ret = ret * 10 + *n++ - '0';
	case  6: ret = ret * 10 + *n++ - '0';
	case  5: ret = ret * 10 + *n++ - '0';
	case  4: ret = ret * 10 + *n++ - '0';
	case  3: ret = ret * 10 + *n++ - '0';
	case  2: ret = ret * 10 + *n++ - '0';
	case  1: ret = ret * 10 + *n++ - '0';
	}
	return ret;
}

static inline void Json_num_covert(Json_val_t *val)
{
	double ret = 0;
	int flag = 0;
	const char *num, *frace, *exp;
	int nlen, flen, elen;

	if (val->var_type != JT_NUM_RAW)
		return;

	num = val->v.numraw.number;
	nlen = val->v.numraw.int_len;

	frace = num + nlen + 1;
	flen = val->v.numraw.frace_len;

	exp = num + nlen + flen + !!flen + 1;
	elen = val->v.numraw.exp_len;

	if (nlen > 0 && (*num == '-' || *num == '+')) {
		flag = !!(*num == '-');
		num++, nlen--;
	}

	if (nlen > 19) {
		do {
			ret = ret * 1000000000000000000LL + Json_atoi(num, nlen);
			num += 19, nlen -= 19;
		} while (nlen >= 19);

		if (nlen > 0)
			ret = ret * power10int[nlen] + Json_atoi(num, nlen);
	} else {
		uint64_t rl = Json_atoi(num, nlen);
		if (flen == 0 && elen == 0 && (rl <= 9223372036854775807ULL + flag)) {
			val->v.integer = flag ? -rl : rl;
			val->var_type = JT_INT;
			return;
		}
		ret = rl;
	}

	if (flen > 0)
		ret += (double)Json_atoi(frace, flen) / power10int[flen > 19 ? 19 : flen];

	if (elen > 0) {
		unsigned flag2 = 0, power = 0;
		double expv = 0;
		if (*exp == '-' || *exp == '+') {
			flag2 = (*exp == '-');
			exp++, elen--;
		}
		power = Json_atoi(exp, elen);
		expv = power10float[power > 308 ? 308 : power];
		ret *= flag2 ? expv : 1.0f/expv;
	}

	val->v.real = flag ? -ret : ret;
	val->var_type = JT_REAL;
}

static void print_prefix(int level)
{
	while (level--)
		printf("\t");
}

static void _Json_print(Json_val_t *root, int level)
{
	size_t i = 0;
	print_prefix(level);
	switch (root->var_type) {
	case JT_NULL:
		printf("NULL");
		break;
	case JT_TRUE:
		printf("TRUE");
		break;
	case JT_FALSE:
		printf("FALSE");
		break;
	case JT_STRING:
		printf("\"%.*s\"", root->v.string.len, root->v.string.str);
		break;
	case JT_NUM_RAW:
		i = root->v.numraw.int_len + root->v.numraw.exp_len + !!root->v.numraw.exp_len
			+ root->v.numraw.frace_len + !!root->v.numraw.frace_len;
		printf("%.*s", (int)i, root->v.numraw.number);
		break;
	case JT_ARRAY:
		printf("[\n");
		for (i = 0; i < root->v.array.len; i++) {
			_Json_print(root->v.array.arr + i, level + 1);
			printf(",\n");
		}
		print_prefix(level);
		printf("]");
		break;
	case JT_OBJECT:
		printf("{\n");
		for (i = 0; i < root->v.object.len; i++) {
			Json_pair_t *pair = root->v.object.objects + i;
			print_prefix(level + 1);
			printf("\"%.*s\" : ", pair->name.v.string.len, pair->name.v.string.str);
			if (pair->value.var_type != JT_ARRAY && pair->value.var_type != JT_OBJECT) {
				_Json_print(&pair->value, 0);
			} else {
				printf("\n");
				_Json_print(&pair->value, level + 1);
			}
			printf(",\n");
		}
		print_prefix(level);
		printf("}");
		break;
	}
}

void Json_print(Json_val_t *root)
{
	_Json_print(root, 0);
	printf("\n");
}

#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <sys/time.h>
int main(int argc, char *argv[])
{
	int fd, oldsize;
	char *old = NULL;
	Json_t *json;
	Json_decode_ctx *ctx = Json_decode_create();
	struct timeval tv[2];

	if((fd = open(argv[1],O_RDONLY,0)) < 0 ||
           (oldsize=lseek(fd,0,SEEK_END)) == -1 ||
           (old = calloc(1, oldsize+1)) == NULL ||
	   lseek(fd,0,SEEK_SET) != 0 ||
	   read(fd,old,oldsize) != oldsize ||
	   close(fd) == -1)
	{
		err(1,"%s", argv[1]);
	}

	#if 0
	gettimeofday(tv, NULL);
	for (fd = 0; fd < 100000; fd++) {
		int p = strlen(old);
		assert(p == oldsize);
		old[p] = 0;
	}
	gettimeofday(tv + 1, NULL);
	// Json_print(&json->root);
	printf("time = %ld\n", (long)(tv[1].tv_sec * 1000000 + tv[1].tv_usec - tv[0].tv_sec * 1000000 - tv[0].tv_usec));
	#endif

	gettimeofday(tv, NULL);

	for (fd = 0; fd < 100000; fd++) {
		char bu[oldsize + 1];
		memcpy(bu, old, oldsize);
		bu[oldsize] = 0;
		json = Json_parse(ctx, bu);
		assert(json);
		Json_destroy(json);
	}
	gettimeofday(tv + 1, NULL);
	// Json_print(&json->root);
	printf("time = %ld\n", (long)(tv[1].tv_sec * 1000000 + tv[1].tv_usec - tv[0].tv_sec * 1000000 - tv[0].tv_usec));

	free(old);

	Json_decode_destroy(ctx);

	return 0;
}
