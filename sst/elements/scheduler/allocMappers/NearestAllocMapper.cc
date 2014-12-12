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

#include "sst_config.h"
#include "NearestAllocMapper.h"

#ifdef HAVE_METIS
#include <metis.h>
#endif

#include "AllocInfo.h"
#include "Job.h"
#include "FibonacciHeap.h"
#include "Machine.h"
#include "MeshMachine.h"
#include "TaskCommInfo.h"
#include "output.h"

#include <cfloat>
#include <queue>

using namespace SST::Scheduler;
using namespace std;

NearestAllocMapper::NearestAllocMapper(const MeshMachine & mach,
                                       bool allocateAndMap,
                                       NodeGenType inNodeGen)
    : AllocMapper(mach, allocateAndMap), mMachine(mach)
{
    nodeGen = inNodeGen;
    lastNode = 0;
}

NearestAllocMapper::~NearestAllocMapper()
{

}

std::string NearestAllocMapper::getSetupInfo(bool comment) const
{
    std::string com;
    if (comment) {
        com="# ";
    } else  {
        com="";
    }
    return com + "Nearest AllocMapper";
}

void NearestAllocMapper::allocMap(const AllocInfo & ai,
                                  vector<long int> & usedNodes,
                                  vector<int> & taskToNode)
{
    Job *job = ai.job;
    int nodesNeeded = ai.getNodesNeeded();
    int jobSize = job->getProcsNeeded();

    //choose center node
    switch(nodeGen){
    case GREEDY_NODE:
        centerNode = getCenterNodeGr();
        break;
    case EXHAUST_NODE:
        centerNode = getCenterNodeExh(nodesNeeded);
        break;
    default:
        schedout.fatal(CALL_INFO, 1, "Unknown node selection algorithm for Nearest AllocMapper");
    };

    //Optimization: Simple allocation & mapping if single node is needed
    if(nodesNeeded == 1){
        usedNodes[0] = centerNode;
        for(int i = 0; i < jobSize; i++){
            taskToNode[i] = centerNode;
        }
        return;
    }

    //create breadth-first tree around a center task
    createCommGraph(*job);

    list<pair<int, double> > tasks; //task No's and their weights
    tasks.push_back(pair<int,double>(centerTask, 0));
    list<int> frameNodes; //nodes that "frame" the current allocation
    frameNodes.push_back(centerNode);
    marked.resize(commGraph->size(), false);
    marked[centerTask] = true;
    vertexToNode.resize(nodesNeeded);
    fill(vertexToNode.begin(), vertexToNode.end(), -1);
    int mappedCounter = 0;

    //main loop
    while(mappedCounter < nodesNeeded){
        if(tasks.empty()){ //This only happens if the task graph is not connected
            //find an unmapped vertex and put it in the task list
            for(int taskIt = 0; taskIt < jobSize; taskIt++){
                if(vertexToNode[taskIt] == -1){
                    tasks.push_front(pair<int,double>(taskIt, 0));
                    break;
                }
            }
        } else {
            int curTask = tasks.back().first;
            tasks.pop_back();
            //get which node to allocate
            int nodeToAlloc = bestNode(frameNodes, curTask);

            //allocate and update containers
            vertexToNode[curTask] = nodeToAlloc;
            usedNodes[mappedCounter++] = nodeToAlloc;
            isFree->at(nodeToAlloc) = false;

            getTaskNeighbors(curTask, tasks);

            //extend frame - do not add the same node twice
            list<int> *newNodes = new list<int>();
            closestNodes(nodeToAlloc, 0, newNodes);
            for(list<int>::iterator newIt = newNodes->begin(); newIt != newNodes->end(); newIt++){
                bool found = false;
                for(list<int>::iterator oldIt = frameNodes.begin(); oldIt != frameNodes.end(); oldIt++){
                    if(*oldIt == *newIt){
                        found = true;
                        break;
                    }
                }
                if(!found){
                    frameNodes.push_back(*newIt);
                }
            }
            delete newNodes;
        }
    }

    //fill taskToNode
    for(int taskIt = 0; taskIt < jobSize; taskIt++){
        taskToNode[taskIt] = vertexToNode[taskToVertex[taskIt]];
    }

    //free memory
    vertexToNode.clear();
    taskToVertex.clear();
    delete commGraph;
    weightTree.clear();
    marked.clear();
}

