/*
 * pifo-queue.cc
 *
 *  Created on: May 23, 2022
 *      Author: wqy
 */

#include <stdio.h>
#include <assert.h>
#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "pifo-queue.h"

NS_LOG_COMPONENT_DEFINE ("PIFOQueue");

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (PIFOQueue);

TypeId PIFOQueue::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::PIFOQueue")
    .SetParent<Queue> ()
    .AddConstructor<PIFOQueue> ()
    .AddAttribute ("Mode",
                   "Whether to use bytes (see MaxBytes) or packets (see MaxPackets) as the maximum queue size metric.",
                   EnumValue (QUEUE_MODE_BYTES),
                   MakeEnumAccessor (&PIFOQueue::SetMode),
                   MakeEnumChecker (QUEUE_MODE_BYTES, "QUEUE_MODE_BYTES",
                                    QUEUE_MODE_PACKETS, "QUEUE_MODE_PACKETS"))
    .AddAttribute ("MaxPackets",
                   "The maximum number of packets accepted by this PIFOQueue.",
                   UintegerValue (100),
                   MakeUintegerAccessor (&PIFOQueue::m_maxPackets),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxBytes",
                   "The maximum number of bytes accepted by this PIFOQueue.",
                   UintegerValue (30000 * 65535),
                   MakeUintegerAccessor (&PIFOQueue::m_maxBytes),
                   MakeUintegerChecker<uint32_t> ())
  ;

  return tid;
}

PIFOQueue::PIFOQueue () :
  Queue (),
  m_packets (),
  m_bytesInQueue (0)
{
  NS_LOG_FUNCTION_NOARGS ();
}

PIFOQueue::~PIFOQueue ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

void
PIFOQueue::SetMode (PIFOQueue::QueueMode mode)
{
  NS_LOG_FUNCTION (mode);
  m_mode = mode;
}

PIFOQueue::QueueMode
PIFOQueue::GetMode (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_mode;
}

bool
PIFOQueue::DoEnqueue (Ptr<Packet> p)
{
	NS_LOG_FUNCTION (this << p);
	if (m_mode == QUEUE_MODE_PACKETS && (m_packets.size () >= m_maxPackets))
	{
		NS_LOG_LOGIC ("Queue full (at max packets) -- droppping pkt");
		Drop (p);
		return false;
	}

	if (m_mode == QUEUE_MODE_BYTES && (m_bytesInQueue + p->GetSize () >= m_maxBytes))
	  {
		NS_LOG_LOGIC ("Queue full (packet would exceed max bytes) -- droppping pkt");
		Drop (p);
		return false;
	  }

	m_bytesInQueue += p->GetSize ();

	RankTag r_tag;
	assert(p->PeekPacketTag(r_tag));
	RankPacket r_p = {r_tag.GetRank(), p};
	m_packets.push(r_p);

	NS_LOG_LOGIC ("Number packets " << m_packets.size ());
	NS_LOG_LOGIC ("Number bytes " << m_bytesInQueue);
	return true;
}

Ptr<Packet>
PIFOQueue::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);
  if (m_packets.empty ())
  {
    NS_LOG_LOGIC ("Queue empty");
    return 0;
  }

	Ptr<Packet> p = m_packets.top().packet;
	m_packets.pop ();
	m_bytesInQueue -= p->GetSize ();

	NS_LOG_LOGIC ("Popped " << p);

	NS_LOG_LOGIC ("Number packets " << m_packets.size ());
	NS_LOG_LOGIC ("Number bytes " << m_bytesInQueue);

	return p;
}

Ptr<const Packet>
PIFOQueue::DoPeek (void) const
{
	NS_LOG_FUNCTION (this);
	if (m_packets.empty ())
	{
		NS_LOG_LOGIC ("Queue empty");
		return 0;
	}

	Ptr<Packet> p = m_packets.top().packet;
	//std::cout << "Buffer peek:" << p->GetSize() << "\n";

	NS_LOG_LOGIC ("Number packets " << m_packets.size ());
	NS_LOG_LOGIC ("Number bytes " << m_bytesInQueue);

	return p;
}

}// namespace ns3

