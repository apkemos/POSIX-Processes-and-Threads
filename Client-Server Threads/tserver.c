#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/wait.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <syscall.h>
#include <fcntl.h>

#define GOLD 0
#define ARMOR 1
#define AMMO 2
#define LUMBER 3
#define MAGIC 4
#define ROCK 5

#define ITEMS_PER_GAME 6
#define MSG_SIZE 1024
#define GAMES 3



typedef enum {PLAYER_CONNECTED, STANDBY, GAME_IS_FULL} state_t; //Each player has 3 states
/*signal and disconnect handlers */
void sigint_handler(int signo);
void exit_all();
void disconnect();
void *sig_handler(void* arg);

void setNonBlocking(int sock);
void setBlocking(int sock);

//Client thread initializing function
void* accept_client( void * info);


/*Declare player info functions */
int readPlayer(char* buf,char *player_name, int * inventory, int* item_selected );
int checkIfAvailable(int* player_inventory, int gnum);


 /*portno stores the port number on which the server accepts connections. */
int portno=5005;
//A sockaddr_in is a structure containing an internet address. This structure is defined in netinet/in.h.
//The variable serv_addr will contain the address of the server, and cli_addr will contain the address of the client which connects to the server.
struct sockaddr_in serv_addr, cli_addr;
socklen_t clilen = sizeof(cli_addr); //clilen stores the size of the address of the client. This is needed for the accept system call.

/*sockfd and new_sfd are file descriptors, i.e. array subscripts into the file descriptor table .
 These two variables store the values returned by the socket system call and the accept system call */
int sockfd, new_sfd;

int MEM_PER_PLAYER, PLAYERS_PER_GAME, MAX_CLIENTS;
char alrtmsg[50];
FILE *fp;
int inventory[6];
int cnt, ClientsNum=0;
/* Sockets hold the current active sockets,
* SpotStatus the status of all games position
* (0 for free, 1 for player waiting for game to begin,and -1 for game started).
* GameInfo has blocks of (ITEMS_PER_GAME+1) ints. The first element of each block
* shows the number of players and the next ITEMS_PER_GAME elements show the quota of each game's items
* (Could also be implemented with array of structs) */
int *Sockets , *SpotStatus, *GameInfo;

pthread_t main_tid, *threads; //Array of the client servants threads
pthread_t alarm_thread; //Signal handling threads
pthread_mutex_t* mutex, mutGen ; //Muteces for general data
pthread_cond_t * game_full_cv;
pthread_attr_t attr;
sigset_t set;
struct sigaction act, act2;
extern int errno;


int c,stop=0;
int game_quota = 0; //quota per player
int gnum = 0 , player_num;
const char *item_names[6] = { "gold" , "armor", "ammo" , "lumber" , "magic" , "rock" };

/*Struct that contains the info gained by the initial message sent by the player */
typedef struct players
{
   char name[50];
   int inventory[6];
   int item_selected[6]; //1 for select 0 for not
   state_t state;
   int gnum; //Game number
   int num; //Player number in current game
} Player_t;

//Info of latest player connected to server
Player_t obj;
Player_t* player = &obj; //Pointer to info


char *fileName;
int i = 0, j =0;



int main( int argc, char *argv[] )
{
main_tid = pthread_self(); //Get main thread id
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
              fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            else //Character not printable, print it as hex value
              fprintf (stderr,
                       "Unknown option character `\\x%x'.\n",
                       optopt);
            return 1;
       }
}


MAX_CLIENTS = GAMES*PLAYERS_PER_GAME;


char cwd[80],ch; //cwd has the current working dirrectory
memset(cwd, 0 , sizeof(cwd)); //Clear array
getcwd(cwd, sizeof(cwd));

i=0;
cwd[strlen(cwd)] = '/'; //Put '/' at the end of cwd
strcat(cwd, fileName); //Append file name to cwd
fp = fopen(cwd, "r"); // open fil e in read mode
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





/* Allocate thread shared memory */
Sockets = malloc( MAX_CLIENTS * sizeof(int) ); //This array will hold all open sockets from clients connected to the server
SpotStatus = malloc( MAX_CLIENTS * sizeof(int)); //SpotStatus is an array that holds the status of every game position. 0 is for open position, 1 is for player waiting on a game to start and -1 shows that a game is running, no players can connect to it*/
GameInfo = malloc ( GAMES*(ITEMS_PER_GAME+1) * sizeof(int) ); //array that shows the items' and players' number of every game. The first index of each block shows the number of players and the next ITEMS_PER_GAME elements show the quota of game's items
threads = malloc ( MAX_CLIENTS * sizeof(pthread_t) ); //This array holds all the active threads of the server
game_full_cv = malloc ( GAMES* sizeof(pthread_cond_t) ); //This array holds the condition variables which a thread locks on waiting it's game to be full

