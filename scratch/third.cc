/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation;
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#undef PGO_TRAINING
#define PATH_TO_PGO_CONFIG "path_to_pgo_config"

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <time.h> 
#include <queue>
#include "ns3/core-module.h"
#include "ns3/qbb-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/global-route-manager.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/broadcom-node.h"
#include "ns3/packet.h"
#include "ns3/error-model.h"
#include "ns3/voq.h"
#include "ns3/switch-mmu.h"
#include <ns3/rdma.h>
#include <ns3/rdma-client.h>
#include <ns3/rdma-client-helper.h>
#include <ns3/rdma-driver.h>
#include <ns3/switch-node.h>
#include <ns3/sim-setting.h>
#include <ns3/settings.h>
#include <ns3/flow-generator.h>
#include <numeric>
#include <climits>

#include<iomanip>
#include<math.h>
#include<algorithm>
#include<random>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("GENERIC_SIMULATION");
uint32_t flow_seed = 200071;
double flow_size_mul = 1.0;

bool enable_qcn = true, use_dynamic_pfc_threshold = true;
uint32_t packet_payload_size = 1000, l2_chunk_size = 0, l2_ack_interval = 0;
uint32_t packet_mtu = 1000+78;
double pause_time = 5, simulator_stop_time = 3.01;
std::string data_rate, link_delay, topology_file, flow_file, trace_file, trace_output_file, switchwin_config_file;
std::string fct_output_file = "fct.txt";
std::string pfc_output_file = "pfc.txt";
std::string ci_output_file = "ci.txt";
std::string filename_keyword;

double alpha_resume_interval = 55, rp_timer, ewma_gain = 1 / 16;
double rate_decrease_interval = 4;
uint32_t fast_recovery_times = 5;
std::string rate_ai, rate_hai, min_rate = "100Mb/s";
std::string dctcp_rate_ai = "1000Mb/s";

bool clamp_target_rate = false, l2_back_to_zero = false;
double error_rate_per_link = 0.0;
uint32_t has_win = 1;
uint32_t global_t = 1;
uint32_t mi_thresh = 5;
bool var_win = false, fast_react = true;
bool multi_rate = true;
bool sample_feedback = false;
double u_target = 0.95;
uint32_t int_multi = 1;
bool rate_bound = true;

//assert
bool cc_set = false;

// proactive cc
uint32_t nic_token_mode = RdmaEgressQueue::NICTokenNo;	// how to send token on nic, defined in RdmaEgressQueue
uint32_t l4_token_mode = RdmaEgressQueue::L4TokenNo;		// how to generate token in l4, defined in RdmaEgressQueue
uint32_t max_unsch = 1;		// unsch phase, unit: BDP
uint32_t limit_tkn_mode = 0;		// 0: don't limit total token num
									// 1: limit qp's total token num = sum(flow size)
									// 2: limit qp's total token num = sum(flow size) - # unsch packets
uint32_t turn_sch_phase_mode = 0;	// 0: turn to sch phase upon receiving token or sending out all unsch packets;
									// 1: turn to sch phase upon sending out all unsch packets;
uint32_t rate_limiter_mode = 1;		// 0: always use +=;
									// 1: now+ when nxt_time arrival but no packets;
									// 2: always use now+
bool resume_bdp_tkn = false;			// Under data-driven, when sender turns to sch phase (if limit_tkn_mode == 2 or unsch < BDP),
									// it may haven't sent out one BDP packet.
									// Thus, token on receiver should be added up to one BDP when no congestion
// mibo
uint32_t token_cc = Settings::CC_DCQCN;		// only support DCQCN for now
uint32_t credit_Emin = 8; 	// us
uint32_t credit_Emax = 10;	// us
double credit_Ep = 0.5;
double mibo_tkn_ecn = 1;
uint32_t mibo_cc_mode = Settings::HYPR;

// ep
uint32_t ep_credit_qlen = 8;	// the number of credit

// swift
// calculate target delay
uint64_t swift_base_target = 3000;		// base target delay
uint64_t swift_h = 1500;				// per hop scaling factor
double swift_fs_range_alpha;		// max scaling range
uint64_t swift_fs_range = 0.2;
double swift_fs_max_cwnd;		// max cwnd for target scaling
double swift_fs_min_cwnd;		// min cwnd for target scaling
// adjust cwnd
double swift_ai;				// additive increment
double swift_md;				// multiplicative decrease
double swift_max_mdf;			// maximum multiplicative decrease factor
uint64_t swift_end_target;
uint32_t swift_retx_reset_threshold;	// RTO
double swift_min_cwnd;
double swift_max_cwnd;
// EWMA
double swift_ewma_alpha;

uint32_t ack_high_prio = 0;
uint32_t switch_ack_high_prio = 0;
uint64_t link_down_time = 0;
uint32_t link_down_A = 0, link_down_B = 0;

uint32_t enable_trace = 1;

uint32_t buffer_size = 16;

uint32_t qlen_dump_interval = 1000000, qlen_mon_interval = 100;
uint64_t qlen_mon_start = 2000000000, qlen_mon_end = 5000000000;
string qlen_mon_file;
string bw_mon_file = "mix/bw.txt";
string queueing_mon_file = "mix/queueing.txt";
bool output_realtime = true;

// wqy: for flow generation
uint32_t qpNumOfHostPair = 1;
uint32_t seed = 1;
uint32_t avg_message_per_qp = 10;
uint32_t flow_cdf = 4;
uint32_t arrival_mode = 1;
uint32_t host_per_rack = 0, tor_num = 0, core_num = 0;
double load = 0.8;
bool flow_from_file = 0;
uint32_t storage_qp_num = 64;
uint32_t storage_times = 3;
uint32_t storage_iodepth = 8;
uint32_t storage_message_size = 8192;
uint64_t min_time = 0;
uint64_t max_time = 0;
uint32_t qp_max = 0;
uint32_t incast_mix = 0;
uint32_t incast_interval = 0;
uint32_t incast_cdf = 0;
double incast_load = 0.1;
uint32_t incast_time = 0;
uint32_t incast_flow_size = 1;
double incast_scale = 0.0;
uint32_t incremental_incast_time; // how many incast dst at one time(only use when gen_incast_incremental==true)
bool reset_per_qp_msg = true;
uint32_t intra_src = 2;
uint32_t inter_src = 2;
uint32_t flow_size = 1000000;
uint32_t msg_size = 1000000;
uint32_t remote_qp_num = 8;
uint64_t poisson_total = 0; // for bfc workload

//for generating equal-sized flows
double equal_flow_size = 1;//unit: bdp

unordered_map<uint64_t, uint32_t> rate2kmax, rate2kmin;
unordered_map<uint64_t, double> rate2pmax;

uint32_t default_prio = 3;
// switch queue configure
// pfc control
std::vector<bool> pfc_ctrls;	// whether to send paused/resume based on ingress counter, i.e. the traditional PFC control
std::vector<bool> pfc_queue_ctrls;	// whether to do pause/resume on switch queues
uint16_t pfc_dst_nic = Settings::PAUSE_PG;		// the fine to pause on nic (should work with SToR); (the fine of pause/resume on switch port is decided by mmu->pfc_mode)
// rate_limiter
std::vector<double> rate_limiters;
// priority
std::vector<std::vector<uint32_t> > priorities;

uint64_t nic_rate;

uint64_t maxRtt, maxBdp, maxHRTT;
uint64_t minHRTT = INT_MAX;

// IRN config
uint32_t max_bitmap_size; //without use, we just use CC window control
uint32_t rto_high; // us
uint32_t rto_low; // us
uint32_t rto_low_threshold;

uint32_t flow_num;
uint32_t qp_flow;
bool has_test_flow;
uint32_t flow_index = 0;

struct Interface{
	uint32_t idx;
	bool up;
	uint64_t delay;
	uint64_t bw;

	Interface() : idx(0), up(false){}
};

/*
 * For flow generation
 */
uint32_t qp_global = 0;
double Get_Oracle_Fct(uint32_t s, uint32_t t, uint32_t flow_size);
double Get_Oracle_Rcv_Fct(uint32_t s, uint32_t t, uint32_t flow_size);
struct Flow{
	bool isTest;
	uint32_t flow_id;
	uint32_t src;
	uint32_t dst;
	uint32_t size;
	uint32_t qp_id_input;
	uint32_t msg_seq_input;
	uint32_t qp_id;
	uint32_t msg_seq;
	int64_t oracle_ns;
	int64_t oracle_rcv_ns;
	Time startTime;
	Time stopTime;
	Time finishTime;
	Time rcvFinishTime;
	uint64_t dataQueuingTime[Settings::MAXHOP];
	static double totalAvgQueuingTime[Settings::MAXHOP];

	Flow(uint32_t p_src, uint32_t p_dst, uint32_t p_size, double p_startTime){
		src = p_src;
		dst = p_dst;
		size = p_size;
		msg_seq_input = msg_seq = 0;
		qp_id_input = qp_id = qp_global++;
		flow_id = flow_index++;
		startTime = Seconds(p_startTime);
		oracle_ns = Get_Oracle_Fct(src,dst,(uint32_t)size)*1e9;
		oracle_rcv_ns = Get_Oracle_Rcv_Fct(src,dst,(uint32_t)size)*1e9;
		stopTime = Time(0);
		finishTime = Time(0);
		rcvFinishTime = Time(0);
		isTest = false;
		for (uint32_t i = 0; i < Settings::MAXHOP; ++i){
			dataQueuingTime[i] = 0;
		}
	}

	Flow(uint32_t p_src, uint32_t p_dst, uint32_t p_size, double p_startTime, uint32_t p_qp_id, uint32_t p_msg_seq, bool reset){
		src = p_src;
		dst = p_dst;
		size = p_size;
		if (!reset){
			qp_id_input = qp_id = p_qp_id;
			msg_seq_input = msg_seq = p_msg_seq;
		}else{
			qp_id_input = p_qp_id;
			msg_seq_input = p_msg_seq;
			msg_seq = 0;
			qp_id = qp_global;
		}
		qp_global = std::max(qp_id, qp_global) + 1;
		flow_id = flow_index++;
		startTime = Seconds(p_startTime);
		oracle_ns = Get_Oracle_Fct(src,dst,(uint32_t)size)*1e9;
		oracle_rcv_ns = Get_Oracle_Rcv_Fct(src,dst,(uint32_t)size)*1e9;
		stopTime = Time(0);
		finishTime = Time(0);
		rcvFinishTime = Time(0);
		isTest = false;
		for (uint32_t i = 0; i < Settings::MAXHOP; ++i){
			dataQueuingTime[i] = 0;
		}
	}

	void SetStopTime(double time){
		stopTime = Seconds(time);
	}

	bool IsComplete(){
		return finishTime != Time(0);
	}

	bool IsRcvFinished(){
		return rcvFinishTime != Time(0);
	}
};
double Flow::totalAvgQueuingTime[Settings::MAXHOP];


NodeContainer n;
queue<Flow> flows;
queue<Flow> test_flows;
map<Ptr<Node>, map<Ptr<Node>, Interface> > nbr2if;
// Mapping destination to next hop for each node: <node, <dest, <nexthop0, ...> > >
map<Ptr<Node>, map<Ptr<Node>, vector<Ptr<Node> > > > nextHop;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairDelay;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairTxDelay;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairBw;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairBdp;
map<Ptr<Node>, map<Ptr<Node>, uint64_t> > pairRtt;

/* ------------------------------- wqy ----------------------------------- */
uint32_t GetWin(uint32_t src, uint32_t dst){
	return has_win?(global_t==1?maxBdp:pairBdp[n.Get(src)][n.Get(dst)]):0;
}

uint64_t GetBaseRTT(uint32_t src, uint32_t dst){
	return global_t==1?maxRtt:pairRtt[n.Get(src)][n.Get(dst)];
}

typedef std::priority_queue<std::pair<uint32_t, uint32_t>,std::vector<std::pair<uint32_t, uint32_t> >, std::greater<std::pair<uint32_t, uint32_t> > >  priority_uint32_uint32_queue;

std::vector<std::vector<uint32_t> > pd; // propagation delay (ns)
std::vector<std::vector<uint32_t> > td; // transmission delay of MTU bytes (ns)
std::map<uint32_t, Flow> flows_map;
std::set<uint32_t> unfinished_flows;
std::set<uint32_t> unfinished_rcv_flows;
std::vector<double> all_fct;
std::vector<double> all_slowdown;
std::vector<double> all_rcv_fct;
std::vector<double> all_rcv_slowdown;

std::vector<double> all_fct_test;
std::vector<double> all_slowdown_test;

void realtime_buffer(uint32_t switch_id, uint32_t queue_id, uint32_t port_all, uint32_t port_data, uint32_t egress_counter_data, uint32_t voq_group0_buffer, uint32_t voq_buffer, uint32_t switch_buffer){
	if (switch_id >= Settings::host_num){
		Settings::switch_buffer_out[switch_id - Settings::host_num] << Simulator::Now() << " " << queue_id
				<< " " << port_all << " " << port_data << " " << egress_counter_data << " "
				<< voq_group0_buffer << " " << voq_buffer << " " << switch_buffer << std::endl;
	}
}

void realtime_switch_bw(uint32_t switch_id, uint32_t queue_id, uint64_t data, uint64_t ctrl, uint64_t switchACK, uint64_t rcv_data, uint64_t rcv_ctrl, uint64_t rcv_switchACK){
	if (switch_id >= Settings::host_num){
		Settings::switch_bw_out[switch_id - Settings::host_num] << Simulator::Now() << " " << queue_id
				<< " " << data << " " << ctrl << " " << switchACK 
				<< " " << rcv_data << " " << rcv_ctrl << " " << rcv_switchACK << std::endl;
	}
}

void Floyd(std::vector<std::vector<uint32_t> >& G, std::vector<std::vector<uint32_t> >& nextHop){
	uint32_t n = G.size();
	for(uint32_t k=0;k<n;++k)
		for(uint32_t i=0;i<n;++i)
			for(uint32_t j=0;j<n;++j){
				if(G[i][k]+G[k][j]<G[i][j]){
					G[i][j] = G[i][k]+G[k][j];
					nextHop[i][j] = k;
				}
			}
}

double GetDefaultDelayUs(uint32_t s,uint32_t t,uint32_t pkt_size) {
	return (pd[s][t]+td[s][t]/(double)Settings::MTU*pkt_size)/1000.0;
}

double Get_Oracle_Fct(uint32_t s, uint32_t t, uint32_t flow_size){

	int full_pkts = flow_size/packet_payload_size;
	int left_bytes = flow_size%packet_payload_size;
	double fct = 0;

	if(full_pkts>=1){
		double first_delay = GetDefaultDelayUs(s, t, packet_mtu) + GetDefaultDelayUs(s, t, 84);
		fct = first_delay/1e6 + (full_pkts-1)*packet_mtu/Settings::host_bw_Bps+(left_bytes+78)/Settings::host_bw_Bps;//s
	}

	else
		fct = GetDefaultDelayUs(s, t, left_bytes)/1e6 + GetDefaultDelayUs(s, t, 84)/1e6;

	return fct;
}

double Get_Oracle_Rcv_Fct(uint32_t s, uint32_t t, uint32_t flow_size){

	int full_pkts = flow_size/packet_payload_size;
	int left_bytes = flow_size%packet_payload_size;
	double fct = 0;

	if(full_pkts>=1){
		double first_delay = GetDefaultDelayUs(s, t, packet_mtu);
		fct = first_delay/1e6 + (full_pkts-1)*packet_mtu/Settings::host_bw_Bps+(left_bytes+78)/Settings::host_bw_Bps;//s
	}

	else
		fct = GetDefaultDelayUs(s, t, left_bytes)/1e6;

	return fct;
}

void printUnorderedMCTStatistic(vector<double> all_fct, vector<double> all_slowdown, string fct_s, string slowdown_s){
	sort(all_fct.begin(),all_fct.end());
	sort(all_slowdown.begin(),all_slowdown.end());
	int size = all_fct.size();

	double sum=0;
	for(int i=0;i<size;i++){
		sum+=all_fct[i];
	}
	std::cout<<std::endl;
	std::cout << fct_s << ": avg 20% 50% 90% 95% 99%"<<std::endl;
	std::cout<<sum/double(size)<<std::endl;
	std::cout<<all_fct[(size-1)*0.25]<<std::endl;//small
	std::cout<<all_fct[(size-1)*0.5]<<std::endl;//middle
	std::cout<<all_fct[(size-1)*0.9]<<std::endl; //0-(size-1)
	std::cout<<all_fct[(size-1)*0.95]<<std::endl;
	std::cout<<all_fct[(size-1)*0.99]<<std::endl;

	double sum_slowdown=0;
	for(int i=0;i<size;i++){
		sum_slowdown+=all_slowdown[i];//us
	}
	std::cout<<std::endl;
	std::cout<< slowdown_s << ": avg 20% 50% 90% 95% 99%"<<std::endl;
	std::cout<<sum_slowdown/double(size)<<std::endl;
	std::cout<<all_slowdown[(size-1)*0.25]<<std::endl;//small
	std::cout<<all_slowdown[(size-1)*0.5]<<std::endl;//middle
	std::cout<<all_slowdown[(size-1)*0.9]<<std::endl; //0-(size-1)
	std::cout<<all_slowdown[(size-1)*0.95]<<std::endl;
	std::cout<<all_slowdown[(size-1)*0.99]<<std::endl;

	std::cout << std::endl;
}

void printOrderedMCTForQPMode(std::map<uint32_t, Flow>& senders, bool isTest = false) {
	std::map<uint32_t, priority_uint32_uint32_queue> qps;// <qp_id, [<msg_seq, sender_index>]>
	std::vector<double> all_mct;
	std::vector<double> all_mct_slowdown;
	std::vector<double> all_mct_slowdown_based_on_fact_fct_before;

	std::map<uint32_t, Flow>::iterator flow_it = senders.begin();
	while(flow_it != senders.end()){
		if (flow_it->second.isTest == isTest){
			std::map<uint32_t, priority_uint32_uint32_queue >::iterator it = qps.find(flow_it->second.qp_id_input);
			if (it != qps.end()){
				it->second.push(std::make_pair(flow_it->second.msg_seq_input, flow_it->first));
			}else{
				priority_uint32_uint32_queue qp;
				qp.push(std::make_pair(flow_it->second.msg_seq_input, flow_it->first));
				qps[flow_it->second.qp_id_input] = qp;
			}
		}
		flow_it++;
	}

	std::map<uint32_t, priority_uint32_uint32_queue >::iterator it = qps.begin();
	std::cout << "OrderedMCT: " << std::endl;

	// traverse all QP to calculate MCT
	while (it != qps.end()) {

		// finished_above(ns) is the ordered message finished time before this message
		// oracle_finished_above(ns) is the ordered message oracle finished time before this message
		int64_t finish_above = 0, transmit_empty_oracle_above = 0;

		// traverse all message of this QP in order
		while (!it->second.empty()) {
			int64_t oracle_finished_ns = 0;
			uint32_t sender_i = it->second.top().second;
			it->second.pop();

			// calculate fact ordered mct
			int64_t orderedFinishTime = std::max(finish_above, senders.at(sender_i).finishTime.GetNanoSeconds());
			int64_t currMCT = orderedFinishTime - senders.at(sender_i).startTime.GetNanoSeconds();
			finish_above = orderedFinishTime;

			// get oracle MCT
			// ---> 1. calculate oracle MCT
			// ---> 2. update oracle_finished_above and calculate oracle MCT of this message
			int64_t transmit_ns = 0;
			int64_t pkts = (uint64_t) senders.at(sender_i).size;
			if (Settings::pktsmode) {
				pkts = packet_payload_size * (int64_t) senders.at(sender_i).size;
				transmit_ns = 1e9 * packet_mtu
						* (int64_t) senders.at(sender_i).size
						/ Settings::host_bw_Bps;
			} else {
				transmit_ns = 1e9
						* ((int64_t) senders.at(sender_i).size / packet_payload_size
								* packet_mtu
								+ ((int64_t) senders.at(sender_i).size
										% packet_payload_size + 84))
						/ Settings::host_bw_Bps;
			}
			int64_t oracle_ns = senders.at(sender_i).oracle_ns;

			// calculate oracle mct based on fact mct before
			if (senders.at(sender_i).startTime.GetNanoSeconds()
					< transmit_empty_oracle_above) {
				oracle_finished_ns = transmit_empty_oracle_above
						+ oracle_ns;
				transmit_empty_oracle_above += transmit_ns;
			} else {
				oracle_finished_ns =
						senders.at(sender_i).startTime.GetNanoSeconds()
								+ oracle_ns;
				transmit_empty_oracle_above =
						senders.at(sender_i).startTime.GetNanoSeconds()
								+ transmit_ns;
			}
			int64_t currOracleMCT = oracle_finished_ns
					- senders.at(sender_i).startTime.GetNanoSeconds();

			// print message info
			all_mct.push_back(currMCT / 1e3);
			all_mct_slowdown.push_back((double) currMCT / currOracleMCT);
			std::cout << senders.at(sender_i).src << "\t"
					<< senders.at(sender_i).dst << "\t" << pkts
					<< "\t" << senders.at(sender_i).startTime.GetTimeStep()
					<< "\t" << senders.at(sender_i).qp_id_input << "\t"
					<< senders.at(sender_i).msg_seq_input << "\t"
					<< (senders.at(sender_i).rcvFinishTime - senders.at(sender_i).startTime).GetNanoSeconds()/1e3 << "\t"
					<< senders.at(sender_i).oracle_rcv_ns/1e3 << "\t"
					<< (double)(senders.at(sender_i).rcvFinishTime
							- senders.at(sender_i).startTime).GetNanoSeconds()/senders.at(sender_i).oracle_rcv_ns << "\t"
					<< (senders.at(sender_i).finishTime
							- senders.at(sender_i).startTime).GetNanoSeconds()
							/ 1e3 << "\t" << oracle_ns / 1e3 << "\t"
					<< (double) (senders.at(sender_i).finishTime
							- senders.at(sender_i).startTime).GetNanoSeconds()
							/ oracle_ns << "\t" << currMCT / 1e3 << "\t"
					<< currOracleMCT / 1e3 << "\t"
					<< (double) currMCT / currOracleMCT << "\t"
					<< senders.at(sender_i).qp_id << "\t"
					<< senders.at(sender_i).msg_seq << "\t"
					<< std::endl;

		}

		it++;
	}

	printUnorderedMCTStatistic(all_mct, all_mct_slowdown, "ordered mct", "ordered slowdown");
}

void rcv_finish(FILE* fout, Ptr<RdmaRxOperation> rx){

	uint32_t flow_id = rx->m_flowId;
	Flow curr = flows_map.at(flow_id);

	int64_t fct = Simulator::Now().GetNanoSeconds() - curr.startTime.GetNanoSeconds();
	fprintf(fout, "rcv %u\t%u\t%u\t%lu\t%u\t%u\t%f\t%f\t%f \n", curr.src, curr.dst, curr.size, curr.startTime.GetTimeStep(), curr.qp_id, curr.msg_seq, fct/1e3, curr.oracle_rcv_ns/1e3, (double)fct/curr.oracle_rcv_ns);
	fflush(fout);

	if (unfinished_rcv_flows.find(flow_id) != unfinished_rcv_flows.end()){
		unfinished_rcv_flows.erase(flow_id);

		// calcu avg queuing time
		Settings::queuing_out << flow_id << " "
				<< curr.src << " "
				<< curr.dst << " "
				<< curr.size << " "
				<< curr.startTime.GetNanoSeconds() << " "
				<< fct/1e3 << " "
				<< curr.oracle_rcv_ns/1e3 << " ";
		for (uint32_t i = 0; i < Settings::MAXHOP; ++i){
			if (rx->m_queuingTime[i].size() == 0) {
				Settings::queuing_out << "0 ";
				continue;
			}
			uint64_t sum = 0;
			for (uint64_t j = 0; j < rx->m_queuingTime[i].size(); ++j){
				uint64_t curr = rx->m_queuingTime[i][j];
				sum += curr;
			}
			double mean =  sum / rx->m_queuingTime[i].size();
			Settings::queuing_out << mean << " ";
			curr.dataQueuingTime[i] = mean;
			Flow::totalAvgQueuingTime[i] += mean;
		}
		Settings::queuing_out << std::endl;

		flows_map.at(flow_id).rcvFinishTime = Simulator::Now();
		all_rcv_fct.push_back(fct/1e3);
		all_rcv_slowdown.push_back((double)fct/curr.oracle_rcv_ns);

		std::cout << "qp rcv finished " << flows_map.size() - unfinished_rcv_flows.size() << " "
				<< Simulator::Now() << " "
				<< curr.src << " "
				<< curr.dst << " "
				<< curr.size << " "
				<< curr.qp_id << " "
				<< curr.msg_seq << " "
				<< fct/1e3 << " "
				<< (double)fct/curr.oracle_rcv_ns << " "
				<< std::endl;
	}else{
		std::cout << "rcv finished flow finish again: " << Simulator::Now() << " "
					<< curr.src << " "
					<< curr.dst << " "
					<< curr.size << " "
					<< curr.qp_id << " "
					<< curr.msg_seq << " "
					<< fct/1e3 << " "
					<< (double)fct/curr.oracle_rcv_ns << " "
					<< std::endl;
	}
}

void test_msg_finish(FILE* fout, Ptr<RdmaSndOperation> q){

	int64_t fct = Simulator::Now().GetNanoSeconds() - q->startTime.GetNanoSeconds();
	fprintf(fout, "%u\t%u\t%u\t%lu\t%u\t%u\t%f\t%f\t%f \n", q->m_src, q->m_dst, q->m_size,
			q->startTime.GetTimeStep(), q->m_qpid, q->m_msgSeq, fct/1e3,
			flows_map.at(q->m_flowId).oracle_ns/1e3, (double)fct/flows_map.at(q->m_flowId).oracle_ns);
	fflush(fout);

	if (unfinished_flows.find(q->m_flowId) != unfinished_flows.end()){
		unfinished_flows.erase(q->m_flowId);
		flows_map.at(q->m_flowId).finishTime = Simulator::Now();
		all_fct_test.push_back(fct/1e3);
		all_slowdown_test.push_back((double)fct/flows_map.at(q->m_flowId).oracle_ns);

		static uint32_t finished_flow_num_test = 0;
		++finished_flow_num_test;
		std::cout << "test msg finished " << finished_flow_num_test << " "
				<< Simulator::Now() << " "
				<< q->m_src << " "
				<< q->m_dst << " "
				<< q->m_size << " "
				<< q->m_qpid << " "
				<< q->m_msgSeq << " "
				<< fct/1e3 << " "
				<< (double)fct/flows_map.at(q->m_flowId).oracle_ns << " "
				<< std::endl;
	}else{
		std::cout << "finished flow finish again: " << Simulator::Now() << " "
					<< q->m_src << " "
					<< q->m_dst << " "
					<< q->m_size << " "
					<< q->m_qpid << " "
					<< q->m_msgSeq << " "
					<< fct/1e3 << " "
					<< (double)fct/flows_map.at(q->m_flowId).oracle_ns << " "
					<< std::endl;
	}
}

void msg_finish(FILE* fout, Ptr<RdmaSndOperation> q){

	if (q->m_isTestFlow){
		test_msg_finish(fout, q);
		return;
	}

	int64_t fct = Simulator::Now().GetNanoSeconds() - q->startTime.GetNanoSeconds();
	fprintf(fout, "%u\t%u\t%u\t%lu\t%u\t%u\t%f\t%f\t%f \n", q->m_src, q->m_dst, q->m_size, q->startTime.GetTimeStep(), q->m_qpid, q->m_msgSeq,
			fct/1e3, flows_map.at(q->m_flowId).oracle_ns/1e3, (double)fct/flows_map.at(q->m_flowId).oracle_ns);
	fflush(fout);

	if (unfinished_flows.find(q->m_flowId) != unfinished_flows.end()){
		unfinished_flows.erase(q->m_flowId);
		flows_map.at(q->m_flowId).finishTime = Simulator::Now();
		all_fct.push_back(fct/1e3);
		all_slowdown.push_back((double)fct/flows_map.at(q->m_flowId).oracle_ns);

		static uint32_t finished_flow_num = 0;
		++finished_flow_num;
		std::cout << "msg finished " << finished_flow_num << " "
				<< Simulator::Now() << " "
				<< q->m_src << " "
				<< q->m_dst << " "
				<< q->m_size << " "
				<< q->m_qpid << " "
				<< q->m_msgSeq << " "
				<< fct/1e3 << " "
				<< (double)fct/flows_map.at(q->m_flowId).oracle_ns << " "
				<< std::endl;
		if (flow_num == finished_flow_num){
//		if (178 == finished_flow_num){
			printOrderedMCTForQPMode(flows_map);
			if (has_test_flow){
				std::cout << "\nTest flows statistic: " << std::endl;
				printOrderedMCTForQPMode(flows_map, true);
			}

			std::cout << "rcv finished: " << all_rcv_fct.size();
			printUnorderedMCTStatistic(all_rcv_fct, all_rcv_slowdown, "mct(rcv)", "slowdown(rcv)");

			std::cout << "src finished: " << all_fct.size();
			printUnorderedMCTStatistic(all_fct, all_slowdown, "mct(src)", "slowdown(src)");

			if (has_test_flow){
				std::cout << "\nTest flows: " << all_fct_test.size();
				printUnorderedMCTStatistic(all_fct_test, all_slowdown_test, "mct(test)", "slowdown(test)");
			}
			Simulator::Stop();
		}
	}else{
		std::cout << "finished flow finish again: " << Simulator::Now() << " "
				<< q->m_src << " "
				<< q->m_dst << " "
				<< q->m_size << " "
				<< q->m_qpid << " "
				<< q->m_msgSeq << " "
				<< fct/1e3 << " "
				<< (double)fct/flows_map.at(q->m_flowId).oracle_ns << " "
				<< std::endl;
	}
}

