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

#ifndef _MEMORYCONTROLLER_H
#define _MEMORYCONTROLLER_H

#include "sst_config.h"

#include <sst/core/event.h>
#include <sst/core/sst_types.h>
#include <sst/core/component.h>
#include <sst/core/link.h>
#include <sst/core/output.h>
#include <map>

#include <sst/core/interfaces/memEvent.h>
using namespace SST::Interfaces;

#include "bus.h"

#if defined(HAVE_LIBDRAMSIM)
// DRAMSim uses DEBUG
#ifdef DEBUG
# define OLD_DEBUG DEBUG
# undef DEBUG
#endif
#include <DRAMSim.h>
#ifdef OLD_DEBUG
# define DEBUG OLD_DEBUG
# undef OLD_DEBUG
#endif
#endif


namespace SST {
namespace MemHierarchy {

class MemController : public SST::Component {
public:
    MemController(ComponentId_t id, Params &params);
    void init(unsigned int);
    void setup();
    void finish();


private:

    struct DRAMReq {
        enum Status_t {NEW, PROCESSING, RETURNED, DONE};

        MemEvent *reqEvent;
        MemEvent *respEvent;
        bool isWrite;
        bool canceled;
        bool isACK;

        size_t size;
        size_t amt_in_process;
        size_t amt_processed;
        Status_t status;

        Addr addr;
        uint32_t num_req; // size / bus width;

        DRAMReq(MemEvent *ev, const size_t busWidth) :
            reqEvent(new MemEvent(ev)), respEvent(NULL),
            isWrite(ev->getCmd() == SupplyData || ev->getCmd() == WriteReq),
            canceled(false), isACK(false),
            size(ev->getSize()), amt_in_process(0), amt_processed(0), status(NEW)
        {
            Addr reqEndAddr = ev->getAddr() + ev->getSize();
            addr = ev->getAddr() & ~(busWidth -1); // round down to bus alignment;

            num_req = (reqEndAddr - addr) / busWidth;
            if ( (reqEndAddr - addr) % busWidth ) num_req++;

            size = num_req * busWidth;
#if 0
            printf(
                    "***************************************************\n"
                    "Buswidth = %zu\n"
                    "Ev:   0x%08lx  + 0x%02x -> 0x%08lx\n"
                    "Req:  0x%08lx  + 0x%02x   [%u count]\n"
                    "***************************************************\n",
                    busWidth, ev->getAddr(), ev->getSize(), reqEndAddr,
                    addr, size, num_req);
#endif
        }

        ~DRAMReq() {
            delete reqEvent;
        }

        bool isSatisfiedBy(const MemEvent *ev)
        {
            if ( isACK ) return false;
            return ((reqEvent->getAddr() >= ev->getAddr()) &&
                    (reqEvent->getAddr()+reqEvent->getSize() <= (ev->getAddr() + ev->getSize())));
        }

    };

    struct isDone {
      bool operator() (DRAMReq *req) const {
	return (DRAMReq::DONE == req->status);
      }
    };

    class MemCtrlEvent : public SST::Event {
    public:
        MemCtrlEvent(DRAMReq* req) : SST::Event(), req(req)
        { }

        DRAMReq *req;
    private:
        friend class boost::serialization::access;
        template<class Archive>
            void
            serialize(Archive & ar, const unsigned int version )
            {
                ar & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Event);
                ar & BOOST_SERIALIZATION_NVP(req);
            }
    };



    MemController();  // for serialization only
    ~MemController();

    void handleEvent(SST::Event *event);
    void handleBusEvent(SST::Event *event);

    void addRequest(MemEvent *ev);
    void cancelEvent(MemEvent *ev);
    bool clock(SST::Cycle_t cycle);

    bool isRequestAddressValid(MemEvent *ev);
    Addr convertAddressToLocalAddress(Addr addr);
    void performRequest(DRAMReq *req);

    void sendBusPacket(Bus::key_t key);
    void sendBusCancel(Bus::key_t key);

    void sendResponse(DRAMReq *req);

    void handleMemResponse(DRAMReq *req);
    void handleSelfEvent(SST::Event *event);
    void handleCubeEvent(SST::Event *event);

    bool divert_DC_lookups;
    bool use_dramsim;
    bool use_vaultSim;

    Output dbg;
    SST::Link *self_link;
    SST::Link *cube_link; // link to chain of cubes
    SST::Link *upstream_link;
    bool use_bus;
    bool bus_requested;
    std::deque<DRAMReq*> busReqs;

    typedef std::deque<DRAMReq*> dramReq_t;
    dramReq_t requestQueue;
    dramReq_t requests;

    typedef std::map<SST::Interfaces::MemEvent::id_type,DRAMReq*> memEventToDRAMMap_t;
    memEventToDRAMMap_t outToCubes; // map of events sent out to the cubes

    int backing_fd;
    uint8_t *memBuffer;
    size_t memSize;
    size_t requestSize;
    Addr rangeStart;
    size_t numPages;
    Addr interleaveSize;
    Addr interleaveStep;
    bool respondToInvalidates;

    Output::output_location_t statsOutputTarget;
    uint64_t numReadsSupplied;
    uint64_t numReadsCanceled;
    uint64_t numWrites;
    uint64_t numReqOutstanding;    
    uint64_t numCycles;

#if defined(HAVE_LIBDRAMSIM)
    void dramSimDone(unsigned int id, uint64_t addr, uint64_t clockcycle);

    DRAMSim::MultiChannelMemorySystem *memSystem;

    std::map<uint64_t, std::deque<DRAMReq*> > dramReqs;
#endif


};


}}

#endif /* _MEMORYCONTROLLER_H */
