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
*
* Author: Yuliang Li <yuliangli@g.harvard.com>
*/

#define __STDC_LIMIT_MACROS 1
#include <iostream>
#include "ns3/qbb-net-device.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/data-rate.h"
#include "ns3/object-vector.h"
#include "ns3/pause-header.h"
#include "ns3/drop-tail-queue.h"
#include "ns3/assert.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-header.h"
#include "ns3/simulator.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/qbb-channel.h"
#include "ns3/random-variable.h"
#include "ns3/flow-id-tag.h"
#include "ns3/qbb-header.h"
#include "ns3/error-model.h"
#include "ns3/cn-header.h"
#include "ns3/ppp-header.h"
#include "ns3/udp-header.h"
#include "ns3/seq-ts-header.h"
#include "ns3/pointer.h"
#include "ns3/custom-header.h"
#include "ns3/settings.h"

#define DEBUG_MODE 0
#define DEBUG_NODE 55
#define DEBUG_QP 50710

NS_LOG_COMPONENT_DEFINE("QbbNetDevice");

namespace ns3 {
	
	uint32_t RdmaEgressQueue::ack_q_idx = 3;	// turn on by default
	uint32_t RdmaEgressQueue::max_qp_num = 0;
	// RdmaEgressQueue
	TypeId RdmaEgressQueue::GetTypeId (void)
	{
		static TypeId tid = TypeId ("ns3::RdmaEgressQueue")
			.SetParent<Object> ()
			.AddTraceSource ("RdmaEnqueue", "Enqueue a packet in the RdmaEgressQueue.",
					MakeTraceSourceAccessor (&RdmaEgressQueue::m_traceRdmaEnqueue))
			.AddTraceSource ("RdmaDequeue", "Dequeue a packet in the RdmaEgressQueue.",
					MakeTraceSourceAccessor (&RdmaEgressQueue::m_traceRdmaDequeue))
			;
		return tid;
	}

	RdmaEgressQueue::RdmaEgressQueue(){
		m_rrlast = 0;
		m_rrlastTkn = 0;
		m_qlast = 0;
		m_mtu = 1000;
		m_ackQ = CreateObject<DropTailQueue>();
		m_ackQ->SetAttribute("MaxBytes", UintegerValue(0xffffffff)); // queue limit is on a higher level, not here
		m_tknQ = CreateObject<DropTailQueue>();
		m_tknQ->SetAttribute("MaxBytes", UintegerValue(0xffffffff)); // queue limit is on a higher level, not here
		m_tokenMode = NICTokenNo;
		m_l4TokenMode = L4TokenNo;
		m_rate_limiter_mode = 1;
		m_rate_limiting = false;
		assert(!!m_ackQ);
	}

	Ptr<Packet> RdmaEgressQueue::DequeueQindex(int qIndex, bool is_tkn){
		if (qIndex == -1){ // high prio: ack packets are always assigned the highest priority on NICs.
			Ptr<Packet> p = m_ackQ->Dequeue();
			m_qlast = -1;
			if (Settings::fc_mode == Settings::FC_BFC && Settings::queue_allocate_in_host == 0){
				UpstreamTag upTag;
				QPTag qptag;
				p->PeekPacketTag(qptag);
				upTag.SetUpstreamQ(qptag.GetQPID());
				p->AddPacketTag(upTag);
			}else if(Settings::fc_mode == Settings::FC_BFC && Settings::queue_allocate_in_host == 1){//liuchang: add for ack/nack setting upstreamq in host
				UpstreamTag upTag;
				upTag.SetUpstreamQ(ack_q_idx);
				p->AddPacketTag(upTag);
			}
			m_traceRdmaDequeue(p, 0, is_tkn);
			return p;
		}

		if (qIndex == -2){	// token queue
			Ptr<Packet> p = m_tknQ->Dequeue();
			m_qlast = -2;
			m_traceRdmaDequeue(p, 1, is_tkn);
			return p;
		}

		if (qIndex >= 0){
			Ptr<Packet> p = NULL;
			if (!is_tkn){	// data queue
				p = m_rdmaGetNxtPkt(m_qpGrp->Get(qIndex));
				m_rrlast = qIndex;
				assert(m_qpGrp->Get(qIndex)->is_sndQP);
				m_traceRdmaDequeue(p, DynamicCast<RdmaSndQueuePair>(m_qpGrp->Get(qIndex))->m_pg, is_tkn);

				if (Settings::fc_mode == Settings::FC_BFC){
					UpstreamTag upTag;
					QPTag qptag;
					p->PeekPacketTag(qptag);
					if(Settings::queue_allocate_in_host == 0)
						upTag.SetUpstreamQ(qptag.GetQPID());
					else if(Settings::queue_allocate_in_host == 1)
						upTag.SetUpstreamQ(m_qpGrp->Get(qIndex)->GetQueueId());
					p->AddPacketTag(upTag);
				}


			}else{	// token queue
				p = m_rdmaGetTknPkt(m_qpGrp->Get(qIndex));
				m_rrlastTkn = qIndex;
				m_traceRdmaDequeue(p, 1, is_tkn);
			}
			m_qlast = qIndex;
			return p;
		}
		return 0;
	}

