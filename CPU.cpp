/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "CPU.H"

#include <cmath>


OOOCPU::OOOCPU(Engine *engineArg,
		const string& nameArg,
		const string& descArg,
		uint64 debugStartArg,
		StatContainer *statCont,
		unsigned coreIdArg,
		unsigned pidArg,
		IMemoryManager *managerArg,
		IMemory *instrCacheArg,
		IMemory *dataCacheArg,
		TraceReaderBase *readerArg,
		unsigned blockSizeArg,
		uint64 instrLimitArg,
		uint64 instrProgressPeriodArg,
		unsigned robSizeArg,
		unsigned issueWidthArg) :
		CPU(engineArg, nameArg, descArg, debugStartArg, statCont, coreIdArg, pidArg, managerArg, instrCacheArg, dataCacheArg, readerArg, blockSizeArg, instrLimitArg, instrProgressPeriodArg),
				robSize(robSizeArg),
				issueWidth(issueWidthArg),
				instrTotalTime(statCont, nameArg + "_instr_total_time", "Number of cycles of " + descArg + " instruction requests", 0),
				instrL1WaitTime(statCont, nameArg + "_instr_L1_wait_time", "Number of cycles " + descArg + " instruction requests wait for requests to the same block in the L1", 0),
				instrL2WaitTime(statCont, nameArg + "_instr_L2_wait_time", "Number of cycles " + descArg + " instruction requests wait for requests to the same block in the L2", 0),
				instrCpuPauseTime(statCont, nameArg + "_instr_cpu_pause_time", "Number of cycles " + descArg + " instruction requests pause at CPU", 0),
				instrCpuStallTime(statCont, nameArg + "_instr_cpu_stall_time", "Number of cycles " + descArg + " instruction requests stall at CPU", 0),
				instrL1TagTime(statCont, nameArg + "_instr_L1_tag_time", "Number of cycles " + descArg + " instruction requests spend on L1 tag access", 0),
				instrL1StallTime(statCont, nameArg + "_instr_L1_stall_time", "Number of cycles " + descArg + " instruction requests stall at L1", 0),
				instrL2TagTime(statCont, nameArg + "_instr_L2_tag_time", "Number of cycles " + descArg + " instruction requests spend on L2 tag access", 0),
				instrL2StallTime(statCont, nameArg + "_instr_L2_stall_time", "Number of cycles " + descArg + " instruction requests stall at L2", 0),

				instrDramQueueTime(statCont, nameArg + "_instr_dram_queue_time", "Number of cycles " + descArg + " instruction requests wait in the DRAM queue", 0),
				instrDramCloseTime(statCont, nameArg + "_instr_dram_close_time", "Number of cycles " + descArg + " instruction requests wait closing DRAM banks", 0),
				instrDramOpenTime(statCont, nameArg + "_instr_dram_open_time", "Number of cycles of " + descArg + " instruction requests spend opening DRAM banks", 0),
				instrDramAccessTime(statCont, nameArg + "_instr_dram_access_time", "Number of cycles " + descArg + " instruction requests spend accessing DRAM banks", 0),
				instrDramBusQueueTime(statCont, nameArg + "_instr_dram_bus_queue_time", "Number of cycles " + descArg + " instruction requests wait in the DRAM bus queue", 0),
				instrDramBusTime(statCont, nameArg + "_instr_dram_bus_time", "Number of cycles " + descArg + " instruction requests spend in the DRAM bus", 0),

				instrPcmQueueTime(statCont, nameArg + "_instr_pcm_queue_time", "Number of cycles " + descArg + " instruction requests wait in the PCM queue", 0),
				instrPcmCloseTime(statCont, nameArg + "_instr_pcm_close_time", "Number of cycles " + descArg + " instruction requests wait closing PCM banks", 0),
				instrPcmOpenTime(statCont, nameArg + "_instr_pcm_open_time", "Number of cycles of " + descArg + " instruction requests spend opening PCM banks", 0),
				instrPcmAccessTime(statCont, nameArg + "_instr_pcm_access_time", "Number of cycles " + descArg + " instruction requests spend accessing PCM banks", 0),
				instrPcmBusQueueTime(statCont, nameArg + "_instr_pcm_bus_queue_time", "Number of cycles " + descArg + " instruction requests wait in the PCM bus queue", 0),
				instrPcmBusTime(statCont, nameArg + "_instr_pcm_bus_time", "Number of cycles " + descArg + " instruction requests spend in the PCM bus", 0),

				instrL1Count(statCont, nameArg + "_instr_L1_count", "Number of " + descArg + " instruction L1 requests (ignoring reads that wait for other requests)", 0),
				instrL2Count(statCont, nameArg + "_instr_L2_count", "Number of " + descArg + " instruction L2 requests (ignoring reads that wait for other requests)", 0),
				instrDramCloseCount(statCont, nameArg + "_instr_dram_close_count", "Number of " + descArg + " instruction requests that close a DRAM row", 0),
				instrDramOpenCount(statCont, nameArg + "_instr_dram_open_count", "Number of " + descArg + " instruction requests that open a DRAM row", 0),
				instrDramAccessCount(statCont, nameArg + "_instr_dram_access_count", "Number of " + descArg + " instruction requests that access a DRAM row", 0),
				instrPcmCloseCount(statCont, nameArg + "_instr_pcm_close_count", "Number of " + descArg + " instruction requests that close a PCM row", 0),
				instrPcmOpenCount(statCont, nameArg + "_instr_pcm_open_count", "Number of " + descArg + " instruction requests that open a PCM row", 0),
				instrPcmAccessCount(statCont, nameArg + "_instr_pcm_access_count", "Number of " + descArg + " instruction requests that access a PCM row", 0),

				dataTotalTime(statCont, nameArg + "_data_total_time", "Number of cycles of " + descArg + " data requests", 0),
				dataL1WaitTime(statCont, nameArg + "_data_L1_wait_time", "Number of cycles " + descArg + " data requests wait for requests to the same block in the L1", 0),
				dataL2WaitTime(statCont, nameArg + "_data_L2_wait_time", "Number of cycles " + descArg + " data requests wait for requests to the same block in the L2", 0),
				dataCpuPauseTime(statCont, nameArg + "_data_cpu_pause_time", "Number of cycles " + descArg + " data requests pause at CPU", 0),
				dataCpuStallTime(statCont, nameArg + "_data_cpu_stall_time", "Number of cycles " + descArg + " data requests stall at CPU", 0),
				dataL1TagTime(statCont, nameArg + "_data_L1_tag_time", "Number of cycles " + descArg + " data requests spend on L1 tag access", 0),
				dataL1StallTime(statCont, nameArg + "_data_L1_stall_time", "Number of cycles " + descArg + " data requests stall at L1", 0),
				dataL2TagTime(statCont, nameArg + "_data_L2_tag_time", "Number of cycles " + descArg + " data requests spend on L2 tag access", 0),
				dataL2StallTime(statCont, nameArg + "_data_L2_stall_time", "Number of cycles " + descArg + " data requests stall at L2", 0),

				dataDramQueueTime(statCont, nameArg + "_data_dram_queue_time", "Number of cycles " + descArg + " data requests wait in the DRAM queue", 0),
				dataDramCloseTime(statCont, nameArg + "_data_dram_close_time", "Number of cycles " + descArg + " data requests wait closing DRAM banks", 0),
				dataDramOpenTime(statCont, nameArg + "_data_dram_open_time", "Number of cycles of " + descArg + " data requests spend opening DRAM banks", 0),
				dataDramAccessTime(statCont, nameArg + "_data_dram_access_time", "Number of cycles " + descArg + " data requests spend accessing DRAM banks", 0),
				dataDramBusQueueTime(statCont, nameArg + "_data_dram_bus_queue_time", "Number of cycles " + descArg + " data requests wait in the DRAM bus queue", 0),
				dataDramBusTime(statCont, nameArg + "_data_dram_bus_time", "Number of cycles " + descArg + " data requests spend in the DRAM bus", 0),

				dataPcmQueueTime(statCont, nameArg + "_data_pcm_queue_time", "Number of cycles " + descArg + " data requests wait in the PCM queue", 0),
				dataPcmCloseTime(statCont, nameArg + "_data_pcm_close_time", "Number of cycles " + descArg + " data requests wait closing PCM banks", 0),
				dataPcmOpenTime(statCont, nameArg + "_data_pcm_open_time", "Number of cycles of " + descArg + " data requests spend opening PCM banks", 0),
				dataPcmAccessTime(statCont, nameArg + "_data_pcm_access_time", "Number of cycles " + descArg + " data requests spend accessing PCM banks", 0),
				dataPcmBusQueueTime(statCont, nameArg + "_data_pcm_bus_queue_time", "Number of cycles " + descArg + " data requests wait in the PCM bus queue", 0),
				dataPcmBusTime(statCont, nameArg + "_data_pcm_bus_time", "Number of cycles " + descArg + " data requests spend in the PCM bus", 0),

				dataL1Count(statCont, nameArg + "_data_L1_count", "Number of " + descArg + " data L1 requests (ignoring reads that wait for other requests)", 0),
				dataL2Count(statCont, nameArg + "_data_L2_count", "Number of " + descArg + " data L2 requests (ignoring reads that wait for other requests)", 0),
				dataDramCloseCount(statCont, nameArg + "_data_dram_close_count", "Number of " + descArg + " data requests that close a DRAM row", 0),
				dataDramOpenCount(statCont, nameArg + "_data_dram_open_count", "Number of " + descArg + " data requests that open a DRAM row", 0),
				dataDramAccessCount(statCont, nameArg + "_data_dram_access_count", "Number of " + descArg + " data requests that access a DRAM row", 0),
				dataPcmCloseCount(statCont, nameArg + "_data_pcm_close_count", "Number of " + descArg + " data requests that close a PCM row", 0),
				dataPcmOpenCount(statCont, nameArg + "_data_pcm_open_count", "Number of " + descArg + " data requests that open a PCM row", 0),
				dataPcmAccessCount(statCont, nameArg + "_data_pcm_access_count", "Number of " + descArg + " data requests that access a PCM row", 0)

				{

	rob = new RobEntry[robSize];
	for (unsigned i = 0; i < robSize; i++){
		rob[i].state = DONE;
	}
	robHead = 0;
	robTail = 0;
	robFull = false;

	instrPaused = false;

	fetchEntry = new unsigned[issueWidth];
	numFetchEntries = 0;

	nextFetchEntry = 0;
	nextEntryValid = 0;
	lastEntry = numeric_limits<unsigned>::max();
	currentTraceTimestamp = 0;

	eventScheduled[0] = false;
	eventScheduled[1] = false;
	instrUnstall[0] = false;
	instrUnstall[1] = false;
	dataUnstall[0] = false;
	dataUnstall[1] = false;
}

