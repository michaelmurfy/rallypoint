// movie_player.inc -- W5-E (R-034 / R-200 / U-5): campaign movies through the
// existing Bink decoder (src/bink.cpp). Included by main.cpp before
// campaign_scenes.inc (CampaignLoadScene / CampaignResultScene) and GameScene.
//
// [RE: campaign.md §8] the map's [Basic] Intro= / Brief= / Win= / Lose= name
// the movies (A01_f00e, ...), found in MOVIES01.MIX / MOVIES02.MIX /
// MOVMD03.MIX. On this install those archives are 4-byte stubs, so the player
// looks, in order, for:
//   0. a loose <name>.bik in $RA2_MOVIE_DIR (a user movie folder),
//   1. a loose <name>.bik in the data dir or in <data dir>/Movies/,
//   2. a loose <name>.bik in the writable pref dir's Movies/ folder,
//   3. <name>.bik inside any user-supplied MIX in the data dir whose file name
//      starts with "MOVIES" / "MOVMD" (the stock names included, when real).
// A missing movie (or an unreadable / non-Bink file) is skipped silently and
// logged ("[pres] movie NAME unavailable"); the Bink audio track is not
// decoded (R-134), so movies play silent.

std::string pref_dir_path();

struct MovieFile {
    std::unique_ptr<ra2assets::BinkFile> file;
    std::unique_ptr<ra2assets::BinkVideoDecoder> dec;
    std::string name, source;
};

inline std::string movie_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Locate + parse `name` (without extension); false when unavailable.
inline bool find_movie(const std::string& name_in, MovieFile& out) {
    if (name_in.empty()) return false;
    std::string name = movie_lower(name_in);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".bik") name.resize(name.size() - 4);
    const std::string file = name + ".bik";
    auto accept = [&](std::vector<uint8_t> bytes, const std::string& src) {
        if (bytes.size() < 44 || bytes[0] != 'B' || bytes[1] != 'I' || bytes[2] != 'K') return false;
        try {
            out.file = std::make_unique<ra2assets::BinkFile>(ra2assets::BinkFile::parse(std::move(bytes)));
            if (out.file->num_frames <= 0 || out.file->width <= 0) return false;
            out.dec = std::make_unique<ra2assets::BinkVideoDecoder>(*out.file);
            out.name = name;
            out.source = src;
            return true;
        } catch (const std::exception&) {
            out.file.reset();
            out.dec.reset();
            return false;
        }
    };
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string dir = env_install_dir();
    std::vector<std::string> dirs;
    if (const char* md = std::getenv("RA2_MOVIE_DIR")) dirs.push_back(std::string(md) + "/");   // user movie folder
    dirs.insert(dirs.end(), {dir, dir + "Movies/", dir + "movies/"});
    const std::string pref = pref_dir_path();
    if (!pref.empty()) dirs.push_back(pref + "Movies/");
    for (const std::string& d : dirs) {
        if (!fs::is_directory(d, ec)) continue;
        for (const auto& e : fs::directory_iterator(d, ec)) {
            if (!e.is_regular_file(ec) || movie_lower(e.path().filename().string()) != file) continue;
            try {
                if (accept(ra2assets::read_file(e.path().string()), e.path().string())) return true;
            } catch (const std::exception&) {}
        }
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string fn = movie_lower(e.path().filename().string());
        if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".mix") continue;
        if (fn.rfind("movies", 0) != 0 && fn.rfind("movmd", 0) != 0) continue;
        if (e.file_size(ec) < 64) continue;   // the 4-byte install stubs
        try {
            ra2assets::MixFile m = ra2assets::MixFile::open_path(e.path().string());
            if (const ra2assets::MixEntry* me = m.find(file))
                if (accept(m.read(*me), e.path().string() + ":" + file)) return true;
        } catch (const std::exception&) {}
    }
    return false;
}

