/****************************************************************
 * Name        :  Paul Asu                                      *
 * Class       :  CSC 415                                       *
 * Date        :  07/01/2020                                    *
 * Description :  Writting a simple shell program               *
 *                that will execute simple commands. The main   *
 *                goal of the assignment is working with        *
 *                fork, pipes and exec system calls.            *
 ****************************************************************/

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <termios.h>

/* CANNOT BE CHANGED */
#define BUFFERSIZE 256
/* --------------------*/
#define PROMPT "myShell >> "
#define PROMPTSIZE sizeof(PROMPT)

// #define DELIMS " \t\n"
// #define FOREVER for(;;)

// function prototypes
//command execution controller.
void execute_commands(char *[], int, int);
// index of args if found in the array myargv
// int arg_found(char *[], char*, int);
int arg_found(char *[], char*, int);
// execute command in the background processes
void bg_process(char *[], int);
void first_redirect_output(char *[], int);
void second_redirect_output(char *[], int); 
void redirect_input(char *[], int);
void single_piped_command(char *[], int);
void execute(char *[], int);
void pwd();
void change_dir();
void ls();


int 
main(int argc, char** argv)
{
    //string lines
    char *str_line = NULL;
    ssize_t bytes_read;
    size_t len = 0;
    const char delims[1] = "";
    // const char delims[32] = " ";
    char *token;
    //we stored parsed substrings
    char *myargv[BUFFERSIZE];
    //count of the num of arguments stored in the myargv array
    int myargc;
    // char *arg_vector[32];

    printf("%s," PROMPT);
    
    while (getline(&str_line, &len, stdin)!= -1)
    {
        //to reset
        myargc = 0;
        str_line[strlen(str_line)-1] = '\0';
        token = strtok(str_line, delims);

        while (token != NULL)
        {
            //to store array of token in myargv array
            myargv[myargc++] = token;
            token = strtok(NULL, delims);
        }

        if (myargc > 0)
        {
            if (strcmp(myargv[0], "exit") == 0)
            {
                exit(-1);
            } else
            {
                execute_commands(myargv, myargc, 0);
            }
        }
        printf("%s", PROMPT);
    }
    
    return 0;
}

void execute_commands(char *myargv[], int myargc, int bg){
    if (strcmp(myargv[0], "pwd") == 0)
    {
        pwd();
    } 
    else if(arg_found(myargv, ">", myargc)>= 0)
    {
        first_redirect_output(myargv, myargc);
    }
     else if (arg_found(myargv, ">>", myargc) >= 0) {
        // redirect the second standard output of a command to a file
        second_redirect_output(myargv, myargc);

        // #4 condition
    } else if (arg_found(myargv, "<", myargc) >= 0) {
        // redirect the standard input of a command to come from a file
        redirect_input(myargv, myargc);
    }
    else if(arg_found(myargv, "|", myargc) >= 0) 
    {
        // execute multiple commands connected by a single shell pipe
        single_piped_command(myargv, myargc);
    }
    else if (arg_found(myargv, "&", myargc) >= 0) {
        // execute a command in background
        bg_process(myargv, myargc);

        // #2 condition
    }
    else if (strcmp(myargv[0], "cd") == 0) {
        // change directory
        change_dir(myargv[1]);
    }
    else if ((strcmp(myargv[0], "ls") == 0)&&(myargc==1)) {
        // change directory
        ls(myargc, myargv);
    }
    else
    {
        execute(myargv, bg);
    }
        
}

// ***  condition 2   *** //
void execute(char *myargv[], int bg){
    pid_t pid = fork();
    if (bg == 0)
    {
        if (bg)
        {
            setpgid(pid, 0);
        }
        execvp(myargv[0], myargv);
    }
    else if (pid > 0)
    {
        wait(NULL);
    }
    else
    {
        perror("The creation of a child process was not successful");
    }
}

int arg_found(char *myargv[], char *arg, int myargc){
    int index = 0;
    while (myargc > index)
    {
        if (strcmp(myargv[index], arg) == 0)
        {
            return index;
        }
        index++;
    }
    return(-1);
    
}

void by_process(char *myargv[], int myargc){
    int index_of_ampersand = arg_found(myargv, "&", myargc);
    if (index_of_ampersand != myargc -1)
    {
        perror("'&' is not found at the ene of the command\n");
        exit(EXIT_FAILURE);
    }
    else
    {
        myargv[myargc - 1] = NULL;
        myargc--;
        execute_commands(myargv, myargc, 1);
    }
}

