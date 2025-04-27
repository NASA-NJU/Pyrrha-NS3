/*
 * settings.h
 *
 *  Created on: Aug 10, 2021
 *      Author: wqy
 */

#ifndef SRC_POINT_TO_POINT_MODEL_SETTINGS_H_
#define SRC_POINT_TO_POINT_MODEL_SETTINGS_H_

#include <fstream>
#include <cstdio>
#include <stdint.h>
#include <string>
#include <algorithm>
#include <vector>
#include <numeric>
#include <sstream>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <list>
#include "ns3/nstime.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include <limits.h>
#include <map>
#include <unordered_map>

namespace ns3{

class Settings {
public:
	Settings() {}
	virtual ~Settings() {}

	enum { POISSON = 1, LOG_NORMAL = 2} ARRIVAL_MODE;
	enum { PAUSE_OFF = 0, PAUSE_PG = 1, PAUSE_DST = 2, PAUSE_QP = 3, PAUSE_SPOT = 4, PAUSE_QUEUE = 5 } PAUSE_GRAN;

	// switch queue allocation
	enum { BFC_PAUSE_RESUME = 0 } BFC_QUEUE;
	enum { MIBO_ACK = 0, MIBO_TOKENACK = 0, MIBO_TOKEN = 1, MIBO_DATA = 2} MIBO_QUEUE;
	enum { EP_ACK = 0, EP_CREDIT = 1, EP_DATA = 2} EP_QUEUE;
	enum { DEFAULT_ACK = 3, DEFAULT_DATA = 3} DEFAULT_QUEUE;

	static const uint32_t NODESCALE = 3000;
	static const uint32_t SWITCHSCALE = 1000;
	static const uint32_t MAXHOP = 8;
static const uint32_t QUEUENUM = 8;
	static const uint32_t NICQNUM = 2000;
	static const uint32_t PORTNUM = 257;

	// wqy: for setting
	static uint32_t packet_payload;
	static uint32_t MTU;

	// wqy: for timeout retransmission
	static uint32_t rto_us;

	// wqy: for qp mode
	static bool reset_qp_rate;
	enum { MSG_FIFO = 1, MSG_RR = 2};
	static uint32_t msg_scheduler;

	// wqy: for flow generatoin
	static bool pktsmode;
	static uint32_t maxx; // max flow size
	static double homa_longflow_threshold;
	static std::vector<std::pair<uint32_t,double> > cdfFromFile;
	static int avgSizeFromFile;
	static double threshold_slope;
	static double threshold_bias;

	//lkx: for incast-mix flow generation
	static bool one_incast_dst;//one incast dst is convenient for analysis of victim flows

	//liuchang: incrementally gen incast flows for scalable test
	static bool gen_incast_incremental;
	static bool gen_incast_fattree_nsdi24;
	static uint32_t fattree_k; // defaut set k=8
	static uint32_t fattree_incast_m;

	//liuchang: generate MOE traffic
	static bool gen_MOE_traffic;
	static double moe_send_intervel; // us
	static double moe_shifft_time_mul;
	static uint32_t moe_traffic_size;
	static uint32_t moe_send_times;
	static uint32_t moe_burst_times;
	//generate MOE trace
	static std::vector<double> expert_probability;

	//liuchang: for queue allocation in host port
	static uint32_t queue_allocate_in_host;

	/*--------------------incast traffic generation (added by liuchang on 2022/07/18)---------------------*/
	static uint32_t incast_min;
	static uint32_t incast_max;

	/*--------------------traffic generation (added by lkx on 2022/06/22)---------------------*/
	static uint32_t time_shift;

	/*--------------------for flow send (added by liuchang on 2024/03/21)---------------------*/
	static bool enable_qp_winBound; // defaut set to ture: enbale window control for qp

	/*---------------------Topology information----------------------*/
	/*
	 * the node link with <node, port>
	 */
	static uint32_t link_node[Settings::NODESCALE][Settings::PORTNUM];
	/*
	 * whether a port is an upstream port
	 */
	static bool up_port[Settings::SWITCHSCALE][Settings::PORTNUM];

	/*---------------------For common switch configure(added by wqy on 2021/8/10)----------------------*/
	/**
	 * When enqueue packet, whether check whole switch buffer.
	 * If check, packet will be dropped when buffer overflows.
	 */
	static bool check_shared_buffer;
	
