#include <iostream>
#include <fstream>
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/object-vector.h"
#include "ns3/uinteger.h"
#include "ns3/log.h"
#include "ns3/assert.h"
#include "ns3/global-value.h"
#include "ns3/boolean.h"
#include "ns3/broadcom-node.h"
#include "ns3/random-variable.h"
#include "ns3/switch-mmu.h"
#include "ns3/hash-functions.h"
#include "ns3/broadcom-egress-queue.h"
#include <assert.h>
#include <list>
#include "limits.h"

#define DEBUG_MODE 0
#define DEBUG_DST_ID 22

NS_LOG_COMPONENT_DEFINE("SwitchMmu");
namespace ns3 {
	uint32_t SwitchMmu::max_fid_conflict = 0;
	uint32_t SwitchMmu::max_active_q[Settings::SWITCHSCALE][Settings::PORTNUM] = {0};
	uint32_t SwitchMmu::max_egress_queue_bytes = 0;
	uint32_t SwitchMmu::max_egress_cache_bytes = 0;
	uint32_t SwitchMmu::max_egress_cache_bytes_all = 0;
	uint64_t SwitchMmu::max_nxt_data_psn = 0;
	std::vector<uint32_t> SwitchMmu::sentCN;
	uint32_t SwitchMmu::switch_byte_credit_counter = 0;
	uint32_t SwitchMmu::switch_timeout_credit_counter = 0;

	TypeId SwitchMmu::GetTypeId(void){
		static TypeId tid = TypeId("ns3::SwitchMmu")
			.SetParent<Object>()
			.AddConstructor<SwitchMmu>();
		return tid;
	}

