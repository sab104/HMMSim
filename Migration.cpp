/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Migration.H"

#include <zlib.h>

#include <algorithm>
#include <functional>
#include <iterator>

#include <cmath>

BaseMigrationPolicy::BaseMigrationPolicy(
	const string& nameArg,
	Engine *engineArg,
	uint64 debugStartArg,
	uint64 dramPagesArg,
	AllocationPolicy allocPolicyArg,
	unsigned numPidsArg,
	double maxFreeDramArg) :
		name(nameArg),
		engine(engineArg),
		debugStart(debugStartArg),
		dramPages(dramPagesArg),
		allocPolicy(allocPolicyArg),
		numPids(numPidsArg),
		maxFreeDram(maxFreeDramArg){

	myassert(numPids > 0);
	dramPagesLeft = dramPages;
	maxFreeDramPages = dramPages * maxFreeDram;

	dramFull = false;
}

PageType BaseMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	PageType ret;
	if (allocPolicy == DRAM_FIRST) {
		if (dramFull){
			ret = PCM;
		} else {
			ret = DRAM;
		}
	} else if (allocPolicy == PCM_ONLY) {
		ret = PCM;
	} else {
		myassert(false);
	}

	if (ret == DRAM){
		myassert(dramPagesLeft > 0);
		dramPagesLeft--;
		if (dramPagesLeft == 0){
			dramFull = true;
		}

	}

	return ret;
}

bool BaseMigrationPolicy::demote(unsigned *pid, addrint *addr){
	if (allocPolicy == DRAM_FIRST) {
		if (dramFull){
			return selectDemotionPage(pid, addr);
		} else {
			return false;
		}
	} else if (allocPolicy == PCM_ONLY) {
		return selectDemotionPage(pid, addr);
	} else {
		myassert(false);
		return false;
	}
}

void BaseMigrationPolicy::monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progressArg){
	progress = progressArg;
}

void BaseMigrationPolicy::setInstrCounter(Counter* counter){
	instrCounter = counter;
}

void BaseMigrationPolicy::setNumDramPages(uint64 dramPagesNew){
	if (dramPagesNew > dramPages) {
		dramPagesLeft += (dramPagesNew - dramPages);
	} else if (dramPagesNew < dramPages) {
		myassert(dramPagesNew >= 0);
		dramPagesLeft -= (dramPages - dramPagesNew);
		if (!dramFull){
			if (dramPagesLeft <= 0){
				dramFull = true;
			}
		}
	} else {
		return;
	}
	dramPages = dramPagesNew;
	maxFreeDramPages = dramPages * maxFreeDram;
}

void BaseMigrationPolicy::finish(){

}

NoMigrationPolicy::NoMigrationPolicy(
	const string& nameArg,
	Engine *engineArg,
	uint64 debugArg,
	uint64 dramPagesArg,
	AllocationPolicy allocPolicyArg,
	unsigned numPidsArg) : BaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, numPidsArg, 0.0) {
}

//Multi Queue

MultiQueueMigrationPolicy::MultiQueueMigrationPolicy(
	const string& nameArg,
	Engine *engineArg,
	uint64 debugStartArg,
	uint64 dramPagesArg,
	AllocationPolicy allocPolicyArg,
	unsigned numPidsArg,
	double maxFreeDramArg,
	unsigned numQueuesArg,
	unsigned thresholdQueueArg,
	uint64 lifetimeArg,
	bool logicalTimeArg,
	uint64 filterThresholdArg,
	bool secondDemotionEvictionArg,
	bool agingArg,
	bool useHistoryArg,
	bool usePendingListArg,
	bool enableRollbackArg,
	bool promotionFilterArg,
	unsigned demotionAttemptsArg,
	uint64 rollbackExpirationArg) :
		BaseMigrationPolicy(nameArg, engineArg, debugStartArg, dramPagesArg, allocPolicyArg, numPidsArg, maxFreeDramArg),
		numQueues(numQueuesArg),
		thresholdQueue(thresholdQueueArg),
		lifetime(lifetimeArg),
		logicalTime(logicalTimeArg),
		filterThreshold(filterThresholdArg),
		secondDemotionEviction(secondDemotionEvictionArg),
		aging(agingArg),
		useHistory(useHistoryArg),
		usePendingList(usePendingListArg),
		enableRollback(enableRollbackArg),
		promotionFilter(promotionFilterArg),
		demotionAttempts(demotionAttemptsArg),
		rollbackExpiration(rollbackExpirationArg){

	queues[DRAM].resize(numQueues);
	queues[PCM].resize(numQueues);
	thresholds.resize(numQueues);

	for (unsigned i = 0; i < numQueues - 1; i++) {
		thresholds[i] = static_cast<uint64>(pow(2.0, (int) i + 1));
	}
	thresholds[numQueues - 1] = numeric_limits<uint64>::max();

	pages = new PageMap[numPids];

	currentTime = 0;

	tries = demotionAttempts;

	myassert(thresholdQueue > 0);
}

PageType MultiQueueMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	int index = numPids == 1 ? 0 : pid;
	PageType ret = BaseMigrationPolicy::allocate(pid, addr, read, instr);
	if (ret == DRAM){
		uint64 exp = (logicalTime ? currentTime : engine->getTimestamp()) + lifetime;
		//uint64 count = thresholds[thresholdQueue-1];
		//auto ait = queues[DRAM][thresholdQueue].emplace(queues[DRAM][thresholdQueue].end(), AccessEntry(pid, addr, exp, count, false, false));
		uint64 count = 0;
		auto ait = queues[DRAM][0].emplace(queues[DRAM][0].end(), AccessEntry(pid, addr, exp, count, false, false, false, 0));
		bool ins = pages[index].emplace(addr, PageEntry(ret, 0, ait)).second;
		myassert(ins);
	} else if (ret == PCM){
		auto ait = history.emplace(history.end(), AccessEntry(pid, addr, 0, 0, false, false, false, 0));
		bool ins = pages[index].emplace(addr, PageEntry(ret, -2, ait)).second;
		myassert(ins);
	} else {
		myassert(false);
	}
	return ret;
}

