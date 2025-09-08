//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//Includes

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//A structure for keeping track of background processes

struct BGProcessManager
{
	int backgroundProcesses[MAX_BACKGROUND_PROCESSES];
	int backgroundProcessesSize;

	struct
	{
		int _pid;
		int _status;
	}
	finishedBackgroundProcesses[MAX_BACKGROUND_PROCESSES];
	int finishedBackgroundProcessesSize;
};

void InitBGProcessManager(struct BGProcessManager* manager)
{
	manager->backgroundProcessesSize = 0;
	manager->finishedBackgroundProcessesSize = 0;
}

bool AddBGProcess(struct BGProcessManager* manager, int pid)
{
	if(manager->backgroundProcessesSize < MAX_BACKGROUND_PROCESSES)
	{
		manager->backgroundProcesses[manager->backgroundProcessesSize] = pid;
		manager->backgroundProcessesSize++;

		return true;
	}
	else
	{
		return false;
	}
}

bool AddFinishedBGProcess(struct BGProcessManager* manager, int pid, int status)
{
	if(manager->finishedBackgroundProcessesSize < MAX_BACKGROUND_PROCESSES)
	{
		manager->finishedBackgroundProcesses[manager->finishedBackgroundProcessesSize]._pid = pid;
		manager->finishedBackgroundProcesses[manager->finishedBackgroundProcessesSize]._status = status;
		manager->finishedBackgroundProcessesSize++;

		return true;
	}
	else
	{
		return false;
	}
}

bool RemoveBGProcess(struct BGProcessManager* manager, int pid)
{
	for(int i = 0; i < manager->backgroundProcessesSize; i++)
	{
		if(manager->backgroundProcesses[i] == pid)
		{
			manager->backgroundProcesses[i] = manager->backgroundProcesses[manager->backgroundProcessesSize - 1]; 
			manager->backgroundProcessesSize--;

			return true;
		}
	}

	return false;
}

void PrintFinishedProcesses(struct BGProcessManager* manager)
{
	for(int i=0; i<manager->finishedBackgroundProcessesSize; i++)
	{
		int pid = manager->finishedBackgroundProcesses[i]._pid;
		int status = manager->finishedBackgroundProcesses[i]._status;

		if(WIFEXITED(status))
		{
			int exitStatus = WEXITSTATUS(status);
			dprintf(STDOUT_FILENO, "Background process %d terminated. (exited with status %d)\n", pid, exitStatus);
		}
		else if(WIFSIGNALED(status))
		{
			int signalNo = WTERMSIG(status);
			dprintf(STDOUT_FILENO, "Background process %d terminated. (killed by signal %d)\n", pid, signalNo);
		}
	}

	manager->finishedBackgroundProcessesSize = 0;
}

struct BGProcessManager bgManager;

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//Custom shell's signal handlers

int activeForegroundProcesses;

