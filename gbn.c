#include "gbn.h"

state_t s;

uint16_t checksum(uint16_t *buf, int nwords)
{

    uint32_t sum;

    for (sum = 0; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return ~sum;
}

ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags){

    /* Hint: Check the data length field 'len'.
     *       If it is > DATALEN, you will have to split the data
     *       up into multiple packets - you don't have to worry
     *       about getting more than N * DATALEN.
     */

    /* Create socket */
    struct sockaddr server;
    socklen_t server_len = sizeof(server);

    printf("In Send() buf len: %d, flags: %d\n", (int)len, flags);
    printf("Data length: %d\n", len);

    /* Input buffer location pointer */
    int data_offset = 0;

    /* Get number of packets to send */
    int num_packets = ((len + DATALEN - 1) / DATALEN);

    /* Create packet buffer for packets remaining to be sent */
    /* and create Packet buffer counter */
    gbnhdr *pkt_buffer[num_packets];
    int pkt_buf_counter = 0;

    /* Make a copy of len for remaining len to be added to buffer */
    int curr_len = len;

    if (s.curr_state == ESTABLISHED) {

        printf("Creating %d packet(s)\n\n", num_packets);

        /* ----------Create data packets to be sent and add them to the packet buffer-----------------*/

        /* While the remaining length is greater than DATALEN, create packets
         * with data of length 1024 and add it them to the packet buffer */
        while (curr_len > DATALEN) {

            printf("Creating packet %d\n", pkt_buf_counter + 1);
            gbnhdr *DATA_packet = malloc(sizeof(*DATA_packet));
            memset(DATA_packet->data, 0, sizeof(DATA_packet->data));

            /* Call create_DATA_packet to complete packet creation */
            create_DATA_packet(DATA_packet, s.seq_num+data_offset, buf+data_offset, DATALEN);

            printf("gbn_send DATA_packet checksum: %d\n", DATA_packet->checksum);
            printf("DATA packet content: %s\n", DATA_packet->data);

            /* Add DATA_packet to packet buffer array */
            pkt_buffer[pkt_buf_counter] = DATA_packet;

            printf("Packet %d added to pkt_buffer\n", pkt_buf_counter + 1);

            /* Increase data offset pointer to next data location */
            data_offset += DATALEN;
            /* Increase the packet buffer counter */
            pkt_buf_counter += 1;
            /* Decrement curr_len by added data length */
            curr_len -= DATALEN;

            printf("Remaining length: %d\n", curr_len);

        }

        /* If length is less than DATALEN, create packet with corresponding data length */
        printf("Creating packet %d\n", pkt_buf_counter + 1);

        gbnhdr *DATA_packet = malloc(sizeof(*DATA_packet));
        memset(DATA_packet->data, 0, sizeof(DATA_packet->data));

        /* Create (last) DATA_packet using create_DATA_packet function */
        create_DATA_packet(DATA_packet, s.seq_num+data_offset, buf+data_offset, curr_len);

        printf("gbn_send DATA_packet checksum: %d\n", DATA_packet->checksum);
        printf("DATA packet content: %s\n", DATA_packet->data);

        /* Add DATA_packet to packet buffer array */
        pkt_buffer[pkt_buf_counter] = DATA_packet;

        printf("Packet %d added to pkt_buffer\n\n\n", pkt_buf_counter + 1);

        /* ------------------------------------------------------------------------------------------*/


        /* ----------------------Send data packets from packet buffer--------------------------------*/

        printf("Sending %d packets\n\n", num_packets);

        gbnhdr *DATAACK_packet = malloc(sizeof(*DATAACK_packet));
        memset(DATAACK_packet->data, 0, sizeof(DATAACK_packet->data));

        int attempts = 1;
        int confirmed_pkts = 0;
        while (confirmed_pkts < num_packets) {

            /* Set current state seq_num to cur_pkt seqnum */
            s.seq_num = pkt_buffer[confirmed_pkts]->seqnum;
            printf("Current seq_num: %d\n", s.seq_num);

            int pkts_sent = 0;

            printf("Window size %d...\n", s.window_size);
            printf("Confirmed Packets: %d\n", confirmed_pkts);;

            if(attempts > MAX_ATTEMPTS) {
                printf("MAX attempts reached, exiting program...\n");
                return(-1);
            }

            int i = 0;
            while ((i < s.window_size) && ((confirmed_pkts+i) < num_packets)) {
                printf("Sending packet num %d...\n", confirmed_pkts+i+1);

                if (sendto(sockfd, pkt_buffer[confirmed_pkts + i], sizeof(*pkt_buffer[confirmed_pkts + i]), flags, &s.address, s.sock_len) == -1) {
                    perror("Data packet send error");
                    return(-1);
                }

                printf("DATA_packet content: %s\nDATA_packet sent successfully...\n\n", pkt_buffer[confirmed_pkts+i]->data);
                pkts_sent += 1;
                i++;
            }

            int x;
            for (x = 0; x < pkts_sent; x++){

                printf("Last packet actual length: %d\n", pkt_buffer[confirmed_pkts]->actual_len);

                if (recvfrom(sockfd, DATAACK_packet, sizeof(*DATAACK_packet), flags, &server, &server_len) == -1) {
                    perror("Data ack packet recv error");
                    return(-1);
                }

                if (DATAACK_packet->type == DATAACK) {
                    printf("Expected DATAACK_packet seq_num: %d\n", (s.seq_num+pkt_buffer[confirmed_pkts]->actual_len));
                    printf("DATAACK_packet with seq_num %d received...\n", DATAACK_packet->seqnum);

                    if (DATAACK_packet->seqnum == (s.seq_num+pkt_buffer[confirmed_pkts]->actual_len)) {
                        printf("Expected DATAACK_packet received...\n");

                        s.seq_num = pkt_buffer[confirmed_pkts]->seqnum + pkt_buffer[confirmed_pkts]->actual_len;
                        printf("Current seq_num: %d\n", s.seq_num);

                        confirmed_pkts++;

                        switch(s.window_size){
                            case 1:
                                s.window_size = 2;
                                break;
                            case 2:
                                s.window_size = 4;
                                break;
                            case 4:
                                break;
                            default:
                                break;
                        }

                        printf("Window size changed to %d...\n", s.window_size);
                    }
                    else {
                        printf("Incorrect DATAACK_packet received...\n");
                        attempts++;
                        printf("\nAttempt number: %d\n", attempts);
                    }
                }
            }
        }

        printf("Freeing memory...\n");
        free(DATAACK_packet);

        int z;
        for (z = 0; z < pkt_buf_counter; z++) {
            free(pkt_buffer[z]);
        }

        /*
        int cur_pkt;
        for (cur_pkt = 0; cur_pkt < num_packets; cur_pkt++) {

            /* Set current state seq_num to cur_pkt seqnum
            s.seq_num = pkt_buffer[cur_pkt]->seqnum;

            gbnhdr *DATAACK_packet = malloc(sizeof(*DATAACK_packet));
            memset(DATAACK_packet->data, 0, sizeof(DATAACK_packet->data));

            printf("Sending packet num %d...\n", i);

            if (sendto(sockfd, pkt_buffer[cur_pkt], sizeof(*pkt_buffer[cur_pkt]), flags, &s.address, s.sock_len) == -1) {
                perror("Data packet send error");
                return(-1);
            }

            printf("DATA_packet content: %s\nDATA_packet sent successfully...\n", pkt_buffer[cur_pkt]->data);

            printf("Waiting for DATAACK packet num %d...\n", cur_pkt);

            last_ack_not_received = s.seq_num + pkt_buffer[cur_pkt]->actual_len;

            if (recvfrom(sockfd, DATAACK_packet, sizeof(*DATAACK_packet), flags, &server, &server_len) == -1) {
                perror("Data ack packet recv error");
                return(-1);
            }

            if (DATAACK_packet->DATAACK) {
                if (DATAACK_packet->seqnum == last_ack_not_received) {
                    printf("Expected DATAACK_packet received...\n");
                }
                else {
                    printf("Incorrect DATAACK_packet received...\n");
                }
            }

            printf("DATAACK_packet received...\n");

            /* set to null
            free(pkt_buffer[cur_pkt]);
            free(DATAACK_packet);

        }
        */

        return (len);
    }

    return (-1);
}

ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags){
    printf("In gbn_recv()\n");

    /*------- create the data and data ack used to receive and respond to incoming packet -------*/
    gbnhdr *DATA_packet = malloc(sizeof(*DATA_packet));
    memset(DATA_packet->data, 0, sizeof(DATA_packet->data));

    gbnhdr *DATAACK_packet = malloc(sizeof(*DATAACK_packet));
    memset(DATAACK_packet->data, 0, sizeof(DATAACK_packet->data));
    DATAACK_packet->type = DATAACK;
    /* ------------------------------------------------------------------------------------------*/

    /* When the sender is done transmitting data will attempt to close by sending FIN, respond with FINACK */
    gbnhdr *FINACK_packet = malloc(sizeof(*FINACK_packet));
    memset(FINACK_packet->data, 0, sizeof(FINACK_packet->data));
    FINACK_packet->type = FINACK;

    /* Variable declaration to be populated from peer client */
    struct sockaddr client;
    socklen_t client_len = sizeof(client);

    /* Actual number of bytes received from the sender */
    ssize_t byte_length;

    while(s.curr_state == ESTABLISHED){

        printf("Connection is established. Waiting for DATA packet...\n");

        if ((byte_length = recvfrom(sockfd,DATA_packet, sizeof(*DATA_packet), flags, &client, &client_len)) == -1) {
            perror("Data packet recv error");
            return(-1);
        }

        printf("Data packet size: %d\n Byte length upon receive: %d\n",
                sizeof(*DATA_packet),
                (unsigned int)byte_length);

        if (DATA_packet->type == DATA && validate_packet(DATA_packet)) {

            /* Copy data received to buffer */
            memcpy(buf, DATA_packet->data, DATA_packet->actual_len);

            /* Create DATAACK packet to be sent to peer */
            DATAACK_packet->checksum = checksum((uint16_t  *)DATAACK_packet, sizeof(*DATAACK_packet) / sizeof(uint16_t));
            DATAACK_packet->seqnum = DATA_packet->seqnum + DATA_packet->actual_len;
            s.seq_num = DATA_packet->seqnum + DATA_packet->actual_len + 1;

            printf("Data packet content: %s\n Buffer content: %s\n", DATA_packet->data, buf);
            printf("Data packet data length: %d\n", DATA_packet->actual_len);
            printf("DATA_packet received.\n Sending DATAACK_packet with seqnum %d...\n", DATAACK_packet->seqnum);

            if(sendto(sockfd, DATAACK_packet, sizeof(*DATAACK_packet), flags, &client, client_len) == -1) {
                perror("DATAACK packet error");
                return(-1);
            }
        }
        if (DATA_packet->type == FIN) {
            s.curr_state = FIN_RCVD;

            printf("FIN received, responding with FINACK\n");

            if (sendto(sockfd, FINACK_packet, sizeof(*FINACK_packet), flags, &client, client_len) == -1) {
                perror("FINACK sendto error\n");
                s.curr_state = CLOSED;

                free(DATA_packet);
                free(DATAACK_packet);
                free(FINACK_packet);

                return(-1);
            }

            printf("FINACK successfully sent, closing connection...\n");
            /* TODO need to return the buffer now to write it to the file? */
            return(0);
        }
        return(DATA_packet->actual_len); /* DO NOT REMOVE BUT MODIFY as desired*/
    }
    printf("DATA_packet->data length: %d\n", (int)sizeof(DATA_packet->data));

    free(DATA_packet);
    free(DATAACK_packet);
    free(FINACK_packet);

    return(-1);
}

