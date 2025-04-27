#ifndef RDMA_QUEUE_PAIR_H
#define RDMA_QUEUE_PAIR_H

#include <ns3/object.h>
#include <ns3/packet.h>
#include <ns3/ipv4-address.h>
#include <ns3/data-rate.h>
#include <ns3/event-id.h>
#include <ns3/custom-header.h>
#include <ns3/int-header.h>
#include <vector>
#include <queue>
#include "ns3/settings.h"
#include "ns3/rdma-operation.h"

namespace ns3 {

class RdmaQueuePair : public Object {

public:
	bool is_sndQP;
	uint16_t m_ipid;
	uint32_t sip, dip;
	uint32_t m_qpid;
	uint64_t m_total_size;	// unit: bytes, sum size of all msgs in this QP
	std::map<uint32_t, Ptr<RdmaOperation> > m_finished_msgs;
	uint32_t GetHash(void);
	RdmaQueuePair();
	virtual ~RdmaQueuePair();			// to avoid memory leak
	virtual bool RemoveRdmaOperation(uint32_t) = 0;
	virtual Ptr<RdmaOperation> Peek() = 0;
	void InitalCC(DataRate bps, bool hp_multipleRate);

	// reactive cc: data rate; proactive cc: token rate
	DataRate m_rate;	//< Current rate (data sending rate for reactive CC; token sending rate for proactive CC)
	DataRate m_max_rate; // max rate

	// reactive cc: data win; proactive cc: token win
	uint32_t m_win; // bound of on-the-fly packets
	bool m_var_win; // variable window size
	void SetWin(uint32_t win);
	void SetVarWin(bool v);
	bool IsWinBound(uint64_t inflight);
	uint64_t GetWin(); // window size calculated from m_rate

	/**
	 * Proactive CC
	 */
	Time m_nextTknAvail;	//< Soonest time of next token send
	uint32_t m_tkn_nxt;		// unit: MTU
	uint32_t m_tkn_ack;		// unit: MTU, psns before(not include) have received
	uint64_t m_tkn_max;		// unit: bytes
	uint32_t m_sch_start;	// the min_seq of sch packets
	uint32_t m_lst_resumed_unsch_tkn;
	std::queue<Ptr<Credit> > m_sent_credits;		// on the side which sends credits

	struct {
		double m_weight;
		uint32_t m_rttDroppedCredit;
		uint32_t m_rttSentCredit;
		double m_rttPreIncreasing;
		double m_rtt;
		Time m_lastUpdate;
		double m_curRate;
		double m_max_rate;		// credits per seconds
	} ep;

	/**
	 * Re-active CC
	 */
	struct {
		uint64_t m_endDelay;		// EWMA
		uint64_t m_rtt;				// EWMA
		Time m_lastDecreaseT;
		uint32_t m_retransmitCnt;
		double m_endWin;			// unit: packet
		double m_fabricWin;			// unit: packet
		uint64_t m_pacingDelay;
	} swift;
	struct {
		DataRate m_targetRate;	//< Target rate
		EventId m_eventUpdateAlpha;
		double m_alpha;
		bool m_alpha_cnp_arrived; // indicate if CNP arrived in the last slot
		bool m_first_cnp; // indicate if the current CNP is the first CNP
		EventId m_eventDecreaseRate;
		bool m_decrease_cnp_arrived; // indicate if CNP arrived in the last slot
		uint32_t m_rpTimeStage;
		EventId m_rpTimer;
	} mlx;
	struct {
		uint32_t m_lastUpdateSeq;
		DataRate m_curRate;
		IntHop hop[IntHeader::maxHop];
		uint32_t keep[IntHeader::maxHop];
		uint32_t m_incStage;
		double m_lastGap;
		double u;
		struct {
			double u;
			DataRate Rc;
			uint32_t incStage;
		}hopState[IntHeader::maxHop];
	} hp;
	struct{
		uint32_t m_lastUpdateSeq;
		DataRate m_curRate;
		uint32_t m_incStage;
		uint64_t lastRtt;
		double rttDiff;
	} tmly;
	struct{
		uint32_t m_lastUpdateSeq;
		uint32_t m_caState;
		uint32_t m_highSeq; // when to exit cwr
		double m_alpha;
		uint32_t m_ecnCnt;
		uint32_t m_batchSizeOfAlpha;
	} dctcp;

	// for printing rate
	uint64_t m_last_print_rate;
	void PrintRate();
	// for printing window
	uint64_t m_last_print_win;
	void PrintWin();

	//liuchang
	uint32_t m_queue_id;
	void SetQueueId(uint32_t id);
	uint32_t GetQueueId();
};

/*---------------------------------------- Sender -----------------------------------------------*/

/**
 * RdmaSndQueuePair class:
 * A group of messages on sender host
 */
class RdmaSndQueuePair : public RdmaQueuePair {
public:
	std::vector<Ptr<RdmaSndOperation> >  rdma_msgs_sending;	// msg which still has packets to send
	std::vector<Ptr<RdmaSndOperation> >  rdma_msgs_waiting;	// msgs which has sent all packets, but hasn't received last ack

