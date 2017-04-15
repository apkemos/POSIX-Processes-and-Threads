#include <stdio.h>
#include <unistd.h> //Contains process types and functions
#include <stdlib.h>
#include <errno.h>
#include <signal.h>  //This header file contains definitions of a number of function and datatypes used in signals
#include <sys/types.h> //This header file contains definitions of a number of data types used in system calls
#include <sys/socket.h> //The header file socket.h includes a number of definitions of structures needed for sockets
#include <netinet/in.h> //The header file in.h contains constants and structures needed for internet domain addresses.
#include <string.h>
#include <sys/wait.h>
#include <ctype.h> //Contains isprint
#include <fcntl.h> //Contains flags for open()
//#include <sys/stat.h>
#include <sys/mman.h> //Contains memory management declarations
#include <semaphore.h> //The <semaphore.h> header shall define the sem_t type and semaphore related functions


#define GOLD 0
#define ARMOR 1
#define AMMO 2
#define LUMBER 3
#define MAGIC 4
#define ROCK 5

#define ITEMS_PER_GAME 6
#define SHM_SIZE 1024 //This is the size of the buffer that will hold the message that players send
#define GAMES 3 //Number of games sizeof(cli_addr);



/*signal and disconnect handlers */
void sigchld_parent_handler(int signo); //SIGCHLD handler of main's root process
void sigint_handler(int signo);
void  sigalrm_handler(int signo);
void disconnect();

/*Reading player's info functions */
int readPlayer(char* buf,char *player_name, int * inventory, int* item_selected );
int checkIfAvailable(int* player_inventory);
void giveItemsBack(int* player_inventory);


void void_handler() /* Dummy handler that will be run when SIGUSR or SIGALRM is fired */
{
};

 /*portno stores the port number on which the server accepts connections.
cnt is the return value for the read() and write() calls; i.e. it contains the number of characters read or written.*/
int portno=5005, cnt;



//A sockaddr_in is a structure containing an internet address. This structure is defined in netinet/in.h.
//The variable serv_addr will contain the address of the server, and cli_addr will contain the address of the client which connects to the server.
struct sockaddr_in serv_addr, cli_addr;
socklen_t clilen = sizeof(cli_addr); //clilen stores the size of the address of the client. This is needed for the accept system call.

int PLAYERS_PER_GAME, MAX_CLIENTS;
char alrtmsg[50]; //Message sent to waiting players
FILE *fp; //File pointer to the inventory's file
int inventory[6]; //Holds the server's item quota

/*sockfd and new_sfd are file descriptors, i.e. array subscripts into the file descriptor table .
 These two variables store the values returned by the socket system call and the accept system call */
int sockfd, new_sfd;
pid_t client_pid; //Forked process id
typedef enum {PLAYER_CONNECTED, STANDBY, GAME_IS_FULL} state_t; //Each player has 3 states
state_t state;
int game_quota = 0; //quota per player
int gnum = 0 , player_num=0; //Game number and player number on each game gnum range is [0, GNUM-1] and player_num is [0, PLAYER_PER_GAMES-1]
const char *item_names[6] = { "gold" , "armor", "ammo" , "lumber" , "magic" , "rock" };

/* Struct that cointains the info of each player connected */
typedef struct players
{
   char name[50];
   int inventory[6];
   int item_selected[6]; // 0 for not selected in txt 1 for item selected
} Player;

/* These are pointers to the shared memory of the proccesses */
int * shProcessId, *shSubChildPID, *shSpotStatus, *shClientsNum, *shGameInfo, *shPlayerSpeaking;
char *shData;
/*Pointers to the unnamed semaphores allocated memory */
sem_t *shNewMessage, *shAllPlayersRead, *shOtherPlayersRead;

int shmfd0, shmfd00, shmfd1,shmfd2,shmfd3 ,shmfd4, shmfd5, shmfd6 ,shmfd7, shmfd8, shmfd9; //File descriptors for named shared memory
int c = 0;
char *fileName;
int i = 0, j =0;
//int *ClientSockets;

/*Pointers to the start of an array of shared semaphore pointers*/
sem_t *semSpotStatus, *semGameInfo, *semClientsNum , *semData, **semNewMessage, **semAllPlayersRead, **semOtherPlayersRead;




