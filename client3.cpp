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
public:
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

  Client() : clientId(NULL), recvBuf(NULL) {
    sin = {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr("10.0.1.37");

    check_nn(eventChannel = rdma_create_event_channel());
    check_z(rdma_create_id(eventChannel, &clientId, NULL, RDMA_PS_TCP));
    check_z(rdma_resolve_addr(clientId, NULL, (sockaddr *) &sin, 2000));
  }

  virtual ~Client() {
    if (clientId)
      rdma_destroy_qp(clientId);

    if (compQueue)
      ibv_destroy_cq(compQueue);

    if (protDomain)
      ibv_dealloc_pd(protDomain);

    rdma_destroy_id(clientId);
    rdma_destroy_event_channel(eventChannel);
  }
};

void clientServerSends(const opts &opt) {
  Client Client;
  Client.HandleAddrResolved();
  Client.HandleRouteResolved();
  Client.Setup();

  assert(rdma_connect(Client.clientId, &Client.connParams) == 0);
  assert(rdma_get_cm_event(Client.eventChannel, &Client.event) == 0);
  assert(Client.event->event == RDMA_CM_EVENT_ESTABLISHED);

  rdma_ack_cm_event(Client.event);

  uint32_t *Key = new uint32_t();
  *Key = 15;
  MemRegion KeyMR(Key, sizeof(uint32_t), Client.protDomain);
  PostWrSend SendKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Client.clientId->qp);

  uint32_t *Do = new uint32_t[opt.OutputEntries]();
  MemRegion DoMR(Do, sizeof(uint32_t) * opt.OutputEntries, Client.protDomain);
  PostWrRecv RecvDo((uint64_t) Do, sizeof(uint32_t) * opt.OutputEntries, DoMR.getRegion()->lkey,
                    Client.clientId->qp);

  for (unsigned it = 0; it < 50; ++it) {
    auto t0 = timer_start();
    *Key = it;
    SendKey.exec();

    RecvDo.exec();

    // We can simply wait for the 2 events
    Client.WaitForCompletion(2);

    timer_end(t0);
    std::cout << "Do[" << opt.OutputEntries - 1 << "]=" << Do[opt.OutputEntries - 1] << "\n";
  }

  delete[] Do;
  delete Key;
  rdma_disconnect(Client.clientId);
}

void clientLocalCompClient(const opts &opt) {
  Client Client;
  Client.HandleAddrResolved();
  Client.HandleRouteResolved();
  Client.Setup();

  assert(rdma_connect(Client.clientId, &Client.connParams) == 0);
  assert(rdma_get_cm_event(Client.eventChannel, &Client.event) == 0);
  assert(Client.event->event == RDMA_CM_EVENT_ESTABLISHED);

  rdma_ack_cm_event(Client.event);

  uint32_t *Key = new uint32_t();
  *Key = 15;
  MemRegion KeyMR(Key, sizeof(uint32_t), Client.protDomain);
  PostWrSend SendKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Client.clientId->qp);

  uint32_t *Di = new uint32_t[opt.KeysForFunc]();
  MemRegion DiMR(Di, sizeof(uint32_t) * opt.KeysForFunc, Client.protDomain);
  PostWrRecv RecvDi((uint64_t) Di, sizeof(uint32_t) * opt.KeysForFunc, DiMR.getRegion()->lkey,
                    Client.clientId->qp);

  for (unsigned it = 0; it < 50; ++it) {
    auto t0 = timer_start();
    *Key = it;
    SendKey.exec();

    RecvDi.exec();

    // We can simply wait for the 2 events
    Client.WaitForCompletion(2);
    expensiveFunc();
    timer_end(t0);
    std::cout << "Di[" << opt.KeysForFunc - 1 << "]=" << Di[opt.KeysForFunc - 1] << "\n";
  }

  delete[] Di;
  delete Key;
  rdma_disconnect(Client.clientId);
}

int main(int argc, char *argv[]) {
  opts opt = parse_cl(argc, argv);

  if (opt.send) {
    // send key and then receive Do.
    clientServerSends(opt);
  } else if (opt.write) {
  } else {
    // local computation on client.
    // send key and receive Di. then execute expensiveFunc.
    clientLocalCompClient(opt);
  }

  return 0;
}