/* ------------------------------- wqy ----------------------------------- */

void get_pfc(FILE* fout, Ptr<QbbNetDevice> dev, uint32_t type, uint32_t qIndex){
	fprintf(fout, "%lu %u %u %u %u %u\n", Simulator::Now().GetTimeStep(), dev->GetNode()->GetId(), dev->GetNode()->GetNodeType(), dev->GetIfIndex(), qIndex, type);
	fflush(fout);
}

void get_ci(FILE* fout, Ptr<QbbNetDevice> dev, uint8_t type, uint32_t root_switch, uint32_t root_port, uint32_t old_switch, uint32_t old_port){
	fprintf(fout, "%lu %u %u %u %u %u %u %u %u\n", Simulator::Now().GetTimeStep(), dev->GetNode()->GetId(), dev->GetNode()->GetNodeType(), dev->GetIfIndex(), +type, root_switch, root_port, old_switch, old_port);
	fflush(fout);
}

struct QlenDistribution{
	vector<uint32_t> cnt; // cnt[i] is the number of times that the queue len is i KB

	void add(uint32_t qlen){
		uint32_t kb = qlen / 1000;
		if (cnt.size() < kb+1)
			cnt.resize(kb+1);
		cnt[kb]++;
	}
};

map<uint32_t, map<uint32_t, QlenDistribution> > queue_result;//queue each switch.port
map<uint32_t, QlenDistribution> queue_result_total;//queue each switch as a whole
uint32_t max_port = 0;
uint32_t max_switch = 0;
void monitor_buffer(FILE* qlen_output, NodeContainer *n){
	for (uint32_t i = 0; i < n->GetN(); i++){
		if (n->Get(i)->GetNodeType() == 1){ // is switch
			Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n->Get(i));
			if (queue_result.find(i) == queue_result.end())
				queue_result[i];

			uint32_t total_size = 0;
			for (uint32_t j = 1; j < sw->GetNDevices(); j++){//i: switch id; j: port id
				uint32_t size = 0;
				for (uint32_t k = 0; k < sw->m_mmu->egress_bytes[j].size(); k++)
					size += sw->m_mmu->egress_bytes[j][k];//add up the several queue length among a port
				queue_result[i][j].add(size);//the queue length of each port j of switch i
				max_port = max(max_port, size);

				total_size += size;
			}
			queue_result_total[i].add(total_size);//the queue length of the total switch i
			max_switch = max(max_switch, total_size);
		}
	}
	if (Simulator::Now().GetTimeStep() % qlen_dump_interval == 0){//dump_interval is the interval cout 100us, monitor interval is the interval write 100ns
		fprintf(qlen_output, "time: %lu\n", Simulator::Now().GetTimeStep());
		for (auto &it0 : queue_result){
			for (auto &it1 : it0.second){
				fprintf(qlen_output, "%u %u", it0.first, it1.first);//it0.first -> switch id; it1.first -> port id
				auto &dist = it1.second.cnt; //it1.second -> QlenDistribution
				for (uint32_t i = 0; i < dist.size(); i++)
					fprintf(qlen_output, " %uth: %u", i, dist[i]);
				fprintf(qlen_output, "\n");
			}
		}

		//added by lkx, to analysis the total used buffer in a switch
		for(auto &it0 : queue_result_total){
			fprintf(qlen_output, "%u-total", it0.first);//switch id
			auto &dist = it0.second.cnt;
			for(uint32_t i=0; i< dist.size(); ++i)
				fprintf(qlen_output, " %uth: %u",i, dist[i]);
			fprintf(qlen_output, "\n");
		}
		fprintf(qlen_output, "maxPortLength %u maxSwitchLength %u\n", max_port, max_switch);
		fflush(qlen_output);
	}
	if (Simulator::Now().GetTimeStep() < qlen_mon_end)
		Simulator::Schedule(NanoSeconds(qlen_mon_interval), &monitor_buffer, qlen_output, n);
}

//added by lkx
double analysis_bw(std::vector<double> a[], uint32_t totalNodes, uint32_t hosts, FILE* bw_output, uint16_t flag){
	double avg = 0;
	uint32_t a_size = Settings::host_num;//the maximum node size
	uint32_t start_index = 0;

	//flag == 0 analysis bw; otherwise, analysis queuing delay

	for(uint32_t i=0;i<a_size;i++){
		if(a[i].size() != 0){
			start_index = i;
			break;
		}
	}

	//first, get rid of the zero elements of the vector
	for(uint32_t i=start_index;i<a_size;++i){
		if(a[i].empty()) continue;

		//while(!a[i].empty() && a[i].back() == 0){
		while(!a[i].empty() && a[i].back() <= 0){//=0 or = -nan
			a[i].pop_back();
		}
	}

	vector<double> bw_vector;
	for(uint32_t i=0;i<a[start_index].size();++i){//the bw of the i-th index
		bool stop = false;
		double sum=0;
		uint32_t cnt = 0;//to cnt the queueing delay which is not NaN
		//std::cout<<i<<"-th time: ";
		for(uint32_t j=0;j<totalNodes;++j){
			if(a[j].size() == 0){
				//std::cout<<"null "<<std::endl;
				continue;//this is the bw of switch
			}
			if(i<a[j].size()){
				if(flag == 0)//bw
					sum+=a[j][i];
				else {//queueing time
					if(a[j][i]>=0){ // not NaN
						sum+=a[j][i];
						++cnt;
					}
				}
				//std::cout<<a[j][i]<<" ";
			}else stop = true;
		}
		if(stop) break;
		if(flag == 0){
			fprintf(bw_output, " %.3f",sum/hosts);
			bw_vector.push_back(sum/hosts);
		}else{
			if(cnt > 0){
				fprintf(bw_output, " %.3f",sum/cnt);
				bw_vector.push_back(sum/cnt);
			}
		}
	}

	uint64_t count = 0;
	for(uint32_t i=0;i<bw_vector.size();++i){
		if(i>=bw_vector.size()/4 && i<bw_vector.size()*3/4){
			avg += bw_vector[i];
			++count;
		}
	}
	avg /= count;

	fprintf(bw_output, " count%lu: %.3f",count, avg);
	fprintf(bw_output, "\n\n");
	fflush(bw_output);

	return avg;
}

void CalculateRoute(Ptr<Node> host){
	// queue for the BFS.
	vector<Ptr<Node> > q;
	// Distance from the host to each node.
	map<Ptr<Node>, int> dis;
	map<Ptr<Node>, uint64_t> delay;
	map<Ptr<Node>, uint64_t> txDelay;
	map<Ptr<Node>, uint64_t> bw;
	// init BFS.
	q.push_back(host);
	dis[host] = 0;
	delay[host] = 0;
	txDelay[host] = 0;
	bw[host] = 0xfffffffffffffffflu;
	// BFS.
	for (int i = 0; i < (int)q.size(); i++){
		Ptr<Node> now = q[i];
		int d = dis[now];
		for (auto it = nbr2if[now].begin(); it != nbr2if[now].end(); it++){
			// skip down link
			if (!it->second.up)
				continue;
			Ptr<Node> next = it->first;
			// If 'next' have not been visited.
			if (dis.find(next) == dis.end()){
				dis[next] = d + 1;
				delay[next] = delay[now] + it->second.delay;
				txDelay[next] = txDelay[now] + packet_payload_size * 1000000000lu * 8 / it->second.bw;
				bw[next] = std::min(bw[now], it->second.bw);
				// we only enqueue switch, because we do not want packets to go through host as middle point
				if (next->GetNodeType() == 1)
					q.push_back(next);
			}
			// if 'now' is on the shortest path from 'next' to 'host'.
			if (d + 1 == dis[next]){
				nextHop[next][host].push_back(now);
			}
		}
	}
	for (auto it : delay)
		pairDelay[it.first][host] = it.second;
	for (auto it : txDelay)
		pairTxDelay[it.first][host] = it.second;
	for (auto it : bw)
		pairBw[it.first][host] = it.second;
}

void CalculateRoutes(NodeContainer &n){
	for (int i = 0; i < (int)n.GetN(); i++){
		Ptr<Node> node = n.Get(i);
		if (node->GetNodeType() == 0)
			CalculateRoute(node);
	}
}

void SetRoutingEntries(){
	// For each node.
	for (auto i = nextHop.begin(); i != nextHop.end(); i++){
		Ptr<Node> node = i->first;
		auto &table = i->second;
		for (auto j = table.begin(); j != table.end(); j++){
			// The destination node.
			Ptr<Node> dst = j->first;
			// The IP address of the dst.
			Ipv4Address dstAddr = dst->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
			// The next hops towards the dst.
			vector<Ptr<Node> > nexts = j->second;
			for (int k = 0; k < (int)nexts.size(); k++){
				Ptr<Node> next = nexts[k];
				uint32_t interface = nbr2if[node][next].idx;
				if (node->GetNodeType() == 1){
					DynamicCast<SwitchNode>(node)->AddTableEntry(dstAddr, interface);
				}else{
					node->GetObject<RdmaDriver>()->m_rdma->AddTableEntry(dstAddr, interface);
				}
			}
		}
	}
}

// take down the link between a and b, and redo the routing
void TakeDownLink(NodeContainer n, Ptr<Node> a, Ptr<Node> b){
	if (!nbr2if[a][b].up)
		return;
	// take down link between a and b
	nbr2if[a][b].up = nbr2if[b][a].up = false;
	nextHop.clear();
	CalculateRoutes(n);
	// clear routing tables
	for (uint32_t i = 0; i < n.GetN(); i++){
		if (n.Get(i)->GetNodeType() == 1)
			DynamicCast<SwitchNode>(n.Get(i))->ClearTable();
		else
			n.Get(i)->GetObject<RdmaDriver>()->m_rdma->ClearTable();
	}
	DynamicCast<QbbNetDevice>(a->GetDevice(nbr2if[a][b].idx))->TakeDown();
	DynamicCast<QbbNetDevice>(b->GetDevice(nbr2if[b][a].idx))->TakeDown();
	// reset routing table
	SetRoutingEntries();

	// redistribute qp on each host
	for (uint32_t i = 0; i < n.GetN(); i++){
		if (n.Get(i)->GetNodeType() == 0)
			n.Get(i)->GetObject<RdmaDriver>()->m_rdma->RedistributeQp();
	}
}

uint64_t get_nic_rate(NodeContainer &n){
	for (uint32_t i = 0; i < n.GetN(); i++)
		if (n.Get(i)->GetNodeType() == 0)
			return DynamicCast<QbbNetDevice>(n.Get(i)->GetDevice(1))->GetDataRate().GetBitRate();
}

/*
 * Get workload from file
 */
void getCdfFromFile(uint32_t type){

	std::string distFileName="";
	if(type==4)//W1
		distFileName = std::string("distribution/FacebookKeyValueMsgSizeDist.txt");
	else if(type==5)//W2
		distFileName = std::string("distribution/Google_SearchRPC.txt");
	else if(type==6)//W3
		distFileName = std::string("distribution/Google_AllRPC.txt");
	else if(type==7)//W4
		distFileName = std::string("distribution/Facebook_HadoopDist_All.txt");
	else if(type==8)//W5
		distFileName = std::string("distribution/DCTCP_MsgSizeDist.txt");//...
	else if(type==13)
		distFileName = std::string("distribution/Facebook_WebServerDist_IntraCluster.txt");//web sever
	else if(type==14)
		distFileName = std::string("distribution/Facebook_CacheFollowerDist_IntraCluster.txt");//Cache Follower
	else
		NS_FATAL_ERROR("no such wordload!");

	std::ifstream distFileStream;
	distFileStream.open(distFileName.c_str());

	if (!distFileStream) std::cout<<"error opening file"<<std::endl;

	std::string avgMsgSizeStr;
	std::string sizeProbStr;
	Settings::cdfFromFile.clear();

	getline(distFileStream, avgMsgSizeStr);
	sscanf(avgMsgSizeStr.c_str(), "%d", &Settings::avgSizeFromFile);

	while(getline(distFileStream,sizeProbStr)){
		int msgSize;
		double prob;
		sscanf(sizeProbStr.c_str(), "%d %lf", &msgSize, &prob);
		if(type!=8)
			Settings::cdfFromFile.push_back(std::make_pair(msgSize,prob));
		else
			Settings::cdfFromFile.push_back(std::make_pair(msgSize*packet_payload_size,prob));
		//std::cout<<msgSize<<std::endl;
	}
	distFileStream.close();

	if(type==4){
		int sizeOffset = Settings::cdfFromFile.back().first;
		double probOffset = Settings::cdfFromFile.back().second;

		// Generalized pareto distribution parameters
		const double k = 0.348238;
		const double sigma = 214.476;

		const double maxProb = 0.999;
		double probIncr = (maxProb - probOffset) / 1000;
		double probInit = probOffset + probIncr;
		uint32_t size = 0;
		for (double prob = probInit; prob <= maxProb; prob += probIncr) {
			size = (uint32_t)(round(sizeOffset +
				(pow((1-probOffset)/(1-prob), k) - 1) * sigma / k));
			if (size != Settings::cdfFromFile.back().first) {
				Settings::cdfFromFile.push_back(std::make_pair(size, prob));
			} else {
				Settings::cdfFromFile.back().second = prob;
			}
		}
		Settings::cdfFromFile.back().second = 1.0;
	}
}

/*
 * Generate QPs for function "generate_flow_qp_mode"
 */
std::vector<std::pair<uint32_t, uint32_t> > generate_QP(uint32_t host_num, uint32_t flow_num){
	std::vector<std::pair<uint32_t, uint32_t> > qps;

	if (avg_message_per_qp != 0){
		std::vector<std::pair<uint32_t, uint32_t> > all_qps;
		for (uint32_t sender = 0; sender < host_num; sender++){
			for (uint32_t receiver = 0; receiver < host_num; receiver++){
				if (sender/host_per_rack == receiver/host_per_rack) continue;
				all_qps.push_back(std::make_pair(sender, receiver));
			}
		}

		uint32_t qp_num = flow_num/avg_message_per_qp;
		if (qp_num > all_qps.size())
			qp_num = all_qps.size();
		else if (qp_num < 1)
			qp_num = 1;

		RngSeedManager::SetDiffStreamIndex(-2);	// to align flow generation with MIBO
		Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable> ();
		std::cout << "QP num: " << qp_num << " ~" << all_qps.size() << " " << x->GetStream() << " " << RngSeedManager::GetSeed ()  << " " << host_num << std::endl;

		if (qp_num < all_qps.size()/2){
			// add qp from all_qps into qps
			while (qps.size() < qp_num){
				uint32_t curr = x->GetInteger(0, all_qps.size()-1);
				qps.push_back(all_qps.at(curr));
				all_qps.erase(all_qps.begin()+curr);
			}
			assert(qps.size() == qp_num);
			return qps;
		}else{
			// remove qp from all_qps
			while (all_qps.size() > qp_num){
				uint32_t curr = x->GetInteger(0, all_qps.size()-1);
				all_qps.erase(all_qps.begin()+curr);
			}
			assert(all_qps.size() == qp_num);
			return all_qps;
		}
	}else
		return qps;
}
/**
 * A fixed amount of traffic is generated, with each host generating traffic to other Pods.
*/
std::vector<std::pair<uint32_t, uint32_t> > generate_QP_random(uint32_t host_num, uint32_t flow_num){
	std::vector<std::pair<uint32_t, uint32_t> > qps;
	
	if (avg_message_per_qp != 0){
		uint32_t flows_per_src = flow_num / host_num;
		if(flow_num % host_num != 0){
			flows_per_src += 1;
		}
		uint32_t qp_num = flows_per_src * host_num / avg_message_per_qp;
		RngSeedManager::SetSeed(flow_seed);
		Ptr<UniformRandomVariable> rnd = CreateObject<UniformRandomVariable>();
		
		for (uint32_t sender = 0; sender < host_num; sender++){
			for(int i = 0; i < flows_per_src; i++){
				uint32_t dst;
				do{
					dst = rnd->GetInteger(0, host_num-1);
				}while(sender == dst || sender / host_per_rack == dst / host_per_rack || dst >= Settings::host_num);
				qps.push_back(make_pair(sender, dst));
			}
		}
		std::cout << "QP num: " << qp_num << " ~" << qps.size() << " " << rnd->GetStream() << " " << RngSeedManager::GetSeed ()  << " " << host_num << std::endl;

		return qps;
	}else
		return qps;
}

/*
 * Generate poisson traffic with QP id
 * when flow_cdf == 15, generate equal-sized flows, where flows' arriving interval following Poisson process
 */
void generate_flow_qp_mode_poisson(uint32_t shift_time = 0) {

	uint32_t mean = 0, maxx = 0;
	Ptr<RandomVariableStream> rnd;

	switch (flow_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	case 15:
		mean = equal_flow_size * Settings::packet_payload;//unit: bytes
		rnd = FlowGenerator::GetEqualSizeStream(0, mean, maxx);
		break;		//equal sized flows
	default:
		getCdfFromFile(flow_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, flow_cdf);
		break;
	}

	if (poisson_total > 0){
		flow_num = uint32_t(poisson_total/mean);
		std::cout << "poisson flows: " << flow_num << std::endl;
	}

	uint64_t total_pkts_analysis = 0;
	uint64_t min_time = 0;

	Settings::maxx = maxx;
	std::priority_queue<std::pair<Time, uint32_t>,
			std::vector<std::pair<Time, uint32_t> >,
			std::greater<std::pair<Time, uint32_t> > > q;
	double lambda = Settings::host_bw_Bps * load / (mean);

	double lambda_per_host;

	uint32_t rack_num = Settings::host_num/host_per_rack;
	lambda_per_host = lambda / (Settings::host_num * (rack_num-1) / rack_num);

	std::cout << "test lambda " << lambda << std::endl;
	std::cout << "Lambda: " << lambda_per_host << " interval mean: " << 1e6/lambda_per_host << "us" <<std::endl;
	// RngSeedManager::SetSeed(flow_seed);
	Ptr<ExponentialRandomVariable> e = Create<ExponentialRandomVariable>();
	e->SetStream(0);
	// std::vector<std::pair<uint32_t, uint32_t> > QPs = generate_QP_random(Settings::host_num, flow_num);
	std::vector<std::pair<uint32_t, uint32_t> > QPs = generate_QP(Settings::host_num, flow_num);

	std::map<uint32_t, uint32_t> qpIndexMap;

	std::cout << "QPs: " << std::endl;
	for (uint32_t i = 0; i < QPs.size(); ++i){
		Time t = Seconds(e->GetValue(1/lambda_per_host, 0))+MicroSeconds(shift_time);
		q.push(std::make_pair(t,QPs[i].first*1000+QPs[i].second));
		qpIndexMap[QPs[i].first*1000+QPs[i].second] = i*1000000;  // !! note that for one QP, the maximum number of message is 10000
	}

	if (q.empty()) {
		std::cout << "error: fail to generate flows!" << std::endl;
		return;
	}
	uint64_t sum_t = 0;
	while (flows.size() != flow_num) {
		std::pair<Time, uint32_t> p = q.top(); q.pop();
		Time t = p.first;
		uint32_t qp_id = qpIndexMap[p.second]/1000000;
		uint32_t msg_seq = (qpIndexMap[p.second]++)%1000000;      // return first, then do +1, so msg_seq starts from 0
		assert(qpIndexMap[p.second]%1000000 != 0);        // when assert fails, it means that msg_seq is larger than 10000 which will modify qp's index
		//std::cout<<t.GetSeconds()<<std::endl;
		uint32_t i = p.second/1000;
		uint32_t j = p.second%1000;

		uint64_t s = rnd->GetInteger();
		s = s*flow_size_mul;
		if (flow_cdf <= 3)
			s = s * packet_payload_size;

		Time tt = Seconds(e->GetValue(1/lambda_per_host, 0));
		q.push(std::make_pair(t + tt, i * 1000 + j));

		flows.push(Flow(i, j, s, 2 + t.GetSeconds(), qp_id, msg_seq, reset_per_qp_msg));

//		std::cout << std::setprecision(10) << i << " " << j << " " << s << " "
//				<< 2 + t.GetSeconds() << " " << qp_id << " " << msg_seq
//				<< std::endl;

		total_pkts_analysis += s;
		sum_t += tt.GetNanoSeconds();
		max_time = std::max(uint64_t(t.GetNanoSeconds()), max_time);
		min_time = std::min(uint64_t(t.GetNanoSeconds()), min_time);

	}
	std::cout << "total packets " << total_pkts_analysis << " min time "
			<< min_time << " max time " << max_time << " "
			<< total_pkts_analysis / (double) (max_time - min_time)
			<< " avg_interval: " << sum_t/(double)flows.size()
			<< std::endl;

}

/*
 * Generate lognormal traffic with QP id
 * when flow_cdf == 15, generate equal-sized flows, where flows' arriving interval following Poisson process
 */
void generate_flow_qp_mode_lognormal(uint32_t shift_time = 0) {

	uint32_t mean = 0, maxx = 0;
	Ptr<RandomVariableStream> rnd;

	switch (flow_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	case 15:
		mean = equal_flow_size * Settings::max_bdp;//unit: bytes
		rnd = FlowGenerator::GetEqualSizeStream(0, mean, maxx);
		break;		//equal sized flows
	default:
		getCdfFromFile(flow_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, flow_cdf);
		break;
	}

	if (poisson_total > 0){
		flow_num = uint32_t(poisson_total/mean);
		std::cout << "poisson flows: " << flow_num << std::endl;
	}

	uint64_t total_pkts_analysis = 0;
	uint64_t min_time = 0;

	Settings::maxx = maxx;
	std::priority_queue<std::pair<Time, uint32_t>,
			std::vector<std::pair<Time, uint32_t> >,
			std::greater<std::pair<Time, uint32_t> > > q;
	double lambda = Settings::host_bw_Bps * load / (mean);

	double lambda_per_host;

	uint32_t rack_num = Settings::host_num/host_per_rack;
	lambda_per_host = lambda / (Settings::host_num * (rack_num-1) / rack_num);

	std::cout << "test lambda " << lambda << std::endl;
	std::cout << "Lambda: " << lambda_per_host << " interval mean: " << 1e6/lambda_per_host << "us" <<std::endl;

	uint32_t unit = 1e6;
	double sigma = 2;
	double mu = log(unit/lambda_per_host)-sigma*sigma/2;
	assert(mu > 0);
	Ptr<LogNormalRandomVariable> e = Create<LogNormalRandomVariable>();
	e->SetStream(0);
	std::vector<std::pair<uint32_t, uint32_t> > QPs = generate_QP(Settings::host_num, flow_num);
	std::map<uint32_t, uint32_t> qpIndexMap;

	std::cout << "QPs: " << std::endl;
	for (uint32_t i = 0; i < QPs.size(); ++i){
		Time t = Seconds(e->GetValue(mu, sigma)/unit)+MicroSeconds(shift_time);
		q.push(std::make_pair(t,QPs[i].first*1000+QPs[i].second));
		qpIndexMap[QPs[i].first*1000+QPs[i].second] = i*100000;  // !! note that for one QP, the maximum number of message is 10000
	}

	if (q.empty()) {
		std::cout << "error: fail to generate flows!" << std::endl;
		return;
	}
	uint64_t sum_t = 0;
	while (flows.size() != flow_num) {
		std::pair<Time, uint32_t> p = q.top(); q.pop();
		Time t = p.first;
		uint32_t qp_id = qpIndexMap[p.second]/100000;
		uint32_t msg_seq = (qpIndexMap[p.second]++)%100000;      // return first, then do +1, so msg_seq starts from 0
		assert(qpIndexMap[p.second]%100000 != 0);        // when assert fails, it means that msg_seq is larger than 10000 which will modify qp's index
		uint32_t i = p.second/1000;
		uint32_t j = p.second%1000;

		uint64_t s = rnd->GetInteger();

		if (flow_cdf <= 3)
			s = s * packet_payload_size;

		Time tt = Seconds(e->GetValue(mu, sigma)/unit);
		q.push(std::make_pair(t + tt, i * 1000 + j));

		flows.push(Flow(i, j, s, 2 + t.GetSeconds(), qp_id, msg_seq, reset_per_qp_msg));

//		std::cout << std::setprecision(10) << i << " " << j << " " << s << " "
//				<< 2 + t.GetSeconds() << " " << qp_id << " " << msg_seq
//				<< std::endl;

		total_pkts_analysis += s;
		sum_t += tt.GetNanoSeconds();
		max_time = std::max(uint64_t(t.GetNanoSeconds()), max_time);
		min_time = std::min(uint64_t(t.GetNanoSeconds()), min_time);

	}
	std::cout << "total packets " << total_pkts_analysis << " min time "
			<< min_time << " max time " << max_time << " "
			<< total_pkts_analysis / (double) (max_time - min_time)
			<< " avg_interval: " << sum_t/(double)flows.size()
			<< std::endl;

}

void generate_flow_qp_mode(uint32_t shift_time = 0){
	if (Settings::POISSON == arrival_mode)
		generate_flow_qp_mode_poisson(shift_time);
	else if (Settings::LOG_NORMAL == arrival_mode)
		generate_flow_qp_mode_lognormal(shift_time);
	else
		assert(false);
}

/*
 * Generate QPs for function "generate_flow_qp_mode_mutiple_for_host_pair"
 * Note that there are several(qpNumOfHostPair) connections between a specific host pair
 */
std::vector<std::pair<uint32_t, uint32_t> > generate_QP_mutiple_for_host_pair(uint32_t host_num,
		uint32_t flow_num) {
	std::vector<std::pair<uint32_t, uint32_t> > qps;

	if (avg_message_per_qp != 0) {
		std::vector<std::pair<uint32_t, uint32_t> > all_qps;
		for (uint32_t sender = 0; sender < host_num; sender++) {
			for (uint32_t receiver = 0; receiver < host_num; receiver++) {
				if (sender / host_per_rack
						== receiver / host_per_rack)
					continue;
				all_qps.push_back(std::make_pair(sender, receiver));
			}
		}

		uint32_t qp_num = flow_num / avg_message_per_qp / qpNumOfHostPair;
		if (qp_num > all_qps.size())
			qp_num = all_qps.size();
		else if (qp_num < 1)
			qp_num = 1;
		std::cout << "QP num: " << qp_num << " ~" << std::endl;

		Ptr<UniformRandomVariable> x = CreateObject<UniformRandomVariable>();
		x->SetStream(0);
		if (qp_num < all_qps.size() / 2) {
			// add qp from all_qps into qps
			while (qps.size() < qp_num) {
				uint32_t curr = x->GetInteger(0, all_qps.size() - 1);
				qps.push_back(all_qps.at(curr));
				all_qps.erase(all_qps.begin() + curr);
			}
			assert(qps.size() == qp_num);
			return qps;
		} else {
			// remove qp from all_qps
			while (all_qps.size() > qp_num) {
				uint32_t curr = x->GetInteger(0, all_qps.size() - 1);
				all_qps.erase(all_qps.begin() + curr);
			}
			assert(all_qps.size() == qp_num);
			return all_qps;
		}

	} else
		return qps;
}

/*
 * Generate poisson traffic with QP id
 * Note that there are several connections between a specific host pair
 */