	SwitchMmu::SwitchMmu(void){
		buffer_size = 12 * 1024 * 1024;
		reserve = 4 * 1024;
		resume_offset = 3 * 1024;

		// headroom
		shared_used_bytes = 0;
		memset(m_buffering_bytes, 0, sizeof(m_buffering_bytes));
		memset(m_buffering_egress_bytes, 0, sizeof(m_buffering_egress_bytes));
		memset(hdrm_bytes, 0, sizeof(hdrm_bytes));
		memset(ingress_bytes, 0, sizeof(ingress_bytes));
		memset(paused, 0, sizeof(paused));
		memset(pfc_mode, Settings::PAUSE_PG, sizeof(pfc_mode));	// use traditional PFC by default
		memset(pfc_th_static, 0, sizeof(pfc_th_static));	// use dynamic threshold by default
		memset(m_tx_data, 0, sizeof(m_tx_data));
		memset(m_tx_ctrl, 0, sizeof(m_tx_ctrl));
		memset(m_tx_tkn, 0, sizeof(m_tx_tkn));
		memset(m_tx_tknack, 0, sizeof(m_tx_tknack));
		memset(m_tx_switchACK, 0, sizeof(m_tx_switchACK));
		memset(m_nxt_data_psn, 0, sizeof(m_nxt_data_psn));
		memset(m_rcv_ack_psn, 0, sizeof(m_rcv_ack_psn));
		memset(m_rcv_data_psn, 0, sizeof(m_rcv_data_psn));
		memset(m_rcv_data, 0, sizeof(m_rcv_data));
		memset(m_rcv_ctrl, 0, sizeof(m_rcv_ctrl));
		memset(m_rcv_tkn, 0, sizeof(m_rcv_tkn));
		memset(m_rcv_tknack, 0, sizeof(m_rcv_tknack));
		memset(m_rcv_switchACK, 0, sizeof(m_rcv_switchACK));
		memset(m_nxt_ack_psn, 0, sizeof(m_nxt_ack_psn));
		memset(m_lst_ack_psn, 0, sizeof(m_lst_ack_psn));
		memset(m_delay_map, 0, sizeof(m_delay_map));
		memset(m_bw_map, 0, sizeof(m_bw_map));

		// switchwin
		for (uint32_t i = 0; i < Settings::SWITCHSCALE; ++i){
			m_ingressDstCredits[i].clear();
			for (uint32_t j = 0; j < Settings::host_num; ++j){
				m_lstrcv_ack_time[i][j] = Time(0);
				m_ingressLastTime[i][j] = Time(0);
			}
		}

		for (uint32_t i = 0; i < Settings::PORTNUM; ++i){
			std::map<uint32_t, FlowEntry> curr;
			m_flow_tb[i] = curr;
			std::map<uint32_t, uint32_t> tmp;
			m_pause_counter[i] = tmp;
			assert(m_egress_cache[i].empty());
			for (uint32_t j = 0; j < Settings::QUEUENUM; j++){
				egress_bytes[i].push_back(0);
			}
		}
	}
	bool SwitchMmu::CheckIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize){
		if (psize + hdrm_bytes[port][qIndex] > headroom[port] && psize + GetSharedUsed(port, qIndex) > GetPfcThreshold(port)){
			printf("%lu %u Drop: queue:%u,%u: Headroom full\n", Simulator::Now().GetTimeStep(), node_id, port, qIndex);
			for (uint32_t i = 1; i < 64; i++)
				printf("(%u,%u)", hdrm_bytes[i][qIndex], ingress_bytes[i][qIndex]);
			printf("\n");
			return false;
		}
		return true;
	}
	bool SwitchMmu::CheckEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize){
		if (qIndex >= Settings::QUEUENUM)	return true;	// voq: always return true
		assert(egress_bytes[port].size() > qIndex);
		if (egress_bytes[port][qIndex] + psize > Settings::max_qlen[qIndex])
			return false;
		return true;
	}
	void SwitchMmu::UpdateIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize){
		uint32_t new_bytes = ingress_bytes[port][qIndex] + psize;
		//use reserve first
		if (new_bytes <= reserve){
			ingress_bytes[port][qIndex] += psize;
		}else {
			uint32_t thresh = GetPfcThreshold(port);
			//trigger PFC, then begin to use headroom
			if (new_bytes - reserve > thresh){
				hdrm_bytes[port][qIndex] += psize;
			}else {
			//otherwise (PFC has not been triggered yet), use shared buffer
				ingress_bytes[port][qIndex] += psize;
				shared_used_bytes += std::min(psize, new_bytes - reserve);
			}
		}
	}
	void SwitchMmu::UpdateEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize){
//#if DEBUG_MODE
//		std::cout << "enqueue: " << port << " " << qIndex << " " << psize << std::endl;
//#endif
		while (qIndex >= egress_bytes[port].size()){
			egress_bytes[port].push_back(0);
		}
		egress_bytes[port][qIndex] += psize;
	}
	void SwitchMmu::RemoveFromIngressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize){
		/*if use headroom, from_hdrm = psize, from_shared = 0; 
		then hdrm_bytes -= psize; ingress_bytes -= 0, shared_used_bytes = 0
		if headroom is not used currently, from_hdrm = 0, from_shared = depends on whether reserve has been used up;
		then hdrm_bytes -= 0, ingress_bytes -= psize;*/
		uint32_t from_hdrm = std::min(hdrm_bytes[port][qIndex], psize);
		uint32_t from_shared = std::min(psize - from_hdrm, ingress_bytes[port][qIndex] > reserve ? ingress_bytes[port][qIndex] - reserve : 0);
		hdrm_bytes[port][qIndex] -= from_hdrm;
		ingress_bytes[port][qIndex] -= psize - from_hdrm;
		shared_used_bytes -= from_shared;
	}
	void SwitchMmu::RemoveFromEgressAdmission(uint32_t port, uint32_t qIndex, uint32_t psize){
//#if DEBUG_MODE
//		std::cout << "dequeue:" << port << " " << qIndex << " " << psize << std::endl;
//#endif
		assert(egress_bytes[port].size() > qIndex && egress_bytes[port][qIndex] >= psize);
		egress_bytes[port][qIndex] -= psize;
	}
	bool SwitchMmu::CheckShouldPause(uint32_t port, uint32_t qIndex){
		return !paused[port][qIndex] && (hdrm_bytes[port][qIndex] > 0 || GetSharedUsed(port, qIndex) >= GetPfcThreshold(port));
	}
	bool SwitchMmu::CheckShouldResume(uint32_t port, uint32_t qIndex){
		if (!paused[port][qIndex])
			return false;
		uint32_t shared_used = GetSharedUsed(port, qIndex);
		return hdrm_bytes[port][qIndex] == 0 && (shared_used == 0 || shared_used + resume_offset <= GetPfcThreshold(port));
	}
	void SwitchMmu::SetPause(uint32_t port, uint32_t qIndex){
		paused[port][qIndex] = true;
	}
	void SwitchMmu::SetResume(uint32_t port, uint32_t qIndex){
		paused[port][qIndex] = false;
	}

	uint32_t SwitchMmu::GetPfcThreshold(uint32_t port){
		if (pfc_mode[port] == Settings::PAUSE_OFF)
			return INT_MAX;
		if (pfc_th_static[port] > 0)
			return pfc_th_static[port];
		return (buffer_size - total_hdrm - total_rsrv - shared_used_bytes) >> pfc_a_shift[port];

	}
	uint32_t SwitchMmu::GetSharedUsed(uint32_t port, uint32_t qIndex){
		uint32_t used = ingress_bytes[port][qIndex];
		return used > reserve ? used - reserve : 0;
	}
	bool SwitchMmu::ShouldSendCN(uint32_t ifindex, uint32_t qIndex){
		assert(egress_bytes[ifindex].size() > qIndex);
		uint32_t bytes = egress_bytes[ifindex][qIndex];
		max_egress_queue_bytes = std::max(max_egress_queue_bytes, bytes);

		double ecn_unit = 1;	// default: use 1
		if (qIndex < Settings::QUEUENUM) ecn_unit = Settings::ecn_unit[qIndex];	// configure
		uint32_t max = kmax[ifindex] * ecn_unit;
		uint32_t min = kmin[ifindex] * ecn_unit;
		if (qIndex == 0)
			return false;

//#if DEBUG_MODE
//		std::cout << Simulator::Now() << " " << node_id << " " << qIndex << " " << Settings::ecn_unit[qIndex] << " " << max << " " << min << " " << bytes << std::endl;
//#endif
		while (sentCN.size() <= qIndex){
			sentCN.push_back(0);
		}
		if (bytes > max){
			++sentCN[qIndex];
			return true;
		}
		if (bytes > min){
			double p = pmax[ifindex] * double(bytes - min) / (max - min);
			if (UniformVariable(0, 1).GetValue() < p){
				++sentCN[qIndex];
				return true;
			}
		}
		return false;
	}
	void SwitchMmu::ConfigEcn(uint32_t port, uint32_t _kmin, uint32_t _kmax, double _pmax){
		kmin[port] = _kmin * 1000;
		kmax[port] = _kmax * 1000;
		pmax[port] = _pmax;
	}
	void SwitchMmu::ConfigHdrm(uint32_t port, uint32_t size){
		headroom[port] = size;
	}
	void SwitchMmu::ConfigNPort(uint32_t n_port){
		total_hdrm = 0;
		total_rsrv = 0;
		for (uint32_t i = 1; i <= n_port; i++){
			total_hdrm += headroom[i];
			total_rsrv += reserve;
		}
	}
	void SwitchMmu::ConfigBufferSize(uint32_t size){
		buffer_size = size;
	}

