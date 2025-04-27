/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* Copyright (c) 2006 Georgia Tech Research Corporation, INRIA
*
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

#ifndef BROADCOM_EGRESS_H
#define BROADCOM_EGRESS_H

#include <queue>
#include "ns3/packet.h"
#include "ns3/queue.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/pifo-queue.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/event-id.h"
#include "ns3/simulator.h"
#include "ns3/settings.h"
#include <assert.h>

#define MAX_HOP 5

namespace ns3 {

	class TraceContainer;

	class BEgressQueue : public Queue {
	friend class SwitchNode;
	public:
		static TypeId GetTypeId(void);

		// settings
		enum {QUEUE_DROPTAIL = 0, QUEUE_PIFO = 1}; // Types of switch queues
		static uint32_t queueType;

		// statistics
		static std::vector<uint32_t> maxQueueLen;
		static uint32_t maxPortVOQ;
		static uint32_t maxActivePortVOQ;
		static uint32_t maxPortVOQLen;

		uint32_t m_queueId;
		uint32_t m_devId;
		uint32_t m_switchId;
		bool m_isLastHop;
		bool use_hier_q;

		BEgressQueue();
		virtual ~BEgressQueue();
		bool Enqueue(Ptr<Packet> p, uint32_t qIndex);
		Ptr<Packet> DequeueRR(std::map<uint32_t, bool>& paused, uint8_t pfc_queue, std::map<uint32_t, uint32_t>& ip2id);
		uint32_t GetNQueue() const;
		uint32_t GetNBytes(uint32_t qIndex) const;
		uint32_t GetNBytesTotal() const;
		uint32_t GetLastQueue();
		inline void SetSwitchId(uint32_t switchId) {m_switchId = switchId;}
		inline void SetDevId(uint32_t devId) {m_devId = devId;}
		inline void SetQueueId(uint32_t queueId) {m_queueId = queueId;}

		void InitialHierOQ();
		uint32_t RegisterNewQ(uint32_t level);

		// configure
		inline void SetPFCCtrl(std::vector<bool> pfc_ctrls) {m_pfc_ctrls = pfc_ctrls;}
		inline void SetPriority(std::vector<std::vector<uint32_t> > priority) {
			m_priorities[0] = priority;
			std::vector<uint32_t> curr_rrlast;
			for (uint32_t i = 0; i < m_priorities[0].size(); ++i){
				curr_rrlast.push_back(0);
			}
			m_rrlasts[0] = curr_rrlast;
		}
		inline void SetRateLimiter(std::vector<double> rate_limiters, DataRate maxrate) {
			m_rate_limiters = rate_limiters;
			m_max_rate = maxrate;
//			std::cout << "switch:" << m_switchId << " queue:" << m_queueId
//					<< " token rate_limiters:" << m_rate_limiters[1] <<
//					" maxrate:" << m_max_rate << std::endl;
		}

		// rate-limiter
		uint32_t m_rate_limiter_mode;
		Time GetRateLimiterAvail();

		uint32_t GetTotalReceivedBytes () const;
		void ResetStatistics();

		TracedCallback<Ptr<const Packet>, uint32_t> m_traceBeqEnqueue;
		TracedCallback<Ptr<const Packet>, uint32_t> m_traceBeqDequeue;

		Callback<bool, uint32_t, uint32_t> m_dequeueCheckFC;

		// VOQ for dst
		std::map<uint32_t, uint32_t> m_dstQ_map;	// <dst, voq_id>
		uint32_t RegisterVOQ(uint32_t dst);

		// CIQ for congestion isolation
		std::unordered_map<uint32_t, std::set<uint32_t> > order_blocked_q;	// useless
		uint32_t RegisterCIQ(uint16_t level);
		uint32_t GetOQBytesTotal(uint16_t outdev);//added for calculating bytes on OQ

