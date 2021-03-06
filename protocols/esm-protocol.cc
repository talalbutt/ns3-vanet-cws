/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "esm-protocol.h"

#include "dsrc-bsm-header.h"
#include "../vehicle-mobility-model.h"
#include "../default-parameters.h"
#include "ns3/log.h"
#include "ns3/assert.h"

NS_LOG_COMPONENT_DEFINE ("EsmProtocol");
uint32_t Esm::CreateMessageId(uint32_t origin, uint32_t event)
{
  return origin * MAX_COUNT + event;
}
Esm::Esm(uint32_t origin, uint32_t event, Time originTime) :
      originId(origin), eventId(event), sentMessages(0), originTimestamp(originTime), scheduledEvent()
{
  NS_ASSERT(origin < MAX_COUNT);
  NS_ASSERT(event < MAX_COUNT);
}
Esm::~Esm()
{ }
uint32_t Esm::GetMessageId() const
{
  return originId * MAX_COUNT + eventId;
}
void Esm::DoDispose()
{
  scheduledEvent = EventId();
}


//const uint16_t EsmProtocol::PROTOCOL_NUMBER = 0x88DD;
uint16_t EsmProtocol::s_messageSize = DP_ESM_SIZE;
EsmProtocol::RecvCallback EsmProtocol::s_recvCallback;
EsmProtocol::SendCallback EsmProtocol::s_sendCallback;

NS_OBJECT_ENSURE_REGISTERED (EsmProtocol);
TypeId 
EsmProtocol::GetTypeId(void) {
  static TypeId tid = TypeId("EsmProtocol")
    .SetParent<WsmProtocol> ()
    .AddConstructor<EsmProtocol> () 
    ;
  return tid;
}
void EsmProtocol::SetRecvCallback(EsmProtocol::RecvCallback cb)
{
  s_recvCallback = cb;
}
void EsmProtocol::SetSendCallback(EsmProtocol::SendCallback cb)
{
  s_sendCallback = cb;
}
void EsmProtocol::SetMessageSize(uint32_t size)
{
  s_messageSize = size;
}

EsmProtocol::EsmProtocol() : WsmProtocol(ESMP_NUMBER, ESMP_PRIORITY), m_currentEventId(0), m_history()
{ }

EsmProtocol::~EsmProtocol() {
}

int EsmProtocol::GetTxPowerLevel() const
{
  return -1;
}
//void EsmProtocol::ReceiveBeacon(uint32_t senderId, uint32_t receiverId, Time delay) { }

void EsmProtocol::SendInitialWarning() {
  NS_ASSERT (GetNode() != 0);
}

void EsmProtocol::DoReceivePacket(Ptr< Node > node, Ptr< Packet > packet, const WsmpHeader& wsmpHeader)
{
  //NS_LOG_DEBUG("@" << Simulator::Now() << " [EsmProtocol::DoReceivePacket] packet=" << *packet);
  //uint32_t nid = node->GetId();
  EsmpHeader esmpHeader;
  packet->RemoveHeader(esmpHeader);
  DsrcBsmHeader bsmHeader;
  packet->RemoveHeader(bsmHeader);
  //g_stat->UpdateEsmReceived(nid, mid, latency.GetSeconds()); //move this to exp
  //NS_LOG_DEBUG("@" << Simulator::Now() << " [EsmProtocol::DoReceivePacket] Receive: id=" << rid << ", senderId=" << header.GetSenderId());
  //NS_LOG_DEBUG("@" << Simulator::Now() << " [EsmProtocol::DoReceivePacket] from=" << from);
  //NotifyFirstEsmReceived
  
  if ( ReceiveEsm(node, packet, wsmpHeader, esmpHeader, bsmHeader) ) {
    uint32_t mid = Esm::CreateMessageId( esmpHeader.GetOriginId(), esmpHeader.GetEventId() );
    Time latency = Simulator::Now() - esmpHeader.GetOriginTimeStamp();
    if (!s_recvCallback.IsNull()) s_recvCallback(node->GetId(), mid, latency);
  }
}

void EsmProtocol::AddReceivers(Ptr< Esm > esm, EsmpHeader& esmpHeader)
{ }

Ptr<Esm> 
EsmProtocol::CreateNewEvent() {
  Ptr<Esm> esm = GetLastEvent();
  if (esm != 0) {
    NS_LOG_DEBUG("[EsmProtocol::StartNewWarning] cancel previous event");
    esm->scheduledEvent.Cancel(); //cancel the previous event
    esm = 0;
  }
  m_currentEventId++;
  NS_LOG_DEBUG("[EsmProtocol::CreateNewEvent] eventId=" << m_currentEventId);
  esm = CreateEsm(GetNode()->GetId(), m_currentEventId, Simulator::Now());
  AddHistory(esm);
  return esm;
}