void NearestAllocMapper::createCommGraph(const Job & job)
{
    std::vector<std::map<int,int> >* rawCommGraph = job.taskCommInfo->getCommInfo();
    int jobSize = job.procsNeeded;
    int nodesNeeded = ceil((float) jobSize / mach.coresPerNode);

    taskToVertex.clear();
    taskToVertex.resize(jobSize, -1);

    centerTask = job.taskCommInfo->centerTask;

    if(mach.coresPerNode == 1){
        commGraph = rawCommGraph;
        for(int i = 0; i < nodesNeeded; i++){
            taskToVertex[i] = i;
        }
        //assign center task
        if(centerTask == -1){
            centerTask = getCenterTask(*rawCommGraph);
        }
    } else {
        //find which task to put in which node using METIS partitioner
#ifdef HAVE_METIS
        idx_t nvtxs = jobSize; //num vertices
        idx_t ncon = 1; //number of balancing constraints
        idx_t nparts = nodesNeeded; //number of parts to partition
        idx_t objval; //output variable
        //create graph info in CSR format
        vector<idx_t> xadj(jobSize + 1);
        vector<idx_t> adjncy;
        vector<idx_t> adjwgt;
        for(int taskIt = 0; taskIt < jobSize; taskIt++){
            xadj[taskIt] = adjncy.size();
            for(std::map<int, int>::iterator it = rawCommGraph->at(taskIt).begin(); it != rawCommGraph->at(taskIt).end(); it++){
                adjncy.push_back(it->first);
                adjwgt.push_back(it->second);
            }
        }
        xadj[jobSize] = adjncy.size();

        std::vector<idx_t> METIS_taskToVertex(taskToVertex.size()); //to avoid build error

        //partition
        METIS_PartGraphKway(&nvtxs, &ncon, &xadj[0], &adjncy[0], NULL, NULL,
                            &adjwgt[0], &nparts, NULL, NULL, NULL, &objval, &METIS_taskToVertex[0]);

        //check if partitioning is balanced or not, correct if necessary
        //count number of tasks at each vertex
        std::vector<int> numTasksInNode(nodesNeeded, 0);
        std::vector<int> extraTasks; //keeps overflowing tasks
        bool isBalanced = true;
        for(unsigned int taskNo = 0; taskNo < taskToVertex.size(); taskNo++){
            numTasksInNode[METIS_taskToVertex[taskNo]]++;
            if(numTasksInNode[METIS_taskToVertex[taskNo]] > mach.coresPerNode){
                extraTasks.push_back(taskNo);
                isBalanced = false;
            } else {
                taskToVertex[taskNo] = METIS_taskToVertex[taskNo];
            }
        }
        //fix if not balanced
        long int nodeIter = 0;
        while(!isBalanced && !extraTasks.empty()){
            if(numTasksInNode[nodeIter] < mach.coresPerNode){
                numTasksInNode[nodeIter]++;
                taskToVertex[extraTasks.back()] = nodeIter;
                extraTasks.pop_back();
            } else {
                nodeIter++;
            }
        }
        //fill commGraph - O(V + E lg V)
        commGraph = new std::vector<std::map<int,int> >(nodesNeeded);
        //for all tasks
        for(int taskIt = 0; taskIt < jobSize; taskIt++){
            //for all neighbors
            for(map<int, int>::iterator it = rawCommGraph->at(taskIt).begin(); it != rawCommGraph->at(taskIt).end(); it++){
                //add communication weight if not in the same vertex
                if(taskToVertex[taskIt] != taskToVertex[it->first]){
                    if(commGraph->at(taskToVertex[taskIt]).count(taskToVertex[it->first]) == 0){
                        commGraph->at(taskToVertex[taskIt])[taskToVertex[it->first]] = it->second;
                    } else {
                        commGraph->at(taskToVertex[taskIt])[taskToVertex[it->first]] += it->second;
                    }
                }
            }
        }
        delete rawCommGraph;

        //assign center task
        if(centerTask == -1){
            centerTask = getCenterTask(*commGraph);
        } else {
            centerTask = taskToVertex[centerTask];
        }
#else
        schedout.fatal(CALL_INFO, 1, "NearestAllocMapper requires METIS library for multi-core nodes.");
#endif
    }
}

int NearestAllocMapper::getCenterTask(const std::vector<std::map<int,int> > & inCommGraph, const long int upperLimit) const
{
    int centerTask = -1;
    double minDist = DBL_MAX;
    int jobSize = inCommGraph.size();
    long int searchCount = 0;

    double newDist;
    int minTask = max((long int) 0, jobSize / 2  - upperLimit / 2);
    int maxTask = min((long int) jobSize, jobSize / 2 + upperLimit / 2);
    for(int task = minTask; task < maxTask; task++){
        //start from the middle task - higher probability of smallest distance
        newDist = dijkstraWithLimit(inCommGraph, task, minDist);
        if(newDist < minDist){
            minDist = newDist;
            centerTask = task;
        }
        searchCount++;
        if(searchCount > upperLimit){
            break;
        }
    }
    return centerTask;
}

