/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This file also contains code from the following file(s) from ns-3.26:
 * Filename : wifi-phy.cc
 * Copyright (c) 2005,2006 INRIA
 * Authors: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 *          Sébastien Deronne <sebastien.deronne@gmail.com>
 * 
 * Copyright (c) 2017 Cisco and/or its affiliates
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
 * Authors: Balamurugan Ramachandran
 *          Ramachandra Murthy
 *          Bibek Sahu
 *          Mukesh Taneja
 */

/* 
 * This file is for OFDMA/802.11ax type of systems. It is not 
 * fully compliant to IEEE 802.11ax standards.
 */

#include "HE-wifi-phy.h"
#include "HE-wifi-channel.h"
#include "wifi-phy-state-helper.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/double.h"
#include "ampdu-tag.h"
#include "ns3/rng-seed-manager.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/per-tag.h"

#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("HEWifiPhy");

NS_OBJECT_ENSURE_REGISTERED (HEWifiPhy);

TypeId
HEWifiPhy::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::HEWifiPhy")
    .SetParent<WifiPhy> ()
    .SetGroupName ("Wifi")
    .AddConstructor<HEWifiPhy> ()
  ;
  return tid;
}

HEWifiPhy::HEWifiPhy ()
{
  NS_LOG_FUNCTION (this);
}

HEWifiPhy::~HEWifiPhy ()
{
  NS_LOG_FUNCTION (this);
}

void
HEWifiPhy::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_channel = 0;
}

bool
HEWifiPhy::DoChannelSwitch (uint16_t nch)
{
  if (!IsInitialized ())
    {
      //this is not channel switch, this is initialization
      NS_LOG_DEBUG ("initialize to channel " << nch);
      return true;
    }

  NS_ASSERT (!IsStateSwitching ());
  switch (m_state->GetState ())
    {
    case HEWifiPhy::RX:
      NS_LOG_DEBUG ("drop packet because of channel switching while reception");
      m_endPlcpRxEvent.Cancel ();
      m_endRxEvent.Cancel ();
      goto switchChannel;
      break;
    case HEWifiPhy::TX:
      NS_LOG_DEBUG ("channel switching postponed until end of current transmission");
      Simulator::Schedule (GetDelayUntilIdle (), &WifiPhy::SetChannelNumber, this, nch);
      break;
    case HEWifiPhy::CCA_BUSY:
    case HEWifiPhy::IDLE:
      goto switchChannel;
      break;
    case HEWifiPhy::SLEEP:
      NS_LOG_DEBUG ("channel switching ignored in sleep mode");
      break;
    default:
      NS_ASSERT (false);
      break;
    }

  return false;

switchChannel:

  NS_LOG_DEBUG ("switching channel " << GetChannelNumber () << " -> " << nch);
  m_state->SwitchToChannelSwitching (GetChannelSwitchDelay ());
  m_interference.EraseEvents ();
  /*
   * Needed here to be able to correctly sensed the medium for the first
   * time after the switching. The actual switching is not performed until
   * after m_channelSwitchDelay. Packets received during the switching
   * state are added to the event list and are employed later to figure
   * out the state of the medium after the switching.
   */
  return true;
}

bool
HEWifiPhy::DoFrequencySwitch (uint32_t frequency)
{
  if (!IsInitialized ())
    {
      //this is not channel switch, this is initialization
      NS_LOG_DEBUG ("start at frequency " << frequency);
      return true;
    }

  NS_ASSERT (!IsStateSwitching ());
  switch (m_state->GetState ())
    {
    case HEWifiPhy::RX:
      NS_LOG_DEBUG ("drop packet because of channel/frequency switching while reception");
      m_endPlcpRxEvent.Cancel ();
      m_endRxEvent.Cancel ();
      goto switchFrequency;
      break;
    case HEWifiPhy::TX:
      NS_LOG_DEBUG ("channel/frequency switching postponed until end of current transmission");
      Simulator::Schedule (GetDelayUntilIdle (), &WifiPhy::SetFrequency, this, frequency);
      break;
    case HEWifiPhy::CCA_BUSY:
    case HEWifiPhy::IDLE:
      goto switchFrequency;
      break;
    case HEWifiPhy::SLEEP:
      NS_LOG_DEBUG ("frequency switching ignored in sleep mode");
      break;
    default:
      NS_ASSERT (false);
      break;
    }

  return false;

switchFrequency:

  NS_LOG_DEBUG ("switching frequency " << GetFrequency () << " -> " << frequency);
  m_state->SwitchToChannelSwitching (GetChannelSwitchDelay ());
  m_interference.EraseEvents ();
  /*
   * Needed here to be able to correctly sensed the medium for the first
   * time after the switching. The actual switching is not performed until
   * after m_channelSwitchDelay. Packets received during the switching
   * state are added to the event list and are employed later to figure
   * out the state of the medium after the switching.
   */
  return true;
}