/*--------------------------------------SwitchWin--------------------------------*/
	std::ofstream SwitchMmu::win_out;

	void SwitchMmu::ConfigVOQGroup(uint32_t group_id, uint32_t voq_limit, bool dynamic_hash){
		if (m_voqGroups.find(group_id) == m_voqGroups.end()){
			Ptr<VOQGroup> voqGroup = Create<VOQGroup>(node_id, group_id, voq_limit, dynamic_hash);
			voqGroup->m_checkWinCallback = MakeCallback(&SwitchMmu::CheckDstWin, this);
			voqGroup->m_dequeueCallback = MakeCallback(&SwitchMmu::VOQDequeueCallback, this);
			m_voqGroups[group_id] = voqGroup;
		}else{
			m_voqGroups[group_id]->m_voq_limit = voq_limit;
			m_voqGroups[group_id]->m_dynamic_hash = dynamic_hash;
		}
	}

	void SwitchMmu::ConfigDst(uint32_t dst, uint32_t group_id){
		assert(group_id == (uint32_t)-1 || m_voqGroups.find(group_id) != m_voqGroups.end());	// make sure group_id makes sense
		m_dst2group[dst] = group_id;
	}

	bool SwitchMmu::ShouldIgnore(uint32_t dst){
		assert(m_dst2group.find(dst) != m_dst2group.end());
		return m_dst2group[dst] == (uint32_t)-1;
	}

	/**
	 * Register destination if not yet
	 * (i.e. assign runtime switch window)
	 */
	void SwitchMmu::EnsureRegisteredWin(uint32_t dst){
		if (m_wins.find(dst) == m_wins.end()){
			if (Settings::switch_ack_mode != Settings::HOST_PER_PACKET_ACK && Settings::reset_only_ToR_switch_win)
				dst = Settings::host_num + dst/Settings::host_per_rack;	// dst-ToR id

			std::map<std::pair<uint32_t, uint32_t>, uint32_t>::iterator it = VOQ::BDPMAP.find(std::make_pair(node_id, dst));
			if (it != VOQ::BDPMAP.end()){
				m_wins[dst] = it->second;
				if (Settings::switch_absolute_psn)
					win_out << node_id << " " << dst << " " << Simulator::Now().GetNanoSeconds() << " " << m_wins[dst] << " " << GetInflightBytes(dst) << std::endl;
				else
					win_out << node_id << " " << dst << " " << Simulator::Now().GetNanoSeconds() << " " << m_wins[dst] << std::endl;
			}else{
				NS_ASSERT("undefined VOQ window");
				assert(false);
			}
		}
	}

	/**
	 * Get VOQ of destination
	 * Create if not exist
	 */
	Ptr<VOQ> SwitchMmu::GetVOQ(uint32_t dst){
		NS_LOG_FUNCTION (this << dst);

		assert(m_dst2group.find(dst) != m_dst2group.end());
		Ptr<VOQGroup> group = m_voqGroups[m_dst2group[dst]];

		return group->GetVOQ(dst);
	}

	uint32_t SwitchMmu::GetBufferDst(uint32_t dst){
		if (m_buffering.find(dst) == m_buffering.end()) return 0;
		return m_buffering[dst];
	}

	uint32_t SwitchMmu::GetInflightBytes(uint32_t dst){
		uint32_t inflight = 0;
		for (uint32_t i = 0; i < Settings::SWITCHSCALE; ++i){
			assert(m_nxt_data_psn[i][dst] >= m_rcv_ack_psn[i][dst]);
			inflight += (m_nxt_data_psn[i][dst] - m_rcv_ack_psn[i][dst]);
		}
		return inflight;
	}

	bool SwitchMmu::CheckDstWin(uint32_t dst, uint32_t pktSize){
		NS_LOG_FUNCTION (this << dst << pktSize);
		EnsureRegisteredWin(dst);
		if (Settings::switch_absolute_psn){
			if (GetInflightBytes(dst) + pktSize > m_wins[dst])
				return false;
			return true;
		}else{
			if (m_wins[dst] < pktSize)
				return false;
			return true;
		}
	}

	bool SwitchMmu::CheckEgressWin(uint32_t outDev, uint32_t pktSize){
		NS_LOG_FUNCTION (this << outDev << pktSize);
		if (!Settings::use_port_win)
			return true;	// no per-port window

		if (m_egress_wins[outDev] < pktSize)
			return false;
		return true;
	}

	void SwitchMmu::RecoverWin(SwitchACKTag acktag, uint32_t dev){
		NS_LOG_FUNCTION (this);
		for (uint32_t i = 0; i < Settings::host_num; ++i){
			assert(Settings::hostId2IpMap.count(i) > 0);
			if (acktag.getACKEntry(i) > 0){
				uint64_t resume_credit = acktag.getACKEntry(i);
				if (Settings::switch_absolute_psn) {
					uint32_t tmp = dev;
					if (Settings::reset_only_ToR_switch_win){
						tmp = i/Settings::host_per_rack;
					}
					if (resume_credit > m_rcv_ack_psn[tmp][i]){
						resume_credit -= m_rcv_ack_psn[tmp][i];
						m_rcv_ack_psn[tmp][i] = acktag.getACKEntry(i);
					}else
						resume_credit = 0;
				}
				UpdateWin(i, resume_credit, dev, true);
			}
		}
	}

	void SwitchMmu::UpdateWin(uint32_t dst, uint32_t pktsize, uint32_t dev, bool is_add){
		NS_LOG_FUNCTION (this << dst << pktsize << is_add);
		UpdateDstWin(dst, pktsize, is_add);
		UpdateEgressWin(dev, pktsize, is_add);
	}

	void SwitchMmu::UpdateDstWin(uint32_t dst, uint32_t pktsize, bool is_add){
		NS_LOG_FUNCTION (this << dst << pktsize << is_add);
		if (ShouldIgnore(dst)) return;
		if (Settings::switch_absolute_psn){
			win_out << this->node_id << " " << dst << " " << Simulator::Now().GetNanoSeconds() << " " << m_wins[dst] << " " << GetInflightBytes(dst) << std::endl;
			return;
		}
		if (is_add){
			assert(m_wins.find(dst) != m_wins.end());
			m_wins[dst] += pktsize;
		}else{
			EnsureRegisteredWin(dst);
			m_wins[dst] -= pktsize;
		}
		win_out << this->node_id << " " << dst << " " << Simulator::Now().GetNanoSeconds() << " " << m_wins[dst] << std::endl;
	}

	void SwitchMmu::UpdateEgressWin(uint32_t dev, uint32_t pktsize, bool is_add){
		NS_LOG_FUNCTION (this << dev << pktsize << is_add);
		if (!Settings::use_port_win) return;
		if (is_add){
			m_egress_wins[dev] += pktsize;
		}else{
			m_egress_wins[dev] -= pktsize;
		}
	}

	/*
	 * when packet is passing to egress, update PSN counter and tag SwitchPSNTag
	 */
	void SwitchMmu::UpdateDataPSN(uint32_t dev, uint32_t dst, Ptr<Packet> packet){
		assert(Settings::switch_absolute_psn);
		uint32_t tmp = dev;
		if (Settings::reset_only_ToR_switch_win) tmp = dst/Settings::host_per_rack;
		// send data with absolute psn
		m_nxt_data_psn[tmp][dst] += packet->GetSize();
		SwitchPSNTag psnTag;
		psnTag.SetPSN(m_nxt_data_psn[tmp][dst]);
		packet->AddPacketTag(psnTag);
		SwitchMmu::max_nxt_data_psn = std::max(m_nxt_data_psn[tmp][dst], SwitchMmu::max_nxt_data_psn);
	}

	/*
	 * (RR schedule)
	 * Dequeue all window-allowed packets
	 */
	void SwitchMmu::CheckAndSendVOQ(){
		NS_LOG_FUNCTION (this);

		// RR schedule
		std::queue<Ptr<VOQ> > dstLeft;
		std::map<uint32_t, uint32_t>::iterator it = m_wins.begin();
		while (it != m_wins.end()){
			if (it->second > 0){
				uint32_t dst = it->first;
				if (ShouldIgnore(dst)) return;
				assert(m_dst2group.find(dst) != m_dst2group.end());
				Ptr<VOQGroup> group = m_voqGroups[m_dst2group[dst]];

				Ptr<VOQ> voq = group->FindVOQ(dst);
				if (!!voq && voq->CheckAndSendOne()){
					dstLeft.push(voq);
				}
			}
			it++;
		}

		while (!dstLeft.empty()){
			Ptr<VOQ> voq = dstLeft.front(); dstLeft.pop();
			if (!!voq && voq->CheckAndSendOne()){
				dstLeft.push(voq);
			}
		}
	}

	/*
	 * Dequeue all window-allowed packets of a specific dst
	 */
	void SwitchMmu::CheckAndSendVOQ(uint32_t dst){
		NS_LOG_FUNCTION (this);

		if (ShouldIgnore(dst)) return;
		assert(m_dst2group.find(dst) != m_dst2group.end());
		Ptr<VOQGroup> group = m_voqGroups[m_dst2group[dst]];

		Ptr<VOQ> voq = group->FindVOQ(dst);
		if (!!voq){
			voq->CheckAndSendAll();
		}else{
			assert(GetBufferDst(dst) == 0);
		}
	}

	void SwitchMmu::CheckAndSendCache(uint32_t outDev){
		NS_LOG_FUNCTION (this << outDev);
		if (!Settings::use_port_win) return;
		assert(outDev < Settings::PORTNUM);
		while (!m_egress_cache[outDev].empty()){
			Ptr<PacketUnit> top = m_egress_cache[outDev].front();
			if (!CheckEgressWin(outDev, top->GetPacketSize())) break;
			UpdateEgressWin(outDev, top->GetPacketSize(), false);
			m_egress_cache[outDev].pop();
			m_egress_cache_bytes[outDev] -= top->GetPacketSize();
			top->GoOnReceive();
		}
	}

	uint32_t SwitchMmu::VOQDequeueCallback(uint32_t dst, uint32_t pktsize, uint32_t outDev, Ptr<Packet> pkt){
		NS_LOG_FUNCTION (this << dst << pktsize);

		// when packet is passing to egress, update PSN counter and tag SwitchPSNTag
		if (Settings::switch_absolute_psn) UpdateDataPSN(outDev, dst, pkt);

		UpdateDstWin(dst, pktsize, false);
		UpdateBufferingCount(dst, pktsize, false);
		return m_buffering[dst];
	}

	uint32_t SwitchMmu::GetAllEgressCache(){
		if (!Settings::use_port_win) return 0;
		uint32_t res = 0;
		for (uint32_t i = 0; i < Settings::PORTNUM; ++i){
			res += m_egress_cache_bytes[i];
			SwitchMmu::max_egress_cache_bytes = std::max(SwitchMmu::max_egress_cache_bytes, m_egress_cache_bytes[i]);
		}
		return res;
	}

	void SwitchMmu::UpdateBufferingCount(uint32_t dst, uint32_t pktSize, bool isadd){
		NS_LOG_FUNCTION (this << dst << pktSize << isadd);

		if (isadd){
			if (m_buffering.find(dst) == m_buffering.end()){
				m_buffering.insert(std::make_pair(dst, pktSize));
			}else{
				m_buffering[dst] += pktSize;
			}
		}else{
			assert(m_buffering.find(dst) != m_buffering.end());
			m_buffering[dst] -= pktSize;
		}

	}

	void SwitchMmu::UpdateMaxVOQNum(){
		assert(node_id >= Settings::host_num && node_id - Settings::host_num < Settings::SWITCHSCALE);
		uint32_t voq_n = 0;
		std::map<uint32_t, Ptr<VOQGroup> >::iterator it = m_voqGroups.begin();
		while (m_voqGroups.end() != it){
			voq_n += it->second->VOQs.size();
			if (VOQ::node_maxVOQNum[node_id - Settings::host_num].find(it->first) ==
					VOQ::node_maxVOQNum[node_id - Settings::host_num].end())
				VOQ::node_maxVOQNum[node_id - Settings::host_num].insert(std::make_pair(it->first, 0));
			VOQ::node_maxVOQNum[node_id - Settings::host_num][it->first] =
					std::max(VOQ::node_maxVOQNum[node_id - Settings::host_num][it->first], voq_n);
			it++;
		}
		VOQ::maxVOQNum = std::max(VOQ::maxVOQNum, voq_n);

	}

	void SwitchMmu::UpdateMaxActiveDstNum(){
		uint32_t dst_n = 0;
		std::map<uint32_t, uint32_t>::iterator it = m_buffering.begin();
		while (m_buffering.end() != it){
			if (it->second > 0) ++dst_n;
			it++;
		}
		VOQ::maxActiveDstNum = std::max(VOQ::maxActiveDstNum, dst_n);
	}

	void SwitchMmu::AdaptiveUpdateWin(uint32_t dst){

		NS_LOG_FUNCTION(this<<Settings::adaptive_win);

		uint32_t total_bytes = VOQ::GetTotalBytes(node_id);
		if(!Settings::adaptive_whole_switch)
			total_bytes = m_buffering[dst];
		double bytes_per_port = total_bytes;

		uint32_t pod_num = Settings::host_num / Settings::host_per_rack;
		uint32_t tor_per_pod = Settings::tor_num / pod_num;
		uint32_t agg_per_pod = (Settings::switch_num - Settings::tor_num - Settings::core_num) / pod_num;//spine switch, = 0 under 2-tier topo

		if(node_id < Settings::host_num + Settings::tor_num)//tor switch
			bytes_per_port /= (double) Settings::host_per_rack;
		//spine switch, the same as former if(...tor_num...) under 2-tier topo
		else if(node_id < Settings::host_num + Settings::switch_num - Settings::core_num)
			bytes_per_port /= (double) tor_per_pod;
		else//core switch
			bytes_per_port /= (double) pod_num;//=tor num under 2-tier topo

		double base_bdp = 0;

		if (Settings::switch_ack_mode != Settings::HOST_PER_PACKET_ACK && Settings::reset_only_ToR_switch_win)
			dst = Settings::host_num + dst/Settings::host_per_rack;	// dst-ToR id
		std::map<std::pair<uint32_t, uint32_t>, uint32_t>::iterator it = VOQ::BDPMAP.find(std::make_pair(node_id, dst));
		if (it != VOQ::BDPMAP.end()){
			base_bdp = it->second;
		}else{
			NS_ASSERT("undefined VOQ window");
			assert(false);
		}

		bytes_per_port /= base_bdp;//in unit of base_bdp

		if(bytes_per_port >= 1){

			bytes_per_port = std::min(bytes_per_port, (double)Settings::host_per_rack);//upper bound of bytes_per_port

			if(Settings::adaptive_win == 1){//linear
				m_wins[dst] = std::max(m_wins[dst], (uint32_t)ceil( 4 * bytes_per_port * base_bdp) );
			}else if(Settings::adaptive_win == 2){//square
				m_wins[dst] = std::max(m_wins[dst], (uint32_t)ceil(0.5 * bytes_per_port * bytes_per_port * base_bdp) );
			}else if(Settings::adaptive_win == 3){//exponential
				m_wins[dst] = std::max(m_wins[dst], (uint32_t)ceil(pow(2, bytes_per_port) * base_bdp) );
			}
			win_out << this->node_id << " " << dst << " " << Simulator::Now().GetNanoSeconds() << " " << m_wins[dst] << std::endl;
		}

	}

	void SwitchMmu::AddCreditCounter(uint32_t dev, uint32_t dst, uint32_t bytes){
		assert(dev < Settings::SWITCHSCALE);
		if (m_ingressDstCredits[dev].count(dst) == 0) m_ingressDstCredits[dev][dst] == 0;
		m_ingressDstCredits[dev][dst] += bytes;
	}

	void SwitchMmu::UpdateIngressLastSendTime(uint32_t dev, uint32_t dst){
		assert(dev < Settings::SWITCHSCALE);
		m_ingressLastTime[dev][dst] = Simulator::Now();
		ResetCreditTimer();
	}

	void SwitchMmu::ResetCreditTimer(){
		if (Settings::switch_credit_interval == 0) return;
		m_creditTimer.Cancel();

		Time minTime = Simulator::Now() + Seconds(1);	// the initial value should be large enough
		uint32_t dev = -1;
		uint32_t dst = -1;
		for (uint32_t i = 0; i < Settings::SWITCHSCALE; ++i){
			for (uint32_t j = 0; j < Settings::host_num; ++j){
				if (m_ingressLastTime[i][j] != Time(0) && m_ingressLastTime[i][j] < minTime){
					minTime = m_ingressLastTime[i][j];
					dev = i;
					dst = j;
				}
			}
		}

		if (dev != (uint32_t)-1){
//			std::cout << Simulator::Now() << " switch-" << node_id << "-dev" << dev << " reset credit timer " << minTime << " " << minTime + MicroSeconds(Settings::switch_credit_interval) << std::endl;
			assert(minTime + MicroSeconds(Settings::switch_credit_interval) >= Simulator::Now());
			Time t = minTime + MicroSeconds(Settings::switch_credit_interval) - Simulator::Now();
			m_creditTimer = Simulator::Schedule(t, &SwitchMmu::CreditTimeout, this, dev, dst);
		}
	}

	void SwitchMmu::CreditTimeout(uint32_t dev, uint32_t dst){
		assert(dev < Settings::SWITCHSCALE && dst < Settings::host_num && Settings::switch_ack_mode != Settings::HOST_PER_PACKET_ACK);
		bool sentCredit = false;
		if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
			if (CheckIngressCreditCounter(dev, 1)) {	// send switchACK when there are accumulate credits
				SwitchACKTag acktag = GetSwitchACKTag(dev);
				m_creditIngressTimerCallback(dev, acktag);
				sentCredit = true;
			}
		}else{
			uint64_t credit = GetIngressDstCreditCounter(dev, dst);
			if (credit > 0) {	// send switchACK when there are accumulate credits
				if (Settings::switch_absolute_psn) {
					credit = m_nxt_ack_psn[dev][dst];
				}
				m_creditDstTimerCallback(dev, Settings::hostId2IpMap[dst], 0, credit);
				sentCredit = true;
			}
		}
		if (!sentCredit) {
			UpdateIngressLastSendTime(dev, dst);
		}else{
			++switch_timeout_credit_counter;
		}
	}

	uint32_t SwitchMmu::GetIngressDstCreditCounter(uint32_t dev, uint32_t dst){
		assert(dev < Settings::SWITCHSCALE);
		if (Settings::switch_absolute_psn){
			if (Settings::switch_ack_th_m == 0 || m_buffering[dst] <= m_th_ack){
				assert(m_nxt_ack_psn[dev][dst] >= m_lst_ack_psn[dev][dst]);
				return m_nxt_ack_psn[dev][dst] - m_lst_ack_psn[dev][dst];
			}
		}else{
			if (m_ingressDstCredits[dev].find(dst) != m_ingressDstCredits[dev].end()){
				// check active delay switchACK
				if (Settings::switch_ack_th_m == 0 || m_buffering[dst] <= m_th_ack)
					return m_ingressDstCredits[dev][dst];
			}
		}
		return 0;
	}

	bool SwitchMmu::CheckIngressCreditCounter(uint32_t dev, uint32_t th){
		assert(dev < Settings::SWITCHSCALE);
		uint32_t sum = 0;
		for (uint32_t i = 0; i < Settings::host_num; ++i){
			sum += GetIngressDstCreditCounter(dev, i);
		}
		if (sum >= th) return true;
		return false;
	}

	void SwitchMmu::CleanIngressDstCreditCounter(uint32_t dev, uint32_t dst_id, uint64_t ackPSN){
		if (Settings::switch_absolute_psn){
			m_lst_ack_psn[dev][dst_id] = ackPSN;
		}else{
			assert(m_ingressDstCredits[dev].find(dst_id) != m_ingressDstCredits[dev].end());
			assert(ackPSN == m_ingressDstCredits[dev][dst_id]);
			m_ingressDstCredits[dev][dst_id] = 0;
		}
	}

	void SwitchMmu::CleanIngressCreditCounter(uint32_t dev, SwitchACKTag& acktag){
		assert(dev < Settings::SWITCHSCALE);
		if (Settings::switch_absolute_psn){
			for (uint32_t i = 0; i < Settings::host_num; ++i){
				if (acktag.getACKEntry(i) > 0) m_lst_ack_psn[dev][i] = acktag.getACKEntry(i);
			}
		}else{
			if (Settings::switch_ack_th_m == 0) m_ingressDstCredits[dev].clear();
			else{
				std::map<uint32_t, uint32_t>::iterator it = m_ingressDstCredits[dev].begin();
				while(it != m_ingressDstCredits[dev].end()){
					uint32_t dst = it->first;
					assert(it->second >= acktag.getACKEntry(dst));
					it->second -= acktag.getACKEntry(dst);
					it++;
				}
			}
		}
	}

	SwitchACKTag SwitchMmu::GetSwitchACKTag(uint32_t dev){
		assert(dev < Settings::SWITCHSCALE);
		SwitchACKTag acktag;
		if (Settings::switch_absolute_psn){
			for (uint32_t i = 0; i < Settings::host_num; ++i){
				if (m_nxt_ack_psn[dev][i] > m_lst_ack_psn[dev][i]){
					// check active delay switchACK
					if (Settings::switch_ack_th_m == 0 || m_buffering[i] <= m_th_ack)
						acktag.SetACKEntry(i, m_nxt_ack_psn[dev][i]);
				}
			}
		}else{
			std::map<uint32_t, uint32_t>::iterator it = m_ingressDstCredits[dev].begin();
			while(it != m_ingressDstCredits[dev].end()){
				if (it->second > 0){
					// check active delay switchACK
					if (Settings::switch_ack_th_m == 0 || m_buffering[it->first] <= m_th_ack)
						acktag.SetACKEntry(it->first, it->second);
				}
				it++;
			}
		}
		return acktag;
	}

	SwitchACKTag SwitchMmu::GetDstsSwitchACKTag(uint32_t dev, std::set<uint32_t> dsts){
		assert(dev < Settings::SWITCHSCALE && Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT && Settings::switch_absolute_psn);
		SwitchACKTag acktag;
		for (uint32_t i = 0; i < Settings::host_num; ++i){
			if (m_nxt_ack_psn[dev][i] > m_lst_ack_psn[dev][i] || dsts.count(i) > 0){	// when SwitchACK dropped, should check dsts.count(i) > 0
				// check active delay switchACK
				if (Settings::switch_ack_th_m == 0 || m_buffering[i] <= m_th_ack)
					acktag.SetACKEntry(i, m_nxt_ack_psn[dev][i]);
			}
		}
		return acktag;
	}

	void SwitchMmu::UpdateSynTime(uint32_t dev, uint32_t dst, SwitchACKTag& acktag){
		if (Settings::switch_absolute_psn == 0 || Settings::switch_syn_timeout_us == 0) return;
		if (Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT)
			m_lstrcv_ack_time[dev][dst] = Simulator::Now();
		else if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
			for (uint32_t i = 0; i < Settings::host_num; ++i){
				if (acktag.getACKEntry(i) > 0) m_lstrcv_ack_time[dev][i] = Simulator::Now();
			}
		}

		if (!m_syn_timeout_event[dev].IsRunning())
			m_syn_timeout_event[dev] = Simulator::Schedule(MicroSeconds(Settings::switch_syn_timeout_us), &SwitchMmu::SynTimeout, this, dev);
	}

	void SwitchMmu::SynTimeout(uint32_t dev){
		m_syn_timeout_event[dev].Cancel();
		SwitchSYNTag syntag = GetSwitchSYNTag(dev);
		if (syntag.GetPacketSize() > 0) m_synTimerCallback(dev, syntag);	// has in-flight data/SwitchACK -> send SYN
		m_syn_timeout_event[dev] = Simulator::Schedule(MicroSeconds(Settings::switch_syn_timeout_us), &SwitchMmu::SynTimeout, this, dev);
	}

	SwitchSYNTag SwitchMmu::GetSwitchSYNTag(uint32_t dev){
		SwitchSYNTag tag;
		for (uint32_t i = 0; i < Settings::host_num; ++i){
			if (m_rcv_ack_psn[dev][i] < m_nxt_data_psn[dev][i] && m_lstrcv_ack_time[dev][i] + MicroSeconds(Settings::switch_syn_timeout_us) >= Simulator::Now()){
				// has in-flight data/SwitchACK && hasn't receive SwitchACK for a period of time
				tag.SetPSNEntry(i, m_rcv_ack_psn[dev][i], m_nxt_data_psn[dev][i]);
			}
		}
		return tag;
	}

	std::set<uint32_t> SwitchMmu::CheckSYN(SwitchSYNTag& syntag, uint32_t dev){
		NS_LOG_FUNCTION (this);
		std::set<uint32_t> dsts;
		for (uint32_t i = 0; i < Settings::host_num; ++i){
			assert(Settings::hostId2IpMap.count(i) > 0);
			if (syntag.GetDataPSNEntry(i) > m_rcv_data_psn[dev][i]){	// has dropped data packets
				m_nxt_ack_psn[dev][i] = syntag.GetDataPSNEntry(i);
				m_rcv_data_psn[dev][i] = syntag.GetDataPSNEntry(i);
			}
			if (syntag.GetACKPSNEntry(i) < m_nxt_ack_psn[dev][i])	// has dropped packets
				dsts.insert(i);
		}
		return dsts;
	}

