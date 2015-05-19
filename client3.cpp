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

class Client : public RDMAPeer {
protected:
  rdma_cm_id *clientId;
  ibv_mr *memReg;
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

  void Connect() {
    assert(eventChannel != NULL);
    assert(event != NULL);
    assert(clientId != NULL);

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);
  }

public:
  Client() : clientId(NULL), recvBuf(NULL) {

    recvBuf = (char *) malloc(sizeof(char) * 256);
    memset(recvBuf, '\0', 256);

    sin = {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr("10.0.1.37");

    check_nn(eventChannel = rdma_create_event_channel());
    check_z(rdma_create_id(eventChannel, &clientId, NULL, RDMA_PS_TCP));
    check_z(rdma_resolve_addr(clientId, NULL, (sockaddr *) &sin, 2000));
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

  virtual void Start(uint32_t entries) {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    PostWrRecv recvWr((uint64_t) recvBuf, entries * sizeof(TestData),
                      memReg->lkey, clientId->qp);
    recvWr.Execute();

    Connect();
    WaitForCompletion();

    for (unsigned i = 0; i < entries; ++i) {
      TestData *entry = (TestData *) (recvBuf + i * sizeof(TestData));
      D(std::cout << "entry " << i << " key " << entry->key << "\n");
    }

    rdma_disconnect(clientId);
  }
};

class ClientSWrites : Client {
public:
  ClientSWrites() {
  }

  ~ClientSWrites() {
  }

  void Start(uint32_t entries) override {
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

    sleep(1); // doesn't work without this, probably missing something.

    std::cout << "recv buffer: " << recvBuf << "\n";
    WaitForCompletion();
    rdma_disconnect(clientId);
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

  void Start(uint32_t entries) override {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    // receive RRI
    ibv_mr *mrInfo;
    check_nn(mrInfo = ibv_reg_mr(protDomain, (void *) info, sizeof(RemoteRegInfo),
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ));
    PostWrRecv recvWr((uint64_t) info, sizeof(RemoteRegInfo),
                      mrInfo->lkey, clientId->qp);
    recvWr.Execute();

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);

    WaitForCompletion();
    D(std::cout << "received addr=" << std::hex << info->addr << "\n");
    D(std::cout << "received rkey=" << std::dec << info->rKey << "\n");

    auto t0 = timer_start();

    // issue RDMA read
    PostRDMAWrSend rdmaSend((uint64_t) recvBuf, entries * sizeof(TestData), memReg->lkey, clientId->qp,
                            info->addr, info->rKey);
    rdmaSend.Execute(true);
    WaitForCompletion();

    timer_end(t0);

    for (unsigned i = 0; i < entries; ++i) {
      TestData *entry = (TestData *) (recvBuf + i * sizeof(TestData));
      D(std::cout << "entry " << i << " key " << entry->key << "\n");
    }

    rdma_disconnect(clientId);
  }
};

int main(int argc, char *argv[]) {
  opts opt = parse_cl(argc, argv);

  if (opt.read) {
    ClientCReads client;
    client.Start(opt.entries);
  } else if (opt.write) {
    ClientSWrites client;
    client.Start(opt.entries);
  } else {
    Client client;
    client.Start(opt.entries);
  }

  return 0;
}