int gbn_close(int sockfd){

    /*--------- for sending FIN and receiving FINACK ---------*/
    gbnhdr *FIN_packet = malloc(sizeof(*FIN_packet));
    memset(FIN_packet->data, 0, sizeof(FIN_packet->data));
    FIN_packet->type = FIN;

    gbnhdr *FINACK_packet = malloc(sizeof(*FINACK_packet));
    memset(FINACK_packet->data, 0, sizeof(FINACK_packet->data));
    /*--------- END FIN/FINACK packet creation ---------*/

    if (s.curr_state == FIN_RCVD) {
        printf("Closing connection from receiver side...\n");
        s.curr_state = CLOSED;
        return(1);
    }
    int attempts = 0;
    while (s.curr_state != CLOSED) {

        if (attempts > MAX_ATTEMPTS) {
            printf("MAX attempts reached, exiting program...\n");
            free(FIN_packet);
            free(FINACK_packet);
            return(-1);
        }

        printf("Sending FIN_packet...\n");

        if (sendto(sockfd, FIN_packet, sizeof(*FIN_packet), 0, &s.address, s.sock_len) == -1) {
            perror("close FIN_packet error\n");
            s.curr_state = CLOSED;
            return(-1);
        }

        if (FIN_packet->type == FIN) {
            printf("FIN_packet sent.\nWaiting for FINACK_packet...\n");

            alarm(TIMEOUT);
            attempts++;
            printf("Attempt number: %d\n", attempts);

            if (recvfrom(sockfd, FINACK_packet, sizeof(*FINACK_packet), 0, &s.address, &s.sock_len) == -1) {
                if (errno != EINTR) {
                    perror("close FINACK_packet error\n");
                    s.curr_state = CLOSED;
                    return(-1);
                }
            } else {
                s.curr_state = CLOSED;
            }

        }

    }

    printf("Freeing memory...\n");
    printf("Closing socket...\n");

    free(FIN_packet);
    free(FINACK_packet);
    close(sockfd);
    return(0);
}