int NearestAllocMapper::getCenterNodeExh(const int nodesNeeded, const long int upperLimit)
{
    int bestNode = -1;
    double bestScore = -DBL_MAX;
    long int searchCount = 0;
    //approximate L1 from center to keep all the nodes in a 3D diamond
    const int optDist = cbrt(nodesNeeded);
    //for all nodes
    for(long int nodeIt = 0; nodeIt < mach.numNodes; nodeIt++){
        if(isFree->at(lastNode)){
            double curScore = 1;
            int availNodes = 1;
            for(int dist = 1; dist <= optDist + 1; dist++){
                double scoreFactor = dist*dist*dist;
                int availInDist = closestNodes(lastNode, dist);
                if(availNodes < nodesNeeded && availNodes + availInDist > nodesNeeded){
                    curScore += (nodesNeeded - availNodes) / scoreFactor;
                    break;
                } else if(availNodes < nodesNeeded){
                    curScore += availInDist / scoreFactor;
                    availNodes += availInDist;
                } else { //penalize extra nodes
                    curScore -= availInDist / scoreFactor;
                }
            }
            
            //update best node
            if(curScore > bestScore){
                bestScore = curScore;
                bestNode = lastNode;
            }

            //preempt if upper bound is reached
            searchCount++;
            if(searchCount >= upperLimit){
                break;
            }
        }
        lastNode = (lastNode + 1) % mach.numNodes;
    }
    return bestNode;
}

int NearestAllocMapper::getCenterNodeGr()
{
    for(long int nodeIt = 0; nodeIt < mach.numNodes; nodeIt++){
        if(isFree->at(lastNode)){
            break;
        } else {
            lastNode = (lastNode + 1) % mach.numNodes;
        }
    }
    return lastNode;
}

double NearestAllocMapper::dijkstraWithLimit(const vector<map<int,int> > & graph,
                                         const unsigned int source,
                                         const double limit
                                         ) const
{
    //initialize
    double totDist = 0;
    vector<double> dists(graph.size(), DBL_MAX);
    dists[source] = 0;
    vector<unsigned int> prevVertex(graph.size());
    prevVertex[source] = source;
    FibonacciHeap heap(dists.size());
    for(unsigned int i = 0; i < dists.size(); i++){
        heap.insert(i, dists[i]);
    }

    //main loop
    while(!heap.isEmpty()){
        unsigned int curNode = heap.deleteMin();
        //preempt if total distance is higher than limit
        totDist += dists[curNode];
        if(totDist > limit){
            break;
        }
        for(std::map<int, int>::const_iterator it = graph[curNode].begin(); it != graph[curNode].end(); it++){
            double newDist = dists[curNode] + (double) 1 / it->second;
            if(newDist < dists[it->first]){
                dists[it->first] = newDist;
                prevVertex[it->first] = curNode;
                heap.decreaseKey(it->first, newDist);
            }
        }
    }
    return totDist;
}

int NearestAllocMapper::closestNodes(const long int srcNode, const int initDist, std::list<int> *outList) const
{
    int nodeCount = 0;
    int retNode;
    int delta = initDist - 1;  //L1 distance to search for
    int srcX = mMachine.xOf(srcNode);
    int srcY = mMachine.yOf(srcNode);
    int srcZ = mMachine.zOf(srcNode);
    int largestDelta = mMachine.getXDim() + mMachine.getYDim() + mMachine.getZDim();
    while(nodeCount == 0){
        //increase L1 distance of the search
        delta++;
        if(delta > largestDelta){
            //no available node found - this is an exception while allocating the last node
            break;
        }
        //scan the nodes with L1 dist = delta;
        for(int x = srcX - delta; x < mMachine.getXDim() && x < (srcX + delta + 1); x++){
            if(x < 0){
                continue;
            } else {
                int yRange = delta - abs(srcX - x);
                for(int y = srcY - yRange; y < mMachine.getYDim() && y < (srcY + yRange + 1); y++){
                    if(y < 0){
                        continue;
                    } else {
                        int zRange = yRange - abs(srcY - y);
                        int z = srcZ - zRange;
                        if(z >= 0) {
                            retNode = mMachine.indexOf(x, y, z);
                            if(isFree->at(retNode)){
                                nodeCount++;
                                if(outList != NULL){
                                    outList->push_back(retNode);
                                }
                            }
                        }
                        z = srcZ + zRange;
                        if(zRange != 0 && z < mMachine.getZDim()){
                            retNode = mMachine.indexOf(x, y, z);
                            if(isFree->at(retNode)){
                                nodeCount++;
                                if(outList != NULL){
                                    outList->push_back(retNode);
                                }
                            }
                        }
                    }
                }
            }
        }
        if(initDist != 0){ //function is called for specific distance
            break;
        }
    }
    return nodeCount;
}

