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
* Author: Yibo Zhu <yibzh@microsoft.com>
*/
#ifndef QBB_NET_DEVICE_H
#define QBB_NET_DEVICE_H

#include "ns3/point-to-point-net-device.h"
#include "ns3/broadcom-node.h"
#include "ns3/qbb-channel.h"
//#include "ns3/fivetuple.h"
#include "ns3/event-id.h"
#include "ns3/broadcom-egress-queue.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-header.h"
#include "ns3/udp-header.h"
#include "ns3/rdma-queue-pair.h"
#include <vector>
#include <map>
#include <ns3/rdma.h>

namespace ns3 {

class RdmaEgressQueue : public Object{
public:
	static uint32_t max_qp_num;
	static uint32_t ack_q_idx;
	uint32_t m_mtu;
	int m_qlast;
	Ptr<DropTailQueue> m_ackQ; // highest priority queue
	Ptr<DropTailQueue> m_tknQ; // data driven token queue
	Ptr<RdmaQueuePairGroup> m_qpGrp; // queue pairs

	typedef Callback<Ptr<Packet>, Ptr<RdmaQueuePair> > RdmaGetNxtPkt;

	// callback for get next packet
	RdmaGetNxtPkt m_rdmaGetNxtPkt;
	uint32_t m_rrlast;

	// wqy: rate-based credit
	RdmaGetNxtPkt m_rdmaGetTknPkt;		// callback for getting next token packet
	Time m_nxtTknTime;
	uint32_t m_rrlastTkn;
	enum {NICTokenNo = 0, NICTokenRateBased = 1};
	uint32_t m_tokenMode;
	enum {L4TokenNo = 0, L4TokenSrcRateBased = 1, L4TokenDstRateBased = 2, L4TokenDstDataDriven = 3};
	uint32_t m_l4TokenMode;
	uint32_t m_rate_limiter_mode;
	bool m_rate_limiting;
	void EnqueueToken(Ptr<Packet> p);
	void CleanToken(TracedCallback<Ptr<const Packet>, uint32_t> dropCb);

	// wqy: callback for checking sending packet
	typedef Callback<bool, Ptr<RdmaQueuePair> > RdmaCheckQPPacket;
	RdmaCheckQPPacket m_rdmaCheckData;
	RdmaCheckQPPacket m_rdmaCheckToken;
	typedef Callback<bool, Ptr<RdmaSndQueuePair> > RdmaCheckSndQPPacket;
	RdmaCheckSndQPPacket m_rdmaCheckUschQuota;

	static TypeId GetTypeId (void);
	RdmaEgressQueue();
	Ptr<Packet> DequeueQindex(int qIndex, bool is_tkn = false);
	int GetNextQindex(std::map<uint32_t, bool>& paused, uint8_t pfc_queue_mode, bool& token_mode);
	int GetLastQueue();
	uint32_t GetNBytes(uint32_t qIndex);
	uint32_t GetFlowCount(void);
	Ptr<RdmaQueuePair> GetQp(uint32_t i);
	void RecoverQueue(uint32_t i);
	void EnqueueHighPrioQ(Ptr<Packet> p);
	void CleanHighPrio(TracedCallback<Ptr<const Packet>, uint32_t> dropCb);

	TracedCallback<Ptr<const Packet>, uint32_t> m_traceRdmaEnqueue;
	TracedCallback<Ptr<const Packet>, uint32_t, bool> m_traceRdmaDequeue;
};

/**
 * \class QbbNetDevice
 * \brief A Device for a IEEE 802.1Qbb Network Link.
 */
class QbbNetDevice : public PointToPointNetDevice 
{
	friend class SwitchNode;
public:

  /**
   * The queues for each priority class.
   * @see class Queue
   * @see class InfiniteQueue
   */
  Ptr<BEgressQueue> m_queue;

  static TypeId GetTypeId (void);

  QbbNetDevice ();
  virtual ~QbbNetDevice ();

  /**
   * Receive a packet from a connected PointToPointChannel.
   *
   * This is to intercept the same call from the PointToPointNetDevice
   * so that the pause messages are honoured without letting
   * PointToPointNetDevice::Receive(p) know
   *
   * @see PointToPointNetDevice
   * @param p Ptr to the received packet.
   */
  virtual void Receive (Ptr<Packet> p);

  /**
   * Send a packet to the channel by putting it to the queue
   * of the corresponding priority class
   *
   * @param packet Ptr to the packet to send
   * @param dest Unused
   * @param protocolNumber Protocol used in packet
   */
  virtual bool Send(Ptr<Packet> packet, const Address &dest, uint16_t protocolNumber);
  virtual bool SwitchSend (uint32_t qIndex, Ptr<Packet> packet, CustomHeader &ch);

  /**
   * Get the size of Tx buffer available in the device
   *
   * @return buffer available in bytes
   */
  //virtual uint32_t GetTxAvailable(unsigned) const;

  /**
   * TracedCallback hooks
   */
  void ConnectWithoutContext(const CallbackBase& callback);
  void DisconnectWithoutContext(const CallbackBase& callback);