	int RdmaEgressQueue::GetNextQindex(std::map<uint32_t, bool>& paused, uint8_t pfc_gran, bool& is_token){
		is_token = false;
		uint32_t qIndex;
		assert(!!m_ackQ);
		if ((paused.find(ack_q_idx) == paused.end() || !paused[ack_q_idx]) && m_ackQ->GetNPackets() > 0){
			return -1;
		}

		// no pkt in highest priority queue, do rr for each qp
		uint32_t fcount = m_qpGrp->GetN();
		max_qp_num = std::max(max_qp_num, fcount);

		// send token
		switch (m_tokenMode){
		case NICTokenNo:
			break;
		case NICTokenRateBased:
			if (m_nxtTknTime.GetTimeStep() > Simulator::Now().GetTimeStep()) break;
			for (qIndex = 1; qIndex <= fcount; qIndex++){	// RR for token packet secondly (when l4_token_mode == L4TokenDstRateBased/L4TokenSrcRateBased)
				Ptr<RdmaQueuePair> qp = m_qpGrp->Get((qIndex + m_rrlastTkn) % fcount);
				if (m_rdmaCheckToken(qp)){
					is_token = true;
					return (qIndex + m_rrlastTkn) % fcount;
				}
			}
			// time arrivals but no tokens
			if (m_tokenMode == RdmaEgressQueue::NICTokenRateBased && m_rate_limiter_mode == 1){
				m_nxtTknTime = Simulator::Now();
			}
			break;
		}

		// RR for data packet
		for (qIndex = 1; qIndex <= fcount; qIndex++){
			Ptr<RdmaQueuePair> qp = m_qpGrp->Get((qIndex + m_rrlast) % fcount);

			// Check QP type
			if (!qp->is_sndQP) continue;
			Ptr<RdmaSndQueuePair> sndQp = DynamicCast<RdmaSndQueuePair>(qp);	

			// Check L4 logic
			if (!m_rdmaCheckData(sndQp)) continue;
			if (Settings::quota_mode && !m_rdmaCheckUschQuota(sndQp)) continue;	
					
			// Check Flow Control
			bool is_pause = false;
			if (pfc_gran == Settings::PAUSE_SPOT){
				// match each hop
				RoutingTag routingTag = DynamicCast<RdmaSndOperation>(sndQp->Peek())->routingTag;
				for (uint32_t i = 0; i < routingTag.GetActiveHop(); ++i){
					uint32_t root_id = routingTag.GetHopRoutingSwitch(i) * (Settings::switch_num + Settings::host_per_rack) + routingTag.GetHopRoutingPort(i);
					if (paused.find(root_id) != paused.end() && paused[root_id]){
						// match this congestion root
						is_pause = true;
//						break;
					}
					// TODO: match oversubscribed node
				}
			}else{
				uint32_t pfc_id = -1;		// pause a specific priority
				if (pfc_gran == Settings::PAUSE_PG) pfc_id = sndQp->m_pg;
				else if (pfc_gran == Settings::PAUSE_DST) pfc_id = sndQp->m_dst;
				else if (pfc_gran == Settings::PAUSE_QP) pfc_id = sndQp->m_qpid;
				else if (pfc_gran == Settings::PAUSE_QUEUE){
					pfc_id = sndQp->GetQueueId(); //liuchang: added for pause a queue in this port
				}
				else assert(false);		// unsupport pfc granularity
				is_pause = paused.find(pfc_id) != paused.end() && paused[pfc_id];
			}

#if DEBUG_MODE
			if (qp->m_qpid == DEBUG_QP){
				Settings::warning_out << Simulator::Now() << " NICRR QP, is_pause:" << is_pause << " left:" << sndQp->GetBytesLeft() << std::endl;
			}
#endif
			if (!is_pause) return (qIndex + m_rrlast) % fcount;
		}
		return -1024;
	}

	int RdmaEgressQueue::GetLastQueue(){
		return m_qlast;
	}

	uint32_t RdmaEgressQueue::GetFlowCount(void){
		return m_qpGrp->GetN();
	}

	Ptr<RdmaQueuePair> RdmaEgressQueue::GetQp(uint32_t i){
		return m_qpGrp->Get(i);
	}

	void RdmaEgressQueue::EnqueueToken(Ptr<Packet> p){
		m_traceRdmaEnqueue(p, 1);
		m_tknQ->Enqueue(p);
	}

	void RdmaEgressQueue::CleanToken(TracedCallback<Ptr<const Packet>, uint32_t> dropCb){
		while (m_tknQ->GetNPackets() > 0){
			Ptr<Packet> p = m_tknQ->Dequeue();
			dropCb(p, 1);
		}
	}

	void RdmaEgressQueue::EnqueueHighPrioQ(Ptr<Packet> p){
		m_traceRdmaEnqueue(p, 0);
		m_ackQ->Enqueue(p);
	}

