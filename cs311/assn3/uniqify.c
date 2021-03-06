/* Compiler directives */
#define _XOPEN_SOURCE 500	//needed to make sigaction, etc work
#define _POSIX_SOURCE		//for fdopen()

/* Includes */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Definitions and global variables */
#define MAX_WORD_LEN 100	//maximum word length
pid_t *process_array;
int num_sorts = 0;
int **sortpipefds;
int **suppipefds;
struct word_counter {
	char word[MAX_WORD_LEN];
	int count;
};

/* Function prototypes */
int alpha_index(int num_words, char **words);
void close_pipe(int pfd);
void close_pipes_array(int **pipe_array, int end);
void create_pipe(int *fds);
void free_pipes_array(int num_pipes, int **pipes_array);
int **generate_pipes_array(int num_pipes);
void grim_reaper(int s);
void help();
int is_empty(char *str);
void puke_and_exit(char *errormessage);
void r_r_parser(int **out_pipe);
void spawn_sorts(int **in_pipe, int **out_pipe);
char *strip_newline(char *word);
void spawn_suppressor(int **in_pipe);
char *strtolower(char *str);
void suppressor_process(int **in_pipe);
void reap_children(int num_children);

int main(int argc, char **argv)
{
	if (argc < 2)
		help();

	/* Get number of processes to use */
	num_sorts = atoi(argv[1]);

	if (num_sorts > 1000)
		printf("Number of sorts is OVER 9000!!!\n");

	/* Setup signal handlers */
	struct sigaction act;

	act.sa_handler = grim_reaper;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	sigaction(SIGQUIT, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	/* Generate all necessary pipes for sort */
	sortpipefds = generate_pipes_array(num_sorts);

	/* Generate pipes for the suppressor */
	suppipefds = generate_pipes_array(num_sorts);

	/* Spawn sort processes */
	spawn_sorts(sortpipefds, suppipefds);

	/* Parse STDIN */
	r_r_parser(sortpipefds);

	/* Spawn suppressor process */
	spawn_suppressor(suppipefds);

	/* Wait for child processes to die */
	reap_children(num_sorts);

	/* Free malloced arrays of pipes */
	free_pipes_array(num_sorts, sortpipefds);
	free_pipes_array(num_sorts, suppipefds);
	free(process_array);
	return (0);
}

/*
Signal Handler
Reference: http://stackoverflow.com/questions/1641182/how-can-i-catch-a-ctrl-c-event-c
*/
void grim_reaper(int s)
{
	int i;
	int kid_signal = 0; /* signal to send to children */
	switch(s){
		case SIGQUIT:
			kid_signal = SIGQUIT;
			break;
		case SIGHUP:
			kid_signal = SIGQUIT;
			break;
		case SIGINT:
			kid_signal = SIGINT;
			break;
		default:
			kid_signal = SIGQUIT;
	}

	/* Send signals to children */
	if (process_array != NULL) {
		for (i = 0; i < num_sorts; i++) {
			kill(process_array[i], kid_signal);
		}
	}
	/* Wait for child processes to die */
	reap_children(num_sorts);

	/* Free malloced arrays */
	if (sortpipefds != NULL)
		free_pipes_array(num_sorts, sortpipefds);
	if (suppipefds != NULL)
		free_pipes_array(num_sorts, suppipefds);
	free(process_array);
	
	exit(1);
}

/* Waits on child processes */
void reap_children(int num_children)
{
	int i;
	for (i = 0; i < num_children; i++) {
		wait(NULL);
	}
}

void help()
{
	printf("Usage:\n");
	printf("uniqify [number of sorting processes]\n");
	exit(0);
}

void puke_and_exit(char *errormessage)
{
	perror(errormessage);
	printf("Errno = %d\n", errno);
	exit(-1);
}

void close_pipe(int pfd)
{
	if (close(pfd) == -1) {
		char errormessage[8];
		snprintf(errormessage, 8, "Error closing pipe %d\n", pfd);
		puke_and_exit(errormessage);
	}
}

void create_pipe(int *fds)
{
	if (pipe(fds) == -1)
		puke_and_exit("Error creating pipes\n");
}

/* returns an empty 2-dimensional array */
int **generate_pipes_array(int num_pipes)
{				
	int **pipes_array = malloc(sizeof(int *) * (num_pipes));
	int i;
	for (i = 0; i < num_pipes; i++) {
		pipes_array[i] = malloc(sizeof(int) * 2);
	}
	return (pipes_array);
}

/* creates an array containing all the PIDs of the child processes */
void spawn_sorts(int **in_pipe, int **out_pipe)
{
	//Spawn all the sort processes
	process_array = malloc(sizeof(pid_t) * num_sorts);
	pid_t pid;
        int i;
        for (i = 0; i < num_sorts; i++){
                create_pipe(in_pipe[i]);
                create_pipe(out_pipe[i]);  
                switch(pid = fork()){
                        case -1: //Oops case
                                puke_and_exit("Forking error\n");
                        case 0: //Child case
			        close(STDIN_FILENO);
			        close(STDOUT_FILENO);
			        if (in_pipe[i][0] != STDIN_FILENO) {	//Defensive check
					if (dup2(in_pipe[i][0], STDIN_FILENO) ==
					    -1)
						puke_and_exit("dup2 0");
				}
				/* Bind stdout to out_pipe*/
				close_pipe(out_pipe[i][0]);	//close read end of output pipe
				if (out_pipe[i][1] != STDOUT_FILENO) {	//Defensive check
					if (dup2(out_pipe[i][1], STDOUT_FILENO) ==
					    -1)
						puke_and_exit("dup2 1");
				}
                                /* 
                                Pipes from previously-spawned children are still open in this child
                                Close them and close the duplicate pipes just created by dup2 
                                */
                                close_pipes_array(in_pipe, i+1);
                                close_pipes_array(out_pipe, i+1);
                                execlp("sort", "sort", (char *)NULL);
                        default: //Parent case
                                process_array[i] = pid;
                                close_pipe(in_pipe[i][0]);
                		close_pipe(out_pipe[i][1]);
                                break; 
                }
        }
}

/* 
Round Robin parser
Sends words that contain only alphabetical characters 
*/
void r_r_parser(int **out_pipe)
{				
	int i;   
        char buf[MAX_WORD_LEN];
        FILE *outputs[num_sorts];
        for (i = 0; i < num_sorts; i++) {
                outputs[i] = fdopen(out_pipe[i][1], "w");
        }

        /* Scan and discard any leading unwanted chars */ 
	int result = scanf("%*[^a-zA-Z]"); 
	while (result != EOF){
		result = scanf("%[a-zA-Z]", buf); //scan what we want
		if(!is_empty(buf)){ //avoid putting an empty buffer into the pipe
			fputs(strtolower(buf), outputs[i % num_sorts]);
			fputs("\n", outputs[i % num_sorts]);
		}
		buf[0] = '\0'; //"empty" buffer
		result = scanf("%*[^a-zA-Z]"); //scan what we don't.
		i++;
	}

        /* Flush the streams */
        for (i = 0; i < num_sorts; i++) {
                fclose(outputs[i]);
        }
}

/* Converts a string to lowercase */
char *strtolower(char *str){
	int i;
	for(i=0;i<sizeof(str);i++){
		str[i] = tolower(str[i]);
	}
	return str;
}

void spawn_suppressor(int **in_pipe)
{
	pid_t pid;
	/* Fork to suppressor */
	switch (pid = fork()) {
	case -1: 
		break;
	case 0:
		suppressor_process(in_pipe);
		_exit(EXIT_SUCCESS);
		break;
	default:
		waitpid(pid, NULL, 0);
		break;
	}
}

void suppressor_process(int **in_pipe)
{
	int i;
	char **words;
	FILE *inputs[num_sorts];
	struct word_counter *cur_word = malloc(sizeof(struct word_counter));
	int alpha;		//index of alpha word in pipe
	int null_count = 0; //Increments if output from pipe is NULL (meaning EOF)

	/* initialize word array */
	words = malloc(num_sorts * sizeof(char *));
	for (i = 0; i < num_sorts; i++) {
		words[i] = malloc(MAX_WORD_LEN * sizeof(char));
	}

	/* fdopen in_pipes and get first batch of words to initialize cur_word with */
	for (i = 0; i < num_sorts; i++) {
		inputs[i] = fdopen(in_pipe[i][0], "r");
		if (fgets(words[i], MAX_WORD_LEN, inputs[i]) == NULL){
			words[i] = NULL;
			null_count++;
		}
	}
	/* Find the lowest alphabetical word in the array */
	alpha = alpha_index(num_sorts, words);

	/* Make this word our current word with count 1 */
	strncpy(cur_word->word, words[alpha], MAX_WORD_LEN);
	cur_word->count = 1;

	while (null_count < num_sorts) {
		if (fgets(words[alpha], MAX_WORD_LEN, inputs[alpha]) == NULL) {
			words[alpha] = NULL;
			null_count++;
		}
		alpha = alpha_index(num_sorts, words);
		if (alpha == -1) /* Meaning that the entire array was NULL */
			break;
		/* If the next word is the same as the current one, increment count */
		if (!strcmp(cur_word->word, words[alpha])) {
			cur_word->count++;
		} else {
			/* If it's a new word, print the last one and set current to the new one */
			printf("%d %s", cur_word->count, cur_word->word);
			strncpy(cur_word->word, words[alpha], MAX_WORD_LEN);
			cur_word->count = 1;
		}
	}

	/* Print last word */
	printf("%d %s", cur_word->count, cur_word->word);

	/* Free words array */
	for (i = 0; i < num_sorts; i++) {
		free(words[i]);
	}
	free(words);
	free(cur_word);

	/* Close inputs */
	for (i = 0; i < num_sorts; i++) {
		fclose(inputs[i]);
	}
}

/* removes trailing newline characters from strings */
char *strip_newline(char *word)
{				
	if (word[strlen(word) - 1] == '\n')
		word[strlen(word) - 1] = '\0';
	return word;
}

/* returns the index of the lowest alphabetical word in an array */
int alpha_index(int num_words, char **words)
{
	int alpha = -1;
	int i;
	/* First, find the first alpha word. If all the words are NULL, return -1 for error */
	for (i = 0; i < num_words; i++) {
		if (words[i] == NULL) {
			continue;
		} else {
			alpha = i;
			break;
		}
	}
	if (alpha == -1)
		return -1;
	/* Now find the lowest alphabetical word */
	for (i = 0; i < num_words; i++) {
		if (words[i] == NULL)
			continue;
		if (strcmp(words[i], words[alpha]) < 0)
			alpha = i;
	}
	return alpha;
}

void free_pipes_array(int num_pipes, int **pipes_array)
{
	int i;
	for (i = 0; i < num_pipes; i++) {
		free(pipes_array[i]);
	}
	free(pipes_array);
}

/* closes an array of pipes from zero up to a certain end point */
void close_pipes_array(int **pipe_array, int end)
{
        int i, j;
        for (i = 0; i < end; i++) {
                for ( j = 0; j < 2; j++) {
                	/* Using close_pipes() here will throw an error because some pipes have already been closed */
                        close(pipe_array[i][j]);
                }
        }
}

/* Returns 1 if passed-in string is empty */
int is_empty(char *str){
	if(str[0] == '\0')
		return 1;
	return 0;
}
