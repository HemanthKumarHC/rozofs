/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation, version 2.

 Rozofs is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see
 <http://www.gnu.org/licenses/>.
 */
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <netdb.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/rpc/eproto.h>
#include "expgw_common.h"



expgw_eid_ctx_t      expgw_eid_table[EXPGW_EID_MAX_IDX];
expgw_exportd_ctx_t  expgw_exportd_table[EXPGW_EXPORTD_MAX_IDX];


static north_remote_ip_list_t my_list[32];  /**< list of the connection for the exportd */

 /**
 *  socket configuration for the family
 */
static af_unix_socket_conf_t  af_inet_exportd_conf =
{
  1,  //           family: identifier of the socket family    */
  0,         /**< instance number within the family   */
  sizeof(uint32_t),  /* headerSize  -> size of the header to read                 */
  0,                 /* msgLenOffset->  offset where the message length fits      */
  sizeof(uint32_t),  /* msgLenSize  -> size of the message length field in bytes  */
  
  (1024*256), /*  bufSize        -> length of buffer (xmit and received)        */
  (300*1024), /*  so_sendbufsize -> length of buffer (xmit and received)        */
  rozofs_tx_userRcvAllocBufCallBack, /*  userRcvAllocBufCallBack -> user callback for buffer allocation             */
  rozofs_tx_recv_rpc_cbk,            /*  userRcvCallBack         -> callback provided by the connection owner block */
  rozofs_tx_xmit_abort_rpc_cbk,      /*  userDiscCallBack        ->callBack for TCP disconnection detection         */
  NULL,                              /* userConnectCallBack     -> callback for client connection only              */
  NULL,  //    userXmitDoneCallBack; /**< optional call that must be set when the application when to be warned when packet has been sent */
  NULL,  //    userRcvReadyCallBack; /* NULL for default callback                    */
  NULL,  //    userXmitReadyCallBack; /* NULL for default callback                    */
  NULL,  //    userXmitEventCallBack; /* NULL for default callback                    */
  rozofs_tx_get_rpc_msg_len_cbk,        /* userHdrAnalyzerCallBack ->NULL by default, function that analyse the received header that returns the payload  length  */
  ROZOFS_RPC_SRV,       /* recv_srv_type ---> service type for reception : ROZOFS_RPC_SRV or ROZOFS_GENERIC_SRV  */
  0,       /*   rpc_recv_max_sz ----> max rpc reception buffer size : required for ROZOFS_RPC_SRV only */
  NULL,  //    *userRef;           /* user reference that must be recalled in the callbacks */
  NULL,  //    *xmitPool; /* user pool reference or -1 */
  NULL   //    *recvPool; /* user pool reference or -1 */
};

/*
**______________________________________________________________________________
*/
/**
*  Convert a hostname into an IP v4 address in host format 

@param host : hostname
@param ipaddr_p : return IP V4 address arreay

@retval 0 on success
@retval -1 on error (see errno faor details
*/
int expgw_host2ip(char *host,uint32_t *ipaddr_p)
{
    struct hostent *hp;    
    /*
    ** get the IP address of the storage node
    */
    if ((hp = gethostbyname(host)) == 0) {
        severe("gethostbyname failed for host : %s, %s", host,
                strerror(errno));
        return -1;
    }
    bcopy((char *) hp->h_addr, (char *) ipaddr_p, hp->h_length);
    *ipaddr_p = ntohl(*ipaddr_p);
    return 0;
    
}




/*
**____________________________________________________
*/
/**
*   API to create a load balancing group towards an exportd

  @param exportclt : context of the exportd (hostname, lbg reference , etc...  
  @retval  0 on success

*/
int expgw_expgateway_lbg_initialize(expgw_expgw_ctx_t *exportclt) 
{
    int status = -1;
    int lbg_size;
    
    /*
    ** store the IP address and port in the list of the endpoint
    */
    my_list[0].remote_port_host = exportclt->port;
    my_list[0].remote_ipaddr_host = exportclt->ipaddr ;
    lbg_size = 1;
    
     af_inet_exportd_conf.recv_srv_type = ROZOFS_RPC_SRV;
     af_inet_exportd_conf.rpc_recv_max_sz = rozofs_large_tx_recv_size;
     
     exportclt->gateway_lbg_id = north_lbg_create_af_inet("EXPGW",INADDR_ANY,0,my_list,ROZOFS_SOCK_FAMILY_EXPORT_NORTH,lbg_size,&af_inet_exportd_conf);
     if (exportclt->gateway_lbg_id >= 0)
     {
       status = 0;
       return status;    
     }
     severe("Cannot create Load Balancing Group for Exportd");

     return  status;
}     