int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){

    printf("In Connect() with socket: %d, server: %d, socklen: %d\n", sockfd, server->sa_family, socklen);

    /* create the SYN packet and populate it */
    gbnhdr *SYN_packet = malloc(sizeof(*SYN_packet));
    memset(SYN_packet->data, 0, sizeof(SYN_packet->data));
    SYN_packet->type = SYN;
    SYN_packet->seqnum = s.seq_num;
    SYN_packet->checksum = 0;
    SYN_packet->checksum = checksum((uint16_t  *)SYN_packet, sizeof(*SYN_packet) / sizeof(uint16_t));

    printf("gbn_connect SYN_PACKET checksum: %d\n",  SYN_packet->checksum ) ;

    /* init the SYNACK packet to be sent */
    gbnhdr *SYNACK_packet = malloc(sizeof(*SYNACK_packet));
    memset(SYNACK_packet->data, 0, sizeof(SYNACK_packet->data));

    /* counter that will handle when the close the connection on timeout/fail */
    int attempts = 0;

    while(1) {

        if (attempts > MAX_ATTEMPTS) {
            printf("\nMax attempt exceeded, exiting program...\n");
            s.curr_state = CLOSED;

            free(SYN_packet);
            free(SYNACK_packet);
            /* we assume connection is failing so we shut down */
            return(-1);
        }

        if (s.curr_state == CLOSED) {

            printf("Sending SYN_packet with seqnum: %d...\n", SYN_packet->seqnum);

            if (sendto(sockfd, SYN_packet, sizeof(*SYN_packet), 0, server, socklen) == -1) {
                perror("Sendto error");
                return(-1);
            }

            s.curr_state = SYN_SENT;

            printf("Current state SYN_SENT: %d\n", s.curr_state);
        }

        if (s.curr_state == SYN_SENT) {

            alarm(TIMEOUT);
            attempts++;
            printf("\nAttempt number: %d \nWaiting for SYNACK_packet...\n", attempts);

            if (recvfrom(
                    sockfd, SYNACK_packet, sizeof(*SYNACK_packet), 0, (struct sockaddr *) &server, &socklen) == -1)
            {
                if (errno != EINTR) {
                    perror("Recvfrom SYNACK_packet error\n");
                    s.curr_state = CLOSED;
                    return(-1);
                }
            }

            if ((SYNACK_packet->type == SYNACK) && (validate_packet(SYNACK_packet) == 1)) {

                /* resetting the alarm to no alarm */

                alarm(0);
                printf("SYNACK_packet received\n");

                s.address = *(struct sockaddr *) &server;
                s.sock_len = socklen;
                s.curr_state = ESTABLISHED;
                s.seq_num = SYNACK_packet->seqnum;
                printf("Current state ESTABLISHED: %d\n", s.curr_state);

                free(SYN_packet);
                free(SYNACK_packet);

                return sockfd;

            }
        }
    }
}