Ptr<WifiChannel>
HEWifiPhy::GetChannel (void) const
{
  return m_channel;
}

void
HEWifiPhy::SetChannel (Ptr<HEWifiChannel> channel)
{
  m_channel = channel;
  m_channel->Add (this);
}

void
HEWifiPhy::SetSleepMode (void)
{
  NS_LOG_FUNCTION (this);
  switch (m_state->GetState ())
    {
    case HEWifiPhy::TX:
      NS_LOG_DEBUG ("setting sleep mode postponed until end of current transmission");
      Simulator::Schedule (GetDelayUntilIdle (), &HEWifiPhy::SetSleepMode, this);
      break;
    case HEWifiPhy::RX:
      NS_LOG_DEBUG ("setting sleep mode postponed until end of current reception");
      Simulator::Schedule (GetDelayUntilIdle (), &HEWifiPhy::SetSleepMode, this);
      break;
    case HEWifiPhy::SWITCHING:
      NS_LOG_DEBUG ("setting sleep mode postponed until end of channel switching");
      Simulator::Schedule (GetDelayUntilIdle (), &HEWifiPhy::SetSleepMode, this);
      break;
    case HEWifiPhy::CCA_BUSY:
    case HEWifiPhy::IDLE:
      NS_LOG_DEBUG ("setting sleep mode");
      m_state->SwitchToSleep ();
      break;
    case HEWifiPhy::SLEEP:
      NS_LOG_DEBUG ("already in sleep mode");
      break;
    default:
      NS_ASSERT (false);
      break;
    }
}

void
HEWifiPhy::ResumeFromSleep (void)
{
  NS_LOG_FUNCTION (this);
  switch (m_state->GetState ())
    {
    case HEWifiPhy::TX:
    case HEWifiPhy::RX:
    case HEWifiPhy::IDLE:
    case HEWifiPhy::CCA_BUSY:
    case HEWifiPhy::SWITCHING:
      {
        NS_LOG_DEBUG ("not in sleep mode, there is nothing to resume");
        break;
      }
    case HEWifiPhy::SLEEP:
      {
        NS_LOG_DEBUG ("resuming from sleep mode");
        Time delayUntilCcaEnd = m_interference.GetEnergyDuration (DbmToW (GetCcaMode1Threshold ()));
        m_state->SwitchFromSleep (delayUntilCcaEnd);
        break;
      }
    default:
      {
        NS_ASSERT (false);
        break;
      }
    }
}

void
HEWifiPhy::SetReceiveOkCallback (RxOkCallback callback)
{
  m_state->SetReceiveOkCallback (callback);
}

void
HEWifiPhy::SetReceiveErrorCallback (RxErrorCallback callback)
{
  m_state->SetReceiveErrorCallback (callback);
}