  bool Attach (Ptr<QbbChannel> ch);

   virtual Ptr<Channel> GetChannel (void) const;

   void SetQueue (Ptr<BEgressQueue> q);
   Ptr<BEgressQueue> GetQueue ();
   virtual bool IsQbb(void) const;
   void NewQp(Ptr<RdmaQueuePair> qp);
   void ReassignedQp(Ptr<RdmaSndQueuePair> qp);
   void TriggerTransmit(void);

	void SendPfc(uint32_t qIndex, uint32_t type); // type: 0 = pause, 1 = resume
	// Congestion isolation
	TracedCallback<uint8_t, uint32_t, uint32_t, uint32_t, uint32_t> m_traceCI; // <type(0: pause, 1: resume, 2: dealloc), root_switch, root_port>
	Ptr<Packet> GetCIPacket(uint32_t root_switch, uint32_t root_port, uint8_t type, uint16_t hop = 0, uint32_t old_switch = 0, uint32_t old_port = 0);
	void SendCIPacket(uint32_t root_switch, uint32_t root_port, uint8_t type, uint16_t hop = 0, uint32_t old_switch = 0, uint32_t old_port = 0);
	void PushOrderMark(uint32_t qIndex, uint32_t blocked_q);
	// Floodgate
	void SendSwitchACK(SwitchACKTag acktag, uint32_t src = 0, uint32_t dst = 0);
	Ptr<Packet> GetSwitchACKPacket(SwitchACKTag acktag, uint32_t src = 0, uint32_t dst = 0);
	void SendSwitchSYN(SwitchSYNTag acktag, uint32_t src = 0, uint32_t dst = 0);
	Ptr<Packet> GetSwitchSYNPacket(SwitchSYNTag acktag, uint32_t src = 0, uint32_t dst = 0);

	TracedCallback<Ptr<const Packet>, uint32_t> m_traceEnqueue;
	TracedCallback<Ptr<const Packet>, uint32_t> m_traceDequeue;
	TracedCallback<Ptr<const Packet>, uint32_t> m_traceDrop;
	TracedCallback<uint32_t, uint32_t> m_tracePfc; // 0: resume, 1: pause
	uint8_t m_pfc_fine;		// pfc pause/resume data packet based on PG/DST/QP
protected:

  bool TransmitStart (Ptr<Packet> p);
  
  virtual void DoDispose(void);

  /// Reset the channel into READY state and try transmit again
  virtual void TransmitComplete(void);

  /// Look for an available packet and send it using TransmitStart(p)
  virtual void DequeueAndTransmit(void);

  /// Resume a paused queue and call DequeueAndTransmit()
  virtual void Resume(unsigned qIndex);

  Ptr<QbbChannel> m_channel;

  // PFC
  bool m_qbbEnabled;	//< PFC behaviour enabled
  bool m_qcnEnabled;
  bool m_dynamicth;
  uint32_t m_pausetime;	//< Time for each Pause
  /*
   * Under different nodes(switch/host), the key of m_paused has different meaning which defined in settings(in third.cc)
   * Generally, when it's a device on switches, the key is represent for index of Queue.
   */
  std::map<uint32_t, bool> m_paused; // <qIndex, paused>

  // QCN
  /* RP parameters */
  EventId  m_nextSend;		//< The next send event
  /* State variable for rate-limited queues */

//  struct ECNAccount{
//	  Ipv4Address source;
//	  uint32_t qIndex;
//	  uint32_t port;
//	  uint8_t ecnbits;
//	  uint16_t qfb;
//	  uint16_t total;
//  };
//
//  std::vector<ECNAccount> *m_ecn_source;

public:
	Ptr<RdmaEgressQueue> m_rdmaEQ;
	void RdmaEnqueueHighPrioQ(Ptr<Packet> p);

	// callback for processing packet in RDMA
	typedef Callback<int, Ptr<Packet>, CustomHeader&> RdmaReceiveCb;
	RdmaReceiveCb m_rdmaReceiveCb;
	// callback for link down
	typedef Callback<void, Ptr<QbbNetDevice> > RdmaLinkDownCb;
	RdmaLinkDownCb m_rdmaLinkDownCb;
	// callback for sent a packet
	typedef Callback<void, Ptr<RdmaQueuePair>, Ptr<Packet>, Time, bool > RdmaPktSent;
	RdmaPktSent m_rdmaPktSent;

	Ptr<RdmaEgressQueue> GetRdmaQueue();
	void TakeDown(); // take down this device
	void UpdateNextAvail(Time t);

	TracedCallback<Ptr<const Packet>, Ptr<RdmaQueuePair>, bool > m_traceQpDequeue; // the trace for printing dequeue

	//liuchang 
	std::vector<uint32_t> m_queue_size; // queueid----how many flows in this queue
	//find a queue for a flow
	int GetQueueforFlow();
	//update the number of flows in a queue
	bool UpdateQueueforFlow(int idx, bool ad);
};

} // namespace ns3

#endif // QBB_NET_DEVICE_H
