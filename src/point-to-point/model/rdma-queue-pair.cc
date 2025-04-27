#include <ns3/hash.h>
#include <ns3/uinteger.h>
#include <ns3/seq-ts-header.h>
#include <ns3/udp-header.h>
#include <ns3/ipv4-header.h>
#include <ns3/simulator.h>
#include "ns3/ppp-header.h"
#include "ns3/rdma-queue-pair.h"
#include "ns3/settings.h"
#include <assert.h>

#define DEBUG_MODE 0
#define DEBUG_QP 290

namespace ns3 {
/**************************
 * RdmaQueuePair
 *************************/
RdmaQueuePair::RdmaQueuePair(){
	m_tkn_ack = 0;
	m_tkn_nxt = 0;
	m_sch_start = INT_MAX;
	m_lst_resumed_unsch_tkn = 0;
	m_total_size = 0;
	m_last_print_rate = 0;
	m_rate = 0;
	m_nextTknAvail = Time(0);
	m_win = 0;
	m_max_rate = 0;
	m_ipid = 0;

	ep.m_weight = 0.5;
	ep.m_rttSentCredit = ep.m_rttDroppedCredit = 0;
	ep.m_rttPreIncreasing = false;
	ep.m_rtt = 0;
	ep.m_lastUpdate = Simulator::Now();

	mlx.m_alpha = 1;
	mlx.m_alpha_cnp_arrived = false;
	mlx.m_first_cnp = true;
	mlx.m_decrease_cnp_arrived = false;
	mlx.m_rpTimeStage = 0;
	hp.m_lastUpdateSeq = 0;

	for (uint32_t i = 0; i < sizeof(hp.keep) / sizeof(hp.keep[0]); i++)
		hp.keep[i] = 0;
	hp.m_incStage = 0;
	hp.m_lastGap = 0;
	hp.u = 1;
	for (uint32_t i = 0; i < IntHeader::maxHop; i++){
		hp.hopState[i].u = 1;
		hp.hopState[i].incStage = 0;
	}

	tmly.m_lastUpdateSeq = 0;
	tmly.m_incStage = 0;
	tmly.lastRtt = 0;
	tmly.rttDiff = 0;

	dctcp.m_lastUpdateSeq = 0;
	dctcp.m_caState = 0;
	dctcp.m_highSeq = 0;
	dctcp.m_alpha = 1;
	dctcp.m_ecnCnt = 0;
	dctcp.m_batchSizeOfAlpha = 0;

	swift.m_endDelay = 0;
	swift.m_rtt = 0;

	//liuchang: for queue allocate
	m_queue_id = UINT32_MAX;
}

RdmaQueuePair::~RdmaQueuePair(){
	// release shared_ptr
	m_finished_msgs.clear();
	while (!m_sent_credits.empty())
		m_sent_credits.pop();
}

void RdmaQueuePair::InitalCC(DataRate bps, bool hp_multipleRate){

	m_rate = bps;
	m_max_rate = bps;
	m_nextTknAvail = Simulator::Now();

	mlx.m_alpha = 1;
	mlx.m_alpha_cnp_arrived = false;
	mlx.m_first_cnp = true;
	mlx.m_decrease_cnp_arrived = false;
	mlx.m_rpTimeStage = 0;
	hp.m_lastUpdateSeq = 0;
	mlx.m_targetRate = bps;

	for (uint32_t i = 0; i < sizeof(hp.keep) / sizeof(hp.keep[0]); i++)
		hp.keep[i] = 0;
	hp.m_incStage = 0;
	hp.m_lastGap = 0;
	hp.u = 1;
	for (uint32_t i = 0; i < IntHeader::maxHop; i++){
		hp.hopState[i].u = 1;
		hp.hopState[i].incStage = 0;
		if (hp_multipleRate) 	hp.hopState[i].Rc = bps;
	}
	hp.m_curRate = bps;

	tmly.m_lastUpdateSeq = 0;
	tmly.m_incStage = 0;
	tmly.lastRtt = 0;
	tmly.rttDiff = 0;
	tmly.m_curRate = bps;

	dctcp.m_lastUpdateSeq = 0;
	dctcp.m_caState = 0;
	dctcp.m_highSeq = 0;
	dctcp.m_alpha = 1;
	dctcp.m_ecnCnt = 0;
	dctcp.m_batchSizeOfAlpha = 0;

	ep.m_weight = 0.5;
	ep.m_rttSentCredit = ep.m_rttDroppedCredit = 0;
	ep.m_rttPreIncreasing = false;
	ep.m_rtt = 0;
	ep.m_lastUpdate = Simulator::Now();
	ep.m_curRate = ep.m_max_rate = 1/bps.CalculateTxTime(Settings::MTU);

	swift.m_lastDecreaseT = Time(0);
	swift.m_retransmitCnt = 0;
	swift.m_pacingDelay = 0;
	swift.m_endWin = m_win/(double)Settings::packet_payload;
	swift.m_fabricWin = m_win/(double)Settings::packet_payload;

	m_tkn_ack = m_tkn_nxt = m_tkn_max = 0;

}

void RdmaQueuePair::SetWin(uint32_t win){
	m_win = win;
	PrintWin();
}

void RdmaQueuePair::SetVarWin(bool v){
	m_var_win = v;
}

bool RdmaQueuePair::IsWinBound(uint64_t msg_inflight){
	if(Settings::enable_qp_winBound == false)
	{
		return false;
	}
	uint64_t w = GetWin();
	return w != 0 && msg_inflight >= w;
}

uint64_t RdmaQueuePair::GetWin(){
	if (m_win == 0)
		return 0;
	uint64_t w;
	if (m_var_win){
		w = m_win * m_rate.GetBitRate() / m_max_rate.GetBitRate();
		if (w == 0)
			w = 1; // must > 0
	}else{
		w = m_win;
	}
	return w;
}

void RdmaQueuePair::PrintRate(){
	if (m_last_print_rate != m_rate.GetBitRate()){
		Settings::rate_out << m_qpid << " " << is_sndQP << " " << Simulator::Now().GetTimeStep()
				<< " " << m_rate.GetBitRate()
//				<< " alpha: " << mlx.m_alpha
//				<< " target: " << mlx.m_targetRate
//				<< " rpgStage: " << mlx.m_rpTimeStage
				<< std::endl;
		m_last_print_rate = m_rate.GetBitRate();
	}
}

void RdmaQueuePair::PrintWin(){
	if (Settings::is_out_win && m_last_print_win != m_win){
#if DEBUG_MODE
		if (m_qpid != DEBUG_QP) return;
#endif
		Settings::win_out << m_qpid << " " << is_sndQP << " " << Simulator::Now().GetTimeStep()
				<< " " << m_win
				<< " rtt:" << swift.m_rtt
				<< " endDelay:" << swift.m_endDelay
				<< " endWin:" << swift.m_endWin
				<< " fabricWin:" << swift.m_fabricWin
				<< std::endl;
		m_last_print_win = m_win;
	}
}

uint32_t RdmaQueuePair::GetHash(void){
	union{
		struct {
			uint32_t sip, dip;
			uint32_t qpid;
		};
		char c[12];
	} buf;
	buf.sip = sip;
	buf.dip = dip;
	buf.qpid = m_qpid;
	return Hash32(buf.c, 12);
}
void RdmaQueuePair::SetQueueId(uint32_t id){
		m_queue_id = id;
	}
uint32_t RdmaQueuePair::GetQueueId(){
		return m_queue_id;
	}

/**************************
 * RdmaSndQueuePair
 *************************/
TypeId RdmaSndQueuePair::GetTypeId (void)
{
	static TypeId tid = TypeId ("ns3::RdmaSndQueuePair")
		.SetParent<Object> ()
		;
	return tid;
}

RdmaSndQueuePair::RdmaSndQueuePair(uint16_t pg, Ipv4Address _sip, Ipv4Address _dip, uint16_t _sport, uint16_t _dport){
	is_sndQP = true;
	sip = _sip.Get();
	dip = _dip.Get();

	m_rrlastMsg = 0;

	startTime = Simulator::Now();

	m_dst = 0;
	m_pg = pg;
	m_baseRtt = 0;
	m_var_win = false;
	m_nextAvail = Time(0);

	m_nxt_psn = 0;
	m_ack_psn = 0;
	m_unschCount = 0;
	m_status = 0x00;		// unsch
}

RdmaSndQueuePair::~RdmaSndQueuePair(){
	// release shared_ptr
	rdma_msgs_sending.clear();
	rdma_msgs_waiting.clear();
	while (!m_credits.empty())
		m_credits.pop();
}

/*
 * The callback of device's DequeueAndTransmit method
 * for timeout retransmission
 */
void RdmaSndQueuePair::SetRTOCallback(RTOCallback rtoCallback){
	m_RTOCallback = rtoCallback;
}

/*
 * for timeout retransmission
 * when a new action happen, reset this message's last action time(used for calculate timeout time)
 */
void RdmaSndQueuePair::ResetMSGRTOTime(uint32_t msgSeq){
	std::priority_queue<Ptr<RdmaSndOperation>, std::vector<Ptr<RdmaSndOperation> >, RdmaOperationLastActionTimeCMP> all_inflight_msgs;

	uint32_t unfinshed_num = rdma_msgs_waiting.size();
	for (uint32_t i = 0; i < unfinshed_num; i++){
		Ptr<RdmaSndOperation> curr = rdma_msgs_waiting[i];
		if (curr->GetBytesLeft() == 0 && curr->GetOnTheFly())
			all_inflight_msgs.push(curr);
	}

	uint32_t sending_num = rdma_msgs_sending.size();
	for (uint32_t i = 0; i < sending_num; i++){
		Ptr<RdmaSndOperation> curr = rdma_msgs_sending[i];
		if (curr->GetBytesLeft() == 0 && curr->GetOnTheFly())
			all_inflight_msgs.push(curr);
	}

	/*
	 * has found timeout msg in unfinished queue
	 * --> reset timeout time
	 */
	if (all_inflight_msgs.size() > 0){
		Time earliest_timeout = all_inflight_msgs.top()->m_lastActionTime + MicroSeconds(Settings::rto_us);
		ResetRTOEvent(earliest_timeout);
	}
}

/*
 * Most of the time, used for Settings::qp_mode
 * for timeout retransmission to reset QP's retransmission event
 */
void RdmaSndQueuePair::ResetRTOEvent(Time earliest){
	m_eventTimeoutRetransmission.Cancel();
	Time t;
	if (earliest <= Simulator::Now())
		t = Time(0);
	else
		t = earliest - Simulator::Now();
	m_eventTimeoutRetransmission = Simulator::Schedule(t, &RdmaSndQueuePair::RecoverMSG, this);
}

/*
 * for timeout retransmission
 * When timeouted, do recover for message
 */
void RdmaSndQueuePair::RecoverMSG(){
	/*
	 * unfinished queue:
	 * find out timeout msgs
	 */
	std::priority_queue<Ptr<RdmaSndOperation>, std::vector<Ptr<RdmaSndOperation> >, RdmaOperationMsgSeqCMP> targets;
	std::vector<Ptr<RdmaSndOperation> >::iterator it = rdma_msgs_waiting.begin();
	while (it != rdma_msgs_waiting.end()){
		if ((*it)->m_lastActionTime + MicroSeconds(Settings::rto_us) <= Simulator::Now()){
			std::vector<Ptr<RdmaSndOperation> >::iterator tmp = it;
			//it++;
			(*tmp)->Recover();
			targets.push(*tmp);
			it = rdma_msgs_waiting.erase(tmp);

		}else
			it++;
	}

	/*
	 * sending queue:
	 * 1. find timeout msgs
	 * 2. move timeout msgs in unfinished queue back to sending queue
	 */
	uint32_t targets_num = targets.size();

	uint32_t sending_num = rdma_msgs_sending.size();
	for (uint32_t i = 0; i < sending_num; i++){
		Ptr<RdmaSndOperation> curr = rdma_msgs_sending[i];
		if (curr->m_lastActionTime + MicroSeconds(Settings::rto_us) <= Simulator::Now()){
			curr->Recover();
		}
	}

	while (targets.size() > 0){
		rdma_msgs_sending.push_back(targets.top());		// todo: it's better to sort?
		targets.pop();
	}

	if (targets_num > 0){
		// call nic to send packets
		Settings::timeout_times++;
		m_RTOCallback(this);
	}

}

uint32_t RdmaSndQueuePair::GetMsgNumber(){
	return rdma_msgs_sending.size() + rdma_msgs_waiting.size();
}

Ptr<RdmaSndOperation> RdmaSndQueuePair::GetMsg(uint32_t msgSeq, bool& is_sending){
	uint32_t sending_num = rdma_msgs_sending.size();
	for (uint32_t i = 0; i < sending_num; i++){
		if (rdma_msgs_sending[i]->m_msgSeq == msgSeq){
			is_sending = true;
			return rdma_msgs_sending[i];
		}
	}
	uint32_t waiting_num = rdma_msgs_waiting.size();
	for (uint32_t i = 0; i < waiting_num; i++){
		if (rdma_msgs_waiting[i]->m_msgSeq == msgSeq){
			is_sending = false;
			return rdma_msgs_waiting[i];
		}
	}
	return NULL;
}

Ptr<RdmaOperation> RdmaSndQueuePair::Peek(){
	Ptr<RdmaSndOperation> msg = NULL;
	if (Settings::msg_scheduler == Settings::MSG_FIFO){
		msg = PeekFIFOMsg();
	}else if (Settings::msg_scheduler == Settings::MSG_RR){
		msg = PeekRRMsg();
	}else{
		assert(false);	// cannot
	}
	return msg;
}

/*
 * MSG scheduler - FIFO : Peek message with minimum sequence which has packets to send
 */
Ptr<RdmaSndOperation> RdmaSndQueuePair::PeekFIFOMsg(){
	uint32_t sending_num = rdma_msgs_sending.size();
	if (sending_num > 0){
		Ptr<RdmaSndOperation> msg = NULL;
		for (uint32_t i = 0; i < sending_num; i++){
			if (rdma_msgs_sending[i]->GetBytesLeft() > 0 || (Settings::use_irn && rdma_msgs_sending[i]->m_irn.m_rtxQueuingCnt > 0)){
				if (!msg || msg->m_msgSeq > rdma_msgs_sending[i]->m_msgSeq){
					msg = rdma_msgs_sending[i];
				}
			}
		}
		return msg;
	}else
		return NULL;
}

/*
 * MSG scheduler - RR
 */
Ptr<RdmaSndOperation> RdmaSndQueuePair::PeekRRMsg(){
	uint32_t sending_num = rdma_msgs_sending.size();
	if (sending_num > 0){
		for (uint32_t i = 0; i < sending_num; i++){
			if (rdma_msgs_sending[(m_rrlastMsg + i)%sending_num]->GetBytesLeft() > 0 || (Settings::use_irn && rdma_msgs_sending[(m_rrlastMsg + i)%sending_num]->m_irn.m_rtxQueuingCnt > 0)){
				return rdma_msgs_sending[(m_rrlastMsg + i)%sending_num];
			}
		}
	}
	return NULL;
}

void RdmaSndQueuePair::SetRRLast(Ptr<RdmaSndOperation> msg){
	uint32_t sending_num = rdma_msgs_sending.size();
	for (uint32_t i = 0; i < sending_num; i++){
		if (rdma_msgs_sending[i]->m_msgSeq == msg->m_msgSeq){
			m_rrlastMsg = i;
			return;
		}
	}
}

void RdmaSndQueuePair::AddRdmaOperation(Ptr<RdmaSndOperation> msg){
	rdma_msgs_sending.push_back(msg);		// todo: do some unique check ?
	m_total_size += msg->m_size;
}

/*
 * Called when current msg has sent all of packets
 * -->  move current msg into unfinished queue
 */
void RdmaSndQueuePair::MoveRdmaOperationToWaiting(uint32_t msg_seq){
	Ptr<RdmaSndOperation> curr = NULL;
	std::vector<Ptr<RdmaSndOperation> >::iterator it = rdma_msgs_sending.begin();
	while (it != rdma_msgs_sending.end()){
		if ((*it)->m_msgSeq == msg_seq){
			curr = *it;
			rdma_msgs_sending.erase(it);
			break;
		}
		it++;
	}
	assert(!!curr);
	rdma_msgs_waiting.push_back(curr);
}

/*
 * When receive a NACK of a msg in waiting queue, move it back
 */
void RdmaSndQueuePair::MoveRdmaOperationToSending(uint32_t msg_seq){
	Ptr<RdmaSndOperation> target_msg = NULL;
	std::vector<Ptr<RdmaSndOperation> >::iterator it = rdma_msgs_waiting.begin();
	while (it != rdma_msgs_waiting.end()){
		if (msg_seq == (*it)->m_msgSeq){
			target_msg = *it;
			rdma_msgs_waiting.erase(it);
			break;
		}else
			it++;
	}
	assert(!!target_msg);
	rdma_msgs_sending.push_back(target_msg);

}

/*
 * Called when msg receive last ACK, i.e., when msg finish
 * --> Remove msg from unfinished queue
 */
bool RdmaSndQueuePair::RemoveRdmaOperation(uint32_t msgSeq){
	bool result = false;
	// msg has been put into unfinished queue
	// remove msg from msg_unfinished queue
	std::vector<Ptr<RdmaSndOperation> >::iterator it = rdma_msgs_waiting.begin();
	while (it != rdma_msgs_waiting.end()){
		if (msgSeq == (*it)->m_msgSeq){
			rdma_msgs_waiting.erase(it);
			result = true;
			break;
		}else
			it++;
	}
	if (!result){
		it = rdma_msgs_sending.begin();
		while (it != rdma_msgs_sending.end()){
			if (msgSeq == (*it)->m_msgSeq){
				rdma_msgs_sending.erase(it);
				result = true;
				break;
			}else
				it++;
		}
	}

	if (GetMsgNumber() == 0){
		// no unfinished msg -- empty QP
		Simulator::Cancel(m_eventTimeoutRetransmission);
	}

	return result;
}

/*
 * For test flow, generate next msg
 */
void RdmaSndQueuePair::ContinueTestFlow(){
	if (rdma_msgs_sending.size() > 0){
		// create message
		Ptr<RdmaSndOperation> msg = Create<RdmaSndOperation>(rdma_msgs_sending.front()->m_pg, rdma_msgs_sending.front()->sip, rdma_msgs_sending.front()->dip, rdma_msgs_sending.front()->sport+1, rdma_msgs_sending.front()->dport);
		msg->SetSrc(rdma_msgs_sending.front()->m_src);
		msg->SetDst(rdma_msgs_sending.front()->m_dst);
		msg->SetMSGSeq(rdma_msgs_sending.front()->m_msgSeq+1);
		msg->SetQPId(rdma_msgs_sending.front()->m_qpid);
		msg->SetSize(rdma_msgs_sending.front()->m_size);
		msg->SetTestFlow(rdma_msgs_sending.front()->m_isTestFlow);
		rdma_msgs_sending.push_back(msg);
	}
}

void RdmaSndQueuePair::SetBaseRtt(uint64_t baseRtt){
	m_baseRtt = baseRtt;
}

void RdmaSndQueuePair::SetQPId(uint32_t qp){
	m_qpid = qp;
}

void RdmaSndQueuePair::SetDst(uint32_t dst){
	m_dst = dst;
}

uint64_t RdmaSndQueuePair::GetBytesLeft(){
	Ptr<RdmaSndOperation> msg = DynamicCast<RdmaSndOperation>(Peek());
	if (!msg) return 0;
	return msg->GetBytesLeft();
}

Time RdmaSndQueuePair::GetNextAvailT(){
	return m_nextAvail + NanoSeconds(swift.m_pacingDelay);
}

/*********************
 * RdmaRxQueuePair
 ********************/
RdmaRxQueuePair::RdmaRxQueuePair(){
	is_sndQP = false;
	sip = dip = 0;
	m_tkn_max = 0;
}

RdmaRxQueuePair::~RdmaRxQueuePair(){
	// release shared_ptr
	rdma_rx_msgs.clear();
}

void RdmaRxQueuePair::AddRdmaOperation(Ptr<RdmaRxOperation> msg){
	rdma_rx_msgs.push_back(msg);
	m_total_size += msg->m_size;
}

bool RdmaRxQueuePair::RemoveRdmaOperation(uint32_t msg_seq){
	for(uint32_t i = 0; i < rdma_rx_msgs.size(); ++i){
		if (rdma_rx_msgs[i]->m_msgSeq == msg_seq){
			rdma_rx_msgs.erase(rdma_rx_msgs.begin()+i);
			return true;
		}
	}
	return false;
}

Ptr<RdmaRxOperation> RdmaRxQueuePair::GetRxMSG(uint32_t msg_seq){
	for(uint32_t i = 0; i < rdma_rx_msgs.size(); ++i){
		if (rdma_rx_msgs[i]->m_msgSeq == msg_seq){
			return rdma_rx_msgs[i];
		}
	}
	return NULL;
}

uint32_t RdmaRxQueuePair::GetUnfinishedNum(){
	uint32_t num = 0;
	for(uint32_t i = 0; i < rdma_rx_msgs.size(); ++i){
		if (!rdma_rx_msgs[i]->ReceivedAll(Settings::packet_payload)){
			++num;
		}
	}
	return num;
}

Ptr<RdmaOperation> RdmaRxQueuePair::Peek(){
	for(uint32_t i = 0; i < rdma_rx_msgs.size(); ++i){
		if (!rdma_rx_msgs[i]->ReceivedAll(Settings::packet_payload)){
			return rdma_rx_msgs[i];
		}
	}
	return NULL;
}

/*********************
 * RdmaQueuePairGroup
 ********************/
TypeId RdmaQueuePairGroup::GetTypeId (void)
{
	static TypeId tid = TypeId ("ns3::RdmaQueuePairGroup")
		.SetParent<Object> ()
		;
	return tid;
}

RdmaQueuePairGroup::RdmaQueuePairGroup(void){
}

uint32_t RdmaQueuePairGroup::GetN(void){
	return m_qps.size();
}

Ptr<RdmaQueuePair> RdmaQueuePairGroup::Get(uint32_t idx){
	return m_qps[idx];
}

Ptr<RdmaQueuePair> RdmaQueuePairGroup::operator[](uint32_t idx){
	return m_qps[idx];
}

void RdmaQueuePairGroup::AddQp(Ptr<RdmaQueuePair> qp){
	m_qps.push_back(qp);
}

void RdmaQueuePairGroup::Clear(void){
	m_qps.clear();
}

}