void ShellSigchldHandler(int signum)
{
	int status;
	pid_t pid;

	while((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		if(RemoveBGProcess(&bgManager, pid))
		{
			AddFinishedBGProcess(&bgManager, pid, status);
		}
		else
		{
			activeForegroundProcesses--;
		}
	}
}

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//Funcions for executing commands

//Returns true, if com was a builtin command
bool ExecuteIfBuiltin(command* com)
{
	char* functionName = com->args->arg;

	for(int i=0; builtins_table[i].name!=NULL; i++)
	{
		if(strcmp(builtins_table[i].name, functionName)==0)
		{
			//ARGUMENTS
			char* argv[MAX_LINE_LENGTH] = {NULL};
			int argc = 0;

			argseq* as = com->args;
			do
			{
				argv[argc] = as->arg;
				argc++;
				as = as->next;
			}
			while(as != com->args);

			//EXECUTION
			int e = builtins_table[i].fun(argv);
			if(e==BUILTIN_ERROR)
			{
				dprintf(STDERR_FILENO, "Builtin %s error.\n", functionName);
			}
			return true;
		}
	}

	return false;
}

void ExecuteCommand(char* argv[], command* com, bool isBackground)
{
	//REDIRECTIONS
	int redirErr = 0;
	int input_fd = -1;
	int output_fd = -1;

	redirseq * redirs = com->redirs;
	if(redirs)
	{
		do
		{	
			int flags = redirs->r->flags;

			if(IS_RIN(flags) || IS_ROUT(flags) || IS_RAPPEND(flags))
			{
				int* fdToUpdate = NULL;
				int openingFlags;

				if(IS_RIN(flags))
				{
					fdToUpdate = &input_fd;
					openingFlags = O_RDONLY;
				}
				else
				{
					fdToUpdate = &output_fd;
					openingFlags = O_WRONLY | O_CREAT | (IS_ROUT(flags) ? O_TRUNC : O_APPEND);
				}


				if(*fdToUpdate > 0)
				{
					close(*fdToUpdate);
				}
				*fdToUpdate = open(redirs->r->filename, openingFlags, DEFAULT_FILE_PERMISSIONS);
				if(*fdToUpdate < 0)
				{
					redirErr = 1;
					break;
				}
			}

			redirs = redirs->next;
		}
		while(redirs!=com->redirs);	

		if(redirErr == 1)
		{
			if(errno == ENOENT)
			{
        		dprintf(STDERR_FILENO, "%s: no such file or directory\n", redirs->r->filename);
    		}
			else if(errno == EACCES)
			{
        		dprintf(STDERR_FILENO, "%s: permission denied\n", redirs->r->filename);
    		}
			else
			{
        		dprintf(STDERR_FILENO, "%s: exec error\n", redirs->r->filename);
    		}

			return;
		}
	}

	//INPUT AND OUTPUT REDIRECTION
	if(input_fd >= 0)
	{
		if(dup2(input_fd, STDIN_FILENO) < 0)
		{
			perror("dup2");
			dprintf(STDERR_FILENO, "Dup2 error stdin.\n");
			exit(EXEC_FAILURE);
		}
		close(input_fd);
	}

	if(output_fd >= 0)
	{
		if(dup2(output_fd, STDOUT_FILENO) < 0)
		{
			perror("dup2");
			dprintf(STDERR_FILENO, "Dup2 error stdout.\n");
			exit(EXEC_FAILURE);
		}
		close(output_fd);
	}

	//SIGINT AND SIGCHLD HANDLING
	struct sigaction sa_int;
	sa_int.sa_handler = SIG_DFL;
	sigaction(SIGINT, &sa_int, NULL);

	struct sigaction sa_chld;
	sa_chld.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa_chld, NULL);

	//EXECUTION
	execvp(argv[0], argv);
	
	//IF WE REACH HERE, IT MEANS THAT EXECUTION FAILED
	int errorNO = errno;

	if(errorNO == ENOENT)
	{
		dprintf(STDERR_FILENO, "%s: no such file or directory\n", argv[0]);
	}
	else if(errorNO == EACCES)
	{
		dprintf(STDERR_FILENO, "%s: permission denied\n", argv[0]);
	}
	else
	{
		dprintf(STDERR_FILENO, "%s: exec error\n", argv[0]);
	}

	exit(EXEC_FAILURE);

	if(input_fd >= 0)
	{
		close(input_fd);
	}
	if(output_fd >= 0)
	{
		close(output_fd);
	}
}

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//Function for linking pipeline commands with pipes and executing them

struct sigaction sa_chld_block;

void ExecutePipeline(pipeline* pl)
{
	//printpipeline(pl, 0);

	activeForegroundProcesses = 0;

	int commandNo = 0;
	commandseq* commands = pl->commands;
	if(commands == NULL)
	{
		return;
	}

	do
	{
		commandNo++;
		commands= commands->next;
	}
	while(commands != pl->commands);
	if(commandNo == 0)
	{
		return;
	}
	else if(commandNo == 1)
	{
		if(pl->commands->com != NULL)
		{
			if(ExecuteIfBuiltin(pl->commands->com))
			{
				return;
			}
		}
		else
		{
			return;
		}
	}

	bool isBackground = (pl->flags & INBACKGROUND);

	commands = pl->commands;
	int previousPipeRead = -1;

	if(sigprocmask(SIG_BLOCK, &sa_chld_block.sa_mask, NULL) < 0)
	{
		perror("sigprocmask");
		dprintf(STDERR_FILENO, "Sigprocmask error block.\n");
		exit(EXEC_FAILURE);
	}

	for(int i=0; i<commandNo; i++)
	{
		command* com = commands->com;

		int pipefd[2];
		if(i < commandNo - 1)
		{
			if(pipe(pipefd) < 0)
			{
				perror("pipe");
				dprintf(STDERR_FILENO, "Pipe error.\n");
                exit(EXEC_FAILURE);
			}
		}

		//ARGUMENTS
		char* argv[MAX_LINE_LENGTH] = {NULL};
		int argc = 0;

		argseq* as = com->args;
		do
		{
			argv[argc] = as->arg;
			argc++;
			as = as->next;
		}
		while(as != com->args);

		int child = fork();
		if(child < 0)
		{
			perror("fork");
			dprintf(STDERR_FILENO, "Currently unable to create a new process. Try again later.\n");
			exit(EXEC_FAILURE);
		}
		else if(child == 0)
		{
			if(sigprocmask(SIG_UNBLOCK, &sa_chld_block.sa_mask, NULL) < 0)
			{
				perror("sigprocmask");
				dprintf(STDERR_FILENO, "Sigprocmask error block.\n");
				exit(EXEC_FAILURE);
			}

			if(previousPipeRead != -1)
			{
				if(dup2(previousPipeRead, STDIN_FILENO) < 0)
				{
					perror("dup2");
					dprintf(STDERR_FILENO, "Dup2 error stdin.\n");
					exit(EXEC_FAILURE);
				}

				close(previousPipeRead);
			}

			if(i < commandNo - 1)
			{
				close(pipefd[0]);
				if(dup2(pipefd[1], STDOUT_FILENO) < 0)
				{
					perror("dup2");
					dprintf(STDERR_FILENO, "Dup2 error stdout.\n");
					exit(EXEC_FAILURE);
				}

				close(pipefd[1]);
			}

			if(isBackground)
			{
				setsid();
			}

			ExecuteCommand(argv, com, isBackground);
			exit(EXEC_FAILURE);
		}
		else
		{
			if(isBackground)
			{
				AddBGProcess(&bgManager, child);
			}
			else
			{
				activeForegroundProcesses++;
			}

			if(previousPipeRead != -1)
			{
				close(previousPipeRead);
			}

			if(i < commandNo - 1)
			{
				close(pipefd[1]);
				previousPipeRead = pipefd[0];
			}

			commands = commands->next;
		}
	}

	struct sigaction sa_waitForChildren;
	sigemptyset(&sa_waitForChildren.sa_mask);

	if(!isBackground)
    {
		while(activeForegroundProcesses > 0)
		{
			sigsuspend(&sa_waitForChildren.sa_mask);
		}
    }

	if(sigprocmask(SIG_UNBLOCK, &sa_chld_block.sa_mask, NULL) < 0)
	{
		perror("sigprocmask");
		dprintf(STDERR_FILENO, "Sigprocmask error block.\n");
		exit(EXEC_FAILURE);
	}
}

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//Command parser

