#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include <cassert>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include "common3.h"

class Client {
protected:
  rdma_event_channel *eventChannel;
  rdma_cm_id *clientId;
  int port;
  ibv_pd *protDomain;
  ibv_mr *memReg;
  ibv_cq *compQueue;
  rdma_cm_event *event;

  rdma_conn_param connParams;
  sockaddr_in sin;
  ibv_qp_init_attr qpAttr;

  char *recvBuf;

  void HandleAddrResolved() {
    assert(eventChannel != NULL);
    assert(clientId != NULL);
    assert(event == NULL);

    D(std::cerr << "HandleAddrResolved\n");
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ADDR_RESOLVED);

    D(std::cerr << "Received RDMA_CM_EVENT_ADDR_RESOLVED\n");

    rdma_ack_cm_event(event);
  }

  void HandleRouteResolved() {
    assert(event != NULL);

    D(std::cerr << "HandleRouteResolved\n");
    assert(rdma_resolve_route(clientId, 2000) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ROUTE_RESOLVED);
    D(std::cerr << "Received RDMA_CM_EVENT_ROUTE_RESOLVED\n");
    rdma_ack_cm_event(event);
  }

  void Setup() {
    assert(clientId != NULL);

    assert((protDomain = ibv_alloc_pd(clientId->verbs)) != NULL);
    assert((memReg = ibv_reg_mr(protDomain, (void *) recvBuf, 256,
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ)) != NULL);

    assert((compQueue = ibv_create_cq(clientId->verbs, 32, 0, 0, 0)) != NULL);
    qpAttr.send_cq = qpAttr.recv_cq = compQueue;

    // queue pair
    assert(rdma_create_qp(clientId, protDomain, &qpAttr) == 0);
  }

  void ReceiveWR() {
    assert(recvBuf != NULL);
    assert(memReg != NULL);
    assert(clientId->qp != NULL);

    PostWrRecv recvWr((uint64_t) recvBuf, 256, memReg->lkey, clientId->qp);
    recvWr.Execute();

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);
  }

  void WaitForCompletion() {
    assert(compQueue != NULL);
    int ret = 0;

    ibv_wc workComp = {};

    while ((ret = ibv_poll_cq(compQueue, 1, &workComp)) == 0) {}

    if (ret < 0)
      printf("ibv_poll_cq returned %d\n", ret);

    if (workComp.status == IBV_WC_SUCCESS)
      printf("IBV_WC_SUCCESS\n");
    else
      printf("not IBV_WC_SUCCESS\n");
  }

public:
  Client() : eventChannel(NULL), clientId(NULL), port(21234),
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

    recvBuf = (char *) malloc(sizeof(char) * 256);
    memset(recvBuf, '\0', 256);

    assert((eventChannel = rdma_create_event_channel()) != NULL);
    assert(rdma_create_id(eventChannel, &clientId, NULL, RDMA_PS_TCP) == 0);

    sin = {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr("10.0.1.37");

    assert(rdma_resolve_addr(clientId, NULL, (sockaddr *) &sin, 2000) == 0);
  }

  ~Client() {
    if (clientId)
      rdma_destroy_qp(clientId);

    if (memReg)
      ibv_dereg_mr(memReg);

    if (compQueue)
      ibv_destroy_cq(compQueue);

    if (protDomain)
      ibv_dealloc_pd(protDomain);

    if (recvBuf)
      free(recvBuf);

    rdma_destroy_id(clientId);
    rdma_destroy_event_channel(eventChannel);
  }

  virtual void Start() {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();
    ReceiveWR();
    WaitForCompletion();
    printf("data received: %s\n", recvBuf);
  }

};

class ClientSWrites : Client {
public:
  ClientSWrites() {
  }

  ~ClientSWrites() {
  }

  void Start() override {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);

    SendRRI sendRRI(recvBuf, memReg, protDomain, clientId->qp);
    sendRRI.Execute();

    sleep(1);

    std::cout << "recv buffer: " << recvBuf << "\n";
    WaitForCompletion();
  }
};

class ClientCReads : Client {
  RemoteRegInfo *info;
public:

  ClientCReads() {
    info = new RemoteRegInfo();
  }

  ~ClientCReads() {
    delete info;
  }

  void Start() override {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    // receive RRI
    ibv_mr *mrInfo;
    check_nn(mrInfo = ibv_reg_mr(protDomain, (void *) info, 256,
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ));
    PostWrRecv recvWr((uint64_t) info, 256, mrInfo->lkey, clientId->qp);
    recvWr.Execute();

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);

    WaitForCompletion();
    D(std::cout << "received addr=" << std::hex << info->addr);
    D(std::cout << "\nreceived rkey=" << std::dec << info->rKey);

    // issue RDMA read
    PostRDMAWrSend rdmaSend((uint64_t) recvBuf, 256, memReg->lkey, clientId->qp,
                            info->addr, info->rKey);
    rdmaSend.Execute(true);
    sleep(1);

    std::cout << "\nrecv buffer: " << recvBuf << "\n";
  }

};

int main(int argc, char *argv[]) {
  int setting = parse_cl(argc, argv);

  switch(setting) {
    case 1: // client reads
    {
      ClientCReads client;
      client.Start();
      break;
    }
    case 2: // server writes
    {
      ClientSWrites client;
      client.Start();
      break;
    }
    default:
    {
      Client client;
      client.Start();
    }
  }

  return 0;
}
