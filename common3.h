#ifndef COMMON_H
#define COMMON_H

#include <rdma/rdma_cma.h>
#include <stdexcept>
#include <iostream>

#ifndef REL
#define D(x) x
#else
#define D(x)
#endif

inline void test_nz(int t) {
  if (t != 0)
    throw std::runtime_error("test_nz");
}

struct Sge {
  ibv_sge sge;

  Sge(uint64_t addr, uint32_t length, uint32_t lkey) {
    sge = {};
    sge.addr = addr;
    sge.length = length;
    sge.lkey = lkey;
  }
};

class PostRDMAWrSend {
  ibv_qp *queuePair;
  Sge *sge;
  uint64_t rAddr;
  uint32_t rKey;

public:
  PostRDMAWrSend(uint64_t addr, uint32_t len, uint32_t lkey, ibv_qp *qp,
                 uint64_t rAddr, uint32_t rKey)
    : queuePair(qp), rAddr(rAddr), rKey(rKey) {
    sge = new Sge(addr, len, lkey);
  }

  ~PostRDMAWrSend() {
    delete sge;
  }

  void Execute() {
    ibv_send_wr sendWr = {};
    sendWr.sg_list = &(sge->sge);
    sendWr.num_sge = 1;
    sendWr.opcode = IBV_WR_RDMA_WRITE;
    sendWr.next = NULL;
    sendWr.wr.rdma.remote_addr = rAddr;
    sendWr.wr.rdma.rkey = rKey;

    test_nz(ibv_post_send(queuePair, &sendWr, NULL));
  }
};

class PostWrSend {
  ibv_qp *queuePair;
  Sge *sge;

public:
  PostWrSend(uint64_t addr, uint32_t len, uint32_t lkey, ibv_qp *qp)
    : queuePair(qp) {
    sge = new Sge(addr, len, lkey);
  }

  ~PostWrSend() {
    delete sge;
  }

  void Execute() {
    ibv_send_wr sendWr = {};
    sendWr.sg_list = &(sge->sge);
    sendWr.num_sge = 1;
    sendWr.opcode = IBV_WR_SEND;
    sendWr.send_flags = IBV_SEND_SIGNALED;
    sendWr.next = NULL;

    test_nz(ibv_post_send(queuePair, &sendWr, NULL));
  }
};

class PostWrRecv {
  ibv_qp *queuePair;
  Sge *sge;

public:
  PostWrRecv(uint64_t addr, uint32_t len, uint32_t lkey, ibv_qp *qp)
    : queuePair(qp) {
    sge = new Sge(addr, len, lkey);
  }

  ~PostWrRecv() {
    delete sge;
  }

  void Execute() {
    ibv_recv_wr recvWr = {};
    recvWr.sg_list = &(sge->sge);
    recvWr.num_sge = 1;
    recvWr.next = NULL;

    test_nz(ibv_post_recv(queuePair, &recvWr, NULL));
  }
};

struct RemoteRegInfo {
  uint64_t addr;
  uint32_t rKey;
};

#endif
