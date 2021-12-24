#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>
#include <pthread.h>
#include <netinet/in.h> 
#include <sys/socket.h> 
//you write the code here

//For execution mode
#define CLIENT 0
#define SERVER 1
//For thread migration
#define PORT 8080 
#define REG_EBP 10 
#define REG_ESP 15
#define REG_EIP 16
#define FBP_H_ADDR 0x4
#define RAS_ADDR 0x8
#define GREGS_SIZE 0x10
//For socket programming
#define BACKLOG 3
#define MSSG_LEN 1024

typedef struct psu_thread_info 
{
	int mode;                /* Execution flow */
	int mssg_read, socket;   /* Socket Programming */
	struct sockaddr_in addr; /* Socket Programming */
	uint64_t _RAS;            /* Stack Info */
	uint64_t _FPSP;           /* Stack Info */
	uint64_t _FPBP;           /* Stack Info */
	uint64_t _FPstacksize;    /* Stack Info */
	uint64_t *addr_upper_bit; /* Stack Info */
	uint64_t *addr_lower_bit; /* Stack Info */
} psu_thread_info_t;

//Global vairable
psu_thread_info_t obj;
pthread_t thread_id;

void check_status(int flag) 
{
	if (flag < 0 ) 
	{ 
	   printf("Error!!! Please Check ");
           exit(EXIT_FAILURE);
        }
}


void set_RAS(psu_thread_info_t *thread_info, ucontext_t *ctx_ptr)
{
 	//printf("Stack base pointer:%x Stack head pointer:%x\n", (long)ctx_ptr->uc_mcontext.gregs[REG_EBP], (long)ctx_ptr->uc_mcontext.gregs[REG_ESP]);
 	thread_info->_RAS = ctx_ptr->uc_mcontext.gregs[REG_EBP] + RAS_ADDR; //RAS value
	//printf("EBP (previous frame pointer)stored at: %x\n", thread_info->_RAS);  	
}

void set_FPSP(psu_thread_info_t *thread_info, ucontext_t *ctx_ptr)
{
	thread_info->_FPSP = ctx_ptr->uc_mcontext.gregs[REG_EBP] + GREGS_SIZE;
}

void set_FPBP(psu_thread_info_t *thread_info, ucontext_t *ctx_ptr)
{
	thread_info->addr_upper_bit = ctx_ptr->uc_mcontext.gregs[REG_EBP]+ FBP_H_ADDR;
	thread_info->addr_lower_bit = ctx_ptr->uc_mcontext.gregs[REG_EBP];
	thread_info->_FPBP = ((*thread_info->addr_upper_bit << 32) | *thread_info->addr_lower_bit);
        //printf("Upper bit:%x lower bit:%x\n", thread_info->addr_upper_bit, thread_info->addr_lower_bit);
        //printf("previous stack base pointer:%x stack head pointer:%x\n",thread_info->_FPBP, thread_info->_FPSP);
}

void set_stacksize(psu_thread_info_t *thread_info) 
{
	thread_info->_FPstacksize = thread_info->_FPBP - thread_info->_FPSP;
        //printf("stack size:%x\n", thread_info._FPstacksize);
}

/*
void get_ras(psu_thread_info_t *thread_info, char *data)
{
	//First entry in the data from client is RAS
	memcpy(thread_info->_RAS, &data, sizeof(uint64_t));
}

void get_fpstacksize(psu_thread_info_t *thread_info, char *data)
{
	//Second entry in the data from client is _FPstacksize
	memcpy(thread_info->_FPstacksize, *data+0x1, sizeof(uint64_t));
}

uint64_t get_num_stack(psu_thread_info_t *thread_info, char *data)
{
	//Third entry in the data from client is _FPstacksize
	uint64_t num_stack = 0;
	memcpy(num_stack, *data+0x2, sizeof(uint64_t));
	return num_stack;
}

void get_data_buffer(psu_thread_info_t *thread_info, char *data, uint64_t num_stack, char *buf)
{
	//Fourth entry in the data from client is data buffer
	memcpy(*buf, *data+0x3, num_stack*(thread_info->_FPstacksize + GREGS_SIZE));
}
*/	

int calc_num_stack(psu_thread_info_t *thread_info)
{
	uint64_t num_stack =1;
	uint64_t *ptr = thread_info->_FPBP;
	while(*ptr != NULL){
	     ptr = *ptr;
	     num_stack++;
	};
	//printf("Num stack: %x\n",num_stack);
	return num_stack;
}

void psu_thread_setup_init(int mode)
{
	//Read from a file to set up the socket connection between the client and the server
	memset(&obj, '0', sizeof(obj)); //Init stuct obj with '0'
	if (mode == CLIENT) {
	    //Client Mode
	    obj.mode = CLIENT;
            obj.socket = socket(AF_INET, SOCK_STREAM, 0);
            check_status(obj.socket);  
	    obj.addr.sin_family = AF_INET;   //Check how to make it short (obj.addr.xyz)
	    obj.addr.sin_port = htons(PORT);
	} else if (mode == SERVER) {
	    //Server Mode
	    int opt = 1;
            bool status = false;
	    obj.mode = SERVER;
	    obj.socket = socket(AF_INET, SOCK_STREAM, 0);
            check_status(obj.socket);
	    status = setsockopt(obj.socket, SOL_SOCKET,
                                SO_REUSEADDR | SO_REUSEPORT, 
			        &opt, sizeof(opt));
            check_status(status);
	    obj.addr.sin_family = AF_INET; 
	    obj.addr.sin_addr.s_addr = INADDR_ANY; 
	    obj.addr.sin_port = htons(PORT);
	    status = bind(obj.socket, (struct sockaddr *)&obj.addr, sizeof(obj.addr));
            check_status(status);
	} else {
	    printf(" Invalid mode. Please choose either 0/1. \n");
	    exit(EXIT_FAILURE);
	}
	return 0;
}

