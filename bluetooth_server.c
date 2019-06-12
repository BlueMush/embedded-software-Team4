#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>
#include <wiringPi.h>

#define MAX_TIMINGS    85
#define LED1 18//BCM_GPIO 18   LED
#define DHT_PIN 3 //BCM_GPIO 23  DHT22


static const unsigned short signal_dht = 23; //dht22  signal 로제어
unsigned short data[5] = {0, 0, 0, 0, 0};


short readData()
{
	unsigned short val = 0x00;
	unsigned short signal_length = 0;
	unsigned short val_counter = 0;
	unsigned short loop_counter = 0;
	
	while (1)
	{
		// Count only HIGH signal
		while (digitalRead(signal_dht) == HIGH)
		{
			signal_length++;
			
			// When sending data ends, high signal occur infinite.
			// So we have to end this infinite loop.
			if (signal_length >= 200)
			{				
				return -1;
			}

			delayMicroseconds(1);
		}

		// If signal is HIGH
		if (signal_length > 0)
		{
			loop_counter++;	// HIGH signal counting

			// The DHT22 sends a lot of unstable signals.
			// So extended the counting range.
			if (signal_length < 10)
			{
				// Unstable signal
				val <<= 1;		// 0 bit. Just shift left
			}

			else if (signal_length < 30)
			{
				// 26~28us means 0 bit
				val <<= 1;		// 0 bit. Just shift left
			}

			else if (signal_length < 85)
			{
				// 70us means 1 bit	
				// Shift left and input 0x01 using OR operator
				val <<= 1;
				val |= 1;
			}

			else
			{
				// Unstable signal
				return -1;
			}

			signal_length = 0;	// Initialize signal length for next signal
			val_counter++;		// Count for 8 bit data
		}

		// The first and second signal is DHT22's start signal.
		// So ignore these data.
		if (loop_counter < 3)
		{
			val = 0x00;
			val_counter = 0;
		}

		// If 8 bit data input complete
		if (val_counter >= 8)
		{
			// 8 bit data input to the data array
			data[(loop_counter / 8) - 1] = val;

			val = 0x00;
			val_counter = 0;
		}
	}
}











struct thread_data{  
    int fd;  
    char ip[20];  
};  

void *ThreadMain(void *argument);
bdaddr_t bdaddr_any = {0, 0, 0, 0, 0, 0};
bdaddr_t bdaddr_local = {0, 0, 0, 0xff, 0xff, 0xff};

int _str2uuid( const char *uuid_str, uuid_t *uuid ) {
    /* This is from the pybluez stack */

    uint32_t uuid_int[4];
    char *endptr;

    if( strlen( uuid_str ) == 36 ) {
        char buf[9] = { 0 };

        if( uuid_str[8] != '-' && uuid_str[13] != '-' &&
        uuid_str[18] != '-' && uuid_str[23] != '-' ) {
        return 0;
    }
    // first 8-bytes
    strncpy(buf, uuid_str, 8);
    uuid_int[0] = htonl( strtoul( buf, &endptr, 16 ) );
    if( endptr != buf + 8 ) return 0;
        // second 8-bytes
        strncpy(buf, uuid_str+9, 4);
        strncpy(buf+4, uuid_str+14, 4);
        uuid_int[1] = htonl( strtoul( buf, &endptr, 16 ) );
        if( endptr != buf + 8 ) return 0;

        // third 8-bytes
        strncpy(buf, uuid_str+19, 4);
        strncpy(buf+4, uuid_str+24, 4);
        uuid_int[2] = htonl( strtoul( buf, &endptr, 16 ) );
        if( endptr != buf + 8 ) return 0;

        // fourth 8-bytes
        strncpy(buf, uuid_str+28, 8);
        uuid_int[3] = htonl( strtoul( buf, &endptr, 16 ) );
        if( endptr != buf + 8 ) return 0;

        if( uuid != NULL ) sdp_uuid128_create( uuid, uuid_int );
    } else if ( strlen( uuid_str ) == 8 ) {
        // 32-bit reserved UUID
        uint32_t i = strtoul( uuid_str, &endptr, 16 );
        if( endptr != uuid_str + 8 ) return 0;
        if( uuid != NULL ) sdp_uuid32_create( uuid, i );
    } else if( strlen( uuid_str ) == 4 ) {
        // 16-bit reserved UUID
        int i = strtol( uuid_str, &endptr, 16 );
        if( endptr != uuid_str + 4 ) return 0;
        if( uuid != NULL ) sdp_uuid16_create( uuid, i );
    } else {
        return 0;
    }

    return 1;

}



