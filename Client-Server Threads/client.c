#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>//This header file contains definitions of a number of function and datatypes used in signals
#include <sys/types.h>//This header file contains definitions of a number of data types used in system calls
#include <sys/socket.h>//The header file socket.h includes a number of definitions of structures needed for sockets
#include <string.h>
#include <sys/wait.h>
#include <netdb.h> // The file netdb.h defines the structure hostent, which will be used below.
#include <ctype.h>
#include <arpa/inet.h>

#define SHM_SIZE 1024  /* make it a 1K shared memory segment */

#define GOLD 0
#define ARMOR 1
#define AMMO 2
#define LUMBER 3
#define MAGIC 4
#define ROCK 5
#define BADKEY 6

#define PORT "5005"
#define NKEYS (sizeof(lookuptable)/sizeof(t_symstruct))


/*Handler declerations */
void sigchld_handler(int signo);
void sigint_handler(int signo);
//void sigchld_handler(int signo);

int sockfd,  cnt, i,len;
pid_t parent_pid, child_pid; //process that will block on recv
struct sockaddr_in serv_addr; //serv_addr will contain the address of the server to which we want to connect.

//The variable server is a pointer to a structure of type hostent. This structure is defined in the header file netdb.h
struct hostent *server;
int inventory[6]; //This will contain the values of a chosen item, if the item has not be chosen zero will be placed instead
int quota;
char c,*fileName, *server_name,*player_name, message[SHM_SIZE], item_name[30];

FILE *fp; //Pointer to file inventory
void *__gxx_personality_v0;

typedef struct { const char *key; int val; } t_symstruct;

char buf[80];
char serv_msg[80];
/*t_symstruct lookuptable[] = {
    { "gold", GOLD }, { "armor", ARMOR }, { "ammo", AMMO }, { "lumber", LUMBER }, { "magic", MAGIC }, { "rock", ROCK },
};
t_symstruct *sym;
int keyfromstring(char *key); */