void generate_flow_qp_mode_mutiple_for_host_pair(uint32_t shift_time = 0) {

	uint32_t mean = 0, maxx = 0;
	Ptr<RandomVariableStream> rnd;

	switch (flow_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	default:
		getCdfFromFile(flow_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, flow_cdf);
		break;
	}

	uint64_t total_pkts_analysis = 0;

	Settings::maxx = maxx;
	std::priority_queue<std::pair<Time, uint32_t>,
			std::vector<std::pair<Time, uint32_t> >,
			std::greater<std::pair<Time, uint32_t> > > q;
	double lambda = Settings::host_bw_Bps * load / (mean);

	double lambda_per_host;

	uint32_t rack_num = Settings::host_num/host_per_rack;
	lambda_per_host = lambda / (Settings::host_num * (rack_num-1) / rack_num);

	std::cout << "test lambda " << lambda << std::endl;
	std::cout << "Lambda: " << lambda_per_host << std::endl;
	Ptr<ExponentialRandomVariable> e = Create<ExponentialRandomVariable>();
	e->SetStream(0);
	std::vector<std::pair<uint32_t, uint32_t> > QPs = generate_QP_mutiple_for_host_pair(Settings::host_num, flow_num);
	std::map<uint32_t, uint32_t> qpIndexMap;
	std::map<uint32_t, uint32_t> qpMsgNum;

	for (uint32_t i = 0; i < QPs.size(); ++i) {
		Time t = Seconds(e->GetValue(1 / lambda_per_host, 0))
				+ MicroSeconds(shift_time);
		q.push(std::make_pair(t, QPs[i].first * 1000 + QPs[i].second));
		qpIndexMap[QPs[i].first * 1000 + QPs[i].second] = i;
		for (uint32_t j = 0; j < qpNumOfHostPair; j++){
			qpMsgNum[i*qpNumOfHostPair+j] = 0;
		}
	}

	if (q.empty()) {
		std::cout << "error: fail to generate flows!" << std::endl;
		return;
	}

	Ptr<UniformRandomVariable> rnd_qp = CreateObject<UniformRandomVariable>();
	rnd_qp->SetStream(0);
	while (flows.size() != flow_num) {
		std::pair<Time, uint32_t> p = q.top();
		q.pop();
		Time t = p.first;
		uint32_t qp_id = (qpIndexMap[p.second])*qpNumOfHostPair + rnd_qp->GetInteger(0, qpNumOfHostPair-1);
		uint32_t msg_seq = (qpMsgNum[qp_id]++);// return first, then do +1, so msg_seq starts from 0
		uint32_t i = p.second / 1000;
		uint32_t j = p.second % 1000;

		uint64_t s = rnd->GetInteger();
		if (flow_cdf <= 3)
			s = s * packet_payload_size;

		flows.push(Flow(i, j, s, 2 + t.GetSeconds(), qp_id, msg_seq, reset_per_qp_msg));

		std::cout << std::setprecision(10) << i << " " << j << " " << s
				<< " " << 2 + t.GetSeconds() << " " << qp_id << " "
				<< msg_seq << std::endl;

		Time tt = Seconds(e->GetValue(1 / lambda_per_host, 0));
		q.push(std::make_pair(t + tt, i * 1000 + j));

		total_pkts_analysis += s;
		max_time = std::max(uint64_t(t.GetNanoSeconds()), max_time);
		min_time = std::min(uint64_t(t.GetNanoSeconds()), min_time);

	}
	std::cout << "total packets " << total_pkts_analysis << " min time "
			<< min_time << " max time " << max_time << " "
			<< total_pkts_analysis / (double) (max_time - min_time)
			<< std::endl;

}

/*
 * Generate poisson traffic
 */
void generate_flow_homa_test(uint32_t flow_num, uint32_t shift_time = 0) {
	uint32_t mean = 0, maxx = 0;
	Ptr<RandomVariableStream> rnd;

	switch (flow_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	default:
		getCdfFromFile(flow_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, flow_cdf);
		break;

	}

	uint64_t total_pkts_analysis = 0;

	static uint32_t flow_number = 0;

	Settings::maxx = maxx;
	std::priority_queue<std::pair<Time, uint32_t>,
			std::vector<std::pair<Time, uint32_t> >,
			std::greater<std::pair<Time, uint32_t> > > q;
	double lambda = Settings::host_bw_Bps * load / (mean);

	double lambda_per_host;

	uint32_t rack_num = Settings::host_num/host_per_rack;
	lambda_per_host = lambda / (Settings::host_num * (rack_num-1) / rack_num);

	//std::cout<<node.size()/9<<std::endl;
	std::cout << "test lambda " << lambda << std::endl;
	std::cout << "Lambda: " << lambda_per_host << std::endl;
	Ptr<ExponentialRandomVariable> e = Create<ExponentialRandomVariable>();
	e->SetStream(0);
	for (uint32_t i = 0; i < Settings::host_num; ++i) {	// sender
		for (uint32_t j = 0; j < Settings::host_num; ++j) { // receiver
			if (i / host_per_rack == j / host_per_rack)
				continue;
			Time t = Seconds(e->GetValue(1 / lambda_per_host, 0))
					+ MicroSeconds(shift_time);
			q.push(std::make_pair(t, i * 1000 + j));
		}
	}

	if (q.empty()) {
		std::cout << "error: fail to generate flows!" << std::endl;
		return;
	}
	while (flows.size() != flow_num) {
		std::pair<Time, uint32_t> p = q.top();
		q.pop();
		Time t = p.first;
		uint32_t i = p.second / 1000;
		uint32_t j = p.second % 1000;
		uint64_t s = rnd->GetInteger();
		if (flow_cdf <= 3)
			s = s * packet_payload_size;

		flows.push(Flow(i, j, s, 2 + t.GetSeconds()));

		Time tt = Seconds(e->GetValue(1 / lambda_per_host, 0));
		q.push(std::make_pair(t + tt, i * 1000 + j));

		++flow_number;
		total_pkts_analysis += s;
		max_time = std::max(uint64_t(t.GetNanoSeconds()), max_time);
		min_time = std::min(uint64_t(t.GetNanoSeconds()), min_time);

	}
	std::cout << "total packets " << total_pkts_analysis << " min time "
			<< min_time << " max time " << max_time << " "
			<< total_pkts_analysis / (double) (max_time - min_time)
			<< std::endl;

}

/*
 * Generate storage traffic
 * The size of each message is 'storage_message_size'. IO depth = 'storage_iodepth'
 * Rather than poisson arrival, traffic is average arrival.
 */
void generate_storage_traffic() {
	uint64_t total_pkts_analysis = 0;
	uint32_t qp_id = 0;

	Ptr<UniformRandomVariable> rnd_msg = CreateObject<UniformRandomVariable>();
	rnd_msg->SetStream(0);
	for (uint32_t src = 0; src < Settings::host_num; src++){
		for (uint32_t dst = 0; dst < Settings::host_num; dst++){
			if (src == dst) continue;
			double start_time = 1e-6;
			uint32_t qp_start = qp_id;
			std::vector<uint32_t> qp_msg(storage_qp_num, 0);	// used to record message id
			for (uint32_t curr = 0; curr < storage_times; curr++){
				for (uint32_t qp_i = 0; qp_i < storage_qp_num; qp_i++){
					uint32_t msg_num = rnd_msg->GetInteger(1, storage_iodepth);
					for (uint32_t msg = 0; msg < msg_num;msg++){
						flows.push(Flow(src, dst, storage_message_size, 2 + start_time, qp_start + qp_i, qp_msg[qp_i], reset_per_qp_msg));
						std::cout << std::setprecision(10) << src << " " << dst << " "
								<< storage_message_size
								<< " " << start_time << " " << qp_start + qp_i << " "
								<< qp_msg[qp_i] << std::endl;
						qp_msg[qp_i]++;

						total_pkts_analysis += storage_message_size;
						max_time = std::max(uint64_t(Seconds(start_time).GetNanoSeconds()), max_time);
						min_time = std::min(uint64_t(Seconds(start_time).GetNanoSeconds()), min_time);
						qp_max = std::max(qp_max, qp_start + qp_i);

					}
					start_time += storage_message_size * msg_num / Settings::host_bw_Bps / load * (Settings::host_num-1);
				}
			}
			qp_id = qp_start + storage_qp_num;
		}
	}

	std::cout << "total packets " << total_pkts_analysis << " min time "
			<< min_time << " max time " << max_time << " "
			<< total_pkts_analysis / (double) (max_time - min_time)
			<< std::endl;

}

/*
 * Generate test flow (when flow-cdf == 98, i.e., under storage traffic)
 * Test flow will initial a unique QP connection,
 * and once last message's finish, a new message will push into QP.
 * The size of each test messages is the same as background messages, i.e., storage_message_size.
 * The direction of test flow is unidirectional, e.g. hostA -> hostB, hostB -> hostC, hostC -> hostA
 * Test flows arrives after a period of simulation.
 */
void generate_test_flow(){
	std::cout << "Start generating test flows:" << std::endl;
	Ptr<UniformRandomVariable> rnd = CreateObject<UniformRandomVariable>();
	rnd->SetStream(0);
	uint32_t qp_id = qp_max + 1;
	uint64_t start_time = min_time + rnd->GetInteger(0, (uint32_t)((max_time-min_time)*0.1));

	std::vector<uint32_t> hosts;
	for (uint32_t host = 0; host < Settings::host_num; host++) {
		hosts.push_back(host);
	}

	uint32_t first_host = hosts[0];
	uint32_t src = first_host, dst_index = 0, dst = 0;
	hosts.erase(hosts.begin());
	while (hosts.size() > 0) {
		dst_index = rnd->GetInteger(0, hosts.size() - 1);
		dst = hosts[dst_index];
		hosts.erase(hosts.begin() + dst_index);
		uint32_t t = start_time + rnd->GetInteger(0, 100);
		test_flows.push(Flow(src, dst, storage_message_size, 2+t/1e9, qp_id, 0, reset_per_qp_msg));
		std::cout << std::setprecision(10) << src << " " << dst << " "
				<< storage_message_size
				<< " " << 2+t/1e9 << " " << qp_id << " "
				<< 0 << std::endl;
		src = dst;
		++qp_id;
	}
	// last test flow
	uint32_t t = start_time + rnd->GetInteger(0, 100);
	test_flows.push(Flow(src, first_host, storage_message_size, 2+t/1e9, qp_id, 0, reset_per_qp_msg));
	std::cout << std::setprecision(10) << src << " " << first_host << " "
			<< storage_message_size
			<< " " << 2+t/1e9 << " " << qp_id << " "
			<< 0 << std::endl;

	std::cout << "Finish generating " << test_flows.size() << " test flows."
			<< std::endl;
}

/*
 * Generate interval incast flows, while each flow/message belongs to a specific connection.
 * The size of incast flows is specified from cdf file, and the scale of incast is defined by Settings::incast_scale..
 * Note that
 * 1. each pair of hosts is mapped to one specific connection.
 * 2. all incast flows are sent to the same receiver.
 */
void generate_incast_from_cdf_withQP() {

	uint32_t dst_host = 0;
	uint32_t maxx = 0, mean = 0;

	//uniformly select several src and one dst
	Ptr<UniformRandomVariable> host_stream =
			CreateObject<UniformRandomVariable>();
	host_stream->SetAttribute("Min", DoubleValue(0)); //host num: 0~79
	host_stream->SetAttribute("Max", DoubleValue(Settings::host_num - 1));

	getCdfFromFile(incast_cdf);
	Ptr<RandomVariableStream> rnd_size = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, flow_cdf);
	Settings::maxx = maxx;

	std::set<uint32_t> src_set;
	std::vector<uint32_t> src_v;
	std::map<uint32_t, std::pair<uint32_t, uint32_t> > QPs;
	std::map<uint32_t, uint32_t> qp_msg_nums;
	uint32_t qp_id = 0;

	//not all hosts are involved into incast, for HPCC
	//select several src host and an appointed dst host
	if (incast_scale != 1) {
		while (src_set.size() < Settings::host_num * incast_scale) {//choose several src
			uint32_t i = host_stream->GetValue();
			//dst_host * Settings::host_per_rack is the dst
			//and the host must not be selected before
			if (i / host_per_rack != dst_host
					&& src_set.find(i) == src_set.end()) {
				src_set.insert(i);
				qp_msg_nums[qp_id] = 0;
				QPs[qp_id++] = std::make_pair(i, dst_host * host_per_rack);
			}
		}
	} else {
		for (uint32_t i = 0; i < Settings::host_num; i++) {
			if (i / host_per_rack != dst_host) {
				qp_msg_nums[qp_id] = 0;
				QPs[qp_id++] = std::make_pair(i, dst_host * host_per_rack);
			}
		}
	}

	std::priority_queue<std::pair<Time, uint32_t>,
			std::vector<std::pair<Time, uint32_t> >,
			std::greater<std::pair<Time, uint32_t> > > queue_time_msg;
	for (uint32_t i = 0; i < incast_time; i++) {
		uint32_t time_shift = i * incast_interval;
		while (flows.size() + queue_time_msg.size() < (i + 1) * flow_num) {
			Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
					+ MicroSeconds(time_shift);
			queue_time_msg.push(
					std::make_pair(t, queue_time_msg.size() % QPs.size()));
		}
	}

	while (!queue_time_msg.empty()) {
		uint32_t qp_id = queue_time_msg.top().second;
		uint32_t src = QPs[qp_id].first;
		uint32_t dst = QPs[qp_id].second;
		Time t = queue_time_msg.top().first;
		uint64_t s = rnd_size->GetInteger();
		queue_time_msg.pop();

		flows.push(Flow(src, dst, s, 2+t.GetSeconds(), qp_id, qp_msg_nums[qp_id], reset_per_qp_msg));

		std::cout << std::setprecision(10) << src << " " << dst << " " << s
				<< " " << 2 + t.GetSeconds() << " " << qp_id << " "
				<< qp_msg_nums[qp_id] << std::endl;
		qp_msg_nums[qp_id]++;

	}
}

/*
 * Generate the interval incast flows with sizes from cdf file.
 * The size of incast flows is corresponding to cdf file, and the scale of incast is defined by Settings::incast_scale.
 * Note that
 * 1. each pair of hosts is mapped to one specific connection.
 * 2. all incast flows are sent to the same receiver.
 */
void generate_incast_from_cdf(uint32_t flow_num, uint32_t dst_host, uint32_t time_shift = 0) {

	uint32_t maxx = 0, mean = 0;
	Settings::maxx = maxx;

	//of no use, just to make the cutoff un-empty -> otherwise the program will crush
	getCdfFromFile(incast_cdf);

	//uniformly select several src and one dst
	Ptr<UniformRandomVariable> host_stream =
			CreateObject<UniformRandomVariable>();
	host_stream->SetAttribute("Min", DoubleValue(0));
	host_stream->SetAttribute("Max", DoubleValue(Settings::host_num - 1));

	Ptr<RandomVariableStream> rnd_size = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, flow_cdf);

	std::set<uint32_t> src_set;
	std::vector<uint32_t> src_v;

	// not all hosts are involved into incast, for HPCC
	// select several src host and an appointed dst host
	if (incast_scale != 1) {
		while (src_set.size() < Settings::host_num * incast_scale) {//choose several src
			uint32_t i = host_stream->GetValue();
			// dst_host * Settings::host_per_rack is the dst
			// and the host must not be selected before
			if (i != dst_host * host_per_rack
					&& src_set.find(i) == src_set.end())
				src_set.insert(i);
		}

		for (std::set<uint32_t>::iterator it = src_set.begin();
				it != src_set.end(); ++it) {
			src_v.push_back(*it);
		}
		src_v.push_back(dst_host * host_per_rack);//dst_host is determined
	}

	uint32_t src_host = 0;
	uint32_t shuffle_index = 0;

	while (flows.size() < flow_num) {
		Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
				+ MicroSeconds(time_shift);

		uint32_t i, j = 0;

		//the last host in src_v as dst, others as the sender
		//several incast dst may fall into one switch
		if (incast_scale != 1) {
			i = src_v[shuffle_index];
			j = src_v[src_v.size() - 1];
			shuffle_index = (shuffle_index + 1) % (src_v.size() - 1);
		} else {		//if all the host is involved into incast
			i = src_host % Settings::host_num;
			j = dst_host * host_per_rack;
			++src_host;		//just a variable to generate i
		}

		if (i == j)
			continue;

		flows.push(Flow(i, j, rnd_size->GetInteger(), 2+t.GetSeconds()));
	}
}

/*
 * Generate the incast flows, while each flow/message belongs to a specific connection.
 * The size of incast flows is BDP*Settings::incast_flow_size, and the scale of incast is defined by Settings::incast_scale.
 * Note that
 * 1. each pair of hosts is mapped to one specific connection.
 * 2. all incast flows are sent to the same receiver.
 */
void generate_incast_withQP() {

	uint32_t dst_host = 0;
	uint32_t maxx = Settings::packet_payload * Settings::free_token * incast_flow_size;
	Settings::maxx = maxx;

	RngSeedManager::SetDiffStreamIndex(-2);	// to align flow generation with MIBO
	//uniformly select several src and one dst
	Ptr<UniformRandomVariable> host_stream =
			CreateObject<UniformRandomVariable>();
	host_stream->SetAttribute("Min", DoubleValue(0));
	host_stream->SetAttribute("Max", DoubleValue(Settings::host_num - 1));

	std::set<uint32_t> src_set;
	std::map<uint32_t, std::pair<uint32_t, uint32_t> > QPs;
	std::map<uint32_t, uint32_t> qp_msg_nums;
	uint32_t qp_id = 0;

	// not all hosts are involved into incast, for HPCC, select several src host and an appointed dst host
	if (incast_scale != 1) {
		while (src_set.size() < Settings::host_num * incast_scale) { //choose several src
			uint32_t i = host_stream->GetValue();
			if (i / host_per_rack != dst_host
					&& src_set.find(i) == src_set.end()) {
				src_set.insert(i);
				qp_msg_nums[qp_id] = 0;
				QPs[qp_id++] = std::make_pair(i,
						dst_host * host_per_rack);
			}
		}
	} else {
		for (uint32_t i = 0; i < Settings::host_num; i++) {
			if (i / host_per_rack != dst_host) {
				qp_msg_nums[qp_id] = 0;
				QPs[qp_id++] = std::make_pair(i,
						dst_host * host_per_rack);
			}
		}
	}

	// generate flows
	std::priority_queue<std::pair<Time, uint32_t>,
			std::vector<std::pair<Time, uint32_t> >,
			std::greater<std::pair<Time, uint32_t> > > queue_time_msg;
	for (uint32_t i = 0; i < incast_time; i++) {
		uint32_t time_shift = i * incast_interval;
		while (flows.size() + queue_time_msg.size()
				< (i + 1) * flow_num) {
			Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
					+ MicroSeconds(time_shift);
			queue_time_msg.push(
					std::make_pair(t, queue_time_msg.size() % QPs.size()));
		}
	}

	Ptr<RandomVariableStream> rnd = CreateObject<UniformRandomVariable>();
	rnd->SetAttribute("Min", DoubleValue(Settings::host_bw_Bps/1250000000*3*1460));
    rnd->SetAttribute("Max", DoubleValue(Settings::host_bw_Bps/1250000000*4*1460));
    maxx = Settings::host_bw_Bps/1250000000*4*1460;
	while (!queue_time_msg.empty()) {
		uint32_t qp_id = queue_time_msg.top().second;
		uint32_t src = QPs[qp_id].first;
		uint32_t dst = QPs[qp_id].second;
		Time t = queue_time_msg.top().first;
		queue_time_msg.pop();

		flows.push(Flow(src, dst, rnd->GetInteger(), 2+t.GetSeconds(), qp_id, qp_msg_nums[qp_id], reset_per_qp_msg));

		qp_msg_nums[qp_id]++;
	}
}

/*
 * Generate the incast flows.
 * The incast flows arrive periodically.
 * The size of incast flows is BDP*Settings::incast_flow_size, and the scale of incast is defined by Settings::incast_scale.
 * Note that
 * 1. each message is mapped to one specific connection.
 * 2. all incast flows are sent to the same receiver.
 */
void generate_incast(uint32_t flow_num, uint32_t dst_host, uint32_t time_shift = 0) {

	uint32_t maxx = 0;
	maxx = Settings::packet_payload * Settings::free_token * incast_flow_size;
	Settings::maxx = maxx;

	//uniformly select several src and one dst
	Ptr<UniformRandomVariable> host_stream =
			CreateObject<UniformRandomVariable>();
	host_stream->SetAttribute("Min", DoubleValue(0)); //host num: 0~79
	host_stream->SetAttribute("Max", DoubleValue(Settings::host_num - 1));

	std::set<uint32_t> src_set;
	std::vector<uint32_t> src_v;

	//not all hosts are involved into incast, for HPCC
	//select several src host and an appointed dst host
	if (incast_scale != 1) {
		while (src_set.size() < Settings::host_num * incast_scale) {//choose several src
			uint32_t i = host_stream->GetValue();
			//dst_host * Settings::host_per_rack is the dst
			//and the host must not be selected before
			if (i != dst_host * host_per_rack
					&& src_set.find(i) == src_set.end())
				src_set.insert(i);
		}

		for (std::set<uint32_t>::iterator it = src_set.begin();
				it != src_set.end(); ++it) {
			src_v.push_back(*it);
		}
		src_v.push_back(dst_host * host_per_rack);//dst_host is determined to be dst_host * Settings::host_per_rack
	}

	uint32_t src_host = 0;
	uint32_t shuffle_index = 0;

	while (flows.size() < flow_num) {

		Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
				+ MicroSeconds(time_shift);

		uint32_t i, j = 0;

		//the last host in src_v as dst, others as the sender
		//several incast dst may fall into one switch
		if (incast_scale != 1) {
			i = src_v[shuffle_index];
			j = src_v[src_v.size() - 1];
			shuffle_index = (shuffle_index + 1) % (src_v.size() - 1);
		} else {		//if all the host is involved into incast
			i = src_host % Settings::host_num;
			j = dst_host * host_per_rack;
			++src_host;		//just a variable to generate i
		}

		if (i/host_per_rack == j/host_per_rack)
			continue;

		flows.push(Flow(i, j, maxx, 2+t.GetSeconds()));
	}
}

/*
 * Generate the flows mixed with poisson flows and incast, while each flow/message belongs to a specific connection.
 * The poisson flows(#poisson flows: Settings::flows) arrive corresponding to Settings::load after incast flows arrive,
 * while the incast flows arrive periodically(Settings::incast_mix every Settings::incast_interval microseconds for Settings::incast_time times).
 * The size of poisson flows is specified from cdf file, and the size of incast flows is BDP*Settings::incast_flow_size.
 * Note that
 * 1. each pair of hosts is mapped to one specific connection.
 * 2. incast flows and poisson flows don't share the same receivers.
 */
void generate_poisson_fromcdf_incast_withQP() {

	uint32_t maxx = Settings::packet_payload * Settings::free_token * incast_flow_size;

	Ptr<UniformRandomVariable> rnd_host = CreateObject<UniformRandomVariable>();

	std::map<uint32_t, std::pair<uint32_t, uint32_t> > QPs;
	std::map<uint32_t, std::pair<uint32_t, uint32_t> > QPs_poisson;
	std::map<uint32_t, uint32_t> qp_msg_nums;
	std::map<uint32_t, uint32_t> QP_incast_map; // <src*1000+dst, qp_id>, used for mapping a pair of src and dst to qp_id
	uint32_t qp_id = 0, poisson_qp_start = 0;

	// generate all-to-one QPs for incast traffic
	uint32_t rack_num = Settings::host_num / host_per_rack;
	for (uint32_t dst_host = 0; dst_host < rack_num; dst_host++) {
		for (uint32_t i = 0; i < Settings::host_num; i++) {
			if (i != dst_host * host_per_rack) {
				QP_incast_map[i * 1000 + dst_host * host_per_rack] = qp_id;
				qp_msg_nums[qp_id] = 0;
				QPs[qp_id] = std::make_pair(i, dst_host * host_per_rack);
				++qp_id;
			}
		}
	}

	// generate all-to-all QPs for poission traffic
	poisson_qp_start = qp_id;
	for (uint32_t i = 0; i < Settings::host_num; i++) {
		for (uint32_t j = 0; j < Settings::host_num; j++) {
			if (i % host_per_rack != j % host_per_rack && j % host_per_rack != 0) {
				QP_incast_map[i * 1000 + j] = qp_id;
				qp_msg_nums[qp_id] = 0;
				QPs_poisson[qp_id] = std::make_pair(i, j);
				++qp_id;
			}
		}
	}

	// generate flows
	std::priority_queue<std::pair<Time, std::pair<uint32_t, uint32_t> >,
			std::vector<std::pair<Time, std::pair<uint32_t, uint32_t> > >,
			std::greater<std::pair<Time, std::pair<uint32_t, uint32_t> > > > queue_time_msg;//<startTime, qp_id>
	// generate incast flows
	for (uint32_t i = 0; i < incast_time; i++) {
		uint32_t time_shift = i * incast_interval;
		uint32_t dst = i % rack_num * host_per_rack;
		while (queue_time_msg.size() < incast_mix * (i + 1)) {
			Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
					+ MicroSeconds(time_shift);
			uint32_t src = rnd_host->GetInteger(0, Settings::host_num - 1);
			if (src / host_per_rack == dst / host_per_rack)
				continue;
			uint32_t qp_id = QP_incast_map[src * 1000 + dst];
			queue_time_msg.push(std::make_pair(t, std::make_pair(qp_id, maxx)));
		}
	}
	// generate poisson flows
	uint32_t mean = 0;
	Ptr<RandomVariableStream> rnd;
	switch (incast_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	default:
		getCdfFromFile(incast_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, incast_cdf);
	}
	Settings::maxx = maxx;
	double lambda = Settings::host_bw_Bps * load / (mean);
	double lambda_per_host;

	lambda_per_host = lambda / (Settings::host_num * (rack_num-1) / rack_num);

	std::cout << "test lambda " << lambda << std::endl;
	std::cout << "Lambda: " << lambda_per_host << std::endl;
	Ptr<ExponentialRandomVariable> e = Create<ExponentialRandomVariable>();
	e->SetStream(0);
	std::map<uint32_t, std::pair<uint32_t, uint32_t> >::iterator qp_it =
			QPs_poisson.begin();
	while (queue_time_msg.size() < flow_num + incast_mix * (incast_time)) {
		Time t = Seconds(e->GetValue(1 / lambda_per_host, 0))
				+ MicroSeconds(incast_time * incast_interval);	// poisson come with incast
		uint32_t size = rnd->GetInteger();
		queue_time_msg.push(
				std::make_pair(t, std::make_pair(qp_it->first, size)));
		qp_it++;
		if (qp_it == QPs_poisson.end())
			qp_it = QPs_poisson.begin();
	}

	// assign flows
	while (!queue_time_msg.empty()) {
		uint32_t qp_id = queue_time_msg.top().second.first;
		uint32_t src, dst;
		if (qp_id < poisson_qp_start) {
			// incast flows
			src = QPs[qp_id].first;
			dst = QPs[qp_id].second;
		} else {
			// poisson flows
			src = QPs_poisson[qp_id].first;
			dst = QPs_poisson[qp_id].second;
		}
		uint32_t s = queue_time_msg.top().second.second;
		Time t = queue_time_msg.top().first;
		queue_time_msg.pop();

		flows.push(Flow(src, dst, s, 2+t.GetSeconds(), qp_id, qp_msg_nums[qp_id], reset_per_qp_msg));

		std::cout << std::setprecision(10) << src << " " << dst << " " << s
				<< " " << 2 + t.GetSeconds() << " " << qp_id << " "
				<< qp_msg_nums[qp_id] << std::endl;
		++qp_msg_nums[qp_id];

	}
}

/*
 * Generate the flows mixed with poisson flows and incast, while each flow/message belongs to a specific connection.
 * The poisson flows(#poisson flows: Settings::flows) arrive corresponding to Settings::load after incast flows arrive,
 * while the incast flows arrive with load(Settings::incast_load).
 * The size of poisson and incast flows is specified from cdf file.
 * Note that
 * 1. each pair of hosts is mapped to one specific connection.
 * 2. incast flows and poisson flows don't share the same receivers.
 * 3. incast dst num = incremental_incast_time.
 * 
 * used in fat-tree topo, incast dst all in the first pod, incast src in other pods.
 * incremental_incast_time == 1, src(pod1)--->dst(pod0)
 * incremental_incast_time == 2, src(pod1 and pod2)--->dst(pod0)
 * incremental_incast_time == 3, src(pod1 and pod2 and pod3)--->dst(pod0)
 * ...
 */