int gbn_listen(int sockfd, int backlog){

    /* ---- There is no need to listen. Using only UDP calls. ---- */

    printf("In Listen(), sockfd: %d, backlog: %d \n", sockfd, backlog);

    /* Setting the default to closed since there is no connection established yet */
    s.curr_state = CLOSED;

    return(0);
}

int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen){

    int status;

    printf("In bind(),  sockfd: %d\n", sockfd);

    /* Binding to the socket for further use */
    if ((status = bind(sockfd, server, socklen)) == -1) {

        perror("Bind error");

        return(-1);
    }

    return status;
}

int gbn_socket(int domain, int type, int protocol){

    /*----- Randomizing the seed. This is used by the rand() function -----*/
    srand((unsigned)time(0));

    printf("domain: %d, type %d, protocol %d\n", domain, type, protocol);

    /* Assign a random seq number used when sending or receiving packets */
    s.seq_num = (uint8_t)rand();

    /* Set the size to 1. This state will be modified according to the go-back-n protocol */
    s.window_size = 1;

    /* declare signal for setting alarm */
    signal(SIGALRM, timeout_hdler);

    printf("Seq num: %d, Window size: %d\n", s.seq_num, s.window_size);

    int sockfd = socket(domain, type, protocol);

    printf("Socket created %d\n", sockfd);

    return sockfd;
}