memset(Sockets, 0, MAX_CLIENTS * sizeof(int));
memset(SpotStatus, 0, MAX_CLIENTS*sizeof(int));
memset(GameInfo, 0, GAMES*(ITEMS_PER_GAME+1) * sizeof(int));
memset(threads, 0, MAX_CLIENTS * sizeof(pthread_t));
memset(game_full_cv, 0, GAMES* sizeof(pthread_cond_t));

/* main inventory storing */
for(i=0; i<GAMES; i++)
{
   for(j=0; j<ITEMS_PER_GAME; j++) //for ever game store the inventory read from cmd line in the shared memory
      GameInfo[ i*(ITEMS_PER_GAME+1) + (j+1)] = inventory[j]; //j+1 because first spot is reserved for game's player number

}
//Initialize attribute
 pthread_attr_init(&attr);
 /*We wont need the client threads to be joinable so we create them
 as detached so their resources will be released upon termination
 No need to call pthread_detach(),the detachstate thread creation attribute is sufficient,
 since a thread need never be dynamically detached*/
 pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);



//Initialize condition variables for each game
for (i=0; i<GAMES; i++)
{
    pthread_cond_init(&game_full_cv[i], NULL); //call destroy after
}


 /* Block SIGALRM, only ; other threads created by main()
              will inherit a copy of the signal mask. */

int s;
sigemptyset(&set);
sigaddset(&set, SIGINT);
sigaddset(&set, SIGALRM);
/*Block SIGALRM and SIGINT on main thread, create a signal handling thread that will catch them
The alarm will be called every 5 seconds and the players who are in waiting mode
will recieve information about missing players from sig_handler thread*/
s = pthread_sigmask(SIG_BLOCK, &set, NULL);
if (s != 0)
   perror("pthread_sigmask");

s = pthread_create(&alarm_thread, &attr, sig_handler, (void*) &set);
alarm(5);
if (s != 0)
   perror("pthread_create alrm");




while(1)
{

   /*The accept() system call causes the process to block until a client connects to the server.
    Thus, it wakes up the process when a connection from a client has been successfully established.
     It returns a new file descriptor, and all communication on this connection should be done using the new file descriptor.
     The second argument is a reference pointer to the address of the client on the other end of the connection, and the third argument is the size of this structure */
    new_sfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);


      if( new_sfd < 0 ) //Accept was interrupted with SIGINT returning -1  and setting errno to EINTR, or failure to accept client, exit program either way
         {
            if ( errno == EINTR )
            {
               printf("Server was interrupted with SIGINT\n", errno);
               exit_all(); //Deallocate memory and exit

            }
            else
            {
               exit_all();
            }
         }
         printf("A new client has connected \n");
         pthread_mutex_lock( &mutGen );
         ClientsNum++;
         player->state = PLAYER_CONNECTED;
         player->gnum = -1;

        for (i= 0; (i<GAMES) && !stop ; i++) //for every game
         {
            for(j=0; (j<PLAYERS_PER_GAME) && !stop; j++) //for every player
            {
               if ( SpotStatus[ i*PLAYERS_PER_GAME + j ] == 0 ) //find first free game and socket position
                  {
                     player->gnum = i;
                     player->num = j;
                     stop=1; //break both for
                  }
            }
         }

         stop = 0;
         pthread_mutex_unlock( &mutGen );
         printf("Game_number %d\n" , player->gnum);
         printf("Player_number %d\n" , player->num);
         if ( player->gnum == -1)
         {
            cnt = send(new_sfd, "Server is full, try later\n" , strlen("Server is full, try later\n"), 0);
            ClientsNum--;
         }
         else
         {
            int pos = player->gnum*PLAYERS_PER_GAME + player->num; //Position in arrays according to game number and player number
            Sockets[pos] = new_sfd; //Store socket

            /* Create serving thread with accept_client as starting function and
            player containing the info of the player just connected*/
            pthread_create(&threads[pos], &attr, accept_client, player);

         }
}


} //close main


