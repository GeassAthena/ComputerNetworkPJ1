#include "bt_parse.h"
#include "job.h"
#include "queue.h"
#include "conn.h"

extern bt_config_t config;
extern job_t job;


void init_down_pool(down_pool_t* pool) {
	int i = 0 ;
	int max = config.max_conn;
	pool->flag = (int*)malloc(sizeof(int)*max);
	pool->connection = (down_conn_t**)malloc(sizeof(down_conn_t*)*max);
	int* flags = pool->flag;
	while(i < max) {
		flags[i++] = 0;
	}
}


void init_up_pool(up_pool_t* pool) {
	int i = 0 ;
	int max = config.max_conn;

	pool->flag = (int*)malloc(sizeof(int)*max);
	pool->connection = (up_conn_t**)malloc(sizeof(up_conn_t));
	int* flags = pool->flag;
	while(i < max) {
		flags[i++] = 0;
	}
	//fprintf(stderr, "%f\n", job.cwnd);
}


void init_down_conn(down_conn_t** conn, bt_peer_t* provider, 
	queue_t* chunk, queue_t* get_queue) {
	(*conn) = (down_conn_t*)malloc(sizeof(down_conn_t));
	(*conn)->provider = provider;
	(*conn)->chunks = chunk;
	(*conn)->get_queue = get_queue;
	(*conn)->next_pkt = 1;
	gettimeofday(&((*conn)->last_time), NULL); // initial time
}


void init_up_conn(up_conn_t** conn, bt_peer_t* receiver,  
	data_packet_t** pkt_array) {
	(*conn) = (up_conn_t*)malloc(sizeof(up_conn_t));
	(*conn)->receiver = receiver;
	(*conn)->pkt_array = pkt_array;
	(*conn)->l_ack = 0;
	(*conn)->l_available = 1;
	(*conn)->duplicate = 1;
	(*conn)->cwnd = INIT_CWND;
	(*conn)->ssthresh = INIT_SSTHRESH;
}


down_conn_t* en_down_pool(down_pool_t* pool,bt_peer_t* provider, 
queue_t* chunk, queue_t* get_queue) { 
	if( pool->num >= config.max_conn) {
		return NULL;
	}
	// find next available connection position
	int i = 0;
	while (i<10) {
		if( pool->flag[i] == 0)
			break;
		i++;
	}
	init_down_conn(&(pool->connection[i]),provider,chunk, get_queue);
	pool->flag[i] = 1;
	pool->num++;
	return pool->connection[i];
}


up_conn_t* en_up_pool(up_pool_t* pool,bt_peer_t* receiver,  
	data_packet_t** pkt_array) { 
	if( pool->num >= config.max_conn) {
		return NULL;
	}
	// find next available connection position
	int i = 0;
	while(i<10) {
		if( pool->flag[i] == 0)
			break;
		i++;
	}
	init_up_conn(&(pool->connection[i]),receiver,pkt_array);
	pool->flag[i] = 1;
	pool->num++;
	return pool->connection[i];
}


void de_up_pool(up_pool_t* pool,bt_peer_t* peer) {
	int i = 0;
	up_conn_t** conns = pool->connection;
	while( i < config.max_conn ) {
		if( pool->flag[i] == 1 && conns[i]->receiver->id == peer->id) {
			conns[i]->receiver = NULL;
			free(conns[i]->pkt_array);
			conns[i]->pkt_array = NULL;
			conns[i]->l_ack = 0;
			conns[i]->l_available = 1;
			conns[i]->duplicate = 0;
			conns[i]->cwnd = INIT_CWND;
			conns[i]->ssthresh = INIT_SSTHRESH;
			pool->flag[i] = 0;
			(pool->num)--;
			break;
		}
		i++;
	}
}


void de_down_pool(down_pool_t* pool,bt_peer_t* peer) {
	int i = 0;
	down_conn_t** conns = pool->connection;
	while( i < config.max_conn ) {
		if(pool->flag[i] == 1 && conns[i]->provider->id == peer->id) {
			if(dequeue(conns[i]->get_queue) != NULL ) {
				// This should never happen!
				fprintf(stderr, "downloading connection pool error!\n");
			}
			conns[i]->provider = NULL;
			conns[i]->chunks = NULL;
			conns[i]->get_queue = NULL;
			pool->flag[i] = 0;
			pool->num--;
			break;
		}
		i++;
	}
}


down_conn_t* get_down_conn(down_pool_t* pool, bt_peer_t* peer) {
	int i = 0; 
	down_conn_t** conns = pool->connection;
	while( i<= config.max_conn) {
		if( pool->flag[i] == 1 && conns[i]->provider->id == peer->id) {
			return conns[i];	
		}
		i++;
	}
	return NULL;
}



up_conn_t* get_up_conn(up_pool_t* pool, bt_peer_t* peer) {
	int i = 0; 
	up_conn_t** conns = pool->connection;
	while( i<= config.max_conn) {
		if( pool->flag[i] == 1 && conns[i]->receiver->id == peer->id) {
			return conns[i];	
		}
		i++;
	}
	return NULL;
}



void up_conn_recur_send(up_conn_t* conn, struct sockaddr* to) {
	while(conn->l_available <= 512 && 
		  conn->l_available - conn->l_ack <= conn->cwnd) {
		if (VERBOSE)
			fprintf(stderr, "send data:%d!!!!\n",conn->l_available);
		//print_pkt((data_packet_t*)(conn->pkt_array[conn->l_available-1]));
		packet_sender((data_packet_t*)(conn->pkt_array[conn->l_available-1]),
			          to);
		conn->l_available++;
	}
}


void update_up_conn(up_conn_t* conn, bt_peer_t* peer, data_packet_t* get_pkt) {
	// construct new data pkt array
	data_packet_t** data_pkt_array = DATA_pkt_array_maker(get_pkt);
	conn->receiver = peer;
	conn->pkt_array = data_pkt_array;
	conn->l_ack = 0;
	conn->l_available = 1;
	conn->duplicate = 1;
	conn->cwnd = INIT_CWND;
	conn->ssthresh = INIT_SSTHRESH;
}


void update_down_conn( down_conn_t* conn, bt_peer_t* peer) {
	// removed finished GET request
	conn->next_pkt = 1;
}


void print_cwnd(up_conn_t *conn) {
    int elapsed;
    elapsed = get_time_diff(&(config.start_time));
    fprintf(config.cwnd, "%df%d\t%d\t%d\n",config.identity, conn->receiver->id, 
    	                                  (int)(conn->cwnd), (int)elapsed);
    fflush(config.cwnd);
}