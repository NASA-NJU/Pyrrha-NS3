//liuchang

#ifndef SACK_HEADER_H
#define SACK_HEADER_H

#include <stdint.h>
#include "ns3/header.h"
#include "ns3/buffer.h"
#include "ns3/int-header.h"

namespace ns3 {

/**
 * \ingroup Pause
 * \brief Header for the Congestion Notification Message
 *
 * This class has two fields: The five-tuple flow id and the quantized
 * congestion level. This can be serialized to or deserialzed from a byte
 * buffer.
 */
 
class sackHeader : public Header
{
public:
 
  enum {
	  FLAG_CNP = 0
  };
  sackHeader (uint16_t pg);
  sackHeader ();
  virtual ~sackHeader ();

//Setters
  /**
   * \param pg The PG
   */
  void SetPG (uint16_t pg);
  void SetACKSeq(uint32_t seq);
  void SetNACKSeq(uint32_t seq);
  void SetSport(uint32_t _sport);
  void SetDport(uint32_t _dport);
  void SetTs(uint64_t ts);
  void SetRemoteDelay(uint64_t remoteDelay);
  void SetCnp();
  void SetIntHeader(const IntHeader &_ih);

//Getters
  /**
   * \return The pg
   */
  uint16_t GetPG () const;
  uint32_t GetACKSeq() const;
  uint32_t GetNACKSeq() const;
  uint16_t GetPort() const;
  uint16_t GetSport() const;
  uint16_t GetDport() const;
  uint64_t GetTs() const;
  uint8_t GetCnp() const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);
  static uint32_t GetBaseSize(); // size without INT

private:
  uint16_t sport, dport;
  uint16_t flags;
  uint16_t m_pg;
  uint32_t irnAckSeq; // for ACK and SACK to acknowledge packet received by receiver
  uint32_t irnNAckSeq; // for SACK to acknowledge receiver's expected seq
  IntHeader ih;

  
  
};

}; // namespace ns3

#endif /* QBB_HEADER */