	private:
		bool DoEnqueue(Ptr<Packet> p, uint32_t qIndex);
		Ptr<Packet> DoDequeueRR(std::map<uint32_t, bool>& paused, uint8_t pfc_queue, std::map<uint32_t, uint32_t>& ip2id);
		//for compatibility
		virtual bool DoEnqueue(Ptr<Packet> p);
		virtual Ptr<Packet> DoDequeue(void);
		virtual Ptr<const Packet> DoPeek(void) const;
		double m_maxBytes; //total bytes limit
		std::vector<uint32_t> m_bytesInQueue;
		uint32_t m_bytesInQueueTotal;
		uint32_t m_qlast;
		std::vector<Ptr<Queue> > m_queues; // uc queues
		// pfc control
		std::vector<bool> m_pfc_ctrls;
		// rate_limiter
		DataRate m_max_rate;
		std::vector<double> m_rate_limiters;
		std::vector<Time> m_nxt_avails;
		std::vector<bool> m_rate_limiting;
		std::map<uint32_t, std::vector<uint32_t> > hier_Q_map;		// <level, queue_ids>
		// priority
		std::map<uint32_t, std::vector<std::vector<uint32_t> > > m_priorities;	// <level, queue_ids[prio]>
		// RR of different priorities
		std::map<uint32_t, std::vector<uint32_t> > m_rrlasts; // <level, prio_rr_last>
	};

	/*
	 * Block one Q
	 */
	class BlockMarkTag : public Tag
	{
	public:
		BlockMarkTag(){ qid = -1;}
		static TypeId GetTypeId (void){
			static TypeId tid = TypeId ("ns3::BlockMarkTag") .SetParent<Tag> () .AddConstructor<BlockMarkTag> ();
			return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{
			return sizeof(uint32_t);
		}
		virtual void Serialize (TagBuffer i) const{
			i.WriteU32(qid);
		}
		virtual void Deserialize (TagBuffer i){
			qid = i.ReadU32();
		}
		void SetBlockedQ(uint32_t _qid){
			qid = _qid;
		}
		uint32_t GetBlockedQ() {return qid;}
		virtual void Print (std::ostream &os) const{ }
	private:
		uint32_t qid;
	};

	/*
	 * Congestion isolation signal
	 */
	class CITag : public Tag
	{
	public:
		enum {CI_PAUSE = 0, CI_RESUME = 1, CI_DEALLOC = 2, CI_MERGE = 3};
		CITag(){ root_switch = -1; root_port = -1; old_switch = 0, old_port = 0; type = 0;hop = 0;}
		static TypeId GetTypeId (void){
			static TypeId tid = TypeId ("ns3::CITag") .SetParent<Tag> () .AddConstructor<CITag> ();
			return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{
			return sizeof(uint32_t)*4 + sizeof(uint8_t) + sizeof(uint16_t);
		}
		virtual void Serialize (TagBuffer i) const{
			i.WriteU32(root_switch);
			i.WriteU32(root_port);
			i.WriteU32(old_switch);
			i.WriteU32(old_port);
			i.WriteU16(hop);
			i.WriteU8(type);
		}
		virtual void Deserialize (TagBuffer i){
			root_switch = i.ReadU32();
			root_port = i.ReadU32();
			old_switch = i.ReadU32();
			old_port = i.ReadU32();
			hop = i.ReadU16();
			type = i.ReadU8();
		}
		void SetCI(uint32_t _root_switch, uint32_t _root_port, uint8_t _type){
			root_switch = _root_switch;
			root_port = _root_port;
			type = _type;
		}
		void SetOldRoot(uint32_t _old_switch, uint32_t _old_port){
			old_switch = _old_switch;
			old_port = _old_port;
		}
		void SetHop(uint16_t _hop){
			hop = _hop;
		}
		uint32_t GetRootSwitch() {return root_switch;}
		uint32_t GetRootPort() {return root_port;}
		uint32_t GetOldSwitch() {return old_switch;}
		uint32_t GetOldPort() {return old_port;}
		uint8_t GetCIType() {return type;}
		uint16_t GetHop() {return hop;}
		bool IsPause() { return type == CI_PAUSE;}
		bool IsResume() { return type == CI_RESUME;}
		bool IsDealloc() { return type == CI_DEALLOC;}
		bool IsMerge() { return type == CI_MERGE;}
		virtual void Print (std::ostream &os) const{ }
	private:
		uint32_t root_switch;
		uint32_t root_port;
		uint32_t old_switch;
		uint32_t old_port;
		uint16_t hop;
		uint8_t type;
	};

