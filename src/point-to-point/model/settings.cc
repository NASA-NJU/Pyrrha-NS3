/*
 * settings.cc
 *
 *  Created on: Aug 10, 2021
 *      Author: wqy
 */


#include "ns3/settings.h"

namespace ns3{
	//liuchang: add for incast flow generation
	uint32_t Settings::incast_min = 3;
	uint32_t Settings::incast_max = 4;

	//uint32_t Settings::fc_mode = Settings::FC_FLOODGATE;
	uint32_t Settings::fc_mode = Settings::FC_PFC;
	uint32_t Settings::time_shift = 1;//poisson flows arrive after incast flows after time_shift passes
	uint32_t Settings::hdrm_ratio = 2;//actually, it is better to be set to 2

	double Settings::host_bw_Bps;
	std::vector<double> Settings::bwList[NODESCALE];
	std::vector<double> Settings::QDelayList[NODESCALE];
	std::vector<double> Settings::ctrlList[NODESCALE];
	std::vector<double> Settings::tpList[NODESCALE];

	uint32_t Settings::packet_payload = 1460;
	uint32_t Settings::MTU = 1532;		// will be calculated in main function

	// wqy: l4, for timeout retransmission
	uint32_t Settings::rto_us = 5000;

	// wqy: for qp mode
	bool Settings::reset_qp_rate = false;
	uint32_t Settings::msg_scheduler = Settings::MSG_FIFO;

	//liuchang: for flow send
	bool Settings::enable_qp_winBound = true; // defaut enbale window control for every qp

	// wqy: for switch config
	uint32_t Settings::max_qlen[QUEUENUM];
	double Settings::ecn_unit[QUEUENUM];	// ecn unit for each queue
	bool Settings::check_shared_buffer = false;
	bool Settings::piggy_ingress_ecn = false;
	uint32_t Settings::piggy_qid = -1;
	bool Settings::hier_Q = false;

	// wqy: for quota
	uint32_t Settings::free_token = 9;
	uint32_t Settings::max_bdp;
	uint32_t Settings::quota_num = 1;
	uint32_t Settings::quota_mode = 0;

	// wqy: for floodgate
	std::map<uint32_t, uint32_t> Settings::hostIp2IdMap;
	std::map<uint32_t, uint32_t> Settings::hostId2IpMap;
	// important settings
	double Settings::switch_win_m = 1.5;
	uint32_t Settings::switch_ack_mode = Settings::SWITCH_DST_CREDIT;
	bool Settings::reset_only_ToR_switch_win = false;
	uint32_t Settings::switch_byte_counter = 1;
	double Settings::switch_credit_interval = 10;	// us
	double Settings::switch_ack_th_m = 0;		// active delay SwitchACK or not(supported only when SWTICH_ACCUMULATE_CREDIT); performance has no outstanding improvement
	uint64_t Settings::switch_absolute_psn = 0;
	uint32_t Settings::switch_syn_timeout_us = 1000;
	// fixd settings(validated performance)
	uint32_t Settings::reset_isolate_down_host = 1;		// always use 1: isolate upstream and downstream at ToR (performance is not bad, but more intuitive)
	bool Settings::use_port_win = false;				// always false: heavy HoL
	double Settings::switch_port_win_m = 1.5;
	uint16_t Settings::adaptive_win = 0;  //lkx, for adaptive update window, always turn off: performance is not good
	bool Settings::adaptive_whole_switch = false; //lkx, for adaptive update window, always turn off: performance is not good

	// wqy: for BFC
	double Settings::sticky_th = 2;	// unit: HRTT
	uint32_t Settings::flowtb_size = 100;	// unit: the number of queues in switch
	bool Settings::use_hash_fid = true;		// turn on by default
	bool Settings::assign_sticky_q = true;		// turn on by default (not included in BFC paper)
	bool Settings::assign_paused_q = true;		// turn on by default (not included in BFC paper)

	// wqy: for VOQ+PFC
	double Settings::voq_pauseth_m = 2;
	uint32_t Settings::voq_pause_mode = EGRESSVQLEN_DST_PERHOP;
	uint32_t Settings::flow_sch_th = INT_MAX;
	bool Settings::ctrl_all_egress = false;

	// wqy: for congestion isolation
	double Settings::th_hol = 1.1;
	double Settings::th_congestion = 2.0;
	double Settings::th_resume = 1.0;
	double Settings::th_ciq_rate = 1.0;
	bool Settings::nearest_match = false;
	bool Settings::congestion_merge = true;
	bool Settings::dealloc_on = true;
	// liuchang: for enable pyrrha only in down port
	bool Settings::only_down_port_enable = false;

