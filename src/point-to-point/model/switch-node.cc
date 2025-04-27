#include "ns3/ipv4.h"
#include "ns3/packet.h"
#include "ns3/ipv4-header.h"
#include "ns3/pause-header.h"
#include "ns3/flow-id-tag.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/switch-node.h"
#include "ns3/qbb-net-device.h"
#include "ns3/ppp-header.h"
#include "ns3/int-header.h"
#include "ns3/settings.h"
#include "ns3/broadcom-egress-queue.h"
#include "ns3/log.h"
#include "ns3/random-variable.h"
#include <ns3/seq-ts-header.h>
#include <assert.h>
#include <unordered_map>

#define DEBUG_MODE 0
#define DEBUG_MODE_QP 0
#define DEBUG_MODE_DEV 0
#define DEBUG_QP 50710
#define DEBUG_DST_ID 0
#define DEBUG_SWITCH 162
#define DEBUG_INDEV 12
#define DEBUG_EGRESS 17

namespace ns3 {

TypeId SwitchNode::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::SwitchNode")
    .SetParent<Node> ()
    .AddConstructor<SwitchNode> ()
	.AddAttribute("EcnEnabled",
			"Enable ECN marking.",
			BooleanValue(false),
			MakeBooleanAccessor(&SwitchNode::m_ecnEnabled),
			MakeBooleanChecker())
	.AddAttribute("CcMode",
			"CC mode.",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ccMode),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("AckHighPrio",
			"Set high priority for ACK/NACK or not",
			UintegerValue(0),
			MakeUintegerAccessor(&SwitchNode::m_ackHighPrio),
			MakeUintegerChecker<uint32_t>())
	.AddAttribute("OutputRealTimeBuffer",
			"Output realTime buffer.",
			BooleanValue(true),
			MakeBooleanAccessor(&SwitchNode::output_realtime_buffer),
			MakeBooleanChecker())
	.AddTraceSource ("RealtimeQueueLength", "print realtime queue length",
			MakeTraceSourceAccessor (&SwitchNode::m_traceRealtimeQueue))
	.AddTraceSource ("RealtimeSwitchBw", "print realtime switch throughput",
			MakeTraceSourceAccessor (&SwitchNode::m_traceRealtimeSwitchBw))
  ;
  return tid;
}

SwitchNode::SwitchNode(){
	m_node_type = 1;
	m_isToR = false;
	m_isCore = false;
	m_mmu = CreateObject<SwitchMmu>();
	m_mmu->m_ecmpSeed = 1;
	m_mmu->m_bfcSeed = 1;		// use the same hash function for each switch
	m_mmu->m_creditIngressTimerCallback = MakeCallback(&SwitchNode::SendAccSwitchACK, this);
	m_mmu->m_creditDstTimerCallback = MakeCallback(&SwitchNode::SendSwitchACK, this);
	m_mmu->m_synTimerCallback = MakeCallback(&SwitchNode::SendSYN, this);
	m_drill_candidate = 2;	// for drill: power of two

	for (uint32_t i = 0; i < Settings::PORTNUM; i++)
		for (uint32_t j = 0; j < Settings::PORTNUM; j++)
			for (uint32_t k = 0; k < Settings::QUEUENUM; k++){
				m_bytes[i][j].push_back(0);
			}
	for (uint32_t i = 0; i < Settings::PORTNUM; i++)
		m_txBytes[i] = 0;

	Simulator::Schedule(Seconds(2), &SwitchNode::PrintQlength, this);
}

void SwitchNode::CalcuUpPortsNum(){
	m_mmu->up_port_num = 0;
	for (uint32_t i = 0; i < m_devices.size(); i++){
		Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[i]);
		if (!!device && Settings::up_port[m_id-Settings::host_num][i]){
			m_mmu->up_port_num += 1;
		}
	}
}

uint32_t SwitchNode::MatchDefaultQ(const CustomHeader &ch){
	uint32_t qIndex;
	if (ch.l3Prot == CustomHeader::ProtTypeE::SWITCH_PSN || ch.l3Prot == CustomHeader::ProtTypeE::SWITCH_ACK
			|| ch.l3Prot == CustomHeader::ProtTypeE::QCN || ch.l3Prot == CustomHeader::ProtTypeE::PFC
			|| ch.l3Prot == CustomHeader::ProtTypeE::REQ
			|| (m_ackHighPrio && (ch.l3Prot == CustomHeader::ProtTypeE::NACK || ch.l3Prot == CustomHeader::ProtTypeE::ACK || ch.l3Prot == CustomHeader::ProtTypeE::SACK))){  // Switch-PSN or Switch-ACK or QCN or PFC or ACK/NACK ot TOKEN_ACK, go highest priority
		qIndex = 0;
	}else if(ch.l3Prot == CustomHeader::ProtTypeE::TOKEN || ch.l3Prot == CustomHeader::ProtTypeE::TOKEN_ACK){
		qIndex = ch.tkn.pg;
	}else if (ch.l3Prot == CustomHeader::ProtTypeE::NACK || ch.l3Prot == CustomHeader::ProtTypeE::ACK || ch.l3Prot == CustomHeader::ProtTypeE::SACK){
		qIndex = ch.tkn.pg;
	}else{
		qIndex = (ch.l3Prot == CustomHeader::ProtTypeE::TCP ? 1 : ch.udp.pg); // if TCP, put to queue 1
	}
	return qIndex;
}

/*
 * When a port is receiving a packet, match it into an output Q
 */
uint32_t SwitchNode::MatchQ(uint32_t inDev, uint32_t idx, CustomHeader &ch, Ptr<Packet> p){
	uint32_t qIndex = MatchDefaultQ(ch);
	Ptr<QbbNetDevice> out_dev = DynamicCast<QbbNetDevice>(m_devices[idx]);
	Ptr<QbbNetDevice> in_dev = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	// re-determine Q for packets
	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
		if (qIndex != 0){
			/*
			 * check whether this packet will reach a congestion root
			 * -> if so, isolate this packet in corresponding Q
			 */
			RoutingTag routingTag;
			assert(p->PeekPacketTag(routingTag));

			uint32_t root_switch, root_port;
			// match farther hops with congestion roots
			Ptr<CAMEntry> matched_entry = m_mmu->MatchNextCongestionRoot(idx, routingTag, -1, root_switch, root_port);
			if (!!matched_entry){
				qIndex = matched_entry->oport_ciq[idx];
			}

#if DEBUG_MODE_QP
			QPTag qptag;
			if (ch.l3Prot == CustomHeader::ProtTypeE::UDP && p->PeekPacketTag(qptag) && DEBUG_QP == qptag.GetQPID()){
				Settings::warning_out << Simulator::Now() << " node" << m_id << " enqueue data packet(seq" << qptag.GetQPPSN() << ") to Q" << qIndex
						<< " qLen:" << out_dev->m_queue->GetNBytes(qIndex) << std::endl;
				if (out_dev->m_queue->order_blocked_q.find(qIndex) != out_dev->m_queue->order_blocked_q.end()){
					Settings::warning_out << "blocked by:";
					for (auto it = out_dev->m_queue->order_blocked_q[qIndex].begin(); it != out_dev->m_queue->order_blocked_q[qIndex].end(); it++)
						Settings::warning_out << *it << ", ";
					Settings::warning_out << std::endl;
				}
			}
#endif
		}
	}else if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
		if (qIndex != 0){	// it is not packet with hightest priority
			// check unsch part or sch part
			SchTag schTag;
			if (p->PeekPacketTag(schTag)){	// sch part
				qIndex = 2;
			}else{		// unsch part / ack(when !m_ackHighPrio)
				qIndex = 1;
			}
			// it is not the last hop(DToR) -> has VOQ, check whether packet should be enqueued into VOQ
			if (!out_dev->m_queue->m_isLastHop){
				uint32_t dst = Settings::hostIp2IdMap[ch.dip];
				if (out_dev->m_queue->m_dstQ_map.find(dst) != out_dev->m_queue->m_dstQ_map.end()){	// has this VOQ
					uint32_t voqid = out_dev->m_queue->m_dstQ_map[dst];
					assert(out_dev->m_queue->m_bytesInQueue.size() > voqid);
					if ((out_dev->m_paused.find(voqid) != out_dev->m_paused.end() && out_dev->m_paused[voqid]) // voq has been paused
							|| out_dev->m_queue->m_bytesInQueue[voqid])		// or there are packets in voq
					qIndex = out_dev->m_queue->m_dstQ_map[dst];
				}
			}
		}
	}else if (Settings::FC_BFC == Settings::fc_mode){
		if (qIndex != 0){	// it is not packet with hightest priority
			FlowTag ftag;
			assert(p->PeekPacketTag(ftag));
			uint32_t fid = m_mmu->GetHashFID(ch, m_devices.size(), ftag.getIndex());
			bool reassign_q = false;
			if (m_mmu->m_flow_tb[idx].find(fid) == m_mmu->m_flow_tb[idx].end()){
				SwitchMmu::FlowEntry curr;
				curr.size = 0;
				curr.time = Time(0);
				curr.in_dev = inDev;
				m_mmu->m_flow_tb[idx][fid] = curr;
				reassign_q = true;

				// for statistic
				if (m_mmu->m_fid_flows.find(fid) == m_mmu->m_fid_flows.end()){
					std::set<uint32_t> curr;
					m_mmu->m_fid_flows[fid] = curr;
				}
				if (m_mmu->m_fid_flows[fid].count(ftag.getIndex()) < 1){
					m_mmu->m_fid_flows[fid].insert(ftag.getIndex());
					SwitchMmu::max_fid_conflict = std::max(SwitchMmu::max_fid_conflict, uint32_t(m_mmu->m_fid_flows[fid].size()));
				}
			}else{
				if (m_mmu->m_flow_tb[idx][fid].size == 0
						&& (Simulator::Now() - m_mmu->m_flow_tb[idx][fid].time) > Seconds(Settings::sticky_th * 2 * m_mmu->m_delay_map[inDev]))
					reassign_q = true;
				else
					qIndex = m_mmu->m_flow_tb[idx][fid].q_assign;
			}
			if (reassign_q){
				qIndex = m_mmu->GetUsableQ(idx, out_dev->m_paused);		// use usable Q
				if (qIndex >= Settings::QUEUENUM){
					// no usable queues --> select a random Q
					qIndex = rand()%(Settings::QUEUENUM-1)+1;	// >= 1
				}
				m_mmu->m_flow_tb[idx][fid].q_assign = qIndex;
			}
		}
	}
	return qIndex;
}