void
HEWifiPhy::StartReceivePreambleAndHeader (Ptr<Packet> packet,
                                            double rxPowerDbm,
                                            WifiTxVector txVector,
                                            enum WifiPreamble preamble,
                                            enum mpduType mpdutype,
                                            Time rxDuration)
{

  //This function should be later split to check separately whether plcp preamble and plcp header can be successfully received.
  //Note: plcp preamble reception is not yet modeled.
  NS_LOG_FUNCTION (this << packet << rxPowerDbm << txVector.GetMode () << preamble << (uint32_t)mpdutype);
  AmpduTag ampduTag;
  rxPowerDbm += GetRxGain ();
  double rxPowerW = DbmToW (rxPowerDbm);
  Time endRx = Simulator::Now () + rxDuration;
  Time preambleAndHeaderDuration = CalculatePlcpPreambleAndHeaderDuration (txVector, preamble);
  enum WifiPhy::State phyState = m_state->GetState ();
  bool receiveTbMpdu = false;
  packet->RemovePaddingAtEnd();
  Ptr<InterferenceHelper::Event> event;
  event = m_interference.Add (packet->GetSize (),
                              txVector,
                              preamble,
                              rxDuration,
                              rxPowerW);
  if ((GetColor() != 0 && txVector.GetColor() !=0) && txVector.GetColor() != GetColor())
    {
      NS_LOG_DEBUG ("station dropped packet received on different BSS");
      NotifyRxDrop (packet);
      //return;
      goto maybeCcaBusy;
    }
  if (txVector.GetRu() != 0xff)
   {
     if (!m_state->IsRxingOnRu(txVector))
       {
	 NS_LOG_DEBUG ("station dropped packet received on unexpected RU :" <<txVector.GetRu());
	 NotifyRxDrop (packet);
	 return;
       }
      if (GetAid())
        {
          //Station PHY
          NS_LOG_DEBUG ("Station receive packet received on expected channel(RU) :" <<txVector.GetRu());
        }
      else
        {
	  // AP PHY
          NS_LOG_DEBUG ("AP Station receive packet received on expected channel(RU) :" <<txVector.GetRu());
          phyState = HEWifiPhy::IDLE;
          receiveTbMpdu = true;
        }
   }
  switch (phyState)
    {
    case HEWifiPhy::SWITCHING:
      NS_LOG_DEBUG ("drop packet because of channel switching");
      NotifyRxDrop (packet);
      m_plcpSuccess = false;
      /*
       * Packets received on the upcoming channel are added to the event list
       * during the switching state. This way the medium can be correctly sensed
       * when the device listens to the channel for the first time after the
       * switching e.g. after channel switching, the channel may be sensed as
       * busy due to other devices' tramissions started before the end of
       * the switching.
       */
      if (endRx > Simulator::Now () + m_state->GetDelayUntilIdle ())
        {
          //that packet will be noise _after_ the completion of the
          //channel switching.
          goto maybeCcaBusy;
        }
      break;
    case HEWifiPhy::RX:
      NS_LOG_DEBUG ("drop packet because already in Rx (power=" <<
                    rxPowerW << "W)");
      NotifyRxDrop (packet);
      if (endRx > Simulator::Now () + m_state->GetDelayUntilIdle ())
        {
          //that packet will be noise _after_ the reception of the
          //currently-received packet.
          goto maybeCcaBusy;
        }
      break;
    case HEWifiPhy::TX:
      NS_LOG_DEBUG ("drop packet because already in Tx (power=" <<
                    rxPowerW << "W)");
      NotifyRxDrop (packet);
      if (endRx > Simulator::Now () + m_state->GetDelayUntilIdle ())
        {
          //that packet will be noise _after_ the transmission of the
          //currently-transmitted packet.
          goto maybeCcaBusy;
        }
      break;
    case HEWifiPhy::CCA_BUSY:
    case HEWifiPhy::IDLE:
      if (rxPowerW > GetEdThresholdW ()) //checked here, no need to check in the payload reception (current implementation assumes constant rx power over the packet duration)
        {
          if (preamble == WIFI_PREAMBLE_NONE && (m_mpdusNum == 0 || m_plcpSuccess == false))
            {
              m_plcpSuccess = false;
              m_mpdusNum = 0;
              NS_LOG_DEBUG ("drop packet because no PLCP preamble/header has been received");
              NotifyRxDrop (packet);
              goto maybeCcaBusy;
            }
          else if (preamble != WIFI_PREAMBLE_NONE && packet->PeekPacketTag (ampduTag) && m_mpdusNum == 0)
            {
              //received the first MPDU in an MPDU
              m_mpdusNum = ampduTag.GetRemainingNbOfMpdus ();
              m_rxMpduReferenceNumber++;
            }
          else if (preamble == WIFI_PREAMBLE_NONE && packet->PeekPacketTag (ampduTag) && m_mpdusNum > 0)
            {
              //received the other MPDUs that are part of the A-MPDU
              if (ampduTag.GetRemainingNbOfMpdus () < m_mpdusNum)
                {
                  m_mpdusNum--;
                }
            }
          else if (preamble != WIFI_PREAMBLE_NONE && packet->PeekPacketTag (ampduTag) && m_mpdusNum > 0)
            {
              NS_LOG_DEBUG ("New A-MPDU started while " << m_mpdusNum << " MPDUs from previous are in progress");
              m_mpdusNum += ampduTag.GetRemainingNbOfMpdus ();
            }
          else if (preamble != WIFI_PREAMBLE_NONE && m_mpdusNum > 0 )
            {
              NS_LOG_DEBUG ("Didn't receive the last MPDUs from an A-MPDU " << m_mpdusNum);
              // we will receive some AMPDUs and some non-AMPDU. if they are mixed, we can not drop all ampdus
              //m_mpdusNum = 0;
            }

          NS_LOG_DEBUG ("sync to signal (power=" << rxPowerW << "W)");
          //sync to signal
          m_state->SwitchToRx (rxDuration);
          if (receiveTbMpdu && (!m_endPlcpRxEvent.IsExpired () || !m_endRxEvent.IsExpired ()))
            {
              NotifyRxBegin (packet);
              m_interference.NotifyRxStart ();

              if (preamble != WIFI_PREAMBLE_NONE)
                {
                  Simulator::Schedule (preambleAndHeaderDuration, &HEWifiPhy::StartReceivePacket, this,
                                                          packet, txVector, preamble, mpdutype, event);
                }
              Simulator::Schedule (rxDuration, &HEWifiPhy::EndReceive, this, packet, preamble, mpdutype, event);
            }
          else
            {
              NS_ASSERT (m_endPlcpRxEvent.IsExpired ());
              NotifyRxBegin (packet);
              m_interference.NotifyRxStart ();

              if (preamble != WIFI_PREAMBLE_NONE)
                {
                  NS_ASSERT (m_endPlcpRxEvent.IsExpired ());
                  m_endPlcpRxEvent = Simulator::Schedule (preambleAndHeaderDuration, &HEWifiPhy::StartReceivePacket, this,
                                                          packet, txVector, preamble, mpdutype, event);
                }

              NS_ASSERT (m_endRxEvent.IsExpired ());
              m_endRxEvent = Simulator::Schedule (rxDuration, &HEWifiPhy::EndReceive, this,
                                                  packet, preamble, mpdutype, event);
            }
        }
      else
        {
          NS_LOG_DEBUG ("drop packet because signal power too Small (" <<
                        rxPowerW << "<" << GetEdThresholdW () << ")");
          NotifyRxDrop (packet);
          m_plcpSuccess = false;
          goto maybeCcaBusy;
        }
      break;
    case HEWifiPhy::SLEEP:
      NS_LOG_DEBUG ("drop packet because in sleep mode");
      NotifyRxDrop (packet);
      m_plcpSuccess = false;
      break;
    }

  return;

maybeCcaBusy:
  //We are here because we have received the first bit of a packet and we are
  //not going to be able to synchronize on it
  //In this model, CCA becomes busy when the aggregation of all signals as
  //tracked by the InterferenceHelper class is higher than the CcaBusyThreshold

  Time delayUntilCcaEnd = Seconds(0.0);
#if 1
  if (txVector.GetColor() != GetColor())
#else
  if (0)
#endif
  {
    delayUntilCcaEnd = m_interference.GetEnergyDuration (DbmToW (GetObssCcaThreshold ()));
  }
  else{
    delayUntilCcaEnd = m_interference.GetEnergyDuration (DbmToW (GetCcaMode1Threshold ()));
  }
  if (!delayUntilCcaEnd.IsZero ())
    {
      m_state->SwitchMaybeToCcaBusy (delayUntilCcaEnd);
    }
}

