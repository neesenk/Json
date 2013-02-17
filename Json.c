#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include "Json.h"

static int parse_any(Json_decode_ctx *ctx, Json_val_t *val);
static void _Json_destroy(Json_val_t *root);

typedef struct _darray {
	struct _darray *next;
	size_t		max;
	size_t		len;
	Json_val_t	arr[0];
} darray_t;

struct _Json_decode_ctx {
	uint8_t		is_raw;
	uint8_t		last_type;
	const char	*raw;
	const char	*pos;
	darray_t	*darray_list;
};

static darray_t *darray_create(size_t size)
{
	darray_t *n = calloc(1, sizeof(darray_t) + sizeof(Json_val_t) * size);
	if (n)
		n->max = size, n->len = 0, n->next = NULL;
	return n;
}

static int darray_append(darray_t **pda, Json_val_t *val)
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

static darray_t *darray_pop(Json_decode_ctx *ctx)
{
	if (ctx->darray_list) {
		darray_t *ret  = ctx->darray_list;
		ctx->darray_list = ret->next;
		return ret;
	}

	return darray_create(64);
}

static void darray_push(Json_decode_ctx *ctx, darray_t *da)
{
	da->len = 0;
	da->next = ctx->darray_list;
	ctx->darray_list = da;
}

#ifdef __SSE4_2__ // 支持sse4.2指令
#include <nmmintrin.h>
static inline const char *_skip_charset(const char *p, const char *chs)
{
	__m128i w = _mm_loadu_si128((const __m128i *)chs);
	for (;;) {
		__m128i s = _mm_loadu_si128((const __m128i *)p);
		unsigned r = _mm_cvtsi128_si32(_mm_cmpistrm(w, s,
				_SIDD_UBYTE_OPS|_SIDD_CMP_EQUAL_ANY|_SIDD_BIT_MASK|_SIDD_NEGATIVE_POLARITY));
		if (r != 0) // some of characters may be non-whitespace
			return p + __builtin_ffs(r) - 1;
		p += 16;
	}
}

static inline const char *_skip_blank(const char *p)
{
	static const char whitespace[16] = " \n\r\t";
	return _skip_charset(p, whitespace);
}

static inline const char *_skip_digits(const char* p)
{
	static const char digits[16] = "0123456789";
	return _skip_charset(p, digits);
}
#else
#include <ctype.h>
static inline const char *_skip_blank(const char *pos)
{
	while (isspace(*pos))
		pos++;
	return pos;
}

static inline const char *_skip_digits(const char *pos)
{
	while (isdigit(*pos))
		pos++;
	return pos;
}
#endif

static const uint64_t power10int[] = {
	1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
	10000000000ULL, 100000000000ULL, 1000000000000ULL, 10000000000000ULL,
	100000000000000ULL, 1000000000000000ULL, 10000000000000000ULL,
	100000000000000000ULL, 1000000000000000000ULL, 10000000000000000000ULL,
};

static const double power10float[] = { // 1e+0...1e308: 309 * 8 bytes = 2472 bytes
	1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,
	1e20,1e21,1e22,1e23,1e24,1e25,1e26,1e27,1e28,1e29,1e30,1e31,1e32,1e33,1e34,1e35,1e36,1e37,
	1e38,1e39,1e40,1e41,1e42,1e43,1e44,1e45,1e46,1e47,1e48,1e49,1e50,1e51,1e52,1e53,1e54,1e55,
	1e56,1e57,1e58,1e59,1e60,1e61,1e62,1e63,1e64,1e65,1e66,1e67,1e68,1e69,1e70,1e71,1e72,1e73,
	1e74,1e75,1e76,1e77,1e78,1e79,1e80,1e81,1e82,1e83,1e84,1e85,1e86,1e87,1e88,1e89,1e90,1e91,
	1e92,1e93,1e94,1e95,1e96,1e97,1e98,1e99,1e100,1e101,1e102,1e103,1e104,1e105,1e106,1e107,1e108,
	1e109,1e110,1e111,1e112,1e113,1e114,1e115,1e116,1e117,1e118,1e119,1e120,1e121,1e122,1e123,
	1e124,1e125,1e126,1e127,1e128,1e129,1e130,1e131,1e132,1e133,1e134,1e135,1e136,1e137,1e138,
	1e139,1e140,1e141,1e142,1e143,1e144,1e145,1e146,1e147,1e148,1e149,1e150,1e151,1e152,1e153,
	1e154,1e155,1e156,1e157,1e158,1e159,1e160,1e161,1e162,1e163,1e164,1e165,1e166,1e167,1e168,
	1e169,1e170,1e171,1e172,1e173,1e174,1e175,1e176,1e177,1e178,1e179,1e180,1e181,1e182,1e183,
	1e184,1e185,1e186,1e187,1e188,1e189,1e190,1e191,1e192,1e193,1e194,1e195,1e196,1e197,1e198,
	1e199,1e200,1e201,1e202,1e203,1e204,1e205,1e206,1e207,1e208,1e209,1e210,1e211,1e212,1e213,
	1e214,1e215,1e216,1e217,1e218,1e219,1e220,1e221,1e222,1e223,1e224,1e225,1e226,1e227,1e228,
	1e229,1e230,1e231,1e232,1e233,1e234,1e235,1e236,1e237,1e238,1e239,1e240,1e241,1e242,1e243,
	1e244,1e245,1e246,1e247,1e248,1e249,1e250,1e251,1e252,1e253,1e254,1e255,1e256,1e257,1e258,
	1e259,1e260,1e261,1e262,1e263,1e264,1e265,1e266,1e267,1e268,1e269,1e270,1e271,1e272,1e273,
	1e274,1e275,1e276,1e277,1e278,1e279,1e280,1e281,1e282,1e283,1e284,1e285,1e286,1e287,1e288,
	1e289,1e290,1e291,1e292,1e293,1e294,1e295,1e296,1e297,1e298,1e299,1e300,1e301,1e302,1e303,
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

static inline void _Json_num_convert(const char *num, int nlen, const char *frace, int flen,
				     const char *exp, int elen, Json_val_t *val)
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
			flag2 = (*exp != '-');
			exp++, elen--;
		}
		power = Json_atoi(exp, elen);
		expv = power10float[power > 308 ? 308 : power];
		ret *= flag2 ? expv : 1.0f/expv;
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

	_Json_num_convert(num, nlen, frace, flen, exp, elen, val);
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
	if (ctx->is_raw) {
		val->val_type = JT_NUM_RAW;
		val->v.numraw = (Json_num_t){nlen, flen, elen, ctx->pos};
	} else {
		_Json_num_convert(ctx->pos, nlen, frace, flen, exp, elen, val);
	}

	ctx->pos = pos;
	return true;
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
		pos += 2, esc = JF_IS_ESCAPES;
	}

	if (*pos != '"')
		return false;

	val->val_type = JT_STRING;
	val->val_flag = esc;
	val->v.string = (Json_str_t) { pos - ctx->pos - 1, ctx->pos + 1};
	ctx->pos = pos + 1;

	return true;
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
			if ((code = hex2unicode(rp + 2)) == 0xffffffff)
				return NULL;
			if (code >= 0xD800 && code <= 0xDBFF) { // Handle UTF-16 surrogate pair
				unsigned code2;
				if (rp[6] != '\\' || rp[7] != 'u')
					return NULL;
				if ((code2 = hex2unicode(rp + 8)) == 0xffffffff)
					return NULL;
				if (code2 < 0xDC00 || code2 > 0xDFFF)
					return NULL;
				code = (((code - 0xD800) << 10) | (code2 - 0xDC00)) + 0x10000;
				if ((code = utf8_encode(wp, code)) == 0)
					return NULL;
				rp += 12, wp += code;
			} else {
				if ((code = utf8_encode(wp, code)) == 0)
					return NULL;
				rp += 6, wp += code;
			}
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
	if (ctx->is_raw)
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
	val->val_type = ctx->last_type = JT_FALSE;
	if (ctx->pos[1] != 'a' || ctx->pos[2] != 'l' || ctx->pos[3] != 's' || ctx->pos[4] != 'e')
		return false;
	ctx->pos += 5;
	return true;
}

