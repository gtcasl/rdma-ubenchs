#ifndef COMMON_H
#define COMMON_H

#include <rdma/rdma_cma.h>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#ifndef REL
#define D(x) x
#else
#define D(x)
#endif

typedef std::chrono::high_resolution_clock Time;
typedef std::chrono::microseconds microsec;
typedef std::chrono::duration<float> dsec;

const unsigned NUM_REP = 500;

struct TestData {
  uint64_t key;
};

struct Sge {
  ibv_sge sge;

  Sge(uint64_t Addr, uint32_t length, uint32_t lkey) {
    sge = {};
    sge.addr = Addr;
    sge.length = length;
    sge.lkey = lkey;
  }

  Sge() {
    sge = {};
  }
};

struct SetupInfo {
  uint64_t Addr;
  uint32_t RemoteKey;
};

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


inline std::vector<TestData> filterData(uint64_t key, TestData *buf, uint32_t entries) {
  std::vector<TestData> result;

  for (unsigned i = 0; i < entries; ++i) {
    if (key == buf[i].key) {
      result.push_back(buf[i]);
    }
  }

  return result;
}

inline TestData *vecToArray(const std::vector<TestData> &Vec) {
  TestData *arr = new TestData[Vec.size()];
  std::copy(Vec.begin(), Vec.end(), arr);
  return arr;
}

inline void initData(TestData *Data, uint32_t NumEntries, uint32_t NumOnes) {
  // place ones at the end
  for (unsigned i = 0; i < NumEntries; ++i) {
    if (i >= NumEntries - NumOnes) {
      Data[i].key = 1;
    } else {
      Data[i].key = 2;
    }
  }
}

class RDMAPeer {
public:
  rdma_event_channel *eventChannel;
  int port;
  ibv_pd *protDomain;
  ibv_cq *compQueue;
  rdma_cm_event *event;

  rdma_conn_param connParams;
  sockaddr_in sin;
  ibv_qp_init_attr qpAttr;

  RDMAPeer() : eventChannel(NULL), port(21234), protDomain(NULL), compQueue(NULL),
               event(NULL) {
    connParams = {};
    connParams.initiator_depth = 3;
    connParams.responder_resources = 3;
    connParams.rnr_retry_count = 3;
    connParams.retry_count = 3;
    qpAttr = {};
    qpAttr.cap.max_send_wr = 32;
    qpAttr.cap.max_recv_wr = 32;
    qpAttr.cap.max_send_sge = 3;
    qpAttr.cap.max_recv_sge = 3;
    qpAttr.cap.max_inline_data = 64;
    qpAttr.qp_type = IBV_QPT_RC;
  }

  void checkPollResult(int RetVal, ibv_wc const &workComp) {
    check(RetVal > 0, "ibv_poll_cq returned < 0");

    if (workComp.status != IBV_WC_SUCCESS) {
      std::ostringstream sstm;
      sstm << "ibv_poll_cq status was not IBV_WC_SUCCESS, it was " << workComp.status;
      check(false, sstm.str());
    }
  }

  void WaitForCompletion() {
    assert(compQueue != NULL);
    int ret = 0;
    ibv_wc workComp = {};

    // wait for the first wc
    while ((ret = ibv_poll_cq(compQueue, 1, &workComp)) == 0) {}

    // this is the first wc that arrived
    checkPollResult(ret, workComp);

    // if there are more, keep consuming them
    while ((ret = ibv_poll_cq(compQueue, 1, &workComp)) != 0) {
      checkPollResult(ret, workComp);
    }
  }

  void checkPollResult(int RetVal, ibv_wc *WorkComps) {
    check(RetVal > 0, "ibv_poll_cq returned < 0");

    for (unsigned i = 0; i < (unsigned) RetVal; ++i) {
      if (WorkComps[i].status != IBV_WC_SUCCESS) {
        std::ostringstream sstm;
        sstm << "ibv_poll_cq status was not IBV_WC_SUCCESS, it was " << WorkComps[i].status;
        check(false, sstm.str());
      }
    }
  }

  void WaitForCompletion(uint32_t NumReqs) {
    assert(compQueue != NULL);
    int RetVal = 0;
    ibv_wc *WorkComps = new ibv_wc[NumReqs]();

    while (NumReqs > 0) {
      while ((RetVal = ibv_poll_cq(compQueue, NumReqs, WorkComps)) == 0) {}

      checkPollResult(RetVal, WorkComps);

      NumReqs -= RetVal;
    }

    delete[] WorkComps;
  }
};

class MemRegion {
protected:
  ibv_mr *MR;

public:
  MemRegion(void *Buffer, size_t Size, ibv_pd *ProtDom) {
    assert(Buffer != NULL);
    assert(ProtDom != NULL);

    check_nn(MR = ibv_reg_mr(ProtDom, Buffer, Size,
                            IBV_ACCESS_REMOTE_WRITE |
                            IBV_ACCESS_LOCAL_WRITE |
                            IBV_ACCESS_REMOTE_READ));
  }

  ~MemRegion() {
    ibv_dereg_mr(MR);
  }

  ibv_mr *getRegion() {
    return MR;
  }
};

class SendWR {
  Sge SGE;

public:
  ibv_send_wr WR;

  SendWR(Sge &SGE)
    : SGE(SGE) {
    WR = {};
    WR.sg_list = &(SGE.sge);
    WR.num_sge = 1;
    WR.send_flags = IBV_SEND_SIGNALED;
    WR.next = NULL;
  }