/*------------------------------------For BFC---------------------------------*/
	uint32_t SwitchMmu::GetHashFID(CustomHeader &ch, uint32_t port_num, uint32_t flowid){
		if (!Settings::use_hash_fid)	return flowid;

		union {
			uint8_t u8[4+4+4];
			uint32_t u32[3];
		} buf;

		buf.u32[0] = ch.sip;
		buf.u32[1] = ch.dip;

		uint16_t sport, dport;
		if (ch.l3Prot == CustomHeader::ProtTypeE::TCP){
			sport = ch.tcp.sport;
			dport = ch.tcp.dport;
		}else if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){
			sport = ch.udp.sport;
			dport = ch.udp.dport;
		}else if (ch.l3Prot == CustomHeader::ProtTypeE::ACK || ch.l3Prot == CustomHeader::ProtTypeE::NACK || ch.l3Prot == CustomHeader::ProtTypeE::SACK){
			sport = ch.ack.sport;
			dport = ch.ack.dport;
		}else if (ch.l3Prot == CustomHeader::ProtTypeE::TOKEN || ch.l3Prot == CustomHeader::ProtTypeE::TOKEN_ACK){
			sport = ch.tkn.sport;
			dport = ch.tkn.dport;
		}

		buf.u32[2] = sport | ((uint32_t)dport << 16);

		return Settings::EcmpHash(buf.u8, 12, m_bfcSeed)%(Settings::QUEUENUM*port_num*Settings::flowtb_size);
	}

	uint32_t SwitchMmu::GetUsableQ(uint32_t egress, std::map<uint32_t, bool>& egress_paused){
		assert(egress < Settings::PORTNUM);

		std::set<uint32_t> usedQ;
		std::map<uint32_t, FlowEntry>::iterator it = m_flow_tb[egress].begin();
		while(it != m_flow_tb[egress].end()){
			if (it->second.size > 0
					|| (!Settings::assign_paused_q && egress_paused.find(it->second.q_assign) != egress_paused.end() || egress_paused[it->second.q_assign])
					|| (Simulator::Now() <= it->second.time + Seconds(Settings::sticky_th * 2 * m_delay_map[it->second.in_dev]) && !Settings::assign_sticky_q)	// it's a sticky q
					){
				usedQ.insert(it->second.q_assign);
			}
			it++;
		}

		uint32_t i = 1;		// 0 for ctrl packet
		for (; i < Settings::QUEUENUM; i++){
			if (usedQ.count(i) == 0){
				break;
			}
		}
		usedQ.clear();
		return i;
	}

	uint32_t SwitchMmu::GetBFCPauseTh(uint32_t ingress, uint32_t egress, std::map<uint32_t, bool>& egress_paused){
		uint32_t n_active = 0;
		for (uint32_t i = 1; i < Settings::QUEUENUM; i++){
			if (egress_bytes[egress][i] > 0 && (egress_paused.find(i) == egress_paused.end() || !egress_paused[i])){
				n_active += 1;
			}
		}
		NS_LOG_FUNCTION(this<<ingress<<egress<<n_active);
		max_active_q[node_id-Settings::host_num][egress] = std::max(max_active_q[node_id-Settings::host_num][egress], n_active);
		//if (n_active == 0) return INT_MAX;
		if (n_active == 0) n_active = 1;
		uint32_t th = (m_bw_map[egress]/8.0/n_active)*m_delay_map[ingress]*2;
		NS_LOG_FUNCTION(this << th);
		return th>0?th:1;
	}

	bool SwitchMmu::CheckBFCPause(uint32_t ingress, uint32_t egress, uint32_t fid, std::map<uint32_t, bool>& egress_paused){
		assert(m_flow_tb[egress].find(fid) != m_flow_tb[egress].end());
		uint32_t th = GetBFCPauseTh(ingress, egress, egress_paused);
		assert(m_flow_tb[egress][fid].q_assign < egress_bytes[egress].size());
		if (egress_bytes[egress][m_flow_tb[egress][fid].q_assign] > th){
			return true;
		}
		return false;
	}