int main( int argc, char *argv[] )
{
/* Get opt function is used for command line parsing, the letters in the third argument indicate the option characters
 An option character in this string can be followed by a colon (‘:’) to indicate that it takes a required argument.
This variable optarg is set by getopt to point at the value of the option argument, when one is matched.
 The getopt function returns the option character for the next command line option.
 When no more option arguments are available, it returns -1 */
while ( (c = getopt(argc, argv, "p:i:q:"))!= -1 )
{
  switch(c)
    {
    case 'p':
    PLAYERS_PER_GAME = atoi(optarg);
    break;
   case 'i':
    fileName = (char *)malloc((strlen(optarg))*sizeof (char));
    fileName = strcpy(fileName, optarg);
    break;
    case 'q':
    game_quota = atoi(optarg);
    break;
    case '?': //Not valid character
         if (isprint (optopt)) //Print wrong character if printable
           fprintf (stderr, "Unknown option `-%c'.\n", optopt); //When getopt encounters an unknown option character or an option with a missing required argument, it stores that option character in this variable.
         else //Character not printable, print it as hex value
           fprintf (stderr,
                    "Unknown option character `\\x%x'.\n",
                    optopt);
         return 1;
    }
}

MAX_CLIENTS = GAMES*PLAYERS_PER_GAME;


char cwd[80],ch; //cwd has the current working dirrectory
memset(cwd, 0, sizeof(cwd));
getcwd(cwd, sizeof(cwd));

i=0;
cwd[strlen(cwd)] = '/'; //Put '/' at the end of cwd
strcat(cwd, fileName); //Append file name to cwd
printf("%s\n", cwd);
fp = fopen(cwd, "r"); // open file in read mode
if( fp == NULL )
{
    perror("Error while opening the file.\n");
    exit(EXIT_FAILURE);
}
/* Read file values*/
while( ( ch = fgetc(fp) ) != EOF ) //Read until end of file
{
    if ( ch == '\t' ) //If we meet a tab character
    {
        fscanf( fp , "%d" , &inventory[i] ); //fscanf the value into inventory[i]
        i++;
    }
}

fclose(fp); //Close file

/*The socket() system call creates a new socket. It takes three arguments. The first is the address domain of the socket.
* We use AF_INET for internet domain for connection of any two hosts on the Internet
* The second argument is the type of socket. We use SOCK_STREAM for stream socket that uses TCP protocol.
* The third argument is the protocol. The zero argument will leave the operating system to choose the most appropriate protocol. It will choose TCP for stream sockets
*/
sockfd = socket(AF_INET, SOCK_STREAM, 0);
if (sockfd < 0)
{
printf("server socket failure %d\n", errno);
perror("server: ");
exit(1);
}
bzero((char *) &serv_addr, sizeof(serv_addr)); //The function bzero() sets all values in the buffer serv_addr to zero.

/*The variable serv_addr is a structure of type struct sockaddr_in. This structure has four fields. \
The first field is short sin_family, which contains a code for the address family. It should always be set to the symbolic constant AF_INET */
serv_addr.sin_family = AF_INET;


/*The second field of serv_addr is unsigned short sin_port, which contain the port number.
However, instead of simply copying the port number to this field,
it is necessary to convert this to network byte order using the function htons()
which converts a port number in host byte order to a port number in network byte order. */
serv_addr.sin_port = htons(portno);


/*The third field of sockaddr_in is a structure of type struct in_addr which contains only a single field unsigned long s_addr.
This field contains the IP address of the host. For server code, this will always be the IP address of the machine
 on which the server is running, and there is a symbolic constant INADDR_ANY which gets this address. */
serv_addr.sin_addr.s_addr = INADDR_ANY;


/*Allows other sockets to bind() to this port, unless there is an active listening socket bound to the port already.
 This enables to get around those "Address already in use" error messages when you try to restart your server after a crash.*/
int yes = 1;
if ( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1 )
{
    perror("setsockopt");
}



/*The bind() system call binds a socket to an address, in this case the address of the current host and port number on which the server will run.
It takes three arguments, the socket file descriptor, the address to which is bound,
and the size of the address to which it is bound. The second argument is a pointer to a structure of type sockaddr,
but what is passed in is a structure of type sockaddr_in, and so this must be cast to the correct type.
This can fail for a number of reasons, the most obvious being that this socket is already in use on this machine. */
if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0 )
{
printf("server bind failure %d\n", errno);
perror("server: ");
exit(1);
}



/*The listen system call allows the process to listen on the socket for connections. The first argument is the socket file descriptor,
 and the second is the size of the backlog queue, i.e., the number of connections that can be waiting while the process is handling a particular connection */
if (listen(sockfd, MAX_CLIENTS) < 0)
{
printf("server listen failure %d\n", errno);
perror("server: ");
exit(1);
}

/* print the hostname of the server */
char hostname[128];
gethostname(hostname, sizeof hostname);
printf("My hostname: %s\n", hostname);

struct sigaction sa, sa2, new_act1, new_act2;
/* install signal handlers */
signal (SIGHUP, sigint_handler);

signal (SIGUSR2, SIG_IGN); //Ignore it when SIG_CHLD handler is called from grandparent process
sigemptyset(&sa2.sa_mask); //Dont block signals while in handler
sa2.sa_handler = &sigint_handler; //Use sigchld_parent_handler for SIGCHLD for every terminating child process
sa2.sa_flags = 0;
sigaction(SIGINT, &sa2, 0);

sigemptyset(&sa.sa_mask); //Dont block signals while in handler
sa.sa_handler = &sigchld_parent_handler; //Use sigchld_parent_handler for SIGCHLD for every terminating child process
sa.sa_flags =  SA_RESTART| SA_NODEFER; //Restart system call when it becomes interrupted from SIGCHLD and returns with EINTR = -1
sigaction(SIGCHLD, &sa, 0);

sigemptyset(&new_act1.sa_mask); //Dont block signals while in handler
new_act1.sa_handler  = void_handler; //Use void_handler for SIGUSR. This signal will be used to signal other processes that a game is full.
new_act1.sa_flags = 0; //Explicitly set the handler to NOT restart (SA_RESTART is default) a system call so it will return with EINTR = -1.
sigaction(SIGUSR1, &new_act1, NULL);

sigemptyset(&new_act2.sa_mask); //Dont block signals while in handler
new_act2.sa_handler  = sigalrm_handler; //Use void_handler for SIGALRM. This signal will be used to signal waiting players of the players remaining till a game is full
new_act2.sa_flags = 0; //Explicitly set the handler to NOT restart (SA_RESTART is default) a system call.
sigaction(SIGALRM, &new_act2, NULL);

/*shm_open() creates and opens a new, POSIX shared memory object (SHM).
 name given is created as a file in /dev/shm
 O_CREAT create object if it does not already exists
 mode is set to reading and writing
*/