void SwitchNode::SendToDev(Ptr<Packet>p, CustomHeader &ch){

	// call ResetQueueStatitics if necessary
	if (!m_eventResetQueueStatitics.IsRunning()) ResetQueueStatisticsInterval();

	FlowIdTag t;
	p->PeekPacketTag(t);
	uint32_t inDev = t.GetFlowId();

	// update statistics
	if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){
		m_mmu->m_rcv_data[inDev] += p->GetSize();
	}else{
		m_mmu->m_rcv_ctrl[inDev] += p->GetSize();
		if (ch.l3Prot == CustomHeader::ProtTypeE::SWITCH_ACK){
			m_mmu->m_rcv_switchACK[inDev] += p->GetSize();
		}else if (ch.l3Prot == CustomHeader::ProtTypeE::TOKEN){
			m_mmu->m_rcv_tkn[inDev] += p->GetSize();
		}else if (ch.l3Prot == CustomHeader::ProtTypeE::TOKEN_ACK){
			m_mmu->m_rcv_tknack[inDev] += p->GetSize();
		}
	}

	CITag citag;
	if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION && p->PeekPacketTag(citag)){
		ReceiveCI(citag, inDev);
		return;	// per-hop control signal
	}

	SwitchSYNTag syntag;
	if (m_mmu->m_use_switchwin && p->PeekPacketTag(syntag)){
		ReceiveSYN(syntag, ch, inDev);
		return;
	}

	SwitchPSNTag psntag;
	if (Settings::switch_absolute_psn && p->PeekPacketTag(psntag)){

		// remove and copy PSNTag as IngressPSNTag
		p->RemovePacketTag(psntag);
		SwitchIngressPSNTag ingress_psntag;
		ingress_psntag.SetPSN(psntag.GetPSN());
		p->AddPacketTag(ingress_psntag);

		// update rcv_data_psn
		uint64_t data_psn = psntag.GetPSN();
		uint32_t dst_id = Settings::hostIp2IdMap[ch.dip];
		uint32_t tmp = inDev;
		if (Settings::reset_only_ToR_switch_win) tmp = Settings::hostIp2IdMap[ch.sip]/Settings::host_per_rack;
		if (data_psn > m_mmu->m_rcv_data_psn[tmp][dst_id]){
			m_mmu->m_rcv_data_psn[tmp][dst_id] = data_psn;
		}
	}

	SwitchACKTag acktag;
	if (m_mmu->m_use_switchwin && p->PeekPacketTag(acktag)){
		if (ResumeVOQWin(acktag, ch, inDev)) return;
	}

	int idx = GetOutDev(p, ch);
	if (idx >= 0){
		NS_ASSERT_MSG(m_devices[idx]->IsLinkUp(), "The routing table look up should return link that is up");

		Ptr<QbbNetDevice> out_dev = DynamicCast<QbbNetDevice>(m_devices[idx]);
		Ptr<QbbNetDevice> in_dev = DynamicCast<QbbNetDevice>(m_devices[inDev]);
		assert(!!in_dev);

		// determine the qIndex
		uint32_t qIndex = MatchQ(inDev, idx, ch, p);
		while (qIndex >= Settings::total_packets.size()){
			Settings::total_packets.push_back(0);
		}
		++Settings::total_packets[qIndex];

		uint32_t pfc_fine = qIndex;
		if (m_mmu->pfc_mode[inDev] == Settings::PAUSE_DST) pfc_fine = Settings::hostIp2IdMap[ch.dip];
		// Admission control
		if ((qIndex >= Settings::QUEUENUM || !m_mmu->pfc_ingress_ctrls[qIndex] || m_mmu->CheckIngressAdmission(inDev, pfc_fine, p->GetSize()))	// PFC control
				&& m_mmu->CheckEgressAdmission(idx, qIndex, p->GetSize())		// egress qlength control
				&& (!Settings::check_shared_buffer || m_mmu->buffer_size > p->GetSize() + DoStatistics())	// shared buffer control
			){
			if (qIndex < Settings::QUEUENUM && m_mmu->pfc_ingress_ctrls[qIndex])
				m_mmu->UpdateIngressAdmission(inDev, pfc_fine, p->GetSize());
			assert(ch.l3Prot != CustomHeader::ProtTypeE::SWITCH_ACK);
			m_mmu->UpdateEgressAdmission(idx, qIndex, p->GetSize());
		}else if (Settings::fc_mode == Settings::FC_PFCoff && (ch.l3Prot == CustomHeader::ProtTypeE::ACK || ch.l3Prot == CustomHeader::ProtTypeE::SACK)){// when turn off PFC, don't drop ACK or SACK
			m_mmu->UpdateEgressAdmission(idx, qIndex, p->GetSize());
		}else{
			while (qIndex >= Settings::drop_packets.size()){
				Settings::drop_packets.push_back(0);
			}
			++Settings::drop_packets[qIndex];
			Settings::warning_out << Simulator::Now() << " node-" << m_id << " drop packet! qIndex:" << qIndex << " pktsize:" << p->GetSize() << " l3Port:" << ch.l3Prot << std::endl;
			return; // Drop
		}

		/**
		 * When use Floodgate
		 * --> if it's a data packet,
		 * --> only when has enough remaining window,
		 * --> send this packet to egress port.
		 */
		if (Settings::fc_mode == Settings::FC_FLOODGATE){
			if (m_mmu->m_use_switchwin && !CheckVOQWin(p, ch, inDev, idx, qIndex))
				return;
		}

		while (m_bytes[inDev][idx].size() <= qIndex){
			m_bytes[inDev][idx].push_back(0);
		}
		m_bytes[inDev][idx][qIndex] += p->GetSize();

		// PFC: check to send pause
		if (qIndex < Settings::QUEUENUM && m_mmu->pfc_ingress_ctrls[qIndex])
			CheckAndSendPfc(inDev, pfc_fine);

		if (Settings::hier_Q && Settings::QUEUENUM <= qIndex && !m_eventDoHierRR.IsRunning()){
			m_eventDoHierRR = Simulator::Schedule(Seconds(0), &SwitchNode::DoHierQRR, this);
		}

		if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
			NotifyEnqueue_CI(inDev, idx, qIndex, p);
			//for hpcc, packets pushed into CQs will carry a CQTag
			//it is used for identify congested/uncongested flow when dequeuing OQ
			if(qIndex >= Settings::QUEUENUM && Settings::cc_mode == Settings::CC_HPCC){
				CQTag cqtag;
				if(!p->PeekPacketTag(cqtag)){
					p->AddPacketTag(cqtag);
				}
			}
		}else if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
			// check to send pause
			if (qIndex != 0){
				m_mmu->m_buffering_bytes[Settings::hostIp2IdMap[ch.dip]] += p->GetSize();
				m_mmu->m_buffering_egress_bytes[idx][Settings::hostIp2IdMap[ch.dip]] += p->GetSize();
				uint32_t dst = Settings::hostIp2IdMap[ch.dip];
				if (!m_mmu->paused[inDev][dst] && m_mmu->CheckVOQPause(out_dev->m_queue->m_isLastHop, inDev, idx, qIndex, dst)){
					in_dev->SendPfc(dst, 0);
					m_mmu->SetPause(inDev, dst);
				}
			}
		}else if (Settings::fc_mode == Settings::FC_BFC){
			// check to send pause
			if (qIndex != 0){
				// update flow_table
				FlowTag ftag;
				assert(p->PeekPacketTag(ftag));
				uint32_t fid = m_mmu->GetHashFID(ch, m_devices.size(), ftag.getIndex());
				assert(m_mmu->m_flow_tb[idx].find(fid) != m_mmu->m_flow_tb[idx].end());
				m_mmu->m_flow_tb[idx][fid].size += 1;
				m_mmu->m_flow_tb[idx][fid].time = Simulator::Now();
				m_mmu->m_flow_tb[idx][fid].in_dev = inDev;
				assert(inDev < m_devices.size());
				// check and send pause
				MetaTag metaTag;
				UpstreamTag upTag;
				assert(p->PeekPacketTag(upTag));
				if (m_mmu->CheckBFCPause(inDev, idx, fid, out_dev->m_paused)){
					// set metadata
					metaTag.SetCounterIncr(1);
					// update pauseCounter and check to send pause
					if (m_mmu->m_pause_counter[inDev].find(upTag.GetUpstreamQ()) == m_mmu->m_pause_counter[inDev].end()){
						m_mmu->m_pause_counter[inDev][upTag.GetUpstreamQ()] = 0;
					}
					m_mmu->m_pause_counter[inDev][upTag.GetUpstreamQ()] += 1;
					if (1 == m_mmu->m_pause_counter[inDev][upTag.GetUpstreamQ()]){ // send pause
						in_dev->SendPfc(upTag.GetUpstreamQ(), 0);
					}
				}
				p->AddPacketTag(metaTag);
			}
		}

		DoSwitchSend(p, ch, idx, qIndex);

		DoStatistics();
	}
}

void SwitchNode::DoSwitchSend(Ptr<Packet>p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex){
	m_devices[outDev]->SwitchSend(qIndex, p, ch);
}

/**
 * When receive packets(except PFC frames)
 */
bool SwitchNode::SwitchReceiveFromDevice(Ptr<NetDevice> device, Ptr<Packet> packet, CustomHeader &ch){
	SendToDev(packet, ch);
	return true;
}

void SwitchNode::SwitchNotifyDequeue(uint32_t ifIndex, uint32_t inDev, uint32_t qIndex, Ptr<Packet> p){
	CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
	ch.getInt = 1; // parse INT header
	p->PeekHeader(ch);
#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == m_id && DEBUG_EGRESS == ifIndex){
			Settings::warning_out << Simulator::Now() << " dequeue packet " << m_id << "-" << ifIndex << ":" << ch.l3Prot << std::endl;
		}