	/*---hdrm ratio (added by lkx on 2022/06/22)---*/
	static uint32_t hdrm_ratio;

	/**
	 * Max queue length of each switch queue.
	 * Packet will be dropped when queue overflows.
	 */
	static uint32_t max_qlen[QUEUENUM];
	/**
	 * ECN unit for each queue
	 */
	static double ecn_unit[QUEUENUM];
	/**
	 * For faster congestion notification (must work with SYMMETRIC_ON)
	 * when dequeue a data packet, piggy ECN of ingress queue with id == piggy_qid
	 */
	static bool piggy_ingress_ecn;
	static uint32_t piggy_qid;
	/**
	 * Support hierarchical queues on switch
	 */
	static bool hier_Q;

	/*---------------------For Load Balance/Routing/Solve disordered(added by wqy on 2020/12/8)-------------*/
	static uint32_t EcmpHash(const uint8_t* key, size_t len, uint32_t seed);
	/**
	 * Set routing method for install
	 * (0/Default) --> (ecmp, non-symmetric for default)
	 * (1) DRILL --> (per packet routing, non-symmetric for default)
	 */
	static uint32_t routing_mode;
	static const uint32_t ROUTING_GLOBAL = 0;
	static const uint32_t ROUTING_DRILL = 1;
	static bool IsPacketLevelRouting();
	/**
	 * Set symmetric mode
	 * (0) turn off
	 * (1) turn on symmetric hash
	 */
	static uint32_t symmetic_routing_mode;
	static const uint32_t SYMMETRIC_OFF = 0;
	static const uint32_t SYMMETRIC_ON = 1;
	/*---------------ECMP-------------------*/
	static uint32_t ecmp_seed;
	static uint32_t load_imbalance_th;
	/**
	 * ECMP routing unit
	 */
	static uint32_t ecmp_unit;
	enum { ECMP_FLOW = 0, ECMP_QP = 1 };
	/*
	 * for a node, map from ip address (u32) to possible ECMP port (index of dev)
	 * <node, <dip, nexthops> >
	 */
	static std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::vector<int> > > node_rtTable;
	/*---------------DRILL-------------------*/
	/**
	 * (0/default) use port's queuing length as load
	 * (1) use port's received number of packets for a period of time as load
	 */
	static uint32_t drill_load_mode;
	static const uint32_t DRILL_LOAD_DEFAULT = 0;
	static const uint32_t DRILL_LOAD_INTERVAL_SENT = 1;
	/**
	 * add by wqy on 2020/12/15
	 * The interval of reseting queue's statistics info
	 * (Only used when drill_load_mode == DRILL_LOAD_INTERVAL_SENT for now)
	 */
	static uint32_t queue_statistic_interval_us;

	/*---------------------L2: Flow Control------------------------------*/
	enum { FC_PFC = 0, FC_FLOODGATE = 1, FC_BFC = 2, FC_VOQ_DSTPFC = 3, FC_CONGESTION_ISOLATION = 4, FC_PFCoff = 5} FC_MODE;
	static uint32_t fc_mode;
	/*---------PFC-----------*/
	static bool srcToR_dst_pfc;
	static uint32_t pfc_dst_alpha;
	static double pfc_th_static;
	/*---------Floodgate(added by wqy on 2020/12/8)-----*/
	/**
	 * The map between hosts' IP and ID
	 * initial when build topology
	 */
	static std::map<uint32_t, uint32_t> hostIp2IdMap;
	static std::map<uint32_t, uint32_t> hostId2IpMap;

