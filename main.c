#include <errno.h>
#include <stdlib.h>

#include "moedance.h"
#include "config.h"


static const char *
_load_default_path(char buffer[], size_t size)
{
	const char *const env = getenv("HOME");
	if (env == NULL) {
		log_err(0, "_load_default_path: invalid \"$HOME\" env variable");
		exit(1);
	}

	if (snprintf(buffer, size, "%s/Music", env) == 0) {
		log_err(errno, "_load_default_path: snprintf");
		exit(1);
	}

	return buffer;
}


int
main(int argc, char *argv[])
{
	int ret = 1;
	MoeDance m;
	const char *path;
	char buffer[4096];


	switch (argc) {
	case 1:
		path = _load_default_path(buffer, sizeof(buffer));
		break;
	case 2:
		path = argv[1];
		break;
	default:
		printf("%s [DIR]\n", argv[0]);
		return ret;
	}

	moedance_init(&m, path);
	ret = moedance_run(&m);
	moedance_deinit(&m);
	return ret;
}