void generate_load_poisson_fromcdf_incremental_incast() {
	// generate poisson flows
	//generate_flow_qp_mode(30);	// poisson come after incast
	if (flow_num != 0) // FLOW_NUM == 0, didn't gen poisson flow
		generate_flow_qp_mode(Settings::time_shift);	// poisson come after incast

	uint32_t possion_num = flows.size();
	std::cout << "poisson num:" << possion_num << std::endl;

	Ptr<UniformRandomVariable> rnd_host = CreateObject<UniformRandomVariable>();
	uint32_t rack_num = Settings::host_num / host_per_rack;

	// generate flows
	uint32_t mean = 0, maxx = 0;
	Ptr<RandomVariableStream> rnd;
	double mean_size;//added by liuchang

	// generate incast flows
	switch (incast_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	case 9:
		rnd = CreateObject<UniformRandomVariable>();
		rnd->SetAttribute("Min", DoubleValue(Settings::host_bw_Bps/1250000000*Settings::incast_min));
        rnd->SetAttribute("Max", DoubleValue(Settings::host_bw_Bps/1250000000*Settings::incast_max));
		mean_size = 1.0*(Settings::incast_min + Settings::incast_max) / 2;
        mean = Settings::host_bw_Bps/1250000000*mean_size*packet_payload_size;
        maxx = Settings::host_bw_Bps/1250000000*Settings::incast_max*packet_payload_size;
		std::cout<<"setting:incast_min = "<<Settings::incast_min<<endl;
		std::cout<<"setting:incast_max = "<<Settings::incast_max<<endl;
		std::cout<<"mean_size = "<<mean_size<<endl;
		break;
	case 10:
		rnd = CreateObject<UniformRandomVariable>();
		rnd->SetAttribute("Min", DoubleValue(20*1e6/100));
        rnd->SetAttribute("Max", DoubleValue(20*1e6/100));
        mean = 20*1e6/100;
        maxx = 20*1e6/100;
		break;
	default:
		getCdfFromFile(incast_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, incast_cdf);
	}
	Settings::maxx = std::max(maxx, Settings::maxx);
	incast_interval = mean * incast_mix / (Settings::host_bw_Bps * incast_load / 1e9);
	incast_time = (uint32_t)max_time / incast_interval;
	incast_time = std::max((uint32_t)1, incast_time);
	std::cout << "incast_time: " << incast_time << " incast_interval: " << incast_interval << std::endl;
	std::set<uint32_t> incast_dsts;
	for (uint32_t i = 0; i < incast_time; i++) {
		uint32_t time_shift = i * incast_interval / 1e3;

		uint32_t tor_per_rack = sqrt(host_per_rack);
		uint32_t dst = 0;
		uint32_t src_start = host_per_rack; // incast src start id

		for (uint32_t j = 0; j < incast_mix; ++j){
			Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
						+ MicroSeconds(time_shift);
			dst = 0;
			for (uint32_t v = 1; v <= incremental_incast_time; v++) {
				uint32_t src = rnd_host->GetInteger(src_start*v, src_start*v + host_per_rack - 1);
				uint32_t size = rnd->GetInteger();
				if (incast_cdf <= 3 || incast_cdf == 9)
					size = size * packet_payload_size;
				flows.push(Flow(src, dst, size, 2+t.GetSeconds()));
				incast_dsts.insert(dst);
				dst ++; // continuous selection dst
			}
		}
	}

	// remove poisson flows which use incast dst as src or dst
	for (uint32_t i = 0; i < possion_num; ++i){
		Flow curr = flows.front();
		flows.pop();
		if (incast_dsts.count(curr.src) <= 0 && incast_dsts.count(curr.dst) <= 0)
			flows.push(curr);
	}

	std::cout << "flows num:" << flows.size() << " incast dsts num:" << incast_dsts.size() << std::endl;
}

/*
 * Generate the flows mixed with poisson flows and incast, while each flow/message belongs to a specific connection.
 * The poisson flows(#poisson flows: Settings::flows) arrive corresponding to Settings::load after incast flows arrive,
 * while the incast flows arrive with load(Settings::incast_load).
 * The size of poisson and incast flows is specified from cdf file.
 * Note that
 * 1. each pair of hosts is mapped to one specific connection.
 * 2. incast flows and poisson flows don't share the same receivers.
 */
void generate_load_poisson_fromcdf_incast() {

	// generate poisson flows
	//generate_flow_qp_mode(30);	// poisson come after incast
	generate_flow_qp_mode(Settings::time_shift);	// poisson come after incast

	uint32_t possion_num = flows.size();
	std::cout << "poisson num:" << possion_num << std::endl;

	Ptr<UniformRandomVariable> rnd_host = CreateObject<UniformRandomVariable>();
	uint32_t rack_num = Settings::host_num / host_per_rack;

	// generate flows
	uint32_t mean = 0, maxx = 0;
	Ptr<RandomVariableStream> rnd;
	double mean_size;//added by liuchang

	// generate incast flows
	switch (incast_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	case 9:
		rnd = CreateObject<UniformRandomVariable>();
		rnd->SetAttribute("Min", DoubleValue(Settings::host_bw_Bps/1250000000*Settings::incast_min));
        rnd->SetAttribute("Max", DoubleValue(Settings::host_bw_Bps/1250000000*Settings::incast_max));
		mean_size = 1.0*(Settings::incast_min + Settings::incast_max) / 2;
        mean = Settings::host_bw_Bps/1250000000*mean_size*packet_payload_size;
        maxx = Settings::host_bw_Bps/1250000000*Settings::incast_max*packet_payload_size;
		std::cout<<"setting:incast_min = "<<Settings::incast_min<<endl;
		std::cout<<"setting:incast_max = "<<Settings::incast_max<<endl;
		std::cout<<"mean_size = "<<mean_size<<endl;
		break;
	case 10:
		rnd = CreateObject<UniformRandomVariable>();
		rnd->SetAttribute("Min", DoubleValue(20*1e6/100));
        rnd->SetAttribute("Max", DoubleValue(20*1e6/100));
        mean = 20*1e6/100;
        maxx = 20*1e6/100;
		break;
	default:
		getCdfFromFile(incast_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, incast_cdf);
	}
	Settings::maxx = std::max(maxx, Settings::maxx);
	incast_interval = mean * incast_mix / (Settings::host_bw_Bps * incast_load / 1e9);
	incast_time = (uint32_t)max_time / incast_interval;
	incast_time = std::max((uint32_t)1, incast_time);
	std::cout << "incast_time: " << incast_time << " incast_interval: " << incast_interval << std::endl;
	std::set<uint32_t> incast_dsts;
	for (uint32_t i = 0; i < incast_time; i++) {
		uint32_t time_shift = i * incast_interval / 1e3;
		
		uint32_t dst = i % rack_num * host_per_rack;
		if(Settings::one_incast_dst)
			dst = 0;
		
		incast_dsts.insert(dst);
		for (uint32_t j = 0; j < incast_mix; ++j){
			Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
						+ MicroSeconds(time_shift);
			uint32_t src = rnd_host->GetInteger(0, Settings::host_num - 1);
			if (src / host_per_rack == dst / host_per_rack){
				--j;
				continue;
			}
			uint32_t size = rnd->GetInteger();
			if (incast_cdf <= 3 || incast_cdf == 9)
				size = size * packet_payload_size;

			flows.push(Flow(src, dst, size, 2+t.GetSeconds()));
		}
	}

	// remove poisson flows which use incast dst as src or dst
	for (uint32_t i = 0; i < possion_num; ++i){
		Flow curr = flows.front();
		flows.pop();
		if (incast_dsts.count(curr.src) <= 0 && incast_dsts.count(curr.dst) <= 0)
			flows.push(curr);
	}

	std::cout << "flows num:" << flows.size() << " incast dsts num:" << incast_dsts.size() << std::endl;
}

/**
 * generate incast flows for fat-tree topo; k(defualt 8), n = k**3/4, m
 * the incast mode is : n * (m-1)/m -----send to----> n * 1/m
*/
void generate_incast_fattree_nsdi24(uint32_t m, uint32_t k=8, uint32_t incast_time=1) {
	// get sender receiver pairs
	uint32_t host_num = k * k * k / 4;
	std::unordered_map<uint32_t, uint32_t> sender_2_receiver;
	uint32_t incast_flow_num = 0;
	for(int nd = 0; nd < host_num; nd++)
	{
		if(nd > host_num/m - 1) // be sender
		{
			uint32_t sender = nd;
			uint32_t receiver = (sender-host_num/m) / (m-1);
			sender_2_receiver[sender] = receiver;
			incast_flow_num++;
		}
	}
	std::cout << "generte incast flows for fattree, k: "<< k <<" m: "<< m <<" incast time: " << incast_time << " maxRTT: " <<maxRtt<< std::endl;
	std::cout << "the sender and receiver pairs are: " << std::endl;
	for(int nd = 0; nd < host_num; nd++)
	{
		if(sender_2_receiver.find(nd) != sender_2_receiver.end()) // be sender
		{
			uint32_t sender = nd;
			std::cout<<"sender: "<< sender << " send to:";
			std::cout << " " << sender_2_receiver[sender];
			std::cout << std::endl;
		}
	}

	// generate flows
	uint32_t mean = 0, maxx = 0;
	Ptr<RandomVariableStream> rnd;
	double mean_size;//added by liuchang

	// generate incast flows
	switch (incast_cdf) {
	case 0:
		rnd = FlowGenerator::GetBurstStream(0, mean, maxx);
		break;
	case 1:
		rnd = FlowGenerator::GetIcmStream(0, mean, maxx);
		break;
	case 2:
		rnd = FlowGenerator::GetWebSearchStream(0, mean, maxx);
		break;
	case 3:
		rnd = FlowGenerator::GetDataMiningStream(0, mean, maxx);
		break;
	case 9:
		rnd = CreateObject<UniformRandomVariable>();
		rnd->SetAttribute("Min", DoubleValue(Settings::host_bw_Bps/1250000000*Settings::incast_min));
        rnd->SetAttribute("Max", DoubleValue(Settings::host_bw_Bps/1250000000*Settings::incast_max));
		mean_size = 1.0*(Settings::incast_min + Settings::incast_max) / 2;
        mean = Settings::host_bw_Bps/1250000000*mean_size*packet_payload_size;
        maxx = Settings::host_bw_Bps/1250000000*Settings::incast_max*packet_payload_size;
		std::cout<<"setting:incast_min = "<<Settings::incast_min<<endl;
		std::cout<<"setting:incast_max = "<<Settings::incast_max<<endl;
		std::cout<<"mean_size = "<<mean_size<<endl;
		break;
	case 10:
		rnd = CreateObject<UniformRandomVariable>();
		rnd->SetAttribute("Min", DoubleValue(20*1e6/100));
        rnd->SetAttribute("Max", DoubleValue(20*1e6/100));
        mean = 20*1e6/100;
        maxx = 20*1e6/100;
		break;
	default:
		getCdfFromFile(incast_cdf);
		rnd = FlowGenerator::GetFabricatedHeavyMiddle(0, mean, maxx, incast_cdf);
	}
	for(int nd = 0; nd < host_num; nd++)
	{
		if(sender_2_receiver.find(nd) != sender_2_receiver.end()) // be sender
		{
			uint32_t sender = nd;
			uint32_t receiver = sender_2_receiver[sender];
			uint64_t time_offset = 0;
			for(int times = 0; times < incast_time; times++)
			{
				uint32_t size = rnd->GetInteger();
				if (incast_cdf <= 3 || incast_cdf == 9)
					size = size * packet_payload_size;
				// Time t = Seconds(1e-7) + NanoSeconds(rand() % 100) + time_offset;
				Time t = Seconds(1e-7) + NanoSeconds(rand() % 100) + NanoSeconds(time_offset);
				time_offset = maxRtt;
				flows.push(Flow(sender, receiver, size, 2+t.GetSeconds()));
			}
		}
	}
}

// 按照概率分布随机选择一个数字
int selectByProbability(const std::vector<double>& probabilities) {
    // 累积概率
    std::vector<double> cumulativeProbabilities(probabilities.size());
    cumulativeProbabilities[0] = probabilities[0];
    for (size_t i = 1; i < probabilities.size(); ++i) {
        cumulativeProbabilities[i] = cumulativeProbabilities[i - 1] + probabilities[i];
    }

    // 生成[0,1)之间的随机浮点数
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, cumulativeProbabilities.back());

    // 找到对应的数字
    double randomValue = dis(gen);
    size_t index = std::lower_bound(cumulativeProbabilities.begin(), cumulativeProbabilities.end(), randomValue) - cumulativeProbabilities.begin();

    return index;
}

/**
 * generate flows for MOE traffic pattern.
 * 32 hosts are randomly set to 2 groups; alltoall communication is performed between 16 hosts in a group,
 * Each host sends one flow, The size is traffic_size. The destination host is selected based on the probability vector: expert_probability
 * shifft_time_mul: the diff between two groups send time, we set shifft_time to 0, 1/4 * send_intervel, 2/4 * send_intervel, 3/4 send_intervel
 * send_intervel: the intervel between two times of sending in a host, we set send_intervel to 1.5*max(flow_fct)
 * send_times: The number of times each host sends data
*/
void generate_MOE_traffic(double shifft_time_mul, double send_intervel, uint32_t traffic_size, uint32_t send_times)
{
	// get the host id
	std::vector<uint32_t> MOE_hosts; // 0~31
	for(uint32_t i = 0; i < 32; i++)
	{
		MOE_hosts.push_back(i);
	}
	// shuffle the hosts randomly, and get the two groups of hosts
	std::random_device rd;
    std::mt19937 gen {31};
	std::shuffle(MOE_hosts.begin(), MOE_hosts.end(), gen);
	vector<uint32_t> group1(MOE_hosts.begin(), MOE_hosts.begin()+16);
	vector<uint32_t> group2(MOE_hosts.begin()+16, MOE_hosts.end());
	std::cout<<"the two groups is"<<std::endl;
	for(int i=0; i<group1.size(); i++)
	{
		cout<<group1[i]<<" ";
	}
	std::cout<<endl;
	for(int i=0; i<group2.size(); i++)
	{
		cout<<group2[i]<<" ";
	}
	cout<<endl;
	// get flows
	Time start_time = Seconds(2)+Seconds(1e-7);
	for(uint32_t times=0; times < send_times; times++)
	{
		// generate flows for the first group
		for(uint32_t g1=0; g1<group1.size(); g1++)
		{
			uint32_t curr_sender = group1[g1];
			uint32_t curr_receiver;
			do
			{
				uint32_t idx = selectByProbability(Settings::expert_probability);
				curr_receiver = group1[idx];

			} while (curr_receiver == curr_sender);
			Time t = start_time + MicroSeconds(times * send_intervel) + NanoSeconds(rand() % 100);
			flows.push(Flow(curr_sender, curr_receiver, traffic_size, t.GetSeconds()));
		}
		// generate flows for the second group
		for(uint32_t g2=0; g2<group2.size(); g2++)
		{
			uint32_t curr_sender = group2[g2];
			uint32_t curr_receiver;
			do
			{
				uint32_t idx = selectByProbability(Settings::expert_probability);
				curr_receiver = group1[idx];

			} while (curr_receiver == curr_sender);
			Time t = start_time + MicroSeconds(shifft_time_mul*send_intervel) + MicroSeconds(times * send_intervel) + NanoSeconds(rand() % 100);
			flows.push(Flow(curr_sender, curr_receiver, traffic_size, t.GetSeconds()));
		}
	}
	
}

void generate_MOE_traffic(double shifft_time_mul, double send_intervel, uint32_t traffic_size, uint32_t send_times, uint32_t burst_times)
{
	// get the host id
	// std::vector<uint32_t> MOE_hosts; // 0~31
	// uint32_t tor_num = 16;
	// for(uint32_t i = 0; i < 10; i++)
	// {
	// 	// MOE_hosts.push_back(i);
	// 	for(uint32_t j=0; j<4; j++)
	// 	{
	// 		MOE_hosts.push_back(i*tor_num + j);
	// 	}
	// }
	// // shuffle the hosts randomly, and get the two groups of hosts
	// std::random_device rd;
    // std::mt19937 gen {31};
	// std::shuffle(MOE_hosts.begin(), MOE_hosts.end(), gen);
	// vector<uint32_t> group1(MOE_hosts.begin(), MOE_hosts.begin()+16);
	// vector<uint32_t> group2(MOE_hosts.begin()+16, MOE_hosts.end());
	vector<uint32_t> group1 = {80,0,2,112,18,32,115,19,113,83,67,16,99,50,96,66};
	vector<uint32_t> group2 = {35,1,3,82,48,65,114,97,33,51,17,49,34,81,64,98};
	std::cout<<"the two groups is"<<std::endl;
	for(int i=0; i<group1.size(); i++)
	{
		cout<<group1[i]<<" ";
	}
	std::cout<<endl;
	for(int i=0; i<group2.size(); i++)
	{
		cout<<group2[i]<<" ";
	}
	cout<<endl;
	// get flows
	Time start_time = Seconds(2)+Seconds(1e-7);
	for(uint32_t times=0; times < send_times; times++)
	{
		// generate flows for the first group
		for(uint32_t g1=0; g1<group1.size(); g1++)
		{
			uint32_t curr_sender = group1[g1];
			for (uint32_t burst_time=0; burst_time<burst_times; burst_time++)
			{
				uint32_t curr_receiver;
				do
				{
					uint32_t idx = selectByProbability(Settings::expert_probability);
					curr_receiver = group1[idx];

				} while (curr_receiver == curr_sender);
				Time t = start_time + MicroSeconds(times * send_intervel) + NanoSeconds(rand() % 100) + NanoSeconds(burst_time*maxRtt);
				flows.push(Flow(curr_sender, curr_receiver, traffic_size/burst_times, t.GetSeconds()));
			}
		}
		// generate flows for the second group
		for(uint32_t g2=0; g2<group2.size(); g2++)
		{
			uint32_t curr_sender = group2[g2];
			for (uint32_t burst_time=0; burst_time<burst_times; burst_time++)
			{
				uint32_t curr_receiver;
				do
				{
					uint32_t idx = selectByProbability(Settings::expert_probability);
					curr_receiver = group2[idx];

				} while (curr_receiver == curr_sender);
				Time t = start_time + MicroSeconds(shifft_time_mul*send_intervel) + MicroSeconds(times * send_intervel) + NanoSeconds(rand() % 100) + NanoSeconds(burst_time*maxRtt);
				flows.push(Flow(curr_sender, curr_receiver, traffic_size/burst_times, t.GetSeconds()));
			}
		}
	}
	
}

/*
 * Generate the flows mixed with poisson flows and incast, while each flow/message belongs to a specific connection.
 * The poisson flows(#poisson flows: Settings::flows) arrive corresponding to Settings::load after incast flows arrive, while the incast flows arrive periodically
 * (Settings::incast_mix every Settings::incast_interval microseconds for Settings::incast_time times).
 * The size of poisson flows is BDP*Settings::incast_flow_size-1, and the size of incast flows is BDP*Settings::incast_flow_size.
 * Note that
 * 1. each pair of hosts is mapped to one specific connection.
 * 2. incast flows and poisson flows don't share same receivers.
 */
void generate_poisson_incast_withQP() {

	uint32_t maxx = Settings::packet_payload * Settings::free_token * incast_flow_size;
	Settings::maxx = maxx;

	RngSeedManager::SetDiffStreamIndex(-2);	// to align flow generation with MIBO
	Ptr<UniformRandomVariable> rnd_host = CreateObject<UniformRandomVariable>();

	std::map<uint32_t, std::pair<uint32_t, uint32_t> > QPs;
	std::map<uint32_t, std::pair<uint32_t, uint32_t> > QPs_poisson;
	std::map<uint32_t, uint32_t> qp_msg_nums;// used for recording the number of msgs in a QP
	std::map<uint32_t, uint32_t> QP_incast_map; // <src*1000+dst, qp_id>, used for mapping a pair of src and dst to qp_id

	// generate all-to-one QPs for incast traffic
	uint32_t rack_num = Settings::host_num / host_per_rack;
	uint32_t qp_id = 0;
	for (uint32_t dst_host = 0; dst_host < rack_num; dst_host++) {
		for (uint32_t i = 0; i < Settings::host_num; i++) {
			if (i / host_per_rack != dst_host) {
				// sender and receiver are not in same rack
				qp_msg_nums[qp_id] = 0;
				QPs[qp_id] = std::make_pair(i, dst_host * host_per_rack);
				QP_incast_map[i * 1000 + dst_host * host_per_rack] = qp_id;
				qp_id++;
			}
		}
	}
	// generate all-to-all QPs for poission traffic
	uint32_t poisson_qp_start = qp_id;
	for (uint32_t i = 0; i < Settings::host_num; i++) {
		for (uint32_t j = 0; j < Settings::host_num; j++) {
			if (i / host_per_rack != j / host_per_rack && j % host_per_rack != 0) {
				// sender and receiver are not in same rack and this receiver is not the receiver of incast flows
				qp_msg_nums[qp_id] = 0;
				QPs_poisson[qp_id] = std::make_pair(i, j);
				qp_id++;
			}
		}
	}

	// generate flows
	std::priority_queue<std::pair<Time, std::pair<uint32_t, uint32_t> >,
			std::vector<std::pair<Time, std::pair<uint32_t, uint32_t> > >,
			std::greater<std::pair<Time, std::pair<uint32_t, uint32_t> > > > queue_time_msg;// <startTime, qp_id>
	// generate incast flows
	for (uint32_t i = 0; i < incast_time; i++) {
		uint32_t time_shift = i * incast_interval;
		uint32_t dst = i * host_per_rack;
		while (queue_time_msg.size() < incast_mix * (i + 1)) {
			Time t = Seconds(1e-7) + NanoSeconds(rand() % 100)
					+ MicroSeconds(time_shift);
			uint32_t src = rnd_host->GetInteger(0, Settings::host_num - 1);
			if (src / host_per_rack == dst / host_per_rack)
				continue;
			uint32_t qp_id = QP_incast_map[src * 1000 + dst];
			queue_time_msg.push(std::make_pair(t, std::make_pair(qp_id, maxx)));
		}
	}
	// generate poisson flows
	uint32_t mean = Settings::free_token * Settings::packet_payload - 1;
	double lambda = Settings::host_bw_Bps * load / (mean);
	double lambda_per_host;

	lambda_per_host = lambda / (Settings::host_num * (rack_num-1) / rack_num);

	std::cout << "test lambda " << lambda << std::endl;
	std::cout << "Lambda: " << lambda_per_host << std::endl;
	Ptr<ExponentialRandomVariable> e = Create<ExponentialRandomVariable>();
	e->SetStream(0);
	std::map<uint32_t, std::pair<uint32_t, uint32_t> >::iterator qp_it =
			QPs_poisson.begin();
	while (queue_time_msg.size() < flow_num + incast_mix * (incast_time)) {
		Time t = Seconds(e->GetValue(1 / lambda_per_host, 0))
				+ MicroSeconds( incast_time * incast_interval);	// poisson come with incast
		queue_time_msg.push(
				std::make_pair(t, std::make_pair(qp_it->first, mean)));
		qp_it++;
		if (qp_it == QPs_poisson.end())
			qp_it = QPs_poisson.begin();
	}

	// assign flows
	while (!queue_time_msg.empty()) {
		uint32_t qp_id = queue_time_msg.top().second.first;
		uint32_t src, dst;
		if (qp_id < poisson_qp_start) {
			// incast flows
			src = QPs[qp_id].first;
			dst = QPs[qp_id].second;
		} else {
			// poisson flows
			src = QPs_poisson[qp_id].first;
			dst = QPs_poisson[qp_id].second;
		}
		uint32_t s = queue_time_msg.top().second.second;
		Time t = queue_time_msg.top().first;
		queue_time_msg.pop();

		flows.push(Flow(src, dst, s, 2+t.GetSeconds(), qp_id, qp_msg_nums[qp_id], reset_per_qp_msg));

		std::cout << std::setprecision(10) << src << " " << dst << " " << s
				<< " " << 2 + t.GetSeconds() << " " << qp_id << " "
				<< qp_msg_nums[qp_id] << std::endl;
		qp_msg_nums[qp_id]++;

	}
}

/**
 * inter_src senders from remote DC, intra_src senders from local DC
 * Each sender establishes qp_num QP with receiver,
 * and each QP sends a large flow of flow_size which will split into small message with size of msg_size.
 */
void generate_remote_pattern(){
	uint32_t dst = 0;
	intra_src = std::min(intra_src, host_per_rack-1);
	inter_src = std::min(inter_src, host_per_rack);
	flow_size = std::max(flow_size, msg_size);

	std::cout << "generate remote pattern: intra-src = " << intra_src << " "
			<< "inter-src = " << inter_src << " "
			<< "flow-size = " << flow_size << " "
			<< "msg-size = " << msg_size << " "
			<< "qp-num = " << remote_qp_num << std::endl;

	// intra-DC traffic
	uint32_t src = 0;
	while (flows.size() < intra_src*remote_qp_num*(flow_size/msg_size)){
		src++;
		if (src == dst || src/host_per_rack != dst/host_per_rack)
			continue;
		for (uint32_t i = 0; i < remote_qp_num; i++){
			uint32_t arrive_time = rand()%100;	// us
			for (uint32_t j = 0; j < flow_size/msg_size; ++j){
				flows.push(Flow(src, dst, msg_size, arrive_time/1e9, src*remote_qp_num+i, j, reset_per_qp_msg));
				arrive_time += rand()%100;
			}
		}
	}
	// inter-DC traffic
	while (flows.size() < (intra_src + inter_src)*remote_qp_num*(flow_size/msg_size)){
		src++;
		if (src == dst || src/host_per_rack == dst/host_per_rack)
			continue;
		for (uint32_t i = 0; i < remote_qp_num; i++){
			uint32_t arrive_time = rand()%100;
			for (uint32_t j = 0; j < flow_size/msg_size; ++j){
				flows.push(Flow(src, dst, msg_size, arrive_time/1e9, src*remote_qp_num+i, j, reset_per_qp_msg));
				arrive_time += rand()%100;
			}
		}
	}
}

/*
 * For validation
 * two large flows
 */
//void generate_toy_pattern(){
//	uint32_t host1 = 0, host2 = 16;
//	Ptr<UniformRandomVariable> rnd_size = CreateObject<UniformRandomVariable>();
//	for (uint32_t i = 0; i < 10000; ++i){
//		uint32_t s = rnd_size->GetInteger(1, 1460*2-1);
//		flows.push(Flow(host1, host2, s, rand()%100/1e9));
//		flows.push(Flow(host2, host1, s, rand()%100/1e9));
//	}
//}

/*
 * For validation
 * several large flows + incast
 */
void generate_toy_pattern(){
	uint32_t dst = 0;
	uint64_t start_time = 0;

//	uint32_t flow_num = std::ceil(Settings::host_bw_Bps*8/DataRate(min_rate).GetBitRate()*2);
//	uint64_t incast_interval = maxRtt*1000;
//	uint32_t incast_size_cell = maxBdp*50;
//	uint32_t size = incast_size_cell*(flow_num+50);

	uint64_t incast_interval = maxRtt*100;
	uint32_t incast_size_cell = maxBdp*500;
	uint32_t size = INT_MAX;

	std::cout << "incast flow num: " << flow_num << " inject interval: " << incast_interval << " first incast_size: " << size << std::endl;
	Ptr<UniformRandomVariable> rnd_src = CreateObject<UniformRandomVariable>();
	while (flows.size() < flow_num){
		uint32_t src = rnd_src->GetInteger(Settings::host_per_rack, Settings::host_num-1);
		uint64_t stop_time = (2*flow_num-flows.size()-1)*incast_interval;
		Flow curr(src, dst, size, 2+start_time/1e9);
		curr.SetStopTime(stop_time/1e9);
		flows.push(curr);
		std::cout << src << " " << dst << " " << size << " " << start_time/1e9 << " " << stop_time/1e9 << std::endl;
//		size -= incast_size_cell;
		start_time += incast_interval;
	}
}

/*
 * For validation
 * all to all large flows
 */
void generate_toy_all_to_all(){
	uint64_t stop_time = 2.1*1e9;
	for (uint32_t i = 0; i < Settings::host_num; ++i){
		for (uint32_t j = 0; j < Settings::host_num; ++j){
		// for (uint32_t j = 0; j < 1; ++j){
			if (i == j || i/Settings::host_per_rack == j/Settings::host_per_rack) continue;
//			if (i == j) continue;
			for (uint32_t k = 0; k < 4; ++k){
				double start_time = 2+rand()%50/1e9;
				Flow curr(i, j, INT_MAX, start_time);
				curr.SetStopTime(stop_time/1e9);
				flows.push(curr);
				std::cout << i << " " << j << " " << INT_MAX << " " << start_time << " " << stop_time << std::endl;
			}
		}
	}
}

/*
 * Pattern for fairness/convenge experiment
 * Srcs under ToR1 -> Rcv(host0)
 * Flows join ToR0-host0 link one by one every 0.1 second and leave afterwards.
 */
void generate_fairness_incast(){
	// generate flows
	uint32_t dst = 0;
	uint32_t src = Settings::host_per_rack;
	while (flows.size() < flow_num){
		Flow curr(src++, dst, INT_MAX, 2+0.1*flows.size());
		curr.SetStopTime(2+(2*flow_num-flows.size()-1)*0.1);
		flows.push(curr);
	}
	simulator_stop_time = 2+(2*flow_num-1)*0.1;
}

/*
 * Pattern for fairness/convergence experiment
 * Srcs under ToR1 -> Rcvs under ToR0
 * Attention: the sum of bandwidth between ToR0 and ToR1 should be equal to nic bandwidth
 * Flows join ToR0-Core-ToR1 link one by one every 0.1 second and leave afterwards.
 */
void generate_fairness_oversubscribed_topo(){
	// generate flows
	uint32_t dst = 0;
	uint32_t src = Settings::host_per_rack;
	double interval = 0.01;
	assert(flow_num < Settings::host_per_rack);
	while (flows.size() < flow_num){
		Flow curr(src++, dst++, INT_MAX, 2+interval*flows.size());
		curr.SetStopTime(2+(2*flow_num-flows.size()-1)*interval);
		flows.push(curr);
	}
	simulator_stop_time = 2+(2*flow_num-1)*interval;
}

