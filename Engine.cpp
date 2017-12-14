/*
 * Copyright (c) 2015 Santiago Bock
 *
 * See the file LICENSE.txt for copying permission.
 */

#include "Engine.H"

#include <cassert>

Engine::Engine(StatContainer *statsArg, uint64 statsPeriodArg, const string& statsFilename, uint64 progressPeriodArg) :
		stats(statsArg),
		statsPeriod(statsPeriodArg),
		progressPeriod(progressPeriodArg),
		currentInterval(0),
		statsNextEvent(statsPeriodArg),
		progressNextEvent(progressPeriodArg),
		done(false),
		curIndex(0),
		timestamp(0),
		lastTimestamp(0),
		numEvents(0),
		lastNumEvents(0),
		finalTimestamp(stats, "final_timestamp", "Final timestamp", this, &Engine::getFinalTimestamp),
		totalEvents(stats, "total_events", "Total number of events", 0),
		executionTime(stats, "execution_time", "Execution time in seconds", 0),
		eventRate(stats, "event_rate", "Event rate in events per second", &totalEvents, &executionTime)

{

	if (statsFilename == ""){
		statsPeriod = statsNextEvent = 0;
	}

	if (statsNextEvent == 0){
		if (progressNextEvent == 0){

		} else {
			addEvent(progressNextEvent - timestamp, this);
		}
	} else {
		statsOut.open(statsFilename.c_str());
		if (!statsOut.is_open()){
			error("Could not open statistics file '%s'", statsFilename.c_str());
		}
		statsOut.setf(std::ios::fixed);
		statsOut.precision(2);
		if (progressNextEvent == 0){
			addEvent(statsNextEvent -timestamp, this);
		} else {
			if (statsNextEvent < progressNextEvent){
				addEvent(statsNextEvent -timestamp, this);
			} else {
				addEvent(progressNextEvent - timestamp, this);
			}
		}
	}

	gettimeofday(&start, NULL);
	last = start;
}

void Engine::run(){
	if (statsNextEvent != 0){
		stats->printNames(statsOut);
		statsOut << endl;
	}
	bool empty = currentEventsEmpty();
	while (!done && !(empty && events.empty()) ){
		if (curIndex == currentEvents[timestamp % currentSize].size()){
			curIndex = 0;
			currentEvents[timestamp % currentSize].clear();
			auto b = events.begin();
			bool found = false;
			if (!empty){
				uint64 originalTimestamp = timestamp;
				for (unsigned i = 1; i < currentSize; i++){
					if (b != events.end() && b->first == originalTimestamp + i){
						timestamp = originalTimestamp + i;
						found = true;
						for (auto eit = b->second.begin(); eit != b->second.end(); ++eit){
							numEvents++;
							eit->execute();
						}
						b = events.erase(b);
					}

					if (!currentEvents[(originalTimestamp + i) % currentSize].empty()){
						timestamp = originalTimestamp + i;
						found = true;
						break;
					}
				}
			}
			if (!found){
				timestamp = b->first;
				for (auto eit = b->second.begin(); eit != b->second.end(); ++eit){
					numEvents++;
					eit->execute();
				}
				b = events.erase(b);
			}
		} else {
			Event& event = currentEvents[timestamp % currentSize][curIndex];
			curIndex++;

			numEvents++;
			event.execute();
		}

		empty = currentEventsEmpty();
	}
	updateStats();
	if (statsNextEvent != 0){
		statsOut.close();
	}
}

void Engine::quit(){
	done = true;
}

void Engine::addEvent(uint64 delay, IEventHandler *handler, uint64 data){
	if (delay < currentSize){
		currentEvents[(timestamp + delay) % currentSize].emplace_back(handler, data);
	} else {
		auto res = events.emplace(timestamp+delay, vector<Event>(1, Event(handler, data)));
		if (!res.second){
			res.first->second.emplace_back(handler, data);
		}
	}
}

bool Engine::currentEventsEmpty(){
	if (curIndex < currentEvents[(timestamp) % currentSize].size()){
		return false;
	}
	for (unsigned i = 1; i < currentSize; i++){
		if (!currentEvents[(timestamp + i) % currentSize].empty()){
			return false;
		}
	}
	return true;
}

void Engine::process(const Event * event){
	if (timestamp == statsNextEvent){
		updateStats();
		statsNextEvent += statsPeriod;
		stats->printInterval(statsOut);
		statsOut << endl;
		currentInterval++;
		stats->startInterval();
	}
	if (timestamp == progressNextEvent){
		progressNextEvent += progressPeriod;
		uint64 eventsInPeriod = numEvents - lastNumEvents;
		struct timeval current;
		gettimeofday(&current, NULL);
		long seconds  = current.tv_sec  - last.tv_sec;
		long useconds = current.tv_usec - last.tv_usec;
		long mtime = seconds * 1000000 + useconds;
		double eventsPerSecond = 1000000 * (static_cast<double>(eventsInPeriod) / static_cast<double>(mtime));
		cout << "Between timestamps " << lastTimestamp << " and " << timestamp << " executed " << eventsInPeriod << " events (" << eventsPerSecond << " events per second)" << endl;
		lastTimestamp = timestamp;
		lastNumEvents = numEvents;
		last = current;
	}
	if (!currentEventsEmpty() || !events.empty()){
		if (statsNextEvent == 0){
			if (progressNextEvent != 0){
				addEvent(progressNextEvent - timestamp, this);
			}
		} else {
			if (progressNextEvent == 0){
				addEvent(statsNextEvent -timestamp, this);
			} else {
				if (statsNextEvent < progressNextEvent){
					addEvent(statsNextEvent -timestamp, this);
				} else {
					addEvent(progressNextEvent - timestamp, this);
				}
			}
		}
	}
}

void Engine::updateStats(){
	gettimeofday(&end, NULL);
	totalEvents = numEvents;
	long seconds  = end.tv_sec  - start.tv_sec;
	long useconds = end.tv_usec - start.tv_usec;
	executionTime = static_cast<double>(seconds * 1000000 + useconds)/1000000;

}