// W5-E2 (R-134): the movie's first Bink Audio track, decoded whole when the
// movie starts and streamed to the mixer's music slot as 22050 Hz stereo
// (the theme is paused for the movie and restored after).
class PcmMusic : public ra2engine::MusicSource {
public:
    explicit PcmMusic(std::vector<int16_t> lr) : lr_(std::move(lr)) {}
    std::size_t read(int16_t* out, std::size_t frames) override {
        std::lock_guard<std::mutex> lk(mu_);
        const std::size_t avail = (lr_.size() - pos_) / 2;
        const std::size_t n = std::min(frames, avail);
        std::copy(lr_.begin() + static_cast<std::ptrdiff_t>(pos_), lr_.begin() + static_cast<std::ptrdiff_t>(pos_ + 2 * n), out);
        pos_ += 2 * n;
        return n;
    }

private:
    std::vector<int16_t> lr_;
    std::size_t pos_ = 0;
    std::mutex mu_;
};
// The mixer movies play their sound through (set by run_menu / GameScene).
ra2engine::Mixer* g_movie_mixer = nullptr;
AudioSystem* g_movie_audio = nullptr;   // pumped by MovieScene (the shell's audio)
inline std::vector<int16_t> movie_audio_22k_stereo(const ra2assets::BinkFile& bf) {
    std::vector<int16_t> pcm;
    if (bf.audio.empty()) return pcm;
    const ra2assets::BinkAudioTrack& t = bf.audio[0];
    if (t.sample_rate <= 0) return pcm;
    ra2assets::BinkAudioDecoder dec(t.sample_rate, t.channels(), t.dct(), bf.data.size() > 3 && bf.data[3] == 'b');
    for (int f = 0; f < bf.num_frames; ++f) {
        const auto pk = bf.audio_packet(f, 0);
        if (pk.first) dec.decode_packet(pk.first, pk.second, pcm);
    }
    const int ch = dec.channels();
    const std::size_t frames = pcm.size() / static_cast<std::size_t>(ch);
    const double step = static_cast<double>(t.sample_rate) / 22050.0;
    const std::size_t n = static_cast<std::size_t>(frames / step);
    std::vector<int16_t> lr(2 * n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t k = std::min(frames - 1, static_cast<std::size_t>(i * step));
        lr[2 * i] = pcm[k * ch];
        lr[2 * i + 1] = pcm[k * ch + (ch > 1 ? 1 : 0)];
    }
    return lr;
}

// A full-screen movie, letterboxed into the window, at the file's frame rate;
// any key / click skips it.
class MoviePlayer {
public:
    bool start(const std::string& name) {
        async_.reset();   // joins the worker before its decoder goes
        mv_ = MovieFile{};
        frame_ = -1;
        acc_ = 0.0;
        tex_frame_ = -1;
        if (!find_movie(name, mv_)) {
            std::cerr << "[pres] movie " << name << " unavailable (skipped)\n";
            pres_log(0, "movie " + name + " unavailable");
            return false;
        }
        const double num = mv_.file->fps_num > 0 ? mv_.file->fps_num : 15.0;
        const double den = mv_.file->fps_den > 0 ? mv_.file->fps_den : 1.0;
        dt_ = den / num;
        try {
            mv_.dec->decode_frame(0);
        } catch (const std::exception& ex) {   // a damaged file: skip it, never crash
            pres_log(0, "movie " + name + " undecodable (" + ex.what() + ")");
            mv_ = MovieFile{};
            return false;
        }
        frame_ = 0;
        // W5-E2 (R-173): the rest decodes on a worker; --play waits for each
        // frame so captures stay deterministic.
        async_ = std::make_unique<ra2engine::BinkAsync>(mv_.dec.get(), mv_.file->num_frames);
        async_->set_blocking(g_pres_play_log);
        async_->want(0);
        pres_log(0, "movie " + name + " playing from " + mv_.source);
        {   // W5-E2: the audio track
            std::vector<int16_t> lr = movie_audio_22k_stereo(*mv_.file);
            audio_frames_ = lr.size() / 2;
            if (!lr.empty()) {
                pres_log(0, "movie " + name + " audio frames=" + std::to_string(audio_frames_));
                if (g_movie_mixer) {
                    saved_music_ = g_movie_mixer->music();
                    g_movie_mixer->set_music(std::make_shared<PcmMusic>(std::move(lr)));
                    music_set_ = true;
                }
            }
        }
        return true;
    }
    std::size_t audio_frames() const { return audio_frames_; }
    void stop_audio() {
        if (music_set_ && g_movie_mixer) g_movie_mixer->set_music(saved_music_);
        music_set_ = false;
        saved_music_.reset();
    }
    bool active() const { return mv_.dec && frame_ >= 0 && frame_ < mv_.file->num_frames; }
    void skip() { frame_ = mv_.file ? mv_.file->num_frames : -1; stop_audio(); }
    void update(double dt) {
        if (!active()) { stop_audio(); return; }
        if (async_ && async_->failed()) { frame_ = mv_.file->num_frames; stop_audio(); return; }   // damaged frame
        acc_ += dt;
        bool moved = false;
        while (acc_ >= dt_ && active()) {
            acc_ -= dt_;
            if (frame_ + 1 >= mv_.file->num_frames) { frame_ = mv_.file->num_frames; break; }
            ++frame_;
            moved = true;
        }
        if (moved && active() && async_) async_->want(frame_);
        if (async_ && async_->failed()) frame_ = mv_.file->num_frames;
    }
    void render(SDL_Renderer* ren) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (!active()) return;
        const int w = mv_.file->width, h = mv_.file->height;
        if (!tex_) tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!tex_) return;
        if (async_) {
            const int got = async_->take(tex_frame_, px_);
            if (got >= 0 && got != tex_frame_) {
                if (static_cast<int>(px_.size()) == w * h * 4) SDL_UpdateTexture(tex_, nullptr, px_.data(), w * 4);
                tex_frame_ = got;
            }
        }
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_RenderGetLogicalSize(ren, &ow, &oh);
        if (ow <= 0 || oh <= 0) { ow = 1280; oh = 720; }
        const double sc = std::min(static_cast<double>(ow) / w, static_cast<double>(oh) / h);
        const SDL_Rect d{static_cast<int>((ow - w * sc) / 2), static_cast<int>((oh - h * sc) / 2),
                         static_cast<int>(w * sc), static_cast<int>(h * sc)};
        SDL_RenderCopy(ren, tex_, nullptr, &d);
    }
    ~MoviePlayer() {
        stop_audio();
        async_.reset();
        if (tex_) SDL_DestroyTexture(tex_);
    }
    MoviePlayer() = default;
    MoviePlayer(const MoviePlayer&) = delete;
    MoviePlayer& operator=(const MoviePlayer&) = delete;