/*------------------------------------For VOQ+PFC---------------------------------*/
	bool SwitchMmu::CheckVOQPause(bool isLasthop, uint32_t ingress, uint32_t egress, uint32_t qIndex, uint32_t dst){
		uint32_t th = (m_bw_map[egress]/8.0)*m_delay_map[ingress]*2*Settings::voq_pauseth_m;
		if (Settings::EGRESSVQLEN_DST_PERHOP == Settings::voq_pause_mode){
			if (isLasthop || qIndex >= Settings::QUEUENUM){	// queue of DToR or VOQ
				assert(qIndex < egress_bytes[egress].size());
				return egress_bytes[egress][qIndex] > th;
			}
		}else if (Settings::BUFFER_DST_PERHOP == Settings::voq_pause_mode){
			return m_buffering_bytes[dst] > th;
		}else if (Settings::EGRESSBUFFER_DST_PERHOP == Settings::voq_pause_mode){
			return m_buffering_egress_bytes[egress][dst] > th;
		}
		return false;
	}

	bool SwitchMmu::CheckVOQResume(bool isLasthop, uint32_t ingress, uint32_t egress, uint32_t qIndex, uint32_t dst){
		uint32_t th = (m_bw_map[egress]/8.0)*m_delay_map[ingress]*2;
		if (Settings::EGRESSVQLEN_DST_PERHOP == Settings::voq_pause_mode){
			if (isLasthop || qIndex >= Settings::QUEUENUM){	// only check queue length of DToR or VOQ
				assert(qIndex < egress_bytes[egress].size());
				return egress_bytes[egress][qIndex] <= th;
			}
		}else if (Settings::BUFFER_DST_PERHOP == Settings::voq_pause_mode){
			return m_buffering_bytes[dst] <= th;
		}else if (Settings::EGRESSBUFFER_DST_PERHOP == Settings::voq_pause_mode){
			return m_buffering_egress_bytes[egress][dst] <= th;
		}
		return false;
	}

