#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include <cassert>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include "common3.h"

class Server : public RDMAPeer {
public:
  rdma_cm_id *serverId;
  rdma_cm_id *clientId;
  ibv_mr *memReg;

  Server() : RDMAPeer(), serverId(NULL), clientId(NULL), memReg(NULL) {
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

    if (compQueue)
      ibv_destroy_cq(compQueue);

    if (protDomain)
      ibv_dealloc_pd(protDomain);

    rdma_destroy_id(serverId);
    rdma_destroy_event_channel(eventChannel);
  }

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

  void HandleDisconnect() {
    assert(event != NULL);
    D(std::cerr << "HandleDisconnect\n");

    check_z(rdma_get_cm_event(eventChannel, &event));
    assert(event->event == RDMA_CM_EVENT_DISCONNECTED);
    rdma_ack_cm_event(event);
  }
};

void srvServerSends(const opts &opt) {
  Server Srv;
  Srv.HandleConnectRequest();

  uint32_t *Key = new uint32_t();
  MemRegion KeyMR(Key, sizeof(uint32_t), Srv.protDomain);
  PostWrRecv RecvKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Srv.clientId->qp);

  unsigned int outputSize = getOutputSize(opt);
  std::cout << "Server - server sends\n";
  std::cout << "Will send Do buffer of size " << outputSize << "\n";
  std::cout << "The cost of comp will be " << opt.CompCost << "\n";

  uint32_t *Do = new uint32_t[outputSize]();
  Do[outputSize - 1] = 0x1234;
  MemRegion DoMR(Do, sizeof(uint32_t) * outputSize, Srv.protDomain);
  PostWrSend SendDo((uint64_t) Do, sizeof(uint32_t) * outputSize, DoMR.getRegion()->lkey,
                     Srv.clientId->qp);

  Perf perf(opt.Measure);

  // WARM UP
  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    RecvKey.exec();
    // This way we save ourselves from waiting for Do to be sent.
    if (it == 0) {
      Srv.HandleConnectionEstablished();
      Srv.WaitForCompletion(1);
    } else {
      Srv.WaitForCompletion(2);
    }

    expensiveFunc(opt.CompCost);
    SendDo.exec();
    std::cout << "Warm up " << it << "\n";
  }

  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    RecvKey.exec();

    Srv.WaitForCompletion(2);

    // key can be used from this point forward safely

    // assume the function needs a subset A of a large set B to exec. if we were to
    // run the func locally on the client, we would need to transfer A first.
    expensiveFunc(opt.CompCost);

    Do[outputSize - 1] = it * 100;
    SendDo.exec();

    perf.stop();
    std::cout << "key=" << *Key << "\n";
  }

  Srv.WaitForCompletion(1);

  delete Key;
  delete[] Do;
  Srv.HandleDisconnect();
}

void srvServerWrites(const opts &opt) {
  unsigned int outputSize = getOutputSize(opt);

  std::cout << "Server - server writes\n";
  std::cout << "Will write Do buffer of size " << outputSize << "\n";
  std::cout << "The cost of comp will be " << opt.CompCost << "\n";

  Server Srv;
  Srv.HandleConnectRequest();

  uint32_t *Key = new uint32_t();
  MemRegion KeyMR(Key, sizeof(uint32_t), Srv.protDomain);
  PostWrRecv RecvKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Srv.clientId->qp);

  uint32_t *Do = new uint32_t[outputSize]();
  size_t WriteSize = outputSize * sizeof(uint32_t);
  Do[outputSize - 1] = 0x1234;
  MemRegion DoMR(Do, WriteSize, Srv.protDomain);
  Sge WriteSGE((uint64_t) Do, WriteSize, DoMR.getRegion()->lkey);
  SendWR WriteWR(WriteSGE);
  WriteWR.setOpcode(IBV_WR_RDMA_WRITE);

  SendWR ZeroWR;
  ZeroWR.setOpcode(IBV_WR_SEND);

  RecvSI RecvSI(Srv.protDomain);
  RecvSI.post(Srv.clientId->qp);
  Srv.WaitForCompletion(1);
  WriteWR.setRdma(RecvSI.Info->Addr, RecvSI.Info->RemoteKey);

  Perf perf(opt.Measure);

  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    RecvKey.exec();

    // The first time we are here, we have to establish the connection.
    if (it == 0) {
      Srv.HandleConnectionEstablished();
      Srv.WaitForCompletion(1);
    } else {
      Srv.WaitForCompletion(3);
    }

    expensiveFunc(opt.CompCost);
    WriteWR.post(Srv.clientId->qp);
    ZeroWR.post(Srv.clientId->qp);
  }

  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    RecvKey.exec();

    Srv.WaitForCompletion(3);

    // key can be used from this point forward safely

    // assume the function needs a subset A of a large set B to exec. if we were to
    // run the func locally on the client, we would need to transfer A first.
    expensiveFunc(opt.CompCost);

    Do[outputSize - 1] = it * 100;


    WriteWR.post(Srv.clientId->qp);
    ZeroWR.post(Srv.clientId->qp);

    perf.stop();
    std::cout << "key=" << *Key << "\n";
  }

  Srv.WaitForCompletion(2);

  delete Key;
  delete[] Do;
  Srv.HandleDisconnect();
}

