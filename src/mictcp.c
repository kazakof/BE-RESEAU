#include <mictcp.h>
#include <api/mictcp_core.h>

#define MAX_SOCKET 256



static struct mic_tcp_sock socket_local[MAX_SOCKET];
static struct mic_tcp_sock_addr addr_distant;
static int socket_nb = 0;
int PE=0;
int PA=0;



/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(initialize_components(sm)==-1){
        printf("erreur intialize_components\n");
        return -1;
    }/* Appel obligatoire */
    socket_local[socket_nb].fd=socket_nb;
    socket_local[socket_nb].state=IDLE;
    socket_nb++;
    set_loss_rate(2);

    return socket_nb-1;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   //socket_local[socket].addr=addr;
   return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr *addr)
{
    
    struct mic_tcp_pdu syn_ack;
    struct mic_tcp_pdu syn;
    struct mic_tcp_pdu ack;
    
    int timeout=100;
    
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    while(socket_local[socket].state != SYN_RECEIVED){ //attente du syn
        IP_recv(&syn,&addr_distant,timeout);
        if(syn.header.syn==1){
            socket_local[socket].state=SYN_RECEIVED;
        }
    } 
    syn_ack.header.ack = 1 ;
    syn_ack.header.syn = 1;
    
    IP_send(syn_ack,*addr);	//envoi du syn_ack

    while (socket_local[socket].state != ESTABLISHED){ //attente du ack
        IP_recv(&ack,&addr_distant,timeout);
        if(ack.header.ack==1){
            socket_local[socket].state=ESTABLISHED;
        }
    }
    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    /*struct mic_tcp_pdu pdu_emis;
    struct mic_tcp_pdu pdu_recu;
    struct mic_tcp_sock_addr addr_recu;

    addr_distant=addr;
    
    int timeout=100;

    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    /*pdu_emis.header.syn=1;

    do{
        IP_send(pdu_emis,addr);//envoi du pdu syn
        IP_recv(&pdu_recu,&addr_recu,timeout);//attente de reception d'un syn_ack
      }while(!(pdu_recu.header.syn == 1 && pdu_recu.header.ack == 1));

    //preparation de l'emission d'un ack

    pdu_emis.header.syn=0;
    pdu_emis.header.ack=1;

    IP_send(pdu_emis,addr);//envoi du pdu ack
*/
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

    int timeout=100;
    

    pdu_emis.header.source_port=socket_local[mic_sock].addr.port;
    pdu_emis.header.dest_port=addr_distant.port;
    pdu_emis.header.seq_num=PE;
    pdu_emis.header.ack_num=PE;
    pdu_emis.payload.data=mesg;
    pdu_emis.payload.size=mesg_size;

    PE=(PE+1)%2;//acquittement cumulatif, on incrémente


    int sent_size = IP_send(pdu_emis, addr_distant);
    IP_send(pdu_emis, addr_distant);//envoi du message

    IP_recv(&pdu_recu, &addr_recu, timeout); //on recupere l'acquittement
    

    while(!(pdu_recu.header.ack==1 && pdu_recu.header.ack_num==PE)){ //boucle wait et renvoi du message si pas bon numéo
        IP_send(pdu_emis, addr_distant);
        IP_recv(&pdu_recu, &addr_recu, timeout);
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
    //struct mic_tcp_pdu pdu_fin;
    //memset(&pdu_fin,sizeof(struct mic_tcp_pdu),0);

    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");

    /*pdu_fin.header.fin=1;

    if(IP_send(pdu_fin, addr_distant)==-1){ //envoi du pdu de fin de connexion
            printf("erreur IP_send\n");
            return -1;
    }*/

    
    //socket_local[socket].state=CLOSED;
    return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    struct mic_tcp_pdu pdu_ack;
        printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
        if(pdu.header.seq_num==PA){
            app_buffer_put(pdu.payload);
            PA=(PA+1)%2;
        }
        
        pdu_ack.header.ack=1;   
        pdu_ack.header.ack_num=PA;
     

        IP_send(pdu_ack, addr_distant);
    
}
