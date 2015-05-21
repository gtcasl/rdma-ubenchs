#ifndef COMMON_H
#define COMMON_H

#include <rdma/rdma_cma.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <sstream>
#include <string>

#ifndef REL
#define D(x) x
#else
#define D(x)
#endif

typedef std::chrono::high_resolution_clock Time;
typedef std::chrono::microseconds microsec;
typedef std::chrono::duration<float> dsec;

inline void check(bool b, const std::string &msg) {
  if (!b)
    throw std::runtime_error(msg);
}

inline void check_z(int t) {
  check((t == 0), "check_z");
}

inline void check_nn(void *t) {
  check((t != NULL), "check_nn");
}

inline Time::time_point timer_start() {
  return Time::now();
}

inline void timer_end(const Time::time_point &t0) {
  Time::time_point t1 = Time::now();
  dsec duration = t1 - t0;
  microsec res = std::chrono::duration_cast<microsec>(duration);
  std::cout << "elapsed time: " << res.count() << " us\n";
}

class RDMAPeer {
protected:
  rdma_event_channel *eventChannel;
  int port;
  ibv_pd *protDomain;
  ibv_cq *compQueue;
  rdma_cm_event *event;

  rdma_conn_param connParams;
  sockaddr_in sin;
  ibv_qp_init_attr qpAttr;

public:
  RDMAPeer() : eventChannel(NULL), port(21234), protDomain(NULL), compQueue(NULL),
               event(NULL) {
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
  }

  void WaitForCompletion() {
    assert(compQueue != NULL);
    int ret = 0;

    ibv_wc workComp = {};

    while ((ret = ibv_poll_cq(compQueue, 1, &workComp)) == 0) {}

    check(ret >= 0, "ibv_poll_cq didn't return 0");

    if (workComp.status != IBV_WC_SUCCESS) {
      std::ostringstream sstm;
      sstm << "status was not IBV_WC_SUCCESS, it was " << workComp.status;
      check(false, sstm.str());
    }
  }
};

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

  void Execute(bool read = false) {
    ibv_send_wr sendWr = {};
    sendWr.sg_list = &(sge->sge);
    sendWr.num_sge = 1;

    if (read)
      sendWr.opcode = IBV_WR_RDMA_READ;
    else
      sendWr.opcode = IBV_WR_RDMA_WRITE;

    sendWr.send_flags = IBV_SEND_SIGNALED;
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
    ibv_dereg_mr(mr);
  }

  void Execute() {
    PostWrSend send((uint64_t) info, sizeof(RemoteRegInfo), mr->lkey, qp);
    send.Execute();

    D(std::cerr << "Sent addr=" << std::hex << info->addr << "\n");
    D(std::cerr << "Sent rkey=" << std::dec << info->rKey << "\n");
  }
};

struct TestData {
  uint64_t key;
};

class SendTD {
public:
  ibv_mr *mr;
  TestData *data;
  ibv_qp *qp;
  ibv_pd *protDomain;
  unsigned numEntries;

public:
  SendTD(ibv_pd *protDomain, ibv_qp *qp, unsigned numEntries)
    : mr(NULL), data(NULL), qp(qp), protDomain(protDomain), numEntries(numEntries) {
    assert(protDomain != NULL);
    assert(qp != NULL);

    data = new TestData[numEntries];

    for (unsigned i = 0, e = numEntries / 2; i < e; ++i) {
      data[i].key = 1;
    }

    for (unsigned i = numEntries / 2; i < numEntries; ++i) {
      data[i].key = 2;
    }

    //for (unsigned i = 0; i < numEntries; ++i) {
    //  D(std::cout << "entry " << i << " key " << data[i].key << "\n");
    //}

    check_nn(mr = ibv_reg_mr(protDomain, (void *) data, sizeof(TestData) * numEntries,
                            IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ));
  }

  ~SendTD() {
    delete[] data;
    ibv_dereg_mr(mr);
  }

  virtual void Execute() {
    PostWrSend send((uint64_t) data, sizeof(TestData) * numEntries, mr->lkey, qp);
    send.Execute();
  }
};

class SendTDRdma : public SendTD {
public:
  SendTDRdma(ibv_pd *protDomain, ibv_qp *qp, unsigned numEntries)
    : SendTD(protDomain, qp, numEntries) {

  }

  void Execute() override {
    SendRRI sendRRI(data, mr, protDomain, qp);
    sendRRI.Execute();
  }
};

struct opts {
  bool read;
  bool write;
  uint32_t entries;
};

opts parse_cl(int argc, char *argv[]) {
  opts opt = {};

  while (1) {
    int c = getopt(argc, argv, "rwe:");

    if (c == -1) {
      break;
    }

    switch(c) {
    case 'r':
      opt.read = true; // client reads
      break;
    case 'w':
      opt.write = true; // server writes
      break;
    case 'e':
    {
      std::string str(optarg);
      std::stringstream sstm(str);
      sstm >> opt.entries;
      break;
    }
    default:
      std::cerr << "Invalid option" << "\n";
      check_z(1);
    }
  }

  if (opt.read || opt.write) {
    check((opt.entries != 0), "must provide number of entries");
  }

  D(std::cout << "number of entries=" << opt.entries << "\n");

  return opt;
}

#endif
