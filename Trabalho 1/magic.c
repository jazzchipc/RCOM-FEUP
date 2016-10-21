/*Non-Canonical Input Processing*/
#include "utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#define TRANSMITTER 1
#define RECEIVER 0

#define RR 0x01
#define REJ 0x05 //todo mudar isto

#define DEBUG 1

volatile int STOP=FALSE;

int flag = 1; /* 1 if an alarm has not been set and a SET needs to be sent. 0 if it's waiting for a response*/
int numOfTries = 1; /* number tries made to send SET */


struct termios oldtio,newtio;
struct applicationLayer appL;
struct linkLayer linkL;


// ALARM HANDLER

void alarmHandler()
{
  if (DEBUG)
  {
    printf("handled alarm. Try number: %d\n", numOfTries);
  }
  flag = 1;
  numOfTries++;
}

// BEGIN OF STUFFING FUNCTIONS

/*
    Returns the number of bytes that was either a FLAG or a ESCAPE and have been altered.
*/
int byteStuff(Array* inArray, Array* outArray)
{
    int count = 0;

    initArray(outArray, 1); // to reset the array, so information can't be corrupted

    for(int i = 0; i < inArray->used; i++)
    {
        if (inArray->array[i] == FLAG)
        {
            insertArray(outArray, ESCAPE);
            insertArray(outArray, (FLAG^XOR_BYTE));
            count++;
        }

        else if (inArray->array[i] == ESCAPE)
        {
            insertArray(outArray, ESCAPE);
            insertArray(outArray, (ESCAPE^XOR_BYTE));
            count++;
        }

        else
        {
            insertArray(outArray, inArray->array[i]);
        }
    }

    return count;
}

int byteUnstuff(Array* inArray, Array* outArray)
{
    int count = 0;

    initArray(outArray, 1);

    for(int i = 0; i < inArray->used; i++)
    {
        if ((inArray->array[i] == ESCAPE) && (inArray->array[i+1] == (FLAG^XOR_BYTE)))
        {
            insertArray(outArray, FLAG);
           
            i++; // to skip next byte evaluation
            count++;
        }

        else if ((inArray->array[i] == ESCAPE) && (inArray->array[i+1] == (ESCAPE^XOR_BYTE)))
        {
            insertArray(outArray, ESCAPE);
            
            i++;
            count++;
        }

        else
        {
            insertArray(outArray, inArray->array[i]);
        }
    }

    return count;
}

// END OF STUFFING FUNCTIONS

// FUNCTION FOR RECEIVER TO VERIFY A SET

void receive_set(int fd){

  	unsigned char receivedSET[5];	// array to store SET bytes

	//CYCLE
    while (STOP==FALSE)  // loop for input
	{      
     	
		int res = read(fd,receivedSET, sizeof(receivedSET)); // reads the flag value

		if(DEBUG)
		{
			printf ("%d bytes received\n", res);
			printf ("SET: 0x%x , 0x%x, 0x%x, 0x%x, 0x%x\n", receivedSET[0], receivedSET[1], receivedSET[2], receivedSET[3], receivedSET[4]);
		}	

    	if (res >= 1)
		  {
        if (DEBUG)
          printf ("badSET = %d\n", badSET(receivedSET));

        

        if(!badSET(receivedSET))
        {
          res = write(fd, linkL.frame, sizeof(linkL.frame)); // send response
          STOP=TRUE;	//end cycle

          if(DEBUG)
          {
            printf("Sent response UA.\n");
            printf("%d bytes written\n", res);
          }
        }

        break;
    	}
	}
}

int send_cycle(int fd, char * msg){
	
  (void)signal(SIGALRM, alarmHandler); /* sets alarmHandler function as SIGALRM handler*/

  int res = 0;

  //Cycle that sends the SET bytes, while waiting for UA
  while (numOfTries <= MAX_TRIES)
  {
    if(flag)
    {
      if(DEBUG)
      {
        printf("linkL.frame: ");
        printHexArray(linkL.frame, 10);
      }

      res = write(fd, linkL.frame, sizeof(linkL.frame)); // writes the flags

      if(DEBUG)
      {
        printf("%d bytes written to receiver: 0x%x 0x%x 0x%x 0x%x 0x%x\n", res, linkL.frame[0], linkL.frame[1], linkL.frame[2], linkL.frame[3], linkL.frame[4]);
      }

      alarm(3); /* waits 3 seconds, then activates a SIGALRM */
      flag = 0; /* doesn't resend a signal until an alarm is handled */
    }

    res = read(fd, msg, sizeof(msg)); // read feedback

    if (res >= 1) // if it read something
      break;
  }

  if(numOfTries == MAX_TRIES && res < 1)
  {
    printf("ERROR: No response from receiver.\n");
    return (-1);
  }

  numOfTries = 0;

  flag = 1;

  return res;
}


void send_set(int fd){
	int tries = 3; //number of tries to receive feedback
	int res;
	char* msg = (unsigned char *) malloc(5*sizeof(unsigned char));;
  
	res = send_cycle(fd, msg);
    if(res == -1) {
    	printf("deu erro a enviar!");
    	return;
    }
    //todo ver isto dp
    
  if (res < 1)
  {
    printf("ERROR: no message received.\n");
  }  
	else if(badUA(msg)) {
    printf("ERROR: bad UA received.\n");
    exit(-1);
  }
  else
  {
    if(DEBUG)
    {
      printf("Received valid UA.\n");
    }
  }

  if (DEBUG)
  {
    printf("I'm outside the feedback loop!\n");
    printf("%d bytes received: ", res);
    printf("0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", msg[0], msg[1], msg[2], msg[3], msg[4]);
    printf("badUA: %d\n", badUA(msg));
  }


	free(msg);
}


