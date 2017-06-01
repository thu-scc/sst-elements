// Copyright 2009-2017 Sandia Corporation. Under the terms
// of Contract DE-NA0003525 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2017, Sandia Corporation
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef MEMHIERARHCY_MOVEEVENT_H
#define MEMHIERARHCY_MOVEEVENT_H

#include <sst/core/sst_types.h>
#include <sst/core/event.h>
#include "sst/core/element.h"
#include "util.h"
#include "memTypes.h"
#include "memEventBase.h"

namespace SST { namespace MemHierarchy {

using namespace std;
typedef uint64_t Addr;

/**
 * Events used to move (or copy) data from one location to another
 *
 */
class MoveEvent : public MemEventBase  {
public:

    typedef std::vector<uint8_t> dataVec;       /** Data Payload type */

    /** Creates a new MoveEvent - Generic */
    MoveEvent(std::string src, Addr srcAddr, Addr srcBaseAddr, Addr dstAddr, Addr dstBaseAddr, Command cmd) : MemEventBase(src, cmd) {
        initialize();
        srcAddr_ = srcAddr;
        srcBaseAddr_ = srcBaseAddr;
        dstAddr_ = dstAddr;
        dstBaseAddr_ = dstBaseAddr;
    }

    MoveEvent * makeResponse() {
        MoveEvent * ev = new MoveEvent(*this);
        ev->setResponse(this);
        ev->setDstVirtualAddress(dstVAddr_);
        ev->setSrcVirtualAddress(srcVAddr_);
        ev->setInstructionPointer(iPtr_);
        return ev;
    }

    void initialize() {
        dstAddr_           = 0;
        dstBaseAddr_       = 0;
        srcAddr_        = 0;
        srcBaseAddr_    = 0;
        size_           = 0;

        dstVAddr_          = 0;
        srcVAddr_          = 0;
        iPtr_           = 0;
    }

// Getters and setters
    
    // dstAddr
    Addr getDstAddr(void) const { return dstAddr_; }
    void setDstAddr(Addr addr) { dstAddr_ = addr; }
    
    // dstBaseAddr
    Addr getDstBaseAddr(void) const { return dstBaseAddr_; }
    void setDstBaseAddr(Addr addr) { dstBaseAddr_ = addr; }
    
    // srcAddr
    Addr getSrcAddr(void) const { return srcAddr_; }
    void setSrcAddr(Addr addr) { srcAddr_ = addr; }
    
    // srcBaseAddr
    Addr getSrcBaseAddr(void) const { return srcBaseAddr_; }
    void setSrcBaseAddr(Addr addr) { srcBaseAddr_ = addr; }
    
    // size
    uint32_t getSize(void) const { return size_; }
    void setSize(uint32_t size) { size_ = size; }
    
    // vAddr
    Addr getSrcVirtualAddress(void) const { return srcVAddr_; }
    void setSrcVirtualAddress(Addr addr) { srcVAddr_ = addr; }
    Addr getDstVirtualAddress(void) const { return dstVAddr_; }
    void setDstVirtualAddress(Addr addr) { dstVAddr_ = addr; }
    
    // iPtr
    Addr getInstructionPointer(void) const { return iPtr_; }
    void setInstructionPointer(Addr iptr) { iPtr_ = iptr; }
    
    virtual MoveEvent* clone(void) override {
        return new MoveEvent(*this);
    }
    
    virtual std::string getVerboseString() {
        std::ostringstream str;
        str << " SrcAddr: " << srcAddr_ << " SrcBaseAddr: " << srcBaseAddr_;
        str << " DstAddr: " << dstAddr_ << " DstBaseAddr: " << dstBaseAddr_;
        str << " SrcVA: " << srcVAddr_ << " DstVA: " << dstVAddr_ << " IP: " << iPtr_;
        str << " Size: " << size_;
        return MemEventBase::getVerboseString() + str.str();
    }

    virtual std::string getBriefString() {
        std::ostringstream str;
        str << " SrcAddr: " << srcAddr_ << " SrcBaseAddr: " << srcBaseAddr_;
        str << " DstAddr: " << dstAddr_ << " DstBaseAddr: " << dstBaseAddr_;
        str << " Size: " << size_;
        return MemEventBase::getBriefString() + str.str();
    }
    
    virtual bool doDebug(Addr addr) {
        if (addr >= dstBaseAddr_ && addr < dstAddr_ + size_) return true;
        if (addr >= srcBaseAddr_ && addr < srcAddr_ + size_) return true;
        return false;
    }

    virtual Addr getRoutingAddress() {
        // Return "local" address - destination if Get, source if Put
        if (cmd_ == Command::Get) return dstBaseAddr_;
        else return srcBaseAddr_;
    }

    virtual size_t getPayloadSize() {
        return 0;
    }

private:
    Addr            dstAddr_;          // Address (target address if this is a get/put)
    Addr            dstBaseAddr_;      // Base address (line-aligned address, target if a get/put)
    Addr            srcAddr_;       // Source address if this is a get/put
    Addr            srcBaseAddr_;   // Source base address if this is a get/put
    uint32_t        size_;          // Either size of data payload or requested # of bytes
    Addr            srcVAddr_;         // Virtual address of the request
    Addr            dstVAddr_;         // Virtual address of the request
    Addr            iPtr_;          // Instruction pointer of the request

    MoveEvent() {} // For serialization only

public:
    void serialize_order(SST::Core::Serialization::serializer &ser)  override {
        MemEventBase::serialize_order(ser);
        ser & dstAddr_;
        ser & dstBaseAddr_;
        ser & srcAddr_;
        ser & srcBaseAddr_;
        ser & size_;
        ser & dstVAddr_;
        ser & srcVAddr_;
        ser & iPtr_;
    }
     
    ImplementSerializable(SST::MemHierarchy::MoveEvent);     
};

}}

#endif /* MEMHIERARCHY_MOVEEVENT_H */