sdp_session_t *register_service(uint8_t rfcomm_channel) {

    /* A 128-bit number used to identify this service. The words are ordered from most to least
    * significant, but within each word, the octets are ordered from least to most significant.
    * For example, the UUID represneted by this array is 00001101-0000-1000-8000-00805F9B34FB. (The
    * hyphenation is a convention specified by the Service Discovery Protocol of the Bluetooth Core
    * Specification, but is not particularly important for this program.)
    *
    * This UUID is the Bluetooth Base UUID and is commonly used for simple Bluetooth applications.
    * Regardless of the UUID used, it must match the one that the Armatus Android app is searching
    * for.
    */
    const char *service_name = "Armatus Bluetooth server";
    const char *svc_dsc = "A HERMIT server that interfaces with the Armatus Android app";
    const char *service_prov = "Armatus";

    uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid,
           svc_class_uuid;
    sdp_list_t *l2cap_list = 0,
                *rfcomm_list = 0,
                 *root_list = 0,
                  *proto_list = 0,
                   *access_proto_list = 0,
                    *svc_class_list = 0,
                     *profile_list = 0;
    sdp_data_t *channel = 0;
    sdp_profile_desc_t profile;
    sdp_record_t record = { 0 };
    sdp_session_t *session = 0;

    // set the general service ID
    //sdp_uuid128_create(&svc_uuid, &svc_uuid_int);
    _str2uuid("00001101-0000-1000-8000-00805F9B34FB",&svc_uuid);
    sdp_set_service_id(&record, svc_uuid);

    char str[256] = "";
    sdp_uuid2strn(&svc_uuid, str, 256);
    printf("Registering UUID %s\n", str);

    // set the service class
    sdp_uuid16_create(&svc_class_uuid, SERIAL_PORT_SVCLASS_ID);
    svc_class_list = sdp_list_append(0, &svc_class_uuid);
    sdp_set_service_classes(&record, svc_class_list);

    // set the Bluetooth profile information
    sdp_uuid16_create(&profile.uuid, SERIAL_PORT_PROFILE_ID);
    profile.version = 0x0100;
    profile_list = sdp_list_append(0, &profile);
    sdp_set_profile_descs(&record, profile_list);

    // make the service record publicly browsable
    sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
    root_list = sdp_list_append(0, &root_uuid);
    sdp_set_browse_groups(&record, root_list);

    // set l2cap information
    sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
    l2cap_list = sdp_list_append(0, &l2cap_uuid);
    proto_list = sdp_list_append(0, l2cap_list);

    // register the RFCOMM channel for RFCOMM sockets
    sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
    channel = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
    rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
    sdp_list_append(rfcomm_list, channel);
    sdp_list_append(proto_list, rfcomm_list);

    access_proto_list = sdp_list_append(0, proto_list);
    sdp_set_access_protos(&record, access_proto_list);

    // set the name, provider, and description
    sdp_set_info_attr(&record, service_name, service_prov, svc_dsc);

    // connect to the local SDP server, register the service record,
    // and disconnect
    session = sdp_connect(&bdaddr_any, &bdaddr_local, SDP_RETRY_IF_BUSY);
    sdp_record_register(session, &record, 0);

    // cleanup
    sdp_data_free(channel);
    sdp_list_free(l2cap_list, 0);
    sdp_list_free(rfcomm_list, 0);
    sdp_list_free(root_list, 0);
    sdp_list_free(access_proto_list, 0);
    sdp_list_free(svc_class_list, 0);
    sdp_list_free(profile_list, 0);

    return session;
}

char command[100] ={"killall omxplayer"};
char buf[80];
char input[1024] = { 0 };
int flag =0;

char *read_server(int client) {
    
     signal( SIGPIPE, SIG_IGN );
     if(wiringPiSetupGpio() ==-1){
	 printf("fail\n");
	 }  
    // read data from the client
    int bytes_read;
    bytes_read = read(client, input, sizeof(input));
    if (bytes_read > 0) {
      
      
        printf("\n read_server !! ->>received %s \n", input);
        int ret = atoi(input);
        
        if(ret == 0){
            printf("\n 불이꺼집니다.\n");
            pinMode(LED1,OUTPUT);
            digitalWrite(LED1,0);
            flag =0;
           
        }
        else if(ret ==1){
            printf("불이켜집니다.\n");
            pinMode(LED1,OUTPUT);
            digitalWrite(LED1,1);
            if(flag==0){
             system("omxplayer coin.wav");
             system(command);
             flag=1;
              }//if
          }//else if
	
	 else if(ret ==2){
	     write_dht22(client);
	     return input;
	 }
     
        return input;
       }
    
    else if(bytes_read ==0){
        return 0;
    }
    else {
        return NULL;
    }
}