	uint16_t m_pg;
	uint32_t m_dst;

	uint32_t m_rrlastMsg; 		// for RR scheduler of sending data packet

	Time startTime;
	uint32_t m_nxt_psn;		// incremental, carried by QPTag on data packet
	uint32_t m_ack_psn;

	uint64_t m_baseRtt; // base RTT of this qp
	Time m_nextAvail;	//< Soonest time of next send
	uint32_t wp; // current window of packets
	uint32_t lastPktSize;

	/******************************
	 * runtime states
	 *****************************/
	/*
	 * Pro-active CC
	 */
	enum {
		IS_SCH = 0x01
	};
	std::queue<Ptr<Credit> > m_credits;	// received but unused credits
	/*
	 * about qp's status:
	 * unsch/sch:
	 * unsch ---(has sent one BDP successive unsch packet or has credits to use)--> sch
	 * sch ---(no unfinished flows and all credits are out-of-date)--> unsch
	 */
	uint8_t m_status;			// 0-3: is_sch, 4-7: undefined
	uint32_t m_unschCount;		// successive unsch packet counter

	/***********
	 * methods
	 **********/
	static TypeId GetTypeId (void);
	RdmaSndQueuePair(uint16_t pg, Ipv4Address _sip, Ipv4Address _dip, uint16_t _sport, uint16_t _dport);
	virtual ~RdmaSndQueuePair();
	void SetBaseRtt(uint64_t baseRtt);
	void SetQPId(uint32_t);
	void SetDst(uint32_t);
	Time GetNextAvailT();

	/*
	 * wqy, on Nov 2, 2020
	 * For timeout retransmission
	 */
	EventId m_eventTimeoutRetransmission;
	typedef Callback<void, Ptr<RdmaQueuePair> > RTOCallback;
	RTOCallback m_RTOCallback;
	void SetRTOCallback(RTOCallback rtoCallback);
	// reset RTO depending on parameter
	void ResetRTOEvent(Time earliest);
	// once a new action(receive ACK/send packet) happens, update this msg's RTO startTime - lastActionTime
	void ResetMSGRTOTime(uint32_t msgSeq);
	// when trigger TRO, recover msg's status and move timeout msg back to sending queue
	void RecoverMSG();

	/*
	 * wqy, on Oct 20, 2020
	 * For QP scheduler
	 */
	void AddRdmaOperation(Ptr<RdmaSndOperation>);
	bool RemoveRdmaOperation(uint32_t);
	void ContinueTestFlow();
	uint32_t GetMsgNumber();
	Ptr<RdmaSndOperation> GetMsg(uint32_t msgSeq, bool& is_sending);
	Ptr<RdmaOperation> Peek();	// FIFO
	void SetRRLast(Ptr<RdmaSndOperation>);
	void MoveRdmaOperationToWaiting(uint32_t msg_seq);	// when a msg sent all packets, move into unfinished queue
	void MoveRdmaOperationToSending(uint32_t msg_seq);	// when receive a NACK of a msg in unfinished queue, move it back

	uint64_t GetNxtPSN();
	uint64_t GetBytesLeft();
private:
	Ptr<RdmaSndOperation> PeekFIFOMsg();	// FIFO
	Ptr<RdmaSndOperation> PeekRRMsg(); // RR
};

/*--------------------------------------- Receiver -------------------------------------------*/
/**
 * RdmaRxQueuePair class:
 * A group of messages on receiver host
 */
class RdmaRxQueuePair : public RdmaQueuePair { // Rx side queue pair
public:
	std::vector<Ptr<RdmaRxOperation> > rdma_rx_msgs;
	struct ECNAccount{
		uint16_t qIndex;
		uint8_t ecnbits;
		uint16_t qfb;
		uint16_t total;

		ECNAccount() { memset(this, 0, sizeof(ECNAccount));}
	};
	ECNAccount m_ecn_source;
	EventId QcnTimerEvent; // if destroy this rxQp, remember to cancel this timer

	RdmaRxQueuePair();
	virtual ~RdmaRxQueuePair();
	void AddRdmaOperation(Ptr<RdmaRxOperation>);
	bool RemoveRdmaOperation(uint32_t);

	Ptr<RdmaRxOperation> GetRxMSG(uint32_t msg_seq);
	Ptr<RdmaOperation> Peek();
	uint32_t GetUnfinishedNum();
};

class RdmaQueuePairGroup : public Object {
public:
	std::vector<Ptr<RdmaQueuePair> > m_qps;

	static TypeId GetTypeId (void);
	RdmaQueuePairGroup(void);
	uint32_t GetN(void);
	Ptr<RdmaQueuePair> Get(uint32_t idx);
	Ptr<RdmaQueuePair> operator[](uint32_t idx);
	void AddQp(Ptr<RdmaQueuePair> qp);
	void Clear(void);
};

}

#endif /* RDMA_QUEUE_PAIR_H */
