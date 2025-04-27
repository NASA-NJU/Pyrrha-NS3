#ifndef RDMA_HW_H
#define RDMA_HW_H

#include <ns3/rdma.h>
#include <ns3/rdma-queue-pair.h>
#include <ns3/node.h>
#include <ns3/custom-header.h>
#include "qbb-net-device.h"
#include <unordered_map>

#include "ns3/settings.h"
#include "ns3/broadcom-egress-queue.h"

const uint32_t BW_INTERVAL = 5;//unit: us

namespace ns3 {

struct RdmaInterfaceMgr{
	Ptr<QbbNetDevice> dev;
	Ptr<RdmaQueuePairGroup> qpGrp;

	RdmaInterfaceMgr() : dev(NULL), qpGrp(NULL) {}
	RdmaInterfaceMgr(Ptr<QbbNetDevice> _dev){
		dev = _dev;
	}
};

class RdmaHw : public Object {
public:

	static TypeId GetTypeId (void);
	RdmaHw();
	~RdmaHw();

	Ptr<Node> m_node;
	uint32_t m_mtu;
	uint32_t m_cc_mode;
	uint32_t m_mibo_cc_mode;
	double m_nack_interval;
	uint32_t m_chunk;
	uint32_t m_ack_interval;
	bool m_backto0;
	bool m_var_win, m_fast_react;
	uint32_t m_win_initial;
	uint64_t m_baseRtt_initial;
	bool m_rateBound;
	std::vector<RdmaInterfaceMgr> m_nic; // list of running nic controlled by this RdmaHw
	std::unordered_map<uint64_t, Ptr<RdmaSndQueuePair> > m_qpMap; // mapping from uint64_t to qp
	std::unordered_map<uint64_t, Ptr<RdmaRxQueuePair> > m_rxQpMap; // mapping from uint64_t to rx qp
	std::unordered_map<uint32_t, std::vector<int> > m_rtTable; // map from ip address (u32) to possible ECMP port (index of dev)

	RoutingTag CalcuAllRoutingHops(uint32_t curr_node, uint32_t curr_idx, uint32_t sip, uint32_t dip, uint32_t sport, uint32_t dport, uint32_t qp_id);

	typedef Callback<void, Ptr<RdmaRxOperation> > RcvFinishCallback;
	typedef Callback<void, Ptr<RdmaSndOperation> > MsgCompleteCallback;
	RcvFinishCallback m_rcvFinishCallback;
	MsgCompleteCallback m_msgCompleteCallback;

	// for rtt & window configure
	Callback<uint32_t, uint32_t> m_searchWinCallback;
	Callback<uint64_t, uint32_t> m_searchBaseRTTCallback;

	//for bw analysis
	void BW();
	uint64_t bw;
	uint64_t tp;
	uint64_t ctrl;
	EventId bwId;
	//for queueing time analysis
	uint64_t totalQueuingPackets;
	double totalQueuingTimeUs;

	/*
	 * wqy, on Oct 10, 2020
	 */
	void ResetQP(Ptr<RdmaSndQueuePair> qp);

	void SetNode(Ptr<Node> node);
	void SetQuota();
	void Setup(RcvFinishCallback rcvcb, MsgCompleteCallback mcb); // setup shared data and callbacks with the QbbNetDevice
	static uint64_t GetQpKey(uint16_t sport, uint16_t pg, uint32_t qpid); // get the lookup key for m_qpMap
	void AddQueuePair(uint64_t size, uint16_t pg, Ipv4Address _sip, Ipv4Address _dip, uint16_t _sport, uint16_t _dport, uint32_t win, uint64_t baseRtt, uint32_t qpid, uint32_t msgSeq, uint32_t src, uint32_t dst, uint32_t flow_id, bool isTestFlow); // add a new qp (new send)
	void StopMessage(uint32_t qpid, uint32_t msgseq);

	uint32_t GetNicIdxOfQp(Ptr<RdmaQueuePair> qp); // get the NIC index of the qp
	uint32_t GetNicIdxOfSndQp(Ptr<RdmaSndQueuePair> qp); // get the NIC index of the qp
	uint32_t GetNicIdxOfRxQp(Ptr<RdmaRxQueuePair> q); // get the NIC index of the rxQp
	Ptr<RdmaSndQueuePair> GetSndQp(uint16_t sport, uint16_t pg, uint32_t qpid); // get the qp
	Ptr<RdmaRxQueuePair> GetRxQp(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport, uint16_t pg, uint32_t flow_id, uint32_t qpid, bool create); // get a rxQp

	void SendCnp(Ptr<RdmaQueuePair> qp, CustomHeader &ch, uint8_t ecnbits);
	int ReceiveUdp(Ptr<Packet> p, CustomHeader &ch);
	int ReceiveCnp(Ptr<Packet> p, CustomHeader &ch);
	int ReceiveAck(Ptr<Packet> p, CustomHeader &ch); // handle both ACK and NACK
	int Receive(Ptr<Packet> p, CustomHeader &ch); // callback function that the QbbNetDevice should use when receive packets. Only NIC can call this function. And do not call this upon PFC
	void OnRTO(Ptr<RdmaQueuePair> qp);
	void OnSACK(Ptr<RdmaQueuePair> qp);

