#include "http.h"

#include "../platform/platform.h"
#include "../platform/threads.h"

#include <deque>

namespace http {
namespace {

const size_t WORKER_STACK = 1024 * 1024; // libSceHttp needs a large stack

struct Job {
	Request request;
	Callback on_done;
	Callback on_worker;
	Response response;
};

threads::Mutex g_mutex;
threads::CondVar g_cond;
std::deque<std::shared_ptr<Job>> g_queue;
std::deque<std::shared_ptr<Job>> g_done;
bool g_started = false;

void *worker(void *) {
	for (;;) {
		std::shared_ptr<Job> job;
		{
			threads::Lock lock(g_mutex);
			while (g_queue.empty()) {
				g_cond.wait(g_mutex);
			}
			job = g_queue.front();
			g_queue.pop_front();
		}
		job->response = perform(job->request);
		if (job->on_worker) {
			job->on_worker(job->response);
		}
		threads::Lock lock(g_mutex);
		g_done.push_back(job);
	}
	return nullptr;
}

} // namespace

bool init(int workers) {
	if (g_started) {
		return true;
	}
	if (!backend_init()) {
		platform::log("http: network init failed");
		return false;
	}
	for (int i = 0; i < workers; i++) {
		if (!threads::start(worker, nullptr, WORKER_STACK, "stremio_http")) {
			platform::log("http: failed to start worker %d", i);
			return false;
		}
	}
	g_started = true;
	return true;
}

void send(const Request &request, Callback on_done, Callback on_worker) {
	auto job = std::make_shared<Job>();
	job->request = request;
	job->on_done = std::move(on_done);
	job->on_worker = std::move(on_worker);
	threads::Lock lock(g_mutex);
	g_queue.push_back(job);
	g_cond.notify_one();
}

void post_json(const std::string &url, const std::string &json_body, Callback on_done) {
	Request r;
	r.method = "POST";
	r.url = url;
	r.headers.push_back("Content-Type: application/json");
	r.body = json_body;
	send(r, std::move(on_done));
}

void get(const std::string &url, Callback on_done, Callback on_worker) {
	Request r;
	r.url = url;
	send(r, std::move(on_done), std::move(on_worker));
}

void poll() {
	std::deque<std::shared_ptr<Job>> done;
	{
		threads::Lock lock(g_mutex);
		done.swap(g_done);
	}
	for (auto &job : done) {
		if (job->on_done) {
			job->on_done(job->response);
		}
	}
}

} // namespace http
