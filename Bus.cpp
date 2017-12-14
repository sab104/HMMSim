/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Bus.H"


Bus::Bus(const string& nameArg,
		const string& descArg,
		Engine *engineArg,
		StatContainer *statCont,
		uint64 debugStartArg,
		uint64 latencyArg) :
		name(nameArg),
				desc(descArg),
				engine(engineArg),
				debugStart(debugStartArg),

				latency(latencyArg) {

	gaps.emplace(numeric_limits<uint64>::max(), 0);
	//debugStart = 0;
	//debugStart = 231894;
}

Bus::~Bus(){
	clearGaps();
	myassert(queue.empty());
	myassert(gaps.size() == 1);
}

uint64 Bus::schedule(uint64 delay, IBusCallback *caller){
	uint64 timestamp = engine->getTimestamp();
	debug("(%lu, %s)", delay, caller->getName());

//	debug(": gap size before: %lu", gaps.size());
//	for (auto git = gaps.begin(); git != gaps.end(); ++git){
//		debug(":\tgap before : %lu, %lu", git->second, git->first);
//	}

	clearGaps();

//	debug(": gap size after fix: %lu", gaps.size());
//	for (auto git = gaps.begin(); git != gaps.end(); ++git){
//		debug(":\tgap after fix : %lu, %lu", git->second, git->first);
//	}
//
//	debug(": queue size before: %lu", queue.size());
//	for (auto qit = queue.begin(); qit != queue.end(); ++qit){
//		debug(":\tqueue before: %lu, %lu", qit->first, qit->first+latency);
//	}

	uint64 start = delay + timestamp;
	auto git = gaps.lower_bound(start+latency);
	start = max(start, git->second);
	uint64 end = start+latency;
	uint64 newActualDelay = start - timestamp;
	myassert(git != gaps.end());
	myassert(start >= git->second);
	if (start - git->second >= latency){
		gaps.emplace(start, git->second);
	}
	myassert(git->first >= end);
	if (git->first - end < latency){
		gaps.erase(git);
	} else {
		git->second = end;
	}

	bool ins = queue.emplace(newActualDelay+timestamp, caller).second;
	myassert(ins);

	debug(": scheduled %lu, %lu", newActualDelay+timestamp, newActualDelay+latency+timestamp);
	engine->addEvent(newActualDelay+latency, this);

//	debug(": gap size after: %lu", gaps.size());
//	for (auto git = gaps.begin(); git != gaps.end(); ++git){
//		debug(":\tgap after : %lu, %lu", git->second, git->first);
//	}
//
//	debug(": queue size after: %lu", queue.size());
//	for (auto qit = queue.begin(); qit != queue.end(); ++qit){
//		debug(":\tqueue after: %lu, %lu", qit->first, qit->first+latency);
//	}

	return newActualDelay;

//	uint64 oldActualDelay;
//	debug(": start: %lu", start);
//	auto it = queue.begin();
//	if (it == queue.end()){
//		debug(": queue == end()");
//		queue.emplace(start, caller);
//		oldActualDelay = start - timestamp;
//	} else {
//		uint64 prevEnd = timestamp;
//		debug(":\tprevEnd: %lu", prevEnd);
//		while (it != queue.end() && (prevEnd < start || it->first - latency < prevEnd)){
//			prevEnd = it->first + latency;
//			debug(":\tprevEnd: %lu", prevEnd);
//			++it;
//		}
//		if (prevEnd < start){
//			debug(": prevEnd < start");
//			bool ins = queue.emplace(start, caller).second;
//			myassert(ins);
//			oldActualDelay = start - timestamp;
//		} else {
//			bool ins = queue.emplace(prevEnd, caller).second;
//			myassert(ins);
//			oldActualDelay = prevEnd - timestamp;
//		}
//	}
//	debug(": \tscheduled bus at : %lu (callback at %lu)", oldActualDelay+timestamp, oldActualDelay+latency+timestamp);
//	debug(": scheduled %lu, %lu", oldActualDelay+timestamp, oldActualDelay+latency+timestamp);
//	engine->addEvent(oldActualDelay+latency, this);
//
//	debug(": start: %lu, end: %lu, actualDelay: %lu, newActualDelay: %lu", start, end, oldActualDelay, newActualDelay);
//	myassert(oldActualDelay == newActualDelay);

}


void Bus::process(const Event *event){
	uint64 timestamp = engine->getTimestamp();
	auto it = queue.begin();
	myassert(it != queue.end());
	myassert(it->first == timestamp - latency);
	debug("(): removed: %lu, %lu", it->first, it->first+latency);
	IBusCallback *caller = it->second;
	queue.erase(it);
	caller->transferCompleted();
}

void Bus::clearGaps(){
	uint64 timestamp = engine->getTimestamp();
	auto git = gaps.lower_bound(timestamp+latency);
	myassert(git != gaps.end());
	git = gaps.erase(gaps.begin(), git);
	myassert(git != gaps.end());

	if (git->second < timestamp){
		git->second = timestamp;
	}
}