int llopen(){
	int fd;
	unsigned char trama_su[5];
	
	trama_su[0] = FLAG;
	trama_su[1] = A_SND;
	trama_su[2] = C_UA;
	trama_su[3] = (A_SND^C_UA);
	trama_su[4] = FLAG;
	if(appL.status == TRANSMITTER){
		trama_su[2] = C_SET;
		trama_su[3] = (A_SND^C_SET);
	}

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */
	
	
    fd = open(linkL.port, O_RDWR | O_NOCTTY );
    if (fd <0) {perror(linkL.port); exit(-1); }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 0;   /* blocking read until 5 chars received */


  /* 
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a 
    leitura do(s) prÃ³ximo(s) caracter(es)
  */

    tcflush(fd, TCIOFLUSH);

    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    if(DEBUG)
    {
      printf("New termios structure set\n");
    }
	
	memcpy(linkL.frame, trama_su, sizeof(trama_su));

  if(DEBUG)
  {
  printf("trama_su: %x %x %x %x %x %x\n", trama_su[0], trama_su[1], trama_su[2], trama_su[3],trama_su[4], trama_su[5]);
	printf("frame: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", linkL.frame[0], linkL.frame[1], linkL.frame[2], linkL.frame[3], linkL.frame[4], linkL.frame[5]);
  }

	if(appL.status == TRANSMITTER)
		send_set(fd);
	else 
		receive_set(fd);
	
	return fd;
}

void close_set(int fd){
	int tries = 3; //number of tries to receive feedback
	int res;
	char buf[MAX_SIZE];
	linkL.frame[2] = C_DISC;

	res = send_cycle(fd, buf);

	if(buf[2] == C_DISC){
			printf("read disconnect success");
			linkL.frame[2] = C_UA;
			res = write(fd, linkL.frame, 5);
		}

	if(DEBUG)
		printf("I'm outside the feedback loop!\n");
}

int llwrite(int fd, char* buffer, int length)
{
  if(DEBUG)
  {
    printf ("insed llwrite \n");
  }



  Array msgBuffer; // strcut to store message buffer
  initArray(&msgBuffer, 1);

  if(DEBUG)
  {
    printf ("insed after msgBuffer \n");
  }

  copyArray(buffer, &msgBuffer, length);

  if(DEBUG)
  {
    printf ("insed after copy \n");
  }
  
  Array byteStuffed;  // struct to store converted array
  initArray(&byteStuffed, 1);

  if(DEBUG)
  {
    printf ("Inside llwrite. Got after the declaration of arrays.\n");
  }

  if(DEBUG)
  {
    printf ("Msg array: ");
    printHexArray(msgBuffer.array, 10);
    printf ("\n");
  }

  int alteredBytes = byteStuff(&msgBuffer, &byteStuffed);

  if(DEBUG)
  {
    printf ("Inside llwrite. Got after the byte stuffing.\n");
  }

  if(DEBUG)
  {
    printf ("Altered bytes on llwrite debuffing: %d\n", alteredBytes);
  }

  char response[MAX_SIZE];

  memcpy(linkL.frame, byteStuffed.array, byteStuffed.used);

  send_cycle(fd, &response);
	
  if(DEBUG)
  {
    printf ("Stuffed array: ");
    printHexArray(byteStuffed.array, 10);
    printf ("\n");
  }

  return 0;

  free(&msgBuffer);
  free(&byteStuffed);
}

int llread(int fd, char* buffer)
{
  tcflush(fd, TCIOFLUSH);

  STOP=FALSE;

  //CYCLE
  while (STOP==FALSE)  // loop for input
	{      
		int res = read(fd, buffer, sizeof(buffer));

    	if (res >= 1)
		  {
        if(DEBUG)
        {
          printf ("Buffer inside llread: ");
          printHexArray(buffer, 10);
          printf ("\n");
        }

        break;
    	}
	}

  return 0;
}

int llclose(int fd){
	/*
	if(appL.status == TRANSMITTER) 
	else do do chica chica boo boo
	*/
	sleep(2);
	
    if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    close(fd);
}

int main(int argc, char** argv)
{
    
	//todo mudar isto para ter 3 argumentos (nao me apetece por agr lol :D)
    if ((argc < 3) ||
      ((strcmp("/dev/ttyS0", argv[1]) != 0) &&
       (strcmp("/dev/ttyS1", argv[1]) != 0))){
    printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1 <t or r>\n");
    exit(1);
 	}

 	if(strcmp(argv[2],"t") == 0) appL.status = TRANSMITTER; //mudar dp
 	else if(strcmp(argv[2],"r") == 0) appL.status = RECEIVER;
 	else{
 		printf("2nd argument must be t(transmitter) our r (receiver)");
 		exit(2);
 	}

	strcpy(linkL.port, argv[1]);

  if(DEBUG)
  {
    printf ("linkL.port in main: %s\n", linkL.port);
  }
	
	
	int fd = llopen();

  sleep(2);

  if(appL.status == TRANSMITTER)
  {
    unsigned char msg[4]= {FLAG, FLAG, ESCAPE, 1};
    printHexArray(&msg, 4);
    llwrite(fd, msg, 4);
  }

  else
  {
    char msg[10];
    llread(fd, msg);
    printf ("Received message: ");
    printHexArray(&msg, 10);
  }
	  
  llclose(fd);

    return 0;
}