	// wqy: for QoS
	uint32_t Settings::qos_mode = Settings::QOS_NONE;
	uint32_t Settings::qos_rank_num = 10;
	std::vector<uint32_t> Settings::rank_split;

	// wqy: for PFC
	bool Settings::srcToR_dst_pfc = false;
	uint32_t Settings::pfc_dst_alpha = 8;
	double Settings::pfc_th_static = 1.5;		// should work with "USE_DYNAMIC_PFC_THRESHOLD 0" and "SRCTOR_DST_PFC 1"

	// wqy: for flow generation
	bool Settings::pktsmode;
	uint32_t Settings::maxx; // max flow size
	double Settings::homa_longflow_threshold;
	std::vector<std::pair<uint32_t,double> > Settings::cdfFromFile;
	int Settings::avgSizeFromFile;
	double Settings::threshold_slope;
	double Settings::threshold_bias;

	//lkx: for incast-mix flow generation
	bool Settings::one_incast_dst = true;//one incast dst is convenient for analysis of victim flows

	// liuchang: generate flows
	bool Settings::gen_incast_incremental = false;
	bool Settings::gen_incast_fattree_nsdi24 = false;
	uint32_t Settings::fattree_k = 8; // defaut set k=8
	uint32_t Settings::fattree_incast_m = 0;
	// generate MOE traffic
	bool Settings::gen_MOE_traffic = false;
	double Settings::moe_send_intervel = 0;
	double Settings::moe_shifft_time_mul = 0;
	uint32_t Settings::moe_traffic_size = 8000000; // 8MB
	uint32_t Settings::moe_send_times = 1;
	uint32_t Settings::moe_burst_times = 100;
	// std::vector<double>  Settings::expert_probability = {0.0020186727226848347, 0.0063083522583901085, 0.05677517032551098, 0.03305576583396417, 0.0035326772646984608, 0.07645722937168811, 0.17587686096391622, 0.012869038607115822, 0.22382033812768104, 0.0055513499873832955, 0.0035326772646984608, 0.0047943477163764825, 0.0070653545293969215, 0.15972747918243754, 0.20186727226848347, 0.026747413575574062};
	// std::vector<double>  Settings::expert_probability = {0.006953066799106034, 0.44201638937174076, 0.48025825676682393, 0.05587285820710206, 0.0017382666997765085, 0.0022349143282840824, 0.0004966476285075739, 0.0012416190712689348, 0.0024832381425378696, 0.0017382666997765085, 0.0004966476285075739, 0.0007449714427613609, 0.0009932952570151478, 0.0014899428855227217, 0.0009932952570151478, 0.00024832381425378696};
	std::vector<double> Settings::expert_probability = {0.00650104481077316, 0.4132807058277223, 0.4490364522869747, 0.05224053865799861, 0.00162526120269329, 0.00812630601346645, 0.01068028790341305, 0.005804504295333178, 0.0046436034362665425, 0.006036684467146506, 0.010912468075226375, 0.010912468075226375, 0.00650104481077316, 0.0076619456698397955, 0.0011609008590666356, 0.00487578360807987};
	//liuchang: for queue allocation in host port
	uint32_t Settings::queue_allocate_in_host = 0;

	//liuchang: for IRN
	bool Settings::use_irn = false;
	uint32_t Settings::duplicate_rtxCount = 0;

	// wqy: for statistic
	bool Settings::is_out_win = false;
	uint32_t Settings::timeout_times = 0;
	std::vector<uint64_t> Settings::drop_packets;
	std::vector<uint64_t> Settings::total_packets;
	std::ofstream Settings::switch_buffer_out[SWITCHSCALE];
	std::ofstream Settings::switch_bw_out[SWITCHSCALE];
	std::ofstream Settings::bw_out;
	std::ofstream Settings::rate_out;
	std::ofstream Settings::win_out;
	std::ofstream Settings::queuing_out;
	std::ofstream Settings::warning_out;
	uint32_t Settings::bw_interval = 10;
	uint32_t Settings::buffer_interval = 10;
	uint32_t Settings::host_num = 144;
	uint32_t Settings::switch_num = 13;
	uint32_t Settings::host_per_rack = 16;
	uint32_t Settings::tor_num = 9;
	uint32_t Settings::core_num = 4;
	uint32_t Settings::max_port_length = 0;
	uint32_t Settings::max_port_index = 0;
	uint32_t Settings::max_voq_length = 0;
	uint32_t Settings::max_switch_voq_length = 0;
	uint32_t Settings::max_switch_length = 0;