void
HEWifiPhy::StartReceivePacket (Ptr<Packet> packet,
                                 WifiTxVector txVector,
                                 enum WifiPreamble preamble,
                                 enum mpduType mpdutype,
                                 Ptr<InterferenceHelper::Event> event)
{
  NS_LOG_FUNCTION (this << packet << txVector.GetMode () << preamble << (uint32_t)mpdutype);
  NS_ASSERT (IsStateRx ());
  NS_ASSERT (m_endPlcpRxEvent.IsExpired ());
  WifiMode txMode = txVector.GetMode ();

#if 0
  struct InterferenceHelper::SnrPer snrPer;
  snrPer = m_interference.CalculatePlcpHeaderSnrPer (event);

  NS_LOG_DEBUG ("snr(dB)=" << RatioToDb (snrPer.snr) << ", per=" << snrPer.per);
  if (m_random->GetValue () > snrPer.per) //plcp reception succeeded
#endif
    if (1)
      {
      if (IsModeSupported (txMode) || IsMcsSupported (txMode))
        {
          NS_LOG_DEBUG ("receiving plcp payload"); //endReceive is already scheduled
          m_plcpSuccess = true;
        }
      else //mode is not allowed
        {
          NS_LOG_DEBUG ("drop packet because it was sent using an unsupported mode (" << txMode << ")");
          NotifyRxDrop (packet);
          m_plcpSuccess = false;
        }
    }
  else //plcp reception failed
    {
      NS_LOG_DEBUG ("drop packet because plcp preamble/header reception failed");
      NotifyRxDrop (packet);
      //m_plcpSuccess = false;
      m_plcpSuccess = true;  //Rama_tbd:      Address this
    }
}

