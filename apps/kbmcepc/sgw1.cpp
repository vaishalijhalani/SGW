#include "K_sgw.h"


using namespace std;

#define MAX_THREADS 1

//std::mutex mutex1, mutex2, mutex3, mutex4, mutex5, mutex6;

queue <int> free_port;

struct cadata{
	int fd,num;
};

//State 
unordered_map<uint32_t, uint64_t> s11_id; /* S11 UE identification table: s11_cteid_sgw -> imsi */
unordered_map<uint32_t, uint64_t> s1_id; /* S1 UE identification table: s1_uteid_ul -> imsi */
unordered_map<uint32_t, uint64_t> s5_id; /* S5 UE identification table: s5_uteid_dl -> imsi */
unordered_map<uint64_t, UeContext> ue_ctx; /* UE context table: imsi -> UeContext */

//Not needed for single core
pthread_mutex_t s11id_mux; /* Handles s11_id */
pthread_mutex_t s1id_mux; /* Handles s1_id */
pthread_mutex_t s5id_mux; /* Handles s5_id */
pthread_mutex_t uectx_mux; /* Handles ue_ctx */
pthread_mutex_t epoll_mux; 

struct thread_data{
   int id;
   int core;
   int min;
   int max;
};

struct pgw_data
{
	int sockfd;
	int port;
	struct sockaddr_in c_addr;

};

#define print_error_then_terminate(en, msg) \
  do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)


int lsfd;
struct sockaddr_in server;

