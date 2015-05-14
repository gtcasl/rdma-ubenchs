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

class PostWrSend {

  uint64_t dataAddr;
  uint32_t dataLen;
  uint32_t localKey; // key of local memory region
  ibv_qp *queuePair;

public:
  PostWrSend(uint64_t addr, uint32_t len, uint32_t lkey, ibv_qp *qp)
    : dataAddr(addr), dataLen(len), localKey(lkey), queuePair(qp) {

  }

  void Execute() {
    ibv_sge sge = {};
    sge.addr = dataAddr;
    sge.length = dataLen;
    sge.lkey = localKey;

    ibv_send_wr sendWr = {};
    sendWr.sg_list = &sge;
    sendWr.num_sge = 1;
    sendWr.opcode = IBV_WR_SEND;
    sendWr.send_flags = IBV_SEND_SIGNALED;
    sendWr.next = NULL;

    test_nz(ibv_post_send(queuePair, &sendWr, NULL));
  }
};

class PostWrRecv {

  uint64_t dataAddr;
  uint32_t dataLen;
  uint32_t localKey; // key of local memory region
  ibv_qp *queuePair;

public:
  PostWrRecv(uint64_t addr, uint32_t len, uint32_t lkey, ibv_qp *qp)
    : dataAddr(addr), dataLen(len), localKey(lkey), queuePair(qp) {

  }

  void Execute() {
    ibv_sge sge = {};
    sge.addr = dataAddr;
    sge.length = dataLen;
    sge.lkey = localKey;

    ibv_recv_wr recvWr = {};
    recvWr.sg_list = &sge;
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