OOOCPU::~OOOCPU(){
	debug(": robHead: %u, robTail: %u, robFull: %u", robHead, robTail, robFull);
	myassert(robHead == robTail && !robFull);
	for (unsigned  i = 0; i < robSize; i++){
		myassert(rob[i].state == DONE);
	}
}

void OOOCPU::start(){
	nextEntryValid = readNextEntry();
	if (nextEntryValid){
		currentTraceTimestamp = firstEntry.timestamp;
		scheduleEvent();
	}
}

void OOOCPU::process(const Event * event){
	uint64 timestamp = engine->getTimestamp();
	EventType type = static_cast<EventType>(event->getData());
	if(type == PROCESS) {
		totalTime = timestamp;
		uint64 index = (timestamp - 1) % 2;
		debug("(): index: %lu", index);
		myassert(eventScheduled[index]);
		eventScheduled[index] = false;

		resumePrivate(index);

		if (instrUnstall[index]){
			instrUnstall[index] = false;
			unstallInstr();
		}

		if (dataUnstall[index]){
			dataUnstall[index] = false;
			unstallData();
		}

		processInstr(index);
		fetch();
		processData(index);
		commit();
	} else if (type == DRAIN){
		auto dit = drainRequests.begin();
		while (dit != drainRequests.end() && dit->second.requestsLeft > 0){
			++dit;
		}
		myassert(dit != drainRequests.end());
		addrint page = dit->first;
		IDrainCallback *callback = dit->second.callback;
		drainRequests.erase(dit);
		callback->drainCompleted(page);
	} else {
		myassert(false);
	}

}