void
HEWifiPhy::SendPacket (Ptr<const Packet> packet, WifiTxVector txVector, WifiPreamble preamble)
{
  SendPacket (packet, txVector, preamble, NORMAL_MPDU);
}

void
HEWifiPhy::SendPacket (Ptr<const Packet> packet, WifiTxVector txVector, WifiPreamble preamble, enum mpduType mpdutype)
{
  NS_LOG_FUNCTION (this << packet << txVector.GetMode () 
    << txVector.GetMode ().GetDataRate (txVector)
    << preamble << (uint32_t)txVector.GetTxPowerLevel () << (uint32_t)mpdutype);
  /* Transmission can happen if:
   *  - we are syncing on a packet. It is the responsability of the
   *    MAC layer to avoid doing this but the PHY does nothing to
   *    prevent it.
   *  - we are idle
   */
  txVector.SetColor(GetColor());
  if (txVector.GetRu () != 0xff&& !m_state->IsStateRx () && !m_state->IsTxingOnRu(txVector))
    {
      m_state->SetTxingForRu(txVector, true);
    }
  if (txVector.GetRu () == 0xff || (m_state->IsLastRuTx() && !m_state->IsStateTx ()))
    {
      NS_ASSERT (!m_state->IsStateTx () && !m_state->IsStateSwitching ());
    }
  if (m_state->IsStateSleep ())
    {
      NS_LOG_DEBUG ("Dropping packet because in sleep mode");
      NotifyTxDrop (packet);
      return;
    }

  Time txDuration = CalculateTxDuration (packet->GetSize (), txVector, preamble, GetFrequency (), mpdutype, 1);

  NS_ASSERT (txDuration > NanoSeconds (0));
  if (m_state->IsStateRx ())
    {
      m_endPlcpRxEvent.Cancel ();
      m_endRxEvent.Cancel ();
      m_interference.NotifyRxEnd ();
    }
  NotifyTxBegin (packet);
  uint32_t dataRate500KbpsUnits;
  if (txVector.GetMode ().GetModulationClass () == WIFI_MOD_CLASS_HT || txVector.GetMode ().GetModulationClass () == WIFI_MOD_CLASS_VHT
      || txVector.GetMode ().GetModulationClass () == WIFI_MOD_CLASS_HE)
    {
      dataRate500KbpsUnits = 128 + txVector.GetMode ().GetMcsValue ();
    }
  else
    {
      dataRate500KbpsUnits = txVector.GetMode ().GetDataRate (txVector.GetChannelWidth (), txVector.IsShortGuardInterval (), 1) * txVector.GetNss () / 500000;
    }
  if (mpdutype == MPDU_IN_AGGREGATE && preamble != WIFI_PREAMBLE_NONE)
    {
      //send the first MPDU in an MPDU
      m_txMpduReferenceNumber++;
    }
  struct mpduInfo aMpdu;
  aMpdu.type = mpdutype;
  aMpdu.mpduRefNumber = m_txMpduReferenceNumber;
  NotifyMonitorSniffTx (packet, (uint16_t)GetFrequency (), GetChannelNumber (), dataRate500KbpsUnits, preamble, txVector, aMpdu);
  if (txVector.GetRu () == 0xff || (m_state->IsLastRuTx() && !m_state->IsStateTx ()))
    {
      m_state->SwitchToTx (txDuration, packet, GetPowerDbm (txVector.GetTxPowerLevel ()), txVector, preamble);
    }
  m_channel->Send (this, packet, GetPowerDbm (txVector.GetTxPowerLevel ()) + GetTxGain (), txVector, preamble, mpdutype, txDuration);
  if (txVector.GetRu () != 0xff)
    {
      Simulator::Schedule (txDuration, &WifiPhyStateHelper::SetTxingForRu, m_state, txVector, false);
    }
}