/*
**____________________________________________________
*/
/*
  Request port mapper to give the port number of a TCP rpc service on a remote host
  @param host  The remote host name
  @param prog  The program number
  @param vers  The version number of the program
*/
static int get_service_tcp_port(char *host ,unsigned long prog, unsigned long vers) {
  struct sockaddr_in server;
  struct hostent *hp;
  int port = 0;

  server.sin_family = AF_INET;

  if ((hp = gethostbyname(host)) == 0) {
      severe("gethostbyname failed for host : %s, %s", host,strerror(errno));
      return 0;
  }

  bcopy((char *) hp->h_addr, (char *) &server.sin_addr, hp->h_length);
  if ((port = pmap_getport(&server, prog, vers, IPPROTO_TCP)) == 0) {
    warning("pmap_getport failed %s (%x:%d) %s",  host, (unsigned int)prog, (int)vers, clnt_spcreateerror(""));
    errno = EPROTO;
    return 0;
  }
  
  info("rpc service %s (%x:%d) available on port %d",  host, (unsigned int)prog, (int)vers, port);
  return port;
}     
/*
**____________________________________________________
*/
/*
  Periodic timer expiration
*/
static void export_lbg_periodic_ticker(void * param) {
  int status;
  uint16_t port;
  expgw_exportd_ctx_t *exportclt = (expgw_exportd_ctx_t *) param;
   
  /* Check whether the export LBG is up ,*/
  status = north_lbg_get_state(exportclt->export_lbg_id);
//  info("export_lbg_periodic_ticker status %d\n", status);
  
  if (status == NORTH_LBG_UP) return;

  /* Try to find out whether the export service is up again */
  port = get_service_tcp_port(exportclt->hostname,EXPORT_PROGRAM, EXPORT_VERSION);
  if (port == 0) {
    return; /* Still unavailable */
  }
 
  /* The service is back again on a new port */
  my_list[0].remote_port_host = port;
  north_lbg_re_configure_af_inet_destination_port(exportclt->export_lbg_id, my_list, 1); 
}
/*
**____________________________________________________
*/
/*
  start a periodic timer to chech wether the export LBG is down
  When the export is restarted its port may change, and so
  the previous configuration of the LBG is not valid any more
*/
static void export_lbg_start_timer(expgw_exportd_ctx_t *exportclt) {
  struct timer_cell * export_lbg_periodic_timer;

  export_lbg_periodic_timer = ruc_timer_alloc(0,0);
  if (export_lbg_periodic_timer == NULL) {
    severe("export_lbg_start_timer");
    return;
  }
  ruc_periodic_timer_start (export_lbg_periodic_timer, 
                            4000,
 	                    export_lbg_periodic_ticker,
 			    exportclt);

}


/*
**____________________________________________________
*/
/**
*   API to create a load balancing group towards an exportd

  @param exportclt : context of the exportd (hostname, lbg reference , etc...
  @param prog,vers : rpc program and version needed by port map
  @param port_num: if portnum is not null, port map is not used
  
  @retval  0 on success

*/
int expgw_export_lbg_initialize(expgw_exportd_ctx_t *exportclt ,unsigned long prog,
        unsigned long vers,uint32_t port_num) {
    int status = -1;
    struct sockaddr_in server;
    struct hostent *hp;
    int port = 0;
    int lbg_size;
    
    DEBUG_FUNCTION;    

    server.sin_family = AF_INET;


    if ((hp = gethostbyname(exportclt->hostname)) == 0) {
        severe("gethostbyname failed for host : %s, %s", exportclt->hostname,
                strerror(errno));
        goto out;
    }

    bcopy((char *) hp->h_addr, (char *) &server.sin_addr, hp->h_length);
    if (port_num == 0) {
        if ((port = pmap_getport(&server, prog, vers, IPPROTO_TCP)) == 0) {
            warning("pmap_getport failed%s", clnt_spcreateerror(""));
            errno = EPROTO;
            goto out;
        }
        server.sin_port = htons(port);
    } else {
        server.sin_port = htons(port_num);
    }
    /*
    ** store the IP address and port in the list of the endpoint
    */
    my_list[0].remote_port_host = ntohs(server.sin_port);
    my_list[0].remote_ipaddr_host = ntohl(server.sin_addr.s_addr);
    lbg_size = 1;
    
     af_inet_exportd_conf.recv_srv_type = ROZOFS_RPC_SRV;
     af_inet_exportd_conf.rpc_recv_max_sz = rozofs_large_tx_recv_size;
     
     exportclt->export_lbg_id = north_lbg_create_af_inet("EXPORTD",INADDR_ANY,0,my_list,ROZOFS_SOCK_FAMILY_EXPORT_NORTH,lbg_size,&af_inet_exportd_conf);
     if (exportclt->export_lbg_id >= 0)
     {
       status = 0;
       export_lbg_start_timer (exportclt);      
       return status;    
     }
     severe("Cannot create Load Balancing Group for Exportd");

out:
     return  status;
}     
      