void OOOCPU::accessCompleted(MemoryRequest *request, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%p, %s)", request, caller->getName());
	myassert(request->read);
	if (request->instr){
		instrMsg[timestamp % 2].emplace_back(request);
	} else {
		dataMsg[timestamp % 2].emplace_back(request);
	}
	scheduleEvent();
}

void OOOCPU::unstall(IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%s)", caller->getName());
	if (caller == instrCache){
		instrUnstall[timestamp % 2] = true;
	}
	if (caller == dataCache){
		dataUnstall[timestamp % 2] = true;
	}
	scheduleEvent();
}

void OOOCPU::resume(unsigned pidArg, addrint virtualPage){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	myassert(pidArg == pid);
	resumeMsg[timestamp % 2].emplace_back(virtualPage);
	scheduleEvent();
}

void OOOCPU::drain(addrint page, IDrainCallback *caller){
	unsigned hits = 0;
	for (auto it = stalledInstrRequests.begin(); it != stalledInstrRequests.end(); ++it){
		if (page == manager->getIndex((*it)->addr)){
			hits++;
		}
	}
	for (auto it = stalledDataRequests.begin(); it != stalledDataRequests.end(); ++it){
		if (page == manager->getIndex((*it)->addr)){
			hits++;
		}
	}
	bool ins = drainRequests.emplace(page, DrainEntry(caller, hits)).second;
	myassert(ins);
	if (hits == 0){
		addEvent(1, DRAIN);
	}
}

