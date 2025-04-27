/*
 * pifo-queue.h
 *
 *  Created on: May 23, 2022
 *      Author: wqy
 */

#ifndef SRC_NETWORK_UTILS_PIFO_QUEUE_H_
#define SRC_NETWORK_UTILS_PIFO_QUEUE_H_


#include <queue>
#include "ns3/packet.h"
#include "ns3/queue.h"

namespace ns3 {

class TraceContainer;

class RankTag: public Tag
{
public:
	RankTag(){ rank = 0; }
	static TypeId GetTypeId (void){
		static TypeId tid = TypeId ("ns3::RankTag") .SetParent<Tag> () .AddConstructor<RankTag> ();
		return tid;
	}
	virtual TypeId GetInstanceTypeId (void) const{ return GetTypeId(); }
	virtual uint32_t GetSerializedSize (void) const{ return sizeof(uint32_t); }
	virtual void Serialize (TagBuffer i) const{ i.WriteU32(rank); }
	virtual void Deserialize (TagBuffer i){ rank = i.ReadU32(); }
	virtual void Print (std::ostream &os) const{ }
	void SetRank(uint32_t r){rank = r;}
	uint32_t GetRank(){return rank;}
private:
	uint32_t rank;
};

struct RankPacket {
	uint32_t rank;
	Ptr<Packet> packet;

	bool operator<(const RankPacket& cmp) const{	// smaller rank, higher priority
		return rank > cmp.rank;
	}
};

/**
 * \ingroup queue
 *
 * \brief A PIFO packet queue that schedule packet depending on QoS
 */
class PIFOQueue : public Queue {
public:
  static TypeId GetTypeId (void);
  /**
   * \brief PIFOQueue Constructor
   *
   * Creates a PIFO queue
   */
  PIFOQueue ();

  virtual ~PIFOQueue();

  /**
   * Set the operating mode of this device.
   *
   * \param mode The operating mode of this device.
   *
   */
  void SetMode (PIFOQueue::QueueMode mode);

  /**
   * Get the encapsulation mode of this device.
   *
   * \returns The encapsulation mode of this device.
   */
  PIFOQueue::QueueMode GetMode (void);

private:
  // override from Class Queue but useless
  virtual bool DoEnqueue (Ptr<Packet> p);
  virtual Ptr<Packet> DoDequeue (void);
  virtual Ptr<const Packet> DoPeek (void) const;

  std::priority_queue<RankPacket> m_packets;
  uint32_t m_maxPackets;
  uint32_t m_maxBytes;
  uint32_t m_bytesInQueue;
  QueueMode m_mode;
};

} // namespace ns3



#endif /* SRC_NETWORK_UTILS_PIFO_QUEUE_H_ */