#endif
	// update statistics
	m_txBytes[ifIndex] += p->GetSize();
	if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){
		m_mmu->m_tx_data[ifIndex] += p->GetSize();
	}else {
		m_mmu->m_tx_ctrl[ifIndex] += p->GetSize();
		if (ch.l3Prot == CustomHeader::ProtTypeE::SWITCH_ACK){
			m_mmu->m_tx_switchACK[ifIndex] += p->GetSize();
			return;
		}else if (ch.l3Prot == CustomHeader::ProtTypeE::TOKEN){
			m_mmu->m_tx_tkn[ifIndex] += p->GetSize();
		}else if (ch.l3Prot == CustomHeader::ProtTypeE::TOKEN_ACK){
			m_mmu->m_tx_tknack[ifIndex] += p->GetSize();
		}
		else if (ch.l3Prot == CustomHeader::ProtTypeE::PFC || ch.l3Prot == CustomHeader::ProtTypeE::CI){		// PFC/CI packets don't have inDev
			return;
		}
	}

	assert(inDev < m_devices.size() && ifIndex < m_devices.size());

	uint32_t pfc_fine = qIndex;
	if (m_mmu->pfc_mode[inDev] == Settings::PAUSE_DST) pfc_fine = Settings::hostIp2IdMap[ch.dip];

	if (qIndex < Settings::QUEUENUM && m_mmu->pfc_ingress_ctrls[qIndex])
		m_mmu->RemoveFromIngressAdmission(inDev, pfc_fine, p->GetSize());
	m_mmu->RemoveFromEgressAdmission(ifIndex, qIndex, p->GetSize());
	m_bytes[inDev][ifIndex][qIndex] -= p->GetSize();

	if (qIndex != 0) {
		m_mmu->m_buffering_bytes[Settings::hostIp2IdMap[ch.dip]] -= p->GetSize();
		m_mmu->m_buffering_egress_bytes[ifIndex][Settings::hostIp2IdMap[ch.dip]] -= p->GetSize();
	}

	if (m_ecnEnabled){
		bool egressCongested = false;

		// tag ECN on out-port queues
		if (qIndex >= Settings::QUEUENUM 				// tag ECN for packets in VOQ by default
				|| Settings::ecn_unit[qIndex] != 0){	// tag ECN for packets in configured ECN-markable Q
			egressCongested |= m_mmu->ShouldSendCN(ifIndex, qIndex);
		}

		// piggy ECN on in-port queues
		if (Settings::piggy_ingress_ecn && Settings::ecn_unit[Settings::piggy_qid] != 0){
			egressCongested |= m_mmu->ShouldSendCN(inDev, Settings::piggy_qid);
		}

		if (egressCongested){
			PppHeader ppp;
			Ipv4Header h;
			p->RemoveHeader(ppp);
			p->RemoveHeader(h);
			h.SetEcn((Ipv4Header::EcnType)0x03);
			p->AddHeader(h);
			p->AddHeader(ppp);
		}
	}

	Ptr<QbbNetDevice> out_dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);
	Ptr<QbbNetDevice> in_dev = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){
		PppHeader ppp;
		Ipv4Header ip;
		UdpHeader udp;
		SeqTsHeader seqts;
		p->RemoveHeader(ppp);
		p->RemoveHeader(ip);
		p->RemoveHeader(udp);
		p->RemoveHeader(seqts);
		if (m_ccMode == Settings::CC_HPCC){ // HPCC
			uint32_t carried_buffer = out_dev->GetQueue()->GetNBytesTotal();
			if (Settings::fc_mode == Settings::FC_FLOODGATE){
				/**
				 * when use floodgate
				 * --> only incast flows consider voq buffer
				 * (egress_bytes counts all data packets)
				 */
				if (m_mmu->m_use_switchwin && m_mmu->GetBufferDst(ch.dip) > 0){
					assert(m_mmu->egress_bytes[ifIndex].size() > qIndex);
					carried_buffer = m_mmu->egress_bytes[ifIndex][qIndex];
				}
			}else if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
				/**
				 * when use VOQ_DSTPFC
				 * --> non-incast: sch queue length
				 * --> incast: all data packets of egress
				 */
				assert(m_mmu->egress_bytes[ifIndex].size() > 2);
				carried_buffer = m_mmu->egress_bytes[ifIndex][2];		// sch queue length by default
				// check incast dst
				uint32_t dst = Settings::hostIp2IdMap[ch.dip];
				if (out_dev->GetQueue()->m_dstQ_map.find(dst) != out_dev->GetQueue()->m_dstQ_map.end()){
					uint32_t voqid = out_dev->m_queue->m_dstQ_map[dst];
					assert(out_dev->m_queue->m_bytesInQueue.size() > voqid);
					if ((out_dev->m_paused.find(voqid) != out_dev->m_paused.end() && out_dev->m_paused[voqid]) // voq has been paused
							|| out_dev->m_queue->m_bytesInQueue[voqid])		// or there are packets in voq
						carried_buffer = out_dev->GetQueue()->GetNBytesTotal();		// incast dst: all packets of egress
				}
			}else if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
				// TODO
				//only congested flows consider CQ buffer
				CQTag cqtag;
				if(!p->PeekPacketTag(cqtag)){//if it is an uncongested packet
					carried_buffer = GetOQBytesTotal(ifIndex);
				}else{
					p->RemovePacketTag(cqtag);
				}
			}
			seqts.ih.PushHop(Simulator::Now().GetTimeStep(), m_txBytes[ifIndex], carried_buffer, out_dev->GetDataRate().GetBitRate());
		}else if (m_ccMode == Settings::CC_SWIFT){
			seqts.ih.hopCnt += 1;
		}
		p->AddHeader(seqts);
		p->AddHeader(udp);
		p->AddHeader(ip);
		p->AddHeader(ppp);
	}

	// check and resume
	if (qIndex < Settings::QUEUENUM && m_mmu->pfc_ingress_ctrls[qIndex]) {
		CheckAndSendResume(inDev, pfc_fine);
	}

	if (Settings::fc_mode == Settings::FC_FLOODGATE){
		if (m_mmu->m_use_switchwin && ch.l3Prot == CustomHeader::ProtTypeE::UDP){	// send an udp

			bool ignore = m_mmu->ShouldIgnore(Settings::hostIp2IdMap[ch.sip]);	// it is srcToR or not
			if (!m_isToR || !ignore){	// downstream: not the srcToR which would check whether send SwitchACK

				uint32_t dst_id = Settings::hostIp2IdMap[ch.dip];

				uint32_t creditId = inDev;
				if (Settings::reset_only_ToR_switch_win){
					creditId = Settings::hostIp2IdMap[ch.sip]/Settings::host_per_rack;
				}
				if (Settings::switch_absolute_psn){
					// as downstream -> update nxt_ack_psn
					SwitchIngressPSNTag ingress_psntag;
					assert(p->RemovePacketTag(ingress_psntag));
					m_mmu->m_nxt_ack_psn[creditId][dst_id] = std::max(ingress_psntag.GetPSN(), m_mmu->m_nxt_ack_psn[creditId][dst_id]);
				}else
					m_mmu->AddCreditCounter(creditId, dst_id, p->GetSize());

				/*
				 * check and send SwitchACK
				 */
				uint32_t upstream_num = 1;
				uint32_t bc = SwitchMmu::switch_byte_credit_counter;
				if (Settings::switch_ack_th_m > 0)	{	// When use delayACK, check all up-streams
					if (Settings::reset_only_ToR_switch_win){
						upstream_num = Settings::tor_num;
					}else{
						upstream_num = m_devices.size();
					}
				}
				for (uint32_t i = 0; i < upstream_num; ++i, creditId = (creditId+1) % upstream_num){
					if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
						if (m_mmu->m_ingressLastTime[creditId][0] == Time(0)) m_mmu->UpdateIngressLastSendTime(creditId, 0);	// initialize ingress_lasttime
						if (m_mmu->CheckIngressCreditCounter(creditId, Settings::switch_byte_counter)) {
							SwitchACKTag acktag = m_mmu->GetSwitchACKTag(creditId);
							SendAccSwitchACK(creditId, acktag);
							++SwitchMmu::switch_byte_credit_counter;
						}
					}else if(Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT){
						if (m_mmu->m_ingressLastTime[creditId][dst_id] == Time(0)) m_mmu->UpdateIngressLastSendTime(creditId, dst_id);	// initialize ingress_lasttime
						uint64_t creditPippyback = m_mmu->GetIngressDstCreditCounter(creditId, dst_id);
						if (creditPippyback >= Settings::switch_byte_counter) {
							if (Settings::switch_absolute_psn) {
								creditPippyback = m_mmu->m_nxt_ack_psn[creditId][dst_id];
							}
							uint32_t ack_dst_ip = ch.sip;
							if (Settings::reset_only_ToR_switch_win)
								ack_dst_ip = Settings::hostId2IpMap[creditId*Settings::host_per_rack];
							SendSwitchACK(creditId, ch.dip, ack_dst_ip, creditPippyback);
							++SwitchMmu::switch_byte_credit_counter;
						}
					}
				}
				/*
				 * when no credits were sent, check K and T
				 * -> if K > 1 && T == 0, the last several(<K) credits may always cannot send back.
				 * -> when no packets in VOQ buffer, send credits back
				 */
				if (bc == SwitchMmu::switch_byte_credit_counter
						&& Settings::switch_byte_counter > 1 && Settings::switch_credit_interval == 0
						&& m_mmu->GetBufferDst(dst_id) == 0){
					// check all up-streams
					if (Settings::reset_only_ToR_switch_win){
						upstream_num = Settings::tor_num;
					}else{
						upstream_num = m_devices.size();
					}
					for (uint32_t i = 0; i < upstream_num; ++i, creditId = (creditId+1) % upstream_num){
						if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
							if (m_mmu->CheckIngressCreditCounter(creditId, 1)) {
								SwitchACKTag acktag = m_mmu->GetSwitchACKTag(creditId);
								SendAccSwitchACK(creditId, acktag);
								++SwitchMmu::switch_byte_credit_counter;
							}
						}else if(Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT){
							uint64_t creditPippyback = m_mmu->GetIngressDstCreditCounter(creditId, dst_id);
							if (creditPippyback >= 1) {
								if (Settings::switch_absolute_psn) {
									creditPippyback = m_mmu->m_nxt_ack_psn[creditId][dst_id];
								}
								uint32_t ack_dst_ip = ch.sip;
								if (Settings::reset_only_ToR_switch_win)
									ack_dst_ip = Settings::hostId2IpMap[creditId*Settings::host_per_rack];
								SendSwitchACK(creditId, ch.dip, ack_dst_ip, creditPippyback);
								++SwitchMmu::switch_byte_credit_counter;
							}
						}
					}
				}
			}
		}

	}else if (Settings::fc_mode == Settings::FC_BFC){
		if (qIndex != 0){
			// update flow_table
			FlowTag ftag;
			assert(p->PeekPacketTag(ftag));
			uint32_t fid = m_mmu->GetHashFID(ch, m_devices.size(), ftag.getIndex());
			assert(m_mmu->m_flow_tb[ifIndex].find(fid) != m_mmu->m_flow_tb[ifIndex].end());
			m_mmu->m_flow_tb[ifIndex][fid].size -= 1;
			m_mmu->m_flow_tb[ifIndex][fid].time = Simulator::Now();
			// check resume
			MetaTag metaTag;
			assert(p->RemovePacketTag(metaTag));
			UpstreamTag uptag;
			assert(p->RemovePacketTag(uptag));
			if (metaTag.GetCounterIncr() == 1){
				m_mmu->m_pause_counter[inDev][uptag.GetUpstreamQ()] -= 1;
				if (0 == m_mmu->m_pause_counter[inDev][uptag.GetUpstreamQ()]){ // send resume
					Ptr<QbbNetDevice> in_dev = DynamicCast<QbbNetDevice>(m_devices[inDev]);
					in_dev->SendPfc(uptag.GetUpstreamQ(), 1);
				}
			}
			// update upstreamQ
			uptag.SetUpstreamQ(qIndex);
			p->AddPacketTag(uptag);
		}
	}else if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
		if (qIndex != 0){
			uint32_t dst = Settings::hostIp2IdMap[ch.dip];
			if (m_mmu->CheckVOQResume(out_dev->m_queue->m_isLastHop, inDev, ifIndex, qIndex, dst)){
				for (uint32_t i = 0; i < m_devices.size(); i++){	// resume all paused upstream
					if (m_devices[i]->IsQbb() && m_mmu->paused[i][dst]){
						DynamicCast<QbbNetDevice>(m_devices[i])->SendPfc(dst, 1);
						m_mmu->SetResume(i, dst);
					}
				}
			}
		}
	}else if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION){
		NotifyDequeue_CI(inDev, ifIndex, qIndex, p);
	}
}