bool MultiQueueMigrationPolicy::migrate(unsigned pid, addrint addr){
	debug("(%u, %lu)", pid, addr);
	if (dramPagesLeft > 0){
		int index = numPids == 1 ? 0 : pid;
		auto it = pages[index].find(addr);
		myassert(it != pages[index].end());
		myassert(it->second.type == PCM);
		myassert(it->second.accessIt->pid == pid);
		myassert(it->second.accessIt->addr == addr);
		myassert(!it->second.accessIt->migrating);

		if (!promotionFilter || (promotionFilter && it->second.queue >= thresholdQueue)){
			AccessEntry entry(*it->second.accessIt);
			if (it->second.queue == -2){
				history.erase(it->second.accessIt);
			} else if (it->second.queue == -1){
				victims.erase(it->second.accessIt);
			} else {
				queues[PCM][it->second.queue].erase(it->second.accessIt);
			}
			it->second.type = DRAM;
			entry.migrating = true;
			entry.startTime = engine->getTimestamp();
			it->second.queue = 0;
			for(unsigned i = 0; i < numQueues-1; i++){
				if(entry.count < thresholds[i]){
					it->second.queue = i;
					break;
				}
			}
			it->second.accessIt = queues[DRAM][it->second.queue].emplace(queues[DRAM][it->second.queue].end(), entry);
			dramPagesLeft--;

			//info(": migrating.");

			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

bool MultiQueueMigrationPolicy::complete(unsigned *pid, addrint *addr){
	debug("()");
	if (true){
		auto maxPit = progress.end();
		uint32 maxBlocks = 0;
		auto pit = progress.begin();
		for(; pit != progress.end(); ++pit){
			if (pit->blocksRead > maxBlocks){
				auto it = pages[pit->pid].find(pit->page);
				myassert(it != pages[pit->pid].end());
				//info(": it->second.accessIt->migrating: %u, it->second.type == DRAM: %u,  !it->second.accessIt->completing: %u", it->second.accessIt->migrating, it->second.type == DRAM, !it->second.accessIt->completing);
				if (it->second.accessIt->migrating && it->second.type == DRAM && !it->second.accessIt->completing){
					maxBlocks = pit->blocksRead;
					maxPit = pit;
				}
			}
		}
		if(maxPit != progress.end()){
			auto it = pages[maxPit->pid].find(maxPit->page);
			myassert(it != pages[maxPit->pid].end());
			it->second.accessIt->completing = true;
			*pid = maxPit->pid;
			*addr = maxPit->page;
			debug(": completing (1). pid: %u, addr: %lu", *pid, *addr);
			return true;
		} else {
			return false;
		}
	} else {
		//for (unsigned i = 0; i < numQueues - thresholdQueue; i++) {
		for (unsigned i = 0; i < numQueues; i++) {
			unsigned q = numQueues - 1 - i;
			//cout << "queue[PCM][" << q << "]: " << queues[PCM][q].size() << endl;
			if (!queues[DRAM][q].empty()){
				auto qit = queues[DRAM][q].begin();
				while (qit != queues[DRAM][q].end()){
					if (qit->migrating && !qit->completing){
						qit->completing = true;
						*pid = qit->pid;
						*addr = qit->addr;
						debug(": completing (2). pid: %u, addr: %lu", *pid, *addr);
						return true;
					} else {
						++qit;
					}
				}
			}
		}
		return false;
	}
}

bool MultiQueueMigrationPolicy::rollback(unsigned *pid, addrint *addr){
	debug("()");
	auto maxPit = progress.end();
	uint64 maxTime = rollbackExpiration;
	auto pit = progress.begin();
	for(; pit != progress.end(); ++pit){
		auto it = pages[pit->pid].find(pit->page);
		myassert(it != pages[pit->pid].end());
		uint64 time = engine->getTimestamp() - it->second.accessIt->startTime;
		if (time > maxTime){
			if (it->second.type == DRAM && !it->second.accessIt->completing){
				maxTime = time;
				maxPit = pit;
			}
		}
	}
	if(maxPit != progress.end()){
		*pid = maxPit->pid;
		*addr = maxPit->page;
		debug(": rolling back. pid: %u, addr: %lu", *pid, *addr);
		return true;
	} else {
		return false;
	}
}

bool MultiQueueMigrationPolicy::selectDemotionPage(unsigned *pid, addrint *addr){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	if (dramPagesLeft > maxFreeDramPages){
		return false;
	}

	if (tries == demotionAttempts){
		tries = 0;
	} else {
		tries++;
		return false;
	}

	auto vit = victims.begin();
	if (enableRollback){
		while(vit != victims.end() && vit->completing){
			++vit;
		}
	} else {
		while(vit != victims.end() && vit->migrating){
			++vit;
		}
	}
	if (vit != victims.end()){
		AccessEntry entry = *vit;
		PageMap::iterator pageIt = pages[entry.pid].find(entry.addr);
		myassert(pageIt != pages[entry.pid].end());
		myassert(pageIt->second.type == DRAM);
		myassert(pageIt->second.queue == -1);
		uint64 oldCount = pageIt->second.accessIt->count;
		uint64 exp = pageIt->second.accessIt->expirationTime;
		victims.erase(vit);
		*pid = entry.pid;
		*addr = entry.addr;
		pageIt->second.type = PCM;
		pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(entry.pid, entry.addr, exp, oldCount, false, true, false, timestamp));
		pageIt->second.queue = -2;
		dramPagesLeft++;
		debug(": demoting (1). pid: %u, addr: %lu", *pid, *addr);
		return true;
	} else {
		for (int i = 0; i < thresholdQueue; i++) {
			if (!queues[DRAM][i].empty()){
				auto qit = queues[DRAM][i].begin();
				if (enableRollback){
					while (qit != queues[DRAM][i].end() && qit->completing){
						++qit;
					}
				} else {
					while (qit != queues[DRAM][i].end() && qit->migrating){
						++qit;
					}
				}
				if (qit != queues[DRAM][i].end()){
					AccessEntry entry(*qit);
					queues[DRAM][i].erase(qit);
					*pid = entry.pid;
					*addr = entry.addr;
					PageMap::iterator pageIt = pages[*pid].find(*addr);
					pageIt->second.type = PCM;
					entry.migrating = true;
					entry.startTime = timestamp;
					pageIt->second.accessIt = queues[PCM][pageIt->second.queue].emplace(queues[PCM][pageIt->second.queue].end(), entry);
					dramPagesLeft++;
					debug(": demoting (2). pid: %u, addr: %lu", *pid, *addr);
					return true;
				}
			}
		}
		debug(": No page to demote");
		return false;
	}
}

void MultiQueueMigrationPolicy::done(unsigned pid, addrint addr){
	debug("(%u, %lu)", pid, addr);
	int index = numPids == 1 ? 0 : pid;
	auto it = pages[index].find(addr);
	myassert(it != pages[index].end());
	myassert(it->second.accessIt->migrating);
	it->second.accessIt->migrating = false;
	it->second.accessIt->completing = false;
}

void MultiQueueMigrationPolicy::monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progressArg){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	for(auto cit = counts.begin(); cit != counts.end(); ++cit){
		int index = numPids == 1 ? 0 : cit->pid;
		PageMap::iterator it = pages[index].find(cit->page);
		myassert(it != pages[index].end());
		uint64 count = cit->reads;

		currentTime++;
		uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
		if (it->second.queue == -2){
			//bring back from history list
			uint64 oldCount = it->second.accessIt->count + count;
			bool oldMigrating = it->second.accessIt->migrating;
			bool oldCompleting = it->second.accessIt->completing;
			uint64 oldStartTime = it->second.accessIt->startTime;
			if (useHistory){
				if (aging){
					uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
					uint64 periodsSinceExpiration = timeSinceExpiration/lifetime;
					if (periodsSinceExpiration >= 64){
						periodsSinceExpiration = 63;
					}
					oldCount /= static_cast<uint64>(pow(2.0l, static_cast<int>(periodsSinceExpiration)));
				}
			} else {
				oldCount = count;
			}
			history.erase(it->second.accessIt);
			it->second.queue = 0;
			for(unsigned i = 0; i < numQueues-1; i++){
				if(oldCount < thresholds[i]){
					it->second.queue = i;
					break;
				}
			}
			it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(cit->pid, cit->page, exp, oldCount, false, oldMigrating, oldCompleting, oldStartTime));

		} else if (it->second.queue == -1){
			//bring back from victim list
			uint64 oldCount = it->second.accessIt->count + count;
			bool oldMigrating = it->second.accessIt->migrating;
			bool oldCompleting = it->second.accessIt->completing;
			uint64 oldStartTime = it->second.accessIt->startTime;
			if (aging){
				uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
				uint64 periodsSinceExpiration = timeSinceExpiration/lifetime;
				if (periodsSinceExpiration >= 64){
					periodsSinceExpiration = 63;
				}
				oldCount /= static_cast<uint64>(pow(2.0l, static_cast<int>(periodsSinceExpiration)));
			}
			victims.erase(it->second.accessIt);
			it->second.queue = 0;
			for(unsigned i = 0; i < numQueues-1; i++){
				if(oldCount < thresholds[i]){
					it->second.queue = i;
					break;
				}
			}
			it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(cit->pid, cit->page, exp, oldCount, false, oldMigrating, oldCompleting, oldStartTime));
		} else if (it->second.queue >= 0){
			it->second.accessIt->count += count;
			uint64 oldCount = it->second.accessIt->count;
			bool oldMigrating = it->second.accessIt->migrating;
			bool oldCompleting = it->second.accessIt->completing;
			uint64 oldStartTime = it->second.accessIt->startTime;
			queues[it->second.type][it->second.queue].erase(it->second.accessIt);
			it->second.queue = 0;
			for(unsigned i = 0; i < numQueues-1; i++){
				if(oldCount < thresholds[i]){
					it->second.queue = i;
					if (usePendingList && it->second.queue >= thresholdQueue){
						pending.emplace_back(make_pair(index, it->first));
					}
					break;
				}
			}
			it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(cit->pid, cit->page, exp, oldCount, false, oldMigrating, oldCompleting, oldStartTime));
		} else {
			myassert(false);
		}

		for (unsigned i = 0; i < 2; i++){
			for (vector<AccessQueue>::iterator qit = queues[i].begin(); qit != queues[i].end(); ++qit){
				if (!qit->empty()){
					if((logicalTime && currentTime > qit->front().expirationTime) || (!logicalTime && timestamp > qit->front().expirationTime)){
						int frontPid = qit->front().pid;
						PageMap::iterator pageIt = pages[frontPid].find(qit->front().addr);
						myassert(pageIt != pages[frontPid].end());
						debug(": page: %lu, type: %u, queue: %d, completing: %u", pageIt->first, pageIt->second.type, pageIt->second.queue, pageIt->second.accessIt->completing);
						uint64 oldCount = pageIt->second.accessIt->count;
						bool oldMigrating = pageIt->second.accessIt->migrating;
						bool oldCompleting = pageIt->second.accessIt->completing;
						uint64 oldStartTime = pageIt->second.accessIt->startTime;
						uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
						if (aging){
							oldCount /= 2;
						}
						if (pageIt->second.queue == 0){
							queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
							if (pageIt->second.type == DRAM){
								pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating, oldCompleting, oldStartTime));
								pageIt->second.queue = -1;
							} else {
								pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating, oldCompleting, oldStartTime));
								pageIt->second.queue = -2;
							}
						} else if (pageIt->second.queue > 0){
							if (secondDemotionEviction && pageIt->second.accessIt->demoted){
								queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
								if (pageIt->second.type == DRAM){
									pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating, oldCompleting, oldStartTime));
									pageIt->second.queue = -1;
								} else {
									pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, false, oldMigrating, oldCompleting, oldStartTime));
									pageIt->second.queue = -2;
								}
							} else {
								queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
								pageIt->second.queue--;
								pageIt->second.accessIt = queues[i][pageIt->second.queue].emplace(queues[i][pageIt->second.queue].end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, true, oldMigrating, oldCompleting, oldStartTime));
							}
						} else {
							myassert(false);
						}
					}
				}
			}
		}
	}
	BaseMigrationPolicy::monitor(counts, progressArg);
}

NewPolicy::NewPolicy(
	const string& nameArg,
	Engine *engineArg,
	uint64 debugStartArg,
	uint64 dramPagesArg,
	AllocationPolicy allocPolicyArg,
	unsigned numPidsArg,
	double maxFreeDramArg,
	uint64 addedBlocksThresholdArg,
	uint64 rollbackThresholdArg,
	bool rollbackLogicalTimeArg,
	unsigned numAgingPeriodsArg,
	uint64 agingPeriodArg,
	bool agingLogicalTimeArg,
	bool rollbackAsDemotionArg) :
			BaseMigrationPolicy(nameArg, engineArg, debugStartArg, dramPagesArg, allocPolicyArg, numPidsArg, maxFreeDramArg),
			addedBlocksThreshold(addedBlocksThresholdArg),
			rollbackThreshold(rollbackThresholdArg),
			rollbackLogicalTime(rollbackLogicalTimeArg),
			numAgingPeriods(numAgingPeriodsArg),
			agingPeriod(agingPeriodArg),
			agingLogicalTime(agingLogicalTimeArg),
			rollbackAsDemotion(rollbackAsDemotionArg)

{

	finished = false;
	pages = new PageMap[numPids];

	currentPeriod = 0;

	threshold = 0;
	thresholdIt = countMap.end();

	currentTime = 0;
	currentTimeInPeriod = 0;

	if (!agingLogicalTime){
		engine->addEvent(agingPeriod, this, 0);
	}
}