void *accept_client( void * info)
{

       int cnt=0;
       Player_t obj; //create new player structure
       obj = *(Player_t *)info; //assign the global parametere structure to it
       Player_t *player = &obj; //Create a pointer to this new object

       int gnum = player->gnum, player_num = player->num; //Shorter names
       char buf[MSG_SIZE]; //

        if( ( cnt = recv(Sockets[gnum*PLAYERS_PER_GAME + player_num], buf, sizeof (buf),0) >=0  ) ) //Recieve player info message
               {

                   if (cnt == 0) //When a TCP connection is closed on one side read() on the other side returns 0 byte.
                      {
                          printf("Client disconnected\n");
                          disconnect(player);
                      }
                                  int fail = 1, i; //asume failure
                                  int success;
                                  int available=0;
                                  success = readPlayer(buf, player->name, player->inventory, player->item_selected );
                                  if ( success == 1) //If message has correct form
                                          available = checkIfAvailable(player->inventory, gnum); //Check if item chosen are available

                                  if (success ==1 && available == 1 ) //If both reading and checking was succesfull let him confirm the connection
                                  {
                                        char check; //last check
                                        cnt = recv(Sockets[gnum*PLAYERS_PER_GAME + player_num], &check, sizeof (char),0);

                                        if ( cnt == 0)
                                          {
                                                 strcpy(buf , "Connection Declined, client disconnected \n");
                                                 fail =1;
                                          }

                                       else if ( cnt==1  )   //check if client sent new line character
                                          {
                                                 if (check == '\n')
                                                   {
                                                        SpotStatus[gnum*PLAYERS_PER_GAME + player_num] = 1; //Mark the player as a "waiting" player
                                                        fail=0;
                                                        memset(buf, 0, sizeof (buf));
                                                        player->state = STANDBY; //change state to waiting
                                                        printf("Player %s connected \n", player->name);
                                                        printf("Player %s took items\n", player->name);
                                                        for (i = 0; i<6; i++)
                                                         {
                                                            if ( player->item_selected[i] == 1)
                                                            {
                                                               printf("%s\t %d\n" , item_names[i], player->inventory[i]);
                                                            }
                                                          }
                                                          strcpy(buf , "Connection Accepted\n");  /* send confirmation message */
                                                          send( Sockets[gnum*PLAYERS_PER_GAME + player_num] , buf, strlen(buf), 0);
                                                          memset(buf, 0 ,sizeof(buf));

                                                          pthread_mutex_lock( &mutGen); //Lock general mutex
                                                          GameInfo[gnum*(ITEMS_PER_GAME+1)]++; //increase player
                                                          printf("********** SERVER INFO **********\n");
                                                          printf("Number of clients %d\n" , ClientsNum);
                                                          for(i=0; i<GAMES; i++)
                                                           {
                                                              printf("Game number %d\n", i+1);
                                                              printf("Players number %d\n" , GameInfo[i*(ITEMS_PER_GAME+1)] );
                                                              for (j=0; j<ITEMS_PER_GAME; j++)
                                                               {
                                                                     printf("%s  %d\n", item_names[j] ,  GameInfo[ i*(ITEMS_PER_GAME+1) + (j+1)] );
                                                               }
                                                           }
                                                         for(i=0; i<MAX_CLIENTS; i++)
                                                               {
                                                                  printf("SpotStatus[%d] is %d\n", i, SpotStatus[i]);
                                                               }

                                                        if ( GameInfo[gnum*(ITEMS_PER_GAME+1)] == PLAYERS_PER_GAME ) //Last player connected
                                                                        {
                                                                        /*  unblock all threads currently blocked on the current game waiting to be fulled
                                                                              use instead of pthread_cond_signal because multiple signals could be waiting
                                                                              on the condition */
                                                                            pthread_cond_broadcast(&game_full_cv[gnum]);

                                                                            for(i=0; i<PLAYERS_PER_GAME; i++)
                                                                                       SpotStatus[gnum*PLAYERS_PER_GAME + i] = -1; //game about to start, dont make the position available until game finishes

                                                                           for(i=0; i<ITEMS_PER_GAME; i++)
                                                                                {
                                                                                    GameInfo[ gnum*(ITEMS_PER_GAME+1) + (i+1)  ] = inventory[i]; //renew game's inventory
                                                                                }
                                                                        }
                                                      pthread_mutex_unlock( &mutGen);
                                                   }   //close if check == '\r
                                             } // close else if

                                       } //close if success

                                    else if ( available == -1) //Case of item's correct, but not available on server
                                                strcpy(buf , "Connection Declined, quota of item not available, disconnecting\n");

                                    else if (success == 2 ) //Case of quota chosen bigger than server's acceptable quota
                                                strcpy(buf , "Connection Declined, quota of item is not legal, disconnecting\n");

                                    else if (success == 3) //Case of item name not recognized
                                                strcpy(buf , "Connection Declined, item was not recognized, disconnecting\n");

                                    if (fail == 1) //Send fail message and disconnect client
                                                {
                                                   send( Sockets[gnum*PLAYERS_PER_GAME + player_num] , buf, strlen(buf), 0);  //send fail message
                                                   disconnect(player);
                                                }





         } //close if recv(message)

         if(player->state == STANDBY)
            {

               pthread_mutex_lock( &mutGen);
            /*
             Check the value of count and signal waiting thread when condition is
             reached.  Note that this occurs while mutex is locked.
             */

         /* Use while instead of if because:
          -If several threads are waiting for the same wake up signal, they will take turns acquiring the mutex, and any one of them can then modify the condition they all waited for.
          -If the thread received the signal in error due to a program bug
          -The Pthreads library is permitted to issue spurious wake ups to a waiting thread without violating the standard. */
               while (  GameInfo[gnum*(ITEMS_PER_GAME+1)] != PLAYERS_PER_GAME )
                     {

                        pthread_cond_wait( &game_full_cv[gnum], &mutGen ); //Wait for pthread_cond_broadcast that will be called from the last player that will connect

                     }
               pthread_mutex_unlock( &mutGen);
               player->state = GAME_IS_FULL; //Change state
                if ((cnt = send(Sockets[gnum*PLAYERS_PER_GAME + player_num], "Game Started\n", strlen("Game Started\n"), 0))== -1) //Player has disconnected in waiting mode
                           {
                              disconnect(player);
                           }

                setNonBlocking(Sockets[gnum*PLAYERS_PER_GAME + player_num]); //Make socket non blocking
                cnt = recv(Sockets[gnum*PLAYERS_PER_GAME + player_num] , buf , sizeof(buf) , 0); //recieve any messages if exist prior to game begining
                memset(buf, 0 , sizeof(buf)); //ignore them
                setBlocking(Sockets[gnum*PLAYERS_PER_GAME + player_num]); //Make socket blocking again for recv

               } //close player->state == STANDBY
            if (player->state == GAME_IS_FULL)
                     {
                        char subName[30]; //This will hold the name of the player from the message recieved
                        while(1) //Loop indefinately waiting for messages
                           {
                               if( ( cnt = recv(Sockets[gnum*PLAYERS_PER_GAME + player_num], buf, sizeof (buf),0) ) >0   ) //Recoeve messages from both serving client and other players
                                    {
                                       /* Now find the name of the message sender
                                       if the message was sent from serving client sent it to other threads
                                       else the message was recieved from another thread ( client) so send it to
                                       our serving client, ignore [ before name and ] after then copy exactly to subName */
                                       i = 1;
                                       while ( buf[i] != ']')
                                       {
                                          subName[i-1]= buf[i];
                                          i++;
                                       }
                                       subName[i] = '\0';

                                       if ( strcmp(player->name, subName) == 0) //The message was sent from the serving client, send it to all other players
                                          {
                                             for(i=0; i<PLAYERS_PER_GAME; i++)
                                                {
                                                   if ( Sockets[gnum*PLAYERS_PER_GAME + i]!=0 && i!=player_num) //Send to everyone except to our serving client
                                                   {
                                                            send( Sockets[gnum*PLAYERS_PER_GAME + i] , buf, sizeof(buf), 0);
                                                   }
                                                }
                                          }
                                       else //The message was sent from another thread who serves another client from the same game
                                          {
                                             send( Sockets[gnum*PLAYERS_PER_GAME + player_num] , buf, sizeof(buf), 0); //Send this message to serving client

                                          }
                                          memset(buf, 0 , sizeof(buf));
                                          memset(subName , 0 , sizeof(subName));
                                       } //close if cnt
                                 else if ( cnt == 0 ) //Player has disconnected, recv returned with 0
                                 {
                                          GameInfo[gnum*(ITEMS_PER_GAME+1)] --; //We have accepted the player so we have to reduce the current players by 1
                                          if ( GameInfo[gnum*(ITEMS_PER_GAME +1)] == 0) //All players left game, make it available to other players to connect.
                                             {
                                                printf("Game %d ended\n", player->gnum);

                                                for(i=0; i<PLAYERS_PER_GAME; i++) //Make avaialble every spot
                                                   {
                                                       SpotStatus[ gnum*PLAYERS_PER_GAME + i] = 0;
                                                   }

                                             }

                                       disconnect(player);
                                 }

                           } //close while(1)

                     } //close player->state == GAME_IS_FULL

}// close accept_client