int psu_thread_create(void * (*user_func)(void*), void *user_args)
{
	// make thread related setup
	if (obj.mode == CLIENT) {
		// create thread and start running the function based on *user_func
		int is_successful = pthread_create(&thread_id, NULL, *user_func, user_args);
                check_status(is_successful);
		pthread_join(thread_id, NULL);
	} else {
		//Skip thread creation as it is not needed. Put server in listetning mode
		server_listen();
	}
	return 0; 
}

void psu_thread_migrate(const char *hostname)
{
	//thread Migration related code
	if(obj.mode == CLIENT) { //Client Mode
	    bool status = false;
	    ucontext_t 	context , *cp = &context;
            					
	    status = inet_pton(AF_INET, hostname , &obj.addr.sin_addr);
            check_status(status); 
	    status = connect(obj.socket, (struct sockaddr *)&obj.addr, sizeof(obj.addr));
            check_status(status); 
	    check_status(getcontext(cp));

	    //Sending EBP from client to server (needed for App1)
	    set_RAS(&obj, cp);
            //Sending stack to server (needed for APP2)
            set_FPSP(&obj, cp);
	    set_FPBP(&obj, cp);
	    //Copying multiple stacks (needed for APP3)
	    set_stacksize(&obj);
	    uint64_t num_stack = calc_num_stack(&obj);

	    //Create mssg
	    char *buffer = (char*) malloc(num_stack*(obj._FPstacksize + GREGS_SIZE)); //size in bytes
            memcpy(buffer, obj._FPSP, num_stack*(obj._FPstacksize + GREGS_SIZE));
            char *sendobject = (char*) malloc(MSSG_LEN);
            //Try packing -unpacking?
            memcpy(sendobject, obj._RAS, sizeof(uint64_t));
	    memcpy(sendobject + sizeof(uint64_t), &obj._FPstacksize, sizeof(uint64_t));
            memcpy(sendobject + 2*sizeof(uint64_t), &num_stack, sizeof(uint64_t));
	    memcpy(sendobject + 3*sizeof(uint64_t), buffer, num_stack*(obj._FPstacksize+GREGS_SIZE));

	    //Send mssg to server
            send(obj.socket , sendobject, MSSG_LEN, 0);
	    //Delete tmp buffer
	    free(buffer);
            free(sendobject);
	    //Exit program execution gracefully as it is server job to execute the rest
	    exit(EXIT_SUCCESS); 
	} else if(obj.mode == SERVER){ //Server Mode
	    //Do nothing
	} else {
	    printf(" How??");  
	    exit(EXIT_FAILURE);
	}

	return 0;
}

void server_listen() 
{
	int addrlen = sizeof(obj.addr); 
        ucontext_t curr_context; 
        bool status = false;
        char * recv = (char *) malloc(MSSG_LEN);
        status = listen(obj.socket, BACKLOG);
	check_status(status);
	int client_socket = accept(obj.socket, (struct sockaddr *)&obj.addr, (socklen_t*)&addrlen);
	check_status(client_socket);
	size_t num_bytes = read(client_socket, &recv, MSSG_LEN);
	check_status(num_bytes);

	//Mssg recieval from client
	//get_ras(&obj, &recv);
	//get_fpstacksize(&obj, &recv);
	uint64_t recv_num_stack = 0; //get_num_stack(&obj, &recv);
        memcpy(&obj._RAS, &recv, sizeof(uint64_t));
        memcpy(&obj._FPstacksize, &recv+0x1, sizeof(uint64_t)); 
        memcpy(&recv_num_stack, &recv+0x2, sizeof(uint64_t));
        char recv_buffer[recv_num_stack*(obj._FPstacksize+ GREGS_SIZE)];
        //get_data_buffer(&obj, &recv, recv_num_stack, &recv_buffer);
	memcpy(&recv_buffer, &recv + 0x3, recv_num_stack*(obj._FPstacksize + GREGS_SIZE));

        //Get current context
	getcontext(&curr_context);

        //APP1 - Setting  eip as ebp recieved
	curr_context.uc_mcontext.gregs[REG_EIP] = obj._RAS;

        //Store exsisitng Return address, previous frame bp before new stack overwrite
        uint64_t *_prevRA = curr_context.uc_mcontext.gregs[REG_EBP] + RAS_ADDR;        
        uint64_t *lower_nibble_addr = (curr_context.uc_mcontext.gregs[REG_EBP]);

	int num_stack = 1;
        for(num_stack = 1; num_stack < recv_num_stack; num_stack++){
		uint64_t prev_ebp = ((char*) recv_buffer + (num_stack+1)*obj._FPstacksize + (num_stack)*GREGS_SIZE);
		memcpy(recv_buffer + num_stack*obj._FPstacksize + (num_stack-1)*GREGS_SIZE, &prev_ebp, sizeof(uint64_t));

	}
        memcpy(recv_buffer+ num_stack*obj._FPstacksize + (num_stack - 1)*GREGS_SIZE, lower_nibble_addr, sizeof(uint64_t));
        memcpy(recv_buffer + num_stack*obj._FPstacksize + (num_stack - 1)*GREGS_SIZE + RAS_ADDR, _prevRA, sizeof(uint64_t));
        
	curr_context.uc_mcontext.gregs[REG_EBP] = recv_buffer + obj._FPstacksize;
        curr_context.uc_mcontext.gregs[REG_ESP] = &recv_buffer;

        //APP2/APP3 - updating bp, ras and sp in current context
	setcontext(&curr_context);
}