void
HEWifiPhy::RegisterListener (WifiPhyListener *listener)
{
  m_state->RegisterListener (listener);
}

void
HEWifiPhy::UnregisterListener (WifiPhyListener *listener)
{
  m_state->UnregisterListener (listener);
}

void
HEWifiPhy::EndReceive (Ptr<Packet> packet, enum WifiPreamble preamble, enum mpduType mpdutype, Ptr<InterferenceHelper::Event> event)
{
  NS_LOG_FUNCTION (this << packet << event);
  NS_ASSERT (IsStateRx ());
  NS_ASSERT (event->GetEndTime () == Simulator::Now ());
  Ptr<UniformRandomVariable> urv = CreateObject<UniformRandomVariable> ();

  struct InterferenceHelper::SnrPer snrPer;

  snrPer = m_interference.CalculatePlcpPayloadSnrPer (event);
  if (packet->GetSize() < 150){
    snrPer.per = 0;
  }
  PerTag tag;
  if(packet->PeekPacketTag(tag))
  {
    packet->RemovePacketTag (tag);
  }
  tag.Set(snrPer.per);
  packet->AddPacketTag(tag);
  if (event->GetTxVector ().GetRu() == 0xff || m_state->IsLastRuRx())
    {
      m_interference.NotifyRxEnd ();
    }

  //if (m_plcpSuccess == true)  //Per flow plcp flag is not there
  if (1)
    {
      NS_LOG_DEBUG ("mode=" << (event->GetPayloadMode ().GetDataRate (event->GetTxVector ())) <<
                    ", snr(dB)=" << RatioToDb (snrPer.snr) << ", per=" << snrPer.per << ", size=" << packet->GetSize ());

#if 1 
      if (m_random->GetValue () > snrPer.per)
#else
      if (1)  // Disable Per Drop
#endif
        {
          NotifyRxEnd (packet);
          uint32_t dataRate500KbpsUnits;
          if ((event->GetPayloadMode ().GetModulationClass () == WIFI_MOD_CLASS_HT) || (event->GetPayloadMode ().GetModulationClass () == WIFI_MOD_CLASS_VHT))
            {
              dataRate500KbpsUnits = 128 + event->GetPayloadMode ().GetMcsValue ();
            }
          else
            {
              dataRate500KbpsUnits = event->GetPayloadMode ().GetDataRate (event->GetTxVector ().GetChannelWidth (), event->GetTxVector ().IsShortGuardInterval (), 1) * event->GetTxVector ().GetNss () / 500000;
            }
          struct signalNoiseDbm signalNoise;
          signalNoise.signal = RatioToDb (event->GetRxPowerW ()) + 30;
          signalNoise.noise = RatioToDb (event->GetRxPowerW () / snrPer.snr) - GetRxNoiseFigure () + 30;
          struct mpduInfo aMpdu;
          aMpdu.type = mpdutype;
          aMpdu.mpduRefNumber = m_rxMpduReferenceNumber;
          NotifyMonitorSniffRx (packet, (uint16_t)GetFrequency (), GetChannelNumber (), dataRate500KbpsUnits, event->GetPreambleType (), event->GetTxVector (), aMpdu, signalNoise);
          m_state->SwitchFromRxEndOk (packet, snrPer.snr, event->GetTxVector (), event->GetPreambleType ());
        }
      else
        {
          /* failure. */
          NotifyRxDrop (packet);
          m_state->SwitchFromRxEndError (packet, snrPer.snr, event->GetTxVector());
        }
    }
  else
    {
      m_state->SwitchFromRxEndError (packet, snrPer.snr, event->GetTxVector());
    }

  if (preamble == WIFI_PREAMBLE_NONE && mpdutype == LAST_MPDU_IN_AGGREGATE)
    {
      m_plcpSuccess = false;
    }
}

} //namespace ns3