/*
**______________________________________________________________________________
*/
/**
* That API must be called before opening the export gateway service

*/
void expgw_export_tableInit()
{  
  memset(expgw_eid_table,0,sizeof(expgw_eid_table));
  memset(expgw_exportd_table,0,sizeof(expgw_exportd_table));
  int i,j;
  for (i = 0; i < EXPGW_EXPORTD_MAX_IDX; i++)
  {
    expgw_exportd_table[i].export_lbg_id = -1;  
    for (j = 0; j < EXPGW_EXPGW_MAX_IDX; j++)
    {
      expgw_exportd_table[i].expgw_list[j].gateway_lbg_id = -1;  
    }

  }
}

/*
**______________________________________________________________________________
*/
/**
*  Add an eid entry 

  @param exportd_id : reference of the exportd that mangament the eid
  @param eid: eid within the exportd
  @param hostname: hostname of the exportd
  @param port:   port of the exportd
  @param nb_gateways : number of gateways
  @param gateway_rank : rank of the current export gateway
  
  @retval 0 on success
  @retval -1 on error (see errno for details)
  
*/

int expgw_export_add_eid(uint16_t exportd_id, uint16_t eid, char *hostname, 
                      uint16_t port,uint16_t nb_gateways,uint16_t gateway_rank)
{

  if ((exportd_id >= EXPGW_EID_MAX_IDX) || (eid >= EXPGW_EXPORTD_MAX_IDX))
  {
    errno = EINVAL;
    return -1;
  }
  if (nb_gateways <= gateway_rank)
  {
    errno = EINVAL;
    return -1;  
  }
  expgw_eid_table[eid].eid = eid;
  expgw_eid_table[eid].exportd_id = exportd_id;
    
  expgw_exportd_table[exportd_id].nb_gateways  = nb_gateways;
  expgw_exportd_table[exportd_id].gateway_rank = gateway_rank;
  /*
  ** check if the export_id has already been declared
  */
  if (expgw_exportd_table[exportd_id].exportd_id == exportd_id)
  {
    /*
    ** already configured, nothing more to do:
    ** we assume that the IP address cannot be changed: VIP
    */
    return 0;
  }
  expgw_exportd_table[exportd_id].exportd_id  = exportd_id;  
  strcpy(expgw_exportd_table[exportd_id].hostname, hostname);
  if (expgw_host2ip(hostname,&expgw_exportd_table[exportd_id].ipaddr) < 0)
  {
    return -1;  
  }
  expgw_exportd_table[exportd_id].port  = port;  
  /*
  ** create the load balancing group
  */
  if (expgw_export_lbg_initialize(&expgw_exportd_table[exportd_id],EXPORT_PROGRAM, EXPORT_VERSION, 0) != 0)
  {
    return -1;
  }
  return 0;
}                      
  
  
/*
**______________________________________________________________________________
*/
/**
*  update an ienetry with a new gateway count and gateway rank 

  @param eid: eid within the exportd
  @param nb_gateways : number of gateways
  @param gateway_rank : rank of the current export gateway
  
  @retval 0 on success
  @retval -1 on error (see errno for details)
  
*/
int expgw_export_update_eid_gw_info(uint16_t eid,uint16_t nb_gateways,uint16_t gateway_rank)
{
  int exportd_id;
  if ((eid >= EXPGW_EXPORTD_MAX_IDX) || (nb_gateways <= gateway_rank))
  {
    errno = EINVAL;
    return -1;
  }
  exportd_id = expgw_eid_table[eid].exportd_id;
  if (exportd_id == 0)
  {
    errno = ENOENT;
    return -1;    
  }    
  expgw_exportd_table[exportd_id].nb_gateways  = nb_gateways;
  expgw_exportd_table[exportd_id].gateway_rank = gateway_rank;
  return 0;
}

/*
**______________________________________________________________________________
*/
/**
*  Add an export gateway entry 

  @param exportd_id : reference of the exportd that mangament the eid
  @param hostname: hostname of the exportd
  @param port:   port of the exportd
  @param gateway_rank : rank of the current export gateway
  
  @retval 0 on success
  @retval -1 on error (see errno for details)
  
*/