private:
    MovieFile mv_;
    std::unique_ptr<ra2engine::BinkAsync> async_;   // after mv_: destroyed first
    std::vector<uint8_t> px_;
    int frame_ = -1, tex_frame_ = -1;
    double acc_ = 0.0, dt_ = 1.0 / 15.0;
    SDL_Texture* tex_ = nullptr;
    std::size_t audio_frames_ = 0;
    std::shared_ptr<ra2engine::MusicSource> saved_music_;
    bool music_set_ = false;
};

// A scene that plays a list of movies (missing ones skipped) and then hands
// over to `then()`. With nothing playable it hands over on the first frame.
class MovieScene : public Scene {
public:
    MovieScene(std::vector<std::string> names, std::function<Scene*()> then)
        : names_(std::move(names)), then_(std::move(then)) {
        advance();
    }
    void handle(const ra2engine::InputEvent& ev) override {
        if (ev.kind == ra2engine::InputEvent::Key ||
            (ev.kind == ra2engine::InputEvent::PointerDown && ev.button == ra2engine::InputEvent::Left))
            player_.skip();   // [the original: Esc / a click skips the movie]
    }
    void update(double dt) override {
        player_.update(dt);
        if (g_movie_audio && g_movie_audio->enabled && g_movie_audio->mixer)   // W5-E2: feed the sink
            g_movie_audio->out.pump(*g_movie_audio->mixer);
        if (!player_.active()) advance();
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache&) override {
        player_.render(static_cast<SDL_Renderer*>(plat.renderer()));
    }
    Scene* next() override {
        if (!done_) return nullptr;
        done_ = false;
        return then_ ? then_() : nullptr;
    }
    void play_probe(ra2play::Probe& p) override {
        p.scene = "movie";
        p.set("movies_played", played_);
        p.targets["skip"] = ra2play::Rect{560, 300, 160, 120};
    }

private:
    void advance() {
        if (g_movie_audio && g_movie_audio->enabled) g_movie_mixer = g_movie_audio->mixer.get();
        while (ix_ < names_.size()) {
            if (player_.start(names_[ix_++])) { ++played_; return; }
        }
        done_ = true;
    }
    std::vector<std::string> names_;
    std::function<Scene*()> then_;
    MoviePlayer player_;
    std::size_t ix_ = 0;
    int played_ = 0;
    bool done_ = false;
};

// True when at least one of `names` can be played (so the flow only detours
// through a MovieScene when there is something to show).
inline bool any_movie(const std::vector<std::string>& names) {
    for (const std::string& n : names) {
        MovieFile m;
        if (find_movie(n, m)) return true;
        if (!n.empty()) pres_log(0, "movie " + n + " unavailable (skipped)");
    }
    return false;
}