void OOOCPU::resumePrivate(uint64 index){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu)", index);
	bool instrPausedAgain = false;
	bool instrUnpaused = false;
	for (vector<addrint>::iterator mit = resumeMsg[index].begin(); mit != resumeMsg[index].end(); ++mit){
		debug(" resumeMsg: %lu", *mit);
		auto pit = pausers.find(*mit);
		myassert(pit != pausers.end());
		auto rit = pit->second.begin();
		while (rit != pit->second.end()){
			debug("\trob[%u].state: %u", *rit, rob[*rit].state);
			bool pausedAgain = false;
			if (rob[*rit].state == FETCHING){
				for (unsigned i = 0; i < rob[*rit].numInstr; i++){
					if (rob[*rit].instrPause[i] && manager->getIndex(rob[*rit].instrReqs[i].addr) == pit->first){
						addrint physicalAddr;
						if (manager->access(pid, rob[*rit].instrReqs[i].addr, true, true, &physicalAddr, this)){
							pausedAgain = true;
							instrPausedAgain = true;
						} else {
							instrUnpaused = true;
							rob[*rit].instrReqs[i].addr = physicalAddr;
							rob[*rit].instrPause[i] = false;
							rob[*rit].instrReqs[i].counters[CPU_PAUSE] = timestamp - rob[*rit].instrReqs[i].counters[CPU_PAUSE];
							debug(":\tcalling 1 instrCache.access(%p, %lu, %s)", &rob[*rit].instrReqs[i], physicalAddr, rob[*rit].instrReqs[i].read ? "read" : "write");
							if(!stalledInstrRequests.empty() || !instrCache->access(&rob[*rit].instrReqs[i], this)){
								stalledInstrRequests.emplace_back(&rob[*rit].instrReqs[i]);
								rob[*rit].instrReqs[i].counters[CPU_STALL] = timestamp;
							}
							instrReads++;
						}
					}
				}
			} else if (rob[*rit].state == DATA){
				for (unsigned i = 0; i < rob[*rit].numData; i++){
					if (rob[*rit].dataPause[i] && manager->getIndex(rob[*rit].dataReqs[i]->addr) == pit->first){
						addrint physicalAddr;
						if (manager->access(pid, rob[*rit].dataReqs[i]->addr, rob[*rit].dataReqs[i]->read, false, &physicalAddr, this)){
							debug(":\tdata stalled again");
							pausedAgain = true;
						} else {
							rob[*rit].dataReqs[i]->addr = physicalAddr;
							rob[*rit].dataPause[i] = false;
							if (rob[*rit].dataReqs[i]->read){
								bool ins = lsq.emplace(rob[*rit].dataReqs[i], *rit).second;
								myassert(ins);
							}
							rob[*rit].dataReqs[i]->counters[CPU_PAUSE] = timestamp - rob[*rit].dataReqs[i]->counters[CPU_PAUSE];
							debug(":\tcalling 1 dataCache.access(%p, %lu, %s)", rob[*rit].dataReqs[i], physicalAddr, rob[*rit].dataReqs[i]->read ? "read" : "write");
							if (!stalledDataRequests.empty() || !dataCache->access(rob[*rit].dataReqs[i], this)){
								debug(":\tstalled on data request: %lu", physicalAddr);
								rob[*rit].dataReqs[i]->counters[CPU_STALL] = timestamp;
								stalledDataRequests.emplace_back(rob[*rit].dataReqs[i]);
							}

							if (rob[*rit].dataReqs[i]->read){
								dataReads++;
							} else {
								rob[*rit].numDataLeft--;
								dataWrites++;
							}
						}
					}
				}
				if (rob[*rit].numDataLeft == 0){
					rob[*rit].state = DONE;
				}
			} else {
				myassert(false);
			}

			if (pausedAgain){
				rit++;
			} else {
				rit = pit->second.erase(rit);
			}

		}
		if (pit->second.empty()){
			pausers.erase(pit);
		}
	}
	resumeMsg[index].clear();

	if (instrPaused && instrUnpaused && !instrPausedAgain){
		instrPaused = false;
		numPauseCycles += (timestamp - startPauseTimestamp);
	}
}

void OOOCPU::unstallInstr(){
	uint64 timestamp = engine->getTimestamp();
	list<MemoryRequest *>::iterator it = stalledInstrRequests.begin();
	while(it != stalledInstrRequests.end()){
		uint64 origStallTimestamp = (*it)->counters[CPU_STALL];
		(*it)->counters[CPU_STALL] = timestamp - (*it)->counters[CPU_STALL];
		if (instrCache->access(*it, this)){
			debug(":\tunstalled instruction access: %lu", (*it)->addr);
			auto dit = drainRequests.find(manager->getIndex((*it)->addr));
			if (dit != drainRequests.end()){
				myassert(dit->second.requestsLeft > 0);
				dit->second.requestsLeft--;
				if (dit->second.requestsLeft == 0){
					addEvent(1, DRAIN);
				}
			}
			it = stalledInstrRequests.erase(it);
		} else {
			(*it)->counters[CPU_STALL] = origStallTimestamp;
			break;
		}
	}
}

void OOOCPU::unstallData(){
	uint64 timestamp = engine->getTimestamp();
	list<MemoryRequest *>::iterator it = stalledDataRequests.begin();
	while(it != stalledDataRequests.end()){
		uint64 origStallTimestamp = (*it)->counters[CPU_STALL];
		(*it)->counters[CPU_STALL] = timestamp - (*it)->counters[CPU_STALL];
		if (dataCache->access(*it, this)){
			debug(":\tunstalled data access: %lu", (*it)->addr);
			auto dit = drainRequests.find(manager->getIndex((*it)->addr));
			if (dit != drainRequests.end()){
				myassert(dit->second.requestsLeft > 0);
				dit->second.requestsLeft--;
				if (dit->second.requestsLeft == 0){
					addEvent(1, DRAIN);
				}
			}
			it = stalledDataRequests.erase(it);
		} else {
			(*it)->counters[CPU_STALL] = origStallTimestamp;
			break;
		}
	}
}

