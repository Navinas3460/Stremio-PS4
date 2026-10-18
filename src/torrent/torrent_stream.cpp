// The torrent engine behind TorrentStream. Threads:
//   engine     one poll() loop: peer connections, DHT, metadata, piece picking
//   announce   one short-lived thread per tracker and round (they block)
//   reader     the player's I/O thread, waiting in read() for pieces
// Shared state (marked "guarded") sits behind mutex_; everything else belongs
// to the engine thread. The threads hold the Engine through shared_ptrs, so a
// slow tracker can outlive the TorrentStream safely.
#include "torrent_stream.h"

#include "bencode.h"
#include "dht.h"
#include "magnet.h"
#include "net.h"
#include "sha1.h"
#include "trackers.h"

#include "../platform/platform.h"
#include "../platform/threads.h"

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace torrent {

namespace {

const uint32_t BLOCK = 16384;
const size_t MAX_PEERS = 50;     // with a completed handshake (the PS4 allows 256 fds)
// Connecting or waiting for the handshake. Most swarm addresses never answer,
// and NATs/VPNs may accept the TCP connection for a peer that's gone, so many
// attempts run at once and a missing handshake counts as a failed connect.
const size_t MAX_HALF_OPEN = 80;
// Downloaded ahead of the read position: ~90 s of the video at the rate the
// player reads it, within these bounds (40 MiB is ~1 min of 1080p at 5 Mbit/s;
// a 25 Mbit/s remux needs the upper end).
const uint64_t READAHEAD_MIN = 40ull << 20, READAHEAD_MAX = 112ull << 20;
const double READAHEAD_SECONDS = 90;
const uint64_t KEEP_BEHIND = 8ull << 20; // kept behind it (small rewinds)
const uint64_t CACHE_BYTES = 160ull << 20;
const uint64_t ACTIVE_BYTES = 48ull << 20; // pieces being assembled
const double CONNECT_TIMEOUT = 5, HANDSHAKE_TIMEOUT = 3;
const double REQUEST_TIMEOUT = 10;     // a block not delivered by then is asked of someone else
const double DUPLICATE_AFTER = 2;      // urgent blocks pending this long are also asked of another peer
const double METADATA_TIMEOUT = 10;    // per peer
const double OPEN_TIMEOUT = 90;        // no torrent info by then: give up
const double STALL_TIMEOUT = 20;       // unchoked, requests out, nothing received
const double REANNOUNCE = 120;         // tracker round interval while peers are scarce
const double DHT_RESEARCH = 60;
// Starved (no untried addresses left and few peers sending): ask again sooner.
const double REANNOUNCE_STARVED = 45, DHT_RESEARCH_STARVED = 15;
// An address is tried again after this long (in a starved, small swarm the
// same few peers come back from every source; they may have had no free slot).
const double RETRY_AFTER = 180, RETRY_AFTER_STARVED = 30;
const uint8_t EXT_METADATA = 1, EXT_PEX = 2; // our extension ids

const char *const DHT_BOOTSTRAP[][2] = {
	{ "router.bittorrent.com", "6881" }, { "dht.transmissionbt.com", "6881" },
	{ "router.utorrent.com", "6881" }, { "dht.libtorrent.org", "25401" },
};

const char *const VIDEO_EXTENSIONS[] = { ".mkv", ".mp4", ".m4v", ".avi", ".webm", ".mov", ".ts", ".m2ts", ".wmv", ".mpg", ".mpeg", ".flv" };

void put32(std::string &s, uint32_t v) {
	for (int i = 3; i >= 0; i--) s += (char)(v >> (i * 8));
}
uint32_t get32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

bool is_video(const std::string &path) {
	std::string lower = path;
	for (char &c : lower) c = (char)tolower((unsigned char)c);
	for (const char *ext : VIDEO_EXTENSIONS) {
		const size_t n = strlen(ext);
		if (lower.size() > n && lower.compare(lower.size() - n, n, ext) == 0) return true;
	}
	return false;
}

} // namespace

class Engine : public std::enable_shared_from_this<Engine> {
public:
	explicit Engine(const Magnet &m);

	bool start();
	bool wait_ready();
	int read(uint64_t pos, uint8_t *buf, int len);
	uint64_t size() const { return file_len_; }
	void abort();
	void stop();
	std::string error();
	std::string status();

private:
	struct Request {
		int piece, block;
		uint32_t gen;
		double at;
	};
	struct Peer {
		net::Endpoint ep;
		int fd = -1;
		enum State { CONNECTING, HANDSHAKE, ACTIVE, DEAD } state = CONNECTING;
		double since = 0, last_data = 0, last_send = 0, unchoked_at = 0;
		std::string in, out;
		bool ext = false, choked = true;
		int ut_metadata = 0, ut_pex = 0;
		int64_t metadata_size = 0;
		bool metadata_failed = false;
		std::vector<bool> have;
		std::vector<Request> inflight;
		uint64_t received = 0, window_bytes = 0;
		double rate = 0; // bytes/s, smoothed
		std::string client;
		bool has(int piece) const { return piece >= 0 && piece < (int)have.size() && have[piece]; }
	};
	struct Piece {
		uint32_t gen;
		std::vector<uint8_t> data;
		std::vector<uint8_t> state; // per block: FREE, ASKED, DONE
		std::vector<uint8_t> owners; // peers it's in flight from
		std::vector<double> asked_at;
		int done = 0;
	};
	enum { FREE = 0, ASKED = 1, DONE = 2 };
	struct FileEntry {
		std::string path;
		uint64_t offset, length;
	};

	static void *engine_entry(void *arg);
	static void *announce_entry(void *arg);
	static void *bootstrap_entry(void *arg);
	void run();
	void start_announce_round();
	void fail(const std::string &message);

	// engine thread
	void take_shared_input();
	void add_candidate(const net::Endpoint &e, double now);
	void connect_more(double now);
	void maintain(double now);
	void request_metadata(double now);
	void on_connected(Peer &p, double now);
	void on_readable(Peer &p, double now);
	void on_message(Peer &p, uint8_t id, const uint8_t *pl, size_t n, double now);
	void on_extended(Peer &p, const uint8_t *pl, size_t n);
	void on_block(Peer &p, uint32_t piece, uint32_t begin, const uint8_t *data, size_t len, double now);
	void piece_complete(int index);
	bool got_info(const std::string &info);
	void recompute_priority();
	void fill_requests(Peer &p, double now);
	void release(Peer &p, const Request &r);
	void release_all(Peer &p);
	void kill(Peer &p, const char *why);
	void send_msg(Peer &p, uint8_t id, const std::string &payload = std::string());
	void send_ext(Peer &p, uint8_t ext_id, const std::string &payload);
	int piece_size(int index) const;
	int first_piece() const { return (int)(file_off_ / piece_len_); }
	int last_piece() const { return (int)((file_off_ + file_len_ - 1) / piece_len_); }
	uint64_t readahead() const;
	void window(int &first, int &last) const; // pieces from the read position to the readahead end
	void track_play_rate(double now);
	void evict(int window_first, int window_last);
	void update_status(double now);

	Magnet magnet_;
	uint8_t peer_id_[20];
	std::string handshake_;

	threads::Mutex mutex_;
	threads::CondVar ready_cv_; // open(): info known, or failed
	threads::CondVar piece_cv_; // read(): a piece arrived, or abort
	std::atomic<bool> stopping_{ false }, aborted_{ false }, engine_running_{ false };
	// guarded
	bool ready_ = false;
	std::string error_, status_;
	uint64_t want_pos_ = 0;
	bool want_changed_ = true;
	std::map<int, std::vector<uint8_t>> cache_; // verified pieces
	uint64_t cache_bytes_ = 0;
	std::vector<net::Endpoint> incoming_peers_, incoming_nodes_;
	int announces_running_ = 0;

	// set before ready_, read-only afterwards
	std::atomic<uint64_t> file_len_{ 0 };
	uint64_t file_off_ = 0;
	int64_t piece_len_ = 0;
	int num_pieces_ = 0;
	std::string hashes_;
	std::vector<FileEntry> files_;

	// engine thread
	std::vector<std::unique_ptr<Peer>> peers_;
	std::vector<net::Endpoint> candidates_;
	std::map<net::Endpoint, double> tried_; // last connect attempt
	std::unique_ptr<Dht> dht_;
	int64_t meta_size_ = 0;
	std::vector<std::string> meta_pieces_;
	std::vector<bool> meta_have_;
	Peer *meta_peer_ = nullptr;
	double meta_asked_ = 0;
	std::map<int, Piece> active_;
	uint32_t next_gen_ = 1;
	std::vector<bool> have_; // mirrors cache_ keys
	std::vector<int> priority_;
	uint64_t local_want_ = 0;
	int window_first_ = -1, window_last_ = -1; // what priority_ was built for
	uint64_t play_anchor_pos_ = 0;              // for the player's read rate
	double play_anchor_at_ = 0, play_rate_ = 0;
	bool tail_fetched_ = false;
	bool starved_ = false;
	double started_ = 0, last_tick_ = 0, last_announce_ = 0, last_dht_ = 0, last_status_ = 0;
	uint64_t total_bytes_ = 0, rate_bytes_ = 0;
	double rate_ = 0;
	int pieces_bad_ = 0;
	int attempts_ = 0, connects_ = 0, handshakes_ = 0; // for the log
	double last_log_ = 0;
	bool info_logged_ = false;
};

Engine::Engine(const Magnet &m) : magnet_(m) {
	// Peer id: Azureus style, client "SP" (Stremio PS4), random tail.
	memcpy(peer_id_, "-SP1000-", 8);
	uint8_t seed[28];
	const double t = platform::now();
	memcpy(seed, &t, 8);
	memcpy(seed + 8, m.info_hash, 20);
	uint8_t h[20];
	sha1(seed, sizeof(seed), h);
	for (int i = 8; i < 20; i++) peer_id_[i] = (uint8_t)('0' + h[i] % 10);

	handshake_ += (char)19;
	handshake_ += "BitTorrent protocol";
	const char reserved[8] = { 0, 0, 0, 0, 0, 0x10 /* extension protocol */, 0, 0x01 /* DHT */ };
	handshake_.append(reserved, 8);
	handshake_.append((const char *)m.info_hash, 20);
	handshake_.append((const char *)peer_id_, 20);
}

// --- threads and the shared side --------------------------------------------------

struct EngineRef {
	std::shared_ptr<Engine> engine;
	std::string url;
};

bool Engine::start() {
	started_ = platform::now();
	engine_running_ = true;
	if (!threads::start(engine_entry, new EngineRef{ shared_from_this(), "" }, 512 * 1024, "stremio_torrent")) {
		engine_running_ = false;
		fail("Couldn't start the torrent engine");
		return false;
	}
	threads::start(bootstrap_entry, new EngineRef{ shared_from_this(), "" }, 1024 * 1024, "stremio_dht_dns");
	start_announce_round();
	return true;
}

void *Engine::engine_entry(void *arg) {
	std::unique_ptr<EngineRef> ref((EngineRef *)arg);
	ref->engine->run();
	ref->engine->engine_running_ = false;
	return nullptr;
}

void *Engine::bootstrap_entry(void *arg) {
	std::unique_ptr<EngineRef> ref((EngineRef *)arg);
	Engine &e = *ref->engine;
	for (const auto &b : DHT_BOOTSTRAP) {
		if (e.stopping_) break;
		uint32_t ip;
		if (net::resolve(b[0], ip)) {
			threads::Lock lock(e.mutex_);
			e.incoming_nodes_.push_back(net::Endpoint{ ip, (uint16_t)atoi(b[1]) });
		} else {
			platform::log("torrent: dns failed for %s", b[0]);
		}
	}
	return nullptr;
}

void Engine::start_announce_round() {
	std::vector<std::string> list = magnet_.trackers;
	for (const std::string &t : default_trackers()) {
		if (std::find(list.begin(), list.end(), t) == list.end()) list.push_back(t);
	}
	for (const std::string &url : list) {
		{
			threads::Lock lock(mutex_);
			announces_running_++;
		}
		// HTTP trackers go through libSceHttp, which needs a big stack.
		if (!threads::start(announce_entry, new EngineRef{ shared_from_this(), url }, 1024 * 1024, "stremio_announce")) {
			threads::Lock lock(mutex_);
			announces_running_--;
		}
	}
	last_announce_ = platform::now();
}

void *Engine::announce_entry(void *arg) {
	std::unique_ptr<EngineRef> ref((EngineRef *)arg);
	Engine &e = *ref->engine;
	if (!e.stopping_) {
		const uint64_t left = e.file_len_ ? e.file_len_.load() : (1ull << 30);
		AnnounceResult r = announce(ref->url, e.magnet_.info_hash, e.peer_id_, left);
		if (r.error.empty()) {
			platform::log("torrent: %s: %zu peers (%d seeders, %d leechers)", ref->url.c_str(), r.peers.size(), r.seeders, r.leechers);
		} else {
			platform::log("torrent: %s: %s", ref->url.c_str(), r.error.c_str());
		}
		threads::Lock lock(e.mutex_);
		e.incoming_peers_.insert(e.incoming_peers_.end(), r.peers.begin(), r.peers.end());
	}
	threads::Lock lock(e.mutex_);
	e.announces_running_--;
	return nullptr;
}

bool Engine::wait_ready() {
	threads::Lock lock(mutex_);
	while (!ready_ && error_.empty() && !aborted_) ready_cv_.wait(mutex_);
	return ready_;
}

int Engine::read(uint64_t pos, uint8_t *buf, int len) {
	const uint64_t size = file_len_;
	if (pos >= size || len <= 0) return size || piece_len_ ? 0 : -1;
	const uint64_t abs = file_off_ + pos;
	const int index = (int)(abs / piece_len_);
	const uint64_t in_piece = abs % piece_len_;
	threads::Lock lock(mutex_);
	if (want_pos_ != pos) {
		want_pos_ = pos;
		want_changed_ = true;
	}
	auto it = cache_.find(index);
	while (it == cache_.end()) {
		if (aborted_ || stopping_ || !error_.empty()) return -1;
		piece_cv_.wait(mutex_);
		it = cache_.find(index);
	}
	const uint64_t n = std::min<uint64_t>({ (uint64_t)len, it->second.size() - in_piece, size - pos });
	memcpy(buf, it->second.data() + in_piece, (size_t)n);
	return (int)n;
}

void Engine::abort() {
	aborted_ = true;
	threads::Lock lock(mutex_);
	ready_cv_.notify_all();
	piece_cv_.notify_all();
}

void Engine::stop() {
	stopping_ = true;
	abort();
}

void Engine::fail(const std::string &message) {
	platform::log("torrent: failed: %s", message.c_str());
	threads::Lock lock(mutex_);
	if (error_.empty()) error_ = message;
	ready_cv_.notify_all();
	piece_cv_.notify_all();
}

std::string Engine::error() {
	threads::Lock lock(mutex_);
	return error_;
}

std::string Engine::status() {
	threads::Lock lock(mutex_);
	return status_;
}

// --- engine loop ----------------------------------------------------------------------

void Engine::run() {
	platform::log("torrent: starting %s (file %d, %zu trackers in the link)", to_hex(magnet_.info_hash, 20).c_str(),
			magnet_.file_index, magnet_.trackers.size());
	dht_.reset(new Dht(magnet_.info_hash));
	dht_->search();
	last_dht_ = platform::now();
	std::vector<uint8_t> rbuf(64 * 1024);
	std::vector<pollfd> pfs;
	std::vector<Peer *> polled;
	std::vector<net::Endpoint> dht_peers;
	while (!stopping_) {
		const double now = platform::now();
		take_shared_input();
		if (now - last_tick_ >= 0.25) {
			last_tick_ = now;
			maintain(now);
			if (stopping_) break;
		}
		pfs.clear();
		polled.clear();
		if (dht_->fd() >= 0) pfs.push_back({ dht_->fd(), POLLIN, 0 });
		for (auto &up : peers_) {
			Peer &p = *up;
			if (p.state == Peer::DEAD) continue;
			short ev = POLLIN;
			if (p.state == Peer::CONNECTING || !p.out.empty()) ev |= POLLOUT;
			pfs.push_back({ p.fd, ev, 0 });
			polled.push_back(&p);
		}
		dht_->tick(now);
		if (poll(pfs.data(), pfs.size(), 50) <= 0) continue;
		const double t = platform::now();
		size_t k = 0;
		if (dht_->fd() >= 0) {
			if (pfs[0].revents & POLLIN) {
				dht_peers.clear();
				dht_->on_readable(dht_peers);
				for (const net::Endpoint &e : dht_peers) add_candidate(e, t);
			}
			k = 1;
		}
		for (size_t i = 0; i < polled.size(); i++, k++) {
			Peer &p = *polled[i];
			const short re = pfs[k].revents;
			if (!re || p.state == Peer::DEAD) continue;
			if (p.state == Peer::CONNECTING) {
				if ((re & (POLLERR | POLLHUP)) || !net::connect_ok(p.fd)) {
					kill(p, "connect failed");
					continue;
				}
				on_connected(p, t);
			}
			if (re & (POLLIN | POLLHUP | POLLERR)) {
				for (;;) {
					const long r = net::recv_some(p.fd, rbuf.data(), rbuf.size());
					if (r > 0) {
						p.in.append((const char *)rbuf.data(), (size_t)r);
						if (r < (long)rbuf.size()) break;
						continue;
					}
					if (r == 0 || !net::would_block()) kill(p, r == 0 ? "closed" : "recv error");
					break;
				}
				if (p.state != Peer::DEAD) on_readable(p, t);
			}
			if (p.state != Peer::DEAD && !p.out.empty()) {
				const long w = net::send_some(p.fd, p.out.data(), p.out.size());
				if (w > 0) {
					p.out.erase(0, (size_t)w);
					p.last_send = t;
				} else if (w < 0 && !net::would_block()) {
					kill(p, "send error");
				}
			}
		}
		// Drop dead peers (their requests were released in kill()).
		peers_.erase(std::remove_if(peers_.begin(), peers_.end(), [](const std::unique_ptr<Peer> &p) { return p->state == Peer::DEAD; }),
				peers_.end());
	}
	for (auto &p : peers_) kill(*p, "stopped");
	peers_.clear();
	dht_.reset();
	active_.clear();
	threads::Lock lock(mutex_);
	cache_.clear(); // the memory goes now, not when the last announce thread ends
	cache_bytes_ = 0;
	platform::log("torrent: stopped after %.0f s, %.1f MiB downloaded, %d bad pieces", platform::now() - started_,
			total_bytes_ / 1048576.0, pieces_bad_);
}

void Engine::take_shared_input() {
	std::vector<net::Endpoint> peers, nodes;
	bool want_changed = false;
	{
		threads::Lock lock(mutex_);
		peers.swap(incoming_peers_);
		nodes.swap(incoming_nodes_);
		if (want_changed_ && ready_) {
			want_changed = true;
			want_changed_ = false;
			local_want_ = want_pos_;
		}
	}
	const double now = platform::now();
	// Shuffled: trackers list peers in their own order (often oldest first).
	for (size_t i = peers.size(); i > 1; i--) std::swap(peers[i - 1], peers[(size_t)(now * 1e6 + i * 2654435761u) % i]);
	for (const net::Endpoint &e : peers) add_candidate(e, now);
	for (const net::Endpoint &e : nodes) dht_->add_node(e);
	if (want_changed) {
		track_play_rate(now);
		// The player reads in small steps; the plan only changes when the
		// window moves by a piece.
		int first, last;
		window(first, last);
		if (first != window_first_ || last != window_last_) {
			recompute_priority();
			for (auto &p : peers_) fill_requests(*p, now);
		}
	}
}

// The player's read rate, measured over 20 s of sequential reading (seeks and
// reads of the file's index restart the measurement).
void Engine::track_play_rate(double now) {
	if (local_want_ < play_anchor_pos_ || local_want_ > play_anchor_pos_ + (256ull << 20) || play_anchor_at_ == 0) {
		play_anchor_pos_ = local_want_;
		play_anchor_at_ = now;
		return;
	}
	if (now - play_anchor_at_ < 20) return;
	const double rate = (local_want_ - play_anchor_pos_) / (now - play_anchor_at_);
	play_rate_ = play_rate_ > 0 ? play_rate_ * 0.5 + rate * 0.5 : rate;
	play_anchor_pos_ = local_want_;
	play_anchor_at_ = now;
}

uint64_t Engine::readahead() const {
	const uint64_t want = (uint64_t)(play_rate_ * READAHEAD_SECONDS);
	return std::max<uint64_t>(std::min(std::max(want, READAHEAD_MIN), READAHEAD_MAX), 2 * (uint64_t)piece_len_);
}

void Engine::window(int &first, int &last) const {
	const uint64_t want = std::min<uint64_t>(local_want_, file_len_ - 1);
	first = (int)((file_off_ + want) / piece_len_);
	last = (int)((file_off_ + std::min<uint64_t>(file_len_, want + readahead()) - 1) / piece_len_);
}

void Engine::add_candidate(const net::Endpoint &e, double now) {
	if (!e.routable()) return;
	auto it = tried_.find(e);
	if (it != tried_.end() && now - it->second < (starved_ ? RETRY_AFTER_STARVED : RETRY_AFTER)) return; // tried recently (or connected)
	if (std::find(candidates_.begin(), candidates_.end(), e) != candidates_.end()) return;
	candidates_.push_back(e);
}

void Engine::connect_more(double now) {
	size_t active = 0, half_open = 0;
	for (auto &p : peers_) {
		if (p->state == Peer::ACTIVE) active++;
		if (p->state == Peer::CONNECTING || p->state == Peer::HANDSHAKE) half_open++;
	}
	while (active < MAX_PEERS && half_open < MAX_HALF_OPEN && !candidates_.empty()) {
		const net::Endpoint e = candidates_.front();
		candidates_.erase(candidates_.begin());
		tried_[e] = now;
		std::unique_ptr<Peer> p(new Peer());
		p->ep = e;
		p->fd = net::tcp_socket();
		if (p->fd < 0) {
			platform::log("torrent: socket() failed (error %d)", net::last_error());
			candidates_.insert(candidates_.begin(), e);
			return;
		}
		if (!net::connect_start(p->fd, e)) {
			net::close_socket(p->fd);
			continue;
		}
		p->since = now;
		attempts_++;
		peers_.push_back(std::move(p));
		half_open++;
	}
}

void Engine::maintain(double now) {
	bool is_ready;
	{
		threads::Lock lock(mutex_);
		is_ready = ready_;
	}
	if (!is_ready && now - started_ > OPEN_TIMEOUT) {
		fail(tried_.empty() ? "Couldn't find any peers for this torrent" : "Couldn't get the torrent's file list from its peers");
		stopping_ = true;
		return;
	}
	size_t active = 0, unchoked = 0;
	Peer *rotate = nullptr;
	for (auto &up : peers_) {
		Peer &p = *up;
		if (p.state == Peer::DEAD) continue;
		if (p.state == Peer::CONNECTING && now - p.since > CONNECT_TIMEOUT) {
			kill(p, "connect timeout");
			continue;
		}
		if (p.state == Peer::HANDSHAKE && now - p.since > HANDSHAKE_TIMEOUT) {
			kill(p, "handshake timeout");
			continue;
		}
		if (p.state != Peer::ACTIVE) continue;
		active++;
		// Requests nobody answered go back to the pool.
		for (size_t i = 0; i < p.inflight.size();) {
			if (now - p.inflight[i].at > REQUEST_TIMEOUT) {
				release(p, p.inflight[i]);
				p.inflight.erase(p.inflight.begin() + i);
			} else {
				i++;
			}
		}
		if (!p.choked) {
			unchoked++;
			if (!p.inflight.empty() && now - std::max(p.last_data, p.unchoked_at) > STALL_TIMEOUT) {
				kill(p, "stalled");
				continue;
			}
		} else if (now - p.since > 45 && (!rotate || p.since < rotate->since)) {
			rotate = &p; // choking us for a long time: make room for someone else
		}
		p.rate = p.rate * 0.75 + (p.window_bytes / 0.25) * 0.25;
		p.window_bytes = 0;
		if (now - p.last_send > 100) {
			p.out.append(4, '\0'); // keep-alive
			p.last_send = now;
		}
		fill_requests(p, now);
	}
	if (rotate && active + 5 >= MAX_PEERS && !candidates_.empty()) kill(*rotate, "choked us for 45 s");
	connect_more(now);
	if (!is_ready) request_metadata(now);
	const bool starved = candidates_.empty() && unchoked < 5 && (is_ready || now - started_ > 15);
	starved_ = starved;
	if (now - last_dht_ > (starved ? DHT_RESEARCH_STARVED : DHT_RESEARCH) && unchoked < 10) {
		dht_->search();
		last_dht_ = now;
	}
	if (now - last_announce_ > (starved ? REANNOUNCE_STARVED : REANNOUNCE) && unchoked < 10) {
		bool idle;
		{
			threads::Lock lock(mutex_);
			idle = announces_running_ == 0;
		}
		if (idle) start_announce_round();
	}
	if (now - last_status_ >= 1) update_status(now);
}

void Engine::update_status(double now) {
	const double dt = now - last_status_;
	last_status_ = now;
	rate_ = rate_ * 0.5 + (rate_bytes_ / std::max(dt, 0.001)) * 0.5;
	rate_bytes_ = 0;
	int active = 0, sending = 0;
	for (auto &p : peers_) {
		if (p->state != Peer::ACTIVE) continue;
		active++;
		if (!p->choked) sending++;
	}
	if (now - last_log_ >= 5) {
		last_log_ = now;
		platform::log("torrent: %d peers active (%d sending), %zu waiting; %d tried, %d connected, %d handshakes; dht %d replies, %d peers; "
					  "%.2f MB/s, %zu MiB cached, readahead %zu MiB",
				active, sending, candidates_.size(), attempts_, connects_, handshakes_, dht_ ? dht_->replies() : 0,
				dht_ ? dht_->peers_found() : 0, rate_ / 1e6, (size_t)(cache_bytes_ >> 20), (size_t)(readahead() >> 20));
	}
	char buf[160];
	threads::Lock lock(mutex_);
	if (!ready_) {
		if (active == 0) {
			snprintf(buf, sizeof(buf), "Finding peers \xC2\xB7 %zu found", tried_.size() + candidates_.size());
		} else {
			snprintf(buf, sizeof(buf), "Getting torrent info \xC2\xB7 %d peers", active);
		}
	} else {
		snprintf(buf, sizeof(buf), "%d peers (%d sending) \xC2\xB7 %.1f MB/s", active, sending, rate_ / 1e6);
	}
	status_ = buf;
}

// --- peer wire --------------------------------------------------------------------------

void Engine::send_msg(Peer &p, uint8_t id, const std::string &payload) {
	put32(p.out, (uint32_t)payload.size() + 1);
	p.out += (char)id;
	p.out += payload;
}

void Engine::send_ext(Peer &p, uint8_t ext_id, const std::string &payload) {
	std::string body(1, (char)ext_id);
	body += payload;
	send_msg(p, 20, body);
}

void Engine::on_connected(Peer &p, double now) {
	p.state = Peer::HANDSHAKE;
	p.since = now;
	connects_++;
	p.out = handshake_ + p.out;
}

void Engine::kill(Peer &p, const char *why) {
	if (p.state == Peer::DEAD) return;
	if (p.received) platform::log("torrent: peer %s (%s) dropped: %s, sent us %.1f MiB", p.ep.str().c_str(), p.client.c_str(), why, p.received / 1048576.0);
	release_all(p);
	net::close_socket(p.fd);
	p.fd = -1;
	p.state = Peer::DEAD;
	if (meta_peer_ == &p) meta_peer_ = nullptr;
}

void Engine::on_readable(Peer &p, double now) {
	if (p.state == Peer::HANDSHAKE) {
		if (p.in.size() < 68) return;
		const uint8_t *h = (const uint8_t *)p.in.data();
		if (h[0] != 19 || memcmp(h + 1, "BitTorrent protocol", 19) != 0 || memcmp(h + 28, magnet_.info_hash, 20) != 0) {
			kill(p, "bad handshake");
			return;
		}
		p.ext = (h[25] & 0x10) != 0;
		p.in.erase(0, 68);
		p.state = Peer::ACTIVE;
		p.since = now;
		handshakes_++;
		if (p.ext) {
			send_ext(p, 0, "d1:md11:ut_metadata" + bint(EXT_METADATA) + "6:ut_pex" + bint(EXT_PEX) + "e1:v" +
					bstr("StremioPS4/" APP_VERSION) + "e");
		}
		send_msg(p, 2); // interested, right away: peers unchoke sooner
	}
	size_t used = 0;
	while (p.state == Peer::ACTIVE && p.in.size() - used >= 4) {
		const uint8_t *m = (const uint8_t *)p.in.data() + used;
		const uint32_t len = get32(m);
		if (len > (1u << 20) + 64) {
			kill(p, "oversized message");
			return;
		}
		if (p.in.size() - used < 4 + (size_t)len) break;
		if (len > 0) on_message(p, m[4], m + 5, len - 1, now);
		used += 4 + len;
	}
	if (p.state != Peer::DEAD) p.in.erase(0, used);
}

void Engine::on_message(Peer &p, uint8_t id, const uint8_t *pl, size_t n, double now) {
	switch (id) {
		case 0: // choke
			p.choked = true;
			release_all(p);
			break;
		case 1: // unchoke
			if (p.choked) p.unchoked_at = now;
			p.choked = false;
			fill_requests(p, now);
			break;
		case 4: // have
			if (n >= 4) {
				const uint32_t index = get32(pl);
				if (index < (1u << 22)) {
					if (p.have.size() <= index) p.have.resize(index + 1);
					p.have[index] = true;
					fill_requests(p, now);
				}
			}
			break;
		case 5: // bitfield
			p.have.assign(n * 8, false);
			for (size_t i = 0; i < n * 8; i++) p.have[i] = (pl[i / 8] >> (7 - i % 8)) & 1;
			fill_requests(p, now);
			break;
		case 7: // piece
			if (n >= 8) on_block(p, get32(pl), get32(pl + 4), pl + 8, n - 8, now);
			break;
		case 9: // port: the peer's DHT node
			if (n >= 2) dht_->add_node(net::Endpoint{ p.ep.ip, (uint16_t)(pl[0] << 8 | pl[1]) });
			break;
		case 20:
			on_extended(p, pl, n);
			break;
		default: // interested, request, cancel...: we don't upload
			break;
	}
}

void Engine::on_extended(Peer &p, const uint8_t *pl, size_t n) {
	if (n < 1) return;
	const uint8_t id = pl[0];
	BNode d;
	const uint8_t *q = pl + 1, *e = pl + n;
	if (!bdecode(q, e, d) || d.type != BNode::DICT) return;
	if (id == 0) { // their extension handshake
		if (const BNode *m = d.get("m")) {
			p.ut_metadata = (int)m->get_int("ut_metadata", 0);
			p.ut_pex = (int)m->get_int("ut_pex", 0);
		}
		p.metadata_size = d.get_int("metadata_size", 0);
		p.client = d.get_string("v");
		return;
	}
	if (id == EXT_PEX) {
		const std::string added = d.get_string("added");
		const double now = platform::now();
		for (size_t i = 0; i + 6 <= added.size(); i += 6) add_candidate(net::compact(added.data() + i), now);
		return;
	}
	if (id != EXT_METADATA || meta_pieces_.empty()) return;
	const int type = (int)d.get_int("msg_type"), piece = (int)d.get_int("piece");
	if (type == 2) { // reject
		p.metadata_failed = true;
		if (meta_peer_ == &p) meta_peer_ = nullptr;
		return;
	}
	if (type != 1 || piece < 0 || piece >= (int)meta_pieces_.size()) return;
	meta_pieces_[piece].assign((const char *)q, e - q);
	meta_have_[piece] = true;
	if (!std::all_of(meta_have_.begin(), meta_have_.end(), [](bool b) { return b; })) return;
	std::string info;
	for (const std::string &s : meta_pieces_) info += s;
	if ((int64_t)info.size() > meta_size_) info.resize((size_t)meta_size_);
	uint8_t h[20];
	sha1(info.data(), info.size(), h);
	if (memcmp(h, magnet_.info_hash, 20) != 0) {
		platform::log("torrent: torrent info from %s doesn't match the info-hash", p.ep.str().c_str());
		p.metadata_failed = true;
		meta_peer_ = nullptr;
		std::fill(meta_have_.begin(), meta_have_.end(), false);
		return;
	}
	meta_pieces_.clear();
	meta_have_.clear();
	meta_peer_ = nullptr;
	if (!got_info(info)) {
		stopping_ = true;
		return;
	}
	const double now = platform::now();
	for (auto &o : peers_) fill_requests(*o, now);
}

void Engine::request_metadata(double now) {
	if (meta_peer_ && now - meta_asked_ > METADATA_TIMEOUT) {
		meta_peer_->metadata_failed = true;
		meta_peer_ = nullptr;
	}
	if (meta_peer_) return;
	for (auto &up : peers_) {
		Peer &p = *up;
		if (p.state != Peer::ACTIVE || p.ut_metadata <= 0 || p.metadata_failed) continue;
		if (p.metadata_size <= 0 || p.metadata_size > (16 << 20)) continue;
		if (meta_size_ != p.metadata_size) {
			meta_size_ = p.metadata_size;
			const int count = (int)((meta_size_ + BLOCK - 1) / BLOCK);
			meta_pieces_.assign(count, std::string());
			meta_have_.assign(count, false);
		}
		for (size_t k = 0; k < meta_have_.size(); k++) {
			if (!meta_have_[k]) send_ext(p, (uint8_t)p.ut_metadata, "d8:msg_typei0e5:piece" + bint((int64_t)k) + "e");
		}
		meta_peer_ = &p;
		meta_asked_ = now;
		return;
	}
}

bool Engine::got_info(const std::string &raw) {
	BNode info;
	if (!bdecode(raw, info) || info.type != BNode::DICT) {
		fail("The torrent's info is damaged");
		return false;
	}
	const int64_t piece_len = info.get_int("piece length");
	const std::string pieces = info.get_string("pieces");
	if (piece_len < BLOCK || piece_len > (64 << 20) || pieces.empty() || pieces.size() % 20) {
		fail("The torrent's info is damaged");
		return false;
	}
	std::vector<FileEntry> files;
	const std::string name = info.get_string("name");
	uint64_t total = 0;
	if (const BNode *list = info.get("files")) {
		for (const BNode &f : list->list) {
			FileEntry fe;
			if (const BNode *path = f.get("path")) {
				for (const BNode &part : path->list) fe.path += (fe.path.empty() ? "" : "/") + part.s;
			}
			fe.offset = total;
			fe.length = (uint64_t)std::max<int64_t>(0, f.get_int("length", 0));
			total += fe.length;
			files.push_back(fe);
		}
	} else {
		files.push_back(FileEntry{ name, 0, (uint64_t)std::max<int64_t>(0, info.get_int("length", 0)) });
		total = files[0].length;
	}
	const int num_pieces = (int)(pieces.size() / 20);
	if (total == 0 || (total + piece_len - 1) / piece_len != (uint64_t)num_pieces) {
		fail("The torrent's info is damaged");
		return false;
	}
	int pick = magnet_.file_index;
	if (pick < 0 || pick >= (int)files.size() || files[pick].length == 0) {
		pick = -1;
		for (int pass = 0; pass < 2 && pick < 0; pass++) { // the largest video, else the largest file
			for (int i = 0; i < (int)files.size(); i++) {
				if (pass == 0 && !is_video(files[i].path)) continue;
				if (pick < 0 || files[i].length > files[pick].length) pick = i;
			}
		}
	}
	const FileEntry &f = files[pick];
	platform::log("torrent: '%s': %zu files, %d pieces of %lld KiB; playing #%d '%s' (%.1f MiB)", name.c_str(), files.size(),
			num_pieces, (long long)(piece_len / 1024), pick, f.path.c_str(), f.length / 1048576.0);
	piece_len_ = piece_len;
	num_pieces_ = num_pieces;
	hashes_ = pieces;
	files_ = files;
	file_off_ = f.offset;
	file_len_ = f.length;
	have_.assign(num_pieces, false);
	{
		threads::Lock lock(mutex_);
		ready_ = true;
		want_changed_ = true;
		ready_cv_.notify_all();
	}
	take_shared_input(); // the reader's first position
	return true;
}

// --- pieces ---------------------------------------------------------------------------------

int Engine::piece_size(int index) const {
	const uint64_t total = files_.empty() ? 0 : files_.back().offset + files_.back().length;
	return (int)std::min<uint64_t>((uint64_t)piece_len_, total - (uint64_t)index * piece_len_);
}

// The pieces to fetch, most urgent first: the read position onwards, plus the
// file's last pieces early on (MP4 indexes and MKV cues often sit there).
void Engine::recompute_priority() {
	priority_.clear();
	int first, last;
	window(first, last);
	window_first_ = first;
	window_last_ = last;
	for (int i = first; i <= last; i++) {
		if (!have_[i]) priority_.push_back(i);
	}
	if (!tail_fetched_) {
		const int file_last = last_piece();
		const int tail_first = std::max(first_piece(), (int)((file_off_ + (file_len_ > (2u << 20) ? file_len_ - (2u << 20) : 0)) / piece_len_));
		std::vector<int> tail;
		for (int i = std::max(tail_first, last + 1); i <= file_last; i++) {
			if (!have_[i]) tail.push_back(i);
		}
		if (tail.empty()) tail_fetched_ = true;
		const size_t at = std::min<size_t>(priority_.size(), 2);
		priority_.insert(priority_.begin() + at, tail.begin(), tail.end());
	}
	// Half-assembled pieces nobody wants any more (after a seek) are dropped;
	// late blocks for them are ignored (generation check).
	for (auto it = active_.begin(); it != active_.end();) {
		if (std::find(priority_.begin(), priority_.end(), it->first) == priority_.end()) {
			it = active_.erase(it);
		} else {
			++it;
		}
	}
	evict(first, last);
}

void Engine::fill_requests(Peer &p, double now) {
	if (p.state != Peer::ACTIVE || p.choked || !have_.size() || priority_.empty()) return;
	// Enough requests to cover ~1.5 s at the peer's speed.
	const size_t depth = (size_t)std::max(6.0, std::min(64.0, p.rate * 1.5 / BLOCK));
	size_t unchoked = 0;
	for (auto &o : peers_) unchoked += o->state == Peer::ACTIVE && !o->choked;
	const size_t max_active = std::max<size_t>(2, std::min<size_t>(unchoked * 2 + 2, ACTIVE_BYTES / piece_len_));
	for (size_t k = 0; k < priority_.size() && p.inflight.size() < depth; k++) {
		const int index = priority_[k];
		if (!p.has(index)) continue;
		auto it = active_.find(index);
		if (it == active_.end()) {
			if (active_.size() >= max_active && k >= 2) continue;
			Piece pc;
			pc.gen = next_gen_++;
			const int size = piece_size(index);
			const int blocks = (size + BLOCK - 1) / BLOCK;
			pc.data.resize(size);
			pc.state.assign(blocks, FREE);
			pc.owners.assign(blocks, 0);
			pc.asked_at.assign(blocks, 0);
			it = active_.emplace(index, std::move(pc)).first;
		}
		Piece &pc = it->second;
		for (size_t b = 0; b < pc.state.size() && p.inflight.size() < depth; b++) {
			bool ask = pc.state[b] == FREE;
			if (!ask && k < 2 && pc.state[b] == ASKED && pc.owners[b] < 2 && now - pc.asked_at[b] > DUPLICATE_AFTER) {
				ask = std::none_of(p.inflight.begin(), p.inflight.end(), [&](const Request &r) { return r.piece == index && r.block == (int)b; });
			}
			if (!ask) continue;
			pc.state[b] = ASKED;
			pc.owners[b]++;
			pc.asked_at[b] = now;
			p.inflight.push_back(Request{ index, (int)b, pc.gen, now });
			const uint32_t begin = (uint32_t)b * BLOCK;
			std::string pl;
			put32(pl, (uint32_t)index);
			put32(pl, begin);
			put32(pl, std::min<uint32_t>(BLOCK, (uint32_t)pc.data.size() - begin));
			send_msg(p, 6, pl);
		}
	}
}

void Engine::release(Peer &, const Request &r) {
	auto it = active_.find(r.piece);
	if (it == active_.end() || it->second.gen != r.gen) return;
	Piece &pc = it->second;
	if (pc.owners[r.block] > 0) pc.owners[r.block]--;
	if (pc.owners[r.block] == 0 && pc.state[r.block] == ASKED) pc.state[r.block] = FREE;
}

void Engine::release_all(Peer &p) {
	for (const Request &r : p.inflight) release(p, r);
	p.inflight.clear();
}

void Engine::on_block(Peer &p, uint32_t index, uint32_t begin, const uint8_t *data, size_t len, double now) {
	p.received += len;
	p.window_bytes += len;
	p.last_data = now;
	total_bytes_ += len;
	rate_bytes_ += len;
	const int b = (int)(begin / BLOCK);
	for (size_t i = 0; i < p.inflight.size(); i++) {
		if (p.inflight[i].piece == (int)index && p.inflight[i].block == b) {
			release(p, p.inflight[i]);
			p.inflight.erase(p.inflight.begin() + i);
			break;
		}
	}
	auto it = active_.find((int)index);
	if (it != active_.end() && begin % BLOCK == 0 && b < (int)it->second.state.size()) {
		Piece &pc = it->second;
		const size_t expect = std::min<size_t>(BLOCK, pc.data.size() - begin);
		if (pc.state[b] != DONE && len == expect) {
			memcpy(pc.data.data() + begin, data, len);
			pc.state[b] = DONE;
			if (++pc.done == (int)pc.state.size()) piece_complete((int)index);
		}
	}
	fill_requests(p, now);
}

void Engine::piece_complete(int index) {
	auto it = active_.find(index);
	uint8_t h[20];
	sha1(it->second.data.data(), it->second.data.size(), h);
	if (memcmp(h, hashes_.data() + (size_t)index * 20, 20) != 0) {
		pieces_bad_++;
		platform::log("torrent: piece %d failed its hash check", index);
		active_.erase(it); // fetched again from scratch
		return;
	}
	std::vector<uint8_t> data;
	data.swap(it->second.data);
	active_.erase(it);
	have_[index] = true;
	priority_.erase(std::remove(priority_.begin(), priority_.end(), index), priority_.end());
	{
		threads::Lock lock(mutex_);
		cache_bytes_ += data.size();
		cache_[index] = std::move(data);
		piece_cv_.notify_all();
	}
	if (!info_logged_) {
		info_logged_ = true;
		platform::log("torrent: first piece after %.1f s", platform::now() - started_);
	}
	int first, last;
	window(first, last);
	evict(first, last);
}

// Keeps the cache under CACHE_BYTES: drops the pieces farthest from the
// window [first, last] (and KEEP_BEHIND before it) first.
void Engine::evict(int first, int last) {
	const int behind = (int)(KEEP_BEHIND / piece_len_);
	const int tail_first = (int)((file_off_ + (file_len_ > (2u << 20) ? file_len_ - (2u << 20) : 0)) / piece_len_);
	threads::Lock lock(mutex_);
	while (cache_bytes_ > CACHE_BYTES) {
		auto victim = cache_.end();
		long best = 0;
		for (auto it = cache_.begin(); it != cache_.end(); ++it) {
			const int i = it->first;
			if (i >= first - behind && i <= last) continue;
			if (i >= tail_first && first < tail_first - 64) continue; // the file's index, while playing the start
			const long distance = i < first ? (long)(first - i) * 2 : (long)(i - last); // behind goes first
			if (distance > best) {
				best = distance;
				victim = it;
			}
		}
		if (victim == cache_.end()) break;
		cache_bytes_ -= victim->second.size();
		have_[victim->first] = false;
		cache_.erase(victim);
	}
}

} // namespace torrent

// --- TorrentStream ---------------------------------------------------------------------------

TorrentStream::TorrentStream(const std::string &magnet) {
	torrent::Magnet m;
	if (torrent::parse_magnet(magnet, m)) engine_ = std::make_shared<torrent::Engine>(m);
}

TorrentStream::~TorrentStream() {
	if (engine_) engine_->stop();
}

bool TorrentStream::open() {
	return engine_ && engine_->start() && engine_->wait_ready();
}

int TorrentStream::read(uint64_t pos, uint8_t *buf, int len) {
	return engine_ ? engine_->read(pos, buf, len) : -1;
}

uint64_t TorrentStream::size() const {
	return engine_ ? engine_->size() : 0;
}

void TorrentStream::abort() {
	if (engine_) engine_->abort();
}

std::string TorrentStream::error() {
	return engine_ ? engine_->error() : "Not a valid magnet link";
}

std::string TorrentStream::status() {
	return engine_ ? engine_->status() : std::string();
}