int main( int argc, char *argv[] )
{
parent_pid = getpid();
/*Check if argv is passed as argument and if it exists store it on server_name, if not terminate*/
if (argc != 6)
   {
      printf("Wrong number of arguments %d\n", argc);
      exit(1);
   }
else
   {
      server_name = (char*)malloc((strlen(argv[5]))*sizeof (char));
      server_name = strcpy(server_name,argv[5]);
   }
//getopt is explained thoroughly in server's code
while ( (c = getopt(argc, argv, "n:i:"))!= -1 )
{
  switch(c)
    {
   case 'n':
         player_name = (char*)malloc((strlen(optarg))*sizeof (char));
         player_name = strcpy(player_name,optarg);
         break;
   case 'i':
       fileName = (char*)malloc((strlen(optarg))*sizeof (char));
       fileName = optarg;
       break;
   case '?':
         if (isprint (optopt))
           fprintf (stderr, "Unknown option `-%c'.\n", optopt);
         else
           fprintf (stderr,
                    "Unknown option character `\\x%x'.\n",
                    optopt);
         return 1;
    }
}



char cwd[80]; //cwd has the current working dirrectory
memset(cwd, 0, sizeof(cwd));
char ch;
int k=0,flag=0; //k is used for an index on item_name array, flag is for when we finished reading an item's name,
getcwd(cwd, sizeof(cwd));
cwd[strlen(cwd)] = '/'; //Put '/' at the end of cwd
strcat(cwd, fileName); //Append file name to cwd
printf("%s\n" , cwd);
fp = fopen(cwd , "r"); // open file in read mode
if( fp == NULL )
{
	perror("Error while opening the file.\n");
	exit(EXIT_FAILURE);
}

strcpy(message, player_name); //Top of the message contains the name
message[strlen(message)] = '\n'; //replace \0 with \n
len = strlen(message); //store index so far
i=0;
char temp[5]; //storing quota as characters

while( ( ch = fgetc(fp) ) != EOF )
{
    if( ch == '\t')  //If we see a tab character it means the next character will have a a value, set flag and start loop
    {
        flag = 1;
        k=0; //item name read, reset for the next item name
        continue;
    }
    if(flag == 1 && ch != '\n' && ch != '\r') //Every character we will read from now on will be a digit of the value,store it on temp[], when \n is seen break.
        {
           temp[k] = ch;
            k++;
            continue;
        }
    if (ch == '\n') //We saw a new line character, process data so far (item name and quota)
        {
            flag = 0;
            quota = atoi(temp); //Make an integer out of the temp string, which has the item value
            item_name[strlen(item_name)] = '\0';

            /*Store the value quota read on the player's inentory
                according the item_name that was read */
            if (strcmp(item_name, "gold")==0)
               inventory[GOLD] = quota;
            else if (strcmp(item_name, "armor") == 0)
                inventory[ARMOR] = quota;
            else if (strcmp(item_name, "ammo") == 0)
                inventory[AMMO] = quota;
            else if (strcmp(item_name, "lumber") == 0)
                inventory[LUMBER] = quota;
            else if (strcmp(item_name, "magic") == 0 )
                inventory[MAGIC] = quota;
            else if (strcmp(item_name, "rock") == 0)
                inventory[ROCK] = quota;

        /* zero values */
        quota= 0;
        memset(item_name, 0 , sizeof (item_name));
        memset(temp, 0 , sizeof (temp));
        i=0;
        continue;
        }

    item_name[i] = ch; //If nothing of the above occures, the character is part of the item's name
    i++;

}




fclose(fp); //close file


fp = fopen(cwd , "r"); // reopen file in read mode
if( fp == NULL )
{
	perror("Error while opening the file.\n");
	exit(EXIT_FAILURE);
}
/* Read whole message to a buffer (after the name) */
while( ( ch = fgetc(fp) ) != EOF )
{
   message[len] = ch;
   len++;
}
message[len]='\0';
fclose(fp);
puts(message);






/*The socket() system call creates a new socket. It takes three arguments. The first is the address domain of the socket.
* We use AF_INET for internet domain for connection of any two hosts on the Internet
* The second argument is the type of socket. We use SOCK_STREAM for stream socket that uses TCP protocol.
* The third argument is the protocol. The zero argument will leave the operating system to choose the most appropriate protocol. It will choose TCP for stream sockets */
sockfd = socket(AF_INET, SOCK_STREAM, 0);
if (sockfd < 0)
{
printf("client socket failure %d\n", errno);
perror("client: ");
exit(1);
}


struct addrinfo hints, *serverinfo; //for getaddrinfo()
memset(&hints, 0 , sizeof hints);
hints.ai_family = AF_UNSPEC; // use AF_INET6 to force IPv6
hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
hints.ai_flags = AI_PASSIVE; // use my IP address


/* int getaddrinfo(const char *node,     // e.g. "www.example.com" or IP
                const char *service,  // e.g. "http" or port number
                const struct addrinfo *hints, // hints parameter points to a struct addrinfo filled above
                struct addrinfo **res) // will point to the results */
int rv;
if ((rv = getaddrinfo(server_name, PORT , &hints, &serverinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    exit(1);
}
freeaddrinfo(serverinfo); // free the linked list

struct sockaddr_in *addr;
addr = (struct sockaddr_in *)serverinfo->ai_addr;
/*inet_nota converts from a struct in_addr (part of struct sockaddr_in)
to a string in dots-and-numbers format (e.g. "192.168.5.10") and vice-versa */
printf("Server's IP address is: %s\n",inet_ntoa((struct in_addr)addr->sin_addr));

/*Take a server's name as an argument and returns a pointer to a hostent containing information about that host.
If this structure is NULL, the system could not locate a host with this name. */
if (serverinfo == NULL)
{
  fprintf(stderr,"ERROR, no such host");
  exit(0);
}

/*The connect function is called by the client to establish a connection to the server.
It takes three arguments, the socket file descriptor, the address of the host to which
it wants to connect (including the port number), and the size of this address.
This function returns 0 on success and -1 if it fails.
*/
if (connect(sockfd,(struct sockaddr *)serverinfo->ai_addr ,serverinfo->ai_addrlen) < 0)
{
	perror("client connect failure: ");
	exit(1);
}
signal(SIGINT, sigint_handler); //Install handler for SIGINT ( also CTR+C)

child_pid = fork(); //Create a child proccess for recieving messages from server

while(1)
   {
   if (child_pid > 0)//Parent proccess
      {
            /*Send name and item info */
             if ((cnt = send(sockfd,message, strlen(message),0))== -1)
                {
                         fprintf(stderr, "Failure Sending Message\n");

                }
               //Read \n character and send it to server
               printf("Confirming connection to server:\n");
        //       ch = getchar(); //Uncomment when not run as a background process
               sleep(2);
               ch = '\n';
               if ((cnt = send(sockfd, &ch, sizeof(char),0))== -1)
                {
                         fprintf(stderr, "Failure Sending Message\n");

                }

                 char final_message[100]; //Buffer containing message to send
                 memset( final_message, 0 , sizeof(final_message));
               /* Place the server in an infinite loop,
               of reading strings with fgets and and sending to server */
                 while(1)
                  {
                     /* append [*name*] style before any message */
                       final_message[0] = '[';
                       strcat(final_message, player_name);
                       strcat(final_message , "]: ");
                  //    fgets(buf, sizeof(buf), stdin);
                      strcat(buf, "hello\n"); //Uncomment above line and comments this and the line bellow if you want to write messages manually
                      sleep(3);

                       strcat(final_message, buf); //append it to final message

                       if ((cnt = send(sockfd, final_message, strlen(final_message),0))== -1)
                      {
                               fprintf(stderr, "Failure Sending Message\n");

                      }
                      memset( final_message, 0 , sizeof(final_message));
                      memset( buf, 0, sizeof(buf));
                  }
            }
   else if (child_pid == 0) //Child process, recieves messages from server
           {
                   signal( SIGINT, SIG_IGN);
                   while(1)
                   {
                      cnt = recv(sockfd, serv_msg, sizeof(serv_msg),0);
                      if ( cnt == 0 ) //Server has closed their socket or explicitly disconnected us
                      {
                              printf("Disconnected from server\n");
                              close(sockfd);
                              kill(parent_pid, SIGINT); //Inform parent to terminate
                              sleep(100); //wait for parent to kill us from his sigint handler
                      }

                      serv_msg[cnt] = '\0';
                      printf("%s",serv_msg); //\n should be included in message
                      memset(serv_msg, '0', sizeof(serv_msg));
                   }
              }

   else if (child_pid < 0)
       {
           fprintf(stderr, "Fork Failed");
           exit(1);
      }
   }
}

/*This handler recieves SIGINT and terminates the main process plus the kid process if it exists*/

void sigint_handler(int signo)
{
 if (child_pid > 0 ) //If main thread created a
    {
      pid_t PID;
      kill(child_pid, SIGTERM); //Terminate recieving child process
      int status;

      do {
       PID = waitpid(-1,&status,WAIT_ANY); //Clean zombie child function, wait any children
       printf("Reciever terminated\n");
     }
     while ( PID != -1 );
   }
   close(sockfd); //close socket
   exit(1);
}