void Parse(char readLine[], int *readLineLength)
{
	pipelineseq * ln = parseline(readLine);
	*readLineLength = 0;
	if(ln == NULL)
	{
		dprintf(STDERR_FILENO, "%s\n", SYNTAX_ERROR_STR);
		return;
	}

	if(ln->pipeline == NULL)
	{
		return;
	}

	pipelineseq * ps = ln;

	do
	{
		ExecutePipeline(ps->pipeline);
		ps= ps->next;
	}
	while(ps!=ln);
}

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------
//Main function

int main(int argc, char *argv[])
{
	char buf[MAX_LINE_LENGTH]; 
	ssize_t readBytes;

	char readLine[MAX_LINE_LENGTH] = {0};
	int readLineLength = 0;

	int tty = isatty(STDIN_FILENO);

	//Shell sends SIGINT to all foreground processes when it itself receives SIGINT
	struct sigaction sa_int;
	sa_int.sa_handler = SIG_IGN;
	sigemptyset(&sa_int.sa_mask);
	sa_int.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa_int, NULL);

	//Shell listens to all finished child processes statuses when it receives SIGCHLD
	struct sigaction sa_chld;
	sa_chld.sa_handler = ShellSigchldHandler;
	sigemptyset(&sa_chld.sa_mask);
	sa_chld.sa_flags = SA_RESTART; //Restart interrupted syscalls
	sigaction(SIGCHLD, &sa_chld, NULL);

	sigemptyset(&sa_chld_block.sa_mask);
	sigaddset(&sa_chld_block.sa_mask, SIGCHLD);

	//Shell's main loop
	while(1)
	{
		if(tty)
		{
			PrintFinishedProcesses(&bgManager);
			dprintf(STDOUT_FILENO, "%s", PROMPT_STR);
		}

		readBytes = read(STDIN_FILENO, buf, MAX_LINE_LENGTH);	
		if(readBytes == 0)
		{
			if(readLineLength >= MAX_LINE_LENGTH)
			{
				dprintf(STDERR_FILENO, "%s\n", SYNTAX_ERROR_STR);
			}
			else if(readLineLength > 0)
			{
				readLine[readLineLength] = '\0';
				Parse(readLine, &readLineLength);
			}

			break;
		}
		else if(readBytes < 0)
		{
			if(errno == EINTR)
			{
				continue;
			}
			perror("read");
			exit(EXEC_FAILURE);
		}
		
		char* start = buf;
		char* firstEndline;

		//Divide the received data into individual commands
		while((firstEndline = memchr(start, '\n', buf + readBytes - start)) != NULL)
		{
			size_t currentLineLength = firstEndline - start; 

			if(readLineLength + currentLineLength >= MAX_LINE_LENGTH)
			{
				dprintf(STDERR_FILENO, "%s\n", SYNTAX_ERROR_STR);
			}
			else
			{
				memcpy(readLine + readLineLength, start, currentLineLength);
				readLineLength += currentLineLength;
				readLine[readLineLength] = '\0';
				Parse(readLine, &readLineLength);
			}

			readLineLength = 0;
			start = firstEndline + 1;
		}

		size_t remainingLineLength = buf + readBytes - start;
		if(remainingLineLength > 0)
		{
			if(readLineLength + remainingLineLength >= MAX_LINE_LENGTH)
			{
				//The error will be printed after reading an endline in a future iteration
				readLineLength = MAX_LINE_LENGTH;
			}
			else
			{
				memcpy(readLine + readLineLength, start, remainingLineLength);
				readLineLength += remainingLineLength;
			}
		}
	}

	return 0;
}

//----------------------------------------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------------------------------------