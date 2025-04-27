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
#include <iostream>
#include <stdio.h>
#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/simulator.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/broadcom-egress-queue.h"


NS_LOG_COMPONENT_DEFINE("BEgressQueue");

#define DEBUG_MODE 0
#define DEBUG_QUEUE 288

namespace ns3 {

	NS_OBJECT_ENSURE_REGISTERED(BEgressQueue);

	uint32_t SwitchACKTag::switch_ack_payload;
	uint32_t SwitchACKTag::switch_ack_credit_bit;
	uint32_t SwitchACKTag::switch_ack_id_bit;
	uint32_t SwitchACKTag::max_switchack_size = 0;
	uint32_t SwitchSYNTag::switch_psn_bit = 8;		// todo: not correct
	uint32_t SwitchSYNTag::max_switchsyn_size = 0;

	std::vector<uint32_t> BEgressQueue::maxQueueLen;
	uint32_t BEgressQueue::maxPortVOQ = 0;
	uint32_t BEgressQueue::maxPortVOQLen = 0;
	uint32_t BEgressQueue::maxActivePortVOQ = 0;

	// FIFO Queue by default
	uint32_t BEgressQueue::queueType = BEgressQueue::QUEUE_DROPTAIL;

	TypeId BEgressQueue::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::BEgressQueue")
			.SetParent<Queue>()
			.AddConstructor<BEgressQueue>()
			.AddAttribute("MaxBytes",
				"The maximum number of bytes accepted by this BEgressQueue.",
				DoubleValue(1000.0 * 1024 * 1024),
				MakeDoubleAccessor(&BEgressQueue::m_maxBytes),
				MakeDoubleChecker<double>())
			.AddTraceSource ("BeqEnqueue", "Enqueue a packet in the BEgressQueue. Multiple queue",
					MakeTraceSourceAccessor (&BEgressQueue::m_traceBeqEnqueue))
			.AddTraceSource ("BeqDequeue", "Dequeue a packet in the BEgressQueue. Multiple queue",
					MakeTraceSourceAccessor (&BEgressQueue::m_traceBeqDequeue))
			;

