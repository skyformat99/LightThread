#include <thread>
#include <mutex>
#include <queue>
#include <set>
#include <condition_variable>
static std::mutex mtx;


static std::queue<void*> threads;


class Thread {
public:
	std::thread thread;
	std::mutex mtx;
	std::condition_variable evt;
	std::function<void()> work;
	bool hasWork;
	Thread() {
		hasWork = false;
		thread = std::thread([=](){
			bool first = true;
			while(true) {
				{
					if(!first) {
					//Add ourselves to the availability list
					std::unique_lock<std::mutex> ml(::mtx);
					threads.push(this);
					}
				}
			if(!hasWork) {
				std::unique_lock<std::mutex> l(mtx);
			evt.wait(l);
			}
			//It is the caller's responsibility to remove us from the list of available threads
			first = false;
			work();
			hasWork = false;
			//Free the lambda by swapping it out
			work = std::function<void()>();
			}
		});
	}
	~Thread() {

	}
};
static void SubmitWork(const std::function<void()>& item) {
	std::unique_lock<std::mutex> l(mtx);
	//We don't like to be kept waiting! Add a new thread immediately!
	if(threads.empty()) {
		auto bot = new Thread();
		bot->work = item;
		bot->hasWork = true;
		bot->evt.notify_one();
	}else {
		auto bot = ((Thread*)threads.front());
		threads.pop();
		bot->work = item;
		bot->hasWork = true;
		bot->evt.notify_one();

	}

}


class TimerEvent {
public:
	std::function<void()> functor;
	size_t timeout;
	bool* cancellationToken;
	TimerEvent* next;
	bool operator<(const TimerEvent& other) const {
		return other.timeout>timeout;
	}
	TimerEvent() {
		next = 0;
	}
};

class TimerPool {
public:
	std::thread thread;
	std::set<TimerEvent> events;
	std::mutex mtx;
	std::condition_variable c;

	TimerPool() {
		thread = std::thread([=](){
			while(true) {
				{
					{
					std::unique_lock<std::mutex> l(mtx);
					while(!events.empty()) {
						std::vector<TimerEvent> currentEvents;
						TimerEvent cevt = *events.begin();
						size_t ctimeout = cevt.timeout;
						while(true) {
							currentEvents.push_back(cevt);
							if(cevt.next == 0) {
								break;
							}
							TimerEvent* ptr = cevt.next;
							cevt = *ptr;
							delete ptr;
						}
						events.erase(events.begin());
						l.unlock();
						auto start = std::chrono::steady_clock::now();

						std::mutex mx;
						std::unique_lock<std::mutex> ml(mx);
						if(c.wait_for(ml,std::chrono::milliseconds(ctimeout)) == std::cv_status::timeout) {


						for(auto i = currentEvents.begin(); i != currentEvents.end();i++) {
							if(*(i->cancellationToken)) {
							SubmitWork(i->functor);
							}
							delete i->cancellationToken;
						}
						}else {
							auto end = std::chrono::steady_clock::now();
							auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
							for(auto i = currentEvents.begin();i!= currentEvents.end();i++) {
								i->timeout-=milliseconds;
								events.insert(*i);
							}
							printf("Interrupt\n");
						}
						l.lock();
					}

				}
				}
				std::mutex mx;
				std::unique_lock<std::mutex> ml(mx);
				c.wait(ml);

			}
		});
	}
};
static TimerPool timerPool;
static bool* CreateTimer(const std::function<void()>& callback, size_t timeout) {

	TimerEvent evt;
	evt.functor = callback;
	evt.timeout = timeout;
	evt.cancellationToken = new bool(true);
	{
	std::lock_guard<std::mutex> mg(timerPool.mtx);
	if(timerPool.events.find(evt) == timerPool.events.end()) {
	timerPool.events.insert(evt);
	}else {
		TimerEvent found = *timerPool.events.find(evt);
		evt.next = found.next;
		found.next = new TimerEvent(evt);
		timerPool.events.erase(found);
		timerPool.events.insert(found);
	}
	timerPool.c.notify_one();
	}
	return evt.cancellationToken;
}
static void CancelTimer(bool* cancellationToken) {
	*cancellationToken = false;
	timerPool.c.notify_one();
}