static int parse_null(Json_decode_ctx * ctx, Json_val_t *val)
{
	val->val_type = ctx->last_type = JT_NULL;
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

static void darray_clean(darray_t *da)
{
	size_t i = 0;
	for (i = 0; i < da->len; i++)
		_Json_destroy(da->arr + i);
	da->len = 0;
}

static int parse_array(Json_decode_ctx *ctx, Json_val_t *val)
{
	int ret = false, i;
	Json_val_t obj, *arr;
	darray_t *da = darray_pop(ctx);

	if (da == NULL)
		return false;

	ctx->last_type = JT_ARRAY;
	ctx->pos++; // skip [
	for (;;) {
		skip_content(ctx);
		if (*ctx->pos == ']')
			break;
		if (!parse_any(ctx, &obj) || !darray_append(&da, &obj))
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

	val->val_type = JT_ARRAY;
	val->v.array.len = da->len, val->v.array.arr = arr;
	ret = true;
DONE:
	if (!ret)
		darray_clean(da);
	darray_push(ctx, da);
	return ret;
}

static int parse_object(Json_decode_ctx *ctx, Json_val_t *val)
{
	Json_pair_t *objs = NULL;
	Json_val_t tmp;
	int ret = false, i = 0, j = 0;
	darray_t *da = darray_pop(ctx);
	if (da == NULL)
		return false;

	ctx->last_type = JT_OBJECT;
	ctx->pos++; // skip {
	for (;;) {
		skip_content(ctx);
		if (*ctx->pos == '}')
			break;
		if (*ctx->pos != '"' || !parse_string(ctx, &tmp) || !darray_append(&da, &tmp))
			goto DONE;

		skip_content(ctx);
		if (*ctx->pos != ':')
			goto DONE;
		ctx->pos++;

		skip_content(ctx);
		if (!parse_any(ctx, &tmp) || !darray_append(&da, &tmp))
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

	val->val_type = JT_OBJECT;
	val->val_flag = 0;
	val->v.object = (Json_obj_t) {da->len/2, objs};
	ret = true;
DONE:
	if (!ret)
		darray_clean(da);
	darray_push(ctx, da);
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
	switch (root->val_type) {
	case JT_STRING:
		if (root->val_flag & JF_IS_ALLOC)
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
	if (val->val_type == JT_STRING && val->val_flag & JF_IS_ESCAPES)
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

	if (!(object->val_flag & JF_IS_SORT) && obj->len >= OBJECT_FIELDS_SORT_NUM) {
		qsort(fields, obj->len, sizeof(*fields), Json_object_cmp);
		object->val_flag |= JF_IS_SORT;
	}

	cmp.name.v.string = (Json_str_t ){strlen(field), field};

	if (object->val_flag & JF_IS_SORT) {
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

	return Json_val_convert(root);
}

Json_decode_ctx *Json_decode_create(int is_raw)
{
	Json_decode_ctx *ctx = calloc(1, sizeof(Json_decode_ctx));
	if (ctx)
		ctx->is_raw = !!is_raw;
	return ctx;
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