	/*
	 * Tag all routings for data packets
	 * The routing info(cnt, routing_switch, routing_hop) is carried when sender sends this packet,
	 * and curr_hop records current hop which should be added at each hop.
	 */
	class RoutingTag : public Tag
	{
	public:
		RoutingTag(){ curr_hop = 0; cnt = 0; }
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::RoutingTag") .SetParent<Tag> () .AddConstructor<RoutingTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{
		  return sizeof(uint32_t)*(2+Settings::MAXHOP*2);
	  }
	  virtual void Serialize (TagBuffer i) const{
		  i.WriteU32(curr_hop);
		  i.WriteU32(cnt);
		  for(int k=0;k<Settings::MAXHOP;++k){
			  i.WriteU32(routing_switch[k]);
			  i.WriteU32(routing_port[k]);
		  }
	  }
	  virtual void Deserialize (TagBuffer i){
		  curr_hop = i.ReadU32();
		  cnt = i.ReadU32();
		  for(int k=0;k<Settings::MAXHOP;++k){
			  routing_switch[k] = i.ReadU32();
			  routing_port[k] = i.ReadU32();
		  }
	  }
	  uint32_t GetActiveHop() {return cnt;}
	  uint32_t GetCurrHop() {return curr_hop;}
	  void AddCurrHop() { ++curr_hop;}
	  uint32_t GetHopRoutingSwitch(uint32_t i) {
		  assert(i < cnt);
		  return routing_switch[i];
	  }
	  uint32_t GetHopRoutingPort(uint32_t i) {
		  assert(i < cnt);
		  return routing_port[i];
	  }
	  virtual void Print (std::ostream &os) const{ }
	  void EnqueueRouting(uint32_t switch_id, uint32_t port_id){
		  assert(cnt < Settings::MAXHOP);
		  routing_switch[cnt] = switch_id;
		  routing_port[cnt] = port_id;
		  ++cnt;
	  }
	private:
	  uint32_t curr_hop;
	  uint32_t cnt;
	  uint32_t routing_switch[Settings::MAXHOP]; //8 is the maximum hop
	  uint32_t routing_port[Settings::MAXHOP]; //8 is the maximum hop
	};

	// tag scheduled part of flows
	class SchTag: public Tag{
	public:
		SchTag(){
		}
		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::SchTag") .SetParent<Tag> () .AddConstructor<SchTag> ();
		  return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{ return 0;}
		virtual void Serialize (TagBuffer i) const{
		}
		virtual void Deserialize (TagBuffer i){
		}
		virtual void Print (std::ostream &os) const{ }
	private:
	};

	// for Swift
	class NICRxTimeTag : public Tag
	{
	public:
		NICRxTimeTag(){ nicRxT = 0; }
		static TypeId GetTypeId (void){
			static TypeId tid = TypeId ("ns3::NICRxTimeTag") .SetParent<Tag> () .AddConstructor<NICRxTimeTag> ();
			return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint64_t); }
		virtual void Serialize (TagBuffer i) const{ i.WriteU64(nicRxT); }
		virtual void Deserialize (TagBuffer i){ nicRxT = i.ReadU64(); }
		virtual void Print (std::ostream &os) const{ }
		void SetNICRxTime(uint64_t t){nicRxT = t;}
		uint64_t GetNICRxTime(){return nicRxT;}
	private:
		uint64_t nicRxT;
	};

	// for BFC
	class UpstreamTag : public Tag
	{
	public:
		UpstreamTag(){ upstreamQ = 0; }
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::UpstreamTag") .SetParent<Tag> () .AddConstructor<UpstreamTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t); }
	  virtual void Serialize (TagBuffer i) const{ i.WriteU32(upstreamQ); }
	  virtual void Deserialize (TagBuffer i){ upstreamQ = i.ReadU32(); }
	  virtual void Print (std::ostream &os) const{ }
	  void SetUpstreamQ(uint32_t q){upstreamQ = q;}
	  uint32_t GetUpstreamQ(){return upstreamQ;}
	private:
	  uint32_t upstreamQ;
	};