/*------------------------------------For Congestion Isolation---------------------------------*/
	/*
	 * Get congestion entry
	 */
	Ptr<CAMEntry> SwitchMmu::GetEntry(uint32_t root_switch, uint32_t root_port){
		if (congestion_entries.find(root_switch) != congestion_entries.end()
					&& congestion_entries[root_switch].find(root_port) != congestion_entries[root_switch].end()){
			return congestion_entries[root_switch][root_port];
		}
		return nullptr;
	}

	bool SwitchMmu::IsRoot(uint32_t port){
		return (upstream_paused.find(port) != upstream_paused.end() && !upstream_paused[port].empty());
	}

	/*
	 * Match congestion root
	 * whether a packet with routingTag will pass congestion roots recording by congestion_entries
	 * @curr_level: -1 when the first match, otherwise start matching from the level(hop count from this hop)
	 */
	Ptr<CAMEntry> SwitchMmu::MatchNextCongestionRoot(uint32_t egress, RoutingTag routingTag, uint32_t curr_level, uint32_t& root_switch, uint32_t& root_port){
		root_switch = -1, root_port = -1;
		if (Settings::nearest_match){
			if (curr_level == (uint32_t)-1) curr_level = 0;
			for (uint32_t i = routingTag.GetCurrHop() + 1 + curr_level; i < routingTag.GetActiveHop(); ++i){
				uint32_t hop_switch = routingTag.GetHopRoutingSwitch(i);
				uint32_t hop_port = routingTag.GetHopRoutingPort(i);
				if (congestion_entries.find(hop_switch) != congestion_entries.end()
						&& congestion_entries[hop_switch].find(hop_port) != congestion_entries[hop_switch].end()
						&& congestion_entries[hop_switch][hop_port]->oport_ciq[egress] != (uint32_t)-1){
						// match this congestion root
						root_switch = hop_switch;
						root_port = hop_port;
						return congestion_entries[hop_switch][hop_port];
				}
			}
		}else{
			if (curr_level == (uint32_t)-1) curr_level = routingTag.GetActiveHop() - routingTag.GetCurrHop();
			for (uint32_t i = routingTag.GetCurrHop() + curr_level - 1; i > routingTag.GetCurrHop(); --i){
				uint32_t hop_switch = routingTag.GetHopRoutingSwitch(i);
				uint32_t hop_port = routingTag.GetHopRoutingPort(i);
				if (congestion_entries.find(hop_switch) != congestion_entries.end()
						&& congestion_entries[hop_switch].find(hop_port) != congestion_entries[hop_switch].end()
						&& congestion_entries[hop_switch][hop_port]->oport_ciq[egress] != (uint32_t)-1){
						// match this congestion root
						root_switch = hop_switch;
						root_port = hop_port;
						return congestion_entries[hop_switch][hop_port];
				}
			}
		}
		return nullptr;
	}

	/*
	 * Check Pause for congestion root
	 * note: only arrival of data packets will trigger this check and only one queue for data packets
	 */
	bool SwitchMmu::CheckRootPause(uint32_t ingress, uint32_t egress, uint32_t qIndex){
		uint32_t th = (m_bw_map[egress]/8.0)*m_delay_map[ingress]*2*Settings::th_congestion;	// based on per-hop BDP
		assert(qIndex < egress_bytes[egress].size());
		uint32_t qlen = egress_bytes[egress][qIndex];
		if (qlen > th){
			if ((!Settings::up_port[node_id-Settings::host_num][egress] || qlen > Settings::th_hol * bytes_up_data / up_port_num)
					&& (upstream_paused.find(egress) == upstream_paused.end() || upstream_paused[egress].find(ingress) == upstream_paused[egress].end() || !upstream_paused[egress][ingress])){
				return true;
			}
		}
		return false;
	}

	/*
	 * Check Resume for congestion root
	 */
	bool SwitchMmu::CheckRootResume(uint32_t ingress, uint32_t egress, uint32_t qIndex){
		uint32_t th = (m_bw_map[egress]/8.0)*m_delay_map[ingress]*2*Settings::th_resume;	// based on per-hop BDP
		assert(qIndex < egress_bytes[egress].size());
		return egress_bytes[egress][qIndex] < th;
	}

	void SwitchMmu::SetRootPause(uint32_t ingress, uint32_t egress){
		upstream_paused[egress][ingress] = true;
	}

	void SwitchMmu::SetRootResume(uint32_t ingress, uint32_t egress){
		upstream_paused[egress][ingress] = false;
	}

	void SwitchMmu::SetRootDealloc(uint32_t ingress, uint32_t egress){
		assert(upstream_paused[egress].find(ingress) != upstream_paused[egress].end());
		upstream_paused[egress].erase(ingress);
	}

	/*
	 * Check Pause for spreading congestion
	 * note: only arrival of data packets will trigger this check and only one queue for data packets
	 */
	bool SwitchMmu::CheckCIQPause(uint32_t ingress, uint32_t egress, uint32_t qIndex, Ptr<CAMEntry> entry){
		assert(!!entry);
		if (entry->oport_status[egress] == CAMEntry::MERGE) return false;	// merging CIQ doesn't send Pause
		uint32_t th = (m_bw_map[egress]/8.0)*m_delay_map[ingress]*2*Settings::th_congestion;	// based on per-hop BDP
		if (entry->iport_counter[ingress] > th){
			if (entry->iport_paused.find(ingress) == entry->iport_paused.end() || !entry->iport_paused[ingress]){
				return true;
			}
		}
		return false;
	}

	/*
	 * Check Resume for congestion root
	 */
	bool SwitchMmu::CheckCIQResume(uint32_t ingress, uint32_t egress, uint32_t qIndex, Ptr<CAMEntry> entry){
		assert(!!entry);
		if (entry->oport_status[egress] == CAMEntry::MERGE) return false;	// merging CIQ doesn't send Resume (only trigger on ToR)
		uint32_t th = (m_bw_map[egress]/8.0)*m_delay_map[ingress]*2*Settings::th_resume;	// based on per-hop BDP
		return (entry->iport_counter[ingress] < th && entry->iport_paused.find(ingress) != entry->iport_paused.end() && entry->iport_paused[ingress]);
	}

	void SwitchMmu::SetCIQPause(uint32_t ingress, Ptr<CAMEntry> entry){
		assert(!!entry);
		entry->iport_paused[ingress] = true;
	}

	void SwitchMmu::SetCIQResume(uint32_t ingress, Ptr<CAMEntry> entry){
		assert(!!entry);
		entry->iport_paused[ingress] = false;
	}

	void SwitchMmu::SetCIQDealloc(uint32_t ingress, Ptr<CAMEntry> entry){
		assert(!!entry && entry->iport_paused.find(ingress) != entry->iport_paused.end());
		entry->iport_paused.erase(ingress);
	}
}
