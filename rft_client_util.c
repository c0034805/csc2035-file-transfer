/*
 * Replace the following string of 0s with your student number
 * c0034805
 */
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include "rft_client_logging.h"

/* 
 * is_corrupted - returns true with the given probability 
 * 
 * The result can be passed to the checksum function to "corrupt" a 
 * checksum with the given probability to simulate network errors in 
 * file transfer.
 *
 * DO NOT EDIT the is_corrupted function.
 */
static bool is_corrupted(float prob) {
    if (fpclassify(prob) == FP_ZERO)
        return false;
    
    float r = (float) rand();
    float max = (float) RAND_MAX;
    
    return (r / max) <= prob;
}

/* 
 * The verify_server function checks that the give server structure is 
 * equivalent to the server field of the proto struct. That is, the protocol
 * family, port and address of the two server structs are the same. 
 * It also checks that the given server_size is the same as the size of the
 * socket address size field of the proto struct.
 *
 * These checks can be used to ensure that an ACK is from the same server that 
 * a client sent data to.
 * 
 * If any of the checks fail, the function returns a corresponding error 
 * protocol state. If the servers are the same, the function returns the state 
 * of the protocol before the function was called.
 *
 * DO NOT EDIT the verify_server function.
 */
static proto_state verify_server(protocol_t* proto, struct sockaddr_in* server, 
    socklen_t server_size) {
    if (server_size != proto->sockaddr_size)
        return PS_BAD_S_SIZE;
    
    if (server->sin_port != proto->server.sin_port)
        return PS_BAD_S_PORT;
        
    if (server->sin_family != proto->server.sin_family)
        return PS_BAD_S_FAM;
        
    if (server->sin_addr.s_addr != proto->server.sin_addr.s_addr)
        return PS_BAD_S_ADDR;
        
    return proto->state;
}

/*
 * The setnlog_protocol function sets the protocol state to the given 
 * state, sets the proto src_line to the given line number and then 
 * logs output for the protocol.
 *
 * DO NOT EDIT the setnlog_protocol function.
 */
static void setnlog_protocol(protocol_t* proto, proto_state state, int line) {
    proto->state = state;
    proto->src_line = line;
    log_protocol(proto);
}

/* 
 * init_protocol - initialises fields of the given protocol struct to the 
 * values shown in its implementation.
 *
 * DO NOT EDIT the init_protocol function.
 */
void init_protocol(protocol_t* proto) {
    memset(proto, 0, sizeof(protocol_t));
    proto->state = PS_INIT;
    proto->in_file = NULL;
    proto->sockfd = -1;
    proto->seg_size = sizeof(segment_t);
    proto->sockaddr_size = (socklen_t) sizeof(struct sockaddr_in); 
    proto->timeout_sec = DEFAULT_TIMEOUT;
    proto->curr_retry = 0;
    proto->max_retries = DEFAULT_RETRIES;
    
    init_segment(proto, DATA_SEG, false);
    init_segment(proto, ACK_SEG, false);
}

/* 
 * TODO: you must implement this function.
 *
 * See documentation in rft_client_util.h and the assignment specification
 */
void init_segment(protocol_t* proto, seg_type type, bool payload_only) {
    if(!payload_only){
        if(type == 0){
            memset(&proto->data, 0, sizeof(segment_t));
            proto->data.type = type;
        }
        else{
            memset(&proto->ack, 0, sizeof(segment_t));
            proto->ack.type = type;
        }
    }
    else{
        if(type == 0){
            memset(proto->data.payload, 0, PAYLOAD_SIZE);
        }
        else{
            memset(proto->ack.payload, 0, PAYLOAD_SIZE);
        }
    }   
}

/* 
 * TODO: you must implement this function.
 *
 * See documentation in rft_client_util.h and the assignment specification
 *
 * Hint:
 *  - you have to detect an error when reading from the proto's input file
 */
void read_data(protocol_t* proto) {
    memset(proto->data.payload, 0, PAYLOAD_SIZE);

    proto->data.file_data = fread(proto->data.payload, 1, PAYLOAD_SIZE - 1, proto->in_file);

    if(ferror(proto->in_file) != 0){
        exit_err_state(proto, PS_BAD_READ,__LINE__);
    }

    proto->tfr_bytes -= proto->data.file_data;
}

/* 
 * TODO: you must implement this function.
 *
 * See documentation in rft_client_util.h and the assignment specification
 * and see how to send data in the preliminary exercise.
 *
 * Hints:
 *  - you have to handle the case when the current retry count 
 *      exceeds the maximum number of retries allowed
 *  - you have to introduce a probability of corruption of the payload checksum
 *      if the tfr_mode is with timeout (wt)
 *  - you have to detect an error from sending data - see how the rft_server
 *      detects an error when sending an ACK
 */