	void RdmaEgressQueue::CleanHighPrio(TracedCallback<Ptr<const Packet>, uint32_t> dropCb){
		while (m_ackQ->GetNPackets() > 0){
			Ptr<Packet> p = m_ackQ->Dequeue();
			dropCb(p, 0);
		}
	}

	/******************
	 * QbbNetDevice
	 *****************/
	NS_OBJECT_ENSURE_REGISTERED(QbbNetDevice);

	TypeId
		QbbNetDevice::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::QbbNetDevice")
			.SetParent<PointToPointNetDevice>()
			.AddConstructor<QbbNetDevice>()
			.AddAttribute("QbbEnabled",
				"Enable the generation of PAUSE packet.",
				BooleanValue(true),
				MakeBooleanAccessor(&QbbNetDevice::m_qbbEnabled),
				MakeBooleanChecker())
			.AddAttribute("QcnEnabled",
				"Enable the generation of PAUSE packet.",
				BooleanValue(false),
				MakeBooleanAccessor(&QbbNetDevice::m_qcnEnabled),
				MakeBooleanChecker())
			.AddAttribute("DynamicThreshold",
				"Enable dynamic threshold.",
				BooleanValue(false),
				MakeBooleanAccessor(&QbbNetDevice::m_dynamicth),
				MakeBooleanChecker())
			.AddAttribute("PauseTime",
				"Number of microseconds to pause upon congestion",
				UintegerValue(5),
				MakeUintegerAccessor(&QbbNetDevice::m_pausetime),
				MakeUintegerChecker<uint32_t>())
			.AddAttribute ("TxBeQueue", 
					"A queue to use as the transmit queue in the device.",
					PointerValue (),
					MakePointerAccessor (&QbbNetDevice::m_queue),
					MakePointerChecker<Queue> ())
			.AddAttribute ("RdmaEgressQueue", 
					"A queue to use as the transmit queue in the device.",
					PointerValue (),
					MakePointerAccessor (&QbbNetDevice::m_rdmaEQ),
					MakePointerChecker<Object> ())
			.AddTraceSource ("QbbEnqueue", "Enqueue a packet in the QbbNetDevice.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceEnqueue))
			.AddTraceSource ("QbbDequeue", "Dequeue a packet in the QbbNetDevice.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceDequeue))
			.AddTraceSource ("QbbDrop", "Drop a packet in the QbbNetDevice.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceDrop))
			.AddTraceSource ("RdmaQpDequeue", "A qp dequeue a packet.",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceQpDequeue))
			.AddTraceSource ("QbbPfc", "get a PFC packet. 0: resume, 1: pause",
					MakeTraceSourceAccessor (&QbbNetDevice::m_tracePfc))
			.AddTraceSource ("CISignal", "get a CI signal. 0: pause, 1: resume, 2: dealloc",
					MakeTraceSourceAccessor (&QbbNetDevice::m_traceCI))
			;

		return tid;
	}

	QbbNetDevice::QbbNetDevice()
	{
		NS_LOG_FUNCTION(this);
//		m_ecn_source = new std::vector<ECNAccount>;
//		for (uint32_t i = 0; i < Settings::NICQNUM; i++){
//			m_paused[i] = false;
//		}
		m_pfc_fine = Settings::PAUSE_PG;

		m_rdmaEQ = CreateObject<RdmaEgressQueue>();
		for(int i=0; i<Settings::QUEUENUM; i++){
			m_queue_size.push_back(0);
		}
		assert(!!m_rdmaEQ);
		assert(!!m_rdmaEQ->m_ackQ);
	}

	QbbNetDevice::~QbbNetDevice()
	{
		NS_LOG_FUNCTION(this);
	}

	void
		QbbNetDevice::DoDispose()
	{
		NS_LOG_FUNCTION(this);

		PointToPointNetDevice::DoDispose();
	}

	void
		QbbNetDevice::TransmitComplete(void)
	{
		NS_LOG_FUNCTION(this);
		NS_ASSERT_MSG(m_txMachineState == BUSY, "Must be BUSY if transmitting");
		m_txMachineState = READY;
		NS_ASSERT_MSG(m_currentPkt != 0, "QbbNetDevice::TransmitComplete(): m_currentPkt zero");
		m_phyTxEndTrace(m_currentPkt);
		m_currentPkt = 0;
		DequeueAndTransmit();
	}

	void
		QbbNetDevice::DequeueAndTransmit(void)
	{
		NS_LOG_FUNCTION(this);
//#if DEBUG_MODE
//		if (m_node->m_id == DEBUG_NODE){
//			std::cout << Simulator::Now() << " DequeueAndTransmit busy: "
//					<< (m_txMachineState==BUSY) << " " << m_rdmaEQ << std::endl;
//		}
//#endif
		if (!m_linkUp) return; // if link is down, return
		if (m_txMachineState == BUSY) return;	// Quit if channel busy
		Ptr<Packet> p;
		if (m_node->GetNodeType() == 0){
			bool is_tkn = false;
			assert(!!m_rdmaEQ);
			int qIndex = m_rdmaEQ->GetNextQindex(m_paused, m_pfc_fine, is_tkn);
//
//#if DEBUG_MODE
//			if (m_node->m_id == DEBUG_NODE){
//				std::cout << Simulator::Now() << " DequeueAndTransmit "
//						<< " istkn: " << is_tkn
//						<< " qIndex: " << qIndex << " " << m_rdmaEQ << std::endl;
//			}
//#endif

			if (is_tkn && m_rdmaEQ->m_tokenMode == RdmaEgressQueue::NICTokenRateBased){
				if (!m_rdmaEQ->m_rate_limiting){
					m_rdmaEQ->m_rate_limiting = true;
					m_rdmaEQ->m_nxtTknTime = Simulator::Now();
				}
				if (m_rdmaEQ->m_rate_limiter_mode == 0 || m_rdmaEQ->m_rate_limiter_mode == 1)
					m_rdmaEQ->m_nxtTknTime += Seconds(m_bps.CalculateTxTime(Settings::MTU + 84));
				else
					m_rdmaEQ->m_nxtTknTime = Simulator::Now() + Seconds(m_bps.CalculateTxTime(Settings::MTU + 84));
			}

			if (qIndex != -1024){
				if (qIndex == -1 || qIndex == -2){ // high prio or token
					p = m_rdmaEQ->DequeueQindex(qIndex, is_tkn);
					NICRxTimeTag nicTag;
					if (p->PeekPacketTag(nicTag)){	// pick remoteDelay
						CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
						ch.getInt = 1; // parse INT header
						p->PeekHeader(ch);
						if (ch.l3Prot == CustomHeader::ProtTypeE::ACK || ch.l3Prot == CustomHeader::ProtTypeE::NACK || ch.l3Prot == CustomHeader::ProtTypeE::SACK){
							PppHeader ppp;
							Ipv4Header ip;
							qbbHeader qbb;
							p->RemoveHeader(ppp);
							p->RemoveHeader(ip);
							p->RemoveHeader(qbb);
							qbb.SetRemoteDelay(Simulator::Now().GetNanoSeconds() - nicTag.GetNICRxTime());
							p->AddHeader(qbb);
							p->AddHeader(ip);
							p->AddHeader(ppp);
						}
					}
					m_traceDequeue(p, 0);

				}else if (qIndex >= 0){	// a qp dequeue a packet
					Ptr<RdmaQueuePair> lastQp = m_rdmaEQ->GetQp(qIndex);
					p = m_rdmaEQ->DequeueQindex(qIndex, is_tkn);

					m_traceQpDequeue(p, lastQp, is_tkn);
					// update for the next avail time
					m_rdmaPktSent(lastQp, p, m_tInterframeGap, is_tkn);
				}
				assert(!!p);

				if (Settings::qos_mode != Settings::QOS_NONE){
					// all packet should tag a qos rank
					RankTag r_t;
					if (!p->PeekPacketTag(r_t)){
						r_t.SetRank(0);			// default qos rank: 0
						p->AddPacketTag(r_t);
					}
				}

				TransmitStart(p);	// transmit
			}else { // no packet to send
				NS_LOG_INFO("PAUSE prohibits send at node " << m_node->GetId());
				Time t_data = Simulator::GetMaximumSimulationTime();
				Time t_tkn = Simulator::GetMaximumSimulationTime();
				for (uint32_t i = 0; i < m_rdmaEQ->GetFlowCount(); i++){
					Ptr<RdmaQueuePair> qp = m_rdmaEQ->GetQp(i);
//#if DEBUG_MODE
//					if (m_node->m_id == DEBUG_NODE){
//						std::cout << Simulator::Now() << " RR QP " << qp->m_qpid << " " << qp->is_sndQP
//								<< " " << qp->m_nextTknAvail
//								<< " t_tkn: " << t_tkn << " t_data: " << t_data << std::endl;
//					}
//#endif

					if (qp->is_sndQP){		// sender QP: check both data and token
						Ptr<RdmaSndQueuePair> sndQP = DynamicCast<RdmaSndQueuePair>(qp);
						if (sndQP->GetBytesLeft() == 0)
							continue;
#if DEBUG_MODE
						if (m_node->m_id == DEBUG_NODE && qp->m_qpid == DEBUG_QP){
							Settings::warning_out << Simulator::Now() << " senderQP " << qp->m_qpid << " nxtData:" << sndQP->m_nextAvail << std::endl;
						}
#endif
						if (RdmaEgressQueue::L4TokenNo == m_rdmaEQ->m_l4TokenMode){
							t_data = Min(sndQP->GetNextAvailT(), t_data);		// m_nextAvail only be used under reactive CC
						}else if (m_rdmaEQ->m_l4TokenMode == RdmaEgressQueue::L4TokenSrcRateBased)
							t_tkn = Min(qp->m_nextTknAvail, t_tkn);			// next available l4 generating token time
					}else {		// receiver QP: only check token
						if (m_rdmaEQ->m_l4TokenMode == RdmaEgressQueue::L4TokenDstRateBased || m_rdmaEQ->m_l4TokenMode == RdmaEgressQueue::L4TokenDstDataDriven){
							if (DynamicCast<RdmaRxQueuePair>(qp)->GetUnfinishedNum() == 0)
								continue;
							t_tkn = Min(qp->m_nextTknAvail, t_tkn);			// next available l4 generating token time
						}
					}
				}

//#if DEBUG_MODE
//				if (m_node->m_id == DEBUG_NODE){
//					std::cout << Simulator::Now() << " NetDeivce update next avail 1 " << m_nextSend.GetTs() << " "
//							<< t_tkn << " " << t_data << " " << m_rdmaEQ->m_nxtTknTime << std::endl;
//				}
//#endif

				if (m_rdmaEQ->m_tokenMode == RdmaEgressQueue::NICTokenRateBased)
					t_tkn = Max(t_tkn, m_rdmaEQ->m_nxtTknTime);			// next available nic pulling token time

				Time t = Min(t_tkn, t_data);

//#if DEBUG_MODE
//				if (m_node->m_id == DEBUG_NODE){
//					std::cout << Simulator::Now() << " NetDeivce update next avail 2 " << m_nextSend.GetTs() << " "
//							<< t <<  std::endl;
//				}
//#endif

				if (m_nextSend.IsExpired() && t < Simulator::GetMaximumSimulationTime() && t > Simulator::Now()){
					m_nextSend = Simulator::Schedule(t - Simulator::Now(), &QbbNetDevice::DequeueAndTransmit, this);
//#if DEBUG_MODE
//					if (m_node->m_id == DEBUG_NODE){
//						std::cout << Simulator::Now() << " NetDeivce update next avail 3 " << m_nextSend.GetTs() << " "
//								<< t_tkn << " " << t_data << " " << m_rdmaEQ->m_nxtTknTime << std::endl;
//					}
//#endif
				}
			}
			return;
		}else{   //switch, doesn't care about qcn, just send
			p = m_queue->DequeueRR(m_paused, m_pfc_fine, Settings::hostIp2IdMap);
			if (p != 0){
				m_snifferTrace(p);
				m_promiscSnifferTrace(p);
				Ipv4Header h;
				Ptr<Packet> packet = p->Copy();
				uint16_t protocol = 0;
				ProcessHeader(packet, protocol);
				packet->RemoveHeader(h);
				FlowIdTag t;
				p->RemovePacketTag(t);
				TransmitStart(p);
				uint32_t qIndex = m_queue->GetLastQueue();
				m_node->SwitchNotifyDequeue(m_ifIndex, t.GetFlowId(), qIndex, p);
				m_traceDequeue(p, qIndex);

				//to analysis queuing time
				QueueingTag q;
				if(p->RemovePacketTag(q)){ q.Dequeue(); p->AddPacketTag(q); }

				return;
			}else{ //No queue can deliver any packet
				Time t = m_queue->GetRateLimiterAvail();
				if (t < Simulator::GetMaximumSimulationTime()){ //nothing to send, possibly due to qcn flow control, if so reschedule sending
					if (m_nextSend.IsExpired() && t > Simulator::Now()){
						m_nextSend = Simulator::Schedule(t - Simulator::Now(), &QbbNetDevice::DequeueAndTransmit, this);
					}
				}
			}
		}
		return;
	}

	void
		QbbNetDevice::Resume(unsigned qIndex)
	{
		NS_LOG_FUNCTION(this << qIndex);
		NS_ASSERT_MSG(m_paused.find(qIndex) != m_paused.end() && (Settings::ctrl_all_egress || m_paused[qIndex]), "Must be PAUSEd");
		m_paused[qIndex] = false;
		NS_LOG_INFO("Node " << m_node->GetId() << " dev " << m_ifIndex << " queue " << qIndex <<
			" resumed at " << Simulator::Now().GetSeconds());
		DequeueAndTransmit();
	}

	void
		QbbNetDevice::Receive(Ptr<Packet> packet)
	{
		NS_LOG_FUNCTION(this << packet);
		if (!m_linkUp){
			m_traceDrop(packet, 0);
			return;
		}

		if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt(packet))
		{
			// 
			// If we have an error model and it indicates that it is time to lose a
			// corrupted packet, don't forward this packet up, let it go.
			//
			m_phyRxDropTrace(packet);
			return;
		}

		m_macRxTrace(packet);
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		ch.getInt = 1; // parse INT header
		packet->PeekHeader(ch);

//#if DEBUG_MODE
//		if (DEBUG_NODE == m_node->m_id){
//			uint32_t type = ch.l3Prot;
//			std::cout << Simulator::Now() << " node" << m_node->m_id << " receive packet: "
//					<< type << " " << packet->GetSize() << " " << m_rdmaEQ << std::endl;
//		}
//#endif

		if (ch.l3Prot == CustomHeader::ProtTypeE::PFC){ // PFC
			if (!m_qbbEnabled) return;
			unsigned qIndex = ch.pfc.qIndex;

			// trace
			if (ch.pfc.time > 0){
				m_tracePfc(1, qIndex);
			}else{
				m_tracePfc(0, qIndex);
			}

			if (m_node->GetNodeType() > 0){	// switch
				m_node->SwitchReceivePFC(m_ifIndex, qIndex, ch.pfc.time);
			}else{ // NIC
				if(Settings::fc_mode == Settings::FC_BFC){
					if (ch.pfc.time > 0 && qIndex != RdmaEgressQueue::ack_q_idx){//liuchang: if pause or resume RdmaEgressQueue::ack_q_idx(m_ackQ) ignore it!
						m_paused[qIndex] = true;
					}else if(qIndex != RdmaEgressQueue::ack_q_idx){
						Resume(qIndex);
					}
				}else{
					if (ch.pfc.time > 0){
						m_paused[qIndex] = true;
					}else{
						Resume(qIndex);
					}
				}
#if DEBUG_MODE
		if (DEBUG_NODE == m_node->m_id){
			Settings::warning_out << Simulator::Now() << " node" << m_node->m_id << qIndex << " " << m_paused.size() << " " << m_paused[qIndex] << std::endl;
		}
#endif
			}
		}else { // non-PFC packets (data, ACK, NACK, CNP, Switch-ACK...)
			if (m_node->GetNodeType() > 0){ // switch
				packet->AddPacketTag(FlowIdTag(m_ifIndex));

				// to anlysis queuing time
				QueueingTag q;
				if(packet->RemovePacketTag(q)){q.Enqueue(); packet->AddPacketTag(q); }

				m_node->SwitchReceiveFromDevice(this, packet, ch);
			}else { // NIC
				if (ch.l3Prot == CustomHeader::ProtTypeE::CI){
					CITag citag;
					assert(Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION && packet->PeekPacketTag(citag));
					m_traceCI(citag.GetCIType(), citag.GetRootSwitch(), citag.GetRootPort(), citag.GetOldSwitch(), citag.GetOldPort());
					// TODO: should distinguish with oversubsribed

					// assume that the max number of port is host_per_rack+switch_num
					uint32_t root_id = (Settings::host_per_rack + Settings::switch_num)*citag.GetRootSwitch() + citag.GetRootPort();
					if (citag.IsPause()){
						m_paused[root_id] = true;
					}else if (citag.IsResume()) {
						m_paused[root_id] = false;
						DequeueAndTransmit();
					}else if (citag.IsMerge()){
						uint32_t old_id = (Settings::host_per_rack + Settings::switch_num)*citag.GetOldSwitch() + citag.GetOldPort();
						m_paused[old_id] = false;
						m_paused[root_id] = false;
						DequeueAndTransmit();
					}
#if DEBUG_MODE
				if (DEBUG_NODE == m_node->m_id){
					Settings::warning_out << Simulator::Now() << " receive CI, node" << m_node->m_id << " root_switch:" << citag.GetRootSwitch() << ", root_port:" << citag.GetRootPort()
							<< ", type:" << citag.GetCIType() << ", root_id:" << root_id << ", pause[root_id]:" << m_paused[root_id] << std::endl;
				}
#endif
				}else{
					// send to RdmaHw
					int ret = m_rdmaReceiveCb(packet, ch);
				}
				// TODO we may based on the ret do something
			}
		}
		return;
	}

