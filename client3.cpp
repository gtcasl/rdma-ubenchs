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

void srvServerSends(const opts &opt) {
  std::cout << "Server - server sends\n";
  Client Client;
  Client.HandleAddrResolved();
  Client.HandleRouteResolved();
  Client.Setup();

  assert(rdma_connect(Client.clientId, &Client.connParams) == 0);
  assert(rdma_get_cm_event(Client.eventChannel, &Client.event) == 0);
  assert(Client.event->event == RDMA_CM_EVENT_ESTABLISHED);

  rdma_ack_cm_event(Client.event);

  unsigned int outputSize = getOutputSize(opt);

  uint32_t *Key = new uint32_t();
  *Key = 15;
  MemRegion KeyMR(Key, sizeof(uint32_t), Client.protDomain);
  PostWrSend SendKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Client.clientId->qp);

  uint32_t *Do = new uint32_t[outputSize]();
  MemRegion DoMR(Do, sizeof(uint32_t) * outputSize, Client.protDomain);
  PostWrRecv RecvDo((uint64_t) Do, sizeof(uint32_t) * outputSize, DoMR.getRegion()->lkey,
                    Client.clientId->qp);

  Perf perf(opt.Measure);

  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    SendKey.exec();
    RecvDo.exec();
    Client.WaitForCompletion(2);
    std::cout << "Warm up " << it << "\n";
  }

  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    *Key = it;
    SendKey.exec();

    RecvDo.exec();

    // We can simply wait for the 2 events
    Client.WaitForCompletion(2);

    perf.stop();
    std::cout << "Do[" << outputSize - 1 << "]=" << Do[outputSize - 1] << "\n";
  }

  delete[] Do;
  delete Key;
  rdma_disconnect(Client.clientId);
}

void srvServerWrites(const opts &opt) {
  Client Client;
  Client.HandleAddrResolved();
  Client.HandleRouteResolved();
  Client.Setup();

  assert(rdma_connect(Client.clientId, &Client.connParams) == 0);
  assert(rdma_get_cm_event(Client.eventChannel, &Client.event) == 0);
  assert(Client.event->event == RDMA_CM_EVENT_ESTABLISHED);

  rdma_ack_cm_event(Client.event);

  unsigned int outputSize = getOutputSize(opt);

  uint32_t *Key = new uint32_t();
  *Key = 15;
  MemRegion KeyMR(Key, sizeof(uint32_t), Client.protDomain);
  PostWrSend SendKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Client.clientId->qp);

  uint32_t *Do = new uint32_t[outputSize]();
  MemRegion DoMR(Do, sizeof(uint32_t) * outputSize, Client.protDomain);
  SendSI SendSI(Do, DoMR.getRegion(), Client.protDomain);
  SendSI.post(Client.clientId->qp);
  ibv_recv_wr ZeroRecv = {};
  Client.WaitForCompletion(1);

  Perf perf(opt.Measure);

  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    SendKey.exec();
    check_z(ibv_post_recv(Client.clientId->qp, &ZeroRecv, NULL));

    Client.WaitForCompletion(2);
  }

  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    *Key = it;

    SendKey.exec();
    check_z(ibv_post_recv(Client.clientId->qp, &ZeroRecv, NULL));

    // We can simply wait for the 2 events
    Client.WaitForCompletion(2);

    perf.stop();
    std::cout << "Do[" << outputSize - 1 << "]=" << Do[outputSize - 1] << "\n";

    if (Do[outputSize - 1] != it * 100) {
      std::cout << "it=" << it << "Do=" << Do[outputSize - 1] << "\n";
      check(false, "data mismatch");
    }
  }

  delete[] Do;
  delete Key;
  rdma_disconnect(Client.clientId);
}