int main(int argc, char *argv[])
{
	clock_t begint, endt;
	begint = clock();
#ifndef PGO_TRAINING
	if (argc > 1)
#else
	if (true)
#endif
	{
		//Read the configuration file
		std::ifstream conf;
#ifndef PGO_TRAINING
		conf.open(argv[1]);
#else
		conf.open(PATH_TO_PGO_CONFIG);
#endif

		//the file should be opened successfully. otherwise, return error
		if(!conf.is_open()){
			std::cerr<<"Error: Fail to open file "<<argv[1]<<std::endl;
			return 0;
		}

		while (!conf.eof())
		{
			std::string key;
			conf >> key;

			//std::cout << conf.cur << "\n";

			if (key.compare("ENABLE_QCN") == 0)
			{
				uint32_t v;
				conf >> v;
				enable_qcn = v;
				if (enable_qcn)
					std::cout << "ENABLE_QCN\t\t\t" << "Yes" << "\n";
				else
					std::cout << "ENABLE_QCN\t\t\t" << "No" << "\n";
			}
			else if (key.compare("USE_DYNAMIC_PFC_THRESHOLD") == 0)
			{
				uint32_t v;
				conf >> v;
				use_dynamic_pfc_threshold = v;
				if (use_dynamic_pfc_threshold)
					std::cout << "USE_DYNAMIC_PFC_THRESHOLD\t" << "Yes" << "\n";
				else
					std::cout << "USE_DYNAMIC_PFC_THRESHOLD\t" << "No" << "\n";
			}
			else if (key.compare("CLAMP_TARGET_RATE") == 0)
			{
				uint32_t v;
				conf >> v;
				clamp_target_rate = v;
				if (clamp_target_rate)
					std::cout << "CLAMP_TARGET_RATE\t\t" << "Yes" << "\n";
				else
					std::cout << "CLAMP_TARGET_RATE\t\t" << "No" << "\n";
			}
			else if (key.compare("PAUSE_TIME") == 0)
			{
				double v;
				conf >> v;
				pause_time = v;
				std::cout << "PAUSE_TIME\t\t\t" << pause_time << "\n";
			}
			else if (key.compare("DATA_RATE") == 0)
			{
				std::string v;
				conf >> v;
				data_rate = v;
				std::cout << "DATA_RATE\t\t\t" << data_rate << "\n";
			}
			else if (key.compare("LINK_DELAY") == 0)
			{
				std::string v;
				conf >> v;
				link_delay = v;
				std::cout << "LINK_DELAY\t\t\t" << link_delay << "\n";
			}
			else if (key.compare("PACKET_PAYLOAD_SIZE") == 0)
			{
				uint32_t v;
				conf >> v;
				packet_payload_size = v;
//				packet_mtu = packet_payload_size + CustomHeader::GetUdpHeaderSize() + 20 + 38;
				Settings::packet_payload = packet_payload_size;
//				Settings::MTU = packet_mtu;
				std::cout << "PACKET_PAYLOAD_SIZE\t\t" << packet_payload_size << "\n";
			}
			else if (key.compare("L2_CHUNK_SIZE") == 0)
			{
				uint32_t v;
				conf >> v;
				l2_chunk_size = v;
				std::cout << "L2_CHUNK_SIZE\t\t\t" << l2_chunk_size << "\n";
			}
			else if (key.compare("L2_ACK_INTERVAL") == 0)
			{
				uint32_t v;
				conf >> v;
				l2_ack_interval = v;
				std::cout << "L2_ACK_INTERVAL\t\t\t" << l2_ack_interval << "\n";
			}
			else if (key.compare("L2_BACK_TO_ZERO") == 0)
			{
				uint32_t v;
				conf >> v;
				l2_back_to_zero = v;
				if (l2_back_to_zero)
					std::cout << "L2_BACK_TO_ZERO\t\t\t" << "Yes" << "\n";
				else
					std::cout << "L2_BACK_TO_ZERO\t\t\t" << "No" << "\n";
			}
			else if (key.compare("TOPOLOGY_FILE") == 0)
			{
				std::string v;
				conf >> v;
				topology_file = v;
				std::cout << "TOPOLOGY_FILE\t\t\t" << topology_file << "\n";
			}
			else if (key.compare("QUOTA_MODE") == 0){
				uint32_t v;
				conf >> v;
				Settings::quota_mode = v;
				std::cout << "QUOTA_MODE\t\t\t" << Settings::quota_mode << "\n";
			}
			else if (key.compare("QUOTA_NUM") == 0){
				uint32_t v;
				conf >> v;
				Settings::quota_num = v;
				std::cout << "QUOTA_NUM\t\t\t" << Settings::quota_num << "\n";
			}else if (key.compare("REALTIME_BUFFER_BW_FILE") == 0){
				conf >> filename_keyword;
				char s_file[200];

				sprintf(s_file,"%s-bw.log", filename_keyword.c_str());
				Settings::bw_out.open(s_file);
				sprintf(s_file,"%s-rate.log", filename_keyword.c_str());
				Settings::rate_out.open(s_file);
				sprintf(s_file,"%s-dataQueuing.log", filename_keyword.c_str());
				Settings::queuing_out.open(s_file);
				sprintf(s_file,"%s-warning.log", filename_keyword.c_str());
				Settings::warning_out.open(s_file);

				sprintf(s_file,"%s.tr", filename_keyword.c_str());
				trace_output_file = std::string(s_file);
				sprintf(s_file,"%s-pfc.txt", filename_keyword.c_str());
				pfc_output_file = std::string(s_file);
				sprintf(s_file,"%s-ci.txt", filename_keyword.c_str());
				ci_output_file = std::string(s_file);
				sprintf(s_file,"%s-fct.txt", filename_keyword.c_str());
				fct_output_file = std::string(s_file);
				sprintf(s_file,"%s-bw.txt", filename_keyword.c_str());
				bw_mon_file = std::string(s_file);
				sprintf(s_file,"%s-queuing.txt", filename_keyword.c_str());
				queueing_mon_file = std::string(s_file);
				sprintf(s_file,"%s-qlen.txt", filename_keyword.c_str());
				qlen_mon_file = std::string(s_file);

				std::cout << "REALTIME_BUFFER_BW\t\t\t" << filename_keyword << "\n";
			}else if (key.compare("BW_INTERVAL") == 0){
				conf >> Settings::bw_interval;
				std::cout << "BW_INTERVAL\t\t\t" << Settings::bw_interval << "\n";
			}else if (key.compare("BUFFER_INTERVAL") == 0){
				conf >> Settings::buffer_interval;
				std::cout << "BUFFER_INTERVAL\t\t\t" << Settings::buffer_interval << "\n";
			}
			else if (key.compare("FLOW_FILE") == 0)
			{
				std::string v;
				conf >> v;
				flow_file = v;
				std::cout << "FLOW_FILE\t\t\t" << flow_file << "\n";
			}
			else if (key.compare("TRACE_FILE") == 0)
			{
				std::string v;
				conf >> v;
				trace_file = v;
				std::cout << "TRACE_FILE\t\t\t" << trace_file << "\n";
			}
			else if (key.compare("SIMULATOR_STOP_TIME") == 0)
			{
				double v;
				conf >> v;
				simulator_stop_time = v;
				std::cout << "SIMULATOR_STOP_TIME\t\t" << simulator_stop_time << "\n";
			}
			else if (key.compare("ALPHA_RESUME_INTERVAL") == 0)
			{
				double v;
				conf >> v;
				alpha_resume_interval = v;
				std::cout << "ALPHA_RESUME_INTERVAL\t\t" << alpha_resume_interval << "\n";
			}
			else if (key.compare("RP_TIMER") == 0)
			{
				double v;
				conf >> v;
				rp_timer = v;
				std::cout << "RP_TIMER\t\t\t" << rp_timer << "\n";
			}
			else if (key.compare("EWMA_GAIN") == 0)
			{
				double v;
				conf >> v;
				ewma_gain = v;
				std::cout << "EWMA_GAIN\t\t\t" << ewma_gain << "\n";
			}
			else if (key.compare("FAST_RECOVERY_TIMES") == 0)
			{
				uint32_t v;
				conf >> v;
				fast_recovery_times = v;
				std::cout << "FAST_RECOVERY_TIMES\t\t" << fast_recovery_times << "\n";
			}
			else if (key.compare("RATE_AI") == 0)
			{
				std::string v;
				conf >> v;
				rate_ai = v;
				std::cout << "RATE_AI\t\t\t\t" << rate_ai << "\n";
			}
			else if (key.compare("RATE_HAI") == 0)
			{
				std::string v;
				conf >> v;
				rate_hai = v;
				std::cout << "RATE_HAI\t\t\t" << rate_hai << "\n";
			}
			else if (key.compare("ERROR_RATE_PER_LINK") == 0)
			{
				double v;
				conf >> v;
				error_rate_per_link = v;
				std::cout << "ERROR_RATE_PER_LINK\t\t" << error_rate_per_link << "\n";
			}
			else if (key.compare("CC_MODE") == 0){
				conf >> Settings::cc_mode;
				std::cout << "CC_MODE\t\t" << Settings::cc_mode << '\n';
				cc_set = true;
			}else if(key.compare("mibo_cc_mode") == 0){
				conf >> mibo_cc_mode;
				std::cout<<"mibo_cc_mode\t\t" << mibo_cc_mode << '\n';
			}
			else if (key.compare("RATE_DECREASE_INTERVAL") == 0){
				double v;
				conf >> v;
				rate_decrease_interval = v;
				std::cout << "RATE_DECREASE_INTERVAL\t\t" << rate_decrease_interval << "\n";
			}else if (key.compare("MIN_RATE") == 0){
				conf >> min_rate;
				std::cout << "MIN_RATE\t\t" << min_rate << "\n";
			}else if (key.compare("HAS_WIN") == 0){
				conf >> has_win;
				std::cout << "HAS_WIN\t\t" << has_win << "\n";
			}else if (key.compare("GLOBAL_T") == 0){
				conf >> global_t;
				std::cout << "GLOBAL_T\t\t" << global_t << '\n';
			}else if (key.compare("MI_THRESH") == 0){
				conf >> mi_thresh;
				std::cout << "MI_THRESH\t\t" << mi_thresh << '\n';
			}else if (key.compare("VAR_WIN") == 0){
				uint32_t v;
				conf >> v;
				var_win = v;
				std::cout << "VAR_WIN\t\t" << v << '\n';
			}else if (key.compare("FAST_REACT") == 0){
				uint32_t v;
				conf >> v;
				fast_react = v;
				std::cout << "FAST_REACT\t\t" << v << '\n';
			}else if (key.compare("U_TARGET") == 0){
				conf >> u_target;
				std::cout << "U_TARGET\t\t" << u_target << '\n';
			}else if (key.compare("INT_MULTI") == 0){
				conf >> int_multi;
				std::cout << "INT_MULTI\t\t\t\t" << int_multi << '\n';
			}else if (key.compare("RATE_BOUND") == 0){	// 1: use rate control
				uint32_t v;
				conf >> v;
				rate_bound = v;
				std::cout << "RATE_BOUND\t\t" << rate_bound << '\n';
			}else if (key.compare("ACK_HIGH_PRIO") == 0){
				conf >> ack_high_prio;
				std::cout << "ACK_HIGH_PRIO\t\t" << ack_high_prio << '\n';
			}else if (key.compare("SWITCH_ACK_HIGH_PRIO") == 0){
				conf >> switch_ack_high_prio;
				std::cout << "SWITCH_ACK_HIGH_PRIO\t\t" << switch_ack_high_prio << '\n';
			}else if (key.compare("DCTCP_RATE_AI") == 0){
				conf >> dctcp_rate_ai;
				std::cout << "DCTCP_RATE_AI\t\t\t\t" << dctcp_rate_ai << "\n";
			}else if (key.compare("LINK_DOWN") == 0){
				conf >> link_down_time >> link_down_A >> link_down_B;
				std::cout << "LINK_DOWN\t\t\t\t" << link_down_time << ' '<< link_down_A << ' ' << link_down_B << '\n';
			}else if (key.compare("ENABLE_TRACE") == 0){
				conf >> enable_trace;
				std::cout << "ENABLE_TRACE\t\t\t\t" << enable_trace << '\n';
			}else if (key.compare("KMAX_MAP") == 0){
				int n_k ;
				conf >> n_k;
				std::cout << "KMAX_MAP\t\t\t\t";
				for (int i = 0; i < n_k; i++){
					uint64_t rate;
					uint32_t k;
					conf >> rate >> k;
					rate2kmax[rate] = k;
					std::cout << ' ' << rate << ' ' << k;
				}
				std::cout<<'\n';
			}else if (key.compare("KMIN_MAP") == 0){
				int n_k ;
				conf >> n_k;
				std::cout << "KMIN_MAP\t\t\t\t";
				for (int i = 0; i < n_k; i++){
					uint64_t rate;
					uint32_t k;
					conf >> rate >> k;
					rate2kmin[rate] = k;
					std::cout << ' ' << rate << ' ' << k;
				}
				std::cout<<'\n';
			}else if (key.compare("PMAX_MAP") == 0){
				int n_k ;
				conf >> n_k;
				std::cout << "PMAX_MAP\t\t\t\t";
				for (int i = 0; i < n_k; i++){
					uint64_t rate;
					double p;
					conf >> rate >> p;
					rate2pmax[rate] = p;
					std::cout << ' ' << rate << ' ' << p;
				}
				std::cout<<'\n';
			}else if (key.compare("BUFFER_SIZE") == 0){
				conf >> buffer_size;
				std::cout << "BUFFER_SIZE\t\t\t\t" << buffer_size << '\n';
			}else if (key.compare("QLEN_MON_START") == 0){
				conf >> qlen_mon_start;
				std::cout << "QLEN_MON_START\t\t\t\t" << qlen_mon_start << '\n';
			}else if (key.compare("QLEN_MON_END") == 0){
				conf >> qlen_mon_end;
				std::cout << "QLEN_MON_END\t\t\t\t" << qlen_mon_end << '\n';
			}else if (key.compare("MULTI_RATE") == 0){
				int v;
				conf >> v;
				multi_rate = v;
				std::cout << "MULTI_RATE\t\t\t\t" << multi_rate << '\n';
			}else if (key.compare("SAMPLE_FEEDBACK") == 0){
				int v;
				conf >> v;
				sample_feedback = v;
				std::cout << "SAMPLE_FEEDBACK\t\t\t\t" << sample_feedback << '\n';
			}else if (key.compare("FLOW_CDF") == 0){
				conf >> flow_cdf;
				std::cout << "FLOW_CDF\t\t\t\t" << flow_cdf << '\n';
			}else if (key.compare("LOAD") == 0){
				double v;
				conf >> v;
				load = v;
				std::cout << "LOAD\t\t\t\t" << load << '\n';
			}else if (key.compare("ARRIVAL_MODE") == 0){
				conf >> arrival_mode;
				std::cout << "ARRIVAL_MODE\t\t\t\t" << arrival_mode << '\n';
			}else if(key.compare("AVG_MESSAGE_PER_QP") == 0){
				conf >> avg_message_per_qp;
				std::cout << "AVG_MESSAGE_PER_QP\t\t\t\t" << avg_message_per_qp << '\n';
			}else if(key.compare("FLOW_NUM") == 0){
				conf >> flow_num;
				std::cout << "FLOW_NUM\t\t\t\t" << flow_num << '\n';
			}else if(key.compare("FLOW_FROM_FILE") == 0){
				conf >> flow_from_file;
				std::cout << "FLOW_FROM_FILE\t\t\t\t" << flow_from_file << '\n';
			}else if(key.compare("SEED") == 0){
				conf >> seed;
				srand(seed);
				std::cout << "SEED\t\t\t\t" << seed << '\n';
			}else if(key.compare("STORAGE_QP_NUM") == 0){
				conf >> storage_qp_num;
				std::cout << "STORAGE_QP_NUM\t\t\t\t" << storage_qp_num << '\n';
			}else if(key.compare("STORAGE_TIMES") == 0){
				conf >> storage_times;
				std::cout << "STORAGE_TIMES\t\t\t\t" << storage_times << '\n';
			}else if(key.compare("STORAGE_MESSAGE_SIZE") == 0){
				conf >> storage_message_size;
				std::cout << "STORAGE_MESSAGE_SIZE\t\t\t\t" << storage_message_size << '\n';
			}else if(key.compare("STORAGE_IODEPTH") == 0){
				conf >> storage_iodepth;
				std::cout << "STORAGE_IODEPTH\t\t\t\t" << storage_iodepth << '\n';
			}else if(key.compare("RESET_QP_RATE") == 0){
				uint32_t reset_qp_rate_int;
				conf >> reset_qp_rate_int;
				if (reset_qp_rate_int == 1) Settings::reset_qp_rate = true;
				else Settings::reset_qp_rate = false;
				std::cout << "DCQCN_RESET_QP_RATE\t\t\t\t" << Settings::reset_qp_rate << '\n';
			}else if(key.compare("RTO_us") == 0){
				conf >> Settings::rto_us;
				std::cout << "RTO_us\t\t\t\t" << Settings::rto_us << '\n';
			}else if(key.compare("FC_MODE") == 0){
				conf >> Settings::fc_mode;
				std::cout << "FC_MODE\t\t\t\t" << Settings::fc_mode << '\n';
			}else if(key.compare("SWITCHWIN_CONFIG_FILE") == 0){
				conf >> switchwin_config_file;
				std::cout << "SWITCHWIN_CONFIG_FILE\t\t\t\t" <<  switchwin_config_file << '\n';
			}else if(key.compare("RESET_ONLY_TOR_SWITCH_WIN") == 0){
				uint32_t only_ToR_switch_win_int;
				conf >> only_ToR_switch_win_int;
				Settings::reset_only_ToR_switch_win = (only_ToR_switch_win_int == 1);
				std::cout << "RESET_ONLY_TOR_SWITCH_WIN\t\t\t\t" << Settings::reset_only_ToR_switch_win << '\n';
			}else if(key.compare("SWITCH_WIN_M") == 0){
				conf >> Settings::switch_win_m;
				std::cout << "SWITCH_WIN_M\t\t\t\t" << Settings::switch_win_m << '\n';
			}else if(key.compare("SWITCH_PORT_WIN_M") == 0){
				conf >> Settings::switch_port_win_m;
				std::cout << "SWITCH_PORT_WIN_M\t\t\t\t" << Settings::switch_port_win_m << '\n';
			}else if(key.compare("ROUTING_MODE") == 0){
				conf >> Settings::routing_mode;
				std::cout << "ROUTING_MODE\t\t\t\t" << Settings::routing_mode << '\n';
			}else if(key.compare("DRILL_LOAD_MODE") == 0){
				conf >> Settings::drill_load_mode;
				std::cout << "DRILL_LOAD_MODE\t\t\t\t" << Settings::drill_load_mode << '\n';
			}else if(key.compare("SYMMETRIC_ROUTING_MODE") == 0){
				conf >> Settings::symmetic_routing_mode;
				assert(Settings::symmetic_routing_mode == 0 || Settings::symmetic_routing_mode == 1);
				std::cout << "SYMMETRIC_ROUTING_MODE\t\t\t\t" << Settings::symmetic_routing_mode << '\n';
			}else if(key.compare("LOAD_IMBALANCE_TH") == 0){
				conf >> Settings::load_imbalance_th;
				std::cout << "LOAD_IMBALANCE_TH " << Settings::load_imbalance_th << '\n';
			}else if(key.compare("QUEUE_STATISTIC_INTERVAL_US") == 0){
				conf >> Settings::queue_statistic_interval_us;
				std::cout << "QUEUE_STATISTIC_INTERVAL_US\t\t\t\t" << Settings::queue_statistic_interval_us << '\n';
			}else if(key.compare("NACK_REACTION") == 0){
				uint32_t nack_reaction_int;
				conf >> nack_reaction_int;
				Settings::nack_reaction = (nack_reaction_int == 1);
				std::cout << "NACK_REACTION\t\t\t\t" << Settings::nack_reaction << '\n';
			}else if(key.compare("INCAST_MIX") == 0){
				conf >> incast_mix;
				std::cout << "INCAST_MIX\t\t\t\t" << incast_mix << '\n';
			}else if(key.compare("INCAST_INTERVAL") == 0){
				conf >> incast_interval;
				std::cout << "INCAST_INTERVAL\t\t\t\t" << incast_interval << '\n';
			}else if(key.compare("INCAST_CDF") == 0){
				conf >> incast_cdf;
				std::cout << "INCAST_CDF\t\t\t\t" << incast_cdf << '\n';
			}else if(key.compare("INCAST_LOAD") == 0){
				conf >> incast_load;
				std::cout << "INCAST_LOAD\t\t\t\t" << incast_load << '\n';
			}else if(key.compare("INCAST_TIME") == 0){
				conf >> incast_time;
				std::cout << "INCAST_TIME\t\t\t\t" << incast_time << '\n';
			}else if(key.compare("INCAST_FLOW_SIZE") == 0){
				conf >> incast_flow_size;
				std::cout << "INCAST_FLOW_SIZE\t\t\t\t" << incast_flow_size << '\n';
			}else if(key.compare("INCAST_SCALE") == 0){
				conf >> incast_scale;
				std::cout << "INCAST_SCALE\t\t\t\t" << incast_scale << '\n';
			}else if(key.compare("ONE_INCAST_DST") == 0){
				conf >> Settings::one_incast_dst;
				std::cout << "ONE_INCAST_DST\t\t\t\t" << Settings::one_incast_dst << '\n';

			}else if(key.compare("RESET_PER_QP_MSG") == 0){
				int reset_per_qp_msg_int;
				conf >> reset_per_qp_msg_int;
				reset_per_qp_msg = (reset_per_qp_msg_int == 1);
				std::cout << "RESET_PER_QP_MSG\t\t\t\t" << reset_per_qp_msg << '\n';
			}else if(key.compare("INTRA_SRC") == 0){
				conf >> intra_src;
				std::cout << "INTRA_SRC\t\t\t\t" << intra_src << '\n';
			}else if(key.compare("INTER_SRC") == 0){
				conf >> inter_src;
				std::cout << "INTER_SRC\t\t\t\t" << inter_src << '\n';
			}else if(key.compare("FLOW_SIZE") == 0){
				conf >> flow_size;
				std::cout << "FLOW_SIZE\t\t\t\t" << flow_size << '\n';
			}else if(key.compare("MSG_SIZE") == 0){
				conf >> msg_size;
				std::cout << "MSG_SIZE\t\t\t\t" << msg_size << '\n';
			}else if(key.compare("REMOTE_QP_NUM") == 0){
				conf >> remote_qp_num;
				std::cout << "REMOTE_QP_NUM\t\t\t\t" << remote_qp_num << '\n';
			}else if(key.compare("OUTPUT_REALTIME_BUFFER") == 0){
				conf >> output_realtime;
				std::cout << "OUTPUT_REALTIME_BUFFER\t\t\t\t" << output_realtime << '\n';
			}else if(key.compare("ADAPTIVE_WIN") == 0){
				uint16_t adaptive_win;
				conf >> adaptive_win;
				Settings::adaptive_win = adaptive_win;
				std::cout << "ADAPTIVE_WIN\t\t\t\t" << Settings::adaptive_win << '\n';
			}else if(key.compare("ADAPTIVE_WHOLE_SWITCH") == 0){
				bool adaptive_whole_switch;
				conf >> adaptive_whole_switch;
				Settings::adaptive_whole_switch = adaptive_whole_switch;
				std::cout << "ADAPTIVE_WHOLE_SWITCH\t\t\t\t" << Settings::adaptive_whole_switch << '\n';
			}else if(key.compare("SWTICH_ACK_MODE") == 0){
				conf >> Settings::switch_ack_mode;
				assert(Settings::switch_ack_mode >= Settings::HOST_PER_PACKET_ACK && Settings::switch_ack_mode <= Settings::SWITCH_INGRESS_CREDIT);
				std::cout << "SWTICH_ACK_MODE\t\t\t\t" << Settings::switch_ack_mode << '\n';
			}else if(key.compare("SWTICH_CREDIT_INTERVAL") == 0){
				conf >> Settings::switch_credit_interval;
				std::cout << "SWTICH_CREDIT_INTERVAL\t\t\t\t" << Settings::switch_credit_interval << '\n';
			}else if(key.compare("SWTICH_BYTE_COUNTER") == 0){
				conf >> Settings::switch_byte_counter;
				std::cout << "SWTICH_BYTE_COUNTER\t\t\t\t" << Settings::switch_byte_counter << '\n';
			}else if(key.compare("PFC_DST_ALHPA") == 0){
				conf >> Settings::pfc_dst_alpha;
				std::cout << "PFC_DST_ALHPA\t\t\t\t" << Settings::pfc_dst_alpha << '\n';
			}else if(key.compare("SRCTOR_DST_PFC") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::srcToR_dst_pfc = (tmp > 0);
				std::cout << "SRCTOR_DST_PFC\t\t\t\t" << Settings::srcToR_dst_pfc << '\n';
			}else if(key.compare("PFC_TH_STATIC") == 0){
				conf >> Settings::pfc_th_static;
				std::cout << "PFC_THE_STATIC\t\t\t\t" << Settings::pfc_th_static << '\n';
			}else if(key.compare("USE_PORT_WIN") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::use_port_win = (tmp > 0);
				std::cout << "USE_PORT_WIN\t\t\t\t" << Settings::use_port_win << '\n';
			}else if(key.compare("SWITCH_ACK_TH_M") == 0){
				conf >> Settings::switch_ack_th_m;
				std::cout << "SWITCH_ACK_TH_M\t\t\t\t" << Settings::switch_ack_th_m << '\n';
			}else if(key.compare("SWITCH_ABSOLUTE_PSN") == 0){
				conf >> Settings::switch_absolute_psn;
				std::cout << "SWITCH_ABSOLUTE_PSN\t\t\t\t" << Settings::switch_absolute_psn << '\n';
			}else if(key.compare("SWITCH_SYN_TIMEOUT_US") == 0){
				conf >> Settings::switch_syn_timeout_us;
				std::cout << "SWITCH_SYN_TIMEOUT_US\t\t\t\t" << Settings::switch_syn_timeout_us << '\n';
			}else if(key.compare("L4_TOKEN_MODE") == 0){
				conf >> l4_token_mode;
				std::cout << "L4_TOKEN_MODE\t\t\t\t" << l4_token_mode << '\n';
			}else if(key.compare("NIC_TOKEN_MODE") == 0){
				conf >> nic_token_mode;
				std::cout << "NIC_TOKEN_MODE\t\t\t\t" << nic_token_mode << '\n';
			}else if(key.compare("CREDIT_EMIN") == 0){
				conf >> credit_Emin;
				std::cout << "CREDIT_EMIN\t\t\t\t" << credit_Emin << '\n';
			}else if(key.compare("CREDIT_EMAX") == 0){
				conf >> credit_Emax;
				std::cout << "CREDIT_EMAX\t\t\t\t" << credit_Emax << '\n';
			}else if(key.compare("CREDIT_EP") == 0){
				conf >> credit_Ep;
				std::cout << "CREDIT_EP\t\t\t\t" << credit_Ep << '\n';
			}else if(key.compare("TOKEN_CC_MODE") == 0){
				conf >> token_cc;
				std::cout << "TOKEN_CC_MODE\t\t\t\t" << token_cc << '\n';
			}else if(key.compare("PIGGY_INGRESS_ECN") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::piggy_ingress_ecn = (tmp == 1);
				std::cout << "PIGGY_INGRESS_ECN\t\t\t\t" << Settings::piggy_ingress_ecn << '\n';
			}else if(key.compare("MAX_UNSCH") == 0){
				conf >> max_unsch;
				std::cout << "MAX_UNSCH\t\t\t\t" << max_unsch << '\n';
			}else if(key.compare("LIMIT_TKN_MODE") == 0){
				conf >> limit_tkn_mode;
				std::cout << "LIMIT_TKN_MODE\t\t\t\t" << limit_tkn_mode << '\n';
			}else if(key.compare("TURN_SCH_PHASE_MODE") == 0){
				conf >> turn_sch_phase_mode;
				std::cout << "TURN_SCH_PHASE_MODE\t\t\t\t" << turn_sch_phase_mode << '\n';
			}else if(key.compare("RATE_LIMITER_MODE") == 0){
				uint32_t tmp;
				conf >> tmp;
				rate_limiter_mode = (tmp == 1);
				std::cout << "RATE_LIMITER_MODE\t\t\t\t" << rate_limiter_mode << '\n';
			}else if(key.compare("RESUME_BDP_TKN") == 0){
				uint32_t tmp;
				conf >> tmp;
				resume_bdp_tkn = (tmp == 1);
				std::cout << "RESUME_BDP_TKN\t\t\t\t" << resume_bdp_tkn << '\n';
			}else if(key.compare("HOST_CNP") == 0){
				conf >> Settings::host_cnp;
				std::cout << "HOST_CNP\t\t\t\t" << Settings::host_cnp << '\n';
			}else if(key.compare("MIBO_TKN_ECN") == 0){
				conf >> mibo_tkn_ecn;
				std::cout << "MIBO_TKN_ECN\t\t\t\t" << mibo_tkn_ecn << '\n';
			}else if(key.compare("EQUAL_FLOW_SIZE") == 0){
				conf >> equal_flow_size;
				std::cout << "EQUAL_FLOW_SIZE" << equal_flow_size << '\n';
			}else if(key.compare("STICKY_TH") == 0){
				conf >> Settings::sticky_th;
				std::cout << "STICKY_TH" << Settings::sticky_th << '\n';
			}else if(key.compare("FLOWTB_SIZE") == 0){
				conf >> Settings::flowtb_size;
				std::cout << "FLOWTB_SIZE " << Settings::flowtb_size << '\n';
			}else if(key.compare("USE_HASH_FID") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::use_hash_fid = (tmp == 1);
				std::cout << "USE_HASH_FID" << Settings::use_hash_fid << '\n';
			}else if(key.compare("ASSIGN_STICKY_Q") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::assign_sticky_q = (tmp == 1);
				std::cout << "ASSIGN_STICKY_Q" << Settings::assign_sticky_q << '\n';
			}else if(key.compare("VOQ_PAUSE_M") == 0){
				conf >> Settings::voq_pauseth_m;
				std::cout << "VOQ_PAUSE_M" << Settings::voq_pauseth_m << '\n';
			}else if(key.compare("VOQ_PAUSE_MODE") == 0){
				conf >> Settings::voq_pause_mode;
				std::cout << "VOQ_PAUSE_MODE"  << Settings::voq_pause_mode << '\n';
			}else if(key.compare("CTRL_ALL_EGRESS") == 0){
				conf >> Settings::ctrl_all_egress;
				std::cout << "CTRL_ALL_EGRESS"  << Settings::ctrl_all_egress << '\n';
			}else if(key.compare("FLOW_SCH_TH") == 0){
				conf >> Settings::flow_sch_th;
				std::cout << "FLOW_SCH_TH" << Settings::flow_sch_th << '\n';
			}else if(key.compare("MAX_RANGE_SCALE_ALPHA") == 0){
				conf >> swift_fs_range_alpha;
				std::cout << "MAX_RANGE_SCALE_ALPHA\t\t\t\t" << swift_fs_range_alpha << '\n';
			}else if(key.compare("MIN_CWND_SCALE") == 0){
				conf >> swift_fs_min_cwnd;
				std::cout << "MIN_CWND_SCALE\t\t\t\t" << swift_fs_min_cwnd << '\n';
			}else if(key.compare("MIN_CWND") == 0){
				conf >> swift_min_cwnd;
				std::cout << "MIN_CWND\t\t\t\t" << swift_min_cwnd << '\n';
			}else if(key.compare("AI_CWND") == 0){
				conf >> swift_ai;
				std::cout << "AI_CWND\t\t\t\t" << swift_ai << '\n';
			}else if(key.compare("MD_CWND") == 0){
				conf >> swift_md;
				std::cout << "MD_CWND\t\t\t\t" << swift_md << '\n';
			}else if(key.compare("MAX_MDF") == 0){
				conf >> swift_max_mdf;
				std::cout << "MAX_MDF\t\t\t\t" << swift_max_mdf << '\n';
			}else if(key.compare("ALPHA_EWMA") == 0){
				conf >> swift_ewma_alpha;
				std::cout << "ALPHA_EWMA\t\t\t\t" << swift_ewma_alpha << '\n';
			}else if(key.compare("RETX_RESET_TH") == 0){
				conf >> swift_retx_reset_threshold;
				std::cout << "RETX_RESET_TH\t\t\t\t" << swift_retx_reset_threshold << '\n';
			}else if(key.compare("END_TARGET") == 0){
				conf >> swift_end_target;
				std::cout << "END_TARGET\t\t\t\t" << swift_end_target << '\n';
			}else if(key.compare("SWIFT_HOP_SCALE") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::swift_hop_scale = (tmp == 1);
				std::cout << "SWIFT_HOP_SCALE " << Settings::swift_hop_scale << '\n';
			}else if(key.compare("OUT_WIN") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::is_out_win = (tmp == 1);
				std::cout << "OUT_WIN " << Settings::is_out_win << '\n';
			}else if(key.compare("TH_HOL") == 0){
				conf >> Settings::th_hol;
				std::cout << "TH_HOL " << Settings::th_hol << '\n';
			}else if(key.compare("TH_CONGESTION") == 0){
				conf >> Settings::th_congestion;
				std::cout << "TH_CONGESTION " << Settings::th_congestion << '\n';
			}else if(key.compare("TH_RESUME") == 0){
				conf >> Settings::th_resume;
				std::cout << "TH_RESUME " << Settings::th_resume << '\n';
			}else if(key.compare("TH_CIQ_RATE") == 0){
				conf >> Settings::th_ciq_rate;
				std::cout << "TH_CIQ_RATE " << Settings::th_ciq_rate << '\n';
			}else if(key.compare("CONGESTION_MERGE") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::congestion_merge = (tmp == 1);
				std::cout << "CONGESTION_MERGE " << Settings::congestion_merge << '\n';
			}else if(key.compare("NEAREST_MATCH") == 0){
				uint32_t tmp;
				conf >> tmp;
				Settings::nearest_match = (tmp == 1);
				std::cout << "NEAREST_MATCH " << Settings::nearest_match << '\n';
			}else if(key.compare("QOS_MODE") == 0){
				conf >> Settings::qos_mode;
				std::cout << "QOS_MODE " << Settings::qos_mode << '\n';
			}else if(key.compare("TIME_SHIFT") == 0){
				conf >> Settings::time_shift;
				std::cout<<"TIME_SHIFT "<<Settings::time_shift<< '\n';
			}else if(key.compare("HDRM_RATIO") == 0){
				conf >> Settings::hdrm_ratio;
				std::cout<<"HDRM_RATIO "<<Settings::hdrm_ratio<< '\n';
			}else if(key.compare("INCAST_MIN") == 0){
				conf >> Settings::incast_min;
				std::cout<<"INCAST_MIN "<<Settings::incast_min<< '\n';
			}else if(key.compare("INCAST_MAX") == 0){
				conf >> Settings::incast_max;
				std::cout<<"INCAST_MAX "<<Settings::incast_max<< '\n';
			}else if(key.compare("QUEUE_ALLOCATE_IN_HOST") == 0){
				conf >> Settings::queue_allocate_in_host;
				std::cout<<"QUEUE_ALLOCATE_IN_HOST "<<Settings::queue_allocate_in_host<< '\n';
			}else if(key.compare("USE_IRN") == 0){
				int use_irn;
				conf >> use_irn;
				if (use_irn) Settings::use_irn = true;
				else Settings::use_irn = false;
				std::cout<<"USE_IRN "<<Settings::use_irn<< '\n';
			}else if(key.compare("MAX_BITMAP_SIZE") == 0){
				conf >> max_bitmap_size;
				std::cout<<"MAX_BITMAP_SIZE "<<max_bitmap_size<< '\n';
			}else if(key.compare("RTO_HIGH") == 0){
				conf >> rto_high;
				std::cout<<"RTO_HIGH "<<rto_high<< '\n';
			}else if(key.compare("RTO_LOW") == 0){
				conf >> rto_low;
				std::cout<<"RTO_LOW "<<rto_low<< '\n';
			}else if(key.compare("RTO_LOW_TH") == 0){
				conf >> rto_low_threshold;
				std::cout<<"RTO_LOW_TH "<<rto_low_threshold<< '\n';
			}else if(key.compare("GEN_INCAST_INCREMENTAL") == 0){
				uint32_t v;
				conf >> v;
				if(v == 0) {
					Settings::gen_incast_incremental = false;
					std::cout<<"GEN_INCAST_INCREMENTAL false"<< '\n';
				}else{
					Settings::gen_incast_incremental = true;
					std::cout<<"GEN_INCAST_INCREMENTAL true"<< '\n';
				}
			}else if(key.compare("INCREMENTAL_INCAST_TIME") == 0){
				conf >> incremental_incast_time;
				std::cout<<"INCREMENTAL_INCAST_TIME "<<incremental_incast_time<< '\n';
			}else if(key.compare("FLOW_SEED") == 0){
				conf >> flow_seed;
				std::cout<<"FLOW_SEED "<<flow_seed<< '\n';
			}else if(key.compare("FLOW_SIZE_MUL") == 0){
				conf >> flow_size_mul;
				std::cout<<"FLOW_SIZE_MUL "<<flow_size_mul<< '\n';
			}else if(key.compare("ONLY_DOWN_PORT_ENABLE") == 0){ // for only enbale pyrrha in the down ports
				int only_down_port_enable = 0; // defaut set to false
				conf >> only_down_port_enable;
				std::cout<<"ONLY_DOWN_PORT_ENABLE "<<only_down_port_enable<< '\n';
				if(only_down_port_enable > 0)
				{
					Settings::only_down_port_enable = true;
				}else{
					Settings::only_down_port_enable = false;
				}
			}else if(key.compare("ENABLE_QP_WIN_BOUND") == 0){ // for enbale window control for qp
				int enable_qp_win_bound = 1; // defaut set to true
				conf >> enable_qp_win_bound;
				std::cout<<"ENABLE_QP_WIN_BOUND "<< enable_qp_win_bound<< '\n';
				if(enable_qp_win_bound > 0)
				{
					Settings::enable_qp_winBound = true;
				}else{
					Settings::enable_qp_winBound = false;
				}
			}else if(key.compare("GEN_FATTREE_INCAST_NDSI24") == 0){
				int gen_fattree_incast = 0; // defaut set to false
				conf >> gen_fattree_incast;
				std::cout<<"GEN_FATTREE_INCAST_NDSI24 "<< gen_fattree_incast<< '\n';
				if(gen_fattree_incast > 0){
					Settings::gen_incast_fattree_nsdi24 = true;
				}else{
					Settings::gen_incast_fattree_nsdi24 = false;
				}
			}else if(key.compare("FATTREE_K") == 0){
				conf >> Settings::fattree_k;
				std::cout<<"FATTREE_K "<< Settings::fattree_k<< '\n';
			}else if(key.compare("FATTREE_INCAST_M") == 0){
				conf >> Settings::fattree_incast_m;
				std::cout<<"FATTREE_INCAST_M "<< Settings::fattree_incast_m<< '\n';
			}else if(key.compare("GEN_MOE_TRAFFIC") == 0){
				int gen_moe = 0;
				conf >> gen_moe;
				std::cout<<"GEN_MOE_TRAFFIC "<< gen_moe << '\n';
				if(gen_moe > 0){
					Settings::gen_MOE_traffic = true;
				}else{
					Settings::gen_MOE_traffic = false;
				}
			}else if(key.compare("MOE_SEND_INTERVEL") == 0){
				conf >> Settings::moe_send_intervel;
				std::cout<<"MOE_SEND_INTERVEL "<< Settings::moe_send_intervel<< '\n';
			}else if(key.compare("MOE_SHIFT_TIME_MUL") == 0){
				conf >> Settings::moe_shifft_time_mul;
				std::cout<<"MOE_SHIFT_TIME_MUL "<< Settings::moe_shifft_time_mul<< '\n';
			}else if(key.compare("MOE_TRAFFIC_SIZE") == 0){
				conf >> Settings::moe_traffic_size;
				std::cout<<"MOE_TRAFFIC_SIZE "<< Settings::moe_traffic_size<< '\n';
			}else if(key.compare("MOE_SEND_TIMES") == 0){
				conf >> Settings::moe_send_times;
				std::cout<<"MOE_SEND_TIMES "<< Settings::moe_send_times<< '\n';
			}else if(key.compare("MOE_BURST_TIME") == 0){
				conf >> Settings::moe_burst_times;
				std::cout<<"MOE_BURST_TIME "<< Settings::moe_burst_times<< '\n';
			}
			fflush(stdout);
		}
		conf.close();

		assert(cc_set);

		/*----------------------------------supplementary default configure (start)----------------------------------------*/
		// set int_multi
		IntHop::multi = int_multi;
		// IntHeader::mode
		if (Settings::cc_mode == Settings::CC_TIMELY || (Settings::cc_mode == Settings::CC_MIBO_NSDI22 && token_cc == Settings::CC_TIMELY)) // timely, use ts
			IntHeader::mode = 1;
		else if (Settings::cc_mode == Settings::CC_HPCC || (Settings::cc_mode == Settings::CC_MIBO_NSDI22 && token_cc == Settings::CC_HPCC)) // hpcc, use int
			IntHeader::mode = 0;
		else if (Settings::cc_mode == Settings::CC_SWIFT || (Settings::cc_mode == Settings::CC_MIBO_NSDI22 && token_cc == Settings::CC_SWIFT)) // swift, use sendts/hopCnt/remote delay
			IntHeader::mode = 2;
		else // others, no extra header
			IntHeader::mode = 5;

		packet_mtu = packet_payload_size + CustomHeader::GetUdpHeaderSize() + 20 + 38;
		Settings::MTU = packet_mtu;
		std::cout << "MTU: " << Settings::MTU << std::endl;

		// switch queue configure
		Settings::check_shared_buffer = false;
		for (uint32_t i = 0; i < Settings::QUEUENUM; ++i){
			Settings::max_qlen[i] = INT_MAX;
			Settings::ecn_unit[i] = 0;			// no ecn by default
		}
		// qos
		if (Settings::qos_mode == Settings::QOS_PIFOQ)
			BEgressQueue::queueType = BEgressQueue::QUEUE_PIFO;

		// flow control
		bool default_pfc = true, default_queue_pfc = true;
		if (Settings::fc_mode == Settings::FC_FLOODGATE){
			default_pfc = false;
		}else if (Settings::fc_mode == Settings::FC_BFC){
			default_pfc = false;		// when use BFC, turn off PFC in switchmmu
			if(Settings::queue_allocate_in_host == 0)
				pfc_dst_nic = Settings::PAUSE_QP;
			else
				pfc_dst_nic = Settings::PAUSE_QUEUE;
		}else if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC) {
			default_pfc = false;
			pfc_dst_nic = Settings::PAUSE_DST;
		}else if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION) {
			default_pfc = false;
			pfc_dst_nic = Settings::PAUSE_SPOT;
			Settings::hier_Q = true;
		}else if(Settings::fc_mode == Settings::FC_PFCoff) {
			default_pfc = false;
			default_queue_pfc = false;
			Settings::check_shared_buffer = true;
		}
		if (Settings::srcToR_dst_pfc){
			pfc_dst_nic = Settings::PAUSE_DST;
		}

		// congestion control
		switch (Settings::cc_mode){
		case Settings::CC_MIBO_NSDI22:{

			double ratio = (84/(double)(Settings::MTU + 84));
			std::cout << "mibo-nsdi22: token rate limiter on switch queue - " << ratio << std::endl;
			// 3 priorities(0, 1, others), limit q1(token) 5%, q0(ACK and token_ack) and q1 are not PFC queues
			// priority & PFC queues

			// ctrl queue:
			std::vector<uint32_t> prio0;
			prio0.push_back(0);
			rate_limiters.push_back(0);
			pfc_ctrls.push_back(false);
			pfc_queue_ctrls.push_back(false);

			// token queue:
			std::vector<uint32_t> prio1;
			prio1.push_back(Settings::MIBO_TOKEN);
			rate_limiters.push_back(ratio);
			pfc_ctrls.push_back(false);
			pfc_queue_ctrls.push_back(false);

			// others:
			std::vector<uint32_t> prio2;
			for (uint32_t i = Settings::MIBO_TOKEN+1; i < Settings::QUEUENUM; ++i){
				prio2.push_back(i);
				pfc_ctrls.push_back(default_pfc);
				pfc_queue_ctrls.push_back(default_queue_pfc);
				rate_limiters.push_back(0);
			}

			priorities.push_back(prio0);
			priorities.push_back(prio1);
			priorities.push_back(prio2);

			// set ecn_unit for token queue (1 by default)
			Settings::ecn_unit[Settings::MIBO_ACK] = 0;		// don't use ecn for ack and token_ack queue
			Settings::ecn_unit[Settings::MIBO_TOKEN] = 84/(double)(Settings::MTU)/mibo_tkn_ecn;		// smaller for token queue
			Settings::ecn_unit[Settings::MIBO_DATA] = 1;

			Settings::check_shared_buffer = true;
			Settings::piggy_qid = Settings::MIBO_TOKEN;
			break;
		}
		case Settings::CC_EP: {

			double ratio = (84/(double)(Settings::MTU + 84));
			std::cout << "ep: token rate limiter on switch queue - " << ratio << std::endl;
			// priority & PFC queues

			// ctrl queue:
			std::vector<uint32_t> prio0;
			prio0.push_back(0);
			rate_limiters.push_back(0);
			pfc_ctrls.push_back(false);
			pfc_queue_ctrls.push_back(false);

			// token queue:
			std::vector<uint32_t> prio1;
			prio1.push_back(Settings::EP_CREDIT);
			rate_limiters.push_back(ratio);
			pfc_ctrls.push_back(false);
			pfc_queue_ctrls.push_back(false);

			// others:
			std::vector<uint32_t> prio2;
			for (uint32_t i = Settings::EP_CREDIT+1; i < Settings::QUEUENUM; ++i){
				prio2.push_back(i);
				pfc_ctrls.push_back(false);
				pfc_queue_ctrls.push_back(false);
				rate_limiters.push_back(0);
			}

			priorities.push_back(prio0);
			priorities.push_back(prio1);
			priorities.push_back(prio2);

			Settings::max_qlen[Settings::EP_CREDIT] = 84*ep_credit_qlen;
			Settings::piggy_qid = Settings::EP_CREDIT;
			Settings::check_shared_buffer = true;
			break;
		}
		case Settings::CC_DCQCN:
		case Settings::CC_DCTCP:
		case Settings::CC_HPCC:
		case Settings::CC_TIMELY:
		case Settings::CC_SWIFT:{
			if (Settings::FC_VOQ_DSTPFC == Settings::fc_mode){
				// 3 priorities (ctrl, small part, large part & VOQs), no rate limiter, q0 is not PFC queue
				// priority 0
				std::vector<uint32_t> prio0;
				prio0.push_back(0);
				pfc_ctrls.push_back(false);
				pfc_queue_ctrls.push_back(false);
				priorities.push_back(prio0);
				// priority 1
				std::vector<uint32_t> prio1;
				prio1.push_back(1);
				pfc_ctrls.push_back(false);
				pfc_queue_ctrls.push_back(false);
				priorities.push_back(prio1);
				// priority 2
				std::vector<uint32_t> prio2;
				for (uint32_t i = 2; i < Settings::QUEUENUM; ++i){
					prio2.push_back(i);
					pfc_ctrls.push_back(default_pfc);
					pfc_queue_ctrls.push_back(default_queue_pfc);
				}
				priorities.push_back(prio2);
				// rate limiter
				for (uint32_t i = 0; i < Settings::QUEUENUM; ++i){
					rate_limiters.push_back(0);
				}
				// ecn
				Settings::ecn_unit[2] = 1;		// only large part queue mark ECN
			}else{
				// default: 2 priorities(0, others), no rate limiter, q0 is not PFC queue
				// priority & PFC queues
				std::vector<uint32_t> prio0;
				prio0.push_back(0);
				pfc_ctrls.push_back(false);
				pfc_queue_ctrls.push_back(false);
				std::vector<uint32_t> prio1;
				for (uint32_t i = 1; i < Settings::QUEUENUM; ++i){
					prio1.push_back(i);
					pfc_ctrls.push_back(default_pfc);
					pfc_queue_ctrls.push_back(default_queue_pfc);
				}
				priorities.push_back(prio0);
				priorities.push_back(prio1);
				// rate limiter
				for (uint32_t i = 0; i < Settings::QUEUENUM; ++i){
					rate_limiters.push_back(0);
				}

				Settings::ecn_unit[Settings::DEFAULT_DATA] = 1;
			}
			break;
		}
		default:
			std::cerr << "un-supported cc mode " << Settings::cc_mode << std::endl;
			return -1;
		}

		if (Settings::cc_mode == Settings::CC_MIBO_NSDI22){
			Settings::symmetic_routing_mode = Settings::SYMMETRIC_ON;
			Settings::msg_scheduler = Settings::MSG_RR;
			Settings::ecmp_unit = Settings::ECMP_QP;

			if (Settings::piggy_ingress_ecn){
				assert(Settings::host_cnp != Settings::ADDITION_CNP);
				std::cout << "use piggy_ingress_ecn --> reset host_cnp == false !!!" << std::endl;
			}

			assert(nic_token_mode == RdmaEgressQueue::NICTokenRateBased && l4_token_mode != RdmaEgressQueue::L4TokenNo);
			assert(l4_token_mode != RdmaEgressQueue::L4TokenDstDataDriven || max_unsch > 0);		// max_unsch must larger than 0 under data-driven generation

			if (limit_tkn_mode == Settings::LIMIT_TKN_EXCLUDE_UNSCH)
				assert(max_unsch > 0);
		}

		if (Settings::cc_mode == Settings::CC_EP){
			Settings::symmetic_routing_mode = Settings::SYMMETRIC_ON;
			nic_token_mode = RdmaEgressQueue::NICTokenRateBased;
			l4_token_mode = RdmaEgressQueue::L4TokenDstRateBased;
			token_cc = Settings::CC_EP;
			if (max_unsch != 0){
				max_unsch = 0;
				std::cout << "CC_EP: reset max_unsch = 0 !!!" << std::endl;
			}
		}

		if (Settings::CC_SWIFT == Settings::cc_mode || token_cc == Settings::CC_SWIFT){
			var_win = false;
			has_win = true;
		}

		/*----------------------------------supplementary default configure (end)----------------------------------------*/

		/*----------------------------------check settings conflict (start)----------------------------------------------*/

		// statistic output
		if (Settings::is_out_win){
			char s_file[200];
			sprintf(s_file,"%s-win.log", filename_keyword.c_str());
			Settings::win_out.open(s_file);
		}

		// if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
		// 	Settings::routing_mode = Settings::ROUTING_GLOBAL;	// only support ecmp
		// }

		if (Settings::piggy_ingress_ecn){
			assert(Settings::symmetic_routing_mode == Settings::SYMMETRIC_ON);
			assert(Settings::piggy_qid != -1);
		}

		if (token_cc == Settings::CC_EP || Settings::cc_mode == Settings::CC_EP){
			assert(!Settings::IsPacketLevelRouting());		// cannot have dis-ordered packets
			assert(has_win == 0 && var_win == 0);			// no window
			assert(limit_tkn_mode == Settings::NO_LIMIT_TKN);
		}

		if (resume_bdp_tkn){
			assert(RdmaEgressQueue::L4TokenDstDataDriven == l4_token_mode);
		}

		if (l4_token_mode == RdmaEgressQueue::L4TokenDstDataDriven){
			assert(max_unsch > 0);	// must has unsch phase
		}

		if (l4_token_mode != RdmaEgressQueue::L4TokenNo)	// if use token, nic should support
			assert(nic_token_mode == RdmaEgressQueue::NICTokenRateBased);

