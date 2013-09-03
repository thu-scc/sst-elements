// Copyright 2009-2013 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2013, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef ED_RTRIF_H
#define ED_RTRIF_H

#include <sstream>
#include <deque>

#include <sst/core/event.h>
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/core/log.h>
#include "sst/elements/SS_router/SS_router/SS_network.h"

#define RTRIF_DBG 1 
#ifndef RTRIF_DBG
#define RTRIF_DBG 0
#endif

#define db_RtrIF(fmt,args...) \
    m_dbg.write( "%s():%d: "fmt, __FUNCTION__, __LINE__, ##args)

namespace SST {
namespace SS_router {

class RtrIF : public Component {

private:

    int                     rtrCountP;

    typedef deque<RtrEvent*> ToNic;

    class ToRtr {

        unsigned int tokensP;
        deque<RtrEvent*> &eventQP;

    public:
        ToRtr( int num_tokens, deque<RtrEvent*> &eventQ ) :
                tokensP(num_tokens), eventQP(eventQ) {}

        bool push( RtrEvent* event) {
            networkPacket* pkt = &event->packet;
            if ( pkt->sizeInFlits > tokensP ) return false;
            tokensP -= pkt->sizeInFlits;
            eventQP.push_back(event);
            return true;
        }
        int size() {
            return eventQP.size();
        }

        bool willTake( unsigned int numFlits ) {
            return (numFlits <= tokensP );
        }

        void returnTokens( int num ) {
            tokensP += num;
        }
    };

    map<int,ToNic*>         toNicMapP;
    map<int,ToRtr*>         toRtrMapP;

    uint                    num_vcP;

    deque<RtrEvent*>        toRtrQP;

    Link*                   m_rtrLink;
    int                     m_id;
    int                     m_num_nodes;
    int                     m_current_send_node;
    int                     m_node_recvd;
    bool                    m_exit;
    Log< RTRIF_DBG >&       m_dbg;
    Log< RTRIF_DBG >&       m_dummyDbg;
    Log<>&                  m_log;


public:
        void finish() {
	    printf("%d: Messages recieved: %d\n",m_id,m_node_recvd);
	}

   RtrIF( ComponentId_t id, Params& params ) :
        Component(id),
        rtrCountP(0),
        num_vcP(2),
        m_dbg( *new Log< RTRIF_DBG >( "RtrIF::", false ) ),
        m_dummyDbg( *new Log< RTRIF_DBG >( "Dummy::RtrIF::", false ) ),
        m_log( *new Log<>( "INFO RtrIF: ", false ) )
    {
        int num_tokens = 512;
        std::string frequency;

        if ( params.find( "info" ) != params.end() ) {
            if ( params[ "info" ].compare( "yes" ) == 0 ) {
                m_log.enable();
            }
        }

        if ( params.find( "debug" ) != params.end() ) {
            if ( params[ "debug" ].compare( "yes" ) == 0 ) {
                m_dbg.enable();
            }
        }

        if ( params.find( "dummyDebug" ) != params.end() ) {
            if ( params[ "dummyDebug" ].compare( "yes" ) == 0 ) {
                m_dummyDbg.enable();
            }
        }

        if ( params.find( "id" ) == params.end() ) {
            _abort(RtrIF,"couldn't find routerID\n" );
        }
        m_id = params.find_integer( "id" );

        if ( params.find("clock") != params.end() ) {
            frequency = params["clock"];
        }

        if ( params.find( "num_vc" ) != params.end() ) {
            num_vcP = params.find_integer( "num_vc" );
        }

        if ( params.find( "Node2RouterQSize_flits" ) != params.end() ) {
            num_tokens = params.find_integer( "Node2RouterQSize_flits" );
        }

        std::ostringstream idStr;
        idStr << m_id << ":";
        m_dbg.prepend( idStr.str() );
        m_dummyDbg.prepend( idStr.str() );
        m_log.prepend( idStr.str() );

        m_log.write("num_vc=%d num_tokens=%d\n",num_vcP,num_tokens);
        m_log.write("nic id=%d frequency=%s\n", m_id, frequency.c_str());

        registerAsPrimaryComponent();
        primaryComponentDoNotEndSim();

	m_rtrLink = configureLink( "rtr", new Event::Handler<RtrIF>(this, &RtrIF::processEvent));

        Params tmp_params = params.find_prefix_params( "dummy." );
        dummyInit( tmp_params, frequency );

//         ClockHandler_t* clockHandler = new EventHandler< RtrIF, bool, Cycle_t >
//                                                 ( this, &RtrIF::clock );

//         if ( ! registerClock( frequency, clockHandler ) ) {
//             _abort(XbarV2,"couldn't register clock handler");
//         }
	registerClock( frequency, new Clock::Handler<RtrIF>(this, &RtrIF::clock) );

        db_RtrIF("Done registering clock\n");

        for ( unsigned int i=0; i < num_vcP; i++ ) {
            toNicMapP[i] = new ToNic();
            toRtrMapP[i] = new ToRtr(num_tokens,toRtrQP);
        }
    }

    void newSetup() {}

    void newFinish() {}

    bool toNicQ_empty(unsigned int vc)
    {
        if ( vc >= num_vcP ) _abort(RtrIF,"vc=%d\n",vc);
        return toNicMapP[vc]->empty();
    }

    RtrEvent *toNicQ_front(unsigned int vc)
    {
        if ( vc >= num_vcP ) _abort(RtrIF,"vc=%d\n",vc);
        db_RtrIF("vc=%d\n",vc);
        return toNicMapP[vc]->front();
    }

    void toNicQ_pop(unsigned int vc)
    {
        if ( vc >= num_vcP ) _abort(RtrIF,"vc=%d\n",vc);
        db_RtrIF("vc=%d\n",vc);
        returnTokens2Rtr( vc, toNicMapP[vc]->front()->packet.sizeInFlits );
        toNicMapP[vc]->pop_front();
    }

    bool send2Rtr( RtrEvent *event)
    {
        networkPacket* pkt = &event->packet;
        if ( pkt->vc >= (int) num_vcP ) _abort(RtrIF,"vc=%d\n",pkt->vc);
        bool retval = toRtrMapP[pkt->vc]->push( event );
        if ( retval )
            db_RtrIF("vc=%d src=%d dest=%d pkt=%p\n",
                     pkt->vc,pkt->srcNum,pkt->destNum,pkt);
        if ( retval ) {
            sendPktToRtr( toRtrQP.front() );
            toRtrQP.pop_front();
        }
        return retval;
    }

private:

    void dummyInit( Params, std::string );
    bool dummyLoad( Cycle_t cycle );
    int   m_dummyFd_in;
    int   m_dummyFd_out;
    size_t  m_dummySize;
    size_t  m_dummyOffset;

    bool rtrWillTake( unsigned int vc, int numFlits )
    {
        if ( vc >= num_vcP ) _abort(RtrIF,"\n");
        db_RtrIF("vc=%d numFlits=%d\n",vc,numFlits);
        return toRtrMapP[vc]->willTake( numFlits );
    }


    void processEvent( Event* e)
    {
        RtrEvent* event = static_cast<RtrEvent*>(e);

        db_RtrIF("type=%ld\n",event->type);

        switch ( event->type ) {
        case RtrEvent::Credit:
            returnTokens2Nic( event->credit.vc, event->credit.num );
            delete event;
            break;

        case RtrEvent::Packet:
            send2Nic( event );
            break;

        default:
            _abort(RtrIF,"unknown type %d\n",event->type);
        }
    }

    bool clock( Cycle_t cycle)
    {
        rtrCountP = (rtrCountP >= 0) ? 0 : rtrCountP + 1;

        if ( ! toRtrQP.empty() ) {
            sendPktToRtr( toRtrQP.front());
            toRtrQP.pop_front();
        }
        return false;
    }

    void send2Nic( RtrEvent* event )
    {
        networkPacket *pkt = &event->packet; 

        pkt->vc = RTR_2_NIC_VC(pkt->vc);

        if ( pkt->vc >= (int) num_vcP ) {
            _abort(RtrIF,"vc=%d pkt=%p\n",pkt->vc,pkt);
        }

        db_RtrIF("vc=%d src=%d dest=%d pkt=%p\n",
                 pkt->vc,pkt->srcNum,pkt->destNum,pkt);
        toNicMapP[pkt->vc]->push_back( event );
    }

    void returnTokens2Nic( unsigned int vc, uint32_t num )
    {
        if ( vc >= num_vcP ) _abort(RtrIF,"\n");
        db_RtrIF("vc=%d numFlits=%d\n", vc, num );
        toRtrMapP[vc]->returnTokens( num );
    }

    void returnTokens2Rtr( int vc, uint numFlits ) 
    {
        RtrEvent* event = new RtrEvent;

        db_RtrIF("vc=%d numFlits=%d\n", vc, numFlits );

        event->type = RtrEvent::Credit;
        event->credit.num = numFlits;
        event->credit.vc = vc;
        m_rtrLink->send( event ); 
    }

    void sendPktToRtr( RtrEvent* event ) 
    {
        networkPacket* pkt = &event->packet;

        db_RtrIF("vc=%d src=%d dest=%d pkt=%p\n",
                 pkt->vc,pkt->srcNum,pkt->destNum,pkt);

        event->type = RtrEvent::Packet;
        event->packet = *pkt;
        int lat = reserveRtrLine(pkt->sizeInFlits);
        m_rtrLink->send( lat, event ); 
    }

    int reserveRtrLine (int cyc)
    {
        db_RtrIF("cyc=%d\n",cyc);
        int ret = (rtrCountP <= 0) ? -rtrCountP : 0;
        rtrCountP -= cyc;
        return ret;
    }
};

}
}

#endif
