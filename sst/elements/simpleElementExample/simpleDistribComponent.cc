// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2015, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include <assert.h>

#include "sst_config.h"
#include "sst/core/serialization.h"
#include "sst/core/element.h"
#include "sst/core/params.h"

#include "simpleDistribComponent.h"

using namespace SST;
using namespace SST::RNG;
using namespace SST::SimpleDistribComponent;

void simpleDistribComponent::finish() {
	if(bin_results) {
		std::map<int64_t, uint64_t>::iterator map_itr;

		std::cout << "Bin:" << std::endl;
		for(map_itr = bins->begin(); map_itr != bins->end(); map_itr++) {
			std::cout << map_itr->first << " " << map_itr->second << std::endl;
		}
	}
}

simpleDistribComponent::simpleDistribComponent(ComponentId_t id, Params& params) :
  Component(id) {

  // tell the simulator not to end without us
  registerAsPrimaryComponent();
  primaryComponentDoNotEndSim();

  rng_max_count = params.find_integer("count", 1000);
  rng_count = 0;

  bins = new std::map<int64_t, uint64_t>();

  if("1" == params.find_string("binresults", "1")) {
	bin_results = true;
  } else {
	bin_results = false;
  }

  std::string distrib_type = params.find_string("distrib", "gaussian");
  if("gaussian" == distrib_type || "normal" == distrib_type) {
	double mean = params.find_floating("mean", 1.0);
	double stddev = params.find_floating("stddev", 0.2);

	comp_distrib = new SSTGaussianDistribution(mean, stddev, new MersenneRNG(10111));
  } else if("exponential" == distrib_type) {
	double lambda = params.find_floating("lambda", 1.0);
	comp_distrib = new SSTExponentialDistribution(lambda, new MersenneRNG(10111));
  } else if("poisson" == distrib_type) {
	double lambda = params.find_floating("lambda", 3.0);
	comp_distrib = new SSTPoissonDistribution(lambda, new MersenneRNG(10111));
  } else {
	std::cerr << "Unknown distribution type." << std::endl;
	exit(-1);
  }

  //set our clock
  registerClock( "1GHz", new Clock::Handler<simpleDistribComponent>(this,
			&simpleDistribComponent::tick ) );
}

simpleDistribComponent::simpleDistribComponent() :
    Component(-1)
{
    // for serialization only
}

bool simpleDistribComponent::tick( Cycle_t cyc ) {

	double next_result = comp_distrib->getNextDouble();
	int64_t int_next_result = (int64_t) next_result;

	if(bins->find(int_next_result) == bins->end()) {
		bins->insert( std::pair<int64_t, uint64_t>(int_next_result, 1) );
	} else {
		bins->at(int_next_result)++;
	}

	rng_count++;

	if(rng_max_count == rng_count) {
      		primaryComponentOKToEndSim();
		return true;
	}

	return false;
}

// Element Library / Serialization stuff

BOOST_CLASS_EXPORT(simpleDistribComponent)

