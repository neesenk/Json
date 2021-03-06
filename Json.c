/**
 * Copyright (c) 2013, Zhiyong Liu <NeeseNK at gmail dot com>
 * All rights reserved.
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include "Json.h"

static int parse_any(Json_decode_ctx *ctx, Json_val_t *val);
static void _Json_destroy(Json_val_t *root);

typedef struct _darray {
	size_t		max;
	size_t		len;
	Json_val_t	*arr;
} darray_t;

struct _Json_decode_ctx {
	unsigned	options;
	uint8_t		last_type;
	const char	*raw;
	const char	*pos;
	darray_t	darr[1];
};

struct fmt_stack { int type, nums; };

struct _Json_encode_ctx {
	char		*fmt_buffer;
	size_t		buffer_size;
	size_t		buffer_len;

	unsigned	option;
	int		max_depth;
	int		curr_depth;

	struct fmt_stack fmt_stack[0];
};

static int darray_append(darray_t *da, Json_val_t *val)
{
	if (da->max == da->len) {
		size_t n = da->max * 2 < 64 ? 64 : da->max * 2;
		Json_val_t *narr = realloc(da->arr, sizeof(Json_val_t) * n);
		if (narr == NULL)
			return false;
		da->arr = narr, da->max = n;
	}

	da->arr[da->len++] = *val;
	return true;
}

static inline const char *_skip_digits(const char *pos)
{
	while (isdigit(*pos))
		pos++;
	return pos;
}

static const uint64_t power10int[] = {
	1,10,100,1000,10000,100000,1000000,10000000,100000000,1000000000,
	10000000000ULL,100000000000ULL,1000000000000ULL,10000000000000ULL,
	100000000000000ULL,1000000000000000ULL,10000000000000000ULL,
	100000000000000000ULL,1000000000000000000ULL,10000000000000000000ULL,
};

static const double power10float[] = { // 1e+0...1e308: 309 * 8 bytes = 2472 bytes
	1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,1e10,1e11,1e12,1e13,1e14,1e15,
	1e16,1e17,1e18,1e19,1e20,1e21,1e22,1e23,1e24,1e25,1e26,1e27,1e28,1e29,
	1e30,1e31,1e32,1e33,1e34,1e35,1e36,1e37,1e38,1e39,1e40,1e41,1e42,1e43,
	1e44,1e45,1e46,1e47,1e48,1e49,1e50,1e51,1e52,1e53,1e54,1e55,1e56,1e57,
	1e58,1e59,1e60,1e61,1e62,1e63,1e64,1e65,1e66,1e67,1e68,1e69,1e70,1e71,
	1e72,1e73,1e74,1e75,1e76,1e77,1e78,1e79,1e80,1e81,1e82,1e83,1e84,1e85,
	1e86,1e87,1e88,1e89,1e90,1e91,1e92,1e93,1e94,1e95,1e96,1e97,1e98,1e99,
	1e100,1e101,1e102,1e103,1e104,1e105,1e106,1e107,1e108,1e109,1e110,1e111,
	1e112,1e113,1e114,1e115,1e116,1e117,1e118,1e119,1e120,1e121,1e122,1e123,
	1e124,1e125,1e126,1e127,1e128,1e129,1e130,1e131,1e132,1e133,1e134,1e135,
	1e136,1e137,1e138,1e139,1e140,1e141,1e142,1e143,1e144,1e145,1e146,1e147,
	1e148,1e149,1e150,1e151,1e152,1e153,1e154,1e155,1e156,1e157,1e158,1e159,
	1e160,1e161,1e162,1e163,1e164,1e165,1e166,1e167,1e168,1e169,1e170,1e171,
	1e172,1e173,1e174,1e175,1e176,1e177,1e178,1e179,1e180,1e181,1e182,1e183,
	1e184,1e185,1e186,1e187,1e188,1e189,1e190,1e191,1e192,1e193,1e194,1e195,
	1e196,1e197,1e198,1e199,1e200,1e201,1e202,1e203,1e204,1e205,1e206,1e207,
	1e208,1e209,1e210,1e211,1e212,1e213,1e214,1e215,1e216,1e217,1e218,1e219,
	1e220,1e221,1e222,1e223,1e224,1e225,1e226,1e227,1e228,1e229,1e230,1e231,
	1e232,1e233,1e234,1e235,1e236,1e237,1e238,1e239,1e240,1e241,1e242,1e243,
	1e244,1e245,1e246,1e247,1e248,1e249,1e250,1e251,1e252,1e253,1e254,1e255,
	1e256,1e257,1e258,1e259,1e260,1e261,1e262,1e263,1e264,1e265,1e266,1e267,
	1e268,1e269,1e270,1e271,1e272,1e273,1e274,1e275,1e276,1e277,1e278,1e279,
	1e280,1e281,1e282,1e283,1e284,1e285,1e286,1e287,1e288,1e289,1e290,1e291,
	1e292,1e293,1e294,1e295,1e296,1e297,1e298,1e299,1e300,1e301,1e302,1e303,
	1e304,1e305,1e306,1e307,1e308
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

static inline void _num_convert(const char *num, int nlen, const char *frace,
				int flen, const char *exp, int elen, Json_val_t *val)
{
	double ret = 0;
	int flag = 0;

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
			val->val_type = JT_INT;
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
		ret *= !flag2 ? expv : 1.0f/expv;
	}

	val->v.real = flag ? -ret : ret;
	val->val_type = JT_REAL;
}

static inline void Json_num_convert(Json_val_t *val)
{
	const char *num = val->v.numraw.number;
	int nlen = val->v.numraw.nlen;

	const char *frace = num + nlen + 1;
	int flen = val->v.numraw.flen;

	const char *exp = num + nlen + flen + !!flen + 1;
	int elen = val->v.numraw.elen;

	_num_convert(num, nlen, frace, flen, exp, elen, val);
}

static int parse_number(Json_decode_ctx *ctx, Json_val_t *val)
{
	const char *num = NULL, *frace = NULL, *exp = NULL, *pos = NULL;
	size_t nlen = 0, flen = 0, elen = 0;

	ctx->last_type = JT_NUM_RAW;
	num = pos = _skip_digits(ctx->pos + 1);

	if (*ctx->pos == '-' && num == ctx->pos + 1)
		return false;

	if (*pos == '.') {
		frace = pos + 1;
		pos = _skip_digits(frace);
		flen = pos - frace - 1;
	}

	if (*pos == 'E' || *pos == 'e') {
		const char *tmp = pos + !!(pos[1] == '+' || pos[1] == '-');
		exp = pos + 1;
		if ((pos = _skip_digits(tmp + 1)) == tmp + 1)
			return false;
		elen = pos - tmp - !!(*tmp != '+' && *tmp != '-');
	}

	nlen = num - ctx->pos;
	if (ctx->options & JSON_DEOCDE_OPT_RAW) {
		val->val_type = JT_NUM_RAW;
		val->v.numraw = (Json_num_t){nlen, flen, elen, ctx->pos};
	} else {
		_num_convert(ctx->pos, nlen, frace, flen, exp, elen, val);
	}

	ctx->pos = pos;
	return true;
}

static inline int parse_string_raw(Json_decode_ctx *ctx, Json_val_t *val)
{
	const char *pos = ctx->pos + 1;
	int esc = 0, c;
	ctx->last_type = JT_STRING;

	for (;;) {
		for (c = *pos; c != '\0' && c != '"' && c != '\\'; c = *pos)
			pos++;
		if (c != '\\' || pos[1] == '\0')
			break;
		pos += 2, esc = JF_ESCAPES;
	}

	if (*pos != '"')
		return false;

	val->val_type = JT_STRING;
	val->val_flag = esc;
	val->v.string = (Json_str_t) { pos - ctx->pos - 1, ctx->pos + 1};
	ctx->pos = pos + 1;

	return true;
}

static inline unsigned hex2uint(unsigned char *in)
{
	unsigned v = 0;
	static const char hex[256] = {
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
		-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	};
	if (__builtin_expect(hex[in[0]] < 0, 0))
		return 0xffffffff;
	v += hex[in[0]] << 12;
	if (__builtin_expect(hex[in[1]] < 0, 0))
		return 0xffffffff;
	v += hex[in[1]] << 8;
	if (__builtin_expect(hex[in[2]] < 0, 0))
		return 0xffffffff;
	v += hex[in[2]] << 4;
	if (__builtin_expect(hex[in[3]] < 0, 0))
		return 0xffffffff;
	return v + hex[in[3]];
}

static inline unsigned char unescape(unsigned char c)
{
	switch (c) {
	case 'b': return '\b';
	case 'f': return '\f';
	case 'n': return '\n';
	case 'r': return '\r';
	case 't': return '\t';
	}
	return c;;
}

static inline const char *_parse_string(unsigned char *pos, Json_val_t *val)
{
	unsigned char *wp, *rp = pos;

	while (*rp != '"' && *rp != '\\' && *rp != '\0')
		rp++;

	wp = rp;
	for (;;) {
		if (*rp == '"' || *rp == '\0' || *(rp + 1) == '\0')
			break;

		if (rp[1] != 'u') {
			*wp++ = unescape(rp[1]), rp += 2;
		} else {
			unsigned code = hex2uint(rp + 2);
			if (code >= 0xD800 && code <= 0xDBFF) {
				unsigned u = hex2uint(rp + 8);
				if (rp[6] != '\\' || rp[7] != 'u' || u < 0xDC00 || u > 0xDFFF)
					return NULL;

				code = (((code - 0xD800) << 10) | (u - 0xDC00)) + 0x10000;

				*wp++ = 0xF0 | ((code >> 18) & 0xFF);
				*wp++ = 0x80 | ((code >> 12) & 0x3F);
				*wp++ = 0x80 | ((code >> 6) & 0x3F);
				*wp++ = 0x80 | (code & 0x3F);

				rp += 6;
			} else if (code <= 0x7F) {
				*wp++ = code;
			} else if (code <= 0x7FF) {
				*wp++ = 0xC0 | ((code >> 6) & 0xFF);
				*wp++ = 0x80 | ((code & 0x3F));
			} else if (code <= 0xFFFF) {
				*wp++ = 0xE0 | ((code >> 12) & 0xFF);
				*wp++ = 0x80 | ((code >> 6) & 0x3F);
				*wp++ = 0x80 | (code & 0x3F);
			} else {
				return NULL;
			}

			rp += 6;
		}
		while (*rp != '"' && *rp != '\\' && *rp != '\0')
			*wp++ = *rp++;
	}

	if (*rp != '"')
		return NULL;

	val->val_type = JT_STRING;
	val->val_flag = 0;
	val->v.string = (Json_str_t) {wp - pos, (const char *)pos};

	return (const char *)rp + 1;
}

static int parse_string(Json_decode_ctx *ctx, Json_val_t *val)
{
	const char *pos = NULL;
	ctx->last_type = JT_STRING;
	if (ctx->options & JSON_DEOCDE_OPT_RAW)
		return parse_string_raw(ctx, val);
	if ((pos = _parse_string((unsigned char *)ctx->pos + 1, val)) != NULL)
		ctx->pos = pos;
	return !!pos;
}

static inline int Json_unescape(Json_val_t *val)
{
	return !!_parse_string((unsigned char *)val->v.string.str, val);
}

static int parse_true(Json_decode_ctx *ctx, Json_val_t *val)
{
	val->val_type = ctx->last_type = JT_TRUE;
	if (ctx->pos[1] != 'r' || ctx->pos[2] != 'u' || ctx->pos[3] != 'e')
		return false;
	ctx->pos += 4;
	return true;
}

static int parse_false(Json_decode_ctx *ctx, Json_val_t *val)
{
	const char *pos = ctx->pos;
	val->val_type = ctx->last_type = JT_FALSE;
	if (pos[1] != 'a' || pos[2] != 'l' || pos[3] != 's' || pos[4] != 'e')
		return false;
	ctx->pos += 5;
	return true;
}

static int parse_null(Json_decode_ctx *ctx, Json_val_t *val)
{
	val->val_type = ctx->last_type = JT_NULL;
	if (ctx->pos[1] != 'u' || ctx->pos[2] != 'l' || ctx->pos[3] != 'l')
		return false;
	ctx->pos += 4;
	return true;
}

static inline int skip_comment(Json_decode_ctx *ctx)
{
	const char *pos = ctx->pos + 1;
	if (*pos == '*') {
		for (;;) {
			if ((pos = strchr(pos + 1, '*')) == NULL)
				return false;

			if (pos[1] == '/') {
				pos += 2;
				break;
			}
		}
	} else if (*pos == '/') {
		if ((pos = strchr(pos + 1,  '\n')) == NULL)
			pos = ctx->pos + strlen(ctx->pos);
	} else {
		return false;
	}

	ctx->pos = pos;
	return true;
}

static inline int skip_content(Json_decode_ctx *ctx)
{
	for (;;) {
		while (isspace(*ctx->pos))
			ctx->pos++;
		if (*ctx->pos != '/')
			break;
		if (!skip_comment(ctx))
			return false;
	}

	return true;
}

static void darray_clean(darray_t *da)
{
	size_t i = 0;
	for (i = 0; i < da->len; i++)
		_Json_destroy(da->arr + i);
	da->len = 0;
}

static int parse_array(Json_decode_ctx *ctx, Json_val_t *val)
{
	int i;
	Json_val_t obj, *arr;
	size_t off = ctx->darr->len;

	ctx->last_type = JT_ARRAY;
	ctx->pos++; // skip [
	for (;;) {
		if (!skip_content(ctx))
			return false;
		if (*ctx->pos == ']')
			break;
		if (!parse_any(ctx, &obj) || !darray_append(ctx->darr, &obj))
			return false;
		if (!skip_content(ctx))
			return false;
		if (*ctx->pos == ']')
			break;
		if (*ctx->pos != ',')
			return false;
		ctx->pos++;
	}
	ctx->pos++; // skip ]

	if (!(arr = calloc(ctx->darr->len - off, sizeof(*arr))))
		return false;

	for (i = off; i < ctx->darr->len; i++)
		arr[i - off] = ctx->darr->arr[i];

	val->val_type = JT_ARRAY;
	val->v.array.len = ctx->darr->len - off, val->v.array.arr = arr;
	ctx->darr->len = off;

	return true;
}

static int parse_object(Json_decode_ctx *ctx, Json_val_t *val)
{
	Json_pair_t *objs = NULL;
	Json_val_t tmp;
	int i = 0, j = 0;
	size_t off = ctx->darr->len, n;

	ctx->last_type = JT_OBJECT;
	ctx->pos++; // skip {
	for (;;) {
		if (!skip_content(ctx))
			return false;
		if (*ctx->pos == '}')
			break;
		if (*ctx->pos != '"')
			return false;
		if (!parse_string(ctx, &tmp) || !darray_append(ctx->darr, &tmp))
			return false;
		if (!skip_content(ctx))
			return false;
		if (*ctx->pos != ':')
			return false;
		ctx->pos++;
		if (!skip_content(ctx))
			return false;
		if (!parse_any(ctx, &tmp) || !darray_append(ctx->darr, &tmp))
			return false;
		if (!skip_content(ctx))
			return false;
		if (*ctx->pos == '}')
			break;
		if (*ctx->pos != ',')
			return false;
		ctx->pos++;
	}
	ctx->pos++; // skip }

	n = (ctx->darr->len - off) / 2;
	if (!(objs = calloc(n, sizeof(*objs))))
		return false;

	for (i = off, j = 0; i < ctx->darr->len; i += 2, j++) {
		objs[j].name  = ctx->darr->arr[i + 0];
		objs[j].value = ctx->darr->arr[i + 1];
	}

	val->val_type = JT_OBJECT;
	val->val_flag = 0;
	val->v.object = (Json_obj_t) {n, objs};
	ctx->darr->len = off;

	return true;
}

static int parse_any(Json_decode_ctx *ctx, Json_val_t *val)
{
	switch (*ctx->pos) {
	case '"': return parse_string(ctx, val);
	case '0':case '1':case '2':case '3':case '4':case '5':
	case '6':case '7':case '8':case '9':case '-':
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

	if (!skip_content(ctx))
		goto DONE;
	if (!parse_any(ctx, &val))
		goto DONE;
	if (!skip_content(ctx))
		goto DONE;
	if (*ctx->pos != '\0')
		goto DONE;
	if ((ret = calloc(1, sizeof(*ret))) != NULL)
		ret->root = val;
DONE:
	darray_clean(ctx->darr);
	return ret;
}

static void _Json_destroy(Json_val_t *root)
{
	size_t i = 0;
	switch (root->val_type) {
	case JT_STRING:
		if (root->val_flag & JF_ALLOC)
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

inline Json_val_t *Json_val_convert(Json_val_t *val)
{
	if (val->val_type == JT_NUM_RAW)
		Json_num_convert(val);
	if (val->val_type == JT_STRING && val->val_flag & JF_ESCAPES)
		Json_unescape(val);
	return val;
}

static inline int Json_strcmp(const char *str1, size_t len1, const char *str2, size_t len2)
{
	int ret = memcmp(str1, str2, len1 > len2 ? len2 : len1);
	return (!ret || len1 == len2) ? ret : (len1 > len2 ? 1 : -1);
}

static int Json_object_cmp(const void *o1, const void *o2)
{
	const Json_val_t *a = Json_val_convert(&((Json_pair_t *)o1)->name);
	const Json_val_t *b = Json_val_convert(&((Json_pair_t *)o2)->name);
	return Json_strcmp(a->v.string.str, a->v.string.len, b->v.string.str, b->v.string.len);
}

Json_val_t *Json_object_value(Json_val_t *object, const char *field)
{
	size_t i;
	Json_obj_t *obj	    = &object->v.object;
	Json_pair_t *fields = obj->objects, cmp;

	if (object->val_type != JT_OBJECT)
		return NULL;

	if (!(object->val_flag & JF_SORT) && obj->len >= OBJECT_FIELDS_SORT_NUM) {
		qsort(fields, obj->len, sizeof(*fields), Json_object_cmp);
		object->val_flag |= JF_SORT;
	}

	cmp.name.v.string = (Json_str_t ){strlen(field), field};

	if (object->val_flag & JF_SORT) {
		fields = bsearch(&cmp, fields, obj->len, sizeof(*fields), Json_object_cmp);
		return fields ? &fields->value : NULL;
	}

	for (i = 0; i < obj->len; i++) {
		if (!Json_object_cmp(fields + i, &cmp))
			return &fields[i].value;
	}

	return NULL;
}

Json_val_t *Json_array_index(Json_val_t *root, size_t pos)
{
	if (root->val_type != JT_ARRAY || pos >= root->v.array.len)
		return NULL;
	return Json_val_convert(root->v.array.arr + pos);
}

Json_val_t *Json_query(Json_val_t *root, const char *fmt, ...)
{
	va_list vp;

	va_start(vp, fmt);
	for (; *fmt; fmt++) {
		if (!root)
			return NULL;
		switch (*fmt) {
		case 'o':
			root = Json_object_value(root, va_arg(vp, const char *));
			break;
		case 'a':
			root = Json_array_index(root, va_arg(vp, int));
			break;
		default:
			return NULL;
		}

	}
	va_end(vp);

	return root ? Json_val_convert(root) : NULL;
}

Json_decode_ctx *Json_decode_ctx_create(unsigned options)
{
	Json_decode_ctx *ctx = calloc(1, sizeof(Json_decode_ctx));
	if (ctx)
		ctx->options = options;
	return ctx;
}

void Json_decode_ctx_destroy(Json_decode_ctx *ctx)
{
	if (ctx) {
		free(ctx->darr->arr);
		free(ctx);
	}
}

Json_encode_ctx *Json_encode_ctx_create(size_t init_len, size_t max_depth, unsigned option)
{
	Json_encode_ctx *enc = calloc(1, sizeof(*enc) + max_depth * sizeof(struct fmt_stack));
	char *buffer = calloc(1, init_len);

	if (!enc || !buffer) {
		free(enc);
		free(buffer);
		return NULL;
	}

	enc->fmt_buffer		= buffer;
	enc->buffer_size	= init_len;
	enc->max_depth		= max_depth;
	enc->option		= option;
	enc->curr_depth		= -1;
	return enc;
}

void Json_encode_ctx_destroy(Json_encode_ctx *enc)
{
	if (enc) {
		free(enc->fmt_buffer);
		free(enc);
	}
}

void Json_encode_ctx_clear(Json_encode_ctx *enc)
{
	if (enc) {
		enc->buffer_len = 0;
		enc->curr_depth = -1;
	}
}

#define NUMMAXLEN (100)
#define JSON_VAL(n) ((union Json_val)(int64_t)n)
#define STRMAXLEN(len) (6 * len)
#define JSONVAL_MAX_LEN(t,v) ((t)==JT_STRING?STRMAXLEN((v).string.len):NUMMAXLEN+1)

static inline int Json_encode_buffer_reserve(Json_encode_ctx *enc, size_t len)
{
	if (enc->buffer_size < enc->buffer_len + len) {
		size_t size = enc->buffer_size + (enc->buffer_size > len ? enc->buffer_size : len);
		char *n = realloc(enc->fmt_buffer, size);
		if (n) {
			enc->fmt_buffer = n;
			enc->buffer_size = size;
		}

		return !!n;
	}

	return true;
}

static inline int Json_encode_buffer_append(Json_encode_ctx *enc, const char *str, int len)
{
	assert(enc->buffer_len + len <= enc->buffer_size);
	memcpy(enc->fmt_buffer + enc->buffer_len, str, len);
	enc->buffer_len += len;
	return true;
}

static inline void Json_encode_buffer_append_char(Json_encode_ctx *enc, char c)
{
	assert(enc->buffer_len < enc->buffer_size);
	enc->fmt_buffer[enc->buffer_len++] = c;
}

static inline int Json_encode_buffer_append_integer(Json_encode_ctx *enc, int64_t l)
{
	assert(enc->buffer_len + NUMMAXLEN <= enc->buffer_size);
	enc->buffer_len += snprintf(enc->fmt_buffer + enc->buffer_len, NUMMAXLEN, "%lld", (long long)l);
	return true;
}

static inline int Json_encode_buffer_append_real(Json_encode_ctx *enc, double r)
{
	assert(enc->buffer_len + NUMMAXLEN <= enc->buffer_size);
	enc->buffer_len += snprintf(enc->fmt_buffer + enc->buffer_len, NUMMAXLEN, "%g", r);
	return true;
}

static int Json_encode_buffer_append_string(Json_encode_ctx *enc, const char *str, size_t len)
{
	const unsigned char *io = (const unsigned char *)str;
	const unsigned char *end = io + len;
	char *out = enc->fmt_buffer + enc->buffer_len;

	static const char hex[] = "0123456789ABCDEF";
	static const char map[256] = {
		0,1,1,1,1,1,1,1,'b','t','n',1,'f','r',1,1,1,1,1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,0,0,'"',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0, '\\',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
		2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
		4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1
	};
	#define TOHEX(_v) do {						\
		unsigned v = (_v);					\
		*out++ = '\\',  *out++ = 'u';				\
		*out++ = hex[(v & 0xf000) >> 12];			\
		*out++ = hex[(v & 0x0f00) >> 8];			\
		*out++ = hex[(v & 0x00f0) >> 4];			\
		*out++ = hex[v & 0x000f];				\
	} while (0)

	assert(enc->buffer_len + STRMAXLEN(len) <= enc->buffer_size);
	*out++ = '"';
	while (io < end) {
		unsigned in, ucs;
		int len = map[*io];
		switch (len) {
		case 0:
			*out++ = *io++;
			break;
		case 1:
			ucs = *io++;
			TOHEX(ucs);
			break;
		case 2:
			if (io + 2 > end)
				return false;

			in = (unsigned)io[0] | ((unsigned)io[1] << 8);
			ucs = ((in & 0x1f) << 6) | ((in >> 8) & 0x3f);
			if (ucs < 0x80)
				return false;
			io += 2;
			TOHEX(ucs);
			break;
		case 3:
			if (io + 3 > end)
				return false;

			in = (unsigned)io[0] | ((unsigned)io[1] << 8)
				| ((unsigned)io[2] << 16);
			ucs = ((in & 0x0f) << 12) | ((in & 0x3f00) >> 2)
				| ((in & 0x3f0000) >> 16);
			if (ucs < 0x800)
				return false;
			io += 3;
			TOHEX(ucs);
			break;
		case 4:
			if (io + 4 > end)
				return false;
			in = (unsigned)io[0] | ((unsigned)io[1] << 8)
			     | ((unsigned)io[2] << 16) | ((unsigned)io[3] << 24);
			ucs = ((in & 0x07) << 18) | ((in & 0x3f00) << 4)
			     | ((in & 0x3f0000) >> 10) | ((in & 0x3f000000) >> 24);
			if (ucs < 0x10000)
				return false;
			io += 4;
			ucs -= 0x10000;
			TOHEX((ucs >> 10) + 0xd800);
			TOHEX((ucs & 0x3ff) + 0xdc00);
			break;
		case 5: case 6:
			return false;
		default:
			*out++ = '\\', *out++ = len;
			io++;
			break;
		}
	}

	*out++ = '"';
	enc->buffer_len = out - enc->fmt_buffer;

	return true;
}

static inline int Json_encode_buffer_append_value(Json_encode_ctx *enc, int type, union Json_val val)
{
	switch (type) {
	case JT_RAW:	return Json_encode_buffer_append(enc, val.string.str, val.string.len);
	case JT_NULL:	return Json_encode_buffer_append(enc, "null", 4);
	case JT_FALSE:  return Json_encode_buffer_append(enc, "false", 5);
	case JT_TRUE:	return Json_encode_buffer_append(enc, "true", 4);
	case JT_STRING: return Json_encode_buffer_append_string(enc, val.string.str, val.string.len);
	case JT_INT:	return Json_encode_buffer_append_integer(enc, val.integer);
	case JT_REAL:	return Json_encode_buffer_append_real(enc, val.real);
	}

	return false;
}

int Json_encode_append_string(Json_encode_ctx *enc, const char *str, size_t len,
			      const char *name, size_t nlen)
{
	Json_str_t v = (Json_str_t){len, str};
	return Json_encode_append(enc, name, nlen, JT_STRING, (union Json_val)v);
}

int Json_encode_append_real(Json_encode_ctx *enc, double real,
			    const char *name, size_t nlen)
{
	return Json_encode_append(enc, name, nlen, JT_REAL, (union Json_val)real);
}

int Json_encode_append_integer(Json_encode_ctx *enc, int64_t v,
			       const char *name, size_t nlen)
{
	return Json_encode_append(enc, name, nlen, JT_INT, (union Json_val)v);
}

int Json_encode_append_null(Json_encode_ctx *enc, const char *name, size_t nlen)
{
	return Json_encode_append(enc, name, nlen, JT_NULL, JSON_VAL(0));
}

int Json_encode_append_bool(Json_encode_ctx *enc, int b, const char *name, size_t nlen)
{
	if (b)
		return Json_encode_append(enc, name, nlen, JT_TRUE, JSON_VAL(1));
	else
		return Json_encode_append(enc, name, nlen, JT_FALSE, JSON_VAL(0));
}

int Json_encode_append(Json_encode_ctx *enc, const char *name, size_t nlen,
		       int type, union Json_val val)
{
	struct fmt_stack *s = NULL;
	size_t nowlen = enc->buffer_len;
	int maxlen = 0;

	s = enc->fmt_stack + enc->curr_depth;
	if (name == NULL) {
		if (enc->curr_depth >= 0 && s->type != JT_ARRAY)
			return false;
		if (enc->curr_depth < 0) {
			enc->fmt_stack[0].type = type;
			enc->fmt_stack[0].nums = -1;
			enc->curr_depth = 0;
			s = enc->fmt_stack;
		}
	} else {
		if (enc->curr_depth < 0 || s->type != JT_OBJECT)
			return false;
	}

	maxlen = JSONVAL_MAX_LEN(type, val) + STRMAXLEN(nlen) + 2;
	if (!Json_encode_buffer_reserve(enc, maxlen))
		return false;

	if (s->nums > 0)
		Json_encode_buffer_append_char(enc, ',');

	if (name) {
		if (enc->option & JSON_ENCODE_OPT_RAW_FIELDNAME) {
			enc->fmt_buffer[enc->buffer_len++] = '"';
			Json_encode_buffer_append(enc, name, nlen);
			enc->fmt_buffer[enc->buffer_len++] = '"';
		} else {
			if (!Json_encode_buffer_append_string(enc, name, nlen))
				goto ERROR;
		}
		Json_encode_buffer_append_char(enc, ':');
	}

	if (!Json_encode_buffer_append_value(enc, type, val))
		goto ERROR;
	s->nums++;
	return true;
ERROR:
	enc->buffer_len = nowlen;
	return false;
}

static inline int Json_encode_begin_complex(Json_encode_ctx *enc, int type,
					    const char *name, size_t len)
{
	Json_str_t v = {1, type == JT_ARRAY ? "[" : "{"};
	if (enc->curr_depth >= enc->max_depth)
		return false;
	if (!Json_encode_append(enc, name, len, JT_RAW, (union Json_val)v))
		return false;
	enc->curr_depth++;
	enc->fmt_stack[enc->curr_depth].type = type;
	enc->fmt_stack[enc->curr_depth].nums = 0;

	return true;
}

static int Json_encode_end_complex(Json_encode_ctx *enc, int type)
{
	if (enc->curr_depth < 0 || enc->fmt_stack[enc->curr_depth].type != type)
		return false;
	if (!Json_encode_buffer_reserve(enc, 1))
		return false;
	Json_encode_buffer_append_char(enc, type == JT_ARRAY ? ']' : '}');
	enc->curr_depth--;
	return true;
}

int Json_encode_begin_array(Json_encode_ctx *enc, const char *name, size_t len)
{
	return Json_encode_begin_complex(enc, JT_ARRAY, name, len);
}

int Json_encode_end_array(Json_encode_ctx *enc)
{
	return Json_encode_end_complex(enc, JT_ARRAY);
}

int Json_encode_begin_object(Json_encode_ctx *enc, const char *name, size_t len)
{
	return Json_encode_begin_complex(enc, JT_OBJECT, name, len);
}

int Json_encode_end_object(Json_encode_ctx *enc)
{
	return Json_encode_end_complex(enc, JT_OBJECT);
}

int Json_encode_get_result(Json_encode_ctx *enc, const char **result, size_t *len)
{
	struct fmt_stack *s = enc->fmt_stack + enc->curr_depth;
	if (enc->curr_depth > 0)
		return false;
	if (enc->curr_depth == 0 && (s->type == JT_ARRAY || s->type == JT_OBJECT))
		return false;

	*result = (const char *)enc->fmt_buffer;
	*len	= enc->buffer_len;
	return true;
}