		return tid;
	}

	BEgressQueue::BEgressQueue() :
		Queue()
	{
		NS_LOG_FUNCTION_NOARGS();
		m_isLastHop = false;
		m_bytesInQueueTotal = 0;
		m_rate_limiter_mode = 1;
		ResetStatistics();
		InitialHierOQ();
		for (uint32_t i = 0; i < Settings::QUEUENUM; i++)
		{
			m_bytesInQueue.push_back(0);
			if (BEgressQueue::queueType == BEgressQueue::QUEUE_PIFO)
				m_queues.push_back(CreateObject<PIFOQueue>());
			else
				m_queues.push_back(CreateObject<DropTailQueue>());
			m_rate_limiters.push_back(0);
			m_nxt_avails.push_back(Time(0));
			m_rate_limiting.push_back(false);
			m_pfc_ctrls.push_back(false);
		}
		use_hier_q = false;
	}

	/*
	 * Add OQs into hierarchical structure
	 */
	void BEgressQueue::InitialHierOQ(){
		std::vector<uint32_t> level0;
		for (uint32_t i = 0; i < Settings::QUEUENUM; ++i){
			level0.push_back(i);
		}
		hier_Q_map[0] = level0;
	}

	BEgressQueue::~BEgressQueue()
	{
		NS_LOG_FUNCTION_NOARGS();
	}

	uint32_t BEgressQueue::GetTotalReceivedBytes () const{
		return m_nTotalReceivedBytes;
	}

	void
	BEgressQueue::ResetStatistics (void)
	{
	  NS_LOG_FUNCTION (this);
	  m_nTotalReceivedBytes = 0;
	  m_nTotalReceivedPackets = 0;
	  m_nTotalDroppedBytes = 0;
	  m_nTotalDroppedPackets = 0;
	}

	bool
		BEgressQueue::DoEnqueue(Ptr<Packet> p, uint32_t qIndex)
	{
		NS_LOG_FUNCTION(this << p);

		if (m_bytesInQueueTotal + p->GetSize() < m_maxBytes)  //infinite queue
		{
			m_queues[qIndex]->Enqueue(p);
			m_bytesInQueueTotal += p->GetSize();
			m_bytesInQueue[qIndex] += p->GetSize();
			while (maxQueueLen.size() <= qIndex){
				maxQueueLen.push_back(0);
			}
			maxQueueLen[qIndex] = std::max(maxQueueLen[qIndex], m_bytesInQueue[qIndex]);
			if (qIndex >= Settings::QUEUENUM){
				maxPortVOQLen = std::max(maxPortVOQLen, m_bytesInQueue[qIndex]);
				uint32_t active = 0;
				for (uint32_t i = Settings::QUEUENUM; i < m_bytesInQueue.size(); ++i){
					if (m_bytesInQueue[i] > 0) active += 1;
				}
				maxActivePortVOQ = std::max(maxActivePortVOQ, active);
			}
		}
		else
		{
			return false;
		}
		return true;
	}

	// Only RR queues of the first level
	Ptr<Packet>
		BEgressQueue::DoDequeueRR(std::map<uint32_t, bool>& paused, uint8_t pfc_gran, std::map<uint32_t, uint32_t>& ip2id) //this is for switch only
	{
		NS_LOG_FUNCTION(this);

		if (m_bytesInQueueTotal == 0)
		{
			NS_LOG_LOGIC("Queue empty");
			return 0;
		}
		bool found = false;
		uint32_t prio;
		uint32_t qIndex;
		uint32_t rrlast;

		for (prio = 0; !found && prio < m_priorities[0].size(); prio++)	// priority
		{
			for (uint32_t i = 0; !found && i < m_priorities[0][prio].size() ; i++)	// RR in same priority
			{
				qIndex = m_priorities[0][prio][(i + m_rrlasts[0][prio]) % m_priorities[0][prio].size()];
#if DEBUG_MODE
				if (m_queueId == DEBUG_QUEUE)
					std::cout << Simulator::Now() << " " << m_queueId << " qIndex:" << qIndex
						<< " m_queues:" << m_queues[qIndex]->GetNPackets()
						<< " rate_limiter:" << m_rate_limiters[qIndex]
						<< " m_nxt_avails:" << m_nxt_avails[qIndex]
						<< std::endl;
#endif
				if (m_queues[qIndex]->GetNPackets() > 0
						&& (m_rate_limiters[qIndex] == 0 || m_nxt_avails[qIndex] <= Simulator::Now())){		// rate_limiter

//					if (order_blocked_q.find(qIndex) != order_blocked_q.end() && !order_blocked_q[qIndex].empty()) continue;

					if (!m_pfc_ctrls[qIndex]){	// no PFC ctrl
						rrlast = i;
						found = true;
					}else{
						// PFC control
						if (pfc_gran == Settings::PAUSE_PG){	// pause queue, by default
							if (paused.find(qIndex) == paused.end() || !paused[qIndex]){
								rrlast = i;
								found = true;
							}
						}else if (pfc_gran == Settings::PAUSE_DST){	// pause dst, not be used
							CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header);
							m_queues[qIndex]->Peek()->Copy()->PeekHeader(ch);
							if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){
								assert(ip2id.count(ch.dip) > 0);
								if (paused.find(ip2id[ch.dip]) != paused.end() && paused[ip2id[ch.dip]]) continue;
							}
							rrlast = i;
							found = true;
						}else if (pfc_gran == Settings::PAUSE_QP){	// pause qp, not be used
							Ptr<Packet> top = m_queues[qIndex]->Peek()->Copy();
							CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header);
							top->PeekHeader(ch);
							if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){
								QPTag qptag;
								assert(top->PeekPacketTag(qptag));
								if (paused.find(qptag.GetQPID()) != paused.end() && paused[qptag.GetQPID()]) continue;
							}
							rrlast = i;
							found = true;
						}else{
							assert(false);	// conflict settings! With PFC but without PFC granularity
						}
					}
				}
			}
		}

		if (found)
		{
			Ptr<Packet> p = m_queues[qIndex]->Dequeue();
			assert(!!p);
			m_traceBeqDequeue(p, qIndex);
			m_bytesInQueueTotal -= p->GetSize();
			m_bytesInQueue[qIndex] -= p->GetSize();

			if (qIndex != 0){
				m_rrlasts[0][prio] = rrlast;
			}
			m_qlast = qIndex;

#if DEBUG_MODE
			if (m_queueId == DEBUG_QUEUE)
				std::cout << Simulator::Now() << " " << m_queueId << " dequeue qIndex:" << qIndex
					<< " m_queues:" << m_queues[qIndex]->GetNPackets()
					<< " rate_limiter:" << m_rate_limiters[qIndex]
					<< " m_nxt_avails:" << m_nxt_avails[qIndex]
					<< std::endl;
#endif
			// update next available time for rate-limiters
			for (uint32_t i = 0; i < m_rate_limiters.size(); ++i){
				if (m_rate_limiters[i] != 0){
					if (i == qIndex){		// sending this rate limited packet
						if (!m_rate_limiting[i]){	// start rate-limiter
							m_rate_limiting[i] = true;
							m_nxt_avails[i] = Simulator::Now();
						}

						if (m_rate_limiter_mode == 0 || m_rate_limiter_mode == 1)
							m_nxt_avails[i] += Seconds(m_max_rate.CalculateTxTime(p->GetSize()) / m_rate_limiters[i]);
						else
							m_nxt_avails[i] = Simulator::Now() + Seconds(m_max_rate.CalculateTxTime(p->GetSize()) / m_rate_limiters[i]);
					}else if (m_rate_limiter_mode == 1){	// sending other packets
						if (m_bytesInQueue[i] == 0 && m_nxt_avails[i] < Simulator::Now())
							m_nxt_avails[i] = Simulator::Now();
					}
				}
			}
			NS_LOG_LOGIC("Popped " << p);
			NS_LOG_LOGIC("Number bytes " << m_bytesInQueueTotal);

			// dequeue orderMarks behind of normal packets
//			BlockMarkTag blocktag;
//			assert(!p->PeekPacketTag(blocktag));
//			while (m_queues[qIndex]->GetNPackets() > 0){
//				Ptr<Packet> top = m_queues[qIndex]->Peek()->Copy();
//				if (top->PeekPacketTag(blocktag)){
//					m_queues[qIndex]->Dequeue();
//					m_bytesInQueueTotal -= top->GetSize();
//					m_bytesInQueue[qIndex] -= top->GetSize();
//					assert(order_blocked_q.find(blocktag.GetBlockedQ()) != order_blocked_q.end() && order_blocked_q[blocktag.GetBlockedQ()].count(qIndex) > 0);
//					order_blocked_q[blocktag.GetBlockedQ()].erase(qIndex);
//				}else
//					break;
//			}

			return p;
		}

		// no packet send
		if (m_rate_limiter_mode == 1){
			for (uint32_t i = 0; i < m_rate_limiters.size(); ++i){
				if (m_rate_limiters[i] != 0 && m_bytesInQueue[i] == 0 && m_nxt_avails[i] < Simulator::Now()){	// rate-limited queue is free, skip this available time
					m_nxt_avails[i] = Simulator::Now();
				}
			}
		}
		NS_LOG_LOGIC("Nothing can be sent");
		return 0;
	}

	bool
		BEgressQueue::Enqueue(Ptr<Packet> p, uint32_t qIndex)
	{
		NS_LOG_FUNCTION(this << p);
		//
		// If DoEnqueue fails, Queue::Drop is called by the subclass
		//
		bool retval = DoEnqueue(p, qIndex);
		if (retval)
		{
			NS_LOG_LOGIC("m_traceEnqueue (p)");
			m_traceEnqueue(p);
			m_traceBeqEnqueue(p, qIndex);

			uint32_t size = p->GetSize();
			m_nBytes += size;
			m_nTotalReceivedBytes += size;

			m_nPackets++;
			m_nTotalReceivedPackets++;

		}else{
			m_nTotalDroppedBytes += p->GetSize();
			m_nTotalDroppedPackets ++ ;
			std::cout << "BEgressQueue drop!!! qIndex:" << qIndex << std::endl;
		}
		return retval;
	}

	Ptr<Packet>
		BEgressQueue::DequeueRR(std::map<uint32_t, bool>& paused, uint8_t pfc_queue, std::map<uint32_t, uint32_t>& ip2id)
	{
		NS_LOG_FUNCTION(this);
		Ptr<Packet> packet = DoDequeueRR(paused, pfc_queue, ip2id);
		if (packet != 0)
		{
			NS_ASSERT(m_nBytes >= packet->GetSize());
			NS_ASSERT(m_nPackets > 0);
			m_nBytes -= packet->GetSize();
			m_nPackets--;
			NS_LOG_LOGIC("m_traceDequeue (packet)");
			m_traceDequeue(packet);
		}
		return packet;
	}

	bool
		BEgressQueue::DoEnqueue(Ptr<Packet> p)	//for compatiability
	{
		std::cout << "Warning: Call Broadcom queues without priority\n";
		uint32_t qIndex = 0;
		NS_LOG_FUNCTION(this << p);
		if (m_bytesInQueueTotal + p->GetSize() < m_maxBytes)
		{
			m_queues[qIndex]->Enqueue(p);
			m_bytesInQueueTotal += p->GetSize();
			m_bytesInQueue[qIndex] += p->GetSize();
		}
		else
		{
			return false;

		}
		return true;
	}


	Ptr<Packet>
		BEgressQueue::DoDequeue(void)
	{
		NS_ASSERT_MSG(false, "BEgressQueue::DoDequeue not implemented");
		return 0;
	}


	Ptr<const Packet>
		BEgressQueue::DoPeek(void) const	//DoPeek doesn't work for multiple queues!!
	{
		std::cout << "Warning: Call Broadcom queues without priority\n";
		NS_LOG_FUNCTION(this);
		if (m_bytesInQueueTotal == 0)
		{
			NS_LOG_LOGIC("Queue empty");
			return 0;
		}
//		NS_LOG_LOGIC("Number bytes " << m_bytesInQueue);
		return m_queues[0]->Peek();
	}

	uint32_t
		BEgressQueue::GetNQueue() const
	{
		return m_bytesInQueue.size();
	}

	uint32_t
		BEgressQueue::GetNBytes(uint32_t qIndex) const
	{
		return m_bytesInQueue[qIndex];
	}


	uint32_t
		BEgressQueue::GetNBytesTotal() const
	{
		return m_bytesInQueueTotal;
	}

	uint32_t
		BEgressQueue::GetLastQueue()
	{
		return m_qlast;
	}

	/*
	 * when no packets to send, get next token sending time
	 */
	Time BEgressQueue::GetRateLimiterAvail(){
		Time t = Simulator::GetMaximumSimulationTime();
		for (uint32_t i = 0; i < Settings::QUEUENUM; ++i){
			if (m_queues[i]->GetNPackets() > 0 && m_rate_limiters[i] > 0 && m_nxt_avails[i] < t && m_nxt_avails[i] > Simulator::Now()){
				t = m_nxt_avails[i];
			}
		}
		return t;
	}

	uint32_t BEgressQueue::RegisterNewQ(uint32_t level){
		assert(m_bytesInQueue.size() == m_queues.size());
		uint32_t id = m_queues.size();
		m_bytesInQueue.push_back(0);
		m_queues.push_back(CreateObject<DropTailQueue>());
		m_rate_limiters.push_back(0);
		m_nxt_avails.push_back(Time(0));
		m_rate_limiting.push_back(false);
		m_pfc_ctrls.push_back(true);		// use PFC for default
		if (use_hier_q){
			auto it = hier_Q_map.find(level);
			if (it == hier_Q_map.end()){
				std::vector<uint32_t> curr_level;
				curr_level.push_back(id);
				hier_Q_map[level] = curr_level;

				std::vector<std::vector<uint32_t> > level_priorities;
				std::vector<uint32_t> curr_prio;
				curr_prio.push_back(id);
				level_priorities.push_back(curr_prio);
				m_priorities[level] = level_priorities;

				std::vector<uint32_t> curr_rrlast;
				curr_rrlast.push_back(0);
				m_rrlasts[level] = curr_rrlast;
			}else{
				it->second.push_back(id);
				m_priorities[level][m_priorities[level].size()-1].push_back(id);		// voq has lowest priority on this level
			}
		}else{
			m_priorities[0][m_priorities[0].size()-1].push_back(id);		// voq has lowest priority on this level
		}
		maxPortVOQ = std::max(maxPortVOQ, id+1-Settings::QUEUENUM);
		return id;
	}

	/*----------------------------------------DSTPFC_VOQ---------------------------------------------------*/
	uint32_t BEgressQueue::RegisterVOQ(uint32_t dst){
		if (m_dstQ_map.find(dst) != m_dstQ_map.end()) return m_dstQ_map[dst];
		uint32_t id = RegisterNewQ(0);	// has no hierarchical queues
		m_dstQ_map[dst] = id;
		return id;
	}

	/*----------------------------------------Congestion Isolation---------------------------------------------------*/
	uint32_t BEgressQueue::RegisterCIQ(uint16_t level){
		return RegisterNewQ(level);
	}

}