	bool QbbNetDevice::Send(Ptr<Packet> packet, const Address &dest, uint16_t protocolNumber)
	{
		NS_ASSERT_MSG(false, "QbbNetDevice::Send not implemented yet\n");
		return false;
	}

	bool QbbNetDevice::SwitchSend (uint32_t qIndex, Ptr<Packet> packet, CustomHeader &ch){
		m_macTxTrace(packet);
		m_traceEnqueue(packet, qIndex);
		m_queue->Enqueue(packet, qIndex);
		DequeueAndTransmit();
		return true;
	}

	void QbbNetDevice::SendPfc(uint32_t qIndex, uint32_t type){
		Ptr<Packet> p = Create<Packet>(0);
		PauseHeader pauseh((type == 0 ? m_pausetime : 0), m_queue->GetNBytes(qIndex), qIndex);
		p->AddHeader(pauseh);
		Ipv4Header ipv4h;  // Prepare IPv4 header
		ipv4h.SetProtocol(0xFE);
		ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
		ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
		ipv4h.SetPayloadSize(p->GetSize());
		ipv4h.SetTtl(1);
		ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
		p->AddHeader(ipv4h);
		AddHeader(p, 0x800);
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		SwitchSend(0, p, ch);
	}



	bool
		QbbNetDevice::Attach(Ptr<QbbChannel> ch)
	{
		NS_LOG_FUNCTION(this << &ch);
		m_channel = ch;
		m_channel->Attach(this);
		NotifyLinkUp();
		return true;
	}

