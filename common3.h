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

inline void check_z(int t) {
  if (t != 0)
    throw std::runtime_error("check_z");
}

inline void check_nn(void *t) {
  if (t == NULL)
    throw std::runtime_error("check_nn");
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

    check_z(ibv_post_send(queuePair, &sendWr, NULL));
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

    check_z(ibv_post_send(queuePair, &sendWr, NULL));
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

    check_z(ibv_post_recv(queuePair, &recvWr, NULL));
  }
};

struct RemoteRegInfo {
  uint64_t addr;
  uint32_t rKey;
};

class SendRRI {
  ibv_mr *mr;
  RemoteRegInfo *info;
  ibv_qp *qp;

public:
  SendRRI(void *buf, ibv_mr *bufMemReg, ibv_pd *protDomain, ibv_qp *qp) : mr(NULL), info(NULL), qp(qp) {
    assert(buf != NULL);
    assert(bufMemReg != NULL);
    assert(protDomain != NULL);
    assert(qp != NULL);

    info = new RemoteRegInfo();

    info->addr = (uint64_t) buf;
    info->rKey = bufMemReg->rkey;

    check_nn(mr = ibv_reg_mr(protDomain, (void *) info, sizeof(RemoteRegInfo),
                            IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));
  }

  ~SendRRI() {
    delete info;
  }

  void Execute() {
    PostWrSend send((uint64_t) info, sizeof(RemoteRegInfo), mr->lkey, qp);
    send.Execute();

    D(std::cerr << "Sent addr=" << std::hex << info->addr << "\n");
    D(std::cerr << "Sent rkey=" << std::dec << info->rKey << "\n");
  }
};

#endif
