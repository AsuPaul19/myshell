/****************************************************************
 * Name        :  Paul Asu                                      *
 * Class       :  CSC 415                                       *
 * Date        :  08/08/2020                                    *
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
//Used for getting the terminal dimensions
#include <sys/ioctl.h>
#include <fcntl.h>
//Needed to use the waitpid function
#include <sys/types.h>
#include <sys/wait.h>
//Needed for getting the home directory
#include <pwd.h>
//Needed for changing the shell to 'raw' mode
#include <termios.h>

//Maximum size of the input the user can enter
#define BUFFERSIZE 256
//The string and size of the string used to prompt the user
#define PROMPT "myShell "
#define PROMPTSIZE sizeof(PROMPT)
//Colors for the prompt
#define GREEN "\033[32m"
#define BLUE "\033[34m"
#define RESETCOLOR "\x1b[0m"
//Simple keywords for booleans
#define true 1
#define false 0



//Contain all of the commands and thier information from an input
struct command
{
  //A 2D array to store all of the commands entered
  char*** commandTable;
  //The file the first command should read from
  char* inputFile;
  //The file the last command should write to
  char* outputFile;
  //Boolean to determine if the last command should append (1) to or truncate (0) the output file
  int append;
  //Boolean to determine if all of the commands should run in the background (1) or not (0)
  int background;
};

//Containts the history of commands run and the current history being viewed
struct history
{
  char** history;
  int totalItems;
};

//Functions declerations
struct termios* rawShell();
void resetShell(struct termios* oldSettings);
int getInput(char* prompt, char* input, int maxSize, struct history* commandHistory);
int parseCommands(char* string, struct command* allCommands, int commandSize);
void execute(struct command* allCommands, int numCommands);

int main(int argc, char** argv)
{
  //Store what the user inputs
  char* input = malloc(BUFFERSIZE);
  //Create a structure to hold all of the commands.
  //Each command in the command table could use as much memory as the input.
  //So allocate the command table with enough memory to hold the maximum number of commands with all with the same length as  input
  struct command allCommands = { malloc(BUFFERSIZE*BUFFERSIZE/2) };
  //Change the shell to raw mode and save the previous settings
  struct termios* oldSettings = rawShell();
  //Stores all of the previous commands run
  struct history commandHistory = { malloc(1000000), 0 };
  //Used to hold the number of commands in the struct
  int numCommands;
  
  while (1)
  {
    //Reset the variables in the struct. Use memset to set all of the elements of the 2D array to 0
    memset(allCommands.commandTable, 0, BUFFERSIZE*BUFFERSIZE/2);
    allCommands.inputFile = NULL;
    allCommands.outputFile = NULL;
    allCommands.append = false;
    allCommands.background = false;
    
    //Reset the input string
    memset(input, 0, BUFFERSIZE);
    
    //Get an input from the user. If the user wants to exit the program, then exit.
    if(!getInput(PROMPT, input, BUFFERSIZE, &commandHistory))
    {
      //Change the settings of the terminal back to what they were
      resetShell(oldSettings);
      return 0;
    }
    
    //Parse the input into the struct and store the number of commands parsed
    //The worst case is that the entire input is in one command, so allocate each command the same amount of memory as the input
    numCommands = parseCommands(input, &allCommands, BUFFERSIZE);

    //Execute the commands if there are any
    if(numCommands > 0)
      execute(&allCommands, numCommands);
  }

  //Change the settings of the terminal back to what they were
  resetShell(oldSettings);
  return 0;
}

//Change the shell to raw mode so that inputs get sent to the shell and not output to the screen
struct termios* rawShell()
{ 
  //Variables that hold the termios structs. Use the size of an unintialized termios struct to allocate memory to oldSettings
  struct termios newSettings;
  struct termios* oldSettings = malloc(sizeof(newSettings));
  tcgetattr(STDIN_FILENO, oldSettings);
  memcpy(&newSettings, oldSettings, sizeof(newSettings));
  //Add the options to make the terminal raw
  newSettings.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOKE | ICRNL);
  //Apply the settings
  tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);

  return oldSettings;
}

//Reset the previous state of the terminal
void resetShell(struct termios* oldSettings)
{
  tcsetattr(STDIN_FILENO, TCSANOW, oldSettings);
}

//Return the current directory. Helper function for getInput
char* getCurrDirectory()
{
  //Full working directory
  char* fullDirectory = malloc(4096);
  getcwd(fullDirectory, 4096);

  //Get the length of the working directory
  int fullLength = 0;

  while(fullDirectory[fullLength] != '\0')
    fullLength++;
  
  //Home directory
  char* homeDirectory = getenv("HOME");
  //Get the length of the home directory
  int homeLength = sizeof(homeDirectory);
  
  //If the home directory is a substring of the current directory, simplify the current directory
  if(!strncmp(fullDirectory, homeDirectory, homeLength) && fullLength >= homeLength)
  {
    //Working directory excluding the home direcory. It is the length of the full directory - home directory + ~
    char* simplifiedDirectory = malloc(fullLength - homeLength + 10);

    //Copy the second half of the full directory not including the home directory
    strncpy(simplifiedDirectory, fullDirectory + homeLength + 1, fullLength - homeLength);
    //Replace the first element with '~' to represent the home directory
    simplifiedDirectory[0] = '~';
    
    return simplifiedDirectory;
  }
  
  //If the home directory is not a substring, return the whole directory
  return fullDirectory;
}

void addStringArray(char** array, int targetIndex, char* item)
{
  int i = 0;
  
  //Keep iterating until i is at the end of the aray
  while(array[i] != NULL)
    i++;

  //Loop back trough the array and move each elemetn up one index
  for(; i > targetIndex; i--)
    array[i] = array[i-1];

  array[targetIndex] = item;
}

//Take an offset from the last column and move the cursor there
void moveCursor(struct winsize* window, int* newOffset, int* totalLength)
{
  //Calclate the position of the cursor given its offset, the length of the command, and the dimensiosn of the terminal
  int newX = *newOffset % window->ws_col;
  int newY = window->ws_row - ((*totalLength / window->ws_col) - (*newOffset / window->ws_col));
  
  printf("\033[%d;%dH", newY, newX + 1);
}

//Move the cursor left on the screen
void left(struct winsize* window, int* cursorOffset, int* totalLength)
{
  (*cursorOffset)--;
  moveCursor(window, cursorOffset, totalLength);
}

//Move the cursor right on the screen
void right(struct winsize* window, int* cursorOffset, int* totalLength)
{
  (*cursorOffset)++;
  moveCursor(window, cursorOffset, totalLength);
}

//Not implemented yet
void backspace(int n, struct winsize* window, int* cursorOffset, int* totalLength)
{
}

//Not implemented yet
void delete(int n, struct winsize* window, int* cursorOffset, int* totalLength)
{
}

//Writes a string to the terminal and overwrites the existing text
void overWrite (char* string, int overWriteSize)
{
  //Clear the buffer
  fflush(stdout);
  
  int i = 0;

  //Print the string
  while(string[i] != '\0')
  {
    printf("%c", string[i]);
    i++;
  }

  //If the string doesn't clear the terminal, print spaces
  while(i < overWriteSize)
  {
    printf(" ");
    i++;
  }
}

//Add a command stored in a string the history data structure
void addHistory(struct history* commandHistory, char* command, int maxSize)
{
  commandHistory->history[commandHistory->totalItems] = malloc(maxSize);
  strcpy(commandHistory->history[commandHistory->totalItems], command);
  (commandHistory->totalItems)++;
}

//Prompt the user for an input and store it in the given buffer. If the return value is 0, the user wants to exit
int getInput(char* prompt, char* input, int maxSize, struct history* commandHistory)
{
  //Character that stores each key input
  char keyPressed;
  //Iterate through the array
  int i = 0;
  //Iterate through history array
  int historyIndex = commandHistory->totalItems;

  //Store the cursor's offset from the beginning of the prompt
  int cursorOffset = 0;
  int cursorMin = 0;
  int cursorMax = 0;
  //Store the dimensions of the terminal window
  struct winsize window;

  //Print the full prompt
  char* fullPrompt = malloc(5000);
  sprintf(fullPrompt, "%s%s%s%s%s >> ", GREEN, PROMPT, BLUE, getCurrDirectory(), RESETCOLOR);
  printf("%s", fullPrompt);

  //Iterate through the array to determine how long it is
  while(fullPrompt[cursorMin] != '\0')
    cursorMin++;

  //Account for the color characters in the string
  cursorMin -= 14;
  
  cursorMax = cursorMin;
  cursorOffset = cursorMin;
  
  //Keep reading inputs from the user while it is not a new line and add the character to the input array
  while((keyPressed = getchar()) != 10)
  {
    //Update the window size
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window);
    
    //If the user enters "CTR+D", return false
    if(keyPressed == 4)
    {
      printf("\n");
      return false;
    }
    //If the user enters a control character
    else if(keyPressed == 27)
    {
      //Ignore the second input
      getchar();
      //Get the third one
      keyPressed = getchar();

      //If the user presses up, dispay the previous command
      if(keyPressed == 65)
      {
	if(historyIndex > 0)
	{
	  historyIndex--;
	  
	  cursorOffset = cursorMin;
	  moveCursor(&window, &cursorOffset, &cursorMax);
	  
	  char* currentHistory = commandHistory->history[historyIndex];
	  overWrite(currentHistory, cursorMax - cursorMin);

	  i = 0;
	  
	  while(currentHistory[i] != '\0')
	    i++;

	  cursorMax = cursorMin + i;
	  cursorOffset = cursorMax;
	  moveCursor(&window, &cursorOffset, &cursorMax);

	  strcpy(input, currentHistory);
	}
      }
      //If the user presses down, display the next command
      else if(keyPressed == 66)
      {
  	if(historyIndex < commandHistory->totalItems - 1)
	{
	  historyIndex++;
	  
	  cursorOffset = cursorMin;
	  moveCursor(&window, &cursorOffset, &cursorMax);
	  
	  char* currentHistory = commandHistory->history[historyIndex];
	  overWrite(currentHistory, cursorMax - cursorMin);

	  i = 0;
	  
	  while(currentHistory[i] != '\0')
	    i++;

	  cursorMax = cursorMin + i;
	  cursorOffset = cursorMax;
	  moveCursor(&window, &cursorOffset, &cursorMax);

	  strcpy(input, currentHistory);
	}
	else
	{
	  cursorOffset = cursorMin;
	  moveCursor(&window, &cursorOffset, &cursorMax);
	  overWrite("", cursorMax - cursorMin);
	  moveCursor(&window, &cursorOffset, &cursorMax);
	}
      }
      //If the user presses right, move the cursor right
      else if(keyPressed == 67)
      {
	if(cursorOffset < cursorMax)
	{
	  i++;
	  right(&window, &cursorOffset, &cursorMax);
	}
      }
      //If the user presses left, move the cursor left
      else
      {
	if(cursorOffset > cursorMin)
	{
	  i--;
	  left(&window, &cursorOffset, &cursorMax);
	}
      }
    }
    //If the user hits backspace, do nothing
    else if(keyPressed == 127)
    {
    }
    //If the key pressed is a valid character, append it to the input string
    else if(keyPressed > 31)
    {
      //Add the character to the first open index
      input[i] = keyPressed;
      //Print the character just typed
      printf("%c", keyPressed);
      
      i++;
      cursorMax++;
      cursorOffset++;
    }
  }
  
  printf("\n");
  
  //If the user enters "exit", return false
  if(!strcmp(input, "exit"))
    return false;

  //After the command is processes, add it to the history
  addHistory(commandHistory, input, maxSize);
  
  return true;
}

//Take a string of commands and arguments and parse it into a command struct
int parseCommands(char* string, struct command* allCommands, int commandSize)
{
    //Integers to iterate through the 2D array
    int command = 0;
    int argument = 1;
    //Delimiters to split the input string
    const char* delims = " ";
    //Store the current token being processed
    char* token;
    
    //Create the first row and set its first index to the first token.
    allCommands->commandTable[0] = malloc(commandSize);
    allCommands->commandTable[0][0] = strtok(string, delims);
    
    //While there are tokens left, keep looping and populate the array with the tokens
    while((token = strtok(NULL, delims)) != NULL)
    {
      //If the current token is a new pipe symbol, create a new array and start counting from the beginning of that array
      if(!strcmp(token, "|"))
      {
	command++;
	argument = 1;
	allCommands->commandTable[command] = malloc(commandSize);
	allCommands->commandTable[command][0] = strtok(NULL, delims);
      }
      //If an input file is specified, store it in the struct
      else if(!strcmp(token, "<"))
	allCommands->inputFile = strtok(NULL, delims);
      //If an output file is specified, store it in the struct
      else if(!strcmp(token, ">"))
	allCommands->outputFile = strtok(NULL, delims);
      //If an output file is specified in append mode, store it in the struct and toggle the append boolean
      else if(!strcmp(token, ">>"))
      {
	allCommands->outputFile = strtok(NULL, delims);
	allCommands->append = true;
      }
      //Otherwise, add the token to the array
      else
      {
	//If the token starts with a quote, keep adding tokens to it until the end quote is found
        if(token[0] == '\"' || token[0] == '\'')
	{
	  char quote = token[0];
	  
	  //Move each char back an element and overwrite the beginning quote
	  for(int i = 0; token[i] != '\0'; i++)
	    token[i] = token[i+1];

	  //Keep adding the next token until the end quote is found
	  //If the token starts with a double quote, search for another double quote
	  if(quote == '\"')
	    while(token[strlen(token)-1] != '\"')
	      sprintf(token, "%s %s", token, strtok(NULL, delims));
	  //If the token starts with a single quote, search for another single quote
	  else
	    while(token[strlen(token)-1] != '\'')
	      sprintf(token, "%s %s", token, strtok(NULL, delims));

	  //Remove the last quote
	  token[strlen(token)-1] = '\0';
	}
	
        allCommands->commandTable[command][argument] = token;
	argument++;
      }
    }

    if(allCommands->commandTable[0][0] == NULL)
      return 0;
    
    //If the last argument is '&', toggle the background boolean in the struct
    if(argument > 0 && !strcmp(allCommands->commandTable[command][argument-1], "&"))
    {
      allCommands->background = true;
      //Remove '&' from the command
      allCommands->commandTable[command][argument-1] = 0;
    }

    //If the last command is ls or grep, add the option to enable color for commands
    char* firstCommand = allCommands->commandTable[command][0];
    
    if(!strcmp(firstCommand, "ls") || !strcmp(firstCommand, "grep"))
    {
      //Set the second element to the color option
      addStringArray(allCommands->commandTable[command], 1, "--color=auto");
    }
    
    //Return the number of commands in the array
    return command + 1;
}

/*Built-In shell commands*/

