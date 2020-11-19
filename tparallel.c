#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define VERSION "v0.0.1"

struct threaded_file 
{
   pthread_mutex_t mutex;
   FILE *fp;
   char *filename;
};

struct threaded_file commands, failures, unprocessed;

pthread_mutex_t screen;
int num_threads = 10;
volatile int should_quit = 0;
volatile int found_running;

struct thread_data
{
   int index;
   int num_executions;
   int num_failures;
   char last_command[100];
   int dirty;
   int aborted;
   pthread_mutex_t lock;
};

struct option long_options[] =
{
   {"threads",      required_argument,  0, 't'},
   {"commands",     required_argument,  0, 'c'},
   {"unprocessed",  required_argument,  0, 'u'},
   {"failures",     required_argument,  0, 'f'},
   {0, 0, 0, 0}
};

void print_thread_data(struct thread_data *data);

void threaded_file_close(struct threaded_file *file)
{
   if (file->fp != NULL) {
      fclose(file->fp);
      pthread_mutex_destroy(&file->mutex);
      file->fp = NULL;
   }
}

FILE *threaded_file_open(struct threaded_file *file, char *mode)
{
   if (file->filename == NULL)
     file->filename = "/dev/null";

   file->fp = fopen(file->filename, mode);
   if (file->fp == NULL) {
      fprintf(stderr, "Could not open file %s\n", strerror(errno));
      return NULL;
   }
   pthread_mutex_init(&file->mutex, NULL);
   return file->fp;
}

void *process(void *data) 
{
   struct thread_data *thread_data = (struct thread_data *) data;
   char line[8192];

   while(!should_quit)
     {
	pthread_mutex_lock(&commands.mutex);
	if (fgets(line, sizeof(line), commands.fp) == NULL) 
	  {
	     pthread_mutex_unlock(&commands.mutex);
	     pthread_mutex_lock(&thread_data->lock);
	     thread_data->aborted = 1;
	     thread_data->dirty = 1;
	     pthread_mutex_unlock(&thread_data->lock);
	     return NULL;
	  }

	pthread_mutex_unlock(&commands.mutex);

	pthread_mutex_lock(&thread_data->lock);
	strncpy(thread_data->last_command, line, 100);
	char *b = thread_data->last_command;
	while(*b != '\0') {if (*b == '\n' || *b == '\r') *b = ' '; b++;}
	thread_data->dirty = 1;
	pthread_mutex_unlock(&thread_data->lock);

	print_thread_data(thread_data);
	/* thread_data->dirty = 0; */

	pid_t f = fork();
	if (f == 0) {
	   int fd = open("/dev/null", O_WRONLY);
	   dup2(fd, 1);
	   dup2(fd, 2);
	   close(fd);
	   char *argv[] = {
	      "/usr/bin/env",
	      "bash",
	      "-c",
	      line,
	      NULL
	   };
	   char *env[] = {
	      NULL
	   };
	   if (execve("/usr/bin/env", argv, env) != 0) { 
	      fprintf(stderr, "Could not launch process: %s\n", strerror(errno));
	   }
	} else {
	   int ret;
	   waitpid(f, &ret, WUNTRACED | WCONTINUED);
	   pthread_mutex_lock(&thread_data->lock);
	   if (WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT || WTERMSIG(ret) == SIGQUIT))  
	     should_quit = 1;
	   
	   if (ret != 0) {
	     thread_data->num_failures++;
	      pthread_mutex_lock(&failures.mutex);
	      fputs(line, failures.fp);
	      fflush(failures.fp);
	      pthread_mutex_unlock(&failures.mutex);
	   }

	   thread_data->num_executions++;
	   pthread_mutex_unlock(&thread_data->lock);
	}
     }
   pthread_mutex_lock(&thread_data->lock);
   thread_data->aborted = 1;
   thread_data->dirty = 1;
   pthread_mutex_unlock(&thread_data->lock);
   return NULL;
}