void *sig_handler(void* arg)
{
   sigset_t *set = arg;
   snprintf(alrtmsg, sizeof(alrtmsg),"Waiting for %d players\n" , PLAYERS_PER_GAME-GameInfo[gnum*(ITEMS_PER_GAME +1)]);
   int s, sig, i,cnt, gnum, player_num;

   for (;;)
    {
         s = sigwait(set, &sig); //Wait for a signal to catch
         if (s != 0)
             perror("sigwait");
         switch( sig ) //Find what signal was caught
         {
           case SIGALRM:
               for(i=0; i<MAX_CLIENTS; i++)
                  {
              //       printf("SpotStatus[%d] is %d\n", i, SpotStatus[i]);
                     if ( SpotStatus[i] == 1 )
                        {

                              gnum = i/PLAYERS_PER_GAME; //Find the game number according to i
                              snprintf(alrtmsg, sizeof(alrtmsg),"Waiting for %d players\n" , PLAYERS_PER_GAME-GameInfo[gnum*(ITEMS_PER_GAME +1)]);
                              cnt = send(Sockets[i], alrtmsg, strlen(alrtmsg), 0);
                        }

                  }
               alarm(5);
               break;
           case SIGINT:
                  pthread_kill( main_tid, SIGUSR1); //Break accept on main
                  break;
           default: printf("Signal %d caught, " , sig);
         }


   }
}