int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen){

    printf("In Accept() socket: %d, client address: %d\n", sockfd, client->sa_family);
    printf("Current state: %d\n", s.curr_state);

    /* init SYN_packet to be populated */
    gbnhdr *SYN_packet = malloc(sizeof(*SYN_packet));
    memset(SYN_packet->data, '\0', sizeof(SYN_packet->data));

    /* init the SYNACK packet to be sent after the SYN packet */
    gbnhdr *SYNACK_packet = malloc(sizeof(*SYNACK_packet));
    memset(SYNACK_packet->data, 0, sizeof(SYNACK_packet->data));
    SYNACK_packet->type = SYNACK;

    gbnhdr *RST_packet = malloc(sizeof(*RST_packet));
    memset(RST_packet->data, 0, sizeof(RST_packet->data));
    RST_packet->type = RST;
    /* number of bytes received */
    ssize_t byte_count;

    int attempts = 0;


    while(1) {

        if (attempts > MAX_ATTEMPTS) {
            printf("\nMax attempt exceeded, exiting program...\n");
            s.curr_state = CLOSED;

            free(SYN_packet);
            free(SYNACK_packet);

            return(-1);
        }

        if (s.curr_state == CLOSED) {

            /* Waiting for a SYN packet to establish connection */

            alarm(TIMEOUT);
            attempts++;
            printf("Attempt number: %d\n Waiting for SYN_packet...\n", attempts);

            if ((byte_count = recvfrom(sockfd, SYN_packet, sizeof(*SYN_packet), 0, client, socklen) == -1)) {

                if (errno != EINTR) {
                    perror("SYN receive error\n");
                    s.curr_state = CLOSED;
                    return(-1);
                }
            }

            printf("\nPacket received with byte_count: %d, SYN type: %d\n", (int)byte_count, SYN_packet->type);
        }


        if ((SYN_packet->type == SYN) && (validate_packet(SYN_packet) == 1)) {

            /* reseting the alarm to zero (no alarm) for further use */
            alarm(0);

            printf("Packet is SYN_packet\n");

            s.curr_state = SYN_RCVD;
            s.seq_num = SYN_packet->seqnum + sizeof(*SYN_packet);

            SYNACK_packet->seqnum = s.seq_num;

            SYNACK_packet->checksum = checksum((uint16_t *)SYNACK_packet, sizeof(*SYNACK_packet) / sizeof(uint16_t));

            printf("Current state SYN_RCVD: %d\n Sending SYNACK_packet...\n with seqnum: %d, checksum: %d\n",
                   s.curr_state,
                   SYNACK_packet->seqnum,
                   SYNACK_packet->checksum);

            if (sendto(sockfd, SYNACK_packet, sizeof(*SYNACK_packet), 0, client, *socklen) == -1) {
                perror("SYNACK send error\n");
                return(-1); /* TODO retry sending SYNACK */
            }

            s.curr_state = ESTABLISHED;
            s.address = *client;
            s.sock_len = *socklen;

            printf("Current state ESTABLISHED: %d\n", s.curr_state);

            free(SYN_packet);
            free(SYNACK_packet);

            return sockfd;
        }
    }
}

