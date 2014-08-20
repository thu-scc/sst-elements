// Copyright 2009-2014 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2014, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef SST_SCHEDULER_ALLOCMAPPER_H_
#define SST_SCHEDULER_ALLOCMAPPER_H_

#include "Allocator.h"
#include "TaskMapper.h"

#include <map>
#include <vector>

namespace SST {
    namespace Scheduler {

        class AllocInfo;
        class Job;
        class Machine;
        class TaskMapInfo;

        // This class is an interface for the classes that
        // allocate a given job and map its tasks simultaneously

        class AllocMapper : public Allocator, public TaskMapper {
            public:
                AllocMapper(const Machine & mach) : Allocator(mach), TaskMapper(mach) { };
                ~AllocMapper(){
                    if(!mappings.empty()){
                        for(std::map<long int, std::vector<int>*>::const_iterator it = mappings.begin(); it != mappings.end(); it++){
                            delete it->second;
                        }
                    }
                }

                virtual std::string getSetupInfo(bool comment) const = 0;

                //returns information on the allocation or NULL if it wasn't possible
                //(doesn't make allocation; merely returns info on possible allocation)
                //implementations should store the task mapping information by calling addMapping()
                virtual AllocInfo* allocate(Job* job) = 0;

                //returns task mapping info of a single job; does not map the tasks
                //deletes the job mapping info after calling the function
                TaskMapInfo* mapTasks(AllocInfo* allocInfo);

            private:
                static std::map<long int, std::vector<int>*> mappings; //keeps the task mapping after allocation

            protected:
                //stores mapping information
                void addMapping(long int jobNum, std::vector<int>* taskToNode);
        };
    }
}

#endif /* SST_SCHEDULER_ALLOCMAPPER_H_ */

