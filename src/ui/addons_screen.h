// Installed add-ons, public catalogs and confirmed account actions.
#pragma once

#include "../core/models.h"
#include "screen.h"
#include "tab_bar.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AddonsScreen : public Screen {
public:
	AddonsScreen(Fonts &fonts, Navigator &nav);
	void update(double dt, uint32_t pressed) override;
	void draw(Renderer &r) override;

private:
	enum Filter : int {
		FILTER_INSTALLED = 0,
		FILTER_OFFICIAL,
		FILTER_COMMUNITY,
		FILTER_COUNT
	};

	struct CatalogState {
		std::vector<Addon> items;
		bool loading = false;
		bool loaded = false;
		bool failed = false;
		int revision = 0;
	};

	const std::vector<Addon> &current_addons() const;
	void set_filter(int filter);
	void load_catalog(bool force = false);
	void draw_filters(Renderer &r);
	void update_search(uint32_t pressed);
	void move_search_selection(uint32_t pressed);
	void activate_search_key();
	void draw_search(Renderer &r);

	bool searching_ = false;
	int search_key_ = 0;
	std::string addon_query_;
	mutable std::vector<Addon> filtered_addons_;
	mutable std::string filtered_query_;
	mutable int filtered_revision_ = -1;
	bool is_installed(const Addon &addon) const;
	void activate_addon();
	void execute_change();
	void draw_action(Renderer &r);
	void draw_confirmation(Renderer &r);
	void open_phone_config();
	void update_config(uint32_t pressed);
	void draw_config_options(Renderer &r);
	void draw_phone_config(Renderer &r);

	bool config_options_open_ = false;
	bool phone_config_open_ = false;
	bool phone_sync_pending_ = false;
	int config_option_ = 0;
	std::vector<uint8_t> phone_qr_;

	std::shared_ptr<int> alive_ = std::make_shared<int>(0);
	ui::Toast toast_;
	bool operation_pending_ = false;
	bool confirming_ = false;
	bool confirm_ok_ = false;
	bool confirm_install_ = false;
	int confirm_auth_rev_ = -1;
	Addon confirmed_addon_;

	int filter_ = FILTER_INSTALLED;
	bool on_filters_ = false;
	std::shared_ptr<CatalogState> catalogs_[FILTER_COUNT - FILTER_OFFICIAL] = {
		std::make_shared<CatalogState>(),
		std::make_shared<CatalogState>()
	};

	Fonts &fonts_;
	Navigator &navigator_;
	tabs::TabBar bar_{ tabs::ADDONS };
	int selected_ = 0;
	float scroll_ = 0;
	bool on_bar_ = false;
	std::vector<float> focus_;
};