/*
 * For simplify the logic,
 * given that all ports have the same bandwidth, handle all scheduling logic of each ports in one scheduler
 */
void SwitchNode::DoHierQRR(){
	DataRate rate;
	uint32_t buffer_sum = 0;
	for (uint32_t dev = 0; dev < m_devices.size(); dev++){
		if (!m_devices[dev]->IsQbb()) continue;
		bool intoOQ = false;
		Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[dev]);
		rate = device->GetDataRate();
		Ptr<BEgressQueue> q = device->GetQueue();
		buffer_sum += q->GetNBytesTotal();
		for (uint32_t level = Settings::MAXHOP; level > 0; --level){
			if (q->m_priorities.find(level) == q->m_priorities.end()) continue;
			for (uint32_t prio = 0; prio < q->m_priorities[level].size(); prio++)	// priority
			{
				for (uint32_t i = 0; i < q->m_priorities[level][prio].size() ; i++)	// RR in same priority
				{
					uint32_t qIndex = q->m_priorities[level][prio][i];
					if (q->m_queues[qIndex]->GetNPackets() == 0) continue;

					// check dequeue or not
					bool dequeue = false;
					uint32_t next_q = -1;
					if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION)
						dequeue = CheckDequeueHierQ_CI(dev, level, qIndex, next_q);
					if (!dequeue) continue;

					// do dequeue
					Ptr<Packet> p = q->m_queues[qIndex]->Dequeue();
					assert(!!p);
					FlowIdTag t;
					p->PeekPacketTag(t);
					uint32_t inDev = t.GetFlowId();
					q->m_bytesInQueueTotal -= p->GetSize();
					q->m_bytesInQueue[qIndex] -= p->GetSize();
					if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION)
						NotifyDequeue_CI(inDev, dev, qIndex, p);
					m_mmu->RemoveFromEgressAdmission(dev, qIndex, p->GetSize());
					m_bytes[inDev][dev][qIndex] -= p->GetSize();
					// do enqueue
					assert(next_q != (uint32_t)-1);
					assert(q->DoEnqueue(p, next_q));
					if (Settings::fc_mode == Settings::FC_CONGESTION_ISOLATION)
						NotifyEnqueue_CI(t.GetFlowId(), dev, next_q, p);
					while (m_bytes[inDev][dev].size() <= next_q){
						m_bytes[inDev][dev].push_back(0);
					}
					m_bytes[inDev][dev][next_q] += p->GetSize();
					m_mmu->UpdateEgressAdmission(dev, next_q, p->GetSize());
					if (next_q < Settings::QUEUENUM && m_mmu->pfc_ingress_ctrls[next_q])
						m_mmu->UpdateIngressAdmission(inDev, next_q, p->GetSize());

					if (next_q < Settings::QUEUENUM)
						intoOQ = true;

				}// end of all q in one prio
			}// end of all prios of one level
		}// end of all level
		if (intoOQ){
			device->DequeueAndTransmit();
		}
	}// end of all devices
	if (buffer_sum > 0)
		m_eventDoHierRR = Simulator::Schedule(Seconds(rate.CalculateTxTime(Settings::MTU)/Settings::th_ciq_rate), &SwitchNode::DoHierQRR, this);
}

/*-------------------------------For Routing (ECMP by default) ---------------------------------*/
void SwitchNode::AddTableEntry(Ipv4Address &dstAddr, uint32_t intf_idx){
	uint32_t dip = dstAddr.Get();
	m_rtTable[dip].push_back(intf_idx);
	Settings::node_rtTable[m_id][dip].push_back(intf_idx);
}

void SwitchNode::ClearTable(){
	m_rtTable.clear();
	Settings::node_rtTable[m_id].clear();
}

int SwitchNode::GetOutDev(Ptr<const Packet> p, CustomHeader &ch){
	// look up entries
	auto entry = m_rtTable.find(ch.dip);

	// no matching entry
	if (entry == m_rtTable.end())
		return -1;

	// entry found
	auto &nexthops = entry->second;

	// pick one next hop based on hash
	uint32_t egress = 0;
	if (Settings::routing_mode == Settings::ROUTING_DRILL){
		egress = SelectEgressByDRILL(nexthops, ch);
	}
	else{		// ECMP
		uint32_t idx = 0;
		union {
			uint8_t u8[4+4+4];
			uint32_t u32[3];
		} buf;

		if (Settings::symmetic_routing_mode == Settings::SYMMETRIC_ON){
			buf.u32[0] = ch.sip | ch.dip;
			buf.u32[1] = ch.dip | ch.sip;
		}else{
			buf.u32[0] = ch.sip;
			buf.u32[1] = ch.dip;
		}

		if (Settings::ecmp_unit == Settings::ECMP_QP){
			QPTag qptag;
			p->PeekPacketTag(qptag);
			buf.u32[2] = qptag.GetQPID();
		}else{
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

			if (Settings::symmetic_routing_mode == Settings::SYMMETRIC_ON){
				buf.u32[2] = sport | dport;
			}else
				buf.u32[2] = sport | ((uint32_t)dport << 16);
		}

		idx = Settings::EcmpHash(buf.u8, 12, m_id);
		idx = idx % (nexthops.size() + Settings::load_imbalance_th);
		if (idx >= nexthops.size()) idx = 0;
		egress = nexthops[idx % nexthops.size()];

		if (nexthops.size() == 1 && Settings::up_port[m_id-Settings::host_num][egress]){
			Settings::warning_out << Simulator::Now() << " false up_port:" << m_id << " " << egress << std::endl;
		}else if (nexthops.size() > 1 && !Settings::up_port[m_id-Settings::host_num][egress]){
			Settings::warning_out << Simulator::Now() << " false down_port:" << m_id << " " << egress << std::endl;
		}
//#if DEBUG_MODE
//	std::cout << Simulator::Now() << " SwitchNode::GetOutDev " << this->m_id << " " << m_ecmpSeed << " " << buf.u32[0] << " " << buf.u32[1] << " " << buf.u32[2] << " "<< egress << " " << idx << " " << idx % nexthops.size() << " " << ch.l3Prot << std::endl;
//#endif
	}

	return egress;
}

/*---------------------------------DRILL-------------------------------------*/
uint32_t SwitchNode::CalculateInterfaceLoad (uint32_t interface){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[interface]);
	assert(!!device && !!device->GetQueue());
	if (Settings::drill_load_mode == Settings::DRILL_LOAD_INTERVAL_SENT){
		return device->GetQueue()->GetTotalReceivedBytes();
	}else{
		return device->GetQueue()->GetNBytesTotal();
	}
}

uint32_t SwitchNode::SelectEgressByDRILL(std::vector<int>& nexthops, CustomHeader &ch){

	uint32_t leastLoadInterface = 0;
	uint32_t leastLoad = std::numeric_limits<uint32_t>::max ();

	std::random_shuffle (nexthops.begin (), nexthops.end ());

	std::map<uint32_t, uint32_t>::iterator itr = m_previousBestInterfaceMap.find (ch.dip);

	if (itr != m_previousBestInterfaceMap.end ())
	{
	  leastLoadInterface = itr->second;
	  leastLoad = CalculateInterfaceLoad (itr->second);
	}

	uint32_t sampleNum = m_drill_candidate < nexthops.size () ? m_drill_candidate : nexthops.size ();

	for (uint32_t samplePort = 0; samplePort < sampleNum; samplePort ++)
	{
	  uint32_t sampleLoad = CalculateInterfaceLoad (nexthops[samplePort]);
	  if (sampleLoad < leastLoad)
	  {
		leastLoad = sampleLoad;
		leastLoadInterface = nexthops[samplePort];
	  }
	}

//	NS_LOG_INFO (this << " Drill routing chooses interface: " << leastLoadInterface << ", since its load is: " << leastLoad);

	m_previousBestInterfaceMap[ch.dip] = leastLoadInterface;
	return leastLoadInterface;
}

void SwitchNode::ResetQueueStatisticsInterval(){
//	NS_LOG_FUNCTION (this);

	// only used when DRILL_LOAD_INTERVAL_SENT for now
	if (Settings::drill_load_mode != Settings::DRILL_LOAD_INTERVAL_SENT) return;

	for (uint32_t i = 0; i < m_devices.size(); i++){
		Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[i]);
		if (!!device && !!device->GetQueue()){
			device->GetQueue()->ResetStatistics();
		}
	}
	m_eventResetQueueStatitics.Cancel();
	m_eventResetQueueStatitics = Simulator::Schedule(MicroSeconds(Settings::queue_statistic_interval_us), &SwitchNode::ResetQueueStatisticsInterval, this);
}

uint32_t SwitchNode::GetOQBytesTotal(uint16_t ifIndex){
	uint32_t OQBytesTotal = 0;
	for(int i = 0; i < Settings::QUEUENUM; ++i){
		OQBytesTotal += m_mmu->egress_bytes[ifIndex][i];
	}
	return OQBytesTotal;
}

/*-------------------------------PFC / VOQ_DSTPFC---------------------------------*/
void SwitchNode::CheckAndSendPfc(uint32_t inDev, uint32_t qIndex){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (m_mmu->CheckShouldPause(inDev, qIndex)){
		device->SendPfc(qIndex, 0);
		m_mmu->SetPause(inDev, qIndex);
	}
}