//		/**
//		 * When L4 use DCTCP
//		 * --> Sequences of packets are used to identify different RTTs,
//		 * --> so it cannot solve unordered packets.
//		 * --> Reset routing mode if necessary.
//		 */
//		if (Settings::cc_mode == Settings::CC_DCTCP){
//			if (Settings::IsPacketLevelRouting()){
//				Settings::routing_mode = Settings::ROUTING_GLOBAL;
//				std::cout << "DCTCP cannot solve unordered packets --> Reset routing_mode as ROUTING_GLOBAL" << std::endl;
//			}
//		}

		/**
		 * When allow dis-order
		 * --> to avoid unnecessary retransmission, turn off nack_reaction.
		 */
		if (Settings::AllowDisOrder()){
			assert(!Settings::nack_reaction);
			std::cout << "to avoid unnecessary retransmission, should turn off nack_reaction." << std::endl;
		}

		if (Settings::fc_mode == Settings::FC_FLOODGATE){

			char s_file[200];
			sprintf(s_file,"%s-switchwin.log", filename_keyword.c_str());
			SwitchMmu::win_out.open(s_file);

			Settings::reset_isolate_down_host = Settings::RESET_ISOLATE_DIFFGROUP;
			std::cout << "Settings::reset_isolate_down_host = Settings::RESET_ISOLATE_DIFFGROUP !!! " << std::endl;
			if (Settings::switch_ack_mode == Settings::HOST_PER_PACKET_ACK){
				assert(1 == l2_ack_interval);	// must support per-packet-ack
				if (!Settings::reset_only_ToR_switch_win
						&& Settings::symmetic_routing_mode != Settings::SYMMETRIC_ON){
					Settings::symmetic_routing_mode = Settings::SYMMETRIC_ON;
					std::cout << "When used switchwin and didn't reset to use only_ToR_switch_win, symmetic_routing_mode must include SYMMETRIC_RECEIVER!!! RESET IT!!!" << std::endl;
				}
			}else {
				if (Settings::switch_byte_counter == 0){
					Settings::switch_byte_counter = 1;
					std::cout << "switch_byte_counter cannot be 0, reset 1 !!!" << std::endl;
				}
			}

			if (Settings::use_port_win){
				// when use port_win, must has per-hop switchACK
				assert(!Settings::reset_only_ToR_switch_win && Settings::switch_ack_mode != Settings::HOST_PER_PACKET_ACK);
			}

			// absolute_psn can only work with switches which would send SwitchACK
			if (Settings::switch_absolute_psn)
				assert(Settings::switch_ack_mode != Settings::HOST_PER_PACKET_ACK);
			// syn_timeout must work with absolute psn
			if (Settings::switch_syn_timeout_us)
				assert(Settings::switch_absolute_psn > 0);

		}else{
			Settings::reset_only_ToR_switch_win = false;
			Settings::use_port_win = false;
			std::cout << "When did't use switchwin, reset switch_voq_ecn_mode = 0, only_ToR_switch_win = false" << std::endl;
		}

		if (Settings::fc_mode == Settings::FC_BFC){
			ack_high_prio = 1;	// on NICs, ACK packets have the highest priority
			assert(Settings::CC_EP != Settings::cc_mode && Settings::CC_MIBO_NSDI22 != Settings::cc_mode);
		}

		if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
			// can only work with reactive CC
			assert(Settings::CC_EP != Settings::cc_mode && Settings::CC_MIBO_NSDI22 != Settings::cc_mode);
		}

		if (!use_dynamic_pfc_threshold){
			assert(Settings::pfc_th_static > 1);
			std::cout << "PFC static threshold only turn on when port uses DST-PFC for now!!!" << std::endl;
		}
		/*----------------------------------check settings conflict (end)----------------------------------------------*/

	}
	else
	{
		std::cout << "Error: require a config file\n";
		fflush(stdout);
		return 1;
	}

	bool dynamicth = use_dynamic_pfc_threshold;

	Config::SetDefault("ns3::QbbNetDevice::PauseTime", UintegerValue(pause_time));
	Config::SetDefault("ns3::QbbNetDevice::QcnEnabled", BooleanValue(enable_qcn));
	Config::SetDefault("ns3::QbbNetDevice::DynamicThreshold", BooleanValue(dynamicth));

	//SeedManager::SetSeed(time(NULL));

	/*-----------------------------build topology--------------------------------------*/
	std::ifstream topof, flowf, tracef;
	topof.open(topology_file.c_str());
	if (!topof.is_open()){
		std::cerr << "cannot open topology file" << std::endl;
		return 0;
	}
	uint32_t node_num, switch_num, link_num, trace_num;
	topof >> node_num >> switch_num >> host_per_rack >> tor_num >> core_num >> link_num;

	Settings::host_num = node_num - switch_num;
	Settings::host_per_rack = host_per_rack;
	Settings::switch_num = switch_num;
	Settings::tor_num = tor_num;
	Settings::core_num = core_num;
	std::cout<<Settings::host_num<<" "<<Settings::host_per_rack<<" "<<Settings::switch_num<<" "<<Settings::tor_num<<" "<<Settings::core_num<<std::endl;
	if (output_realtime){
		char s_file[200];
		for(uint32_t i = 0; i< switch_num; i++){
			sprintf(s_file,"%s-buffer%d.log",filename_keyword.c_str(), i);
			Settings::switch_buffer_out[i].open(s_file);
			sprintf(s_file,"%s-bw%d.log",filename_keyword.c_str(), i);
			Settings::switch_bw_out[i].open(s_file);
		}
	}

	std::vector<uint32_t> v;
	std::vector<std::vector<uint32_t> > nextBDPHop;
	std::vector<std::vector<uint32_t> > bws;
	for(uint32_t i=0;i<node_num;++i)
		v.push_back(0x3fffffff);
	for(uint32_t i=0;i<node_num;++i) {
		pd.push_back(v);
		td.push_back(v);
		bws.push_back(v);
		nextBDPHop.push_back(v);
		pd[i][i] = td[i][i] = bws[i][i] = nextBDPHop[i][i] = 0;
	}

	std::vector<uint32_t> node_type(node_num, 0);
	std::map<uint32_t, std::set<uint32_t> > switch_down_hosts;
	for (uint32_t i = 0; i < switch_num; i++)
	{
		uint32_t sid;
		topof >> sid;
		node_type[sid] = 1;

		std::set<uint32_t> curr;
		switch_down_hosts.insert(std::make_pair(sid, curr));
	}

	for (uint32_t i = 0; i < node_num; i++){
		if (node_type[i] == 0)//server
			n.Add(CreateObject<Node>());
		else{//switch
			Ptr<SwitchNode> sw = CreateObject<SwitchNode>();
			std::cout << "initial switch " << i << std::endl;
			n.Add(sw);
			sw->SetAttribute("EcnEnabled", BooleanValue(enable_qcn));
			// wqy: for statistic
			sw->SetAttribute("OutputRealTimeBuffer", BooleanValue(output_realtime));
			sw->TraceConnectWithoutContext("RealtimeQueueLength", MakeCallback(&realtime_buffer));
			sw->TraceConnectWithoutContext("RealtimeSwitchBw", MakeCallback(&realtime_switch_bw));
		}
	}


	NS_LOG_INFO("Create nodes.");

	InternetStackHelper internet;
	internet.Install(n);

	//
	// Assign IP to each server
	//
	std::vector<Ipv4Address> serverAddress;
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 0){ // is server
			serverAddress.resize(i + 1);
			serverAddress[i] = Ipv4Address(0x0b000001 + ((i / 256) * 0x00010000) + ((i % 256) * 0x00000100));
		}
	}

	NS_LOG_INFO("Create channels.");

	//
	// Explicitly create the channels required by the topology.
	//

	Ptr<RateErrorModel> rem = CreateObject<RateErrorModel>();
	Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
	rem->SetRandomVariable(uv);
	uv->SetStream(50);
	rem->SetAttribute("ErrorRate", DoubleValue(error_rate_per_link));
	rem->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));

	FILE *pfc_file = fopen(pfc_output_file.c_str(), "w");
	FILE *ci_file = nullptr;
	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION)
		ci_file = fopen(ci_output_file.c_str(), "w");

	double max_bw_GBps = 0;

	QbbHelper qbb;
	Ipv4AddressHelper ipv4;
	for (uint32_t i = 0; i < link_num; i++)
	{
		uint32_t src, dst;
		std::string data_rate, link_delay;
		double error_rate;
		//topof >> src >> dst >> data_rate >> link_delay >> error_rate;
		topof >> src >> dst >> data_rate >> link_delay;

		nextBDPHop[src][dst] = dst;
		nextBDPHop[dst][src] = src;
		bws[src][dst] = bws[dst][src] = (uint32_t)std::atoi(data_rate.c_str());
		if (bws[src][dst]/8.0 > max_bw_GBps)
			max_bw_GBps = bws[src][dst]/8.0;
		pd[src][dst] = pd[dst][src] = (uint32_t)std::atoi(link_delay.c_str());
		td[src][dst] = td[dst][src] = Settings::MTU*8/(uint32_t)std::atoi(data_rate.c_str());
		error_rate = error_rate_per_link;

		std::cout<<src <<" "<<dst<<" "<<data_rate<<" "<<link_delay<<" "<<error_rate<<std::endl;
		minHRTT = std::min((uint64_t)pd[dst][src]*2, minHRTT);
		maxHRTT = std::max((uint64_t)pd[dst][src]*2, maxHRTT);
		Ptr<Node> snode = n.Get(src), dnode = n.Get(dst);

		assert(dst > src);
		if (snode->GetNodeType() == 0){	// host-switch
			assert(dst >= Settings::host_num);
			if (switch_down_hosts[dst].count(src) < 1)
				switch_down_hosts[dst].insert(src);
		}else{ // switch-switch
			std::set<uint32_t> src_hosts = switch_down_hosts[src];
			std::set<uint32_t>::iterator it = src_hosts.begin();
			while (it != src_hosts.end()){ // add hosts under src-switch into dst-switch
				if (switch_down_hosts[dst].count(*it) < 1)
					switch_down_hosts[dst].insert(*it);
				it++;
			}
		}

		//qbb.SetDeviceAttribute("DataRate", StringValue(data_rate));
		//qbb.SetChannelAttribute("Delay", StringValue(link_delay));

		qbb.SetDeviceAttribute("DataRate", StringValue(data_rate + "Gbps"));
		qbb.SetChannelAttribute("Delay", StringValue(link_delay+"ns"));

		if (error_rate > 0)
		{
			Ptr<RateErrorModel> rem = CreateObject<RateErrorModel>();
			Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
			rem->SetRandomVariable(uv);
			uv->SetStream(50);
			rem->SetAttribute("ErrorRate", DoubleValue(error_rate));
			rem->SetAttribute("ErrorUnit", StringValue("ERROR_UNIT_PACKET"));
			qbb.SetDeviceAttribute("ReceiveErrorModel", PointerValue(rem));
		}
		else
		{
			qbb.SetDeviceAttribute("ReceiveErrorModel", PointerValue(rem));
		}

		fflush(stdout);
		
		// Assign server IP
		// Note: this should be before the automatic assignment below (ipv4.Assign(d)),
		// because we want our IP to be the primary IP (first in the IP address list),
		// so that the global routing is based on our IP
		NetDeviceContainer d = qbb.Install(snode, dnode);
		if (snode->GetNodeType() == 0){
			Ptr<Ipv4> ipv4 = snode->GetObject<Ipv4>();
			ipv4->AddInterface(d.Get(0));
			ipv4->AddAddress(1, Ipv4InterfaceAddress(serverAddress[src], Ipv4Mask(0xff000000)));

			// set pfc_mode if necessary
			if (dnode->GetNodeType() > 0){	// src-srcToR, set per-dst pfc
				Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(dst));
				sw->m_mmu->pfc_ingress_ctrls = pfc_ctrls;
				DynamicCast<QbbNetDevice>(d.Get(0))->m_pfc_fine = pfc_dst_nic;
				if (Settings::srcToR_dst_pfc){
					sw->m_mmu->pfc_mode[sw->GetNDevices()-1] = Settings::PAUSE_DST;
				}
			}

			DynamicCast<QbbNetDevice>(d.Get(0))->m_rdmaEQ->m_tokenMode = nic_token_mode;
			DynamicCast<QbbNetDevice>(d.Get(0))->m_rdmaEQ->m_l4TokenMode = l4_token_mode;
			DynamicCast<QbbNetDevice>(d.Get(0))->m_rdmaEQ->m_rate_limiter_mode = rate_limiter_mode;

			// switch queue configure
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->SetPFCCtrl(pfc_queue_ctrls);
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->SetPriority(priorities);
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->SetRateLimiter(rate_limiters, DataRate(data_rate + "Gbps"));
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->m_rate_limiter_mode = rate_limiter_mode;
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->m_isLastHop = true;
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->use_hier_q = Settings::hier_Q;
		}else if(dnode->GetNodeType() > 0){		// switch-switch
			// switch queue configure
			Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(src));
			sw->m_mmu->pfc_ingress_ctrls = pfc_ctrls;