PageType NewPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	//uint64 timestamp = engine->getTimestamp();
	//debug("(%u, %lu, %u, %u)", pid, addr, read, instr);
	int index = numPids == 1 ? 0 : pid;
	PageType ret = BaseMigrationPolicy::allocate(pid, addr, read, instr);
	AccessListType type;
	AccessList::iterator ait;
	bool inLru;
	LRUList::iterator lit;
	if (ret == DRAM){
		type = AccessListType::INDRAM;
		inLru = true;
		lit = lruList.emplace(lruList.end(), index, addr);
	} else {
		type = AccessListType::INPCM;
		inLru = false;
		lit = lruList.end();
	}
	ait = accessLists[type].emplace(accessLists[type].begin(), index, addr);
	auto cit = countMap.emplace_hint(countMap.cbegin(), 0, CountMapEntry(index, addr, false));
	if (countMap.size() == dramPages){
		cit->second.inSort = true;
		thresholdIt = countMap.begin();
	} else if (countMap.size() < dramPages){
		cit->second.inSort = true;
	}
	//debug("page: %lu, inSort: %u", addr, cit->second.inSort);
	bool ins = pages[index].emplace(addr, PageEntry(ret, numAgingPeriods, type, ait, inLru, lit, cit)).second;
	myassert(ins);
	return ret;
}

bool NewPolicy::migrate(unsigned pid, addrint addr){
	uint64 timestamp = engine->getTimestamp();
	debug("(%u, %lu)", pid, addr);
	if (dramPagesLeft > 0){
		int index = numPids == 1 ? 0 : pid;
		auto pit = pages[index].find(addr);
		myassert(pit != pages[index].end());
		myassert(pit->second.type == PCM);
		myassert(!pit->second.promoting);
		myassert(!pit->second.completing);
		myassert(!pit->second.rollingBack);
		myassert(!pit->second.demoting);
		myassert(pit->second.accessType == INPCM);

		pit->second.type = DRAM;
		pit->second.promoting = true;
		pit->second.startTime = rollbackLogicalTime ? currentTime : timestamp;
		pit->second.lastReadTime = rollbackLogicalTime ? currentTime : timestamp;

		if (rollbackAsDemotion){
			if (pit->second.accessIt->count <= threshold){
				pit->second.inLru = true;
				pit->second.lruIt = lruList.emplace(lruList.end(), index, addr);
			}
		}

		AccessEntry old = *pit->second.accessIt;
		accessLists[INPCM].erase(pit->second.accessIt);
		pit->second.accessType = PROMOTING;
		pit->second.accessIt = accessLists[PROMOTING].emplace(accessLists[PROMOTING].begin(), old);

		dramPagesLeft--;

		debug("(%u, %lu) returning true", pid, addr);

		return true;
	} else {
		return false;
	}
}

bool NewPolicy::complete(unsigned *pid, addrint *addr){
	debug("()");
	auto maxIt = max_element(accessLists[PROMOTING].begin(), accessLists[PROMOTING].end());
	if (maxIt != accessLists[PROMOTING].end() && maxIt->count > threshold + addedBlocksThreshold){

		auto pit = pages[maxIt->pid].find(maxIt->page);
		myassert(pit != pages[maxIt->pid].end());
		myassert(pit->second.promoting);
		myassert(!pit->second.completing);
		myassert(!pit->second.rollingBack);
		myassert(!pit->second.demoting);
		myassert(pit->second.type == DRAM);

		pit->second.completing = true;

		if (pit->second.inLru){
			lruList.erase(pit->second.lruIt);
			pit->second.inLru = false;
			pit->second.lruIt = lruList.end();
		}

		AccessEntry old = *pit->second.accessIt;
		accessLists[PROMOTING].erase(maxIt);
		pit->second.accessType = INDRAM;
		pit->second.accessIt = accessLists[INDRAM].emplace(accessLists[INDRAM].begin(), old);

		*pid = maxIt->pid;
		*addr = maxIt->page;

		debug("(): returning true; *pid: %u, *addr: %lu", *pid, *addr);
		return true;
	} else {
		return false;
	}
}

bool NewPolicy::rollback(unsigned *pid, addrint *addr){
	debug("()");
	auto minIt = min_element(rollbackList.begin(), rollbackList.end());
	if (minIt != rollbackList.end()){
		auto pit = pages[minIt->pid].find(minIt->page);
		myassert(pit != pages[minIt->pid].end());
		myassert(pit->second.promoting);
		myassert(!pit->second.completing);
		myassert(!pit->second.rollingBack);
		myassert(!pit->second.demoting);
		myassert(pit->second.type == DRAM);
		myassert(pit->second.accessType == PROMOTING);

		pit->second.type = PCM;
		pit->second.rollingBack = true;

		AccessEntry old = *pit->second.accessIt;
		accessLists[PROMOTING].erase(pit->second.accessIt);
		pit->second.accessType = DEMOTING;
		pit->second.accessIt = accessLists[DEMOTING].emplace(accessLists[DEMOTING].begin(), old);

		if (pit->second.inLru){
			lruList.erase(pit->second.lruIt);
			pit->second.inLru = false;
			pit->second.lruIt = lruList.end();
		}

		*pid = minIt->pid;
		*addr = minIt->page;

		rollbackList.erase(minIt);

		//info("(): returning true; *pid: %u, *addr: %lu", *pid, *addr);

		return true;
	} else {
		return false;
	}
}

void NewPolicy::done(unsigned pid, addrint addr){
	debug("(%u, %lu)", pid, addr);
	unsigned index = numPids == 1 ? 0 : pid;
	auto pit = pages[index].find(addr);
	myassert(pit != pages[index].end());
	myassert(pit->second.promoting ^ pit->second.demoting);
	//myassert((rollbackAsDemotion && (pit->second.promoting || pit->second.demoting))|| (!rollbackAsDemotion && (pit->second.promoting ^ pit->second.demoting)));

	auto rlit = find_if(rollbackList.begin(), rollbackList.end(), [index, addr](const RollbackEntry& entry){
		return entry.pid == index && entry.page == addr;
	});
	if (rlit != rollbackList.end()){
		rollbackList.erase(rlit);
	}

	if (pit->second.promoting && !pit->second.completing && !pit->second.rollingBack){
		myassert(pit->second.accessType == PROMOTING);
		AccessEntry old = *pit->second.accessIt;
		accessLists[PROMOTING].erase(pit->second.accessIt);
		pit->second.accessType = INDRAM;
		pit->second.accessIt = accessLists[INDRAM].emplace(accessLists[INDRAM].begin(), old);
	}

	if (pit->second.demoting || (pit->second.promoting && !pit->second.completing && pit->second.rollingBack)){
		myassert(pit->second.accessType == DEMOTING);
		accessLists[DEMOTING].erase(pit->second.accessIt);
		pit->second.accessType = INPCM;
		pit->second.accessIt = accessLists[INPCM].emplace(accessLists[INPCM].begin(), index, addr);

		if (countMap.size() < dramPages){
			CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, true);
			countMap.erase(pit->second.countIt);
			pit->second.countIt = countMap.emplace(0, entry);
		} else {
			if (pit->second.countIt->second.inSort){
				debug("\t: inSort");
				--thresholdIt;
				myassert(!thresholdIt->second.inSort);
				thresholdIt->second.inSort = true;
				CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, false);
				countMap.erase(pit->second.countIt);
				pit->second.countIt = countMap.emplace_hint(countMap.cbegin(), 0, entry);
			} else {
				debug("\t: !inSort");
				CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, false);
				countMap.erase(pit->second.countIt);
				auto hint = countMap.lower_bound(0);
				pit->second.countIt = countMap.emplace_hint(hint, 0, entry);
				//pit->second.countIt = countMap.emplace(0, entry);
			}

//			auto dis = distance(thresholdIt, countMap.end());
//			if (dis != static_cast<int64>(dramPages)){
//				info(": distance: %lu", dis);
//			}
//			myassert(dis == static_cast<int64>(dramPages));
//			for (auto it = countMap.begin(); it != thresholdIt; ++it){
//				myassert(!it->second.inSort);
//			}
//			for (auto it = thresholdIt; it != countMap.end(); ++it){
//				myassert(it->second.inSort);
//			}

		}
	}

	if (!rollbackAsDemotion){
		if (pit->second.type == DRAM){
			if (pit->second.accessIt->count <= threshold){
				pit->second.inLru = true;
				pit->second.lruIt = lruList.emplace(lruList.end(), index, addr);
			}
		}
	}

	pit->second.promoting = false;
	pit->second.completing = false;
	pit->second.rollingBack = false;
	pit->second.demoting = false;
}

bool NewPolicy::selectDemotionPage(unsigned *pid, addrint *addr){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	if (dramPagesLeft > maxFreeDramPages){
		return false;
	}
	if (lruList.empty()){
		//info(": lru list empty");
		return false;
	}

	LRUEntry first = lruList.front();
	lruList.pop_front();

	debug("\tlruList.front: %u, %lu", first.pid, first.page);

	auto pit = pages[first.pid].find(first.page);
	myassert(pit != pages[first.pid].end());
	myassert(pit->second.type == DRAM);
	myassert(rollbackAsDemotion || (!rollbackAsDemotion && !pit->second.promoting));
	myassert(!pit->second.demoting);
	if (!((pit->second.promoting && pit->second.accessType == PROMOTING) || (!pit->second.promoting && pit->second.accessType == INDRAM))){
		info(": promoting: %u, demoting %u, completing: %u, accessType %u", pit->second.promoting, pit->second.demoting, pit->second.completing, pit->second.accessType);
	}

	myassert((pit->second.promoting && pit->second.accessType == PROMOTING) || (!pit->second.promoting && pit->second.accessType == INDRAM));


	pit->second.type = PCM;
	pit->second.promoting = false;
	pit->second.demoting = true;
	pit->second.startTime = rollbackLogicalTime ? currentTime : timestamp;
	pit->second.lastReadTime = rollbackLogicalTime ? currentTime : timestamp;

	AccessEntry old = *pit->second.accessIt;
	accessLists[pit->second.accessType].erase(pit->second.accessIt);
	pit->second.accessType = DEMOTING;
	pit->second.accessIt = accessLists[DEMOTING].emplace(accessLists[DEMOTING].begin(), old);

	pit->second.inLru = false;
	pit->second.lruIt = lruList.end();

	*pid = first.pid;
	*addr = first.page;

	dramPagesLeft++;

	//info("(): returning true; *pid: %u, *addr: %lu", *pid, *addr);

	return true;
}