void disconnect(Player_t* player)
{

   int gnum = player->gnum, player_num = player->num;
   printf("Thread in game %d with number %d exiting (disconnect)\n", player->gnum , player->num);
   pthread_mutex_lock( &mutGen);
    if ( (player->state == PLAYER_CONNECTED) || (player->state == STANDBY)) //disconnected before game begins, make the spot available for another one to connect
      {
         SpotStatus[gnum*PLAYERS_PER_GAME + player_num] = 0;
      }

   close(Sockets[gnum*PLAYERS_PER_GAME + player_num]);
   Sockets[gnum*PLAYERS_PER_GAME + player_num] = 0;
   threads[gnum*PLAYERS_PER_GAME + player_num] = 0;
   ClientsNum--;
   pthread_mutex_unlock( &mutGen);
   pthread_exit(NULL);
}

void exit_all()
{
   int i=0,s=0;
   void* res;
/* Free resources, only free necessary though */
   close(sockfd);
   close(new_sfd);
   for(i=0; i<MAX_CLIENTS; i++)
      close( Sockets[i]);

   pthread_attr_destroy(&attr);
   pthread_mutex_destroy( &mutGen);
   free(SpotStatus);
   free(GameInfo);
   free(threads);
   free(Sockets);

   printf("main thread exits\n");
   exit(1);

}


/* This function takes as arguments the message send from player containing his name and the items he selected,
It stores them to player->inventory and player-> item_selected if called by reference
item_selected will be used after to show if any items are missing */

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
int checkIfAvailable(int* player_inventory, int gnum)
{
      pthread_mutex_lock ( &mutGen); //Lock general mutex
      int i = 0;
      for(i = 0; i<ITEMS_PER_GAME; i++)
         {
            if(player_inventory[i] > GameInfo[gnum*(ITEMS_PER_GAME+1) + (i+1)] ) //We found one item that doesn't exist on game's inventory, return with an error indication.
               {
                  printf("Item is %d and shared is %d\n" , player_inventory[i] , GameInfo[gnum*(ITEMS_PER_GAME+1) + (i+1)]);
                  pthread_mutex_unlock ( &mutGen); //Unlock general mutex
                  return -1;
               }

         }
       //Every item quota requested exists on server.
       for(i = 0; i<ITEMS_PER_GAME; i++)
          GameInfo[gnum*(ITEMS_PER_GAME+1) + (i+1)] -=  player_inventory[i]; //decrease supplement

      pthread_mutex_unlock ( &mutGen); //Unlock general mutex
      return 1;
}


/* This functions sets the socket to non blocking mode for functions such as recv etc
uses fcntl which preforms an operation on the passed fd */
void setNonBlocking(int sock)
{
    int opts;

    opts = fcntl(sock,F_GETFL); //Get flag
    if (opts < 0) {
        perror("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }
    opts = (opts | O_NONBLOCK); //Make it O_NONBLOCK
    if (fcntl(sock,F_SETFL,opts) < 0) {
        perror("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }
    return;
}
/* This functions sets the socket back to blocking mode */
void setBlocking(int sock)
{
    int opts;

    opts = fcntl(sock,F_GETFL);
    if (opts < 0) {
        perror("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }
    opts = opts & (~O_NONBLOCK); //Make it ~O_NONBLOCK meaning blocking
    if (fcntl(sock,F_SETFL,opts) < 0) {
        perror("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }
    return;
}