int NearestAllocMapper::bestNode(list<int> & tiedNodes, int inTask) const
{
    int bestNode = 0;
    //optimization
    if(tiedNodes.size() == 1){
        bestNode = tiedNodes.front();
        tiedNodes.clear();
    } else {
        //get allocated neighbors
        vector<int> neighbors;
        vector<int> neighWeights;
        for(map<int, int>::iterator it = commGraph->at(inTask).begin(); it != commGraph->at(inTask).end(); it++){
            if(vertexToNode[it->first] != -1){
                neighbors.push_back(it->first);
                neighWeights.push_back(it->second);
            }
        }

        //for each possible node
        double minDist = DBL_MAX;
        list<int>::iterator bestIt;
        for(list<int>::iterator nodeIt = tiedNodes.begin(); nodeIt != tiedNodes.end(); nodeIt++){
            double curDist = 0;
            //iterate over task neighbors and calculate the total comm distance if this task was allocated here
            for(unsigned int nIt = 0; nIt < neighbors.size(); nIt++){
                curDist += mach.getNodeDistance(vertexToNode[neighbors[nIt]], *nodeIt) * neighWeights[nIt];
            }
            if(curDist < minDist){
                minDist = curDist;
                bestNode = *nodeIt;
                bestIt = nodeIt;
            }
        }
        tiedNodes.erase(bestIt);
    }
    return bestNode;
}

void NearestAllocMapper::getTaskNeighbors(int curTask, list<pair<int, double> > & taskList)
{
    vector<pair<int, double> > neighborArray;

    //find unmarked neighbors
    for(map<int, int>::const_iterator it = commGraph->at(curTask).begin(); it != commGraph->at(curTask).end(); it++){
        if(!marked[it->first]){ //add this neighbor
            marked[it->first] = true;
            //calculate weight
            double weight = 0;
            for(map<int, int>::const_iterator neighIt = commGraph->at(it->first).begin();
                    neighIt != commGraph->at(it->first).end();
                    neighIt++) {
                //if allocated
                if(vertexToNode[neighIt->first] != -1){
                    weight += neighIt->second; //add to weight
                }
            }
            neighborArray.push_back(pair<int, double>(it->first, weight));
        }
    }

    //sort neighborList, ascending
    sort(neighborArray.begin(), neighborArray.end(), ByWeights());

    //merge new list and the input list
    list<pair<int, double> >::iterator taskIt = taskList.begin();
    for(unsigned int i = 0; i < neighborArray.size(); i++){
        while(taskIt != taskList.end() && neighborArray[i].second > taskIt->second){
            taskIt++;
        }
        taskList.insert(taskIt, neighborArray[i]);
    }

    //now we know that last task will be allocated next - if available
    if(!taskList.empty()){
        pair<int, double> toAlloc =  taskList.back();
        taskList.pop_back();
        //update weights in task list based on the first task
        for(taskIt = taskList.begin(); taskIt != taskList.end(); taskIt++){
            //update weights
            if(commGraph->at(toAlloc.first).count(taskIt->first) != 0){
                taskIt->second += commGraph->at(toAlloc.first)[taskIt->first];
            }
        }
        //reorder list
        taskList.sort(ByWeights());
        //re-add toAlloc to the beginning
        taskList.push_back(toAlloc);
    }
}

/*
vector<int> NearestAllocMapper::sortIndices(const vector<int> & toSort) const
{
   vector<pair<int, int> > pairs(toSort.size());
   vector<int> indices(toSort.size());

   for(unsigned int i = 0; i < toSort.size(); i++){
       pairs[i] = pair<int, int>(toSort[i], i);
   }

   sort(pairs.begin(), pairs.end(), SortHelper());

   for(unsigned int i = 0; i < toSort.size(); i++){
       indices[i] = pairs[i].second;
   }

   return indices;
}
*/