void NewPolicy::monitor(vector<CountEntry>& counts, const vector<ProgressEntry>& progress){
	uint64 timestamp = engine->getTimestamp();
	debug("()");
	//info(": dramPagesLeft: %lu", dramPagesLeft);
	//debug("\t: threshold: page: %lu, count: %lu", thresholdIt->page, thresholdIt->count);

	//uint64 oldThreshold = threshold;
	sort(counts.begin(), counts.end());
	for (auto cit = counts.begin(); cit != counts.end(); ++cit){
		int index = numPids == 1 ? 0 : cit->pid;
		auto pit = pages[index].find(cit->page);
		myassert(pit != pages[index].end());

		if (cit->reads > 0){
			currentTime += cit->reads;

			currentTimeInPeriod += cit->reads;
			if (agingLogicalTime){
				if (currentTimeInPeriod >= agingPeriod){
					currentTimeInPeriod = (currentTimeInPeriod % agingPeriod);
					calculateThreshold();
				}
			}
			pit->second.lastReadTime = rollbackLogicalTime ? currentTime : cit->lastReadTime;
			pit->second.periods[currentPeriod] += cit->reads;

			auto count = pit->second.countIt->first + cit->reads;
			//debug("\t: page: %lu, (%lu) + (%lu) = (%lu)", cit->page, pit->second.countIt->count, cit->reads, count);
			if (countMap.size() < dramPages){
				CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, true);
				countMap.erase(pit->second.countIt);
				pit->second.countIt = countMap.emplace(count, entry);
			} else {
				if (pit->second.countIt->second.inSort){
					//debug("\t: inSort");
					if (pit->second.countIt == thresholdIt){
						//debug("\t: countIt == thresholdIt");
						CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, true);
						if (thresholdIt == countMap.begin()){
							countMap.erase(pit->second.countIt);
							pit->second.countIt = countMap.emplace(count, entry);
							thresholdIt = countMap.begin();
						} else {
							--thresholdIt;
							countMap.erase(pit->second.countIt);
							pit->second.countIt = countMap.emplace(count, entry);
							++thresholdIt;
						}
					} else {
						//debug("\t: countIt != thresholdIt");
						CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, true);
						countMap.erase(pit->second.countIt);
						pit->second.countIt = countMap.emplace(count, entry);
					}
				} else {
					//debug("\t: !inSort");
					if (count <= thresholdIt->first){
						CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, false);
						countMap.erase(pit->second.countIt);
						auto hint = countMap.lower_bound(count);
						pit->second.countIt = countMap.emplace_hint(hint, count, entry);
					} else {
						CountMapEntry entry(pit->second.countIt->second.pid, pit->second.countIt->second.page, true);
						countMap.erase(pit->second.countIt);
						pit->second.countIt = countMap.emplace(count, entry);
						thresholdIt->second.inSort = false;
						++thresholdIt;
					}
				}
				threshold = thresholdIt->first;
			}

			pit->second.accessIt->count += cit->reads;
			if (pit->second.type == DRAM && ((rollbackAsDemotion && (!pit->second.promoting || (pit->second.promoting && !pit->second.completing))) || (!rollbackAsDemotion && !pit->second.promoting))){
				if (pit->second.accessIt->count <= threshold){
					if (pit->second.inLru){
						myassert(pit->second.lruIt != lruList.end());
						auto oldIt = pit->second.lruIt;
						pit->second.lruIt = lruList.emplace(lruList.end(), *oldIt);
						lruList.erase(oldIt);
					} else {
						pit->second.inLru = true;
						pit->second.lruIt = lruList.emplace(lruList.end(), index, cit->page);
					}
				} else {
					if (pit->second.inLru){
						myassert(pit->second.lruIt != lruList.end());
						lruList.erase(pit->second.lruIt);
						pit->second.inLru = false;
						pit->second.lruIt = lruList.end();
					}
				}
			}
		}
	}

//	auto dis = distance(thresholdIt, countMap.end());
//	if (dis != static_cast<int64>(dramPages)){
//		info(": distance: %lu", dis);
//	}
//	myassert(dis == static_cast<int64>(dramPages));
//	for (auto it = countMap.begin(); it != thresholdIt; ++it){
//		myassert(!it->second.inSort);
//	}
//	for (auto it = thresholdIt; it != countMap.end(); ++it){
//		myassert(it->second.inSort);
//	}

	if (!rollbackAsDemotion){
		rollbackList.clear();
		for (auto prit = progress.begin(); prit != progress.end(); ++prit){
			int index = numPids == 1 ? 0 : prit->pid;
			auto pit = pages[index].find(prit->page);
			myassert(pit != pages[index].end());
			myassert(pit->second.promoting ^ pit->second.demoting);
			//myassert((rollbackAsDemotion && (pit->second.promoting || pit->second.demoting))|| (!rollbackAsDemotion && (pit->second.promoting ^ pit->second.demoting)));

			if(pit->second.promoting && !pit->second.completing && !pit->second.rollingBack && !pit->second.demoting){
				uint64 migTime = (rollbackLogicalTime ? currentTime : timestamp) - pit->second.startTime;
				uint64 elapsedTime = pit->second.lastReadTime - pit->second.startTime;
				//debug("(): page: %lu, blocks: %u, migration time: %lu, elapsed time: %lu", prit->page, prit->blocksRead, migTime, elapsedTime);

				if (migTime >= rollbackThreshold && pit->second.accessIt->count <= threshold + addedBlocksThreshold){
					if (elapsedTime > 0){
						//cout << timestamp << ": page " << prit->page << " inserted in rollback list: migTime: " << migTime << ", elapsedTime: " << elapsedTime << endl;
						double rate = static_cast<double>(prit->blocksRead)/static_cast<double>(elapsedTime);
						rollbackList.emplace_back(index, prit->page, rate);
					}
				}
			}
		}
	}
}

void NewPolicy::calculateThreshold(){
	debug("()");
	auto s = countMap.size();
	countMap.clear();
	currentPeriod = (currentPeriod + 1) % numAgingPeriods;
	if (s <= dramPages){
		for (unsigned pid = 0; pid < numPids; ++pid){
			for (auto pit = pages[pid].begin(); pit != pages[pid].end(); ++pit){
				unsigned p = currentPeriod;
				uint64 sum = 0;
				do {
					//pit->second.periods[p] /= 2;
					sum += pit->second.periods[p];
					p = (p + 1) % numAgingPeriods;
				} while (p != currentPeriod);
				pit->second.periods[p] = 0;
				pit->second.accessIt->count = sum;
				pit->second.countIt = countMap.emplace(sum, CountMapEntry(pid, pit->first, true));
			}
		}
		threshold = 0;
	} else {
		for (unsigned pid = 0; pid < numPids; ++pid){
			for (auto pit = pages[pid].begin(); pit != pages[pid].end(); ++pit){
				unsigned p = currentPeriod;
				uint64 sum = 0;
				do {
					//pit->second.periods[p] /= 2;
					sum += pit->second.periods[p];
					p = (p + 1) % numAgingPeriods;
				} while (p != currentPeriod);
				pit->second.periods[p] = 0;
				pit->second.accessIt->count = sum;
				pit->second.countIt = countMap.emplace(sum, CountMapEntry(pid, pit->first, false));
			}
		}

		auto rcit = countMap.rbegin();
		for (uint64 i = 0; i < dramPages; i++){
			rcit->second.inSort = true;
			++rcit;
		}
		thresholdIt = rcit.base();
		threshold = thresholdIt->first;


//		auto dis = distance(thresholdIt, countMap.end());
//		if (dis != static_cast<int64>(dramPages)){
//			info(": distance: %lu", dis);
//		}
//		myassert(dis == static_cast<int64>(dramPages));
//		for (auto it = countMap.begin(); it != thresholdIt; ++it){
//			myassert(!it->second.inSort);
//		}
//		for (auto it = thresholdIt; it != countMap.end(); ++it){
//			myassert(it->second.inSort);
//		}

	}

	struct LRUPlusTime {
		LRUEntry entry;
		uint64 timestamp;
		LRUPlusTime(const LRUEntry& entryArg, uint64 timestampArg) : entry(entryArg), timestamp(timestampArg) {}
		bool operator<(const LRUPlusTime& rhs) const {
			return timestamp < rhs.timestamp;
		}
	};

	//priority_queue<LRUPlusTime, vector<LRUPlusTime>, greater<LRUPlusTime> > queue;
	vector<LRUPlusTime> vec;
	for (unsigned pid = 0; pid < numPids; ++pid){
		for (auto pit = pages[pid].begin(); pit != pages[pid].end(); ++pit){
			pit->second.inLru = false;
			pit->second.lruIt = lruList.end();
			if (pit->second.type == DRAM && ((rollbackAsDemotion && (!pit->second.promoting || (pit->second.promoting && !pit->second.completing))) || (!rollbackAsDemotion && !pit->second.promoting))){
				if (pit->second.accessIt->count <= threshold){
					vec.emplace_back(LRUEntry(pid, pit->first), pit->second.lastReadTime);
				}
			}
		}
	}
	sort(vec.begin(), vec.end());

	lruList.clear();
	for (auto vit = vec.begin(); vit != vec.end(); ++vit){
		auto pit = pages[vit->entry.pid].find(vit->entry.page);
		myassert(pit != pages[vit->entry.pid].end());
		pit->second.inLru = true;
		pit->second.lruIt = lruList.emplace(lruList.end(), vit->entry);
	}

	//info(": threshold: %lu, lruList.size(): %lu", threshold, lruList.size());
}

