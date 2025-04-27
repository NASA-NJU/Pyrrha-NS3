/*
 * rdma-operation.cc
 *
 *  Created on: Aug 4, 2021
 *      Author: wqy
 */

#include "rdma-operation.h"
#include <ns3/simulator.h>
#include <assert.h>

namespace ns3 {
/**************************
 * RdmaSndOperation
 *************************/
TypeId RdmaSndOperation::GetTypeId (void)
{
	static TypeId tid = TypeId ("ns3::RdmaSndOperation")
		.SetParent<Object> ()
		;
	return tid;
}

RdmaSndOperation::RdmaSndOperation(uint16_t pg, uint32_t _sip, uint32_t _dip, uint16_t _sport, uint16_t _dport){
	is_sndMsg = true;
	m_pg = pg;
	sip = _sip;
	dip = _dip;
	sport = _sport;
	dport = _dport;
	snd_nxt = 0;
	snd_una = 0;
	startTime = Simulator::Now();
	m_lastActionTime = Simulator::Now();
	m_qpid = 0;
	m_msgSeq = 0;
	m_size = 0;
	m_src = 0;
	m_dst = 0;
	timeouted = false;
	m_received_last_ack = false;
}

void RdmaSndOperation::UpdateQoSRank(){
	qosRankTag.SetRank(Settings::GetRank(m_size));
}

void RdmaSndOperation::SetQPId(uint32_t qpId){
	m_qpid = qpId;
}

void RdmaSndOperation::SetMSGSeq(uint32_t msgSeq){
	m_msgSeq = msgSeq;
}

void RdmaSndOperation::SetSize(uint32_t size){
	m_size = size;
}

void RdmaSndOperation::SetSrc(uint32_t src){
	m_src = src;
}

void RdmaSndOperation::SetDst(uint32_t dst){
	m_dst = dst;
}

void RdmaSndOperation::SetTestFlow(bool isTestFlow){
	m_isTestFlow = isTestFlow;
}

void RdmaSndOperation::SetFlowId(uint32_t flow_id){
	m_flowId = flow_id;
}

void RdmaSndOperation::SetMTU(uint32_t mtu){
	m_mtu = mtu;
}

void RdmaSndOperation::ResetLastActionTime(){
	m_lastActionTime = Simulator::Now();
}

void RdmaSndOperation::Recover(){
	timeouted = true;
	m_timeout_psn = m_sent_psn;
	if (!Settings::AllowDisOrder())
		snd_nxt = snd_una;
}

uint32_t RdmaSndOperation::GetNxtPayload(uint32_t& seq, bool isSending){
	seq = snd_nxt;
	if (Settings::AllowDisOrder()){
		if (seq >= m_size && timeouted && m_timeout_psn.size() > 0){
			/**
			 * Under packet-level-routing, don't use go-back-N.
			 * Flow has sent out all packets at once (qp->snd_nxt >= qp->m_size),
			 * however, till timeout triggered(qp->timeouted), there are some in-flight data packet(m_timeout_psn).
			 * These (ACKs of)packets may dropped, so retransmit these packets.
			 */
			seq = *(m_timeout_psn.begin());
			if (isSending){
				m_timeout_psn.erase(m_timeout_psn.begin());
				assert(seq < m_size);
				if (m_timeout_psn.size() == 0)	{	// start a new round of timeout check
					timeouted = false;
				}
			}
		}else{
			if (isSending) {m_sent_psn.insert(seq);}
		}
	}

	uint32_t payload_size = m_mtu;
	if (seq + m_mtu >= m_size){
		payload_size = m_size - seq;
	}
	return payload_size;
}

uint64_t RdmaSndOperation::GetBytesLeft(){
	uint64_t result = m_size >= snd_nxt ? m_size - snd_nxt : 0;
	if (Settings::AllowDisOrder() && timeouted && m_timeout_psn.size() > 0){
		/**
		 * When the routing is packet-level and has timeouted
		 * --> the packets in `m_timeout_psn` will be sent
		 */
		if (m_size%Settings::packet_payload != 0){
			result = (m_timeout_psn.size() - 1) * Settings::packet_payload;
			if (m_timeout_psn.count(m_size - m_size%Settings::packet_payload))
				// if last packet has not sent --> add the last packet
				result += m_size%Settings::packet_payload;
			else
				// if last packet has sent --> add a whole packet
				result += Settings::packet_payload;
		}else{
			result = m_timeout_psn.size() * Settings::packet_payload;
		}
	}
	if (Settings::use_irn && m_irn.m_rtxQueuingCnt > 0){
		result += m_irn.m_rtxQueuingCnt;
	}
	return result;
}

uint64_t RdmaSndOperation::GetOnTheFly(){
	if (!Settings::AllowDisOrder()){
		if(Settings::use_irn){
			int num = m_irn.GetWindowSize();
			int sq = m_irn.GetBaseSeq();
			for (int i = 0; i < m_irn.GetWindowSize(); i++) {
				if(m_irn.GetIrnState(sq) == RdmaSndOperation::ACK || m_irn.GetIrnState(sq) == RdmaSndOperation::NACK) {
					num--;
				}
				sq += Settings::packet_payload;
			}
			return num;
		}else{
			return snd_nxt - snd_una;
		}
	}
	else{
		uint32_t result = 0;
		if (m_size%Settings::packet_payload != 0){
			result = (m_sent_psn.size() - 1) * Settings::packet_payload;
			if (m_sent_psn.count(m_size - m_size%Settings::packet_payload))
				// if last packet has not sent --> add the last packet
				result += m_size%Settings::packet_payload;
			else
				// if last packet has sent --> add a whole packet
				result += Settings::packet_payload;
		}else{
			result = m_sent_psn.size() * Settings::packet_payload;
		}
		if (timeouted) {
			/**
			 * When flow has triggered timeout,
			 * m_timeout_psn stands for the remaining packets which should retransmit
			 * m_sent_psn stands for the all packets which should retransmit
			 * so, m_sent_psn - m_timeout_psn stands for the inflight packets
			 */
			uint32_t timeout_sent = 0;
			if (m_size%Settings::packet_payload != 0){
				timeout_sent = (m_timeout_psn.size() - 1) * Settings::packet_payload;
				if (m_timeout_psn.count(m_size - m_size%Settings::packet_payload))
					// if last packet has not sent --> add the last packet
					timeout_sent += m_size%Settings::packet_payload;
				else
					// if last packet has sent --> add a whole packet
					timeout_sent += Settings::packet_payload;
			}else{
				timeout_sent = m_timeout_psn.size() * Settings::packet_payload;
			}
			assert(timeout_sent <= result);
			result -= timeout_sent;
		}
		return result;
	}
}

bool RdmaSndOperation::IsFinished(){
	if (!Settings::AllowDisOrder())
		return snd_una >= m_size;
	else
		return m_received_last_ack;
}

void RdmaSndOperation::Acknowledge(uint64_t ack, bool isLastACK){
	if (Settings::AllowDisOrder()){
		if (m_sent_psn.count(ack)) {
			m_sent_psn.erase(ack);
			if (m_timeout_psn.count(ack))
				m_timeout_psn.erase(ack);
			assert(0 == m_sent_psn.count(ack));
			UpdateReceivedACK();
		}
		if (isLastACK) m_received_last_ack = true;
	}else{
		if (ack > snd_una){
			snd_una = ack;
		}
	}
}

void RdmaSndOperation::UpdateReceivedACK(){
	assert(Settings::AllowDisOrder());
	if (m_sent_psn.empty()){
		// all sent packets have received.
		snd_una = snd_nxt;
	}else{
		snd_una = *(m_sent_psn.begin());
	}
}

// IRN inner class implementation
void
RdmaSndOperation::Irn::SendNewPacket (uint32_t payloadSize)
{
  m_states.push_back (RdmaSndOperation::IRN_STATE::UNACK);
  m_payloads.push_back (payloadSize);
  m_rtxEvents.push_back (EventId ());
}

RdmaSndOperation::IRN_STATE
RdmaSndOperation::Irn::GetIrnState (const uint32_t &seq) const
{
  if (seq >= GetNextSequenceNumber ())
    return IRN_STATE::UNDEF;
  else if (seq >= m_baseSeq) {
	uint32_t idx = (seq - m_baseSeq) / Settings::packet_payload;
	return m_states[idx];
  }
  else
    return IRN_STATE::ACK;
}

uint64_t
RdmaSndOperation::Irn::GetPayloadSize (const uint32_t &seq) const
{
  if (seq >= GetNextSequenceNumber () || seq < m_baseSeq)
    {
      NS_ASSERT_MSG (false, "RdmaSndOperation::m_irn::GetPayloadSize: "
                            "Out of bound sequence number");
      return 0;
    }
  else {
	uint32_t idx = (seq - m_baseSeq) / Settings::packet_payload;
	return m_payloads[idx];
  }
}

void
RdmaSndOperation::Irn::MoveWindow (){
  while (!m_states.empty () && m_states.front () == IRN_STATE::ACK){
    	m_states.pop_front ();
    	m_payloads.pop_front ();
    	m_rtxEvents.pop_front ();
		m_baseSeq += Settings::packet_payload;
    }
}

void
RdmaSndOperation::Irn::AckIrnState (const uint32_t &seq)
{
  if (GetIrnState (seq) == IRN_STATE::UNDEF)
    {
      NS_ASSERT_MSG (false, "RdmaSndOperation::m_irn::AckIrnState: "
                            "Out of bound sequence number");
    }
  else if (GetIrnState (seq) == IRN_STATE::UNACK || GetIrnState (seq) == IRN_STATE::NACK)
    {
	  uint32_t idx = (seq - m_baseSeq) / Settings::packet_payload;
      m_states[idx] = IRN_STATE::ACK;
      // Cancel timer
      Simulator::Cancel (m_rtxEvents[idx]);
    }
  // If is ACK, do nothing.
  MoveWindow ();
}

void
RdmaSndOperation::Irn::SackIrnState (const uint32_t &seq, const uint32_t &ack)
{
  if (GetIrnState (seq) == IRN_STATE::UNDEF)
    {
      NS_ASSERT_MSG (false, "RdmaSndOperation::m_irn::SackIrnState: "
                            "Out of bound sequence number");
    }
  else if (GetIrnState (seq) == IRN_STATE::UNACK || GetIrnState (seq) == IRN_STATE::NACK)
    {
      auto expIndex = (ack - m_baseSeq) / Settings::packet_payload;
      auto index = (seq - m_baseSeq) / Settings::packet_payload;
      m_states[index] = IRN_STATE::ACK;
      // Set NACK sequence
      for (auto i = expIndex; i < index; i++)
        {
          m_states[i] = IRN_STATE::NACK;
          // Cancel timer (because we will set timer when retransmitting)
          Simulator::Cancel (m_rtxEvents[i]);
        }
    }
	
  MoveWindow ();
}

void
RdmaSndOperation::Irn::SetRtxEvent (const uint32_t &seq, const EventId &id)
{
  if (seq >= m_baseSeq && seq < GetNextSequenceNumber ())
    {
	  uint32_t idx = (seq - m_baseSeq) / Settings::packet_payload;
      Simulator::Cancel (m_rtxEvents[idx]);
      m_rtxEvents[idx] = id;
    }
  else
    {
      NS_ASSERT_MSG (false, "RdmaSndOperation::m_irn::SetRtxEvent: "
                            "Invalid sequence number");
    }
}

uint32_t
RdmaSndOperation::Irn::GetNextSequenceNumber () const
{
  return m_baseSeq + m_states.size() * Settings::packet_payload;
}

uint32_t
RdmaSndOperation::Irn::GetWindowSize () const
{
  return m_states.size ();
}

uint32_t RdmaSndOperation::Irn::GetBaseSeq () const
{
	return m_baseSeq;
}

void RdmaSndOperation::Irn::SetSeqState(const uint32_t &seq, RdmaSndOperation::IRN_STATE state){
	m_states[(seq - m_baseSeq) / Settings::packet_payload] = state;
}

/*********************
 * RdmaRxOperation
 ********************/
TypeId RdmaRxOperation::GetTypeId (void)
{
	static TypeId tid = TypeId ("ns3::RdmaRxOperation")
		.SetParent<Object> ()
		;
	return tid;
}

RdmaRxOperation::RdmaRxOperation(){
	is_sndMsg = false;
	sport = dport = 0;
	m_nackTimer = Time(0);
	m_lastNACK = 0;
	m_received_last_psn_packet = false;
	m_milestone_rx = 0;
	ReceiverNextExpectedSeq = 0;
}

bool RdmaRxOperation::ReceivedAll(uint32_t payload){
	if (!m_received_last_psn_packet) return false;
	uint32_t expected = 0;
	for (auto it:m_received_psn){
		if (expected != it) return false;
		expected += payload;
	}
	return true;
}

// IRN inner class implementation
RdmaRxOperation::IRN_STATE
RdmaRxOperation::Irn::GetIrnState (const uint32_t &seq) const
{
  if (seq >= GetNextSequenceNumber ()) // out of window
    return IRN_STATE::UNDEF;
  else if (seq >= m_baseSeq) // in window
    return m_states[(seq - m_baseSeq) / Settings::packet_payload];
  else // before window
    return IRN_STATE::ACK;
}

void
RdmaRxOperation::Irn::MoveWindow ()
{
  while (!m_states.empty () && m_states.front () == IRN_STATE::ACK)
    {
      m_states.pop_front ();
      m_baseSeq += Settings::packet_payload;
    }
}

void
RdmaRxOperation::Irn::UpdateIrnState (const uint32_t &seq)
{
  // Packet not seen before
  if (GetIrnState (seq) == IRN_STATE::UNDEF)
    {
      // Sequence number out of order
      if (seq > GetNextSequenceNumber ())
        {
          while (seq > GetNextSequenceNumber ())
            m_states.push_back (IRN_STATE::NACK);
        }
      // ACK this packet
      m_states.push_back (IRN_STATE::ACK);
    }
  // Retransmission packet
  else if (GetIrnState (seq) == IRN_STATE::NACK)
    {
      // Ack this packet
      m_states[(seq - m_baseSeq) / Settings::packet_payload] = IRN_STATE::ACK;
    }
  // If is ACKed, do nothing.
  MoveWindow ();
}

uint32_t
RdmaRxOperation::Irn::GetNextSequenceNumber () const
{
  return m_baseSeq + m_states.size() * Settings::packet_payload;
}

bool
RdmaRxOperation::Irn::IsReceived (const uint32_t &seq) const
{
  return GetIrnState (seq) == IRN_STATE::ACK;
}

uint32_t
RdmaRxOperation::Irn::GetBaseSeq () const
{
  return m_baseSeq;
}

}