//Print the directory the shell is currently in
void printWorkingDirectory()
{
  //Create a string to store the file path. The maximum filepath length is 4096 byte.
  char* input = malloc(4096);
  getcwd(input, 4096);
  printf("%s\n", input);
}

//Change the directory the shell is currently in
void changeDirectory(char* destination)
{
  chdir(destination);
}

void redirectFD(int newDest, int oldDest)
{
  dup2(newDest, oldDest);
  close(newDest);
}

void createPipe(int* readEnd, int* writeEnd)
{
  //Create a new pipe
  int newPipe[2];
  pipe(newPipe);

  //Store the read and write ends of the pipe
  *readEnd = newPipe[0];
  *writeEnd = newPipe[1];
}

//Execute the command. General algorithm came from:
//https://www.cs.purdue.edu/homes/grr/SystemsProgrammingBook/Book/Chapter5-WritingYourOwnShell.pdf
void execute(struct command* allCommands, int numCommands)
{
  //Store the defualt input and outfile locations
  int defaultIn = dup(STDIN_FILENO);
  int defaultOut = dup(STDOUT_FILENO);
  //Store the input and output locations for each process
  int input;
  int output;
  //Hold the process id of the child process
  int childProcess;

  //If there is no input file stored in the struct, use the default input
  if(allCommands->inputFile != NULL)
    input = open(allCommands->inputFile, O_RDONLY);
  //Otherwise, store its file id in the 'input' input
  else
    input = dup(defaultIn);

  //Loop through all of the commands in the struct
  for(int i = 0; i < numCommands; i++)
  {
    //If this is the first process, direct the input to the location determined above
    //If not, direct the input to the location inherited from the previous iteration
    redirectFD(input, STDIN_FILENO);

    //If the current command isn't the last, create a pipe. This process will write to the pipe and the child will inheir 'input'
    if(i != numCommands - 1)
      createPipe(&input, &output);
    //If the current command is the last command, determine where the output should go
    else
    {
      //If there is no output file stored in the strut, set the output to the default output location
      if(allCommands->outputFile == NULL)
	output = dup(defaultOut);
      //If there is an output file specified and the append boolean is true, open the file with the append flag
      else if(allCommands->append)
	output = open(allCommands->outputFile, O_CREAT | O_WRONLY | O_APPEND, 0666);
      //If there is an output file specifid and the append boolean is flase, open the file with the truncate flag
      else
	output = open(allCommands->outputFile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    }

    //After the output location is determined (default location, a file, or a pipe), redirect the program's output to that location
    redirectFD(output, STDOUT_FILENO);
    
    //If the current command is 'pwd', run the built in pwd function
    if(!strcmp(allCommands->commandTable[i][0], "pwd"))
      printWorkingDirectory();
    //If the current command is 'cd', run the built in cd function
    else if(!strcmp(allCommands->commandTable[i][0], "cd"))
      changeDirectory(allCommands->commandTable[i][1]);
    //If the command is not built into the shell, create a child process, find the program, and execute it
    else
    {
      //Create a child process
      childProcess = fork();
      
      //If the current process is the child, execute the command
      if(!childProcess)
      {
        execvp(allCommands->commandTable[i][0], allCommands->commandTable[i]);
        perror("");
        exit(1);
      }
    }
  }

  //After all of the commands are run, reset the defaut input and output files
  redirectFD(defaultIn, STDIN_FILENO);
  redirectFD(defaultOut, STDOUT_FILENO);

  //If the background boolean is not set in the struct, wait for the last process to finish before returning
  if(!allCommands->background)
    waitpid(childProcess, NULL, 0);
}




// **********************      //
// **** first work well ****** //

// #include <string.h>
// #include <unistd.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <fcntl.h>

// #define BUFFERSIZE 256
// #define PROMPT "myShell >> "
// #define PROMPTSIZE sizeof(PROMPT)
// #define ARGVMAX 64
// #define PIPECNTMAX 10 // only we need one pipe but if more we can create more
// #define READ 0 // stdin for pipe
// #define WRITE 1 // stdout for pipe
// // #define READ1 2
// // #define WRITE 3

// int main(int argc, char** argv) {

//     char buf[BUFFERSIZE];
//     char *token;
//     char *myargv[ARGVMAX];
//     char *argvpipe[ARGVMAX];
//     char *directory;
//     int myargc;
//     pid_t childPid; 
//     pid_t pipeChild;   
//     // int fd; // creating the pipes

//     // pipe(fd); // fd[0] -> reading fd[1] -> writing fd[2] -> append -> fd[3] -> f[4]

//     while (1) {
//         int k = 0;
//         int fd, byte_length;
//         // int fd1[2];

        

//         printf("%s", PROMPT);
// 		// myargv[BUFFERSIZE + 1] = (char**) malloc(sizeof(int*)*256);        
//         // fflush(stdout); // just to know that I'm writing to the file
//         // gets(buf);
//         memset(buf, '\0', BUFFERSIZE);
//         for (int i=0; i < ARGVMAX; i++) {
//             myargv[i] = NULL;
//         }
//         if (fgets(buf, BUFFERSIZE, stdin) == NULL) {  // Ctrl + D
//             break;
//         } 
//         // printf("$PATH %s", buf); // I guess for testing purposes 

//         token = strtok(buf, " \t\n"); // tokenize the arguments
//         if (token == NULL) {
//             continue;
//         } 
        
//         if (strcmp(buf, "exit") == 0 ) {
//             return 0;
//         }
//         // token = strtok(buf, " \n"); // tokenize the arguments
//         while ( token != NULL ) {
//             myargv[k] = token;
//             // token = strtok(NULL, " ");
//             // k++;
//             token = strtok(NULL, " \n");
//             k++; // go to next element
            
//         }
//         // begins the processID
//         int fd1[2];
//         childPid = fork();
//         // pipeChild = fork();
//         pipe(fd1);

//         // tried to do the piping process 

//         // child process 1
//         // if ( pipeChild  > 0) {
//         //     dup2(fd1[READ], READ);
//         //     close(fd1[WRITE]);

//         //     execvp(argvpipe[0], argvpipe);
//         //     perror("execvp failed");

//         // } else if ( (pipeChild == fork()) == 0) {
//         //     dup2(fd1[WRITE], WRITE);

//         //     close(fd1[READ]);
//         //     execvp(argvpipe[0], argvpipe);
//         // } else {
//         //     waitpid(pipeChild, NULL, 0);
//         // }


//         if ( childPid < 0 ) {
            
//             perror("failed to fork");
//             exit(1);
//         } else if ( childPid > 0 ) { // Parent Process
//             // close(fd1[READ]); // closing the first file descriptor
//             // dup2(fd1[WRITE], 1); // copying fd[write] 
//             // close(fd1[WRITE]); // end file descriptor;

//             // if (execlp(myargv[1], myargv[1], NULL) == -1) {
//             //     perror("error execlp");
//             // }  
//             // Background Processing 
//             if(strcmp(myargv[k-1], "&") == 0) {
//                 printf("Background process detected don't wait\n");
//             }

//             int status, pid;
//             pid = wait(&status);
//             printf("pid %d exit status %d\n", pid, status);
//             // return 0;    
//             sleep(1); // to givd more time for the child
//             // wait(NULL);
//         }

//         else if ( childPid == 0 ) {

//             // close(pipe[0]); // file 1 
//             // int fd;
//             // int in, out, append, change_directory = 1;
//             if ( k > 2 ) {

//                 // if (in) {

//                     // if ((fd[0] = open(myargv[k-2], O_RDONLY, 0)) < 0) {
//                     //     perror("fd[0]->");
//                     //     exit(0);
//                     // }b

//                 if (strcmp(myargv[k-2], "<") == 0) { 

//                     fd = open(myargv[k-1], O_RDONLY, 0);
//                     dup2(fd, 0);
//                 // in = 0;
//                     close(fd); 
                    
//                 }

//                     // in = 1; // return successful if completed
            
//                 // }

//                 // if (out) {
//                 if (strcmp(myargv[k-2], ">") == 0) {

//                     fd= open(myargv[k-1], O_CREAT | O_TRUNC | O_WRONLY, 0600);
//                     dup2(fd, STDOUT_FILENO);
//                     close(fd);
//                 }

//                     // out = 1; // returns succesful if completed
//                 // }
//                     // execvp(myargv[0], myargv);

//                 // if (append) {
//                 if (strcmp(myargv[k-2], ">>") == 0) {

//                     fd = open(myargv[k-1], O_RDWR | O_CREAT| O_APPEND, 0644);
//                     dup2(fd, STDOUT_FILENO);
//                     close(fd);
                    
//                 }

//                 // if (strcmp(myargv[k-2], "|") == 0) {
//                 //     int write_passed = 
//                 //     fd = open(myargv[k-1], )
//                 // }


//             }

//             // piping process ending 

//             // closing the pipes in the childer header

//             // close(fd1[WRITE]);
//             // dup2(fd1[READ], 0);
//             // close(fd1[READ]); // closing the read descriptor
//             // execute the reading command
//             // if (execlp(argv[2], argv[2], NULL) ==  -1) { // Execute the reader passed arg 
//             //     perror("connect");
//             // }
            

//             // pwd

//             if (strcmp("pwd", myargv[0]) == 0) {
//                 char pwd[BUFFERSIZE];
//                 if (getcwd(pwd, BUFFERSIZE) != NULL) {
//                     printf("%s\n", pwd);
//                 } else {
//                     printf("pwd failed");
//                 }
//                 continue;
//             }


//                         // Cd 
//             if (strcmp(myargv[0], "cd") == 0) {
//                 char *directory = myargv[1];
//                 int expected;
//                 expected = chdir(directory);
//                 if (expected == -1) {
//                     perror("Directory failed");
//                 }
//             } 
//                 // printf("\n");

//                 if (k > 1 && strcmp(myargv[k-1], "&") == 0) {
//                     myargv[k-1] = NULL;
//                 } else if (k >2 && strcmp(myargv[k-2], "<") == 0) {
//                     // Delete last two arguments for < case
//                     myargv[k-2] = NULL;
//                     myargv[k-1] = NULL;
//                 } else if (k > 2 && strcmp(myargv[k-2], ">") == 0) {
//                     // Delete last two arguments for > case
//                     myargv[k-2] = NULL;
//                     myargv[k-1] = NULL;
//                 } else if (k > 2 && strcmp(myargv[k-2], ">>") == 0){
//                     myargv[k-2] = NULL;
//                     myargv[k-1] = NULL;
//                 } else if (k > 2 && strcmp(myargv[k-2], "|") == 0) {
//                     myargv[k-2] = NULL;
//                     myargv[k-1] = NULL;
//                 }

//                 execvp(myargv[0], myargv);
//                 // perror("execvp");
//                 // _exit(1);

//                 // if (strcmp(myargv[k-2], "exit")) {
//                 //     break;
//                 // }

//             // if (execvp(buf, myargv) == -1) {
//             //     perror(buf);
//             //     return -1;
//             // }
//         }

//     }

// 	return 1;

// }



//  *********************************  //




// #include <string.h>
// #include <unistd.h>
// #include <stdlib.h>
// #include <stdio.h>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <sys/wait.h>
// #include <fcntl.h>
// #include <dirent.h>
// #include <termios.h>

// /* CANNOT BE CHANGED */
// #define BUFFERSIZE 256
// /* --------------------*/
// #define PROMPT "myShell >> "
// #define PROMPTSIZE sizeof(PROMPT)

// // #define DELIMS " \t\n"
// // #define FOREVER for(;;)

// // function prototypes
// //command execution controller.
// void execute_commands(char *[], int, int);
// // index of args if found in the array myargv
// // int arg_found(char *[], char*, int);
// int arg_found(char *[], char*, int);
// // execute command in the background processes
// void bg_process(char *[], int);
// void first_redirect_output(char *[], int);
// void second_redirect_output(char *[], int); 
// void redirect_input(char *[], int);
// void single_piped_command(char *[], int);
// void execute(char *[], int);
// void pwd();
// void change_dir();
// void ls();


// int 
// main(int argc, char** argv)
// {
//     //string lines
//     char *str_line = NULL;
//     ssize_t bytes_read;
//     size_t len = 0;
//     const char delims[1] = "";
//     // const char delims[32] = " ";
//     char *token;
//     //we stored parsed substrings
//     char *myargv[BUFFERSIZE];
//     //count of the num of arguments stored in the myargv array
//     int myargc;
//     // char *arg_vector[32];

//     printf("%s," PROMPT);
    
//     while (getline(&str_line, &len, stdin)!= -1)
//     {
//         //to reset
//         myargc = 0;
//         str_line[strlen(str_line)-1] = '\0';
//         token = strtok(str_line, delims);

//         while (token != NULL)
//         {
//             //to store array of token in myargv array
//             myargv[myargc++] = token;
//             token = strtok(NULL, delims);
//         }

//         if (myargc > 0)
//         {
//             if (strcmp(myargv[0], "exit") == 0)
//             {
//                 exit(-1);
//             } else
//             {
//                 execute_commands(myargv, myargc, 0);
//             }
//         }
//         printf("%s", PROMPT);
//     }
    
//     return 0;
// }

// void execute_commands(char *myargv[], int myargc, int bg){
//     if (strcmp(myargv[0], "pwd") == 0)
//     {
//         pwd();
//     } 
//     else if(arg_found(myargv, ">", myargc)>= 0)
//     {
//         first_redirect_output(myargv, myargc);
//     }
//      else if (arg_found(myargv, ">>", myargc) >= 0) {
//         // redirect the second standard output of a command to a file
//         second_redirect_output(myargv, myargc);

//         // #4 condition
//     } else if (arg_found(myargv, "<", myargc) >= 0) {
//         // redirect the standard input of a command to come from a file
//         redirect_input(myargv, myargc);
//     }
//     else if(arg_found(myargv, "|", myargc) >= 0) 
//     {
//         // execute multiple commands connected by a single shell pipe
//         single_piped_command(myargv, myargc);
//     }
//     else if (arg_found(myargv, "&", myargc) >= 0) {
//         // execute a command in background
//         bg_process(myargv, myargc);

//         // #2 condition
//     }
//     else if (strcmp(myargv[0], "cd") == 0) {
//         // change directory
//         change_dir(myargv[1]);
//     }
//     else if ((strcmp(myargv[0], "ls") == 0)&&(myargc==1)) {
//         // change directory
//         ls(myargc, myargv);
//     }
//     else
//     {
//         execute(myargv, bg);
//     }
        
// }

// // ***  condition 2   *** //
// void execute(char *myargv[], int bg){
//     pid_t pid = fork();
//     if (bg == 0)
//     {
//         if (bg)
//         {
//             setpgid(pid, 0);
//         }
//         execvp(myargv[0], myargv);
//     }
//     else if (pid > 0)
//     {
//         wait(NULL);
//     }
//     else
//     {
//         perror("The creation of a child process was not successful");
//     }
// }

// int arg_found(char *myargv[], char *arg, int myargc){
//     int index = 0;
//     while (myargc > index)
//     {
//         if (strcmp(myargv[index], arg) == 0)
//         {
//             return index;
//         }
//         index++;
//     }
//     return(-1);
    
// }

// void by_process(char *myargv[], int myargc){
//     int index_of_ampersand = arg_found(myargv, "&", myargc);
//     if (index_of_ampersand != myargc -1)
//     {
//         perror("'&' is not found at the ene of the command\n");
//         exit(EXIT_FAILURE);
//     }
//     else
//     {
//         myargv[myargc - 1] = NULL;
//         myargc--;
//         execute_commands(myargv, myargc, 1);
//     }
// }

// //first redirect output() to redirect standard output of cmd file
// void first_redirect_output(char *myargv[], int myargc){
//     int index = arg_found(myargv, ">", myargc);

//     if (index != myargc -2 || myargc < 3){
//         perror("Standard output redirection error. '>' is not in proper position.\n");
//         exit(EXIT_FAILURE);
//     }
//     char *left_side_arg[index+1];
//     int i = 0;
//     while (index > i)
//     {
//         left_side_arg[i] = myargv[i];
//         i++;
//     }
//     left_side_arg[index] = NULL;
//     int out = dup(STDOUT_FILENO);

//     int new_fd = open(myargv[myargc -1], O_RDWR | O_CREAT | O_TRUNC, 0644);
//     if (new_fd < 0)
//     {
//         perror("error opening file.");
//         exit(EXIT_FAILURE);
//     }
//     else
//     {
//         dup2(new_fd, STDOUT_FILENO);
//         execute_commands(left_side_arg, index, 0);
//         close(new_fd);
//         dup2(out, STDOUT_FILENO);
//     }
// }

// void second_redirect_output(char *myargv[], int myargc){
//     int index = arg_found(myargv, ">>", myargc);
//     if (index != myargc - 2 || myargc < 3){
//         perror("Standard output redirection error '>>' is not proper position.\n");
//         exit(-1);
//     }
//     char *left_side_arg[index +1];
//     int i = 0;
//     while (index > i)
//     {
//         left_side_arg[i] = myargv[i];
//         i++;
//     }

//     left_side_arg[index] = NULL;
//     int out = dup(STDOUT_FILENO);

//     int new_fd = open(myargv[myargc -1], O_RDWR|O_CREAT|O_APPEND, 0644);
//     if (new_fd < 0){
//         perror("Error openning file.");
//         exit(-1);
//     }else 
//     {
//         dup2(new_fd, STDOUT_FILENO);
//         execute_commands(left_side_arg, index, 0);
//         close(new_fd);
//         dup2(out, STDOUT_FILENO);
//     }
// } // end second_redirect_output()

// void redirect_input(char *myargv[], int myargc){
//     int index = arg_found(myargv, "<", myargc);

//     if (index != myargc -2 || myargc < 3)
//     {
//         perror("The standard input rediretion error... '<' is not in proper position.\n");
//         exit(-1);
//     }
//     char *left_side_arg[index + 1];
//     int i = 0;
//     while (index > i) {
//         left_side_arg[i] = myargv[i];
//         i++;
//     }
//     left_side_arg[index] = NULL;

//     int new_fd = open(myargv[myargc - 1], O_RDONLY);
//     if (new_fd < 0)
//     {
//         perror("Error opening file....");
//         exit(-1);
//     }else
//     {
//         dup2(new_fd, STDIN_FILENO);
//         execute_commands(left_side_arg, index, 0);
//         close(new_fd);
//     }
// }



// // execute multiple commands connected by a single shell pipe
// void single_piped_command(char *myargv[], int myargc) {

//     int fspipe[2];
//     char* token;
//     char *pipe_strings1 = NULL;
//     char *pipe_strings2 = NULL ;
//     printf("hello");
//     sleep(0.1);
//     fflush(stdout);
//     fflush(stdin);
//     pipe(fspipe);
//     pid_t pid1 = fork();
//     if (pid1 == (pid_t) 0)
//     {

//         //tokens for LHS of |
//         token = strtok(pipe_strings1, " ");
        
//         while(token != NULL)
//         {
//             myargv = realloc(myargv, sizeof(char *) * ++myargc);
//             if(myargv == NULL)
//             {
//                 exit(-1);//Memory allocation failed
//                 printf("Memory allocation failed");
//             }
            
//             myargv[myargc - 1] = token;
//             token = strtok(NULL, " ");
//         }
        
//         myargv = realloc(myargv, sizeof(char *) * (myargc+1));
//         myargv[myargc] = 0;

//         close(fspipe[0]);
//         dup2(fspipe[1], 1);
//         close(fspipe[1]);
        
//         execvp(&*myargv[0], myargv);
//         exit(0);
    
//     }
//     else if (pid1 > (pid_t) 0)
//     {
//         free(myargv);
//         fflush(stdout);
//         fflush(stdin);
//         pid_t pid2 = fork();
//         if (pid2 == (pid_t) 0)
//         {
//             //tokens for RHS of |
//             token = strtok(pipe_strings2, " ");
            
//             while(token != NULL){
//             myargv = realloc(myargv, sizeof(char *) * ++myargc);
//             if(myargv == NULL){
//             exit(-1);//Memory allocation failed
//             printf("Memory allocation failed");
//             }
            
//             myargv[myargc - 1] = token;
//             token = strtok(NULL, " ");
//             }
            
//             myargv = realloc(myargv, sizeof(char *) * (myargc+1));
//             myargv[myargc] = 0;

//             close(fspipe[1]);
//             dup2(fspipe[0], 0);
//             close(fspipe[0]);
            
//             if(execvp(&*myargv[0], myargv) < 0){
//             perror("execvp ");
//             exit(-5);
//             }
//             exit(0);
//         }
//         else if (pid2 > (pid_t) 0){
//             close(fspipe[0]);
//             close(fspipe[1]);
//             wait(NULL);
            
//             wait(NULL);
//         }
//         else{
//         perror("pid2");
//         }
//     }
//     else{

//         perror("pid1 ");
//     }
// }

// // pwd() function prints working directory
// void pwd() 
// {
//     char path_name[BUFFERSIZE];

//     if (getcwd(path_name, sizeof(path_name)) != NULL) 
//     {
//         printf("%s\n", path_name);
//     } else 
//     {
//         perror("Error printing working directory...\n");
//         exit(EXIT_FAILURE);
//     } // end if-else

// } // end pwd()


// // #10
// // change_dir() function changes directory
// void change_dir(const char *path_name) 
// {

//     if (chdir(path_name) != 0) 
//     {
//         perror("Error changing directory...\n");
//         exit(EXIT_FAILURE);
//     }
// } // end change_dir()


// // ls()
// void ls(int argc, char *argv[]) 
// {

//     struct dirent **namelist;
//     int i, n;

//     if(argc < 1)
//     {
//         exit(EXIT_FAILURE);
//     }
//     else if (argc == 1)
//     {
//         n = scandir(".", &namelist, NULL, alphasort());
//     }
//     else {
//         n = scandir(argv[1], &namelist, NULL, alphasort());
//     }
//     if(n < 0) {
//         perror("scandir");
//         exit(EXIT_FAILURE);
//     }
//     else if(n==2)
//     {
//     printf("The directory is empty\n");
//     }
//     else {
//     while (n--)
//     {
//     if(n>2){
//     printf("%s\n",namelist[n]->d_name);
//     }
//     free(namelist[n]);


//     } free(namelist);
//     }

// }