void write_server(int client, char *message) {
    // send data to the client
    char messageArr[1024] = { 0 };
    int bytes_sent;
    //char buf[15] = "1164서울70사6430";
    
   
    //strcpy(messageArr,buf);
    
     
    bytes_sent = write(client, messageArr, strlen(messageArr));
    if (bytes_sent > 0) {
        printf("sent [%s] %d\n", messageArr, bytes_sent);
    }
}



void write_dht22(int client) {
    // send data to the client
    char messageArr[1024] = { 0 };
    int bytes_sent;
    
    float humidity;
	float celsius;
	float fahrenheit;
	short checksum;
  
	// GPIO Initialization
	if (wiringPiSetupGpio() == -1)
	{
		printf("[x_x] GPIO Initialization FAILED.\n");
		return -1;
	}
    
    
     //시작!!
     
		pinMode(signal_dht, OUTPUT);

		// Send out start signal
		digitalWrite(signal_dht, LOW);
		delay(20);					// Stay LOW for 5~30 milliseconds
		pinMode(signal_dht, INPUT);		// 'INPUT' equals 'HIGH' level. And signal read mode

		readData();		// Read DHT22 signal

		// The sum is maybe over 8 bit like this: '0001 0101 1010'.
		// Remove the '9 bit' data using AND operator.
		checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
		
		// If Check-sum data is correct (NOT 0x00), display humidity and temperature
		if (data[4] == checksum && checksum != 0x00)
		{
			// * 256 is the same thing '<< 8' (shift).
			humidity = ((data[0] * 256) + data[1]) / 10.0;
			celsius = data[3] / 10.0;

			// If 'data[2]' data like 1000 0000, It means minus temperature
			if (data[2] == 0x80)
			{
				celsius *= -1;
			}

			fahrenheit = ((celsius * 9) / 5) + 32;

			// Display all data
            char buf1[100];
            
			printf("temp: %6.2f *C (%6.2f *F) | hum: %6.2f %\n\n", celsius, fahrenheit, humidity);
            sprintf(buf1," 온도:%6.2f *C          습도: %6.2f % \n", celsius,humidity);
            strcpy(messageArr,buf1);
            
        }

		else
		{
            char buf1[100];
			printf("다시한번 눌러주세요~!.\n");
            sprintf(buf1,"다시한번 눌러주세요~!.\n");
            strcpy(messageArr,buf1);
            
		}

		// Initialize data array for next loop
		for (unsigned char i = 0; i < 5; i++)
		{
			data[i] = 0;
		}

		delay(1000);
        // DHT22 average sensing period is 2 seconds

    bytes_sent = write(client, messageArr, strlen(messageArr));
 
  wiringPiSetup();
    if (bytes_sent > 0) {
        printf(messageArr);
        printf("\n byte :%d\n",bytes_sent);
    }
}

int main()
{

    pthread_t thread_id;  
  
    signal( SIGPIPE, SIG_IGN );  
      
    
    int port = 5, result, sock, client, bytes_read, bytes_sent;
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    char buffer[1024] = { 0 };
    socklen_t opt = sizeof(rem_addr);

    // local bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = bdaddr_any;
    loc_addr.rc_channel = (uint8_t) port;

    // register service
    sdp_session_t *session = register_service(port);
    // allocate socket
    sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    printf("socket() returned %d\n", sock);

    // bind socket to port 3 of the first available
    
    result = bind(sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
    if(result  !=0){
        perror("bind");
        return 0;
    }
    printf("bind() on channel %d returned %d\n", port, result);
 
    // put socket into listening mode
    result = listen(sock, 1);
    printf("listen() returned %d\n", result);

    //sdpRegisterL2cap(port);
    
    
    while(1)
    {
        // accept one connection
        printf("calling accept()\n");
        client = accept(sock, (struct sockaddr *)&rem_addr, &opt);
        if(client == -1)
        {
            perror("accept");
                 
            continue;
        }
        printf("accept() returned %d\n", client);
    
        ba2str(&rem_addr.rc_bdaddr, buffer);
        fprintf(stderr, "accepted connection from %s\n", buffer);
        memset(buffer, 0, sizeof(buffer));
        
        pthread_create( &thread_id, NULL, ThreadMain, (void*)client);   
    }
    return 0;
    
}



void *ThreadMain(void *argument)  

{  

    char buf[1024];  

  

    pthread_detach(pthread_self());  

    int client = (int)argument;  

 

  

    while(1)  

    {  
        
        
      
        char *recv_message = read_server(client);
       
        if ( recv_message == NULL ){

            printf("client disconnected\n");

            break;

        } 
         
   
        
       // 온습도값 을여기다 가해줘야할듯?
        printf("\n받은값:%s\n", recv_message);

    }  

  

    printf("disconnected\n" );  

    close(client);  

  

    return 0;     

}  