	class MetaTag : public Tag
	{
	public:
		MetaTag(){ counterIncr = 0; }
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::MetaTag") .SetParent<Tag> () .AddConstructor<MetaTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint8_t); }
	  virtual void Serialize (TagBuffer i) const{ i.WriteU8(counterIncr); }
	  virtual void Deserialize (TagBuffer i){ counterIncr = i.ReadU8(); }
	  virtual void Print (std::ostream &os) const{ }
	  void SetCounterIncr(uint8_t q){counterIncr = q;}
	  uint32_t GetCounterIncr(){return counterIncr;}
	private:
	  uint8_t counterIncr;
	};

	// add timestamp to analysis the queueing time
	class QueueingTag : public Tag
	{
	public:
	  QueueingTag(){ lastEnqueue = 0; cnt = 0; }
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::QueueingTag") .SetParent<Tag> () .AddConstructor<QueueingTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint64_t)*(Settings::MAXHOP+2); }
	  virtual void Serialize (TagBuffer i) const{ i.WriteU64(cnt); i.WriteU64(lastEnqueue); for(int k=0;k<Settings::MAXHOP;++k) i.WriteU64(history[k]); }
	  virtual void Deserialize (TagBuffer i){ cnt = i.ReadU64(); lastEnqueue = i.ReadU64(); for(int k=0;k<Settings::MAXHOP;++k) history[k] = i.ReadU64(); }
	  double GetQueueingTimeUs() const{
		  uint64_t total = 0;
		  for(uint64_t k=0;k<cnt;++k) {
			  total += history[k];
		  }
		  return total/1000.0;
	  }
	  uint64_t GetActiveHop() {return cnt;}
	  uint64_t GetHopQueuingTime(uint64_t i) {
		  assert(i < cnt);
		  return history[i];
	  }
	  virtual void Print (std::ostream &os) const{ }
	  inline void Enqueue(){ lastEnqueue = Simulator::Now().GetNanoSeconds(); }
	  inline void Dequeue(){ history[cnt++] = Simulator::Now().GetNanoSeconds() - lastEnqueue; }
	private:
	  uint64_t cnt;
	  uint64_t lastEnqueue;
	  uint64_t history[Settings::MAXHOP]; //8 is the maximum hop
	};

	class UschTag : public Tag
	{
	public:
		UschTag(){ pktsize = 0; }
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::UschTag") .SetParent<Tag> () .AddConstructor<UschTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t); }
	  virtual void Serialize (TagBuffer i) const{ i.WriteU32(pktsize); }
	  virtual void Deserialize (TagBuffer i){ pktsize = i.ReadU32(); }
	  virtual void Print (std::ostream &os) const{ }
	  void SetPktSize(uint32_t size){pktsize = size;}
	  uint32_t GetPktSize(){return pktsize;}
	private:
	  uint32_t pktsize;
	};

	class TokenTag : public Tag
	{
	public:
		TokenTag(){ psn = 0; }
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::TokenTag") .SetParent<Tag> () .AddConstructor<TokenTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t); }
	  virtual void Serialize (TagBuffer i) const{ i.WriteU32(psn); }
	  virtual void Deserialize (TagBuffer i){ psn = i.ReadU32(); }
	  virtual void Print (std::ostream &os) const{ }
	  void SetPSN(uint32_t size){psn = size;}
	  uint32_t GetPSN(){return psn;}
	private:
	  uint32_t psn;
	};

