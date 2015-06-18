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

  // no filtering is performed at the server. we get the complete buffer.
  // the server knows there is an incoming request because we issue a connect.
  // we have to filter once we get the complete data.
  virtual void start(uint32_t entries) {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    TestData *Data = new TestData[entries]();

    check_nn(memReg = ibv_reg_mr(protDomain, (void *) Data, entries * sizeof(TestData),
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ));

    PostWrRecv recvWr((uint64_t) recvBuf, entries * sizeof(TestData),
                      memReg->lkey, clientId->qp);
    recvWr.exec();

    Connect();
    WaitForCompletion();

    // filter in the client, server sent everything in buffer
    std::vector<TestData> filtered = filterData(1, Data, entries);

    for (std::vector<TestData>::const_iterator i = filtered.begin(),
         e = filtered.end(); i < e; ++i) {
      D(std::cout << "key " << (*i).key << "\n");
    }

    ibv_dereg_mr(memReg);
    rdma_disconnect(clientId);
  }
};

// client for send/recv, receives filtered data
class ClientFiltered : Client {
public:
  virtual void start(uint32_t entries) {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    D(std::cout << "Send/recv filtered data client\n");

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    recvBuf = (char *) malloc(entries * sizeof(TestData));
    memset(recvBuf, '\0', entries * sizeof(TestData));

    check_nn(memReg = ibv_reg_mr(protDomain, (void *) recvBuf, entries * sizeof(TestData),
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ));

    PostWrRecv recvWr((uint64_t) recvBuf, entries * sizeof(TestData),
                      memReg->lkey, clientId->qp);
    recvWr.exec();

    Connect();
    WaitForCompletion();

    //for (unsigned i = 0; i < entries; ++i) {
    //  TestData *entry = (TestData *) (recvBuf + i * sizeof(TestData));
    //  D(std::cout << "entry " << i << " key " << entry->key << "\n");
    //}

    free(recvBuf);
    ibv_dereg_mr(memReg);
    rdma_disconnect(clientId);

  }
};

class ClientSWrites : Client {
public:
  ClientSWrites() {
  }

  ~ClientSWrites() {
  }

  void start(uint32_t entries) override {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);

    uint32_t *Key = new uint32_t();
    *Key = 15;
    MemRegion KeyMR(Key, sizeof(uint32_t), protDomain);
    PostWrSend SendKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                       clientId->qp);

    uint32_t *Do = new uint32_t[32]();
    MemRegion DoMR(Do, sizeof(uint32_t) * 32, protDomain);
    PostWrRecv RecvDo((uint64_t) Do, sizeof(uint32_t) * 32, DoMR.getRegion()->lkey,
                      clientId->qp);

    SendKey.exec();
    WaitForCompletion();

    for (unsigned it = 0; it < 10; ++it) {
      RecvDo.exec();
      WaitForCompletion();
      std::cout << "Do[7]=" << Do[7] << "\n";

      *Key = it;
      SendKey.exec();
      WaitForCompletion();
    }

    rdma_disconnect(clientId);
    delete[] Do;
    delete Key;
  }
};

class ClientCReads : Client {
public:
  void start(uint32_t entries) override {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    recvBuf = (char *) malloc(entries * sizeof(TestData));
    memset(recvBuf, '\0', entries * sizeof(TestData));

    assert((memReg = ibv_reg_mr(protDomain, (void *) recvBuf, entries * sizeof(TestData),
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ)) != NULL);

    // receive RRI
    RecvSI ReceiveSI(protDomain);
    ReceiveSI.post(clientId->qp);

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);

    WaitForCompletion();
    ReceiveSI.print();

    auto t0 = timer_start();

    // issue RDMA read
    PostRDMAWrSend rdmaSend((uint64_t) recvBuf, entries * sizeof(TestData), memReg->lkey, clientId->qp,
                            ReceiveSI.Info->Addr, ReceiveSI.Info->RemoteKey);
    rdmaSend.exec(true);
    WaitForCompletion();

    timer_end(t0);

    //for (unsigned i = 0; i < entries; ++i) {
    //  TestData *entry = (TestData *) (recvBuf + i * sizeof(TestData));
    //  D(std::cout << "entry " << i << " key " << entry->key << "\n");
    //}

    free(recvBuf);
    ibv_dereg_mr(memReg);
    rdma_disconnect(clientId);
  }
};

class ClientCReadsFiltered : Client {
public:
  void start(uint32_t entries) override {
    assert(eventChannel != NULL);
    assert(clientId != NULL);

    D(std::cout << "RDMA filtered client (filtering occurs at server)\n");

    HandleAddrResolved();
    HandleRouteResolved();
    Setup();

    TestData *Data = new TestData[entries]();

    assert((memReg = ibv_reg_mr(protDomain, (void *) Data, entries * sizeof(TestData),
                                IBV_ACCESS_REMOTE_WRITE |
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ)) != NULL);

    // receive RRI
    RecvSI ReceiveSI(protDomain);
    ReceiveSI.post(clientId->qp);

    assert(rdma_connect(clientId, &connParams) == 0);
    assert(rdma_get_cm_event(eventChannel, &event) == 0);
    assert(event->event == RDMA_CM_EVENT_ESTABLISHED);

    rdma_ack_cm_event(event);

    WaitForCompletion();
    ReceiveSI.print();

    auto t0 = timer_start();

    // issue RDMA read
    // TODO: fix assumption of entries / 2
    PostRDMAWrSend rdmaSend((uint64_t) Data, (entries / 2) * sizeof(TestData), memReg->lkey, clientId->qp,
                            ReceiveSI.Info->Addr, ReceiveSI.Info->RemoteKey);
    rdmaSend.exec(true);
    WaitForCompletion();

    timer_end(t0);

    //for (unsigned i = 0; i < entries; ++i) {
    //  TestData *entry = (TestData *) (recvBuf + i * sizeof(TestData));
    //  D(std::cout << "entry " << i << " key " << entry->key << "\n");
    //}

    ibv_dereg_mr(memReg);
    rdma_disconnect(clientId);
  }
};

int main(int argc, char *argv[]) {
  opts opt = parse_cl(argc, argv);

  if (opt.read) {
    if (opt.filtered) { // filtered rdma
      ClientCReadsFiltered client;
      client.start(opt.entries);
    } else { // unfiltered rdma
      ClientCReads client;
      client.start(opt.entries);
    }
  } else if (opt.write) {
    ClientSWrites client;
    client.start(opt.entries);
  } else if (opt.filtered) { // filtered client
    ClientFiltered client;
    client.start(opt.entries);
  } else { // unfiltered client
    Client client;
    client.start(opt.entries);
  }

  return 0;
}