void clntServerSends(const opts &opt) {
  std::cout << "Client - server sends\n";
  std::cout << "Computation cost of " << opt.CompCost << "\n";

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

  uint32_t *Di = new uint32_t[opt.DiSize]();
  MemRegion DiMR(Di, sizeof(uint32_t) * opt.DiSize, Client.protDomain);
  PostWrRecv RecvDi((uint64_t) Di, sizeof(uint32_t) * opt.DiSize, DiMR.getRegion()->lkey,
                    Client.clientId->qp);

  Perf perf(opt.Measure);

  // WARM UP
  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    *Key = it;
    SendKey.exec();
    RecvDi.exec();
    Client.WaitForCompletion(2);
    expensiveFunc(opt.CompCost);
    std::cout << "Warm up " << it << "\n";
  }

  // REAL BENCHMARK
  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    *Key = it;
    SendKey.exec();

    RecvDi.exec();

    Client.WaitForCompletion(2);
    expensiveFunc(opt.CompCost);
    perf.stop();

    if (Di[opt.DiSize - 1] != it * 100) {
      std::cout << "it=" << it << "Di=" << Di[opt.DiSize - 1] << "\n";
      check(false, "data mismatch");
    }

    std::cout << "Di[" << opt.DiSize - 1 << "]=" << Di[opt.DiSize - 1] << "\n";
  }

  delete[] Di;
  delete Key;
  rdma_disconnect(Client.clientId);
}

void clntClientReads(const opts &opt) {
  std::cout << "Client - client reads\n";
  std::cout << "Computation cost of " << opt.CompCost << "\n";

  Client Client;
  Client.HandleAddrResolved();
  Client.HandleRouteResolved();
  Client.Setup();

  assert(rdma_connect(Client.clientId, &Client.connParams) == 0);
  assert(rdma_get_cm_event(Client.eventChannel, &Client.event) == 0);
  assert(Client.event->event == RDMA_CM_EVENT_ESTABLISHED);

  rdma_ack_cm_event(Client.event);

  RecvSI RecvSI(Client.protDomain);

  uint32_t *Key = new uint32_t();
  *Key = 15;
  MemRegion KeyMR(Key, sizeof(uint32_t), Client.protDomain);
  PostWrSend SendKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Client.clientId->qp);

  uint32_t *Di = new uint32_t[opt.DiSize]();
  size_t ReadSize = opt.DiSize * sizeof(uint32_t);
  MemRegion DiMR(Di, ReadSize, Client.protDomain);
  Sge ReadSGE((uint64_t) Di, ReadSize, DiMR.getRegion()->lkey);
  SendWR ReadWR(ReadSGE);
  ReadWR.setOpcode(IBV_WR_RDMA_READ);

  ibv_recv_wr ZeroRecv = {};

  RecvSI.post(Client.clientId->qp);
  Client.WaitForCompletion(1);
  RecvSI.print();

  ReadWR.setRdma(RecvSI.Info->Addr, RecvSI.Info->RemoteKey);

  Perf perf(opt.Measure);
  ReadWR.setSignaled();

  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    SendKey.exec();
    Client.WaitForCompletion(1);
    check_z(ibv_post_recv(Client.clientId->qp, &ZeroRecv, NULL)); // wait for signal to read mem
    Client.WaitForCompletion(1);
    ReadWR.post(Client.clientId->qp);
    Client.WaitForCompletion(1);
    expensiveFunc(opt.CompCost);
  }


  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();

    *Key = it;
    SendKey.exec();
    Client.WaitForCompletion(1); // wait for key to be sent

    check_z(ibv_post_recv(Client.clientId->qp, &ZeroRecv, NULL)); // wait for signal to read mem
    Client.WaitForCompletion(1);

    ReadWR.post(Client.clientId->qp);
    Client.WaitForCompletion(1);

    expensiveFunc(opt.CompCost);

    perf.stop();

    std::cout << "Di[" << opt.DiSize - 1 << "]=" << Di[opt.DiSize - 1] << "\n";

    if (Di[opt.DiSize - 1] != it * 100) {
      std::cout << "it=" << it << "Di=" << Di[opt.DiSize - 1] << "\n";
      check(false, "data mismatch");
    }
  }

  delete[] Key;
  delete[] Di;
  rdma_disconnect(Client.clientId);
}

int main(int argc, char *argv[]) {
  opts opt = parse_cl(argc, argv);

  if (opt.send && opt.ExecServer) {
    srvServerSends(opt);
  } else if (opt.write && opt.ExecServer) {
    srvServerWrites(opt);
  } else if (opt.Read && opt.ExecClient) {
    clntClientReads(opt);
  } else if (opt.send && opt.ExecClient) {
    clntServerSends(opt);
  } else {
    check(false, "Invalid combination of options");
  }

  return 0;
}