	bool
		QbbNetDevice::TransmitStart(Ptr<Packet> p)
	{
		NS_LOG_FUNCTION(this << p);
		NS_LOG_LOGIC("UID is " << p->GetUid() << ")");
		//
		// This function is called to start the process of transmitting a packet.
		// We need to tell the channel that we've started wiggling the wire and
		// schedule an event that will be executed when the transmission is complete.
		//
		NS_ASSERT_MSG(m_txMachineState == READY, "Must be READY to transmit");
		m_txMachineState = BUSY;
		m_currentPkt = p;
		m_phyTxBeginTrace(m_currentPkt);
		Time txTime = Seconds(m_bps.CalculateTxTime(p->GetSize()));
		Time txCompleteTime = txTime + m_tInterframeGap;
		NS_LOG_LOGIC("Schedule TransmitCompleteEvent in " << txCompleteTime.GetSeconds() << "sec");
		Simulator::Schedule(txCompleteTime, &QbbNetDevice::TransmitComplete, this);

		bool result = m_channel->TransmitStart(p, this, txTime);
		if (result == false)
		{
			m_phyTxDropTrace(p);
		}
		return result;
	}

	Ptr<Channel>
		QbbNetDevice::GetChannel(void) const
	{
		return m_channel;
	}

   bool QbbNetDevice::IsQbb(void) const{
	   return true;
   }

