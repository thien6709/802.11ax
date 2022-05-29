/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This file also contains code from the following file(s) from ns-3.26:
 * Filename : wifi-channel.h
 * Copyright (c) 2006,2007 INRIA
 * Authors: Mathieu Lacage, <mathieu.lacage@sophia.inria.fr>
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

#ifndef HE_WIFI_CHANNEL_H
#define HE_WIFI_CHANNEL_H

#include <vector>
#include <stdint.h>
#include "ns3/packet.h"
#include "wifi-channel.h"
#include "wifi-mode.h"
#include "wifi-preamble.h"
#include "wifi-tx-vector.h"
#include "HE-wifi-phy.h"
#include "ns3/nstime.h"

namespace ns3 {

class NetDevice;
class PropagationLossModel;
class PropagationDelayModel;

struct HeParameters
{
  double rxPowerDbm;
  enum mpduType type;
  Time duration;
  WifiTxVector txVector;
  WifiPreamble preamble;
};

/**
 * \brief A HE wifi channel
 * \ingroup wifi
 *
 * This wifi channel implements the propagation model described in
 * "Yet Another Network Simulator", (http://cutebugs.net/files/wns2-HE.pdf).
 *
 * This class is expected to be used in tandem with the ns3::HEWifiPhy
 * class and contains a ns3::PropagationLossModel and a ns3::PropagationDelayModel.
 * By default, no propagation models are set so, it is the caller's responsability
 * to set them before using the channel.
 */
class HEWifiChannel : public WifiChannel
{
public:
  static TypeId GetTypeId (void);

  HEWifiChannel ();
  virtual ~HEWifiChannel ();

  //inherited from Channel.
  virtual uint32_t GetNDevices (void) const;
  virtual Ptr<NetDevice> GetDevice (uint32_t i) const;

  /**
   * Adds the given HEWifiPhy to the PHY list
   *
   * \param phy the HEWifiPhy to be added to the PHY list
   */
  void Add (Ptr<HEWifiPhy> phy);

  /**
   * \param loss the new propagation loss model.
   */
  void SetPropagationLossModel (Ptr<PropagationLossModel> loss);
  /**
   * \param delay the new propagation delay model.
   */
  void SetPropagationDelayModel (Ptr<PropagationDelayModel> delay);

  /**
   * \param sender the device from which the packet is originating.
   * \param packet the packet to send
   * \param txPowerDbm the tx power associated to the packet
   * \param txVector the TXVECTOR associated to the packet
   * \param preamble the preamble associated to the packet
   * \param mpdutype the type of the MPDU as defined in WifiPhy::mpduType.
   * \param duration the transmission duration associated to the packet
   *
   * This method should not be invoked by normal users. It is
   * currently invoked only from WifiPhy::Send. HEWifiChannel
   * delivers packets only between PHYs with the same m_channelNumber,
   * e.g. PHYs that are operating on the same channel.
   */
  void Send (Ptr<HEWifiPhy> sender, Ptr<const Packet> packet, double txPowerDbm,
             WifiTxVector txVector, WifiPreamble preamble, enum mpduType mpdutype, Time duration) const;
  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model.  Return the number of streams (possibly zero) that
   * have been assigned.
   *
   * \param stream first stream index to use
   *
   * \return the number of stream indices assigned by this model
   */
  int64_t AssignStreams (int64_t stream);


private:
  /**
   * A vector of pointers to HEWifiPhy.
   */
  typedef std::vector<Ptr<HEWifiPhy> > PhyList;

  /**
   * This method is scheduled by Send for each associated HEWifiPhy.
   * The method then calls the corresponding HEWifiPhy that the first
   * bit of the packet has arrived.
   *
   * \param i index of the corresponding HEWifiPhy in the PHY list
   * \param packet the packet being sent
   * \param atts a vector containing the received power in dBm and the packet type
   * \param txVector the TXVECTOR of the packet
   * \param preamble the type of preamble being used to send the packet
   */
  void Receive (uint32_t i, Ptr<Packet> packet, struct HeParameters parameters) const;

  PhyList m_phyList;                   //!< List of HEWifiPhys connected to this HEWifiChannel
  Ptr<PropagationLossModel> m_loss;    //!< Propagation loss model
  Ptr<PropagationDelayModel> m_delay;  //!< Propagation delay model
};

} //namespace ns3

#endif /* HE_WIFI_CHANNEL_H */