	static double switch_win_m;
	/**
	 * (true)ONLY_TOR: only ToRs deploy SwitchWin; only dst-ToR send switch-ack
	 * (false)ALL_SWITCHES: link-by-link control
	 */
	static bool reset_only_ToR_switch_win;
	/**
	 * isolate all up and down streams' dst on switches besides core
	 * (0) turn off
	 * (1) turn on: use ignore as isolation solution
	 * (2) turn on: use different VOQ as isolation solution
	 */
	static uint32_t reset_isolate_down_host;
	static const uint32_t RESET_ISOLATE_IGNORE = 1;
	static const uint32_t RESET_ISOLATE_DIFFGROUP = 2;		// default
	/*
	 * define the way to send ACK/credit
	 * HOST_PER_PACKET_ACK: reuse ACK as SwitchCredit/SwitchACK; host must support per-packet-ack
	 * SWITCH_DST_CREDITT: switches are responsible for sending SwitchACK; a SwitchACK will carry credits of a dst from a same ingress
	 * SWITCH_INGRESS_CREDIT: switches are responsible for sending SwitchACK; a SwitchACK will carry credits of different dsts from a same ingress
	 */
	enum { HOST_PER_PACKET_ACK = 0u, SWITCH_DST_CREDIT = 1u, SWITCH_INGRESS_CREDIT = 2u};
	static uint32_t switch_ack_mode;
	static uint32_t switch_byte_counter;		// 0/1: per packet ack; useless when HOST_PER_PACKET_ACK
	static double switch_credit_interval;		// 0: turn off; unit: us; useless when HOST_PER_PACKET_ACK or switch_byte_counter = 0/1
	/*
	 * handle packet-loss: absolute data/SwitchACK PSN + timeout
	 * only work with SWITCH_DST_CREDIT and SWITCH_INGRESS_CREDIT
	 */
	static uint64_t switch_absolute_psn;		// 0: turn off
	static uint32_t switch_syn_timeout_us;		// 0: turn off; useful when switch_absolute_psn turns on
	/*
	 * delayACK: the threshold to send SwitchACK
	 */
	static double switch_ack_th_m;
	/*
	 * per-port win should work with ALL-SWITCHES + SWITCH_DST_CREDIT/SWITCH_INGRESS_CREDIT
	 * When use port_win, there is an egress_cache layer between VOQ buffer and egress_queue.
	 * (port win will cause heavy HoL-blocking, always turn off!!!)
	 */
	static bool use_port_win;
	static double switch_port_win_m;
	/*
	 * 0: stands for no adaptive
	 * 1: stands for linear: k * x, where x denotes # incast
	 * 2: stands for square: x^2
	 * 3: stands for exponential: 2^x
	 * (the buffer cannot be controlled well, always turn off!!!)
	 */
	static uint16_t adaptive_win;
	static bool adaptive_whole_switch;
	/*-----------BFC(added by wqy on 2021/10/5)--------*/
	static double sticky_th;
	static uint32_t flowtb_size;
	/*
	 * (It is not included in paper)
	 * To get a ideal performance of BFC, we can turn on this setting to avoid hash conflict of FID.
	 */
	static bool use_hash_fid;
	/*
	 * (It is not included in paper)
	 * Incast flows may use sticky queues and poisson flows may come among the sticky period,
	 * which will result in a bad performance of the poisson flows.
	 * To avoid the situation above, add "assign_sticky_q" setting,
	 * we can turn off this setting to let poisson not to be assigned to a sticky queue.
	 */
	static bool assign_sticky_q;
	/*
	 * (It is not included in paper)
	 * From simulation results, when queues are not adequate, it's much more better to assign empty but paused queue to new flow.
	 */
	static bool assign_paused_q;
	/*-----------FC_VOQ_DSTPFC(per-port-VOQ + per-dst-PFC) (added by wqy on 2021/10/18)--------*/
	static double voq_pauseth_m;	// unit: HBDP
	/**
	 * add by wqy on 2021/11/23
	 * modes for pause (METRIC_FINE_CTRLLINK)
	 * EGRESSVQLEN_DST_PERHOP: use queue length of egress queue at DToR and VOQ as metric, per-dst per-hop pause
	 * BUFFER_DST_PERHOP: use buffer of dst's packets as metric, per-dst per-hop pause
	 */
	enum { EGRESSVQLEN_DST_PERHOP = 0, BUFFER_DST_PERHOP = 1, EGRESSBUFFER_DST_PERHOP = 2};
	static uint32_t voq_pause_mode;
	/**
	 * add by wqy on 2021/11/23
	 * the threshold to distinguish burst part and scheduled part
	 * 0: don't distinguish flows
	 * INT_MAX: automatic identification according to the state of the flow (receive ACK or not, send out first window or not)
	 * (unit: MTU)
	 */
	static uint32_t flow_sch_th;
	/**
	 * add by wqy on 2021/11/24
	 * when receive pause/resume, pause/resume corresponding queue/dst of all egresses
	 */
	static bool ctrl_all_egress;
	/*---------------------For Congestion Isolation (added by wqy on 2021/12/16)-------------*/
	// threshold
	static double th_hol;
	static double th_congestion;
	static double th_resume;
	static double th_ciq_rate;
	// design choice
	static bool nearest_match;
	static bool congestion_merge;
	static bool dealloc_on;