void SwitchNode::CheckAndSendResume(uint32_t inDev, uint32_t qIndex){
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	if (m_mmu->CheckShouldResume(inDev, qIndex)){
		device->SendPfc(qIndex, 1);
		m_mmu->SetResume(inDev, qIndex);
	}
}

/**
 * When receive PFC(pause/resume)
 * --> update status
 */
void SwitchNode::SwitchReceivePFC(uint32_t ifIndex, uint32_t qIndex, uint32_t time){
	if (time > 0){// pause frame
		if (Settings::ctrl_all_egress){
			for (uint32_t i = 0; i < m_devices.size(); i++){	// update pause status of all devs
				if (m_devices[i]->IsQbb()){
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[i]);

					// when use FC_VOQ_DSTPFC, qIndex is dst
					if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
						qIndex = dev->m_queue->RegisterVOQ(qIndex);	// allocate VOQ when receive pause frame
						m_mmu->UpdateEgressAdmission(i, qIndex, 0);
					}

					dev->m_paused[qIndex] = true;
				}
			}
		}else{
			assert(m_devices[ifIndex]->IsQbb());
			Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);

			// when use FC_VOQ_DSTPFC, qIndex is dst
			if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC)
				qIndex = dev->m_queue->RegisterVOQ(qIndex);	// allocate VOQ when receive pause frame

			dev->m_paused[qIndex] = true;
		}
	}else{// resume frame
		if (Settings::ctrl_all_egress){
			for (uint32_t i = 0; i < m_devices.size(); i++){	// update resume status of all devs
				if (m_devices[i]->IsQbb()){
					Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[i]);

					// when use FC_VOQ_DSTPFC, qIndex is dst
					if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
						assert(dev->m_queue->m_dstQ_map.find(qIndex) != dev->m_queue->m_dstQ_map.end());
						qIndex = dev->m_queue->m_dstQ_map[qIndex];
					}

					dev->Resume(qIndex);
				}
			}
		}else{
			assert(m_devices[ifIndex]->IsQbb());
			Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ifIndex]);

			// when use FC_VOQ_DSTPFC, qIndex is dst
			if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
				assert(dev->m_queue->m_dstQ_map.find(qIndex) != dev->m_queue->m_dstQ_map.end());
				qIndex = dev->m_queue->m_dstQ_map[qIndex];
			}

			DynamicCast<QbbNetDevice>(m_devices[ifIndex])->Resume(qIndex);
		}
	}
}

/*-------------------------------Congestion Isolation---------------------------------*/
/*
 * When receive CI signal
 */
void SwitchNode::ReceiveCI(CITag citag, uint32_t ingress){
	uint32_t root_switch = citag.GetRootSwitch();
	uint32_t root_port = citag.GetRootPort();
	Ptr<QbbNetDevice> dev = DynamicCast<QbbNetDevice>(m_devices[ingress]);
	dev->m_traceCI(citag.GetCIType(), citag.GetRootSwitch(), citag.GetRootPort(), citag.GetOldSwitch(), citag.GetOldPort());
	Ptr<CAMEntry> entry = m_mmu->GetEntry(root_switch, root_port);
	if (citag.IsPause()){
		/*
		 * It's a pause signal
		 * -> create entry if not exist and update oport ciq/status
		 * -> check to merge congestion
		 */
		bool initial = false;
		if (!entry) {
			entry = Create<CAMEntry>();
			entry->hop = citag.GetHop();
			assert(entry->hop > 0);
			m_mmu->congestion_entries[root_switch][root_port] = entry;
		}
		if (root_port != (uint32_t)-1 && entry->oport_ciq[ingress] == (uint32_t)-1){
			uint32_t ciq = dev->m_queue->RegisterCIQ(entry->hop);
			entry->oport_ciq[ingress] = ciq;
			SwitchMmu::Spot root;
			root.switch_id = root_switch;
			root.port_id = root_port;
			m_mmu->ciq_root_map[ingress][ciq] = root;
			m_mmu->UpdateEgressAdmission(ingress, ciq, 0);
			// for avoiding dis-order
//			dev->PushOrderMark(Settings::DEFAULT_DATA, ciq);
			initial = true;
		}
		entry->oport_status[ingress] = CAMEntry::PAUSE;
		dev->m_paused[entry->oport_ciq[ingress]] = true;		// the control unit of queues on switches(the key of m_paused is the index of Queue)

		if (Settings::congestion_merge){
			/*
			 * check to merge
			 * When the pause carry the meaning of initial congestion node locally(newer than local congestion root)
			 * -> if it's a downstream port and congestion root -> should merge
			 */
			if (initial && !Settings::up_port[m_id-Settings::host_num][ingress] && m_mmu->IsRoot(ingress)){
				std::unordered_map<uint32_t, bool>::iterator it = m_mmu->upstream_paused[ingress].begin();
				while (it != m_mmu->upstream_paused[ingress].end()){
					Simulator::Schedule(NanoSeconds(0), &QbbNetDevice::SendCIPacket, DynamicCast<QbbNetDevice>(m_devices[it->first]),
							root_switch, root_port, CITag::CI_MERGE, entry->hop+1, m_id, ingress);
					m_mmu->SetCIQResume(it->first, entry);
					it++;
				}
				m_mmu->upstream_paused[ingress].clear();
				m_mmu->upstream_paused.erase(ingress);
			}
		}

#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == root_switch && DEBUG_EGRESS == root_port){
			Settings::warning_out << Simulator::Now() << " " << m_id << "-" << ingress << " receive Pause (" << root_switch << ", " << root_port << "). ";
			Settings::warning_out << " initial: " << initial << std::endl;
		}
#endif

	}else if (citag.IsResume()){
		/*
		 * It's a resume signal
		 * -> update oport status
		 * -> check and send Dealloc
		 * note: consider Delloc signal from child node may in-flight -> update entry when it exists
		 */
		if (!!entry && entry->oport_ciq[ingress] != (uint32_t)-1){
			entry->oport_status[ingress] = CAMEntry::RESUME;
			dev->m_paused[entry->oport_ciq[ingress]] = false;
			CheckAndSendDealloc(root_switch, root_port);
			dev->DequeueAndTransmit();
		}

#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == root_switch && DEBUG_EGRESS == root_port){
			Settings::warning_out << Simulator::Now() << " " << m_id << "-" << ingress << " receive Resume (" << root_switch << ", " << root_port << "). ";
			Settings::warning_out << " entry: " << !!entry << " ciq: " << ((!!entry)?entry->oport_ciq[ingress]:0) << std::endl;
			if (!!entry){
				Settings::warning_out << " iport_paused[DEBUG_INDEV]: "
				<< entry->iport_paused.find(DEBUG_INDEV) == entry->iport_paused.end() << "/"
				<< (entry->iport_paused.find(DEBUG_INDEV) == entry->iport_paused.end() && entry->iport_paused[DEBUG_INDEV]) << std::endl;
			}
		}
#endif
	}else if (citag.IsDealloc()){
		/*
		 * It's a dealloc signal
		 * -> update iport record(note: different structure of root and children)
		 * -> if it's not congestion root, check and send Dealloc
		 * note:
		 * 1. consider that the merge may inflight -> deallocate only when iport_paused[ingress]/upstream_paused[root_port][ingress] exists
		 * 2. consider that the pause/resume may inflight -> deallocate only when ingress is resumed
		 */

#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == root_switch && DEBUG_EGRESS == root_port){
			Settings::warning_out << Simulator::Now() << " " << m_id << "-" << ingress << " receive Dealloc (" << root_switch << ", " << root_port << "). " << std::endl;
		}
#endif
		if (root_switch != m_id){ // not congestion root
			if (!!entry
					&& entry->iport_paused.find(ingress) != entry->iport_paused.end()
					&& entry->iport_paused[ingress] == false){
				m_mmu->SetCIQDealloc(ingress, entry);
				CheckAndSendDealloc(root_switch, root_port);
			}
		}else{ // congestion root
#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == root_switch && DEBUG_EGRESS == root_port){
			Settings::warning_out << "Here is congestion root.";
			if (m_mmu->upstream_paused.find(root_port) != m_mmu->upstream_paused.end()
					&& m_mmu->upstream_paused[root_port].find(ingress) != m_mmu->upstream_paused[root_port].end()){
				Settings::warning_out << " upstream_paused[ingress] " << m_mmu->upstream_paused[root_port][ingress] << std::endl;
			}
		}
#endif
			if (m_mmu->upstream_paused.find(root_port) != m_mmu->upstream_paused.end()
					&& m_mmu->upstream_paused[root_port].find(ingress) != m_mmu->upstream_paused[root_port].end()
					&& m_mmu->upstream_paused[root_port][ingress] == false){
				m_mmu->SetRootDealloc(ingress, root_port);
			}
		}
	}else if (citag.IsMerge()){
		/*
		 * It's a merge signal
		 * -> add the new congestion root if it doesn't exist.
		 * -> If there is old congestion root locally, update old congestion root status,
		 * -> and transmit Merge signal if there are children
		 * note: most of the time, old congestion root entry exists unless the Dealloc is in-flight when downstream send Merge
		 */
#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == root_switch && DEBUG_EGRESS == root_port){
			Settings::warning_out << Simulator::Now() << " " << m_id << "-" << ingress << " receive Merge (" << root_switch << ", " << root_port << "). "<< std::endl;
		}
#endif
		// add new root
		if (!entry) {
			entry = Create<CAMEntry>();
			entry->hop = citag.GetHop();
			assert(entry->hop > 0);
			m_mmu->congestion_entries[root_switch][root_port] = entry;
		}
		// assert ((root_port != (uint32_t)-1 && entry->oport_ciq[ingress] == (uint32_t)-1)
		// 		|| entry->oport_status[ingress] == CAMEntry::MERGE);
		uint32_t ciq = entry->oport_ciq[ingress];
		if (root_port != (uint32_t)-1 && ciq == (uint32_t)-1){
			ciq = dev->m_queue->RegisterCIQ(entry->hop);
			entry->oport_ciq[ingress] = ciq;
			SwitchMmu::Spot root;
			root.switch_id = root_switch;
			root.port_id = root_port;
			m_mmu->ciq_root_map[ingress][ciq] = root;
			m_mmu->UpdateEgressAdmission(ingress, ciq, 0);
		}
		entry->oport_status[ingress] = CAMEntry::RESUME;
		dev->m_paused[entry->oport_ciq[ingress]] = false;

		uint32_t old_switch = citag.GetOldSwitch(), old_port = citag.GetOldPort();
		Ptr<CAMEntry> old_entry = m_mmu->GetEntry(old_switch, old_port);
#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == root_switch && DEBUG_EGRESS == root_port){
			Settings::warning_out << " old (" << old_switch << ", " << old_port << "). " << !!old_entry << std::endl;
		}
#endif
		if (!!old_entry){
			// update old root status
			old_entry->oport_status[ingress] = CAMEntry::MERGE;
			if (dev->m_paused.find(old_entry->oport_ciq[ingress]) != dev->m_paused.end() && dev->m_paused[old_entry->oport_ciq[ingress]])
				dev->m_paused[old_entry->oport_ciq[ingress]] = false;
			// trasmit Merge to children
			std::unordered_map<uint32_t, bool>::iterator it = old_entry->iport_paused.begin();
			while (it != old_entry->iport_paused.end()){
				if (!m_isToR || it->second){
					Simulator::Schedule(NanoSeconds(0), &QbbNetDevice::SendCIPacket, DynamicCast<QbbNetDevice>(m_devices[it->first]),
							root_switch, root_port, CITag::CI_MERGE, entry->hop+1, old_switch, old_port);
					m_mmu->SetCIQResume(it->first, entry);
				}
				it++;
			}
			old_entry->iport_paused.clear();

			// for avoiding dis-order
//			if (old_entry->oport_ciq[ingress] != (uint32_t)-1 && dev->m_queue->GetNQueue() > old_entry->oport_ciq[ingress])
//				dev->PushOrderMark(old_entry->oport_ciq[ingress], ciq);

			// release CIQ/CAMEntry
			ReleaseMergeCIQEntry(old_switch, old_port, ingress);
		}
		dev->DequeueAndTransmit();
	}
}

/*
 * Check whether this queue can dequeue packets
 */
bool SwitchNode::CheckDequeueHierQ_CI(uint32_t dev, uint32_t level, uint32_t qIndex, uint32_t& next_q){
	next_q = -1;
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[dev]);
	if (device->m_paused.find(qIndex) == device->m_paused.end() || !device->m_paused[qIndex]){
		Ptr<BEgressQueue> q = device->GetQueue();
		Ptr<Packet> p = q->m_queues[qIndex]->Peek()->Copy();
		RoutingTag routingTag;
		assert(p->PeekPacketTag(routingTag));
		uint32_t next_root_switch = -1, next_root_port = -1;
		Ptr<CAMEntry> entry = m_mmu->MatchNextCongestionRoot(dev, routingTag, level, next_root_switch, next_root_port);
		if (!entry){
			CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
			ch.getInt = 1; // parse INT header
			p->PeekHeader(ch);
			next_q = MatchDefaultQ(ch);
		}else{
			next_q = entry->oport_ciq[dev];
		}
		return true;
	}
	return false;
}

/*
 * When a packet is enqueuing
 * -> do corresponding logic based on the type of Q
 */
void SwitchNode::NotifyEnqueue_CI(uint32_t inDev, uint32_t idx, uint32_t qIndex, Ptr<Packet> p){
	if (qIndex == 0) return;	// don't check packets in queue0

	// congestion detect
	if (qIndex < Settings::QUEUENUM){	// packet dequeue from OQ, detect congestion as root
		NotifyEnqueueIntoOQ_CI(inDev, idx, qIndex, p);
	}else{	// packet dequeue from CIQ, detect congestion as child
		NotifyEnqueueIntoCIQ_CI(inDev, idx, qIndex, p);
	}
#if DEBUG_MODE_DEV
	if (DEBUG_SWITCH == m_id && DEBUG_EGRESS == idx){
		Settings::warning_out << Simulator::Now() << " enqueue packet " << m_id << "-" << inDev << "-" << qIndex << std::endl;
	}
#endif
	// todo: oversubscribed congestion detect
}

/*
 * When a packet is enqueued into an OQ
 * -> check to pause upstream as congestion root
 */
void SwitchNode::NotifyEnqueueIntoOQ_CI(uint32_t inDev, uint32_t idx, uint32_t qIndex, Ptr<Packet> p){
	// if pyrrha only enable in down_port and this port is a up_port, then return
	if (Settings::only_down_port_enable == true && Settings::up_port[m_id-Settings::host_num][idx] == true)
	{
		return;
	}
	if (m_mmu->CheckRootPause(inDev, idx, qIndex)){
		Simulator::Schedule(NanoSeconds(0), &QbbNetDevice::SendCIPacket, DynamicCast<QbbNetDevice>(m_devices[inDev]),
				m_id, idx, CITag::CI_PAUSE, 1, 0, 0);
		m_mmu->SetRootPause(inDev, idx);
	}
}

/*
 * When a packet enqueues a CIQ
 * -> update ingress counter
 * -> check to pause congestion child
 */
void SwitchNode::NotifyEnqueueIntoCIQ_CI(uint32_t inDev, uint32_t idx, uint32_t qIndex, Ptr<Packet> p){
	assert(m_mmu->ciq_root_map[idx].find(qIndex) != m_mmu->ciq_root_map[idx].end());
	SwitchMmu::Spot root = m_mmu->ciq_root_map[idx][qIndex];
	Ptr<CAMEntry> entry = m_mmu->GetEntry(root.switch_id, root.port_id);
	assert(!!entry);
	entry->iport_counter[inDev] += p->GetSize();
	if (m_mmu->CheckCIQPause(inDev, idx, qIndex, entry)){
		Simulator::Schedule(NanoSeconds(0), &QbbNetDevice::SendCIPacket, DynamicCast<QbbNetDevice>(m_devices[inDev]),
				root.switch_id, root.port_id, CITag::CI_PAUSE, entry->hop+1, 0, 0);
		m_mmu->SetCIQPause(inDev, entry);
	}
}

/*
 * When there is a Q dequeuing packets
 * -> update routingTag
 * -> do corresponding logic based on the type of Q
 */
void SwitchNode::NotifyDequeue_CI(uint32_t inDev, uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p){
	if (qIndex == 0) return;
#if DEBUG_MODE_QP
	QPTag qptag;
	if (ch.l3Prot == CustomHeader::ProtTypeE::UDP && p->PeekPacketTag(qptag) && DEBUG_QP == qptag.GetQPID()){
		Settings::warning_out << Simulator::Now() << " node" << m_id << " dequeue data packet(seq" << qptag.GetQPPSN() << ") from Q" << qIndex << std::endl;
	}
#endif
#if DEBUG_MODE_DEV
	if (DEBUG_SWITCH == m_id && DEBUG_EGRESS == ifIndex){
		Settings::warning_out << Simulator::Now() << " dequeue packet " << m_id << "-" << ifIndex << std::endl;
	}
#endif
	// check and send resume
	if (qIndex < Settings::QUEUENUM){
		RoutingTag routingTag;
		assert(p->RemovePacketTag(routingTag));
		// only check routingTag in ecmp, or when this port is down port in drill
		// if(Settings::routing_mode == Settings::ROUTING_GLOBAL || (Settings::routing_mode == Settings::ROUTING_DRILL && Settings::up_port[m_id - Settings::host_num][ifIndex] == false))
		// {
		// 	assert(routingTag.GetHopRoutingPort(routingTag.GetCurrHop()) == ifIndex);
		//  assert(routingTag.GetHopRoutingSwitch(routingTag.GetCurrHop()) == m_id);
		// }
		routingTag.AddCurrHop();
		p->AddPacketTag(routingTag);
		NotifyDequeueFromOQ_CI(inDev, ifIndex, qIndex, p);
	}else{
		NotifyDequeueFromCIQ_CI(inDev, ifIndex, qIndex, p);
	}
}

/*
 * When it's OQ dequeued a packet
 * -> check to resume congestion root
 */
void SwitchNode::NotifyDequeueFromOQ_CI(uint32_t inDev, uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p){
#if DEBUG_MODE_DEV
	if (DEBUG_SWITCH == m_id && DEBUG_EGRESS == ifIndex){
		Settings::warning_out << "Root check resume: " << std::endl;
	}
#endif
	// if pyrrha only enable in down_port and this port is a up_port, then return
	if (Settings::only_down_port_enable == true && Settings::up_port[m_id-Settings::host_num][ifIndex] == true)
	{
		return;
	}
	std::unordered_map<uint32_t, bool>::iterator it = m_mmu->upstream_paused[ifIndex].begin();
	while (it != m_mmu->upstream_paused[ifIndex].end()){
		if (it->second && m_mmu->CheckRootResume(it->first, ifIndex, qIndex)){
			Simulator::Schedule(NanoSeconds(0), &QbbNetDevice::SendCIPacket, DynamicCast<QbbNetDevice>(m_devices[it->first]),
					m_id, ifIndex, CITag::CI_RESUME, 0, 0, 0);
			m_mmu->SetRootResume(it->first, ifIndex);
		}
#if DEBUG_MODE_DEV
		if (DEBUG_SWITCH == m_id && DEBUG_EGRESS == ifIndex){
			Settings::warning_out << it->first << ":" << it->second <<":" << sent
					<< "-qId" << qIndex << ":" << m_mmu->egress_bytes[ifIndex][qIndex] << ":" << out_dev->m_queue->GetNBytes(qIndex) << ":" << out_dev->m_queue->GetNBytesTotal() <<", ";
		}
#endif
		it++;
	}
#if DEBUG_MODE_DEV
	if (DEBUG_SWITCH == m_id && DEBUG_EGRESS == ifIndex){
		Settings::warning_out << std::endl;
	}
#endif
}

/* When it's CIQ dequeued a packet
 * -> update ingress counter
 * -> check to resume congestion child
 * -> check to release: deallocation or release locally
 */
void SwitchNode::NotifyDequeueFromCIQ_CI(uint32_t inDev, uint32_t ifIndex, uint32_t qIndex, Ptr<Packet> p){
	assert(m_mmu->ciq_root_map[ifIndex].find(qIndex) != m_mmu->ciq_root_map[ifIndex].end());
	SwitchMmu::Spot root = m_mmu->ciq_root_map[ifIndex][qIndex];
	Ptr<CAMEntry> entry = m_mmu->GetEntry(root.switch_id, root.port_id);
	assert(!!entry);
	entry->iport_counter[inDev] -= p->GetSize();
	if (m_mmu->CheckCIQResume(inDev, ifIndex, qIndex, entry)){
		Simulator::Schedule(NanoSeconds(0), &QbbNetDevice::SendCIPacket, DynamicCast<QbbNetDevice>(m_devices[inDev]),
				root.switch_id, root.port_id, CITag::CI_RESUME, 0, 0, 0);
		m_mmu->SetCIQResume(inDev, entry);
	}

	if (entry->oport_status[ifIndex] != CAMEntry::MERGE)
		CheckAndSendDealloc(root.switch_id, root.port_id);
	else
		ReleaseMergeCIQEntry(root.switch_id, root.port_id, ifIndex);
}

/*
 * Check and send Dealloc signal
 * -> if it's leaf and oport's CIQ is empty
 * -> deallocate resources and send Dealloc signal from oport to downstream (father node in congestion tree).
 */
void SwitchNode::CheckAndSendDealloc(uint32_t root_switch, uint32_t root_port){
	if (!Settings::dealloc_on) return;
	Ptr<CAMEntry> entry = m_mmu->GetEntry(root_switch, root_port);
	assert(!!entry);

	/*
	 * Check has children or not
	 * -> if it's ToR, consider that host will not send Dealloc, we only chech all iports are not paused.
	 * -> Otherwise, we should check all iports are not controling upstreams(pause/resume).
	 */
	if (m_isToR) {
		std::unordered_map<uint32_t, bool>::iterator it_iport = entry->iport_paused.begin();
		while (it_iport != entry->iport_paused.end()){
			if (it_iport->second != false) return;
			it_iport++;
		}
	}else{
		if (!entry->iport_paused.empty()) return;
	}

	bool dealloc_all = true;
	for (uint32_t i = 0; i < m_devices.size(); ++i){
		Ptr<QbbNetDevice> curr_dev = DynamicCast<QbbNetDevice>(m_devices[i]);
		uint32_t ciq = entry->oport_ciq[i];
		if (ciq == (uint32_t)-1) {	// has been deallocated or never under control
			// assert(entry->oport_status[i] == CAMEntry::CIQStatus::INITIAL);
			continue;
		}
		assert(curr_dev->m_queue->GetNQueue() > ciq);
		if (curr_dev->m_queue->GetNBytes(ciq) == 0 && entry->oport_status[i] == CAMEntry::CIQStatus::RESUME){
			// send Dealloc to father nodes
			Simulator::Schedule(NanoSeconds(0), &QbbNetDevice::SendCIPacket, DynamicCast<QbbNetDevice>(m_devices[i]), root_switch, root_port, CITag::CI_DEALLOC, 0, 0, 0);
			// release CIQ resources
			entry->oport_status[i] = CAMEntry::CIQStatus::INITIAL;
			assert(curr_dev->m_paused.find(ciq) != curr_dev->m_paused.end());
			curr_dev->m_paused.erase(ciq);
			assert(m_mmu->ciq_root_map[i].find(ciq) != m_mmu->ciq_root_map[i].end());
			m_mmu->ciq_root_map[i].erase(ciq);
			entry->oport_ciq[i] = -1;
			// todo: release CIQ in m_queue

		}else
			dealloc_all = false;
	}

	if (dealloc_all){	// release CAMEntry
		m_mmu->congestion_entries[root_switch].erase(root_port);
		if (m_mmu->congestion_entries[root_switch].empty())
			m_mmu->congestion_entries.erase(root_switch);
	}
}

/*
 * When merging(old congestion tree) CIQ is empty, release resources
 */
void SwitchNode::ReleaseMergeCIQEntry(uint32_t root_switch, uint32_t root_port, uint32_t oport){
	assert(Settings::congestion_merge);
	Ptr<CAMEntry> entry = m_mmu->GetEntry(root_switch, root_port);
	assert(!!entry && entry->oport_status[oport] == CAMEntry::CIQStatus::MERGE);
	uint32_t ciq = entry->oport_ciq[oport];
	if (ciq == (uint32_t)-1) {	// has been released
		return;
	}
	Ptr<QbbNetDevice> curr_dev = DynamicCast<QbbNetDevice>(m_devices[oport]);
	assert(curr_dev->m_queue->GetNQueue() > ciq);
	if (curr_dev->m_queue->GetNBytes(ciq) == 0){
		// release CIQ resources
		assert(curr_dev->m_paused.find(ciq) != curr_dev->m_paused.end());
		curr_dev->m_paused.erase(ciq);
		assert(m_mmu->ciq_root_map[oport].find(ciq) != m_mmu->ciq_root_map[oport].end());
		m_mmu->ciq_root_map[oport].erase(ciq);
		entry->oport_ciq[oport] = -1;
	}

	bool release_all = true;
	for (uint32_t i = 0; i < m_devices.size(); ++i){
		if (entry->oport_ciq[i] != (uint32_t)-1){
			release_all = false;
			break;
		}
	}
	if (release_all && entry->iport_paused.empty()){
		m_mmu->congestion_entries[root_switch].erase(root_port);
		if (m_mmu->congestion_entries[root_switch].empty())
			m_mmu->congestion_entries.erase(root_switch);
	}
}

/*-------------------------------Floodgate---------------------------------*/
bool SwitchNode::CheckVOQWin(Ptr<Packet> packet, CustomHeader &ch, uint32_t inDev, uint32_t outDev, uint32_t qIndex){

	/*
	 * When receive data
	 * --> Check Window of this VOQ
	 * --> buffer packet in VOQ if there isn't enough window.
	 */
	uint32_t dst_id = Settings::hostIp2IdMap[ch.dip];
	if (ch.l3Prot == CustomHeader::ProtTypeE::UDP){	// udp

		/**
		 * It is unnecessary to block the host under this ToR.
		 * What's more, blocking the host under this ToR may cause dead-lock.
		 * The ignore list was set when build topology.
		 */
		if (m_mmu->ShouldIgnore(dst_id)) return true;

		QPTag qptag;
		packet->PeekPacketTag(qptag);

		/*
		 * the last packet may go through switchwin when the window is less than MTU,
		 * in which way, the last packet may be received earlier than the packets before,
		 * thus, dis-ordered packet appears.
		 * --> to avoid this situation, should check the buffering packets in voq.
		 */
		if (m_mmu->CheckDstWin(dst_id, packet->GetSize()) && m_mmu->GetBufferDst(dst_id) == 0){

			m_mmu->UpdateDstWin(dst_id, packet->GetSize(), false); // go through VOQ

			// when packet is passing to egress, update PSN counter and tag SwitchPSNTag
			if (Settings::switch_absolute_psn)
				m_mmu->UpdateDataPSN(outDev, dst_id, packet);

			if (!Settings::use_port_win || (m_mmu->CheckEgressWin(outDev, packet->GetSize()) && m_mmu->m_egress_cache[outDev].empty()))
				m_mmu->UpdateEgressWin(outDev, packet->GetSize(), false);	// go through egress_cache
			else{
				Ptr<PacketUnit> curr = Create<PacketUnit>(MakeCallback(&SwitchNode::DoSwitchSend, this), packet, ch, dst_id, outDev, qIndex);
				m_mmu->m_egress_cache[outDev].push(curr);	// buffer in egress_cache
				m_mmu->m_egress_cache_bytes[outDev] += packet->GetSize();
				return false;
			}

		}else{ // buffer in VOQ, do not route
			Ptr<PacketUnit> curr = Create<PacketUnit>(MakeCallback(&SwitchNode::DoDequeueVOQ, this), packet, ch, dst_id, outDev, qIndex);

			// note that we didn't create VOQ until packet should buffer in VOQ
			Ptr<VOQ> voq = m_mmu->GetVOQ(dst_id);
			voq->Enqueue(curr);

			if(Settings::adaptive_win)
				m_mmu->AdaptiveUpdateWin(dst_id);

			m_mmu->UpdateBufferingCount(dst_id, packet->GetSize(), true);

			m_mmu->UpdateMaxVOQNum();
			m_mmu->UpdateMaxActiveDstNum();

			return false;
		}

	}

	return true;
}

/*
* When receive ack
* --> restore window and route some data packets into egress
*/
bool SwitchNode::ResumeVOQWin(SwitchACKTag acktag, CustomHeader &ch, uint32_t inDev){

	uint32_t dst_id = Settings::hostIp2IdMap[ch.sip];
	// for handling loss: update lstrcv_ack_time and reset timeout event
	if (Settings::reset_only_ToR_switch_win)
		m_mmu->UpdateSynTime(dst_id/Settings::host_per_rack, dst_id, acktag);
	else
		m_mmu->UpdateSynTime(inDev, dst_id, acktag);

	if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){

		m_mmu->RecoverWin(acktag, inDev);
		m_mmu->CheckAndSendVOQ();
	}else if (Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT || Settings::switch_ack_mode == Settings::HOST_PER_PACKET_ACK){

		uint32_t resume = acktag.getAckedSize();
		if (Settings::switch_absolute_psn){
			uint32_t id = inDev;
			if (Settings::reset_only_ToR_switch_win) id = dst_id/Settings::host_per_rack;
			if (resume > m_mmu->m_rcv_ack_psn[id][dst_id]){
				m_mmu->m_rcv_ack_psn[id][dst_id] = acktag.getAckedSize();
			}
			resume = 0;
		}
		m_mmu->UpdateWin(dst_id, resume, inDev, true);		// restore window
		m_mmu->CheckAndSendVOQ(dst_id);			// Routing VOQ's packets to egress

	}

	m_mmu->CheckAndSendCache(inDev);

	if (Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT || Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT)
		return true;	// no longer transfer switch-ACK

	return false;	// go on transferring switch-ACK
}

/*
 * For SWTICH_ACCUMULATE_CREDIT mode
 */
void SwitchNode::SendAccSwitchACK(uint32_t dev, SwitchACKTag acktag){
	assert(Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT);

	if (Settings::reset_only_ToR_switch_win){
		uint32_t src = m_id * Settings::host_per_rack;
		uint32_t dst = dev * Settings::host_per_rack;

		Ptr<QbbNetDevice> tmp_device = DynamicCast<QbbNetDevice>(m_devices[1]);
		Ipv4Address dst_add(Settings::hostId2IpMap[dst]);
		Ipv4Address src_add(Settings::hostId2IpMap[src]);
		Ptr<Packet> p = tmp_device->GetSwitchACKPacket(acktag, src_add.Get(), dst_add.Get());

		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		int outId = GetOutDev(p, ch);
		Ptr<QbbNetDevice> outDev = DynamicCast<QbbNetDevice>(m_devices[outId]);
		outDev->SwitchSend(0, p, ch);
	}else{
		Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[dev]);
		device->SendSwitchACK(acktag, 0, 0);
	}
	m_mmu->CleanIngressCreditCounter(dev, acktag);
	m_mmu->UpdateIngressLastSendTime(dev, 0);	// one ingress one timer
