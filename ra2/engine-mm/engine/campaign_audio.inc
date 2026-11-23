// W2-CA-12: mission speech / sound / music through the existing
// presentation audio (pres_audio.inc). Textually included INSIDE class
// GameScene; every member is inert unless campaign_ is set.
// [RE: map-triggers.md §5 -- actions 19 PlaySoundEffect, 20 PlayMusicTheme,
//  21 PlaySpeech, 99 PlaySoundAtWaypoint are executed by TActionClass::Execute;
//  the RA2 mission dialogue (Mis_*) is defined in local.mix/eva.ini.]
private:
    bool camp_audio_ready_ = false;

    // After init_presentation: RA2 eva.ini fills the Mis_* lines evamd.ini
    // lacks, and the map's [Basic] Theme= starts the score ("No theme" is
    // silence) instead of the skirmish playlist head.
    void campaign_audio_init() {
        if (!campaign_) return;
        camp_audio_ready_ = true;
        pres_.sound().add_eva_fallback(camp::load_ra2_local_ini(ra2_, "eva.ini"));
        const std::string theme = campaign_->map_theme();
        if (theme.empty()) return;
        if (camp::up(theme) == "NO THEME") pres_.sound().music_stop();
        else pres_.sound().jukebox_play(theme);
    }

    // Theme by [Themes] index (action 20 Value) from RA2 theme.ini.
    std::string campaign_theme_by_index(int ix) const {
        try {
            const Ini th = camp::load_ra2_local_ini(ra2_, "theme.ini");
            int k = 0;
            for (const auto& kv : th.section("Themes")) {
                const std::string sec = pres_clean(kv.second);
                if (sec.empty()) continue;
                if (k++ == ix) return sec;
            }
        } catch (const std::exception&) {}
        return std::string();
    }

    // Once per scene update, after campaign_->update(dt).
    void campaign_audio_pump() {
        if (!campaign_ || !camp_audio_ready_) return;
        for (const CampaignRuntime::AudioReq& r : campaign_->take_audio()) {
            switch (r.action) {
            case 21:   // PlaySpeech
                if (!r.name.empty()) pres_.sound().eva(r.name);
                break;
            case 19:   // PlaySoundEffect (global)
                if (!r.name.empty()) pres_.sound().play(r.name, 0.0, "trigger sound");
                break;
            case 99: {   // PlaySoundAtWaypoint: panned/attenuated from the view (play_at)
                int x = 0, y = 0;
                if (r.name.empty()) break;
                if (campaign_->waypoint_xy(r.waypoint, x, y)) pres_.sound().play_at(r.name, x, y, "trigger sound at waypoint");
                else pres_.sound().play(r.name, 0.0, "trigger sound");
                break;
            }
            case 10: case 100: case 116:   // W5-E: PlayMovie / PlayIngameMovie / PlayMovieAndPause
                // [RE: map-triggers.md -- the movie name is the action's text
                // parameter]; skipped when the file does not exist.
                if (!r.name.empty()) {
                    pres_log(pres_.frame(), "movie-request " + r.name + " (action " + std::to_string(r.action) + ")");
                    g_movie_mixer = audio_.enabled ? audio_.mixer.get() : nullptr;   // W5-E2
                    ingame_movie_.start(r.name);
                }
                break;
            case 20: {   // PlayMusicTheme
                const std::string t = r.name.empty() ? campaign_theme_by_index(r.value) : r.name;
                if (!t.empty()) pres_.sound().jukebox_play(t);
                break;
            }
            default:
                break;
            }
        }
    }
public:
