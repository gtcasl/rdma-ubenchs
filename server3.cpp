#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include <cassert>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include "common3.h"

class Server {
protected:
  rdma_event_channel *eventChannel;
  rdma_cm_id *serverId;
  rdma_cm_id *clientId;
  int port;
  ibv_pd *protDomain;
  ibv_mr *memReg;
  ibv_cq *compQueue;
  rdma_cm_event *event;

  rdma_conn_param connParams;
  sockaddr_in sin;
  ibv_qp_init_attr qpAttr;

  char *serverBuff;


  void HandleConnectRequest() {
    assert(eventChannel != NULL);
    assert(serverId != NULL);
    assert(event == NULL);
    D(std::cerr << "HandleConnectRequest\n");

    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_CONNECT_REQUEST);

    D(std::cerr << "Received RDMA_CM_EVENT_CONNECT_REQUEST\n");

    clientId = (rdma_cm_id *) event->id;

    // create a prot domain for the client rdma device
    assert((protDomain = ibv_alloc_pd(clientId->verbs)) != NULL);
    assert((compQueue = ibv_create_cq(clientId->verbs, 32, 0, 0, 0)) != NULL);

    qpAttr.send_cq = qpAttr.recv_cq = compQueue;

    // queue pair
    assert(rdma_create_qp(clientId, protDomain, &qpAttr) == 0);
    assert(rdma_accept(clientId, &connParams) == 0);

    rdma_ack_cm_event(event);
  }

  void HandleConnectionEstablished() {
    assert(event != NULL);
    D(std::cerr << "HandleConnectionEstablished\n");

    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);
    rdma_ack_cm_event(event);
  }

  void SendWorkRequest() {
    assert(serverBuff != NULL);
    assert(memReg != NULL);
    assert(clientId != NULL);

    PostWrSend send((uint64_t) serverBuff, strlen(serverBuff) + 1, memReg->lkey, clientId->qp);
    send.Execute();
  }

  void WaitForCompletion() {
    assert(compQueue != NULL);
    int ret = 0;

    ibv_wc workComp = {};

    while ((ret = ibv_poll_cq(compQueue, 1, &workComp)) == 0) {}

    if (ret < 0)
      D(std::cerr << "ibv_poll_cq returned " << ret << "\n");

    if (workComp.status != IBV_WC_SUCCESS)
      D(std::cerr << "not IBV_WC_SUCCESS: " << ret << "\n");
  }

public:
  Server() : eventChannel(NULL), serverId(NULL), clientId(NULL), port(21234),
             protDomain(NULL), memReg(NULL), compQueue(NULL), event(NULL) {

    connParams = {};
    connParams.initiator_depth = 1;
    connParams.responder_resources = 1;
    qpAttr = {};
    qpAttr.cap.max_send_wr = 32;
    qpAttr.cap.max_recv_wr = 32;
    qpAttr.cap.max_send_sge = 1;
    qpAttr.cap.max_recv_sge = 1;
    qpAttr.cap.max_inline_data = 64;
    qpAttr.qp_type = IBV_QPT_RC;

    serverBuff = (char *) malloc(sizeof(char) * 256);
    strcpy(serverBuff, "HellO worlD!");

    assert((eventChannel = rdma_create_event_channel()) != NULL);
    assert(rdma_create_id(eventChannel, &serverId, NULL, RDMA_PS_TCP) == 0);

    sin = {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);

    assert(rdma_bind_addr(serverId, (sockaddr *) &sin) == 0);
    assert(rdma_listen(serverId, 6) == 0);
  }

  virtual ~Server() {
    if (clientId)
      rdma_destroy_qp(clientId);

    if (memReg)
      ibv_dereg_mr(memReg);

    if (compQueue)
      ibv_destroy_cq(compQueue);

    if (protDomain)
      ibv_dealloc_pd(protDomain);

    if (serverBuff)
      free(serverBuff);

    rdma_destroy_id(serverId);
    rdma_destroy_event_channel(eventChannel);

  }

  virtual void Start() {
    assert(eventChannel != NULL);
    assert(serverId != NULL);

    HandleConnectRequest();

    assert((memReg = ibv_reg_mr(protDomain, (void *) serverBuff, 256,
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ)) != NULL);

    HandleConnectionEstablished();
    SendWorkRequest();
    WaitForCompletion();
  }

};

class ServerSWrites : Server {
  RemoteRegInfo *info;

public:

  ServerSWrites() {
    info = new RemoteRegInfo();
  }

  ~ServerSWrites() {
    delete info;
  }

  void Start() override {
    assert(eventChannel != NULL);
    assert(serverId != NULL);

    HandleConnectRequest();

    assert((memReg = ibv_reg_mr(protDomain, (void *) info, sizeof(RemoteRegInfo),
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ)) != NULL);

    // TODO: should dereg the memReg

    // posting before receving RDMA_CM_EVENT_ESTABLISHED, otherwise
    // it fails saying there is no receive posted.
    PostWrRecv recvWr((uint64_t) info, 256, memReg->lkey, clientId->qp);
    recvWr.Execute();

    HandleConnectionEstablished();

    // now setup the remote memory where we'll be able to write directly
    assert((memReg = ibv_reg_mr(protDomain, (void *) serverBuff, 256,
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ)) != NULL);

    D(std::cerr << "client addr=" << std::hex << info->addr);
    D(std::cerr << "\nclient rkey=" << std::dec << info->rKey);

    //WaitForCompletion();

    PostRDMAWrSend rdmaSend((uint64_t) serverBuff, 256, memReg->lkey, clientId->qp,
                            info->addr, info->rKey);
    rdmaSend.Execute();

    strcpy(serverBuff, "HellO worlD RDMA!");

    WaitForCompletion();
  }
};

class ServerCReads : Server {
  RemoteRegInfo *info;

public:

  ServerCReads() {
  }

  ~ServerCReads() {
  }

  void Start() override {
    assert(eventChannel != NULL);
    assert(serverId != NULL);

    HandleConnectRequest();

    HandleConnectionEstablished();
    strcpy(serverBuff, "HellO worlD This is server's buff!");

    assert((memReg = ibv_reg_mr(protDomain, (void *) serverBuff, 256,
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ)) != NULL);


    // send the RRI
    SendRRI sendRRI(serverBuff, memReg, protDomain, clientId->qp);
    sendRRI.Execute();
    strcpy(serverBuff, "HellO worlD This is server's buff!");

    WaitForCompletion();
  }
};

int main(int argc, char *argv[]) {
  int setting = parse_cl(argc, argv);

  switch(setting) {
    case 1: // client reads
    {
      ServerCReads server;
      server.Start();
      break;
    }
    case 2: // server writes
    {
      ServerSWrites server;
      server.Start();
      break;
    }
    default:
    {
      Server server;
      server.Start();
    }
  }

  return 0;
}
