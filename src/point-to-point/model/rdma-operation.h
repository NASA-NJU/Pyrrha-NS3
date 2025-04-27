/*
 * rdma-operation.h
 *
 *  Created on: Aug 4, 2021
 *      Author: wqy
 */

#ifndef SRC_POINT_TO_POINT_MODEL_RDMA_OPERATION_H_
#define SRC_POINT_TO_POINT_MODEL_RDMA_OPERATION_H_

#include <ns3/object.h>
#include <ns3/packet.h>
#include <ns3/ipv4-address.h>
#include <ns3/data-rate.h>
#include <ns3/event-id.h>
#include <ns3/custom-header.h>
#include <ns3/int-header.h>
#include <vector>
#include <queue>
#include "ns3/settings.h"
#include "ns3/broadcom-egress-queue.h"

namespace ns3 {

class Credit: public Object {
public:
	Credit(Time t, uint32_t tkn_psn):m_rcvTime(t), m_tkn_psn(tkn_psn){ m_remain = Settings::packet_payload; m_ecn = false;};
	inline const Time GetTime(){return m_rcvTime;}
	inline const uint32_t GetRemain() {return m_remain;}
	uint32_t UseRemain(uint32_t& used){
		if (used >= m_remain) {
			used -= m_remain;
			m_remain = 0;
		}else {
			m_remain -= used;
			used = 0;
		}
		return m_remain;
	}
	uint32_t GetPSN(){
		return m_tkn_psn;
	}
	void SetECN(bool ecn) {
		m_ecn = ecn;
	}
	bool GetECN(){
		return m_ecn;
	}
private:
	Time m_rcvTime;
	uint32_t m_tkn_psn;
	uint32_t m_remain;
	bool m_ecn;
};

struct CreditPSNCMP
{
    bool operator()(Ptr<Credit> a, Ptr<Credit> b)
    {
        return a->GetPSN()  > b->GetPSN();
    }
};

class RdmaOperation: public Object {
public:
	bool is_sndMsg;
	uint32_t m_qpid;
	uint32_t m_msgSeq;
	uint32_t m_flowId;
	uint32_t sip, dip;
	uint16_t sport, dport;
	uint32_t m_size;
};

struct RdmaOperationMsgSeqCMP
{
    bool operator()(Ptr<RdmaOperation> a, Ptr<RdmaOperation> b)
    {
        return a->m_msgSeq  > b->m_msgSeq;
    }
};

class RdmaSndOperation : public RdmaOperation {
public:
	bool m_isTestFlow;
	uint32_t m_mtu;
	Time startTime;
	Time startTransferTime;
	Time m_lastActionTime;
	uint16_t m_pg;
	uint32_t m_src;
	uint32_t m_dst;
	// routing hops
	RoutingTag routingTag;
	// QoS rank
	RankTag qosRankTag;

	/**
	 * runtime
	 */
	uint64_t snd_nxt, snd_una; // next seq to send, the highest unacked seq
	bool timeouted;
	bool m_received_last_ack;
	std::set<uint32_t> m_sent_psn;
	std::set<uint32_t> m_timeout_psn;

	static TypeId GetTypeId (void);
	RdmaSndOperation(uint16_t pg, uint32_t _sip, uint32_t _dip, uint16_t _sport, uint16_t _dport);
	void SetQPId(uint32_t);
	void SetMSGSeq(uint32_t);
	void SetSize(uint32_t);
	void SetSrc(uint32_t);
	void SetDst(uint32_t);
	void SetMTU(uint32_t);
	void SetFlowId(uint32_t);
	void SetTestFlow(bool);
	void UpdateQoSRank();
	void ResetLastActionTime();
	void Recover();
	uint32_t GetNxtPayload(uint32_t& seq, bool isSending);
	uint64_t GetOnTheFly();
	uint64_t GetBytesLeft();
	bool IsFinished();
	void Acknowledge(uint64_t ack, bool isLastACK = false);
	void UpdateReceivedACK();

	/**
	 * for IRN retransmission
	*/

	/**
	 * IRN tx bitmap state
	 */
	enum IRN_STATE {
		UNACK, // Not acked
		ACK, // Acked
		NACK, // Lost
		UNDEF // Out of window
	};
	class Irn
	{
		public:
		/**
     * After generate and send a new packet, update its IRN bitmap
     * 
     * \param payloadSize log new packet payload size
     */
    void SendNewPacket (uint32_t payloadSize);

    /**
     * Get IRN bitmap state of this sequence number
     * 
     * \param seq sequence number
     * \return IRN bitmap state
     */
    IRN_STATE GetIrnState (const uint32_t &seq) const;

