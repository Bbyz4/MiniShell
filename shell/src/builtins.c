#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <dirent.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>

#include "builtins.h"

int lexit(char*[]);
int lecho(char*[]);
int lcd(char*[]);
int lkill(char*[]);
int lls(char*[]);
int undefined(char *[]);

builtin_pair builtins_table[]={
	{"exit", &lexit},
	{"lecho", &lecho},
	{"lcd",	&lcd},
	{"cd", &lcd},
	{"lkill", &lkill},
	{"lls",	&lls},
	{NULL,NULL}
};

static int getArgc(char* argv[])
{
	int counter = 0;
	while(argv[counter]!=NULL)
	{
		counter++;
	}

	return counter;
}

int lexit(char* argv[])
{
	exit(0);
}

int lecho(char* argv[])
{
	int i = 1;
	if(argv[i])
	{
		printf("%s", argv[i++]);
	}
	while(argv[i])
	{
		printf(" %s", argv[i++]);
	}
	printf("\n");
	fflush(stdout);
	return 0;
}

int lcd(char* argv[])
{
	int argc = getArgc(argv);
	if(argc > 2)
	{
		return BUILTIN_ERROR;
	}

	if(argc==2)
	{
		if(chdir(argv[1]) != 0)
		{
			return BUILTIN_ERROR;
		}
	}
	else
	{
		char* home = getenv("HOME");
		if(home == NULL)
		{
			return BUILTIN_ERROR;
		}

		if(chdir(home) != 0)
		{
			return BUILTIN_ERROR;
		}
	}

	return 0;
}

static bool isANumber(char* str, int startIndex)
{
	if(!str || *str == '\0')
	{
		return false;
	}

	char* endptr;
	long value = strtol(str + startIndex, &endptr, 10);

	return (errno != ERANGE && *endptr == '\0' && value < INT_MAX && value > INT_MIN);
}

int lkill(char* argv[])
{
	int argc = getArgc(argv);
	if(argc < 2 || argc > 3)
	{
		return BUILTIN_ERROR;
	}

	pid_t pid;
	int signal = SIGTERM;

	if(!isANumber(argv[argc-1], 0))
	{
		return BUILTIN_ERROR;
	}

	pid = atoi(argv[argc-1]);

	if(argc==3)
	{
		if(argv[1][0]!='-')
		{
			return BUILTIN_ERROR;
		}

		if(!isANumber(argv[1], 1))
		{
			return BUILTIN_ERROR;
		}

		signal = atoi(argv[1] + 1);
	}

	if(kill(pid, signal) != 0)
	{
		return BUILTIN_ERROR;
	}

	return 0;
}

int lls(char* argv[])
{
	int argc = getArgc(argv);
	if(argc != 1)
	{
		return BUILTIN_ERROR;
	}

	DIR* d;
	struct dirent* dir;

	d = opendir(".");
	if(d == NULL)
	{
		return BUILTIN_ERROR;
	}

	dir = readdir(d);

	while(dir != NULL)
	{
		if(dir->d_name[0] != '.')
		{
			dprintf(STDOUT_FILENO ,"%s\n", dir->d_name);
		}
		dir = readdir(d);
	}

	closedir(d);
	return 0;
}

int undefined(char* argv[])
{
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	return BUILTIN_ERROR;
}
