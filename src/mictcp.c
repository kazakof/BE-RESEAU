#include <mictcp.h>
#include <api/mictcp_core.h>
#include <stdio.h>
#include <pthread.h>

#define MAX_SOCKET 256
#define TAILLE_FENETRE 60
#define LOSS_RATE 10
#define TIMEOUT 100
#define ID_LOSS_PERCENTAGE_SERVER 51 //id dans la table de discussion du pourcentage de pertes acceptable par le server


static struct mic_tcp_sock socket_local[MAX_SOCKET]; //tableau des sockets 
static int socket_nb = 0; //nombre de sockets
static struct mic_tcp_sock_addr addr_distant;

static int loss_img[TAILLE_FENETRE]={1}; //buffer circulaire pour la fenetre glissante, initiliasé avec 100% de pertes pour eviter des erreurs sur le taux de pertes au depart
static int nb_images_sent=0; //nombre d'images envoyés dans la fenetre actuelle

static float tableau_discussion_pertes[1001];//tableau discussion taux de pertes de 0.0 à 100.0

static int id_loss_percentage_client=102; //id du taux de pertes voulues par le client dans table de discussion
static int id_loss_rate_returned=ID_LOSS_PERCENTAGE_SERVER;// id du pourcentage de pertes acceptables, retourné par le server

static float loss_percentage_client; //pourcentage de perte effectif assigné après discussion
static float lost_rate=0.0; //effective loss_rate

int PE=0;
int PA=0;


pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;