void OOOCPU::processInstr(uint64 index){
	uint64 timestamp = engine->getTimestamp();
	for (vector<MemoryRequest *>::iterator oit = instrMsg[index].begin(); oit != instrMsg[index].end(); ++oit){
		RobEntry *robPtr;
		unsigned entry;
		bool found = false;
		while (!found)	{
			unsigned i;
			entry = fetchEntry[nextFetchEntry];
			robPtr = &rob[entry];
			if (robPtr->state == FETCHING){
				for (i = 0; i < robPtr->numInstr; i++){
					if (*oit == &robPtr->instrReqs[i]){
						found = true;
						robPtr->instrReqs[i].counters[TOTAL] = timestamp - 1 - robPtr->instrReqs[i].counters[TOTAL];
//						if (!robPtr->instrReqs[i].checkCounters()){
//							cout << timestamp << ": " << robPtr->instrReqs[i].addr << endl;
//							robPtr->instrReqs[i].printCounters();
//						}
//						myassert(robPtr->instrReqs[i].checkCounters());
						countInstr(&robPtr->instrReqs[i]);
						robPtr->instrReqs[i].addr = INVALID;
						break;
					}
				}
			}

			if (found){
				if (nextFetchEntry < numFetchEntries && i + 1 == robPtr->numInstr){
					nextFetchEntry = (nextFetchEntry + 1) % numFetchEntries;
				}
			} else {
				nextFetchEntry = (nextFetchEntry + 1) % numFetchEntries;
			}
		}
		myassert(found);
		robPtr->numInstrLeft--;
		debug(":\trob[%u].numInstrLeft: %u", entry, robPtr->numInstrLeft);
		if (robPtr->numInstrLeft == 0){
			if (robPtr->numData == 0){
				robPtr->state = DONE;
				debug(":\trob[%u].state = DONE", entry);
			} else {
				robPtr->state = DATA;
				debug(":\trob[%u].state = DATA", entry);
				debug(":\trob[%u].numData: %u", entry, robPtr->numData);
				debug(":\trob[%u].numDataLeft: %u", entry, robPtr->numDataLeft);
				for (unsigned i = 0; i < robPtr->numData; i++){
					MemoryRequest *req = robPtr->dataReqs[i];
					req->counters[TOTAL] = timestamp;
					addrint physicalAddr;
					if (manager->access(pid, req->addr, req->read, false, &physicalAddr, this)){
						robPtr->dataPause[i] = true;
						req->counters[CPU_PAUSE] = timestamp;
						addrint virtualPage = manager->getIndex(req->addr);
						debug(":\trob[%u] paused on data access to virtual page: %lu", entry, virtualPage);
						auto rit = pausers.emplace(virtualPage, set<unsigned>()).first;
						rit->second.emplace(entry);
					} else {
						req->addr = physicalAddr;
						robPtr->dataPause[i] = false;
						if (req->read){
							bool ins = lsq.emplace(req, entry).second;
							myassert(ins);
						}
						debug(":\tcalling 2 dataCache.access(%p, %lu)", req, physicalAddr);
						if (!stalledDataRequests.empty() || !dataCache->access(req, this)){
							debug(": stalled on data request: %lu", physicalAddr);
							stalledDataRequests.emplace_back(req);
							req->counters[CPU_STALL] = timestamp;
						}

						if (req->read){
							dataReads++;
						} else {
							robPtr->numDataLeft--;
							dataWrites++;
						}
					}
				}
				if (robPtr->numDataLeft == 0){
					robPtr->state = DONE;
				}
			}
		}
	}
	instrMsg[index].clear();
}

void OOOCPU::processData(uint64 index){
	uint64 timestamp = engine->getTimestamp();
	for (vector<MemoryRequest *>::iterator oit = dataMsg[index].begin(); oit != dataMsg[index].end(); ++oit){
		debug(":\tdata message: %p", *oit);
		QueueMap::iterator it = lsq.find(*oit);
		if (it != lsq.end()){
			RobEntry *robPtr = &rob[it->second];
			myassert(robPtr->state == DATA);
			bool found = false;
			for (unsigned i = 0; i < robPtr->numData; i++){
				MemoryRequest *req = robPtr->dataReqs[i];
				if (*oit == req){
					found = true;
					req->counters[TOTAL] = timestamp - 1 - req->counters[TOTAL];
//					if (!req->checkCounters()){
//						cout << timestamp << ": " << req << ", " << req->addr << endl;
//						req->printCounters();
//
//					}
//					myassert(req->checkCounters());
					countData(req);
					delete req;
					robPtr->numDataLeft--;
					break;
				}
			}
			myassert(found);
			if (robPtr->numDataLeft == 0){
				robPtr->state = DONE;
				debug(":\trob[%u].state = DONE", it->second);
			}
			lsq.erase(it);
		} else {
			myassert(false);
		}
	}
	dataMsg[index].clear();
}