	class QPTag : public Tag
	{
	public:
		QPTag(){ qpid = 0; msgSeq = 0;qpPSN = 0;}
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::QPTag") .SetParent<Tag> () .AddConstructor<QPTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t)*3; }
	  virtual void Serialize (TagBuffer i) const{
		  i.WriteU32(qpid);
		  i.WriteU32(msgSeq);
		  i.WriteU32(qpPSN);
	  }
	  virtual void Deserialize (TagBuffer i){
		  qpid = i.ReadU32();
		  msgSeq = i.ReadU32();
		  qpPSN = i.ReadU32();
	  }
	  virtual void Print (std::ostream &os) const{ }
	  void SetQPID(uint32_t _qpid){qpid = _qpid;}
	  uint32_t GetQPID(){return qpid;}
	  void SetMsgSeq(uint32_t _msgSeq){msgSeq = _msgSeq;}
	  uint32_t GetMsgSeq(){return msgSeq;}
	  void SetQPPSN(uint32_t _qpPSN){qpPSN = _qpPSN;}
	  uint32_t GetQPPSN(){return qpPSN;}
	private:
	  uint32_t qpid;
	  uint32_t msgSeq;
	  uint32_t qpPSN;
	};

	class SwitchIngressPSNTag : public Tag
	{
	public:
		SwitchIngressPSNTag(){ psn = 0; round = 0;}
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::SwitchIngressPSNTag") .SetParent<Tag> () .AddConstructor<SwitchIngressPSNTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t)+sizeof(uint64_t); }
	  virtual void Serialize (TagBuffer i) const{
		  i.WriteU64(psn);
		  i.WriteU32(round);
	  }
	  virtual void Deserialize (TagBuffer i){
		  psn = i.ReadU64();
		  round = i.ReadU32();
	  }
	  virtual void Print (std::ostream &os) const{ }
	  void SetPSN(uint64_t _psn){psn = _psn;}
	  uint64_t GetPSN(){return psn;}
	  void SetRound(uint32_t _round){round = _round;}
	  uint32_t GetRound(){return round;}
	private:
	  uint64_t psn;
	  uint32_t round;
	};