//			if (Settings::srcToR_dst_pfc) sw->m_mmu->pfc_mode[sw->GetNDevices()-1] = Settings::PAUSE_OFF;
			sw = DynamicCast<SwitchNode>(n.Get(dst));
			sw->m_mmu->pfc_ingress_ctrls = pfc_ctrls;
//			if (Settings::srcToR_dst_pfc) sw->m_mmu->pfc_mode[sw->GetNDevices()-1] = Settings::PAUSE_OFF;

			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->SetPFCCtrl(pfc_queue_ctrls);
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->SetPriority(priorities);
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->SetRateLimiter(rate_limiters, DataRate(data_rate + "Gbps"));
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->m_rate_limiter_mode = rate_limiter_mode;
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->use_hier_q = Settings::hier_Q;
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->SetPFCCtrl(pfc_queue_ctrls);
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->SetPriority(priorities);
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->SetRateLimiter(rate_limiters, DataRate(data_rate + "Gbps"));
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->m_rate_limiter_mode= rate_limiter_mode;
			DynamicCast<QbbNetDevice>(d.Get(1))->m_queue->use_hier_q = Settings::hier_Q;
		}

		if (dnode->GetNodeType() == 0){
			Ptr<Ipv4> ipv4 = dnode->GetObject<Ipv4>();
			ipv4->AddInterface(d.Get(1));
			ipv4->AddAddress(1, Ipv4InterfaceAddress(serverAddress[dst], Ipv4Mask(0xff000000)));
			// set pfc_mode if necessary
			if (snode->GetNodeType() > 0){	// srcToR-src, set per-dst pfc
				Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(src));
				sw->m_mmu->pfc_ingress_ctrls = pfc_ctrls;
				DynamicCast<QbbNetDevice>(d.Get(0))->m_pfc_fine = pfc_dst_nic;
				if (Settings::srcToR_dst_pfc){
					sw->m_mmu->pfc_mode[sw->GetNDevices()-1] = Settings::PAUSE_DST;
				}
			}
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->SetPFCCtrl(pfc_queue_ctrls);
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->SetPriority(priorities);
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->SetRateLimiter(rate_limiters, DataRate(data_rate + "Gbps"));
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->m_rate_limiter_mode = rate_limiter_mode;
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->m_isLastHop = true;
			DynamicCast<QbbNetDevice>(d.Get(0))->m_queue->use_hier_q = Settings::hier_Q;
		}

		// used to create a graph of the topology
		nbr2if[snode][dnode].idx = DynamicCast<QbbNetDevice>(d.Get(0))->GetIfIndex();
		nbr2if[snode][dnode].up = true;
		nbr2if[snode][dnode].delay = DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(0))->GetChannel())->GetDelay().GetTimeStep();
		nbr2if[snode][dnode].bw = DynamicCast<QbbNetDevice>(d.Get(0))->GetDataRate().GetBitRate();
		nbr2if[dnode][snode].idx = DynamicCast<QbbNetDevice>(d.Get(1))->GetIfIndex();
		nbr2if[dnode][snode].up = true;
		nbr2if[dnode][snode].delay = DynamicCast<QbbChannel>(DynamicCast<QbbNetDevice>(d.Get(1))->GetChannel())->GetDelay().GetTimeStep();
		nbr2if[dnode][snode].bw = DynamicCast<QbbNetDevice>(d.Get(1))->GetDataRate().GetBitRate();

		// initial table for helping FC/CC
		Settings::link_node[snode->GetId()][DynamicCast<QbbNetDevice>(d.Get(0))->GetIfIndex()] = dnode->GetId();
		Settings::link_node[dnode->GetId()][DynamicCast<QbbNetDevice>(d.Get(1))->GetIfIndex()] = snode->GetId();
		if (snode->m_id >= Settings::host_num){	// if it's an interface on switch
			if (snode->m_id < dnode->m_id){	// link to higher level switch
				Settings::up_port[snode->m_id-Settings::host_num][DynamicCast<QbbNetDevice>(d.Get(0))->GetIfIndex()] = true;
			}else{ // link to lower level switch
				Settings::up_port[snode->m_id-Settings::host_num][DynamicCast<QbbNetDevice>(d.Get(0))->GetIfIndex()] = false;
			}
		}
		if (dnode->m_id >= Settings::host_num){ // if it's an interface on switch
			if (snode->m_id > dnode->m_id){ // link to higher level switch
				Settings::up_port[dnode->m_id-Settings::host_num][DynamicCast<QbbNetDevice>(d.Get(1))->GetIfIndex()] = true;
			}else{ // link to lower level switch
				Settings::up_port[dnode->m_id-Settings::host_num][DynamicCast<QbbNetDevice>(d.Get(1))->GetIfIndex()] = false;
			}
		}

		// This is just to set up the connectivity between nodes. The IP addresses are useless
		char ipstring[16];
		sprintf(ipstring, "10.%d.%d.0", i / 254 + 1, i % 254 + 1);
		ipv4.SetBase(ipstring, "255.255.255.0");
		ipv4.Assign(d);

		// setup PFC trace
		DynamicCast<QbbNetDevice>(d.Get(0))->TraceConnectWithoutContext("QbbPfc", MakeBoundCallback (&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(0))));
		DynamicCast<QbbNetDevice>(d.Get(1))->TraceConnectWithoutContext("QbbPfc", MakeBoundCallback (&get_pfc, pfc_file, DynamicCast<QbbNetDevice>(d.Get(1))));
		// setup CI trace
		if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
			DynamicCast<QbbNetDevice>(d.Get(0))->TraceConnectWithoutContext("CISignal", MakeBoundCallback (&get_ci, ci_file, DynamicCast<QbbNetDevice>(d.Get(0))));
			DynamicCast<QbbNetDevice>(d.Get(1))->TraceConnectWithoutContext("CISignal", MakeBoundCallback (&get_ci, ci_file, DynamicCast<QbbNetDevice>(d.Get(1))));
		}

	}

	nic_rate = get_nic_rate(n); //nic rate of hosts
	Settings::host_bw_Bps = nic_rate / 8;

	Ipv4Address empty_ip;
	for(uint32_t i = 0;i < Settings::host_num; ++i){
		if(serverAddress[i].IsEqual(empty_ip)) NS_FATAL_ERROR("An end-host belongs to no link");
		Settings::hostId2IpMap[i] = serverAddress[i].Get();
		Settings::hostIp2IdMap[serverAddress[i].Get()] = i;
	}

	// Floyd
	Floyd(pd, nextBDPHop);
	Floyd(td, nextBDPHop);

	/*-------------------------Do preparation for FC--------------------------------*/
	if (Settings::fc_mode == Settings::FC_FLOODGATE){
		// calculate switch-credit payload
		if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
			uint32_t interval_count_max = std::ceil((max_bw_GBps * Settings::switch_credit_interval * 1e3)/Settings::MTU) * Settings::MTU;
			SwitchACKTag::switch_ack_credit_bit = std::min(interval_count_max, Settings::switch_byte_counter);
			int bit = 0;
			while (SwitchACKTag::switch_ack_credit_bit > 0){
				SwitchACKTag::switch_ack_credit_bit /= 2;
				++bit;
			}
			SwitchACKTag::switch_ack_credit_bit = bit;
			if (Settings::reset_only_ToR_switch_win){
				SwitchACKTag::switch_ack_payload = SwitchACKTag::switch_ack_credit_bit * Settings::host_per_rack;
				SwitchACKTag::switch_ack_id_bit = Settings::host_per_rack;
			} else{
				SwitchACKTag::switch_ack_payload = SwitchACKTag::switch_ack_credit_bit * Settings::host_num;
				SwitchACKTag::switch_ack_id_bit = Settings::host_num;
			}

			bit = 0;
			while (SwitchACKTag::switch_ack_id_bit > 0){
				SwitchACKTag::switch_ack_id_bit /= 2;
				++bit;
			}
			SwitchACKTag::switch_ack_id_bit = bit;
			std::cout << "switch-credit max payload:" << SwitchACKTag::switch_ack_payload << " max credit #bit:" << SwitchACKTag::switch_ack_credit_bit << std::endl;
		}

		// calculate BDP map
		std::cout << "switch-win:";
		for (uint32_t i = Settings::host_num; i < node_num; ++i){ // for each switch
			std::cout << i << ":";
			for (uint32_t j = 0; j < Settings::host_num + Settings::host_num/Settings::host_per_rack; ++j){ // for each dst and ToR
				if (i == j) continue;
				double rtt = GetDefaultDelayUs(i, j, Settings::MTU) + GetDefaultDelayUs(i, j, 84);	// us
				uint32_t link = bws[i][nextBDPHop[i][j]];	// Gbps
				uint32_t hopBefore = nextBDPHop[i][j];
				while (0x3fffffff == link){
					link = bws[i][nextBDPHop[i][hopBefore]];
					hopBefore = nextBDPHop[i][hopBefore];
				}
				uint32_t bdp = ceil(rtt*link*1e3/8);
				if (!Settings::reset_only_ToR_switch_win && Settings::switch_ack_mode != Settings::HOST_PER_PACKET_ACK)
					bdp = ceil(link*2*pd[i][hopBefore]/8);
				uint32_t win = Settings::switch_win_m * bdp;
				if (Settings::switch_byte_counter > 1)
					win += ceil(Settings::switch_credit_interval*link*1e3/8);
				win = (win/(double)Settings::MTU)*Settings::MTU;
				cout << j << "-" << bdp << "-" << win << "-" << rtt << "-" << link << "-" << hopBefore << ",";
				VOQ::BDPMAP.insert(std::make_pair(std::make_pair(i, j), win));
			}
			std::cout << "\n";
		}
	}

	/*
	 * read switchwin configure
	 */
	uint32_t default_voq_limit, default_dynamic;
	std::set<uint32_t> use_win_switches;
	// <switch_id, vector<voq_limit, is_dynamic_hash> >
	std::map<uint32_t, std::vector<std::pair<uint32_t, bool> > > switch_voq_groups;
	// <switch_id, vector<dst_id, group_id> >
	std::map<uint32_t, std::vector<std::pair<uint32_t, uint32_t> > > switch_dst_rules;

	std::ifstream switchwin_config_f;
	switchwin_config_f.open(switchwin_config_file.c_str());
	if(Settings::fc_mode != Settings::FC_FLOODGATE || !switchwin_config_f.is_open()){
		std::cout <<"Didn't set use_switch_win or Cannot open switchWin config file " << switchwin_config_file
				<< " --> No SwitchWin on all switches " << std::endl;
	}else{
		std::cout << "Read switchwin config file. " << std::endl;
		uint32_t use_switchwin_num, config_switch_num;
		switchwin_config_f >> use_switchwin_num >> config_switch_num >> default_voq_limit >> default_dynamic;

		if (default_dynamic == 1 && default_voq_limit == 0){
			std::cerr << "when use dynamic hash, must set voq limitation!" << std::endl;
			return 1;
		}

		std::cout << use_switchwin_num << "\t" << config_switch_num << "\t" << default_voq_limit << "\t" <<  default_dynamic << std::endl;
		for (uint32_t i = 0; i < use_switchwin_num; ++i){
			uint32_t switch_id;
			switchwin_config_f >> switch_id;
			if (use_win_switches.count(switch_id) < 1)
				use_win_switches.insert(switch_id);
		}

		for (uint32_t i = 0; i < config_switch_num; ++i){
			uint32_t switch_id, group_num, rule_num;
			switchwin_config_f >> switch_id >> group_num >> rule_num;

			assert(switch_voq_groups.find(switch_id) == switch_voq_groups.end());
			assert(switch_dst_rules.find(switch_id) == switch_dst_rules.end());
			std::vector<std::pair<uint32_t, bool> > curr_groups;
			std::vector<std::pair<uint32_t, uint32_t> > curr_rules;

			for (uint32_t j = 0; j < group_num; ++j){
				uint32_t voq_limit, dynamic_hash;
				switchwin_config_f >> voq_limit >> dynamic_hash;
				curr_groups.push_back(std::make_pair(voq_limit, dynamic_hash==1));
				std::cout << switch_id << " add group: " << j << " " << voq_limit << " " <<  (dynamic_hash==1) << std::endl;
				if (dynamic_hash == 1 && voq_limit == 0){
					std::cerr << "when use dynamic hash, must set voq limitation!" << std::endl;
					return 1;
				}
			}
			for (uint32_t j = 0; j < rule_num; ++j){
				uint32_t dst_id, group_id;
				switchwin_config_f >> dst_id >> group_id;
				curr_rules.push_back(std::make_pair(dst_id, group_id));
				std::cout << switch_id << " add rule: " << dst_id << " " <<  group_id << std::endl;
			}

			switch_voq_groups[switch_id] = curr_groups;
			switch_dst_rules[switch_id] = curr_rules;
		}
	}

	/*-------------------------------------config switch--------------------------------------*/
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 1){ // is switch
			Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));

			// tag ToR switches
			if (i >= Settings::host_num + switch_num - core_num) {
				sw->m_isCore = true;
				std::cout << "set Core: " << sw->m_id << " " << std::endl;
			}else if (i < Settings::host_num + tor_num) {
				sw->m_isToR = true;
				std::cout << "set ToR: " << sw->m_id << " " << std::endl;
			}

			sw->m_mmu->netdevice_num = sw->GetNDevices();
			sw->CalcuUpPortsNum();

			uint32_t shift = 2; // by default 1/8

			for (uint32_t j = 1; j < sw->GetNDevices(); j++){
				Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(sw->GetDevice(j));
				sw->m_mmu->m_bw_map[j] = dev->GetDataRate().GetBitRate();
				sw->m_mmu->m_delay_map[j] = DynamicCast<QbbChannel>(dev->GetChannel())->GetDelay().GetSeconds();
				// set ecn
				uint64_t rate = dev->GetDataRate().GetBitRate();
				NS_ASSERT_MSG(rate2kmin.find(rate) != rate2kmin.end(), "must set kmin for each link speed");
				NS_ASSERT_MSG(rate2kmax.find(rate) != rate2kmax.end(), "must set kmax for each link speed");
				NS_ASSERT_MSG(rate2pmax.find(rate) != rate2pmax.end(), "must set pmax for each link speed");
				sw->m_mmu->ConfigEcn(j, rate2kmin[rate], rate2kmax[rate], rate2pmax[rate]);

				// set dynamic pfc
				uint64_t delay = DynamicCast<QbbChannel>(dev->GetChannel())->GetDelay().GetTimeStep();
				uint32_t bdp = rate * delay / 8 / 1000000000 * Settings::hdrm_ratio;
				sw->m_mmu->ConfigHdrm(j, bdp*4);
				// set pfc alpha, proportional to link bw
				sw->m_mmu->pfc_a_shift[j] = shift;
				if (sw->m_mmu->pfc_mode[j] == Settings::PAUSE_DST) {
					sw->m_mmu->pfc_a_shift[j] = Settings::pfc_dst_alpha;
				}
				while (rate > nic_rate && sw->m_mmu->pfc_a_shift[j] > 0){
					sw->m_mmu->pfc_a_shift[j]--;
					rate /= 2;
				}
				// set static pfc (only turn on when work with DST-PFC for now)
				if (!dynamicth && sw->m_mmu->pfc_mode[j] == Settings::PAUSE_DST){
					sw->m_mmu->pfc_th_static[j] = bdp * Settings::pfc_th_static;
				}
			}
			sw->m_mmu->ConfigNPort(sw->GetNDevices()-1);
			sw->m_mmu->ConfigBufferSize(buffer_size* 1024 * 1024);

			// print PFC threshold
			std::cout << "PFC th " << sw->GetId() << ":";
			for (uint32_t j = 1; j < sw->GetNDevices(); j++){
				cout << j << "-" << int(sw->m_mmu->pfc_mode[j]) << "-" << sw->m_mmu->GetPfcThreshold(j) << ", ";
			}
			cout << "\n #port: " << sw->GetNDevices()-1
					<< " buffer size: " << sw->m_mmu->buffer_size
					<< " headroom: " << sw->m_mmu->total_hdrm
					<< " reserve: " << sw->m_mmu->total_rsrv << endl;

			sw->m_mmu->node_id = sw->GetId();

			/*
			 * config switch for FC
			 */
			if (Settings::fc_mode == Settings::FC_FLOODGATE){
				if (use_win_switches.count(sw->m_id) <= 0 ||
						(Settings::reset_only_ToR_switch_win && !sw->m_isToR))
					continue;

				sw->m_mmu->m_use_switchwin = true;
				std::cout << "switch " << sw->m_id << " use switchwin, ";

				uint64_t max_rate = 0, max_delay = 0;
				cout << "set per-port-win " << Settings::use_port_win << " :\n";
				for (uint32_t j = 1; j < sw->GetNDevices(); j++){
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(sw->GetDevice(j));
					uint64_t rate = dev->GetDataRate().GetBitRate();
					uint64_t delay = DynamicCast<QbbChannel>(dev->GetChannel())->GetDelay().GetTimeStep();
					max_rate = std::max(rate, max_rate);
					max_delay = std::max(delay, max_delay);

					// set per-port window
					uint32_t win = Settings::switch_port_win_m * rate * 2 * delay / 8 / 1e9;
					if (Settings::switch_byte_counter > 1)
						win += ceil(Settings::switch_credit_interval * rate / 1e6 / 8);
					win = (win/(double)Settings::MTU)*Settings::MTU;
					std::cout << j << "-" << win << ",";
					sw->m_mmu->m_egress_wins[j] = win;
				}
				std::cout << "\n";

				// set ack-th
				sw->m_mmu->m_th_ack = max_rate * max_delay * 2 * Settings::switch_ack_th_m / 8 / 1e9;
				cout << "ack-th: " << sw->m_mmu->m_th_ack << endl;

				uint32_t group_num = 0;
				if (switch_voq_groups.find(sw->m_id) == switch_voq_groups.end()){
					std::cout << " default config." << std::endl;
					sw->m_mmu->ConfigVOQGroup(group_num++, default_voq_limit, default_dynamic==1);
					std::cout << "create voq group: " << 0 << " " <<  default_voq_limit << " " <<  (default_dynamic==1) << std::endl;
					std::cout << "add rule: ";
					for (uint32_t j = 0; j < Settings::host_num; ++j){
						sw->m_mmu->ConfigDst(j, 0);
						std::cout << j << "-" << 0 << "\t";
					}
				}else{
					std::cout << " input config." << std::endl;
					std::vector<std::pair<uint32_t, bool> > groups = switch_voq_groups[sw->m_id];
					for (uint32_t j = 0; j < groups.size(); ++j){
						sw->m_mmu->ConfigVOQGroup(group_num++, groups[j].first, groups[j].second);
						std::cout << "create voq group: " << j << " " <<  groups[j].first << " " <<  groups[j].second << std::endl;
					}
					std::vector<std::pair<uint32_t, uint32_t> > rules = switch_dst_rules[sw->m_id];
					// todo: it's better to do sort here
					assert(rules.size() > 0);
					uint32_t index = 0;
					std::cout << "add rule: ";
					for (uint32_t j = 0; j < Settings::host_num; ++j){
						if (index < rules.size() && rules[index].first == j){
							sw->m_mmu->ConfigDst(j, rules[index].second);
							std::cout << j << "-" << rules[index].second << "\t";
							++index;
						}else{
							sw->m_mmu->ConfigDst(j, 0);
							std::cout << j << "-" << 0 << "\t";
						}
					}
				}
				std::cout << std::endl;

				/**
				 * It is unnecessary to block the host under this ToR.
				 * What's more, blocking the host under this ToR may cause dead-lock.
				 */
				if (!sw->m_isCore && Settings::reset_isolate_down_host != 0){
					std::cout << "reset ignore: ";

					uint32_t group_id = -1;			// dstToR will always ignore
					if (!sw->m_isToR && Settings::reset_isolate_down_host == Settings::RESET_ISOLATE_DIFFGROUP){	// dstAgg will be setted as config
						group_id = group_num;
						sw->m_mmu->ConfigVOQGroup(group_num++, default_voq_limit, default_dynamic==1);
					}

					std::set<uint32_t> curr_hosts = switch_down_hosts[sw->GetId()];
					std::set<uint32_t>::iterator it = curr_hosts.begin();
					while (it != curr_hosts.end()){
						sw->m_mmu->ConfigDst(*it, group_id);
						std::cout << *it << "\t";
						it++;
					}

					std::cout << std::endl;
				}
			}
		}
	}

	FILE *fct_output = fopen(fct_output_file.c_str(), "w");
	if(fct_output == NULL)//added, lkx
		NS_ASSERT_MSG(false, "error opening fct_output");

	// install RDMA driver
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 0){ // is server
			// create RdmaHw
			Ptr<RdmaHw> rdmaHw = CreateObject<RdmaHw>();
			rdmaHw->SetAttribute("ClampTargetRate", BooleanValue(clamp_target_rate));
			rdmaHw->SetAttribute("AlphaResumInterval", DoubleValue(alpha_resume_interval));
			rdmaHw->SetAttribute("RPTimer", DoubleValue(rp_timer));
			rdmaHw->SetAttribute("FastRecoveryTimes", UintegerValue(fast_recovery_times));
			rdmaHw->SetAttribute("EwmaGain", DoubleValue(ewma_gain));
			rdmaHw->SetAttribute("RateAI", DataRateValue(DataRate(rate_ai)));
			rdmaHw->SetAttribute("RateHAI", DataRateValue(DataRate(rate_hai)));
			rdmaHw->SetAttribute("L2BackToZero", BooleanValue(l2_back_to_zero));
			rdmaHw->SetAttribute("L2ChunkSize", UintegerValue(l2_chunk_size));
			rdmaHw->SetAttribute("L2AckInterval", UintegerValue(l2_ack_interval));
			rdmaHw->SetAttribute("CcMode", UintegerValue(Settings::cc_mode));
			rdmaHw->SetAttribute("RateDecreaseInterval", DoubleValue(rate_decrease_interval));
			rdmaHw->SetAttribute("MinRate", DataRateValue(DataRate(min_rate)));
			rdmaHw->SetAttribute("Mtu", UintegerValue(packet_payload_size));
			rdmaHw->SetAttribute("MiThresh", UintegerValue(mi_thresh));
			rdmaHw->SetAttribute("VarWin", BooleanValue(var_win));
			rdmaHw->SetAttribute("FastReact", BooleanValue(fast_react));
			rdmaHw->SetAttribute("MultiRate", BooleanValue(multi_rate));
			rdmaHw->SetAttribute("SampleFeedback", BooleanValue(sample_feedback));
			rdmaHw->SetAttribute("TargetUtil", DoubleValue(u_target));
			rdmaHw->SetAttribute("RateBound", BooleanValue(rate_bound));
			rdmaHw->SetAttribute("DctcpRateAI", DataRateValue(DataRate(dctcp_rate_ai)));
			rdmaHw->SetAttribute("LimitTknMode", UintegerValue(limit_tkn_mode));
			rdmaHw->SetAttribute("TurnSchPhaseMode", UintegerValue(turn_sch_phase_mode));
			rdmaHw->SetAttribute("ResumeBDPTkn", BooleanValue(resume_bdp_tkn));
			rdmaHw->SetAttribute("MiboNsdiTknCC", UintegerValue(token_cc));
			rdmaHw->SetAttribute("MiboNsdiCreditEmin", UintegerValue(credit_Emin));
			rdmaHw->SetAttribute("MiboNsdiCreditEmax", UintegerValue(credit_Emax));
			rdmaHw->SetAttribute("MiboNsdiCreditEp", DoubleValue(credit_Ep));
			rdmaHw->SetAttribute("MaxUnsch", UintegerValue(max_unsch));
			rdmaHw->SetAttribute("L4TokenMode", UintegerValue(l4_token_mode));
			rdmaHw->SetAttribute("MIBOccMode", UintegerValue(mibo_cc_mode));
			// swift
			rdmaHw->SetAttribute("MinCwndScale", DoubleValue(swift_fs_min_cwnd));
			rdmaHw->SetAttribute("MinCwnd", DoubleValue(swift_min_cwnd));
			rdmaHw->SetAttribute("AICwnd", DoubleValue(swift_ai));
			rdmaHw->SetAttribute("MDCwnd", DoubleValue(swift_md));
			rdmaHw->SetAttribute("MaxMDF", DoubleValue(swift_max_mdf));
			rdmaHw->SetAttribute("AlphaEWMA", DoubleValue(swift_ewma_alpha));
			rdmaHw->SetAttribute("RetxResetTh", UintegerValue(swift_retx_reset_threshold));
			rdmaHw->SetAttribute("EndTarget", UintegerValue(swift_end_target));

			rdmaHw->m_searchWinCallback = MakeBoundCallback (GetWin, i);
			rdmaHw->m_searchBaseRTTCallback = MakeBoundCallback (GetBaseRTT, i);

			// setup IRN
			rdmaHw->SetUpIrn(max_bitmap_size, rto_high, rto_low, rto_low_threshold); 

			// create and install RdmaDriver
			Ptr<RdmaDriver> rdma = CreateObject<RdmaDriver>();
			Ptr<Node> node = n.Get(i);
			rdma->SetNode(node);
			rdma->SetRdmaHw(rdmaHw);

			node->AggregateObject (rdma);
			rdma->Init();
			rdma->TraceConnectWithoutContext("ReceiveComplete", MakeBoundCallback (rcv_finish, fct_output));
			rdma->TraceConnectWithoutContext("MsgComplete", MakeBoundCallback (msg_finish, fct_output));
		}
	}

	// set ACK priority on hosts
	if (ack_high_prio)
		RdmaEgressQueue::ack_q_idx = 0;
	else
		RdmaEgressQueue::ack_q_idx = Settings::DEFAULT_DATA;

	// setup switch CC
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 1){ // switch
			Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(n.Get(i));
			sw->SetAttribute("CcMode", UintegerValue(Settings::cc_mode));
			sw->SetAttribute("AckHighPrio", UintegerValue(switch_ack_high_prio));
		}
	}

	// setup routing
	CalculateRoutes(n);
	SetRoutingEntries();

	// get BDP and delay
	maxRtt = maxBdp = 0;
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() != 0)
			continue;
		for (uint32_t j = i+1; j < node_num; j++){
			if (n.Get(j)->GetNodeType() != 0)
				continue;
			uint64_t delay = pairDelay[n.Get(i)][n.Get(j)];
			uint64_t txDelay = pairTxDelay[n.Get(i)][n.Get(j)];
			uint64_t rtt = delay * 2 + txDelay;
			uint64_t bw = pairBw[n.Get(i)][n.Get(j)];
			uint64_t bdp = rtt * bw / 1000000000/8;
			pairBdp[n.Get(i)][n.Get(j)] = bdp;
			pairRtt[n.Get(i)][n.Get(j)] = rtt;
			if (bdp > maxBdp)
				maxBdp = bdp;
			if (rtt > maxRtt)
				maxRtt = rtt;
		}
	}
	Settings::max_bdp = maxBdp;
	Settings::free_token = maxBdp%Settings::packet_payload == 0? maxBdp/Settings::packet_payload : maxBdp/Settings::packet_payload + 1;

	std::cout << "maxRtt=" << maxRtt << " maxBdp=" << maxBdp << " freetoken=" << Settings::free_token
			<< " maxHRTT=" << maxHRTT << " minHRTT=" << minHRTT << std::endl;

	// Swift Settings
	if (Settings::cc_mode == Settings::CC_SWIFT){
		swift_fs_max_cwnd = swift_max_cwnd = Settings::free_token;
		swift_fs_range = maxRtt*std::sqrt(swift_fs_max_cwnd/swift_fs_min_cwnd)*swift_fs_range_alpha;
		if (Settings::swift_hop_scale){
			swift_base_target = 2 * minHRTT + Settings::MTU*1.0/Settings::host_bw_Bps*1e9;
			swift_h = 2 * minHRTT;
		}else{
			swift_base_target = maxRtt;
			swift_h = 0;
		}
		std::cout << "Swift configure:\nfs_range: " << swift_fs_range
				<< " swift_base_target: " << swift_base_target
				<< " swift_h: " << swift_h
				<< " swift_(fs_)max_cwnd: " << swift_fs_max_cwnd << std::endl;
	}

	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 0){	// host
			Ptr<RdmaHw> rdmaHw = n.Get(i)->GetObject<RdmaDriver>()->m_rdma;
			// Quota
			rdmaHw->SetQuota();
			// Swift
			rdmaHw->SetAttribute("BaseTarget", UintegerValue(swift_base_target));
			rdmaHw->SetAttribute("HopScaleFactor", UintegerValue(swift_h));
			rdmaHw->SetAttribute("MaxRangeScale", UintegerValue(swift_fs_range));
			rdmaHw->SetAttribute("MaxCwndScale", DoubleValue(swift_fs_max_cwnd));
			rdmaHw->SetAttribute("MaxCwnd", DoubleValue(swift_max_cwnd));
		}
	}

	//
	// add trace
	//
	tracef.open(trace_file.c_str());
	tracef >> trace_num;
	NodeContainer trace_nodes;