    /**
     * Get payload size of this sequence number
     * 
     * \param seq sequence number
     * \return payload size
     */
    uint64_t GetPayloadSize (const uint32_t &seq) const;

    /**
     * Move IRN bitmap window
     */
    void MoveWindow ();

    /**
     * Update IRN state after received ACK
     * 
     * \param seq ACKed sequence number
     */
    void AckIrnState (const uint32_t &seq);

    /**
     * Update IRN state after received SACK
     * 
     * \param seq ACKed sequence number
     * \param ack expected sequence number
     */
    void SackIrnState (const uint32_t &seq, const uint32_t &ack);

    /**
     * Log retransmission event id
     * 
     * \param seq sequence number
     * \param id NS3 event ID
     */
    void SetRtxEvent (const uint32_t &seq, const EventId &id);

    /**
     * Get next sequence number
     * 
     * \return expected sequence number
     */
    uint32_t GetNextSequenceNumber () const;

    /**
     * Get bitmap window size by packet count
     * 
     * \return windows size by packet count
     */
    uint32_t GetWindowSize () const;

	/**
	 * get bitmap window base seq
	 * 
	 * \return bitmap window base seq
	*/
	uint32_t GetBaseSeq () const;

	void SetSeqState(const uint32_t &seq, RdmaSndOperation::IRN_STATE state);

	// for irn retransmisson
	uint32_t m_rtxQueuingCnt = 0;
	std::queue<uint32_t> m_rtxSeqQueues; //packet sequence that need retransmit

  private:
    std::deque<IRN_STATE> m_states; //!< packet state bitmap window
    std::deque<uint64_t> m_payloads; //!< packet payload bitmap window
    std::deque<EventId> m_rtxEvents; //!< packet retransmission event bitmap window
    uint32_t m_baseSeq = 0; //!< bitmap window base sequence i.e. number of index 0
	}m_irn;
};

struct RdmaOperationLastActionTimeCMP
{
    bool operator()(Ptr<RdmaSndOperation> a, Ptr<RdmaSndOperation> b)
    {
        return a->m_lastActionTime  > b->m_lastActionTime;
    }
};

class RdmaRxOperation: public RdmaOperation {
public:
	uint32_t ReceiverNextExpectedSeq;
	Time m_nackTimer;
	uint32_t m_lastNACK;
	int32_t m_milestone_rx;

	/**
	 * add by wqy on 2020/11/9
	 * Used when use packet-level-routing
	 */
	bool m_received_last_psn_packet;
	std::set<uint32_t> m_received_psn;

	/**
	 * add by wqy on 2021/3/8
	 * For queuing time analysis
	 */
	std::vector<uint64_t> m_queuingTime[Settings::MAXHOP];	// ns

	static TypeId GetTypeId (void);
	RdmaRxOperation();
	bool ReceivedAll(uint32_t payload);

   	/**
   	* IRN rx bitmap state
   	*/
	enum IRN_STATE {
		ACK, // Acknowledged
		NACK, // Lost
		UNDEF // Out of window
	};

	/**
	 * \ingroup rdma-operation
	 * \class Irn
	 * \brief Rdma rx queue pair IRN infomation.
	 */
	class Irn
	{
	public:
		/**
		 * Get IRN bitmap state of this sequence number
		 * 
		 * \param seq sequence number
		 * \return IRN bitmap state
		 */
		IRN_STATE GetIrnState (const uint32_t &seq) const;

		/**
		 * Move IRN bitmap window
		 */
		void MoveWindow ();

		/**
		 * After received a packet, update its IRN bitmap
		 * 
		 * \param seq sequence number
		 */
		void UpdateIrnState (const uint32_t &seq);

		/**
		 * Get next sequence number
		 * 
		 * \return expected sequence number
		 */
		uint32_t GetNextSequenceNumber () const;

		/**
		 * Is target sequence number of packet was received
		 * 
		 * \param seq sequence number
		 * \return true for received, false for new packet
		 */
		bool IsReceived (const uint32_t &seq) const;

		/**
		 * get bitmap window base seq
		 * 
		 * \return bitmap window base seq
		*/
		uint32_t GetBaseSeq () const;

	private:
		std::deque<IRN_STATE> m_states; //!< packet state bitmap window
		// std::map<uint32_t, IRN_STATE> m_states; //!< packet state bitmap window, seq2state
		uint32_t m_baseSeq = 0; //!< bitmap window base sequence i.e. number of seq 0
		// uint32_t m_expseq = 0; // expected packet seq
	} m_irn; //!< IRN infomation

};

}


#endif /* SRC_POINT_TO_POINT_MODEL_RDMA_OPERATION_H_ */