//	std::cout << Simulator::Now() << " switch-" << m_id << "-" << inDev << " send a SwitchACK" << std::endl;
}

/*
 * For SWITCH_DST_CREDIT mode
 */
void SwitchNode::SendSwitchACK(uint32_t inDev, uint32_t dst_ip, uint32_t src_ip, uint64_t size){
	assert(Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT);
	SwitchACKTag tag;
	tag.setAckedSize(size);
	Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[inDev]);
	device->SendSwitchACK(tag, dst_ip, src_ip);
	m_mmu->CleanIngressDstCreditCounter(inDev, Settings::hostIp2IdMap[dst_ip], size);
	m_mmu->UpdateIngressLastSendTime(inDev, Settings::hostIp2IdMap[dst_ip]);
}

void SwitchNode::SendSYN(uint32_t dev, SwitchSYNTag syntag){
	if (Settings::reset_only_ToR_switch_win){
		uint32_t src = m_id * Settings::host_per_rack;
		uint32_t dst = dev * Settings::host_per_rack;

		Ptr<QbbNetDevice> tmp_device = DynamicCast<QbbNetDevice>(m_devices[1]);
		Ipv4Address dst_add(Settings::hostId2IpMap[dst]);
		Ipv4Address src_add(Settings::hostId2IpMap[src]);
		Ptr<Packet> p = tmp_device->GetSwitchSYNPacket(syntag, src_add.Get(), dst_add.Get());

		CustomHeader ch(CustomHeader::L2_Header | CustomHeader::L3_Header | CustomHeader::L4_Header);
		p->PeekHeader(ch);
		int outId = GetOutDev(p, ch);
		Ptr<QbbNetDevice> outDev = DynamicCast<QbbNetDevice>(m_devices[outId]);
		outDev->SwitchSend(0, p, ch);
	}else{
		Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[dev]);
		device->SendSwitchSYN(syntag, 0, 0);
	}
	SwitchMmu::win_out << m_id << " " << dev << " " << Simulator::Now().GetNanoSeconds() << " send SYN " << syntag.GetDataPSNEntry(0) << " "
			<< syntag.GetACKPSNEntry(0) << " " << m_mmu->m_nxt_data_psn[dev][0] << " " << m_mmu->m_rcv_ack_psn[dev][0] << std::endl;

}

/*
 * When receive SYN from upstream
 * -> Check syn and send SwitchACK if necessary
 */
void SwitchNode::ReceiveSYN(SwitchSYNTag& syntag, CustomHeader &ch, uint32_t inDev){
	uint32_t fine = inDev;
	if (Settings::reset_only_ToR_switch_win){
		fine = Settings::hostIp2IdMap[ch.sip]/Settings::host_per_rack;
	}
	std::set<uint32_t> dsts = m_mmu->CheckSYN(syntag, fine);
	SwitchMmu::win_out << m_id << " " << Settings::hostIp2IdMap[ch.dip] << " " << Simulator::Now().GetNanoSeconds() << " receive SYN " <<
			dsts.size() << " " << syntag.GetDataPSNEntry(0) << " " <<
			syntag.GetACKPSNEntry(0) << " " << m_mmu->m_lst_ack_psn[fine][0] << " " << m_mmu->m_nxt_ack_psn[fine][0] << std::endl;
	if (!dsts.empty()){
		if (Settings::switch_ack_mode == Settings::SWITCH_INGRESS_CREDIT){
			SwitchACKTag acktag = m_mmu->GetDstsSwitchACKTag(fine, dsts);
			SendAccSwitchACK(fine, acktag);
		}else if (Settings::switch_ack_mode == Settings::SWITCH_DST_CREDIT){
			while (!dsts.empty()){
				uint32_t dst_id = *dsts.begin();
				uint32_t id = inDev;
				if (Settings::reset_only_ToR_switch_win) id = Settings::hostIp2IdMap[ch.dip]/Settings::host_per_rack;
				SendSwitchACK(inDev, Settings::hostId2IpMap[*dsts.begin()], ch.dip, m_mmu->m_nxt_ack_psn[id][dst_id]);
				dsts.erase(dsts.begin());
			}
		}
	}
}

/*
 * The callback when a packet dequeued from VOQ
 */
void SwitchNode::DoDequeueVOQ(Ptr<Packet>p, CustomHeader &ch, uint32_t outDev, uint32_t qIndex){
	if (!Settings::use_port_win){
		DoSwitchSend(p, ch, outDev, qIndex); // go through
		return;
	}

	if (m_mmu->CheckEgressWin(outDev, p->GetSize()) && m_mmu->m_egress_cache[outDev].empty()){
		m_mmu->UpdateEgressWin(outDev, p->GetSize(), false);
		DoSwitchSend(p, ch, outDev, qIndex); // go through egress_cache
	}else{
		Ptr<PacketUnit> curr = Create<PacketUnit>(MakeCallback(&SwitchNode::DoSwitchSend, this), p, ch, ch.dip, outDev, qIndex);
		m_mmu->m_egress_cache[outDev].push(curr);	// buffer in egress_cache
		m_mmu->m_egress_cache_bytes[outDev] += p->GetSize();
	}
}

/*------------------------------------For statistics---------------------------------*/
uint32_t SwitchNode::DoStatistics(){
	uint32_t switch_buffer = m_mmu->GetAllEgressCache();
	SwitchMmu::max_egress_cache_bytes_all = std::max(SwitchMmu::max_egress_cache_bytes_all, switch_buffer);

	uint32_t egress_buffer = 0;
	for (uint32_t i = 0; i < m_devices.size(); i++){
		Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[i]);
		if (!!device && !!device->GetQueue()){
			uint32_t qlength = device->GetQueue()->GetNBytesTotal();
			switch_buffer += qlength;
			if (Settings::max_port_length < qlength){
				Settings::max_port_length = qlength;
				Settings::max_port_index = device->GetQueue()->m_queueId;
			}
		}

		for (uint32_t j = 0; j < m_mmu->egress_bytes[i].size(); ++j){
			egress_buffer += m_mmu->egress_bytes[i][j];
		}
	}

	uint32_t VOQ_buffer = 0;
	std::map<uint32_t, uint32_t>::iterator it = m_mmu->m_buffering.begin();
	while (it != m_mmu->m_buffering.end()){
		VOQ_buffer += it->second;
		it++;
	}

	assert(VOQ_buffer == VOQ::GetTotalBytes(m_id));
	assert(egress_buffer <= switch_buffer + VOQ::GetTotalBytes(m_id));
	Settings::max_switch_length = std::max(Settings::max_switch_length, switch_buffer + VOQ::GetTotalBytes(m_id));
	return switch_buffer;
}


void SwitchNode::PrintQlength(){
	if (output_realtime_buffer){
		uint32_t switch_buffer = DoStatistics();
		for (uint32_t i = 0; i < m_devices.size(); i++){
			Ptr<QbbNetDevice> device = DynamicCast<QbbNetDevice>(m_devices[i]);
			if (!!device && !!device->GetQueue()){
				uint32_t qlength = device->GetQueue()->GetNBytesTotal() + m_mmu->m_egress_cache_bytes[i];

				if (Settings::CC_MIBO_NSDI22 == m_ccMode || Settings::CC_EP == m_ccMode){		// proactive cc
					m_traceRealtimeQueue(m_id, device->GetQueue()->m_queueId,
							qlength, device->GetQueue()->m_bytesInQueue[Settings::DEFAULT_DATA],
							m_mmu->egress_bytes[device->GetIfIndex()].size()>Settings::DEFAULT_DATA?m_mmu->egress_bytes[device->GetIfIndex()][Settings::DEFAULT_DATA]:0,
							VOQ::GetTotalBytes(m_id, 0), VOQ::GetTotalBytes(m_id), switch_buffer + VOQ::GetTotalBytes(m_id));
					m_traceRealtimeSwitchBw(m_id, device->GetQueue()->m_queueId,
							m_mmu->m_tx_data[i], m_mmu->m_tx_ctrl[i], m_mmu->m_tx_tkn[i],
							m_mmu->m_rcv_data[i], m_mmu->m_rcv_ctrl[i], m_mmu->m_rcv_tkn[i]);
				}else{		// reactive cc
					if (Settings::fc_mode == Settings::FC_VOQ_DSTPFC){
						m_traceRealtimeQueue(m_id, device->GetQueue()->m_queueId,
								qlength, m_mmu->egress_bytes[device->GetIfIndex()][0],
								m_mmu->egress_bytes[device->GetIfIndex()].size()>1?m_mmu->egress_bytes[device->GetIfIndex()][1]:0,
								m_mmu->egress_bytes[device->GetIfIndex()].size()>2?m_mmu->egress_bytes[device->GetIfIndex()][2]:0,
								device->m_paused.find(8)==device->m_paused.end()?0:device->m_paused[8],
//								m_mmu->m_buffering_bytes[0],
								switch_buffer);
					}else{
						m_traceRealtimeQueue(m_id, device->GetQueue()->m_queueId, qlength,
								device->GetQueue()->m_bytesInQueue[Settings::DEFAULT_DATA],
								m_mmu->egress_bytes[device->GetIfIndex()].size()>Settings::DEFAULT_DATA?m_mmu->egress_bytes[device->GetIfIndex()][Settings::DEFAULT_DATA]:0,
								VOQ::GetTotalBytes(m_id, 0),
								VOQ::GetTotalBytes(m_id), switch_buffer + VOQ::GetTotalBytes(m_id));
					}
					m_traceRealtimeSwitchBw(m_id, device->GetQueue()->m_queueId,
							m_mmu->m_tx_data[i], m_mmu->m_tx_ctrl[i], m_mmu->m_tx_switchACK[i],
							m_mmu->m_rcv_data[i], m_mmu->m_rcv_ctrl[i], m_mmu->m_rcv_switchACK[i]);
				}
				m_mmu->m_tx_data[i] = 0;
				m_mmu->m_tx_ctrl[i] = 0;
				m_mmu->m_tx_tkn[i] = 0;
				m_mmu->m_tx_tknack[i] = 0;
				m_mmu->m_tx_switchACK[i] = 0;
				m_mmu->m_rcv_data[i] = 0;
				m_mmu->m_rcv_ctrl[i] = 0;
				m_mmu->m_rcv_tkn[i] = 0;
				m_mmu->m_rcv_tknack[i] = 0;
				m_mmu->m_rcv_switchACK[i] = 0;
			}
		}
		Simulator::Schedule(MicroSeconds(Settings::buffer_interval), &SwitchNode::PrintQlength, this);
	}
}

} /* namespace ns3 */
