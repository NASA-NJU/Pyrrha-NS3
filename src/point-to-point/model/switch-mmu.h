#ifndef SWITCH_MMU_H
#define SWITCH_MMU_H

#include <unordered_map>
#include <ns3/node.h>
#include "ns3/voq-group.h"
#include "ns3/settings.h"
#include "ns3/simulator.h"
#include "ns3/broadcom-egress-queue.h"
#include <list>
#include <set>

namespace ns3 {

class Packet;
class CAMEntry;
class SwitchMmu: public Object{
public:
	static TypeId GetTypeId (void);

	SwitchMmu(void);

	bool CheckIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
	bool CheckEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
	void UpdateIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
	void UpdateEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
	void RemoveFromIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);
	void RemoveFromEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize);

	bool CheckShouldPause(uint32_t port, uint32_t qIndex);
	bool CheckShouldResume(uint32_t port, uint32_t qIndex);
	void SetPause(uint32_t port, uint32_t qIndex);
	void SetResume(uint32_t port, uint32_t qIndex);
	//void GetPauseClasses(uint32_t port, uint32_t qIndex);
	//bool GetResumeClasses(uint32_t port, uint32_t qIndex);

	uint32_t GetPfcThreshold(uint32_t port);
	uint32_t GetSharedUsed(uint32_t port, uint32_t qIndex);

	bool ShouldSendCN(uint32_t ifindex, uint32_t qIndex);

	void ConfigEcn(uint32_t port, uint32_t _kmin, uint32_t _kmax, double _pmax);
	void ConfigHdrm(uint32_t port, uint32_t size);
	void ConfigNPort(uint32_t n_port);
	void ConfigBufferSize(uint32_t size);

	// config
	uint32_t node_id;
	uint32_t netdevice_num;
	uint32_t buffer_size;
	uint32_t pfc_a_shift[Settings::PORTNUM];
	uint32_t reserve;
	uint32_t headroom[Settings::PORTNUM];
	uint32_t resume_offset;
	uint32_t kmin[Settings::PORTNUM], kmax[Settings::PORTNUM];
	double pmax[Settings::PORTNUM];
	uint32_t total_hdrm;
	uint32_t total_rsrv;
	uint32_t m_ecmpSeed;
	double m_delay_map[Settings::PORTNUM];		// s
	uint64_t m_bw_map[Settings::PORTNUM];		// bps
	uint32_t pfc_th_static[Settings::PORTNUM];
	std::vector<bool> pfc_ingress_ctrls;	// the switch queues which support PFC on ingress
	uint8_t pfc_mode[Settings::PORTNUM];	// the fine of ingress_counter and the index which piggy by pause/resume frame

	// ecn
	std::vector<uint32_t> egress_bytes[Settings::PORTNUM];

	// for traditional PFC, per-queue pause for each port;
	// for switchWin, per-dst pause for each port
	uint32_t shared_used_bytes;
	uint32_t hdrm_bytes[Settings::PORTNUM][Settings::NODESCALE];
	uint32_t ingress_bytes[Settings::PORTNUM][Settings::NODESCALE];
	uint32_t paused[Settings::PORTNUM][Settings::NODESCALE];

	// statistic
	static uint32_t max_egress_queue_bytes;
	static std::vector<uint32_t> sentCN;

	/*--------------------------Floodgate-------------------------------*/
	static std::ofstream win_out;
	bool m_use_switchwin;

	std::map<uint32_t, uint32_t> m_wins;	// <dst_id, the remaining bytes which could send>
	uint32_t m_th_ack;		// threshold to send SwitchACK
	/*
	 * When use hash, a voq may buffer packets of several dsts,
	 * so we need a table to record the buffering packets of a specific dst.
	 */
	std::map<uint32_t, uint32_t> m_buffering; // <dst_id, the buffering bytes in voq> (can only count)

	std::map<uint32_t, uint32_t> m_dst2group;	// <dst_id, group_id>, configured
	std::map<uint32_t, Ptr<VOQGroup> > m_voqGroups;	// <group_id, group>

	void ConfigVOQGroup(uint32_t group_id, uint32_t voq_limit, bool dynamic_hash);
	void ConfigDst(uint32_t dst, uint32_t group_id);

	Ptr<VOQ> GetVOQ(uint32_t dst);
	void UpdateBufferingCount(uint32_t dst, uint32_t pktSize, bool isadd);
	uint32_t GetBufferDst(uint32_t dst);
	void EnsureRegisteredWin(uint32_t dst);
	bool ShouldIgnore(uint32_t dst);
	bool CheckDstWin(uint32_t dst, uint32_t pktSize);
	uint32_t GetInflightBytes(uint32_t dst);
	bool CheckEgressWin(uint32_t outDev, uint32_t pktSize);
	void UpdateDataPSN(uint32_t inDev, uint32_t dst, Ptr<Packet> pkt);
	void UpdateWin(uint32_t dst, uint32_t pktSize, uint32_t dev, bool isadd);
	void UpdateDstWin(uint32_t dst, uint32_t pktSize, bool isadd);
	void UpdateEgressWin(uint32_t dev, uint32_t pktSize, bool isadd);
	void RecoverWin(SwitchACKTag acktag, uint32_t dev);
	void CheckAndSendVOQ(uint32_t dst);
	void CheckAndSendVOQ();
	void CheckAndSendCache(uint32_t outDev);
	uint32_t VOQDequeueCallback(uint32_t dst, uint32_t pktSize, uint32_t outDev, Ptr<Packet> pkt);

	// for statistic
	void UpdateMaxVOQNum();
	void UpdateMaxActiveDstNum();
	uint64_t m_tx_data[Settings::PORTNUM];
	uint64_t m_tx_ctrl[Settings::PORTNUM];
	uint64_t m_tx_tkn[Settings::PORTNUM];
	uint64_t m_tx_tknack[Settings::PORTNUM];
	uint64_t m_tx_switchACK[Settings::PORTNUM];
	uint64_t m_rcv_data[Settings::PORTNUM];
	uint64_t m_rcv_ctrl[Settings::PORTNUM];
	uint64_t m_rcv_tkn[Settings::PORTNUM];
	uint64_t m_rcv_tknack[Settings::PORTNUM];
	uint64_t m_rcv_switchACK[Settings::PORTNUM];

	//for adaptive update win
	void AdaptiveUpdateWin(uint32_t dst);

	// for switch-accumulate-ack mode
	std::map<uint32_t, uint32_t> m_ingressDstCredits[Settings::SWITCHSCALE];	// <dst_id, credit>
	EventId m_creditTimer;
	Time m_ingressLastTime[Settings::SWITCHSCALE][Settings::NODESCALE];		// <upstream/ingress, <dst_id, last_send_credit_time> >
	Callback<void, uint32_t, SwitchACKTag> m_creditIngressTimerCallback;
	Callback<void, uint32_t, uint32_t, uint32_t, uint64_t> m_creditDstTimerCallback;
	// statistics
	static uint32_t switch_byte_credit_counter;
	static uint32_t switch_timeout_credit_counter;

	// for all_switches + switch-accumulate-ack/switch-per-packet-ack
	uint32_t m_egress_wins[Settings::PORTNUM];
	std::queue<Ptr<PacketUnit> > m_egress_cache[Settings::PORTNUM];
	static uint32_t max_egress_cache_bytes_all;
	static uint32_t max_egress_cache_bytes;
	uint32_t m_egress_cache_bytes[Settings::PORTNUM];	// for statistic
	uint32_t GetAllEgressCache();

	void UpdateIngressLastSendTime(uint32_t inDev, uint32_t dst);
	void ResetCreditTimer();
	void CreditTimeout(uint32_t inDev, uint32_t dst);
	void AddCreditCounter(uint32_t inDev, uint32_t dst, uint32_t bytes);
	uint32_t GetIngressDstCreditCounter(uint32_t inDev, uint32_t dst);
	bool CheckIngressCreditCounter(uint32_t inDev, uint32_t th);
	void CleanIngressDstCreditCounter(uint32_t inDev, uint32_t dst, uint64_t ackPSN);
	void CleanIngressCreditCounter(uint32_t inDev, SwitchACKTag& acktag);
	SwitchACKTag GetSwitchACKTag(uint32_t inDev);
	SwitchACKTag GetDstsSwitchACKTag(uint32_t inDev, std::set<uint32_t> dsts);

	// for handling packetloss
	// for absolute data/SwitchACK PSN
	static uint64_t max_nxt_data_psn;
	uint64_t m_nxt_data_psn[Settings::SWITCHSCALE][Settings::NODESCALE];	// port as upstream
	uint64_t m_rcv_data_psn[Settings::SWITCHSCALE][Settings::NODESCALE];	// port as downstream
	uint64_t m_rcv_ack_psn[Settings::SWITCHSCALE][Settings::NODESCALE];	// port as upstream
	uint64_t m_nxt_ack_psn[Settings::SWITCHSCALE][Settings::NODESCALE];	// port as downstream
	uint64_t m_lst_ack_psn[Settings::SWITCHSCALE][Settings::NODESCALE];	// port as downstream
	// for syn timeout
	Time m_lstrcv_ack_time[Settings::SWITCHSCALE][Settings::NODESCALE];
	EventId m_syn_timeout_event[Settings::SWITCHSCALE];	// !reset_only_ToR_switch_win: #port, reset_only_ToR_switch_win: #ToR
	Callback<void, uint32_t, SwitchSYNTag> m_synTimerCallback;	// send syn packet
	void UpdateSynTime(uint32_t dev, uint32_t dst, SwitchACKTag& acktag);
	void SynTimeout(uint32_t dev);
	SwitchSYNTag GetSwitchSYNTag(uint32_t dev);
	std::set<uint32_t> CheckSYN(SwitchSYNTag& syntag, uint32_t dev);

	/*--------------------------BFC-------------------------------*/
	struct FlowEntry {
		uint32_t size;		// the number of packets in queue
		uint32_t q_assign;
		uint32_t in_dev;	// of last packet (not included in paper, but it's necessary to support "assign_sticky_q = false")
		Time time;			// of last enqueue/dequeue action
	};
	uint32_t m_bfcSeed;
	std::map<uint32_t, FlowEntry> m_flow_tb[Settings::PORTNUM];		// <egress, hash(fid)>
	std::map<uint32_t, uint32_t> m_pause_counter[Settings::PORTNUM];	// <ingress, upstreamQ>
	uint32_t GetHashFID(CustomHeader &ch, uint32_t port_num, uint32_t flowid);
	uint32_t GetUsableQ(uint32_t egress, std::map<uint32_t, bool>& egress_paused);
	uint32_t GetBFCPauseTh(uint32_t ingress, uint32_t egress, std::map<uint32_t, bool>& egress_paused);
	bool CheckBFCPause(uint32_t ingress, uint32_t egress, uint32_t fid, std::map<uint32_t, bool>& egress_paused);
	// for statistics
	std::map<uint32_t, std::set<uint32_t> > m_fid_flows;
	static uint32_t max_fid_conflict;
	static uint32_t max_active_q[Settings::SWITCHSCALE][Settings::PORTNUM];

	/*--------------------------VOQ+PFC-------------------------------*/
	uint32_t m_buffering_bytes[Settings::NODESCALE];
	uint32_t m_buffering_egress_bytes[Settings::PORTNUM][Settings::NODESCALE];
	bool CheckVOQPause(bool isLasthop, uint32_t ingress, uint32_t egress, uint32_t qIndex, uint32_t dst);
	bool CheckVOQResume(bool isLasthop, uint32_t ingress, uint32_t egress, uint32_t qIndex, uint32_t dst);

	/*-----------------------Congestion Isolation-------------------------------*/
	struct Spot{
		uint32_t switch_id;
		uint32_t port_id;
	};
	uint32_t up_port_num;
	uint32_t bytes_up_data;
	// <oport, <iport, pause/resume> >
	// Record control status of upstream
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, bool> > upstream_paused;
	 // <switch, <oport, entry> >
	 // Record congestion status of this node
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, Ptr<CAMEntry> > > congestion_entries;
	std::unordered_map<uint32_t, Spot> ciq_root_map[Settings::PORTNUM];
	Ptr<CAMEntry> MatchNextCongestionRoot(uint32_t egress, RoutingTag routingTag, uint32_t curr_match_hop, uint32_t& root_switch, uint32_t& root_port);
	Ptr<CAMEntry> GetEntry(uint32_t root_switch, uint32_t root_port);
	bool IsRoot(uint32_t port);
	bool CheckRootPause(uint32_t ingress, uint32_t egress, uint32_t qIndex);
	bool CheckRootResume(uint32_t ingress, uint32_t egress, uint32_t qIndex);
	void SetRootPause(uint32_t ingress, uint32_t egress);
	void SetRootResume(uint32_t ingress, uint32_t egress);
	void SetRootDealloc(uint32_t ingress, uint32_t egress);
	bool CheckCIQPause(uint32_t ingress, uint32_t egress, uint32_t qIndex, Ptr<CAMEntry> entry);
	bool CheckCIQResume(uint32_t ingress, uint32_t egress, uint32_t qIndex, Ptr<CAMEntry> entry);
	void SetCIQPause(uint32_t ingress, Ptr<CAMEntry> entry);
	void SetCIQResume(uint32_t ingress, Ptr<CAMEntry> entry);
	void SetCIQDealloc(uint32_t ingress, Ptr<CAMEntry> entry);
};

class CAMEntry: public Object{
public:
	enum CIQStatus {INITIAL, PAUSE, RESUME, MERGE};
	CAMEntry(){
		hop = 0;
		for (uint32_t i = 0; i < Settings::PORTNUM; ++i){
			oport_ciq[i] = -1;
			oport_status[i] = INITIAL;
			iport_counter[i] = 0;
		}
	}
	uint16_t hop;
	// Index of CIQ on an oport
	uint32_t oport_ciq[Settings::PORTNUM];
	// Status of CIQ on an oport
	CIQStatus oport_status[Settings::PORTNUM];
	// status of upstream which link with iport. when iport_paused is empty, this node is leaf of congestion tree
	std::unordered_map<uint32_t, bool> iport_paused;
	// Once initial an CAMEntry, start counting at iports for this congestion root
	uint32_t iport_counter[Settings::PORTNUM];
};

} /* namespace ns3 */

#endif /* SWITCH_MMU_H */

