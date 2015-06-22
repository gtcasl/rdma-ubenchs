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
protected:
  rdma_cm_id *serverId;
  rdma_cm_id *clientId;
  ibv_mr *memReg;

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

public:
  Server() : serverId(NULL), clientId(NULL), memReg(NULL) {

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

  virtual void start(uint32_t entries) {
    assert(eventChannel != NULL);
    assert(serverId != NULL);

    HandleConnectRequest();
    HandleConnectionEstablished();

    SendTD send(protDomain, clientId->qp, entries);

    auto t0 = timer_start();
    send.exec();

    WaitForCompletion();

    timer_end(t0);
    HandleDisconnect();
  }
};

class ServerSWrites : Server {
public:

  ServerSWrites() {
  }

  ~ServerSWrites() {
  }

  void start(const opts &opt) {
    assert(eventChannel != NULL);
    assert(serverId != NULL);

    HandleConnectRequest();

    uint32_t *Key = new uint32_t();
    MemRegion KeyMR(Key, sizeof(uint32_t), protDomain);
    PostWrRecv RecvKey((uint64_t) Key, sizeof(uint32_t), KeyMR.getRegion()->lkey,
                       clientId->qp);

    uint32_t *Do = new uint32_t[32]();
    Do[7] = 0x1234;
    MemRegion DoMR(Do, sizeof(uint32_t) * 32, protDomain);
    PostWrSend SendDo((uint64_t) Do, sizeof(uint32_t) * 32, DoMR.getRegion()->lkey,
                       clientId->qp);

    for (unsigned it = 0; it < 50; ++it) {
      auto t0 = timer_start();
      RecvKey.exec();

      // The first time we are here, we have to establish the connection.
      // Also, we wait for the key to be received (we need it down below).
      // In all the other cases, we wait for 2 wr. That is, the Send request from
      // down below and the Recv req from the beginning of the loop (for the key).
      // This way we save ourselves from waiting for Do to be sent.
      if (it == 0) {
        HandleConnectionEstablished();
        WaitForCompletion(1);
      } else {
        WaitForCompletion(2);
      }

      // key can be used from this point forward safely

      Do[7] = it * 100;
      SendDo.exec();
      timer_end(t0);

      std::cout << "key=" << *Key << "\n";
    }

    WaitForCompletion(1);

    delete Key;
    delete[] Do;
    HandleDisconnect();
  }
};

int main(int argc, char *argv[]) {
  opts opt = parse_cl(argc, argv);

  if (opt.send) {
    ServerSWrites server;
    server.start(opt);
  } else if (opt.write) {
    ServerSWrites server;
    server.start(opt);
  }

  return 0;
}