void send_data(protocol_t* proto) {
    if(proto->curr_retry > proto->max_retries){
        exit_err_state(proto, PS_EXCEED_RETRY, __LINE__);
    }

    if(strncmp(proto->tfr_mode, TIMEOUT_TFR_MODE, 3) == 0){
        proto->data.checksum = checksum(proto->data.payload, is_corrupted(proto->loss_prob));
    }
    else{
        proto->data.checksum = checksum(proto->data.payload, false);
    }

    if(proto->curr_retry == 0){
        setnlog_protocol(proto, PS_DATA_SEND, __LINE__);
    }
    else{
        setnlog_protocol(proto, PS_DATA_RESEND, __LINE__);
    }
    

    ssize_t bytes = sendto(proto->sockfd, &(proto->data), sizeof(segment_t), 0, 
                         (struct sockaddr*) &(proto->server), 
                         sizeof(struct sockaddr_in));

    if(bytes != sizeof(segment_t)){
        printf("Expected data sent size %ld bytes, actual sent size %zd bytes", sizeof(segment_t), bytes);
        exit_err_state(proto, PS_BAD_SEND, __LINE__);
    } 
    else {
        printf(">>>> NETWORK: DATA sent successfully <<<<\n");
    }
}

/* 
 * send_file_normal - sends a file to a server according to the RFT protocol 
 * with positive acknowledgement, without retransmission.
 *
 * DO NOT EDIT the send_file_normal function.
 */
void send_file_normal(protocol_t* proto) { 
    proto->src_file = __FILE__;
    
    setnlog_protocol(proto, PS_START_SEND, __LINE__);
    
    while (proto->tfr_bytes) {
        read_data(proto);
        
        proto->state = PS_DATA_SEND;
        
        send_data(proto);
        
        proto->total_segments++;
        proto->total_file_data += proto->data.file_data;
        
        setnlog_protocol(proto, PS_ACK_WAIT, __LINE__);
 
        init_segment(proto, ACK_SEG, false);        
        socklen_t server_size = proto->sockaddr_size;
        struct sockaddr_in server;
        memset(&server, 0, server_size);
        ssize_t nbytes = recvfrom(proto->sockfd, &proto->ack, proto->seg_size,
            0, (struct sockaddr *) &server, &server_size);

        if (nbytes != proto->seg_size)
            exit_err_state(proto, PS_BAD_ACK, __LINE__);
        
        if (proto->data.sq != proto->ack.sq)
            exit_err_state(proto, PS_BAD_ACK_SQ, __LINE__);

        proto_state state = verify_server(proto, &server, server_size);
        if (proto->state != state)
            exit_err_state(proto, state, __LINE__);

        setnlog_protocol(proto, PS_ACK_RECV, __LINE__);

        proto->data.sq++;
    }

    proto->state = proto->fsize ? PS_TFR_COMPLETE : PS_EMPTY_FILE;
    
    return;
}      

/* 
 * TODO: you must implement this function.
 *
 * See documentation in rft_client_util.h and the assignment specification
 */
void send_file_with_timeout(protocol_t* proto) {   
    return;
}

/* 
 * TODO: you must implement this function.
 *
 * See documentation in rft_client_util.h and the assignment specification
 *
 * Hint:
 *  - you must copy proto information to a metadata struct
 *  - you have to detect an error from sending metadata - see how the rft_server
 *      detects an error when sending an ACK
 */
bool send_metadata(protocol_t* proto) {
    metadata_t* data = malloc(sizeof(metadata_t));

    data->size = proto->fsize;
    strncpy(data->name, proto->out_fname, MAX_FILENAME_SIZE);

    ssize_t bytes = sendto(proto->sockfd, data, sizeof(metadata_t), 0, 
                         (struct sockaddr*) &(proto->server), 
                         sizeof(struct sockaddr_in));

    if(bytes != sizeof(metadata_t)){
        return false;
    }
    return true;
} 
  
/* 
 * TODO: you must implement this function.
 *
 * See documentation in rft_client_util.h and the assignment specification
 */
void set_socket_timeout(protocol_t* proto) {
    struct timeval timeout;
    socklen_t to_size = sizeof(struct timeval);

    timeout.tv_sec = proto->timeout_sec;
    timeout.tv_usec = 0;

    if(setsockopt(proto->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, to_size) != 0){
        exit_err_state(proto, PS_BAD_SOCKTOUT, __LINE__);
    }
}

/* 
 * TODO: you must implement this function.
 *
 * See documentation in rft_client_util.h and the assignment specification,
 * and look at rft_server and preliminary exercise code.
 *
 * Hint:
 *  - Make sure you check the return values of system calls such as 
 *      socket and inet_aton
 */
void set_udp_socket(protocol_t* proto) {  
    proto->sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if(proto->sockfd < 0){
        close(proto->sockfd);
        proto->sockfd = -1;
    }
    else{
        memset(&(proto->server), 0, sizeof(struct sockaddr_in));

        proto->server.sin_family = AF_INET;
        proto->server.sin_port = htons(proto->server_port);
        proto->server.sin_addr.s_addr = htonl(INADDR_ANY);

        if(!inet_aton(proto->server_addr, &(proto->server.sin_addr))) {
            close(proto->sockfd);
            proto->sockfd = -1;
        }
        else{
            proto->state = PS_TFR_READY;
        }
    }
} 