void NewPolicy::process(const Event * event){
	debug("()");
	if (!finished){
		calculateThreshold();
		engine->addEvent(agingPeriod, this, 0);
	}
}

void NewPolicy::finish(){
	finished = true;
}

ThresholdMigrationPolicy::ThresholdMigrationPolicy(
	const string& nameArg,
	Engine *engineArg,
	uint64 debugStartArg,
	uint64 dramPagesArg,
	AllocationPolicy allocPolicyArg,
	unsigned numPidsArg,
	double maxFreeDramArg,
	unsigned numIntervalsArg,
	uint64 intervalLengthArg) :
		BaseMigrationPolicy(nameArg, engineArg, debugStartArg, dramPagesArg, allocPolicyArg, numPidsArg, maxFreeDramArg),
		numIntervals(numIntervalsArg),
		intervalLength(intervalLengthArg),
		finished(false){

	pages.resize(numPids);
	pivot = 0;
	engine->addEvent(intervalLength+1, this, 0); //+1 to guarantee that this event happens after the call to updateCounts()

}

PageType ThresholdMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	int index = numPids == 1 ? 0 : pid;
	PageType ret = BaseMigrationPolicy::allocate(pid, addr, read, instr);
	auto ins = pages[index].emplace(addr, PageEntry(pid, ret, counts.size(), numIntervals));
	myassert(ins.second);
	//ins.first->second.intervalCounts.push_front(0);
	counts.emplace_back(&*ins.first, 0, engine->getTimestamp());
	return ret;
}

bool ThresholdMigrationPolicy::migrate(unsigned pid, addrint addr){
	if (dramPagesLeft > 0){
		int index = numPids == 1 ? 0 : pid;
		auto it = pages[index].find(addr);
		myassert(it != pages[index].end());
		myassert(it->second.type == PCM);
		myassert(!it->second.migrating);

		if (it->second.type == PCM && counts[it->second.countIndex].count > pivot){
			debug("(): promoted page %lu, count: %ld", addr, counts[it->second.countIndex].count);
			it->second.type = DRAM;
			it->second.migrating = true;
			promotions.emplace_back(index, addr);
			dramPagesLeft--;
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

bool ThresholdMigrationPolicy::complete(unsigned *pid, addrint *addr) {
	//look for page currently migrating with lowest count?
	//look for page currently migrating with highest number of blocks already migrated?
	int64 minCount = numeric_limits<int64>::max();
	uint64 minLast = numeric_limits<uint64>::max();
	auto minIt = promotions.end();
	for (auto it = promotions.begin(); it != promotions.end(); ++it){
		auto pit = pages[it->pid].find(it->addr);
		myassert(pit != pages[it->pid].end());
		int64 c = counts[pit->second.countIndex].count;
		uint64 l = counts[pit->second.countIndex].lastAccess;

		if (c < minCount || (c == minCount && l < minLast)){
			minCount = c;
			minLast = l;
			minIt = it;
		}
	}
	if (minIt != promotions.end()){
		auto pit = pages[minIt->pid].find(minIt->addr);
		myassert(pit != pages[minIt->pid].end());
		pit->second.completing = true;
		*pid = minIt->pid;
		*addr = minIt->addr;
		promotions.erase(minIt);

		return true;
	} else {
		return false;
	}

}

void ThresholdMigrationPolicy::done(unsigned pid, addrint addr){
	int index = numPids == 1 ? 0 : pid;
	auto it = pages[index].find(addr);
	myassert(it != pages[index].end());
	myassert(it->second.migrating);
	it->second.migrating = false;
	if (it->second.completing){
		it->second.completing = false;
	}
}

void ThresholdMigrationPolicy::monitor(vector<CountEntry>& monitors, const vector<ProgressEntry>& progress){
	uint64 timestamp = engine->getTimestamp();
	for(auto mit = monitors.begin(); mit != monitors.end(); ++mit){
		int index = numPids == 1 ? 0 : mit->pid;
		PageMap::iterator it = pages[index].find(mit->page);
		myassert(it != pages[index].end());


		it->second.intervalCounts[it->second.head] += mit->reads;
		counts[it->second.countIndex].count += mit->reads;
		counts[it->second.countIndex].lastAccess = timestamp;



	}
	BaseMigrationPolicy::monitor(monitors, progress);
}



bool ThresholdMigrationPolicy::selectDemotionPage(unsigned *pid, addrint *addr){
	if (dramPagesLeft > maxFreeDramPages){
		debug(": not demoted. reason: max free dram pages reached");
		return false;
	}
	if (counts.size() > dramPages){
		Count min(nullptr, numeric_limits<int64>::max(), numeric_limits<uint64>::max());
		for (auto cit = dramCounts.begin(); cit != dramCounts.end(); ++cit){
			if (cit->entry->second.type == DRAM){
				if (cit->count < min.count || (cit->count == min.count && cit->lastAccess < min.lastAccess)){
					min = *cit;
				}
			}
		}
		if (min.entry != nullptr){
			*pid = min.entry->second.pid;
			*addr = min.entry->first;
			min.entry->second.type = PCM;
			min.entry->second.migrating = true;
			dramPagesLeft++;
			debug(": demoted page %lu, count: %lu, last access: %lu", min.entry->first, min.count, min.lastAccess);
			return true;
		} else {
			//debug(": not demoted. reason: no min page");
			return false;
		}
	} else {
		debug(": not demoted. reason: free dram pages");
		return false;
	}
}

void ThresholdMigrationPolicy::process(const Event * event){
	if (!finished){
		for (auto it = pages.begin(); it != pages.end(); ++it){
			for (auto it2 = it->begin(); it2 != it->end(); ++it2){
				it2->second.head = (it2->second.head + 1) % numIntervals;
				int64 b = it2->second.intervalCounts[it2->second.head];
				counts[it2->second.countIndex].count -= b;
				myassert(counts[it2->second.countIndex].count >= 0);
				it2->second.intervalCounts[it2->second.head] = 0;
			}
		}
		updateCounts();
		engine->addEvent(intervalLength, this, 0);
	}
}

void ThresholdMigrationPolicy::finish(){
	finished = true;
}

void ThresholdMigrationPolicy::updateCounts(){
	dramCounts.clear();
	if (counts.size() > dramPages){
		nth_element(counts.begin(), counts.begin()+dramPages, counts.end(), greater<Count>());
		pivot = counts[dramPages].count;
		for (auto cit = counts.begin(); cit != counts.end(); ++cit){
			auto index = cit->entry->second.pid;
			auto pit = pages[index].find(cit->entry->first);
			myassert(pit != pages[index].end());
			pit->second.countIndex = cit - counts.begin();
			if (pit->second.type == DRAM && cit->count <= pivot){
				dramCounts.emplace_back(&*pit, cit->count, cit->lastAccess);
			}
		}
	} else {
		pivot = 0;
	}
	debug(": pivot: %ld, dramCounts.size(): %lu", pivot, dramCounts.size());
}


//Old Migration Policies:

OldBaseMigrationPolicy::OldBaseMigrationPolicy(
	const string& nameArg,
	Engine *engineArg,
	uint64 debugStartArg,
	uint64 dramPagesArg,
	AllocationPolicy allocPolicyArg,
	IAllocator *allocatorArg,
	unsigned numPidsArg) :
		name(nameArg),
		engine(engineArg),
		debugStart(debugStartArg),
		dramPages(dramPagesArg),
		allocPolicy(allocPolicyArg),
		allocator(allocatorArg),
		numPids(numPidsArg){

	myassert(numPids > 0);
	dramPagesLeft = dramPages;

	dramFull = false;
}

void OldBaseMigrationPolicy::setInstrCounter(Counter* counter){
	instrCounter = counter;
}

PageType OldBaseMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	PageType ret;
	if (allocPolicy == DRAM_FIRST) {
		if (dramFull){
			ret = PCM;
		} else {
			ret = DRAM;
		}
	} else if (allocPolicy == PCM_ONLY) {
		ret = PCM;
	} else if (allocPolicy == CUSTOM) {
		PageType hint =	allocator->hint(pid, addr, read, instr);
		if (hint == DRAM && dramPagesLeft > 0){
			ret = DRAM;
		} else {
			ret = PCM;
		}
	} else {
		myassert(false);
	}

	if (ret == DRAM){
		myassert(dramPagesLeft > 0);
		dramPagesLeft--;
		if (dramPagesLeft == 0){
			dramFull = true;
		}

	}

	return ret;
}

bool OldBaseMigrationPolicy::migrate(unsigned *pid, addrint *addr){
	if (allocPolicy == DRAM_FIRST) {
		if (dramFull){
			return selectPage(pid, addr);
		} else {
			return false;
		}
	} else if (allocPolicy == PCM_ONLY || allocPolicy == CUSTOM) {
		return selectPage(pid, addr);
	} else {
		myassert(false);
		return false;
	}
}

void OldBaseMigrationPolicy::changeNumDramPages(uint64 dramPagesNew){
	if (dramPagesNew > dramPages) {
		dramPagesLeft += (dramPagesNew - dramPages);

	} else if (dramPagesNew < dramPages) {
		myassert(dramPagesNew >= 0);
		dramPagesLeft -= (dramPages - dramPagesNew);
		dramPages = dramPagesNew;
		if (!dramFull){
			if (dramPagesLeft <= 0){
				dramFull = true;
			}
		}
	} else {

	}
}

OldNoMigrationPolicy::OldNoMigrationPolicy(
	const string& nameArg,
	Engine *engineArg,
	uint64 debugArg,
	uint64 dramPagesArg,
	AllocationPolicy allocPolicyArg,
	IAllocator *allocatorArg,
	unsigned numPidsArg) : OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg) {
}

OldOfflineMigrationPolicy::OldOfflineMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg,
		unsigned thisPidArg,
		const string& filenameArg,
		const string& metricTypeArg,
		const string& accessTypeArg,
		const string& weightTypeArg,
		uint64 intervalCountArg,
		uint64 metricThresholdArg) :
				OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg),
				thisPid(thisPidArg),
				intervalCount(intervalCountArg),
				metricThreshold(metricThresholdArg),
				previousInterval(0){

//	FILE *trace = fopen(filenameArg.c_str(), "r");
//	if (trace == 0){
//		error("Could not open file %s", filenameArg.c_str());
//	}

	if (metricTypeArg == "accessed"){
		metricType = ACCESSED;
	} else if (metricTypeArg == "access_count"){
		metricType = ACCESS_COUNT;
	} else if (metricTypeArg == "touch_count"){
		metricType = TOUCH_COUNT;
	} else {
		error("Invalid metric type: %s", metricTypeArg.c_str());
	}

	if (accessTypeArg == "reads"){
		accessType = READS;
	} else if (accessTypeArg == "writes"){
		accessType = WRITES;
	} else if (accessTypeArg == "accesses"){
		accessType = ACCESSES;
	} else {
		error("Invalid access type: %s", accessTypeArg.c_str());
	}

	if (weightTypeArg == "uniform"){
		for (uint64 i = 0; i < intervalCount; i++){
			weights.emplace_back(1);
		}
	} else if (weightTypeArg == "linear"){
		for (uint64 i = 0; i < intervalCount; i++){
			weights.emplace_back(intervalCount - i);
		}
	} else if (weightTypeArg == "exponential"){
		for (uint64 i = 0; i < intervalCount; i++){
			weights.emplace_back(static_cast<uint64>(pow(2.0, static_cast<double>(intervalCount - i - 1))));
		}
	} else {
		error("Invalid weight type: %s", weightTypeArg.c_str());
	}

	gzFile trace = gzopen(filenameArg.c_str(), "r");
	if (trace == 0){
		error("Could not open file %s", filenameArg.c_str());
	}

	period = 0;

	if (numPids != 1){
		error("Sharing offline policies is not yet implemented");
	}

	bool done = false;
	while (!done){
		uint64 icount;
		uint32 size;
		//size_t read = fread(&icount, sizeof(uint64), 1, trace);
		int read = gzread(trace, &icount, sizeof(uint64));
		if (read > 0){
			if (period == 0){
				period = icount;
			}
			//fread(&size, sizeof(uint32), 1, trace);
			gzread(trace, &size, sizeof(uint32));
			for (uint32 i = 0; i < size; i++){
				addrint page;
				uint32 reads;
				uint32 writes;
				uint8 readBlocks;
				uint8 writtenBlocks;
				uint8 accessedBlocks;
//				fread(&reads, sizeof(uint32), 1, trace);
//				fread(&writes, sizeof(uint32), 1, trace);
//				fread(&readBlocks, sizeof(uint8), 1, trace);
//				fread(&writtenBlocks, sizeof(uint8), 1, trace);
//				fread(&accessedBlocks, sizeof(uint8), 1, trace);
				//fread(&page, sizeof(addrint), 1, trace);
				gzread(trace, &page, sizeof(addrint));
				gzread(trace, &reads, sizeof(uint32));
				gzread(trace, &writes, sizeof(uint32));
				gzread(trace, &readBlocks, sizeof(uint8));
				gzread(trace, &writtenBlocks, sizeof(uint8));
				gzread(trace, &accessedBlocks, sizeof(uint8));

				uint64 readCount = 0, writeCount = 0, accessCount = 0;
				if (metricType == ACCESSED){
					readCount = reads == 0 ? 0 : 1;
					writeCount = writes == 0 ? 0 : 1;
					accessCount = readCount || writeCount;
				} else if (metricType == ACCESS_COUNT){
					readCount = reads;
					writeCount = writes;
					accessCount = readCount + writeCount;
				} else if (metricType == TOUCH_COUNT){
					readCount = readBlocks;
					writeCount = writtenBlocks;
					accessCount = accessedBlocks;
				} else {
					myassert(false);
				}

				uint64 count = 0;
				if (accessType == READS){
					count = readCount;
				} else if (accessType == WRITES){
					count = writeCount;
				} else if (accessType == ACCESSES){
					count = accessCount;
				} else{
					myassert(false);
				}

				PageMap::iterator pit = pages.emplace(page, PageEntry(INVALID)).first;
				//pit->second.counters.emplace_back(Entry(icount/period, count));
				pit->second.counters.emplace_back(icount/period, count);

			}
		} else {
			//if (feof(trace)){
			if (gzeof(trace)){
				done = true;
			} else{
				error("Error reading file %s", filenameArg.c_str());
			}
		}
	}

	//fclose(trace);
	gzclose(trace);

//	for (PageMap::iterator it = pages.begin(); it != pages.end(); ++it){
//		cout << it->first << endl;
//	}

	//debug = 0;

}

