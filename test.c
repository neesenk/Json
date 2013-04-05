#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "Json.h"

static void print_prefix(int level)
{
	while (level--)
		printf("\t");
}

static void _Json_print(Json_val_t *root, int level)
{
	size_t i = 0;
	print_prefix(level);
	switch (root->val_type) {
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
	case JT_INT:
		printf("%lld", (long long)root->v.integer);
		break;
	case JT_REAL:
		printf("%lg", root->v.real);
		break;
	case JT_NUM_RAW:
		i = root->v.numraw.nlen + root->v.numraw.elen + !!root->v.numraw.elen
			+ root->v.numraw.flen + !!root->v.numraw.flen;
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
			if (pair->value.val_type != JT_ARRAY && pair->value.val_type != JT_OBJECT) {
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

static void _encode(Json_val_t *root, const char *name, size_t len, Json_encode_ctx *enc)
{
	size_t i = 0;
	switch (root->val_type) {
	case JT_NULL:
		assert(Json_encode_append_null(enc, name, len)); break;
	case JT_TRUE:
		assert(Json_encode_append_bool(enc, 1, name, len)); break;
	case JT_FALSE:
		assert(Json_encode_append_bool(enc, 0, name, len)); break;
	case JT_STRING:
		assert(Json_encode_append_string(enc, root->v.string.str,
					  root->v.string.len, name, len)); break;
	case JT_INT:
		assert(Json_encode_append_integer(enc, root->v.integer, name, len)); break;
	case JT_REAL:
		assert(Json_encode_append_real(enc, root->v.real, name, len)); break;
	case JT_ARRAY:
		assert(Json_encode_begin_array(enc, name, len));
		for (i = 0; i < root->v.array.len; i++) {
			_encode(root->v.array.arr + i, NULL, 0, enc);
		}
		assert(Json_encode_end_array(enc));
		break;
	case JT_OBJECT:
		assert(Json_encode_begin_object(enc, name, len));
		for (i = 0; i < root->v.object.len; i++) {
			Json_pair_t *pair = root->v.object.objects + i;
			_encode(&pair->value, pair->name.v.string.str,pair->name.v.string.len,enc);
		}
		assert(Json_encode_end_object(enc));
		break;
	}
}

void encode(Json_t *root, Json_encode_ctx *enc)
{
	_encode(&root->root, NULL, 0, enc);
}

void Json_print(Json_t *root)
{
	_Json_print(&root->root, 0);
	printf("\n");
}

int main(int argc, char *argv[])
{
	const char *buffer = NULL;
	size_t len;
	int fd, oldsize;
	char *old = NULL;
	Json_t *json;
	Json_decode_ctx *ctx = Json_decode_ctx_create(0);
	Json_encode_ctx *enc = Json_encode_ctx_create(256, 10240, JSON_ENCODE_OPT_RAW_FIELDNAME);
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

	{
		char bu[oldsize + 1];
		int i = 0;
		int n = 1000;
		memcpy(bu, old, oldsize);
		bu[oldsize] = 0;
		json = Json_parse(ctx, bu);
		assert(json);

		gettimeofday(tv, NULL);
		for (i = 0; i < n; i++) {
			encode(json, enc);
			assert(Json_encode_get_result(enc, &buffer, &len));
			Json_encode_ctx_clear(enc);
		}
		gettimeofday(tv + 1, NULL);
		printf("time = %ld\n", (long)(tv[1].tv_sec * 1000000 + tv[1].tv_usec - tv[0].tv_sec * 1000000 - tv[0].tv_usec));
		printf("buffer = %ld\n", (long)(n * len));
	}

	#if 0
	gettimeofday(tv, NULL);
		Json_print(&json->root);
		Json_destroy(json);
		break;
	}
	gettimeofday(tv + 1, NULL);
	// Json_print(&json->root);
	printf("time = %ld\n", (long)(tv[1].tv_sec * 1000000 + tv[1].tv_usec - tv[0].tv_sec * 1000000 - tv[0].tv_usec));
	#endif

	free(old);
	Json_decode_ctx_destroy(ctx);
// #endif
	return 0;
}