   void QbbNetDevice::NewQp(Ptr<RdmaQueuePair> qp){
	   qp->m_nextTknAvail = Simulator::Now();
	   if (qp->is_sndQP)
		   DynamicCast<RdmaSndQueuePair>(qp)->m_nextAvail = Simulator::Now();
	   DequeueAndTransmit();
   }

   void QbbNetDevice::ReassignedQp(Ptr<RdmaSndQueuePair> qp){
	   DequeueAndTransmit();
   }

   void QbbNetDevice::TriggerTransmit(void){
	   DequeueAndTransmit();
   }

	void QbbNetDevice::SetQueue(Ptr<BEgressQueue> q){
		NS_LOG_FUNCTION(this << q);
		m_queue = q;
	}

	Ptr<BEgressQueue> QbbNetDevice::GetQueue(){
		return m_queue;
	}

	Ptr<RdmaEgressQueue> QbbNetDevice::GetRdmaQueue(){
		return m_rdmaEQ;
	}

	void QbbNetDevice::RdmaEnqueueHighPrioQ(Ptr<Packet> p){
		m_traceEnqueue(p, 0);
		m_rdmaEQ->EnqueueHighPrioQ(p);
	}

	void QbbNetDevice::TakeDown(){
		// TODO: delete packets in the queue, set link down
		if (m_node->GetNodeType() == 0){
			// clean the high prio queue
			m_rdmaEQ->CleanHighPrio(m_traceDrop);
			// notify driver/RdmaHw that this link is down
			m_rdmaLinkDownCb(this);
		}else { // switch
			// clean the queue
			m_paused.clear();
			while (1){
				Ptr<Packet> p = m_queue->DequeueRR(m_paused, m_pfc_fine, Settings::hostIp2IdMap);
				if (p == 0)
					 break;
				m_traceDrop(p, m_queue->GetLastQueue());
			}
			// TODO: Notify switch that this link is down
		}
		m_linkUp = false;
	}