//	for (uint32_t i = 0; i < trace_num; i++)
//	{
//		uint32_t nid;
//		tracef >> nid;
//		if (nid >= n.GetN()){
//			continue;
//		}
//		trace_nodes = NodeContainer(trace_nodes, n.Get(nid));
//	}

	FILE *trace_output = fopen(trace_output_file.c_str(), "w");

	if(!trace_output){
		std::cout<<"error opening trace_output file"<<std::endl;
		return -1;
	}

	if (enable_trace)
		qbb.EnableTracing(trace_output, trace_nodes);

	// dump link speed to trace file
	{
		SimSetting sim_setting;
		for (auto i: nbr2if){
			for (auto j : i.second){
				uint16_t node = i.first->GetId();
				uint8_t intf = j.second.idx;
				uint64_t bps = DynamicCast<QbbNetDevice>(i.first->GetDevice(j.second.idx))->GetDataRate().GetBitRate();
				sim_setting.port_speed[node][intf] = bps;
			}
		}
		sim_setting.win = maxBdp;
		sim_setting.Serialize(trace_output);
	}

	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	NS_LOG_INFO("Create Applications.");

	Time interPacketInterval = Seconds(0.0000005 / 2);

	// maintain port number for each host
	std::unordered_map<uint32_t, uint16_t> portNumder;
	for (uint32_t i = 0; i < node_num; i++){
		if (n.Get(i)->GetNodeType() == 0)
			portNumder[i] = 10000; // each host use port number from 10000
	}

	/*--------------------------------- generate flows -------------------------------------*/
	if (flow_from_file){
		std::cout << "READ FROM FILE" << std::endl;
		flowf.open(flow_file.c_str());
		if (!flowf.is_open()){
			std::cerr << "cannot open flow file" << std::endl;
			return 0;
		}
		flowf >> flow_num >> qp_flow;
		uint32_t qp_id, msg_seq = 0;
		for (uint32_t i = 0; i < flow_num; i++)
		{
			uint32_t src, dst, pg, maxPacketCount, port, dport;
			double start_time, stop_time;
			//flowf >> src >> dst >> pg >> dport >> maxPacketCount >> start_time;

			if (!qp_flow){
				flowf >> src >> dst >> maxPacketCount >> start_time;
				flows.push(Flow(src, dst, maxPacketCount, start_time));
				qp_id++;
			}
			else{
				flowf >> src >> dst >> maxPacketCount >> start_time >> qp_id >> msg_seq;
				flows.push(Flow(src, dst, maxPacketCount, start_time, qp_id, msg_seq, reset_per_qp_msg));
			}

			Flow curr = flows.front();
			flows_map.insert(std::make_pair(curr.flow_id, curr));
			unfinished_flows.insert(curr.flow_id);
			unfinished_rcv_flows.insert(curr.flow_id);
			flows.pop();

			//maxPacketCount = maxPacketCount * packet_payload_size;//maxPacketCount is in bytes mode
			pg = default_prio;
			dport = 100;

			//to avoid overflow when the flow pattern does not match the topo
			/*uint32_t host_num = node_num - switch_num;
			src %= host_num;
			dst %= host_num;

			if(src == dst)
				continue;*/

			NS_ASSERT(n.Get(src)->GetNodeType() == 0 && n.Get(dst)->GetNodeType() == 0);
			port = portNumder[src]++; // get a new port number

			// modify by wqy
			RdmaClientHelper clientHelper(pg, serverAddress[src], serverAddress[dst], port, dport, maxPacketCount, has_win?(global_t==1?maxBdp:pairBdp[n.Get(src)][n.Get(dst)]):0, global_t==1?maxRtt:pairRtt[n.Get(src)][n.Get(dst)], qp_id, msg_seq, src, dst, curr.flow_id);

			ApplicationContainer appCon = clientHelper.Install(n.Get(src));
			appCon.Start(Seconds(start_time));
			appCon.Stop(Seconds(stop_time));
		}
	}else{
		//generate flows with incremental incast dst number, incast dst number = cremental_incast_time
		if (Settings::gen_incast_incremental) {
			if (incast_mix && avg_message_per_qp) {
				qp_flow = 1;
				if (incast_load) {
					std::cout<<"GEN_INCAST_INCREMENTAL\n";
					reset_per_qp_msg = true;
					generate_load_poisson_fromcdf_incremental_incast();
				}else{
					std::cout<<"ERROR! GEN_INCAST_INCREMENTAL, BUT INCAST_LOAD==0!\n";
				}
			}else{
				std::cout<<"ERROR! GEN_INCAST_INCREMENTAL, BUT (INCAST_MIX && AVG_MESSAGE_PER_QP)==FALSE!\n";
			}
		}
		else if(Settings::gen_incast_fattree_nsdi24) {
			std::cout<<"GEN_INCAST_FOR_FATTREE\n";
			generate_incast_fattree_nsdi24(Settings::fattree_incast_m, Settings::fattree_k, incast_time);
		}
		else if(Settings::gen_MOE_traffic)
		{
			std::cout<<"GEN_MOE_TRAFFIC\n";
			generate_MOE_traffic(Settings::moe_shifft_time_mul, Settings::moe_send_intervel,Settings::moe_traffic_size, Settings::moe_send_times, Settings::moe_burst_times);
		}
		// generate flows based on workload from cdf file
		else if (incast_mix) {	//incast_mix is the incast scale
			if (avg_message_per_qp) {	// generate flows with QPid
				qp_flow = 1;
				if (incast_load) {
					std::cout
							<< "INCAST MIX PATTERN with load:incast periodically, then poisson generate from cdf.(without QP)"
							<< std::endl;
					reset_per_qp_msg = true;
					generate_load_poisson_fromcdf_incast();
				}else{
					if (incast_cdf) {
						std::cout
								<< "INCAST MIX PATTERN:(with QP id) incast periodically, then poisson generate from cdf."
								<< std::endl;
						generate_poisson_fromcdf_incast_withQP();
					} else {
						std::cout
								<< "INCAST MIX PATTERN:(with QP id) incast periodically, then poisson(fixed size)."
								<< std::endl;
						generate_poisson_incast_withQP();
					}
				}
			} else {
				std::cout
						<< "INCAST MIX PATTERN:incast periodically, then poisson generate from cdf."
						<< std::endl;
				uint32_t rack_num = Settings::host_num / host_per_rack;
				for (uint32_t i = 0; i < incast_time; i++)
					generate_incast((i + 1) * incast_mix, i % rack_num, i * 15);//dst rack, shift time
				generate_flow_homa_test(flow_num + incast_time * incast_mix, incast_time * 15);	//possion
			}
		}else if (flow_cdf == 98) {
			qp_flow = 1;
			std::cout << "STORAGE PATTERN: 8KB messages with IO depth 8!" << std::endl;
			// background flow
			generate_storage_traffic();
			// test flows
//			generate_test_flow();
		}else if(flow_cdf == 99){
			if (incast_interval) {
				if (incast_cdf) {
					if (avg_message_per_qp) {
						std::cout << "FLOW PATTERN:(with QP id) several incast with size from cdf!" << std::endl;
						generate_incast_from_cdf_withQP();
					} else {
						std::cout << "FLOW PATTERN: several incast with cdf!" << std::endl;
						for (uint32_t i = 0; i < incast_time; i++)
							generate_incast_from_cdf((i + 1) * flow_num, 0, i * incast_interval);	//dst rack, shift time
					}
				} else {
					if (avg_message_per_qp) {
						std::cout
								<< "FLOW PATTERN:(with QP id) several incast with fixed size!"
								<< std::endl;
						generate_incast_withQP();
					} else {
						std::cout
								<< "FLOW PATTERN: several incast with fixed size!"
								<< std::endl;
						for (uint32_t i = 0; i < incast_time; i++)
							generate_incast((i + 1) * flow_num, 0, i * incast_interval);	//dst rack, shift time
					}
				}
			} else {
				std::cout << "ERROR: NO VALID FLOW PATTERN!" << std::endl;
			}
		}else if (flow_cdf == 100){
			std::cout << "Toy pattern to validate motivation." << std::endl;
			generate_toy_pattern();
		}else if (flow_cdf == 101){
			std::cout << "FLOW PATTERN: share-fairness under incast" << std::endl;
			generate_fairness_incast();
		}else if (flow_cdf == 102){
			std::cout << "FLOW PATTERN: share-fairness under oversubscribed topo" << std::endl;
			generate_fairness_oversubscribed_topo();
		}else if (flow_cdf == 103){
			qp_flow = 1;
			std::cout << "Remote RDMA PATTERN." << std::endl;
			generate_remote_pattern();
		}else if (flow_cdf == 104){
			std::cout << "large flows all to all" << std::endl;
			generate_toy_all_to_all();
		}else if (avg_message_per_qp) {
			qp_flow = 1;
			std::cout << "POISSON PATTERN(with QP id)." << std::endl;
			generate_flow_qp_mode();
//			generate_flow_qp_mode_mutiple_for_host_pair();
		} else {
			std::cout << "POISSON PATTERN." << std::endl;
			generate_flow_homa_test(flow_num);
		}

		// reset flow_num
		flow_num = flows.size();
		// assign flows
		std::vector<uint32_t> qos_metric;
		while (!flows.empty())
		{
			Flow curr = flows.front();
			flows_map.insert(std::make_pair(curr.flow_id, curr));
			unfinished_flows.insert(curr.flow_id);
			unfinished_rcv_flows.insert(curr.flow_id);
			qos_metric.push_back(curr.size);
			flows.pop();
			uint32_t src = curr.src, dst = curr.dst, maxPacketCount = curr.size, qp_id = curr.qp_id, msg_seq = curr.msg_seq;
			double start_time = curr.startTime.GetSeconds(), stop_time = curr.stopTime.GetSeconds();

			uint32_t pg = Settings::DEFAULT_DATA, dport = 100;

			// data's switch queue
			if (Settings::cc_mode == Settings::CC_MIBO_NSDI22)
				pg = Settings::MIBO_DATA;
			else if (Settings::cc_mode == Settings::CC_EP)
				pg = Settings::EP_DATA;

			NS_ASSERT(n.Get(src)->GetNodeType() == 0 && n.Get(dst)->GetNodeType() == 0);
			uint32_t port = portNumder[src]++; // get a new port number

			std::cout << std::setprecision(10) << curr.flow_id << " " << src << " " << dst << " " << maxPacketCount << " "
					<< start_time << " " << stop_time << " " << qp_id << " " << msg_seq
					<< std::endl;

			RdmaClientHelper clientHelper(pg, serverAddress[src], serverAddress[dst], port, dport, maxPacketCount, GetWin(src, dst), GetBaseRTT(src, dst), qp_id, msg_seq, src, dst, curr.flow_id);
			clientHelper.SetDstNode(n.Get(dst));
			ApplicationContainer appCon = clientHelper.Install(n.Get(src));
			appCon.Start(Seconds(start_time));
			appCon.Stop(Seconds(stop_time));
		}

		// assign test flows
		std::cout << "Test flows: " << test_flows.size() << std::endl;
		has_test_flow = (test_flows.size() > 0);
		while (!test_flows.empty())
		{
			Flow curr = test_flows.front();
			curr.isTest = true;
			flows_map.insert(std::make_pair(curr.flow_id, curr));
			unfinished_flows.insert(curr.flow_id);
			unfinished_rcv_flows.insert(curr.flow_id);
			test_flows.pop();
			uint32_t src = curr.src, dst = curr.dst, maxPacketCount = curr.size, qp_id = curr.qp_id, msg_seq = curr.msg_seq;
			double start_time = curr.startTime.GetSeconds(), stop_time = curr.stopTime.GetSeconds();

			uint32_t pg = Settings::DEFAULT_DATA, dport = 100;

			// data's switch queue
			if (Settings::cc_mode == Settings::CC_MIBO_NSDI22)
				pg = Settings::MIBO_DATA;
			else if (Settings::cc_mode == Settings::CC_EP)
				pg = Settings::EP_DATA;

			NS_ASSERT(n.Get(src)->GetNodeType() == 0 && n.Get(dst)->GetNodeType() == 0);
			uint32_t port = portNumder[src]++; // get a new port number

			std::cout << std::setprecision(10) << src << " " << dst << " " << maxPacketCount << " "
					<< start_time << " " << stop_time << " " << qp_id << " " << msg_seq
					<< std::endl;

			RdmaClientHelper clientHelper(pg, serverAddress[src], serverAddress[dst], port, dport, maxPacketCount, GetWin(src, dst), GetBaseRTT(src, dst), qp_id, msg_seq, src, dst, curr.flow_id, true);
			clientHelper.SetDstNode(n.Get(dst));
			ApplicationContainer appCon = clientHelper.Install(n.Get(src));
			appCon.Start(Seconds(start_time));
			appCon.Stop(Seconds(stop_time));
		}

		Settings::SplitRank(qos_metric);
	}

	topof.close();
	flowf.close();
	tracef.close();

	// schedule link down
	if (link_down_time > 0){
		Simulator::Schedule(Seconds(2) + MicroSeconds(link_down_time), &TakeDownLink, n, n.Get(link_down_A), n.Get(link_down_B));
	}

	// schedule buffer monitor
	FILE* qlen_output = fopen(qlen_mon_file.c_str(), "w");
	Simulator::Schedule(NanoSeconds(qlen_mon_start), &monitor_buffer, qlen_output, &n);

	//
	// Now, do the actual simulation.
	//
	std::cout << "Running Simulation. Simulation will stop at " << simulator_stop_time << "s" << std::endl;;
	fflush(stdout);
	NS_LOG_INFO("Run Simulation.");
	Simulator::Stop(Seconds(simulator_stop_time));
	cout<<"fc_mode "<<Settings::fc_mode<<endl;
	Simulator::Run();
	Simulator::Destroy();
	NS_LOG_INFO("Done.");
	fclose(trace_output);

	std::cout << "dst unfinished: " << unfinished_rcv_flows.size() << "\n";
	std::set<uint32_t>::iterator unfinished_curr = unfinished_rcv_flows.begin();
	while (unfinished_curr !=  unfinished_rcv_flows.end()){
		std::cout << "unfinished " << flows_map.at(*unfinished_curr).flow_id << " "
				<< flows_map.at(*unfinished_curr).src << " "
				<< flows_map.at(*unfinished_curr).dst << " "
				<< flows_map.at(*unfinished_curr).size << " "
				<< flows_map.at(*unfinished_curr).startTime << " "
				<< flows_map.at(*unfinished_curr).qp_id << " "
				<< flows_map.at(*unfinished_curr).msg_seq << std::endl;
		unfinished_curr++;
	}

	std::cout << "src unfinished: " << unfinished_flows.size() << "\n";
	unfinished_curr = unfinished_flows.begin();
	while (unfinished_curr !=  unfinished_flows.end()){
		std::cout << "unfinished " << flows_map.at(*unfinished_curr).flow_id << " "
				<< flows_map.at(*unfinished_curr).src << " "
				<< flows_map.at(*unfinished_curr).dst << " "
				<< flows_map.at(*unfinished_curr).size << " "
				<< flows_map.at(*unfinished_curr).startTime << " "
				<< flows_map.at(*unfinished_curr).qp_id << " "
				<< flows_map.at(*unfinished_curr).msg_seq << std::endl;
		unfinished_curr++;
	}

	fclose(fct_output);//added, lkx

	fprintf(qlen_output, "maxPortLength %u maxSwitchLength %u", max_port, max_switch);//more specific value instead of bitmap
	//bw monitor added by lkx
	FILE* bw_output = fopen(bw_mon_file.c_str(),"w");
	double avg_bw = analysis_bw(Settings::bwList, node_num, node_num - switch_num, bw_output, 0);
	for (uint32_t i = 0; i < Settings::NODESCALE; ++i){
		fprintf(bw_output, "%i: ", i);
		for (uint32_t j = 0; j < Settings::bwList[i].size(); ++j){
			fprintf(bw_output, "%.3f ", Settings::bwList[i][j]);
		}
		fprintf(bw_output, "\n");
	}
	fflush(bw_output);

	std::cout<<"bw: "<<avg_bw<<std::endl;

	double avg_tp = analysis_bw(Settings::tpList, node_num, node_num - switch_num, bw_output, 0);
	for (uint32_t i = 0; i < Settings::NODESCALE; ++i){
		fprintf(bw_output, "%i: ", i);
		for (uint32_t j = 0; j < Settings::tpList[i].size(); ++j){
			fprintf(bw_output, "%.3f ", Settings::tpList[i][j]);
		}
		fprintf(bw_output, "\n");
	}
	std::cout<<"tp: "<<avg_bw<<std::endl;

	double avg_ctrl = analysis_bw(Settings::ctrlList, node_num, node_num - switch_num, bw_output, 0);
	std::cout<<"ctrl: "<<avg_bw<<std::endl;

	//queueing monitor added by lkx
	FILE* queueing_output = fopen(queueing_mon_file.c_str(),"w");
	double avg_queueing = analysis_bw(Settings::QDelayList, node_num, node_num - switch_num, queueing_output, 0);
	std::cout<<"queuing: "<<avg_queueing<<std::endl;
	std::cout << "maxPortLength: " << Settings::max_port_length << "\tmaxPortIndex: " << Settings::max_port_index <<  std::endl;
	std::cout << "maxSwitchLength: " <<  Settings::max_switch_length << std::endl;

	if (Settings::fc_mode == Settings::FC_FLOODGATE){
		std::cout << "maxSwitchEgressCache: " << SwitchMmu::max_egress_cache_bytes_all << "\tmaxEgressCache: " << SwitchMmu::max_egress_cache_bytes << std::endl;
		std::cout << "maxSwitchVOQBuffer: " <<  Settings::max_switch_voq_length << "\tmaxVOQLength: " <<  Settings::max_voq_length << std::endl;
		std::cout << "maxUsedVOQNum: " << VOQ::maxVOQNum << "\tmaxActiveDstNum: " << VOQ::maxActiveDstNum << std::endl;
		std::cout << "maxSwitchACKSize: " << SwitchACKTag::max_switchack_size << "\tFixSwitchACKSize: " << SwitchACKTag::switch_ack_payload << std::endl;
		std::cout << "maxSwitchDataPSN: " << SwitchMmu::max_nxt_data_psn << "(" << LONG_MAX << ")" << "\tmaxSwitchSYNSize: " << SwitchSYNTag::max_switchsyn_size << std::endl;
		std::cout << "ByteCreditCounter: " << SwitchMmu::switch_byte_credit_counter << "\tTimeoutCreditCounter: " << SwitchMmu::switch_timeout_credit_counter << std::endl;
		std::cout << "nodes' max used voq num: \n";
		for (uint64_t i = 0; i < node_num-Settings::host_num; ++i){
			std::cout << i << ": ";
			std::map<uint32_t, uint32_t>::iterator it = VOQ::node_maxVOQNum[i].begin();
			while (it != VOQ::node_maxVOQNum[i].end()){
				std::cout << it->first << "-" << it->second << ",";
				it++;
			}
			std::cout << "\t";
		}
		std::cout << std::endl;
	}else if (Settings::fc_mode == Settings::FC_BFC){
		std::cout << "maxFidConflict: " << SwitchMmu::max_fid_conflict << std::endl;
		std::cout << "maxActiveQ on Switches: \n";
		uint32_t max_active = 0;
		for (uint32_t i = 0; i < Settings::switch_num; i++){
			std::cout << i << ":";
			for (uint32_t j = 0; j < Settings::PORTNUM; j++){
				if (SwitchMmu::max_active_q[i][j] > 0){
					max_active = std::max(max_active, SwitchMmu::max_active_q[i][j]);
					std::cout << j << "(" << SwitchMmu::max_active_q[i][j] << ")-";
				}
			}
			std::cout << ", ";
		}
		std::cout << "\nmaxActiveQ: " << max_active << std::endl;
	}else if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC || Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
		std::cout << "maxActivePortVOQ: " << BEgressQueue::maxActivePortVOQ << std::endl;
		std::cout << "maxPortVOQ: " << BEgressQueue::maxPortVOQ << std::endl;
		std::cout << "maxPortVOQLen: " << BEgressQueue::maxPortVOQLen << std::endl;
	}

	if (Settings::cc_mode == Settings::CC_MIBO_NSDI22){
		std::cout << "TotalExpiredCredit: " << RdmaHw::mibo_expired_token << std::endl;
	}else if (Settings::cc_mode == Settings::CC_SWIFT){
		std::cout << "MaxFabricRTT: " << RdmaHw::swift_max_fabric_rtt << std::endl;
	}

	std::cout << "Data packet avg queuing time: \n";
	for (uint64_t i = 0; i < Settings::MAXHOP; ++i){
		std::cout << Flow::totalAvgQueuingTime[i]/all_rcv_fct.size() << "\t";
	}
	std::cout << std::endl;

	std::cout << "maxEgressQueueCounter: " << SwitchMmu::max_egress_queue_bytes << std::endl;
	std::cout << "Switch max queue length / set ECN times: \n";
	for (uint32_t i = 0; i < BEgressQueue::maxQueueLen.size(); ++i){
		std::cout << BEgressQueue::maxQueueLen[i] << " / ";
		if (i < SwitchMmu::sentCN.size()){
			std::cout << SwitchMmu::sentCN[i] << "\t";
		}else{
			std::cout << 0 << "\t";
		}
	}
	std::cout << std::endl;

	std::cout << "Drop packets / total packets (w/o pause/resume packets): " << std::endl;
	for (uint32_t i = 0; i < Settings::total_packets.size(); ++i){
		if (i < Settings::drop_packets.size()){
			std::cout << Settings::drop_packets[i] << " / ";
		}else{
			std::cout << 0 << " / ";
		}
		std::cout << Settings::total_packets[i] << "\t";
	}
	std::cout << std::endl;
	
	std::cout << "Timeout triggers: " << Settings::timeout_times << std::endl;
	std::cout << "Duplicated retransmission Count: " << Settings::duplicate_rtxCount << std::endl;

	endt = clock();
	std::cout << "Simulation time: " << (double)(endt - begint) / CLOCKS_PER_SEC << "s\n";

}