void error(char * errorMsg, int line){ //fonction retour erreur
    fprintf(stderr,"%s at line %d \n",errorMsg,line);
    exit(EXIT_FAILURE);
}

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    tableau_discussion_pertes[0]=0.0; //initialisation du premier element du tableau
    for(int i=1;i<=1001;i++){ //initialisation du tableau de discussion du taux de pertes
        tableau_discussion_pertes[i]=tableau_discussion_pertes[i-1]+0.1;
    }

    if(initialize_components(sm)==-1){
        printf("erreur intialize_components\n");
        return -1;
    }/* Appel obligatoire */
    socket_local[socket_nb].fd=socket_nb;
    socket_nb++;
    set_loss_rate(LOSS_RATE);

    return socket_nb-1;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

   if(socket<socket_nb-1 || socket>=socket_nb){
       error("pb argument socket ",__LINE__);
   }

   socket_local[socket].addr=addr;
   return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr *addr)
{
    socket_local[socket].state=IDLE;
    int pcond;
    
    struct mic_tcp_pdu syn_ack={0};

    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    
    if (pthread_mutex_lock(&mutex)) { //lock du mutex avant la zone critique
            printf("erreur lock\n");
            exit(-1);
    }
        // blocage du thread en attente du pdu syn 
    if((pcond=pthread_cond_wait(&cond,&mutex))!=0){
            printf("erreur wait\n");
            exit(-1);
    }
    

    if (pthread_mutex_unlock(&mutex)) { //unlock du mutex après la zone critique
            printf("erreur unlock\n");
            exit(-1);
    }


    syn_ack.header.ack = 1 ;
    syn_ack.header.syn = 1;
    syn_ack.header.ack_num=id_loss_rate_returned;

    IP_send(syn_ack,*addr);	//envoi du syn_ack

    if (pthread_mutex_lock(&mutex)) { //lock du mutex avant la zone critique
            printf("erreur lock\n");
            exit(-1);
    }

        // blocage du thread en attente du pdu ack 
    if((pcond=pthread_cond_wait(&cond,&mutex))!=0){
        printf("erreur wait\n");
        exit(-1);
    }

    if (pthread_mutex_unlock(&mutex)) { //unlock du mutex après la zone critique
            printf("erreur unlock\n");
            exit(-1);
    }

    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    struct mic_tcp_pdu pdu_emis={0};
    struct mic_tcp_pdu pdu_recu={0};
    struct mic_tcp_sock_addr addr_recu;

    pdu_emis.header.syn=1;
    pdu_emis.header.ack=0;
    pdu_emis.header.seq_num=id_loss_percentage_client; //on met l'id dans le tableau de discussion correspondant au taux de pertes
 


    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");


    do{
        IP_send(pdu_emis,addr);//envoi du pdu syn
        IP_recv(&pdu_recu,&addr_recu,TIMEOUT);//attente de reception d'un syn_ack
      }while(!(pdu_recu.header.syn == 1 && pdu_recu.header.ack == 1));


    loss_percentage_client=tableau_discussion_pertes[pdu_recu.header.ack_num]; // on definit le taux de pertes final acceptable après discussion

    pdu_emis.header.syn=0;
    pdu_emis.header.ack=1;


    IP_send(pdu_emis,addr);//envoi du pdu ack

    socket_local[socket].state=ESTABLISHED;

    return 0;

}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size){

    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    struct mic_tcp_pdu pdu_emis;
    struct mic_tcp_pdu pdu_recu;
    struct mic_tcp_sock_addr addr_recu;

    int lost_images=0;
    

    pdu_emis.header.source_port=socket_local[mic_sock].addr.port;
    pdu_emis.header.dest_port=addr_distant.port;
    pdu_emis.header.seq_num=PE;
    pdu_emis.header.ack_num=PE;
    pdu_emis.payload.data=mesg;
    pdu_emis.payload.size=mesg_size;

    PE=(PE+1)%2;//acquittement cumulatif, on incrémente

    if(nb_images_sent==TAILLE_FENETRE){
        nb_images_sent=0; 
    }

    int sent_size = IP_send(pdu_emis, addr_distant);
    IP_send(pdu_emis, addr_distant);//envoi du message
    nb_images_sent++;


    IP_recv(&pdu_recu, &addr_recu, TIMEOUT); //on recupere l'acquittement




    if(!(pdu_recu.header.ack==1 && pdu_recu.header.ack_num==PE)){ //actualisation du buffer circulaire
        loss_img[nb_images_sent-1]=1;
    }else{
        loss_img[nb_images_sent-1]=0;
    }

    for(int j=0;j<TAILLE_FENETRE;j++){ // calcul du nombre d'images perdues dans le buffer circulaire
        lost_images+=loss_img[j];
    }
    lost_rate=(float)lost_images/(float)TAILLE_FENETRE*100.0; //calcul du loss_rate

    while(!(pdu_recu.header.ack==1 && pdu_recu.header.ack_num==PE)){ //boucle wait et renvoi du message si pas bon numéro
        if(lost_rate<loss_percentage_client){
            PE--;
            return 0;
        }else{
        IP_send(pdu_emis, addr_distant);
        IP_recv(&pdu_recu, &addr_recu,TIMEOUT);
        }
        
    }


    return sent_size; //retourne la taille des données envoyées
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size){

    struct mic_tcp_payload p_recu;
    p_recu.data=mesg;
    p_recu.size=max_mesg_size;
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    int Pl_Size = app_buffer_get(p_recu);

    return Pl_Size;
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{

    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");

    socket_local[socket].state=CLOSED;
    return 0;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    int pcond;
    struct mic_tcp_pdu pdu_ack;
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    if((pdu.header.ack ==0)&&(pdu.header.syn==1)&&socket_local[socket_nb-1].state==IDLE){
        
        if(tableau_discussion_pertes[pdu.header.seq_num]<tableau_discussion_pertes[ID_LOSS_PERCENTAGE_SERVER]){ //discute le pourcentage acceptable si celui proposé est inférieur à celui acceptable par le serveur
            id_loss_rate_returned=pdu.header.seq_num; 
        }
        if (pthread_mutex_lock(&mutex)) { //lock du mutex avant la zone critique
            printf("erreur lock\n");
            exit(-1);
        }
        socket_local[socket_nb-1].state = SYN_RECEIVED ;

        if((pcond=pthread_cond_broadcast(&cond))!=0){ //deblocage du thread main après reception du syn
            printf("erreur wait\n");
            exit(-1);
        }
        
        if (pthread_mutex_unlock(&mutex)) { //unlock du mutex après la zone critique
            printf("erreur unlock\n");
            exit(-1);
        }

    }

    if(pdu.header.ack == 1 && pdu.header.syn == 0 && socket_local[socket_nb-1].state == SYN_RECEIVED){
        
        if (pthread_mutex_lock(&mutex)) { //lock du mutex avant la zone critique
            printf("erreur lock\n");
            exit(-1);
        }
        
        socket_local[socket_nb-1].state = ESTABLISHED;
        
        if((pcond=pthread_cond_broadcast(&cond))!=0){ //deblocage du thread main après réception du ack
            printf("erreur wait\n");
            exit(-1);
        }
        
        if (pthread_mutex_unlock(&mutex)) { //unlock du mutex après la zone critique
            printf("erreur unlock\n");
            exit(-1);
        }

    }


    if(pdu.header.ack == 0 && pdu.header.syn == 0){
        if(pdu.header.seq_num==PA){
            app_buffer_put(pdu.payload);
            PA=(PA+1)%2;
        }
        
        pdu_ack.header.ack=1;   
        pdu_ack.header.ack_num=PA;
     

        IP_send(pdu_ack, addr_distant);
    }
    
}