void OldOfflineMigrationPolicy::monitor(unsigned pid, addrint addr){

}

PageType OldOfflineMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	myassert(pid == thisPid);
	PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
	PageMap::iterator pit = pages.find(addr);
	if (pit == pages.end()){
		pit = pages.emplace(addr, PageEntry(INVALID)).first;
	}
	myassert(pit != pages.end());
	myassert(pit->second.type == INVALID);
	pit->second.cur = 0;
	pit->second.type = ret;

	while (pit->second.cur < pit->second.counters.size() && pit->second.counters[pit->second.cur].interval < previousInterval){
		++pit->second.cur;
	}
	uint64 sum = 0;
	uint64 i = pit->second.cur;
	uint64 lastInterval = previousInterval + intervalCount;
	while(i < pit->second.counters.size() && pit->second.counters[i].interval < lastInterval){
		uint64 windex = pit->second.counters[i].interval - previousInterval;
		sum += pit->second.counters[i].count * weights.at(windex);
		++i;
	}

	if (pit->second.type == DRAM){
		dramMetricMap.emplace(sum, pit->first);
	} else if (pit->second.type == PCM){
		pcmMetricMap.emplace(sum, pit->first);
	} else {
		myassert(false);
	}

	return ret;
}

bool OldOfflineMigrationPolicy::selectPage(unsigned *pid, addrint *addr){
	uint64 curInstr = instrCounter->getTotalValue();
	uint64 currentInterval = curInstr / period + 1;

	//cout << curInstr << " " << currentInterval << endl;

	if (previousInterval != currentInterval){
//		struct timeval start;
//		gettimeofday(&start, NULL);

		previousInterval = currentInterval;

		dramMetricMap.clear();
		pcmMetricMap.clear();

		uint64 lastInterval = currentInterval + intervalCount;

		//cout << "currentInterval: " << currentInterval << " lastInterval: " << lastInterval << endl;
		//if ()

		for (PageMap::iterator it = pages.begin(); it != pages.end(); ++it){
			if (it->second.type != INVALID){
				while (it->second.cur < it->second.counters.size() && it->second.counters[it->second.cur].interval < currentInterval){
					++it->second.cur;
				}
				uint64 sum = 0;
				uint64 i = it->second.cur;
				while(i < it->second.counters.size() && it->second.counters[i].interval < lastInterval){
					uint64 windex = it->second.counters[i].interval - currentInterval;
					sum += it->second.counters[i].count * weights.at(windex);
					++i;
				}

				if (it->second.type == DRAM){
					dramMetricMap.emplace(sum, it->first);
				} else if (it->second.type == PCM){
					pcmMetricMap.emplace(sum, it->first);
				} else {
					myassert(false);
				}
			}
		}

//		struct timeval end;
//		gettimeofday(&end, NULL);
//		long seconds  = end.tv_sec  - start.tv_sec;
//		long useconds = end.tv_usec - start.tv_usec;
//		long mtime = seconds * 1000000 + useconds;
//		cout << mtime << endl;

	}

	//Find page to migrate

	PcmMetricMap::iterator addrOfMaxPcmIt = pcmMetricMap.begin();
	if (addrOfMaxPcmIt == pcmMetricMap.end() || addrOfMaxPcmIt->first == 0){
		//warn("There are no PCM pages ranked. pcmMetricMap.size(): %lu", pcmMetricMap.size());
//		if (engine->getTimestamp() >= debug){
//			cout << engine->getTimestamp() << ": instruction: " << instrCounter->getTotalValue() << "No PCM pages ranked"<< endl;
//		}
		return false;
	}
	PageMap::iterator maxPcmIt = pages.find(addrOfMaxPcmIt->second);
	myassert(maxPcmIt != pages.end());

	if (dramPagesLeft <= 0){
		//migrate to PCM
		DramMetricMap::iterator addrOfMinDramIt = dramMetricMap.begin();
		if (addrOfMinDramIt == dramMetricMap.end()){
			//warn("There are no DRAM pages ranked");
			debug(": instruction: %lu. No DRAM pages ranked", instrCounter->getTotalValue());
			return false;
		}
		PageMap::iterator minDramIt = pages.find(addrOfMinDramIt->second);
		myassert(minDramIt != pages.end());

		if (addrOfMaxPcmIt->first > addrOfMinDramIt->first * metricThreshold){
			debug(": instruction: %lu; DRAM(%lu, %lu) to PCM(%lu, %lu)", instrCounter->getTotalValue(), addrOfMinDramIt->first, addrOfMinDramIt->second, addrOfMaxPcmIt->first, addrOfMaxPcmIt->second);
			pcmMetricMap.emplace(addrOfMinDramIt->first, addrOfMinDramIt->second);
			dramMetricMap.erase(addrOfMinDramIt);
			myassert(minDramIt->second.type == DRAM);
			*pid = thisPid;
			*addr = minDramIt->first;
			minDramIt->second.type = PCM;
			dramPagesLeft++;
			return true;
		} else {
			debug(": instruction: %lu. Below threshold", instrCounter->getTotalValue());
			return false;
		}
	} else {
		//migrate to DRAM
		debug(": instruction: %lu; PCM(%lu, %lu) to DRAM", instrCounter->getTotalValue(), addrOfMaxPcmIt->first, addrOfMaxPcmIt->second);
		dramMetricMap.emplace(addrOfMaxPcmIt->first, addrOfMaxPcmIt->second);
		pcmMetricMap.erase(addrOfMaxPcmIt);
		myassert(maxPcmIt->second.type == PCM);
		*pid = thisPid;
		*addr = maxPcmIt->first;
		maxPcmIt->second.type = DRAM;
		dramPagesLeft--;
		return true;
	}

}