void sig_handler(int signal) 
{
   /* fprintf(stderr, "Interrupting\n"); */
   should_quit = 1;
}

void parse_arguments(int argc, char **argv) 
{
   while(1)
     {
	int option_index;
	int c = getopt_long(argc, argv, "", long_options, &option_index);
	switch(c) 
	  {
	   case -1:                                         return;
	   default: printf("unhandled: %c\n", c);           break;
	   case 't': num_threads = atoi(optarg);            break;
	   case 'c': commands.filename = optarg;            break;
	   case 'f': failures.filename = optarg;            break;
	   case 'u': unprocessed.filename = optarg;         break;
	   case 0:
	     printf("option %s", long_options[option_index].name);
	     if (optarg)
	       printf(" with arg %s", optarg);
	     printf("\n");
	     break;
	  }
     }
}

void print_thread_data(struct thread_data *data)
{
   pthread_mutex_lock(&screen);
   pthread_mutex_lock(&data->lock);
   fprintf(stdout, "\033[%d;0H\033[K", data->index + 3);
   fprintf(stdout, "[Thread: %d, N: %d, F: %d: S: %c] \t%s", data->index, data->num_executions, data->num_failures, data->aborted ? 'a' : 'r', data->last_command);
   fflush(stdout);
   data->dirty = 0;
   pthread_mutex_unlock(&data->lock);
   pthread_mutex_unlock(&screen);
}

void write_unprocessed() 
{
   char line[8192];
   int i=0;
   while(fgets(line, sizeof(line), commands.fp) != NULL) 
     {
	i++;
	fputs(line, unprocessed.fp);
     }
   fprintf(stderr, "%d unprocessed lines found", i);
   fflush(unprocessed.fp);
}


int main(int argc, char **argv)
{
   int ret = 0;
   signal(SIGINT, sig_handler);
   parse_arguments(argc, argv);
   found_running = num_threads;
   pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
   struct thread_data *thread_data = malloc(sizeof(struct thread_data) * num_threads);

   if (pthread_mutex_init(&screen, NULL) != 0)
     exit(2);
   
   if (threaded_file_open(&commands, "r") == NULL
      || threaded_file_open(&failures, "w") == NULL
      || threaded_file_open(&unprocessed, "w") == NULL)
     {
	ret = 1;
	goto cleanup;
     }

   fprintf(stdout, "\033[2J\033[0;0H");
   fprintf(stdout, "tparallel %s\nExecuting file %s with xx threads\n", VERSION, commands.filename);
   for(int i=0;i<num_threads;i++) fprintf(stdout, "Starting thread[%d]\n", i);
   fflush(stdout);
   for(int i=0;i<num_threads;i++) 
     {
	thread_data[i].index = i;
	pthread_mutex_init(&thread_data[i].lock, NULL);
	pthread_create(&(threads[i]), NULL, process, &thread_data[i]);
     }

   int _found_running = num_threads;
   while(!should_quit && _found_running)
     {
	_found_running = 0;
	sleep(1);
	for(int i=0;i<num_threads;i++)
	  {
	     if (!thread_data[i].aborted) _found_running++;
	     if (thread_data[i].dirty)
	       {
		  thread_data[i].dirty = 0;
		  print_thread_data(&thread_data[i]);
	       }
	  }
	found_running = _found_running;
	fprintf(stdout, "\033[0;1Htparallel %s\n\033[KExecuting file %s with %d threads\n", VERSION, commands.filename, found_running);
     }

   for(int i=0;i<num_threads;i++) 
     {
	pthread_join(threads[i], NULL);
	fprintf(stderr, "Thread %d closed\n", i);
     }

   write_unprocessed();

cleanup:
   if (threads) free(threads);
   if (thread_data) free(thread_data);
   threaded_file_close(&commands);
   threaded_file_close(&failures);
   threaded_file_close(&unprocessed);
   return ret;
}
