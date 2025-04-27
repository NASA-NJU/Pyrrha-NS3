#include <stdint.h>
#include <iostream>
#include "sack-header.h"
#include "ns3/buffer.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("sackHeader");

namespace ns3 {

	NS_OBJECT_ENSURE_REGISTERED(sackHeader);

	sackHeader::sackHeader(uint16_t pg)
		: m_pg(pg), sport(0), dport(0), flags(0), irnAckSeq(0), irnNAckSeq(0)
	{
	}

	sackHeader::sackHeader()
		: m_pg(0), sport(0), dport(0), flags(0), irnAckSeq(0), irnNAckSeq(0)
	{}

	sackHeader::~sackHeader()
	{}

	void sackHeader::SetPG(uint16_t pg)
	{
		m_pg = pg;
	}

	void sackHeader::SetACKSeq(uint32_t seq)
	{
		irnAckSeq = seq;
	}

    void sackHeader::SetNACKSeq(uint32_t seq)
	{
		irnNAckSeq = seq;
	}

	void sackHeader::SetSport(uint32_t _sport){
		sport = _sport;
	}
	void sackHeader::SetDport(uint32_t _dport){
		dport = _dport;
	}

	void sackHeader::SetTs(uint64_t ts){
		NS_ASSERT_MSG(IntHeader::mode == 1, "sackHeader cannot SetTs when IntHeader::mode != 1");
		ih.ts = ts;
	}

	void sackHeader::SetRemoteDelay(uint64_t ts){
		NS_ASSERT_MSG(IntHeader::mode == 2, "sackHeader cannot SetRemoteDelay when IntHeader::mode != 2");
		ih.remoteDelay = ts;
	}

	void sackHeader::SetCnp(){
		flags |= 1 << FLAG_CNP;
	}
	void sackHeader::SetIntHeader(const IntHeader &_ih){
		ih = _ih;
	}

	uint16_t sackHeader::GetPG() const
	{
		return m_pg;
	}

	uint32_t sackHeader::GetACKSeq() const
	{
		return irnAckSeq;
	}

    uint32_t sackHeader::GetNACKSeq() const
	{
		return irnNAckSeq;
	}

	uint16_t sackHeader::GetSport() const{
		return sport;
	}
	uint16_t sackHeader::GetDport() const{
		return dport;
	}

	uint64_t sackHeader::GetTs() const {
		NS_ASSERT_MSG(IntHeader::mode == 1, "sackHeader cannot GetTs when IntHeader::mode != 1");
		return ih.ts;
	}
	uint8_t sackHeader::GetCnp() const{
		return (flags >> FLAG_CNP) & 1;
	}

	TypeId
		sackHeader::GetTypeId(void)
	{
		static TypeId tid = TypeId("ns3::sackHeader")
			.SetParent<Header>()
			.AddConstructor<sackHeader>()
			;
		return tid;
	}
	TypeId
		sackHeader::GetInstanceTypeId(void) const
	{
		return GetTypeId();
	}
	void sackHeader::Print(std::ostream &os) const
	{
		os << "qbb:" << "pg=" << m_pg << ",irnAckSeq=" << irnAckSeq<< ",irnNAckSeq=" << irnNAckSeq;
	}
	uint32_t sackHeader::GetSerializedSize(void)  const
	{
		return GetBaseSize() + IntHeader::GetStaticSize();
		//return GetBaseSize() + IntHeader::GetStaticSize()+24;
	}
	uint32_t sackHeader::GetBaseSize() {//12 bytes
		sackHeader tmp;
		return sizeof(tmp.sport) + sizeof(tmp.dport) + sizeof(tmp.flags) + sizeof(tmp.m_pg) + sizeof(tmp.irnAckSeq)+ sizeof(tmp.irnNAckSeq);
	}
	void sackHeader::Serialize(Buffer::Iterator start)  const
	{
		Buffer::Iterator i = start;
		i.WriteU16(sport);
		i.WriteU16(dport);
		i.WriteU16(flags);
		i.WriteU16(m_pg);
		i.WriteU32(irnAckSeq);
        i.WriteU32(irnNAckSeq);

		// write IntHeader
		ih.Serialize(i);

		//24 bytes
		//for(uint32_t i=0;i<6;i+=1)
		//  	  start.WriteU32(0);
	}

	uint32_t sackHeader::Deserialize(Buffer::Iterator start)
	{
		Buffer::Iterator i = start;
		sport = i.ReadU16();
		dport = i.ReadU16();
		flags = i.ReadU16();
		m_pg = i.ReadU16();
		irnAckSeq = i.ReadU32();
        irnNAckSeq = i.ReadU32();

		// read IntHeader
		ih.Deserialize(i);

		//start.Next(24);

		return GetSerializedSize();
	}
}; // namespace ns3
