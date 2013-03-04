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

void Json_print(Json_val_t *root)
{
	_Json_print(root, 0);
	printf("\n");
}

int main(int argc, char *argv[])
{
	int fd, oldsize;
	char *old = NULL;
	Json_t *json;
	Json_decode_ctx *ctx = Json_decode_ctx_create(1);
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
		Json_print(&json->root);
		Json_destroy(json);
		break;
	}
	gettimeofday(tv + 1, NULL);
	// Json_print(&json->root);
	printf("time = %ld\n", (long)(tv[1].tv_sec * 1000000 + tv[1].tv_usec - tv[0].tv_sec * 1000000 - tv[0].tv_usec));

	free(old);
	Json_decode_ctx_destroy(ctx);

	return 0;
}