int expgw_add_export_gateway(uint16_t exportd_id, char *hostname, 
                             uint16_t port,uint16_t gateway_rank)
{

  if (exportd_id >= EXPGW_EID_MAX_IDX) 
  {
    errno = EINVAL;
    return -1;
  }
  if (expgw_exportd_table[exportd_id].exportd_id == 0)
  {
    errno = EINVAL;
    return -1;  
  }
  if (expgw_exportd_table[exportd_id].nb_gateways <= gateway_rank)
  {
    errno = EINVAL;
    return -1;  
  }
  expgw_expgw_ctx_t *Pgw;
  
  Pgw = &expgw_exportd_table[exportd_id].expgw_list[gateway_rank];
  
  strncpy(Pgw->hostname, hostname, ROZOFS_HOSTNAME_MAX);
  if (expgw_host2ip(hostname,&Pgw->ipaddr) < 0)
  {
    return -1;  
  }
  Pgw->port  = port;  
  /*
  ** create the load balancing group
  */
  if (expgw_expgateway_lbg_initialize(Pgw) != 0)
  {
    return -1;
  }
  return 0;
}                      
  


/*
**______________________________________________________________________________
*/
/**
   That function is intended to be used by expgw for ingress
*  check if the fid is to be cached locally 

  @param eid: eid within the exportd
  @param fid : fid of the incoming response or request
  
  @retval 0 local
  @retval ==  1 not local
  @retval <  0 error (see errno for details)
  
*/
int expgw_check_local(uint16_t eid,fid_t fid)
{
    uint32_t slice;
    uint32_t subslice;
    uint16_t srv_rank;

  int exportd_id;
  if (eid >= EXPGW_EXPORTD_MAX_IDX)
  {
    errno = EINVAL;
    return -1;
  }
  exportd_id = expgw_eid_table[eid].exportd_id;
  if (exportd_id == 0)
  {
    errno = ENOENT;
    return -1;    
  }    
  if (expgw_exportd_table[exportd_id].nb_gateways == 0)
  {
    errno = EINVAL;
    return -1;  
  }
  /*
  ** get the slice from the fid
  */
   mstor_get_slice_and_subslice(fid,&slice,&subslice);
   srv_rank = slice%expgw_exportd_table[exportd_id].nb_gateways;
   if (srv_rank == expgw_exportd_table[exportd_id].gateway_rank)
   {
     return 0;
   }
   return 1;
}
/*
**______________________________________________________________________________
*/
/**
* Get the reference of the LBG for the eid (default route to the master exportd)

  @param eid: eid within the exportd
  
  @retval < 0 not found (see errno)
  @retval >= 0 : reference of the load balaning group
  
*/

int expgw_get_exportd_lbg(uint16_t eid)
{

  int exportd_id;
    
  if (eid >= EXPGW_EXPORTD_MAX_IDX)
  {
    errno = EINVAL;
    return -1;
  }
  exportd_id = expgw_eid_table[eid].exportd_id;
  if (exportd_id == 0)
  {
    errno = ENOENT;
    return -1;    
  }    
  return expgw_exportd_table[exportd_id].export_lbg_id;
}





/*
**______________________________________________________________________________
*/
/**
  That function is inteneded to be called to get the reference of an egress
  load balancing group:
   The system attemps to select the exportd gateway load balancing group.
   If the exportd gateway load balancing group is down, it returns the
   default one which is the load balancinbg group towards
   the master EXPORTD associated with the eid
   
   
  @param eid: eid within the exportd
  @param fid : fid of the incoming response or request
  
  @retval >= 0 : reference of the lood balancing group 
  @retval <  0 no load balancing group 
  
*/
int expgw_get_export_gateway_lbg(uint16_t eid,fid_t fid)
{
    uint32_t slice;
    uint32_t subslice;
    uint16_t srv_rank;
    int lbg_id = -1;

  int exportd_id;
  if (eid >= EXPGW_EXPORTD_MAX_IDX)
  {
    errno = EINVAL;
    return -1;
  }
  exportd_id = expgw_eid_table[eid].exportd_id;
  if (exportd_id == 0)
  {
    errno = ENOENT;
    return -1;    
  }    
  if (expgw_exportd_table[exportd_id].nb_gateways == 0)
  {
    return expgw_exportd_table[exportd_id].export_lbg_id;
  }
  /*
  ** get the slice from the fid
  */
   mstor_get_slice_and_subslice(fid,&slice,&subslice);
   srv_rank = slice%expgw_exportd_table[exportd_id].nb_gateways;
   lbg_id = expgw_exportd_table[exportd_id].expgw_list[srv_rank].gateway_lbg_id ;
   if (lbg_id == -1)
   {
     return expgw_exportd_table[exportd_id].export_lbg_id;
   }
   /*
   ** check the state of the load balancing group
   */
   if (north_lbg_get_state(lbg_id) != NORTH_LBG_UP)
   {
     return expgw_exportd_table[exportd_id].export_lbg_id;   
   }
   return lbg_id;
}

