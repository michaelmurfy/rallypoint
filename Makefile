# Rallypoint server and development tasks. `make help` lists everything.
COMPOSE ?= docker compose
.DEFAULT_GOAL := help
-include .env

.PHONY: help config geo up down restart logs status key update test release linux-client

help: ## Show this help
	@awk 'BEGIN{FS=":.*## "} /^[a-z-]+:.*## /{printf "  \033[1mmake %-9s\033[0m %s\n",$$1,$$2}' $(MAKEFILE_LIST)

config: ## Create .env (limit to a country: make config COUNTRIES=NZ)
	@[ -f .env ] || { cp .env.example .env; echo "created .env"; }
	@if [ -n "$(COUNTRIES)" ]; then \
	  echo "$(COUNTRIES)" | grep -Eq '^[A-Za-z]{2}(,[A-Za-z]{2})*$$' || { echo "COUNTRIES must be codes like NZ or NZ,AU"; exit 1; }; \
	  sed -i.bak "s/^ALLOWED_COUNTRIES=.*/ALLOWED_COUNTRIES=$(COUNTRIES)/" .env && rm -f .env.bak; \
	  echo "country limit set to $(COUNTRIES)"; \
	fi

geo: ## Download the IP lists for ALLOWED_COUNTRIES (done automatically by up/update)
	@mkdir -p data/geo
	@for cc in $$(echo "$(ALLOWED_COUNTRIES)" | tr ',A-Z' ' a-z'); do \
	  echo "$$cc" | grep -Eq '^[a-z]{2}$$' || { echo "bad country code: $$cc"; exit 1; }; \
	  curl -fsSL --proto '=https' -o data/geo/$$cc.zone.tmp "https://www.ipdeny.com/ipblocks/data/aggregated/$$cc-aggregated.zone" \
	    && [ -s data/geo/$$cc.zone.tmp ] && mv data/geo/$$cc.zone.tmp data/geo/$$cc.zone \
	    && echo "updated $$cc ($$(wc -l < data/geo/$$cc.zone | tr -d ' ') ranges)" \
	    || { rm -f data/geo/$$cc.zone.tmp; echo "could not download the list for $$cc"; exit 1; }; \
	done

up: config geo ## Build and start the server
	$(COMPOSE) up -d --build
	@echo "Rallypoint server is running on UDP port $${PORT:-7777}"

down: ## Stop the server
	$(COMPOSE) down

restart: config geo ## Apply .env changes
	$(COMPOSE) up -d --build --force-recreate

logs: ## Follow the server log
	$(COMPOSE) logs -f --tail=100 relay

key: ## Show the server's identity fingerprint (players check it the first time they connect)
	$(COMPOSE) run --rm relay key

status: ## Show whether the server is running
	$(COMPOSE) ps

update: ## Get the latest version, refresh country lists, restart
	git pull --ff-only
	$(MAKE) geo
	$(COMPOSE) up -d --build --pull always

test: ## Run the test suites
	cargo test -p generals-core -p generals-proto -p generals-server
	sh tests/run.sh

linux-client: ## Build the Linux launcher (.deb, .rpm, .AppImage); no engine on Linux yet
	@[ "$$(uname -s)" = Linux ] || { echo "linux-client builds on Linux only"; exit 1; }
	@pkg-config --exists webkit2gtk-4.1 gtk+-3.0 || { echo "missing build libraries; on Debian/Ubuntu: sudo apt install build-essential curl libwebkit2gtk-4.1-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev"; exit 1; }
	@cargo tauri --version >/dev/null 2>&1 || cargo install tauri-cli --version "^2" --locked
	cd launcher/src-tauri && cargo tauri build
	@echo "built: target/release/bundle/"

release: ## Tag and publish a release: make release VERSION=0.2.0-beta.1
	@[ -n "$(VERSION)" ] || { echo "usage: make release VERSION=0.1.0-alpha.1"; exit 1; }
	@[ -z "$$(git status --porcelain --untracked-files=no | grep -v '^ m engine')" ] || { echo "commit your changes first"; exit 1; }
	git tag -a v$(VERSION) -m "Rallypoint $(VERSION)"
	git push origin v$(VERSION)