OldMultiQueueMigrationPolicy::OldMultiQueueMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg,
		unsigned numQueuesArg,
		unsigned thresholdQueueArg,
		uint64 lifetimeArg,
		bool logicalTimeArg,
		uint64 filterThresholdArg,
		bool secondDemotionEvictionArg,
		bool agingArg,
		bool useHistoryArg,
		bool usePendingListArg) :
				OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg),
				numQueues(numQueuesArg),
				thresholdQueue(thresholdQueueArg),
				lifetime(lifetimeArg),
				logicalTime(logicalTimeArg),
				filterThreshold(filterThresholdArg),
				secondDemotionEviction(secondDemotionEvictionArg),
				aging(agingArg),
				useHistory(useHistoryArg),
				usePendingList(usePendingListArg){

	queues[DRAM].resize(numQueues);
	queues[PCM].resize(numQueues);
	thresholds.resize(numQueues);

	for (unsigned i = 0; i < numQueues - 1; i++) {
		thresholds[i] = static_cast<uint64>(pow(2.0, (int) i + 1));
	}
	thresholds[numQueues - 1] = numeric_limits<uint64>::max();

	pages = new PageMap[numPids];

	currentTime = 0;
}

void OldMultiQueueMigrationPolicy::monitor(unsigned pid, addrint addr) {
	int index = numPids == 1 ? 0 : pid;
	uint64 timestamp = engine->getTimestamp();
	PageMap::iterator it = pages[index].find(addr);
	myassert(it != pages[index].end());
	bool mon;
	if (it->second.firstMonitor){
		if (timestamp - it->second.lastMonitor >= filterThreshold){
			mon = true;
		} else {
			mon = false;
		}
	} else {
		mon = true;
		it->second.firstMonitor = true;
	}

	if (mon){
		currentTime++;
		it->second.lastMonitor = timestamp;
		uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
		if (it->second.queue == -2){
			//bring back from history list
			uint64 oldCount = it->second.accessIt->count;
			if (useHistory){
				if (aging){
					uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
					uint64 periodsSinceExpiration = timeSinceExpiration/lifetime;
					if (periodsSinceExpiration >= 64){
						periodsSinceExpiration = 63;
					}
					oldCount /= static_cast<uint64>(pow(2.0l, static_cast<int>(periodsSinceExpiration)));
				}
			} else {
				oldCount = 0;
			}
			history.erase(it->second.accessIt);
			it->second.queue = 0;
			for(unsigned i = 0; i < numQueues-1; i++){
				if(oldCount < thresholds[i]){
					it->second.queue = i;
					break;
				}
			}
			it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(pid, addr, exp, oldCount, false));

		} else if (it->second.queue == -1){
			//bring back from victim list
			uint64 oldCount = it->second.accessIt->count;
			if (aging){
				uint64 timeSinceExpiration = timestamp - it->second.accessIt->expirationTime;
				uint64 periodsSinceExpiration = timeSinceExpiration/lifetime;
				if (periodsSinceExpiration >= 64){
					periodsSinceExpiration = 63;
				}
				oldCount /= static_cast<uint64>(pow(2.0l, static_cast<int>(periodsSinceExpiration)));
			}
			victims.erase(it->second.accessIt);
			it->second.queue = 0;
			for(unsigned i = 0; i < numQueues-1; i++){
				if(oldCount < thresholds[i]){
					it->second.queue = i;
					break;
				}
			}
			it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(pid, addr, exp, oldCount, false));
		} else if (it->second.queue >= 0){
			it->second.accessIt->count++;
			uint64 oldCount = it->second.accessIt->count;
			queues[it->second.type][it->second.queue].erase(it->second.accessIt);
			if (oldCount >= thresholds[it->second.queue]){
				it->second.queue++;
				if (usePendingList && it->second.queue == thresholdQueue){
					pending.emplace_back(make_pair(index, it->first));
				}
			}
			it->second.accessIt = queues[it->second.type][it->second.queue].emplace(queues[it->second.type][it->second.queue].end(), AccessEntry(pid, addr, exp, oldCount, false));
		} else {
			myassert(false);
		}

		for (unsigned i = 0; i < 2; i++){
			for (vector<AccessQueue>::iterator qit = queues[i].begin(); qit != queues[i].end(); ++qit){
				if (!qit->empty()){
					if((logicalTime && currentTime > qit->front().expirationTime) || (!logicalTime && timestamp > qit->front().expirationTime)){
						int frontPid = qit->front().pid;
						PageMap::iterator pageIt = pages[frontPid].find(qit->front().addr);
						myassert(pageIt != pages[frontPid].end());
						uint64 oldCount = pageIt->second.accessIt->count;
						uint64 exp = (logicalTime ? currentTime : timestamp) + lifetime;
						if (aging){
							oldCount /= 2;
						}
						if (pageIt->second.queue == 0){
							queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
							if (pageIt->second.type == DRAM){
								pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
								pageIt->second.queue = -1;
							} else {
								pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
								pageIt->second.queue = -2;
							}
						} else if (pageIt->second.queue > 0){
							if (secondDemotionEviction && pageIt->second.accessIt->demoted){
								queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
								if (pageIt->second.type == DRAM){
									pageIt->second.accessIt = victims.emplace(victims.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
									pageIt->second.queue = -1;
								} else {
									pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(frontPid, pageIt->first, pageIt->second.type, exp, oldCount));
									pageIt->second.queue = -2;
								}
							} else {
								queues[i][pageIt->second.queue].erase(pageIt->second.accessIt);
								pageIt->second.queue--;
								pageIt->second.accessIt = queues[i][pageIt->second.queue].emplace(queues[i][pageIt->second.queue].end(), AccessEntry(frontPid, pageIt->first, exp, oldCount, true));
							}
						} else {
							myassert(false);
						}
					}
				}
			}
		}
		//printQueue(cout);
	}
}

PageType OldMultiQueueMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr) {
//	int index = numPids == 1 ? 0 : pid;
//	PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
//	bool ins = pages[index].emplace(addr, PageEntry(ret, history.emplace(history.end(), AccessEntry(pid, addr, ret, 0, 0)))).second;
//	myassert(ins);
//	return ret;
	int index = numPids == 1 ? 0 : pid;
	PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
	if (ret == DRAM){
		uint64 exp = (logicalTime ? currentTime : engine->getTimestamp()) + lifetime;
		uint64 count = thresholds[thresholdQueue-1];
		auto ait = queues[DRAM][thresholdQueue].emplace(queues[DRAM][thresholdQueue].end(), AccessEntry(pid, addr, exp, count, false));
		bool ins = pages[index].emplace(addr, PageEntry(ret, thresholdQueue, ait)).second;
		myassert(ins);
	} else if (ret == PCM){
		auto ait = history.emplace(history.end(), AccessEntry(pid, addr, 0, 0, false));
		bool ins = pages[index].emplace(addr, PageEntry(ret, -2, ait)).second;
		myassert(ins);
	} else {
		myassert(false);
	}
	return ret;
}

bool OldMultiQueueMigrationPolicy::selectPage(unsigned *pid, addrint *addr){
	if (dramPagesLeft <= 0){
		//migrate to PCM
		if (victims.empty()){
			for (int i = 0; i < thresholdQueue; i++) {
				//cout << "queue[DRAM][" << i << "]: " << queues[DRAM][i].size() << endl;
				if (!queues[DRAM][i].empty()){
					AccessEntry entry(queues[DRAM][i].front());
					queues[DRAM][i].pop_front();
					*pid = entry.pid;
					*addr = entry.addr;
					PageMap::iterator pageIt = pages[*pid].find(*addr);
					pageIt->second.type = PCM;
					pageIt->second.accessIt = queues[PCM][pageIt->second.queue].emplace(queues[PCM][pageIt->second.queue].end(), entry);

					dramPagesLeft++;
					return true;
				}
			}
			return false;
		} else {
			AccessEntry entry = victims.front();
			PageMap::iterator pageIt = pages[entry.pid].find(entry.addr);
			myassert(pageIt != pages[entry.pid].end());
			myassert(pageIt->second.type == DRAM);
			myassert(pageIt->second.queue == -1);
			uint64 oldCount = pageIt->second.accessIt->count;
			uint64 exp = pageIt->second.accessIt->expirationTime;
			victims.pop_front();
			*pid = entry.pid;
			*addr = entry.addr;
			pageIt->second.type = PCM;
			pageIt->second.accessIt = history.emplace(history.end(), AccessEntry(entry.pid, entry.addr, pageIt->second.type, exp, oldCount));
			pageIt->second.queue = -2;
			dramPagesLeft++;
			//printQueue(cout);
			return true;
		}
	} else {
		//printQueue(cout);
		if(usePendingList){
			list<PidAddrPair>::iterator listIt = pending.begin();
			while (listIt != pending.end()){
				PageMap::iterator it = pages[listIt->first].find(listIt->second);
				myassert(it != pages[listIt->first].end());
				if (it->second.type == DRAM || (it->second.type == PCM && it->second.queue < thresholdQueue)){
					listIt = pending.erase(listIt);
				} else {
					++listIt;
				}
			}

			if (pending.empty()){
				return false;
			} else {
				*pid = pending.front().first;
				*addr = pending.front().second;
				pending.pop_front();
				PageMap::iterator maxIt = pages[*pid].find(*addr);
				maxIt->second.type = DRAM;
				AccessQueue::iterator oldAccessIt = maxIt->second.accessIt;
				maxIt->second.accessIt = queues[DRAM][maxIt->second.queue].emplace(queues[DRAM][maxIt->second.queue].end(), AccessEntry(*pid, *addr, maxIt->second.accessIt->expirationTime, maxIt->second.accessIt->count, maxIt->second.accessIt->demoted));
				queues[PCM][maxIt->second.queue].erase(oldAccessIt);
				dramPagesLeft--;
				//printQueue(cout);
				return true;
			}
		} else {
			for (unsigned i = 0; i < numQueues - thresholdQueue; i++) {
				unsigned q = numQueues - 1 - i;
				//cout << "queue[PCM][" << q << "]: " << queues[PCM][q].size() << endl;
				if (!queues[PCM][q].empty()){
					AccessEntry entry(queues[PCM][q].front());
					queues[PCM][q].pop_front();
					*pid = entry.pid;
					*addr = entry.addr;
					PageMap::iterator pageIt = pages[entry.pid].find(entry.addr);
					pageIt->second.type = DRAM;
					pageIt->second.accessIt = queues[DRAM][pageIt->second.queue].emplace(queues[DRAM][pageIt->second.queue].end(), entry);

					dramPagesLeft--;
					return true;
				}
			}
			return false;
		}

	}

}