	/**
	 * When m_rate of CC changes, update next available time
	 */
	void QbbNetDevice::UpdateNextAvail(Time t){
		if (m_rdmaEQ->m_tokenMode == RdmaEgressQueue::NICTokenRateBased)
			t = Max(m_rdmaEQ->m_nxtTknTime, t);
		if (!m_nextSend.IsExpired() && t < m_nextSend.GetTs() && t > Simulator::Now()){
			Simulator::Cancel(m_nextSend);
			Time delta = t < Simulator::Now() ? Time(0) : t - Simulator::Now();
			m_nextSend = Simulator::Schedule(delta, &QbbNetDevice::DequeueAndTransmit, this);
		}
//#if DEBUG_MODE
//		if (m_node->m_id == DEBUG_NODE){
//			std::cout << Simulator::Now() << " Netdevice UpdateNextAvail " << m_nextSend.GetTs() << " " << t << std::endl;
//		}
//#endif
	}

	/*----------------------------------Congestion Isolation-----------------------------------*/
	Ptr<Packet> QbbNetDevice::GetCIPacket(uint32_t root_switch, uint32_t root_port, uint8_t type, uint16_t hop, uint32_t old_switch, uint32_t old_port){
		Ptr<Packet> p = Create<Packet>(0);
		Ipv4Header ipv4h;  // Prepare IPv4 header
		ipv4h.SetProtocol(CustomHeader::ProtTypeE::CI);
		ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
		ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
		ipv4h.SetPayloadSize(p->GetSize());
		ipv4h.SetTtl(1);
		ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
		p->AddHeader(ipv4h);
		AddHeader(p, 0x800);
		CITag citag;
		citag.SetCI(root_switch, root_port, type);
		if (type == CITag::CI_MERGE){
			citag.SetOldRoot(old_switch, old_port);
		}
		if (hop > 0){
			citag.SetHop(hop);
		}
		p->AddPacketTag(citag);
		return p;
	}

	void QbbNetDevice::SendCIPacket(uint32_t root_switch, uint32_t root_port, uint8_t type, uint16_t hop, uint32_t old_switch, uint32_t old_port){
		Ptr<Packet> p = GetCIPacket(root_switch, root_port, type, hop, old_switch, old_port);
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		SwitchSend(0, p, ch);
	}

