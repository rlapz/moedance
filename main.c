#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "moedance.h"
#include "config.h"


static const char *
_load_default_path(char buffer[], size_t size)
{
	const char *const env = getenv("HOME");
	if (env == NULL) {
		log_err(0, "main: _load_default_path: invalid \"$HOME\" env variable");
		exit(EXIT_FAILURE);
	}

	if (snprintf(buffer, size, "%s/Music", env) == 0) {
		log_err(errno, "main: _load_default_path: snprintf");
		exit(EXIT_FAILURE);
	}

	return buffer;
}


static void
_print_help(const char app_name[])
{
	printf("MoeDance - A pretty and simple music player\n"
	       "\nUsage: %s [PATH]\n", app_name);
}


int
main(int argc, char *argv[])
{
	int ret = EXIT_FAILURE;
	MoeDance m;
	const char *path;
	char buffer[4096];


	switch (argc) {
	case 1:
		path = _load_default_path(buffer, sizeof(buffer));
		break;
	case 2:
		if (strcmp(argv[1], "-h") == 0) {
			_print_help(argv[0]);
			return 0;
		}

		path = argv[1];
		break;
	default:
		_print_help(argv[0]);
		return ret;
	}

	moedance_init(&m, path);
	ret = moedance_run(&m);
	moedance_deinit(&m);
	return ret;
}