void OldMultiQueueMigrationPolicy::process(const Event * event){

}

void OldMultiQueueMigrationPolicy::printQueue(ostream& os){
//	os << "v: ";
//	for (AccessQueue::iterator it = victims.begin(); it != victims.end(); ++it){
//		os << it->count << " ";
//	}
//	os << endl;
	os << "v size: " << victims.size() << endl;

	for (unsigned i = 0; i < numQueues; i++){
		for (unsigned j = 0; j < 2; j++){
			if (j == DRAM){
				os << i << "DRAM: ";
			} else if (j == PCM){
				os << i << "PCM: ";
			}
			for (AccessQueue::iterator it = queues[j][i].begin(); it != queues[j][i].end(); ++it){
				os << it->count << " ";
			}
			os << endl;
		}
	}
}


OldFirstTouchMigrationPolicy::OldFirstTouchMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg) : OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg), lastPcmAccessValid(false){

	currentIt = queue.begin();
	pages = new PageMap[numPids];
}

PageType OldFirstTouchMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	int index = numPids == 1 ? 0 : pid;
	PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
	AccessQueue::iterator accessIt = queue.end();
	if (ret == DRAM){
		accessIt = queue.emplace(currentIt, AccessEntry(pid, addr));
		currentIt = accessIt;
		++currentIt;
		if (currentIt == queue.end()){
			currentIt = queue.begin();
		}
	}
	bool ins = pages[index].emplace(addr, PageEntry(ret, accessIt)).second;
	myassert(ins);
	return ret;
}

void OldFirstTouchMigrationPolicy::monitor(unsigned pid, addrint addr){
	int index = numPids == 1 ? 0 : pid;
	PageMap::iterator it = pages[index].find(addr);
	myassert(it != pages[index].end());
	it->second.accessIt->ref = true;
	if (it->second.type == PCM){
		lastPcmAccessValid = true;
		lastPcmAccessPid = pid;
		lastPcmAccessAddr = addr;
	}
}

bool OldFirstTouchMigrationPolicy::selectPage(unsigned *pid, addrint *addr){
	if (dramPagesLeft <= 0){
		//migrate to PCM
		while(currentIt->ref){
			currentIt->ref = false;
			++currentIt;
			if (currentIt == queue.end()){
				currentIt = queue.begin();
			}

		}
		*pid = currentIt->pid;
		*addr = currentIt->addr;
		PageMap::iterator it = pages[currentIt->pid].find(currentIt->addr);
		myassert(it != pages[currentIt->pid].end());
		myassert(it->second.accessIt == currentIt);
		it->second.type = PCM;
		it->second.accessIt = queue.end();
		currentIt = queue.erase(currentIt);
		if (currentIt == queue.end()){
			currentIt = queue.begin();
		}
		lastPcmAccessValid = false;
		dramPagesLeft++;
		return true;
	} else {
		//migrate to DRAM
		if (lastPcmAccessValid){
			*pid = lastPcmAccessPid;
			*addr = lastPcmAccessAddr;
			PageMap::iterator it = pages[lastPcmAccessPid].find(lastPcmAccessAddr);
			myassert(it != pages[lastPcmAccessPid].end());
			it->second.type = DRAM;
			it->second.accessIt = queue.emplace(currentIt, AccessEntry(lastPcmAccessPid, lastPcmAccessAddr));

			lastPcmAccessValid = false;
			dramPagesLeft--;
			return true;
		} else {
			return false;
		}
	}
}


OldDoubleClockMigrationPolicy::OldDoubleClockMigrationPolicy(
		const string& nameArg,
		Engine *engineArg,
		uint64 debugArg,
		uint64 dramPagesArg,
		AllocationPolicy allocPolicyArg,
		IAllocator *allocatorArg,
		unsigned numPidsArg) : OldBaseMigrationPolicy(nameArg, engineArg, debugArg, dramPagesArg, allocPolicyArg, allocatorArg, numPidsArg){

	currentDramIt = dramQueue.begin();
	pages = new PageMap[numPids];
}

PageType OldDoubleClockMigrationPolicy::allocate(unsigned pid, addrint addr, bool read, bool instr){
	int index = numPids == 1 ? 0 : pid;
	PageType ret = OldBaseMigrationPolicy::allocate(pid, addr, read, instr);
	AccessQueue::iterator accessIt;
	ListType list;
	if (ret == DRAM){
		accessIt = dramQueue.emplace(currentDramIt, AccessEntry(pid, addr));
		currentDramIt = accessIt;
		++currentDramIt;
		if (currentDramIt == dramQueue.end()){
			currentDramIt = dramQueue.begin();
		}
		list = DRAM_LIST;
	} else {
		accessIt = pcmInactiveQueue.emplace(pcmInactiveQueue.end(), AccessEntry(pid, addr));
		list = PCM_INACTIVE_LIST;
	}
	bool ins = pages[index].emplace(addr, PageEntry(list, accessIt)).second;
	myassert(ins);
	return ret;
}

void OldDoubleClockMigrationPolicy::monitor(unsigned pid, addrint addr){
	int index = numPids == 1 ? 0 : pid;
	PageMap::iterator it = pages[index].find(addr);
	myassert(it != pages[index].end());
	if (it->second.type == DRAM_LIST){
		it->second.accessIt->ref = true;
	} else if (it->second.type == PCM_ACTIVE_LIST){
		it->second.accessIt->ref = true;
	} else if (it->second.type == PCM_INACTIVE_LIST){
		pcmInactiveQueue.erase(it->second.accessIt);
		it->second.type = PCM_ACTIVE_LIST;
		it->second.accessIt = pcmActiveQueue.emplace(pcmActiveQueue.end(), AccessEntry(pid, addr, true));
	} else {
		myassert(false);
	}
}

bool OldDoubleClockMigrationPolicy::selectPage(unsigned *pid, addrint *addr){
	if (dramPagesLeft <= 0){
		//migrate to PCM (demotion)
		while(currentDramIt->ref){
			currentDramIt->ref = false;
			++currentDramIt;
			if (currentDramIt == dramQueue.end()){
				currentDramIt = dramQueue.begin();
			}

		}
		*pid = currentDramIt->pid;
		*addr = currentDramIt->addr;
		PageMap::iterator it = pages[currentDramIt->pid].find(currentDramIt->addr);
		myassert(it != pages[currentDramIt->pid].end());
		myassert(it->second.accessIt == currentDramIt);
		it->second.type = PCM_INACTIVE_LIST;
		it->second.accessIt = pcmInactiveQueue.emplace(pcmInactiveQueue.end(), AccessEntry(it->first, false));
		currentDramIt = dramQueue.erase(currentDramIt);
		if (currentDramIt == dramQueue.end()){
			currentDramIt = dramQueue.begin();
		}

		dramPagesLeft++;
		return true;
	} else {
		//migrate to DRAM (promotion)

		bool found = false;
		AccessQueue::iterator pcmIt = pcmActiveQueue.begin();
		while (pcmIt != pcmActiveQueue.end()){
			if (pcmIt->ref){
				if (!found){
					found = true;
					*pid = pcmIt->pid;
					*addr = pcmIt->addr;
					pcmIt = pcmActiveQueue.erase(pcmIt);
				} else {
					pcmIt->ref = false;
					pcmIt++;
				}
			} else {
				PageMap::iterator it = pages[pcmIt->pid].find(pcmIt->addr);
				myassert(it != pages[pcmIt->pid].end());
				it->second.type = PCM_INACTIVE_LIST;
				it->second.accessIt = pcmInactiveQueue.emplace(pcmInactiveQueue.end(), AccessEntry(pcmIt->pid, pcmIt->addr));
				pcmIt = pcmActiveQueue.erase(pcmIt);
			}
		}

		if (found){
			PageMap::iterator it = pages[*pid].find(*addr);
			myassert(it != pages[*pid].end());
			it->second.type = DRAM_LIST;
			it->second.accessIt = dramQueue.emplace(currentDramIt, AccessEntry(*pid, *addr));
			dramPagesLeft--;
			return true;
		} else {
			return false;
		}
	}
}


istream& operator>>(istream& lhs, AllocationPolicy& rhs){
	string s;
	lhs >> s;
	if (s == "dram_first"){
		rhs = DRAM_FIRST;
	} else if (s == "pcm_only"){
		rhs = PCM_ONLY;
	} else {
		error("Invalid monitoring strategy: %s", s.c_str());
	}
	return lhs;
}

ostream& operator<<(ostream& lhs, AllocationPolicy rhs){
	if(rhs == DRAM_FIRST){
		lhs << "dram_first";
	} else if(rhs == PCM_ONLY){
		lhs << "pcm_only";
	} else {
		error("Invalid monitoring strategy");
	}
	return lhs;
}