	/*
	 * Push a OrderMark in q with qIndex,
	 * and blocked_q will be blocked until the OrderMark in q with qIndex dequeues.
	 */
	void QbbNetDevice::PushOrderMark(uint32_t qIndex, uint32_t blocked_q){
		if (m_queue->GetNBytes(qIndex) == 0) return;
		Ptr<Packet> p = Create<Packet>(0);
		AddHeader(p, 0x800);
		BlockMarkTag blocktag;
		blocktag.SetBlockedQ(blocked_q);
		p->AddPacketTag(blocktag);
		m_queue->Enqueue(p, qIndex);
		m_queue->order_blocked_q[blocked_q].insert(qIndex);
	}

	/*----------------------------------Floodgate-----------------------------------*/
	void QbbNetDevice::SendSwitchACK(SwitchACKTag acktag, uint32_t src, uint32_t dst){
		assert(Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT || Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT);
		Ptr<Packet> p = GetSwitchACKPacket(acktag, src, dst);
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		SwitchSend(0, p, ch);
	}

	Ptr<Packet> QbbNetDevice::GetSwitchACKPacket(SwitchACKTag acktag, uint32_t src, uint32_t dst){
		assert(Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT || Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT);

		uint32_t sz = std::min(Settings::packet_payload, acktag.GetPacketSize());	// todo: when acktag's size > MTU, should split packet
		Ptr<Packet> p = Create<Packet>(sz);
		Ipv4Header ipv4h;  // Prepare IPv4 header
		ipv4h.SetProtocol(0xFB); // Switch-ACK, has no L4 header

		if (src == 0){
			assert(Settings::switch_ack_mode != Settings::SWITCH_DST_CREDIT); // under SWITCH_DST_CREDIT, switchACKTag does not carry dst info and src will be used as dst when recovery window
			ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
		}else
			ipv4h.SetSource(Ipv4Address(src));

		if (dst == 0){
			assert(!Settings::reset_only_ToR_switch_win);
			ipv4h.SetDestination(Ipv4Address("255.255.255.255"));	// dst address has no sense
		}else
			ipv4h.SetDestination(Ipv4Address(dst));

		if (Settings::reset_only_ToR_switch_win)
			ipv4h.SetTtl(64);
		else
			ipv4h.SetTtl(1);

		ipv4h.SetPayloadSize(p->GetSize());
		ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
		p->AddHeader(ipv4h);
		AddHeader(p, 0x800);
		p->AddPacketTag(acktag);

		// qpid may use for routing when settings::ecmp_unit == Settings::ECMP_QP
		QPTag qptag;
		qptag.SetQPID(rand());
		p->AddPacketTag(qptag);
		return p;
	}

	Ptr<Packet> QbbNetDevice::GetSwitchSYNPacket(SwitchSYNTag syntag, uint32_t src, uint32_t dst){
		assert(Settings::switch_absolute_psn && Settings::switch_syn_timeout_us);

		uint32_t sz = std::min(Settings::packet_payload, syntag.GetPacketSize());	// todo: when tag's size > MTU, should split packet
		Ptr<Packet> p = Create<Packet>(sz);
		Ipv4Header ipv4h;  // Prepare IPv4 header
		ipv4h.SetProtocol(0xFA); // Switch-ACK, no L4 header

		if (src == 0){
			ipv4h.SetSource(m_node->GetObject<Ipv4>()->GetAddress(m_ifIndex, 0).GetLocal());
		}else
			ipv4h.SetSource(Ipv4Address(src));

		if (dst == 0){
			assert(!Settings::reset_only_ToR_switch_win);
			ipv4h.SetDestination(Ipv4Address("255.255.255.255"));
		}else
			ipv4h.SetDestination(Ipv4Address(dst));

		if (Settings::reset_only_ToR_switch_win)
			ipv4h.SetTtl(64);
		else
			ipv4h.SetTtl(1);

		ipv4h.SetPayloadSize(p->GetSize());
		ipv4h.SetIdentification(UniformVariable(0, 65536).GetValue());
		p->AddHeader(ipv4h);
		AddHeader(p, 0x800);
		p->AddPacketTag(syntag);

		// qpid may use for routing when settings::ecmp_unit == Settings::ECMP_QP
		QPTag qptag;
		qptag.SetQPID(rand());
		p->AddPacketTag(qptag);
		return p;
	}

	void QbbNetDevice::SendSwitchSYN(SwitchSYNTag syntag, uint32_t src, uint32_t dst){
		assert(Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT || Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT);
		Ptr<Packet> p = GetSwitchSYNPacket(syntag, src, dst);
		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		SwitchSend(0, p, ch);
	}
	int QbbNetDevice::GetQueueforFlow(){
		for(int i=1; i<m_queue_size.size(); i++){
			if(m_queue_size[i] == 0){
				return i;
			}
		}
		int idx = rand() % (Settings::QUEUENUM-1) + 1; //>=1
		return idx;
	}
	bool QbbNetDevice::UpdateQueueforFlow(int idx, bool ad){
		if(ad == true){//is add option
			m_queue_size[idx]++;
			return true;
		}else{
			assert(m_queue_size[idx] > 0);
			m_queue_size[idx]--;
			return true;
		}
		return false;
	}
} // namespace ns3