  // zero byte
  SendWR() {
    WR = {};
    WR.sg_list = NULL;
    WR.num_sge = 0;
    WR.send_flags = IBV_SEND_SIGNALED;
    WR.next = NULL;
  }

  ~SendWR() {}

  void setOpcode(enum ibv_wr_opcode opcode) {
    WR.opcode = opcode;
  }

  void setRdma(uint64_t Raddr, uint32_t Rkey) {
    WR.wr.rdma.remote_addr = Raddr;
    WR.wr.rdma.rkey = Rkey;
  }

  void setImm() {
    WR.imm_data = htonl(0x1234);
  }

  void post(ibv_qp *QP) {
    check_z(ibv_post_send(QP, &WR, NULL));
  }
};

class PostWrSend {
  ibv_qp *queuePair;
  Sge *sge;

public:
  PostWrSend(uint64_t Addr, uint32_t len, uint32_t lkey, ibv_qp *qp)
    : queuePair(qp) {
    sge = new Sge(Addr, len, lkey);
  }

  ~PostWrSend() {
    delete sge;
  }

  void exec() {
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
  PostWrRecv(uint64_t Addr, uint32_t len, uint32_t lkey, ibv_qp *qp)
    : queuePair(qp) {
    sge = new Sge(Addr, len, lkey);
  }

  ~PostWrRecv() {
    delete sge;
  }

  void exec() {
    ibv_recv_wr recvWr = {};
    recvWr.sg_list = &(sge->sge);
    recvWr.num_sge = 1;
    recvWr.next = NULL;

    check_z(ibv_post_recv(queuePair, &recvWr, NULL));
  }
};

class SendSI {
  MemRegion *MR;
  SetupInfo *Info;

public:
  SendSI(void *buf, ibv_mr *bufMemReg, ibv_pd *protDomain) : Info(NULL) {
    assert(buf != NULL);
    assert(bufMemReg != NULL);
    assert(protDomain != NULL);

    Info = new SetupInfo();

    Info->Addr = (uint64_t) buf;
    Info->RemoteKey = bufMemReg->rkey;

    MR = new MemRegion(Info, sizeof(SetupInfo), protDomain);
  }

  ~SendSI() {
    delete Info;
    delete MR;
  }

  void post(ibv_qp *QP) {
    PostWrSend send((uint64_t) Info, sizeof(SetupInfo), MR->getRegion()->lkey, QP);
    send.exec();

    D(std::cerr << "Sent addr=" << std::hex << Info->Addr << "\n");
    D(std::cerr << "Sent remote key=" << std::dec << Info->RemoteKey << "\n");
  }
};

class RecvSI {
  MemRegion *MR;

public:
  SetupInfo *Info;

  RecvSI(ibv_pd *protDom) {
    assert(protDom != NULL);

    Info = new SetupInfo();
    MR = new MemRegion(Info, sizeof(SetupInfo), protDom);
  }

  ~RecvSI() {
    delete Info;
    delete MR;
  }

  void post(ibv_qp *QP) {
    assert(QP != NULL);

    PostWrRecv recv((uint64_t) Info, sizeof(SetupInfo), MR->getRegion()->lkey, QP);
    recv.exec();
  }

  void print() {
    D(std::cout << "Client addr=" << std::hex << Info->Addr << std::dec << "\n");
    D(std::cout << "Client remote key=" << Info->RemoteKey << "\n");
  }
};

bool isPrime(uint32_t Num) {
  // inefficient on purpose
  for (unsigned i = 2; i < Num; ++i) {
    if (Num % i == 0) {
      return false;
    }
  }

  return true;
}

void computePrime(uint32_t NumPrime) {
  for (unsigned i = 2, Found = 0; Found < NumPrime; ++i) {
    if (isPrime(i)) {
      Found++;
    }
  }
}

void expensiveFunc() {
  computePrime(500); // needs aprox 2885 us to run
}

struct opts {
  bool send;
  bool write;
  bool Read;
  uint32_t KeysForFunc;
  uint32_t OutputEntries;
};

void printTestData(TestData *Data, uint32_t NumEntries) {
  for (unsigned i = 0; i < NumEntries; ++i) {
    D(std::cout << "entry " << i << " key " << Data[i].key << "\n");
  }
}

opts parse_cl(int argc, char *argv[]) {
  opts opt = {};

  while (1) {
    int c = getopt(argc, argv, "swrn:o:");

    if (c == -1) {
      break;
    }

    switch(c) {
    case 's':
      opt.send = true; // server uses send for Do
      break;
    case 'w':
      opt.write = true; // server writes for Do
      break;
    case 'r':
      opt.Read = true; // client reads Di
      break;
    case 'n':
    {
      std::string str(optarg);
      std::stringstream sstm(str);
      sstm >> opt.KeysForFunc;
      break;
    }
    case 'o':
    {
      std::string str(optarg);
      std::stringstream sstm(str);
      sstm >> opt.OutputEntries;
      break;
    }
    default:
      std::cerr << "Invalid option" << "\n";
      check_z(1);
    }
  }

  check((opt.KeysForFunc != 0), "must provide number of KeysForFunc");
  check((opt.OutputEntries != 0), "matching keys cannot be 0");

  D(std::cout << "number of KeysForFunc=" << opt.KeysForFunc << "\n");
  D(std::cout << "number of output KeysForFunc=" << opt.OutputEntries << "\n");

  return opt;
}

#endif