uint8_t validate_packet(gbnhdr *packet){
    uint32_t received_checksum = packet->checksum;
    packet->checksum = 0;
    uint16_t packet_checksum = checksum((uint16_t  *)packet, sizeof(*packet) / sizeof(uint16_t));
    printf("packet received checksum: %d, and calculated checksum: %d\n", received_checksum, packet_checksum);

    if (packet_checksum == received_checksum) {
        return(1);
    }
    printf("CHECKSUM FAILED: %d != %d\n",packet_checksum, received_checksum);
    return 0;
}

void create_DATA_packet(gbnhdr *DATA_packet, uint32_t pkt_seqnum, const void *buf_pointer, size_t data_len){

    printf("Received data length: %d\n", data_len);

    DATA_packet->type = DATA;
    DATA_packet->seqnum = pkt_seqnum;
    DATA_packet->checksum = 0;
    DATA_packet->actual_len = data_len;

    printf("Packet seq number %d\n", DATA_packet->seqnum);
    printf("Packet data length: %d\n", DATA_packet->actual_len);

    printf("Adding data to packet %d\n", DATA_packet->seqnum);
    memcpy(DATA_packet->data, buf_pointer, data_len);
    printf("Packet data in create_packet_func: %s\n", DATA_packet->data);

    DATA_packet->checksum = checksum((uint16_t  *)DATA_packet, sizeof(*DATA_packet) / sizeof(uint16_t));

}

void timeout_hdler(int signum) {

    /* apparently bad practice to printf in signal use flag instead */
    printf("\nTIMEOUT has occured with signum: %d\n", signum);

    /* TODO is this safe? race condition? */
    signal(SIGALRM, timeout_hdler);
}

ssize_t maybe_recvfrom(int  s, char *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen){
    /*----- Packet not lost -----*/
    if (rand() > LOSS_PROB*RAND_MAX){


        /*----- Receiving the packet -----*/
        int retval = recvfrom(s, buf, len, flags, from, fromlen);

        /*----- Packet corrupted -----*/
        if (rand() < CORR_PROB*RAND_MAX){
            /*----- Selecting a random byte inside the packet -----*/
            int index = (int)((len-1)*rand()/(RAND_MAX + 1.0));

            /*----- Inverting a bit -----*/
            char c = buf[index];
            if (c & 0x01)
                c &= 0xFE;
            else
                c |= 0x01;
            buf[index] = c;
        }

        return retval;
    }
    /*----- Packet lost -----*/
    return(len);  /* Simulate a success */
}