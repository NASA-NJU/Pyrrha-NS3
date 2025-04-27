#include <ns3/simulator.h>
#include <ns3/seq-ts-header.h>
#include <ns3/udp-header.h>
#include <ns3/ipv4-header.h>
#include <assert.h>
#include "ns3/ppp-header.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/data-rate.h"
#include "ns3/pointer.h"
#include "ns3/rdma-hw.h"
#include "ns3/ppp-header.h"
#include "ns3/qbb-header.h"
#include "ns3/cn-header.h"
#include "ns3/sack-header.h"

#define DEBUG_MODE 0
#define DEBUG_FLOW 50710
#define DEBUG_QP 50710

namespace ns3{

uint32_t RdmaHw::mibo_expired_token = 0;
uint64_t RdmaHw::swift_max_fabric_rtt = 0;

TypeId RdmaHw::GetTypeId (void)
{
	static TypeId tid = TypeId ("ns3::RdmaHw")
		.SetParent<Object> ()
		.AddAttribute("MinRate",
				"Minimum rate of a throttled flow",
				DataRateValue(DataRate("100Mb/s")),
				MakeDataRateAccessor(&RdmaHw::m_minRate),
				MakeDataRateChecker())
		.AddAttribute("Mtu",
				"Mtu.",
				UintegerValue(1000),
				MakeUintegerAccessor(&RdmaHw::m_mtu),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute ("CcMode",
				"which mode of DCQCN is running",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_cc_mode),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute ("MIBOccMode",
				"which mode of mibo cc is running",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_mibo_cc_mode),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("NACK Generation Interval",
				"The NACK Generation interval",
				DoubleValue(500.0),
				MakeDoubleAccessor(&RdmaHw::m_nack_interval),
				MakeDoubleChecker<double>())
		.AddAttribute("L2ChunkSize",
				"Layer 2 chunk size. Disable chunk mode if equals to 0.",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_chunk),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("L2AckInterval",
				"Layer 2 Ack intervals. Disable ack if equals to 0.",
				UintegerValue(0),
				MakeUintegerAccessor(&RdmaHw::m_ack_interval),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("L2BackToZero",
				"Layer 2 go back to zero transmission.",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_backto0),
				MakeBooleanChecker())
		.AddAttribute("EwmaGain",
				"Control gain parameter which determines the level of rate decrease",
				DoubleValue(1.0 / 16),
				MakeDoubleAccessor(&RdmaHw::m_g),
				MakeDoubleChecker<double>())
		.AddAttribute ("RateOnFirstCnp",
				"the fraction of rate on first CNP",
				DoubleValue(1.0),
				MakeDoubleAccessor(&RdmaHw::m_rateOnFirstCNP),
				MakeDoubleChecker<double> ())
		.AddAttribute("ClampTargetRate",
				"Clamp target rate.",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_EcnClampTgtRate),
				MakeBooleanChecker())
		.AddAttribute("RPTimer",
				"The rate increase timer at RP in microseconds",
				DoubleValue(1500.0),
				MakeDoubleAccessor(&RdmaHw::m_rpgTimeReset),
				MakeDoubleChecker<double>())
		.AddAttribute("RateDecreaseInterval",
				"The interval of rate decrease check",
				DoubleValue(4.0),
				MakeDoubleAccessor(&RdmaHw::m_rateDecreaseInterval),
				MakeDoubleChecker<double>())
		.AddAttribute("FastRecoveryTimes",
				"The rate increase timer at RP",
				UintegerValue(5),
				MakeUintegerAccessor(&RdmaHw::m_rpgThreshold),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("AlphaResumInterval",
				"The interval of resuming alpha",
				DoubleValue(55.0),
				MakeDoubleAccessor(&RdmaHw::m_alpha_resume_interval),
				MakeDoubleChecker<double>())
		.AddAttribute("RateAI",
				"Rate increment unit in AI period",
				DataRateValue(DataRate("5Mb/s")),
				MakeDataRateAccessor(&RdmaHw::m_rai),
				MakeDataRateChecker())
		.AddAttribute("RateHAI",
				"Rate increment unit in hyperactive AI period",
				DataRateValue(DataRate("50Mb/s")),
				MakeDataRateAccessor(&RdmaHw::m_rhai),
				MakeDataRateChecker())
		.AddAttribute("VarWin",
				"Use variable window size or not",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_var_win),
				MakeBooleanChecker())
		.AddAttribute("FastReact",
				"Fast React to congestion feedback",
				BooleanValue(true),
				MakeBooleanAccessor(&RdmaHw::m_fast_react),
				MakeBooleanChecker())
		.AddAttribute("MiThresh",
				"Threshold of number of consecutive AI before MI",
				UintegerValue(5),
				MakeUintegerAccessor(&RdmaHw::m_miThresh),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("TargetUtil",
				"The Target Utilization of the bottleneck bandwidth, by default 95%",
				DoubleValue(0.95),
				MakeDoubleAccessor(&RdmaHw::m_targetUtil),
				MakeDoubleChecker<double>())
		.AddAttribute("UtilHigh",
				"The upper bound of Target Utilization of the bottleneck bandwidth, by default 98%",
				DoubleValue(0.98),
				MakeDoubleAccessor(&RdmaHw::m_utilHigh),
				MakeDoubleChecker<double>())
		.AddAttribute("RateBound",
				"Bound packet sending by rate, for test only",
				BooleanValue(true),
				MakeBooleanAccessor(&RdmaHw::m_rateBound),
				MakeBooleanChecker())
		.AddAttribute("MultiRate",
				"Maintain multiple rates in HPCC",
				BooleanValue(true),
				MakeBooleanAccessor(&RdmaHw::m_multipleRate),
				MakeBooleanChecker())
		.AddAttribute("SampleFeedback",
				"Whether sample feedback or not",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_sampleFeedback),
				MakeBooleanChecker())
		.AddAttribute("TimelyAlpha",
				"Alpha of TIMELY",
				DoubleValue(0.875),
				MakeDoubleAccessor(&RdmaHw::m_tmly_alpha),
				MakeDoubleChecker<double>())
		.AddAttribute("TimelyBeta",
				"Beta of TIMELY",
				DoubleValue(0.8),
				MakeDoubleAccessor(&RdmaHw::m_tmly_beta),
				MakeDoubleChecker<double>())
		.AddAttribute("TimelyTLow",
				"TLow of TIMELY (ns)",
				UintegerValue(50000),
				MakeUintegerAccessor(&RdmaHw::m_tmly_TLow),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("TimelyTHigh",
				"THigh of TIMELY (ns)",
				UintegerValue(500000),
				MakeUintegerAccessor(&RdmaHw::m_tmly_THigh),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("TimelyMinRtt",
				"MinRtt of TIMELY (ns)",
				UintegerValue(20000),
				MakeUintegerAccessor(&RdmaHw::m_tmly_minRtt),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("DctcpRateAI",
				"DCTCP's Rate increment unit in AI period",
				DataRateValue(DataRate("1000Mb/s")),
				MakeDataRateAccessor(&RdmaHw::m_dctcp_rai),
				MakeDataRateChecker())
		.AddAttribute("LimitTknMode",
				"when sending token, check total size",
				UintegerValue(false),
				MakeUintegerAccessor(&RdmaHw::m_limit_tkn_mode),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("TurnSchPhaseMode",
				"don't turn into sch status until sending all unsch packets",
				UintegerValue(false),
				MakeUintegerAccessor(&RdmaHw::m_turn_sch_phase_mode),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("ResumeBDPTkn",
				"resume first bdp token",
				BooleanValue(false),
				MakeBooleanAccessor(&RdmaHw::m_resume_bdp_tkn),
				MakeBooleanChecker())
		.AddAttribute("MiboNsdiTknCC",
				"MIBO-NSDI22's token CC",
				UintegerValue(Settings::CC_DCQCN),
				MakeUintegerAccessor(&RdmaHw::m_token_cc),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("MiboNsdiCreditEmin",
				"MIBO-NSDI22's Emin for credit",
				UintegerValue(10),
				MakeUintegerAccessor(&RdmaHw::m_credit_Emin),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("MiboNsdiCreditEmax",
				"MIBO-NSDI22's Emax for credit",
				UintegerValue(40),
				MakeUintegerAccessor(&RdmaHw::m_credit_Emax),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("MiboNsdiCreditEp",
				"MIBO-NSDI22's Ep for credit",
				DoubleValue(0.5),
				MakeDoubleAccessor(&RdmaHw::m_credit_Ep),
				MakeDoubleChecker<double>())
		.AddAttribute("MaxUnsch",
				"max successive unscheduled packet",
				UintegerValue(1),
				MakeUintegerAccessor(&RdmaHw::m_maxUnschCount),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("L4TokenMode",
				"L4 token mode",
				UintegerValue(RdmaEgressQueue::L4TokenNo),
				MakeUintegerAccessor(&RdmaHw::m_l4TokenMode),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("EPWmax",
				"EP wmax",
				DoubleValue(0.5),
				MakeDoubleAccessor(&RdmaHw::m_ep_wmax),
				MakeDoubleChecker<double>())
		.AddAttribute("EPWmin",
				"EP wmin",
				DoubleValue(0.01),
				MakeDoubleAccessor(&RdmaHw::m_ep_wmin),
				MakeDoubleChecker<double>())
		.AddAttribute("EPTargetLoss",
				"EP target loss",
				DoubleValue(0.1),
				MakeDoubleAccessor(&RdmaHw::m_ep_targetloss),
				MakeDoubleChecker<double>())
		.AddAttribute("BaseTarget",
				"Swift: base target delay",
				UintegerValue(3000),
				MakeUintegerAccessor(&RdmaHw::m_base_target),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("HopScaleFactor",
				"Swift: per hop scaling factor",
				UintegerValue(1500),
				MakeUintegerAccessor(&RdmaHw::m_h),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("MaxRangeScale",
				"Swift: max scaling range",
				UintegerValue(50000),
				MakeUintegerAccessor(&RdmaHw::m_fs_range),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("MinCwndScale",
				"Swift: min cwnd for target scaling",
				DoubleValue(0.1),
				MakeDoubleAccessor(&RdmaHw::m_fs_min_cwnd),
				MakeDoubleChecker<double>())
		.AddAttribute("MaxCwndScale",
				"Swift: max cwnd for target scaling",
				DoubleValue(50.0),
				MakeDoubleAccessor(&RdmaHw::m_fs_max_cwnd),
				MakeDoubleChecker<double>())
		.AddAttribute("AICwnd",
				"Swift: additive increment",
				DoubleValue(1),
				MakeDoubleAccessor(&RdmaHw::m_ai),
				MakeDoubleChecker<double>())
		.AddAttribute("MDCwnd",
				"Swift: multiplicative decrease",
				DoubleValue(0.5),
				MakeDoubleAccessor(&RdmaHw::m_beta),
				MakeDoubleChecker<double>())
		.AddAttribute("MaxMDF",
				"Swift: maximum multiplicative decrease factor",
				DoubleValue(0.5),
				MakeDoubleAccessor(&RdmaHw::m_max_mdf),
				MakeDoubleChecker<double>())
		.AddAttribute("AlphaEWMA",
				"Swift: alpha of EWMA",
				DoubleValue(0.7),
				MakeDoubleAccessor(&RdmaHw::m_ewma_alpha),
				MakeDoubleChecker<double>())
		.AddAttribute("RetxResetTh",
				"Swift: retx reset threshold",
				UintegerValue(5),
				MakeUintegerAccessor(&RdmaHw::m_retx_reset_threshold),
				MakeUintegerChecker<uint32_t>())
		.AddAttribute("EndTarget",
				"Swift: end point target",
				UintegerValue(10),
				MakeUintegerAccessor(&RdmaHw::m_end_target),
				MakeUintegerChecker<uint64_t>())
		.AddAttribute("MinCwnd",
				"Swift: min cwnd",
				DoubleValue(0.01),
				MakeDoubleAccessor(&RdmaHw::m_min_cwnd),
				MakeDoubleChecker<double>())
		.AddAttribute("MaxCwnd",
				"Swift: max cwnd",
				DoubleValue(100.0),
				MakeDoubleAccessor(&RdmaHw::m_max_cwnd),
				MakeDoubleChecker<double>())
		;
	return tid;
}

RdmaHw::RdmaHw(){
	bwId = Simulator::Schedule(Seconds(2), &RdmaHw::BW, this);//all applications start at 2s
	bw = 0;
	tp = 0;
	ctrl = 0;
	totalQueuingPackets = 0;
	totalQueuingTimeUs = 0;
}

RdmaHw::~RdmaHw(){
	Simulator::Cancel(bwId);
}

void RdmaHw::SetNode(Ptr<Node> node){
	m_node = node;
}
void RdmaHw::Setup(RcvFinishCallback rcvcb, MsgCompleteCallback mcb){
	for (uint32_t i = 0; i < m_nic.size(); i++){
		Ptr<QbbNetDevice> dev = m_nic[i].dev;
		if (dev == NULL)
			continue;
		// share data with NIC
		dev->m_rdmaEQ->m_qpGrp = m_nic[i].qpGrp;
		// setup callback
		dev->m_rdmaReceiveCb = MakeCallback(&RdmaHw::Receive, this);
		dev->m_rdmaLinkDownCb = MakeCallback(&RdmaHw::SetLinkDown, this);
		dev->m_rdmaPktSent = MakeCallback(&RdmaHw::PktSent, this);
		// config NIC
		dev->m_rdmaEQ->m_mtu = m_mtu;
		dev->m_rdmaEQ->m_rdmaGetNxtPkt = MakeCallback(&RdmaHw::GetNxtPacket, this);
		dev->m_rdmaEQ->m_rdmaGetTknPkt = MakeCallback(&RdmaHw::GetTknPacket, this);
		// wqy: callback for checking qp status
		dev->m_rdmaEQ->m_rdmaCheckData = MakeCallback(&RdmaHw::ShouldSndData, this);
		dev->m_rdmaEQ->m_rdmaCheckToken = MakeCallback(&RdmaHw::ShouldSndTkn, this);
		// wqy: callback for Quota
		dev->m_rdmaEQ->m_rdmaCheckUschQuota = MakeCallback(&RdmaHw::CheckUschQuota, this);
	}
	// setup qp complete callback
	m_rcvFinishCallback = rcvcb;
	m_msgCompleteCallback = mcb;
//	m_quota = std::ceil((double) Settings::max_bdp / m_mtu) * (m_mtu+GetWholeHeaderSize()) * Settings::quota_num;
}

// todo: note here
uint64_t RdmaHw::GetQpKey(uint16_t sport, uint16_t pg, uint32_t qpid){
	return qpid;
//	return ((uint64_t)sport << 16) | (uint64_t)pg;
}

Ptr<RdmaSndQueuePair> RdmaHw::GetSndQp(uint16_t sport, uint16_t pg, uint32_t qpid){
	uint64_t key = GetQpKey(sport, pg, qpid);
	auto it = m_qpMap.find(key);
	if (it != m_qpMap.end())
		return it->second;
	return NULL;
}

/**
 * When a message arrivals on sender
 * -> register RdmaQP and RdmaOperation
 */
void RdmaHw::AddQueuePair(uint64_t size, uint16_t pg, Ipv4Address sip, Ipv4Address dip, uint16_t sport, uint16_t dport, uint32_t win, uint64_t baseRtt, uint32_t qpid, uint32_t msgSeq, uint32_t src, uint32_t dst, uint32_t flow_id, bool isTestFlow){
	// create message
	Ptr<RdmaSndOperation> msg = Create<RdmaSndOperation>(pg, sip.Get(), dip.Get(), sport, dport);
	msg->SetSrc(src);
	msg->SetDst(dst);
	msg->SetMSGSeq(msgSeq);
	msg->SetQPId(qpid);
	msg->SetSize(size);
	msg->SetFlowId(flow_id);
	msg->SetTestFlow(isTestFlow);
	msg->SetMTU(m_mtu);

	uint64_t key = GetQpKey(sport, pg, qpid);
	if (m_qpMap.find(key) == m_qpMap.end()){
		Ptr<RdmaSndQueuePair> qp = CreateObject<RdmaSndQueuePair>(pg, sip, dip, sport, dport);
		qp->SetQPId(qpid);
		qp->SetWin(win);
		qp->SetBaseRtt(baseRtt);
		qp->SetVarWin(m_var_win);
		qp->SetDst(dst);
		qp->AddRdmaOperation(msg);

		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].qpGrp->AddQp(qp);
		//liuchang: find a queue for the qp
		if(Settings::queue_allocate_in_host == 1){
			uint32_t qid = m_nic[nic_idx].dev->GetQueueforFlow();
			qp->SetQueueId(qid);
			m_nic[nic_idx].dev->UpdateQueueforFlow(qid,true);
		}
		m_qpMap[key] = qp;

		// set init variables
		DataRate m_bps = m_nic[nic_idx].dev->GetDataRate();
		qp->InitalCC(m_bps, m_multipleRate);
		qp->m_nextAvail = Simulator::Now();
		qp->PrintRate();

		qp->SetRTOCallback(MakeCallback(&RdmaHw::OnRTO, this));

	}else{
		m_qpMap[key]->AddRdmaOperation(msg);
	}

	/*
	 * Congestion Isolation need routing hops to match congestion root
	 * -> calculate routing for new msgs
	 */
	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION)
		msg->routingTag = CalcuAllRoutingHops(m_node->GetId(), GetNicIdxOfSndQp(m_qpMap[key]), m_qpMap[key]->sip, m_qpMap[key]->dip, msg->sport, msg->dport, m_qpMap[key]->m_qpid);

	if (Settings::qos_mode != Settings::QOS_NONE){
		msg->UpdateQoSRank();
	}

#if DEBUG_MODE
	if (qpid == DEBUG_QP || qpid == DEBUG_FLOW)
		Settings::warning_out << Simulator::Now() << " sender flow arrival, id:"
					<< qpid << " size: " << m_qpMap[key]->m_total_size << std::endl;
#endif

	/**
	 * If it's receiver-driven and no unsch phase -> send a REQ packet
	 */
	if (m_l4TokenMode == RdmaEgressQueue::L4TokenDstRateBased && m_maxUnschCount == 0)
		SendReq(m_qpMap[key], msg);		// trigger dequeue inside
	else{
		// qp may has ability to send data/token -> Notify Nic to pull packet
		uint32_t nic_idx = GetNicIdxOfQp(m_qpMap[key]);
		m_nic[nic_idx].dev->TriggerTransmit();
	}

}

void RdmaHw::StopMessage(uint32_t qpid, uint32_t msgseq){
	uint64_t key = GetQpKey(0, 0, qpid);

	// as sender
	auto it = m_qpMap.find(key);
	if (it != m_qpMap.end()){
		Ptr<RdmaSndQueuePair> qp = it->second;
		bool tmp;
		Ptr<RdmaSndOperation> msg = qp->GetMsg(msgseq, tmp);
		MsgComplete(qp, msg);
	}

	// as receiver
	auto it_rx = m_rxQpMap.find(key);
	if (it_rx != m_rxQpMap.end()){
		Ptr<RdmaRxQueuePair> qp = it_rx->second;
		Ptr<RdmaRxOperation> msg = qp->GetRxMSG(msgseq);
		MsgComplete(qp, msg);
	}
}

Ptr<RdmaRxQueuePair> RdmaHw::GetRxQp(uint32_t sip, uint32_t dip, uint16_t sport, uint16_t dport, uint16_t pg, uint32_t flowId, uint32_t qpid, bool create){
//	uint64_t key = ((uint64_t)dip << 32) | ((uint64_t)pg << 16) | (uint64_t)dport;
	uint64_t key = GetQpKey(dport, pg, qpid);;
	auto it = m_rxQpMap.find(key);
	if (it != m_rxQpMap.end())
		return it->second;
	if (create){
		// create new rx qp
		Ptr<RdmaRxQueuePair> q = CreateObject<RdmaRxQueuePair>();
		// init the qp
		q->sip = sip;
		q->dip = dip;
		q->m_qpid = qpid;
		// store in map
		m_rxQpMap[key] = q;
		uint32_t nic_idx = GetNicIdxOfRxQp(q);
		m_nic[nic_idx].qpGrp->AddQp(q);
		// set init variables
		DataRate m_bps = m_nic[nic_idx].dev->GetDataRate();
		q->SetWin(m_searchWinCallback(Settings::hostIp2IdMap[dip]));
		q->SetVarWin(m_var_win);
		q->InitalCC(m_bps, m_multipleRate);
		q->PrintRate();
		// Notify Nic
		m_nic[nic_idx].dev->NewQp(q);
		return q;
	}
	return NULL;
}

uint32_t RdmaHw::GetNicIdxOfRxQp(Ptr<RdmaRxQueuePair> q){
	auto &v = m_rtTable[q->dip];
//	auto &v = Settings::node_rtTable[m_node->GetId()][q->dip];
	if (v.size() > 0){
		return v[q->GetHash() % v.size()];
	}else{
		NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
	}
}

uint32_t RdmaHw::GetNicIdxOfSndQp(Ptr<RdmaSndQueuePair> qp){
	auto &v = m_rtTable[qp->dip];
//	auto &v = Settings::node_rtTable[m_node->GetId()][qp->dip];
	if (v.size() > 0){
		return v[qp->GetHash() % v.size()];
	}else{
		NS_ASSERT_MSG(false, "We assume at least one NIC is alive");
	}
}

uint32_t RdmaHw::GetNicIdxOfQp(Ptr<RdmaQueuePair> qp){
	if (qp->is_sndQP)
		return GetNicIdxOfSndQp(DynamicCast<RdmaSndQueuePair>(qp));
	else
		return GetNicIdxOfRxQp(DynamicCast<RdmaRxQueuePair>(qp));
}

RoutingTag RdmaHw::CalcuAllRoutingHops(uint32_t curr_node, uint32_t curr_idx, uint32_t sip, uint32_t dip, uint32_t sport, uint32_t dport, uint32_t qp_id){
	RoutingTag routingTag;
	while (Settings::link_node[curr_node][curr_idx] != Settings::hostIp2IdMap[dip]){
		curr_node = Settings::link_node[curr_node][curr_idx];
		// look up entries
		auto entry = Settings::node_rtTable[curr_node].find(dip);

		// no matching entry
		if (entry == m_rtTable.end()){
			Settings::warning_out << Simulator::Now() << " Node" << m_node->GetId() << " cannot reach " << Settings::hostIp2IdMap[dip] << " (request for qp" << qp_id << ")"  << std::endl;
			return routingTag;
		}

		// entry found
		auto &nexthops = entry->second;

		// pick one next hop based on hash
		union {
			uint8_t u8[4+4+4];
			uint32_t u32[3];
		} buf;

		if (Settings::symmetic_routing_mode == Settings::SYMMETRIC_ON){
			buf.u32[0] = sip | dip;
			buf.u32[1] = dip | sip;
		}else{
			buf.u32[0] = sip;
			buf.u32[1] = dip;
		}

		if (Settings::ecmp_unit == Settings::ECMP_QP){
			buf.u32[2] = qp_id;
		}else{
			if (Settings::symmetic_routing_mode == Settings::SYMMETRIC_ON){
				buf.u32[2] = sport | dport;
			}else
				buf.u32[2] = sport | ((uint32_t)dport << 16);
		}

		curr_idx = Settings::EcmpHash(buf.u8, 12, curr_node);
		curr_idx = curr_idx % (nexthops.size() + Settings::load_imbalance_th);
		if (curr_idx >= nexthops.size()) curr_idx = 0;
		curr_idx = nexthops[curr_idx % nexthops.size()];
		routingTag.EnqueueRouting(curr_node, curr_idx);
	}
	return routingTag;
}

void RdmaHw::SendCnp(Ptr<RdmaQueuePair> qp, CustomHeader &ch, uint8_t ecnbits){
	if (!Settings::host_cnp == Settings::ADDITION_CNP)	return;

	Ptr<Packet> newp = Create<Packet>(0);
	// l4 header
	CnHeader cnh;
	cnh.SetECNBits(ecnbits);
	cnh.SetQindex(ch.udp.pg);
	cnh.SetFlow(ch.udp.sport);
	newp->AddHeader(cnh);
	//20 bytes
	Ipv4Header head;	// Prepare IPv4 header
	head.SetDestination(Ipv4Address(ch.sip));
	head.SetSource(Ipv4Address(ch.dip));
	head.SetProtocol(CustomHeader::ProtTypeE::QCN);
	head.SetTtl(64);
	head.SetPayloadSize(newp->GetSize());
	head.SetIdentification(qp->m_ipid++);
	newp->AddHeader(head);
	//ppp: 14 + 24
	AddHeader(newp, 0x800);	// Attach PPP header

	QPTag qptag;
	qptag.SetQPID(qp->m_qpid);
	newp->AddPacketTag(qptag);

//#if DEBUG_MODE
//	std::cout << "send CNP " << Simulator::Now() << " " << qptag.GetQPID() << std::endl;
//#endif

	// send
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
		newp->AddPacketTag(CalcuAllRoutingHops(m_node->m_id, nic_idx, ch.dip, ch.sip, 0, 0, qptag.GetQPID()));
	}
	m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(newp);
	m_nic[nic_idx].dev->TriggerTransmit();
}

void RdmaHw::OnRTO(Ptr<RdmaQueuePair> qp){
	if (Settings::CC_SWIFT == m_cc_mode){
		assert(qp->is_sndQP);
		RTOSwift(DynamicCast<RdmaSndQueuePair>(qp));
	}
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	m_nic[nic_idx].dev->TriggerTransmit();
}

void RdmaHw::OnSACK(Ptr<RdmaQueuePair> qp){
	if (Settings::CC_SWIFT == m_cc_mode){
		assert(qp->is_sndQP);
		FastRecoverySwift(DynamicCast<RdmaSndQueuePair>(qp));
	}
}

int RdmaHw::ReceiveUdp(Ptr<Packet> p, CustomHeader &ch){
	uint8_t ecnbits = ch.GetIpv4EcnBits();

	uint32_t payload_size = p->GetSize() - ch.GetSerializedSize();

	tp += payload_size;
	bw += p->GetSize();

	FlowTag ftag;
	p->PeekPacketTag(ftag);

	QPTag qptag;
	p->PeekPacketTag(qptag);

	Ptr<RdmaRxQueuePair> rxQp = GetRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, ftag.getIndex(), qptag.GetQPID(), true);
	Ptr<RdmaRxOperation> rxMsg = rxQp->GetRxMSG(qptag.GetMsgSeq());
	if (!rxMsg && rxQp->m_finished_msgs.find(qptag.GetMsgSeq()) == rxQp->m_finished_msgs.end()){
		rxMsg = Create<RdmaRxOperation>();
		rxMsg->dip = ch.sip;
		rxMsg->sip = ch.dip;
		rxMsg->dport = ch.udp.sport;
		rxMsg->sport = ch.udp.dport;
		rxMsg->m_size = ch.udp.size;
		rxMsg->m_flowId = ftag.getIndex();
		rxMsg->m_msgSeq = qptag.GetMsgSeq();
		rxMsg->m_milestone_rx = m_ack_interval;
		rxQp->AddRdmaOperation(rxMsg);
	}

#if DEBUG_MODE
		if (rxQp->m_qpid == DEBUG_QP)
			Settings::warning_out << Simulator::Now() << " receiveUDP " << rxQp->m_qpid
						<< " udp_size: " << ch.udp.size
						<< " udp_seq: " << ch.udp.seq
						<< " udp_pg: " << ch.udp.pg
						<< " total_size: " << rxQp->m_total_size << std::endl;
#endif

	if (ecnbits != 0){
		rxQp->m_ecn_source.ecnbits |= ecnbits;
		rxQp->m_ecn_source.qfb++;
	}
	rxQp->m_ecn_source.total++;

	assert(!!rxMsg || rxQp->m_finished_msgs.find(qptag.GetMsgSeq()) != rxQp->m_finished_msgs.end());		// receiving or finished
	if (!rxMsg) return 0;

#if DEBUG_MODE
		if (rxQp->m_qpid == DEBUG_QP)
			Settings::warning_out << Simulator::Now() << " " << rxQp->m_qpid << " receive data " << std::endl;
#endif

	// update queuing time
	QueueingTag q;
	if(p->RemovePacketTag(q)){
		uint64_t max_hop = q.GetActiveHop();
		for (uint64_t i = 0; i < max_hop; ++i){
			rxMsg->m_queuingTime[i].push_back(q.GetHopQueuingTime(i));
		}
	}

	uint32_t expectedSeq = 0;
	int x = ReceiverCheckSeq(ch.udp.seq, rxMsg, payload_size, expectedSeq);

//#if DEBUG_MODE
//	if (!!ecnbits)
//		std::cout << "receive UDP " << Simulator::Now() << " " << (!!ecnbits) << " " << ch.udp.seq << " " << qptag.GetQPID() << std::endl;
//#endif

	bool receivedAll = false;
	rxMsg->m_received_psn.insert(ch.udp.seq);
	LastPacketTag lastTag;
	if (p->PeekPacketTag(lastTag)){
		rxMsg->m_received_last_psn_packet = true;
	}
	receivedAll = rxMsg->ReceivedAll(Settings::packet_payload);

	/**
	 * when use packet-level routing
	 * --> unordered packets are normal
	 * --> so, always send ACK
	 */
	if (Settings::AllowDisOrder()){
		x = 1;
	}

	/*
	 * when use receiver-driven data-driven credit
	 * -> add token quota
	 */
	if (RdmaEgressQueue::L4TokenDstDataDriven == m_l4TokenMode)
		rxQp->m_tkn_max += payload_size;

	/*
	 * When piggy TokenACK -> sch packets
	 * -> update tkn_ack
	 */
	TokenTag tkntag;
	if (p->PeekPacketTag(tkntag)){
		if (m_token_cc == Settings::CC_EP){
			ReceiveDataEP(rxQp, ch, tkntag.GetPSN());
		}
		rxQp->m_tkn_ack = std::max(tkntag.GetPSN()+1, rxQp->m_tkn_ack);

		if (rxQp->m_sch_start > ch.udp.seq)
			rxQp->m_sch_start = ch.udp.seq;

		// resume unsch token when no congestion
		if (!ecnbits && m_resume_bdp_tkn){
			uint32_t resume_value = Settings::max_bdp > rxQp->m_sch_start ? (Settings::max_bdp - rxQp->m_sch_start): 0;
			if (rxQp->m_lst_resumed_unsch_tkn < resume_value){
				rxQp->m_tkn_max += (resume_value - rxQp->m_lst_resumed_unsch_tkn);
				rxQp->m_lst_resumed_unsch_tkn = resume_value;
			}
		}
	}

	bool sentCNP = false;
	/**
	 * under proactive cc:
	 * do not send ACK expect for last data packet
	 * if ecn marked, do something
	 */
	if (m_l4TokenMode != RdmaEgressQueue::L4TokenNo){
		/*
		 * Send ACK only when receive all data packet
		 */
		if (Settings::fc_mode != Settings::FC_FLOODGATE || Settings::switch_ack_mode != Settings::HOST_PER_PACKET_ACK){
			x = 3;
			if (receivedAll) x = 1;
		}

		if (m_token_cc == Settings::CC_DCQCN){
			/*
			 * has ecn marked
			 */
			if (ecnbits){
				switch (m_l4TokenMode){
				/*
				 * sender-driven:
				 * -> generate CNP and send back
				 */
				case RdmaEgressQueue::L4TokenSrcRateBased:{
					SendCnp(rxQp, ch, ecnbits);
					sentCNP = true;
					break;
				}
				/*
				 * receiver-driven:
				 * -> adjust rate
				 */
				case RdmaEgressQueue::L4TokenDstRateBased:
				case RdmaEgressQueue::L4TokenDstDataDriven:{
					cnp_received_mlx(rxQp);
				}
				}
			}
		}
	}

	if (ecnbits && !sentCNP && Settings::host_cnp == Settings::ADDITION_CNP){
		SendCnp(rxQp, ch, ecnbits);
		sentCNP = true;
	}

	//generate ACK or NACK
	if (x == 1 || x == 2){
		Ptr<Packet> newp = NULL;
		if (Settings::use_irn && x == 2) { // generate SACK
			sackHeader sackh;
			sackh.SetACKSeq(ch.udp.seq);
			sackh.SetNACKSeq(expectedSeq);
			sackh.SetPG(ch.udp.pg);	// ack packets take the priority of data packets
			sackh.SetSport(ch.udp.dport);
			sackh.SetDport(ch.udp.sport);
			sackh.SetIntHeader(ch.udp.ih);

			newp = Create<Packet>(std::max(84-38-20-(int)sackh.GetSerializedSize(), 0));// at least 84, modified by lkx
			newp->AddHeader(sackh);
		}else if (Settings::use_irn && x == 1){
			qbbHeader seqh;
			seqh.SetSeq(ch.udp.seq);
			seqh.SetPG(ch.udp.pg);	// ack packets take the priority of data packets
			seqh.SetSport(ch.udp.dport);
			seqh.SetDport(ch.udp.sport);
			seqh.SetIntHeader(ch.udp.ih);
			if (ecnbits && !sentCNP && Settings::host_cnp == Settings::PIGGY_CNP){		// if marked ECN and haven't sent CNP yet -> piggy it
				seqh.SetCnp();
			}

			newp = Create<Packet>(std::max(84-38-20-(int)seqh.GetSerializedSize(), 0));// at least 84, modified by lkx
			newp->AddHeader(seqh);
		}else {
			qbbHeader seqh;
			seqh.SetSeq(rxMsg->ReceiverNextExpectedSeq);
			if (Settings::AllowDisOrder()){
				seqh.SetSeq(ch.udp.seq);
			}
			seqh.SetPG(ch.udp.pg);	// ack packets take the priority of data packets
			seqh.SetSport(ch.udp.dport);
			seqh.SetDport(ch.udp.sport);
			seqh.SetIntHeader(ch.udp.ih);
			if (ecnbits && !sentCNP && Settings::host_cnp == Settings::PIGGY_CNP){		// if marked ECN and haven't sent CNP yet -> piggy it
				seqh.SetCnp();
			}

			newp = Create<Packet>(std::max(84-38-20-(int)seqh.GetSerializedSize(), 0));// at least 84, modified by lkx
			newp->AddHeader(seqh);
		}

		//20 bytes
		Ipv4Header head;	// Prepare IPv4 header
		head.SetDestination(Ipv4Address(ch.sip));
		head.SetSource(Ipv4Address(ch.dip));
		if (x == 1) {
			head.SetProtocol(CustomHeader::ProtTypeE::ACK);
		}else if (x == 2 && Settings::use_irn) {
			head.SetProtocol(CustomHeader::ProtTypeE::SACK);
		}else {
			head.SetProtocol(CustomHeader::ProtTypeE::NACK);
		}
		head.SetTtl(64);
		head.SetPayloadSize(newp->GetSize());
		head.SetIdentification(rxQp->m_ipid++);

		newp->AddHeader(head);

		//ppp: 14 + 24
		AddHeader(newp, 0x800);	// Attach PPP header

		// wqy
		if (Settings::quota_mode){
			UschTag uscht;
			if (p->PeekPacketTag(uscht)){
				newp->AddPacketTag(uscht);
			}
		}

		if (Settings::CC_SWIFT == m_cc_mode){
			NICRxTimeTag nictag;
			nictag.SetNICRxTime(Simulator::Now().GetNanoSeconds());
			newp->AddPacketTag(nictag);
		}
		newp->AddPacketTag(qptag);
		newp->AddPacketTag(ftag);

		/**
		 * add by wqy on 2020/12/08
		 * ACK will piggy back the switchACK info for restore VOQ's window
		 */
		if (Settings::fc_mode == Settings::FC_FLOODGATE && Settings::switch_ack_mode == Settings::HOST_PER_PACKET_ACK){
			SwitchACKTag acktag;
			acktag.setAckedSize(p->GetSize());
			newp->AddPacketTag(acktag);
		}

		if (receivedAll){
			LastPacketTag lastTag;
			newp->AddPacketTag(lastTag);
			MsgComplete(rxQp, rxMsg);
		}

		if(Settings::use_irn && rxMsg->m_irn.GetBaseSeq() >= rxMsg->m_size){
			MsgComplete(rxQp, rxMsg);
		}

		// send
		uint32_t nic_idx = GetNicIdxOfRxQp(rxQp);
		if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
			newp->AddPacketTag(CalcuAllRoutingHops(m_node->m_id, nic_idx, ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, qptag.GetQPID()));
		}
#if DEBUG_MODE
		if (rxQp->m_qpid == DEBUG_QP)
			Settings::warning_out << Simulator::Now() << " " << rxQp->m_qpid << " send ACK" << x << ", nxtSeq:" << rxMsg->ReceiverNextExpectedSeq << std::endl;
#endif
		m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(newp);
		m_nic[nic_idx].dev->TriggerTransmit();
	}else{
		// when receive first udp, receiver-driven CC may need to send token packet -> Notify Nic
		// Or udp may take token-ack which may update inflight token
		if (RdmaEgressQueue::L4TokenDstDataDriven== m_l4TokenMode || RdmaEgressQueue::L4TokenDstRateBased == m_l4TokenMode)
			m_nic[GetNicIdxOfRxQp(rxQp)].dev->TriggerTransmit();
	}
	return 0;
}

int RdmaHw::ReceiveCnp(Ptr<Packet> p, CustomHeader &ch){
	// QCN on NIC
	// This is a Congestion signal
	// Then, extract data from the congestion packet.
	// We assume, without verify, the packet is destinated to me
	uint32_t qIndex = ch.cnp.qIndex;
	uint16_t udpport = ch.cnp.fid; // corresponds to the sport
	uint8_t ecnbits = ch.cnp.ecnBits;

	uint32_t i;

	// get qp
	Ptr<RdmaQueuePair> qp = NULL;
	FlowTag ftag;
	p->PeekPacketTag(ftag);
	QPTag qptag;
	p->PeekPacketTag(qptag);
	qp = GetSndQp(udpport, qIndex, qptag.GetQPID());
	if (!qp)
		qp = GetRxQp(ch.dip, ch.sip, ch.udp.dport, ch.udp.sport, ch.udp.pg, ftag.getIndex(), qptag.GetQPID(), true);

	if (qp == NULL)
		std::cout << "ERROR: QCN NIC cannot find the flow\n";
	// get nic
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;

//	if (qp->m_rate == 0)			// lazy initialization
//	{
//		qp->m_rate = dev->GetDataRate();
//		qp->PrintRate();
//		if (m_cc_mode == Settings::CC_DCQCN){
//			qp->mlx.m_targetRate = dev->GetDataRate();
//		}else if (m_cc_mode == Settings::CC_HPCC){
//			qp->hp.m_curRate = dev->GetDataRate();
//			if (m_multipleRate){
//				for (uint32_t i = 0; i < IntHeader::maxHop; i++)
//					qp->hp.hopState[i].Rc = dev->GetDataRate();
//			}
//		}else if (m_cc_mode == Settings::CC_TIMELY){
//			qp->tmly.m_curRate = dev->GetDataRate();
//		}
//	}

	cnp_received_mlx(qp);
	return 0;
}

int RdmaHw::ReceiveAck(Ptr<Packet> p, CustomHeader &ch){
	uint16_t qIndex = ch.ack.pg;
	uint16_t port = ch.ack.dport;
	uint32_t seq = ch.ack.seq;
	uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
	int i;
	Ptr<RdmaSndQueuePair> qp = NULL;
	uint32_t msg_seq = 0;

	QPTag qptag;
	p->PeekPacketTag(qptag);
	qp = GetSndQp(port, qIndex, qptag.GetQPID());
	if (qp == NULL){
		std::cerr << "ERROR: " << "node:" << m_node->GetId() << ' ' << (ch.l3Prot == CustomHeader::ProtTypeE::ACK ? "ACK" : "NACK") << " NIC cannot find the flow\n";
		return 0;
	}
	
	msg_seq = qptag.GetMsgSeq();
	uint32_t qp_psn = qptag.GetQPPSN();
	bool is_sending = true;
	Ptr<RdmaSndOperation> msg = qp->GetMsg(msg_seq, is_sending);
	assert(!!msg || qp->m_finished_msgs.find(msg_seq) != qp->m_finished_msgs.end());		// sending or finished

	if (!msg) return 0;		// already finish
	
	// wqy
	if (Settings::quota_mode){
		UschTag uscht;
		if (p->PeekPacketTag(uscht)){
			UpdateQuota(uscht.GetPktSize(), true, !qp?-1:msg->m_dst);
		}
	}

#if DEBUG_MODE
	if (qp->m_qpid == DEBUG_QP)
		Settings::warning_out << "receive ACK "<< Simulator::Now() << " " << (!!cnp) << " " << seq << " " << (ch.l3Prot == CustomHeader::ProtTypeE::NACK) << " " << qp->m_qpid << " " << msg_seq << std::endl;
#endif

	LastPacketTag lastTag;
	bool isLastACK = p->PeekPacketTag(lastTag);

	if (Settings::CC_MIBO_NSDI22 == m_cc_mode){
		assert(isLastACK);
		MsgComplete(qp, msg);
		RefreshCredit(qp);
		return 0;
	}

	uint32_t nic_idx = GetNicIdxOfQp(qp);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	if (m_ack_interval == 0)
		std::cout << "ERROR: shouldn't receive ack\n";
	else if(Settings::use_irn){ // liuchang: for IRN
		msg->m_irn.AckIrnState(seq);
		msg->Acknowledge(seq, isLastACK);
		
		// check if have received all the packets
		if (msg->m_irn.GetBaseSeq() >= msg->m_size) {
			MsgComplete(qp, msg);
		}
	}else {
		if (!m_backto0){
			msg->Acknowledge(seq, isLastACK);
		}else {
			uint32_t goback_seq = seq / m_chunk * m_chunk;
			msg->Acknowledge(goback_seq, isLastACK);
		}

		if (msg->IsFinished()){
			MsgComplete(qp, msg);
		}
	}

	if (ch.l3Prot == CustomHeader::ProtTypeE::NACK && Settings::nack_reaction){ // NACK
		msg->Recover();
		if (!is_sending)
			qp->MoveRdmaOperationToSending(msg_seq);
		OnSACK(qp);
	}

	// wqy: for timeout retransmission
	if (Settings::rto_us != 0){
		msg->ResetLastActionTime();
		qp->ResetMSGRTOTime(msg_seq);
	}

	// handle cnp
	if (cnp){
		if (m_cc_mode == Settings::CC_DCQCN){ // mlx version
			cnp_received_mlx(qp);
		} 
	}

	if (m_cc_mode == Settings::CC_HPCC){
		HandleAckHp(qp, p, ch, qp_psn);
		//std::cout<<"ctrl size "<<p->GetSize()<<std::endl;
	}else if (m_cc_mode == Settings::CC_TIMELY){
		HandleAckTimely(qp, p, ch, qp_psn);
	}else if (m_cc_mode == Settings::CC_DCTCP){
		HandleAckDctcp(qp, p, ch, qp_psn);
	}else if (m_cc_mode == Settings::CC_SWIFT){
		HandleAckSwift(qp, p, ch, qp_psn);
	}

	qp->m_ack_psn = std::max(qp_psn, qp->m_ack_psn);
	// ACK may advance the on-the-fly window, allowing more packets to send
	dev->TriggerTransmit();
	return 0;
}

int RdmaHw::ReceiveSack(Ptr<Packet> p, CustomHeader &ch) {
	assert(Settings::use_irn);

	uint16_t qIndex = ch.sack.pg;
	uint16_t port = ch.sack.dport;
	uint32_t ackseq = ch.sack.irnAckSeq;
	uint32_t nackseq = ch.sack.irnNackSeq;
	uint8_t cnp = (ch.sack.flags >> qbbHeader::FLAG_CNP) & 1;
	int i;
	Ptr<RdmaSndQueuePair> qp = NULL;
	uint32_t msg_seq = 0;

	QPTag qptag;
	p->PeekPacketTag(qptag);
	qp = GetSndQp(port, qIndex, qptag.GetQPID());
	if (qp == NULL){
		std::cerr << "ERROR: " << "node:" << m_node->GetId() << ' ' << "SACK" << " NIC cannot find the flow\n";
		return 0;
	}

	msg_seq = qptag.GetMsgSeq();
	uint32_t qp_psn = qptag.GetQPPSN();
	bool is_sending = true;
	Ptr<RdmaSndOperation> msg = qp->GetMsg(msg_seq, is_sending);
	assert(!!msg || qp->m_finished_msgs.find(msg_seq) != qp->m_finished_msgs.end());		// sending or finished

	if (!msg) return 0;		// already finish
	
	// wqy
	if (Settings::quota_mode){
		UschTag uscht;
		if (p->PeekPacketTag(uscht)){
			UpdateQuota(uscht.GetPktSize(), true, !qp?-1:msg->m_dst);
		}
	}

#if DEBUG_MODE
	if (qp->m_qpid == DEBUG_QP)
		Settings::warning_out << "receive SACK "<< Simulator::Now() << " " << (!!cnp) << " " << seq << " " << "SACK" << " " << qp->m_qpid << " " << msg_seq << std::endl;
#endif

	LastPacketTag lastTag;
	bool isLastACK = p->PeekPacketTag(lastTag);

	if (Settings::CC_MIBO_NSDI22 == m_cc_mode){
		assert(isLastACK);
		MsgComplete(qp, msg);
		RefreshCredit(qp);
		return 0;
	}

	uint32_t nic_idx = GetNicIdxOfQp(qp);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	if (m_ack_interval == 0)
		std::cout << "ERROR: shouldn't receive ack\n";
	else { // Add retransmission packets and trigger transmitting
		msg->m_irn.SackIrnState(ackseq, nackseq);
		auto i = nackseq;
		for (; i < ackseq;) {
			msg->m_irn.m_rtxQueuingCnt++;
			msg->m_irn.m_rtxSeqQueues.push(i);
			i+=Settings::packet_payload;
		}
		if (!is_sending)
			qp->MoveRdmaOperationToSending(msg_seq);
		// dev->TriggerTransmit(); //TODO: 是否要加在这里，还是直接函数末尾加就行
		if (msg->m_irn.GetBaseSeq() >= msg->m_size){
			MsgComplete(qp, msg);
		}
	}
	
	// handle cnp
	if (cnp){
		if (m_cc_mode == Settings::CC_DCQCN){ // mlx version
			cnp_received_mlx(qp);
		} 
	}

	if (m_cc_mode == Settings::CC_HPCC){
		HandleAckHp(qp, p, ch, qp_psn);
		//std::cout<<"ctrl size "<<p->GetSize()<<std::endl;
	}else if (m_cc_mode == Settings::CC_TIMELY){
		HandleAckTimely(qp, p, ch, qp_psn);
	}else if (m_cc_mode == Settings::CC_DCTCP){
		HandleAckDctcp(qp, p, ch, qp_psn);
	}else if (m_cc_mode == Settings::CC_SWIFT){
		HandleAckSwift(qp, p, ch, qp_psn);
	}

	qp->m_ack_psn = std::max(qp_psn, qp->m_ack_psn);
	// ACK may advance the on-the-fly window, allowing more packets to send
	dev->TriggerTransmit();
	return 0;
}

int RdmaHw::Receive(Ptr<Packet> p, CustomHeader &ch){
	if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){ // UDP
		ReceiveUdp(p, ch);

		//for analysis, added by lkx
		QueueingTag q;
		if(p->RemovePacketTag(q)){
			++totalQueuingPackets;
			totalQueuingTimeUs += q.GetQueueingTimeUs();
		}
	}else if (ch.l3Prot == CustomHeader::ProtTypeE::QCN){ // CNP
		ctrl += p->GetSize();
		ReceiveCnp(p, ch);
	}else if (ch.l3Prot == CustomHeader::ProtTypeE::NACK ||ch.l3Prot == CustomHeader::ProtTypeE::ACK){ // NACK/ACK
		ctrl += p->GetSize();
		ReceiveAck(p, ch);
	}else if (ch.l3Prot == CustomHeader::ProtTypeE::TOKEN){ // Token
		ctrl += p->GetSize();
		ReceiveToken(p, ch);
	}else if (ch.l3Prot == CustomHeader::ProtTypeE::TOKEN_ACK){ // TokenACK
		ctrl += p->GetSize();
		ReceiveTokenACK(p, ch);
	}else if (ch.l3Prot == CustomHeader::ProtTypeE::REQ){
		ctrl += p->GetSize();
		ReceiveReq(p, ch);
	}else if (ch.l3Prot == CustomHeader::ProtTypeE::SACK){
		ctrl += p->GetSize();
		ReceiveSack(p, ch);
	}
	return 0;
}

int RdmaHw::ReceiverCheckSeq(uint32_t seq, Ptr<RdmaRxOperation> q, uint32_t size, uint32_t& NackSeq){
	if (Settings::use_irn) {
		uint32_t expected = q->m_irn.GetNextSequenceNumber();
		// in window
		if (seq < expected) {
			if(!q->m_irn.IsReceived(seq)) { // Not duplicated packet
				q->m_irn.UpdateIrnState(seq);
			}else{ // Duplicated packet
				Settings::duplicate_rtxCount ++;
			}
			return 1; // send ACK
		}else if (seq == expected) { // expected new packet
			q->m_irn.UpdateIrnState(seq);
			return 1; // send ACK
		}else{ // out of order
			NackSeq = expected;
			q->m_irn.UpdateIrnState(seq);
			return 2; // send SACK
		}
	}
	uint32_t expected = q->ReceiverNextExpectedSeq;
	if (seq == expected){
		q->ReceiverNextExpectedSeq = expected + size;
		if (q->ReceiverNextExpectedSeq >= q->m_milestone_rx){
			q->m_milestone_rx += m_ack_interval;
			return 1; //Generate ACK
		}else if (q->ReceiverNextExpectedSeq % m_chunk == 0){
			return 1;
		}else {
			return 5;
		}
	} else if (seq > expected) {
		Settings::warning_out << Simulator::Now() << " host-" << m_node->m_id << " UnexpectedSeq! rxQp:" << q->m_qpid
				<< " expectedSeq:" << expected << " currSeq:" << seq << std::endl;
		// Generate NACK
		if (Simulator::Now() >= q->m_nackTimer || q->m_lastNACK != expected){
			q->m_nackTimer = Simulator::Now() + MicroSeconds(m_nack_interval);
			q->m_lastNACK = expected;
			if (m_backto0){
				q->ReceiverNextExpectedSeq = q->ReceiverNextExpectedSeq / m_chunk*m_chunk;
			}
			return 2;
		}else
			return 4;
	}else {
		// Duplicate.
		return 3;
	}
}

void RdmaHw::AddHeader (Ptr<Packet> p, uint16_t protocolNumber){
	PppHeader ppp;
	ppp.SetProtocol (EtherToPpp (protocolNumber));
	p->AddHeader (ppp);
}

uint16_t RdmaHw::EtherToPpp (uint16_t proto){
	switch(proto){
		case 0x0800: return 0x0021;   //IPv4
		case 0x86DD: return 0x0057;   //IPv6
		default: NS_ASSERT_MSG (false, "PPP Protocol number not defined!");
	}
	return 0;
}

void RdmaHw::ResetQP(Ptr<RdmaSndQueuePair> qp){
	qp->SetWin(m_win_initial);
	qp->SetBaseRtt(m_baseRtt_initial);
	qp->SetVarWin(m_var_win);
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	DataRate m_bps = m_nic[nic_idx].dev->GetDataRate();

	qp->InitalCC(m_bps, m_multipleRate);

	// sender status
	if (m_cc_mode == Settings::CC_MIBO_NSDI22){
		qp->m_status = 0x00;
		qp->m_unschCount = 0;
		while (!qp->m_credits.empty())
			qp->m_credits.pop();
	}
	qp->PrintRate();
}

void RdmaHw::MsgComplete(Ptr<RdmaQueuePair> qp, Ptr<RdmaOperation> msg){
	if(qp->m_finished_msgs.find(msg->m_msgSeq) != qp->m_finished_msgs.end()){// this msg already finished
		return;
	}
	if (qp->is_sndQP)
		m_msgCompleteCallback(DynamicCast<RdmaSndOperation>(msg));
	else
		m_rcvFinishCallback(DynamicCast<RdmaRxOperation>(msg));

	if (!qp->RemoveRdmaOperation(msg->m_msgSeq)){
		std::cout << "ERROR: remove message\n";
	}
	qp->m_finished_msgs[msg->m_msgSeq] = msg;
	//liuchang: release a flow from a queue
	if(Settings::queue_allocate_in_host == 1 && qp->is_sndQP){
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		uint32_t qid = qp->GetQueueId();
		m_nic[nic_idx].dev->UpdateQueueforFlow(qid,false);
	}
}

//void RdmaHw::RcvFinish(Ptr<RdmaRxQueuePair> qp, Ptr<RdmaRxOperation> msg){
//	NS_ASSERT(!m_rcvFinishCallback.IsNull());
//	m_rcvFinishCallback(msg);
//	if (!qp->RemoveRdmaOperation(msg->m_msgSeq)){
//		std::cout << "ERROR: remove message\n";
//	}
//}

void RdmaHw::SetLinkDown(Ptr<QbbNetDevice> dev){
	printf("RdmaHw: node:%u a link down\n", m_node->GetId());
}

void RdmaHw::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx){
	uint32_t dip = dstAddr.Get();
	m_rtTable[dip].push_back(intf_idx);
	Settings::node_rtTable[m_node->GetId()][dip].push_back(intf_idx);
}

void RdmaHw::ClearTable(){
	m_rtTable.clear();
	Settings::node_rtTable[m_node->GetId()].clear();
}

void RdmaHw::RedistributeQp(){
	// clear old qpGrp
	for (uint32_t i = 0; i < m_nic.size(); i++){
		if (m_nic[i].dev == NULL)
			continue;
		m_nic[i].qpGrp->Clear();
	}

	// redistribute qp
	for (auto &it : m_qpMap){
		Ptr<RdmaSndQueuePair> qp = it.second;
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].qpGrp->AddQp(qp);
		//liuchang
		if(Settings::queue_allocate_in_host == 1){
			uint32_t qid = m_nic[nic_idx].dev->GetQueueforFlow();
			qp->SetQueueId(qid);
			m_nic[nic_idx].dev->UpdateQueueforFlow(qid,true);
		}
		// Notify Nic
		m_nic[nic_idx].dev->ReassignedQp(qp);
	}
}

bool RdmaHw::ShouldSndData(Ptr<RdmaQueuePair> qp){
	if (!qp->is_sndQP)	return false;
	Ptr<RdmaSndQueuePair> sndQP = DynamicCast<RdmaSndQueuePair>(qp);
	Ptr<RdmaSndOperation> msg = DynamicCast<RdmaSndOperation>(sndQP->Peek());
	if (!msg) return false;		// whether QP has data to send

	if (Settings::use_irn) {
		int rtx_count = 0;
		std::queue<uint32_t> rtxSeqQueues;
		while (msg->m_irn.m_rtxQueuingCnt > 0) {
			uint32_t seq = msg->m_irn.m_rtxSeqQueues.front();
			const auto state = msg->m_irn.GetIrnState (seq);

			if (state == RdmaSndOperation::NACK || state == RdmaSndOperation::UNACK) {
				rtx_count++;
				rtxSeqQueues.push(seq);
			}
			msg->m_irn.m_rtxSeqQueues.pop();
			msg->m_irn.m_rtxQueuingCnt--;
		}
		msg->m_irn.m_rtxSeqQueues = rtxSeqQueues;
		msg->m_irn.m_rtxQueuingCnt = rtxSeqQueues.size();

		if (msg->GetBytesLeft() == 0 && rtx_count == 0) {
			sndQP->MoveRdmaOperationToWaiting(msg->m_msgSeq);
			return false;
		}
	}

	if (m_l4TokenMode != RdmaEgressQueue::L4TokenNo){	// credit control: use credit or unsch quota
#if DEBUG_MODE
		if (qp->m_qpid == DEBUG_QP)
			Settings::warning_out << Simulator::Now() << " " << qp->m_qpid << " check sending data " << sndQP->m_credits.size() << std::endl;
#endif
		if (IsSch(sndQP)){
			RefreshCredit(sndQP);
			if (sndQP->m_credits.size() == 0) return false;
			uint32_t seq = msg->snd_nxt;
			uint32_t payload_size = msg->GetNxtPayload(seq, false);
			if (sndQP->m_credits.front()->GetRemain() >= payload_size)
				return true;
			return sndQP->m_credits.size() > 1;
		}else{
			return sndQP->m_unschCount < m_maxUnschCount * Settings::max_bdp;
		}
	}else{
		// check rate control
		if (sndQP->GetNextAvailT() > Simulator::Now())	return false;		// follow the rate control
		return !sndQP->IsWinBound(msg->GetOnTheFly());			// follow the window control
	}
}

Ptr<Packet> RdmaHw::GetNxtPacket(Ptr<RdmaQueuePair> qp){

	// only sender side sends data packets
	if (!qp->is_sndQP)	return NULL;

	Ptr<RdmaSndQueuePair> sndQP = DynamicCast<RdmaSndQueuePair>(qp);
	// select a msg
	Ptr<RdmaSndOperation> msg = DynamicCast<RdmaSndOperation>(sndQP->Peek());

	assert(!!msg);

	uint32_t seq;
	uint32_t payload_size;
	bool is_rtx = false;

	//first do retransmission
	if (Settings::use_irn && msg->m_irn.m_rtxQueuingCnt > 0) {
		while (msg->m_irn.m_rtxQueuingCnt > 0) {
			seq = msg->m_irn.m_rtxSeqQueues.front();
			const auto state = msg->m_irn.GetIrnState (seq);

			msg->m_irn.m_rtxSeqQueues.pop();
			msg->m_irn.m_rtxQueuingCnt--;

			if (state == RdmaSndOperation::NACK || state == RdmaSndOperation::UNACK) {
				payload_size = msg->m_irn.GetPayloadSize(seq);
				msg->m_irn.SetSeqState(seq, RdmaSndOperation::UNACK);
				is_rtx = true;
				break;
			}
		}
	}
	// send new packet
	if (is_rtx == false) {
		seq = msg->snd_nxt;
		payload_size = msg->GetNxtPayload(seq, true);
		// update for new packet send
		if (Settings::use_irn) {
			//check if irn_seq is synchronized with msg->snd_nxt (seq)
			const auto irn_seq = msg->m_irn.GetNextSequenceNumber();
			msg->m_irn.SendNewPacket(payload_size);
		}
	}

	Ptr<Packet> p = Create<Packet> (payload_size);
	// add SeqTsHeader
	SeqTsHeader seqTs;
	seqTs.SetSeq (seq);
	seqTs.SetSize(msg->m_size);
	seqTs.SetPG (msg->m_pg);
	if (m_cc_mode == Settings::CC_SWIFT){		// t1: t_sent
		seqTs.ih.sendTs = Simulator::Now().GetNanoSeconds();
	}
	p->AddHeader (seqTs);
	// add udp header
	UdpHeader udpHeader;
	udpHeader.SetDestinationPort (msg->dport);
	udpHeader.SetSourcePort (msg->sport);
	p->AddHeader (udpHeader);
	// add ipv4 header
	Ipv4Header ipHeader;
	ipHeader.SetSource (Ipv4Address(msg->sip));
	ipHeader.SetDestination (Ipv4Address(msg->dip));
	ipHeader.SetProtocol (CustomHeader::ProtTypeE::UDP);
	ipHeader.SetPayloadSize (p->GetSize());
	ipHeader.SetTtl (64);
	ipHeader.SetTos (0);
	ipHeader.SetIdentification (qp->m_ipid);
	p->AddHeader(ipHeader);
	// add ppp header
	PppHeader ppp;
	ppp.SetProtocol (0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
	p->AddHeader (ppp);

	//add queueing tag
	QueueingTag tag;
	p->AddPacketTag(tag);

	FlowTag ftag;
	ftag.setIndex(msg->m_flowId);
	p->AddPacketTag(ftag);

	QPTag qptag;
	qptag.SetQPID(msg->m_qpid);
	qptag.SetMsgSeq(msg->m_msgSeq);
	qptag.SetQPPSN(sndQP->m_nxt_psn);
	p->AddPacketTag(qptag);
	sndQP->m_nxt_psn += payload_size;

	if (Settings::quota_mode)
		AddUschTag(p, sndQP, msg->m_dst);

	if (seq + m_mtu >= msg->m_size){
		LastPacketTag lastTag;
		p->AddPacketTag(lastTag);
	}

	// tag scheduled packet
	bool tag_sch = false;
	if (Settings::flow_sch_th == INT_MAX){
		if (sndQP->m_ack_psn > 0 || sndQP->m_nxt_psn > sndQP->m_win) tag_sch = true;
	}else{
		if (sndQP->m_nxt_psn > Settings::flow_sch_th) tag_sch = true;
	}
	if (tag_sch){
		SchTag schTag;
		p->AddPacketTag(schTag);
	}

	// tag qos info
	if (Settings::qos_mode != Settings::QOS_NONE){
		p->AddPacketTag(msg->qosRankTag);
	}

	// tag routing info
	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION)
		p->AddPacketTag(msg->routingTag);

#if DEBUG_MODE
	if (msg->m_flowId == DEBUG_FLOW){
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		ch.getInt = 1; // parse INT header
		p->PeekHeader(ch);
		Settings::warning_out << Simulator::Now() << " send UDP " << qp->m_qpid
					<< " seq: " << seq << "	udp_seq: " << ch.udp.seq
					<< " size: " << msg->m_size << " udp_size: " << ch.udp.size
					<< " rate: " << qp->m_rate.GetBitRate()
					<< std::endl;
	}
#endif

	// update state
	if (seq == msg->snd_nxt) msg->snd_nxt += payload_size;		// to distinguish with retransmission
	sndQP->SetRRLast(msg);
	qp->m_ipid++;

	// wqy: for timeout retransmission
	if (Settings::rto_us != 0){
		msg->ResetLastActionTime();
		sndQP->ResetMSGRTOTime(msg->m_msgSeq);
	}

	if (m_l4TokenMode != RdmaEgressQueue::L4TokenNo)
		SendingData(sndQP, p);

	
	if (Settings::use_irn) {
		// Set up IRN timer
		const auto id = IrnTimer (sndQP, msg, seq);
		msg->m_irn.SetRtxEvent (seq, id);
	}

	/*
	 * If sent all packets, move msg into unfinished queue
	 * --> When !Settings::IsPacketLevelRouting(), only check qp->snd_nxt >= qp->m_size
	 * --> else, check qp->snd_nxt >= qp->m_size && (!qp->timeouted || qp->m_timeout_psn.empty())
	 */
	if (msg->GetBytesLeft() == 0){
		sndQP->MoveRdmaOperationToWaiting(msg->m_msgSeq);
		if (Settings::reset_qp_rate)
			ResetQP(sndQP);
	}
	return p;
}

ns3::EventId RdmaHw::IrnTimer(ns3::Ptr<ns3::RdmaSndQueuePair> qp, ns3::Ptr<ns3::RdmaSndOperation> msg, uint32_t irnSeq) {
	if (msg->m_irn.GetWindowSize () <= m_irn.rtoLowThreshold){
		return Simulator::Schedule (m_irn.rtoLow, &RdmaHw::IrnTimerHandler, this, qp, msg, irnSeq);
	}else{
		return Simulator::Schedule (m_irn.rtoHigh, &RdmaHw::IrnTimerHandler, this, qp, msg, irnSeq);
	}
}

void RdmaHw::IrnTimerHandler (ns3::Ptr<ns3::RdmaSndQueuePair> qp, ns3::Ptr<ns3::RdmaSndOperation> msg, uint32_t irnSeq) {
	const auto state = msg->m_irn.GetIrnState (irnSeq);
	if (state == RdmaSndOperation::NACK || state == RdmaSndOperation::UNACK)
	{	
		msg->m_irn.SetSeqState(irnSeq, RdmaSndOperation::NACK);
		msg->m_irn.m_rtxSeqQueues.push (irnSeq);
		msg->m_irn.m_rtxQueuingCnt++;
		// if this msg in waitting list, then move it to sending list
		bool is_sending = true;
		qp->GetMsg(msg->m_msgSeq, is_sending);
		if(is_sending == false)
			qp->MoveRdmaOperationToSending(msg->m_msgSeq);
		uint32_t nic_idx = GetNicIdxOfQp(qp);
		m_nic[nic_idx].dev->TriggerTransmit();
	}
}

void RdmaHw::SetUpIrn(uint32_t MaxBitmapSize, uint32_t rtoH, uint32_t rtoL, uint32_t rtoLowTh) {
	m_irn.maxBitmapSize = MaxBitmapSize;
	m_irn.rtoHigh = MicroSeconds(rtoH);
	m_irn.rtoLow = MicroSeconds(rtoL);
	m_irn.rtoLowThreshold = rtoLowTh;
}
	
void RdmaHw::PktSent(Ptr<RdmaQueuePair> qp, Ptr<Packet> pkt, Time interframeGap, bool is_tkn){
	if (is_tkn){
		if (m_cc_mode == Settings::CC_EP){
			SentCreditEP(qp);
		}
		UpdateNextTknAvail(qp, interframeGap);
	}else{
		if (qp->is_sndQP){
			Ptr<RdmaSndQueuePair> sndQP = DynamicCast<RdmaSndQueuePair>(qp);
			sndQP->lastPktSize = pkt->GetSize();
			UpdateNextAvail(sndQP, interframeGap, sndQP->lastPktSize);
		}
	}
}

void RdmaHw::UpdateNextAvail(Ptr<RdmaSndQueuePair> qp, Time interframeGap, uint32_t pkt_size){
	Time sendingTime;
	if (m_rateBound)
		sendingTime = interframeGap + Seconds(qp->m_rate.CalculateTxTime(pkt_size));
	else
		sendingTime = interframeGap + Seconds(qp->m_max_rate.CalculateTxTime(pkt_size));
	qp->m_nextAvail = Simulator::Now() + sendingTime;
}

void RdmaHw::ChangeRate(Ptr<RdmaQueuePair> qp, DataRate new_rate){

	if (qp->is_sndQP){
		// data rate
		Ptr<RdmaSndQueuePair> sndQP = DynamicCast<RdmaSndQueuePair>(qp);
		sndQP->m_nextAvail = sndQP->m_nextAvail + Seconds(new_rate.CalculateTxTime(sndQP->lastPktSize)) - Seconds(sndQP->m_rate.CalculateTxTime(sndQP->lastPktSize));
		Time t_nxt = sndQP->m_nextAvail;

		// token rate
		if (m_l4TokenMode == RdmaEgressQueue::L4TokenSrcRateBased){
			qp->m_nextTknAvail = qp->m_nextTknAvail + Seconds(new_rate.CalculateTxTime(Settings::MTU)) - Seconds(qp->m_rate.CalculateTxTime(Settings::MTU));
			t_nxt = Min(qp->m_nextTknAvail, t_nxt);
		}

		// update nic's next avail event
		uint32_t nic_idx = GetNicIdxOfQp(sndQP);
		m_nic[nic_idx].dev->UpdateNextAvail(t_nxt);

#if DEBUG_MODE
			if (sndQP->m_qpid == DEBUG_QP)
				Settings::warning_out << Simulator::Now() << "sndQP ChangeRate " << sndQP->m_rate << "->" << new_rate
							<< " nxtTkn:" << sndQP->m_nextTknAvail
							<< " nxtData:" << sndQP->m_nextAvail
							<< " lstPkt:" << sndQP->lastPktSize << std::endl;
#endif

		// change to new rate
		sndQP->m_rate = new_rate;

		sndQP->PrintRate();
	}else{
		// token rate
		if (m_l4TokenMode == RdmaEgressQueue::L4TokenDstRateBased || m_l4TokenMode == RdmaEgressQueue::L4TokenDstDataDriven){
			Ptr<RdmaRxQueuePair> rxQP = DynamicCast<RdmaRxQueuePair>(qp);
			rxQP->m_nextTknAvail = rxQP->m_nextTknAvail + Seconds(new_rate.CalculateTxTime(Settings::MTU + 84)) - Seconds(rxQP->m_rate.CalculateTxTime(Settings::MTU + 84));
			// update nic's next avail event
			uint32_t nic_idx = GetNicIdxOfRxQp(rxQP);
			m_nic[nic_idx].dev->UpdateNextAvail(rxQP->m_nextTknAvail);

#if DEBUG_MODE
			if (rxQP->m_qpid == DEBUG_QP)
				Settings::warning_out << Simulator::Now() << "rxQP ChangeRate " << rxQP->m_nextTknAvail << std::endl;
#endif

			// change to new rate
			rxQP->m_rate = new_rate;

			rxQP->PrintRate();
		}
	}
}

//added
void RdmaHw::BW(){
	double total = Settings::host_bw_Bps * Settings::bw_interval/1000000.0;//the unit of BW_INTERVAL is us
	uint32_t hostId = m_node->GetId();
	Settings::bwList[hostId].push_back(bw / total);
	Settings::tpList[hostId].push_back(tp / total);
	Settings::ctrlList[hostId].push_back(ctrl / total);
	if(totalQueuingPackets == 0)//do not receive a packet at this time
		Settings::QDelayList[hostId].push_back(0);
	else
		Settings::QDelayList[hostId].push_back(totalQueuingTimeUs/totalQueuingPackets);

	uint32_t credit_sum = 0;
	if (RdmaEgressQueue::L4TokenNo != m_l4TokenMode){
		std::unordered_map<uint64_t, Ptr<RdmaSndQueuePair> >::iterator it = m_qpMap.begin();
		while (it != m_qpMap.end()){
			credit_sum += it->second->m_credits.size();
			it++;
		}
	}

	if (Settings::bw_out.is_open())
		Settings::bw_out << hostId << " " << Simulator::Now() << " " << ctrl+bw << " " << bw << " " << tp
						<< " "  << credit_sum
						<< std::endl;

//#if DEBUG_MODE
//	std::cout << hostId << " " << Simulator::Now() << " " << ctrl+bw << " " << bw << " " << tp << std::endl;
//#endif

	bw = 0;
	tp = 0;
	ctrl = 0;
	totalQueuingPackets = 0;
	totalQueuingTimeUs = 0;
	bwId = Simulator::Schedule(MicroSeconds(Settings::bw_interval), &RdmaHw::BW, this);
}

/*------------------------------------Proactive CC(start)------------------------------------*/

/**
 * Send REQ packet
 * to notify receiver that a new flow arrivals
 */
void RdmaHw::SendReq(Ptr<RdmaSndQueuePair> sndQP, Ptr<RdmaSndOperation> msg){
	Ptr<Packet> p = Create<Packet>(std::max(84-(int)CustomHeader::GetAckSerializedSize()-20-38, 0));
	// add l4 header
	qbbHeader seqh;
	seqh.SetPG(Settings::DEFAULT_ACK);	// use ctrl priority by default
	seqh.SetSport(msg->sport);
	seqh.SetDport(msg->dport);
	seqh.SetSeq(msg->m_size);
	p->AddHeader (seqh);
	// add ipv4 header
	Ipv4Header ipHeader;
	ipHeader.SetSource (Ipv4Address(msg->sip));
	ipHeader.SetDestination (Ipv4Address(msg->dip));
	ipHeader.SetProtocol (CustomHeader::ProtTypeE::REQ);
	ipHeader.SetPayloadSize (p->GetSize());
	ipHeader.SetTtl (64);
	ipHeader.SetTos (0);
	ipHeader.SetIdentification (sndQP->m_ipid++);
	p->AddHeader(ipHeader);
	// add ppp header
	PppHeader ppp;
	ppp.SetProtocol (0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
	p->AddHeader (ppp);

	FlowTag ftag;
	ftag.setIndex(msg->m_flowId);
	p->AddPacketTag(ftag);

	QPTag qptag;
	qptag.SetQPID(msg->m_qpid);
	qptag.SetMsgSeq(msg->m_msgSeq);
	p->AddPacketTag(qptag);

	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
		p->AddPacketTag(msg->routingTag);
	}
	// send
	uint32_t nic_idx = GetNicIdxOfQp(sndQP);
	m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(p);
	m_nic[nic_idx].dev->TriggerTransmit();
}

/**
 * Receiver receives a REQ packet
 * -> register corresponding RdmaRxOperation and RdmaRxQP if necessary
 */
void RdmaHw::ReceiveReq(Ptr<Packet> p, CustomHeader &ch){

	FlowTag ftag;
	p->PeekPacketTag(ftag);

	QPTag qptag;
	p->PeekPacketTag(qptag);

#if DEBUG_MODE
	if (qptag.GetQPID() == DEBUG_QP)
		Settings::warning_out << Simulator::Now() << " " << qptag.GetQPID() << " ReceiveReq " << std::endl;
#endif

	// create RxQP and RxMsg
	Ptr<RdmaRxQueuePair> rxQp = GetRxQp(ch.dip, ch.sip, ch.req.dport, ch.req.sport, Settings::MIBO_DATA, ftag.getIndex(), qptag.GetQPID(), true);
	Ptr<RdmaRxOperation> rxMsg = rxQp->GetRxMSG(qptag.GetMsgSeq());
	if (!rxMsg && rxQp->m_finished_msgs.find(qptag.GetMsgSeq()) == rxQp->m_finished_msgs.end()){
		rxMsg = Create<RdmaRxOperation>();
		rxMsg->dip = ch.sip;
		rxMsg->sip = ch.dip;
		rxMsg->dport = ch.udp.sport;
		rxMsg->sport = ch.udp.dport;
		rxMsg->m_size = ch.udp.size;
		rxMsg->m_flowId = ftag.getIndex();
		rxMsg->m_msgSeq = qptag.GetMsgSeq();
		rxMsg->m_milestone_rx = m_ack_interval;
		rxQp->AddRdmaOperation(rxMsg);
#if DEBUG_MODE
		if (rxQp->m_qpid == DEBUG_QP)
			Settings::warning_out << Simulator::Now() << " " << rxQp->m_qpid << " size: " << rxQp->m_total_size << std::endl;
#endif
	}

	// notify Nic to send (token) packet
	m_nic[GetNicIdxOfRxQp(rxQp)].dev->TriggerTransmit();
}

bool RdmaHw::ShouldSndTkn(Ptr<RdmaQueuePair> qp){

	switch (m_l4TokenMode){
	case RdmaEgressQueue::L4TokenNo:
			return false;
	case RdmaEgressQueue::L4TokenSrcRateBased:{
		if (qp->is_sndQP){
			Ptr<RdmaSndQueuePair> sndQP = DynamicCast<RdmaSndQueuePair>(qp);

			// no sending messages (lossless network)
			if (!sndQP->Peek())	// todo: no sending messages or no unfinished messages(qp->GetMsgNumber() > 0)?
				return false;

			break;
		}else
			return false;
	}
	case RdmaEgressQueue::L4TokenDstDataDriven:
	case RdmaEgressQueue::L4TokenDstRateBased:{
		if (!qp->is_sndQP){
			Ptr<RdmaRxQueuePair> rxQP = DynamicCast<RdmaRxQueuePair>(qp);

//#if DEBUG_MODE
//			if (rxQP->m_qpid == DEBUG_QP)
//				Settings::warning_out << Simulator::Now() << " " << rxQP->m_qpid << " check sending tkn " << qp->m_nextTknAvail
//				<< " " << rxQP->GetUnfinishedNum() << std::endl;
//#endif

			// all flows have finished
			if (rxQP->GetUnfinishedNum() == 0)
				return false;

			if (Settings::CC_EP == m_cc_mode){
				CreditFeedback(rxQP);
			}

			// TokenDstDataDriven should check token left
			if (RdmaEgressQueue::L4TokenDstDataDriven == m_l4TokenMode
					&& rxQP->m_tkn_max/Settings::packet_payload < rxQP->m_tkn_nxt)
				return false;

			break;
		}else
			return false;
	}
	default:
		return false;
	}

//#if DEBUG_MODE
//		if (qp->m_qpid == DEBUG_QP){
//			Settings::warning_out << Simulator::Now() << " " << qp->m_qpid
//					<< " tkn_nxt:" << qp->m_tkn_nxt << " tkn_ack:" << qp->m_tkn_ack
//					<< " win: " << qp->GetWin()
//					<< " limit:" << m_limit_tkn_mode
//					<< " total_size: " << qp->m_total_size
//					<< " total_tkn:" << (qp->m_total_size - 1)/Settings::packet_payload
//					<< std::endl;
//			// assert(false);
//		}
//#endif

	// no token window left
	if (qp->IsWinBound(m_mtu*(qp->m_tkn_nxt-qp->m_tkn_ack))){	// todo: a setting for token window ?
		return false;
	}

	// if we know flow size, we can check the total number of token to avoid token/bandwidth waste
	uint32_t max_tkn_nxt = (qp->m_total_size - 1)/Settings::packet_payload;	// -1: flow with size of n*1460 will only send n tokens
	if (m_limit_tkn_mode == Settings::LIMIT_TKN_EXCLUDE_UNSCH && qp->m_sch_start != INT_MAX){
		max_tkn_nxt -= qp->m_sch_start/Settings::packet_payload;
	}
	if (m_limit_tkn_mode != Settings::NO_LIMIT_TKN && qp->m_tkn_nxt > max_tkn_nxt)
		return false;

	return qp->m_nextTknAvail.GetTimeStep() <= Simulator::Now().GetTimeStep();
}

/**
 * token's control unit is QP, in which way, messages in a same QP can share token_ack
 */
Ptr<Packet> RdmaHw::GetTknPacket(Ptr<RdmaQueuePair> qp){
	Ptr<Packet> p = Create<Packet>(std::max(84-(int)CustomHeader::GetAckSerializedSize()-20-38, 0));
	// add l4 header
	qbbHeader seqh;
	if (Settings::CC_EP)
		seqh.SetPG(Settings::EP_CREDIT);
	else if (Settings::CC_MIBO_NSDI22)
		seqh.SetPG(Settings::MIBO_TOKEN);
	seqh.SetSeq(qp->m_tkn_nxt++);
	p->AddHeader (seqh);
	// add ipv4 header
	Ipv4Header ipHeader;
	ipHeader.SetSource (Ipv4Address(qp->sip));
	ipHeader.SetDestination (Ipv4Address(qp->dip));
	ipHeader.SetProtocol (CustomHeader::ProtTypeE::TOKEN);
	ipHeader.SetPayloadSize (p->GetSize());
	ipHeader.SetTtl (64);
	ipHeader.SetTos (0);
	ipHeader.SetIdentification (qp->m_ipid++);
	p->AddHeader(ipHeader);
	// add ppp header
	PppHeader ppp;
	ppp.SetProtocol (0x0021); // EtherToPpp(0x800), see point-to-point-net-device.cc
	p->AddHeader (ppp);

	FlowTag ftag;
	ftag.setIndex(qp->Peek()->m_flowId);
	p->AddPacketTag(ftag);

	QPTag qptag;
	qptag.SetQPID(qp->m_qpid);
	qptag.SetMsgSeq(qp->Peek()->m_msgSeq);
	p->AddPacketTag(qptag);

	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
		p->AddPacketTag(CalcuAllRoutingHops(m_node->m_id, GetNicIdxOfQp(qp), qp->sip, qp->dip, 0, 0, qptag.GetQPID()));
	}
//#if DEBUG_MODE
//	if (qp->m_qpid == DEBUG_QP)
//		Settings::warning_out << Simulator::Now() << " " << qp->m_qpid << " send tkn " << std::endl;
//#endif
	return p;
}

/**
 * rate-based token
 */
void RdmaHw::UpdateNextTknAvail(Ptr<RdmaQueuePair> qp, Time interframeGap){
	Time sendingTime;
	if (m_rateBound)
		sendingTime = interframeGap + Seconds(qp->m_rate.CalculateTxTime(Settings::MTU + 84));	// token:data ratio is 1:MTU
	else
		sendingTime = interframeGap + Seconds(qp->m_max_rate.CalculateTxTime(Settings::MTU + 84));
	qp->m_nextTknAvail = Simulator::Now() + sendingTime;
	// if (Settings::CC_EP == m_cc_mode){
		qp->m_nextTknAvail += NanoSeconds(rand()%50);
	// }
//#if DEBUG_MODE
//	if (qp->m_qpid == DEBUG_QP)
//		Settings::warning_out << Simulator::Now() << " reset nextTknAvail " << qp->m_nextTknAvail << std::endl;
//#endif
}

void RdmaHw::StoreCredit(Ptr<RdmaSndQueuePair> qp, CustomHeader &ch, bool ecn){
	uint16_t port = ch.tkn.dport;

	// add token_ack into credit table and turn to scheduled phase
	Ptr<Credit> curr = Create<Credit>(Simulator::Now(), ch.tkn.seq);
	curr->SetECN(ecn);
	qp->m_credits.push(curr);
	if (m_turn_sch_phase_mode == Settings::RCV_TKN)	TurnToSch(qp);

	// token_ack may allow more packets to send
	uint32_t nic_idx = GetNicIdxOfQp(qp);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	dev->TriggerTransmit();
}

bool RdmaHw::IsSch(Ptr<RdmaSndQueuePair> qp){
	if (m_maxUnschCount == 0) return true;	// when maxUnschCount == 0, no unsch phase
	return (qp->m_status & RdmaSndQueuePair::IS_SCH) == RdmaSndQueuePair::IS_SCH;
}

void RdmaHw::TurnToUnsch(Ptr<RdmaSndQueuePair> qp){
	qp->m_status &= ~RdmaSndQueuePair::IS_SCH;

	// clean counter and last unsch phase's unsch buffer
	qp->m_unschCount = 0;	// reset successive unsch packet counter

}

void RdmaHw::TurnToSch(Ptr<RdmaSndQueuePair> qp){
	qp->m_status |= RdmaSndQueuePair::IS_SCH;
}

/**
 * When receive token
 * L4TokenSrcRateBased: generate a credit(carry back ECN) and send out
 * L4TokenDstRateBased/L4TokenDstDataDriven: store credit
 */
void RdmaHw::ReceiveToken(Ptr<Packet> p, CustomHeader &ch){
	uint8_t ecnbits = ch.GetIpv4EcnBits();		// tagged CNP by switch

	FlowTag ftag;
	p->PeekPacketTag(ftag);

	QPTag qptag;
	p->PeekPacketTag(qptag);

	if (RdmaEgressQueue::L4TokenSrcRateBased == m_l4TokenMode){
		/*
		 * src rate-based token:
		 * this is receiver
		 * -> send token_ack back to sender (carry CNP)
		 */
		Ptr<RdmaRxQueuePair> rxQp = GetRxQp(ch.dip, ch.sip, ch.tkn.dport, ch.tkn.sport, Settings::MIBO_DATA, ftag.getIndex(), qptag.GetQPID(), true);
		Ptr<RdmaRxOperation> rxMsg = rxQp->GetRxMSG(qptag.GetMsgSeq());
		if (!rxMsg && rxQp->m_finished_msgs.find(qptag.GetMsgSeq()) == rxQp->m_finished_msgs.end()){
			rxMsg = Create<RdmaRxOperation>();
			rxMsg->dip = ch.sip;
			rxMsg->sip = ch.dip;
			rxMsg->dport = ch.udp.sport;
			rxMsg->sport = ch.udp.dport;
			rxMsg->m_size = ch.udp.size;
			rxMsg->m_flowId = ftag.getIndex();
			rxMsg->m_msgSeq = qptag.GetMsgSeq();
			rxMsg->m_milestone_rx = m_ack_interval;
			rxQp->AddRdmaOperation(rxMsg);
//#if DEBUG_MODE
//		if (rxQp->m_qpid == DEBUG_QP)
//			Settings::warning_out << Simulator::Now() << " " << rxQp->m_qpid << " newed size: " << rxQp->m_total_size << std::endl;
//#endif
		}

		Ptr<Packet> newp = Create<Packet>(std::max(84-(int)ch.GetSerializedSize(), 0));
		// add l4 header
		qbbHeader seqh;
		seqh.SetPG(Settings::MIBO_TOKENACK);
		seqh.SetSport(ch.tkn.dport);
		seqh.SetDport(ch.tkn.sport);
		seqh.SetSeq(ch.tkn.seq);
		if (ecnbits)
			seqh.SetCnp();
		newp->AddHeader(seqh);
		// add ipv4 header: 20 bytes
		Ipv4Header head;	// Prepare IPv4 header
		head.SetDestination(Ipv4Address(ch.sip));
		head.SetSource(Ipv4Address(ch.dip));
		head.SetProtocol(CustomHeader::ProtTypeE::TOKEN_ACK);
		head.SetTtl(64);
		head.SetPayloadSize(newp->GetSize());
		head.SetIdentification(rxQp->m_ipid++);
		newp->AddHeader(head);
		// add ppp header
		AddHeader(newp, 0x800);

		newp->AddPacketTag(qptag);
		newp->AddPacketTag(ftag);

		uint32_t nic_idx = GetNicIdxOfRxQp(rxQp);
		if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
			newp->AddPacketTag(CalcuAllRoutingHops(m_node->m_id, nic_idx, ch.dip, ch.sip, ch.tkn.dport, ch.tkn.sport, qptag.GetQPID()));
		}

		// send
		m_nic[nic_idx].dev->RdmaEnqueueHighPrioQ(newp);
		m_nic[nic_idx].dev->TriggerTransmit();
	}else if (RdmaEgressQueue::L4TokenDstRateBased == m_l4TokenMode || RdmaEgressQueue::L4TokenDstDataDriven == m_l4TokenMode){
		/*
		 * dst rate-based/data-driven token:
		 * this is sender
		 * -> store token
		 */
		uint16_t port = ch.tkn.dport;
		Ptr<RdmaSndQueuePair> qp = NULL;
		uint32_t msg_seq = 0;

		qp = GetSndQp(port, Settings::MIBO_DATA, qptag.GetQPID());
		if (qp == NULL){
			std::cerr << "ERROR: " << "node:" << m_node->GetId() << " TokenACK NIC cannot find the flow\n";
			return;
		}

//#if DEBUG_MODE
//		if (qp->m_qpid == DEBUG_QP)
//			std::cout << Simulator::Now() << " " << qp->m_qpid << " receive tkn "<< std::endl;
//#endif

		/*
		 * -> if ecn marked:
		 * -> sender-rateBased: send CNP to receiver
		 * -> receiver-xx: rate control
		 */
		if (ecnbits && m_token_cc == Settings::CC_DCQCN){
			switch (m_l4TokenMode){
			case RdmaEgressQueue::L4TokenDstDataDriven:
			case RdmaEgressQueue::L4TokenDstRateBased:
				if (Settings::host_cnp == Settings::ADDITION_CNP)
					SendCnp(qp, ch, ecnbits);
				break;
			case RdmaEgressQueue::L4TokenSrcRateBased:
				cnp_received_mlx(qp);
				break;
			}
		}

		StoreCredit(qp, ch, ecnbits);
	}
}

/**
 * sending a data packet
 * -> remove a credit or use a unsch win
 */
void RdmaHw::SendingData(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> pkt){
	if (IsSch(qp)){
		assert(!qp->m_credits.empty());
		uint32_t payload_size = pkt->GetSize()-CustomHeader::GetUdpHeaderSize() - 20 - 38;
		assert(pkt->GetSize() > CustomHeader::GetUdpHeaderSize() + 20 + 38);

		uint32_t last_tkn_psn = qp->m_credits.front()->GetPSN();
		bool ecn = qp->m_credits.front()->GetECN();
		while (payload_size > 0){
			if (0 == qp->m_credits.front()->UseRemain(payload_size)){
				last_tkn_psn = qp->m_credits.front()->GetPSN();
				ecn |= qp->m_credits.front()->GetECN();
				qp->m_credits.pop();
			}
		}

		if (m_l4TokenMode == RdmaEgressQueue::L4TokenDstDataDriven || m_l4TokenMode == RdmaEgressQueue::L4TokenDstRateBased){
			// for receiver-driven token mode
			TokenTag tkntag;
			tkntag.SetPSN(last_tkn_psn);
			pkt->AddPacketTag(tkntag);

			if (ecn && Settings::host_cnp == Settings::PIGGY_CNP){
				PppHeader ppp;
				Ipv4Header h;
				pkt->RemoveHeader(ppp);
				pkt->RemoveHeader(h);
				h.SetEcn((Ipv4Header::EcnType)0x03);
				pkt->AddHeader(h);
				pkt->AddHeader(ppp);
			}
		}
	}else{
		qp->m_unschCount += pkt->GetSize();
		if (m_maxUnschCount * Settings::max_bdp <= qp->m_unschCount)
			TurnToSch(qp);
	}
}

#define PRINT_LOG 0		// show log of CC for debug

/****************
 * MIBO-NSDI22
 *****************/
/**
 * DESIGN OF TOKEN_ACK IS NOT USED CURRENTLY!!!
 * When receive token_ack
 * MIBO_NSDI22: store in createTable, and get CNP carried by token_ack and adjust m_rate
 */
void RdmaHw::ReceiveTokenACK(Ptr<Packet> p, CustomHeader &ch){
	assert(RdmaEgressQueue::L4TokenSrcRateBased == m_l4TokenMode);
	uint16_t port = ch.tkn.dport;
	uint8_t cnp = (ch.tkn.flags >> qbbHeader::FLAG_CNP) & 1;		// carry CNP in L4 header

	Ptr<RdmaSndQueuePair> qp = NULL;
	uint32_t msg_seq = 0;

	QPTag qptag;
	p->PeekPacketTag(qptag);
	qp = GetSndQp(port, Settings::MIBO_DATA, qptag.GetQPID());
	if (qp == NULL){
		std::cerr << "ERROR: " << "node:" << m_node->GetId() << " TokenACK NIC cannot find the flow\n";
		return;
	}

	qp->m_tkn_ack = std::max(ch.tkn.seq+1, qp->m_tkn_ack);
	if (cnp && m_token_cc == Settings::CC_DCQCN){
		cnp_received_mlx(qp);
	}

	StoreCredit(qp, ch, false);
}

/**
 * MIBO-NSDI22
 * lkx: added for faster increase (change of logic of DCQCN CC when applying it to proactive methods)
*/
void RdmaHw::MIBOIncreaseMlx(Ptr<RdmaQueuePair> q){
	// get NIC
	uint32_t nic_idx = GetNicIdxOfQp(q);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	//increase rate
	q->mlx.m_targetRate += m_rhai;
	if (q->mlx.m_targetRate > dev->GetDataRate())
		q->mlx.m_targetRate = dev->GetDataRate();
	//q->m_rate = q->m_rate * q->mlx.m_alpha + q->mlx.m_targetRate * (1 - q->mlx.m_alpha);
	//q->PrintRate();
	ChangeRate(q, q->m_rate * q->mlx.m_alpha + q->mlx.m_targetRate * (1 - q->mlx.m_alpha));
}

/**
 * clean out-of-date credits
 * -> based on linear probability
 * todo: may need optimize
 */
void RdmaHw::RefreshCredit(Ptr<RdmaSndQueuePair> qp){
	while (!qp->m_credits.empty()){
		Ptr<Credit> credit = qp->m_credits.front();
		int64_t interval = (Simulator::Now()-credit->GetTime()).GetMicroSeconds();
		if (interval < m_credit_Emin)
			break;
		else if (interval > m_credit_Emax){
			qp->m_credits.pop();	// credit expired
			++mibo_expired_token;

		}else{
			double p_rnd = rand()%100/100.0;
			double p = m_credit_Ep / (m_credit_Emax-m_credit_Emin)*interval;
			if (p_rnd < p) {	// credit expired
				qp->m_credits.pop();
				++mibo_expired_token;
			}
			else break;
		}
	}

	/*
	 * under unsch ctrl:
	 * If all credits are expired or used and no unfinished messages
	 * -> qp turns to unsch status
	 */
	if (qp->m_credits.empty() && qp->GetMsgNumber() == 0){
		TurnToUnsch(qp);
	}

}

/**************
 * ExpressPass
 **************/

void RdmaHw::ReceiveDataEP(Ptr<RdmaQueuePair> qp, CustomHeader &ch, uint32_t psn){
	while(qp->m_sent_credits.front()->GetPSN() != psn){
		qp->m_sent_credits.pop();
		++qp->ep.m_rttDroppedCredit;
		++qp->ep.m_rttSentCredit;
	}
	assert(qp->m_sent_credits.front()->GetPSN() == psn);		// must has no disordered packets
	double r = Simulator::Now().GetSeconds() - qp->m_sent_credits.front()->GetTime().GetSeconds();
	qp->m_sent_credits.pop();
	++qp->ep.m_rttSentCredit;
	if(qp->ep.m_rtt==0) qp->ep.m_rtt = r;
	else qp->ep.m_rtt = qp->ep.m_rtt*0.8 + r*0.2;
}

void RdmaHw::SentCreditEP(Ptr<RdmaQueuePair> qp){
	// restore credit sending time
	Ptr<Credit> cdt = Create<Credit>(Simulator::Now(), qp->m_tkn_nxt-1);
	qp->m_sent_credits.push(cdt);
}

void RdmaHw::CreditFeedback(Ptr<RdmaQueuePair> qp) {
	if(qp->ep.m_rtt == 0) return;
	if(qp->ep.m_lastUpdate + Seconds(qp->ep.m_rtt) > Simulator::Now()) return;
	if(qp->ep.m_rttSentCredit == 0) return;

	double old_rate = qp->ep.m_curRate;
	double loss_rate = qp->ep.m_rttDroppedCredit/(double)qp->ep.m_rttSentCredit;
	double target_loss = (1 - qp->ep.m_curRate/qp->ep.m_max_rate) * 0.125;
	double min_rate = 1/qp->ep.m_rtt;

	if(loss_rate > target_loss){
		if(loss_rate >= 1)
			qp->ep.m_curRate = min_rate;
		else
			qp->ep.m_curRate = (qp->ep.m_rttSentCredit-qp->ep.m_rttDroppedCredit)/qp->ep.m_rtt*(1+target_loss);
		if(qp->ep.m_curRate > old_rate)
			qp->ep.m_curRate = old_rate;
		qp->ep.m_weight = std::max(qp->ep.m_weight/2, m_ep_wmin);
		qp->ep.m_rttPreIncreasing = false;
	}else{
		if(qp->ep.m_rttPreIncreasing)
			qp->ep.m_weight = std::max(qp->ep.m_weight+0.05, m_ep_wmax);
		else
			qp->ep.m_rttPreIncreasing = true;
		if(qp->ep.m_curRate < qp->ep.m_max_rate)
			qp->ep.m_curRate = qp->ep.m_weight*qp->ep.m_max_rate + (1-qp->ep.m_weight)*qp->ep.m_curRate;
	}
	if(qp->ep.m_curRate > qp->ep.m_max_rate)
		qp->ep.m_curRate = qp->ep.m_max_rate;
	if(qp->ep.m_curRate < min_rate)
		qp->ep.m_curRate = min_rate;

	qp->ep.m_rttSentCredit = qp->ep.m_rttDroppedCredit = 0;
	qp->ep.m_lastUpdate = Simulator::Now();

	if (old_rate != qp->ep.m_curRate){
		ChangeRate(qp, DataRate(qp->ep.m_curRate * (Settings::MTU + 84) * 8));		// credits per seconds -> data bandwidth bps
	}

}
/*------------------------------------Proactive CC(end)------------------------------------*/

/*------------------------------------Reactive CC(start)------------------------------------*/
/******************************
 * Mellanox's version of DCQCN
 *****************************/
void RdmaHw::UpdateAlphaMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	//std::cout << Simulator::Now() << " alpha update:" << m_node->GetId() << ' ' << q->mlx.m_alpha << ' ' << (int)q->mlx.m_alpha_cnp_arrived << '\n';
	//printf("%lu alpha update: %08x %08x %u %u %.6lf->", Simulator::Now().GetTimeStep(), q->sip.Get(), q->dip.Get(), q->sport, q->dport, q->mlx.m_alpha);
	#endif
	if (q->mlx.m_alpha_cnp_arrived){
		q->mlx.m_alpha = (1 - m_g)*q->mlx.m_alpha + m_g; 	//binary feedback
	}else {
		q->mlx.m_alpha = (1 - m_g)*q->mlx.m_alpha; 	//binary feedback
	}
	#if PRINT_LOG
	//printf("%.6lf\n", q->mlx.m_alpha);
	#endif
	q->mlx.m_alpha_cnp_arrived = false; // clear the CNP_arrived bit
	ScheduleUpdateAlphaMlx(q);
}
void RdmaHw::ScheduleUpdateAlphaMlx(Ptr<RdmaQueuePair> q){
	q->mlx.m_eventUpdateAlpha = Simulator::Schedule(MicroSeconds(m_alpha_resume_interval), &RdmaHw::UpdateAlphaMlx, this, q);
}

void RdmaHw::cnp_received_mlx(Ptr<RdmaQueuePair> q){
	q->mlx.m_alpha_cnp_arrived = true; // set CNP_arrived bit for alpha update
	q->mlx.m_decrease_cnp_arrived = true; // set CNP_arrived bit for rate decrease
	if (q->mlx.m_first_cnp){
		// init alpha
		q->mlx.m_alpha = 1;
		q->mlx.m_alpha_cnp_arrived = false;
		// schedule alpha update
		ScheduleUpdateAlphaMlx(q);
		// schedule rate decrease
		ScheduleDecreaseRateMlx(q, 1); // add 1 ns to make sure rate decrease is after alpha update
		// set rate on first CNP
		q->mlx.m_targetRate = m_rateOnFirstCNP * q->m_rate;
		ChangeRate(q, q->mlx.m_targetRate);
		q->mlx.m_first_cnp = false;
	}
}

void RdmaHw::CheckRateDecreaseMlx(Ptr<RdmaQueuePair> q){
	ScheduleDecreaseRateMlx(q, 0);
	if (q->mlx.m_decrease_cnp_arrived){
		#if PRINT_LOG
		printf("%lu rate dec: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip, q->dip, q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
		#endif
		bool clamp = true;
		if (!m_EcnClampTgtRate){
			if (q->mlx.m_rpTimeStage == 0)
				clamp = false;
		}
		if (clamp)
			q->mlx.m_targetRate = q->m_rate;
		ChangeRate(q, std::max(m_minRate, q->m_rate * (1 - q->mlx.m_alpha / 2)));
		// reset rate increase related things
		q->mlx.m_rpTimeStage = 0;
		q->mlx.m_decrease_cnp_arrived = false;
		Simulator::Cancel(q->mlx.m_rpTimer);
		q->mlx.m_rpTimer = Simulator::Schedule(MicroSeconds(m_rpgTimeReset), &RdmaHw::RateIncEventTimerMlx, this, q);
		#if PRINT_LOG
		printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
		#endif
	}
}
void RdmaHw::ScheduleDecreaseRateMlx(Ptr<RdmaQueuePair> q, uint32_t delta){
	q->mlx.m_eventDecreaseRate = Simulator::Schedule(MicroSeconds(m_rateDecreaseInterval) + NanoSeconds(delta), &RdmaHw::CheckRateDecreaseMlx, this, q);
}

void RdmaHw::RateIncEventTimerMlx(Ptr<RdmaQueuePair> q){
	q->mlx.m_rpTimer = Simulator::Schedule(MicroSeconds(m_rpgTimeReset), &RdmaHw::RateIncEventTimerMlx, this, q);
	RateIncEventMlx(q);
	q->mlx.m_rpTimeStage++;
}
void RdmaHw::RateIncEventMlx(Ptr<RdmaQueuePair> q){

	if(m_cc_mode == Settings::CC_MIBO_NSDI22 && m_mibo_cc_mode == Settings::HYPR){
		if (q->mlx.m_rpTimeStage < m_rpgThreshold){ // fast recovery
			FastRecoveryMlx(q);
		} else {
			MIBOIncreaseMlx(q);
		}
	} else{
		// check which increase phase: fast recovery, active increase, hyper increase
		if (q->mlx.m_rpTimeStage < m_rpgThreshold){ // fast recovery
			FastRecoveryMlx(q);
		}else if (q->mlx.m_rpTimeStage == m_rpgThreshold){ // active increase
			ActiveIncreaseMlx(q);
		}else { // hyper increase
			HyperIncreaseMlx(q);
		}
	}
}

void RdmaHw::FastRecoveryMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	printf("%lu fast recovery: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip, q->dip, q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
	ChangeRate(q, (q->m_rate / 2) + (q->mlx.m_targetRate / 2));
	#if PRINT_LOG
	printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
}

void RdmaHw::ActiveIncreaseMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	printf("%lu active inc: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip, q->dip, q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
	// get NIC
	uint32_t nic_idx = GetNicIdxOfQp(q);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	// increate rate
	q->mlx.m_targetRate += m_rai;
	if (q->mlx.m_targetRate > dev->GetDataRate())
		q->mlx.m_targetRate = dev->GetDataRate();
	ChangeRate(q, (q->m_rate / 2) + (q->mlx.m_targetRate / 2));
	#if PRINT_LOG
	printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif

}

void RdmaHw::HyperIncreaseMlx(Ptr<RdmaQueuePair> q){
	#if PRINT_LOG
	printf("%lu hyper inc: %08x %08x %u %u (%0.3lf %.3lf)->", Simulator::Now().GetTimeStep(), q->sip, q->dip, q->sport, q->dport, q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
	// get NIC
	uint32_t nic_idx = GetNicIdxOfQp(q);
	Ptr<QbbNetDevice> dev = m_nic[nic_idx].dev;
	// increate rate
	q->mlx.m_targetRate += m_rhai;
	if (q->mlx.m_targetRate > dev->GetDataRate())
		q->mlx.m_targetRate = dev->GetDataRate();
	ChangeRate(q, (q->m_rate / 2) + (q->mlx.m_targetRate / 2));
	#if PRINT_LOG
	printf("(%.3lf %.3lf)\n", q->mlx.m_targetRate.GetBitRate() * 1e-9, q->m_rate.GetBitRate() * 1e-9);
	#endif
}

/***********************
 * High Precision CC
 ***********************/
void RdmaHw::HandleAckHp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t ack_seq){
	// update rate
	if (ack_seq > qp->hp.m_lastUpdateSeq){ // if full RTT feedback is ready, do full update
		UpdateRateHp(qp, p, ch, false);
	}else{ // do fast react
		FastReactHp(qp, p, ch);
	}
}

void RdmaHw::UpdateRateHp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool fast_react){
	uint32_t next_seq = qp->m_nxt_psn;
	bool print = !fast_react || true;
	if (qp->hp.m_lastUpdateSeq == 0){ // first RTT
		qp->hp.m_lastUpdateSeq = next_seq;
		// store INT
		IntHeader &ih = ch.ack.ih;
		NS_ASSERT(ih.nhop <= IntHeader::maxHop);
		for (uint32_t i = 0; i < ih.nhop; i++)
			qp->hp.hop[i] = ih.hop[i];
		#if PRINT_LOG
		if (print){
			printf("%lu %s %08x %08x %u %u [%u,%u,%u]", Simulator::Now().GetTimeStep(), fast_react? "fast" : "update", qp->sip, qp->dip.Get(), qp->sport, qp->dport, qp->hp.m_lastUpdateSeq, ch.ack.seq, next_seq);
			for (uint32_t i = 0; i < ih.nhop; i++)
				printf(" %u %lu %lu", ih.hop[i].GetQlen(), ih.hop[i].GetBytes(), ih.hop[i].GetTime());
			printf("\n");
		}
		#endif
	}else {
		// check packet INT
		IntHeader &ih = ch.ack.ih;
		if (ih.nhop <= IntHeader::maxHop){
			double max_c = 0;
			bool inStable = false;
			#if PRINT_LOG
			if (print)
				printf("%lu %s %08x %08x %u %u [%u,%u,%u]", Simulator::Now().GetTimeStep(), fast_react? "fast" : "update", qp->sip, qp->dipGet(), qp->sport, qp->dport, qp->hp.m_lastUpdateSeq, ch.ack.seq, next_seq);
			#endif
			// check each hop
			double U = 0;
			uint64_t dt = 0;
			bool updated[IntHeader::maxHop] = {false}, updated_any = false;
			NS_ASSERT(ih.nhop <= IntHeader::maxHop);
			for (uint32_t i = 0; i < ih.nhop; i++){
				if (m_sampleFeedback){
					if (ih.hop[i].GetQlen() == 0 and fast_react)
						continue;
				}
				updated[i] = updated_any = true;
				#if PRINT_LOG
				if (print)
					printf(" %u(%u) %lu(%lu) %lu(%lu)", ih.hop[i].GetQlen(), qp->hp.hop[i].GetQlen(), ih.hop[i].GetBytes(), qp->hp.hop[i].GetBytes(), ih.hop[i].GetTime(), qp->hp.hop[i].GetTime());
				#endif
				uint64_t tau = ih.hop[i].GetTimeDelta(qp->hp.hop[i]);;
				double duration = tau * 1e-9;
				double txRate = (ih.hop[i].GetBytesDelta(qp->hp.hop[i])) * 8 / duration;
				//win is bdp, m_max_rate is the bw of hosts, bdp / bw = T
				double u = txRate / ih.hop[i].GetLineRate() + (double)std::min(ih.hop[i].GetQlen(), qp->hp.hop[i].GetQlen()) * qp->m_max_rate.GetBitRate() / ih.hop[i].GetLineRate() /qp->m_win;
				#if PRINT_LOG
				if (print)
					printf(" %.3lf %.3lf", txRate, u);
				#endif
				if (!m_multipleRate){
					// for aggregate (single R)
					if (u > U){
						U = u;
						dt = tau;
					}
				}else {//lkx: now used
					// for per hop (per hop R)
					if (tau > qp->m_baseRtt)
						tau = qp->m_baseRtt;
					qp->hp.hopState[i].u = (qp->hp.hopState[i].u * (qp->m_baseRtt - tau) + u * tau) / double(qp->m_baseRtt);
				}
				qp->hp.hop[i] = ih.hop[i];//lkx: update hp by using new ih
			}

			DataRate new_rate;
			int32_t new_incStage;
			DataRate new_rate_per_hop[IntHeader::maxHop];
			int32_t new_incStage_per_hop[IntHeader::maxHop];
			if (!m_multipleRate){
				// for aggregate (single R)
				if (updated_any){
					if (dt > qp->m_baseRtt)
						dt = qp->m_baseRtt;
					qp->hp.u = (qp->hp.u * (qp->m_baseRtt - dt) + U * dt) / double(qp->m_baseRtt);
					max_c = qp->hp.u / m_targetUtil;

					if (max_c >= 1 || qp->hp.m_incStage >= m_miThresh){
						new_rate = qp->hp.m_curRate / max_c + m_rai;
						new_incStage = 0;
					}else{
						new_rate = qp->hp.m_curRate + m_rai;
						new_incStage = qp->hp.m_incStage+1;
					}
					if (new_rate < m_minRate)
						new_rate = m_minRate;
					if (new_rate > qp->m_max_rate)
						new_rate = qp->m_max_rate;
					#if PRINT_LOG
					if (print)
						printf(" u=%.6lf U=%.3lf dt=%u max_c=%.3lf", qp->hp.u, U, dt, max_c);
					#endif
					#if PRINT_LOG
					if (print)
						printf(" rate:%.3lf->%.3lf\n", qp->hp.m_curRate.GetBitRate()*1e-9, new_rate.GetBitRate()*1e-9);
					#endif
				}
			}else{
				// for per hop (per hop R)
				new_rate = qp->m_max_rate;
				for (uint32_t i = 0; i < ih.nhop; i++){
					if (updated[i]){
						double c = qp->hp.hopState[i].u / m_targetUtil;
						if (c >= 1 || qp->hp.hopState[i].incStage >= m_miThresh){
							new_rate_per_hop[i] = qp->hp.hopState[i].Rc / c + m_rai;
							new_incStage_per_hop[i] = 0;
						}else{
							new_rate_per_hop[i] = qp->hp.hopState[i].Rc + m_rai;
							new_incStage_per_hop[i] = qp->hp.hopState[i].incStage+1;
						}
						// bound rate
						//lkx: this is not appeared in paper
						if (new_rate_per_hop[i] < m_minRate)
							new_rate_per_hop[i] = m_minRate;
						if (new_rate_per_hop[i] > qp->m_max_rate)
							new_rate_per_hop[i] = qp->m_max_rate;
						// find min new_rate
						if (new_rate_per_hop[i] < new_rate)
							new_rate = new_rate_per_hop[i];
						#if PRINT_LOG
						if (print)
							printf(" [%u]u=%.6lf c=%.3lf", i, qp->hp.hopState[i].u, c);
						#endif
						#if PRINT_LOG
						if (print)
							printf(" %.3lf->%.3lf", qp->hp.hopState[i].Rc.GetBitRate()*1e-9, new_rate.GetBitRate()*1e-9);
						#endif
					}else{
						if (qp->hp.hopState[i].Rc < new_rate)
							new_rate = qp->hp.hopState[i].Rc;
					}
				}
				#if PRINT_LOG
				printf("\n");
				#endif
			}
			if (updated_any)
				ChangeRate(qp, new_rate);//use the min rate among all link

			if (!fast_react){
				if (updated_any){
					qp->hp.m_curRate = new_rate;
					qp->hp.m_incStage = new_incStage;
				}
				if (m_multipleRate){
					// for per hop (per hop R)
					for (uint32_t i = 0; i < ih.nhop; i++){
						if (updated[i]){
							qp->hp.hopState[i].Rc = new_rate_per_hop[i];
							qp->hp.hopState[i].incStage = new_incStage_per_hop[i];
						}
					}
				}
			}
		}
		if (!fast_react){
			if (next_seq > qp->hp.m_lastUpdateSeq)
				qp->hp.m_lastUpdateSeq = next_seq; //+ rand() % 2 * m_mtu;
		}
	}
}

void RdmaHw::FastReactHp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch){
	if (m_fast_react)
		UpdateRateHp(qp, p, ch, true);
}

/**********************
 * TIMELY
 *********************/
void RdmaHw::HandleAckTimely(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t ack_seq){
	// update rate
	if (ack_seq > qp->tmly.m_lastUpdateSeq){ // if full RTT feedback is ready, do full update
		UpdateRateTimely(qp, p, ch, false);
	}else{ // do fast react
		FastReactTimely(qp, p, ch);
	}
}

void RdmaHw::UpdateRateTimely(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, bool us){
	uint32_t next_seq = qp->m_nxt_psn;
	uint64_t rtt = Simulator::Now().GetTimeStep() - ch.ack.ih.ts;
	bool print = !us;
	if (qp->tmly.m_lastUpdateSeq != 0){ // not first RTT
		int64_t new_rtt_diff = (int64_t)rtt - (int64_t)qp->tmly.lastRtt;
		double rtt_diff = (1 - m_tmly_alpha) * qp->tmly.rttDiff + m_tmly_alpha * new_rtt_diff;
		double gradient = rtt_diff / m_tmly_minRtt;
		bool inc = false;
		double c = 0;
		DataRate new_rate = qp->m_rate;
		#if PRINT_LOG
		if (print)
			printf("%lu node:%u rtt:%lu rttDiff:%.0lf gradient:%.3lf rate:%.3lf", Simulator::Now().GetTimeStep(), m_node->GetId(), rtt, rtt_diff, gradient, qp->tmly.m_curRate.GetBitRate() * 1e-9);
		#endif
		if (rtt < m_tmly_TLow){
			inc = true;
		}else if (rtt > m_tmly_THigh){
			c = 1 - m_tmly_beta * (1 - (double)m_tmly_THigh / rtt);
			inc = false;
		}else if (gradient <= 0){
			inc = true;
		}else{
			c = 1 - m_tmly_beta * gradient;
			if (c < 0)
				c = 0;
			inc = false;
		}
		if (inc){
			if (qp->tmly.m_incStage < 5){
				new_rate = qp->tmly.m_curRate + m_rai;
			}else{
				new_rate = qp->tmly.m_curRate + m_rhai;
			}
			if (new_rate > qp->m_max_rate)
				new_rate = qp->m_max_rate;
			if (!us){
				qp->tmly.m_curRate = new_rate;
				qp->tmly.m_incStage++;
				qp->tmly.rttDiff = rtt_diff;
			}
		}else{
			new_rate = std::max(m_minRate, qp->tmly.m_curRate * c);
			if (!us){
				qp->tmly.m_curRate = new_rate;
				qp->tmly.m_incStage = 0;
				qp->tmly.rttDiff = rtt_diff;
			}
		}
		#if PRINT_LOG
		if (print){
			printf(" %c %.3lf\n", inc? '^':'v', qp->m_rate.GetBitRate() * 1e-9);
		}
		#endif
		ChangeRate(qp, new_rate);
	}
	if (!us && next_seq > qp->tmly.m_lastUpdateSeq){
		qp->tmly.m_lastUpdateSeq = next_seq;
		// update
		qp->tmly.lastRtt = rtt;
	}
}
void RdmaHw::FastReactTimely(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch){
}

/**********************
 * DCTCP
 *********************/
void RdmaHw::HandleAckDctcp(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t ack_seq){
	uint8_t cnp = (ch.ack.flags >> qbbHeader::FLAG_CNP) & 1;
	bool new_batch = false;

	// update alpha
	qp->dctcp.m_ecnCnt += (cnp > 0);
	if (ack_seq > qp->dctcp.m_lastUpdateSeq){ // if full RTT feedback is ready, do alpha update
		#if PRINT_LOG
		printf("%lu %s %08x %08x %u %u [%u,%u,%u] %.3lf->", Simulator::Now().GetTimeStep(), "alpha", qp->sip, qp->dip, qp->sport, qp->dport, qp->dctcp.m_lastUpdateSeq, ch.ack.seq, qp->snd_nxt, qp->dctcp.m_alpha);
		#endif
		new_batch = true;
		if (qp->dctcp.m_lastUpdateSeq == 0){ // first RTT
			qp->dctcp.m_lastUpdateSeq = qp->m_nxt_psn;
			qp->dctcp.m_batchSizeOfAlpha = qp->m_nxt_psn / m_mtu + 1;
		}else {
			double frac = std::min(1.0, double(qp->dctcp.m_ecnCnt) / qp->dctcp.m_batchSizeOfAlpha);
			qp->dctcp.m_alpha = (1 - m_g) * qp->dctcp.m_alpha + m_g * frac;
			qp->dctcp.m_lastUpdateSeq = qp->m_nxt_psn;
			qp->dctcp.m_ecnCnt = 0;
			qp->dctcp.m_batchSizeOfAlpha = (qp->m_nxt_psn - ack_seq) / m_mtu + 1;
			#if PRINT_LOG
			printf("%.3lf F:%.3lf", qp->dctcp.m_alpha, frac);
			#endif
		}
		#if PRINT_LOG
		printf("\n");
		#endif
	}

	// check cwr exit
	if (qp->dctcp.m_caState == 1){
		if (ack_seq > qp->dctcp.m_highSeq)
			qp->dctcp.m_caState = 0;
	}

	DataRate new_rate = qp->m_rate;
	// check if need to reduce rate: ECN and not in CWR
	if (cnp && qp->dctcp.m_caState == 0){
		#if PRINT_LOG
		printf("%lu %s %08x %08x %u %u %.3lf->", Simulator::Now().GetTimeStep(), "rate", qp->sip, qp->dip, qp->sport, qp->dport, qp->m_rate.GetBitRate()*1e-9);
		#endif
		new_rate = std::max(m_minRate, new_rate * (1 - qp->dctcp.m_alpha / 2));
		#if PRINT_LOG
		printf("%.3lf\n", qp->m_rate.GetBitRate() * 1e-9);
		#endif
		qp->dctcp.m_caState = 1;
		qp->dctcp.m_highSeq = qp->m_nxt_psn;
	}

	// additive inc
	if (qp->dctcp.m_caState == 0 && new_batch)
		new_rate = std::min(qp->m_max_rate, qp->m_rate + m_dctcp_rai);

	ChangeRate(qp, new_rate);
}

/**********************
 * SWIFT
 *********************/
void RdmaHw::HandleAckSwift(Ptr<RdmaSndQueuePair> qp, Ptr<Packet> p, CustomHeader &ch, uint32_t ack_seq){
	qp->swift.m_retransmitCnt = 0;
	double num_acked = ack_seq > qp->m_ack_psn ? ack_seq-qp->m_ack_psn : 0.0;
	num_acked /= Settings::packet_payload;

	IntHeader &ih = ch.ack.ih;
	uint64_t fabric_delay = Simulator::Now().GetNanoSeconds() - ih.sendTs;
	swift_max_fabric_rtt = std::max(swift_max_fabric_rtt, fabric_delay);
	if (qp->swift.m_rtt == 0)
		qp->swift.m_rtt = ih.remoteDelay + fabric_delay;
	else
		qp->swift.m_rtt = qp->swift.m_rtt * m_ewma_alpha + (ih.remoteDelay + fabric_delay) * (1-m_ewma_alpha);
	if (qp->swift.m_endDelay == 0)
		qp->swift.m_endDelay = ih.remoteDelay;
	else
		qp->swift.m_endDelay = qp->swift.m_endDelay * m_ewma_alpha + ih.remoteDelay * (1-m_ewma_alpha);

	// update end_cwnd
	if (qp->swift.m_endDelay < m_end_target){
		if (qp->swift.m_endWin >= 1){
			qp->swift.m_endWin += (m_ai/qp->swift.m_endWin * num_acked);
		}else{
			qp->swift.m_endWin += (m_ai * num_acked);
		}
	}else if (qp->swift.m_endDelay > 0 && CanDecrease(qp)){
		qp->swift.m_endWin = std::max(1-m_beta*((qp->swift.m_endDelay - m_end_target)*1.0/qp->swift.m_endDelay), 1-m_max_mdf)*qp->swift.m_endWin;
	}

	// update fabric_cwnd
	uint64_t fabric_target = GetFabricTargetDelay(qp, ih.hopCnt);
	if (fabric_delay < fabric_target){
		if (qp->swift.m_fabricWin >= 1){
			qp->swift.m_fabricWin += (m_ai/qp->swift.m_fabricWin * num_acked);
		}else{
			qp->swift.m_fabricWin += (m_ai * num_acked);
		}
	}else if (CanDecrease(qp)){
		assert(fabric_delay > 0);
		qp->swift.m_fabricWin = std::max(1-m_beta*((fabric_delay - fabric_target)*1.0/fabric_delay), 1-m_max_mdf)*qp->swift.m_fabricWin;
	}

//#if DEBUG_MODE
//		if (qp->m_qpid == DEBUG_QP){
//			Settings::win_out << qp->m_qpid << " " << Simulator::Now().GetTimeStep()
//				<< " fabric_delay: " << fabric_delay
//				<< " fabric_target:" << fabric_target
//				<< " end_delay:" << ih.remoteDelay
//				<< std::endl;
//		}
//#endif

	// update cwnd
	UpdateWin(qp);
}

uint64_t RdmaHw::GetFabricTargetDelay(Ptr<RdmaSndQueuePair> qp, uint8_t nhop){
	double alpha = m_fs_range/(1/std::sqrt(m_fs_min_cwnd) - 1/std::sqrt(m_fs_max_cwnd));
	double beta = alpha/(std::sqrt(m_fs_max_cwnd));
	return m_base_target + nhop * m_h + std::max(0.0, std::min(alpha/std::sqrt(qp->swift.m_fabricWin)-beta, 1.0*m_fs_range));
}

bool RdmaHw::CanDecrease(Ptr<RdmaSndQueuePair> qp){
	return Simulator::Now() - qp->swift.m_lastDecreaseT >= NanoSeconds(qp->swift.m_rtt);
}

void RdmaHw::UpdateWin(Ptr<RdmaSndQueuePair> qp){
	// clamp
	if (qp->swift.m_fabricWin < m_min_cwnd) qp->swift.m_fabricWin = m_min_cwnd;
	if (qp->swift.m_fabricWin > m_max_cwnd) qp->swift.m_fabricWin = m_max_cwnd;
	if (qp->swift.m_endWin < m_min_cwnd) qp->swift.m_endWin = m_min_cwnd;
	if (qp->swift.m_endWin > m_max_cwnd) qp->swift.m_endWin = m_max_cwnd;
	double win = std::min(qp->swift.m_fabricWin, qp->swift.m_endWin);
	if (m_end_target == 0) win = qp->swift.m_fabricWin; // only fabric window control
	// update t_last_decrease
	if (win*Settings::packet_payload < qp->GetWin()){
		qp->swift.m_lastDecreaseT = Simulator::Now();
	}
	qp->SetWin(win*Settings::packet_payload);
	// update pacing_delay
	if (qp->GetWin() < Settings::packet_payload){
		qp->swift.m_pacingDelay = qp->swift.m_rtt/(1.0*qp->GetWin()/Settings::packet_payload);
	}else{
		qp->swift.m_pacingDelay = 0;
	}
}

void RdmaHw::FastRecoverySwift(Ptr<RdmaSndQueuePair> qp){
	qp->swift.m_retransmitCnt = 0;
	if (CanDecrease(qp)){
		qp->SetWin((1-m_max_mdf) * qp->m_win);
	}
}

void RdmaHw::RTOSwift(Ptr<RdmaSndQueuePair> qp){
	qp->swift.m_retransmitCnt += 1;
	if (qp->swift.m_retransmitCnt >= m_retx_reset_threshold){
		qp->SetWin(m_min_cwnd*Settings::packet_payload);
	}else{
		if (CanDecrease(qp)) qp->SetWin((1-m_max_mdf) * qp->m_win);
	}
}
/*------------------------------------Reactive CC(end)------------------------------------*/

/******************************
 * wqy: Quota
 *****************************/

uint32_t RdmaHw::GetWholeHeaderSize(){
	UdpHeader udpHeader;
	Ipv4Header ipHeader;
	PppHeader ppp;
	return SeqTsHeader::GetHeaderSize() + udpHeader.GetSerializedSize() + ipHeader.GetSerializedSize() + ppp.GetSerializedSize();
}

void RdmaHw::SetQuota(){
	m_quota = Settings::free_token * (m_mtu+GetWholeHeaderSize()) * Settings::quota_num;
	for (uint32_t i = 0; i < Settings::NODESCALE; ++i)
		m_quota_dsts[i] = m_quota;
}

bool RdmaHw::CheckUschQuota(Ptr<RdmaSndQueuePair> qp){
	uint32_t payload_size = qp->GetBytesLeft();
	if (payload_size > 0 && qp->m_nxt_psn < Settings::free_token*m_mtu){
		// has left unscheduled packets
		if (m_mtu < payload_size)
			payload_size = m_mtu;
		uint32_t quota = 0;
		if (Settings::quota_mode == 1)
			quota = m_quota;
		else if(Settings::quota_mode == 2){
			assert(qp->m_dst >= 0);
			quota = m_quota_dsts[qp->m_dst];
		}

		if (payload_size + GetWholeHeaderSize() > quota){
			return false;
		}
	}
	return true;
}

void RdmaHw::UpdateQuota(uint32_t pktsize, bool isadd, uint32_t dst){
	if (Settings::quota_mode == 1){
		if (isadd)
			m_quota += (pktsize + GetWholeHeaderSize());
		else
			m_quota -= (pktsize + GetWholeHeaderSize());
	}else if(Settings::quota_mode == 2){
		if (dst == (uint32_t)-1) return;
		assert(dst >= 0);
		if (isadd)
			m_quota_dsts[dst] += (pktsize + GetWholeHeaderSize());
		else
			m_quota_dsts[dst] -= (pktsize + GetWholeHeaderSize());
	}
}

void RdmaHw::AddUschTag(Ptr<Packet>p, Ptr<RdmaSndQueuePair> qp, uint32_t dst){
	uint32_t payload_size = qp->GetBytesLeft();
	if (payload_size > 0 && qp->m_nxt_psn < Settings::free_token*m_mtu){
		if (m_mtu < payload_size)
			payload_size = m_mtu;
		UschTag uscht;
		uscht.SetPktSize(payload_size);
		p->AddPacketTag(uscht);
		UpdateQuota(payload_size, false, dst);
	}
}

}