	class SwitchPSNTag : public Tag
	{
	public:
		SwitchPSNTag(){ psn = 0; round = 0;}
	  static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::SwitchPSNTag") .SetParent<Tag> () .AddConstructor<SwitchPSNTag> ();
		  return tid;
	  }
	  virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	  virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t)+sizeof(uint64_t); }
	  virtual void Serialize (TagBuffer i) const{
		  i.WriteU64(psn);
		  i.WriteU32(round);
	  }
	  virtual void Deserialize (TagBuffer i){
		  psn = i.ReadU64();
		  round = i.ReadU32();
	  }
	  virtual void Print (std::ostream &os) const{ }
	  void SetPSN(uint64_t _psn){psn = _psn;}
	  uint64_t GetPSN(){return psn;}
	  void SetRound(uint32_t _round){round = _round;}
	  uint32_t GetRound(){return round;}
	private:
	  uint64_t psn;
	  uint32_t round;
	};

	/*
	 * In ns3, packet only carry header and tag, there are no real payload,
	 * therefore, under switch-accumulate-credit mode, use tag to carry switch-credits' payload.
	 * Note: when use host/switch per-packet-ack mode, only a ackedSize will be carried in header.
	 */
	class SwitchACKTag: public Tag{
	public:
		static uint32_t switch_ack_payload; // related to byte_counter, credit_interval and topology scale;
		static uint32_t switch_ack_credit_bit;
		static uint32_t switch_ack_id_bit;
		static uint32_t max_switchack_size;
		SwitchACKTag(){
			for(uint32_t i = 0; i < Settings::host_num; ++i){
				acked_size[i] = 0;
			}
		}

		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::SwitchACKTag") .SetParent<Tag> () .AddConstructor<SwitchACKTag> ();
		  return tid;
		}

		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{
			if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT)
				return Settings::host_num*sizeof(uint64_t);
			else
				return sizeof(uint64_t);
		}

		uint32_t GetPacketSize(){
			uint32_t res = 0;
			if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
				for (uint32_t i = 0; i < Settings::host_num;++i){
					if (acked_size[i] > 0) res += (switch_ack_credit_bit + switch_ack_id_bit);
				}
				max_switchack_size = std::max(res, max_switchack_size);
			}
			return res;
		}

		virtual void Serialize (TagBuffer b) const{
			if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
				for (uint32_t i = 0; i < Settings::host_num;++i){
					b.WriteU64(acked_size[i]);
				}
			}else{
				b.WriteU64(acked_size[0]);
			}
		}
		virtual void Deserialize (TagBuffer b){
			if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
				for (uint32_t i = 0; i < Settings::host_num;++i){
					acked_size[i] = b.ReadU64();
				}
			}else{
				acked_size[0] = b.ReadU64();
			}
		}
		virtual void Print (std::ostream &os) const{ }
		inline void setAckedSize(uint64_t id){
			acked_size[0] = id;
		}
		inline uint64_t getAckedSize(){
			return acked_size[0];
		}

		inline void SetACKEntry(uint32_t dst, uint64_t size){
			assert(dst < Settings::host_num);
			acked_size[dst] = size;
		}

		inline uint64_t getACKEntry(uint32_t dst){
			assert(dst < Settings::host_num);
			return acked_size[dst];
		}

	private:
		uint64_t acked_size[Settings::NODESCALE];
	};

	/**
	 * When use absolute-psn, upstream should send a syn packet to downstream to avoid drop several continuous packets periodically
	 */
	class SwitchSYNTag: public Tag{
		public:
		static uint32_t max_switchsyn_size;
		static uint32_t switch_psn_bit;
		SwitchSYNTag(){
			for(uint32_t i = 0; i < Settings::host_num; ++i){
				rcv_ack_size[i] = 0;
				nxt_data_size[i] = 0;
			}
		}

		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::SwitchSYNTag") .SetParent<Tag> () .AddConstructor<SwitchSYNTag> ();
		  return tid;
		}

		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{
			return Settings::host_num*sizeof(uint64_t)*2;
		}

		uint32_t GetPacketSize(){
			uint32_t res = 0;
			for (uint32_t i = 0; i < Settings::host_num;++i){
				if (rcv_ack_size[i] > 0) res += (SwitchACKTag::switch_ack_id_bit + 2* switch_psn_bit);
			}
			max_switchsyn_size = std::max(res, max_switchsyn_size);
			return res;
		}

		virtual void Serialize (TagBuffer b) const{
			for (uint32_t i = 0; i < Settings::host_num;++i){
				b.WriteU64(rcv_ack_size[i]);
				b.WriteU64(nxt_data_size[i]);
			}
		}
		virtual void Deserialize (TagBuffer b){
			for (uint32_t i = 0; i < Settings::host_num;++i){
				rcv_ack_size[i] = b.ReadU64();
				nxt_data_size[i] = b.ReadU64();
			}
		}
		virtual void Print (std::ostream &os) const{ }

		inline void SetPSNEntry(uint32_t dst, uint64_t ack, uint64_t data){
			assert(dst < Settings::host_num);
			rcv_ack_size[dst] = ack;
			nxt_data_size[dst] = data;
		}

		inline uint64_t GetACKPSNEntry(uint32_t dst){
			assert(dst < Settings::host_num);
			return rcv_ack_size[dst];
		}

		inline uint64_t GetDataPSNEntry(uint32_t dst){
			assert(dst < Settings::host_num);
			return nxt_data_size[dst];
		}

	private:
		uint64_t rcv_ack_size[Settings::NODESCALE];
		uint64_t nxt_data_size[Settings::NODESCALE];
	};

	/**
	 * Choose the egress at sender-leaf depends on SymmetricRoutingTag
	 * Attention: must work with RecordRoutingTag
	 */
	class SymmetricRoutingTag: public Tag{
	public:
		SymmetricRoutingTag(){
			index = 0;
			for (uint32_t i = 0; i < MAX_HOP; ++i){
				ingress[i] = 0;
			}
		}
		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::SymmetricRoutingTag") .SetParent<Tag> () .AddConstructor<SymmetricRoutingTag> ();
		  return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{ return (MAX_HOP+1)*sizeof(uint32_t);}
		virtual void Serialize (TagBuffer b) const{
			b.WriteU32(index);
			for (uint32_t i = 0; i < MAX_HOP; ++i){
				b.WriteU32(ingress[i]);
			}
		}
		virtual void Deserialize (TagBuffer b){
			index = b.ReadU32();
			for (uint32_t i = 0; i < MAX_HOP; ++i){
				ingress[i] = b.ReadU32();
			}
		}
		virtual void Print (std::ostream &os) const{ }

		inline void setReceiverLeafIngress(uint32_t id){
			assert(index < MAX_HOP);
			ingress[index++] = id;
		}

		inline void resetIndex(){
			index = 0;
		}

		inline void setIndex(uint32_t i){
			index = i;
		}

		inline uint32_t getIndex(){
			return index;
		}

		inline uint32_t getReceiverLeafIngress(){
			uint32_t i = ingress[index];
			++index;
			return i;
		}
	private:
		uint32_t index;
		uint32_t ingress[MAX_HOP];
	};

	/**
	 * Record the ingress of receiver leaf which indicates the passing spine
	 * Attention: must work with SymmetricRoutingTag
	 * (Only for leaf-spine topology for now)
	 */
	class RecordRoutingTag: public Tag{
	public:
		RecordRoutingTag(){
			index = 0;
			for (uint32_t i = 0; i < MAX_HOP; ++i){
				ingress[i] = 0;
			}
		}
		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::RecordRoutingTag") .SetParent<Tag> () .AddConstructor<RecordRoutingTag> ();
		  return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{ return (MAX_HOP+1)*sizeof(uint32_t);}
		virtual void Serialize (TagBuffer b) const{
			b.WriteU32(index);
			for (uint32_t i = 0; i < MAX_HOP; ++i){
				b.WriteU32(ingress[i]);
			}
		}
		virtual void Deserialize (TagBuffer b){
			index = b.ReadU32();
			for (uint32_t i = 0; i < MAX_HOP; ++i){
				ingress[i] = b.ReadU32();
			}
		}
		virtual void Print (std::ostream &os) const{ }
		inline void setReceiverLeafIngress(uint32_t id){
			assert(index < MAX_HOP);
			ingress[index] = id;
			++index;
		}

		inline void resetIndex(){
			index = 0;
		}

		inline uint32_t getReceiverLeafIngress(uint32_t i){
			return ingress[i];
		}
	private:
		uint32_t index;
		uint32_t ingress[MAX_HOP];
	};

	/**
	 * Tag the last data/ack packet
	 */
	class LastPacketTag: public Tag{
	public:
		LastPacketTag(){
		}
		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::LastPacketTag") .SetParent<Tag> () .AddConstructor<LastPacketTag> ();
		  return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{ return 0;}
		virtual void Serialize (TagBuffer i) const{
		}
		virtual void Deserialize (TagBuffer i){
		}
		virtual void Print (std::ostream &os) const{ }
	private:
	};

	class FlowTag: public Tag{
	public:
		FlowTag(){
			index = -1;
		}
		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::FlowTag") .SetParent<Tag> () .AddConstructor<FlowTag> ();
		  return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t);}
		virtual void Serialize (TagBuffer i) const{
			i.WriteU32(index);
		}
		virtual void Deserialize (TagBuffer i){
			index = i.ReadU32();
		}
		inline void setIndex(uint32_t i){
			index = i;
		}

		inline uint32_t getIndex(){
			return index;
		}
		virtual void Print (std::ostream &os) const{ }
	private:
		uint32_t index;
	};

	// tag packets queued in the CQ
	class CQTag: public Tag{
	public:
		CQTag(){
		}
		static TypeId GetTypeId (void){
		  static TypeId tid = TypeId ("ns3::CQTag") .SetParent<Tag> () .AddConstructor<CQTag> ();
		  return tid;
		}
		virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
		virtual uint32_t GetSerializedSize (void) const{ return 0;}
		virtual void Serialize (TagBuffer i) const{
		}
		virtual void Deserialize (TagBuffer i){
		}
		virtual void Print (std::ostream &os) const{ }
	private:
	};

} // namespace ns3

#endif /* DROPTAIL_H */