void OOOCPU::fetch(){
	//assumes firstEntry, secondEntry and secondEntryValid have been set
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	bool fetchNext = true;
	for (unsigned i = 0; i < numFetchEntries; i++){
		debug(" rob[%u].state: %u", fetchEntry[i], rob[fetchEntry[i]].state);
		if(rob[fetchEntry[i]].state == FETCHING){
			fetchNext = false;
		}
	}
	if (fetchNext && nextEntryValid && !robFull && !instrPaused && stalledInstrRequests.empty() && stalledDataRequests.empty()){
		numFetchEntries = 0;
		nextFetchEntry = 0;
		while(nextEntryValid && !robFull && numFetchEntries < issueWidth){
			RobEntry *robHeadPtr = &rob[robHead];
			uint8 num_instr = 0;
			robHeadPtr->numData = 0;
			robHeadPtr->state = FETCHING;
			debug(" issue rob[%u]", robHead);
			do {
				if (firstEntry.instr){
					addrint physicalAddr;
					MemoryRequest *req = &robHeadPtr->instrReqs[num_instr];
					req->resetCounters();
					req->size = blockSize;
					req->read = true;
					req->instr = true;
					req->priority = HIGH;
					req->counters[TOTAL] = timestamp;
					if (manager->access(pid, firstEntry.address, true, true, &physicalAddr, this)){
						req->addr = firstEntry.address;
						robHeadPtr->instrPause[num_instr] = true;
						req->counters[CPU_PAUSE] = timestamp;
						addrint virtualPage = manager->getIndex(firstEntry.address);
						debug(" paused on instruction access to virtual page: %lu", virtualPage);
						auto rit = pausers.emplace(virtualPage, set<unsigned>()).first;
						rit->second.emplace(robHead);
						instrPaused = true;
						startPauseTimestamp = timestamp;
					} else {
						myassert(physicalAddr != INVALID);
						req->addr = physicalAddr;
						robHeadPtr->instrPause[num_instr] = false;
						debug(":\tcalling 2 instrCache.access(%p, %lu)", req, physicalAddr);
						if (!stalledInstrRequests.empty() || !instrCache->access(req, this)){
							stalledInstrRequests.emplace_back(req);
							req->counters[CPU_STALL] = timestamp;
						}
						instrReads++;
					}
					num_instr++;
				} else {
					robHeadPtr->dataReqs[robHeadPtr->numData] = new MemoryRequest(firstEntry.address, blockSize, firstEntry.read, false, HIGH);
					robHeadPtr->numData++;
				}

				nextEntryValid = readNextEntry();
			} while (nextEntryValid && currentTraceTimestamp == firstEntry.timestamp);
			currentTraceTimestamp = firstEntry.timestamp;

			robHeadPtr->numDataLeft = robHeadPtr->numData;
			robHeadPtr->numInstr = robHeadPtr->numInstrLeft = num_instr;

			fetchEntry[numFetchEntries] = robHead;
			numFetchEntries++;

			robHead = (robHead + 1) % robSize;
			if (robHead == robTail){
				robFull = true;
			}

			if (!nextEntryValid){
				lastEntry = robHead;
			}

		}
	} else {
		debug(" did not fetch. fetchNext: %d, nextEntryValid: %d, !robFull: %d, !instrPaused: %d, stalledInstrRequests.empty(): %d, stalledDataRequests.empty(): %d", fetchNext, nextEntryValid, !robFull, !instrPaused, stalledInstrRequests.empty(), stalledDataRequests.empty());
	}
}

void OOOCPU::commit(){
	debug(":\tcommit: robHead: %u, robTail: %u, robFull: %s, rob[%u].state: %d", robHead, robTail, robFull?"true":"false", robTail, rob[robTail].state);
	unsigned numCommits = 0;
	while ((robHead != robTail || robFull) && rob[robTail].state == DONE && numCommits < issueWidth){
		debug(":\t\tcommit rob[%u]", robTail);

		robTail = (robTail + 1) % robSize;
		if (robFull){
			robFull = false;
		}

		numCommits++;
	}

	if (nextEntryValid || robTail != lastEntry){
		if (numCommits > 0 && rob[robTail].state == DONE){
			scheduleEvent();
		}
	} else {
		manager->finish(coreId);
	}

//	unsigned usedEntries;
//	if (robHead > robTail){
//		usedEntries = robHead - robTail;
//	} else if (robHead < robTail){
//		usedEntries = robSize - robTail + robHead;
//	} else {
//		if (robFull){
//			usedEntries = robSize;
//		} else {
//			usedEntries = 0;
//		}
//	}
//	cout << timestamp << ": commit: used rob entries: " << usedEntries << " lsq size: " << lsq.size() << endl;
}

void OOOCPU::scheduleEvent(){
	uint64 index = (engine->getTimestamp()) % 2;
	if (!eventScheduled[index]){
		eventScheduled[index] = true;
		addEvent(1, PROCESS);
	}
}

void OOOCPU::addEvent(uint64 delay, EventType type){
	engine->addEvent(delay, this, type);
}

void OOOCPU::countInstr(MemoryRequest *request){
	instrTotalTime += request->counters[0];
	instrL1WaitTime += request->counters[1];
	instrL2WaitTime += request->counters[2];
	instrCpuPauseTime += request->counters[3];
	instrCpuStallTime += request->counters[4];
	instrL1TagTime += request->counters[5];
	instrL1StallTime += request->counters[6];
	instrL2TagTime += request->counters[7];
	instrL2StallTime += request->counters[8];
	instrDramQueueTime += request->counters[9];
	instrDramCloseTime += request->counters[10];
	instrDramOpenTime += request->counters[11];
	instrDramAccessTime += request->counters[12];
	instrDramBusQueueTime += request->counters[13];
	instrDramBusTime += request->counters[14];
	instrPcmQueueTime += request->counters[15];
	instrPcmCloseTime += request->counters[16];
	instrPcmOpenTime += request->counters[17];
	instrPcmAccessTime += request->counters[18];
	instrPcmBusQueueTime += request->counters[19];
	instrPcmBusTime += request->counters[20];

	instrL1Count += (request->counters[L1_TAG] == 0 ? 0 : 1);
	instrL2Count += (request->counters[L2_TAG] == 0 ? 0 : 1);
	instrDramCloseCount += (request->counters[DRAM_CLOSE] == 0 ? 0 : 1);
	instrDramOpenCount += (request->counters[DRAM_OPEN] == 0 ? 0 : 1);
	instrDramAccessCount += (request->counters[DRAM_ACCESS] == 0 ? 0 : 1);
	instrPcmCloseCount += (request->counters[PCM_CLOSE] == 0 ? 0 : 1);
	instrPcmOpenCount += (request->counters[PCM_OPEN] == 0 ? 0 : 1);
	instrPcmAccessCount += (request->counters[PCM_ACCESS] == 0 ? 0 : 1);
}