Ptr<Esm> 
EsmProtocol::GetLastEvent() {
  return GetHistory(GetNode()->GetId(), m_currentEventId);
}

Ptr<Esm> 
EsmProtocol::AddHistory(uint32_t originId, uint32_t eventId) {
  Ptr<Esm> esm = CreateEsm(originId, eventId, Simulator::Now());
  m_history[esm->GetMessageId()] = esm;
  return esm;
}

void 
EsmProtocol::AddHistory(Ptr< Esm > esm)
{
  m_history[esm->GetMessageId()] = esm;
}

Ptr<Esm> 
EsmProtocol::GetHistory(uint32_t messageId) {
  THistory::iterator iter = m_history.find(messageId);
  if (iter != m_history.end()) {
    return iter->second;
  } else {
    return 0;
  }
}

Ptr<Esm> 
EsmProtocol::GetHistory(uint32_t originId, uint32_t eventId) {
  return GetHistory( Esm::CreateMessageId(originId, eventId) );
}

Ptr< Esm > EsmProtocol::CreateEsm(uint32_t originId, uint32_t eventId, Time originTime)
{
  Ptr< Esm > esm = Create<Esm>(originId, eventId, originTime);
  return esm;
}

void EsmProtocol::SendEsm(Ptr<Esm> esm) {
  Ptr<Node> sender = GetNode();
  NS_LOG_DEBUG("@" << Simulator::Now() << " [EsmProtocol::SendEsm] senderId=" << sender->GetId() << ", msgId=" << esm->GetMessageId());
  
  Ptr<VehicleMobilityModel> vmm = sender->GetObject<VehicleMobilityModel>();
  const Vector& pos = vmm->GetPosition();
  sae::VehicleState state(pos.x, pos.y, vmm->GetSpeed(), vmm->GetHeading(), vmm->GetAcceleration(), vmm->GetLength(), vmm->GetWidth());
  
  DsrcBsmHeader bsmHeader;
  bsmHeader.SetLaneId(vmm->GetLaneId());
  bsmHeader.SetOffset(vmm->GetOffsetAlongPath());
  bsmHeader.SetVehicleState(state);
  
  EsmpHeader esmpHeader;
  esmpHeader.SetEventId(esm->eventId);
  esmpHeader.SetOriginId(esm->originId);
  esmpHeader.SetOriginTimestamp(esm->originTimestamp);
  AddReceivers(esm, esmpHeader);
  
  Ptr<Packet> packet = Create<Packet> (0);
  packet->AddHeader(bsmHeader);
  packet->AddHeader(esmpHeader);
  uint32_t maxSize = s_messageSize - WsmpHeader::SIZE;
  uint32_t fillSize = maxSize - packet->GetSize();
  if (fillSize > 0) {
    packet->AddPaddingAtEnd(fillSize);
  }
  
  BroadcastPacket(packet, GetTxPowerLevel());
  ++(esm->sentMessages);
  if (!s_sendCallback.IsNull()) s_sendCallback(sender->GetId(), esm->GetMessageId());
}

bool EsmProtocol::ReceiveEsm(Ptr< Node > receiver, Ptr< Packet > packet, const WsmpHeader& wsmpHeader, const EsmpHeader& esmpHeader, const DsrcBsmHeader& bsmHeader)
{ return false; }

void EsmProtocol::DoDispose(void )
{
  THistory::iterator it;
  for (it=m_history.begin(); it!=m_history.end(); ++it) {
    it->second->DoDispose();
  }
  m_history.clear();
  WsmProtocol::DoDispose();
}


NS_OBJECT_ENSURE_REGISTERED (EsmpInstantaneous);
//NS_LOG_COMPONENT_DEFINE ("WP_Instantaneous");
TypeId 
EsmpInstantaneous::GetTypeId(void) {
  static TypeId tid = TypeId("EsmpInstantaneous")
    .SetParent<EsmProtocol> ()
    .AddConstructor<EsmpInstantaneous> () 
    ;
  return tid;
}

EsmpInstantaneous::EsmpInstantaneous() { }
EsmpInstantaneous::~EsmpInstantaneous() { }

void EsmpInstantaneous::SendInitialWarning() {
  //NS_LOG_DEBUG("[EsmpInstantaneous::SendInitialWarning]");
  Ptr<VehicleMobilityModel> m = GetNode()->GetObject<VehicleMobilityModel>();
  Ptr<VehicleMobilityModel> mf = m->GetFollower();
  //Ptr<Node> nf = rts->GetFollower(n);
  while (mf != 0) {
    Ptr<Node> nf = mf->GetNode();
    NS_LOG_DEBUG("[EsmpInstantaneous::SendInitialWarning] Send warning to nodeId="<<nf->GetId());
    if (!s_recvCallback.IsNull()) s_recvCallback(nf->GetId(), 0, MilliSeconds(0));
    //TODO:check safe distance to limit the braking propagation
    mf = mf->GetFollower();
  }
}