void *action(void *arg)
{


	int acfd, portno, n, numev, i, cafd, ccfd, cret,trf, pgw_fd;
	char buf[100];

	long long transactions = 0;
	struct thread_data *my_data;

	my_data = (struct thread_data *) arg;
	int threadID = my_data->id;
	int core_id = my_data->core;
	int min_port = my_data->min;
	int max_port = my_data->max;

	int count,tcount;
	cout << "in thread " << threadID << endl;
	struct sockaddr_in c_addr, a_addr, bserver;
	struct hostent *c_ip;
	
	set<int> srca, srcc, srcr; //set-C-accept/connect/read;
	map<int, int> mm;
	map<int, int> mm1;
	struct cadata cd;
	int buffer = 0;
	
	int returnval,cur_fd, act_type;
	map<int, mdata> fdmap;
	map<int, pgw_data> pgw_port;
	struct mdata fddata;
	struct pgw_data pgdata;
	Packet pkt;
	int pkt_len;
	char * dataptr;
	unsigned char data[1024];

	/*
		SGW Specific data
	*/

	uint32_t s1_uteid_ul;
	uint32_t s5_uteid_ul;
	uint32_t s5_uteid_dl;
	uint32_t s11_cteid_mme;
	uint32_t s11_cteid_sgw;
	uint32_t s5_cteid_ul;
	uint32_t s5_cteid_dl;
	uint64_t imsi;
	uint8_t eps_bearer_id;
	uint64_t apn_in_use;
	uint64_t tai;
	string pgw_s5_ip_addr;
	string ue_ip_addr;
	int pgw_s5_port;
	uint32_t s1_uteid_dl;
	string enodeb_ip_addr;
	int enodeb_port;
	bool res;

	const pthread_t pid = pthread_self();
	cout << "pid " << pid << endl;

	// cpu_set_t: This data set is a bitset where each bit represents a CPU.
    cpu_set_t cpuset;
  

    // CPU_ZERO: This macro initializes the CPU set set to be the empty set.
    CPU_ZERO(&cpuset);
  
    // CPU_SET: This macro adds cpu to the CPU set set.
    CPU_SET(core_id, &cpuset);
 
    // pthread_setaffinity_np: The pthread_setaffinity_np() function sets the CPU affinity mask of the thread thread to the CPU set pointed to by cpuset. If the call is successful, and the thread is not currently running on one of the CPUs in cpuset, then it is migrated to one of those CPUs.
    const int set_result = pthread_setaffinity_np(pid, sizeof(cpu_set_t), &cpuset);
    if (set_result != 0) {
 
    print_error_then_terminate(set_result, "pthread_setaffinity_np");
    }
 
    // Check what is the actual affinity mask that was assigned to the thread.
    // pthread_getaffinity_np: The pthread_getaffinity_np() function returns the CPU affinity mask of the thread thread in the buffer pointed to by cpuset.
    const int get_affinity = pthread_getaffinity_np(pid, sizeof(cpu_set_t), &cpuset);
    if (get_affinity != 0) {
 
    print_error_then_terminate(get_affinity, "pthread_getaffinity_np");
    }

   	int epfd = epoll_create(MAXEVENTS + 5);
   	if( epfd == -1){
   		cout<<"Error: epoll create"<<'\n';
   		exit(-1);
   	}
   	int retval;
   	struct epoll_event ev, rev[MAXEVENTS];
   	
   	pthread_mutex_lock(&epoll_mux);
   	ev.data.fd = lsfd;
   	ev.events = EPOLLIN | EPOLLET;
   	retval = epoll_ctl( epfd, EPOLL_CTL_ADD, lsfd, &ev);
   	pthread_mutex_unlock(&epoll_mux);


   	if( retval == -1) {
   		cout<<"Error: epoll ctl lsfd add"<<'\n';
   		exit(-1);
   	}

   	cout<<"Entering Loop"<<'\n';
   	count = 0;
   	tcount=0;
   	trf = 0;
   	transactions = 0;
   	int start_port = min_port;
   	socklen_t blen;
   	blen = sizeof(bserver);

	
   	//cout << "lsfd " << lsfd << endl;
   	//cout << "ccfd " << ccfd << endl;


   	while( 1 )
   	{


   		numev = epoll_wait( epfd, rev, MAXEVENTS, -1);
   		//cout << numev << endl;
   		if(numev < 0)
   		{
				cout<<"Error: EPOLL wait!"<<'\n';
				exit(-1);
		}

		if(numev == 0)
		{
				if(trf == 1)
				{
					cout<<"Throughput :"<<transactions<<'\n';
					trf = 0;
					transactions = 0;
				}
				//cout<<"Tick "<<'\n';
		}

		for( i = 0; i < numev; i++)
		{


			trf = 1;
			//Check Errors
			if(	(rev[i].events & EPOLLERR) || (rev[i].events & EPOLLHUP)) 
			{

					cout<<"ERROR: epoll monitoring failed, closing fd"<<'\n';
					if(rev[i].data.fd == lsfd){
						cout<<"Oh Oh, lsfd it is"<<'\n';
						exit(-1);
					}
					close(rev[i].data.fd);
					continue;

			}

			
			
			else if(rev[i].events & EPOLLIN)
			{							

					struct sockaddr_in from;
					bzero((char *) &from, sizeof(from) );
					socklen_t fromlen = sizeof(from);
					bzero(buf, 100);
					cafd = rev[i].data.fd;
					//cout << cafd << endl;
					if(cafd == lsfd)
					{
						while(1)
						{
					
								pkt.clear_pkt();
						        n = recvfrom(cafd, pkt.data, BUF_SIZE, 0, (struct sockaddr *) &from, &fromlen);
						        if ( n < 0) {
						           break;
						           cout<<"Error : Read Error "<<'\n';
						           exit(-1);
						        }

						        int port = ntohs(from.sin_port);
						        cout << port << endl;

						        pkt.data_ptr = 0;
								pkt.len = retval;
								pkt.extract_gtp_hdr();


							if(pkt.gtp_hdr.msg_type == 1)
							{//MME attach 3
	

								pkt.extract_item(s11_cteid_mme);
								pkt.extract_item(imsi);
								pkt.extract_item(eps_bearer_id);
								pkt.extract_item(pgw_s5_ip_addr);
								pkt.extract_item(pgw_s5_port);
								pkt.extract_item(apn_in_use);
								pkt.extract_item(tai);
								
								//cout << "packetid " << pkt.gtp_hdr.msg_type << endl;
								s1_uteid_ul = s11_cteid_mme;
								s5_uteid_dl = s11_cteid_mme;
								s11_cteid_sgw = s11_cteid_mme;
								s5_cteid_dl = s11_cteid_mme;

								pthread_mutex_lock(&s11id_mux);
								s11_id[s11_cteid_sgw] = imsi;
								pthread_mutex_unlock(&s11id_mux);
								pthread_mutex_lock(&s1id_mux);
								s1_id[s1_uteid_ul] = imsi;
								pthread_mutex_unlock(&s1id_mux);
								pthread_mutex_lock(&s5id_mux);
								s5_id[s5_uteid_dl] = imsi;
								pthread_mutex_unlock(&s5id_mux);
								TRACE( cout << "attach 3 packet received\n";)
								TRACE(cout<<"Attach 3 in"<<imsi<< " "<<s11_cteid_mme<<endl;)
								
								pthread_mutex_lock(&uectx_mux);
								ue_ctx[imsi].init(tai, apn_in_use, eps_bearer_id, s1_uteid_ul, s5_uteid_dl, s11_cteid_mme, s11_cteid_sgw, s5_cteid_dl, pgw_s5_ip_addr, pgw_s5_port);
								ue_ctx[imsi].tai = tai;
								pthread_mutex_unlock(&uectx_mux);

								pkt.clear_pkt();
								pkt.append_item(s5_cteid_dl);
								pkt.append_item(imsi);
								pkt.append_item(eps_bearer_id);
								pkt.append_item(s5_uteid_dl);
								pkt.append_item(apn_in_use);
								pkt.append_item(tai);
								pkt.prepend_gtp_hdr(2, 1, pkt.len, 0);

						        

						        ccfd = socket(AF_INET, SOCK_DGRAM, 0);
							   	if(ccfd < 0){
							   		cout << "ERROR: C Socket Open\n";
							   		exit(-1);
							   	}

							   	make_socket_nb(ccfd);
							   	int uflag = 1;

							   	if (setsockopt(ccfd, SOL_SOCKET, SO_REUSEADDR, &uflag, sizeof(uflag)) < 0)
							    {
							            cout<<"Error : server setsockopt reuse"<<endl;
							            exit(-2);
							    }

							    bzero((char *) &bserver, sizeof(bserver) );
								bserver.sin_family = AF_INET;
								bserver.sin_addr.s_addr = inet_addr("192.168.122.167");
								blen = sizeof(bserver);

								bserver.sin_port = htons(start_port);

								if(bind(ccfd, (struct sockaddr *) &bserver, sizeof(bserver)) < 0)
								{
									while (bind(ccfd, (struct sockaddr *) &bserver, sizeof(bserver)) < 0) 
									{
									      	start_port++;
									      	bserver.sin_port = htons(start_port);  
									}

								}

								else
									start_port++;

								if(start_port>max_port)
									start_port = min_port;

								pgdata.sockfd = ccfd;
								pgdata.port = start_port-1;
								memcpy (&pgdata.c_addr, &bserver, sizeof (pgdata.c_addr));
								pgw_port.insert(make_pair(port, pgdata));

							   	srcr.insert(ccfd);
							   	buffer = 0;
						        buffer = atoi(buf);
						        mm[ccfd] =  port;
						        mm1[ccfd] = buffer;

							    bzero((char *) &c_addr, sizeof(c_addr) );
								c_addr.sin_family = AF_INET;
								c_addr.sin_addr.s_addr = inet_addr("192.168.122.157");
								c_addr.sin_port = htons(8000);
								ev.data.fd = ccfd;
							   	ev.events = EPOLLIN;
							   

							   	retval = epoll_ctl( epfd, EPOLL_CTL_ADD, ccfd, &ev);
							   	if( retval == -1) {
							   		cout<<"Error: epoll ctl ccfd add"<<'\n';
							   		exit(-1);
							   	}

						        //count++;

			        			//cout << count << " in thread "  << threadID << endl;
						        
						        n = sendto(ccfd, pkt.data, pkt.len, 0,(const struct sockaddr *)&c_addr,sizeof(c_addr));

								if(n <= 0){
									cout<<"Error : Write Error"<<'\n';
									exit(-1);
								}

								//close(ccfd);

								fddata.initial_fd = port;
								fddata.pgw_port = start_port-1;
								fddata.tid = s11_cteid_mme;
								fddata.guti = imsi;
								memcpy(fddata.buf, pkt.data, pkt.len);
								fddata.buflen = pkt.len;
								fdmap.insert(make_pair(ccfd, fddata));

							}

							else
							if(pkt.gtp_hdr.msg_type == 2)
							{//attach 4 from mme

								mlock(s11id_mux);
								if (s11_id.find(pkt.gtp_hdr.teid) != s11_id.end()) {
								imsi = s11_id[pkt.gtp_hdr.teid];
								}
								else
								{
									cout<<"IMSI error"<<endl;
									exit(-1);
								}
								munlock(s11id_mux);

								if (imsi == 0) {
								TRACE(cout << "sgw_handlemodifybearer:" << " :zero imsi " << pkt.gtp_hdr.teid << " " << pkt.len << ": " << imsi << endl;)
								exit(-1);
								}

								pkt.extract_item(eps_bearer_id);
								pkt.extract_item(s1_uteid_dl);
								pkt.extract_item(enodeb_ip_addr);
								pkt.extract_item(enodeb_port);	
								
								mlock(uectx_mux);
								ue_ctx[imsi].s1_uteid_dl = s1_uteid_dl;
								ue_ctx[imsi].enodeb_ip_addr = enodeb_ip_addr;
								ue_ctx[imsi].enodeb_port = enodeb_port;
								s11_cteid_mme = ue_ctx[imsi].s11_cteid_mme;
								munlock(uectx_mux);

								TRACE(cout<<"In attach 4 "<< imsi<< " "<<s11_cteid_mme << "recv "<< s1_uteid_dl<<endl;)
								res = true;
								pkt.clear_pkt();
								pkt.append_item(res);
								pkt.prepend_gtp_hdr(2, 2, pkt.len, s11_cteid_mme);
								//pkt.prepend_len();

								bzero((char *) &a_addr, sizeof(a_addr) );
								a_addr.sin_family = AF_INET;
								a_addr.sin_addr.s_addr = inet_addr("192.168.122.147");
								a_addr.sin_port = htons(port);

								n = sendto(cafd, pkt.data, pkt.len, 0,(const struct sockaddr *)&a_addr,sizeof(a_addr));	
								if(n < 0){
									cout<<"Error MME write back to RAN A2"<<endl;
									exit(-1);
								}

							}//4th attach

							else//Detach 
							if(pkt.gtp_hdr.msg_type == 3)
							{
								mlock(s11id_mux);
								if (s11_id.find(pkt.gtp_hdr.teid) != s11_id.end()) {
									imsi = s11_id[pkt.gtp_hdr.teid];
								}
								else
								{
									cout<<"IMSI error"<<endl;
									exit(-1);
								}
								munlock(s11id_mux);

								pkt.extract_item(eps_bearer_id);
								pkt.extract_item(tai);
								
								mlock(uectx_mux);
								if (ue_ctx.find(imsi) == ue_ctx.end()) {
									TRACE(cout << "sgw_handledetach:" << " no uectx: " << imsi << endl;)
									exit(-1);
								}
								munlock(uectx_mux);

								if(gettid(imsi) != pkt.gtp_hdr.teid)
								{
									cout<<"GUTI not equal Detach acc"<<imsi<<" "<<pkt.gtp_hdr.teid<<endl;
									exit(-1);
								}	

								mlock(uectx_mux);
								pgw_s5_ip_addr = ue_ctx[imsi].pgw_s5_ip_addr;
								pgw_s5_port = ue_ctx[imsi].pgw_s5_port;
								s5_cteid_ul = ue_ctx[imsi].s5_cteid_ul;
								s11_cteid_mme = ue_ctx[imsi].s11_cteid_mme;
								s11_cteid_sgw = ue_ctx[imsi].s11_cteid_sgw;
								s1_uteid_ul = ue_ctx[imsi].s1_uteid_ul;
								s5_uteid_dl = ue_ctx[imsi].s5_uteid_dl;	
								munlock(uectx_mux);

								TRACE(cout<<"In detach "<<imsi<<" s5 "<< s5_cteid_ul<< "s11 "<<s11_cteid_mme <<" rcv " << pkt.gtp_hdr.teid<<endl;)
								
								pkt.clear_pkt();
								pkt.append_item(eps_bearer_id);
								pkt.append_item(tai);
								pkt.prepend_gtp_hdr(2, 4, pkt.len, s5_cteid_ul);
								//pkt.prepend_len();

								//send to pgw
								pgdata = pgw_port[port];
								returnval = sendto(pgdata.sockfd, pkt.data , pkt.len, 0, (const struct sockaddr *)&(c_addr),sizeof(c_addr));

								if(returnval < 0)
								{
									cout<<"Error: pgw write detach "<<errno<<endl;
									exit(-1);
								}

								TRACE(cout << "detach sent to pgw\n";)

								/*fdmap.erase(cur_fd);
								fddata.act = 4;
								fddata.tid = s11_cteid_mme;
								fddata.guti = imsi;
								fddata.buflen = 0;
								fddata.initial_fd = sgw_fd;
								memset(fddata.buf, '\0', 500);
								fdmap.insert(make_pair(pgw_fd, fddata));*/
								//goto case 4

							}


			    		}

			    	}


			      

			        else if(srcr.find(rev[i].data.fd) != srcr.end())
			        {
					    
					  

								cout << " data came from pgw\n";
								int retfd = rev[i].data.fd;
								pkt.clear_pkt();
								bzero((char *) &from, sizeof(from) );
						        n = recvfrom(retfd, pkt.data, BUF_SIZE, 0, (struct sockaddr *) &from, &fromlen);
						        if ( n < 0) {
						        	//break;
						           cout<<"Error : Read Error "<<'\n';
						           exit(-1);
						        }
						        pkt.data_ptr = 0;
								pkt.len =retval;
							

								TRACE(cout << "sgw_handlecreatesession:" << " create session response received from pgw: " << imsi << endl;)
			
						        int num = mm1[retfd];
						        int num2 = mm[retfd];
						        mm.erase(retfd);
						        mm1.erase(retfd);
						        //srcr.erase(retfd);

						        fddata = fdmap[retfd];
						        imsi = fddata.guti;

								pkt.extract_gtp_hdr();

						if(pkt.gtp_hdr.msg_type == 1)
						{//MME attach 3
					
								pkt.extract_item(s5_cteid_ul);
								pkt.extract_item(eps_bearer_id);
								pkt.extract_item(s5_uteid_ul);
								pkt.extract_item(ue_ip_addr);

								mlock(uectx_mux);
								ue_ctx[imsi].s5_uteid_ul = s5_uteid_ul;
								ue_ctx[imsi].s5_cteid_ul = s5_cteid_ul;
								munlock(uectx_mux);

								TRACE(cout<<"Attach 3 received from PGW "<< imsi <<" " << fddata.tid<< " rcv "<<s5_cteid_ul<<endl;)
								
								s1_uteid_ul = fddata.tid;
								s11_cteid_sgw = fddata.tid;
								
								pkt.clear_pkt();
								pkt.append_item(s11_cteid_sgw);
								pkt.append_item(ue_ip_addr);
								pkt.append_item(s1_uteid_ul);
								pkt.append_item(s5_uteid_ul);
								pkt.append_item(s5_uteid_dl);
								pkt.prepend_gtp_hdr(2, 1, pkt.len, s11_cteid_mme);
								bzero((char *) &a_addr, sizeof(a_addr) );
								a_addr.sin_family = AF_INET;
								a_addr.sin_addr.s_addr = inet_addr("192.168.122.147");
								a_addr.sin_port = htons(fddata.initial_fd);

								n = sendto(lsfd, pkt.data, pkt.len, 0,(const struct sockaddr *)&a_addr,sizeof(a_addr));	
								if(n < 0){
									cout<<"Error : Write Error"<<'\n';
									exit(-1);
								}
								


						     	//close(retfd);

						     

			        			//cout << " in thread "  << threadID << "by C" << endl;


						}

						else if(pkt.gtp_hdr.msg_type == 4)
						{

							cout << "detach reply from pgw "<< endl;

							pkt.extract_item(res);
							if (res == false) {
								TRACE(cout << "sgw_handledetach:" << " pgw detach failure: " << imsi << endl;)
								exit(-1);
							}
							s11_cteid_mme = fddata.tid;
							TRACE(cout<<"Detach received "<<pkt.gtp_hdr.teid <<" s11"<<s11_cteid_mme<<endl;)
							
							pkt.clear_pkt();
							pkt.append_item(res);
							pkt.prepend_gtp_hdr(2, 3, pkt.len, s11_cteid_mme);
							//pkt.prepend_len();
		
							pthread_mutex_lock(&s11id_mux);
							if (s11_id.find(s11_cteid_mme) != s11_id.end()) {
									imsi = s11_id[s11_cteid_mme];
								}
							else
							{
								cout<<"Detach imsi not found"<<endl;
								exit(-1);
							}



							s11_id.erase(s11_cteid_mme);							
							pthread_mutex_unlock(&s11id_mux);

							pthread_mutex_lock(&s1id_mux);
							s1_id.erase(s11_cteid_mme);
							pthread_mutex_unlock(&s1id_mux);

							pthread_mutex_lock(&s5id_mux);
							s5_id.erase(s11_cteid_mme);
							pthread_mutex_unlock(&s5id_mux);

							pthread_mutex_lock(&uectx_mux);
							ue_ctx.erase(imsi);
							pthread_mutex_unlock(&uectx_mux);
							cout << "detach sending to mme\n";
							bzero((char *) &a_addr, sizeof(a_addr) );
						a_addr.sin_family = AF_INET;
						a_addr.sin_addr.s_addr = inet_addr("192.168.122.147");
						a_addr.sin_port = htons(fddata.initial_fd);
						cout << fddata.initial_fd << endl;
						n = sendto(lsfd, pkt.data, pkt.len, 0,(const struct sockaddr *)&a_addr,sizeof(a_addr));	
						if(n < 0){
							cout<<"Error : Write Error"<<'\n';
							exit(-1);
						}
							
							/*mme_fd = fddata.initial_fd;
							returnval = sendto(mctx, mme_fd, pkt.data, pkt.len);
							if(returnval < 0)
							{
								cout<<"Error: Cant send to MME detach"<<endl;
								exit(-1);
							}

							fdmap.erase(cur_fd);
							close(cur_fd);
							close(mme_fd);*/

						}

						
						
					}    

				

			}

		}

			        
	}

	//close(lsfd);



}	//End Function