	void CheckandSendQCN(Ptr<RdmaRxQueuePair> q);
	int ReceiverCheckSeq(uint32_t seq, Ptr<RdmaRxOperation> q, uint32_t size, uint32_t& NackSeq);
	void AddHeader (Ptr<Packet> p, uint16_t protocolNumber);
	static uint16_t EtherToPpp (uint16_t protocol);

	void MsgComplete(Ptr<RdmaQueuePair> qp, Ptr<RdmaOperation> msg);
//	void RcvFinish(Ptr<RdmaRxQueuePair> qp, Ptr<RdmaRxOperation> msg);
	void SetLinkDown(Ptr<QbbNetDevice> dev);

	// call this function after the NIC is setup
	void AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx);
	void ClearTable();
	void RedistributeQp();

	bool ShouldSndData(Ptr<RdmaQueuePair> qp);
	Ptr<Packet> GetNxtPacket(Ptr<RdmaQueuePair> qp); // get next packet to send, inc snd_nxt
	void UpdateNextAvail(Ptr<RdmaSndQueuePair> qp, Time interframeGap, uint32_t pkt_size);
	void PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> pkt, Time interframeGap, bool is_tkn);
	void ChangeRate(Ptr<RdmaQueuePair> qp, DataRate new_rate);

	/*---------------------------------------IRN----------------------------------*/
	//liuchang: for IRN
	struct //!< IRN configuration
	{
		uint32_t maxBitmapSize; //!< Maximum bitmap size
		Time rtoHigh; //!< Retransmission timeout high
		Time rtoLow; //!< Retransmission timeout low
		uint32_t rtoLowThreshold; //!< Retransmission timeout low threshold
	} m_irn;
	
	ns3::EventId IrnTimer(ns3::Ptr<ns3::RdmaSndQueuePair> qp, ns3::Ptr<ns3::RdmaSndOperation> msg, uint32_t irnSeq);
	void IrnTimerHandler (ns3::Ptr<ns3::RdmaSndQueuePair> qp, ns3::Ptr<ns3::RdmaSndOperation> msg, uint32_t irnSeq);
	int ReceiveSack(Ptr<Packet> p, CustomHeader &ch); // handle SACK
	void SetUpIrn(uint32_t MaxBitmapSize, uint32_t rtoH, uint32_t rtoL, uint32_t rtoLowTh);
	
	/***************************** Proactive CC *********************************/
	uint32_t m_l4TokenMode;
	uint32_t m_maxUnschCount;	// unsch phase
	uint32_t m_limit_tkn_mode;
	uint32_t m_turn_sch_phase_mode;
	bool m_resume_bdp_tkn;		// under data-driven

	// no unsch phase & dstRateBased: need a request to notify receiver
	void SendReq(Ptr<RdmaSndQueuePair> qp, Ptr<RdmaSndOperation> msg);
	void ReceiveReq(Ptr<Packet> p, CustomHeader &ch);

	// token control
	bool ShouldSndTkn(Ptr<RdmaQueuePair> qp);
	Ptr<Packet> GetTknPacket(Ptr<RdmaQueuePair> qp);
	void UpdateNextTknAvail(Ptr<RdmaQueuePair> qp, Time interframeGap);
	void ReceiveToken(Ptr<Packet> p, CustomHeader &ch);
	void SendingData(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> pkt);
	void StoreCredit(Ptr<RdmaSndQueuePair> qp, CustomHeader &ch, bool ecn);

	// QP's phase transfer
	bool IsSch(Ptr<RdmaSndQueuePair> qp);
	void TurnToUnsch(Ptr<RdmaSndQueuePair> qp);
	void TurnToSch(Ptr<RdmaSndQueuePair> qp);

	/*************
	 * MIBO
	 *************/
	uint32_t m_token_cc;
	uint32_t m_credit_Emin;
	uint32_t m_credit_Emax;
	double m_credit_Ep;
	// for statistic
	static uint32_t mibo_expired_token;
	// Note: we reuse m_rate to transform to token rate and m_rate can be adjusted by reactive CC
	void ReceiveTokenACK(Ptr<Packet> p, CustomHeader &ch);
	void ReceiveECNMarked(Ptr<RdmaQueuePair> qp, CustomHeader &ch);
	void RefreshCredit(Ptr<RdmaSndQueuePair> qp);

	/***************
	 * ExpressPass
	 ***************/
	double m_ep_wmin;
	double m_ep_wmax;
	double m_ep_targetloss;
	void ReceiveDataEP(Ptr<RdmaQueuePair> qp, CustomHeader &ch, uint32_t psn);
	void SentCreditEP(Ptr<RdmaQueuePair> qp);
	void CreditFeedback(Ptr<RdmaQueuePair> qp);

	/***************************** Reactive CC *********************************/
	DataRate m_minRate;		//< Min sending rate

	/******************************
	 * Mellanox's version of DCQCN
	 *****************************/
	double m_g; //feedback weight
	double m_rateOnFirstCNP; // the fraction of line rate to set on first CNP
	bool m_EcnClampTgtRate;
	double m_rpgTimeReset;
	double m_rateDecreaseInterval;
	uint32_t m_rpgThreshold;
	double m_alpha_resume_interval;
	DataRate m_rai;		//< Rate of additive increase
	DataRate m_rhai;		//< Rate of hyper-additive increase
	// the Mellanox's version of alpha update:
	// every fixed time slot, update alpha.
	void UpdateAlphaMlx(Ptr<RdmaQueuePair> q);
	void ScheduleUpdateAlphaMlx(Ptr<RdmaQueuePair> q);
	// Mellanox's version of CNP receive
	void cnp_received_mlx(Ptr<RdmaQueuePair> q);
	// Mellanox's version of rate decrease
	// It checks every m_rateDecreaseInterval if CNP arrived (m_decrease_cnp_arrived).
	// If so, decrease rate, and reset all rate increase related things
	void CheckRateDecreaseMlx(Ptr<RdmaQueuePair> q);
	void ScheduleDecreaseRateMlx(Ptr<RdmaQueuePair> q, uint32_t delta);
	// Mellanox's version of rate increase
	void RateIncEventTimerMlx(Ptr<RdmaQueuePair> q);
	void RateIncEventMlx(Ptr<RdmaQueuePair> q);
	void FastRecoveryMlx(Ptr<RdmaQueuePair> q);
	void ActiveIncreaseMlx(Ptr<RdmaQueuePair> q);
	void HyperIncreaseMlx(Ptr<RdmaQueuePair> q);
	//Help to increase rate more quickly
	void MIBOIncreaseMlx(Ptr<RdmaQueuePair> q);

	/***********************
	 * Swift
	 ***********************/
	// calculate target delay
	uint64_t m_base_target;		// base target delay
	uint64_t m_h;				// per hop scaling factor
	uint64_t m_fs_range;		// max scaling range
	double m_fs_max_cwnd;		// max cwnd for target scaling
	double m_fs_min_cwnd;		// min cwnd for target scaling
	// adjust cwnd
	double m_min_cwnd;
	double m_max_cwnd;
	double m_ai;				// additive increment
	double m_beta;				// multiplicative decrease
	double m_max_mdf;			// maximum multiplicative decrease factor
	uint64_t m_end_target;
	uint32_t m_retx_reset_threshold;	// RTO
	// EWMA
	double m_ewma_alpha;
	static uint64_t swift_max_fabric_rtt;
	void HandleAckSwift(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t num_acked);
	uint64_t GetFabricTargetDelay(Ptr<RdmaSndQueuePair> qp, uint8_t nhop);
	bool CanDecrease(Ptr<RdmaSndQueuePair> qp);
	void UpdateWin(Ptr<RdmaSndQueuePair> qp);
	void FastRecoverySwift(Ptr<RdmaSndQueuePair> qp);
	void RTOSwift(Ptr<RdmaSndQueuePair> qp);

	/***********************
	 * High Precision CC
	 ***********************/
	double m_targetUtil;
	double m_utilHigh;
	uint32_t m_miThresh;
	bool m_multipleRate;
	bool m_sampleFeedback; // only react to feedback every RTT, or qlen > 0
	void HandleAckHp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t ack_seq);
	void UpdateRateHp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool fast_react);
	void UpdateRateHpTest(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool fast_react);
	void FastReactHp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);

	/**********************
	 * TIMELY
	 *********************/
	double m_tmly_alpha, m_tmly_beta;
	uint64_t m_tmly_TLow, m_tmly_THigh, m_tmly_minRtt;
	void HandleAckTimely(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t ack_seq);
	void UpdateRateTimely(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool us);
	void FastReactTimely(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);

	/**********************
	 * DCTCP
	 *********************/
	DataRate m_dctcp_rai;
	void HandleAckDctcp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t ack_seq);
	void HandleDisOrderedAckDctcp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch);

	/******************** Flow Control on NIC((can work with CC)) ***********************/

	/*************
	 * Quota
	 ************/
	// wqy: quota
	uint32_t m_quota;
	uint32_t m_quota_dsts[Settings::NODESCALE];
	uint32_t GetWholeHeaderSize();
	void UpdateQuota(uint32_t pktsize, bool isadd, uint32_t dst=-1);
	bool CheckUschQuota(Ptr<RdmaSndQueuePair> qp);
	void AddUschTag(Ptr<Packet>p, Ptr<RdmaSndQueuePair> qp, uint32_t dst);
};

} /* namespace ns3 */

#endif /* RDMA_HW_H */