void OOOCPU::countData(MemoryRequest *request){
	dataTotalTime += request->counters[0];
	dataL1WaitTime += request->counters[1];
	dataL2WaitTime += request->counters[2];
	dataCpuPauseTime += request->counters[3];
	dataCpuStallTime += request->counters[4];
	dataL1TagTime += request->counters[5];
	dataL1StallTime += request->counters[6];
	dataL2TagTime += request->counters[7];
	dataL2StallTime += request->counters[8];
	dataDramQueueTime += request->counters[9];
	dataDramCloseTime += request->counters[10];
	dataDramOpenTime += request->counters[11];
	dataDramAccessTime += request->counters[12];
	dataDramBusQueueTime += request->counters[13];
	dataDramBusTime += request->counters[14];
	dataPcmQueueTime += request->counters[15];
	dataPcmCloseTime += request->counters[16];
	dataPcmOpenTime += request->counters[17];
	dataPcmAccessTime += request->counters[18];
	dataPcmBusQueueTime += request->counters[19];
	dataPcmBusTime += request->counters[20];

	dataL1Count += (request->counters[L1_TAG] == 0 ? 0 : 1);
	dataL2Count += (request->counters[L2_TAG] == 0 ? 0 : 1);
	dataDramCloseCount += (request->counters[DRAM_CLOSE] == 0 ? 0 : 1);
	dataDramOpenCount += (request->counters[DRAM_OPEN] == 0 ? 0 : 1);
	dataDramAccessCount += (request->counters[DRAM_ACCESS] == 0 ? 0 : 1);
	dataPcmCloseCount += (request->counters[PCM_CLOSE] == 0 ? 0 : 1);
	dataPcmOpenCount += (request->counters[PCM_OPEN] == 0 ? 0 : 1);
	dataPcmAccessCount += (request->counters[PCM_ACCESS] == 0 ? 0 : 1);
}

IOCPU::IOCPU(Engine *engineArg,
		const string& nameArg,
		const string& descArg,
		uint64 debugArg,
		StatContainer *statCont,
		unsigned coreIdArg,
		unsigned pidArg,
		IMemoryManager *managerArg,
		IMemory *instrCacheArg,
		IMemory *dataCacheArg,
		TraceReaderBase *readerArg,
		unsigned blockSizeArg,
		uint64 instrLimitArg,
		uint64 instrProgressPeriodArg) :
		CPU(engineArg, nameArg, descArg, debugArg, statCont, coreIdArg, pidArg, managerArg, instrCacheArg, dataCacheArg, readerArg, blockSizeArg, instrLimitArg, instrProgressPeriodArg),
				exec(statCont, nameArg + "_execution", "Number of cycles " + descArg + " spends in execution", 0) {

	timestamp = 0;

	request.addr = 0;
	request.priority = HIGH;

	startAccessTime = 0;

	waiting = false;
}

void IOCPU::start(){
	if (readNextEntry()){
		timestamp = firstEntry.timestamp;
		scheduleNextAccess();
	}
}

void IOCPU::process(const Event * event){
	uint64 timestamp = engine->getTimestamp();
	totalTime = timestamp;
	if (manager->access(pid, firstEntry.address, firstEntry.read, firstEntry.instr, &request.addr, this)){
		startPauseTimestamp = timestamp;
	} else {
		request.size = firstEntry.size;
		request.read = firstEntry.read;
		request.instr = firstEntry.instr;
		if (firstEntry.instr){
			instrReads++;
			instrCache->access(&request, this);
		} else {
			if (firstEntry.read){
				dataReads++;
			} else {
				dataWrites++;
			}
			dataCache->access(&request, this);
		}
		waiting = true;
		startAccessTime = timestamp;
	}
}

void IOCPU::accessCompleted(addrint addr, IMemory *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s)", addr, caller->getName());
	myassert((firstEntry.instr && caller == instrCache) || (!firstEntry.instr && caller == dataCache));
	myassert(waiting);
	waiting = false;
	numAccessCycles += (timestamp - startAccessTime);
	myassert(addr == request.addr);
	if (readNextEntry()){
		scheduleNextAccess();
	} else {
		manager->finish(coreId);
	}
}

void IOCPU::resume(unsigned pid, addrint virtualPage){
	numPauseCycles += (engine->getTimestamp() - startPauseTimestamp);
	engine->addEvent(0, this);
}



void IOCPU::scheduleNextAccess(){
	uint64 delay = firstEntry.timestamp - timestamp;
	timestamp = firstEntry.timestamp;
	engine->addEvent(delay, this);
	exec += delay;
	if (delay > 1){
		numSleepCycles += (delay - 1);
	}
}