	// enable pyrrha only in down port
	static bool only_down_port_enable;

	/*---------------------For QoS (added by wqy on 2022/5/27)-------------*/
	/**
	 * define the implementation method of QoS
	 */
	enum {QOS_NONE = 0, QOS_PIFOQ = 1};
	static uint32_t qos_mode;
	/**
	 * QoS implementation always depend on the ranks for each packet
	 * define the rank calculation method for supporting QoS
	 * note: only useful when qos_mode != QOS_NONE
	 */
	static uint32_t qos_rank_num;
	static std::vector<uint32_t> rank_split;
	static void SplitRank(std::vector<uint32_t>&);
	static uint32_t GetRank(uint32_t);

	/*---------------------For host L4/L3 settings(added by wqy on 2021/8/11)-------------*/
	/*-----------Quota(per host window)---------------*/
	static uint32_t free_token;
	static uint32_t max_bdp;
	static uint32_t quota_num;
	static uint32_t quota_mode;
	/*------------Congestion control algorithm-----------*/
	enum { CC_DCQCN = 1, CC_MIBO_NSDI22 = 2, CC_HPCC = 3, CC_EP = 4, CC_SWIFT = 5, CC_TIMELY = 7, CC_DCTCP = 8} CC_MODE;
	static uint32_t cc_mode;
	/*---------------------For PCC----------------------*/
	enum { HYPR = 1} MIBO_CC_MODE;
	static uint32_t mibo_cc_mode;
	enum { NO_LIMIT_TKN = 0, LIMIT_TKN = 1, LIMIT_TKN_EXCLUDE_UNSCH = 2} LIMIT_TKN_MODE;
	enum { RCV_TKN = 0, AFTER_UNSCH = 1} TURN_SCH_PAHSE_MODE;
	/*---------------------For Swift(added by wqy on 2021/11/2)----------------------*/
	static bool swift_hop_scale;
	/*------------Congestion control algorithm-----------*/
	/**
	 * add by wqy on 2020/12/8
	 * (false)	Turn off the reaction of NACK.
	 * 		   	--> In this way, drop will cause unfinished message,
	 * 		   	--> however, this will avoid unnecessary retransmission when do packet-level routing.
	 * (true) 	Turn on the reaction of NACK
	 */
	static bool nack_reaction;
	/**
	 * hosts send CNP or not
	 */
	enum { NO_CNP = 0, ADDITION_CNP = 1, PIGGY_CNP = 2} HOST_CNP_MODE;
	static uint32_t host_cnp;
	/*
	 * Solve dis-order or not
	 */
	static bool allow_dis_order;
	static bool AllowDisOrder();

	/*---------------------for IRN---------------------------*/
	static bool use_irn;
	static uint32_t duplicate_rtxCount;

	/*---------------------For statistics----------------------*/
	static bool is_out_win;
	static std::ofstream switch_buffer_out[SWITCHSCALE];
	static std::ofstream switch_bw_out[SWITCHSCALE];
	static std::ofstream bw_out;
	static std::ofstream rate_out;
	static std::ofstream win_out;
	static std::ofstream queuing_out;
	static std::ofstream warning_out;
	static uint32_t bw_interval;
	static uint32_t buffer_interval;
	static uint32_t host_num;
	static uint32_t host_per_rack;
	static uint32_t switch_num;
	static uint32_t tor_num;
	static uint32_t core_num;
	static uint32_t timeout_times;
	static std::vector<uint64_t> drop_packets;
	static std::vector<uint64_t> total_packets;
	static uint32_t max_port_length;
	static uint32_t max_port_index;
	static uint32_t max_switch_voq_length;
	static uint32_t max_switch_length;
	static uint32_t max_voq_length;

	// for bw analysis
	static std::vector<double> bwList[NODESCALE];
	static std::vector<double> QDelayList[NODESCALE];
	static double host_bw_Bps;

	static std::vector<double> ctrlList[NODESCALE];
	static std::vector<double> tpList[NODESCALE];

};


}

#endif /* SRC_POINT_TO_POINT_MODEL_SETTINGS_H_ */