shmfd0 = shm_open("/fd0", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd00 = shm_open("/fd00", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd1 = shm_open("/fd1", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd2 = shm_open("/fd2", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd3 = shm_open("/fd3", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd4 = shm_open("/fd4", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd5 = shm_open("/fd5", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd6 = shm_open("/fd6", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd7 = shm_open("/fd7", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);
shmfd8 = shm_open("/fd8", O_CREAT | O_RDWR , S_IRUSR | S_IWUSR);


/*New SHM objects have length 0
Before mapping, must set size using ftruncate(fd, size)
Bytes in newly extended object are initialized to 0, no memset is needed*/
ftruncate (shmfd0, MAX_CLIENTS*sizeof(int));
ftruncate (shmfd00, MAX_CLIENTS*sizeof(int));
ftruncate (shmfd1, SHM_SIZE*GAMES*sizeof(int));
ftruncate (shmfd2, sizeof(int));
ftruncate (shmfd3, GAMES*(ITEMS_PER_GAME+1)*sizeof(int));
ftruncate (shmfd4, SHM_SIZE*GAMES*sizeof(char));
ftruncate (shmfd5, GAMES*sizeof(int));

/*Shared memory for unnamed semaphore */
ftruncate (shmfd6, GAMES*sizeof(sem_t));
ftruncate (shmfd7, GAMES*sizeof(sem_t));
ftruncate (shmfd8, GAMES*sizeof(sem_t));

/*mmap() creates a new mapping in the virtual address space of the calling process.
Returns address actually used for mapping, treated like a normal c pointer
First argument is the address at which to place mapping in caller’s virtual address space
NULL == let the OS decide
Second argument size of mapping ( we map the whole memory from ftruncate)
PROT_READ | PROT_WRITE for read-write mapping
shmfdX argument is the fd returned form shm_open
Last argument is the offset starting point of mapping in underlying file or SHM object, 0 for mapping from the start
*/

/* shProcessId is a pointer to an array that holds the id of all the processes-clients that are currently active on server */
if ( (shProcessId = mmap(NULL ,MAX_CLIENTS*sizeof(int), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd0, 0)) ==  (int *)-1)
 perror("shPlayersId");
/* shSubChildPID is a pointer to an array that holds the id of all the sub processes-clients that are currently active on server */
if ( (shSubChildPID = mmap(NULL ,MAX_CLIENTS*sizeof(int), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd00, 0)) ==  (int *)-1)
 perror("shSubChildPID");

/* shSpotStatus is a pointer to an array that holds the status of every game position. 0 is for open position, 1 is for player waiting on a game to start and -1 shows that a game is running, no players can connect to it*/
if ( (shSpotStatus= mmap(NULL ,MAX_CLIENTS*sizeof(int), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd1, 0)) ==  (int *)-1)
   perror("shSpotStatus");
/* shClientsNum is a pointer to a variable that counts the current connected players on the server*/
if ( (shClientsNum = mmap(NULL ,sizeof(int), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd2, 0)) ==  (int *)-1)
   perror("shClientsNum");
/* shGameInfo is a pointer to an array that shows the items' and players' number of every game. The first index of each block shows the number of players and the next ITEMS_PER_GAME elements show the quota of game's items */
if ( (shGameInfo = mmap(NULL ,GAMES*(ITEMS_PER_GAME+1)*sizeof(int), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd3, 0)) ==  (int *)-1)
   perror("shGameInfo");
/*shData is a pointer to an array that the data are written when recieved from a player. SHM_SIZE is the shared memory size of every GAME*/
if ( (shData = mmap(NULL ,SHM_SIZE*GAMES*sizeof(char), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd4, 0)) ==  (char *)-1)
   perror("shData");
/*shPlayerSpeaking is a pointer to an array of ints , 1 for each game. It holds the player number of a the player that written last to the shared memory */
if ( (shPlayerSpeaking = mmap(NULL ,GAMES*sizeof(int), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd5, 0)) ==  (int *)-1)
   perror("shPlayerSpeaking");
/* shNewMessage is a pointer to an array of pointers to semaphores, 1 for each game. Each game semaphore is increased everytime one player sends a message so everyone can read that message*/
if ( (shNewMessage = mmap(NULL ,GAMES*sizeof(sem_t), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd6, 0)) ==  (sem_t *)-1)
   perror("shNewMessage");
/* shAllPlayersRead, is a pointer to an array of pointers to semaphores, 1 for each game. Each game semaphore locks until every player reads the message sent by one player, then allows further message to be written in shared memory */
if ( (shAllPlayersRead = mmap(NULL, GAMES*sizeof(sem_t), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd7, 0)) ==  (sem_t *)-1)
   perror("shAllPlayersRead");
/*shOtherPlayersRead is a pointer to an array of pointers to semaphores, 1 for each game. Each game semaphore locks until every player reads the message sent by one player, then allows players t*/
if ( (shOtherPlayersRead = mmap(NULL ,GAMES*sizeof(sem_t), PROT_READ | PROT_WRITE ,MAP_SHARED ,shmfd8, 0)) ==  (sem_t *)-1)
   perror("shOtherPlayersRead");




/* main inventory storing */
for(i=0; i<GAMES; i++)
{
   for(j=0; j<ITEMS_PER_GAME; j++) //for ever game store the inventory read from cmd line in the shared memory
      shGameInfo[ i*(ITEMS_PER_GAME+1) + (j+1)] = inventory[j]; //j+1 because first spot is reserved for game's player number

}



/* The sem_open() function shall establish a connection between a named semaphore and a process.
The process may reference the semaphore associated with name using the address returned from the call */
semSpotStatus  = sem_open("/sem1", O_CREAT, S_IRUSR | S_IWUSR, 1);
semClientsNum = sem_open("/sem2", O_CREAT, S_IRUSR | S_IWUSR, 1);
semGameInfo = sem_open("/sem3", O_CREAT, S_IRUSR | S_IWUSR, 1);
semData = sem_open("/sem4", O_CREAT, S_IRUSR | S_IWUSR, 1);

/*Allocate memory for an array of semaphore pointers.
Each element will have a pointer to a semaphore for the corresponding game
They are actually pointers to the shared semaphore memory but they are used for better readability*/
semNewMessage = malloc( GAMES*sizeof(sem_t) );
semAllPlayersRead = malloc( GAMES*sizeof(sem_t)) ;
semOtherPlayersRead = malloc( GAMES*sizeof(sem_t) );

/* Initialize the shared semaphores
* First value is a pointer to a semaphore
If the second pshared argument has a non-zero value, then the semaphore is shared between processes via shared memory
Third argument is the initial value of the semaphore*/
for (i=0; i<GAMES; i++)
{
   semNewMessage[i] = shNewMessage + i*sizeof(sem_t);
   sem_init( semNewMessage[i], 1, 0); //Initialize to 0 , no new messages when game starts
}

for (i=0; i<GAMES; i++)
{
   semAllPlayersRead[i] = shAllPlayersRead + i*sizeof(sem_t);
   sem_init( semAllPlayersRead[i], 1, 1); //Initialize to 1 , allowing for new messages to be written in shData immediatly after game starts and a message is recieved
}


for (i=0; i<GAMES; i++)
{
   semOtherPlayersRead[i] = shOtherPlayersRead + i*sizeof(sem_t);
   sem_init( semOtherPlayersRead[i], 1, 0); //Initialize to 0, this will be incremented only after a player has read a message
}



int sval;
/* Place the server in an infinite loop, waiting
 * on connection requests to come from clients. */
while(1)
{

   /*The accept() system call causes the process to block until a client connects to the server.
    Thus, it wakes up the process when a connection from a client has been successfully established.
     It returns a new file descriptor, and all communication on this connection should be done using the new file descriptor.
     The second argument is a reference pointer to the address of the client on the other end of the connection, and the third argument is the size of this structure */
    new_sfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

    if( new_sfd < 0 )
    {
        printf("server accept failure %d\n", errno);
        printf("server: ");
        exit(1);
    }
    printf("A new client has connected\n");
 /* If the fork() function is successful then it returns twice.
 Once it returns in the child process with return value ’0′ and then it returns in the parent process with child’s PID as return value. */
   client_pid = fork(); //Create a child proccess and
   if (client_pid > 0) //parent process
      {
            close(new_sfd); //Close the client fd, child has a copy of it
      }
    if (client_pid == 0) //Child proccess
    {

        signal(SIGINT, SIG_IGN); //Use the disconnect function as handler when Ctrl-C -> SIGINT signal is fired
        signal(SIGUSR2, disconnect); //Disconnect when you recieve this signal
        sigaction(SIGCHLD, &sa, 0); //Install same handler for SIGCHLD because this signal is caught only from direct children
        close(sockfd); //Not need it in child , close it


        sem_wait( semClientsNum); // Lock for the shared variable shClientsNum
        sem_wait( semSpotStatus); //Lock for the shared variable shSpotStatus
        (*shClientsNum)++; //Increase clients by one

        gnum=-1; //Not available game found value

        int stop= 0;
        for (i= 0; (i<GAMES) && !stop ; i++) //for every game
         {
            for(j=0; (j<PLAYERS_PER_GAME) && !stop; j++) //for every player
            {
               if ( shSpotStatus[ i*PLAYERS_PER_GAME + j ] == 0 ) //find first free game and socket position and store the game number and player position
                  {
                     gnum = i;
                     player_num = j;
                     stop=1; //break both for
                  }
            }
         }

         shProcessId[gnum*PLAYERS_PER_GAME + player_num] = getpid(); //Store the child proccess in the shared memory
         printf("Game_number %d\n" , gnum);
         printf("Player_number %d\n" , player_num);
         if ( gnum == -1) //If free game was not found disconnect client
         {
            cnt = send(new_sfd, "Server is full, try later\n" , strlen("Server is full, try later\n"), 0);
            (*shClientsNum)--;
            sem_post( semClientsNum);
            sem_post( semSpotStatus);
            disconnect();
         }



        /*Unlock the semaphorees */
        sem_post( semClientsNum);
        sem_post( semSpotStatus);


        pid_t sub_child; //Create a new child
        state = PLAYER_CONNECTED; //New player connected
        Player player_info; //initialize a variable of the player's info
        Player* player = &player_info; //Make a pointer to it
        memset(player->name, 0, sizeof (player->name));
        memset(player->inventory, 0, sizeof (player->inventory));
        memset(player->item_selected, 0, sizeof (player->item_selected));




      char msg[SHM_SIZE+10]; //Final message
      char * ptr = 0; //Pointer to current game's shared buffer
      char buf[SHM_SIZE+10]; //Recieving message

        while(1) /*In case we want to be able to change player states (e.g from GAME_IS_FULL to STANDBY). In our case the loop only goes for GAME_IS_FULL state */
        {

            if(state == PLAYER_CONNECTED)
            {

                if( ( cnt = recv(new_sfd, buf, sizeof (buf),0) >=0  ) ) //Recieve player info message
                        {

                         if (cnt == 0) //When a TCP connection is closed on one side read() on the other side returns 0 byte.
                            {
                                printf("Client disconnected\n");
                                disconnect();
                            }
                            int fail = 1; //asume failure
                            int success;
                            int available=0;
                            success = readPlayer(buf, player->name, player->inventory, player->item_selected ); //Read player info into the struct

                            if ( success == 1) //If message has correct form
                                 available = checkIfAvailable(player->inventory); //Check if item chosen are available and if so, remove from game inventory

                            if (success ==1 && available == 1 ) //If both reading and checking was succesfull let him confirm the connection
                            {
                                  char check; //last check, waiting for player to confirm
                                  cnt = recv(new_sfd, &check, sizeof (char),0);

                                  if ( cnt == 0) //Client disconnected
                                    {
                                           strcpy(buf , "Connection Declined, client disconnected \n");
                                           fail =1;
                                    }

                                 else if ( cnt==1  )   //check if client sent new line character
                                    {
                                           if (check == '\n')
                                           {
                                                  shSpotStatus[ gnum*PLAYERS_PER_GAME + player_num] = 1; //Mark the player as a "waiting" player
                                                  memset(buf, 0, sizeof (buf)); //zero the buffer
                                                  state = STANDBY; //change state to waiting

                                                  printf("Player %s took items\n", player->name);
                                                  for (i = 0; i<6; i++)
                                                   {
                                                      if ( player->item_selected[i] == 1)
                                                      {
                                                         printf("%s\t %d\n" , item_names[i], player->inventory[i]);
                                                      }
                                                    }
                                                    /*Print Server Info*/
                                                  printf("************  SERVER INFO ************ \n");
                                                  printf("Number of clients %d\n" , (*shClientsNum));
                                                  for(i=0; i<GAMES; i++)
                                                        {
                                                           printf("Game number %d\n", i+1);
                                                           printf("Players number %d\n" , shGameInfo[i*(ITEMS_PER_GAME+1)] );
                                                           for (j=0; j<ITEMS_PER_GAME; j++)
                                                            {
                                                                  printf("%s  %d\n", item_names[j] ,  shGameInfo[ i*(ITEMS_PER_GAME+1) + (j+1)] );
                                                            }
                                                        }

                                                   for(i=0; i<GAMES*PLAYERS_PER_GAME; i++)
                                                      printf("shSpotStatus %d is %d\n" , i, shSpotStatus[i]);

                                                    strcpy(buf , "Connection Accepted\n");   /* send confirmation message */
                                                    send( new_sfd , buf, strlen(buf), 0);
                                                    memset(buf, 0 ,sizeof(buf));

                                                    sem_wait(semGameInfo); //Lock semaphore for shGameInfo
                                                    shGameInfo[gnum*(ITEMS_PER_GAME+1)]++; //Increase the game's player by 1


                                                    if ((shGameInfo[gnum*(ITEMS_PER_GAME+1)]) > 0 && (shGameInfo[gnum*(ITEMS_PER_GAME+1)]  < PLAYERS_PER_GAME ))
                                                          {
                                                              alarm(5);              /* set alarm clock to fire in 5 seconds if game is not full yet       */
                                                          }
                                                   else if ( shGameInfo[gnum*(ITEMS_PER_GAME+1)] == PLAYERS_PER_GAME ) //Last player connected
                                                         {


                                                         /* For every other player(process) in the same game except the current send a message that game is full, breaking recv blocking on STANDBY */
                                                            for (i=0; (i<PLAYERS_PER_GAME) && (i!=player_num); i++)
                                                               {
                                                                  kill( shProcessId[gnum*PLAYERS_PER_GAME + i], SIGUSR1);
                                                               }

                                                            sem_wait(semSpotStatus);

                                                            for(i=0; i<PLAYERS_PER_GAME; i++)
                                                                        shSpotStatus[gnum*PLAYERS_PER_GAME + i] = -1; //game about to start, dont make the position available until game finishes

                                                            sem_post(semSpotStatus);

                                                            for(i=0; i<ITEMS_PER_GAME; i++)
                                                               {
                                                                     shGameInfo[ gnum*(ITEMS_PER_GAME+1) + (i+1)  ] = inventory[i]; //renew game's inventory
                                                               }



                                                         }
                                                   sem_post(semGameInfo);
                                                   fail = 0;
                                              } //close if check == '\n'
                                           else
                                             {
                                                   strcpy(buf , "Connection Declined, client didnt confirm correctly\n");
                                             }
                                       } // close else if

                               } //close if success

                            else if ( available == -1) //Case of item's correct, but not available on server
                                  strcpy(buf , "Connection Declined, quota of item not available, disconnecting\n");

                            else if (success == 2 ) //Case of quota chosen bigger than server's acceptable quota
                                    strcpy(buf , "Connection Declined, quota of item is not not legal available, disconnecting\n");

                            else if (success == 3) //Case of item name not recognized
                                    strcpy(buf , "Connection Declined, item was not recognized, disconnecting\n");

                            if (fail == 1) //Send fail message and disconnect client
                                 {
                                    send( new_sfd , buf, strlen(buf), 0);
                                    sem_wait(semClientsNum);
                                    (*shClientsNum)--;
                                    sem_post(semClientsNum);
                                    disconnect();
                                 }
                       } //close if recv(message)
                  else
                        fprintf(stderr, "Failure Recieving Message\n");


            }//close if state== PLAYER_CONNECTED

            else if (state == STANDBY)
                {
                if ( shGameInfo[gnum*(ITEMS_PER_GAME+1)] != PLAYERS_PER_GAME ) /* if it's not the last player */
                           {
                           //      printf("STANDBY\n");
                           //      sleep(1000);
                                  for(; ; )
                                  {
                                    cnt = recv(new_sfd, buf, sizeof (buf),0);
                                    if( (cnt ==0  ) ) //Player disconnected in standby
                                    {
                                       printf("Player %s disconnected in STANDBY\n" , player->name);

                                      /* Decrease player game number and client number by 1 */
                                       sem_wait(semClientsNum);
                                       sem_wait(semGameInfo);
                                       shGameInfo[gnum*(ITEMS_PER_GAME+1)]--;
                                       (*shClientsNum)--;
                                       sem_post(semClientsNum);
                                        sem_post(semGameInfo);
                                       giveItemsBack(player->inventory);


                                       disconnect();
                                    }
                                    else if (cnt == -1) //If recv was interrupted by SIGALRM or SIGUSR
                                       {
                                         if ( shGameInfo[gnum*(ITEMS_PER_GAME+1)] == PLAYERS_PER_GAME ) //if EINTR was from SIGUSR1 break and go GAME_IS_FULL mode else, continue waiting..
                                         {
                                           break;
                                         }

                                       }
                                    else //Any new message will just be reseted
                                       memset(buf, 0, sizeof(buf));
                                  }
                           }

                   else if ( shGameInfo[gnum*(ITEMS_PER_GAME+1)] == PLAYERS_PER_GAME ) //everyone will run this code when game is full

                     /* Fire a SIGLALRM to cancel any pending alarms from before the game was full, and ignore it when it is recieved */
                       new_act2.sa_handler = SIG_IGN;
                       sigaction( SIGALRM, &new_act2, NULL);
                       alarm(0);

                        state = GAME_IS_FULL;

                        if ((cnt = send(new_sfd, "Game Started\n", strlen("Game Started\n"), 0))== -1)
                           {
                              perror("Failure Sending Playing\n");
                           }

                         sub_child = fork(); //Fork a new message recieving process that will block on recv, and will write message sent on shared memory
                         if (sub_child >0)
                            {
                               shSubChildPID[gnum*PLAYERS_PER_GAME + player_num] = sub_child;
                               printf("Subchild is %d\n", shSubChildPID[gnum*PLAYERS_PER_GAME + player_num]);
                           }

                }
            else if ( state == GAME_IS_FULL)
                {
                     if (sub_child > 0 ) //parent process, the one who will send the data
                        {

                                          sem_wait( semNewMessage[gnum] ); //Block until a new message is recieved and semNewMessage will be unlocked

                                             sem_wait( semData);


                                             ptr =  shData + gnum*SHM_SIZE; //Pointer that will point to the correct game buffer
                                             memcpy(msg, ptr, SHM_SIZE*sizeof(char)); //copy sent data


                                          if ( shPlayerSpeaking[gnum] != player_num ) //Dont send message to self
                                          {
                                                if ( (cnt = send( new_sfd, msg , strlen(msg),0) ) ==-1 )
                                                {
                                                   printf("new_sfd is %d \n" , new_sfd);
                                                      perror("sending msg");
                                                }

                                          }
                                          /*shData[ SHM_SIZE*gnum + (SHM_SIZE-1)] is a spot in the last place of the shared memory that will
                                           count the players who have read the message (including the sender). When this reaches the number of
                                           PLAYERS_PER_GAME it will zero the counter, free the sending data*/
                                          shData[ SHM_SIZE*gnum + (SHM_SIZE-1)]++;
                                          memset(msg, 0 , SHM_SIZE*sizeof(char));

                                          if ( shData[ SHM_SIZE*gnum + (SHM_SIZE-1)] == (shGameInfo[gnum*(ITEMS_PER_GAME +1)]) ) //everyone read it, this will be called by the last process that reads the message
                                             {
                                                       printf("All Players read message\n");
                                                       memset(shData + gnum*SHM_SIZE, 0 , SHM_SIZE*sizeof(char)); //delete message from shared buffer

                                                       shData[ SHM_SIZE*gnum + (SHM_SIZE-1)] = 0; //Reset players read counter

                                                       sem_post( semData);
                                                       for (i=0; i<(shGameInfo[gnum*(ITEMS_PER_GAME+1)]-1); i++) //For every other proccess waiting in the sem_wait ( semOtherPlayersRead[gnum] ) unlock the semaphore
                                                            sem_post( semOtherPlayersRead[gnum] );

                                                       sem_post ( semAllPlayersRead[gnum]); //Unlock semaphore, allowing new data to be recieved on sub_child
                                                    }

                                          else
                                          {
                                             sem_post( semData); //Unlock shared memory semaphore
                                             sem_getvalue( semOtherPlayersRead[gnum], &sval);
                                             sem_wait ( semOtherPlayersRead[gnum] ); //Wait for other players to read the message, solves problem where 1 process could go the loop more than 1 time using sem_wait( semNewMessage) more than once

                                          }
                           }

                     else if (sub_child == 0) //subchild
                        {

                           if( ( cnt = recv(new_sfd, buf, sizeof (buf),0) >0  ) )
                              {

                                 sem_wait (semAllPlayersRead[gnum]);

                                 shPlayerSpeaking[gnum] = player_num; //Assign speaking player number on this shared variable, this will allow for not sending the message on the same player

                                 /*Write message to the shared buffer*/
                                 ptr = shData + gnum*SHM_SIZE;
                                 buf[strlen(buf)] = '\0';
                                 memcpy(ptr, buf, SHM_SIZE*sizeof(char));


                                 for (i=0; i< shGameInfo[gnum*(ITEMS_PER_GAME+1)]; i++) //increase semNewMessage for every remaining player, so the can read the message
                                       sem_post ( semNewMessage[gnum] );


                              }
                           else if ( cnt == 0 ) //Client disconnected
                             {
                                printf("Player %s disconnected\n" , player->name);



                                sem_wait(semGameInfo );
                                sem_wait(semClientsNum);
                                sem_wait(semSpotStatus);
                                (*shClientsNum)--; //reduce overall clients by 1
                                shGameInfo[gnum*(ITEMS_PER_GAME +1)]--; //reduce game's player by 1

                                if ( shGameInfo[gnum*(ITEMS_PER_GAME +1)] == 0) //All players left game, game ended, renew inventory, make it available to other players to connect.
                                    {
                                       printf("Game %d finished\n" , gnum);

                                       for(i=0; i<PLAYERS_PER_GAME; i++) //Make avaialble every spot
                                          {
                                              shSpotStatus[ gnum*PLAYERS_PER_GAME + i] = 0;
                                          }
                                    }
                                 sem_post(semSpotStatus);
                                 sem_post(semGameInfo);
                                 sem_post(semClientsNum);
                                 disconnect(); //exit sub_child process

                             }
                           memset(buf, 0, sizeof(buf));

                        } //close else if (sub_child == 0)


                } //close else if (state == STANDBY)

        } //close while

    } //closing bracket for if (client_pid == 0 )

}//closing while(1) bracket

} //close main()


void disconnect()
{

   printf("Process %d disconnected\n", getpid());
   send(new_sfd, "" , 0 , 0); //Disconnect the player on the other side if disconnect was called from server's SIGINT (else he will have disconnected himself)
   if ( state == PLAYER_CONNECTED || (state == STANDBY)) //disconnected before game begins, make the spot available for others to connect, else leave it -1
      {
         shSpotStatus[gnum*PLAYERS_PER_GAME + player_num] = 0;
      }
   shProcessId[gnum*PLAYERS_PER_GAME + player_num] = 0;
   shSubChildPID[gnum*PLAYERS_PER_GAME + player_num] = 0;
   exit(1); // when a process exists all open file descriptors are closed, and all memory mappings are removed automatically, no need to call close() or munmap().
}



/*This handler catches SIGCHLD for grandparent and parent process too */
void sigchld_parent_handler(int signo) {

  pid_t PID;
  int status;
  do {
    PID = waitpid(-1,&status, WNOHANG);
    printf("Child proccess %d reaped from parent %d\n", PID , status);
  }
  while ( PID < 0 );

   kill(getpid(), SIGUSR2); //If we are on a child process kill it too, because SIGCHLD was sent from grandchild
 }
/* CTRL + C in server's processes catches this signal
* Kills every living child and grandchild process, frees memory and exits program
*/
void  sigint_handler(int sig)
{
   struct sigaction sa;
   sa.sa_handler = SIG_DFL; //Use sigchld_parent_handler for SIGCHLD for every terminating child process
   sa.sa_flags = 0; //Restart system call when it becomes interrupted from SIGCHLD and returns with EINTR = -1
   sigaction(SIGCHLD, &sa, 0);



for(i=0; i<MAX_CLIENTS; i++)
   {
      if(shProcessId[i] != 0)
      {
         kill(shProcessId[i], SIGUSR2);
         waitpid (shProcessId [i], NULL, 0);
         printf("Proccess shProcess[%d] \n",i);
      }
      if( shSubChildPID[i] != 0)
      {
         kill(shSubChildPID[i], SIGUSR2);
         waitpid (shSubChildPID [i], NULL, 0);
         printf("Proccess shSubChildPID[%d] \n",i);
      }
   }

         /*free memory occupied with malloc */
         free(semNewMessage);
         free(semAllPlayersRead);
         free(semOtherPlayersRead);



/*The shm_unlink() function shall remove the name of the shared memory object named by the string pointed to by name.
If one or more references to the shared memory object exist when the object is unlinked,
the name shall be removed before shm_unlink() returns, but the removal of the memory object
contents shall be postponed until all open and map references to the shared memory object have been removed,
meaning after every process will exit and it's mappings will be deleted */
         shm_unlink("/fd0");
         shm_unlink("/fd00");
         shm_unlink("/fd1");
         shm_unlink("/fd2");
         shm_unlink("/fd3");
         shm_unlink("/fd4");
         shm_unlink("/fd5");
         shm_unlink("/fd6");
         shm_unlink("/fd7");
         shm_unlink("/fd8");
         shm_unlink("/fd9");


         sem_close(semSpotStatus);
         sem_close(semClientsNum);
         sem_close(semGameInfo);
         sem_close(semData);

/* Destroy shared semaphores */
         for(i=0; i<GAMES; i++);
               sem_destroy(semNewMessage[i]);
        for(i=0; i<GAMES; i++);
               sem_destroy(semAllPlayersRead[i]);
         for(i=0; i<GAMES; i++);
               sem_destroy(semOtherPlayersRead[i]);

/* Remove named semaphores when every process unreferences it */
         sem_unlink("/sem1");
         sem_unlink("/sem2");
         sem_unlink("/sem3");
         sem_unlink("/sem4");





         printf("Server exiting\n");
         exit(1);

}





/* This function sends the "Waiting for %d remaining players" to all waiting players */
void sigalrm_handler(int sig)
{
    snprintf(alrtmsg, sizeof(alrtmsg),"Waiting for %d players\n" , PLAYERS_PER_GAME-shGameInfo[gnum*(ITEMS_PER_GAME +1)]); //Put message on string alrtmsg
    if ( (shSpotStatus[gnum*PLAYERS_PER_GAME + player_num]==1) ) //If it's on waiting mode
        {
        if ((cnt = send(new_sfd, alrtmsg, sizeof(alrtmsg), 0))== -1)
          {
                   fprintf(stderr, "Failure Sending Message\n");

          }
         alarm(5); //Alarm same process
        }
}

/* This function takes as arguments the message send from player containing his name and the items he selected,
It stores them to player->inventory and player-> item_selected if called by reference
item_selected array will be used after to print selected items */

int readPlayer(char* buf,char *player_name, int * inventory, int* item_selected )
{
    char temp[5] , item_name[20],ch; //temp storing quota value as characters
    int next=0;
    int i=0,k=0,flag=0,quota_read=0,j=0,total_quota=0; //k is used for an index on item_name array, flag is for when we finished reading an item's name, quota_read stores the value of an item, total_quota holds the total quota read till now

    memset(item_name , 0, sizeof (item_name) );
    memset(temp , 0, sizeof (temp) );

    for(i=0; i<strlen(buf); i++) //First read player name
        {
        if ( buf[i] == '\n' )
            {
              next = i+1; //Pointer after name has been read
              break;
            }
            player_name[i] = buf[i];
        }
        player_name[strlen(player_name)] = '\0';

    //Read item values
    for (i=next; i<strlen(buf); i++)
        {
            ch=buf[i];  //This is the current character of the string we read
            if( ch == '\t') //If we see a tab character it means the next character will have a a value, set flag and start loop
            {
                flag = 1;
                k=0; //item name read, reset for the next item name
                continue;
            }
            if(flag == 1 && ch != '\n' && ch != '\r' ) //Every character we will read from now on will be a digit of the value,store it on temp[], when \n is seen break.
                {
                   temp[k] = ch;
                    k++;
                    continue;
                }
            if (ch == '\n') //We saw a new line character, process data so far (item name and quota)
                {

                    flag = 0; //reset fkag
                    quota_read = atoi(temp); //Make an integer out of the temp string, which has the item value
                    total_quota+= quota_read; //Increase total quota
                    if (total_quota > game_quota) //If it's bigger than server's quota
                        {
                            return 2;
                        }
                item_name[strlen(item_name)] = '\0';
                /*Store the value quota read on the player's inentory
                according the item's name that was read */
                if (strcmp(item_name, "gold")==0)
                {
                   inventory[GOLD] = quota_read;
                   item_selected[GOLD] = 1;
                }
                else if (strcmp(item_name, "armor") == 0)
                {
                    inventory[ARMOR] = quota_read;
                    item_selected[ARMOR] = 1;
                }
                else if (strcmp(item_name, "ammo") == 0)
                {
                    inventory[AMMO] = quota_read;
                    item_selected[AMMO] = 1;
                }
                else if (strcmp(item_name, "lumber") == 0)
                {
                    inventory[LUMBER] = quota_read;
                    item_selected[LUMBER] = 1;
                }
                else if (strcmp(item_name, "magic") == 0 )
                {
                    inventory[MAGIC] = quota_read;
                    item_selected[MAGIC] = 1;
                }
                else if (strcmp(item_name, "rock") == 0)
                {
                    inventory[ROCK] = quota_read;
                    item_selected[ROCK] = 1;
                }
                else
                {
                    printf("%s Was not recognized\n", item_name);
                    return 3;
                }
                /* zero values */
                quota_read= 0;
                memset(item_name, 0 , sizeof (item_name));
                memset(temp, 0 , sizeof (temp));
                j=0;
                continue;
                }

        item_name[j] = ch; //If nothing of the above occures, the character is part of the item's name
        j++;

        }


   return 1;
}

/* This function checks if the items requested by the clients exist on the Game's inventory */
int checkIfAvailable(int* player_inventory)
{
      sem_wait(semGameInfo); //Lock semaphore so other people wont take items at the same time
      int i = 0;
      for(i = 0; i<ITEMS_PER_GAME; i++)
         {
            if(player_inventory[i] > shGameInfo[gnum*(ITEMS_PER_GAME+1) + (i+1)] ) //We found one item that doesn't exist on game's inventory, return with an error indication.
               {
                  printf("Item is %d and shared is %d\n" , player_inventory[i] , shGameInfo[gnum*(ITEMS_PER_GAME+1) + (i+1)]);
                  sem_post(semGameInfo);
                  return -1;
               }

         }
       //Every item quota requested exists on server.
       for(i = 0; i<ITEMS_PER_GAME; i++)
         shGameInfo[gnum*(ITEMS_PER_GAME+1) + (i+1)] -=  player_inventory[i]; //decrease supplement

      sem_post(semGameInfo); //Unlock semaphore
      return 1;
}


void giveItemsBack(int* player_inventory)
{
      sem_wait( semGameInfo);
      for(i = 0; i<ITEMS_PER_GAME; i++)
         {
               shGameInfo[gnum*(ITEMS_PER_GAME+1) + (i+1)] +=  player_inventory[i];//give back items player took
         }
      sem_post( semGameInfo);
}