CPU::CPU(Engine *engineArg,
		const string& nameArg,
		const string& descArg,
		uint64 debugStartArg,
		StatContainer *statCont,
		unsigned coreIdArg,
		unsigned pidArg,
		IMemoryManager *managerArg,
		IMemory *instrCacheArg,
		IMemory *dataCacheArg,
		TraceReaderBase *readerArg,
		unsigned blockSizeArg,
		uint64 instrLimitArg,
		uint64 instrProgressPeriodArg) :
				engine(engineArg),
				name(nameArg),
				desc(descArg),
				debugStart(debugStartArg),
				coreId(coreIdArg),
				pid(pidArg),
				manager(managerArg),
				instrCache(instrCacheArg),
				dataCache(dataCacheArg),
				reader(readerArg),
				blockSize(blockSizeArg),
				instrLimit(instrLimitArg),
				instrProgressPeriod(instrProgressPeriodArg),
				instrExecuted(statCont, nameArg + "_instructions_executed", "Number of " + descArg + " instructions executed", 0),
				totalTime(statCont, nameArg + "_total_time", "Total number of " + descArg + " cycles", 0),
				instrReads(statCont, nameArg + "_instruction_reads", "Number of " + descArg + " instruction reads", 0),
				dataReads(statCont, nameArg + "_data_reads", "Number of " + descArg + " data reads", 0),
				dataWrites(statCont, nameArg + "_data_writes", "Number of " + descArg + " data writes", 0),
				numSleepCycles(statCont, nameArg + "_sleep_cycles", "Number of " + descArg + " sleep cycles", 0),
				numAccessCycles(statCont, nameArg + "_access_cycles", "Number of " + descArg + " access cycles", 0),
				numPauseCycles(statCont, nameArg + "_pause_cycles", "Number of " + descArg + " pause cycles", 0),
				ipc(statCont, nameArg + "_ipc", descArg + " IPC", &instrExecuted, &totalTime) {

	unsigned logBlockSize = (unsigned) logb(blockSizeArg);
	blockSize = 1 << logBlockSize;
	offsetWidth = (int) logb(blockSize);
	offsetMask = 0;
	for (unsigned i = 0; i < offsetWidth; i++) {
		offsetMask |= (addrint) 1U << i;
	}

	numInstr = 0;

	secondEntryValid = false;

	startPauseTimestamp= 0;

	gettimeofday(&lastRealTime, NULL);
	lastNumEvents = engine->getNumEvents();
	lastTimestamp = engine->getTimestamp();
}

bool CPU::readNextEntry(){
//	uint64 timestamp = engine->getTimestamp();
//	if (numInstr >= 620000000){
//		info(": %lu", numInstr);
//	}
	if (secondEntryValid){
		firstEntry = secondEntry;
		secondEntryValid = false;
	} else {
		if (reader->readEntry(&firstEntry)){
			addrint firstByteBlockAddress = firstEntry.address & ~offsetMask;
			addrint lastByteBlockAddress = (firstEntry.address + firstEntry.size - 1) & ~offsetMask;
			if (firstByteBlockAddress == lastByteBlockAddress){
				firstEntry.address = firstByteBlockAddress;
				firstEntry.size = blockSize;
				secondEntryValid = false;
			} else if (firstByteBlockAddress + blockSize == lastByteBlockAddress){
				firstEntry.address = firstByteBlockAddress;
				firstEntry.size = blockSize;
				secondEntry.timestamp = firstEntry.timestamp;
				secondEntry.address = lastByteBlockAddress;
				secondEntry.size = blockSize;
				secondEntry.read = firstEntry.read;
				secondEntry.instr = firstEntry.instr;
				secondEntryValid = true;
			} else {
				error("Access covers more than one cache block");
			}
			if (firstEntry.instr){
				numInstr++;
				instrCounter++;
				instrExecuted++;
				if (instrProgressPeriod != 0 && numInstr % instrProgressPeriod == 0){
					struct timeval current;
					gettimeofday(&current, NULL);
					long seconds  = current.tv_sec  - lastRealTime.tv_sec;
					long useconds = current.tv_usec - lastRealTime.tv_usec;
					uint64 currentNumEvents = engine->getNumEvents();
					uint64 currentTimestamp = engine->getTimestamp();

					double stime = static_cast<double>(seconds) + static_cast<double>(useconds)/1000000.0;
					double instrPerSecond = static_cast<double>(instrProgressPeriod) / stime;
					double eventsPerSecond = static_cast<double>(currentNumEvents - lastNumEvents) / stime;
					double instrPerCycle = static_cast<double>(instrProgressPeriod) / static_cast<double>(currentTimestamp - lastTimestamp);

					lastRealTime = current;
					lastNumEvents = currentNumEvents;
					lastTimestamp = currentTimestamp;

					printf("Instruction: %lu timestamp: %lu cycles, %.6g instr/sec, %.6g events/sec, %.2f instr/cycle\n", numInstr, currentTimestamp, instrPerSecond, eventsPerSecond, instrPerCycle);
					fflush(stdout);
				}
			}
		} else {
			return false;
		}
	}

	if (firstEntry.instr){
		return numInstr <= instrLimit;
	} else {
		return true;
	}
}