//first redirect output() to redirect standard output of cmd file
void first_redirect_output(char *myargv[], int myargc){
    int index = arg_found(myargv, ">", myargc);

    if (index != myargc -2 || myargc < 3){
        perror("Standard output redirection error. '>' is not in proper position.\n");
        exit(EXIT_FAILURE);
    }
    char *left_side_arg[index+1];
    int i = 0;
    while (index > i)
    {
        left_side_arg[i] = myargv[i];
        i++;
    }
    left_side_arg[index] = NULL;
    int out = dup(STDOUT_FILENO);

    int new_fd = open(myargv[myargc -1], O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (new_fd < 0)
    {
        perror("error opening file.");
        exit(EXIT_FAILURE);
    }
    else
    {
        dup2(new_fd, STDOUT_FILENO);
        execute_commands(left_side_arg, index, 0);
        close(new_fd);
        dup2(out, STDOUT_FILENO);
    }
}

void second_redirect_output(char *myargv[], int myargc){
    int index = arg_found(myargv, ">>", myargc);
    if (index != myargc - 2 || myargc < 3){
        perror("Standard output redirection error '>>' is not proper position.\n");
        exit(-1);
    }
    char *left_side_arg[index +1];
    int i = 0;
    while (index > i)
    {
        left_side_arg[i] = myargv[i];
        i++;
    }

    left_side_arg[index] = NULL;
    int out = dup(STDOUT_FILENO);

    int new_fd = open(myargv[myargc -1], O_RDWR|O_CREAT|O_APPEND, 0644);
    if (new_fd < 0){
        perror("Error openning file.");
        exit(-1);
    }else 
    {
        dup2(new_fd, STDOUT_FILENO);
        execute_commands(left_side_arg, index, 0);
        close(new_fd);
        dup2(out, STDOUT_FILENO);
    }
} // end second_redirect_output()

void redirect_input(char *myargv[], int myargc){
    int index = arg_found(myargv, "<", myargc);

    if (index != myargc -2 || myargc < 3)
    {
        perror("The standard input rediretion error... '<' is not in proper position.\n");
        exit(-1);
    }
    char *left_side_arg[index + 1];
    int i = 0;
    while (index > i) {
        left_side_arg[i] = myargv[i];
        i++;
    }
    left_side_arg[index] = NULL;

    int new_fd = open(myargv[myargc - 1], O_RDONLY);
    if (new_fd < 0)
    {
        perror("Error opening file....");
        exit(-1);
    }else
    {
        dup2(new_fd, STDIN_FILENO);
        execute_commands(left_side_arg, index, 0);
        close(new_fd);
    }
}



// execute multiple commands connected by a single shell pipe
void single_piped_command(char *myargv[], int myargc) {

    int fspipe[2];
    char* token;
    char *pipe_strings1 = NULL;
    char *pipe_strings2 = NULL ;
    printf("hello");
    sleep(0.1);
    fflush(stdout);
    fflush(stdin);
    pipe(fspipe);
    pid_t pid1 = fork();
    if (pid1 == (pid_t) 0)
    {

        //tokens for LHS of |
        token = strtok(pipe_strings1, " ");
        
        while(token != NULL)
        {
            myargv = realloc(myargv, sizeof(char *) * ++myargc);
            if(myargv == NULL)
            {
                exit(-1);//Memory allocation failed
                printf("Memory allocation failed");
            }
            
            myargv[myargc - 1] = token;
            token = strtok(NULL, " ");
        }
        
        myargv = realloc(myargv, sizeof(char *) * (myargc+1));
        myargv[myargc] = 0;

        close(fspipe[0]);
        dup2(fspipe[1], 1);
        close(fspipe[1]);
        
        execvp(&*myargv[0], myargv);
        exit(0);
    
    }
    else if (pid1 > (pid_t) 0)
    {
        free(myargv);
        fflush(stdout);
        fflush(stdin);
        pid_t pid2 = fork();
        if (pid2 == (pid_t) 0)
        {
            //tokens for RHS of |
            token = strtok(pipe_strings2, " ");
            
            while(token != NULL){
            myargv = realloc(myargv, sizeof(char *) * ++myargc);
            if(myargv == NULL){
            exit(-1);//Memory allocation failed
            printf("Memory allocation failed");
            }
            
            myargv[myargc - 1] = token;
            token = strtok(NULL, " ");
            }
            
            myargv = realloc(myargv, sizeof(char *) * (myargc+1));
            myargv[myargc] = 0;

            close(fspipe[1]);
            dup2(fspipe[0], 0);
            close(fspipe[0]);
            
            if(execvp(&*myargv[0], myargv) < 0){
            perror("execvp ");
            exit(-5);
            }
            exit(0);
        }
        else if (pid2 > (pid_t) 0){
            close(fspipe[0]);
            close(fspipe[1]);
            wait(NULL);
            
            wait(NULL);
        }
        else{
        perror("pid2");
        }
    }
    else{

        perror("pid1 ");
    }
}

// pwd() function prints working directory
void pwd() 
{
    char path_name[BUFFERSIZE];

    if (getcwd(path_name, sizeof(path_name)) != NULL) 
    {
        printf("%s\n", path_name);
    } else 
    {
        perror("Error printing working directory...\n");
        exit(EXIT_FAILURE);
    } // end if-else

} // end pwd()


// #10
// change_dir() function changes directory
void change_dir(const char *path_name) 
{

    if (chdir(path_name) != 0) 
    {
        perror("Error changing directory...\n");
        exit(EXIT_FAILURE);
    }
} // end change_dir()

// ls()
void ls(int argc, char *argv[]) 
{

    struct dirent **namelist;
    int n;
    if(argc < 1)
    {
        exit(EXIT_FAILURE);
    }
    else if (argc == 1)
    {
        n = scandir(".",&namelist,NULL,alp);
    }
    else {
    n = scandir(argv[1], &namelist, NULL, alphasort);
    }
    if(n < 0) {
    perror("scandir");
    exit(EXIT_FAILURE);
    }
    else if(n==2)
    {
    printf("The directory is empty\n");
    }
    else {
    while (n--)
    {
    if(n>2){
    printf("%s\n",namelist[n]->d_name);
    }
    free(namelist[n]);


    } free(namelist);
    }

}