int main(int argc, char *argv[])
{
	int i,n,numth,rc;
	
	if(argc != 2)
	{
		//printinput();
		exit(0);
	}

	////////////////////////////////////////////////////////////////////////////

	lsfd = socket(AF_INET, SOCK_DGRAM, 0);

	if(lsfd < 0) {
    	cout<<"ERROR : opening socket"<<'\n';
      	exit(-1);
   	}

   	make_socket_nb(lsfd);
   	int uflag = 1;

	if (setsockopt(lsfd, SOL_SOCKET, SO_REUSEADDR, &uflag, sizeof(uflag)) < 0)
    {
            cout<<"Error : server setsockopt reuse"<<endl;
            exit(-2);
    }

	bzero((char *) &server, sizeof(server) );
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr("192.168.122.167");
	server.sin_port = htons(7000);

	if (bind(lsfd, (struct sockaddr *) &server, sizeof(server)) < 0) {
	      	cout<<"ERROR: BIND ERROR"<<'\n';
	      	exit(-1);      
	}

	//////////////////////////////////////////////////////////////////////////




   	////////////////////////////////////////////////////////////////////

	numth = atoi(argv[1]);
	//duration = atoi(argv[2]);

   /* Set affinity mask to include CPUs 0 to 7 */
	pthread_t th[numth];
	struct thread_data td[numth];

	pthread_attr_t attr;
	void *status;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);


	
	
	for(i = 0; i < numth;i++)
	{	

		td[i].id = i;
		td[i].core = i;
		
		if(i==0){
			td[i].min = 10001;
			td[i].max = 20000;}
		
		else if(i==1)
		{
			td[i].min = 20001;
			td[i].max = 30000;
		}
			rc = pthread_create(&th[i], &attr, action, (void *)&td[i]);
			if(rc)
				cout<<"Error thread"<<endl;

	}

	for(i = 0; i < numth; i++)
		{
			//td[i].status = 1;
			rc = pthread_join(th[i], &status);
		
		    if (rc){
		       cout << "Error:unable to join," << rc << endl;
		       exit(-1);
		    }
			//cout << " M: Joined with status " << status << endl;		
   		}


	pthread_exit(NULL);
		
}