	// wqy, for routing
	uint32_t Settings::routing_mode = 0;
	uint32_t Settings::symmetic_routing_mode = 0;
	uint32_t Settings::link_node[Settings::NODESCALE][Settings::PORTNUM];
	bool Settings::up_port[Settings::SWITCHSCALE][Settings::PORTNUM];
	// ecmp
	std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::vector<int> > > Settings::node_rtTable;
	uint32_t Settings::load_imbalance_th = 0;
	uint32_t Settings::ecmp_unit = Settings::ECMP_FLOW;
	// drill
	uint32_t Settings::drill_load_mode = 0;
	uint32_t Settings::queue_statistic_interval_us = 10;

	// wqy, for host
	uint32_t Settings::cc_mode = Settings::CC_DCQCN;
	bool Settings::nack_reaction = false;
	uint32_t Settings::host_cnp = false;
	bool Settings::allow_dis_order = true;

	// wqy, for swift
	bool Settings::swift_hop_scale = false;

	/**
	 * once return true:
	 * L4 should solve dis-order of packet
	 * Receiver:
	 * 1. Reply to the corresponding ack directly after receiving the packet and set `m_received_psn.insert(curr_psn)`
	 * 2. The receiving end receives a packet marked with lasttag --> `received_last_psn_packet = true`
	 * 3. Every time a package is received, check if `m_received_last_psn_packet` and the previous packets are also received.
	 *    --> Send a lasttag ack to inform the sender that all packets have been received.
	 * Sender:
	 * 1. Send packets in order, `send_next += payload, m_sent_psn.insert(curr_psn)`
	 * 2. Received an ACK, ` m_sent psn.erase(curr_psn)`
	 * 3. The last PSN tag is sending, tag lasttag
	 * 4. Receiving the ack of lasttag tag indicates that the receiver has received it
	 * 5. Note that: for Go-Back-N that cannot be triggered by NACK, retransmission with timeout:
	 *    * `timeouted = true`
	 *    * If `send_nxt ≠ last_psn`, continue to send packets in sequence
	 *    * Otherwise, `m_timeout_psn = m_sent_psn', send packets which indicate by `m_timeout_psn`
	 */
	bool Settings::AllowDisOrder(){
		if (Settings::use_irn) return false;
		return IsPacketLevelRouting() || allow_dis_order;
	}

	bool Settings::IsPacketLevelRouting(){
		switch (Settings::routing_mode){
		case Settings::ROUTING_DRILL:
			return true;
		default:
			return false;
		}
	}

	/*
	 * hash function for ECMP
	 */
	uint32_t Settings::EcmpHash(const uint8_t* key, size_t len, uint32_t seed) {
		  uint32_t h = seed;
		  if (len > 3) {
			const uint32_t* key_x4 = (const uint32_t*) key;
			size_t i = len >> 2;
			do {
			  uint32_t k = *key_x4++;
			  k *= 0xcc9e2d51;
			  k = (k << 15) | (k >> 17);
			  k *= 0x1b873593;
			  h ^= k;
			  h = (h << 13) | (h >> 19);
			  h += (h << 2) + 0xe6546b64;
			} while (--i);
			key = (const uint8_t*) key_x4;
		  }
		  if (len & 3) {
			size_t i = len & 3;
			uint32_t k = 0;
			key = &key[i - 1];
			do {
			  k <<= 8;
			  k |= *key--;
			} while (--i);
			k *= 0xcc9e2d51;
			k = (k << 15) | (k >> 17);
			k *= 0x1b873593;
			h ^= k;
		  }
		  h ^= len;
		  h ^= h >> 16;
		  h *= 0x85ebca6b;
		  h ^= h >> 13;
		  h *= 0xc2b2ae35;
		  h ^= h >> 16;
		  return h;
	}

	/*
	 * Calculate rank bound of QoS
	 * SJF by default
	 */
	void Settings::SplitRank(std::vector<uint32_t>& metrics){
		sort(metrics.begin(), metrics.end());
		uint32_t num = metrics.size();
		for (uint32_t th = 1; th < Settings::qos_rank_num; ++th){
			uint32_t index = num*1.0*th/Settings::qos_rank_num;
//			std::cout << index << " " << metrics[index] << std::endl;
			Settings::rank_split.push_back(metrics[index]);
		}
	}

	uint32_t Settings::GetRank(uint32_t curr){
		if (curr <= rank_split[0]) return 0;
		uint32_t rank = 0;
		for (;rank+1 < Settings::qos_rank_num;++rank){
			if (curr > rank_split[rank] && curr <= rank_split[rank+1])
				break;
		}
//		std::cout << curr << " rank: " << rank << std::endl;
		return rank;
	}
}