void clntServerSends(const opts &opt) {
  std::cout << "Client - server sends\n";
  std::cout << "Will send Di buffer of size " << opt.DiSize << "\n";

  Server Srv;
  Srv.HandleConnectRequest();

  uint32_t *Key = new uint32_t();
  MemRegion KeyMR(Key, sizeof(uint32_t), Srv.protDomain);
  PostWrRecv RecvKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Srv.clientId->qp);

  uint32_t *Di = new uint32_t[opt.DiSize]();
  Di[opt.DiSize - 1] = 0x1234;
  MemRegion DiMR(Di, sizeof(uint32_t) * opt.DiSize, Srv.protDomain);
  PostWrSend SendDi((uint64_t) Di, sizeof(uint32_t) * opt.DiSize, DiMR.getRegion()->lkey,
                     Srv.clientId->qp);

  Perf perf(opt.Measure);

  // WARM UP
  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    RecvKey.exec();

    // The first time we are here, we have to establish the connection.
    // Also, we wait for the key to be received (we need it down below).
    // In all the other cases, we wait for 2 wr. That is, the Send request from
    // down below and the Recv req from the beginning of the loop (for the key).
    // This way we save ourselves from waiting for Do to be sent.
    if (it == 0) {
      Srv.HandleConnectionEstablished();
      Srv.WaitForCompletion(1);
    } else {
      Srv.WaitForCompletion(2);
    }

    SendDi.exec();
    std::cout << "Warm up " << it << "\n";
  }

  // REAL BENCHMARK
  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    RecvKey.exec();

    Srv.WaitForCompletion(2);

    // key can be used from this point forward safely

    Di[opt.DiSize - 1] = it * 100;
    SendDi.exec();
    perf.stop();
    std::cout << "key=" << *Key << "\n";
  }

  Srv.WaitForCompletion(1);

  delete Key;
  delete[] Di;
  Srv.HandleDisconnect();
}

void clntClientReads(const opts &opt) {
  std::cout << "Client - client reads\n";
  std::cout << "Will setup Di buffer of size " << opt.DiSize << "\n";

  Server Srv;
  Srv.HandleConnectRequest();

  uint32_t *Key = new uint32_t();
  MemRegion KeyMR(Key, sizeof(uint32_t), Srv.protDomain);
  PostWrRecv RecvKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Srv.clientId->qp);

  // setup Di buffer, send SI of it
  uint32_t *Di = new uint32_t[opt.DiSize]();
  Di[opt.DiSize - 1] = 0x1234;
  MemRegion DiMR(Di, sizeof(uint32_t) * opt.DiSize, Srv.protDomain);
  SendSI SendSI(Di, DiMR.getRegion(), Srv.protDomain);

  SendWR ZeroWR;
  ZeroWR.setOpcode(IBV_WR_SEND);

  SendSI.post(Srv.clientId->qp);
  Srv.HandleConnectionEstablished();
  Di[opt.DiSize - 1] = 0;
  Srv.WaitForCompletion(1);

  Perf perf(opt.Measure);

  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    RecvKey.exec();
    Srv.WaitForCompletion(1);
    ZeroWR.post(Srv.clientId->qp);
    Srv.WaitForCompletion(1);
  }

  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    RecvKey.exec(); // wait for the key to write our mem
    Srv.WaitForCompletion(1);

    Di[opt.DiSize - 1] = it * 100;

    ZeroWR.post(Srv.clientId->qp); // notify the client to read the mem
    Srv.WaitForCompletion(1);

    perf.stop();
  }

  delete[] Key;
  delete[] Di;
  Srv.HandleDisconnect();
}

void srvClientReads(const opts &opt) {
  unsigned int outputSize = getOutputSize(opt);

  std::cout << "Server - client reads\n";
  std::cout << "Will send Do buffer of size " << outputSize << "\n";
  std::cout << "The cost of comp will be " << opt.CompCost << "\n";

  Server Srv;
  Srv.HandleConnectRequest();

  uint32_t *Key = new uint32_t();
  MemRegion KeyMR(Key, sizeof(uint32_t), Srv.protDomain);
  PostWrRecv RecvKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                     Srv.clientId->qp);

  uint32_t *Do = new uint32_t[outputSize]();
  Do[outputSize - 1] = 0x1234;
  MemRegion DoMR(Do, sizeof(uint32_t) * outputSize, Srv.protDomain);
  SendSI SendSI(Do, DoMR.getRegion(), Srv.protDomain);

  SendWR ZeroWR;
  ZeroWR.setOpcode(IBV_WR_SEND);

  SendSI.post(Srv.clientId->qp);
  Srv.HandleConnectionEstablished();
  Do[outputSize - 1] = 0;
  Srv.WaitForCompletion(1);

  Perf perf(opt.Measure);

  for (unsigned it = 0; it < NUM_WARMUP; ++it) {
    RecvKey.exec();
    Srv.WaitForCompletion(1);
    expensiveFunc(opt.CompCost);
    ZeroWR.post(Srv.clientId->qp);
    Srv.WaitForCompletion(1);
  }

  for (unsigned it = 0; it < NUM_REP; ++it) {
    perf.start();
    RecvKey.exec(); // wait for the key to write our mem
    Srv.WaitForCompletion(1);

    expensiveFunc(opt.CompCost);
    Do[outputSize - 1] = it * 100;

    ZeroWR.post(Srv.clientId->qp); // notify the client to read the mem
    Srv.WaitForCompletion(1);

    perf.stop();
    std::cout << "key=" << *Key << "\n";
  }

  delete[] Key;
  delete[] Do;
  Srv.HandleDisconnect();
}

int main(int argc, char *  argv[]) {
  opts opt = parse_cl(argc, argv);

  if (opt.send && opt.ExecServer) {
    srvServerSends(opt);
  } else if (opt.write && opt.ExecServer) {
    srvServerWrites(opt);
  } else if (opt.Read && opt.ExecServer) {
    srvClientReads(opt);
  } else if (opt.Read && opt.ExecClient) {
    clntClientReads(opt);
  } else if (opt.send && opt.ExecClient) {
    clntServerSends(opt);
  } else {
    check(false, "Invalid combination of options");
  }

  return 0;
}
